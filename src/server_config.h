#ifndef SERVER_CONFIG_H
#define SERVER_CONFIG_H

#include <string>
#include <thread>

struct ServerConfig {
    int port = 8080;
    int worker_count = std::thread::hardware_concurrency();
    std::string www_root = "./www";
    int max_events = 4096;
    int idle_timeout_ms = 60000;
    bool use_sendfile = true;
    bool debug_log = false;
};

#endif