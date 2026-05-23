#include "storage.h"
#include "event.pb.h"
#include <clickhouse/client.h>
#include <clickhouse/columns/numeric.h>
#include <spdlog/spdlog.h>
#include <fstream>
#include <chrono>
#include <sys/stat.h>
#include <ctime>
#include <mutex>

static std::mutex dead_letter_mutex;

Storage::Storage(Metrics& m) : metrics_(m) {
    clickhouse::ClientOptions opts;

    const char* ch_host = std::getenv("EVENT_COLLECTOR_CLICKHOUSE_HOST");
    const char* ch_port = std::getenv("EVENT_COLLECTOR_CLICKHOUSE_PORT");
    opts.SetHost(ch_host ? ch_host : "localhost");
    opts.SetPort(ch_port ? std::stoi(ch_port) : 9000);
    opts.SetConnectionConnectTimeout(std::chrono::seconds(3));
    opts.SetConnectionRecvTimeout(std::chrono::seconds(3));
    opts.SetConnectionSendTimeout(std::chrono::seconds(3));
    client_ = std::make_unique<clickhouse::Client>(opts);
    spdlog::info("Storage initialized (batch_size={}, host={}:{})",
        BATCH_SIZE, opts.host, opts.port);
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
    auto event_id_col   = std::make_shared<clickhouse::ColumnString>();
    auto user_id_col    = std::make_shared<clickhouse::ColumnString>();
    auto event_type_col = std::make_shared<clickhouse::ColumnString>();
    auto platform_col   = std::make_shared<clickhouse::ColumnString>();
    auto ts_col         = std::make_shared<clickhouse::ColumnUInt64>();
    auto payload_col    = std::make_shared<clickhouse::ColumnString>();

    for (auto& d : batch) {
        event::Event evt;
        if (evt.ParseFromString(d)) {
            event_id_col->Append(evt.event_id());
            user_id_col->Append(evt.user_id());
            event_type_col->Append(evt.event_type());
            platform_col->Append(evt.platform());
            ts_col->Append(static_cast<uint64_t>(evt.ts()));
            payload_col->Append(evt.payload());
        } else {
            event_id_col->Append("");
            user_id_col->Append("");
            event_type_col->Append("");
            platform_col->Append("");
            ts_col->Append(0);
            payload_col->Append(d);
        }
    }

    block.AppendColumn("event_id", event_id_col);
    block.AppendColumn("user_id", user_id_col);
    block.AppendColumn("event_type", event_type_col);
    block.AppendColumn("platform", platform_col);
    block.AppendColumn("ts", ts_col);
    block.AppendColumn("payload", payload_col);

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
    std::lock_guard<std::mutex> lock(dead_letter_mutex);

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
