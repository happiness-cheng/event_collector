#!/bin/bash
set -e

SERVER=build2/server
BENCH=venv/bin/python3
PORT=8080
METRICS=9090
NUM_EVENTS=20

echo "=== event_collector 集成测试 ==="

# 1. 启动服务
$SERVER &
SERVER_PID=$!
sleep 2

if ! kill -0 $SERVER_PID 2>/dev/null; then
    echo "FAIL: 服务启动失败"
    exit 1
fi
echo "OK: 服务启动 (PID=$SERVER_PID)"

# 2. 发送事件
$BENCH bench_client.py --count $NUM_EVENTS --threads 2 --host 127.0.0.1 --port $PORT
echo "OK: 发送 $NUM_EVENTS 条事件"

sleep 1

# 3. 检查 Prometheus 指标
METRICS_TEXT=$(curl -s http://localhost:$METRICS)

RECEIVED=$(echo "$METRICS_TEXT" | grep 'event_received_total' | grep -v '#' | awk '{print $2}')
PARSED=$(echo "$METRICS_TEXT" | grep 'event_parsed_total' | grep -v '#' | awk '{print $2}')
VALID=$(echo "$METRICS_TEXT" | grep 'event_valid_total' | grep -v '#' | awk '{print $2}')

PASS=true
if [ "$RECEIVED" != "$NUM_EVENTS" ]; then
    echo "FAIL: received=$RECEIVED expected=$NUM_EVENTS"
    PASS=false
fi
if [ "$PARSED" != "$NUM_EVENTS" ]; then
    echo "FAIL: parsed=$PARSED expected=$NUM_EVENTS"
    PASS=false
fi
if [ "$VALID" != "$NUM_EVENTS" ]; then
    echo "FAIL: valid=$VALID expected=$NUM_EVENTS"
    PASS=false
fi

if $PASS; then
    echo "OK: received=$RECEIVED parsed=$PARSED valid=$VALID"
    echo ""
    echo "=== 所有测试通过 ==="
else
    echo ""
    echo "=== 测试失败 ==="
    kill -2 $SERVER_PID 2>/dev/null
    wait $SERVER_PID 2>/dev/null || true
    exit 1
fi

# 4. 测试优雅退出
kill -2 $SERVER_PID
wait $SERVER_PID 2>/dev/null || true
echo "OK: 优雅退出"
