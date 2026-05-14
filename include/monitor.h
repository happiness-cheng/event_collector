#pragma once
#include "metrics.h"
#include <boost/asio.hpp>
#include <memory>
#include <string>

class Monitor {
public:
    Monitor(boost::asio::io_context& io, uint16_t port, Metrics& m);
    void start();

private:
    void do_accept();
    void handle_request(std::shared_ptr<boost::asio::ip::tcp::socket> sock);

    std::string build_prometheus_text();
    static std::string format_metric(const std::string& name,
                                     const std::string& help,
                                     const std::string& type,
                                     uint64_t value);

    boost::asio::ip::tcp::acceptor acceptor_;
    Metrics& metrics_;
};
