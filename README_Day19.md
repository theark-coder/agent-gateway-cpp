# Day 19 — Length-prefixed epoll LT Echo

Independent of the Day 17 blocking echo and the Day 18 raw-stream epoll echo.
This project demonstrates **message framing** and **bounded output backpressure**.

## Wire format

Each request/response consists of:

```
4 bytes: unsigned payload length in network byte order (big endian)
N bytes: payload, where 0 <= N <= 65536
```

Multiple frames can arrive in a single `recv()`, or one frame may span
multiple `recv()` calls. Frames are echoed verbatim (including their 4-byte
length header). The parser supports empty frames and binary payloads.

Oversized frames (> 64 KiB) and incomplete frames at EOF are rejected by
closing that connection. For this standalone experiment, rejections are
reported as `rejected=N` and do not make the whole server process fail.

## Connection/Memory management

- Linux-only, **nonblocking epoll LT** mode.
- Keep one input buffer and one output buffer per connection.
- Partial sends are tracked by `sent`; `EPOLLOUT` is enabled only if needed.
- Max payload per frame: **64 KiB**.
- Max partial input buffer: **64 KiB + 4 bytes + 4096 bytes**.
- Output pause threshold (high watermark): **128 KiB**.
- Output resume threshold (low watermark): **64 KiB**.
- Hard output cap: **256 KiB**; excess data is rejected and the connection closed.
- `EAGAIN` is not a disconnection; `EINTR` retries; EOF is handled separately.
- Uses the existing `agent::UniqueFd` for owning file descriptors.
- `shutdown(SHUT_WR)` from a client signals its request stream is complete;
  the server still flushes all accepted responses.

The buffer limits are per connection. A production service also needs a total
memory budget, connection admission limits, timeouts, and request policies.
This is a learning experiment, not a hardened production HTTP server.

## Install

Unzip the patch into the root of `~/projects/agent-gateway-cpp` and add **once**
near the end of `CMakeLists.txt`:

```cmake
include(cmake/day19_frames.cmake)
```

The Day18 target and tests remain untouched.

## Build and tests

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug
cmake --build build --parallel
ctest --test-dir build -R '^tcp_frames_day19_' --output-on-failure
ctest --test-dir build --output-on-failure
```

CTest adds five named scenarios: `fragmented`, `coalesced`, `oversized`,
`slow`, and `concurrent` (10 clients). Existing 16 tests remain, for a total
of **21** if no other tests were added.

The standalone server accepts an optional port and the number of total
clients before exiting:

```bash
./build/agent_tcp_framed_server 9092 1
```

Use the Python framing client in `scripts/day19/test_framed.py` for a complete
protocol test; the Day17 plain TCP echo client does not send length headers.

## Invariants

1. One frame is decoded only after receiving all four header bytes and `N`
   payload bytes.
2. `N <= kMaxFrame` before reading or allocating based on that length.
3. Pending application output is bounded; slow readers trigger temporary
   suspension of `EPOLLIN` and later resumption.
4. On disconnect or protocol rejection, epoll interest is deleted and the FD
   is released by `UniqueFd`.
