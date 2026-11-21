#!/usr/bin/env python3
"""Isolated task admission and completion benchmark (Python standard library).

Does not start MySQL or Redis and does not stop any existing services.
Separates 202 submission latency from asynchronous task completion throughput.
"""
import argparse
from collections import Counter
from concurrent.futures import ThreadPoolExecutor, as_completed
import json
import math
import os
from pathlib import Path
import secrets
import socket
import subprocess
import time
import urllib.error
import urllib.request

TERMINAL = {"completed", "failed", "cancelled", "timed_out", "rejected"}


def free_port():
    with socket.socket() as sock:
        sock.bind(("127.0.0.1", 0))
        return sock.getsockname()[1]


def fetch(base, key, method, path, payload=None, timeout=4):
    raw = json.dumps(payload).encode() if payload is not None else None
    req = urllib.request.Request(base + path, data=raw, method=method,
                                 headers={"X-API-Key": key, "Content-Type": "application/json"})
    started = time.perf_counter()
    try:
        with urllib.request.urlopen(req, timeout=timeout) as r:
            status, body = r.status, r.read()
    except urllib.error.HTTPError as e:
        status, body = e.code, e.read()
    except Exception as e:
        return 0, {}, (time.perf_counter() - started) * 1000, type(e).__name__
    try:
        parsed = json.loads(body)
    except ValueError:
        parsed = {}
    return status, parsed, (time.perf_counter() - started) * 1000, None


def pct(values, fraction):
    if not values:
        return None
    ordered = sorted(values)
    return round(ordered[math.ceil(len(ordered) * fraction) - 1], 3)


def run(args):
    host_port = free_port()
    base = f"http://127.0.0.1:{host_port}"
    key = "day5-benchmark-" + secrets.token_urlsafe(24)
    env = os.environ.copy()
    for name in ("MYSQL_HOST", "MYSQL_PASSWORD", "MYSQL_PORT", "REDIS_HOST", "REDIS_PORT"):
        env.pop(name, None)
    env["AGENT_API_KEY"] = key
    proc = subprocess.Popen([str(args.server), str(host_port)], env=env,
                            stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    try:
        for _ in range(120):
            if proc.poll() is not None:
                raise RuntimeError("test server exited before health check")
            if fetch(base, key, "GET", "/health", timeout=0.3)[0] == 200:
                break
            time.sleep(0.05)
        else:
            raise TimeoutError("test server did not become healthy")

        def submit(_):
            return fetch(base, key, "POST", "/api/v1/tasks",
                         {"title": "day5-completion-check", "work_ms": args.work_ms,
                          "deadline_ms": args.deadline_ms})

        start = time.perf_counter()
        with ThreadPoolExecutor(max_workers=args.concurrency) as pool:
            futures = [pool.submit(submit, i) for i in range(args.requests)]
            results = [f.result() for f in as_completed(futures)]
        submission_wall = time.perf_counter() - start
        statuses = Counter(str(r[0]) for r in results)
        errors = Counter(r[1].get("error", "unknown") for r in results if r[0] >= 400)
        transports = Counter(r[3] for r in results if r[3])
        accepted_ids = {r[1]["id"] for r in results
                        if r[0] == 202 and isinstance(r[1].get("id"), int)}
        if statuses.get("202", 0) != len(accepted_ids):
            raise RuntimeError("received accepted tasks without unique IDs")

        pending = set(accepted_ids)
        final = {}
        poll_deadline = time.monotonic() + args.wait_seconds
        while pending and time.monotonic() < poll_deadline:
            # Poll concurrently to avoid making a long queue of sequential status requests.
            with ThreadPoolExecutor(max_workers=min(16, len(pending))) as pool:
                futures = {pool.submit(fetch, base, key, "GET", f"/api/v1/tasks/{id}"): id
                           for id in pending}
                for f in as_completed(futures):
                    id = futures[f]
                    status, response, _, transport_error = f.result()
                    if transport_error:
                        continue
                    if status == 200 and response.get("status") in TERMINAL:
                        final[id] = response["status"]
            pending -= final.keys()
            if pending:
                time.sleep(0.025)
        total_wall = time.perf_counter() - start

        completed = sum(1 for status in final.values() if status == "completed")
        report = {
            "requests": args.requests,
            "concurrency": args.concurrency,
            "work_ms": args.work_ms,
            "submission_status_counts": dict(sorted(statuses.items())),
            "rejection_reasons": dict(sorted(errors.items())),
            "transport_errors": dict(sorted(transports.items())),
            "accepted": len(accepted_ids),
            "accepted_rate": round(len(accepted_ids) / args.requests, 5),
            "submission_wall_seconds": round(submission_wall, 4),
            "accepted_rps_during_submission": round(len(accepted_ids) / submission_wall, 3),
            "submission_latency_p50_ms": pct([r[2] for r in results], .50),
            "submission_latency_p95_ms": pct([r[2] for r in results], .95),
            "final_task_status_counts": dict(sorted(Counter(final.values()).items())),
            "still_pending_at_timeout": len(pending),
            "completed": completed,
            "completed_rate_of_accepted": round(completed / len(accepted_ids), 5) if accepted_ids else None,
            "total_wall_through_completion_seconds": round(total_wall, 4),
            "completed_rps_end_to_end": round(completed / total_wall, 3),
        }
        print(json.dumps(report, ensure_ascii=False, indent=2))
        if args.output:
            args.output.parent.mkdir(parents=True, exist_ok=True)
            args.output.write_text(json.dumps(report, ensure_ascii=False, indent=2) + "\n")
        if pending:
            raise RuntimeError(f"{len(pending)} accepted tasks still pending after {args.wait_seconds}s")
    finally:
        if proc.poll() is None:
            proc.terminate()
            try:
                proc.wait(timeout=4)
            except subprocess.TimeoutExpired:
                proc.kill()
                proc.wait(timeout=4)


def main():
    p = argparse.ArgumentParser(description="Local in-memory async task completion benchmark")
    p.add_argument("server", type=Path)
    p.add_argument("--requests", type=int, default=200)
    p.add_argument("--concurrency", type=int, default=20)
    p.add_argument("--work-ms", type=int, default=20)
    p.add_argument("--deadline-ms", type=int, default=5000)
    p.add_argument("--wait-seconds", type=float, default=15)
    p.add_argument("--output", type=Path)
    a = p.parse_args()
    if not (1 <= a.requests <= 5000 and 1 <= a.concurrency <= min(100, a.requests)):
        p.error("requests 1..5000 and concurrency 1..min(requests,100) required")
    if not (1 <= a.work_ms <= a.deadline_ms <= 60000):
        p.error("1 <= work_ms <= deadline_ms <= 60000 required")
    if not (1 <= a.wait_seconds <= 120):
        p.error("wait-seconds must be 1..120")
    run(a)


if __name__ == "__main__":
    main()
