#include "collector.h"
#include "event.pb.h"
#include <spdlog/spdlog.h>
#include <cstring>

Collector::Collector(boost::asio::io_context& io, uint16_t port, ThreadSafeQueue& q)
    : acceptor_(io, boost::asio::ip::tcp::endpoint(boost::asio::ip::tcp::v4(), port)), queue_(q) {}

void Collector::start() {
    acceptor_.async_accept([this](boost::system::error_code ec, boost::asio::ip::tcp::socket sock) {
        if (!ec) {
            auto remote = sock.remote_endpoint();
            spdlog::info("[接收连接] {}:{}", remote.address().to_string(), remote.port());
            std::make_shared<Session>(std::move(sock), queue_)->start();
        } else {
            spdlog::warn("[接收失败] {}", ec.message());
        }
        start();
    });
}

Session::Session(boost::asio::ip::tcp::socket sock, ThreadSafeQueue& q)
    : socket_(std::move(sock)), queue_(q) {}

void Session::start() { do_read_header(); }

void Session::do_read_header() {
    auto self = shared_from_this();
    boost::asio::async_read(socket_, boost::asio::buffer(header_), [self](boost::system::error_code ec, std::size_t) {
        if (!ec) {
            uint32_t len = 0;
            std::memcpy(&len, self->header_.data(), 4);
            if (len > 0 && len < 102400) {
                spdlog::debug("[收到包头] payload_len={}", len);
                self->body_.resize(len);
                self->do_read_body(len);
            } else {
                spdlog::warn("[非法包长] len={}，连接将终止", len);
                self->socket_.close();
                return;
            }
        } else {
            spdlog::debug("[读取包头结束] {}", ec.message());
        }
    });
}

void Session::do_read_body(std::size_t len) {
    auto self = shared_from_this();
    boost::asio::async_read(socket_, boost::asio::buffer(self->body_), [self](boost::system::error_code ec, std::size_t) {
        if (!ec) {
            self->queue_.push(std::string(self->body_.data(), self->body_.size()));
            spdlog::info("[入队成功] bytes={}", self->body_.size());
            self->do_read_header();
        } else {
            spdlog::debug("[读取包体结束] {}", ec.message());
        }
    });
}
