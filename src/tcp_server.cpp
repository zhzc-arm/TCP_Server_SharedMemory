#include "tcp_server.hpp"
#include <iostream>
#include <cstring>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <signal.h>
#include <sstream>
#include <iomanip>

#define SHM_PATH "/tmp/modbus_shm"
#define SHM_PROJ_ID 'M'
#define NUM_HOLDING_REGISTERS 1024  


static void ignoreSigpipe() 
{
    struct sigaction sa;
    sa.sa_handler = SIG_IGN;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0;
    sigaction(SIGPIPE, &sa, nullptr);
}

TcpServer::TcpServer(uint16_t port) : port_(port), listen_fd_(-1), running_(false),
                                    shm_(SHM_PATH, SHM_PROJ_ID, NUM_HOLDING_REGISTERS * sizeof(uint16_t))
{
    TCP_DriverRegMemory = static_cast<uint16_t*>(shm_.get());
    std::cout << "Shared memory attached at address: " << TCP_DriverRegMemory << std::endl;

    ignoreSigpipe();
}

TcpServer::~TcpServer() 
{
    stop();
    if (listen_fd_ != -1) close(listen_fd_);
    if (parser_thread_.joinable()) parser_thread_.join();
 
    //shm_.remove();
    //std::cout << "Shared memory removed." << std::endl;
}

bool TcpServer::initSocket() 
{
    listen_fd_ = socket(AF_INET, SOCK_STREAM, 0);
    if (listen_fd_ == -1) 
    { 
        perror("socket"); 
        return false; 
    }

    int opt = 1;
    setsockopt(listen_fd_, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port = htons(port_);
    if (bind(listen_fd_, (struct sockaddr*)&addr, sizeof(addr)) == -1) {
        perror("bind"); close(listen_fd_); return false;
    }
    if (listen(listen_fd_, SOMAXCONN) == -1) {
        perror("listen"); close(listen_fd_); return false;
    }
    std::cout << "Server listening on port " << port_ << std::endl;
    return true;
}

std::string bytesToHex(const char* data, size_t len) {
    std::stringstream ss;
    ss << std::hex << std::setfill('0');
    for (size_t i = 0; i < len; ++i) {
        ss << std::setw(2) << (static_cast<unsigned int>(static_cast<unsigned char>(data[i])));
        if (i != len - 1) ss << ' ';  // 字节之间加空格，可选
    }
    return ss.str();
}

void TcpServer::handleClient(int client_fd, ThreadSafeQueue<RawData>* queue, uint16_t *bufdata) 
{
    struct sockaddr_in peer_addr;
    socklen_t addr_len = sizeof(peer_addr);
    getpeername(client_fd, (struct sockaddr*)&peer_addr, &addr_len);
    std::string client_ip = inet_ntoa(peer_addr.sin_addr);
    uint16_t client_port = ntohs(peer_addr.sin_port);

    char buf[4096];
    while (true) 
    {
        ssize_t n = read(client_fd, buf, sizeof(buf));
        if (n <= 0) break;
        // 将原始数据投递给解析线程
        RawData raw;
        raw.client_fd = client_fd;
        raw.data.assign(buf, buf + n);
        queue->push(std::move(raw));
        //打印帧数据
        std::string hexStr = bytesToHex(buf, n);
        std::cout << "Client " << client_ip << ":" << client_port << " :" << hexStr << std::endl;

        std::string mhexStr = bytesToHex((const char*)bufdata, 10);
        std::cout << "Shared " <<  mhexStr << std::endl;
    }

    RawData close_signal;
    close_signal.client_fd = client_fd;
    close_signal.data.clear();  // 空数据表示 EOF
    queue->push(std::move(close_signal));
    close(client_fd);
    std::cout << "Client " << client_fd << " disconnected" << std::endl;
}

void TcpServer::processMessage(int client_fd, const Message& msg) 
{
    std::string request(msg.body.begin(), msg.body.end());
    std::cout << "[" << client_fd << "] Request: " << request << std::endl;
 
    const auto& data = msg.body;
    size_t sent = 0;
    
    while (sent < data.size()) 
    {
        ssize_t n = write(client_fd, data.data() + sent, data.size() - sent);
        if (n > 0) 
        {
            sent += n;
        } 
        else if (n == -1 && (errno == EAGAIN || errno == EWOULDBLOCK)) 
        {
            usleep(1000);
        } 
        else 
        {
            perror("write");
            break;
        }
    }
  //  std::cout << "sent: " << sent << std::endl;
}

void TcpServer::parserLoop() 
{
    while (running_) 
    {
        RawData raw;
        if (!raw_queue_.pop(raw)) 
        {
            if (!running_) break;
            continue;
        }
        int fd = raw.client_fd;
        // 空数据表示连接关闭
        if (raw.data.empty()) {
            auto it = parsers_.find(fd);
            if (it != parsers_.end()) 
            {
                it->second->reset();
                parsers_.erase(it);
            }
            continue;
        }
        // 获取或创建该连接的解析器
        auto it = parsers_.find(fd);
        if (it == parsers_.end()) 
        {
            auto parser = std::make_unique<ProtocolParser>();
            parser->setClientFd(fd);   // 设置 fd
            parser->protoco_driver_regmemcoy = TCP_DriverRegMemory;
            parsers_[fd] = std::move(parser);
            it = parsers_.find(fd);
        }
        ProtocolParser* parser = it->second.get();
        parser->feed(raw.data.data(), raw.data.size());
        
        Message msg;
        while (parser->tryPop(msg)) {
            // 解析出完整消息，立即处理（也可以再放入业务队列）
            processMessage(fd, msg);
        }
    }
    std::cout << "Parser thread stopped" << std::endl;
}

void TcpServer::start() 
{
    if (!initSocket()) 
    {
        std::cerr << "Failed to initialize server socket" << std::endl;
        return;
    }
    running_ = true;
    // 启动解析线程
    parser_thread_ = std::thread(&TcpServer::parserLoop, this);
    
    while (running_) 
    {
        struct sockaddr_in client_addr;
        socklen_t client_len = sizeof(client_addr);
        int client_fd = accept(listen_fd_, (struct sockaddr*)&client_addr, &client_len);
        if (client_fd == -1) 
        {
            if (running_) perror("accept");
            continue;
        }
        std::string client_ip = inet_ntoa(client_addr.sin_addr);
        std::cout << "Client " << client_fd << " IP :" << client_ip << std::endl;
        // 为每个客户端创建一个 I/O 线程（只负责读数据并放入队列）
        std::thread io_thread(handleClient, client_fd, &raw_queue_,TCP_DriverRegMemory);
        io_thread.detach();
    }
    // 通知解析线程停止
    raw_queue_.stop();
    if (parser_thread_.joinable()) parser_thread_.join();
}

void TcpServer::stop() 
{
    running_ = false;
    if (listen_fd_ != -1) 
    {
        shutdown(listen_fd_, SHUT_RD);
    }
}