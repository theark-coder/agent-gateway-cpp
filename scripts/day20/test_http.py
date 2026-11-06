
import json
import socket
import subprocess
import sys
import time
import urllib.request
import urllib.error


def get_free_port():
    with socket.socket() as sock:
        sock.bind(("127.0.0.1", 0))
        return sock.getsockname()[1]


def request(base, method, path, payload=None, raw_body=None):
    if raw_body is not None:
        data = raw_body
    elif payload is not None:
        data = json.dumps(payload).encode()
    else:
        data = None

    headers = {}
    if data is not None:
        headers["Content-Type"] = "application/json"

    req = urllib.request.Request(
        base + path,
        data=data,
        headers=headers,
        method=method
    )

    try:
        with urllib.request.urlopen(req, timeout=2) as response:
            return response.status, response.headers, response.read()

    except urllib.error.HTTPError as error:
        return error.code, error.headers, error.read()


def parse_json(body):
    return json.loads(body.decode())


def wait_ready(base, process):
    deadline = time.monotonic() + 5

    while time.monotonic() < deadline:
        if process.poll() is not None:
            stderr = process.stderr.read()
            raise RuntimeError(
                f"HTTP server exited early: {stderr}"
            )

        try:
            status, _, _ = request(
                base, "GET", "/health"
            )

            if status == 200:
                return

        except Exception:
            time.sleep(0.05)

    raise RuntimeError("HTTP server startup timeout")


def run_scenario(base, scenario):

    if scenario == "health":
        status, headers, body = request(
            base, "GET", "/health"
        )

        assert status == 200
        assert headers.get_content_type() == "application/json"
        assert parse_json(body)["status"] == "ok"

    elif scenario == "task_crud":
        status, _, body = request(
            base, "GET", "/api/v1/tasks"
        )

        assert status == 200
        assert parse_json(body)["tasks"] == []

        status, _, body = request(
            base,
            "POST",
            "/api/v1/tasks",
            payload={"title": "Build HTTP API"}
        )

        created = parse_json(body)

        assert status == 201
        assert created["id"] == 1
        assert created["title"] == "Build HTTP API"
        assert created["status"] == "pending"

        status, _, body = request(
            base, "GET", "/api/v1/tasks"
        )

        tasks = parse_json(body)["tasks"]

        assert status == 200
        assert len(tasks) == 1
        assert tasks[0]["id"] == 1

    elif scenario == "invalid_json":
        status, _, body = request(
            base,
            "POST",
            "/api/v1/tasks",
            raw_body=b'{"title":'
        )

        assert status == 400
        assert parse_json(body)["error"] == "invalid_json"

    elif scenario == "invalid_title":
        status, _, body = request(
            base,
            "POST",
            "/api/v1/tasks",
            payload={"title": ""}
        )

        assert status == 400
        assert parse_json(body)["error"] == "invalid_title"

    elif scenario == "not_found":
        status, _, body = request(
            base, "GET", "/unknown"
        )

        assert status == 404
        assert parse_json(body)["error"] == "not_found"

    else:
        raise ValueError(f"unknown scenario: {scenario}")


def main():
    executable = sys.argv[1]
    scenario = sys.argv[2]

    port = get_free_port()
    base = f"http://127.0.0.1:{port}"

    process = subprocess.Popen(
        [executable, str(port)],
        stdout=subprocess.DEVNULL,
        stderr=subprocess.PIPE,
        text=True
    )

    try:
        wait_ready(base, process)
        run_scenario(base, scenario)

        print(f"PASS: {scenario}")

    finally:
        if process.poll() is None:
            process.terminate()

        try:
            process.wait(timeout=3)
        except subprocess.TimeoutExpired:
            process.kill()
            process.wait()


if __name__ == "__main__":
    main()
