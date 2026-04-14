#!/usr/bin/env sh
set -eu

root="$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)"
out_dir="$root/artifacts/v3_bootstrap"
bridge_env="$out_dir/bootstrap.env"
seed_source="$root/v3/bootstrap/cheng_v3_seed.c"
stage1_source="$root/v3/bootstrap/stage1_bootstrap.cheng"
seed_runner="$out_dir/cheng.bootstrap_bridge_runner"
seed_runner_log="$out_dir/cheng.bootstrap_bridge_runner.build.log"
cc_bin="${CC:-cc}"
lock_dir="$out_dir/bootstrap.lock"
lock_pid="$lock_dir/pid"

mkdir -p "$out_dir"

while ! mkdir "$lock_dir" 2>/dev/null
do
  holder_pid=""
  if [ -f "$lock_pid" ]; then
    holder_pid="$(cat "$lock_pid" 2>/dev/null || true)"
  fi
  if [ -z "$holder_pid" ] || ! kill -0 "$holder_pid" 2>/dev/null; then
    rm -f "$lock_pid" 2>/dev/null || true
    rmdir "$lock_dir" 2>/dev/null || true
    continue
  fi
  sleep 0.1
done

printf '%s\n' "$$" >"$lock_pid"

cleanup() {
  rm -f "$lock_pid" 2>/dev/null || true
  rmdir "$lock_dir" 2>/dev/null || true
}

trap cleanup EXIT INT TERM

if [ -f "$bridge_env" ]; then
  # shellcheck disable=SC1090
  . "$bridge_env"
  if [ -x "${V3_BOOTSTRAP_STAGE3:-}" ] && [ ! "$seed_source" -nt "$V3_BOOTSTRAP_STAGE3" ] && [ ! "$stage1_source" -nt "$V3_BOOTSTRAP_STAGE3" ]; then
    exit 0
  fi
fi

if [ ! -f "$seed_source" ]; then
  echo "v3 bootstrap bridge: missing seed source: $seed_source" >&2
  exit 1
fi

if ! "$cc_bin" -std=c11 -O2 -Wall -Wextra -pedantic \
  "$seed_source" -o "$seed_runner" >"$seed_runner_log" 2>&1; then
  echo "v3 bootstrap bridge: temporary seed runner build failed log=$seed_runner_log" >&2
  tail -n 80 "$seed_runner_log" >&2 || true
  exit 1
fi

if [ ! -x "$seed_runner" ]; then
  echo "v3 bootstrap bridge: missing temporary seed runner: $seed_runner" >&2
  exit 1
fi

"$seed_runner" bootstrap-bridge "$@"
