
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
def wait_for_task_status(
    base,
    task_id,
    expected_status,
    timeout=3.0
):
    deadline = time.monotonic() + timeout
    last_status = None

    while time.monotonic() < deadline:
        status, _, body = request(
            base,
            "GET",
            f"/api/v1/tasks/{task_id}"
        )

        assert status == 200

        task = parse_json(body)
        last_status = task["status"]

        if last_status == expected_status:
            return task

        if last_status in ("failed","cancelled","rejected","timed_out"):
                break

        time.sleep(0.02)

    raise AssertionError(
        f"Task {task_id} did not reach {expected_status}, "
        f"last status: {last_status}"
    )

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
    elif scenario == "async_concurrent":

        task_ids = []
        task_count = 20

        # 1. 连续提交 20 个任务
        for i in range(task_count):
            status, _, body = request(
                base,
                "POST",
                "/api/v1/tasks",
                payload={"title": f"Concurrent task {i}"}
            )

            assert status == 202

            created = parse_json(body)

            assert created["status"] == "accepted"

            task_ids.append(created["id"])

        # 2. 验证 ID 唯一
        assert len(task_ids) == task_count
        assert len(set(task_ids)) == task_count

        # 3. 等待所有任务完成
        for i, task_id in enumerate(task_ids):
            task = wait_for_task_status(
                base,
                task_id,
                "completed",
                timeout=5.0
            )

            assert task["id"] == task_id
            assert task["title"] == f"Concurrent task {i}"
            assert task["status"] == "completed"

        print("20 task submissions: PASS")
        print("unique task IDs: PASS")
        print("20 task results matched: PASS")
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

        assert status == 202
        assert created["status"] == "accepted"
        assert created["id"] == 1
        assert created["title"] == "Build HTTP API"
        assert created["status"] == "accepted"
        status, _, body = request(
            base, "GET", "/api/v1/tasks"
        )

        tasks = parse_json(body)["tasks"]

        assert status == 200
        assert len(tasks) == 1
        assert tasks[0]["id"] == 1
        assert tasks[0]["title"] == "Build HTTP API"
        assert tasks[0]["status"] in (
            "pending",
            "running",
            "completed"
        )

    elif scenario == "task_get":
        # 1. 创建任务
        status, _, body = request(
            base,
            "POST",
            "/api/v1/tasks",
            payload={"title": "Build HTTP API"}
        )

        assert status == 202

        created = parse_json(body)
        task_id = created["id"]

        assert created["status"] == "accepted"

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
        assert task["status"] in (
        "pending",
        "running",
        "completed"
)

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
    elif scenario == "async_task":

        # 1. 提交异步任务
        status, _, body = request(
            base,
            "POST",
            "/api/v1/tasks",
            payload={"title": "Async test"}
        )

        assert status == 202

        created = parse_json(body)
        task_id = created["id"]

        assert created["title"] == "Async test"
        assert created["status"] == "accepted"
        assert created["status_url"] == (
            f"/api/v1/tasks/{task_id}"
        )

        # 2. 轮询任务，直到完成
        task = wait_for_task_status(
            base,
            task_id,
            "completed"
        )

        assert task["id"] == task_id
        assert task["title"] == "Async test"
        assert task["status"] == "completed"

        print("async task submission: PASS")
        print("async task completion: PASS")
    elif scenario == "task_cancel_deadline":

        # 1. 运行中任务取消
        status, _, body = request(
            base,
            "POST",
            "/api/v1/tasks",
            payload={
                "title": "Running cancellation",
                "work_ms": 1500,
                "deadline_ms": 5000
            }
        )

        assert status == 202
        task_id = parse_json(body)["id"]

        wait_for_task_status(base, task_id, "running")

        status, _, body = request(
            base,
            "POST",
            f"/api/v1/tasks/{task_id}/cancel"
        )

        assert status == 202
        assert parse_json(body)["status"] == "cancellation_requested"

        wait_for_task_status(base, task_id, "cancelled")

        print("running task cancellation: PASS")

        # 2. Deadline 超时
        status, _, body = request(
            base,
            "POST",
            "/api/v1/tasks",
            payload={
                "title": "Deadline test",
                "work_ms": 500,
                "deadline_ms": 50
            }
        )

        assert status == 202
        task_id = parse_json(body)["id"]

        task = wait_for_task_status(
            base,
            task_id,
            "timed_out"
        )

        assert task["status"] == "timed_out"

        print("deadline timeout: PASS")

        # 3. 已完成任务不能取消
        status, _, body = request(
            base,
            "POST",
            "/api/v1/tasks",
            payload={
                "title": "Completed task",
                "work_ms": 10,
                "deadline_ms": 5000
            }
        )

        assert status == 202
        task_id = parse_json(body)["id"]

        wait_for_task_status(base, task_id, "completed")

        status, _, body = request(
            base,
            "POST",
            f"/api/v1/tasks/{task_id}/cancel"
        )

        assert status == 409
        assert parse_json(body)["error"] == "task_not_cancellable"

        print("completed task protection: PASS")

        # 4. 非法 Deadline
        status, _, body = request(
            base,
            "POST",
            "/api/v1/tasks",
            payload={
                "title": "Invalid deadline",
                "deadline_ms": 0
            }
        )

        assert status == 400
        assert parse_json(body)["error"] == "invalid_deadline_ms"

        print("invalid deadline rejection: PASS")

        # 5. 排队任务取消
        queued_ids = []

        for i in range(64):
            status, _, body = request(
                base,
                "POST",
                "/api/v1/tasks",
                payload={
                    "title": f"Queued task {i}",
                    "work_ms": 3000,
                    "deadline_ms": 15000
                }
            )

            assert status == 202
            queued_ids.append(parse_json(body)["id"])

        queued_id = queued_ids[-1]

        status, _, body = request(
            base,
            "GET",
            f"/api/v1/tasks/{queued_id}"
        )

        assert status == 200

        queued_task = parse_json(body)

        assert queued_task["status"] == "pending"

        status, _, body = request(
            base,
            "POST",
            f"/api/v1/tasks/{queued_id}/cancel"
        )

        assert status == 200
        assert parse_json(body)["status"] == "cancelled"

        print("queued task cancellation: PASS")
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
