#include "collector.h"
#include "event.pb.h"
#include <spdlog/spdlog.h>
#include <cstring>

std::atomic<int> Session::active_count_{0};

Collector::Collector(boost::asio::io_context& io, uint16_t port, ThreadSafeQueue& q)
    : acceptor_(io, boost::asio::ip::tcp::endpoint(boost::asio::ip::tcp::v4(), port)), queue_(q) {}

void Collector::start() {
    acceptor_.async_accept([this](boost::system::error_code ec, boost::asio::ip::tcp::socket sock) {
        if (!ec) {
            auto remote = sock.remote_endpoint();
            spdlog::info("[连接] {}:{}", remote.address().to_string(), remote.port());
            std::make_shared<Session>(std::move(sock), queue_)->start();
        } else {
            spdlog::warn("[接收失败] {}", ec.message());
        }
        start();
    });
}

Session::Session(boost::asio::ip::tcp::socket sock, ThreadSafeQueue& q)
    : socket_(std::move(sock)), timer_(socket_.get_executor()), queue_(q) {
    active_count_++;
    if (active_count_ > MAX_CONNECTIONS) {
        spdlog::warn("Max connections ({}) exceeded", MAX_CONNECTIONS);
        socket_.close();
    }
}

void Session::start() {
    start_timeout();
    do_read_header();
}

void Session::do_read_header() {
    auto self = shared_from_this();
    boost::asio::async_read(socket_, boost::asio::buffer(header_), [self](boost::system::error_code ec, std::size_t) {
        if (!ec) {
            // Explicit little-endian parsing (portable across architectures)
            uint32_t len = static_cast<uint8_t>(self->header_[0])
                         | (static_cast<uint8_t>(self->header_[1]) << 8)
                         | (static_cast<uint8_t>(self->header_[2]) << 16)
                         | (static_cast<uint8_t>(self->header_[3]) << 24);
            if (len > 0 && len < 102400) {
                spdlog::debug("[包头] payload_len={}", len);
                self->body_.resize(len);
                self->do_read_body(len);
            } else {
                spdlog::warn("[非法包长] len={}", len);
                self->socket_.close();
                self->active_count_--;
                return;
            }
        } else {
            spdlog::debug("[连接关闭] {}", ec.message());
            self->active_count_--;
        }
    });
}

void Session::do_read_body(std::size_t len) {
    auto self = shared_from_this();
    boost::asio::async_read(socket_, boost::asio::buffer(self->body_), [self](boost::system::error_code ec, std::size_t) {
        if (!ec) {
            // Reset timeout on activity
            self->timer_.expires_after(TIMEOUT_SECS);
            self->queue_.push(std::string(self->body_.data(), self->body_.size()));
            spdlog::debug("[入队] bytes={}", self->body_.size());
            self->do_read_header();
        } else {
            spdlog::debug("[连接关闭] {}", ec.message());
            self->active_count_--;
        }
    });
}

void Session::start_timeout() {
    auto self = shared_from_this();
    timer_.expires_after(TIMEOUT_SECS);
    timer_.async_wait([self](boost::system::error_code ec) {
        if (!ec) {
            self->on_timeout();
        }
    });
}

void Session::on_timeout() {
    spdlog::warn("[超时] 关闭空闲连接");
    socket_.close();
    active_count_--;
}
