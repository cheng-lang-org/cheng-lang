#!/usr/bin/env sh
set -eu

root="$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)"
default_compiler="$root/artifacts/v3_backend_driver/cheng"
compiler="${1:-${CHENG_V3_CHAIN_NODE_COMPILER:-$default_compiler}}"
label="${2:-${CHENG_V3_CHAIN_NODE_LABEL:-host}}"
server_src="$root/v3/src/tests/chain_node_process_server_smoke.cheng"
client_src="$root/v3/src/tests/chain_node_process_client_smoke.cheng"
out_dir="$root/artifacts/v3_chain_node_process/$label"
server_bin="$out_dir/chain_node_process_server"
client_bin="$out_dir/chain_node_process_client"
server_compile_log="$out_dir/chain_node_process_server.compile.log"
client_compile_log="$out_dir/chain_node_process_client.compile.log"

mkdir -p "$out_dir"
cd "$root"

if [ "$compiler" = "$default_compiler" ] && [ ! -x "$compiler" ]; then
  sh "$root/v3/tooling/build_backend_driver_v3.sh"
fi

if [ ! -x "$compiler" ]; then
  echo "v3 chain_node process smoke: missing compiler: $compiler" >&2
  exit 1
fi

if [ ! -f "$server_src" ] || [ ! -f "$client_src" ]; then
  echo "v3 chain_node process smoke: missing server/client source" >&2
  exit 1
fi

if ! DIAG_CONTEXT=1 "$compiler" system-link-exec \
  --root "$root/v3" \
  --in "$server_src" \
  --emit exe \
  --target arm64-apple-darwin \
  --out "$server_bin" >"$server_compile_log" 2>&1; then
  echo "v3 chain_node process smoke: server compile failed label=$label" >&2
  tail -n 80 "$server_compile_log" >&2 || true
  exit 1
fi

if ! DIAG_CONTEXT=1 "$compiler" system-link-exec \
  --root "$root/v3" \
  --in "$client_src" \
  --emit exe \
  --target arm64-apple-darwin \
  --out "$client_bin" >"$client_compile_log" 2>&1; then
  echo "v3 chain_node process smoke: client compile failed label=$label" >&2
  tail -n 80 "$client_compile_log" >&2 || true
  exit 1
fi

tmpdir="$(mktemp -d "$out_dir/tmp.XXXXXX")"
server_pid=""
watchdog_pid=""

cleanup() {
  if [ -n "$watchdog_pid" ]; then
    kill -TERM "$watchdog_pid" 2>/dev/null || true
    wait "$watchdog_pid" 2>/dev/null || true
    watchdog_pid=""
  fi
  if [ -n "$server_pid" ]; then
    kill -TERM "$server_pid" 2>/dev/null || true
    wait "$server_pid" 2>/dev/null || true
    server_pid=""
  fi
  rm -rf "$tmpdir"
}

trap cleanup EXIT INT TERM

server_log="$tmpdir/server.log"
client_log="$tmpdir/client.log"
ready_path="$tmpdir/server.ready"
"$server_bin" "--port:0" "--ready-path:$ready_path" >"$server_log" 2>&1 &
server_pid="$!"

(
  sleep 20
  kill -TERM "$server_pid" 2>/dev/null || true
) &
watchdog_pid="$!"

server_port=""
for _ in 1 2 3 4 5 6 7 8 9 10 \
         11 12 13 14 15 16 17 18 19 20 \
         21 22 23 24 25 26 27 28 29 30 \
         31 32 33 34 35 36 37 38 39 40
do
  if [ -f "$ready_path" ] && [ -s "$ready_path" ]; then
    server_port="$(tr -d '[:space:]' <"$ready_path")"
    break
  fi
  if ! kill -0 "$server_pid" 2>/dev/null; then
    break
  fi
  sleep 0.05
done

kill -TERM "$watchdog_pid" 2>/dev/null || true
wait "$watchdog_pid" 2>/dev/null || true
watchdog_pid=""

case "$server_port" in
  ''|*[!0-9]*)
    echo "v3 chain_node process smoke: invalid ready port: $server_port" >&2
    tail -n 80 "$server_log" >&2 || true
    exit 1
    ;;
esac

if ! "$client_bin" "--port:$server_port" >"$client_log" 2>&1; then
  echo "v3 chain_node process smoke: sync failed label=$label port=$server_port" >&2
  tail -n 80 "$server_log" >&2 || true
  tail -n 80 "$client_log" >&2 || true
  exit 1
fi

if ! wait "$server_pid"; then
  echo "v3 chain_node process smoke: server failed label=$label" >&2
  tail -n 80 "$server_log" >&2 || true
  exit 1
fi
server_pid=""
cat "$server_log"
cat "$client_log"
echo "v3 chain_node process smoke ok ($label)"
