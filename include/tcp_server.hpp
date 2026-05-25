#ifndef TCP_SERVER_HPP
#define TCP_SERVER_HPP

#include <atomic>
#include <string>
#include <thread>
#include <memory>
#include "threadsafe_queue.hpp"
#include "protocol.hpp"
#include <unordered_map>
#include "shm_helper.hpp"


struct RawData {
    int client_fd;
    std::vector<char> data;
};

class TcpServer {
public:
    explicit TcpServer(uint16_t port);
    ~TcpServer();
    void start();
    void stop();


    uint16_t *TCP_DriverRegMemory = NULL;
private:
    uint16_t port_;
    int listen_fd_;
    std::atomic<bool> running_;

    SharedMemory shm_;
    
    // 原始数据队列（网络线程 -> 解析线程）
    ThreadSafeQueue<RawData> raw_queue_;
    
    // 解析线程
    std::thread parser_thread_;
    
    // 每个连接对应的解析器实例（由解析线程维护）
    // 解析器实例需要跨线程访问，但解析线程是唯一访问者，因此无需加锁
    std::unordered_map<int, std::unique_ptr<ProtocolParser>> parsers_;
    
    bool initSocket();
    static void handleClient(int client_fd, ThreadSafeQueue<RawData>* queue, uint16_t *bufdata);
    void parserLoop();
    void processMessage(int client_fd, const Message& msg);
};

#endif // TCP_SERVER_HPP