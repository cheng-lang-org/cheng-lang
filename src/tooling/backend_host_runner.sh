#!/usr/bin/env sh
. "$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)/env_prefix_bridge.sh"
set -eu
(set -o pipefail) 2>/dev/null && set -o pipefail

usage() {
  cat <<'EOF'
Usage:
  src/tooling/backend_host_runner.sh init --state:<dir> --base:<exe>
                                         [--layout-hash:<hash>] [--pool-mb:<N>]
                                         [--mode:<trampoline>] [--page-policy:<rw_rx>]
  src/tooling/backend_host_runner.sh commit --state:<dir> --patch:<exe>
                                           [--layout-hash:<hash>] [--symbol:<name>]
                                           [--thunk-id:<id>] [--target-slot:<offset>]
                                           [--max-growth:<bytes>] [--check-exit:<code>]
  src/tooling/backend_host_runner.sh run --state:<dir> [--check-exit:<code>]
  src/tooling/backend_host_runner.sh status --state:<dir>
  src/tooling/backend_host_runner.sh run-host --exe:<path>
                                             [--state:<dir>] [--layout-hash:<hash>]

Notes:
  - Dev hotpatch transaction model: trampoline route + append-only pool + layout-hash fallback restart.
  - Default policy is BACKEND_HOTPATCH_MODE=trampoline and BACKEND_HOSTRUNNER_PAGE_POLICY=rw_rx.
EOF
}

now_ms() {
  if command -v perl >/dev/null 2>&1; then
    perl -MTime::HiRes=time -e 'printf "%.0f\n", time() * 1000'
    return 0
  fi
  echo "$(( $(date +%s) * 1000 ))"
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

is_uint() {
  case "${1:-}" in
    ''|*[!0-9]*)
      return 1
      ;;
  esac
  return 0
}

file_size() {
  if [ ! -f "$1" ]; then
    echo "0"
    return 0
  fi
  wc -c < "$1" | tr -d ' '
}

read_meta() {
  state_dir="$1"
  meta="$state_dir/meta.env"
  if [ ! -f "$meta" ]; then
    echo "[backend_host_runner] missing state metadata: $meta" >&2
    return 1
  fi
  # shellcheck disable=SC1090
  . "$meta"
}

write_meta() {
  state_dir="$1"
  meta="$state_dir/meta.env"
  cat >"$meta" <<EOF
schema_version=1
mode=$mode
page_policy=$page_policy
layout_hash_mode=$layout_hash_mode
on_layout_change=$on_layout_change
target_platforms=$target_platforms
pool_limit_bytes=$pool_limit_bytes
pool_used_bytes=$pool_used_bytes
commit_epoch=$commit_epoch
restart_count=$restart_count
layout_hash=$layout_hash
host_pid=$host_pid
base_exe=$base_exe
current_exe=$current_exe
last_commit_kind=$last_commit_kind
last_symbol=$last_symbol
last_thunk_id=$last_thunk_id
last_target_slot=$last_target_slot
last_code_pool_offset=$last_code_pool_offset
last_patch_size=$last_patch_size
last_apply_ms=$last_apply_ms
last_error=$last_error
created_ms=$created_ms
updated_ms=$updated_ms
EOF
}

print_status() {
  state_dir="$1"
  read_meta "$state_dir"
  cat <<EOF
status=ok
state=$state_dir
schema_version=$schema_version
mode=$mode
page_policy=$page_policy
layout_hash_mode=$layout_hash_mode
on_layout_change=$on_layout_change
target_platforms=$target_platforms
pool_limit_bytes=$pool_limit_bytes
pool_used_bytes=$pool_used_bytes
commit_epoch=$commit_epoch
restart_count=$restart_count
layout_hash=$layout_hash
host_pid=$host_pid
base_exe=$base_exe
current_exe=$current_exe
last_commit_kind=$last_commit_kind
last_symbol=$last_symbol
last_thunk_id=$last_thunk_id
last_target_slot=$last_target_slot
last_code_pool_offset=$last_code_pool_offset
last_patch_size=$last_patch_size
last_apply_ms=$last_apply_ms
last_error=$last_error
created_ms=$created_ms
updated_ms=$updated_ms
EOF
}

