#!/usr/bin/env python3
import socket, struct, time, random, argparse, sys, os, threading
from concurrent.futures import ThreadPoolExecutor, as_completed
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
success = 0; fail = 0; latencies = []; lock = threading.Lock()

def send_batch(host, port, timeout, count):
    global success, fail, latencies
    try:
        sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        sock.settimeout(timeout)
        sock.connect((host, port))
        for _ in range(count):
            start = time.time()
            import event_pb2
            e = event_pb2.Event()
            e.event_id = str(random.randint(1, 999999))
            e.user_id = "user_%d" % random.randint(1, 100)
            e.platform = random.choice(["ios", "android", "web"])
            e.event_type = random.choice(["click", "view", "scroll", "swipe", "longpress"])
            e.ts = int(time.time() * 1000)
            e.payload = b"x" * random.randint(10, 200)
            data = e.SerializeToString()
            sock.sendall(struct.pack("<I", len(data)) + data)
            elapsed = (time.time() - start) * 1000
            with lock:
                latencies.append(elapsed); success += 1
        sock.close()
    except Exception:
        with lock: fail += count

def main():
    parser = argparse.ArgumentParser(description="bench connection reuse")
    parser.add_argument("--host", default="localhost")
    parser.add_argument("--port", type=int, default=8080)
    parser.add_argument("--count", type=int, default=1000)
    parser.add_argument("--threads", type=int, default=50)
    parser.add_argument("--timeout", type=float, default=3.0)
    args = parser.parse_args()
    per_thread = args.count // args.threads
    actual_count = per_thread * args.threads
    print("bench start: %s:%d count=%d threads=%d per_conn=%d" % (args.host, args.port, actual_count, args.threads, per_thread))
    t0 = time.time()
    with ThreadPoolExecutor(max_workers=args.threads) as ex:
        futures = [ex.submit(send_batch, args.host, args.port, args.timeout, per_thread) for _ in range(args.threads)]
        for f in as_completed(futures):
            f.result()
    elapsed = time.time() - t0
    total = max(success + fail, 1)
    print()
    print("========== Results ==========")
    print("Total:    %d" % actual_count)
    print("Success:  %d" % success)
    print("Failed:   %d" % fail)
    print("DropRate: %.2f%%" % (fail / total * 100))
    print("Time:     %.2fs" % elapsed)
    print("QPS:      %.0f" % (success / max(elapsed, 0.001)))
    if latencies:
        latencies.sort()
        print("Avg:      %.2fms" % (sum(latencies) / len(latencies)))
        print("P50:      %.2fms" % latencies[int(len(latencies) * 0.50)])
        print("P90:      %.2fms" % latencies[int(len(latencies) * 0.90)])
        print("P99:      %.2fms" % latencies[int(len(latencies) * 0.99)])
        print("Max:      %.2fms" % latencies[-1])
    sys.exit(0)

if __name__ == "__main__":
    main()
