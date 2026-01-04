#include "worker.h"
#include "connection.h"
#include "http_request.h"
#include "http_response.h"

#include <algorithm>
#include <cstring>
#include <fcntl.h>
#include <fstream>
#include <iostream>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <sstream>
#include <sys/epoll.h>
#include <sys/sendfile.h>
#include <sys/stat.h>
#include <unistd.h>
#include <vector>
#include <sys/uio.h>

Worker::Worker(int id, const ServerConfig &config, const ResponseCache &cache)
    : id_(id), config_(config), cache_(cache) {}

Worker::~Worker() {
    stop();
    join();
    if (epoll_fd_ >= 0)
        close(epoll_fd_);
    if (listen_fd_ >= 0)
        close(listen_fd_);
}

void Worker::start() {
    epoll_fd_ = epoll_create1(EPOLL_CLOEXEC);
    if (epoll_fd_ < 0)
        return;

    listen_fd_ = createListenSocket();
    if (listen_fd_ < 0)
        return;

    // listen_fd 使用 nullptr 作为标记
    addToEpoll(listen_fd_, EPOLLIN | EPOLLET, nullptr);

    running_ = true;
    thread_ = std::thread(&Worker::run, this);
}

void Worker::stop() { running_ = false; }

void Worker::join() {
    if (thread_.joinable())
        thread_.join();
}

