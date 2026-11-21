#!/usr/bin/env python3
"""Local, isolated HTTP load test. Stdlib only; does not touch existing services.

Starts its own server on 127.0.0.1 with in-memory TaskService and an ephemeral API key.
Reports successes/status breakdown, P50/P95 in milliseconds and achieved requests/s.
"""
import argparse
from concurrent.futures import ThreadPoolExecutor, as_completed
import json
import math
import os
from pathlib import Path
import secrets
import socket
import subprocess
import sys
import time
import urllib.error
import urllib.request


def available_port():
    with socket.socket() as sock:
        sock.bind(("127.0.0.1", 0))
        return sock.getsockname()[1]


def request(base, key, endpoint, timeout=5):
    if endpoint == "health":
        path, method, data = "/health", "GET", None
    elif endpoint == "tool":
        path, method, data = "/api/v1/tools/lookup_policy", "POST", {"input": "returns"}
    else:
        path, method, data = "/api/v1/tasks", "POST", {
            "title": "day5-load-test", "work_ms": 20, "deadline_ms": 5000}
    headers = {"X-API-Key": key, "Content-Type": "application/json"}
    payload = json.dumps(data).encode() if data is not None else None
    req = urllib.request.Request(base + path, data=payload, headers=headers, method=method)
    start = time.perf_counter_ns()
    try:
        with urllib.request.urlopen(req, timeout=timeout) as resp:
            status = resp.status
            resp.read()
            err = None
    except urllib.error.HTTPError as exc:
        status = exc.code
        exc.read()
        err = None
    except Exception as exc:
        status = 0
        err = type(exc).__name__
    ms = (time.perf_counter_ns() - start) / 1e6
    return status, ms, err


def stop(proc):
    if proc.poll() is None:
        proc.terminate()
        try:
            proc.wait(timeout=4)
        except subprocess.TimeoutExpired:
            proc.kill()
            proc.wait(timeout=4)


def percentile(sorted_values, p):
    if not sorted_values:
        return None
    return round(sorted_values[math.ceil(p * len(sorted_values)) - 1], 3)


def main():
    parser = argparse.ArgumentParser(description="Local HTTP benchmark (not production)")
    parser.add_argument("server", help="Path to agent_http_server executable")
    parser.add_argument("--endpoint", choices=("health", "tool", "task"), default="tool")
    parser.add_argument("--requests", type=int, default=200)
    parser.add_argument("--concurrency", type=int, default=20)
    parser.add_argument("--output", type=Path, default=None, help="Optional JSON report")
    args = parser.parse_args()
    if not (1 <= args.requests <= 10000 and 1 <= args.concurrency <= 100):
        parser.error("requests must be 1..10000; concurrency must be 1..100")
    if args.concurrency > args.requests:
        parser.error("concurrency cannot exceed request count")

    port = available_port()
    key = "day5-local-" + secrets.token_urlsafe(24)
    env = os.environ.copy()
    for name in ("MYSQL_HOST", "MYSQL_PASSWORD", "REDIS_HOST", "REDIS_PORT"):
        env.pop(name, None)
    env["AGENT_API_KEY"] = key
    base = f"http://127.0.0.1:{port}"
    proc = subprocess.Popen([args.server, str(port)], env=env,
                            stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    try:
        for _ in range(120):
            if proc.poll() is not None:
                raise RuntimeError("benchmark server exited before readiness")
            try:
                if request(base, key, "health")[0] == 200:
                    break
            except Exception:
                pass
            time.sleep(.05)
        else:
            raise TimeoutError("benchmark server did not become ready")
        # Warm-up request is not included in the measured total.
        if request(base, key, args.endpoint)[0] // 100 != 2:
            raise RuntimeError("warm-up request was not successful")

        started = time.perf_counter()
        with ThreadPoolExecutor(max_workers=args.concurrency) as executor:
            futures = [executor.submit(request, base, key, args.endpoint)
                       for _ in range(args.requests)]
            outcomes = [future.result() for future in as_completed(futures)]
        wall = time.perf_counter() - started
    finally:
        stop(proc)

    statuses = {}
    errors = {}
    successes = 0
    for status, ms, err in outcomes:
        statuses[str(status)] = statuses.get(str(status), 0) + 1
        if 200 <= status < 300:
            successes += 1
        if err:
            errors[err] = errors.get(err, 0) + 1
    timings = sorted(row[1] for row in outcomes)
    success_timings = sorted(row[1] for row in outcomes if 200 <= row[0] < 300)
    report = {
        "endpoint": args.endpoint,
        "requests": args.requests,
        "concurrency": args.concurrency,
        "successes": successes,
        "success_rate": round(successes / args.requests, 5),
        "status_counts": dict(sorted(statuses.items())),
        "transport_errors": errors,
        "wall_seconds": round(wall, 4),
        "throughput_attempts_rps": round(args.requests / wall, 3),
        "throughput_success_rps": round(successes / wall, 3),
        "latency_p50_ms": percentile(timings, .50),
        "latency_p95_ms": percentile(timings, .95),
        "successful_latency_p95_ms": percentile(success_timings, .95),
    }
    print(json.dumps(report, indent=2, ensure_ascii=False))
    if args.output:
        args.output.parent.mkdir(parents=True, exist_ok=True)
        args.output.write_text(json.dumps(report, indent=2, ensure_ascii=False) + "\n")
    if not successes:
        sys.exit("ERROR: benchmark produced no successful responses")


if __name__ == "__main__":
    main()
