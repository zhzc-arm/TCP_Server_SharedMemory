#include "tcp_server.hpp"
#include "protocol.hpp"

#include <iostream>
#include <signal.h>
#include <cstdlib>

static TcpServer* g_server = nullptr;

// 信号处理函数，用于优雅退出
void signalHandler(int sig) {
    if (sig == SIGINT || sig == SIGTERM) {
        std::cout << "\nReceived interrupt signal, shutting down..." << std::endl;
        if (g_server) {
            g_server->stop();
        }
    }
}

int main(int argc, char* argv[]) {
    uint16_t port = 6008;  // 默认端口
    if (argc > 1) {
        port = static_cast<uint16_t>(std::atoi(argv[1]));
    }
    
    TcpServer server(port);
    g_server = &server;
    
    // 注册信号处理
    signal(SIGINT, signalHandler);
    signal(SIGTERM, signalHandler);
    
    server.start();
    
    return 0;
}