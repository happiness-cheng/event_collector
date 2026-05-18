#include "rate_limiter.h"
#include <sw/redis++/redis++.h>
#include <spdlog/spdlog.h>

RateLimiter::RateLimiter(const std::string& host, int port, uint64_t limit_per_min)
    : limit_per_min_(limit_per_min) {
    sw::redis::ConnectionOptions opts;
    opts.host = host;
    opts.port = port;

    const char* redis_password = std::getenv("EVENT_COLLECTOR_REDIS_PASSWORD");
    if (redis_password && redis_password[0] != '\0') {
        opts.password = redis_password;
    }

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
        // fail-open：Redis 不可用时放行，避免 Redis 故障导致整个服务不可用
        // 如需 fail-closed 策略，将 return true 改为 return false
        return true;
    }
}
