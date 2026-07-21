#pragma once
#include <memory>
#include <string>
#include <cstdint>
#include <sw/redis++/redis++.h>

class RateLimiter {
public:
    RateLimiter(const std::string& host, int port, uint64_t limit_per_min);
    bool allow(const std::string& user_id);
    [[nodiscard]] uint64_t limit() const { return limit_per_min_; }

private:
    std::unique_ptr<sw::redis::Redis> redis_;
    uint64_t limit_per_min_;
    static constexpr int WINDOW_SECS = 60;
};
