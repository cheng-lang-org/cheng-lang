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

driver="$(sh src/tooling/backend_driver_path.sh)"

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

sha256_file() {
  if command -v shasum >/dev/null 2>&1; then
    shasum -a 256 "$1" | awk '{print $1}'
    return 0
  fi
  if command -v sha256sum >/dev/null 2>&1; then
    sha256sum "$1" | awk '{print $1}'
    return 0
  fi
  return 1
}

target="${BACKEND_TARGET:-$(sh src/tooling/detect_host_target.sh 2>/dev/null || echo auto)}"
reps="$(normalize_int "${UIR_STABILITY_ITERS:-3}" 3 2 12)"
uir_opt2_iters="$(normalize_int "${UIR_OPT2_ITERS:-5}" 5 1 32)"
uir_opt3_iters="$(normalize_int "${UIR_OPT3_ITERS:-4}" 4 1 32)"
uir_opt3_cleanup_iters="$(normalize_int "${UIR_OPT3_CLEANUP_ITERS:-3}" 3 1 32)"
uir_inline_iters="$(normalize_int "${UIR_INLINE_ITERS:-4}" 4 1 16)"
uir_cfg_canon_iters="$(normalize_int "${UIR_CFG_CANON_ITERS:-1}" 1 1 16)"
uir_profile="${UIR_PROFILE:-0}"
uir_simd_raw="${UIR_SIMD:-}"
uir_simd_max_width="${UIR_SIMD_MAX_WIDTH:-0}"
uir_simd_policy="${UIR_SIMD_POLICY:-autovec}"
if [ "$uir_simd_policy" = "" ]; then
  uir_simd_policy="autovec"
fi
modes_raw="${UIR_STABILITY_MODES:-dict}"
if [ "$modes_raw" = "all" ]; then
  modes_raw="dict hybrid"
fi
modes="$(printf '%s' "$modes_raw" | tr ',;' '  ' | tr -s ' ')"
if [ "$modes" = "" ]; then
  modes="dict"
fi
if [ "${UIR_STABILITY_OPT_LEVEL:-}" = "" ]; then
  opt_level="3"
else
  opt_level="$(normalize_int "${UIR_STABILITY_OPT_LEVEL}" 3 0 3)"
fi
if [ "$uir_simd_raw" = "" ]; then
  if [ "$opt_level" -ge 3 ]; then
    uir_simd="1"
  else
    uir_simd="0"
  fi
else
  uir_simd="$uir_simd_raw"
fi
budget="${GENERIC_SPEC_BUDGET:-0}"
fixtures="${UIR_STABILITY_FIXTURES:-tests/cheng/backend/fixtures/return_add.cheng
tests/cheng/backend/fixtures/return_object_fields.cheng
tests/cheng/backend/fixtures/return_while_sum.cheng
tests/cheng/backend/fixtures/return_call9.cheng
tests/cheng/backend/fixtures/return_opt2_cbr_mergeable.cheng
tests/cheng/backend/fixtures/return_opt2_merge_identical_ret_blocks.cheng
tests/cheng/backend/fixtures/return_opt2_merge_equiv_blocks.cheng
tests/cheng/backend/fixtures/return_opt2_merge_jumps.cheng
tests/cheng/backend/fixtures/return_opt2_sroa_deref.cheng
tests/cheng/backend/fixtures/return_opt2_licm_while_cond.cheng}"
normalize_fixture_lines() {
  printf '%s\n' "$1" | tr ';' '\n' | tr '\r' '\n' | sed -e '/^[[:space:]]*$/d'
}

fixture_lines="$(normalize_fixture_lines "$fixtures")"
out_dir="${UIR_STABILITY_OUT_DIR:-artifacts/backend_uir_stability}"
mkdir -p "$out_dir"

if ! sha256_file /dev/null >/dev/null 2>&1; then
  echo "[verify_backend_uir_stability] shasum/sha256sum is required to run this check" 1>&2
  exit 2
fi

run_build() {
  outfile="$1"
  fixture="$2"
  mode="$3"
  frontend="${UIR_STABILITY_FRONTEND:-}"
  if [ "$frontend" != "" ]; then
    env \
      BACKEND_TARGET="$target" \
      BACKEND_FRONTEND="$frontend" \
      BACKEND_EMIT=obj \
      BACKEND_OPT_LEVEL="$opt_level" \
      UIR_OPT2_ITERS="$uir_opt2_iters" \
      UIR_OPT3_ITERS="$uir_opt3_iters" \
      UIR_OPT3_CLEANUP_ITERS="$uir_opt3_cleanup_iters" \
      UIR_INLINE_ITERS="$uir_inline_iters" \
      UIR_CFG_CANON_ITERS="$uir_cfg_canon_iters" \
      UIR_PROFILE="$uir_profile" \
      UIR_SIMD="$uir_simd" \
      UIR_SIMD_MAX_WIDTH="$uir_simd_max_width" \
      UIR_SIMD_POLICY="$uir_simd_policy" \
      GENERIC_MODE="$mode" \
      GENERIC_SPEC_BUDGET="$budget" \
      BACKEND_INPUT="$fixture" \
      BACKEND_OUTPUT="$outfile" \
      "$driver"
    return $?
  fi

  env \
    BACKEND_TARGET="$target" \
    BACKEND_EMIT=obj \
    BACKEND_OPT_LEVEL="$opt_level" \
      UIR_OPT2_ITERS="$uir_opt2_iters" \
      UIR_OPT3_ITERS="$uir_opt3_iters" \
      UIR_OPT3_CLEANUP_ITERS="$uir_opt3_cleanup_iters" \
      UIR_INLINE_ITERS="$uir_inline_iters" \
      UIR_CFG_CANON_ITERS="$uir_cfg_canon_iters" \
    UIR_PROFILE="$uir_profile" \
    UIR_SIMD="$uir_simd" \
    UIR_SIMD_MAX_WIDTH="$uir_simd_max_width" \
    UIR_SIMD_POLICY="$uir_simd_policy" \
    GENERIC_MODE="$mode" \
    GENERIC_SPEC_BUDGET="$budget" \
    BACKEND_INPUT="$fixture" \
    BACKEND_OUTPUT="$outfile" \
    "$driver"
}

for mode in $modes; do
  while IFS= read -r fixture; do
    if [ ! -f "$fixture" ]; then
      echo "[verify_backend_uir_stability] missing fixture: $fixture" 1>&2
      exit 2
    fi

    base_hash=""
    i="1"
    while [ "$i" -le "$reps" ]; do
      base_name="$(basename "$fixture" .cheng)"
      out_path="$out_dir/${base_name}.${mode}.${i}.o"
      rm -f "$out_path"
      run_build "$out_path" "$fixture" "$mode"
      if [ ! -s "$out_path" ]; then
        echo "[verify_backend_uir_stability] failed to produce object: $out_path" 1>&2
        exit 1
      fi

      h="$(sha256_file "$out_path")"
      if [ "$h" = "" ]; then
        echo "[verify_backend_uir_stability] failed to hash object output" 1>&2
        exit 2
      fi
      if [ "$i" -eq 1 ]; then
        base_hash="$h"
      elif [ "$h" != "$base_hash" ]; then
        echo "[verify_backend_uir_stability] non-deterministic output for $mode:$fixture (iter $i/$reps): $base_hash -> $h" 1>&2
        exit 1
      fi
      i=$((i + 1))
    done

    echo "verify_backend_uir_stability: ok ${mode} ${fixture}"
  done <<EOF
$fixture_lines
EOF
done

echo "verify_backend_uir_stability ok"
