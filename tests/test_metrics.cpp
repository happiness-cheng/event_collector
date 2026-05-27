#include "processor.h"
#include "metrics.h"
#include "event.pb.h"
#include <iostream>
#include <cassert>
#include <ctime>
#include <chrono>

static int tests_passed = 0;
static int tests_failed = 0;

#define ASSERT_TRUE(cond) \
    do { \
        if (!(cond)) { \
            std::cerr << "  FAIL: " #cond " at " << __FILE__ << ":" << __LINE__ << std::endl; \
            tests_failed++; return; \
        } \
    } while(0)

// 辅助：构造一个合法的 Event
static event::Event make_valid_event(const std::string& uid = "user_1",
                                      const std::string& etype = "click",
                                      const std::string& platform = "ios") {
    event::Event ev;
    ev.set_event_id("evt_001");
    ev.set_user_id(uid);
    ev.set_event_type(etype);
    ev.set_platform(platform);
    ev.set_ts(std::time(nullptr) * 1000);  // 当前时间
    ev.set_payload("page=home");
    return ev;
}

// ========== validate() 测试 ==========

void test_validate_valid_event() {
    auto ev = make_valid_event();
    ASSERT_TRUE(Processor::validate(ev));
    tests_passed++;
    std::cout << "  test_validate_valid_event: PASS" << std::endl;
}

void test_validate_empty_user_id() {
    auto ev = make_valid_event("");
    ASSERT_TRUE(!Processor::validate(ev));
    tests_passed++;
    std::cout << "  test_validate_empty_user_id: PASS" << std::endl;
}

void test_validate_empty_event_type() {
    auto ev = make_valid_event("user_1", "");
    ASSERT_TRUE(!Processor::validate(ev));
    tests_passed++;
    std::cout << "  test_validate_empty_event_type: PASS" << std::endl;
}

void test_validate_user_id_too_long() {
    std::string long_uid(257, 'a');  // 257 字节，超过 256 限制
    auto ev = make_valid_event(long_uid);
    ASSERT_TRUE(!Processor::validate(ev));
    tests_passed++;
    std::cout << "  test_validate_user_id_too_long: PASS" << std::endl;
}

void test_validate_user_id_boundary_256() {
    std::string uid(256, 'a');  // 刚好 256，应该通过
    auto ev = make_valid_event(uid);
    ASSERT_TRUE(Processor::validate(ev));
    tests_passed++;
    std::cout << "  test_validate_user_id_boundary_256: PASS" << std::endl;
}

void test_validate_payload_too_large() {
    auto ev = make_valid_event();
    std::string big_payload(65537, 'x');  // 65537 > 65536
    ev.set_payload(big_payload);
    ASSERT_TRUE(!Processor::validate(ev));
    tests_passed++;
    std::cout << "  test_validate_payload_too_large: PASS" << std::endl;
}

void test_validate_payload_boundary_64k() {
    auto ev = make_valid_event();
    std::string payload(65536, 'x');  // 刚好 64KB
    ev.set_payload(payload);
    ASSERT_TRUE(Processor::validate(ev));
    tests_passed++;
    std::cout << "  test_validate_payload_boundary_64k: PASS" << std::endl;
}

void test_validate_ts_zero() {
    auto ev = make_valid_event();
    ev.set_ts(0);
    ASSERT_TRUE(!Processor::validate(ev));
    tests_passed++;
    std::cout << "  test_validate_ts_zero: PASS" << std::endl;
}

void test_validate_ts_negative() {
    auto ev = make_valid_event();
    ev.set_ts(-1000);
    ASSERT_TRUE(!Processor::validate(ev));
    tests_passed++;
    std::cout << "  test_validate_ts_negative: PASS" << std::endl;
}

void test_validate_ts_too_far_future() {
    auto ev = make_valid_event();
    // 当前时间 + 2 天（超过 1 天窗口）
    ev.set_ts((std::time(nullptr) + 172800) * 1000);
    ASSERT_TRUE(!Processor::validate(ev));
    tests_passed++;
    std::cout << "  test_validate_ts_too_far_future: PASS" << std::endl;
}

void test_validate_ts_too_far_past() {
    auto ev = make_valid_event();
    // 当前时间 - 31 天（超过 30 天窗口）
    ev.set_ts((std::time(nullptr) - 31 * 86400) * 1000);
    ASSERT_TRUE(!Processor::validate(ev));
    tests_passed++;
    std::cout << "  test_validate_ts_too_far_past: PASS" << std::endl;
}

void test_validate_ts_within_window() {
    auto ev = make_valid_event();
    // 当前时间 - 29 天（在 30 天窗口内）
    ev.set_ts((std::time(nullptr) - 29 * 86400) * 1000);
    ASSERT_TRUE(Processor::validate(ev));
    tests_passed++;
    std::cout << "  test_validate_ts_within_window: PASS" << std::endl;
}

void test_validate_platform_too_long() {
    std::string long_platform(65, 'a');  // 65 > 64
    auto ev = make_valid_event("user_1", "click", long_platform);
    ASSERT_TRUE(!Processor::validate(ev));
    tests_passed++;
    std::cout << "  test_validate_platform_too_long: PASS" << std::endl;
}

// ========== Metrics 原子操作测试 ==========

void test_metrics_concurrent_increment() {
    Metrics m;
    const int N = 10000;
    std::thread t1([&m, N]() { for (int i = 0; i < N; ++i) m.total_received.fetch_add(1); });
    std::thread t2([&m, N]() { for (int i = 0; i < N; ++i) m.total_received.fetch_add(1); });
    t1.join(); t2.join();
    ASSERT_TRUE(m.total_received.load() == 2 * N);
    tests_passed++;
    std::cout << "  test_metrics_concurrent_increment: PASS" << std::endl;
}

int main() {
    std::cout << "=== event_collector Unit Tests ===" << std::endl;

    // validate 测试
    test_validate_valid_event();
    test_validate_empty_user_id();
    test_validate_empty_event_type();
    test_validate_user_id_too_long();
    test_validate_user_id_boundary_256();
    test_validate_payload_too_large();
    test_validate_payload_boundary_64k();
    test_validate_ts_zero();
    test_validate_ts_negative();
    test_validate_ts_too_far_future();
    test_validate_ts_too_far_past();
    test_validate_ts_within_window();
    test_validate_platform_too_long();

    // Metrics 测试
    test_metrics_concurrent_increment();

    std::cout << "=== " << tests_passed << " passed, " << tests_failed << " failed ===" << std::endl;
    return tests_failed > 0 ? 1 : 0;
}
