#!/usr/bin/env python3
"""Optional integration test for Redis HTTP rate limiting; uses in-memory TaskService."""
import json
import os
import socket
import subprocess
import sys
import time
import urllib.error
import urllib.request


def request(base, title):
    req = urllib.request.Request(
        base + "/api/v1/tasks",
        data=json.dumps({"title": title, "work_ms": 10}).encode(),
        headers={"Content-Type": "application/json"}, method="POST")
    try:
        with urllib.request.urlopen(req, timeout=3) as response:
            return response.status
    except urllib.error.HTTPError as error:
        return error.code


def main():
    if os.getenv("AGENT_REDIS_TEST") != "1":
        print("SKIP: set AGENT_REDIS_TEST=1 to run live Redis tests")
        return 77
    executable = sys.argv[1]
    with socket.socket() as sock:
        sock.bind(("127.0.0.1", 0))
        port = sock.getsockname()[1]
    base = f"http://127.0.0.1:{port}"
    env = os.environ.copy()
    env.pop("MYSQL_HOST", None)
    env["REDIS_RATE_LIMIT"] = "2"
    env["REDIS_RATE_WINDOW"] = "60"
    # The fixed local-dev actor shares a key across processes; clean before and after.
    # Use redis-cli to clear this test key only, never FLUSHDB.
    key = "ag:v1:rl:tasks:local-dev"
    cli = ["docker", "exec", "agent-gateway-redis", "redis-cli", "DEL", key]
    try:
        subprocess.run(cli, check=True, stdout=subprocess.DEVNULL, timeout=5)
    except Exception as ex:
        print("SKIP: Docker Redis CLI unavailable:", ex)
        return 77
    proc = subprocess.Popen([executable, str(port)], env=env,
                            stdout=subprocess.DEVNULL, stderr=subprocess.PIPE, text=True)
    try:
        for _ in range(100):
            if proc.poll() is not None:
                raise RuntimeError("server exited: " + proc.stderr.read())
            try:
                with urllib.request.urlopen(base + "/health", timeout=0.4) as resp:
                    if resp.status == 200:
                        break
            except Exception:
                time.sleep(0.05)
        else:
            raise RuntimeError("server startup timeout")
        statuses = [request(base, "Redis limit " + str(i)) for i in range(3)]
        assert statuses == [202, 202, 429], statuses
        print("PASS: HTTP rate limiting 202/202/429")
    finally:
        if proc.poll() is None:
            proc.terminate()
        try:
            proc.wait(timeout=4)
        except subprocess.TimeoutExpired:
            proc.kill()
            proc.wait()
        subprocess.run(cli, stdout=subprocess.DEVNULL, timeout=5)
        if proc.stderr:
            proc.stderr.close()
    return 0


if __name__ == "__main__":
    sys.exit(main())
