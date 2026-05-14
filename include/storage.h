#pragma once
#include "metrics.h"
#include <memory>
#include <clickhouse/client.h>
#include <vector>
#include <string>
#include <mutex>

class Storage {
public:
    Storage(Metrics& m);
    ~Storage();
    bool save(const std::string& serialized_data);
    void flush_and_stop();

private:
    void flush();
    void write_dead_letter(const std::vector<std::string>& batch);

    std::unique_ptr<clickhouse::Client> client_;
    Metrics& metrics_;
    std::vector<std::string> buffer_;
    std::mutex buffer_mutex_;
    static constexpr size_t BATCH_SIZE = 1000;
};
