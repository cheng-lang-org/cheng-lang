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

host_target="$(sh src/tooling/detect_host_target.sh 2>/dev/null || true)"

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
        *arm64*|*aarch64*|*riscv64*|*x86_64*|*amd64*) return 0 ;;
      esac
      return 1
      ;;
    *windows*|*msvc*)
      case "$target" in
        *arm64*|*aarch64*|*x86_64*|*amd64*) return 0 ;;
      esac
      return 1
      ;;
  esac
  return 1
}

select_default_target() {
  if [ "${BACKEND_LINKERLESS_DEV_TARGET:-}" != "" ]; then
    echo "${BACKEND_LINKERLESS_DEV_TARGET}"
    return
  fi
  if [ "$host_target" != "" ]; then
    echo "$host_target"
    return
  fi
  echo "arm64-apple-darwin"
}

target="${BACKEND_TARGET:-}"
target_explicit="0"
if [ "$target" != "" ]; then
  target_explicit="1"
else
  target="$(select_default_target)"
fi

if ! is_supported_target "$target"; then
  if [ "$target_explicit" = "1" ]; then
    echo "[verify_backend_linkerless_dev] unsupported BACKEND_TARGET: $target" >&2
    exit 1
  fi
  echo "[verify_backend_linkerless_dev] skip: unsupported host target ($host_target); set BACKEND_TARGET explicitly" >&2
  exit 2
fi

out_dir="artifacts/backend_linkerless_dev"
rm -rf "$out_dir"
mkdir -p "$out_dir"
safe_target="$(printf '%s' "$target" | tr -c 'A-Za-z0-9._-' '_' | tr -s '_')"
fixture="tests/cheng/backend/fixtures/return_add.cheng"
if [ ! -f "$fixture" ]; then
  fixture="tests/cheng/backend/fixtures/return_i64.cheng"
fi
exe_path="$out_dir/return_add.$safe_target"
build_log="$out_dir/return_add.$safe_target.build.log"
run_log="$out_dir/return_add.$safe_target.run.log"
report="$out_dir/return_add.$safe_target.report.txt"

rm -f "$exe_path" "$exe_path.o" "$build_log" "$run_log" "$report"
rm -rf "$exe_path.objs" "$exe_path.objs.lock"

set +e
env \
  MM="${MM:-orc}" \
  ABI=v2_noptr \
  BACKEND_TARGET="$target" \
  BACKEND_LINKER=self \
  BACKEND_CODESIGN=0 \
  BACKEND_MULTI=0 \
  BACKEND_MULTI_FORCE=0 \
  BACKEND_INCREMENTAL=0 \
  BACKEND_KEEP_EXE_OBJ=0 \
  STAGE1_STD_NO_POINTERS=0 \
  STAGE1_STD_NO_POINTERS_STRICT=0 \
  STAGE1_NO_POINTERS_NON_C_ABI=0 \
  STAGE1_NO_POINTERS_NON_C_ABI_INTERNAL=0 \
  STAGE1_SKIP_SEM="${STAGE1_SKIP_SEM:-0}" \
  STAGE1_SKIP_OWNERSHIP="${STAGE1_SKIP_OWNERSHIP:-1}" \
  sh src/tooling/chengc.sh "$fixture" --frontend:stage1 --emit:exe --out:"$exe_path" >"$build_log" 2>&1
build_status="$?"
set -e

if [ "$build_status" -ne 0 ]; then
  echo "[verify_backend_linkerless_dev] build failed (status=$build_status): $build_log" >&2
  sed -n '1,160p' "$build_log" >&2 || true
  exit 1
fi

if [ ! -s "$exe_path" ]; then
  echo "[verify_backend_linkerless_dev] missing output executable: $exe_path" >&2
  exit 1
fi
if [ -e "$exe_path.o" ]; then
  echo "[verify_backend_linkerless_dev] linkerless dev gate found sidecar obj: $exe_path.o" >&2
  exit 1
fi
if [ -d "$exe_path.objs" ] || [ -d "$exe_path.objs.lock" ]; then
  echo "[verify_backend_linkerless_dev] linkerless dev gate found unexpected multi-object artifacts near: $exe_path" >&2
  exit 1
fi

run_requested="${BACKEND_LINKERLESS_DEV_RUN:-0}"
run_mode="skip"
run_status="0"
if [ "$run_requested" = "1" ] && [ "$target" = "$host_target" ]; then
  case "$target" in
    *windows*|*msvc*|*android*)
      run_mode="skip"
      ;;
    *)
      run_mode="host"
      set +e
      "$exe_path" >"$run_log" 2>&1
      run_status="$?"
      set -e
      if [ "$run_status" -ne 0 ]; then
        echo "[verify_backend_linkerless_dev] executable run failed (status=$run_status): $run_log" >&2
        sed -n '1,120p' "$run_log" >&2 || true
        exit 1
      fi
      ;;
  esac
fi

if [ "$run_mode" = "skip" ]; then
  printf 'skip run: requested=%s target=%s host_target=%s\n' "$run_requested" "$target" "$host_target" >"$run_log"
fi

{
  echo "verify_backend_linkerless_dev report"
  echo "target=$target"
  echo "host_target=$host_target"
  echo "fixture=$fixture"
  echo "exe=$exe_path"
  echo "build_log=$build_log"
  echo "run_log=$run_log"
  echo "run_mode=$run_mode"
  echo "run_status=$run_status"
  echo "sidecar_obj=absent"
  echo "linker=self"
} >"$report"

echo "verify_backend_linkerless_dev ok"
