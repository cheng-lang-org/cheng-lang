#!/usr/bin/env sh
. "$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)/env_prefix_bridge.sh"
set -eu
(set -o pipefail) 2>/dev/null && set -o pipefail

root="$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)"
cd "$root"

if [ "${CLEAN_CHENG_LOCAL:-1}" = "1" ] && [ "${TOOLING_CLEANUP_DEPTH:-0}" = "0" ]; then
  export TOOLING_CLEANUP_DEPTH=1
  cleanup_backend_driver_on_exit() {
    status=$?
    set +e
    sh src/tooling/cleanup_cheng_local.sh
    exit "$status"
  }
  trap cleanup_backend_driver_on_exit EXIT
fi

now_ms() {
  if command -v perl >/dev/null 2>&1; then
    perl -MTime::HiRes=time -e 'printf "%.0f\n", time() * 1000'
    return 0
  fi
  echo "$(( $(date +%s) * 1000 ))"
}

read_kv() {
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

hash_file() {
  file="$1"
  if command -v shasum >/dev/null 2>&1; then
    shasum -a 256 "$file" | awk '{print $1}'
    return
  fi
  if command -v sha256sum >/dev/null 2>&1; then
    sha256sum "$file" | awk '{print $1}'
    return
  fi
  cksum "$file" | awk '{print $1 ":" $2}'
}

host_target="$(sh src/tooling/detect_host_target.sh 2>/dev/null || true)"
if [ "$host_target" = "" ]; then
  host_target="unknown"
fi

is_supported_target() {
  target="$1"
  case "$target" in
    *apple*darwin*|*darwin*)
      case "$target" in
        *arm64*|*aarch64*|*x86_64*|*amd64*) return 0 ;;
      esac
      return 1
      ;;
    *linux*|*android*)
      case "$target" in
        *arm64*|*aarch64*|*riscv64*) return 0 ;;
      esac
      return 1
      ;;
  esac
  return 1
}

select_default_target() {
  if [ "${BACKEND_HOTPATCH_TARGET:-}" != "" ]; then
    echo "${BACKEND_HOTPATCH_TARGET}"
    return
  fi
  if [ "$host_target" != "unknown" ]; then
    echo "$host_target"
    return
  fi
  echo "arm64-apple-darwin"
}

target="${BACKEND_TARGET:-}"
target_explicit="0"
if [ "$target" = "" ]; then
  target="$(select_default_target)"
else
  target_explicit="1"
fi

out_dir="artifacts/backend_hotpatch"
rm -rf "$out_dir"
mkdir -p "$out_dir"
safe_target="$(printf '%s' "$target" | tr -c 'A-Za-z0-9._-' '_' | tr -s '_')"
report="$out_dir/hotpatch.$safe_target.report.txt"

if ! is_supported_target "$target"; then
  if [ "$target_explicit" = "1" ]; then
    echo "[verify_backend_hotpatch] unsupported BACKEND_TARGET: $target" >&2
    exit 1
  fi
  {
    echo "verify_backend_hotpatch report"
    echo "status=skip"
    echo "reason=unsupported_target"
    echo "target=$target"
    echo "host_target=$host_target"
  } >"$report"
  echo "verify_backend_hotpatch skip: unsupported target ($target)"
  exit 0
fi

fixture_v1="tests/cheng/backend/fixtures/hotpatch_slot_v1.cheng"
fixture_v2="tests/cheng/backend/fixtures/hotpatch_slot_v2.cheng"
fixture_v3="tests/cheng/backend/fixtures/hotpatch_slot_v3_overflow.cheng"
for f in "$fixture_v1" "$fixture_v2" "$fixture_v3"; do
  if [ ! -f "$f" ]; then
    echo "[verify_backend_hotpatch] missing fixture: $f" >&2
    exit 1
  fi
done

