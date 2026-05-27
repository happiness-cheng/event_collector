# Event Collector

[![CI](https://github.com/happiness-cheng/event_collector/actions/workflows/ci.yml/badge.svg)](https://github.com/happiness-cheng/event_collector/actions/workflows/ci.yml)

**简体中文** | [English](./README_en.md)

> 单机峰值 **480,000+ QPS**，生产环境 **1.44 亿事件** 累计处理，C epoll 客户端 **318K QPS** 500 并发 0 丢包，8 小时连续运行零容器崩溃。已部署 Azure 云端并通过全场景压测。

基于 Boost.Asio 的高并发 TCP 事件采集服务，接收 Protobuf 序列化事件，经过限流后异步写入 Kafka 和 ClickHouse，通过 Prometheus 暴露运行指标。

## 目录

- [架构与技术选型](#架构与技术选型)
- [快速开始](#快速开始)
- [配置](#配置)
- [性能数据](#性能数据)
- [踩坑记录](#踩坑记录)
- [已知限制](#已知限制)
- [项目结构](#项目结构)
- [测试](#测试)

## 架构与技术选型

### 数据流

```
Client (Python/C epoll)
  │  TCP :8080, 4-byte LE header + Protobuf body
  ▼
Boost.Asio Acceptor (2 IO threads)
  │  async_accept → async_read_header → async_read_body
  │  tcp_no_delay, 5s idle timeout, MAX_CONNECTIONS 20000
  ▼
BoundedQueue (cap 10000, mutex + condition_variable)
  │  背压：满时阻塞 producer，防止 OOM
  ▼
Worker Pool (8 threads)
  ├── Redis Lua 限流 (INCR + EXPIRE 原子操作)
  ├── Kafka Produce (librdkafka, dr_msg_cb 确认)
  └── ClickHouse 批量写入 (3 次重试 + dead_letter.log)
  │
  ▼
Prometheus Metrics (:9090) ── 12 个 Counter 覆盖全链路
```

### 为什么选这些组件

| 决策点 | 选择 | 放弃的选项 | 理由 |
|--------|------|-----------|------|
| 网络框架 | Boost.Asio | libevent, muduo | Proactor 模型，C++ 原生，成熟稳定 |
| 序列化 | Protobuf (proto3) | JSON, FlatBuffers | 二进制紧凑，比 JSON 省 60% 带宽，解析快 10 倍 |
| 消息队列 | Kafka (librdkafka) | RabbitMQ, NATS | 持久化 + 水平扩展 + 生态成熟 |
| 限流 | Redis Lua 脚本 | 本地令牌桶 | 分布式一致性，单线程原子性 |
| 存储 | ClickHouse | PostgreSQL, Doris | 列式存储，高压缩比，分析查询快 |
| 监控 | Prometheus 自定义端点 | StatsD, OpenTelemetry | 零依赖，HTTP 端点直接暴露 |

### 降级设计

所有外部依赖均可选——未配置时降级运行，保证核心链路不中断：

| 组件 | 启用 | 未配置时行为 |
|------|------|-------------|
| Kafka | `EVENT_COLLECTOR_KAFKA_BOOTSTRAP` 非空 | 跳过 Kafka 投递，事件仅进队列 |
| ClickHouse | `EVENT_COLLECTOR_ENABLE_CLICKHOUSE=1` | 跳过 ClickHouse 写入 |
| Redis 限流 | `EVENT_COLLECTOR_REDIS_ENABLE=1` | 跳过限流检查，全部放行 |

## 快速开始

### Docker（推荐）

```bash
git clone https://github.com/happiness-cheng/event_collector.git
cd event_collector
docker compose up -d
```

### 手动编译

```bash
# 安装依赖（Ubuntu/Debian）
sudo apt install -y build-essential cmake libboost-all-dev \
    libprotobuf-dev protobuf-compiler \
    librdkafka-dev libhiredis-dev libssl-dev

git clone https://github.com/happiness-cheng/event_collector.git
cd event_collector
mkdir build && cd build
cmake -G 'Unix Makefiles' ..
make -j$(nproc)
./server
```

服务启动后：
- `:8080` — 事件采集端口（Protobuf over TCP）
- `:9090` — Prometheus 指标端点

## 配置

| 环境变量 | 默认值 | 说明 |
|----------|--------|------|
| `EVENT_COLLECTOR_KAFKA_BOOTSTRAP` | 空 | Kafka broker 地址（空 = 禁用） |
| `EVENT_COLLECTOR_ENABLE_CLICKHOUSE` | `0` | 设为 `1` 启用 ClickHouse |
| `EVENT_COLLECTOR_REDIS_ENABLE` | `0` | 设为 `1` 启用 Redis 限流 |
| `EVENT_COLLECTOR_REDIS_HOST` | `127.0.0.1` | Redis 地址 |
| `EVENT_COLLECTOR_RATE_LIMIT` | `100` | 每用户每分钟限流阈值 |

## 性能数据

> 详见 [perf_report.md](./perf_report.md) 获取完整性能测试报告。

### 云端环境

| 角色 | 平台 | 区域 | 配置 |
|------|------|------|------|
| 服务器 | Azure VM (B2s) | Southeast Asia | 2 核 Intel Xeon Platinum 8370C, 3.8GB RAM, Ubuntu 24.04 |
| 客户端 | DigitalOcean Droplet | Singapore | 1 vCPU, 1GB RAM, Ubuntu 24.04 |
| 中间件 | Docker | 同服务器 | Redis 7 + Kafka 7.7 + ClickHouse 24.3 |

### 生产数据

| 指标 | 数据 |
|------|------|
| 累计处理事件 | **1.44 亿** |
| 峰值 QPS | **480,000** |
| 解析失败率 | **0%** |
| Kafka 写入成功率 | **99.99%** |
| 连续运行时长 | **8 小时** |
| 容器崩溃次数 | **0** |
| C epoll 客户端峰值 | **318,000 QPS**（500 连接，0% 丢包） |
| 每核吞吐 | **24 万 QPS/core** |

### 压测数据（单机，客户端=服务器）

| 测试 | 事件数 | 线程 | QPS | P50 | P99 | 丢包 |
|------|--------|------|-----|-----|-----|------|
| 线程梯度-50 | 50K | 50 | 47,818 | 0.01ms | 18.14ms | 0% |
| **线程梯度-100** | **50K** | **100** | **53,139** | **0.01ms** | **17.19ms** | **0%** |
| 100K 事件 | 100K | 100 | 36,177 | 0.01ms | 36.05ms | 0% |
| 崩溃测试 | 200K | 5000 | 20,768 | — | — | 0% |
| **1 小时稳定性** | **8277 万** | **50** | **~23K** | — | — | **0%** |

### Worker × IO 梯度（多机，10 万事件）

| Worker | IO | QPS | P99 | Drop |
|--------|-----|------|------|------|
| 4 | 1 | 13,017 | 37.93ms | 0% |
| **8** | **2** | **17,282** | **9.13ms** | **0%** |
| 16 | 2 | 10,398 | 41.76ms | 0% |

**最优配置：Worker=8, IO=2**。IO=2 比 IO=1 提升 32%，Worker=8 充分利用 2vCPU 超线程。

### 与同类工具对比

| 工具 | 语言 | 单机吞吐 (参考) | 资源占用 | 适用场景 |
|------|------|----------------|---------|---------|
| **event_collector** | C++ | 48 万 QPS (2 核) | ~2.4GB | 轻量级高并发采集 |
| Fluentd | Ruby/C | ~5 万 QPS | ~500MB | 通用日志收集，插件丰富 |
| Vector (Datadog) | Rust | ~30 万 QPS | ~200MB | 高性能日志/metrics |
| Filebeat | Go | ~10 万 QPS | ~100MB | 日志采集，ELK 生态 |
| Logstash | Java | ~2 万 QPS | ~1GB+ | 功能最全，资源最重 |

> 注：吞吐数据来自各工具官方 benchmark 和社区测试，硬件条件不同，仅供趋势参考。

## 踩坑记录

Azure 云端 8 小时连续运行中发现并解决的关键问题：

### Kafka producer 虚假成功

**现象：** Collector 报告 `kafka_ok_total = 500000`，但 Kafka topic offset 不变，Engine 无数据消费。
**根因：** `rd_kafka_produce()` 返回 0 仅表示「入内部缓冲区」，不代表到达 broker。
**解决：** 添加 `dr_msg_cb` delivery report callback，只在实际投递成功时计数。
**效果：** kafka_ok 反映真实投递率（96-100%）。

### Collector SIGSEGV 崩溃（exit 139）

**现象：** 高并发下每 1-2 分钟崩溃一次，`--restart=always` 自动重启。
**根因：** `async_accept` 回调中 `sock.remote_endpoint()` 在客户端快速断开时抛未捕获异常。
**解决：** 用 `error_code` 重载替代抛异常 + 整个回调包 try-catch。
**效果：** 0 重启。

### Kafka producer 性能瓶颈

**现象：** 队列丢弃 1300 万事件/小时，8 个 worker 线程竞争 `rd_kafka_poll` 锁。
**根因：** 每个 worker produce 后立即 poll，阻塞其他 worker。
**解决：** `rd_kafka_poll` 移到独立后台线程 + `linger.ms=5` + `batch.num.messages=1000`。
**效果：** 队列丢弃从百万级降到 0。

> 完整 12 个问题记录见 [perf_report.md](./perf_report.md)。

## 已知限制

| 限制 | 影响 | 改进方向 |
|------|------|---------|
| 测试覆盖率低（仅队列模块） | 核心逻辑无单测 | 补 validate/produce/storage 测试 |
| 无 health check 端点 | K8s 无法做 liveness probe | 加 `/health` 和 `/ready` |
| 配置分散在各处的 `getenv()` | 难以维护和校验 | 统一 Config 结构体 + YAML 支持 |
| dead_letter.log 是本地文件 | 多实例时无法共享 | 改为对象存储（S3/OSS） |
| 优雅关停无超时 | 队列积压时 shutdown 卡住 | 加 30 秒 drain 超时 |

## 项目结构

```
event_collector/
├── proto/event.proto           # Protobuf 消息定义
├── include/
│   ├── queue.h                 # 线程安全有界队列
│   └── metrics.h               # Atomic 计数器（12 个指标）
├── src/
│   ├── main.cpp                # 入口 + 信号处理 + 优雅关停
│   ├── collector/
│   │   └── collector.cpp       # Boost.Asio TCP acceptor + Session 管理
│   ├── processor/
│   │   ├── processor.cpp       # Protobuf 解析 + Kafka produce + Worker pool
│   │   ├── storage.cpp         # ClickHouse 批量写入 + dead_letter 落盘
│   │   └── rate_limiter.cpp    # Redis Lua 限流
│   └── monitor/
│       └── monitor.cpp         # Prometheus /metrics 端点
├── tests/test_queue.cpp        # 队列单元测试
├── bench_client.py             # Python 压测客户端
├── C epoll 压测客户端          # C 语言原生 epoll 压测
├── docker-compose.yml          # 一键部署
├── Dockerfile                  # 多阶段构建
└── start.bat                   # Windows 启动脚本
```

## 测试

```bash
# 队列测试
g++ -std=c++17 -pthread -Iinclude -o tests/test_queue tests/test_queue.cpp && ./tests/test_queue
```

## License

MIT
