#include "protocol.hpp"
#include <iostream>
#include <unistd.h>
#include <arpa/inet.h>
#include <cstring>
#include "ftp_getfile.hpp"
#include <vector>
#include <cstdint>



#define PROTOCOL_GATEWAY_HEAD     0x68  //Gateway
#define PROTOCOL_NODE_HEAD        0x86  //Node
 
typedef struct __attribute__((packed)) {
    uint8_t head;
    uint16_t length;
   // uint16_t version;
    uint8_t cmd;
    uint8_t data[];   
} FrameHeader;

typedef struct __attribute__((packed)){
    uint8_t cmd;
    char ftpaddr[];
}UpdateFirware_t;


ProtocolParser::ProtocolParser() : state_(WAIT_HEADER), body_len_(0), client_fd_(-1)
{}

void ProtocolParser::setClientFd(int fd) 
{
    client_fd_ = fd;
}

void ProtocolParser::feed(const char* data, size_t len) 
{
    buffer_.insert(buffer_.end(), (uint8_t*)data, (uint8_t*)data + len);
}


uint16_t crc16_modbus(const uint8_t* data, size_t len) 
{
    uint16_t crc = 0xFFFF;
    const uint16_t poly = 0x8005;   

    for (size_t i = 0; i < len; ++i) {
        crc ^= static_cast<uint16_t>(data[i]);   
        for (int bit = 0; bit < 8; ++bit) {
            if (crc & 0x0001) {                 
                crc = (crc >> 1) ^ poly;
            } else {
                crc >>= 1;
            }
        }
    }
    return crc;
}

unsigned short CRC16(unsigned char *data, unsigned int len) 
{
    unsigned short crc = 0xFFFF;
    for (unsigned int i = 0; i < len; i++) 
    {
        crc ^= data[i];
        for (unsigned int j = 0; j < 8; j++) 
        {
            if (crc & 1)
                crc = (crc >> 1) ^ 0xA001;
            else
                crc >>= 1;
        }
    }
    return crc;
}


int ProtocolParser::findSyncWord() 
{
    for (size_t i = 0; i + 1 < buffer_.size(); ++i) 
    {
        //uint16_t word = (buffer_[i] << 8) | buffer_[i+1];
        uint8_t word = (buffer_[i]);
        if (word == PROTOCOL_GATEWAY_HEAD) return i;
    }
    return -1;
}

void ProtocolParser::sendResponse(uint16_t cmd, const std::vector<uint8_t>& data) 
{
    if (client_fd_ == -1) return;
    
    // 构造响应帧
    uint16_t payload_len = data.size();
    uint16_t total_len = sizeof(FrameHeader) + payload_len + sizeof(uint16_t); // + crc
    std::vector<uint8_t> packet(total_len);
    
    FrameHeader* hdr = (FrameHeader*)packet.data();
    hdr->head = PROTOCOL_NODE_HEAD;
    //hdr->version = 0;
    hdr->cmd = cmd;
    //hdr->length = htons(payload_len);
    hdr->length = htons(total_len);
    if (payload_len > 0)
    {
        memcpy(packet.data() + sizeof(FrameHeader), data.data(), payload_len);
    }
    
    //uint16_t crc = crc16_modbus(packet.data(), (total_len - 2));  // 需要CRC，实际应计算
    uint16_t crc = CRC16(packet.data(), (total_len - 2));
    memcpy(packet.data() + sizeof(FrameHeader) + payload_len, &crc, sizeof(crc));
    
    // 发送
    size_t sent = 0;
    while (sent < packet.size()) 
    {
        ssize_t n = write(client_fd_, packet.data() + sent, packet.size() - sent);
        if (n > 0) sent += n;
        else if (n == -1 && (errno == EAGAIN || errno == EWOULDBLOCK)) usleep(1000);
        else break;
    }
}

