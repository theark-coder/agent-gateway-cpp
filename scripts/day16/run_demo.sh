#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT="$(cd "$SCRIPT_DIR/../.." && pwd)"

MODE="${1:?Usage: run_demo.sh <scenario>}"

cmake --build "$ROOT/build" \
    --target agent_day16_integration_tests \
    --parallel

exec "$ROOT/build/agent_day16_integration_tests" "$MODE"