# agent-gateway-cpp — Day 1 最小可运行基架

C++17 / CMake / CTest，无第三方依赖，默认纯本地可构建。

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug
cmake --build build --parallel
ctest --test-dir build --output-on-failure
./build/agent_gateway
```

Windows 原生多配置生成器（如 Visual Studio）请使用：
```powershell
cmake -S . -B build
cmake --build build --config Debug
ctest --test-dir build -C Debug --output-on-failure
.\build\Debug\agent_gateway.exe
```

练习：阅读 3 个 target（agent_core、agent_gateway、agent_tests）；修改 health_message 文本与测试预期，重建测试。

## Day 16: Concurrent Task System v0.2

### Architecture

- TaskService: thread-safe task storage and lifecycle management.
- BoundedQueue: bounded task admission and graceful draining.
- ThreadPool: fixed worker count and asynchronous execution.
- CancellationToken: cooperative task cancellation.
- ThreadPoolStats: submitted and completed task statistics.

### Task Lifecycle

pending -> running -> completed
                    -> failed
                    -> cancelled

pending -> rejected
pending -> cancelled

### Integration Tests

| Scenario | Expected |
|---|---|
| Normal execution | Correct task ID and result |
| Exception isolation | Failed task does not stop other workers |
| Queue full | New task is rejected |
| Cooperative cancellation | Worker observes cancellation |
| Graceful shutdown | Accepted tasks are drained |

### Verification

- CTest: 12/12 passed
- BoundedQueue stress: 20/20 passed
- Maximum worker count: bounded
- Task results: isolated by task ID
### Individual Demonstrations

Run individual concurrency scenarios:

```bash
bash scripts/day16/demo_normal.sh
bash scripts/day16/demo_exception.sh
bash scripts/day16/demo_queue_full.sh
bash scripts/day16/demo_cancellation.sh
bash scripts/day16/demo_shutdown.sh