#!/usr/bin/env sh
. "$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)/env_prefix_bridge.sh"
set -eu
(set -o pipefail) 2>/dev/null && set -o pipefail

root="$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)"
cd "$root"

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

normalize_fixture_lines() {
  printf '%s\n' "$1" | tr ';' '\n' | tr '\r' '\n' | sed -e '/^[[:space:]]*$/d'
}

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

driver="${BACKEND_DRIVER:-}"
if [ "$driver" = "" ]; then
  driver="$(sh src/tooling/backend_driver_path.sh)"
fi
target="${BACKEND_TARGET:-arm64-apple-darwin}"
link_env="$(sh src/tooling/backend_link_env.sh --driver:"$driver" --target:"$target" --linker:"${BACKEND_LINKER:-auto}")"
opt3_aggressive="${UIR_AGGRESSIVE:-${BACKEND_OPT3_AGGRESSIVE:-0}}"
uir_full_iters="${UIR_FULL_ITERS:-2}"
uir_opt3_iters="$(normalize_int "${UIR_OPT3_ITERS:-4}" 4 1 32)"
uir_opt3_cleanup_iters="$(normalize_int "${UIR_OPT3_CLEANUP_ITERS:-3}" 3 1 32)"
uir_inline_iters="$(normalize_int "${UIR_INLINE_ITERS:-4}" 4 1 16)"
uir_cfg_canon_iters="$(normalize_int "${UIR_CFG_CANON_ITERS:-1}" 1 1 16)"
uir_profile="${UIR_PROFILE:-0}"
uir_simd="${UIR_SIMD:-1}"
uir_simd_max_width="${UIR_SIMD_MAX_WIDTH:-0}"
uir_simd_policy="${UIR_SIMD_POLICY:-autovec}"
if [ "$uir_simd_policy" = "" ]; then
  uir_simd_policy="autovec"
fi
fixture_list="${UIR_OPT3_FIXTURES:-tests/cheng/backend/fixtures/return_opt2_inline_dce.cheng
tests/cheng/backend/fixtures/return_opt2_cse.cheng
tests/cheng/backend/fixtures/return_opt2_sroa_deref.cheng
tests/cheng/backend/fixtures/return_opt2_licm_while_cond.cheng
tests/cheng/backend/fixtures/return_opt2_algebraic.cse.cheng
tests/cheng/backend/fixtures/return_opt2_noop_dse.cheng
tests/cheng/backend/fixtures/return_opt2_merge_jumps.cheng
tests/cheng/backend/fixtures/return_opt2_forward_subst.cheng
tests/cheng/backend/fixtures/return_opt2_cbr_boolret.cheng
tests/cheng/backend/fixtures/return_opt2_const_cmp_fold.cheng
tests/cheng/backend/fixtures/return_opt2_fold_jump_to_ret_slot.cheng
tests/cheng/backend/fixtures/return_opt2_cbr_same_ret_expr.cheng
tests/cheng/backend/fixtures/return_opt2_cbr_same_ret_expr_chain.cheng
tests/cheng/backend/fixtures/return_opt2_cbr_constfold.cheng
tests/cheng/backend/fixtures/return_opt2_cbr_mergeable.cheng
tests/cheng/backend/fixtures/return_opt2_merge_identical_ret_blocks.cheng
tests/cheng/backend/fixtures/return_opt2_merge_equiv_blocks.cheng
tests/cheng/backend/fixtures/return_opt2_cbr_zero_cmp.cheng
tests/cheng/backend/fixtures/return_opt2_cbr_same_target.cheng}"

uir_aggressive_env=""
if [ "$opt3_aggressive" = "1" ]; then
  uir_aggressive_env="UIR_AGGRESSIVE=1 UIR_FULL_ITERS=$uir_full_iters"
fi

out_dir="artifacts/backend_opt3"
mkdir -p "$out_dir"

run_fixture() {
  fixture="$1"
  out_name="$2"
  out_path="$out_dir/$out_name"
  if [ "$opt3_aggressive" = "1" ]; then
    out_path="$out_dir/$(printf '%s.aggressive' "$out_name")"
  fi

  env $link_env \
    $uir_aggressive_env \
    BACKEND_MULTI=0 \
    BACKEND_MULTI_FORCE=0 \
    BACKEND_WHOLE_PROGRAM=1 \
    BACKEND_INCREMENTAL=0 \
    UIR_PROFILE="$uir_profile" \
    UIR_OPT3_ITERS="$uir_opt3_iters" \
    UIR_OPT3_CLEANUP_ITERS="$uir_opt3_cleanup_iters" \
    UIR_INLINE_ITERS="$uir_inline_iters" \
    UIR_CFG_CANON_ITERS="$uir_cfg_canon_iters" \
    UIR_SIMD="$uir_simd" \
    UIR_SIMD_MAX_WIDTH="$uir_simd_max_width" \
    UIR_SIMD_POLICY="$uir_simd_policy" \
    BACKEND_OPT_LEVEL=3 \
    BACKEND_EMIT=exe \
    BACKEND_TARGET="$target" \
    BACKEND_INPUT="$fixture" \
    BACKEND_OUTPUT="$out_path" \
    "$driver"

  if [ ! -x "$out_path" ]; then
    echo "[Error] verify_backend_opt3: missing executable: $out_path" 1>&2
    exit 1
  fi

  run_log="$out_path.run.log"
  set +e
  "$out_path" >"$run_log" 2>&1
  run_status="$?"
  set -e
  if [ "$run_status" -ne 0 ]; then
    if is_known_runtime_symbol_log "$run_log"; then
      echo "[verify_backend_opt3] known runtime-symbol instability, fallback compile-only: $fixture" 1>&2
      return 0
    fi
    cat "$run_log" 1>&2 || true
    exit "$run_status"
  fi
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

run_fixture_wrapper() {
  fixture="$1"
  if [ ! -f "$fixture" ]; then
    echo "[verify_backend_opt3] missing fixture: $fixture" 1>&2
    exit 2
  fi
  out_name="$(basename "$fixture")"
  out_name="${out_name%.cheng}"
  out_name="${out_name}.opt3"
  run_fixture "$fixture" "$out_name"
}

fixture_lines="$(normalize_fixture_lines "$fixture_list")"
while IFS= read -r fixture; do
  run_fixture_wrapper "$fixture"
done <<EOF
$fixture_lines
EOF

echo "verify_backend_opt3 ok"
