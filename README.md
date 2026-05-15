# event_collector

高并发 TCP 事件采集服务，基于 C++17 + Boost.Asio 异步 I/O 框架。

## 技术栈

- **C++17** / **CMake**
- **Boost.Asio** — 异步 TCP 网络 I/O
- **Protobuf** — 事件序列化/反序列化
- **Redis** — 固定窗口分布式限流（Lua 脚本保证原子性）
- **Kafka** (librdkafka) — 异步消息队列，解耦上下游
- **ClickHouse** — 列式分析数据库，批量写入
- **Prometheus** — 指标端点，支持 Grafana 监控

## 架构

```
客户端 → TCP :8080 → Collector → Queue → Processor (×4 workers)
                                       ├→ Redis 限流
                                       ├→ Kafka 发送
                                       └→ ClickHouse 存储

Prometheus → HTTP :9090 → Monitor (11 个 Counter 指标)
```

线程模型：2 个 I/O 线程 (io_context) + 4 个 Worker 线程

## 快速启动

```bash
mkdir build && cd build
cmake -G 'Unix Makefiles' ..
make -j$(nproc)
./server

# 或使用启动脚本
./start.sh
```

服务启动后：
- `:8080` — 事件采集端口（Protobuf over TCP）
- `:9090` — Prometheus 指标端点

## 环境变量

| 变量 | 默认值 | 说明 |
|------|--------|------|
| `EVENT_COLLECTOR_KAFKA_BOOTSTRAP` | (空) | Kafka broker 地址，未设置则禁用 |
| `EVENT_COLLECTOR_ENABLE_CLICKHOUSE` | `0` | 设为 `1` 启用 ClickHouse 存储 |
| `EVENT_COLLECTOR_REDIS_ENABLE` | `0` | 设为 `1` 启用 Redis 限流 |
| `EVENT_COLLECTOR_REDIS_HOST` | `127.0.0.1` | Redis 主机 |
| `EVENT_COLLECTOR_REDIS_PORT` | `6379` | Redis 端口 |
| `EVENT_COLLECTOR_RATE_LIMIT` | `100` | 每用户每分钟限流阈值 |

三个中间件均为可选组件，未配置时服务以降级模式运行。

## 测试

```bash
# 集成测试：启动服务 → 发送事件 → 验证 Prometheus 指标 → 优雅退出
./test_integration.sh

# 编译并运行队列单元测试
g++ -std=c++17 -pthread -Iinclude -o tests/test_queue tests/test_queue.cpp && ./tests/test_queue

# 手动测试
python3 bench_client.py --count 100 --threads 2
curl http://localhost:9090
```

## 性能测试

详细测试报告见 [性能测试报告.md](./性能测试报告.md)。

**核心指标**：
- 峰值 QPS：18,708（单组件）/ 13,707（全开）
- P50 延迟：2.4-3.0ms
- 5 分钟稳定性：200 万事件，11MB 内存，无泄漏

## 协议

Length-Prefix Protocol：4 字节小端序头部（payload 长度）+ protobuf 序列化包体。

```
| uint32_t len (LE) | protobuf bytes |
```

## 项目结构

```
├── proto/event.proto          # Protobuf 消息定义
├── include/                   # 头文件
├── src/
│   ├── main.cpp               # 入口 + 线程布局 + 信号处理
│   ├── collector/             # TCP 接收模块
│   ├── processor/             # 事件处理 + 限流 + 存储
│   └── monitor/               # Prometheus 指标
├── tests/test_queue.cpp       # 队列单元测试
├── test_integration.sh        # 集成测试脚本
├── bench_client.py            # Python 压测工具
├── start.sh                   # Linux 启动脚本
└── start.bat                  # Windows 启动脚本
```
