#pragma once
#include <boost/asio.hpp>
#include "queue.h"
#include <memory>
#include <array>
#include <vector>

class Collector {
public:
    Collector(boost::asio::io_context& io, uint16_t port, ThreadSafeQueue& q);
    void start();
private:
    boost::asio::ip::tcp::acceptor acceptor_;
    ThreadSafeQueue& queue_;
};

class Session : public std::enable_shared_from_this<Session> {
public:
    Session(boost::asio::ip::tcp::socket sock, ThreadSafeQueue& q);
    void start();
private:
    void do_read_header();
    void do_read_body(std::size_t body_length);
    boost::asio::ip::tcp::socket socket_;
    std::array<char, 4> header_;
    std::vector<char> body_;
    ThreadSafeQueue& queue_;
};
