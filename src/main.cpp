#include <boost/asio.hpp>
#include <boost/asio/signal_set.hpp>
#include "collector.h"
#include "processor.h"
#include "monitor.h"
#include "metrics.h"
#include <thread>
#include <vector>
#include <spdlog/spdlog.h>

int main() {
    try {
        boost::asio::io_context io_context;
        Metrics metrics;
        ThreadSafeQueue queue(10000);

        Collector collector(io_context, 8080, queue);
        collector.start();

        Processor processor(queue, metrics);
        processor.start(4);

        Monitor monitor(io_context, 9090, metrics);
        monitor.start();

        boost::asio::signal_set signals(io_context, SIGINT, SIGTERM);
        signals.async_wait([&io_context](boost::system::error_code, int sig) {
            spdlog::info("Received signal {}, shutting down...", sig);
            io_context.stop();
        });

        std::vector<std::thread> io_threads;
        for (int i = 0; i < 2; ++i) {
            io_threads.emplace_back([&io_context]() { io_context.run(); });
        }

        spdlog::info("server ready: collector=8080 prometheus=9090");
        for (auto& t : io_threads) t.join();
        processor.stop();
        return 0;
    } catch (const std::exception& e) {
        spdlog::error("startup failed: {}", e.what());
        return 1;
    }
}
