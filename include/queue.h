#pragma once
#include <optional>
#include <queue>
#include <mutex>
#include <condition_variable>
#include <cstddef>
#include <utility>
#include <chrono>
#include <string>

class ThreadSafeQueue {
public:
    explicit ThreadSafeQueue(std::size_t capacity = 10000)
        : capacity_(capacity) {}

    void push(const std::string& value) {
        std::unique_lock<std::mutex> lk(mtx_);
        not_full_.wait(lk, [this]{ return queue_.size() < capacity_; });
        queue_.push(value);
        lk.unlock();
        not_empty_.notify_one();
    }

    void push(std::string&& value) {
        std::unique_lock<std::mutex> lk(mtx_);
        not_full_.wait(lk, [this]{ return queue_.size() < capacity_; });
        queue_.push(std::move(value));
        lk.unlock();
        not_empty_.notify_one();
    }

    bool try_pop(std::string& data) {
        std::lock_guard<std::mutex> lk(mtx_);
        if (queue_.empty()) return false;
        data = std::move(queue_.front());
        queue_.pop();
        not_full_.notify_one();
        return true;
    }

    std::optional<std::string> try_pop_for(std::chrono::milliseconds timeout) {
        std::unique_lock<std::mutex> lk(mtx_);
        bool got = not_empty_.wait_for(lk, timeout, [this]{ return !queue_.empty(); });
        if (!got) return std::nullopt;
        std::string val = std::move(queue_.front());
        queue_.pop();
        lk.unlock();
        not_full_.notify_one();
        return val;
    }

private:
    std::queue<std::string> queue_;
    std::mutex mtx_;
    std::condition_variable not_full_;
    std::condition_variable not_empty_;
    std::size_t capacity_;
};
