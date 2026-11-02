
#!/usr/bin/env bash
set -euo pipefail

binary="$1"
workdir="$2"

mkdir -p "$workdir/logs"

rm -f "$workdir/logs/day14_events.jsonl"
rm -f "$workdir/shutdown.out"

output="$workdir/shutdown.out"

# 启动独立服务进程
(
    cd "$workdir"
    exec "$binary"
) > "$output" 2>&1 &

pid=$!

cleanup() {
    if [[ "$pid" -gt 0 ]] && kill -0 "$pid" 2>/dev/null; then
        kill -KILL "$pid" 2>/dev/null || true
    fi

    if [[ "$pid" -gt 0 ]]; then
        wait "$pid" 2>/dev/null || true
    fi
}

trap cleanup EXIT

# 等待服务进入 READY 状态
ready=0

for _ in $(seq 1 200); do
    if grep -Fq "READY PID=$pid" "$output"; then
        ready=1
        break
    fi

    if ! kill -0 "$pid" 2>/dev/null; then
        cat "$output"
        echo "FAIL: service exited before READY"
        exit 1
    fi

    sleep 0.02
done

if [[ "$ready" -ne 1 ]]; then
    cat "$output"
    echo "FAIL: service READY timeout"
    exit 1
fi

echo "Service ready, PID=$pid"

# 在任务运行期间发送 SIGTERM
kill -TERM "$pid"

if ! wait "$pid"; then
    pid=0
    cat "$output"
    echo "FAIL: service exited with error"
    exit 1
fi

pid=0

# 验证任务排空和关闭行为
grep -Fxq "Completed tasks: 12/12" "$output"
grep -Fxq "Submit after shutdown: REJECTED" "$output"
grep -Fxq "Shutdown complete." "$output"

# 验证日志
log="$workdir/logs/day14_events.jsonl"

test -f "$log"

line_count=$(wc -l < "$log")
test "$line_count" -eq 2

grep -Fq '"event":"shutdown_requested"' "$log"
grep -Fq '"event":"shutdown_complete"' "$log"

echo "Task drain: PASS"
echo "Submit rejection: PASS"
echo "SIGTERM shutdown: PASS"
echo "JSON events: PASS"
echo "Day14 shutdown integration tests passed"
