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
        auto work_guard = boost::asio::make_work_guard(io_context);

        Metrics metrics;
        ThreadSafeQueue queue(10000);

        uint16_t collector_port = 8080;
        const char* collector_port_str = std::getenv("EVENT_COLLECTOR_PORT");
        if (collector_port_str) {
            try {
                collector_port = static_cast<uint16_t>(std::stoi(collector_port_str));
            } catch (const std::exception& e) {
                spdlog::warn("invalid EVENT_COLLECTOR_PORT '{}', using default 8080: {}", collector_port_str, e.what());
            }
        }
        uint16_t monitor_port = 9090;
        const char* monitor_port_str = std::getenv("EVENT_COLLECTOR_PROMETHEUS_PORT");
        if (monitor_port_str) {
            try {
                monitor_port = static_cast<uint16_t>(std::stoi(monitor_port_str));
            } catch (const std::exception& e) {
                spdlog::warn("invalid EVENT_COLLECTOR_PROMETHEUS_PORT '{}', using default 9090: {}", monitor_port_str, e.what());
            }
        }

        Collector collector(io_context, collector_port, queue, metrics);
        collector.start();

        Processor processor(queue, metrics);
        size_t worker_count = 4;
        const char* worker_str = std::getenv("EVENT_COLLECTOR_WORKERS");
        if (worker_str) {
            try {
                worker_count = static_cast<size_t>(std::max(1, std::stoi(worker_str)));
            } catch (const std::exception& e) {
                spdlog::warn("invalid EVENT_COLLECTOR_WORKERS '{}', using default 4: {}", worker_str, e.what());
            }
        }
        processor.start(worker_count);

        Monitor monitor(io_context, monitor_port, metrics);
        monitor.start();

        boost::asio::signal_set signals(io_context, SIGINT, SIGTERM);
        signals.async_wait([&io_context, &work_guard, &collector](boost::system::error_code, int sig) {
            spdlog::info("Received signal {}, shutting down...", sig);
            collector.stop();
            work_guard.reset();
        });

        int io_thread_count = 2;
        const char* io_str = std::getenv("EVENT_COLLECTOR_IO_THREADS");
        if (io_str) {
            try {
                io_thread_count = std::max(1, std::stoi(io_str));
            } catch (const std::exception& e) {
                spdlog::warn("invalid EVENT_COLLECTOR_IO_THREADS '{}', using default 2: {}", io_str, e.what());
            }
        }
        std::vector<std::thread> io_threads;
        io_threads.reserve(io_thread_count);
        for (int i = 0; i < io_thread_count; ++i) {
            io_threads.emplace_back([&io_context]() { io_context.run(); });
        }

        spdlog::info("server ready: collector={} prometheus={} workers={} io_threads={}", collector_port, monitor_port, worker_count, io_thread_count);
        for (auto& t : io_threads) t.join();
        processor.stop();
        return 0;
    } catch (const std::exception& e) {
        spdlog::error("startup failed: {}", e.what());
        return 1;
    }
}
