#ifndef HTTP_SERVER_H
#define HTTP_SERVER_H

#include "server_config.h"
#include "response_cache.h"
#include "worker.h"
#include <vector>
#include <atomic>

class HttpServer {
public:
    explicit HttpServer(const ServerConfig& config): config_(config){};
    void start();
    void stop();

private:
    ServerConfig config_;
    ResponseCache cache_;                              // 静态文件缓存
    std::vector<std::unique_ptr<Worker>> workers_;
    std::atomic<bool> running_{false};
};

#endif