compile_fixture() {
  fixture="$1"
  out_exe="$2"
  out_log="$3"
  preferred_linker="${BACKEND_HOTPATCH_GATE_LINKER:-self}"
  no_runtime_c="1"
  if [ "$preferred_linker" = "system" ]; then
    no_runtime_c="0"
  fi
  set +e
  env \
    MM="${MM:-orc}" \
    ABI=v2_noptr \
    BACKEND_TARGET="$target" \
    BACKEND_LINKER="$preferred_linker" \
    BACKEND_LINKER_SYMTAB=all \
    BACKEND_RUNTIME=off \
    BACKEND_NO_RUNTIME_C="$no_runtime_c" \
    BACKEND_CODESIGN=0 \
    BACKEND_MULTI=0 \
    BACKEND_MULTI_FORCE=0 \
    BACKEND_INCREMENTAL=0 \
    BACKEND_KEEP_EXE_OBJ=0 \
    BACKEND_OPT=0 \
    BACKEND_OPT2=0 \
    BACKEND_OPT_LEVEL=0 \
    STAGE1_NO_POINTERS_NON_C_ABI=0 \
    STAGE1_NO_POINTERS_NON_C_ABI_INTERNAL=0 \
    STAGE1_SKIP_SEM="${STAGE1_SKIP_SEM:-0}" \
    STAGE1_SKIP_OWNERSHIP="${STAGE1_SKIP_OWNERSHIP:-1}" \
    sh src/tooling/chengc.sh "$fixture" --frontend:stage1 --emit:exe --out:"$out_exe" >"$out_log" 2>&1
  status="$?"
  if [ "$status" -ne 0 ] && [ "$preferred_linker" = "self" ]; then
    env \
      MM="${MM:-orc}" \
      ABI=v2_noptr \
      BACKEND_TARGET="$target" \
      BACKEND_LINKER=system \
      BACKEND_LINKER_SYMTAB=all \
      BACKEND_RUNTIME=off \
      BACKEND_NO_RUNTIME_C=0 \
      BACKEND_CODESIGN=0 \
      BACKEND_MULTI=0 \
      BACKEND_MULTI_FORCE=0 \
      BACKEND_INCREMENTAL=0 \
      BACKEND_KEEP_EXE_OBJ=0 \
      BACKEND_OPT=0 \
      BACKEND_OPT2=0 \
      BACKEND_OPT_LEVEL=0 \
      STAGE1_NO_POINTERS_NON_C_ABI=0 \
      STAGE1_NO_POINTERS_NON_C_ABI_INTERNAL=0 \
      STAGE1_SKIP_SEM="${STAGE1_SKIP_SEM:-0}" \
      STAGE1_SKIP_OWNERSHIP="${STAGE1_SKIP_OWNERSHIP:-1}" \
      sh src/tooling/chengc.sh "$fixture" --frontend:stage1 --emit:exe --out:"$out_exe" >>"$out_log" 2>&1
    status="$?"
  fi
  set -e
  if [ "$status" -ne 0 ]; then
    echo "[verify_backend_hotpatch] build failed: fixture=$fixture status=$status log=$out_log" >&2
    sed -n '1,200p' "$out_log" >&2 || true
    exit 1
  fi
  if [ ! -s "$out_exe" ]; then
    echo "[verify_backend_hotpatch] missing output executable: $out_exe" >&2
    exit 1
  fi
}

