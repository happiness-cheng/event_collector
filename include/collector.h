#pragma once
#include <boost/asio.hpp>
#include "queue.h"
#include "metrics.h"
#include <memory>
#include <array>
#include <vector>
#include <atomic>

class Collector {
public:
    Collector(boost::asio::io_context& io, uint16_t port, ThreadSafeQueue& q, Metrics& m);
    void start();
    void stop();
private:
    boost::asio::ip::tcp::acceptor acceptor_;
    ThreadSafeQueue& queue_;
    Metrics& metrics_;
};

class Session : public std::enable_shared_from_this<Session> {
public:
    Session(boost::asio::ip::tcp::socket sock, ThreadSafeQueue& q, Metrics& m);
    void start();
    static int active_connections() { return active_count_.load(); }
    static constexpr std::size_t MAX_CONNECTIONS = 20000;
private:
    void do_read_header();
    void do_read_body(std::size_t body_length);
    void start_timeout();
    void on_timeout();
    void cleanup();
    boost::asio::ip::tcp::socket socket_;
    boost::asio::steady_timer timer_;
    std::array<char, 4> header_;
    std::vector<char> body_;
    ThreadSafeQueue& queue_;
    Metrics& metrics_;
    static constexpr auto TIMEOUT_SECS = std::chrono::seconds(5);
    static std::atomic<int> active_count_;
    std::atomic<bool> cleaned_up_{false};
};
