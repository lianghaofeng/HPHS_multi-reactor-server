#ifndef WORKER_H
#define WORKER_H

#include "server_config.h"
#include "connection.h"
#include "connection_pool.h"
#include "response_cache.h"
#include <thread>
#include <atomic>
#include <vector>

class Worker{
public:
    Worker(int id, const ServerConfig& config, const ResponseCache& cache);
    ~Worker();

    void start();
    void stop();
    void join();

    size_t connectionCount() const {
        return active_conns_.size();
    }
    uint64_t requestCount() const {
        return request_count_;
    }

private:
    void run();
    int createListenSocket();

    void handleAccept();
    void handleRead(Connection* conn, const std::chrono::steady_clock::time_point & now);
    void handleWrite(Connection* conn, const std::chrono::steady_clock::time_point & now);
    void closeConnection(Connection* conn);

    size_t processRequest(Connection& conn, std::string_view data={});
    void serveStaticFile(const std::string& path, class HttpResponse& response);
    bool sendWithSendfile(Connection& conn);
    void checkIdleConnections(const std::chrono::steady_clock::time_point & now);

    bool addToEpoll(int fd, uint32_t events, Connection* conn = nullptr);
    bool modifyEpoll(int fd, uint32_t events, Connection* conn = nullptr);
    void removeFromEpoll(int fd);

private:
    int id_;
    const ServerConfig& config_;
    const ResponseCache& cache_;                // 响应缓存（共享）
    int listen_fd_ = -1;
    int epoll_fd_ = -1;
    std::thread thread_;
    std::atomic<bool> running_{false};
    ConnectionPool conn_pool_;                  // 对象池（构造函数中初始化）
    std::vector<Connection*> active_conns_;     // 活跃连接列表
    std::atomic<uint64_t> request_count_{0};
};

#endif