copy_exec() {
  src="$1"
  dst="$2"
  cp "$src" "$dst"
  chmod 755 "$dst" 2>/dev/null || true
}

init_cmd() {
  state=""
  base=""
  layout_arg=""
  pool_mb="${BACKEND_HOSTRUNNER_POOL_MB:-512}"
  mode_arg="${BACKEND_HOTPATCH_MODE:-trampoline}"
  page_policy_arg="${BACKEND_HOSTRUNNER_PAGE_POLICY:-rw_rx}"
  layout_hash_mode_arg="${BACKEND_HOTPATCH_LAYOUT_HASH_MODE:-full_program}"
  on_layout_change_arg="${BACKEND_HOTPATCH_ON_LAYOUT_CHANGE:-restart}"
  target_platforms_arg="${BACKEND_HOTPATCH_TARGET_PLATFORMS:-darwin,linux}"

  while [ "${1:-}" != "" ]; do
    case "$1" in
      --state:*) state="${1#--state:}" ;;
      --base:*) base="${1#--base:}" ;;
      --layout-hash:*) layout_arg="${1#--layout-hash:}" ;;
      --pool-mb:*) pool_mb="${1#--pool-mb:}" ;;
      --mode:*) mode_arg="${1#--mode:}" ;;
      --page-policy:*) page_policy_arg="${1#--page-policy:}" ;;
      --help|-h) usage; exit 0 ;;
      *)
        echo "[backend_host_runner] init unknown arg: $1" >&2
        usage
        exit 1
        ;;
    esac
    shift || true
  done

  if [ "$state" = "" ] || [ "$base" = "" ]; then
    echo "[backend_host_runner] init requires --state and --base" >&2
    usage
    exit 1
  fi
  if [ ! -f "$base" ]; then
    echo "[backend_host_runner] init base not found: $base" >&2
    exit 1
  fi
  if ! is_uint "$pool_mb"; then
    echo "[backend_host_runner] invalid --pool-mb: $pool_mb" >&2
    exit 1
  fi
  if [ "$pool_mb" -lt 1 ]; then
    pool_mb=1
  fi

  mkdir -p "$state"
  pool_file="$state/code_pool.bin"
  current_file="$state/current.exe"
  : >"$pool_file"
  copy_exec "$base" "$current_file"

  mode="$mode_arg"
  page_policy="$page_policy_arg"
  layout_hash_mode="$layout_hash_mode_arg"
  on_layout_change="$on_layout_change_arg"
  target_platforms="$target_platforms_arg"
  pool_limit_bytes=$((pool_mb * 1024 * 1024))
  pool_used_bytes=0
  commit_epoch=0
  restart_count=0
  if [ "$layout_arg" = "" ]; then
    layout_hash="$(hash_file "$base")"
  else
    layout_hash="$layout_arg"
  fi
  host_pid="$$"
  base_exe="$base"
  current_exe="$current_file"
  last_commit_kind="init"
  last_symbol="-"
  last_thunk_id="0"
  last_target_slot="0"
  last_code_pool_offset="0"
  last_patch_size="0"
  last_apply_ms="0"
  last_error="-"
  created_ms="$(now_ms)"
  updated_ms="$created_ms"

  write_meta "$state"
  print_status "$state"
}

