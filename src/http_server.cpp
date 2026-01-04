#include "http_server.h"
#include "http_request.h"
#include "http_response.h"
#include "worker.h"
#include <iostream>
#include <cstring>
#include <unistd.h>
#include <fcntl.h>
#include <sys/socket.h>
#include <sys/sendfile.h>
#include <sys/stat.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <fstream>
#include <sstream>
#include <netinet/tcp.h>


void HttpServer::start(){
    // 预加载静态文件到缓存
    cache_.preload(config_.www_root);
    std::cout << "Cached " << cache_.size() << " static files" << std::endl;

    for(int i = 0; i < config_.worker_count; ++i){
        workers_.push_back(std::make_unique<Worker>(i, config_, cache_));
        workers_.back()->start();
    }
    running_ = true;
}

void HttpServer::stop(){
    running_ = false;
    for(auto& worker : workers_){
        worker->stop();
    }
    for(auto& worker :workers_){
        worker->join();
    }
}