base_exe="$out_dir/hotpatch_slot.base.$safe_target"
patch_v2="$out_dir/hotpatch_slot.v2.$safe_target"
patch_v3="$out_dir/hotpatch_slot.v3.$safe_target"
patch_v3_growth="$out_dir/hotpatch_slot.v3_growth.$safe_target"
state_dir="$out_dir/runner_state.$safe_target"
build_log_v1="$out_dir/hotpatch_slot.v1.build.log"
build_log_v2="$out_dir/hotpatch_slot.v2.build.log"
build_log_v3="$out_dir/hotpatch_slot.v3.build.log"
init_log="$out_dir/hotpatch_slot.init.log"
apply_log="$out_dir/hotpatch_slot.apply.log"
compile_fail_log="$out_dir/hotpatch_slot.compile_fail.log"
growth_restart_log="$out_dir/hotpatch_slot.max_growth_restart.log"
layout_restart_log="$out_dir/hotpatch_slot.layout_restart.log"
run_v1_log="$out_dir/hotpatch_slot.run.v1.log"
run_v2_log="$out_dir/hotpatch_slot.run.v2.log"
run_v3_log="$out_dir/hotpatch_slot.run.v3.log"
run_after_fail_log="$out_dir/hotpatch_slot.run.after_fail.log"
run_after_layout_restart_log="$out_dir/hotpatch_slot.run.after_layout_restart.log"

rm -f \
  "$base_exe" "$patch_v2" "$patch_v3" "$patch_v3_growth" \
  "$base_exe.o" "$patch_v2.o" "$patch_v3.o" "$patch_v3_growth.o" \
  "$build_log_v1" "$build_log_v2" "$build_log_v3" \
  "$init_log" "$apply_log" "$compile_fail_log" "$growth_restart_log" "$layout_restart_log" \
  "$run_v1_log" "$run_v2_log" "$run_v3_log" "$run_after_fail_log" "$run_after_layout_restart_log" \
  "$report"
rm -rf "$state_dir"

compile_fixture "$fixture_v1" "$base_exe" "$build_log_v1"
compile_fixture "$fixture_v2" "$patch_v2" "$build_log_v2"
compile_fixture "$fixture_v3" "$patch_v3" "$build_log_v3"
cp "$patch_v3" "$patch_v3_growth"
dd if=/dev/zero bs=4096 count=1 >>"$patch_v3_growth" 2>/dev/null || true

layout_hash_same="$(hash_file "$fixture_v1")"
layout_hash_changed="$(hash_file "$fixture_v3")"

sh src/tooling/backend_host_runner.sh init \
  --state:"$state_dir" \
  --base:"$base_exe" \
  --layout-hash:"$layout_hash_same" \
  --pool-mb:"${BACKEND_HOSTRUNNER_POOL_MB:-512}" >"$init_log"
host_pid_init="$(read_kv host_pid "$init_log")"
if [ "$host_pid_init" = "" ]; then
  echo "[verify_backend_hotpatch] failed to resolve host_pid from init log" >&2
  exit 1
fi

set +e
sh src/tooling/backend_host_runner.sh run --state:"$state_dir" >"$run_v1_log" 2>&1
run_v1_status="$?"
set -e
if [ "$run_v1_status" -ne 11 ]; then
  echo "[verify_backend_hotpatch] expected v1 run exit=11, got $run_v1_status" >&2
  sed -n '1,120p' "$run_v1_log" >&2 || true
  exit 1
fi

apply_start_ms="$(now_ms)"
sh src/tooling/backend_hotpatch_apply.sh \
  --base:"$base_exe" \
  --patch:"$patch_v2" \
  --symbol:hotpatch_target \
  --state:"$state_dir" \
  --layout-hash:"$layout_hash_same" >"$apply_log" 2>&1
apply_end_ms="$(now_ms)"
apply_ms=$((apply_end_ms - apply_start_ms))
apply_commit_kind="$(read_kv commit_kind "$apply_log")"
if [ "$apply_commit_kind" != "append" ]; then
  echo "[verify_backend_hotpatch] expected append commit for v2 patch, got: $apply_commit_kind" >&2
  sed -n '1,120p' "$apply_log" >&2 || true
  exit 1
fi

set +e
sh src/tooling/backend_host_runner.sh run --state:"$state_dir" >"$run_v2_log" 2>&1
run_v2_status="$?"
set -e
if [ "$run_v2_status" -ne 22 ]; then
  echo "[verify_backend_hotpatch] expected v2 run exit=22, got $run_v2_status" >&2
  sed -n '1,120p' "$run_v2_log" >&2 || true
  exit 1
