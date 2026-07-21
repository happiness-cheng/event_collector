#include "processor.h"
#include "event.pb.h"
#include "validation.h"
#include "storage.h"
#include "metrics.h"
#include <spdlog/spdlog.h>
#include <chrono>
#include <atomic>
#include <memory>
#include <array>
#include <cstdlib>
#include <ctime>

// 前向声明：delivery report 回调（定义在文件末尾）
static void dr_msg_cb(rd_kafka_t*, const rd_kafka_message_t*, void*);

Processor::Processor(ThreadSafeQueue& q, Metrics& m) : queue_(q), metrics_(m) {
    const char* kafka_bootstrap = std::getenv("EVENT_COLLECTOR_KAFKA_BOOTSTRAP");
    if (kafka_bootstrap && kafka_bootstrap[0] != '\0') {
        rd_kafka_conf_t* conf = rd_kafka_conf_new();
        rd_kafka_conf_set_dr_msg_cb(conf, dr_msg_cb);
        std::array<char, 512> errstr{};
        if (rd_kafka_conf_set(conf, "bootstrap.servers", kafka_bootstrap, errstr.data(), errstr.size()) != RD_KAFKA_CONF_OK) {
            spdlog::warn("Kafka config error (bootstrap): {}", errstr.data());
            rd_kafka_conf_destroy(conf);
        } else if (rd_kafka_conf_set(conf, "acks", "1", errstr.data(), errstr.size()) != RD_KAFKA_CONF_OK) {
            spdlog::warn("Kafka config error (acks): {}", errstr.data());
            rd_kafka_conf_destroy(conf);
        } else if (rd_kafka_conf_set(conf, "compression.codec", "snappy", errstr.data(), errstr.size()) != RD_KAFKA_CONF_OK) {
            spdlog::warn("Kafka config error (compression): {}", errstr.data());
            rd_kafka_conf_destroy(conf);
        } else if (rd_kafka_conf_set(conf, "linger.ms", "5", errstr.data(), errstr.size()) != RD_KAFKA_CONF_OK) {
            spdlog::warn("Kafka config error (linger.ms): {}", errstr.data());
            rd_kafka_conf_destroy(conf);
        } else if (rd_kafka_conf_set(conf, "batch.num.messages", "1000", errstr.data(), errstr.size()) != RD_KAFKA_CONF_OK) {
            spdlog::warn("Kafka config error (batch.num.messages): {}", errstr.data());
            rd_kafka_conf_destroy(conf);
        } else {
            producer_ = rd_kafka_new(RD_KAFKA_PRODUCER, conf, errstr.data(), errstr.size());
            if (!producer_) {
                spdlog::warn("Kafka producer init failed: {}", errstr.data());
            } else {
                const char* kafka_topic_name = std::getenv("EVENT_COLLECTOR_KAFKA_TOPIC");
                std::string topic_name = (kafka_topic_name && kafka_topic_name[0]) ? kafka_topic_name : "event_stream";
                kafka_topic_ = rd_kafka_topic_new(producer_, topic_name.c_str(), nullptr);
                spdlog::info("Kafka enabled, bootstrap={} topic={}", kafka_bootstrap, topic_name);
            }
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
    // Kafka poll 线程：独立刷新 producer 缓冲 + 处理 delivery report
    if (producer_) {
        threads_.emplace_back([this]() {
            while (running_) {
                rd_kafka_poll(producer_, 10);  // 等最多 10ms，处理所有 pending delivery report
            }
        });
        spdlog::info("Kafka poll thread started");
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
                produce_to_kafka(evt.user_id(), data);
                if (storage_) storage_->save(data);
            } else {
                metrics_.total_invalid.fetch_add(1);
            }
        } else {
            metrics_.total_parse_fail.fetch_add(1);
        }
    }

    if (storage_) storage_->flush_and_stop();

    // 优雅关闭：排空队列后，等 Kafka 内部缓冲真正 flush 出去再退，
    // 避免最后一批事件只入了内部缓冲(produce返回0≠到达broker)就进程退出导致丢失
    if (producer_) {
        const int flush_ms = 10000;
        auto remaining = rd_kafka_flush(producer_, flush_ms);
        if (remaining != 0) {
            spdlog::warn("Kafka flush timeout, {} messages still in buffer, forcing purge", remaining);
            rd_kafka_purge(producer_, RD_KAFKA_PURGE_F_QUEUE);
        } else {
            spdlog::info("Kafka buffer flushed, all messages delivered before shutdown");
        }
    }

    spdlog::info("Processor stopped. Final: received={} parsed={} valid={} invalid={} rate_limited={}",
        metrics_.total_received.load(), metrics_.total_parsed.load(),
        metrics_.total_valid.load(), metrics_.total_invalid.load(),
        metrics_.total_rate_limited.load());
}

bool Processor::validate(const event::Event& evt) {
    return validate_event(evt);
}

// Delivery report callback：消息真正到达 broker 后才计数
static void dr_msg_cb(rd_kafka_t*, const rd_kafka_message_t* rkmsg, void* opaque) {
    auto* m = static_cast<Metrics*>(opaque);
    if (rkmsg->err == RD_KAFKA_RESP_ERR_NO_ERROR) {
        m->total_kafka_ok.fetch_add(1);
    } else {
        m->total_kafka_fail.fetch_add(1);
    }
}

void Processor::produce_to_kafka(const std::string& key, const std::string& data) {
    if (!producer_ || !kafka_topic_) return;
    int err = rd_kafka_produce(
        kafka_topic_, RD_KAFKA_PARTITION_UA, RD_KAFKA_MSG_F_COPY,
        (void*)data.data(), data.size(),
        key.data(), key.size(), &metrics_);
    if (err != 0) {
        // 内部缓冲满，触发一次 poll 清理后重试
        rd_kafka_poll(producer_, 0);
        err = rd_kafka_produce(
            kafka_topic_, RD_KAFKA_PARTITION_UA, RD_KAFKA_MSG_F_COPY,
            (void*)data.data(), data.size(),
            key.data(), key.size(), &metrics_);
        if (err != 0) metrics_.total_kafka_fail.fetch_add(1);
    }
    // 入队成功：等后台 poll 线程触发 delivery report 回调计数
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
