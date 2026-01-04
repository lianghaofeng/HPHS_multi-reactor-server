#ifndef CONNECTION_POOL_H
#define CONNECTION_POOL_H

#include "connection.h"
#include <vector>
#include <stack>

// 连接对象池
// 默认预分配 5000 个连接，支持动态扩展
class ConnectionPool {
public:
    // initial_size: 初始预分配数量
    // 对于高并发场景，建议预分配足够的连接数（如 100000）
    explicit ConnectionPool(size_t initial_size = 100000) {
        pool_.reserve(initial_size);
        for(size_t i = 0; i < initial_size; ++i) {
            pool_.push_back(new Connection(-1));
            free_list_.push(pool_.back());
        }
    }

    ~ConnectionPool() {
        for(auto* conn : pool_) {
            delete conn;
        }
    }

    // 禁止拷贝
    ConnectionPool(const ConnectionPool&) = delete;
    ConnectionPool& operator=(const ConnectionPool&) = delete;

    Connection* acquire(int fd) {
        Connection* conn = nullptr;
        if(!free_list_.empty()) {
            conn = free_list_.top();
            free_list_.pop();
            conn->reset(fd);
        } else {
            // 池耗尽时动态扩展（无需预分配时的开销）
            conn = new Connection(fd);
            pool_.push_back(conn);
        }
        return conn;
    }

    void release(Connection* conn) {
        if(conn) {
            conn->reset(-1);
            free_list_.push(conn);
        }
    }

    size_t poolSize() const { return pool_.size(); }
    size_t availableSize() const { return free_list_.size(); }

private:
    std::vector<Connection*> pool_;
    std::stack<Connection*> free_list_;
};

#endif