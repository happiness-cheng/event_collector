#include <boost/asio.hpp>
#include <boost/asio/signal_set.hpp>
#include "collector.h"
#include "processor.h"
#include "monitor.h"
#include "metrics.h"
#include <thread>
#include <vector>
#include <cstdlib>
#include <spdlog/spdlog.h>

int main() {
    try {
        boost::asio::io_context io_context;
        // work guard 防止 io_context.run() 在没有待处理操作时提前返回
        auto work_guard = boost::asio::make_work_guard(io_context);

        Metrics metrics;
        ThreadSafeQueue queue(10000);

        const char* collector_port_str = std::getenv("EVENT_COLLECTOR_PORT");
        uint16_t collector_port = collector_port_str ? static_cast<uint16_t>(std::stoi(collector_port_str)) : 8080;
        const char* monitor_port_str = std::getenv("EVENT_COLLECTOR_PROMETHEUS_PORT");
        uint16_t monitor_port = monitor_port_str ? static_cast<uint16_t>(std::stoi(monitor_port_str)) : 9090;

        Collector collector(io_context, collector_port, queue);
        collector.start();

        Processor processor(queue, metrics);
        processor.start(4);

        Monitor monitor(io_context, monitor_port, metrics);
        monitor.start();

        boost::asio::signal_set signals(io_context, SIGINT, SIGTERM);
        signals.async_wait([&io_context, &work_guard, &processor](boost::system::error_code, int sig) {
            spdlog::info("Received signal {}, shutting down...", sig);
            work_guard.reset();  // 释放 work guard，允许 io_context.run() 返回
            processor.stop();    // 先停止 processor，确保队列排空
            io_context.stop();
        });

        std::vector<std::thread> io_threads;
        for (int i = 0; i < 2; ++i) {
            io_threads.emplace_back([&io_context]() { io_context.run(); });
        }

        spdlog::info("server ready: collector={} prometheus={}", collector_port, monitor_port);
        for (auto& t : io_threads) t.join();
        processor.stop();
        return 0;
    } catch (const std::exception& e) {
        spdlog::error("startup failed: {}", e.what());
        return 1;
    }
}
