#!/usr/bin/env sh
set -eu

root="$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)"
default_compiler="$root/artifacts/v3_backend_driver/cheng"
compiler="${1:-${CHENG_V3_CHAIN_NODE_COMPILER:-$default_compiler}}"
label="${2:-${CHENG_V3_CHAIN_NODE_LABEL:-host}}"
server_src="$root/v3/src/tests/chain_node_process_server_smoke.cheng"
relay_src="$root/v3/src/tests/chain_node_process_relay_smoke.cheng"
client_src="$root/v3/src/tests/chain_node_process_client_smoke.cheng"
out_dir="$root/artifacts/v3_chain_node_three_node/$label"
server_bin="$out_dir/chain_node_process_server"
relay_bin="$out_dir/chain_node_process_relay"
client_bin="$out_dir/chain_node_process_client"

mkdir -p "$out_dir"
cd "$root"

if [ "$compiler" = "$default_compiler" ] && [ ! -x "$compiler" ]; then
  sh "$root/v3/tooling/build_backend_driver_v3.sh"
fi

if [ ! -x "$compiler" ]; then
  echo "v3 chain_node three-node smoke: missing compiler: $compiler" >&2
  exit 1
fi

if [ ! -f "$server_src" ] || [ ! -f "$relay_src" ] || [ ! -f "$client_src" ]; then
  echo "v3 chain_node three-node smoke: missing source" >&2
  exit 1
fi

zsh "$root/v3/tooling/run_v3_compile_exe.sh" \
  "v3 chain_node three-node smoke" \
  "$label server" \
  "$compiler" \
  "$root/v3" \
  "$server_src" \
  "$server_bin"

zsh "$root/v3/tooling/run_v3_compile_exe.sh" \
  "v3 chain_node three-node smoke" \
  "$label relay" \
  "$compiler" \
  "$root/v3" \
  "$relay_src" \
  "$relay_bin"

zsh "$root/v3/tooling/run_v3_compile_exe.sh" \
  "v3 chain_node three-node smoke" \
  "$label client" \
  "$compiler" \
  "$root/v3" \
  "$client_src" \
  "$client_bin"

tmpdir="$(mktemp -d "$out_dir/tmp.XXXXXX")"
server_pid=""
relay_pid=""
server_watchdog_pid=""
relay_watchdog_pid=""

cleanup() {
  if [ -n "$server_watchdog_pid" ]; then
    kill -TERM "$server_watchdog_pid" 2>/dev/null || true
    wait "$server_watchdog_pid" 2>/dev/null || true
    server_watchdog_pid=""
  fi
  if [ -n "$relay_watchdog_pid" ]; then
    kill -TERM "$relay_watchdog_pid" 2>/dev/null || true
    wait "$relay_watchdog_pid" 2>/dev/null || true
    relay_watchdog_pid=""
  fi
  if [ -n "$relay_pid" ]; then
    kill -TERM "$relay_pid" 2>/dev/null || true
    wait "$relay_pid" 2>/dev/null || true
    relay_pid=""
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
relay_log="$tmpdir/relay.log"
client_log="$tmpdir/client.log"
server_ready="$tmpdir/server.ready"
relay_ready="$tmpdir/relay.ready"

"$server_bin" "--port:0" "--ready-path:$server_ready" >"$server_log" 2>&1 &
server_pid="$!"

(
  sleep 20
  kill -TERM "$server_pid" 2>/dev/null || true
) &
server_watchdog_pid="$!"

server_port=""
for _ in 1 2 3 4 5 6 7 8 9 10 \
         11 12 13 14 15 16 17 18 19 20 \
         21 22 23 24 25 26 27 28 29 30 \
         31 32 33 34 35 36 37 38 39 40
do
  if [ -f "$server_ready" ] && [ -s "$server_ready" ]; then
    server_port="$(tr -d '[:space:]' <"$server_ready")"
    break
  fi
  if ! kill -0 "$server_pid" 2>/dev/null; then
    break
  fi
  sleep 0.05
done

kill -TERM "$server_watchdog_pid" 2>/dev/null || true
wait "$server_watchdog_pid" 2>/dev/null || true
server_watchdog_pid=""

case "$server_port" in
  ''|*[!0-9]*)
    echo "v3 chain_node three-node smoke: invalid server ready port: $server_port" >&2
    tail -n 80 "$server_log" >&2 || true
    exit 1
    ;;
esac

"$relay_bin" "--upstream-port:$server_port" "--port:0" "--ready-path:$relay_ready" >"$relay_log" 2>&1 &
relay_pid="$!"

(
  sleep 20
  kill -TERM "$relay_pid" 2>/dev/null || true
) &
relay_watchdog_pid="$!"

relay_port=""
for _ in 1 2 3 4 5 6 7 8 9 10 \
         11 12 13 14 15 16 17 18 19 20 \
         21 22 23 24 25 26 27 28 29 30 \
         31 32 33 34 35 36 37 38 39 40
do
  if [ -f "$relay_ready" ] && [ -s "$relay_ready" ]; then
    relay_port="$(tr -d '[:space:]' <"$relay_ready")"
    break
  fi
  if ! kill -0 "$relay_pid" 2>/dev/null; then
    break
  fi
  sleep 0.05
done

kill -TERM "$relay_watchdog_pid" 2>/dev/null || true
wait "$relay_watchdog_pid" 2>/dev/null || true
relay_watchdog_pid=""

case "$relay_port" in
  ''|*[!0-9]*)
    echo "v3 chain_node three-node smoke: invalid relay ready port: $relay_port" >&2
    tail -n 80 "$server_log" >&2 || true
    tail -n 80 "$relay_log" >&2 || true
    exit 1
    ;;
esac

if ! "$client_bin" "--port:$relay_port" >"$client_log" 2>&1; then
  echo "v3 chain_node three-node smoke: client failed label=$label relay_port=$relay_port" >&2
  tail -n 80 "$server_log" >&2 || true
  tail -n 80 "$relay_log" >&2 || true
  tail -n 80 "$client_log" >&2 || true
  exit 1
fi

if ! wait "$relay_pid"; then
  echo "v3 chain_node three-node smoke: relay failed label=$label" >&2
  tail -n 80 "$relay_log" >&2 || true
  exit 1
fi
relay_pid=""

if ! wait "$server_pid"; then
  echo "v3 chain_node three-node smoke: server failed label=$label" >&2
  tail -n 80 "$server_log" >&2 || true
  exit 1
fi
server_pid=""

cat "$server_log"
cat "$relay_log"
cat "$client_log"
echo "v3 chain_node three-node smoke ok ($label)"
