#!/usr/bin/env sh
set -eu

root="$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)"
compiler="${1:-}"
stage_name="${2:-host}"
out_dir="$root/artifacts/v3_tcp_twoproc"
mkdir -p "$out_dir"
cd "$root"

if [ -z "$compiler" ] || [ ! -x "$compiler" ]; then
  echo "v3 libp2p tcp twoproc: missing compiler: $compiler" >&2
  exit 1
fi

compile_one() {
  src="$1"
  bin="$2"
  compile_log="$3"
  rm -f "$bin" "$bin".* "$compile_log"
  if ! DIAG_CONTEXT=1 "$compiler" system-link-exec \
    --root "$root/v3" \
    --in "$src" \
    --emit exe \
    --target arm64-apple-darwin \
    --out "$bin" >"$compile_log" 2>&1; then
    echo "v3 libp2p tcp twoproc: compile failed: $src" >&2
    tail -n 80 "$compile_log" >&2 || true
    exit 1
  fi
}

server_src="$root/v3/src/tests/libp2p_tcp_twoproc_server_smoke.cheng"
client_src="$root/v3/src/tests/libp2p_tcp_twoproc_client_smoke.cheng"
server_bin="$out_dir/libp2p_tcp_twoproc_server.$stage_name"
client_bin="$out_dir/libp2p_tcp_twoproc_client.$stage_name"
server_compile_log="$out_dir/libp2p_tcp_twoproc_server.$stage_name.compile.log"
client_compile_log="$out_dir/libp2p_tcp_twoproc_client.$stage_name.compile.log"
server_run_log="$out_dir/libp2p_tcp_twoproc_server.$stage_name.run.log"
client_run_log="$out_dir/libp2p_tcp_twoproc_client.$stage_name.run.log"
ready_path="$out_dir/libp2p_tcp_twoproc_server.$stage_name.ready"

echo "[v3 libp2p tcp twoproc] compile $stage_name server"
compile_one "$server_src" "$server_bin" "$server_compile_log"
echo "[v3 libp2p tcp twoproc] compile $stage_name client"
compile_one "$client_src" "$client_bin" "$client_compile_log"

rm -f "$server_run_log" "$client_run_log" "$ready_path"
"$server_bin" "--port:0" "--ready-path:$ready_path" >"$server_run_log" 2>&1 &
server_pid=$!

cleanup() {
  if kill -0 "$server_pid" 2>/dev/null; then
    kill "$server_pid" 2>/dev/null || true
    wait "$server_pid" 2>/dev/null || true
  fi
}
trap cleanup EXIT INT TERM

port=""
for _ in 1 2 3 4 5 6 7 8 9 10 \
         11 12 13 14 15 16 17 18 19 20 \
         21 22 23 24 25 26 27 28 29 30 \
         31 32 33 34 35 36 37 38 39 40
do
  if [ -f "$ready_path" ] && [ -s "$ready_path" ]; then
    port="$(tr -d '[:space:]' <"$ready_path")"
    break
  fi
  if ! kill -0 "$server_pid" 2>/dev/null; then
    break
  fi
  sleep 0.05
done

case "$port" in
  ''|*[!0-9]*)
  echo "v3 libp2p tcp twoproc: invalid ready port: $port" >&2
  tail -n 80 "$server_run_log" >&2 || true
  exit 1
  ;;
esac

echo "[v3 libp2p tcp twoproc] run $stage_name client"
if ! "$client_bin" "--port:$port" >"$client_run_log" 2>&1; then
  echo "v3 libp2p tcp twoproc: client failed" >&2
  tail -n 80 "$client_run_log" >&2 || true
  exit 1
fi

if ! wait "$server_pid"; then
  echo "v3 libp2p tcp twoproc: server failed" >&2
  tail -n 80 "$server_run_log" >&2 || true
  exit 1
fi
trap - EXIT INT TERM

cat "$server_run_log"
cat "$client_run_log"
echo "v3 libp2p_tcp_twoproc_smoke ok"
