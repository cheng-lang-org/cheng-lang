#!/usr/bin/env sh
set -eu
(set -o pipefail) 2>/dev/null && set -o pipefail

root="$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)"
cd "$root"

if [ "${CHENG_CLEAN_BACKEND_MVP_DRIVER_LOCAL:-1}" = "1" ] && [ "${CHENG_TOOLING_CLEANUP_DEPTH:-0}" = "0" ]; then
  export CHENG_TOOLING_CLEANUP_DEPTH=1
  cleanup_backend_driver_on_exit() {
    status=$?
    set +e
    sh src/tooling/cleanup_backend_mvp_driver_local.sh
    exit "$status"
  }
  trap cleanup_backend_driver_on_exit EXIT
fi

strict="${CHENG_BACKEND_MATRIX_STRICT:-0}"
run_fullspec="${CHENG_BACKEND_RUN_FULLSPEC:-0}"
host_os="$(uname -s 2>/dev/null || echo unknown)"
host_arch="$(uname -m 2>/dev/null || echo unknown)"
driver=""
if [ -x "artifacts/backend_selfhost_self_obj/backend_mvp_driver.stage2" ]; then
  driver="artifacts/backend_selfhost_self_obj/backend_mvp_driver.stage2"
elif [ -x "artifacts/backend_selfhost/backend_mvp_driver.stage2" ]; then
  driver="artifacts/backend_selfhost/backend_mvp_driver.stage2"
else
  driver="$(sh src/tooling/backend_driver_path.sh)"
fi
export CHENG_BACKEND_DRIVER="$driver"
backend_mm="${CHENG_BACKEND_MM:-${CHENG_MM:-orc}}"
backend_linker="${CHENG_BACKEND_LINKER:-self}"
backend_target="${CHENG_BACKEND_TARGET:-arm64-apple-darwin}"
safe_target="$(printf '%s' "$backend_target" | tr -c 'A-Za-z0-9._-' '_' | tr -s '_')"
backend_runtime_obj="${CHENG_BACKEND_RUNTIME_OBJ:-chengcache/system_helpers.backend.cheng.${safe_target}.o}"
case "$backend_mm" in
  ""|orc)
    backend_mm="orc"
    ;;
  off|none|0)
    backend_mm="off"
    ;;
  *)
    echo "[verify_backend_closedloop] invalid CHENG_BACKEND_MM/CHENG_MM: $backend_mm (expected orc|off)" 1>&2
    exit 2
    ;;
esac

case "$backend_linker" in
  self|system)
    ;;
  *)
    echo "[verify_backend_closedloop] invalid CHENG_BACKEND_LINKER: $backend_linker (expected self|system)" 1>&2
    exit 2
    ;;
esac

run_step() {
  name="$1"
  shift
  echo "== ${name} =="
  set +e
  "$@"
  status="$?"
  set -e
  if [ "$status" -eq 0 ]; then
    return 0
  fi
  if [ "$status" -eq 2 ]; then
    if [ "$strict" = "1" ]; then
      echo "[verify_backend_closedloop] ${name} returned skip (strict mode)" 1>&2
      exit 1
    fi
    echo "== ${name} (skip) =="
    return 0
  fi
  exit "$status"
}

if [ "$backend_linker" = "self" ]; then
  mkdir -p chengcache
  if [ ! -f "$backend_runtime_obj" ] || [ "src/std/system_helpers_backend.cheng" -nt "$backend_runtime_obj" ]; then
    run_step "backend.runtime_obj.build" env \
      CHENG_MM="$backend_mm" \
      CHENG_BACKEND_ALLOW_NO_MAIN=1 \
      CHENG_BACKEND_WHOLE_PROGRAM=1 \
      CHENG_BACKEND_EMIT=obj \
      CHENG_BACKEND_TARGET="$backend_target" \
      CHENG_BACKEND_FRONTEND=mvp \
      CHENG_BACKEND_INPUT=src/std/system_helpers_backend.cheng \
      CHENG_BACKEND_OUTPUT="$backend_runtime_obj" \
      "$driver"
  fi
fi

run_step "backend.targets" sh src/tooling/verify_backend_targets.sh
run_step "backend.targets_matrix" sh src/tooling/verify_backend_targets_matrix.sh
run_step "backend.runtime_abi" sh src/tooling/verify_backend_runtime_abi.sh
run_step "backend.std_layout_sync" sh src/tooling/verify_std_layout_sync.sh
run_step "backend.stage1_seed_layout" sh src/tooling/verify_stage1_seed_layout.sh
run_step "backend.std_import_surface" sh src/tooling/verify_std_import_surface.sh
if [ "$host_os" = "Darwin" ]; then
  run_step "backend.x86_64_darwin" sh src/tooling/verify_backend_x86_64_darwin.sh
