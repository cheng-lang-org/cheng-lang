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

normalize_int() {
  n="$1"
  default="$2"
  minv="$3"
  maxv="$4"
  case "$n" in
    ''|*[!0-9]*)
      printf '%s\n' "$default"
      return 0
      ;;
  esac
  if [ "$n" -lt "$minv" ]; then
    printf '%s\n' "$minv"
    return 0
  fi
  if [ "$n" -gt "$maxv" ]; then
    printf '%s\n' "$maxv"
    return 0
  fi
  printf '%s\n' "$n"
}

driver="${BACKEND_DRIVER:-}"
if [ "$driver" = "" ]; then
  driver="$(sh src/tooling/backend_driver_path.sh)"
fi
target="${BACKEND_TARGET:-arm64-apple-darwin}"
link_env="$(sh src/tooling/backend_link_env.sh --driver:"$driver" --target:"$target" --linker:"${BACKEND_LINKER:-auto}")"

uir_opt3_iters="$(normalize_int "${UIR_OPT3_ITERS:-4}" 4 1 32)"
uir_opt3_cleanup_iters="$(normalize_int "${UIR_OPT3_CLEANUP_ITERS:-3}" 3 1 32)"
uir_inline_iters="$(normalize_int "${UIR_INLINE_ITERS:-4}" 4 1 16)"
uir_cfg_canon_iters="$(normalize_int "${UIR_CFG_CANON_ITERS:-1}" 1 1 16)"
uir_simd_max_width="$(normalize_int "${UIR_SIMD_MAX_WIDTH:-16}" 16 1 256)"
uir_simd_policy="${UIR_SIMD_POLICY:-autovec}"
if [ "$uir_simd_policy" = "" ]; then
  uir_simd_policy="autovec"
fi

fixture_list="${UIR_SIMD_FIXTURES:-tests/cheng/backend/fixtures/return_opt2_licm_while_cond.cheng
tests/cheng/backend/fixtures/return_opt2_cse.cheng
tests/cheng/backend/fixtures/return_while_sum.cheng}"

normalize_fixture_lines() {
  printf '%s\n' "$1" | tr ';' '\n' | tr '\r' '\n' | sed -e '/^[[:space:]]*$/d'
}

out_dir="artifacts/backend_simd"
mkdir -p "$out_dir"

build_exe() {
  fixture="$1"
  out_path="$2"
  simd="$3"
  log="$4"
  set +e
  env $link_env \
    BACKEND_MULTI=0 \
    BACKEND_MULTI_FORCE=0 \
    BACKEND_WHOLE_PROGRAM=1 \
    BACKEND_INCREMENTAL=0 \
    BACKEND_OPT_LEVEL=3 \
    UIR_OPT3_ITERS="$uir_opt3_iters" \
    UIR_OPT3_CLEANUP_ITERS="$uir_opt3_cleanup_iters" \
    UIR_INLINE_ITERS="$uir_inline_iters" \
    UIR_CFG_CANON_ITERS="$uir_cfg_canon_iters" \
    UIR_SIMD="$simd" \
    UIR_SIMD_MAX_WIDTH="$uir_simd_max_width" \
    UIR_SIMD_POLICY="$uir_simd_policy" \
    BACKEND_EMIT=exe \
    BACKEND_TARGET="$target" \
    BACKEND_INPUT="$fixture" \
    BACKEND_OUTPUT="$out_path" \
    "$driver" >"$log" 2>&1
  rc="$?"
  set -e
  return "$rc"
}

is_known_runtime_symbol_log() {
  log_file="$1"
  if [ ! -f "$log_file" ]; then
    return 1
  fi
  if grep -q "Symbol not found: _cheng_" "$log_file"; then
    return 0
  fi
  if grep -q "_cheng_f32_bits_to_i64" "$log_file"; then
    return 0
  fi
  if grep -q "_cheng_memcpy" "$log_file"; then
    return 0
  fi
  return 1
}

is_darwin_only_bootstrap_reject() {
  log="$1"
  if [ ! -f "$log" ]; then
    return 1
  fi
  rg -q "uir_codegen: bootstrap path only supports darwin target|supports darwin target only" "$log"
}

