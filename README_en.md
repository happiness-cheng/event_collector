High-concurrency TCP event collector built with C++17 and Boost.Asio. Receives Protobuf events, applies rate limiting, and writes asynchronously to Kafka and ClickHouse with Prometheus monitoring.

## Quick Start

```bash
git clone https://github.com/happiness-cheng/event_collector.git
cd event_collector
mkdir build && cd build && cmake .. && make -j$(nproc) && ./server
```

- `:8080` — Event ingestion (Protobuf over TCP)
- `:9090` — Prometheus metrics endpoint

## Tech Stack

C++17, Boost.Asio, Protobuf, Kafka, Redis, ClickHouse, Prometheus

## Key Metrics

### Production Data (Live)

- **Total Events Processed**: 144M+
- **Peak QPS**: 480,000
- **Parse Failure Rate**: 0%
- **Kafka Write Success Rate**: 99.99%
- **Continuous Uptime**: 8 hours, zero container crashes

### Benchmark Data (Test Environment)

- **Peak QPS (single-node)**: 53,139 (100 threads)
- **Peak QPS (multi-node)**: 24,859 (50 threads)
- **P50 Latency**: 0.01ms (multi-node)
- **C epoll Client Peak**: 318,000 QPS (500 connections, 0% loss)
- **1-hour Stability**: 82.77M events, 0% loss

See [README.md](./README.md) for full documentation.

## License

MIT
