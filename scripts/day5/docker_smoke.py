#!/usr/bin/env python3
"""Run a unique temporary gateway container and verify auth/health; never touch other containers."""
import argparse
import json
import secrets
import subprocess
import time
import urllib.error
import urllib.request


def get(url, key=None):
    headers = {"X-API-Key": key} if key else {}
    req = urllib.request.Request(url, headers=headers)
    try:
        with urllib.request.urlopen(req, timeout=2) as resp:
            return resp.status
    except urllib.error.HTTPError as error:
        return error.code


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--image", default="agent-gateway-day5:local")
    parser.add_argument("--port", type=int, default=18080)
    args = parser.parse_args()
    if args.port < 1024 or args.port > 65535:
        parser.error("port must be 1024..65535")
    name = "agent-gateway-day5-smoke-" + secrets.token_hex(4)
    key = "smoke-only-" + secrets.token_urlsafe(24)
    port = f"127.0.0.1:{args.port}:8080"
    base = f"http://127.0.0.1:{args.port}"
    subprocess.run(["docker", "run", "-d", "--name", name, "-p", port,
                    "-e", "AGENT_API_KEY=" + key, args.image],
                   check=True, stdout=subprocess.DEVNULL)
    try:
        for _ in range(80):
            try:
                if get(base + "/health") == 200:
                    break
            except Exception:
                time.sleep(0.15)
        else:
            raise RuntimeError("container did not become healthy")
        unauth = get(base + "/api/v1/tasks")
        auth = get(base + "/api/v1/tasks", key)
        assert (unauth, auth) == (401, 200), (unauth, auth)
        print(json.dumps({"health": 200, "without_api_key": unauth,
                          "with_api_key": auth, "result": "PASS"}, indent=2))
    finally:
        subprocess.run(["docker", "rm", "-f", name],
                       check=False, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)


if __name__ == "__main__":
    main()
