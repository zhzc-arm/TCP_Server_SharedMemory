#ifndef PROTOCOL_HPP
#define PROTOCOL_HPP

#include <vector>
#include <cstdint>
#include <cstring>
#include <string>



struct Message {
    uint16_t cmd;
    std::vector<uint8_t> body;
};

#define PROTOCOL_DATA_QUERY_CMD             0xF1         //Data Query
#define PROTOCOL_PARAMTER_CONFIG_CMD        0xF2         // Parameter Configuration
#define PROTOCOL_EVENT_TRANSINT_CMD         0xF3         //Event Transmission
#define PROTOCOL_CURVE_TRANSINT_CMD         0xF4         //Curve transmission

 
#define DRIVER_REG_ADDR_MAX                 (26)

#define DRIVER_STATUS_REG_ADDR              200
#define DRIVER_CONTROL_REG_ADDR             300


class ProtocolParser {
public:
    ProtocolParser();
    void setClientFd(int fd);  
    void feed(const char* data, size_t len);
    bool tryPop(Message& msg);
    void reset(); 

    int event_protocol_handle(uint8_t *buf, uint16_t len);
    int query_data_getdata(float *buf, uint16_t addr, uint16_t length);
    int devic_status_getregbuf(uint16_t *buf, uint16_t addr, uint16_t length);
    int devic_control_getregbuf(uint16_t *buf, uint16_t addr, uint16_t length);

    int devic_control_setregbuf(uint16_t *buf, uint16_t addr, uint16_t length);

    static std::vector<char> serialize(const Message& msg) 
    {
        uint32_t body_len = msg.body.size();
        std::vector<char> buffer(sizeof(uint32_t) + body_len);
        memcpy(buffer.data(), &body_len, sizeof(uint32_t));
        memcpy(buffer.data() + sizeof(uint32_t), msg.body.data(), body_len);
        return buffer;
    }

    uint16_t * protoco_driver_regmemcoy = NULL;

private:
    enum State { WAIT_SYNC, WAIT_HEADER, WAIT_BODY };
    State state_;
    uint16_t body_len_;
    std::vector<uint8_t> buffer_;
    int client_fd_;        
    
    //SharedMemory *d_regmemory;
    
    int findSyncWord();
    bool parseFrame(Message& msg);
    void Parseframe(uint8_t* buf, uint16_t len);   // 处理并发送响应
    void sendResponse(uint16_t cmd, const std::vector<uint8_t>& data);

    bool getfile_ftp(const std::string& cmdurl, const std::string& image_file);
};

#endif