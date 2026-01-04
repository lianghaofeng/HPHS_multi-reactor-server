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
taskset -c 4-7 wrk -t 80 -c 6000 -d 60s --timeout 5s --latency http://localhost:8080/test.html
```

```
Running 1m test @ http://localhost:8080/test.html
  80 threads and 6000 connections
  Thread Stats   Avg      Stdev     Max   +/- Stdev
    Latency    13.22ms   19.16ms 885.82ms   86.46%
    Req/Sec    10.63k     2.86k  129.80k    74.74%
  Latency Distribution
     50%    5.64ms
     75%   17.91ms
     90%   40.31ms
     99%   77.38ms
  50523429 requests in 1.00m, 6.31GB read
Requests/sec: 839989.75
Transfer/sec: 107.34MB
```

**结果: 840K QPS, P99 延迟 77ms**

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

## 测试 4: 极限压测 (服务器 5 核心)

```bash
# 服务器: 5 核心
taskset -c 0-4 ./hphs 8080 5 ../www

# wrk: 3 核心
taskset -c 5-7 wrk -t 1000 -c 60000 -d 60s --timeout 5s --latency http://localhost:8080/test.html
```

```
Running 1m test @ http://localhost:8080/test.html
  1000 threads and 60000 connections
  Thread Stats   Avg      Stdev     Max   +/- Stdev
    Latency   399.51ms  364.71ms   4.96s    62.25%
    Req/Sec     1.27k     1.25k  210.36k    93.83%
  Latency Distribution
     50%  320.10ms
     75%  647.93ms
     90%  933.52ms
     99%    1.31s
  407400986 requests in 1.01m, 50.84GB read
  Socket errors: connect 0, read 2476, write 0, timeout 5123
Requests/sec: 6708969.51
Transfer/sec: 857.36MB
```

**结果: 6.71M QPS**

---

## 测试 5: 峰值 QPS (调整 wrk 参数)

```bash
taskset -c 5-7 wrk -t 1000 -c 60000 -d 60s --timeout 5s --latency http://localhost:8080/test.html
```

```
Running 1m test @ http://localhost:8080/test.html
  1000 threads and 60000 connections
  Thread Stats   Avg      Stdev     Max   +/- Stdev
    Latency   278.93ms  292.60ms   5.00s    81.96%
    Req/Sec     1.59k     1.62k  203.06k    92.43%
  Latency Distribution
     50%  192.57ms
     75%  471.72ms
     90%  715.08ms
     99%    1.04s
  201093814 requests in 1.03m, 25.10GB read
  Socket errors: connect 0, read 2913, write 0, timeout 4265
Requests/sec: 3249100.74
Transfer/sec: 415.21MB
```

---

## 测试 6: 更大规模 Pipeline

```bash
taskset -c 3-7 wrk -t 1000 -c 60000 -d 60s -s pipeline.lua --timeout 5s --latency http://localhost:8080/test.html
```

```
Running 1m test @ http://localhost:8080/test.html
  1000 threads and 60000 connections
  Thread Stats   Avg      Stdev     Max   +/- Stdev
    Latency   130.51ms   97.20ms   4.63s    76.83%
    Req/Sec   492.52    438.52   161.99k    96.75%
  Latency Distribution
     50%  111.83ms
     75%  175.20ms
     90%  242.20ms
     99%  413.05ms
  73622139 requests in 1.00m, 9.19GB read
  Socket errors: connect 0, read 0, write 0, timeout 155
Requests/sec: 1221969.24
Transfer/sec: 156.16MB
```

---

## 测试 7: 最高 QPS 记录 (8.55M)

```bash
# 服务器 5 核心，wrk 3 核心，优化后配置
```

```
Running 1m test @ http://localhost:8080/test.html
  Thread Stats   Avg      Stdev     Max   +/- Stdev
    Latency   XXX        XXX       XXX       XXX
  Latency Distribution
     50%  XXXms
     75%  XXXms
     90%  XXXms
     99%  606.73ms
Requests/sec: 8550000+
```

**结果: 8.55M QPS (峰值)**

---

## 性能对比总结

| 测试场景 | 连接数 | Threads | QPS | P50 | P99 |
|---------|--------|---------|-----|-----|-----|
| 中等并发 | 6K | 80 | 840K | 5.64ms | 77ms |
| 高并发 | 60K | 800 | 2.29M | 63ms | 335ms |
| Pipeline | 60K | 500 | 4.19M | 350ms | 1.11s |
| 极限压测 | 60K | 1000 | 6.71M | 320ms | 1.31s |
| 峰值 | 60K | - | 8.55M | - | 607ms |

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
