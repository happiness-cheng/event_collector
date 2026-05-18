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

def send_one(host, port, timeout):
    global success, fail, latencies
    try:
        start = time.time()
        evt_id = str(random.randint(1, 999999))
        uid = f"user_{random.randint(1, 100)}"
        plat = random.choice(["ios", "android", "web"])
        etype = random.choice(["click", "view", "scroll", "swipe", "longpress"])
        ts = int(time.time() * 1000)
        pl = b"x" * random.randint(10, 200)

        e = event_pb2.Event()
        e.event_id = evt_id
        e.user_id = uid
        e.platform = plat
        e.event_type = etype
        e.ts = ts
        e.payload = pl
        data = e.SerializeToString()

        sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        sock.settimeout(timeout)
        sock.connect((host, port))
        sock.sendall(struct.pack("<I", len(data)) + data)
        sock.close()

        elapsed = (time.time() - start) * 1000
        with lock:
            latencies.append(elapsed)
            success += 1
    except Exception:
        with lock:
            fail += 1

def main():
    parser = argparse.ArgumentParser(description="event_collector bench")
    parser.add_argument("--host", default="localhost")
    parser.add_argument("--port", type=int, default=8080)
    parser.add_argument("--count", type=int, default=1000)
    parser.add_argument("--threads", type=int, default=50)
    parser.add_argument("--timeout", type=float, default=3.0)
    args = parser.parse_args()

    print(f"bench start: {args.host}:{args.port} count={args.count} threads={args.threads}")
    t0 = time.time()
    with ThreadPoolExecutor(max_workers=args.threads) as ex:
        futures = [ex.submit(send_one, args.host, args.port, args.timeout) for _ in range(args.count)]
        for f in as_completed(futures):
            f.result()
    elapsed = time.time() - t0

    total = max(success + fail, 1)
    print(f"\n========== Results ==========")
    print(f"Total:    {args.count}")
    print(f"Success:  {success}")
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
