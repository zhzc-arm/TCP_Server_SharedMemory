cmake_minimum_required(VERSION 3.10)
project(TcpServer)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(CURL REQUIRED)

include_directories(${CMAKE_SOURCE_DIR}/include ${LIBMODBUS_INCLUDE_DIRS})

# 可执行文件
add_executable(tcp_server 
    src/main.cpp
    src/tcp_server.cpp
    src/protocol.cpp
    src/ftp_getfile.cpp
    src/shm_helper.cpp
)

# 指定头文件目录
target_include_directories(tcp_server PRIVATE ${CURL_INCLUDE_DIRS} )
 
# 链接线程库
target_link_libraries(tcp_server ${CURL_LIBRARIES} pthread)