#!/bin/bash
set -e

echo "=== event_collector 启动脚本 ==="
echo ""

if [ ! -f build/server ]; then
    echo "未找到 build/server，开始编译..."
    mkdir -p build
    cd build
    cmake -G 'Unix Makefiles' ..
    make -j$(nproc)
    cd ..
    echo "编译完成"
fi

export EVENT_COLLECTOR_KAFKA_BOOTSTRAP="${EVENT_COLLECTOR_KAFKA_BOOTSTRAP:-}"
export EVENT_COLLECTOR_ENABLE_CLICKHOUSE="${EVENT_COLLECTOR_ENABLE_CLICKHOUSE:-0}"
export EVENT_COLLECTOR_REDIS_ENABLE="${EVENT_COLLECTOR_REDIS_ENABLE:-0}"
export EVENT_COLLECTOR_REDIS_HOST="${EVENT_COLLECTOR_REDIS_HOST:-127.0.0.1}"
export EVENT_COLLECTOR_REDIS_PORT="${EVENT_COLLECTOR_REDIS_PORT:-6379}"
export EVENT_COLLECTOR_RATE_LIMIT="${EVENT_COLLECTOR_RATE_LIMIT:-100}"

echo "Collector 监听 :8080"
echo "Prometheus 监听 :9090"
echo "Ctrl+C 优雅退出"
echo ""

./build/server
