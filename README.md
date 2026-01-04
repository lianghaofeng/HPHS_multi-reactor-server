# HPHS - High Performance HTTP Server

一个基于 C++17 的高性能静态文件 HTTP 服务器，专注于高并发场景下的极致性能优化。

## 性能指标

| 测试场景 | 并发连接 | QPS | 延迟 (P99) |
|:---|---:|---:|---:|
| 单连接基准 | 6,000 | 633K | 8.45ms |
| 高并发 | 60,000 | 901K | 335ms |
| HTTP Pipeline | 60,000 | 4.1M | 1.1s |
| 极限压测 | 60,000 | 8.55M | 606.73ms |

> 测试环境：8核 ARM64，服务器 4-5 核心，wrk 压测 3-4 核心，静态小文件
> 详细测试数据见 [BENCHMARK.md](./BENCHMARK.md)

## 架构设计

```
┌─────────────────────────────────────────────────────────────┐
│                        HttpServer                           │
│  ┌─────────────┐                                            │
│  │ResponseCache│  ← 启动时预加载静态文件到内存               │
│  └─────────────┘                                            │
├─────────────────────────────────────────────────────────────┤
│  ┌─────────┐  ┌─────────┐  ┌─────────┐  ┌─────────┐        │
│  │ Worker  │  │ Worker  │  │ Worker  │  │ Worker  │  ...   │
│  │ epoll   │  │ epoll   │  │ epoll   │  │ epoll   │        │
│  │ listen  │  │ listen  │  │ listen  │  │ listen  │        │
│  └────┬────┘  └────┬────┘  └────┬────┘  └────┬────┘        │
│       └───────────┴─────┬─────┴───────────┘                 │
│                         │                                    │
│            SO_REUSEPORT (内核级负载均衡)                     │
└─────────────────────────┼───────────────────────────────────┘
                          │
                     ┌────┴────┐
                     │ Clients │
                     └─────────┘
```

### 核心组件

| 组件 | 职责 |
|------|------|
| `HttpServer` | 服务器入口，管理 Worker 生命周期和响应缓存 |
| `Worker` | 独立事件循环，每个 Worker 一个线程 + epoll + listen socket |
| `Connection` | 连接状态机，管理读写缓冲区和生命周期 |
| `ConnectionPool` | 连接对象池，预分配 10 万连接，避免频繁 malloc |
| `ResponseCache` | 静态文件缓存，启动时预构建完整 HTTP 响应 |

## 技术亮点

### 1. Multi-Reactor + SO_REUSEPORT

每个 Worker 独立拥有 listen socket，内核通过 `SO_REUSEPORT` 进行连接分发，避免了传统 accept 锁竞争。

```cpp
// 每个 Worker 都创建自己的 listen socket
setsockopt(sockfd, SOL_SOCKET, SO_REUSEPORT, &opt, sizeof(opt));
```

### 2. 边缘触发 + 非阻塞 IO

使用 `EPOLLET` 边缘触发模式，配合非阻塞 socket，减少 epoll_wait 返回次数：

```cpp
addToEpoll(client_fd, EPOLLIN | EPOLLET, conn);
```

### 3. 零拷贝优化

- **栈上快速通道**：小请求直接在栈上解析，不拷贝到堆
- **writev 合并写**：header 和 body 一次系统调用发送
- **sendfile**：大文件直接内核态传输

```cpp
// 快速通道：直接解析栈上数据
if (conn->readBuffer().empty()) {
    size_t consumed = processRequest(*conn, std::string_view(stack_buffer, bytes));
}

// writev 合并发送
struct iovec iov[2];
writev(fd, iov, iovcnt);
```

### 4. 响应缓存

启动时预加载所有静态文件，构建完整 HTTP 响应存入内存：

```cpp
// 缓存命中直接返回，无需解析、stat、read
const CacheEntry* cached = cache_.find(request.path());
if (cached) {
    conn.setCachedResponse(&cached->response);
}
```

### 5. HTTP Pipelining 支持

支持客户端在一个连接上连续发送多个请求：

```cpp
// 循环处理缓冲区中的多个请求
while (conn->readBuffer().size() > 0) {
    size_t consumed = processRequest(*conn);
    if (consumed == 0) break;
    conn->consumeReadBuffer(consumed);  // O(1) 游标移动
}
```

### 6. epoll_ctl 优化

使用 `has_epollout_` 标志位追踪 EPOLLOUT 注册状态，避免重复系统调用：

```cpp
if (errno == EAGAIN) {
    if (!conn->hasEpollout()) {
        conn->setHasEpollout(true);
        modifyEpoll(fd, EPOLLIN | EPOLLOUT | EPOLLET, conn);
    }
}
```

