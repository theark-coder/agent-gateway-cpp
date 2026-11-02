#!/usr/bin/env bash
set -euo pipefail

binary="$1"
output=$(mktemp)
trap 'rm -f "$output"' EXIT

"$binary" > "$output" 2>&1

python3 - "$output" <<'PY'
import sys
from pathlib import Path

lines = Path(sys.argv[1]).read_text().splitlines()

for task_id in range(101, 106):
    records = [
        line for line in lines
        if f"task_id={task_id} " in line
    ]

    expected = "failed" if task_id == 103 else "completed"

    assert any("event=started" in x for x in records), \
        f"task {task_id}: missing started"

    assert any(f"event={expected}" in x for x in records), \
        f"task {task_id}: missing {expected}"

assert any(
    "Task results: completed=4 failed=1" in x
    for x in lines
), "incorrect task summary"

print("Task ID trace validation: PASS")
print("5 tasks started, 4 completed, 1 failed")
PY