commit_cmd() {
  state=""
  patch=""
  layout_arg=""
  symbol_arg="-"
  thunk_id_arg="0"
  target_slot_arg="0"
  max_growth_arg="${BACKEND_HOTPATCH_MAX_GROWTH:-}"
  check_exit=""

  while [ "${1:-}" != "" ]; do
    case "$1" in
      --state:*) state="${1#--state:}" ;;
      --patch:*) patch="${1#--patch:}" ;;
      --layout-hash:*) layout_arg="${1#--layout-hash:}" ;;
      --symbol:*) symbol_arg="${1#--symbol:}" ;;
      --thunk-id:*) thunk_id_arg="${1#--thunk-id:}" ;;
      --target-slot:*) target_slot_arg="${1#--target-slot:}" ;;
      --max-growth:*) max_growth_arg="${1#--max-growth:}" ;;
      --check-exit:*) check_exit="${1#--check-exit:}" ;;
      --help|-h) usage; exit 0 ;;
      *)
        echo "[backend_host_runner] commit unknown arg: $1" >&2
        usage
        exit 1
        ;;
    esac
    shift || true
  done

  if [ "$state" = "" ] || [ "$patch" = "" ]; then
    echo "[backend_host_runner] commit requires --state and --patch" >&2
    usage
    exit 1
  fi
  if [ ! -f "$patch" ]; then
    echo "[backend_host_runner] commit patch not found: $patch" >&2
    exit 1
  fi
  if ! is_uint "$thunk_id_arg"; then
    echo "[backend_host_runner] invalid --thunk-id: $thunk_id_arg" >&2
    exit 1
  fi
  if ! is_uint "$target_slot_arg"; then
    echo "[backend_host_runner] invalid --target-slot: $target_slot_arg" >&2
    exit 1
  fi
  if [ "$check_exit" != "" ] && ! is_uint "$check_exit"; then
    echo "[backend_host_runner] invalid --check-exit: $check_exit" >&2
    exit 1
  fi
  if [ "$max_growth_arg" != "" ] && ! is_uint "$max_growth_arg"; then
    echo "[backend_host_runner] invalid --max-growth: $max_growth_arg" >&2
    exit 1
  fi

  read_meta "$state"
  pool_file="$state/code_pool.bin"
  current_file="$state/current.exe"
  meta_file="$state/meta.env"
  meta_backup="$state/meta.env.bak"
  pool_backup="$state/code_pool.bin.bak"
  current_backup="$state/current.exe.bak"
  cp "$meta_file" "$meta_backup"
  cp "$pool_file" "$pool_backup"
  cp "$current_file" "$current_backup"

  start_ms="$(now_ms)"
  patch_size="$(file_size "$patch")"
  current_size="$(file_size "$current_file")"
  input_layout="$layout_arg"
  if [ "$input_layout" = "" ]; then
    input_layout="$layout_hash"
  fi

  commit_kind="append"
  restart_reason=""
  code_pool_offset="$pool_used_bytes"
  growth=$((patch_size - current_size))
  if [ "$growth" -lt 0 ]; then
    growth=0
  fi

  if [ "$on_layout_change" = "restart" ] && [ "$input_layout" != "$layout_hash" ]; then
    restart_reason="layout_change"
  fi
  if [ "$restart_reason" = "" ] && [ "$max_growth_arg" != "" ] && [ "$growth" -gt "$max_growth_arg" ]; then
    restart_reason="max_growth"
  fi
  next_used=$((pool_used_bytes + patch_size))
  if [ "$restart_reason" = "" ] && [ "$next_used" -gt "$pool_limit_bytes" ]; then
    restart_reason="pool_exhausted"
  fi

  if [ "$restart_reason" = "" ]; then
    cat "$patch" >>"$pool_file"
    pool_used_bytes="$next_used"
    commit_kind="append"
  else
    : >"$pool_file"
    pool_used_bytes=0
    restart_count=$((restart_count + 1))
    commit_kind="restart_$restart_reason"
    code_pool_offset=0
  fi

  copy_exec "$patch" "$current_file"
  layout_hash="$input_layout"
  commit_epoch=$((commit_epoch + 1))
  last_symbol="$symbol_arg"
  last_thunk_id="$thunk_id_arg"
  last_target_slot="$target_slot_arg"
  last_code_pool_offset="$code_pool_offset"
  last_patch_size="$patch_size"
  last_commit_kind="$commit_kind"
  last_error="-"
  end_ms="$(now_ms)"
  last_apply_ms=$((end_ms - start_ms))
  updated_ms="$end_ms"

  write_meta "$state"

  run_exit=""
  if [ "$check_exit" != "" ]; then
    set +e
    "$current_file" >/dev/null 2>&1
    run_exit="$?"
    set -e
    if [ "$run_exit" -ne "$check_exit" ]; then
      cp "$meta_backup" "$meta_file"
      cp "$pool_backup" "$pool_file"
      copy_exec "$current_backup" "$current_file"
      rm -f "$meta_backup" "$pool_backup" "$current_backup"
      echo "[backend_host_runner] post-commit check failed: expected=$check_exit actual=$run_exit" >&2
      exit 1
    fi
  fi

  rm -f "$meta_backup" "$pool_backup" "$current_backup"
  print_status "$state"
  if [ "$run_exit" != "" ]; then
    echo "postcheck_exit=$run_exit"
  fi
}