fi

host_pid_after_append="$(sh src/tooling/backend_host_runner.sh status --state:"$state_dir" | awk -F= '$1=="host_pid"{print $2; exit}')"
if [ "$host_pid_after_append" != "$host_pid_init" ]; then
  echo "[verify_backend_hotpatch] host pid drift after append commit: init=$host_pid_init current=$host_pid_after_append" >&2
  exit 1
fi

set +e
sh src/tooling/backend_hotpatch_apply.sh \
  --base:"$base_exe" \
  --patch:"$out_dir/missing.patch.$safe_target" \
  --symbol:hotpatch_target \
  --state:"$state_dir" \
  --layout-hash:"$layout_hash_same" >"$compile_fail_log" 2>&1
compile_fail_status="$?"
set -e
if [ "$compile_fail_status" -eq 0 ]; then
  echo "[verify_backend_hotpatch] expected missing patch commit to fail" >&2
  exit 1
fi

set +e
sh src/tooling/backend_host_runner.sh run --state:"$state_dir" >"$run_after_fail_log" 2>&1
run_after_fail_status="$?"
set -e
if [ "$run_after_fail_status" -ne 22 ]; then
  echo "[verify_backend_hotpatch] compile failure should keep old runnable version (expected 22, got $run_after_fail_status)" >&2
  sed -n '1,120p' "$run_after_fail_log" >&2 || true
  exit 1
fi

set +e
sh src/tooling/backend_hotpatch_apply.sh \
  --base:"$base_exe" \
  --patch:"$patch_v3_growth" \
  --symbol:hotpatch_target \
  --state:"$state_dir" \
  --layout-hash:"$layout_hash_same" \
  --max-growth:0 >"$growth_restart_log" 2>&1
growth_restart_status="$?"
set -e
if [ "$growth_restart_status" -ne 0 ]; then
  echo "[verify_backend_hotpatch] max-growth restart commit failed unexpectedly" >&2
  sed -n '1,160p' "$growth_restart_log" >&2 || true
  exit 1
fi
growth_commit_kind="$(read_kv commit_kind "$growth_restart_log")"
case "$growth_commit_kind" in
  restart_*) ;;
  *)
    echo "[verify_backend_hotpatch] expected max-growth path to restart, got commit_kind=$growth_commit_kind" >&2
    exit 1
    ;;
esac

set +e
sh src/tooling/backend_host_runner.sh run --state:"$state_dir" >"$run_v3_log" 2>&1
run_v3_status="$?"
set -e
if [ "$run_v3_status" -ne 33 ]; then
  echo "[verify_backend_hotpatch] expected v3 run exit=33 after max-growth restart, got $run_v3_status" >&2
  sed -n '1,120p' "$run_v3_log" >&2 || true
  exit 1
fi

set +e
sh src/tooling/backend_hotpatch_apply.sh \
  --base:"$base_exe" \
  --patch:"$patch_v2" \
  --symbol:hotpatch_target \
  --state:"$state_dir" \
  --layout-hash:"$layout_hash_changed" >"$layout_restart_log" 2>&1
layout_restart_status="$?"
set -e
if [ "$layout_restart_status" -ne 0 ]; then
  echo "[verify_backend_hotpatch] layout-change restart commit failed unexpectedly" >&2
  sed -n '1,160p' "$layout_restart_log" >&2 || true
  exit 1
fi
layout_commit_kind="$(read_kv commit_kind "$layout_restart_log")"
if [ "$layout_commit_kind" != "restart_layout_change" ]; then
  echo "[verify_backend_hotpatch] expected layout restart commit_kind=restart_layout_change, got $layout_commit_kind" >&2
  exit 1
fi

