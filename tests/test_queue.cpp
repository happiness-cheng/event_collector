#include <gtest/gtest.h>
#include "queue.h"
#include <thread>
#include <atomic>
#include <chrono>
#include <string>

TEST(ThreadSafeQueue, PushPop_OrderPreserved) {
    ThreadSafeQueue q(10);
    EXPECT_TRUE(q.try_push("hello"));
    EXPECT_TRUE(q.try_push("world"));
    std::string data;
    EXPECT_TRUE(q.try_pop(data));
    EXPECT_EQ(data, "hello");
    EXPECT_TRUE(q.try_pop(data));
    EXPECT_EQ(data, "world");
    EXPECT_FALSE(q.try_pop(data));
}

TEST(ThreadSafeQueue, Capacity_BlocksWhenFull) {
    ThreadSafeQueue q(2);
    EXPECT_TRUE(q.try_push("a"));
    EXPECT_TRUE(q.try_push("b"));
    EXPECT_FALSE(q.try_push("c"));  // 满，应拒
    std::string data;
    EXPECT_TRUE(q.try_pop(data));
    EXPECT_EQ(data, "a");
}

TEST(ThreadSafeQueue, TryPopFor_TimeoutReturnsNullopt) {
    ThreadSafeQueue q(10);
    auto result = q.try_pop_for(std::chrono::milliseconds(50));
    EXPECT_FALSE(result.has_value());
}

TEST(ThreadSafeQueue, ConcurrentPushPop_AllReceived) {
    ThreadSafeQueue q(10000);
    const int N = 1000;
    std::atomic<int> received{0};

    std::thread producer([&]() {
        for (int i = 0; i < N; i++) {
            q.try_push("event_" + std::to_string(i));
        }
    });

    std::thread consumer([&]() {
        for (int i = 0; i < N; i++) {
            auto r = q.try_pop_for(std::chrono::seconds(5));
            ASSERT_TRUE(r.has_value());
            received++;
        }
    });

    producer.join();
    consumer.join();
    EXPECT_EQ(received.load(), N);
}