int Worker::createListenSocket() {
    int sockfd = socket(AF_INET, SOCK_STREAM | SOCK_NONBLOCK | SOCK_CLOEXEC, 0);
    if (sockfd < 0)
        return -1;

    int opt = 1;
    setsockopt(sockfd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
    setsockopt(sockfd, SOL_SOCKET, SO_REUSEPORT, &opt, sizeof(opt));

    struct sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port = htons(config_.port);

    if (bind(sockfd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        close(sockfd);
        return -1;
    }
    if (listen(sockfd, SOMAXCONN) < 0) {
        close(sockfd);
        return -1;
    }
    return sockfd;
}

// 主事件循环

void Worker::run() {
    std::vector<struct epoll_event> events(config_.max_events);
    auto last_idle_check = std::chrono::steady_clock::now();

    while (running_) {
        // 获取当前时间
        auto now = std::chrono::steady_clock::now();

        int n = epoll_wait(epoll_fd_, events.data(), config_.max_events, 100);

        if (n < 0) {
            if (errno == EINTR)
                continue;
            break;
        }

        for (int i = 0; i < n; ++i) {
            uint32_t ev = events[i].events;

            // 通过 data.ptr 判断：nullptr = listen_fd，否则是 Connection*
            if (events[i].data.ptr == nullptr) {
                handleAccept();
            } else {
                Connection *conn =
                    static_cast<Connection *>(events[i].data.ptr);
                if (ev & (EPOLLERR | EPOLLHUP)) {
                    closeConnection(conn);
                }
                if ((ev & EPOLLIN) && conn->state() != ConnectionState::CLOSING) {
                    handleRead(conn, now);
                }
                if (ev & EPOLLOUT) {
                    handleWrite(conn, now);
                }
            }
        }

        // 每5秒查看空闲连接
        if (std::chrono::duration_cast<std::chrono::seconds>(now -
                                                             last_idle_check)
                .count() >= 5) {
            checkIdleConnections(now);
            last_idle_check = now;
        }
    }
    // 清理所有活跃连接
    for (Connection *conn : active_conns_) {
        if (conn) {
            close(conn->fd());
            conn_pool_.release(conn);
        }
    }
    active_conns_.clear();
}

void Worker::handleAccept() {
    while (true) {
        int client_fd =
            accept4(listen_fd_, nullptr, nullptr, SOCK_NONBLOCK | SOCK_CLOEXEC);
        if (client_fd < 0) {
            if (errno == EAGAIN || errno == EWOULDBLOCK)
                break;
            if (errno == EINTR || errno == ECONNABORTED || errno == EPROTO)
                continue;
            std::cerr << "handleAccept error: " << strerror(errno) << std::endl;
            break;
        }

        int flag = 1;
        setsockopt(client_fd, IPPROTO_TCP, TCP_NODELAY, &flag, sizeof(flag));

        // 从对象池获取连接
        Connection *conn = conn_pool_.acquire(client_fd);

        // 先尝试添加到 epoll
        if (!addToEpoll(client_fd, EPOLLIN | EPOLLET, conn)) {
            close(client_fd);
            conn_pool_.release(conn);
            continue;
        }

        // 添加到活跃列表，并记录索引
        conn->setPoolIndex(active_conns_.size());
        active_conns_.push_back(conn);
    }
}

void Worker::handleRead(Connection *conn,
                        const std::chrono::steady_clock::time_point &now) {
    if (!conn) return;

    conn->updateActivity(now);
    int fd = conn->fd();

    char stack_buffer[65536];
    while (true) {
        ssize_t bytes = read(fd, stack_buffer, sizeof(stack_buffer));
        if (bytes < 0) {
            if (errno == EAGAIN || errno == EWOULDBLOCK)
                break;
            closeConnection(conn);
            return;
        } else if (bytes == 0) {
            closeConnection(conn);
            return;
        }
        // conn->appendRead(buffer, bytes);

        // 快速通道
        char* current_ptr = stack_buffer;
        size_t remaining = bytes;

        if(conn->readBuffer().empty()){
            while(remaining > 0){
                size_t consumed = processRequest(*conn, std::string_view(current_ptr, remaining));

                if(consumed > 0){
                    current_ptr += consumed;
                    remaining -= consumed;

                    if(conn->state() == ConnectionState::WRITING){
                        handleWrite(conn, now);
                        // 如果在写，剩下的数据存入缓存等待下一次处理
                        if(conn->state() == ConnectionState::WRITING){
                            if(remaining > 0){
                                conn->appendRead(current_ptr, remaining);
                            }
                            goto loop_end;
                        }
                    }
                } else {
                    // 解析失败，跳出循环
                    break;
                }
            }
        }

        // 如果没走快速通道，或者没走完，剩下的数据存入缓存
        if(remaining > 0){
            conn->appendRead(current_ptr, remaining);
        }

        // 慢速通道
        while (conn->readBuffer().size() > 0) {
            if(conn->state() == ConnectionState::WRITING) break;

            size_t consumed = processRequest(*conn);
            if (consumed == 0) break;
            conn->consumeReadBuffer(consumed);

            // 如果设置了WRITING，执行写入
            if (conn->state() == ConnectionState::WRITING) {
                handleWrite(conn, now);
                // 如果写入未完成，等待EPOLLOUT
                if(conn->state() == ConnectionState::WRITING) break;
            }
        }
    }

loop_end:
    return;

}

size_t Worker::processRequest(Connection &conn, std::string_view data) {
    
    std::string_view view_to_parse;
    if(!data.empty()){
        view_to_parse = data;
    } else {
        view_to_parse = conn.readBuffer();
    }

    HttpRequest request;

    if (!request.parse(view_to_parse)) {

        // 解析失败两种可能：1.数据不够， 2.格式错误
        if (conn.readBuffer().size() > 10 * 1024 * 1024) {
            HttpResponse response;
            response.setStatusCode(400);
            response.setBody(
                "<html><body><h1>400 Bad Request</h1></body></html>");
            response.setContentType("text/html");
            response.setKeepAlive(false);

            conn.setWriteBuffer(response.build());
            conn.setState(ConnectionState::WRITING);
            conn.setKeepAlive(false);
            // modifyEpoll(conn.fd(), EPOLLOUT | EPOLLET, &conn);
            // handleWrite(&conn, std::chrono::steady_clock::now());
            return 0;
        }

        return 0;
    }
    ++request_count_;

    // 优先查缓存（避免 stat 和文件读取）
    if (request.method() == HttpRequest::GET ||
        request.method() == HttpRequest::HEAD) {
        const CacheEntry *cached = cache_.find(request.path());
        if (cached) {
            // 缓存命中：直接使用预构建的响应
            conn.setCachedResponse(&cached->response);
            conn.setKeepAlive(true); // 缓存响应默认 keep-alive
            conn.setState(ConnectionState::WRITING);
            // modifyEpoll(conn.fd(), EPOLLOUT | EPOLLET, &conn);
            // handleWrite(&conn, std::chrono::steady_clock::now());
            return request.parseLength();
        }
    }

    // 缓存未命中，走原来的逻辑
    HttpResponse response;
    if (request.method() == HttpRequest::GET ||
        request.method() == HttpRequest::HEAD) {
        serveStaticFile(request.path(), response);
    } else {
        response.setStatusCode(405);
        response.setBody(
            "<html><body><h1>405 Method Not Allowed</h1></body></html>");
        response.setContentType("text/html");
    }

    bool keep_alive = request.keepAlive();
    response.setKeepAlive(keep_alive);
    conn.setKeepAlive(keep_alive);

    if (response.useSendfile()) {
        conn.setWriteBuffer(response.build());
        conn.setSendfile(response.getSendfilePath(),
                         response.getSendfileSize());
    } else {
        conn.setWriteBuffer(response.build());
    }
    conn.setState(ConnectionState::WRITING);
    // modifyEpoll(conn.fd(), EPOLLOUT | EPOLLET, &conn);
    // handleWrite(&conn, std::chrono::steady_clock::now());

    return request.parseLength();
}

void Worker::handleWrite(Connection *conn,
                         const std::chrono::steady_clock::time_point &now) {
    if (!conn) return;

    conn->updateActivity(now);
    int fd = conn->fd();

    // 使用writev合并发送write_buffer和cached_response
    while (conn->writeRemaining() > 0 ||
            (conn->hasCachedResponse() && conn->cachedRemaining() > 0)) {
        
        struct iovec iov[2];
        int iovcnt = 0;

        if(conn->writeRemaining() > 0){
            iov[iovcnt].iov_base = const_cast<char*>(conn->writeData());
            iov[iovcnt].iov_len =  conn->writeRemaining();
            iovcnt++;
        }

        if(conn->hasCachedResponse() && conn->cachedRemaining() > 0){
            iov[iovcnt].iov_base = const_cast<char*>(conn->cachedData());
            iov[iovcnt].iov_len =  conn->cachedRemaining();
            iovcnt++;
        }


        ssize_t sent = writev(fd, iov, iovcnt);
        if (sent < 0) {
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                if(!conn->hasEpollout()){
                    conn->setHasEpollout(true);
                    modifyEpoll(fd, EPOLLIN | EPOLLOUT | EPOLLET, conn);
                }
                return;
            }
            closeConnection(conn);
            return;
        }

        //更新缓冲区偏移
        size_t remaining = sent;
        if(conn->writeRemaining() > 0){
            size_t consume = std::min(remaining, conn->writeRemaining());
            conn->advanceWrite(consume);
            remaining -= consume;
        }
        if(remaining > 0 && conn->hasCachedResponse()){
            conn->advanceCached(remaining);
        }
    }

    conn->clearCachedResponse();

    // 发送sendfile
    if (conn->hasSendfile() && !conn->sendfileComplete()) {
        if (!sendWithSendfile(*conn)) {
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                modifyEpoll(fd, EPOLLIN | EPOLLOUT | EPOLLET, conn);
                return;
            }
            closeConnection(conn);
            return;
        }
        if (!conn->sendfileComplete())
            return;
    }

    if (conn->keepAlive()) {
        // conn->clearReadBuffer();
        conn->setWriteBuffer("");
        conn->setState(ConnectionState::READING);
        if(conn->readBuffer().size()>0){
            handleRead(conn, now);
            return;
        }
        modifyEpoll(fd, EPOLLIN | EPOLLET, conn);
    } else {
        closeConnection(conn);
    }
}

