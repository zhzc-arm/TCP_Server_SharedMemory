#ifndef SHM_HELPER_HPP
#define SHM_HELPER_HPP

#include <cstddef>

class SharedMemory {
public:
    // path: ftok 使用的文件路径, proj_id: 项目ID, size: 共享内存大小（字节）
    SharedMemory(const char* path, int proj_id, std::size_t size);
    ~SharedMemory();

    // 禁止拷贝
    SharedMemory(const SharedMemory&) = delete;
    SharedMemory& operator=(const SharedMemory&) = delete;

    // 获取共享内存基地址
    void* get() const;

    // 删除共享内存（仅在服务器退出时调用）
    void remove();

private:
    int m_shm_id;
    void* m_shm_addr;
};

#endif // SHM_HELPER_HPP