void ProtocolParser::Parseframe(uint8_t* buf, uint16_t len) 
{
    // buf 指向完整的帧（从同步头开始，长度 len 包括头部+负载+CRC）

    //std::cout << "Parseframe Read Frame " << std::endl;

    if (len < sizeof(FrameHeader)) return;
    FrameHeader* hdr = (FrameHeader*)buf;
    if (hdr->head != PROTOCOL_GATEWAY_HEAD) return;
    
    uint16_t cmd = hdr->cmd;
    uint16_t payload_len = ntohs(hdr->length);
    uint8_t* payload = buf + sizeof(FrameHeader);
    
#if 1
    uint16_t crc_num = crc16_modbus(buf, (payload_len - 2));

    uint16_t crc_buf = buf[payload_len - 1] << 8 | buf[payload_len-2];

    std::cout << "CRC16 : " << crc_num << "CRC" << crc_buf << std::endl;

#endif

    // 根据命令字处理并发送响应
    std::cout << " CMD : "<< cmd << std::endl;
    switch (cmd) {
        case PROTOCOL_DATA_QUERY_CMD: {
            std::string response = "Ack for cmd1";
            std::vector<uint8_t> resp_data(response.begin(), response.end());
            sendResponse(cmd, resp_data);
            break;
        }
        case PROTOCOL_PARAMTER_CONFIG_CMD: {

            std::cout << "Protocol at address: " << protoco_driver_regmemcoy << std::endl;
            std::vector<uint8_t> resp_data = {0x01, 0x02, 0x03 , 0x06, 0x05, 0x00, 0x90};
            memcpy(resp_data.data(), protoco_driver_regmemcoy, 8);
            sendResponse(cmd, resp_data);
            break;
        }
        case PROTOCOL_EVENT_TRANSINT_CMD:{
            //getfile_ftp("ftp://172.25.1.137/uboot.img");
            //std::vector<uint8_t> resp_data = {0x08, 0x06, 0x07};
            //sendResponse(cmd, resp_data);  
            event_protocol_handle(hdr->data, hdr->head - 6);
            break;         
        }
        case PROTOCOL_CURVE_TRANSINT_CMD:{
            uint16_t Reg_addr = (hdr->data[0] | (hdr->data[1]<<8)) - 30000;
            uint8_t length = hdr->data[2] % 26;
            Reg_addr = Reg_addr % 26;
            printf("REG_ADDR %d\n",Reg_addr);
            std::vector<uint8_t> resp_data;
            resp_data.resize(length);
            memcpy(resp_data.data(), protoco_driver_regmemcoy + Reg_addr, length);
            //memcpy(resp_data.data(), protoco_driver_regmemcoy , 26*2);
            sendResponse(cmd, resp_data);
            break;
        }        
        default:
            //sendResponse(cmd, {});
            break;
    }
}

int ProtocolParser::event_protocol_handle(uint8_t *buf, uint16_t len)
{
    UpdateFirware_t * u_hdr = (UpdateFirware_t*)buf;
    switch(u_hdr->cmd)
    {
        case 0x1D:
            printf("FTP ADDR : %s\n", u_hdr->ftpaddr);
            getfile_ftp(u_hdr->ftpaddr,"uboot.img");
        break;
        case 0x2D:
            printf("FTP ADDR : %s\n", u_hdr->ftpaddr);
            getfile_ftp(u_hdr->ftpaddr,"Image.img");
        break;
    }

    return 0;
}

bool ProtocolParser::tryPop(Message& msg) 
{
    while (true) 
    {
        if (state_ == WAIT_SYNC) 
        {
            int offset = findSyncWord();
            if (offset == -1) {
                if (buffer_.size() > 2)
                    buffer_.erase(buffer_.begin(), buffer_.end() - 2);
                return false;
            }
            if (offset > 0)
                buffer_.erase(buffer_.begin(), buffer_.begin() + offset);
            state_ = WAIT_HEADER;
        }
        
        if (state_ == WAIT_HEADER) {
            if (buffer_.size() < sizeof(FrameHeader)) return false;
            FrameHeader* hdr = (FrameHeader*)buffer_.data();
            if (hdr->head != PROTOCOL_GATEWAY_HEAD) 
            {
                state_ = WAIT_SYNC;
                std::cout << "Fream Head error " << std::endl;
                buffer_.erase(buffer_.begin(), buffer_.end());
                continue;
            }
            body_len_ = ntohs(hdr->length);
            state_ = WAIT_BODY;
        }
        
        if (state_ == WAIT_BODY) {
            uint16_t total_needed = body_len_ ;//sizeof(FrameHeader) + body_len_ ;//+ sizeof(uint16_t);
           //std::cout << "total_needed : "<< total_needed << "buffer_.size: "<< buffer_.size() << std::endl;
            if (buffer_.size() < total_needed) 
            {
                buffer_.erase(buffer_.begin(), buffer_.end());
                return false;
            }
            // 调用 Parseframe 处理完整帧，它会发送响应
            Parseframe(buffer_.data(), total_needed);
            
            // 移除已处理的帧
            buffer_.erase(buffer_.begin(), buffer_.begin() + total_needed);
            state_ = WAIT_SYNC;
            
            // 填充 msg 供外部使用
            msg.cmd = ntohs(((FrameHeader*)buffer_.data())->cmd);

            return true;  
        }
    }
}

void ProtocolParser::reset() 
{
    state_ = WAIT_HEADER;
    body_len_ = 0;
    buffer_.clear();
}


bool ProtocolParser::getfile_ftp(const std::string& cmdurl,const std::string& output_file)
{
    std::string url = cmdurl;//"ftp://172.25.1.137/uboot.img";
    std::string output_file = output_file; 
    std::string username = "";    
    std::string password = "";

    FtpGetFile ftp_download; 

    printf("ftp download\n");
    bool ok = ftp_download.ftp_get_file(url,output_file, username, password);
    return ok;
}



