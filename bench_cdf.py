#!/usr/bin/env python3
"""
Collector 延迟 CDF 压测客户端
支持：固定 QPS、延迟记录、1% 长尾注入、多 payload 大小

用法：
  python3 bench_cdf.py --qps 5000 --duration 60 --payload-size 200
  python3 bench_cdf.py --qps 5000 --duration 60 --tail-inject --tail-ratio 0.01 --tail-ms 5
"""
import socket
import struct
import time
import random
import argparse
import threading
import sys
import os
import json
from concurrent.futures import ThreadPoolExecutor, as_completed

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import event_pb2


def build_packet(payload_size, ts_ms):
    """构建一个固定大小的 protobuf 事件包"""
    e = event_pb2.Event()
    e.event_id = str(random.randint(1, 999999999))
    e.user_id = f"user_{random.randint(1, 1000)}"
    e.platform = random.choice(["ios", "android", "web"])
    e.event_type = random.choice(["click", "view", "scroll", "swipe", "longpress"])
    e.ts = ts_ms
    e.payload = b"x" * max(10, payload_size - 80)  # 减去 protobuf 字段开销
    data = e.SerializeToString()
    return struct.pack("<I", len(data)) + data


class TokenBucket:
    """令牌桶限速器，控制固定 QPS"""
    def __init__(self, rate):
        self.rate = rate
        self.tokens = 0.0
        self.last = time.perf_counter()
        self.lock = threading.Lock()

    def acquire(self):
        while True:
            with self.lock:
                now = time.perf_counter()
                self.tokens += (now - self.last) * self.rate
                self.last = now
                if self.tokens >= 1.0:
                    self.tokens -= 1.0
                    return
            time.sleep(0.0001)


def worker(host, port, qps_per_worker, duration, payload_size, tail_inject, tail_ratio, tail_ms, latencies, lock, errors):
    """单 worker：独立 TCP 连接，按 QPS 发送，记录延迟"""
    bucket = TokenBucket(qps_per_worker)
    sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    sock.settimeout(5)
    try:
        sock.connect((host, port))
    except Exception as ex:
        with lock:
            errors.append(f"connect_fail: {ex}")
        return

    end_time = time.time() + duration
    while time.time() < end_time:
        bucket.acquire()

        # 长尾注入
        if tail_inject and random.random() < tail_ratio:
            time.sleep(tail_ms / 1000.0)

        ts_ms = int(time.time() * 1000)
        packet = build_packet(payload_size, ts_ms)

        t0 = time.perf_counter()
        try:
            sock.sendall(packet)
            elapsed_us = (time.perf_counter() - t0) * 1_000_000
            with lock:
                latencies.append(elapsed_us)
        except Exception as ex:
            with lock:
                errors.append(f"send_fail: {ex}")
            # 重连
            try:
                sock.close()
                sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
                sock.settimeout(5)
                sock.connect((host, port))
            except:
                break

    sock.close()


def run_test(host, port, qps, duration, payload_size, num_workers, tail_inject, tail_ratio, tail_ms):
    """运行一组测试并返回结果"""
    latencies = []
    errors = []
    lock = threading.Lock()

    qps_per_worker = max(1, qps / num_workers)

    with ThreadPoolExecutor(max_workers=num_workers) as ex:
        futures = []
        for _ in range(num_workers):
            futures.append(ex.submit(
                worker, host, port, qps_per_worker, duration, payload_size,
                tail_inject, tail_ratio, tail_ms, latencies, lock, errors
            ))
        for f in as_completed(futures):
            f.result()

    if not latencies:
        return None

    latencies.sort()
    n = len(latencies)
    actual_duration = duration  # 近似

    return {
        "qps": qps,
        "payload_size": payload_size,
        "tail_inject": tail_inject,
        "total": n,
        "errors": len(errors),
        "qps_actual": n / actual_duration,
        "p50": latencies[int(n * 0.50)],
        "p90": latencies[int(n * 0.90)],
        "p99": latencies[int(n * 0.99)],
        "p999": latencies[int(n * 0.999)] if n > 1000 else latencies[-1],
        "max": latencies[-1],
        "avg": sum(latencies) / n,
        "latencies": latencies,
    }


def save_cdf(latencies, output_file):
    """保存 CDF 数据到 CSV"""
    n = len(latencies)
    with open(output_file, "w") as f:
        f.write("latency_us,percentile\n")
        step = max(1, n // 1000)
        for i in range(0, n, step):
            f.write(f"{latencies[i]:.1f},{i/n:.4f}\n")
        f.write(f"{latencies[-1]:.1f},1.0000\n")


def main():
    parser = argparse.ArgumentParser(description="Collector CDF benchmark")
    parser.add_argument("--host", default="127.0.0.1")
    parser.add_argument("--port", type=int, default=8080)
    parser.add_argument("--qps", type=int, default=5000)
    parser.add_argument("--duration", type=int, default=60)
    parser.add_argument("--payload-size", type=int, default=200)
    parser.add_argument("--workers", type=int, default=50)
    parser.add_argument("--tail-inject", action="store_true")
    parser.add_argument("--tail-ratio", type=float, default=0.01)
    parser.add_argument("--tail-ms", type=float, default=5.0)
    parser.add_argument("--output", default="cdf_result.json")
    parser.add_argument("--cdf-output", default="")
    args = parser.parse_args()

    print(f"=== Collector CDF Benchmark ===")
    print(f"Target: {args.host}:{args.port}")
    print(f"QPS: {args.qps}, Duration: {args.duration}s, Payload: {args.payload_size}B")
    print(f"Workers: {args.workers}, Tail: {args.tail_inject} ({args.tail_ratio*100}%/{args.tail_ms}ms)")
    print()

    result = run_test(
        args.host, args.port, args.qps, args.duration, args.payload_size,
        args.workers, args.tail_inject, args.tail_ratio, args.tail_ms
    )

    if not result:
        print("ERROR: No results collected")
        sys.exit(1)

    print(f"Results:")
    print(f"  Actual QPS: {result['qps_actual']:.0f}")
    print(f"  Total: {result['total']}, Errors: {result['errors']}")
    print(f"  P50:  {result['p50']:.0f} us")
    print(f"  P90:  {result['p90']:.0f} us")
    print(f"  P99:  {result['p99']:.0f} us")
    print(f"  P99.9: {result['p999']:.0f} us")
    print(f"  Max:  {result['max']:.0f} us")
    print(f"  Avg:  {result['avg']:.0f} us")

    # 保存 JSON 结果（不含原始延迟数组）
    result_json = {k: v for k, v in result.items() if k != "latencies"}
    with open(args.output, "w") as f:
        json.dump(result_json, f, indent=2)
    print(f"\nJSON saved to {args.output}")

    # 保存 CDF 数据
    if args.cdf_output:
        save_cdf(result["latencies"], args.cdf_output)
        print(f"CDF data saved to {args.cdf_output}")


if __name__ == "__main__":
    main()