### 7. 连接对象池复用

预分配 10 万 Connection 对象，避免高并发下的内存分配开销：

```cpp
ConnectionPool(size_t initial_size = 100000);
```

### 8. string_view 零拷贝解析

HTTP 请求解析全程使用 `std::string_view`，避免字符串拷贝：

```cpp
// 解析时直接引用原始缓冲区，不产生任何拷贝
bool HttpRequest::parse(std::string_view buffer) {
    std::string_view header_part = buffer.substr(0, header_end);
    std::string_view request_line = header_part.substr(0, line_end);
    // ...
}

// 快速通道：直接从栈上数据构造 view
size_t consumed = processRequest(*conn, std::string_view(stack_buffer, bytes));
```

配合栈上快速通道，小请求从接收到解析完成全程零堆分配。

### 9. 读缓冲区游标优化

使用 offset 游标代替 `erase(0, n)`，将缓冲区消费从 O(n) 优化到 O(1)：

```cpp
void consumeReadBuffer(size_t len) {
    read_offset_ += len;  // O(1)，不移动内存
    if (read_offset_ == read_buffer_.size()) {
        read_buffer_.clear();
        read_offset_ = 0;
    }
}

std::string_view readBuffer() const {
    return std::string_view(read_buffer_.data() + read_offset_,
                            read_buffer_.size() - read_offset_);
}
```

## Quick Start

### 编译

```bash
mkdir -p build && cd build
g++ -std=c++17 -O3 -pthread ../src/*.cpp -o hphs

# 或使用 CMake
cmake .. && make
```

### 运行

```bash
./hphs [port] [workers] [www_root]
./hphs 8080 4 ../www
```

### 测试

```bash
# 基准测试
wrk -t4 -c1000 -d30s http://localhost:8080/

# 高并发测试
wrk -t12 -c60000 -d60s --timeout 5s http://localhost:8080/test.html

# Pipeline 测试
wrk -t12 -c60000 -d60s -s pipeline.lua http://localhost:8080/test.html
```

## 性能调优指南

### 系统参数

```bash
# 增加文件描述符限制
ulimit -n 100000

# 增加可用端口范围
echo "1024 65535" > /proc/sys/net/ipv4/ip_local_port_range

# 开启端口复用
echo 1 > /proc/sys/net/ipv4/tcp_tw_reuse
```

### CPU 绑定

```bash
# 服务器绑定 CPU 0-3
taskset -c 0-3 ./hphs 8080 4 ../www

# wrk 绑定 CPU 4-7（避免与服务器争抢 CPU）
taskset -c 4-7 wrk -t12 -c60000 ...
```

## 项目结构

```
src/
├── main.cpp            # 入口
├── http_server.h/cpp   # 服务器管理
├── worker.h/cpp        # 事件循环核心
├── connection.h        # 连接状态机
├── connection_pool.h   # 对象池
├── response_cache.h    # 响应缓存
├── http_request.h/cpp  # HTTP 解析
├── http_response.h/cpp # HTTP 响应构建
└── server_config.h     # 配置
```

graph TD
    %% 定义样式
    classDef client fill:#f9f,stroke:#333,stroke-width:2px;
    classDef kernel fill:#eee,stroke:#333,stroke-width:1px,stroke-dasharray: 5 5;
    classDef worker fill:#d4f1f4,stroke:#333,stroke-width:1px;
    classDef cache fill:#ffed4a,stroke:#333,stroke-width:2px;

    Client((客户端)):::client

    subgraph OS_Kernel [Linux Kernel Space]
        LB{SO_REUSEPORT<br>内核级负载均衡}:::kernel
    end

    subgraph User_Space [HttpServer Process]
        Cache[ResponseCache<br>预加载静态文件/热点数据]:::cache
        
        subgraph Thread_Pool [Worker Threads]
            direction LR
            W1[Worker 1<br>epoll_fd + listen_fd]:::worker
            W2[Worker 2<br>epoll_fd + listen_fd]:::worker
            W3[Worker 3<br>epoll_fd + listen_fd]:::worker
            W4[Worker N...]:::worker
        end
    end

    %% 连接关系
    Client ==> |TCP Connection| LB
    LB -.-> |Round Robin| W1
    LB -.-> |Round Robin| W2
    LB -.-> |Round Robin| W3
    LB -.-> |Round Robin| W4

    W1 --> |Read| Cache
    W2 --> |Read| Cache
    W3 --> |Read| Cache
    W4 --> |Read| Cache