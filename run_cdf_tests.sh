#!/bin/bash
# 完整 CDF 压测矩阵（对标 brpc 6 维度）
# 用法: bash run_cdf_tests.sh [host] [port]
# 输出: results/ 目录下所有测试结果 + 汇总表

HOST=${1:-127.0.0.1}
PORT=${2:-8080}
RESULTS_DIR="results_$(date +%Y%m%d_%H%M%S)"
mkdir -p "$RESULTS_DIR"

echo "=========================================="
echo " Collector + Engine CDF Benchmark Suite"
echo " Host: $HOST:$PORT"
echo " Time: $(date)"
echo " Output: $RESULTS_DIR/"
echo "=========================================="

# ===== 第一部分：Collector 测试 =====
echo ""
echo "=== Part 1: Collector Tests ==="

# T1: 不同 QPS 下的延迟 CDF（无长尾）
for qps in 1000 5000 10000 15000; do
  echo "[T1] Collector QPS=$qps, no tail, 60s"
  python3 bench_cdf.py --host "$HOST" --port "$PORT" \
    --qps $qps --duration 60 --payload-size 200 --workers 50 \
    --output "$RESULTS_DIR/collector_q${qps}_notail.json" \
    --cdf-output "$RESULTS_DIR/collector_q${qps}_notail.csv"
  sleep 5
done

# T2: 不同 QPS 下的延迟 CDF（1% 长尾注入）
for qps in 1000 5000 10000 15000; do
  echo "[T2] Collector QPS=$qps, 1% tail 5ms, 60s"
  python3 bench_cdf.py --host "$HOST" --port "$PORT" \
    --qps $qps --duration 60 --payload-size 200 --workers 50 \
    --tail-inject --tail-ratio 0.01 --tail-ms 5 \
    --output "$RESULTS_DIR/collector_q${qps}_tail.json" \
    --cdf-output "$RESULTS_DIR/collector_q${qps}_tail.csv"
  sleep 5
done

# T3: 不同 payload 大小（固定 QPS=5000）
for size in 50 200 1024 4096 16384; do
  echo "[T3] Collector payload=${size}B, QPS=5000, 60s"
  python3 bench_cdf.py --host "$HOST" --port "$PORT" \
    --qps 5000 --duration 60 --payload-size $size --workers 50 \
    --output "$RESULTS_DIR/collector_size${size}.json" \
    --cdf-output "$RESULTS_DIR/collector_size${size}.csv"
  sleep 5
done

# T4: 不同连接数（固定 QPS=5000）
for conns in 10 50 100 200 500; do
  echo "[T4] Collector workers=$conns, QPS=5000, 60s"
  python3 bench_cdf.py --host "$HOST" --port "$PORT" \
    --qps 5000 --duration 60 --payload-size 200 --workers $conns \
    --output "$RESULTS_DIR/collector_w${conns}.json" \
    --cdf-output "$RESULTS_DIR/collector_w${conns}.csv"
  sleep 5
done

# ===== 第二部分：Engine 测试（ghz）=====
echo ""
echo "=== Part 2: Engine gRPC Tests ==="
ENGINE_HOST=${3:-127.0.0.1}
ENGINE_PORT=${4:-50051}
PROTO_FILE=${5:-"/home/azureuser/event_stream_engine/proto/event.proto"}

# T5: 不同并发下的 gRPC 延迟
for c in 10 50 100 200; do
  echo "[T5] Engine SendEvent concurrency=$c, 10000n"
  ghz --insecure --proto "$PROTO_FILE" \
    --call event.EventStream.SendEvent \
    -c $c -n 10000 --timeout=120s \
    -d '{"event_id":"bench","user_id":"u1","platform":"ios","event_type":"click","ts":1716800000000,"payload":"dGVzdA=="}' \
    --output "$RESULTS_DIR/engine_c${c}.json" --format json \
    "$ENGINE_HOST:$ENGINE_PORT" 2>/dev/null
  echo "  -> saved to $RESULTS_DIR/engine_c${c}.json"
  sleep 5
done

