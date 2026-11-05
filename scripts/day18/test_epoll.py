#!/usr/bin/env python3
import concurrent.futures
import os
import re
import socket
import subprocess
import sys
import time

server = sys.argv[1]
clients_per_batch = 10
batches = 5
proc = subprocess.Popen([server, "0", str(clients_per_batch * batches)],
                        stdout=subprocess.PIPE, stderr=subprocess.PIPE,
                        text=True, bufsize=1)

def recv_exact(sock, expected):
    parts = []
    remaining = len(expected)
    while remaining:
        data = sock.recv(remaining)
        if not data:
            raise AssertionError("unexpected EOF")
        parts.append(data)
        remaining -= len(data)
    received = b"".join(parts)
    if received != expected:
        raise AssertionError("echo mismatch")


def client_worker(i, port):
    first = (f"client={i};part-one;".encode()) * (1400 if i % 3 == 0 else 10)
    second = (f"client={i};part-two;".encode()) * 7
    with socket.create_connection(("127.0.0.1", port), timeout=4) as sock:
        sock.settimeout(4)
        sock.sendall(first)
        # Before closing the write direction, receive the first echo.
        # This makes the server encounter recv(EAGAIN) on a live connection.
        recv_exact(sock, first)
        time.sleep(0.003)
        sock.sendall(second)
        sock.shutdown(socket.SHUT_WR)
        recv_exact(sock, second)
        if sock.recv(1) != b"":
            raise AssertionError("missing EOF")

try:
    ready = proc.stdout.readline().strip()
    match = re.fullmatch(r"READY 127\.0\.0\.1:(\d+)", ready)
    if not match:
        raise RuntimeError("server not ready: " + ready)
    port = int(match.group(1))
    baseline = len(os.listdir(f"/proc/{proc.pid}/fd"))
    for batch in range(batches):
        with concurrent.futures.ThreadPoolExecutor(max_workers=10) as executor:
            futures = [executor.submit(client_worker, batch * 10 + i, port)
                       for i in range(10)]
            for future in futures:
                future.result()
        for i in range(10):
            line = proc.stdout.readline().strip()
            if not re.fullmatch(r"CLOSED \d+ ACTIVE \d+", line):
                raise AssertionError("unexpected server log: " + line)
            if i == 9 and not line.endswith("ACTIVE 0"):
                raise AssertionError("clients were not released: " + line)
        if batch < batches - 1:
            current = len(os.listdir(f"/proc/{proc.pid}/fd"))
            if current != baseline:
                raise AssertionError(f"FD leak: baseline={baseline}, current={current}")
    done = proc.stdout.readline().strip()
    match = re.fullmatch(r"DONE clients=50 failed=0 eagain_reads=(\d+) active=0", done)
    if not match or int(match.group(1)) <= 0:
        raise AssertionError("unexpected completion: " + done)
    if proc.wait(timeout=5) != 0:
        raise AssertionError("nonzero server exit")
    print("PASS: 10 concurrent clients x 5 batches (50 total)")
    print("PASS: multi-chunk echo, EAGAIN behavior, EOF")
    print("PASS: client FDs released after every batch")
    print(done)
finally:
    if proc.poll() is None:
        proc.terminate()
        try:
            proc.wait(timeout=2)
        except subprocess.TimeoutExpired:
            proc.kill()
            proc.wait()