fi
if [ "$host_os/$host_arch" = "Linux/x86_64" ]; then
  run_step "backend.x86_64_linux" sh src/tooling/verify_backend_x86_64_linux.sh
fi
run_step "backend.determinism" sh src/tooling/verify_backend_determinism.sh
run_step "backend.multi" env \
  CHENG_BACKEND_LINKER="$backend_linker" \
  CHENG_BACKEND_TARGET="$backend_target" \
  CHENG_BACKEND_RUNTIME_OBJ="$backend_runtime_obj" \
  sh src/tooling/verify_backend_multi.sh
run_step "backend.mvp" env \
  CHENG_BACKEND_LINKER="$backend_linker" \
  CHENG_BACKEND_TARGET="$backend_target" \
  CHENG_BACKEND_RUNTIME_OBJ="$backend_runtime_obj" \
  sh src/tooling/verify_backend_mvp.sh

mkdir -p artifacts/backend_closedloop

if [ "$backend_linker" = "self" ]; then
  run_step "backend.closedloop_stage1_smoke.compile" env \
    CHENG_MM="$backend_mm" \
    CHENG_C_SYSTEM=system \
    CHENG_BACKEND_FRONTEND=stage1 \
    CHENG_BACKEND_EMIT=exe \
    CHENG_BACKEND_LINKER=self \
    CHENG_BACKEND_NO_RUNTIME_C=1 \
    CHENG_BACKEND_RUNTIME_OBJ="$backend_runtime_obj" \
    CHENG_BACKEND_TARGET="$backend_target" \
    CHENG_BACKEND_INPUT=tests/cheng/backend/fixtures/hello_puts.cheng \
    CHENG_BACKEND_OUTPUT=artifacts/backend_closedloop/stage1_smoke \
    "$driver"
else
  run_step "backend.closedloop_stage1_smoke.compile" env \
    CHENG_MM="$backend_mm" \
    CHENG_C_SYSTEM=system \
    CHENG_BACKEND_FRONTEND=stage1 \
    CHENG_BACKEND_EMIT=exe \
    CHENG_BACKEND_LINKER=system \
    CHENG_BACKEND_TARGET="$backend_target" \
    CHENG_BACKEND_INPUT=tests/cheng/backend/fixtures/hello_puts.cheng \
    CHENG_BACKEND_OUTPUT=artifacts/backend_closedloop/stage1_smoke \
    "$driver"
fi
run_step "backend.closedloop_stage1_smoke.run" sh -c '
  ./artifacts/backend_closedloop/stage1_smoke | grep -Fq "hello from cheng backend"
'

if [ "$run_fullspec" = "1" ]; then
  if [ "$backend_linker" = "self" ]; then
    run_step "backend.closedloop_fullspec.compile" env \
      CHENG_MM="$backend_mm" \
      CHENG_C_SYSTEM=system \
      CHENG_BACKEND_FRONTEND=stage1 \
      CHENG_BACKEND_EMIT=exe \
      CHENG_BACKEND_VALIDATE=1 \
      CHENG_BACKEND_LINKER=self \
      CHENG_BACKEND_NO_RUNTIME_C=1 \
      CHENG_BACKEND_RUNTIME_OBJ="$backend_runtime_obj" \
      CHENG_BACKEND_TARGET="$backend_target" \
      CHENG_BACKEND_INPUT=examples/stage1_codegen_fullspec.cheng \
      CHENG_BACKEND_OUTPUT=artifacts/backend_closedloop/fullspec_backend \
      "$driver"
  else
    run_step "backend.closedloop_fullspec.compile" env \
      CHENG_MM="$backend_mm" \
      CHENG_C_SYSTEM=system \
      CHENG_BACKEND_FRONTEND=stage1 \
      CHENG_BACKEND_EMIT=exe \
      CHENG_BACKEND_VALIDATE=1 \
      CHENG_BACKEND_LINKER=system \
      CHENG_BACKEND_TARGET="$backend_target" \
      CHENG_BACKEND_INPUT=examples/stage1_codegen_fullspec.cheng \
      CHENG_BACKEND_OUTPUT=artifacts/backend_closedloop/fullspec_backend \
      "$driver"
  fi
  run_step "backend.closedloop_fullspec.run" sh -c '
    ./artifacts/backend_closedloop/fullspec_backend | grep -Fq "fullspec ok"
  '
else
  echo "== backend.closedloop_fullspec (skip: set CHENG_BACKEND_RUN_FULLSPEC=1 to enable) =="
fi

run_step "backend.mm" sh src/tooling/verify_backend_mm.sh
run_step "backend.android" sh src/tooling/verify_backend_android.sh

if [ "${CHENG_ANDROID_RUN:-0}" = "1" ]; then
  run_step "backend.android_run" sh src/tooling/verify_backend_android_run.sh
fi

echo "verify_backend_closedloop ok"