# T6: SendBatch 对比
for c in 10 50 100; do
  echo "[T6] Engine SendBatch×5 concurrency=$c, 5000n"
  ghz --insecure --proto "$PROTO_FILE" \
    --call event.EventStream.SendBatch \
    -c $c -n 5000 --timeout=120s \
    -d '{"events":[{"event_id":"b1","user_id":"u1","platform":"ios","event_type":"click","ts":1716800000000,"payload":"dGVzdA=="},{"event_id":"b2","user_id":"u2","platform":"android","event_type":"view","ts":1716800000000,"payload":"dGVzdA=="},{"event_id":"b3","user_id":"u3","platform":"web","event_type":"scroll","ts":1716800000000,"payload":"dGVzdA=="},{"event_id":"b4","user_id":"u4","platform":"ios","event_type":"swipe","ts":1716800000000,"payload":"dGVzdA=="},{"event_id":"b5","user_id":"u5","platform":"android","event_type":"longpress","ts":1716800000000,"payload":"dGVzdA=="}]}' \
    --output "$RESULTS_DIR/engine_batch_c${c}.json" --format json \
    "$ENGINE_HOST:$ENGINE_PORT" 2>/dev/null
  echo "  -> saved to $RESULTS_DIR/engine_batch_c${c}.json"
  sleep 5
done

# ===== 第三部分：汇总 =====
echo ""
echo "=== Part 3: Summary ==="
echo "Generating summary..."

python3 -c "
import json, os, glob

results_dir = '$RESULTS_DIR'
print('=' * 80)
print('BENCHMARK SUMMARY')
print('=' * 80)

# Collector results
print()
print('Collector Tests:')
print(f'{'QPS':>8} {'Payload':>8} {'Workers':>8} {'Tail':>6} {'P50us':>8} {'P90us':>8} {'P99us':>8} {'Maxus':>8} {'Errors':>8}')
print('-' * 80)

for f in sorted(glob.glob(f'{results_dir}/collector_*.json')):
    try:
        with open(f) as fh:
            r = json.load(fh)
        tail = 'yes' if r.get('tail_inject', False) else 'no'
        print(f'{r.get(\"qps_actual\", 0):>8.0f} {r.get(\"payload_size\", 0):>8} {r.get(\"workers\", \"?\"):>8} {tail:>6} {r.get(\"p50\", 0):>8.0f} {r.get(\"p90\", 0):>8.0f} {r.get(\"p99\", 0):>8.0f} {r.get(\"max\", 0):>8.0f} {r.get(\"errors\", 0):>8}')
    except: pass

# Engine results
print()
print('Engine Tests:')
print(f'{'Concurrency':>12} {'Type':>10} {'RPS':>8} {'P50ms':>8} {'P90ms':>8} {'P99ms':>8} {'Status':>10}')
print('-' * 80)

for f in sorted(glob.glob(f'{results_dir}/engine_*.json')):
    try:
        with open(f) as fh:
            r = json.load(fh)
        rps = r.get('rps', r.get('average', {}).get('rps', 0))
        lat = r.get('latencyDistribution', r.get('average', {}))
        p50 = next((x['latency'] for x in lat if x.get('percentile', 0) == 50), 0) if isinstance(lat, list) else lat.get('p50', 0)
        p90 = next((x['latency'] for x in lat if x.get('percentile', 0) == 90), 0) if isinstance(lat, list) else lat.get('p90', 0)
        p99 = next((x['latency'] for x in lat if x.get('percentile', 0) == 99), 0) if isinstance(lat, list) else lat.get('p99', 0)
        status = 'OK' if r.get('statusCodeDistribution', {}).get('OK', 0) > 0 else 'FAIL'
        ftype = 'batch' if 'batch' in f else 'event'
        c = f.split('_c')[1].split('.')[0] if '_c' in f else '?'
        print(f'{c:>12} {ftype:>10} {rps:>8.0f} {p50:>8.1f} {p90:>8.1f} {p99:>8.1f} {status:>10}')
    except Exception as e:
        print(f'  Error reading {f}: {e}')

print()
print(f'Total test files: {len(glob.glob(results_dir + \"/*.json\"))}')
" 2>&1

echo ""
echo "=========================================="
echo " All tests completed!"
echo " Results in: $RESULTS_DIR/"
echo "=========================================="
