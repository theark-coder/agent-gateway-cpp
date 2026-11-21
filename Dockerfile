# Multi-stage production demo image for a local C++17 Agent Gateway.
FROM ubuntu:24.04 AS builder
ENV DEBIAN_FRONTEND=noninteractive
RUN apt-get update && apt-get install -y --no-install-recommends \
    ca-certificates build-essential cmake git pkg-config python3 \
    libmariadb-dev libhiredis-dev \
    && rm -rf /var/lib/apt/lists/*
WORKDIR /src
COPY . .
# Prefer pre-fetched local dependencies in WSL; CI can fall back to FetchContent.
RUN set -eu; \
    if [ -f /src/.docker_deps/httplib-src/CMakeLists.txt ] && \
       [ -f /src/.docker_deps/nlohmann_json-src/CMakeLists.txt ]; then \
      cmake -S . -B /build -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTING=OFF \
        -DFETCHCONTENT_SOURCE_DIR_HTTPLIB=/src/.docker_deps/httplib-src \
        -DFETCHCONTENT_SOURCE_DIR_NLOHMANN_JSON=/src/.docker_deps/nlohmann_json-src; \
    else \
      cmake -S . -B /build -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTING=OFF; \
    fi; \
    cmake --build /build -j4 --target agent_http_server

FROM ubuntu:24.04 AS runtime
ENV DEBIAN_FRONTEND=noninteractive
RUN apt-get update && apt-get install -y --no-install-recommends \
    ca-certificates libmariadb3 libhiredis-dev \
    && rm -rf /var/lib/apt/lists/* \
    && useradd --system --uid 10001 --create-home gateway
COPY --from=builder /build/agent_http_server /usr/local/bin/agent_http_server
USER gateway
EXPOSE 8080
CMD ["/usr/local/bin/agent_http_server", "8080", "0.0.0.0"]
