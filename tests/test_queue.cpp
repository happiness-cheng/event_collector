#include "queue.h"
#include <iostream>
#include <thread>
#include <vector>
#include <atomic>
#include <chrono>

static int tests_passed = 0;
static int tests_failed = 0;

#define ASSERT_TRUE(cond) \
    do { \
        if (!(cond)) { \
            std::cerr << "  FAIL: " #cond " at " << __FILE__ << ":" << __LINE__ << std::endl; \
            tests_failed++; \
            return; \
        } \
    } while(0)

#define ASSERT_EQ(a, b) \
    do { \
        if ((a) != (b)) { \
            std::cerr << "  FAIL: " #a " == " #b " (" << (a) << " vs " << (b) << ") at " << __FILE__ << ":" << __LINE__ << std::endl; \
            tests_failed++; \
            return; \
        } \
    } while(0)

void test_push_pop() {
    ThreadSafeQueue q(10);
    q.push("hello");
    q.push("world");
    std::string data;
    ASSERT_TRUE(q.try_pop(data));
    ASSERT_EQ(data, std::string("hello"));
    ASSERT_TRUE(q.try_pop(data));
    ASSERT_EQ(data, std::string("world"));
    ASSERT_TRUE(!q.try_pop(data));
    tests_passed++;
    std::cout << "  test_push_pop: PASS" << std::endl;
}

void test_capacity() {
    ThreadSafeQueue q(2);
    q.push("a");
    q.push("b");
    std::string data;
    ASSERT_TRUE(q.try_pop(data));
    ASSERT_EQ(data, std::string("a"));
    ASSERT_TRUE(q.try_pop(data));
    ASSERT_EQ(data, std::string("b"));
    tests_passed++;
    std::cout << "  test_capacity: PASS" << std::endl;
}

void test_try_pop_for_timeout() {
    ThreadSafeQueue q(10);
    auto result = q.try_pop_for(std::chrono::milliseconds(50));
    ASSERT_TRUE(!result.has_value());
    tests_passed++;
    std::cout << "  test_try_pop_for_timeout: PASS" << std::endl;
}

void test_concurrent_push_pop() {
    ThreadSafeQueue q(10000);
    const int N = 1000;
    std::atomic<int> received{0};

    std::thread producer([&q, N]() {
        for (int i = 0; i < N; i++) {
            q.push("event_" + std::to_string(i));
        }
    });

    std::thread consumer([&q, &received, N]() {
        for (int i = 0; i < N; i++) {
            auto r = q.try_pop_for(std::chrono::seconds(5));
            ASSERT_TRUE(r.has_value());
            received++;
        }
    });

    producer.join();
    consumer.join();
    ASSERT_EQ(received.load(), N);
    tests_passed++;
    std::cout << "  test_concurrent_push_pop: PASS (received=" << received.load() << ")" << std::endl;
}

int main() {
    std::cout << "=== Queue Unit Tests ===" << std::endl;
    test_push_pop();
    test_capacity();
    test_try_pop_for_timeout();
    test_concurrent_push_pop();
    std::cout << "=== " << tests_passed << " passed, " << tests_failed << " failed ===" << std::endl;
    return tests_failed > 0 ? 1 : 0;
}
