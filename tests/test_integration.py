#!/usr/bin/env python3
"""集成测试：启动 server（无下游依赖），发事件，验指标，优雅退出。"""
import subprocess
import socket
import struct
import time
import sys
import os

tests_dir = os.path.dirname(os.path.abspath(__file__))
repo_root = os.path.dirname(tests_dir)
sys.path.insert(0, tests_dir)   # tests/event_pb2.py (CI 生成位置)
sys.path.insert(0, repo_root)    # event_pb2.py (本地已有位置)
import event_pb2

TCP_PORT = 18080
METRICS_PORT = 19090


def main():
    env = os.environ.copy()
    env["EVENT_COLLECTOR_PORT"] = str(TCP_PORT)
    env["EVENT_COLLECTOR_PROMETHEUS_PORT"] = str(METRICS_PORT)
    # 不设 EVENT_COLLECTOR_KAFKA_BOOTSTRAP / ENABLE_CLICKHOUSE / REDIS_ENABLE
    # → Kafka/ClickHouse/Redis 全部禁用

    server_bin = os.environ.get("EVENT_COLLECTOR_SERVER_BIN", os.path.join("build", "server"))
    proc = subprocess.Popen(
        [server_bin], env=env,
        stdout=subprocess.PIPE, stderr=subprocess.PIPE,
    )

    time.sleep(2)

    if proc.poll() is not None:
        print(f"SERVER CRASHED (code {proc.returncode})")
        err = proc.stderr.read().decode()
        if err:
            print(err)
        return 1

    # 发送事件
    try:
        sock = socket.create_connection(("127.0.0.1", TCP_PORT), timeout=5)
    except Exception as e:
        print(f"CONNECT FAILED: {e}")
        proc.kill()
        return 1

    N = 10
    for i in range(N):
        ev = event_pb2.Event()
        ev.event_id = f"int_{i}"
        ev.user_id = f"u{i}"
        ev.platform = "web"
        ev.event_type = "click"
        ev.ts = int(time.time() * 1000)
        ev.payload = b'{"ok":1}'
        data = ev.SerializeToString()
        sock.sendall(struct.pack("<I", len(data)) + data)

    sock.close()
    time.sleep(1)

    # 获取指标
    import urllib.request

    try:
        resp = urllib.request.urlopen(
            f"http://127.0.0.1:{METRICS_PORT}/metrics", timeout=5
        )
        body = resp.read().decode()
    except Exception as e:
        print(f"METRICS FAILED: {e}")
        proc.kill()
        return 1

    # 解析指标
    got = {}
    for line in body.splitlines():
        for name in (
            "event_received_total",
            "event_parsed_total",
            "event_valid_total",
            "event_kafka_ok_total",
            "event_stored_total",
        ):
            if line.startswith(name) and not line.startswith("#"):
                got[name] = int(line.split()[-1])

    print(f"Metrics: {got}")

    ok = True
    if got.get("event_received_total") != N:
        print(f"FAIL: received={got.get('event_received_total')} expected={N}")
        ok = False
    if got.get("event_parsed_total") != N:
        print(f"FAIL: parsed={got.get('event_parsed_total')} expected={N}")
        ok = False
    if got.get("event_valid_total") != N:
        print(f"FAIL: valid={got.get('event_valid_total')} expected={N}")
        ok = False
    if got.get("event_kafka_ok_total", -1) != 0:
        print(f"FAIL: kafka_ok={got.get('event_kafka_ok_total')} expected=0")
        ok = False
    if got.get("event_stored_total", -1) != 0:
        print(f"FAIL: stored={got.get('event_stored_total')} expected=0")
        ok = False

    # 优雅退出
    proc.terminate()
    try:
        proc.wait(timeout=5)
    except subprocess.TimeoutExpired:
        print("WARN: graceful shutdown timeout, killing")
        proc.kill()

    if ok:
        print("PASS")
        return 0
    else:
        return 1


if __name__ == "__main__":
    sys.exit(main())
