
#!/usr/bin/env bash
set -euo pipefail

SERVER="${1:?need server path}"
CLIENT="${2:?need client path}"
SCENARIO="${3:-normal}"

case "$SCENARIO" in
    normal)
        payload='Hello Agent Gateway'
        ;;
    empty)
        payload=''
        ;;
    large)
        printf -v payload '%*s' 32768 ''
        payload="${payload// /x}"
        ;;
    *)
        echo "Unknown scenario: $SCENARIO" >&2
        exit 2
        ;;
esac

log="$(mktemp)"
pid=''

cleanup() {
    if [[ -n "$pid" ]]; then
        kill "$pid" 2>/dev/null || true
        wait "$pid" 2>/dev/null || true
    fi
    rm -f "$log"
}

trap cleanup EXIT

# 使用端口0，让系统分配可用的本地端口
"$SERVER" 0 >"$log" 2>&1 &
pid=$!

port=''

for ((i=0; i<100; ++i)); do
    port="$(
        sed -n 's/^READY 127\.0\.0\.1://p' "$log" |
        head -n 1
    )"

    [[ -n "$port" ]] && break

    if ! kill -0 "$pid" 2>/dev/null; then
        cat "$log"
        exit 1
    fi

    sleep 0.03
done

if [[ -z "$port" ]]; then
    echo "FAIL: server did not report ready port" >&2
    cat "$log"
    exit 1
fi

"$CLIENT" "$port" "$payload"

wait "$pid"
pid=''

cat "$log"
