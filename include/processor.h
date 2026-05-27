#pragma once
#include "queue.h"
#include "metrics.h"
#include <thread>
#include <atomic>
#include <vector>
#include <librdkafka/rdkafka.h>
#include "storage.h"
#include "rate_limiter.h"

namespace event { class Event; }

class Processor {
public:
    Processor(ThreadSafeQueue& q, Metrics& m);
    ~Processor();
    void start(size_t thread_count = 4);
    void stop();

    // 验证事件字段合法性（暴露为 public 以支持单元测试）
    static bool validate(const event::Event& evt);

private:
    void produce_to_kafka(const std::string& key, const std::string& data);
    void worker();

    ThreadSafeQueue& queue_;
    Metrics& metrics_;
    std::vector<std::thread> threads_;
    std::atomic<bool> running_{true};
    rd_kafka_t* producer_ = nullptr;
    rd_kafka_topic_t* kafka_topic_ = nullptr;
    std::unique_ptr<Storage> storage_;
    std::unique_ptr<RateLimiter> rate_limiter_;
};
