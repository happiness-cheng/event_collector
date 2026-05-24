#include "collector.h"
#include <spdlog/spdlog.h>
#include <cstring>

std::atomic<int> Session::active_count_{0};

Collector::Collector(boost::asio::io_context& io, uint16_t port, ThreadSafeQueue& q, Metrics& m)
    : acceptor_(io, boost::asio::ip::tcp::endpoint(boost::asio::ip::tcp::v4(), port)), queue_(q), metrics_(m) {}

void Collector::start() {
    acceptor_.async_accept([this](boost::system::error_code ec, boost::asio::ip::tcp::socket sock) {
        if (!ec) {
            if (Session::active_connections() >= Session::MAX_CONNECTIONS) {
                spdlog::warn("Max connections ({}) exceeded, rejecting", Session::MAX_CONNECTIONS);
                sock.close();
                start();
                return;
            }
            auto remote = sock.remote_endpoint();
            spdlog::info("[connect] {}:{}", remote.address().to_string(), remote.port());
            std::make_shared<Session>(std::move(sock), queue_, metrics_)->start();
        } else {
            spdlog::warn("[accept_fail] {}", ec.message());
        }
        start();
    });
}

void Session::stop(){
    boost::system::error_code ec;
      acceptor_.close(ec);
      if (ec) spdlog::warn("acceptor close error: {}", ec.message());
      else spdlog::info("acceptor closed, no new connections");
}

Session::Session(boost::asio::ip::tcp::socket sock, ThreadSafeQueue& q, Metrics& m)
    : socket_(std::move(sock)), timer_(socket_.get_executor()), queue_(q), metrics_(m) {
    active_count_.fetch_add(1);
    socket_.set_option(boost::asio::ip::tcp::no_delay(true));
}

void Session::start() {
    start_timeout();
    do_read_header();
}

void Session::do_read_header() {
    auto self = shared_from_this();
    boost::asio::async_read(socket_, boost::asio::buffer(header_), [self](boost::system::error_code ec, std::size_t) {
        if (!ec) {
            uint32_t len = static_cast<uint8_t>(self->header_[0])
                         | (static_cast<uint8_t>(self->header_[1]) << 8)
                         | (static_cast<uint8_t>(self->header_[2]) << 16)
                         | (static_cast<uint8_t>(self->header_[3]) << 24);
            if (len > 0 && len < 102400) {
                spdlog::debug("[header] payload_len={}", len);
                self->body_.resize(len);
                self->do_read_body(len);
            } else {
                spdlog::warn("[invalid_len] len={}", len);
                self->socket_.close();
                self->active_count_.fetch_sub(1);
                return;
            }
        } else {
            spdlog::debug("[conn_close] {}", ec.message());
            self->active_count_.fetch_sub(1);
        }
    });
}

void Session::do_read_body(std::size_t len) {
    auto self = shared_from_this();
    boost::asio::async_read(socket_, boost::asio::buffer(self->body_), [self](boost::system::error_code ec, std::size_t) {
        if (!ec) {
            self->timer_.expires_after(TIMEOUT_SECS);
            if (!self->queue_.try_push(std::string(self->body_.data(), self->body_.size()))) {
                self->metrics_.total_queue_drop.fetch_add(1);
                spdlog::warn("[queue_full] dropping event");
            }
            spdlog::debug("[enqueue] bytes={}", self->body_.size());
            self->do_read_header();
        } else {
            spdlog::debug("[conn_close] {}", ec.message());
            self->active_count_.fetch_sub(1);
        }
    });
}

void Session::start_timeout() {
    auto self = shared_from_this();
    timer_.expires_after(TIMEOUT_SECS);
    timer_.async_wait([self](boost::system::error_code ec) {
        if (!ec) self->on_timeout();
    });
}

void Session::on_timeout() {
    spdlog::warn("[timeout] closing idle connection");
    socket_.close();
    active_count_.fetch_sub(1);
}
