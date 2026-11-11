#!/usr/bin/env python3
"""End-to-end HTTP idempotency test; --persistence tests MySQL across server restart."""
import json
import socket
import subprocess
import sys
import time
import urllib.error
import urllib.request


def free_port():
    with socket.socket() as s:
        s.bind(("127.0.0.1", 0))
        return s.getsockname()[1]


def request(base, method, path, body=None, key=None):
    headers = {}
    raw = None
    if body is not None:
        raw = json.dumps(body).encode("utf-8")
        headers["Content-Type"] = "application/json"
    if key:
        headers["Idempotency-Key"] = key
    req = urllib.request.Request(base + path, data=raw, headers=headers, method=method)
    try:
        with urllib.request.urlopen(req, timeout=3) as r:
            return r.status, json.loads(r.read())
    except urllib.error.HTTPError as e:
        return e.code, json.loads(e.read())


def start(executable, port):
    p = subprocess.Popen([executable, str(port)], stdout=subprocess.DEVNULL, stderr=subprocess.PIPE, text=True)
    base = f"http://127.0.0.1:{port}"
    deadline = time.monotonic() + 6
    while time.monotonic() < deadline:
        if p.poll() is not None:
            raise RuntimeError("server exited: " + p.stderr.read())
        try:
            if request(base, "GET", "/health")[0] == 200:
                return p
        except Exception:
            time.sleep(0.05)
    stop(p)
    raise TimeoutError("server not ready")


def stop(p):
    if p.poll() is None:
        p.terminate()
        try:
            p.wait(timeout=4)
        except subprocess.TimeoutExpired:
            p.kill()
            p.wait(timeout=4)
    if p.stderr:
        p.stderr.close()


def main():
    executable = sys.argv[1]
    persist = len(sys.argv) > 2 and sys.argv[2] == "--persistence"
    port = free_port()
    base = f"http://127.0.0.1:{port}"
    key = "day2-http-" + str(time.time_ns())
    body = {"title": "Day 2 idempotency test", "work_ms": 10, "deadline_ms": 5000}
    p = start(executable, port)
    try:
        first_code, first = request(base, "POST", "/api/v1/tasks", body, key)
        assert first_code == 202, (first_code, first)
        second_code, second = request(base, "POST", "/api/v1/tasks", body, key)
        assert second_code == 200 and second["id"] == first["id"] and second["replayed"]
        changed = {**body, "work_ms": 20}
        conflict_code, conflict = request(base, "POST", "/api/v1/tasks", changed, key)
        assert conflict_code == 409 and conflict["error"] == "idempotency_conflict"
    finally:
        stop(p)

    if persist:
        p = start(executable, port)
        try:
            get_code, existing = request(base, "GET", "/api/v1/tasks/" + str(first["id"]))
            assert get_code == 200 and existing["id"] == first["id"]
            again_code, again = request(base, "POST", "/api/v1/tasks", body, key)
            assert again_code == 200 and again["id"] == first["id"]
        finally:
            stop(p)
    print("PASS: idempotency 202/200/409" + (" and MySQL restart persistence" if persist else ""))


if __name__ == "__main__":
    main()