bool Worker::sendWithSendfile(Connection &conn) {

    if (conn.fileFd() < 0) {
        int file_fd = open(conn.sendfilePath().c_str(), O_RDONLY);
        if (file_fd < 0) {
            std::cerr << "open() error: " << strerror(errno) << std::endl;
            return false;
        }
        conn.setFileFd(file_fd);
    }

    while (conn.sendfileOffset() < conn.sendfileSize()) {
        ssize_t sent =
            sendfile(conn.fd(), conn.fileFd(), &conn.sendfileOffset(),
                     conn.sendfileSize() - conn.sendfileOffset());

        if (sent < 0) {
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                return true;
            }
            conn.closeFileFd();
            return false;
        }
    }
    conn.closeFileFd();
    return true;
}

void Worker::serveStaticFile(const std::string &path, HttpResponse &response) {
    std::string filepath = config_.www_root + path;
    if (filepath.back() == '/')
        filepath += "index.html";

    struct stat file_stat;
    if (stat(filepath.c_str(), &file_stat) < 0) {
        response.setStatusCode(404);
        response.setBody("<html><body><h1> 404 Not Found</h1></body></html>");
        response.setContentType("text/html");
        return;
    }

    response.setStatusCode(200);
    response.setContentType(HttpResponse::getContentType(filepath));

    if (config_.use_sendfile) {
        response.setSendFilePath(filepath, file_stat.st_size);
    } else {
        std::ifstream file(filepath, std::ios::binary);
        std::ostringstream oss;
        oss << file.rdbuf();
        response.setBody(oss.str());
    }
}

// Epoll操作
bool Worker::addToEpoll(int fd, uint32_t events, Connection *conn) {
    struct epoll_event ev{};
    ev.events = events;
    ev.data.ptr = conn; // 使用指针而非 fd
    return epoll_ctl(epoll_fd_, EPOLL_CTL_ADD, fd, &ev) == 0;
}

bool Worker::modifyEpoll(int fd, uint32_t events, Connection *conn) {
    struct epoll_event ev{};
    ev.events = events;
    ev.data.ptr = conn; // 使用指针而非 fd
    return epoll_ctl(epoll_fd_, EPOLL_CTL_MOD, fd, &ev) == 0;
}

void Worker::removeFromEpoll(int fd) {
    epoll_ctl(epoll_fd_, EPOLL_CTL_DEL, fd, nullptr);
}

void Worker::closeConnection(Connection *conn) {
    if (!conn) return;

    conn->setState(ConnectionState::CLOSING);

    int fd = conn->fd();
    conn->closeFileFd();
    removeFromEpoll(fd);
    close(fd);

    // 从活跃列表移除（使用索引实现真正的 O(1)）
    size_t idx = conn->poolIndex();
    if (idx < active_conns_.size() && active_conns_[idx] == conn) {
        // swap-and-pop
        Connection *last = active_conns_.back();
        if (last != conn) {
            active_conns_[idx] = last;
            last->setPoolIndex(idx); // 更新被移动连接的索引
        }
        active_conns_.pop_back();
    }

    // 归还对象池
    conn_pool_.release(conn);
}

void Worker::checkIdleConnections(
    const std::chrono::steady_clock::time_point &now) {
    std::vector<Connection *> to_close;
    to_close.reserve(100);

    for (Connection *conn : active_conns_) {
        if (conn && conn->isIdle(config_.idle_timeout_ms, now)) {
            to_close.push_back(conn);
        }
    }
    for (Connection *conn : to_close) {
        closeConnection(conn);
    }
}