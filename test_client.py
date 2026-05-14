import socket, struct, event_pb2, time, random, threading
from concurrent.futures import ThreadPoolExecutor

success = 0
fail = 0
latencies = []

def send_one():
    global success, fail, latencies
    try:
        start = time.time()
        event = event_pb2.Event()
        event.event_id = str(random.randint(1, 999999))
        event.user_id = f"user_{random.randint(1, 100)}"
        event.platform = random.choice(["ios", "android", "web"])
        event.event_type = random.choice(["click", "view", "scroll"])
        event.ts = int(time.time() * 1000)
        event.payload = b"x" * random.randint(10, 200)

        data = event.SerializeToString()
        sock = socket.socket()
        sock.settimeout(2)
        sock.connect(("localhost", 8080))
        sock.sendall(struct.pack("<I", len(data)) + data)
        sock.close()
        latencies.append((time.time() - start) * 1000)
        success += 1
    except:
        fail += 1

# 并发1000条
executor = ThreadPoolExecutor(max_workers=50)
for _ in range(1000):
    executor.submit(send_one)
executor.shutdown(wait=True)

print(f"成功: {success}, 失败: {fail}")
print(f"丢弃率: {fail / (success + fail) * 100:.2f}%")
if latencies:
    print(f"平均延迟: {sum(latencies) / len(latencies):.2f}ms")
    print(f"P99延迟: {sorted(latencies)[int(len(latencies) * 0.99)]:.2f}ms")
