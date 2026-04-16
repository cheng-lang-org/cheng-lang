#!/usr/bin/env sh
:
set -eu

root="$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)"
cd "$root"

driver="${BACKEND_DRIVER:-}"
if [ "$driver" = "" ]; then
  driver="$(${TOOLING_SELF_BIN:-artifacts/tooling_cmd/cheng_tooling} backend_driver_path 2>/dev/null || true)"
fi
if [ "$driver" = "" ]; then
  driver="$root/artifacts/v3_backend_driver/cheng"
fi
stage0="${BACKEND_STAGE0_PROBE:-$root/artifacts/backend_selfhost_self_obj/cheng_stage0_default}"
timeout_secs="${BACKEND_PTR_OBJECT_TIMEOUT:-60}"
out_dir="$root/artifacts/backend_ptr_object_abi"
report="$out_dir/backend_ptr_object_abi.report.txt"
mkdir -p "$out_dir"
: >"$report"

log() {
  printf '%s\n' "$1" | tee -a "$report"
}

run_with_timeout_and_sample() {
  timeout="$1"
  log_file="$2"
  sample_file="$3"
  shift 3
  rm -f "$log_file" "$sample_file"
  (
    "$@" >"$log_file" 2>&1
  ) >/dev/null 2>&1 &
  pid=$!
  elapsed=0
  while kill -0 "$pid" 2>/dev/null; do
    if [ "$elapsed" -ge "$timeout" ]; then
      if command -v sample >/dev/null 2>&1; then
        sample "$pid" 5 -file "$sample_file" >/dev/null 2>&1 || true
      fi
      kill -TERM "$pid" 2>/dev/null || true
      sleep 1
      kill -KILL "$pid" 2>/dev/null || true
      wait "$pid" 2>/dev/null || true
      return 124
    fi
    sleep 1
    elapsed=$((elapsed + 1))
  done
  wait "$pid"
  return $?
}

probe_fixture() {
  name="$1"
  fixture="$2"
  exe="$out_dir/$name.exe"
  compile_log="$out_dir/$name.compile.log"
  sample_log="$out_dir/$name.compile.sample.txt"

  compile_rc=0
  if run_with_timeout_and_sample "$timeout_secs" "$compile_log" "$sample_log" \
      env BACKEND_INPUT="$fixture" BACKEND_OUTPUT="$exe" BACKEND_LINKER=system "$driver"; then
    compile_rc=0
  else
    compile_rc=$?
  fi

  run_rc="-"
  if [ "$compile_rc" -eq 0 ]; then
    if "$exe" >/dev/null 2>&1; then
      run_rc=0
    else
      run_rc=$?
    fi
  fi

  status="fail"
  if [ "$compile_rc" -eq 0 ] && [ "$run_rc" = "0" ]; then
    status="pass"
  elif [ "$compile_rc" -eq 124 ]; then
    status="compile_timeout"
  elif [ "$compile_rc" -eq 139 ]; then
    status="compile_sigsegv"
  elif [ "$compile_rc" -eq 223 ]; then
    status="compile_deterministic_exit_223"
  elif [ "$compile_rc" -eq 0 ]; then
    status="runtime_fail"
  fi

  log "[fixture] $name compile_rc=$compile_rc run_rc=$run_rc status=$status"
  if [ -f "$sample_log" ] && [ -s "$sample_log" ]; then
    log "[sample] $name file=$sample_log"
  fi
}

probe_current_source_import() {
  name="$1"
  import_path="$2"
  src="$out_dir/$name.probe.cheng"
  bin="$out_dir/$name.probe.bin"
  log_file="$out_dir/$name.import.log"
  sample_log="$out_dir/$name.import.sample.txt"

  cat >"$src" <<EOF
import $import_path

fn main(): int32 =
    return 0
EOF

  rc=0
  if run_with_timeout_and_sample "$timeout_secs" "$log_file" "$sample_log" \
      env MM=orc STAGE1_SEM_FIXED_0=0 STAGE1_OWNERSHIP_FIXED_0=0 STAGE1_SKIP_CPROFILE=1 \
      STAGE1_AUTO_SYSTEM=0 BACKEND_MULTI=0 BACKEND_MULTI_FORCE=0 BACKEND_INCREMENTAL=1 \
      BACKEND_JOBS=0 BACKEND_VALIDATE=0 BACKEND_LINKER=system BACKEND_NO_RUNTIME_C=0 \
      BACKEND_LDFLAGS='' BACKEND_EMIT=exe BACKEND_TARGET=arm64-apple-darwin \
      BACKEND_FRONTEND=stage1 BACKEND_INPUT="$src" BACKEND_OUTPUT="$bin" "$stage0"; then
    rc=0
  else
    rc=$?
  fi

  status="fail"
  if [ "$rc" -eq 0 ]; then
    status="pass"
  elif [ "$rc" -eq 124 ]; then
    status="compile_timeout"
  elif [ "$rc" -eq 139 ]; then
    status="compile_sigsegv"
  fi
  log "[import] $name rc=$rc status=$status module=$import_path"
  if [ -f "$sample_log" ] && [ -s "$sample_log" ]; then
    log "[sample] $name file=$sample_log"
  fi
}

log "[meta] date=$(date '+%Y-%m-%d %H:%M:%S %z')"
log "[meta] driver=$driver"
log "[meta] stage0=$stage0"
log "[meta] timeout_secs=$timeout_secs"

probe_fixture "object_scalar_control_probe" \
  "$root/tests/cheng/backend/fixtures/object_scalar_control_probe.cheng"
probe_fixture "ptr_object_by_value_probe" \
  "$root/tests/cheng/backend/fixtures/ptr_object_by_value_probe.cheng"
probe_fixture "ptr_object_return_probe" \
  "$root/tests/cheng/backend/fixtures/ptr_object_return_probe.cheng"
probe_fixture "ptr_object_var_assign_probe" \
  "$root/tests/cheng/backend/fixtures/ptr_object_var_assign_probe.cheng"
probe_fixture "buffer_append_bytes" \
  "$root/tests/cheng/backend/fixtures/buffer_append_bytes.cheng"

probe_current_source_import "uir_core_builder_min" "backend/uir/uir_internal/uir_core_builder"
probe_current_source_import "uir_codegen_min" "backend/uir/uir_codegen"

log "[done] report=$report"
