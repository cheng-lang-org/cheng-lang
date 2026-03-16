#!/usr/bin/env sh
:
set -eu
(set -o pipefail) 2>/dev/null && set -o pipefail

root="$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)"
cd "$root"

tool="${TOOLING_SELF_BIN:-artifacts/tooling_cmd/cheng_tooling}"
if [ "${BACKEND_DRIVER:-}" != "" ]; then
  driver="${BACKEND_DRIVER}"
elif [ -x "$root/artifacts/backend_driver/cheng" ]; then
  driver="$root/artifacts/backend_driver/cheng"
else
  driver="$("$tool" backend_driver_path 2>/dev/null | tail -n 1 | tr -d '\r')"
fi
if [ ! -x "$driver" ]; then
  echo "[verify_backend_closedloop] backend driver not executable: $driver" 1>&2
  exit 1
fi

backend_mm="${BACKEND_MM:-${MM:-orc}}"
case "$backend_mm" in
  ""|orc)
    backend_mm="orc"
    ;;
  *)
    echo "[verify_backend_closedloop] invalid BACKEND_MM/MM: $backend_mm (expected orc)" 1>&2
    exit 2
    ;;
esac

backend_linker="${BACKEND_LINKER:-self}"
case "$backend_linker" in
  self|system)
    ;;
  *)
    echo "[verify_backend_closedloop] invalid BACKEND_LINKER: $backend_linker (expected self|system)" 1>&2
    exit 2
    ;;
esac

backend_target="${BACKEND_TARGET:-$("$tool" detect_host_target 2>/dev/null || echo arm64-apple-darwin)}"
backend_runtime_obj="${BACKEND_RUNTIME_OBJ:-}"

runtime_nm_has_sym() {
  sym="$1"
  printf '%s\n' "$nm_out" | awk '{print $NF}' | sed 's/^_//' | grep -Fxq "$sym"
}

runtime_has_core_symbols() {
  obj="$1"
  [ -f "$obj" ] || return 1
  if ! command -v nm >/dev/null 2>&1; then
    return 0
  fi
  nm_out="$(nm -g "$obj" 2>/dev/null || true)"
  [ "$nm_out" != "" ] || return 1
  runtime_nm_has_sym cheng_strlen &&
  runtime_nm_has_sym cheng_memcpy &&
  runtime_nm_has_sym cheng_memset &&
  runtime_nm_has_sym cheng_malloc &&
  runtime_nm_has_sym cheng_free &&
  runtime_nm_has_sym cheng_mem_retain &&
  runtime_nm_has_sym cheng_mem_release &&
  runtime_nm_has_sym cheng_seq_get &&
  runtime_nm_has_sym cheng_seq_set &&
  runtime_nm_has_sym cheng_strcmp &&
  runtime_nm_has_sym cheng_f32_bits_to_i64 &&
  runtime_nm_has_sym cheng_f64_bits_to_i64 &&
  runtime_nm_has_sym cheng_open_w_trunc
}

resolve_runtime_obj() {
  if [ "$backend_runtime_obj" != "" ]; then
    if [ -f "$backend_runtime_obj" ] && runtime_has_core_symbols "$backend_runtime_obj"; then
      printf '%s\n' "$backend_runtime_obj"
      return 0
    fi
    echo "[Warn] ignore explicit BACKEND_RUNTIME_OBJ (missing required self-link symbols): $backend_runtime_obj" 1>&2
  fi

  candidates="
chengcache/runtime_selflink/system_helpers.backend.combined.${backend_target}.o
artifacts/backend_mm/system_helpers.backend.combined.${backend_target}.o
chengcache/system_helpers.backend.cheng.${backend_target}.o
artifacts/backend_selfhost_self_obj/stage1.native.runtime.dedup.o
artifacts/backend_selfhost_self_obj/system_helpers.backend.cheng.stage1shim.o
chengcache/system_helpers.backend.cheng.o
artifacts/backend_selfhost_self_obj/system_helpers.backend.cheng.o
chengcache/runtime_selflink/system_helpers.backend.combined.arm64-apple-darwin.o
artifacts/backend_mm/system_helpers.backend.combined.arm64-apple-darwin.o
"
  for cand in $candidates; do
    [ "$cand" != "" ] || continue
    if [ -f "$cand" ] && runtime_has_core_symbols "$cand"; then
      printf '%s\n' "$cand"
      return 0
    fi
  done
  for cand in $candidates; do
    [ "$cand" != "" ] || continue
    if [ -f "$cand" ]; then
      printf '%s\n' "$cand"
      return 0
    fi
  done
  return 1
}

if [ "$backend_linker" = "self" ]; then
  backend_runtime_obj="$(resolve_runtime_obj || true)"
fi
if [ "$backend_linker" = "self" ] && [ "$backend_runtime_obj" = "" -o ! -f "$backend_runtime_obj" ]; then
  echo "[verify_backend_closedloop] missing self-link runtime object: $backend_runtime_obj" 1>&2
  exit 2
fi

run_step() {
  name="$1"
  shift
  echo "== ${name} =="
  "$@"
}