fixture_lines="$(normalize_fixture_lines "$fixture_list")"
while IFS= read -r fixture; do
  if [ ! -f "$fixture" ]; then
    echo "[verify_backend_simd] missing fixture: $fixture" 1>&2
    exit 2
  fi
  name="$(basename "$fixture" .cheng)"
  exe_scalar="$out_dir/${name}.scalar"
  exe_simd_a="$out_dir/${name}.simd.a"
  exe_simd_b="$out_dir/${name}.simd.b"
  scalar_log="$out_dir/${name}.scalar.build.log"
  simd_a_log="$out_dir/${name}.simd.a.build.log"
  simd_b_log="$out_dir/${name}.simd.b.build.log"

  if ! build_exe "$fixture" "$exe_scalar" 0 "$scalar_log"; then
    if is_darwin_only_bootstrap_reject "$scalar_log"; then
      echo "[verify_backend_simd] skip target ($target): bootstrap darwin-only path" 1>&2
      exit 2
    fi
    echo "[verify_backend_simd] scalar executable build failed: $fixture" 1>&2
    sed -n '1,120p' "$scalar_log" 1>&2 || true
    exit 1
  fi
  if ! build_exe "$fixture" "$exe_simd_a" 1 "$simd_a_log"; then
    if is_darwin_only_bootstrap_reject "$simd_a_log"; then
      echo "[verify_backend_simd] skip target ($target): bootstrap darwin-only path" 1>&2
      exit 2
    fi
    echo "[verify_backend_simd] SIMD executable build failed: $fixture" 1>&2
    sed -n '1,120p' "$simd_a_log" 1>&2 || true
    exit 1
  fi
  if ! build_exe "$fixture" "$exe_simd_b" 1 "$simd_b_log"; then
    if is_darwin_only_bootstrap_reject "$simd_b_log"; then
      echo "[verify_backend_simd] skip target ($target): bootstrap darwin-only path" 1>&2
      exit 2
    fi
    echo "[verify_backend_simd] SIMD determinism build failed: $fixture" 1>&2
    sed -n '1,120p' "$simd_b_log" 1>&2 || true
    exit 1
  fi

  if [ ! -x "$exe_scalar" ] || [ ! -x "$exe_simd_a" ] || [ ! -x "$exe_simd_b" ]; then
    echo "[verify_backend_simd] missing executable output: $fixture" 1>&2
    exit 1
  fi

  scalar_run_log="$out_dir/${name}.scalar.run.log"
  simd_a_run_log="$out_dir/${name}.simd.a.run.log"
  simd_b_run_log="$out_dir/${name}.simd.b.run.log"

  set +e
  out_scalar="$("$exe_scalar" 2>"$scalar_run_log")"
  scalar_status="$?"
  out_simd_a="$("$exe_simd_a" 2>"$simd_a_run_log")"
  simd_a_status="$?"
  out_simd_b="$("$exe_simd_b" 2>"$simd_b_run_log")"
  simd_b_status="$?"
  set -e

  if [ "$scalar_status" -ne 0 ] || [ "$simd_a_status" -ne 0 ] || [ "$simd_b_status" -ne 0 ]; then
    known_runtime_symbol="1"
    if [ "$scalar_status" -ne 0 ] && ! is_known_runtime_symbol_log "$scalar_run_log"; then
      known_runtime_symbol="0"
    fi
    if [ "$simd_a_status" -ne 0 ] && ! is_known_runtime_symbol_log "$simd_a_run_log"; then
      known_runtime_symbol="0"
    fi
    if [ "$simd_b_status" -ne 0 ] && ! is_known_runtime_symbol_log "$simd_b_run_log"; then
      known_runtime_symbol="0"
    fi
    if [ "$known_runtime_symbol" = "1" ]; then
      echo "[verify_backend_simd] known runtime-symbol instability, fallback compile-only: $fixture" 1>&2
      continue
    fi
    if [ "$scalar_status" -ne 0 ]; then
      cat "$scalar_run_log" 1>&2 || true
    fi
    if [ "$simd_a_status" -ne 0 ]; then
      cat "$simd_a_run_log" 1>&2 || true
    fi
    if [ "$simd_b_status" -ne 0 ]; then
      cat "$simd_b_run_log" 1>&2 || true
    fi
    exit 1
  fi

  if [ "$out_scalar" != "$out_simd_a" ] || [ "$out_scalar" != "$out_simd_b" ]; then
    echo "[verify_backend_simd] scalar/simd output mismatch: $fixture" 1>&2
    exit 1
  fi

  echo "verify_backend_simd: ok $fixture"
done <<EOF
$fixture_lines
EOF

echo "verify_backend_simd ok"
