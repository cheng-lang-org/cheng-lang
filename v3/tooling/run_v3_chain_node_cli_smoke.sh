#!/usr/bin/env sh
set -eu

root="$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)"
default_compiler="$root/artifacts/v3_backend_driver/cheng"
compiler="${1:-${CHENG_V3_CHAIN_NODE_COMPILER:-$default_compiler}}"
label="${2:-${CHENG_V3_CHAIN_NODE_LABEL:-host}}"
out_dir="$root/artifacts/v3_chain_node_cli/$label"
bin="$out_dir/chain_node"

mkdir -p "$out_dir"
cd "$root"

if [ "$compiler" = "$default_compiler" ] && [ ! -x "$compiler" ]; then
  sh "$root/v3/tooling/build_backend_driver_v3.sh"
fi

if [ ! -x "$compiler" ]; then
  echo "v3 chain_node cli smoke: missing compiler: $compiler" >&2
  exit 1
fi

CHAIN_NODE_OUT_DIR="$out_dir" \
CHENG_V3_CHAIN_NODE_COMPILER="$compiler" \
CHAIN_NODE_RUN_SELF_TEST=0 \
sh "$root/v3/tooling/build_chain_node_v3.sh" >/dev/null

if [ ! -x "$bin" ]; then
  echo "v3 chain_node cli smoke: missing chain_node binary: $bin" >&2
  exit 1
fi

tmpdir="$(mktemp -d "$out_dir/tmp.XXXXXX")"
server_state="$tmpdir/server.state"
client_state="$tmpdir/client.state"
ready_path="$tmpdir/server.ready"
server_log="$tmpdir/server.log"
sync_log="$tmpdir/sync.log"

cleanup() {
  if [ -n "${server_pid:-}" ]; then
    kill -TERM "$server_pid" 2>/dev/null || true
    wait "$server_pid" 2>/dev/null || true
    server_pid=""
  fi
  rm -rf "$tmpdir"
}

trap cleanup EXIT INT TERM

"$bin" init --state:"$server_state" --node-id:node-a --address-seed:chain-node-a >"$tmpdir/init.log"
"$bin" mint --state:"$server_state" --asset:7 --account:1001 --amount:100 >"$tmpdir/mint.log"
"$bin" transfer --state:"$server_state" --asset:7 --from:1001 --to:2002 --amount:40 >"$tmpdir/transfer.log"
"$bin" balance --state:"$server_state" --asset:7 --account:1001 >"$tmpdir/balance_a.log"
"$bin" balance --state:"$server_state" --asset:7 --account:2002 >"$tmpdir/balance_b.log"
"$bin" show-state --state:"$server_state" >"$tmpdir/show.log"
"$bin" dump-snapshot --state:"$server_state" >"$tmpdir/snapshot.log"

"$bin" daemon-serve \
  --state:"$server_state" \
  --port:0 \
  --ready-path:"$ready_path" \
  --max-serve-count:1 \
  >"$server_log" 2>&1 &
server_pid="$!"

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

case "$server_port" in
  ''|*[!0-9]*)
    echo "v3 chain_node cli smoke: invalid ready port: $server_port" >&2
    tail -n 80 "$server_log" >&2 || true
    exit 1
    ;;
esac

"$bin" daemon-sync \
  --state:"$client_state" \
  --node-id:node-b \
  --address-seed:chain-node-b \
  --host:127.0.0.1 \
  --port:"$server_port" \
  --max-sync-count:1 \
  --interval-ms:10 \
  >"$sync_log" 2>&1

if ! wait "$server_pid"; then
  echo "v3 chain_node cli smoke: daemon-serve failed" >&2
  tail -n 80 "$server_log" >&2 || true
  exit 1
fi
server_pid=""

"$bin" balance --state:"$client_state" --asset:7 --account:1001 >"$tmpdir/client_balance_a.log"
"$bin" balance --state:"$client_state" --asset:7 --account:2002 >"$tmpdir/client_balance_b.log"
"$bin" show-state --state:"$client_state" >"$tmpdir/client_show.log"

grep -Fq "node_id=node-a" "$tmpdir/init.log"
grep -Fq "balance=60" "$tmpdir/balance_a.log"
grep -Fq "balance=40" "$tmpdir/balance_b.log"
grep -Fq "node_id=node-a" "$tmpdir/show.log"
grep -Fq "payload_hex=" "$tmpdir/snapshot.log"
grep -Fq "changed=1" "$sync_log"
grep -Fq "balance=60" "$tmpdir/client_balance_a.log"
grep -Fq "balance=40" "$tmpdir/client_balance_b.log"
grep -Fq "node_id=node-b" "$tmpdir/client_show.log"

cat "$tmpdir/init.log"
cat "$tmpdir/mint.log"
cat "$tmpdir/transfer.log"
cat "$tmpdir/balance_a.log"
cat "$tmpdir/balance_b.log"
cat "$tmpdir/show.log"
cat "$tmpdir/snapshot.log"
cat "$server_log"
cat "$sync_log"
cat "$tmpdir/client_balance_a.log"
cat "$tmpdir/client_balance_b.log"
cat "$tmpdir/client_show.log"
echo "v3 chain_node cli smoke ok ($label)"
