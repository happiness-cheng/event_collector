#!/usr/bin/env python3
import socket
import struct
import time
import random
import argparse
import sys
import os
import threading
from concurrent.futures import ThreadPoolExecutor, as_completed

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import event_pb2

success = 0
fail = 0
latencies = []
lock = threading.Lock()

_counter = 0
_counter_lock = threading.Lock()

def make_event():
    global _counter
    with _counter_lock:
        _counter += 1
        eid = _counter
    e = event_pb2.Event()
    e.event_id = f"{time.time_ns()}_{eid}_{random.randint(1, 999999)}"
    e.user_id = f"user_{random.randint(1, 100)}"
    e.platform = random.choice(["ios", "android", "web"])
    e.event_type = random.choice(["click", "view", "scroll", "swipe", "longpress"])
    e.ts = int(time.time() * 1000)
    e.payload = b"x" * random.randint(10, 200)
    return e.SerializeToString()

def worker_batch(host, port, timeout, events_per_conn):
    global success, fail, latencies
    sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    sock.settimeout(timeout)
    try:
        sock.connect((host, port))
    except Exception:
        with lock:
            fail += events_per_conn
        return
    try:
        for _ in range(events_per_conn):
            data = make_event()
            start = time.time()
            sock.sendall(struct.pack("<I", len(data)) + data)
            elapsed = (time.time() - start) * 1000
            with lock:
                latencies.append(elapsed)
                success += 1
    except Exception:
        with lock:
            fail += 1
    finally:
        sock.close()

def main():
    parser = argparse.ArgumentParser(description="event_collector bench (connection reuse)")
    parser.add_argument("--host", default="localhost")
    parser.add_argument("--port", type=int, default=8080)
    parser.add_argument("--count", type=int, default=1000)
    parser.add_argument("--threads", type=int, default=50)
    parser.add_argument("--timeout", type=float, default=3.0)
    parser.add_argument("--reuse", type=int, default=20, help="events per connection")
    args = parser.parse_args()

    events_per_conn = args.reuse
    num_conns = max(args.count // events_per_conn, 1)

    print(f"bench start: {args.host}:{args.port} count={args.count} threads={args.threads} reuse={events_per_conn}/conn")
    t0 = time.time()
    with ThreadPoolExecutor(max_workers=args.threads) as ex:
        futures = [ex.submit(worker_batch, args.host, args.port, args.timeout, events_per_conn) for _ in range(num_conns)]
        for f in as_completed(futures):
            f.result()
    elapsed = time.time() - t0

    total = max(success + fail, 1)
    print(f"\n========== Results (connection reuse) ==========")
    print(f"Total:    {success}")
    print(f"Failed:   {fail}")
    print(f"DropRate: {fail / total * 100:.2f}%")
    print(f"Time:     {elapsed:.2f}s")
    print(f"QPS:      {success / max(elapsed, 0.001):.0f}")
    if latencies:
        latencies.sort()
        print(f"Avg:      {sum(latencies) / len(latencies):.2f}ms")
        print(f"P50:      {latencies[int(len(latencies) * 0.50)]:.2f}ms")
        print(f"P90:      {latencies[int(len(latencies) * 0.90)]:.2f}ms")
        print(f"P99:      {latencies[int(len(latencies) * 0.99)]:.2f}ms")
        print(f"Max:      {latencies[-1]:.2f}ms")
    sys.exit(0)

if __name__ == "__main__":
    main()
