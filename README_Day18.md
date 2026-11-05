# Day 18 - Linux epoll LT TCP Echo

This is an **additional** experiment: it leaves Day 17's blocking echo intact.

## Integration

Extract the package into the existing `agent-gateway-cpp` repository root.
Append exactly once at the end of the root `CMakeLists.txt`:

```cmake
include(cmake/day18_epoll.cmake)
```

Then run:

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug
cmake --build build --parallel
ctest --test-dir build -R '^tcp_epoll_day18$' --output-on-failure
ctest --test-dir build --output-on-failure
```

## Manual demo

Terminal 1:

```bash
./build/agent_tcp_epoll_server 9091 10
```

Terminal 2: connect from 10 clients using Day 17 client.
The server exits after finishing 10 client sessions.

## Behavior and test scope

- Nonblocking `accept4`, `recv` and `send`; `EAGAIN` is not EOF.
- epoll **level triggered** (LT), using `EPOLLIN` and `EPOLLOUT` when required.
- Each connection has an independent pending output buffer (limit 1 MiB); slow/overlimit clients fail closed.
- `EPOLL_CTL_DEL` before RAII descriptor release; close on failures too.
- Listen only on 127.0.0.1, testing with port 0 for safe ephemeral allocation.
- Test 10 simultaneous clients in 5 successive waves (50 connections), including split writes, `EAGAIN`, EOF and FD count returning to baseline between waves.
- The server is intentionally bounded by a number of accepted connections for deterministic CTest completion, not a production daemon.

Day 19 will cover message framing and proper backpressure. Current simple buffer limit and connection close are teaching choices, not a full production backpressure policy.
