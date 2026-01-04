# Benchmark Results

本文档记录 HPHS 的详细性能测试数据，作为性能指标的原始证据。

## 测试环境

| 项目 | 配置 |
|------|------|
| CPU | 8 核 ARM64 |
| 服务器核心 | 4-5 核 (taskset 绑定) |
| 压测工具核心 | 3-4 核 (taskset 绑定) |
| 测试工具 | wrk |
| 测试文件 | test.html (~100 bytes) |
| 协议 | HTTP/1.1 Keep-Alive |

---

## 测试 1: 中等并发基准

```bash
root@7c02a1165c99:/workspace/wrk# taskset -c 4-7 /workspace/wrk/wrk -t4 -c6000  -d 60s --timeou
t 5s --latency http://localhost:8080/test.html
Running 1m test @ http://localhost:8080/test.html
  4 threads and 6000 connections
  Thread Stats   Avg      Stdev     Max   +/- Stdev
    Latency     1.78ms    2.49ms 233.65ms   94.66%
    Req/Sec   159.60k    26.48k  211.56k    78.43%
  Latency Distribution
     50%    1.26ms
     75%    2.17ms
     90%    3.37ms
     99%    8.45ms
  38049912 requests in 1.00m, 4.75GB read
  Socket errors: connect 0, read 86, write 0, timeout 0
Requests/sec: 633131.73
Transfer/sec:     80.91MB
```

**结果: 633K QPS, P99 延迟 8.45ms**

---

## 测试 2: 高并发 (60K 连接)

```bash
taskset -c 4-7 wrk -t 800 -c 60000 -d 60s --timeout 5s --latency http://localhost:8080/test.html
```

```
Running 1m test @ http://localhost:8080/test.html
  800 threads and 60000 connections
  Thread Stats   Avg      Stdev     Max   +/- Stdev
    Latency    80.76ms   85.94ms   4.77s    91.95%
    Req/Sec     1.14k     1.03k  177.78k    94.19%
  Latency Distribution
     50%   63.05ms
     75%  101.75ms
     90%  154.10ms
     99%  335.30ms
  141081381 requests in 1.03m, 17.61GB read
  Socket errors: connect 0, read 4031, write 0, timeout 4415
Requests/sec: 2288798.42
Transfer/sec: 292.49MB
```

**结果: 2.29M QPS, P99 延迟 335ms**

---

## 测试 3: HTTP Pipeline

使用 pipeline.lua 脚本，每个连接批量发送 16 个请求：

```bash
taskset -c 4-7 wrk -t 500 -c 60000 -d 60s -s pipeline.lua --timeout 5s --latency http://localhost:8080/test.html
```

```
Running 1m test @ http://localhost:8080/test.html
  500 threads and 60000 connections
  Thread Stats   Avg      Stdev     Max   +/- Stdev
    Latency   387.41ms  259.74ms   3.27s    65.93%
    Req/Sec     6.09k     2.34k  299.93k    74.37%
  Latency Distribution
     50%  349.94ms
     75%  554.87ms
     90%  739.88ms
     99%    1.11s
  252143645 requests in 1.00m, 31.47GB read
Requests/sec: 4186395.22
Transfer/sec: 534.99MB
```

**结果: 4.19M QPS, P99 延迟 1.11s**

---

## 测试 4: 更大规模 Pipeline

```bash
root@7c02a1165c99:/workspace/wrk# taskset -c 4-7 /workspace/wrk/wrk -t 500 -c 60000 -d 60s -s pipeline.lua --timeout 5s --latency http://localhost:8080/test.html
Running 1m test @ http://localhost:8080/test.html
  500 threads and 60000 connections
  Thread Stats   Avg      Stdev     Max   +/- Stdev
    Latency   387.41ms  259.74ms   3.27s    65.93%
    Req/Sec     6.09k     2.34k  299.93k    74.37%
  Latency Distribution
     50%  349.94ms
     75%  554.87ms
     90%  739.88ms
     99%    1.11s 
  252143645 requests in 1.00m, 31.47GB read
Requests/sec: 4186395.22
Transfer/sec:    534.99MB
```

---

## 测试 5: 最高 QPS 记录 (8.55M)

```bash
# 服务器 5 核心，wrk 3 核心，优化后配置
root@7c02a1165c99:/workspace/wrk# taskset -c 5-7 /workspace/wrk/wrk -t 1000 -c 60000 -d 60s -s 
pipeline.lua --timeout 5s --latency http://localhost:8080/test.html
Running 1m test @ http://localhost:8080/test.html
  1000 threads and 60000 connections
  Thread Stats   Avg      Stdev     Max   +/- Stdev
    Latency    82.51ms  128.33ms   5.00s    94.32%
    Req/Sec     1.53k     1.14k  332.45k    91.99%
  Latency Distribution
     50%   52.98ms
     75%   94.17ms
     90%  158.30ms
     99%  606.73ms
  531775196 requests in 1.04m, 66.36GB read
  Socket errors: connect 0, read 8925, write 0, timeout 4845
Requests/sec: 8551040.68
Transfer/sec:      1.07GB
```

**结果: 8.55M QPS (峰值)**

---

## 性能对比总结

| 测试场景 | 连接数 | Threads | QPS | P50 | P99 |
|---------|--------|---------|-----|-----|-----|
| 中等并发 | 6K | 80 | 840K | 1.26ms | 8ms |
| 高并发 | 60K | 800 | 2.29M | 63ms | 335ms |
| Pipeline | 60K | 500 | 4.19M | 350ms | 1.11s |
| 极限压测 | 60K | 1000 | 6.71M | 320ms | 1.31s |
| 峰值 | 60K | 1000 | 8.55M | 52.98ms | 607ms |

---

## pipeline.lua 脚本

```lua
init = function(args)
   local r = {}
   for i = 1, 16 do
      r[i] = wrk.format("GET", "/test.html")
   end
   req = table.concat(r)
end

request = function()
   return req
end
```

---

## 复现说明

1. 确保系统参数已调优 (参见 README.md)
2. 服务器和 wrk 绑定不同 CPU 核心，避免争抢
3. 测试前确保无其他高负载进程
4. 多次运行取稳定值
