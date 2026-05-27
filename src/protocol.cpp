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

typedef struct  __attribute__((packed)){ 
    uint16_t stop_status;
    uint16_t standby_status;
    uint16_t run_status;
    uint16_t malfunction_status;
    uint16_t alarm_status;
    uint16_t afar_status;
    uint16_t emergencystop_status;
    uint16_t gridconnected_status;
    uint16_t VFOffline_status;
    uint16_t deratingdue_status;
}Driver_Status_t;

typedef struct  __attribute__((packed)){ 
    uint16_t run_select;
    uint16_t onlineOffline_set;
    uint16_t current_limit;
    uint16_t voltageoutput_set;
    uint16_t frequency_set;
}Driver_Control_t;

//Driver_Status_t *DeviceStatusRegister = NULL;

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
        case PROTOCOL_DATA_QUERY_CMD: { //数据查询
            if (payload_len < 4) break;
            uint16_t regquery_addr = ntohs(*(uint16_t*)payload);
            uint16_t regquery_len  = ntohs(*(uint16_t*)(payload + 2));

            printf("regquery_addr : %d, regquery_len : %d\n", regquery_addr, regquery_len);
            
            //if(regquery_len > payload_len) break;  
            switch(regquery_addr/1000)
            {
                case 10:{
                    std::vector<uint16_t> status_buf(regquery_len);  
                    printf("ADDR 1001 read\n");
                    if(devic_status_getregbuf(status_buf.data(), regquery_addr, regquery_len)==0)
                    {
                       printf("ADDR 1001 control\n");
                       std::vector<uint8_t> resp_data; 
                       resp_data.resize(4 + regquery_len * sizeof(uint16_t));    
                       uint16_t net_addr = htons(regquery_addr);
                       memcpy(resp_data.data(), &net_addr, 2);
                       uint16_t net_len = htons(regquery_len);
                       memcpy(resp_data.data() + 2, &net_len, 2);  
                       memcpy(resp_data.data() + 4, status_buf.data(), regquery_len * sizeof(uint16_t));   
                       sendResponse(cmd, resp_data);                 
                    }  
                    else
                    {
                        printf("ADDR 1001 error\n");
                    }
                }
                break;
                case 30:{ 
                    std::vector<float> data_buf(regquery_len);
                    if(query_data_getdata(data_buf.data(), regquery_addr, regquery_len) == 0)
                    {
                        // 构造响应包：addr + length + 数据
                        //std::cout << " Send 30Addr : "<< regquery_len << std::endl;
                        std::vector<uint8_t> resp_data;
                        resp_data.resize(4 + regquery_len * sizeof(float));
                        // 写入 addr
                        uint16_t net_addr = htons(regquery_addr);
                        memcpy(resp_data.data(), &net_addr, 2);
                        // 写入 length
                        uint16_t net_len = htons(regquery_len);
                        memcpy(resp_data.data() + 2, &net_len, 2);
                        // 写入 float 数据（按内存原样）
                        memcpy(resp_data.data() + 4, data_buf.data(), regquery_len * sizeof(float));
                        sendResponse(cmd, resp_data);
                    }
                    else
                    {
                         printf("CMD : %X\n", regquery_addr);
                    }
                }
                break;
                case 40:{
                    std::vector<uint16_t> control_buf(regquery_len);
                    if(devic_control_getregbuf(control_buf.data(), regquery_addr, regquery_len) == 0)   
                    {
                        std::vector<uint8_t> resp_data;
                        resp_data.resize(4 + regquery_len * sizeof(uint16_t));
                        // 写入 addr
                        uint16_t net_addr = htons(regquery_addr);
                        memcpy(resp_data.data(), &net_addr, 2);
                        // 写入 length
                        uint16_t net_len = htons(regquery_len);
                        memcpy(resp_data.data() + 2, &net_len, 2);
                        // 写入 float 数据（按内存原样）
                        memcpy(resp_data.data() + 4, control_buf.data(), regquery_len * sizeof(uint16_t));
                        sendResponse(cmd, resp_data);
                    } 
                    else
                    {

                    }
                }
                break;
                default:
                break;
            }
            break;
        }
        case PROTOCOL_PARAMTER_CONFIG_CMD: {//参数配置
            if (payload_len < 4) break;
            uint16_t regquery_addr = ntohs(*(uint16_t*)payload);
            uint16_t regquery_len  = ntohs(*(uint16_t*)(payload + 2));

            printf("regquery_addr : %d, regquery_len : %d\n", regquery_addr, regquery_len);

            if((regquery_addr/1000) != 40 && (regquery_addr/1000) !=0)
            {
                break; 
            }
            
            switch((regquery_addr/1000))
            {
                case 40:{
                    std::vector<uint16_t> control_buf(regquery_len);
                    memcpy(control_buf.data(), payload+4, regquery_len * sizeof(uint16_t));
                    if(devic_control_setregbuf(control_buf.data(), regquery_addr, regquery_len) == 0)
                    {
                        std::vector<uint8_t> resp_data;
                        resp_data.resize(4 + regquery_len * sizeof(uint16_t));
                        // 写入 addr
                        uint16_t net_addr = htons(regquery_addr);
                        memcpy(resp_data.data(), &net_addr, 2);
                        // 写入 length
                        uint16_t net_len = htons(regquery_len);
                        memcpy(resp_data.data() + 2, &net_len, 2);
                        // 写入 float 数据（按内存原样）
                        memcpy(resp_data.data() + 4, control_buf.data(), regquery_len * sizeof(uint16_t));
                        sendResponse(cmd, resp_data);                        
                    }
                }
                break;
                case 0:

                break;
            }

            break;
        }
        case PROTOCOL_EVENT_TRANSINT_CMD:{//事件传输
            event_protocol_handle(hdr->data, hdr->head - 6);
            break;         
        }
        case PROTOCOL_CURVE_TRANSINT_CMD:{
           // uint16_t Reg_addr = (hdr->data[0] | (hdr->data[1]<<8)) - 30000;
           // uint8_t length = hdr->data[2] % 26;
          //  Reg_addr = Reg_addr % 26;
           // printf("REG_ADDR %d\n",Reg_addr);
            std::vector<uint8_t> resp_data;
            resp_data.resize(24);
            //memcpy(resp_data.data(), protoco_driver_regmemcoy + Reg_addr, length);
            memcpy(resp_data.data(), protoco_driver_regmemcoy , 24);
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


bool ProtocolParser::getfile_ftp(const std::string& cmdurl,const std::string& image_file)
{
    std::string url = cmdurl;//"ftp://172.25.1.137/uboot.img";
    std::string output_file = image_file; 
    std::string username = "";    
    std::string password = "";

    FtpGetFile ftp_download; 

    printf("ftp download\n");
    bool ok = ftp_download.ftp_get_file(url,output_file, username, password);
    return ok;
}

int ProtocolParser::query_data_getdata(float *buf, uint16_t addr, uint16_t length)
{
    uint16_t reg_addr = addr - 30000;

    if(protoco_driver_regmemcoy == NULL)
    {
        return -1;
    }

    if(reg_addr > DRIVER_REG_ADDR_MAX && reg_addr == 0)
    {
        return -2;
    }

    reg_addr -= 1;

    memcpy((uint8_t*)buf,(protoco_driver_regmemcoy + (reg_addr * 4)), length * 4);


    return 0;
}

int ProtocolParser::devic_status_getregbuf(uint16_t *buf, uint16_t addr, uint16_t length)
{
    uint16_t *status_reg = NULL;
    uint16_t reg_addr = addr - 10000;

    if(protoco_driver_regmemcoy == NULL)
    {
        return -1;
    }
    reg_addr -= 1 ;
    if(reg_addr > 10) return - 2;
    status_reg = (protoco_driver_regmemcoy + DRIVER_STATUS_REG_ADDR);
    printf("reg_addr : %d\n",reg_addr);
    memcpy(buf, status_reg+(reg_addr*2), (length*2));

    return 0;
}

int ProtocolParser::devic_control_getregbuf(uint16_t *buf, uint16_t addr, uint16_t length)
{
    uint16_t *control_reg = NULL;

    uint16_t reg_addr = addr - 40000;

    if(protoco_driver_regmemcoy == NULL)
    {
        return -1;
    }

    reg_addr -= 1;
    control_reg = (protoco_driver_regmemcoy + DRIVER_CONTROL_REG_ADDR);

    memcpy(buf, control_reg+(reg_addr*4), (length*2));

    return 0;    
}

int ProtocolParser::devic_control_setregbuf(uint16_t *buf, uint16_t addr, uint16_t length)
{
    uint16_t *control_reg = NULL;

    uint16_t reg_addr = addr - 40000;

    if(protoco_driver_regmemcoy == NULL)
    {
        return -1;
    }    

    reg_addr -= 1;

    printf("RUN status: %d\n", (uint32_t)buf[0]);

    control_reg = (protoco_driver_regmemcoy + DRIVER_CONTROL_REG_ADDR);

    memcpy(control_reg+(reg_addr*4), buf, (length*2));

    memcpy(buf, control_reg+(reg_addr*4), (length*2));

    return 0;
}


