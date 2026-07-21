#include <gtest/gtest.h>
#include "validation.h"
#include "metrics.h"
#include <ctime>
#include <thread>

event::Event make_valid_event() {
    event::Event evt;
    evt.set_event_id("evt_001");
    evt.set_user_id("user_42");
    evt.set_platform("web");
    evt.set_event_type("click");
    evt.set_ts(static_cast<int64_t>(std::time(nullptr)) * 1000);
    evt.set_payload(R"({"page": "/home"})");
    return evt;
}

auto now_ms = [] { return static_cast<int64_t>(std::time(nullptr)) * 1000; };

// ==================== 合法事件 ====================
TEST(ValidateEvent, ValidEvent_ReturnsTrue) {
    auto evt = make_valid_event();
    EXPECT_TRUE(validate_event(evt));
}

// ==================== user_id ====================
TEST(ValidateEvent, EmptyUserId_ReturnsFalse) {
    auto evt = make_valid_event();
    evt.set_user_id("");
    EXPECT_FALSE(validate_event(evt));
}

TEST(ValidateEvent, UserIdTooLong_ReturnsFalse) {
    auto evt = make_valid_event();
    evt.set_user_id(std::string(257, 'a'));  // > 256
    EXPECT_FALSE(validate_event(evt));
}

TEST(ValidateEvent, UserIdBoundary256_ReturnsTrue) {
    auto evt = make_valid_event();
    evt.set_user_id(std::string(256, 'a'));  // == 256
    EXPECT_TRUE(validate_event(evt));
}

// ==================== event_type ====================
TEST(ValidateEvent, EmptyEventType_ReturnsFalse) {
    auto evt = make_valid_event();
    evt.set_event_type("");
    EXPECT_FALSE(validate_event(evt));
}

// ==================== payload ====================
TEST(ValidateEvent, OversizedPayload_ReturnsFalse) {
    auto evt = make_valid_event();
    evt.set_payload(std::string(65537, 'x'));
    EXPECT_FALSE(validate_event(evt));
}

TEST(ValidateEvent, PayloadExactly65536_ReturnsTrue) {
    auto evt = make_valid_event();
    evt.set_payload(std::string(65536, 'x'));
    EXPECT_TRUE(validate_event(evt));
}

// ==================== ts ====================
TEST(ValidateEvent, TsZero_ReturnsFalse) {
    auto evt = make_valid_event();
    evt.set_ts(0);
    EXPECT_FALSE(validate_event(evt));
}

TEST(ValidateEvent, TsNegative_ReturnsFalse) {
    auto evt = make_valid_event();
    evt.set_ts(-1000);
    EXPECT_FALSE(validate_event(evt));
}

TEST(ValidateEvent, FutureTsBeyondOneDay_ReturnsFalse) {
    auto evt = make_valid_event();
    evt.set_ts(now_ms() + 2 * 86400000LL);
    EXPECT_FALSE(validate_event(evt));
}

TEST(ValidateEvent, PastTsBeyond30Days_ReturnsFalse) {
    auto evt = make_valid_event();
    evt.set_ts(now_ms() - 31 * 86400000LL);
    EXPECT_FALSE(validate_event(evt));
}

TEST(ValidateEvent, PastTsWithin30Days_ReturnsTrue) {
    auto evt = make_valid_event();
    evt.set_ts(now_ms() - 29 * 86400000LL);  // 在 30 天窗口内
    EXPECT_TRUE(validate_event(evt));
}

// ==================== platform ====================
TEST(ValidateEvent, OversizedPlatform_ReturnsFalse) {
    auto evt = make_valid_event();
    evt.set_platform(std::string(65, 'a'));
    EXPECT_FALSE(validate_event(evt));
}

// ==================== Metrics 原子性测试 ====================
TEST(MetricsTest, ConcurrentIncrement_CountsCorrectly) {
    Metrics m;
    const int N = 10000;
    std::thread t1([&]() { for (int i = 0; i < N; ++i) m.total_received.fetch_add(1); });
    std::thread t2([&]() { for (int i = 0; i < N; ++i) m.total_received.fetch_add(1); });
    t1.join();
    t2.join();
    EXPECT_EQ(m.total_received.load(), 2 * N);
}
