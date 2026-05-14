#include "processor.h"
#include "event.pb.h"
#include "storage.h"
#include "metrics.h"
#include <spdlog/spdlog.h>
#include <chrono>
#include <atomic>
#include <memory>
#include <cstdlib>
#include <ctime>

Processor::Processor(ThreadSafeQueue& q, Metrics& m) : queue_(q), metrics_(m) {
    const char* kafka_bootstrap = std::getenv("EVENT_COLLECTOR_KAFKA_BOOTSTRAP");
    if (kafka_bootstrap && kafka_bootstrap[0] != '\0') {
        rd_kafka_conf_t* conf = rd_kafka_conf_new();
        char errstr[512] = {0};
        if (rd_kafka_conf_set(conf, "bootstrap.servers", kafka_bootstrap, errstr, sizeof(errstr)) != RD_KAFKA_CONF_OK) {
            spdlog::warn("Kafka config error (bootstrap): {}", errstr);
        }
        if (rd_kafka_conf_set(conf, "acks", "1", errstr, sizeof(errstr)) != RD_KAFKA_CONF_OK) {
            spdlog::warn("Kafka config error (acks): {}", errstr);
        }
        if (rd_kafka_conf_set(conf, "compression.codec", "snappy", errstr, sizeof(errstr)) != RD_KAFKA_CONF_OK) {
            spdlog::warn("Kafka config error (compression): {}", errstr);
        }

        producer_ = rd_kafka_new(RD_KAFKA_PRODUCER, conf, errstr, sizeof(errstr));
        if (!producer_) {
            spdlog::warn("Kafka producer init failed: {}", errstr);
        } else {
            kafka_topic_ = rd_kafka_topic_new(producer_, "event_stream", nullptr);
            spdlog::info("Kafka enabled, bootstrap={} topic=event_stream", kafka_bootstrap);
        }
    } else {
        spdlog::info("Kafka disabled (set EVENT_COLLECTOR_KAFKA_BOOTSTRAP to enable)");
    }

    const char* ck = std::getenv("EVENT_COLLECTOR_ENABLE_CLICKHOUSE");
    if (ck && std::string(ck) == "1") {
        try {
            storage_ = std::make_unique<Storage>(metrics_);
            spdlog::info("ClickHouse enabled");
        } catch (const std::exception& e) {
            spdlog::warn("ClickHouse init failed: {}", e.what());
        }
    } else {
        spdlog::info("ClickHouse disabled (set EVENT_COLLECTOR_ENABLE_CLICKHOUSE=1 to enable)");
    }

    const char* redis_enable = std::getenv("EVENT_COLLECTOR_REDIS_ENABLE");
    if (redis_enable && std::string(redis_enable) == "1") {
        const char* redis_host = std::getenv("EVENT_COLLECTOR_REDIS_HOST");
        const char* redis_port = std::getenv("EVENT_COLLECTOR_REDIS_PORT");
        const char* rate_str   = std::getenv("EVENT_COLLECTOR_RATE_LIMIT");
        std::string host = redis_host ? redis_host : "127.0.0.1";
        int port         = redis_port ? std::stoi(redis_port) : 6379;
        uint64_t limit   = rate_str   ? std::stoull(rate_str) : 100;
        try {
            rate_limiter_ = std::make_unique<RateLimiter>(host, port, limit);
            spdlog::info("Redis rate limiter enabled, limit={}/min per user", limit);
        } catch (const std::exception& e) {
            spdlog::warn("Redis rate limiter init failed: {}", e.what());
        }
    } else {
        spdlog::info("Redis rate limiter disabled (set EVENT_COLLECTOR_REDIS_ENABLE=1 to enable)");
    }
}

Processor::~Processor() {
    if (kafka_topic_) { rd_kafka_topic_destroy(kafka_topic_); kafka_topic_ = nullptr; }
    if (producer_) { rd_kafka_destroy(producer_); producer_ = nullptr; }
}

void Processor::start(size_t thread_count) {
    for (size_t i = 0; i < thread_count; ++i) {
        threads_.emplace_back(&Processor::worker, this);
    }
    spdlog::info("Processor started with {} worker threads", thread_count);
}

void Processor::stop() {
    running_ = false;
    for (auto& t : threads_) if (t.joinable()) t.join();

    std::string data;
    while (queue_.try_pop(data)) {
        metrics_.total_received.fetch_add(1);
        event::Event evt;
        if (evt.ParseFromString(data)) {
            metrics_.total_parsed.fetch_add(1);
            if (validate(evt)) {
                metrics_.total_valid.fetch_add(1);
                if (storage_) storage_->save(data);
            } else {
                metrics_.total_invalid.fetch_add(1);
            }
        } else {
            metrics_.total_parse_fail.fetch_add(1);
        }
    }

    if (storage_) storage_->flush_and_stop();
    spdlog::info("Processor stopped. Final: received={} parsed={} valid={} invalid={} rate_limited={}",
        metrics_.total_received.load(), metrics_.total_parsed.load(),
        metrics_.total_valid.load(), metrics_.total_invalid.load(),
        metrics_.total_rate_limited.load());
}

bool Processor::validate(const event::Event& evt) {
    if (evt.user_id().empty()) return false;
    if (evt.event_type().empty()) return false;
    if (evt.ts() <= 0) return false;
    auto now = std::time(nullptr) * 1000;
    if (evt.ts() > now + 86400000LL || evt.ts() < now - 86400000LL * 30) return false;
    return true;
}

void Processor::produce_to_kafka(const std::string& key, const std::string& data) {
    if (!producer_ || !kafka_topic_) return;
    int err = rd_kafka_produce(
        kafka_topic_, RD_KAFKA_PARTITION_UA, RD_KAFKA_MSG_F_COPY,
        (void*)data.data(), data.size(),
        key.data(), key.size(), nullptr);
    if (err != 0) {
        metrics_.total_kafka_fail.fetch_add(1);
    } else {
        metrics_.total_kafka_ok.fetch_add(1);
    }
}

void Processor::worker() {
    while (running_) {
        auto data = queue_.try_pop_for(std::chrono::milliseconds(500));
        if (!data) continue;

        metrics_.total_received.fetch_add(1);

        event::Event evt;
        if (!evt.ParseFromString(*data)) {
            metrics_.total_parse_fail.fetch_add(1);
            spdlog::debug("protobuf parse failed, bytes={}", data->size());
            continue;
        }
        metrics_.total_parsed.fetch_add(1);

        if (!validate(evt)) {
            metrics_.total_invalid.fetch_add(1);
            spdlog::debug("validation failed: user={} type={} ts={}",
                evt.user_id(), evt.event_type(), evt.ts());
            continue;
        }
        metrics_.total_valid.fetch_add(1);

        if (rate_limiter_ && !rate_limiter_->allow(evt.user_id())) {
            metrics_.total_rate_limited.fetch_add(1);
            spdlog::debug("rate limited: user={}", evt.user_id());
            continue;
        }

        produce_to_kafka(evt.user_id(), *data);

        if (storage_) {
            storage_->save(*data);
        }

        spdlog::debug("ok id={} type={} user={}", evt.event_id(), evt.event_type(), evt.user_id());
    }
}