run_cmd() {
  state=""
  check_exit=""
  while [ "${1:-}" != "" ]; do
    case "$1" in
      --state:*) state="${1#--state:}" ;;
      --check-exit:*) check_exit="${1#--check-exit:}" ;;
      --help|-h) usage; exit 0 ;;
      *)
        echo "[backend_host_runner] run unknown arg: $1" >&2
        usage
        exit 1
        ;;
    esac
    shift || true
  done
  if [ "$state" = "" ]; then
    echo "[backend_host_runner] run requires --state" >&2
    usage
    exit 1
  fi
  if [ "$check_exit" != "" ] && ! is_uint "$check_exit"; then
    echo "[backend_host_runner] invalid --check-exit: $check_exit" >&2
    exit 1
  fi
  read_meta "$state"
  set +e
  "$current_exe"
  run_status="$?"
  set -e
  echo "status=ok"
  echo "state=$state"
  echo "run_exit=$run_status"
  if [ "$check_exit" != "" ] && [ "$run_status" -ne "$check_exit" ]; then
    echo "[backend_host_runner] run exit mismatch: expected=$check_exit actual=$run_status" >&2
    exit 1
  fi
  exit "$run_status"
}

status_cmd() {
  state=""
  while [ "${1:-}" != "" ]; do
    case "$1" in
      --state:*) state="${1#--state:}" ;;
      --help|-h) usage; exit 0 ;;
      *)
        echo "[backend_host_runner] status unknown arg: $1" >&2
        usage
        exit 1
        ;;
    esac
    shift || true
  done
  if [ "$state" = "" ]; then
    echo "[backend_host_runner] status requires --state" >&2
    usage
    exit 1
  fi
  print_status "$state"
}

run_host_cmd() {
  exe=""
  state=""
  layout_arg=""
  while [ "${1:-}" != "" ]; do
    case "$1" in
      --exe:*) exe="${1#--exe:}" ;;
      --state:*) state="${1#--state:}" ;;
      --layout-hash:*) layout_arg="${1#--layout-hash:}" ;;
      --help|-h) usage; exit 0 ;;
      *)
        echo "[backend_host_runner] run-host unknown arg: $1" >&2
        usage
        exit 1
        ;;
    esac
    shift || true
  done
  if [ "$exe" = "" ]; then
    echo "[backend_host_runner] run-host requires --exe" >&2
    usage
    exit 1
  fi
  if [ ! -f "$exe" ]; then
    echo "[backend_host_runner] run-host executable not found: $exe" >&2
    exit 1
  fi
  temp_state="0"
  if [ "$state" = "" ]; then
    base_name="$(basename "$exe")"
    state="artifacts/backend_host_runner/run_host.${base_name}.$$"
    temp_state="1"
  fi
  init_args="--state:$state --base:$exe"
  if [ "$layout_arg" != "" ]; then
    init_args="$init_args --layout-hash:$layout_arg"
  fi
  # shellcheck disable=SC2086
  sh "$0" init $init_args >/dev/null
  set +e
  sh "$0" run --state:"$state"
  run_status="$?"
  set -e
  if [ "$temp_state" = "1" ] && [ "${BACKEND_HOSTRUNNER_KEEP_STATE:-0}" != "1" ]; then
    rm -rf "$state"
  fi
  exit "$run_status"
}

cmd="${1:-}"
if [ "$cmd" = "" ] || [ "$cmd" = "--help" ] || [ "$cmd" = "-h" ]; then
  usage
  exit 0
fi
shift || true

case "$cmd" in
  init)
    init_cmd "$@"
    ;;
  commit)
    commit_cmd "$@"
    ;;
  run)
    run_cmd "$@"
    ;;
  status)
    status_cmd "$@"
    ;;
  run-host)
    run_host_cmd "$@"
    ;;
  *)
    echo "[backend_host_runner] unknown command: $cmd" >&2
    usage
    exit 1
    ;;
esac
