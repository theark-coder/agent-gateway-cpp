#!/usr/bin/env python3
"""Local HTTP integration test. No real credential or database is required."""
import json
import os
import socket
import subprocess
import sys
import time
import urllib.error
import urllib.request

KEY = "day5-integration-key-123456789"


def free_port():
    with socket.socket() as sock:
        sock.bind(("127.0.0.1", 0))
        return sock.getsockname()[1]


def request(base, method, path, body=None, key=None):
    headers = {}
    raw = None
    if body is not None:
        raw = json.dumps(body).encode("utf-8")
        headers["Content-Type"] = "application/json"
    if key is not None:
        headers["X-API-Key"] = key
    req = urllib.request.Request(base + path, data=raw, headers=headers, method=method)
    try:
        with urllib.request.urlopen(req, timeout=4) as response:
            return response.status, json.loads(response.read())
    except urllib.error.HTTPError as exc:
        return exc.code, json.loads(exc.read())


def start(executable, port):
    env = os.environ.copy()
    # Ensure this test exercises only the in-memory store, without touching
    # the developer's MySQL, Redis, or other existing applications.
    env.pop("MYSQL_HOST", None)
    env.pop("REDIS_HOST", None)
    env["AGENT_API_KEY"] = KEY
    process = subprocess.Popen([executable, str(port)], env=env,
                               stdout=subprocess.DEVNULL, stderr=subprocess.PIPE)
    base = f"http://127.0.0.1:{port}"
    for _ in range(100):
        if process.poll() is not None:
            raise RuntimeError("HTTP server exited before readiness")
        try:
            if request(base, "GET", "/health")[0] == 200:
                return process
        except Exception:
            time.sleep(0.05)
    stop(process)
    raise TimeoutError("HTTP server not ready")


def stop(process):
    if process.poll() is None:
        process.terminate()
        try:
            process.wait(timeout=4)
        except subprocess.TimeoutExpired:
            process.kill()
            process.wait(timeout=4)
    if process.stderr:
        process.stderr.close()


def main():
    executable = sys.argv[1]
    port = free_port()
    base = f"http://127.0.0.1:{port}"
    process = start(executable, port)
    try:
        assert request(base, "GET", "/health")[0] == 200
        assert request(base, "GET", "/api/v1/tasks") == (401, {"error": "unauthorized"})
        assert request(base, "GET", "/api/v1/tasks", key="wrong") [0] == 401
        assert request(base, "GET", "/api/v1/tasks", key=KEY)[0] == 200
        assert request(base, "GET", "/api/v1/unknown")[0] == 401
        assert request(base, "GET", "/api/v1/unknown", key=KEY)[0] == 404

        payload = {"title": "day5 protected", "work_ms": 30, "deadline_ms": 5000}
        assert request(base, "POST", "/api/v1/tasks", payload)[0] == 401
        assert request(base, "POST", "/api/v1/tasks", payload, "wrong")[0] == 401
        code, created = request(base, "POST", "/api/v1/tasks", payload, KEY)
        assert code == 202, (code, created)
        task_id = created["id"]
        assert request(base, "GET", f"/api/v1/tasks/{task_id}")[0] == 401
        assert request(base, "GET", f"/api/v1/tasks/{task_id}", key=KEY)[0] == 200
        assert request(base, "POST", f"/api/v1/tasks/{task_id}/cancel")[0] == 401
        assert request(base, "POST", "/api/v1/tools/lookup_policy", {"input": "returns"})[0] == 401
        assert request(base, "POST", "/api/v1/tools/lookup_policy", {"input": "returns"}, KEY)[0] == 200
        print("PASS: /health public; API routes 401 without key; 200/202 with key")
    finally:
        stop(process)


if __name__ == "__main__":
    main()
