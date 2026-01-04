#include "http_server.h"
#include <iostream>
#include <csignal>
#include <memory>
#include "server_config.h"


int main(int argc, char* argv[]){
    signal(SIGPIPE, SIG_IGN);

    ServerConfig config;
    if(argc >= 2) config.port = std::atoi(argv[1]);
    if(argc >= 3) config.worker_count = std::atoi(argv[2]);
    if(argc >= 4) config.www_root = argv[3];

    HttpServer server(config);
    server.start();

    pause();
    server.stop();
}