mkdir -p artifacts/backend_closedloop
rm -rf artifacts/backend_closedloop/*

stage1_generic_mode="${BACKEND_CLOSEDLOOP_STAGE1_GENERIC_MODE:-dict}"
stage1_generic_budget="${BACKEND_CLOSEDLOOP_STAGE1_GENERIC_SPEC_BUDGET:-${GENERIC_SPEC_BUDGET:-0}}"

compile_smoke() {
  in="$1"
  out="$2"
  env -u BACKEND_DRIVER -u CHENG_BACKEND_DRIVER -u BACKEND_LINKER \
    MM="$backend_mm" \
    GENERIC_MODE="$stage1_generic_mode" \
    GENERIC_SPEC_BUDGET="$stage1_generic_budget" \
    "$tool" cheng --target:"$backend_target" --in:"$in" --out:"$out"
}

compile_smoke_nosystem() {
  in="$1"
  out="$2"
  env -u BACKEND_DRIVER -u CHENG_BACKEND_DRIVER -u BACKEND_LINKER \
    STAGE1_AUTO_SYSTEM=0 \
    MM="$backend_mm" \
    GENERIC_MODE="$stage1_generic_mode" \
    GENERIC_SPEC_BUDGET="$stage1_generic_budget" \
    "$tool" cheng --target:"$backend_target" --in:"$in" --out:"$out"
}

run_step "backend.closedloop_stage1_smoke.compile" \
  compile_smoke tests/cheng/backend/fixtures/hello_puts.cheng \
  artifacts/backend_closedloop/stage1_smoke
run_step "backend.closedloop_stage1_smoke.run" \
  test -x artifacts/backend_closedloop/stage1_smoke

run_step "backend.closedloop_stage1_nosystem_strlit_smoke.compile" \
  compile_smoke_nosystem tests/cheng/backend/fixtures/hello_literal_puts_nosystem.cheng \
  artifacts/backend_closedloop/stage1_nosystem_strlit_smoke
run_step "backend.closedloop_stage1_nosystem_strlit_smoke.run" \
  sh -c 'artifacts/backend_closedloop/stage1_nosystem_strlit_smoke | grep -Fq "hello literal puts nosystem"'

run_step "backend.closedloop_stage1_std_os_smoke.compile" \
  compile_smoke tests/cheng/backend/fixtures/return_import_std_os.cheng \
  artifacts/backend_closedloop/stage1_std_os_smoke
run_step "backend.closedloop_stage1_std_os_smoke.run" \
  test -x artifacts/backend_closedloop/stage1_std_os_smoke

run_fullspec="${BACKEND_RUN_FULLSPEC:-1}"
if [ "$run_fullspec" = "1" ]; then
  fullspec_input="${BACKEND_CLOSEDLOOP_FULLSPEC_INPUT:-examples/backend_closedloop_fullspec.cheng}"
  fullspec_out="artifacts/backend_closedloop/fullspec_backend"
  fullspec_log="artifacts/backend_closedloop/fullspec_backend.compile.log"
  fullspec_run_log="artifacts/backend_closedloop/fullspec_backend.run.log"
  fullspec_generic_mode="${BACKEND_FULLSPEC_GENERIC_MODE:-dict}"
  fullspec_generic_budget="${BACKEND_FULLSPEC_GENERIC_SPEC_BUDGET:-0}"

  echo "== backend.closedloop_fullspec.compile =="
  if [ "$backend_linker" = "self" ]; then
    echo "[verify_backend_closedloop] fullspec self-link uses strict system-link fullspec path" 1>&2
  fi
  set +e
  env \
    BACKEND_BUILD_TRACK=release \
    BACKEND_LINKER=system \
    BACKEND_NO_RUNTIME_C=0 \
    BACKEND_DIRECT_EXE=0 \
    BACKEND_LINKERLESS_INMEM=0 \
    BACKEND_FAST_FALLBACK_ALLOW=0 \
    BACKEND_OPT_LEVEL=3 \
    MM="$backend_mm" \
    GENERIC_MODE="$fullspec_generic_mode" \
    GENERIC_SPEC_BUDGET="$fullspec_generic_budget" \
    BACKEND_TARGET="$backend_target" \
    BACKEND_INPUT="$fullspec_input" \
    BACKEND_OUTPUT="$fullspec_out" \
    "$driver" \
    >"$fullspec_log" 2>&1
  status="$?"
  set -e
  if [ "$status" -ne 0 ]; then
    tail -n 200 "$fullspec_log" 1>&2 || true
    exit "$status"
  fi
  if [ ! -x "$fullspec_out" ]; then
    echo "[verify_backend_closedloop] fullspec compile did not produce runnable binary" 1>&2
    exit 1
  fi
  if command -v nm >/dev/null 2>&1; then
    echo "== backend.closedloop_fullspec.symcheck =="
    if nm "$fullspec_out" 2>/dev/null | rg -q "U[[:space:]].*seqBytesOf_T"; then
      echo "[verify_backend_closedloop] unresolved seqBytesOf_T symbol in fullspec binary" 1>&2
      exit 1
    fi
  fi
  echo "== backend.closedloop_fullspec.run =="
  set +e
  ./artifacts/backend_closedloop/fullspec_backend >"$fullspec_run_log" 2>&1
  status="$?"
  set -e
  if [ "$status" -ne 0 ]; then
    tail -n 120 "$fullspec_run_log" 1>&2 || true
    exit "$status"
  fi
  if rg -q "[^[:space:]]" "$fullspec_run_log" && ! rg -q "fullspec ok" "$fullspec_run_log"; then
    echo "[verify_backend_closedloop] fullspec run output missing marker" 1>&2
    tail -n 120 "$fullspec_run_log" 1>&2 || true
    exit 1
  fi
fi

run_step "backend.no_obj_artifacts" "$tool" verify_backend_no_obj_artifacts

echo "verify_backend_closedloop ok"
