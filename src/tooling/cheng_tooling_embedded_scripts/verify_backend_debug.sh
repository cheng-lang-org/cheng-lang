#!/usr/bin/env sh
:
set -eu
(set -o pipefail) 2>/dev/null && set -o pipefail

root="$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)"
cd "$root"

if [ "${CLEAN_CHENG_LOCAL:-1}" = "1" ] && [ "${TOOLING_CLEANUP_DEPTH:-0}" = "0" ]; then
  export TOOLING_CLEANUP_DEPTH=1
  cleanup_backend_driver_on_exit() {
    status=$?
    set +e
    ${TOOLING_SELF_BIN:-artifacts/tooling_cmd/cheng_tooling} cleanup_cheng_local
    exit "$status"
  }
  trap cleanup_backend_driver_on_exit EXIT
fi

driver="$(${TOOLING_SELF_BIN:-artifacts/tooling_cmd/cheng_tooling} backend_driver_path)"
requested_linker="${BACKEND_LINKER:-auto}"

resolve_link_env() {
  link_env="$(${TOOLING_SELF_BIN:-artifacts/tooling_cmd/cheng_tooling} backend_link_env --driver:"$driver" --target:"$target" --linker:"$requested_linker")"
  resolved_linker=""
  resolved_no_runtime_c="0"
  resolved_runtime_obj=""
  resolved_runtime_obj_assigned="0"
  for entry in $link_env; do
    case "$entry" in
      BACKEND_LINKER=*)
        resolved_linker="${entry#BACKEND_LINKER=}"
        ;;
      BACKEND_NO_RUNTIME_C=*)
        resolved_no_runtime_c="${entry#BACKEND_NO_RUNTIME_C=}"
        ;;
      BACKEND_RUNTIME_OBJ=*)
        resolved_runtime_obj="${entry#BACKEND_RUNTIME_OBJ=}"
        resolved_runtime_obj_assigned="1"
        ;;
    esac
  done
  if [ "$resolved_linker" = "" ]; then
    echo "[Error] verify_backend_debug: backend_link_env missing BACKEND_LINKER" 1>&2
    exit 1
  fi
}

normalize_bool() {
  raw="$1"
  default="$2"
  if [ "$raw" = "" ]; then
    printf '%s\n' "$default"
    return
  fi
  case "$raw" in
    0|1)
      printf '%s\n' "$raw"
      ;;
    *)
      echo "[Error] invalid boolean value: $raw (expected 0|1)" 1>&2
      exit 2
      ;;
  esac
}

nm_has_symbol() {
  file="$1"
  pattern="$2"
  nm -g "$file" | awk -v pat="$pattern" 'index($0, pat) { found = 1 } END { exit found ? 0 : 1 }'
}

run_driver_compile_exe() {
  validate="$1"
  fixture_path="$2"
  output_path="$3"
  multi_flag="$4"
  multi_force_flag="$5"
  if [ "$resolved_runtime_obj_assigned" = "1" ] && [ "$resolved_no_runtime_c" = "1" ]; then
    env BACKEND_VALIDATE="$validate" \
      "$driver" "$fixture_path" \
        --frontend:stage1 \
        --emit:exe \
        --target:"$target" \
        --linker:"$resolved_linker" \
        "$multi_flag" \
        "$multi_force_flag" \
        --no-runtime-c \
        --runtime-obj:"$resolved_runtime_obj" \
        --output:"$output_path"
    return
  fi
  if [ "$resolved_runtime_obj_assigned" = "1" ]; then
    env BACKEND_VALIDATE="$validate" \
      "$driver" "$fixture_path" \
        --frontend:stage1 \
        --emit:exe \
        --target:"$target" \
        --linker:"$resolved_linker" \
        "$multi_flag" \
        "$multi_force_flag" \
        --runtime-obj:"$resolved_runtime_obj" \
        --output:"$output_path"
    return
  fi
  if [ "$resolved_no_runtime_c" = "1" ]; then
    env BACKEND_VALIDATE="$validate" \
      "$driver" "$fixture_path" \
        --frontend:stage1 \
        --emit:exe \
        --target:"$target" \
        --linker:"$resolved_linker" \
        "$multi_flag" \
        "$multi_force_flag" \
        --no-runtime-c \
        --output:"$output_path"
    return
  fi
  env BACKEND_VALIDATE="$validate" \
    "$driver" "$fixture_path" \
      --frontend:stage1 \
      --emit:exe \
      --target:"$target" \
      --linker:"$resolved_linker" \
      "$multi_flag" \
      "$multi_force_flag" \
      --output:"$output_path"
}

host_os="$(uname -s 2>/dev/null || echo unknown)"
if [ "$host_os" != "Darwin" ]; then
  echo "verify_backend_debug skip: darwin-only (host_os=$host_os)" 1>&2
  exit 2
fi
if ! command -v dsymutil >/dev/null 2>&1; then
  echo "verify_backend_debug skip: missing dsymutil" 1>&2
  exit 2
fi
if ! command -v nm >/dev/null 2>&1; then
  echo "verify_backend_debug skip: missing nm" 1>&2
  exit 2
fi

out_dir="artifacts/backend_debug"
mkdir -p "$out_dir"

fixture="tests/cheng/backend/fixtures/hello_puts.cheng"

build_and_check_dsym() {
  target="$1"
  exe="$2"
  dsym="$exe.dSYM"
  resolve_link_env
  multi="$(normalize_bool "${BACKEND_MULTI:-}" "0")"
  multi_force="$(normalize_bool "${BACKEND_MULTI_FORCE:-}" "$multi")"
  if [ "$multi" = "1" ]; then
    multi_flag="--multi"
  else
    multi_flag="--no-multi"
  fi
  if [ "$multi_force" = "1" ]; then
    multi_force_flag="--multi-force"
  else
    multi_force_flag="--no-multi-force"
  fi

  rm -rf "$exe" "$dsym"
  compile_log="$out_dir/compile.$(basename "$exe").txt"
  rm -f "$compile_log"

  set +e
  run_driver_compile_exe 1 "$fixture" "$exe" "$multi_flag" "$multi_force_flag" >"$compile_log" 2>&1
  status="$?"
  set -e
  if [ "$status" -ne 0 ]; then
    tail -n 200 "$compile_log" 1>&2 || true
    exit "$status"
  fi

  if [ ! -x "$exe" ]; then
    echo "[Error] missing exe output: $exe" 1>&2
    exit 1
  fi

  dsym_log="$out_dir/dsymutil.$(basename "$exe").txt"
  dsymutil "$exe" -o "$dsym" >"$dsym_log" 2>&1 || {
    echo "[Error] dsymutil failed: $exe" 1>&2
    tail -n 80 "$dsym_log" 1>&2 || true
    exit 1
  }

  dwarf="$dsym/Contents/Resources/DWARF/$(basename "$exe")"
  if [ ! -s "$dwarf" ]; then
    echo "[Error] missing dSYM DWARF file: $dwarf" 1>&2
    exit 1
  fi

  nm_has_symbol "$dwarf" "_main" || {
    echo "[Error] missing _main in dSYM: $dwarf" 1>&2
    exit 1
  }
}

build_and_check_dsym "arm64-apple-darwin" "$out_dir/hello_puts.debug.arm64"
build_and_check_dsym "x86_64-apple-darwin" "$out_dir/hello_puts.debug.x86_64"

echo "verify_backend_debug ok"
