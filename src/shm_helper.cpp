#include <sys/ipc.h>
#include <sys/shm.h>
#include <cstdio>
#include <cstdlib>
#include "shm_helper.hpp"


SharedMemory::SharedMemory(const char* path, int proj_id, std::size_t size)
    : m_shm_id(-1), m_shm_addr(nullptr)
{
    key_t key = ftok(path, proj_id);
    if (key == -1) {
        perror("ftok");
        exit(EXIT_FAILURE);
    }

    m_shm_id = shmget(key, size, IPC_CREAT | 0666);
    if (m_shm_id == -1) {
        perror("shmget");
        exit(EXIT_FAILURE);
    }

    m_shm_addr = shmat(m_shm_id, nullptr, 0);
    if (m_shm_addr == (void*)-1) {
        perror("shmat");
        exit(EXIT_FAILURE);
    }
}

SharedMemory::~SharedMemory() {
    if (m_shm_addr != nullptr && m_shm_addr != (void*)-1) {
        shmdt(m_shm_addr);
    }
}

void* SharedMemory::get() const {
    return m_shm_addr;
}

void SharedMemory::remove() {
    if (m_shm_id != -1) {
        shmctl(m_shm_id, IPC_RMID, nullptr);
        m_shm_id = -1;
    }
}