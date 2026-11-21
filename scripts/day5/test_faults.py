#!/usr/bin/env python3
"""Isolated gateway fault tests. Does NOT stop Redis/MySQL or existing containers.

Scenarios:
  redis-down: an unreachable local Redis port -> 503 only for rate-limited writes.
  mysql-down: an unreachable local MySQL port -> startup refusal.
"""
import json
import os
import socket
import subprocess
import sys
import time
import urllib.error
import urllib.request

TEST_KEY = "day5-fault-testing-key-only-2026"


def unused_loopback_port():
    with socket.socket() as s:
        s.bind(("127.0.0.1", 0))
        return s.getsockname()[1]


def clean_env():
    env = os.environ.copy()
    for k in ("MYSQL_HOST", "MYSQL_PORT", "MYSQL_PASSWORD", "MYSQL_USER",
              "REDIS_HOST", "REDIS_PORT", "REDIS_TIMEOUT_MS", "AGENT_API_KEY"):
        env.pop(k, None)
    env["AGENT_API_KEY"] = TEST_KEY
    return env


def request(base, method, path, data=None, key=None):
    headers = {}
    body = None
    if data is not None:
        body = json.dumps(data).encode("utf-8")
        headers["Content-Type"] = "application/json"
    if key is not None:
        headers["X-API-Key"] = key
    req = urllib.request.Request(base + path, data=body, headers=headers, method=method)
    try:
        with urllib.request.urlopen(req, timeout=3) as resp:
            return resp.status, json.loads(resp.read())
    except urllib.error.HTTPError as exc:
        return exc.code, json.loads(exc.read())


def stop(proc):
    if proc.poll() is None:
        proc.terminate()
        try:
            proc.wait(timeout=3)
        except subprocess.TimeoutExpired:
            proc.kill()
            proc.wait(timeout=3)
    for stream in (proc.stdout, proc.stderr):
        if stream:
            stream.close()


def redis_down(server):
    port = unused_loopback_port()
    redis_port = unused_loopback_port()
    env = clean_env()
    env["REDIS_HOST"] = "127.0.0.1"
    env["REDIS_PORT"] = str(redis_port)
    env["REDIS_TIMEOUT_MS"] = "150"
    proc = subprocess.Popen([server, str(port)], env=env,
                            stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    base = f"http://127.0.0.1:{port}"
    try:
        for _ in range(120):
            if proc.poll() is not None:
                raise RuntimeError("gateway unexpectedly exited")
            try:
                if request(base, "GET", "/health")[0] == 200:
                    break
            except (OSError, ValueError):
                time.sleep(0.05)
        else:
            raise TimeoutError("gateway not ready")

        assert request(base, "GET", "/health") == (200, {"status": "ok"})
        assert request(base, "GET", "/api/v1/tasks")[0] == 401
        assert request(base, "GET", "/api/v1/tasks", key=TEST_KEY)[0] == 200
        body = {"title": "redis-failure-injection", "work_ms": 10, "deadline_ms": 1000}
        code, response = request(base, "POST", "/api/v1/tasks", body, TEST_KEY)
        assert (code, response.get("error")) == (503, "rate_limiter_unavailable"), (code, response)
        # Failure of the limiter doesn't take down health and unrelated tools.
        assert request(base, "GET", "/health")[0] == 200
        assert request(base, "POST", "/api/v1/tools/lookup_policy",
                       {"input": "returns"}, TEST_KEY)[0] == 200
        print("PASS: Redis unavailable -> POST 503; health/tools remain available; auth 401")
    finally:
        stop(proc)


def mysql_down(server):
    port = unused_loopback_port()
    mysql_port = unused_loopback_port()
    env = clean_env()
    env["MYSQL_HOST"] = "127.0.0.1"
    env["MYSQL_PORT"] = str(mysql_port)
    env["MYSQL_USER"] = "fake-test-user"
    env["MYSQL_PASSWORD"] = "synthetic-testing-password"  # not a real credential
    env["MYSQL_DATABASE"] = "unreachable_test_db"
    proc = subprocess.Popen([server, str(port)], env=env,
                            stdout=subprocess.PIPE, stderr=subprocess.PIPE)
    try:
        try:
            stdout, stderr = proc.communicate(timeout=8)
        except subprocess.TimeoutExpired:
            proc.kill()
            proc.communicate()
            raise AssertionError("gateway did not fail promptly when MySQL was unavailable")
        assert proc.returncode != 0, "gateway incorrectly started without required MySQL"
        assert b"READY http" not in stdout, "gateway announced READY without MySQL"
        assert b"synthetic-testing-password" not in stderr, "credential leaked to logs"
        print("PASS: MySQL unavailable -> startup refused; test credential not exposed")
    finally:
        stop(proc)


def main():
    if len(sys.argv) != 3 or sys.argv[1] not in ("redis-down", "mysql-down"):
        raise SystemExit("Usage: test_faults.py {redis-down|mysql-down} /path/to/agent_http_server")
    if sys.argv[1] == "redis-down":
        redis_down(sys.argv[2])
    else:
        mysql_down(sys.argv[2])


if __name__ == "__main__":
    main()
