#include "monitor.h"
#include <spdlog/spdlog.h>
#include <sstream>

Monitor::Monitor(boost::asio::io_context& io, uint16_t port, Metrics& m)
    : acceptor_(io, boost::asio::ip::tcp::endpoint(boost::asio::ip::tcp::v4(), port)),
      metrics_(m) {}

void Monitor::start() {
    spdlog::info("Prometheus metrics endpoint started, listening :{}", acceptor_.local_endpoint().port());
    do_accept();
}

void Monitor::do_accept() {
    auto sock = std::make_shared<boost::asio::ip::tcp::socket>(acceptor_.get_executor());
    acceptor_.async_accept(*sock, [this, sock](boost::system::error_code ec) {
        if (!ec) {
            handle_request(sock);
        }
        do_accept();
    });
}

void Monitor::handle_request(std::shared_ptr<boost::asio::ip::tcp::socket> sock) {
    auto buf = std::make_shared<std::array<char, 1024>>();
    sock->async_read_some(boost::asio::buffer(*buf),
        [this, sock, buf](boost::system::error_code ec, std::size_t) {
            if (ec) return;
            std::string resp = build_prometheus_text();
            auto rsp = std::make_shared<std::string>(
                "HTTP/1.1 200 OK\r\n"
                "Content-Type: text/plain; version=0.0.4\r\n"
                "Content-Length: " + std::to_string(resp.size()) + "\r\n"
                "Connection: close\r\n\r\n" + resp);
            boost::asio::async_write(*sock, boost::asio::buffer(*rsp),
                [sock, rsp](boost::system::error_code, std::size_t) {});
        });
}

std::string Monitor::format_metric(const std::string& name,
                                    const std::string& help,
                                    const std::string& type,
                                    uint64_t value) {
    std::ostringstream oss;
    oss << "# HELP " << name << " " << help << "\n"
        << "# TYPE " << name << " " << type << "\n"
        << name << " " << value << "\n";
    return oss.str();
}

std::string Monitor::build_prometheus_text() {
    std::ostringstream out;

    out << format_metric("event_received_total",    "Total TCP events received",          "counter", metrics_.total_received.load());
    out << format_metric("event_parsed_total",      "Total protobuf parsed successfully", "counter", metrics_.total_parsed.load());
    out << format_metric("event_valid_total",       "Valid events after field check",     "counter", metrics_.total_valid.load());
    out << format_metric("event_invalid_total",     "Events failing field validation",    "counter", metrics_.total_invalid.load());
    out << format_metric("event_parse_fail_total",  "Events failing protobuf parse",      "counter", metrics_.total_parse_fail.load());
    out << format_metric("event_rate_limited_total","Events dropped by rate limiter",     "counter", metrics_.total_rate_limited.load());
    out << format_metric("event_kafka_ok_total",    "Events successfully sent to Kafka",  "counter", metrics_.total_kafka_ok.load());
    out << format_metric("event_kafka_fail_total",  "Events failed to send to Kafka",     "counter", metrics_.total_kafka_fail.load());
    out << format_metric("event_stored_total",      "Events stored to ClickHouse",        "counter", metrics_.total_stored.load());
    out << format_metric("event_store_fail_total",  "Events failed to store",             "counter", metrics_.total_store_fail.load());
    out << format_metric("event_store_dead_total",  "Events written to dead_letter",      "counter", metrics_.total_store_dead.load());

    return out.str();
}
