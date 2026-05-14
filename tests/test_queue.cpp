#include "queue.h"
#include <cassert>
#include <iostream>
#include <thread>
#include <vector>
#include <atomic>

void test_push_pop() {
    ThreadSafeQueue q(10);
    q.push("hello");
    q.push("world");
    std::string data;
    assert(q.try_pop(data));
    assert(data == "hello");
    assert(q.try_pop(data));
    assert(data == "world");
    assert(!q.try_pop(data));
    std::cout << "  test_push_pop: PASS" << std::endl;
}

void test_capacity() {
    ThreadSafeQueue q(2);
    q.push("a");
    q.push("b");
    std::string data;
    assert(q.try_pop(data));
    assert(data == "a");
    assert(q.try_pop(data));
    assert(data == "b");
    std::cout << "  test_capacity: PASS" << std::endl;
}

void test_try_pop_for_timeout() {
    ThreadSafeQueue q(10);
    auto result = q.try_pop_for(std::chrono::milliseconds(50));
    assert(!result.has_value());
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
            assert(r.has_value());
            received++;
        }
    });

    producer.join();
    consumer.join();
    assert(received == N);
    std::cout << "  test_concurrent_push_pop: PASS (received=" << received.load() << ")" << std::endl;
}

int main() {
    std::cout << "=== Queue Unit Tests ===" << std::endl;
    test_push_pop();
    test_capacity();
    test_try_pop_for_timeout();
    test_concurrent_push_pop();
    std::cout << "=== All tests passed ===" << std::endl;
    return 0;
}
