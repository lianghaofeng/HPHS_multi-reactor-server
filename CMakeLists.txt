cmake_minimum_required(VERSION 3.10)
project(hphs VERSION 1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# 编译选项
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wall -Wextra")
set(CMAKE_CXX_FLAGS_RELEASE "-O3 -DNDEBUG -march=native -flto")
set(CMAKE_CXX_FLAGS_DEBUG "-g -O0")

# 默认 Release 构建
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release)
endif()

# 源文件
set(SOURCES
    src/main.cpp
    src/worker.cpp
    src/http_request.cpp
    src/http_response.cpp
    src/http_server.cpp
    src/thread_pool.cpp
    src/work_stealing_pool.cpp
)

# 头文件目录
include_directories(${CMAKE_SOURCE_DIR}/src)

# 可执行文件
add_executable(hphs ${SOURCES})

# 链接 pthread
find_package(Threads REQUIRED)
target_link_libraries(hphs Threads::Threads)

# 安装
install(TARGETS hphs DESTINATION bin)

# 输出构建信息
message(STATUS "Build type: ${CMAKE_BUILD_TYPE}")
message(STATUS "C++ Standard: ${CMAKE_CXX_STANDARD}")