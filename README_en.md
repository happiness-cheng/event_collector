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

- **Peak QPS**: 18,708 (single component) / 13,707 (full stack)
- **P50 Latency**: 2.4-3.0ms
- **5-min stability**: 2M events, 11MB RSS, zero leaks

See [README_zh.md](./README_zh.md) for full documentation.

## License

MIT
