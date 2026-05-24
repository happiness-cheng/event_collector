#pragma once
#include "metrics.h"
#include <memory>
#include <clickhouse/client.h>
#include <vector>
#include <string>
#include <mutex>
#include <atomic>
#include <thread>

class Storage {
public:
    Storage(Metrics& m);
    ~Storage();
    bool save(const std::string& serialized_data);
    void flush_and_stop();

private:
    void do_flush(std::vector<std::string>& batch);
    void write_dead_letter(const std::vector<std::string>& batch);
    void periodic_flush();

    std::unique_ptr<clickhouse::Client> client_;
    Metrics& metrics_;
    std::vector<std::string> buffer_;
    std::mutex buffer_mutex_;
    static constexpr size_t BATCH_SIZE = 1000;
    static constexpr int FLUSH_INTERVAL_SECS = 5;
    std::atomic<bool> running_{true};
    std::thread flush_thread_;
};
