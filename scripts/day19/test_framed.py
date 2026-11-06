#!/usr/bin/env python3
"""Day19 length-prefixed epoll LT integration tests (stdlib only)."""
import concurrent.futures
import os
import re
import select
import socket
import struct
import subprocess
import sys
import threading
import time

MAX_FRAME = 64 * 1024


def frame(data: bytes) -> bytes:
    assert len(data) <= MAX_FRAME
    return struct.pack('!I', len(data)) + data


def connect(port: int) -> socket.socket:
    s = socket.create_connection(('127.0.0.1', port), timeout=6)
    s.settimeout(12)
    return s


def recv_all(sock: socket.socket) -> bytes:
    out = bytearray()
    while True:
        chunk = sock.recv(64 * 1024)
        if not chunk:
            return bytes(out)
        out.extend(chunk)


def verify(port: int, encoded: bytes, chunks=None) -> None:
    with connect(port) as s:
        if chunks is None:
            s.sendall(encoded)
        else:
            for chunk in chunks:
                s.sendall(chunk)
                time.sleep(0.005)
        s.shutdown(socket.SHUT_WR)
        got = recv_all(s)
        assert got == encoded, (len(got), len(encoded), got[:20], encoded[:20])


def run_slow(port: int) -> None:
    # The sender may block while the receiver sleeps; use full-duplex threads.
    payloads = [os.urandom(32 * 1024) for _ in range(72)]
    expected = b''.join(map(frame, payloads))
    failures = []
    with connect(port) as s:
        s.settimeout(18)

        def writer():
            try:
                s.sendall(expected)
                s.shutdown(socket.SHUT_WR)
            except Exception as ex:
                failures.append(ex)

        worker = threading.Thread(target=writer, daemon=True)
        worker.start()
        # Simulate a receiver which has stopped consuming briefly.
        time.sleep(0.15)
        got = recv_all(s)
        worker.join(timeout=3)
        assert not worker.is_alive(), 'sender stuck'
        assert not failures, failures
        assert got == expected, (len(got), len(expected))


def run_server(binary: str, count: int, action):
    proc = subprocess.Popen(
        [binary, '0', str(count)], stdout=subprocess.PIPE,
        stderr=subprocess.STDOUT, text=True, bufsize=1)
    try:
        ready, _, _ = select.select([proc.stdout], [], [], 5)
        assert ready, 'server readiness timeout'
        first = proc.stdout.readline().strip()
        match = re.fullmatch(r'READY 127\.0\.0\.1:(\d+)', first)
        assert match, 'unexpected server banner: ' + first
        port = int(match.group(1))
        action(port)
        remaining, _ = proc.communicate(timeout=18)
        output = first + '\n' + remaining
        assert proc.returncode == 0, output
        assert f'DONE clients={count}' in output, output
        assert 'failed=0' in output and 'active=0' in output, output
        print(output.strip())
        return output
    finally:
        if proc.poll() is None:
            proc.kill()
            proc.communicate(timeout=3)


def main(binary: str, scenario: str) -> None:
    if scenario == 'fragmented':
        message = frame(b'hello fragmented tcp message')
        chunks = [message[:1], message[1:3], message[3:4],
                  message[4:7], message[7:]]
        run_server(binary, 1, lambda port: verify(port, message, chunks))
    elif scenario == 'coalesced':
        message = frame(b'') + frame(b'abc') + frame(b'hello') + frame(b'Z' * MAX_FRAME)
        run_server(binary, 1, lambda port: verify(port, message))
    elif scenario == 'oversized':
        def send_oversize(port):
            with connect(port) as s:
                s.sendall(struct.pack('!I', MAX_FRAME + 1))
                try:
                    result = s.recv(16)
                except ConnectionResetError:
                    result = b''
                assert result == b'', 'oversized frame was not rejected'
        output = run_server(binary, 1, send_oversize)
        assert 'rejected=1' in output, output
    elif scenario == 'slow':
        run_server(binary, 1, run_slow)
    elif scenario == 'concurrent':
        def many(port):
            with concurrent.futures.ThreadPoolExecutor(max_workers=10) as executor:
                futures = []
                for i in range(10):
                    packet = frame(f'client-{i}'.encode()) + frame(os.urandom(8192))
                    futures.append(executor.submit(verify, port, packet))
                for future in futures:
                    future.result(timeout=15)
        run_server(binary, 10, many)
    else:
        raise ValueError('unknown scenario: ' + scenario)
    print('PASS:', scenario)


if __name__ == '__main__':
    if len(sys.argv) != 3:
        raise SystemExit('usage: test_framed.py SERVER fragmented|coalesced|oversized|slow|concurrent')
    main(sys.argv[1], sys.argv[2])