set +e
sh src/tooling/backend_host_runner.sh run --state:"$state_dir" >"$run_after_layout_restart_log" 2>&1
run_after_layout_restart_status="$?"
set -e
if [ "$run_after_layout_restart_status" -ne 22 ]; then
  echo "[verify_backend_hotpatch] expected run exit=22 after layout-change restart, got $run_after_layout_restart_status" >&2
  sed -n '1,120p' "$run_after_layout_restart_log" >&2 || true
  exit 1
fi

status_log="$out_dir/hotpatch_slot.status.log"
sh src/tooling/backend_host_runner.sh status --state:"$state_dir" >"$status_log"
host_pid_final="$(read_kv host_pid "$status_log")"
restart_count="$(read_kv restart_count "$status_log")"
commit_epoch="$(read_kv commit_epoch "$status_log")"
pool_used_bytes="$(read_kv pool_used_bytes "$status_log")"
pool_limit_bytes="$(read_kv pool_limit_bytes "$status_log")"

if [ "$host_pid_final" != "$host_pid_init" ]; then
  echo "[verify_backend_hotpatch] host pid drift detected: init=$host_pid_init final=$host_pid_final" >&2
  exit 1
fi

max_ms="${BACKEND_HOTPATCH_MAX_MS:-2000}"
case "$max_ms" in
  ''|*[!0-9]*)
    max_ms="2000"
    ;;
esac
if [ "$apply_ms" -gt "$max_ms" ]; then
  echo "[verify_backend_hotpatch] patch latency exceeded threshold: apply_ms=$apply_ms threshold_ms=$max_ms" >&2
  exit 1
fi

{
  echo "verify_backend_hotpatch report"
  echo "status=ok"
  echo "target=$target"
  echo "host_target=$host_target"
  echo "fixture_v1=$fixture_v1"
  echo "fixture_v2=$fixture_v2"
  echo "fixture_v3=$fixture_v3"
  echo "base_exe=$base_exe"
  echo "patch_v2=$patch_v2"
  echo "patch_v3=$patch_v3"
  echo "patch_v3_growth=$patch_v3_growth"
  echo "state_dir=$state_dir"
  echo "host_pid=$host_pid_init"
  echo "host_pid_final=$host_pid_final"
  echo "append_commit_kind=$apply_commit_kind"
  echo "growth_restart_commit_kind=$growth_commit_kind"
  echo "layout_restart_commit_kind=$layout_commit_kind"
  echo "apply_ms=$apply_ms"
  echo "max_ms=$max_ms"
  echo "run_v1_status=$run_v1_status"
  echo "run_v2_status=$run_v2_status"
  echo "run_v3_status=$run_v3_status"
  echo "run_after_fail_status=$run_after_fail_status"
  echo "run_after_layout_restart_status=$run_after_layout_restart_status"
  echo "compile_fail_status=$compile_fail_status"
  echo "growth_restart_status=$growth_restart_status"
  echo "layout_restart_status=$layout_restart_status"
  echo "rollback_status=$compile_fail_status"
  echo "overflow_status=$growth_restart_status"
  echo "restart_count=$restart_count"
  echo "commit_epoch=$commit_epoch"
  echo "pool_used_bytes=$pool_used_bytes"
  echo "pool_limit_bytes=$pool_limit_bytes"
  echo "layout_hash_same=$layout_hash_same"
  echo "layout_hash_changed=$layout_hash_changed"
  echo "build_log_v1=$build_log_v1"
  echo "build_log_v2=$build_log_v2"
  echo "build_log_v3=$build_log_v3"
  echo "init_log=$init_log"
  echo "apply_log=$apply_log"
  echo "compile_fail_log=$compile_fail_log"
  echo "growth_restart_log=$growth_restart_log"
  echo "layout_restart_log=$layout_restart_log"
  echo "status_log=$status_log"
} >"$report"

echo "verify_backend_hotpatch ok"
