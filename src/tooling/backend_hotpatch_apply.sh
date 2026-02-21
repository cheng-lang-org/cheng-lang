#!/usr/bin/env sh
. "$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)/env_prefix_bridge.sh"
set -eu
(set -o pipefail) 2>/dev/null && set -o pipefail

usage() {
  cat <<'EOF'
Usage:
  src/tooling/backend_hotpatch_apply.sh --base:<exe> --patch:<exe> --symbol:<name>
                                       [--check-exit:<code>] [--backup:<path>]
                                       [--max-growth:<bytes>]
                                       [--layout-hash:<hash>] [--state:<dir>]
                                       [--thunk-id:<id>] [--target-slot:<offset>]

Notes:
  - Dev hotpatch terminal path uses trampoline route + append-only code pool.
  - Layout hash drift or pool/max-growth pressure triggers controlled restart, not hard patch failure.
EOF
}

hash_file() {
  file="$1"
  if command -v shasum >/dev/null 2>&1; then
    shasum -a 256 "$file" | awk '{print $1}'
    return 0
  fi
  if command -v sha256sum >/dev/null 2>&1; then
    sha256sum "$file" | awk '{print $1}'
    return 0
  fi
  cksum "$file" | awk '{print $1 ":" $2}'
}

read_status_value() {
  key="$1"
  file="$2"
  awk -F= -v k="$key" '
    $1 == k {
      sub(/^[^=]*=/, "", $0)
      print $0
      found=1
      exit
    }
    END {
      if (!found) print ""
    }
  ' "$file"
}

is_uint() {
  case "${1:-}" in
    ''|*[!0-9]*)
      return 1
      ;;
  esac
  return 0
}

base=""
patch=""
symbol=""
check_exit=""
backup=""
max_growth="${BACKEND_HOTPATCH_MAX_GROWTH:-}"
layout_hash="${BACKEND_HOTPATCH_LAYOUT_HASH:-}"
state_dir="${BACKEND_HOTPATCH_STATE_DIR:-}"
thunk_id="${BACKEND_HOTPATCH_THUNK_ID:-0}"
target_slot="${BACKEND_HOTPATCH_TARGET_SLOT:-0}"

while [ "${1:-}" != "" ]; do
  case "$1" in
    --base:*) base="${1#--base:}" ;;
    --patch:*) patch="${1#--patch:}" ;;
    --symbol:*) symbol="${1#--symbol:}" ;;
    --check-exit:*) check_exit="${1#--check-exit:}" ;;
    --backup:*) backup="${1#--backup:}" ;;
    --max-growth:*) max_growth="${1#--max-growth:}" ;;
    --layout-hash:*) layout_hash="${1#--layout-hash:}" ;;
    --state:*) state_dir="${1#--state:}" ;;
    --thunk-id:*) thunk_id="${1#--thunk-id:}" ;;
    --target-slot:*) target_slot="${1#--target-slot:}" ;;
    --help|-h)
      usage
      exit 0
      ;;
    *)
      echo "[backend_hotpatch_apply] unknown arg: $1" >&2
      usage
      exit 1
      ;;
  esac
  shift
done

if [ "$base" = "" ] || [ "$patch" = "" ] || [ "$symbol" = "" ]; then
  echo "[backend_hotpatch_apply] missing required args (--base --patch --symbol)" >&2
  usage
  exit 1
fi
if [ ! -f "$base" ]; then
  echo "[backend_hotpatch_apply] base executable not found: $base" >&2
  exit 1
fi
if [ ! -f "$patch" ]; then
  echo "[backend_hotpatch_apply] patch executable not found: $patch" >&2
  exit 1
fi
if [ "$check_exit" != "" ] && ! is_uint "$check_exit"; then
  echo "[backend_hotpatch_apply] invalid --check-exit: $check_exit" >&2
  exit 1
fi
if [ "$max_growth" != "" ] && ! is_uint "$max_growth"; then
  echo "[backend_hotpatch_apply] invalid --max-growth: $max_growth" >&2
  exit 1
fi
if ! is_uint "$thunk_id"; then
  echo "[backend_hotpatch_apply] invalid --thunk-id: $thunk_id" >&2
  exit 1
fi
if ! is_uint "$target_slot"; then
  echo "[backend_hotpatch_apply] invalid --target-slot: $target_slot" >&2
  exit 1
fi

if [ "$backup" = "" ]; then
  backup="$base.hotpatch.bak"
fi
if [ "$state_dir" = "" ]; then
  state_dir="$base.hotpatch.state"
fi
if [ "$layout_hash" = "" ]; then
  layout_hash="$(hash_file "$base")"
fi

runner="src/tooling/backend_host_runner.sh"
if [ ! -x "$runner" ]; then
  echo "[backend_hotpatch_apply] missing host runner: $runner" >&2
  exit 1
fi

mkdir -p "$state_dir"
cp "$base" "$backup"

status_tmp="$(mktemp "${TMPDIR:-/tmp}/backend_hotpatch_apply_status.XXXXXX")"
cleanup() {
  rm -f "$status_tmp"
}
trap cleanup EXIT INT TERM HUP

if [ ! -f "$state_dir/meta.env" ]; then
  sh "$runner" init \
    --state:"$state_dir" \
    --base:"$base" \
    --layout-hash:"$layout_hash" >/dev/null
fi

set +e
commit_args="--state:$state_dir --patch:$patch --layout-hash:$layout_hash --symbol:$symbol --thunk-id:$thunk_id --target-slot:$target_slot"
if [ "$max_growth" != "" ]; then
  commit_args="$commit_args --max-growth:$max_growth"
fi
if [ "$check_exit" != "" ]; then
  commit_args="$commit_args --check-exit:$check_exit"
fi
# shellcheck disable=SC2086
sh "$runner" commit $commit_args >"$status_tmp"
status="$?"
set -e

if [ "$status" -ne 0 ]; then
  cp "$backup" "$base"
  echo "[backend_hotpatch_apply] commit failed; restored base from backup: $backup" >&2
  exit "$status"
fi

current_exe="$(read_status_value current_exe "$status_tmp")"
if [ "$current_exe" = "" ] || [ ! -f "$current_exe" ]; then
  cp "$backup" "$base"
  echo "[backend_hotpatch_apply] missing committed executable in state: $state_dir" >&2
  exit 1
fi
cp "$current_exe" "$base"
chmod 755 "$base" 2>/dev/null || true

commit_kind="$(read_status_value last_commit_kind "$status_tmp")"
commit_epoch="$(read_status_value commit_epoch "$status_tmp")"
restart_count="$(read_status_value restart_count "$status_tmp")"
pool_used_bytes="$(read_status_value pool_used_bytes "$status_tmp")"
pool_limit_bytes="$(read_status_value pool_limit_bytes "$status_tmp")"
host_pid="$(read_status_value host_pid "$status_tmp")"
last_apply_ms="$(read_status_value last_apply_ms "$status_tmp")"
resolved_layout_hash="$(read_status_value layout_hash "$status_tmp")"

echo "status=ok"
echo "base=$base"
echo "patch=$patch"
echo "symbol=$symbol"
echo "state=$state_dir"
echo "backup=$backup"
echo "thunk_id=$thunk_id"
echo "target_slot=$target_slot"
echo "commit_kind=$commit_kind"
echo "commit_epoch=$commit_epoch"
echo "restart_count=$restart_count"
echo "pool_used_bytes=$pool_used_bytes"
echo "pool_limit_bytes=$pool_limit_bytes"
echo "host_pid=$host_pid"
echo "apply_ms=$last_apply_ms"
echo "layout_hash=$resolved_layout_hash"
