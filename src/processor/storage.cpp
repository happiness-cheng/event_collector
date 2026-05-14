#include "storage.h"
#include <clickhouse/client.h>
#include <spdlog/spdlog.h>
#include <fstream>
#include <chrono>
#include <sys/stat.h>
#include <ctime>

Storage::Storage(Metrics& m) : metrics_(m) {
    clickhouse::ClientOptions opts;
    opts.SetHost("localhost");
    opts.SetPort(9000);
    client_ = std::make_unique<clickhouse::Client>(opts);
    spdlog::info("Storage initialized (batch_size={})", BATCH_SIZE);
}

Storage::~Storage() {
    flush_and_stop();
}

bool Storage::save(const std::string& serialized_data) {
    std::vector<std::string> batch;
    {
        std::lock_guard<std::mutex> lock(buffer_mutex_);
        buffer_.push_back(serialized_data);
        if (buffer_.size() < BATCH_SIZE) return true;
        batch.swap(buffer_);
    }
    do_flush(batch);
    return true;
}

void Storage::flush_and_stop() {
    std::vector<std::string> batch;
    {
        std::lock_guard<std::mutex> lock(buffer_mutex_);
        if (buffer_.empty()) return;
        batch.swap(buffer_);
    }
    do_flush(batch);
}

void Storage::do_flush(std::vector<std::string>& batch) {
    if (batch.empty()) return;

    clickhouse::Block block;
    auto col = std::make_shared<clickhouse::ColumnString>();
    for (auto& d : batch) col->Append(d);
    block.AppendColumn("payload", col);

    for (int retry = 0; retry < 3; ++retry) {
        try {
            client_->Insert("events", block);
            metrics_.total_stored.fetch_add(batch.size());
            spdlog::info("ClickHouse batch write OK, rows={}", batch.size());
            return;
        } catch (const std::exception& e) {
            spdlog::warn("ClickHouse write fail (retry={}): {}", retry, e.what());
        }
    }

    write_dead_letter(batch);
}

void Storage::write_dead_letter(const std::vector<std::string>& batch) {
    static const std::string log_path = "dead_letter.log";
    struct stat st;
    if (stat(log_path.c_str(), &st) == 0 && st.st_size > 100 * 1024 * 1024) {
        std::string rotated = log_path + "." + std::to_string(std::time(nullptr));
        std::rename(log_path.c_str(), rotated.c_str());
        spdlog::info("Rotated dead_letter.log -> {}", rotated);
    }

    std::ofstream f(log_path, std::ios::app);
    if (!f.is_open()) {
        spdlog::error("Cannot open dead_letter.log, {} events lost", batch.size());
        return;
    }
    for (auto& d : batch) {
        f << d << "\n---\n";
    }
    f.close();
    metrics_.total_store_dead.fetch_add(batch.size());
    spdlog::error("{} events written to dead_letter.log", batch.size());
}
