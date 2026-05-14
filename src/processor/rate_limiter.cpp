#include "rate_limiter.h"
#include <sw/redis++/redis++.h>
#include <spdlog/spdlog.h>

RateLimiter::RateLimiter(const std::string& host, int port, uint64_t limit_per_min)
    : limit_per_min_(limit_per_min) {
    sw::redis::ConnectionOptions opts;
    opts.host = host;
    opts.port = port;
    redis_ = std::make_unique<sw::redis::Redis>(opts);
    spdlog::info("RateLimiter connected to {}:{}, limit={}/min", host, port, limit_per_min_);
}

bool RateLimiter::allow(const std::string& user_id) {
    std::string key = "rl:" + user_id;
    try {
        const std::string script = R"(
            local cnt = redis.call('INCR', KEYS[1])
            if cnt == 1 then
                redis.call('EXPIRE', KEYS[1], ARGV[1])
            end
            return cnt
        )";
        long long cnt = redis_->eval<long long>(script, {key}, {std::to_string(WINDOW_SECS)});
        return static_cast<uint64_t>(cnt) <= limit_per_min_;
    } catch (const std::exception& e) {
        spdlog::warn("Redis rate check failed, pass-through: {}", e.what());
        return true;
    }
}
