#pragma once
#include <atomic>
#include <cstdint>

struct Metrics {
    std::atomic<uint64_t> total_received{0};
    std::atomic<uint64_t> total_parsed{0};
    std::atomic<uint64_t> total_valid{0};
    std::atomic<uint64_t> total_invalid{0};
    std::atomic<uint64_t> total_parse_fail{0};
    std::atomic<uint64_t> total_rate_limited{0};
    std::atomic<uint64_t> total_kafka_ok{0};
    std::atomic<uint64_t> total_kafka_fail{0};
    std::atomic<uint64_t> total_stored{0};
    std::atomic<uint64_t> total_store_fail{0};
    std::atomic<uint64_t> total_store_dead{0};
    std::atomic<uint64_t> total_queue_drop{0};
};
