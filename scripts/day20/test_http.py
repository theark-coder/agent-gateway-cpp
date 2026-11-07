
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

    elif scenario == "task_get":
        # 1. 创建任务
        status, _, body = request(
            base,
            "POST",
            "/api/v1/tasks",
            payload={"title": "Build HTTP API"}
        )

        assert status == 201

        created = parse_json(body)
        task_id = created["id"]

        # 2. 查询已创建任务
        status, _, body = request(
            base,
            "GET",
            f"/api/v1/tasks/{task_id}"
        )

        assert status == 200

        task = parse_json(body)

        assert task["id"] == task_id
        assert task["title"] == "Build HTTP API"
        assert task["status"] == "pending"

        # 3. 查询不存在的任务
        status, _, body = request(
            base,
            "GET",
            "/api/v1/tasks/999999"
        )

        assert status == 404
        assert parse_json(body)["error"] == "task_not_found"

        # 4. 查询 ID 为 0 的任务
        status, _, body = request(
            base,
            "GET",
            "/api/v1/tasks/0"
        )

        assert status == 404
        assert parse_json(body)["error"] == "task_not_found"
    elif scenario == "tool_call":

        # 1. 测试 returns
        status, _, body = request(
            base,
            "POST",
            "/api/v1/tools/lookup_policy",
            payload={"input": "returns"}
        )

        result = parse_json(body)

        assert status == 200
        assert result["tool"] == "lookup_policy"
        assert "30 days" in result["output"]

        # 2. 测试 warranty
        status, _, body = request(
            base,
            "POST",
            "/api/v1/tools/lookup_policy",
            payload={"input": "warranty"}
        )

        result = parse_json(body)

        assert status == 200
        assert "1-year limited warranty" in result["output"]

        # 3. 测试未知工具
        status, _, body = request(
            base,
            "POST",
            "/api/v1/tools/unknown_tool",
            payload={"input": "returns"}
        )

        assert status == 404
        assert parse_json(body)["error"] == "unknown_tool"

        # 4. 测试缺少 input
        status, _, body = request(
            base,
            "POST",
            "/api/v1/tools/lookup_policy",
            payload={}
        )

        assert status == 400
        assert parse_json(body)["error"] == "invalid_input"

        # 5. 测试 input 类型错误
        status, _, body = request(
            base,
            "POST",
            "/api/v1/tools/lookup_policy",
            payload={"input": 123}
        )

        assert status == 400
        assert parse_json(body)["error"] == "invalid_input"

        # 6. 测试未知政策主题
        status, _, body = request(
            base,
            "POST",
            "/api/v1/tools/lookup_policy",
            payload={"input": "unknown_topic"}
        )

        assert status == 422
        assert parse_json(body)["error"] == "tool_execution_failed"
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
