#!/usr/bin/env sh
:
set -eu
(set -o pipefail) 2>/dev/null && set -o pipefail

usage() {
  cat <<'EOF'
Usage:
  src/tooling/cheng_tooling_embedded_scripts/verify_backend_selfhost_currentsrc_proof.sh [--help]

Env:
  SELFHOST_CURRENTSRC_PROOF_SESSION=<name>   default: currentsrc.proof
  SELFHOST_CURRENTSRC_PROOF_OUT_DIR=<path>   default: artifacts/backend_selfhost_self_obj/probe_currentsrc_proof
  SELFHOST_CURRENTSRC_PROOF_STAGE0_LINEAGE_DIR=<path>
                                         default: artifacts/backend_selfhost_self_obj/probe_currentsrc_proof
  SELFHOST_CURRENTSRC_PROOF_TIMEOUT=<sec>    default: 60
  SELFHOST_CURRENTSRC_PROOF_MODE=<fast|strict> default: fast
  SELFHOST_CURRENTSRC_PROOF_REUSE=<0|1>      default: fast=1 when current-source proof stage2 exists, else 0; strict=1 when current-source proof stage2 exists
  SELFHOST_CURRENTSRC_PROOF_STAGE0=<path>    default: stable current-source proof lineage stage2 when usable else strict-fresh snapshot driver; fail if neither is usable
  SELFHOST_CURRENTSRC_PROOF_SMOKE_TIMEOUT=<sec> default: 60
  SELFHOST_CURRENTSRC_PROOF_SMOKE_RUN_TIMEOUT=<sec> default: 60
  SELFHOST_CURRENTSRC_PROOF_SKIP_DIRECT_SMOKE=<0|1> default: 0

Notes:
  - Wraps verify_backend_selfhost_bootstrap_self_obj with
    SELF_OBJ_BOOTSTRAP_DRIVER_INPUT=src/backend/tooling/backend_driver_proof.cheng
  - Publishes a stable current-source proof surface under the out dir above.
  - Strict stage0 candidate selection reads from the stable lineage dir above,
    not from the current run out dir.
  - Smoke-checks both cheng.stage2.proof and bare cheng.stage2 by directly
    compiling tests/cheng/backend/fixtures/return_i64.cheng and then running
    the produced executable.
  - In strict mode, this wrapper defaults to reusing the fresh current-source
    proof stage1/stage2 lineage, enables strict stage2 alias, and skips the
    bootstrap script's older hello_puts smoke because this wrapper already
    performs the direct return_i64 smoke itself.
EOF
}

while [ "${1:-}" != "" ]; do
  case "$1" in
    --help|-h)
      usage
      exit 0
      ;;
    *)
      echo "[verify_backend_selfhost_currentsrc_proof] unknown arg: $1" 1>&2
      usage 1>&2
      exit 2
      ;;
  esac
done

root="$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)"
cd "$root"

session="${SELFHOST_CURRENTSRC_PROOF_SESSION:-currentsrc.proof}"
out_dir="${SELFHOST_CURRENTSRC_PROOF_OUT_DIR:-artifacts/backend_selfhost_self_obj/probe_currentsrc_proof}"
stage0_lineage_dir="${SELFHOST_CURRENTSRC_PROOF_STAGE0_LINEAGE_DIR:-artifacts/backend_selfhost_self_obj/probe_currentsrc_proof}"
timeout="${SELFHOST_CURRENTSRC_PROOF_TIMEOUT:-60}"
mode="${SELFHOST_CURRENTSRC_PROOF_MODE:-fast}"
reuse="${SELFHOST_CURRENTSRC_PROOF_REUSE:-}"
stage0="${SELFHOST_CURRENTSRC_PROOF_STAGE0:-}"
smoke_timeout="${SELFHOST_CURRENTSRC_PROOF_SMOKE_TIMEOUT:-60}"
smoke_run_timeout="${SELFHOST_CURRENTSRC_PROOF_SMOKE_RUN_TIMEOUT:-60}"
skip_direct_smoke="${SELFHOST_CURRENTSRC_PROOF_SKIP_DIRECT_SMOKE:-0}"
bootstrap_strict_allow_fast_reuse="${SELF_OBJ_BOOTSTRAP_STRICT_ALLOW_FAST_REUSE:-}"
bootstrap_strict_stage2_alias="${SELF_OBJ_BOOTSTRAP_STRICT_STAGE2_ALIAS:-}"
bootstrap_skip_smoke="${SELF_OBJ_BOOTSTRAP_SKIP_SMOKE:-}"
bootstrap_sample_on_timeout="${SELF_OBJ_BOOTSTRAP_SAMPLE_ON_TIMEOUT:-}"
bootstrap_timeout_sample_secs="${SELF_OBJ_BOOTSTRAP_TIMEOUT_SAMPLE_SECS:-}"
generic_mode="${GENERIC_MODE:-}"
generic_spec_budget="${GENERIC_SPEC_BUDGET:-}"
generic_lowering="${GENERIC_LOWERING:-}"

if [ "$bootstrap_timeout_sample_secs" = "" ]; then
  bootstrap_timeout_sample_secs="1"
fi
if [ "$generic_mode" = "" ]; then
  generic_mode="dict"
fi
if [ "$generic_spec_budget" = "" ]; then
  generic_spec_budget="0"
fi
if [ "$generic_lowering" = "" ]; then
  generic_lowering="mir_dict"
fi

proof_timeout_budget_with_sample() {
  total="$1"
  sample_secs="$2"
  margin_secs="4"
  budget="$total"
  case "$budget" in
    ''|*[!0-9]*)
      printf '%s\n' "$budget"
      return 0
      ;;
  esac
  case "$sample_secs" in
    ''|*[!0-9]*)
      sample_secs="0"
      ;;
  esac
  child_detect_secs="$sample_secs"
  if [ "$child_detect_secs" -gt 5 ] 2>/dev/null; then
    child_detect_secs="5"
  fi
  reserve="$((sample_secs + sample_secs + child_detect_secs + margin_secs))"
  if [ "$budget" -gt "$reserve" ] 2>/dev/null; then
    printf '%s\n' "$((budget - reserve))"
    return 0
  fi
  if [ "$budget" -gt 1 ] 2>/dev/null; then
    printf '%s\n' "$((budget - 1))"
    return 0
  fi
  printf '%s\n' "$budget"
}

proof_timeout_budget="$(proof_timeout_budget_with_sample "$timeout" "$bootstrap_timeout_sample_secs")"
proof_smoke_timeout_budget="$(proof_timeout_budget_with_sample "$smoke_timeout" "$bootstrap_timeout_sample_secs")"
proof_smoke_run_timeout_budget="$(proof_timeout_budget_with_sample "$smoke_run_timeout" "0")"

case "$out_dir" in
  /*) abs_out_dir="$out_dir" ;;
  *) abs_out_dir="$root/$out_dir" ;;
esac

case "$stage0_lineage_dir" in
  /*) abs_stage0_lineage_dir="$stage0_lineage_dir" ;;
  *) abs_stage0_lineage_dir="$root/$stage0_lineage_dir" ;;
esac

if [ "$mode" != "strict" ] && [ "$mode" != "fast" ]; then
  echo "[verify_backend_selfhost_currentsrc_proof] invalid SELFHOST_CURRENTSRC_PROOF_MODE=$mode (expected fast|strict)" 1>&2
  exit 2
fi
if [ "$mode" = "strict" ]; then
  if [ "$generic_mode" != "dict" ]; then
    echo "[verify_backend_selfhost_currentsrc_proof] strict mode requires GENERIC_MODE=dict (got: $generic_mode)" 1>&2
    exit 2
  fi
  if [ "$generic_spec_budget" != "0" ]; then
    echo "[verify_backend_selfhost_currentsrc_proof] strict mode requires GENERIC_SPEC_BUDGET=0 (got: $generic_spec_budget)" 1>&2
    exit 2
  fi
  if [ "$generic_lowering" != "mir_dict" ]; then
    echo "[verify_backend_selfhost_currentsrc_proof] strict mode requires GENERIC_LOWERING=mir_dict (got: $generic_lowering)" 1>&2
    exit 2
  fi
fi

proof_detect_host_target() {
  host_os="$(uname -s 2>/dev/null || echo unknown)"
  host_arch="$(uname -m 2>/dev/null || echo unknown)"
  case "$host_os:$host_arch" in
    Darwin:arm64)
      printf '%s\n' "arm64-apple-darwin"
      return 0
      ;;
    Linux:aarch64|Linux:arm64)
      printf '%s\n' "aarch64-unknown-linux-gnu"
      return 0
      ;;
  esac
  printf '\n'
}

proof_bootstrap_driver_source_requires_direct_exports() {
  src="$1"
  base="$(basename -- "$src")"
  case "$base" in
    backend_driver.cheng|backend_driver_proof.cheng|backend_driver_uir_sidecar_wrapper.cheng)
      return 0
      ;;
  esac
  return 1
}

proof_strict_sidecar_bundle_path() {
  target="$1"
  stable_path="$root/chengcache/backend_driver_sidecar/backend_driver_uir_sidecar.$target.bundle"
  current_path="$stable_path.current"
  if [ -f "$stable_path" ]; then
    printf '%s\n' "$stable_path"
    return 0
  fi
  if [ -f "$current_path" ]; then
    printf '%s\n' "$current_path"
    return 0
  fi
  printf '%s\n' "$stable_path"
}

proof_strict_sidecar_compiler_path() {
  target="$1"
  printf '%s\n' "$root/chengcache/backend_driver_sidecar/backend_driver_currentsrc_sidecar_wrapper.$target.sh"
}

proof_resolve_sidecar_mode() {
  explicit="${SELF_OBJ_BOOTSTRAP_PROOF_SIDECAR_MODE:-${BACKEND_UIR_SIDECAR_MODE:-}}"
  case "$explicit" in
    cheng)
      printf '%s\n' "$explicit"
      return 0
      ;;
    "")
      ;;
    *)
      printf '\n'
      return 0
      ;;
  esac
  resolved="$(sh "$root/src/tooling/cheng_tooling_embedded_scripts/resolve_backend_sidecar_defaults.sh" --root:"$root" --field:mode 2>/dev/null || true)"
  case "$resolved" in
    cheng)
      printf '%s\n' "$resolved"
      return 0
      ;;
  esac
  target="$(proof_detect_host_target)"
  compiler="$(proof_strict_sidecar_compiler_path "$target")"
  bundle="$(proof_strict_sidecar_bundle_path "$target")"
  if [ -x "$compiler" ] && [ -f "$bundle" ]; then
    printf '%s\n' "cheng"
    return 0
  fi
  printf '\n'
}

proof_resolve_sidecar_bundle() {
  explicit="${SELF_OBJ_BOOTSTRAP_PROOF_SIDECAR_BUNDLE:-${BACKEND_UIR_SIDECAR_BUNDLE:-}}"
  if [ "$explicit" != "" ]; then
    case "$explicit" in
      /*) ;;
      *) explicit="$root/$explicit" ;;
    esac
    if [ -f "$explicit" ]; then
      printf '%s\n' "$explicit"
      return 0
    fi
    printf '\n'
    return 0
  fi
  resolved="$(sh "$root/src/tooling/cheng_tooling_embedded_scripts/resolve_backend_sidecar_defaults.sh" --root:"$root" --field:bundle 2>/dev/null || true)"
  if [ "$resolved" != "" ] && [ -f "$resolved" ]; then
    printf '%s\n' "$resolved"
    return 0
  fi
  target="$(proof_detect_host_target)"
  stable_bundle="$(proof_strict_sidecar_bundle_path "$target")"
  if [ -f "$stable_bundle" ]; then
    printf '%s\n' "$stable_bundle"
    return 0
  fi
  printf '\n'
}

proof_resolve_sidecar_compiler() {
  explicit="${SELF_OBJ_BOOTSTRAP_PROOF_SIDECAR_COMPILER:-${BACKEND_UIR_SIDECAR_COMPILER:-}}"
  if [ "$explicit" != "" ]; then
    case "$explicit" in
      /*) ;;
      *) explicit="$root/$explicit" ;;
    esac
    if [ -x "$explicit" ]; then
      printf '%s\n' "$explicit"
      return 0
    fi
    printf '\n'
      return 0
  fi
  template_compiler="$root/src/tooling/cheng_tooling_embedded_scripts/backend_driver_currentsrc_sidecar_wrapper.sh"
  if [ -x "$template_compiler" ]; then
    printf '%s\n' "$template_compiler"
    return 0
  fi
  resolved="$(sh "$root/src/tooling/cheng_tooling_embedded_scripts/resolve_backend_sidecar_defaults.sh" --root:"$root" --field:compiler 2>/dev/null || true)"
  if [ "$resolved" != "" ] && [ -x "$resolved" ]; then
    printf '%s\n' "$resolved"
    return 0
  fi
  target="$(proof_detect_host_target)"
  stable_compiler="$(proof_strict_sidecar_compiler_path "$target")"
  if [ -x "$stable_compiler" ]; then
    printf '%s\n' "$stable_compiler"
    return 0
  fi
  printf '\n'
}

proof_resolve_sidecar_child_mode() {
  explicit="${SELF_OBJ_BOOTSTRAP_PROOF_SIDECAR_CHILD_MODE:-${BACKEND_UIR_SIDECAR_CHILD_MODE:-}}"
  case "$explicit" in
    cli|outer_cli)
      printf '%s\n' "$explicit"
      return 0
      ;;
    "")
      ;;
    *)
      printf '\n'
      return 0
      ;;
  esac
  resolved="$(sh "$root/src/tooling/cheng_tooling_embedded_scripts/resolve_backend_sidecar_defaults.sh" --root:"$root" --field:child_mode 2>/dev/null || true)"
  case "$resolved" in
    cli|outer_cli)
      printf '%s\n' "$resolved"
      return 0
      ;;
  esac
  compiler="$(proof_resolve_sidecar_compiler)"
  meta_path="${compiler}.meta"
  if [ "$compiler" != "" ] && [ -f "$meta_path" ]; then
    meta_child_mode="$(sed -n 's/^sidecar_child_mode=//p' "$meta_path" | head -n 1)"
    case "$meta_child_mode" in
      cli|outer_cli)
        printf '%s\n' "$meta_child_mode"
        return 0
        ;;
    esac
  fi
  printf '%s\n' "cli"
}

proof_resolve_sidecar_outer_compiler() {
  explicit="${SELF_OBJ_BOOTSTRAP_PROOF_OUTER_COMPILER:-${BACKEND_UIR_SIDECAR_OUTER_COMPILER:-}}"
  if [ "$explicit" != "" ]; then
    case "$explicit" in
      /*) ;;
      *) explicit="$root/$explicit" ;;
    esac
    if [ -x "$explicit" ]; then
      printf '%s\n' "$explicit"
      return 0
    fi
    printf '\n'
    return 0
  fi
  resolved="$(sh "$root/src/tooling/cheng_tooling_embedded_scripts/resolve_backend_sidecar_defaults.sh" --root:"$root" --field:outer_companion 2>/dev/null || true)"
  if [ "$resolved" != "" ] && [ -x "$resolved" ]; then
    printf '%s\n' "$resolved"
    return 0
  fi
  printf '\n'
}

proof_materialize_stage0_wrapper() {
  template_compiler="$1"
  real_driver="$2"
  out_compiler="$3"
  out_meta="${out_compiler}.meta"
  tmp_compiler="${out_compiler}.tmp.$$"
  tmp_meta="${out_meta}.tmp.$$"
  [ -x "$template_compiler" ] || return 1
  [ -x "$real_driver" ] || return 1
  mkdir -p "$(dirname -- "$out_compiler")"
  cp "$template_compiler" "$tmp_compiler"
  chmod +x "$tmp_compiler"
  {
    echo "sidecar_contract_version=1"
    echo "sidecar_child_mode=cli"
    echo "sidecar_outer_companion="
    echo "sidecar_real_driver=$real_driver"
  } > "$tmp_meta"
  mv "$tmp_compiler" "$out_compiler"
  mv "$tmp_meta" "$out_meta"
  return 0
}

proof_stage0_real_driver_path() {
  cand="$1"
  case "$cand" in
    *.sh)
      meta_path="${cand}.meta"
      if [ -f "$meta_path" ]; then
        real_driver="$(sed -n 's/^sidecar_real_driver=//p' "$meta_path" | head -n 1)"
        if [ "$real_driver" != "" ] && [ -x "$real_driver" ]; then
          printf '%s\n' "$real_driver"
          return 0
        fi
      fi
      printf '\n'
      return 0
      ;;
  esac
  if [ "$cand" != "" ] && [ -x "$cand" ]; then
    printf '%s\n' "$cand"
    return 0
  fi
  printf '\n'
}

proof_meta_field() {
  meta_path="$1"
  key="$2"
  if [ ! -f "$meta_path" ]; then
    printf '\n'
    return 0
  fi
  sed -n "s/^${key}=//p" "$meta_path" | head -n 1
}

proof_stamp_field() {
  stamp_path="$1"
  key="$2"
  if [ ! -f "$stamp_path" ]; then
    printf '\n'
    return 0
  fi
  awk -F= -v key="$key" '$1 == key { print substr($0, index($0, "=") + 1); exit }' "$stamp_path"
}

proof_internal_ownership_fixed_0_field() {
  stamp_path="$1"
  suffix="$2"
  value="$(proof_stamp_field "$stamp_path" "stage1_ownership_fixed_0_${suffix}")"
  if [ "$value" = "" ]; then
    value="$(proof_stamp_field "$stamp_path" "stage1_skip_ownership_${suffix}")"
  fi
  printf '%s\n' "$value"
}

proof_write_published_compile_stamp() {
  src_stamp="$1"
  dst_stamp="$2"
  [ -s "$src_stamp" ] || return 1
  awk '
    /^stage1_skip_ownership_raw=/ {
      sub(/^stage1_skip_ownership_raw=/, "stage1_ownership_fixed_0_raw=")
      print
      next
    }
    /^stage1_skip_ownership_effective=/ {
      sub(/^stage1_skip_ownership_effective=/, "stage1_ownership_fixed_0_effective=")
      print
      next
    }
    /^stage1_skip_ownership_default=/ {
      sub(/^stage1_skip_ownership_default=/, "stage1_ownership_fixed_0_default=")
      print
      next
    }
    { print }
  ' "$src_stamp" > "${dst_stamp}.tmp.$$"
  mv "${dst_stamp}.tmp.$$" "$dst_stamp"
}

proof_expected_generic_mode() {
  printf '%s\n' "dict"
}

proof_expected_generic_lowering() {
  printf '%s\n' "${GENERIC_LOWERING:-mir_dict}"
}

proof_bootstrap_stage0_surface() {
  cand="$1"
  real_driver="$(proof_stage0_real_driver_path "$cand")"
  case "$real_driver" in
    "$abs_stage0_lineage_dir"/cheng_stage0_currentsrc.proof)
      return 0
      ;;
  esac
  return 1
}

proof_stage0_sources_newer_than() {
  out="$1"
  [ -e "$out" ] || return 0
  search_dirs=""
  for d in src/backend src/stage1 src/std src/core src/system; do
    if [ -d "$d" ]; then
      search_dirs="$search_dirs $d"
    fi
  done
  if [ "$search_dirs" = "" ]; then
    return 1
  fi
  # shellcheck disable=SC2086
  find $search_dirs -type f \( -name '*.cheng' -o -name '*.c' -o -name '*.h' \) \
    -newer "$out" -print -quit | grep -q .
}

proof_report_field() {
  report_path="$1"
  key="$2"
  if [ ! -f "$report_path" ]; then
    printf '\n'
    return 0
  fi
  awk -F= -v key="$key" '$1 == key { print substr($0, index($0, "=") + 1); exit }' "$report_path"
}

proof_existing_stage1_stamp_path() {
  for cand in \
    "$abs_stage0_lineage_dir/stage1.native.after.compile_stamp.txt" \
    "$abs_stage0_lineage_dir/stage1.native.compile_stamp.txt" \
    "$abs_stage0_lineage_dir/stage1.native.serial.compile_stamp.txt"
  do
    if [ -s "$cand" ]; then
      printf '%s\n' "$cand"
      return 0
    fi
  done
  printf '\n'
}

proof_write_stage0_meta() {
  label="$1"
  outer_driver="$2"
  meta_path="$3"
  stamp_path="$4"
  tmp_meta="${meta_path}.tmp.$$"
  {
    echo "meta_contract_version=2"
    echo "label=$label"
    echo "outer_driver=$outer_driver"
    echo "driver_input=$(proof_stamp_field "$stamp_path" "input")"
    echo "fixture=$root/tests/cheng/backend/fixtures/return_i64.cheng"
    echo "stage1_ownership_fixed_0_effective=$(proof_internal_ownership_fixed_0_field "$stamp_path" "effective")"
    echo "stage1_ownership_fixed_0_default=$(proof_internal_ownership_fixed_0_field "$stamp_path" "default")"
    echo "uir_phase_contract_version=$(proof_stamp_field "$stamp_path" "uir_phase_contract_version")"
    echo "generic_lowering=$(proof_stamp_field "$stamp_path" "generic_lowering")"
    echo "generic_mode=$(proof_stamp_field "$stamp_path" "generic_mode")"
  } >"$tmp_meta"
  mv "$tmp_meta" "$meta_path"
}

proof_publish_existing_strict_alias_stage0_surface() {
  stage1_path="$abs_stage0_lineage_dir/cheng.stage1"
  stage2_path="$abs_stage0_lineage_dir/cheng.stage2"
  stage1_meta="$abs_stage0_lineage_dir/cheng.stage1.meta"
  stage2_meta="$abs_stage0_lineage_dir/cheng.stage2.meta"
  stage1_pub_stamp="$abs_stage0_lineage_dir/cheng.stage1.compile_stamp.txt"
  stage2_pub_stamp="$abs_stage0_lineage_dir/cheng.stage2.compile_stamp.txt"
  stage1_stamp="$(proof_existing_stage1_stamp_path)"
  stage2_smoke_report="$abs_stage0_lineage_dir/cheng.stage2.smoke.report.txt"
  [ -x "$stage1_path" ] || return 0
  [ -x "$stage2_path" ] || return 0
  [ "$stage1_stamp" != "" ] || return 0
  [ -f "$stage2_smoke_report" ] || return 0
  cmp -s "$stage1_path" "$stage2_path" || return 0
  [ "$(proof_stamp_field "$stage1_stamp" "input")" = "src/backend/tooling/backend_driver_proof.cheng" ] || return 0
  [ "$(proof_stamp_field "$stage1_stamp" "frontend")" = "stage1" ] || return 0
  [ "$(proof_stamp_field "$stage1_stamp" "whole_program")" = "1" ] || return 0
  [ "$(proof_internal_ownership_fixed_0_field "$stage1_stamp" "effective")" = "0" ] || return 0
  [ "$(proof_internal_ownership_fixed_0_field "$stage1_stamp" "default")" = "0" ] || return 0
  [ "$(proof_stamp_field "$stage1_stamp" "generic_mode")" = "$(proof_expected_generic_mode)" ] || return 0
  [ "$(proof_stamp_field "$stage1_stamp" "generic_lowering")" = "$(proof_expected_generic_lowering)" ] || return 0
  [ "$(proof_report_field "$stage2_smoke_report" "compiler_bin")" = "$stage2_path" ] || return 0
  [ "$(proof_report_field "$stage2_smoke_report" "compile_rc")" = "0" ] || return 0
  [ "$(proof_report_field "$stage2_smoke_report" "run_rc")" = "0" ] || return 0
  [ "$(proof_report_field "$stage2_smoke_report" "fallback_used")" = "0" ] || return 0
  if proof_stage0_sources_newer_than "$stage1_path"; then
    return 0
  fi
  if proof_stage0_sources_newer_than "$stage2_path"; then
    return 0
  fi
  proof_write_published_compile_stamp "$stage1_stamp" "$stage1_pub_stamp"
  proof_write_published_compile_stamp "$stage1_stamp" "$stage2_pub_stamp"
  proof_write_stage0_meta "stage1" "$stage1_path" "$stage1_meta" "$stage1_pub_stamp"
  proof_write_stage0_meta "stage2" "$stage2_path" "$stage2_meta" "$stage2_pub_stamp"
}

proof_existing_bootstrap_stage0_stamp_path() {
  for cand in \
    "$abs_stage0_lineage_dir/stage1.native.after.compile_stamp.txt" \
    "$abs_stage0_lineage_dir/stage1.native.compile_stamp.txt" \
    "$abs_stage0_lineage_dir/stage1.native.serial.compile_stamp.txt"
  do
    if [ -s "$cand" ]; then
      printf '%s\n' "$cand"
      return 0
    fi
  done
  printf '\n'
}

proof_publish_existing_bootstrap_stage0_surface() {
  stage0_path="$abs_stage0_lineage_dir/cheng_stage0_currentsrc.proof"
  stage0_meta="${stage0_path}.meta"
  stage0_stamp="$(proof_existing_bootstrap_stage0_stamp_path)"
  [ -x "$stage0_path" ] || return 0
  [ "$stage0_stamp" != "" ] || return 0
  [ "$(proof_stamp_field "$stage0_stamp" "input")" = "src/backend/tooling/backend_driver_proof.cheng" ] || return 0
  [ "$(proof_stamp_field "$stage0_stamp" "frontend")" = "stage1" ] || return 0
  [ "$(proof_stamp_field "$stage0_stamp" "whole_program")" = "1" ] || return 0
  [ "$(proof_internal_ownership_fixed_0_field "$stage0_stamp" "effective")" = "0" ] || return 0
  [ "$(proof_internal_ownership_fixed_0_field "$stage0_stamp" "default")" = "0" ] || return 0
  [ "$(proof_stamp_field "$stage0_stamp" "generic_mode")" = "$(proof_expected_generic_mode)" ] || return 0
  [ "$(proof_stamp_field "$stage0_stamp" "generic_lowering")" = "$(proof_expected_generic_lowering)" ] || return 0
  proof_write_stage0_meta "currentsrc.proof.bootstrap" "$stage0_path" "$stage0_meta" "$stage0_stamp"
}

proof_published_stage0_surface() {
  cand="$1"
  real_driver="$(proof_stage0_real_driver_path "$cand")"
  case "$real_driver" in
    "$abs_stage0_lineage_dir"/cheng.stage1|\
    "$abs_stage0_lineage_dir"/cheng.stage2|\
    "$abs_stage0_lineage_dir"/cheng.stage2.proof)
      return 0
      ;;
  esac
  return 1
}

proof_stage0_meta_path_for_real_driver() {
  real_driver="$1"
  case "$real_driver" in
    */cheng.stage1|*/cheng.stage2|*/cheng.stage2.proof|*/cheng_stage0_currentsrc.proof)
      printf '%s.meta\n' "$real_driver"
      return 0
      ;;
  esac
  printf '\n'
}

proof_stage0_expected_label_for_real_driver() {
  real_driver="$1"
  case "$real_driver" in
    */cheng_stage0_currentsrc.proof)
      printf '%s\n' "currentsrc.proof.bootstrap"
      return 0
      ;;
    */cheng.stage1)
      printf '%s\n' "stage1"
      return 0
      ;;
    */cheng.stage2)
      printf '%s\n' "stage2"
      return 0
      ;;
    */cheng.stage2.proof)
      printf '%s\n' "stage2.proof"
      return 0
      ;;
  esac
  printf '\n'
}

proof_stage0_direct_export_driver_path() {
  cand="$1"
  real_driver="$(proof_stage0_real_driver_path "$cand")"
  case "$real_driver" in
    *.proof)
      outer_driver="$(proof_meta_field "${real_driver}.meta" "outer_driver")"
      if [ "$outer_driver" != "" ]; then
        printf '%s\n' "$outer_driver"
        return 0
      fi
      ;;
  esac
  printf '%s\n' "$real_driver"
}

proof_stage0_direct_export_surface_ok() {
  cand="$1"
  export_driver="$(proof_stage0_direct_export_driver_path "$cand")"
  [ "$export_driver" != "" ] || return 1
  [ -x "$export_driver" ] || return 1
  command -v nm >/dev/null 2>&1 || return 1
  surface_log="$(mktemp "${TMPDIR:-/tmp}/proof_stage0_nm.XXXXXX")"
  set +e
  (nm -gU "$export_driver" 2>/dev/null || nm "$export_driver" 2>/dev/null) >"$surface_log"
  nm_status="$?"
  set -e
  if [ "$nm_status" -ne 0 ]; then
    rm -f "$surface_log"
    return 1
  fi
  if ! grep -q 'driver_export_build_emit_obj_from_file_stage1_target_impl' "$surface_log"; then
    rm -f "$surface_log"
    return 1
  fi
  if ! grep -q 'driver_export_prefer_sidecar_builds' "$surface_log"; then
    rm -f "$surface_log"
    return 1
  fi
  rm -f "$surface_log"
  return 0
}

proof_stage0_surface_meta_ok() {
  cand="$1"
  real_driver="$(proof_stage0_real_driver_path "$cand")"
  [ "$real_driver" != "" ] || return 1
  meta_path="$(proof_stage0_meta_path_for_real_driver "$real_driver")"
  expected_label="$(proof_stage0_expected_label_for_real_driver "$real_driver")"
  [ "$meta_path" != "" ] || return 1
  [ "$expected_label" != "" ] || return 1
  [ -f "$meta_path" ] || return 1
  [ "$(proof_meta_field "$meta_path" "meta_contract_version")" = "2" ] || return 1
  [ "$(proof_meta_field "$meta_path" "label")" = "$expected_label" ] || return 1
  meta_outer_driver="$(proof_meta_field "$meta_path" "outer_driver")"
  case "$expected_label" in
    stage2.proof)
      [ "$meta_outer_driver" != "" ] || return 1
      [ -x "$meta_outer_driver" ] || return 1
      [ "$(proof_meta_field "$meta_path" "sidecar_mode")" = "cheng" ] || return 1
      [ "$(proof_meta_field "$meta_path" "sidecar_bundle")" != "" ] || return 1
      [ "$(proof_meta_field "$meta_path" "sidecar_compiler")" != "" ] || return 1
      ;;
    *)
      [ "$meta_outer_driver" = "$real_driver" ] || return 1
      ;;
  esac
  [ "$(proof_meta_field "$meta_path" "driver_input")" = "src/backend/tooling/backend_driver_proof.cheng" ] || return 1
  [ "$(proof_meta_field "$meta_path" "stage1_ownership_fixed_0_effective")" = "0" ] || return 1
  [ "$(proof_meta_field "$meta_path" "stage1_ownership_fixed_0_default")" = "0" ] || return 1
  [ "$(proof_meta_field "$meta_path" "generic_mode")" = "$(proof_expected_generic_mode)" ] || return 1
  [ "$(proof_meta_field "$meta_path" "generic_lowering")" = "$(proof_expected_generic_lowering)" ] || return 1
  return 0
}

proof_published_stage0_surface_meta_ok() {
  cand="$1"
  proof_published_stage0_surface "$cand" || return 1
  proof_stage0_surface_meta_ok "$cand" || return 1
  proof_stage0_direct_export_surface_ok "$cand" || return 1
  return 0
}

proof_bootstrap_stage0_surface_meta_ok() {
  cand="$1"
  proof_bootstrap_stage0_surface "$cand" || return 1
  real_driver="$(proof_stage0_real_driver_path "$cand")"
  [ "$real_driver" != "" ] || return 1
  meta_path="$(proof_stage0_meta_path_for_real_driver "$real_driver")"
  [ "$meta_path" != "" ] || return 1
  [ -f "$meta_path" ] || return 1
  [ "$(proof_meta_field "$meta_path" "meta_contract_version")" = "2" ] || return 1
  [ "$(proof_meta_field "$meta_path" "label")" = "currentsrc.proof.bootstrap" ] || return 1
  [ "$(proof_meta_field "$meta_path" "outer_driver")" = "$real_driver" ] || return 1
  [ "$(proof_meta_field "$meta_path" "stage1_ownership_fixed_0_effective")" = "0" ] || return 1
  [ "$(proof_meta_field "$meta_path" "stage1_ownership_fixed_0_default")" = "0" ] || return 1
  [ "$(proof_meta_field "$meta_path" "generic_mode")" = "$(proof_expected_generic_mode)" ] || return 1
  [ "$(proof_meta_field "$meta_path" "generic_lowering")" = "$(proof_expected_generic_lowering)" ] || return 1
  return 0
}

proof_stage0_surface_current_enough() {
  cand="$1"
  real_driver="$(proof_stage0_real_driver_path "$cand")"
  [ "$real_driver" != "" ] || return 1
  [ -x "$real_driver" ] || return 1
  if proof_published_stage0_surface "$cand"; then
    if proof_stage0_sources_newer_than "$real_driver"; then
      return 1
    fi
  fi
  return 0
}

proof_trusted_stage0_surface() {
  cand="$1"
  if proof_published_stage0_surface "$cand"; then
    proof_published_stage0_surface_meta_ok "$cand" || return 1
    proof_stage0_surface_current_enough "$cand"
    return $?
  fi
  if proof_bootstrap_stage0_surface "$cand"; then
    proof_bootstrap_stage0_surface_meta_ok "$cand"
    return $?
  fi
  return 1
}

run_with_timeout_status() {
  timeout_s="$1"
  shift
  perl -e '
    use POSIX qw(setsid WNOHANG);
    my $timeout = shift;
    my $pid = fork();
    if (!defined $pid) { exit 127; }
    if ($pid == 0) {
      setsid();
      exec @ARGV;
      exit 127;
    }
    my $end = time + $timeout;
    while (1) {
      my $r = waitpid($pid, WNOHANG);
      if ($r == $pid) {
        my $status = $?;
        if (($status & 127) != 0) {
          exit(128 + ($status & 127));
        }
        exit($status >> 8);
      }
      if (time >= $end) {
        kill "TERM", -$pid;
        kill "TERM", $pid;
        select(undef, undef, undef, 1.0);
        kill "KILL", -$pid;
        kill "KILL", $pid;
        waitpid($pid, 0);
        exit 124;
      }
      select(undef, undef, undef, 0.1);
    }
  ' "$timeout_s" "$@"
}

proof_stage0_compile_probe_ok() {
  cand="$1"
  [ "$cand" != "" ] || return 1
  [ -x "$cand" ] || return 1
  if proof_bootstrap_stage0_surface "$cand"; then
    return 1
  fi
  if proof_published_stage0_surface "$cand" && ! proof_stage0_surface_meta_ok "$cand"; then
    return 1
  fi
  if ! proof_stage0_surface_current_enough "$cand"; then
    return 1
  fi
  host_target="$(proof_detect_host_target)"
  [ "$host_target" != "" ] || return 1
  probe_src="$root/src/backend/tooling/backend_driver_proof.cheng"
  [ -f "$probe_src" ] || return 1
  strict_sidecar_mode="$(proof_resolve_sidecar_mode)"
  strict_sidecar_bundle="$(proof_resolve_sidecar_bundle)"
  strict_sidecar_compiler="$(proof_resolve_sidecar_compiler)"
  strict_sidecar_child_mode="$(proof_resolve_sidecar_child_mode)"
  strict_sidecar_outer_compiler="$(proof_resolve_sidecar_outer_compiler)"
  [ "$strict_sidecar_mode" = "cheng" ] || return 1
  [ -f "$strict_sidecar_bundle" ] || return 1
  [ -x "$strict_sidecar_compiler" ] || return 1
  case "$strict_sidecar_child_mode" in
    cli|outer_cli) ;;
    *) return 1 ;;
  esac
  if [ "$strict_sidecar_child_mode" = "outer_cli" ] && [ ! -x "$strict_sidecar_outer_compiler" ]; then
    return 1
  fi
  probe_tmp_base="$(mktemp "${TMPDIR:-/tmp}/proof_stage0_probe.XXXXXX")"
  probe_log="${probe_tmp_base}.log"
  probe_out="${probe_tmp_base}.o"
  rm -f "$probe_tmp_base"
  probe_real_driver_override="$cand"
  probe_wrapper_cli="0"
  case "$cand" in
    *.sh)
      probe_real_driver_override=""
      probe_wrapper_cli="1"
      ;;
  esac
  rm -f "$probe_out"
  set +e
  if proof_bootstrap_driver_source_requires_direct_exports "$probe_src"; then
    if [ "$probe_wrapper_cli" = "1" ]; then
      run_with_timeout_status "$timeout" \
        env \
          BACKEND_STAGE1_PARSE_MODE=outline \
          BACKEND_UIR_SIDECAR_DISABLE=1 \
          BACKEND_UIR_PREFER_SIDECAR=0 \
          BACKEND_UIR_FORCE_SIDECAR=0 \
          BACKEND_VALIDATE=0 \
          "$cand" "$probe_src" \
          --frontend:stage1 \
          --emit:obj \
          --target:"$host_target" \
          --linker:system \
          --allow-no-main \
          --whole-program \
          --no-multi \
          --no-multi-force \
          --no-incremental \
          --jobs:8 \
          --fn-jobs:8 \
          --opt-level:0 \
          --no-opt \
          --no-opt2 \
          --generic-mode:dict \
          --generic-spec-budget:0 \
          --generic-lowering:mir_dict \
          --output:"$probe_out" >"$probe_log" 2>&1
    else
      run_with_timeout_status "$timeout" \
        env \
          ${probe_real_driver_override:+TOOLING_BUILD_GLOBAL_CURRENTSOURCE_REAL_DRIVER="$probe_real_driver_override"} \
          BACKEND_STAGE1_PARSE_MODE=outline \
          BACKEND_UIR_SIDECAR_DISABLE=1 \
          BACKEND_UIR_PREFER_SIDECAR=0 \
          BACKEND_UIR_FORCE_SIDECAR=0 \
          BACKEND_INTERNAL_ALLOW_EMIT_OBJ=1 \
          BACKEND_VALIDATE=0 \
          "$cand" "$probe_src" \
          --emit:obj \
          --target:"$host_target" \
          --frontend:stage1 \
          --linker:system \
          --allow-no-main \
          --whole-program \
          --no-multi \
          --no-multi-force \
          --no-incremental \
          --jobs:8 \
          --fn-jobs:8 \
          --opt-level:0 \
          --no-opt \
          --no-opt2 \
          --generic-mode:dict \
          --generic-spec-budget:0 \
          --generic-lowering:mir_dict \
          --output:"$probe_out" >"$probe_log" 2>&1
    fi
  elif [ "$probe_wrapper_cli" = "1" ] && [ "$strict_sidecar_child_mode" = "outer_cli" ]; then
    run_with_timeout_status "$timeout" \
      env \
        BACKEND_STAGE1_PARSE_MODE=outline \
        BACKEND_CURRENTSRC_WRAPPER_PRESERVE_SIDECAR=1 \
        BACKEND_VALIDATE=0 \
        "$cand" "$probe_src" \
        --frontend:stage1 \
        --emit:obj \
        --target:"$host_target" \
        --linker:system \
        --allow-no-main \
        --whole-program \
        --no-multi \
        --no-multi-force \
        --no-incremental \
        --jobs:8 \
        --fn-jobs:8 \
        --opt-level:0 \
        --no-opt \
        --no-opt2 \
        --generic-mode:dict \
        --generic-spec-budget:0 \
        --generic-lowering:mir_dict \
        --sidecar-mode:"$strict_sidecar_mode" \
        --sidecar-bundle:"$strict_sidecar_bundle" \
        --sidecar-compiler:"$strict_sidecar_compiler" \
        --sidecar-child-mode:"$strict_sidecar_child_mode" \
        --sidecar-outer-compiler:"$strict_sidecar_outer_compiler" \
        --output:"$probe_out" >"$probe_log" 2>&1
  elif [ "$probe_wrapper_cli" = "1" ]; then
    run_with_timeout_status "$timeout" \
      env \
        BACKEND_STAGE1_PARSE_MODE=outline \
        BACKEND_CURRENTSRC_WRAPPER_PRESERVE_SIDECAR=1 \
        BACKEND_VALIDATE=0 \
        "$cand" "$probe_src" \
        --frontend:stage1 \
        --emit:obj \
        --target:"$host_target" \
        --linker:system \
        --allow-no-main \
        --whole-program \
        --no-multi \
        --no-multi-force \
        --no-incremental \
        --jobs:8 \
        --fn-jobs:8 \
        --opt-level:0 \
        --no-opt \
        --no-opt2 \
        --generic-mode:dict \
        --generic-spec-budget:0 \
        --generic-lowering:mir_dict \
        --sidecar-mode:"$strict_sidecar_mode" \
        --sidecar-bundle:"$strict_sidecar_bundle" \
        --sidecar-compiler:"$strict_sidecar_compiler" \
        --sidecar-child-mode:"$strict_sidecar_child_mode" \
        --output:"$probe_out" >"$probe_log" 2>&1
  elif [ "$strict_sidecar_child_mode" = "outer_cli" ]; then
    run_with_timeout_status "$timeout" \
      env \
        ${probe_real_driver_override:+TOOLING_BUILD_GLOBAL_CURRENTSOURCE_REAL_DRIVER="$probe_real_driver_override"} \
        BACKEND_STAGE1_PARSE_MODE=outline \
        BACKEND_UIR_SIDECAR_MODE="$strict_sidecar_mode" \
        BACKEND_UIR_SIDECAR_BUNDLE="$strict_sidecar_bundle" \
        BACKEND_UIR_SIDECAR_COMPILER="$strict_sidecar_compiler" \
        BACKEND_UIR_SIDECAR_CHILD_MODE="$strict_sidecar_child_mode" \
        BACKEND_UIR_SIDECAR_OUTER_COMPILER="$strict_sidecar_outer_compiler" \
        BACKEND_INTERNAL_ALLOW_EMIT_OBJ=1 \
        BACKEND_VALIDATE=0 \
        "$cand" "$probe_src" \
        --emit:obj \
        --target:"$host_target" \
        --frontend:stage1 \
        --linker:system \
        --allow-no-main \
        --whole-program \
        --no-multi \
        --no-multi-force \
        --no-incremental \
        --jobs:8 \
        --fn-jobs:8 \
        --opt-level:0 \
        --no-opt \
        --no-opt2 \
        --generic-mode:dict \
        --generic-spec-budget:0 \
        --generic-lowering:mir_dict \
        --output:"$probe_out" >"$probe_log" 2>&1
  else
    run_with_timeout_status "$timeout" \
      env \
        ${probe_real_driver_override:+TOOLING_BUILD_GLOBAL_CURRENTSOURCE_REAL_DRIVER="$probe_real_driver_override"} \
        BACKEND_STAGE1_PARSE_MODE=outline \
        BACKEND_UIR_SIDECAR_MODE="$strict_sidecar_mode" \
        BACKEND_UIR_SIDECAR_BUNDLE="$strict_sidecar_bundle" \
        BACKEND_UIR_SIDECAR_COMPILER="$strict_sidecar_compiler" \
        BACKEND_UIR_SIDECAR_CHILD_MODE="$strict_sidecar_child_mode" \
        BACKEND_INTERNAL_ALLOW_EMIT_OBJ=1 \
        BACKEND_VALIDATE=0 \
        "$cand" "$probe_src" \
        --emit:obj \
        --target:"$host_target" \
        --frontend:stage1 \
        --linker:system \
        --allow-no-main \
        --whole-program \
        --no-multi \
        --no-multi-force \
        --no-incremental \
        --jobs:8 \
        --fn-jobs:8 \
        --opt-level:0 \
        --no-opt \
        --no-opt2 \
        --generic-mode:dict \
        --generic-spec-budget:0 \
        --generic-lowering:mir_dict \
        --output:"$probe_out" >"$probe_log" 2>&1
  fi
  probe_rc="$?"
  set -e
  rm -f "$probe_log" "$probe_out" 2>/dev/null || true
  [ "$probe_rc" -eq 0 ]
}

proof_currentsrc_stage2="$abs_stage0_lineage_dir/cheng.stage2"
proof_currentsrc_stage1="$abs_stage0_lineage_dir/cheng.stage1"
proof_currentsrc_bootstrap_stage0="$abs_stage0_lineage_dir/cheng_stage0_currentsrc.proof"
proof_publish_existing_strict_alias_stage0_surface
proof_publish_existing_bootstrap_stage0_surface
resolved_proof_sidecar_mode="$(proof_resolve_sidecar_mode)"
resolved_proof_sidecar_bundle="$(proof_resolve_sidecar_bundle)"
resolved_proof_sidecar_compiler="$(proof_resolve_sidecar_compiler)"
resolved_proof_sidecar_child_mode="$(proof_resolve_sidecar_child_mode)"
resolved_proof_sidecar_outer_compiler="$(proof_resolve_sidecar_outer_compiler)"
proof_currentsrc_stage2_usable="0"
if [ -x "$proof_currentsrc_stage2" ] && \
   (proof_trusted_stage0_surface "$proof_currentsrc_stage2" || proof_stage0_compile_probe_ok "$proof_currentsrc_stage2"); then
  proof_currentsrc_stage2_usable="1"
fi
proof_currentsrc_stage1_usable="0"
if [ -x "$proof_currentsrc_stage1" ] && \
   (proof_trusted_stage0_surface "$proof_currentsrc_stage1" || proof_stage0_compile_probe_ok "$proof_currentsrc_stage1"); then
  proof_currentsrc_stage1_usable="1"
fi
proof_currentsrc_bootstrap_stage0_usable="0"
if [ -x "$proof_currentsrc_bootstrap_stage0" ] && \
   proof_trusted_stage0_surface "$proof_currentsrc_bootstrap_stage0"; then
  proof_currentsrc_bootstrap_stage0_usable="1"
fi

if [ "$mode" = "strict" ]; then
  if [ "$reuse" = "" ]; then
    if [ "$proof_currentsrc_stage2_usable" = "1" ]; then
      reuse="1"
    else
      reuse="0"
    fi
  fi
  if [ "$stage0" = "" ]; then
    if [ "$proof_currentsrc_stage2_usable" = "1" ]; then
      stage0="$proof_currentsrc_stage2"
    elif [ "$proof_currentsrc_stage1_usable" = "1" ]; then
      stage0="$proof_currentsrc_stage1"
    elif [ "$proof_currentsrc_bootstrap_stage0_usable" = "1" ]; then
      stage0="$proof_currentsrc_bootstrap_stage0"
    else
      if [ -x "$proof_currentsrc_stage2" ] && ! proof_stage0_surface_current_enough "$proof_currentsrc_stage2"; then
        echo "[verify_backend_selfhost_currentsrc_proof] stable current-source proof stage2 is stale against current sources: $proof_currentsrc_stage2" 1>&2
      fi
      if [ -x "$proof_currentsrc_stage1" ] && ! proof_stage0_surface_current_enough "$proof_currentsrc_stage1"; then
        echo "[verify_backend_selfhost_currentsrc_proof] stable current-source proof stage1 is stale against current sources: $proof_currentsrc_stage1" 1>&2
      fi
      echo "[verify_backend_selfhost_currentsrc_proof] missing usable strict stage0 driver (stable_stage2=$proof_currentsrc_stage2 stable_stage1=$proof_currentsrc_stage1 bootstrap=$proof_currentsrc_bootstrap_stage0)" 1>&2
      exit 1
    fi
  fi
  if [ "$bootstrap_strict_allow_fast_reuse" = "" ]; then
    if [ "$reuse" = "1" ]; then
      bootstrap_strict_allow_fast_reuse="1"
    else
      bootstrap_strict_allow_fast_reuse="0"
    fi
  fi
  if [ "$bootstrap_strict_stage2_alias" = "" ]; then
    bootstrap_strict_stage2_alias="1"
  fi
  if [ "$bootstrap_skip_smoke" = "" ]; then
    bootstrap_skip_smoke="1"
  fi
else
  if [ "$reuse" = "" ]; then
    if [ "$proof_currentsrc_stage2_usable" = "1" ]; then
      reuse="1"
    else
      reuse="0"
    fi
  fi
  if [ "$stage0" = "" ]; then
    if [ "$proof_currentsrc_stage2_usable" = "1" ]; then
      stage0="$proof_currentsrc_stage2"
    elif [ "$proof_currentsrc_stage1_usable" = "1" ]; then
      stage0="$proof_currentsrc_stage1"
    elif [ "$proof_currentsrc_bootstrap_stage0_usable" = "1" ]; then
      stage0="$proof_currentsrc_bootstrap_stage0"
    else
      echo "[verify_backend_selfhost_currentsrc_proof] missing usable strict stage0 driver (stable_stage2=$proof_currentsrc_stage2 stable_stage1=$proof_currentsrc_stage1 bootstrap=$proof_currentsrc_bootstrap_stage0)" 1>&2
      exit 1
    fi
  fi
fi

resolved_stage0_real_driver="$(proof_stage0_real_driver_path "$stage0")"

if [ "$bootstrap_sample_on_timeout" = "" ]; then
  bootstrap_sample_on_timeout="1"
fi

env \
  SELF_OBJ_BOOTSTRAP_MODE="$mode" \
  SELF_OBJ_BOOTSTRAP_REUSE="$reuse" \
  SELF_OBJ_BOOTSTRAP_TIMEOUT="$proof_timeout_budget" \
  SELF_OBJ_BOOTSTRAP_SMOKE_TIMEOUT="$proof_smoke_timeout_budget" \
  SELF_OBJ_BOOTSTRAP_STAGE0_PROBE_TIMEOUT="$proof_timeout_budget" \
  SELF_OBJ_BOOTSTRAP_STAGE1_PROBE_TIMEOUT="$proof_timeout_budget" \
  SELF_OBJ_BOOTSTRAP_MULTI_PROBE_TIMEOUT="$proof_timeout_budget" \
  SELF_OBJ_BOOTSTRAP_ALLOW_RETRY=0 \
  SELF_OBJ_BOOTSTRAP_MULTI_PROBE=0 \
  SELF_OBJ_BOOTSTRAP_ALLOW_SYSTEM_LINK_FALLBACK=0 \
  SELF_OBJ_BOOTSTRAP_CSTRING_LINK_RETRY=0 \
  SELF_OBJ_BOOTSTRAP_SAMPLE_ON_TIMEOUT="$bootstrap_sample_on_timeout" \
  SELF_OBJ_BOOTSTRAP_TIMEOUT_SAMPLE_SECS="$bootstrap_timeout_sample_secs" \
  SELF_OBJ_BOOTSTRAP_SESSION="$session" \
  SELF_OBJ_BOOTSTRAP_OUT_DIR="$out_dir" \
  SELF_OBJ_BOOTSTRAP_CURRENTSRC_LINEAGE_DIR="$stage0_lineage_dir" \
  SELF_OBJ_BOOTSTRAP_STAGE0="$stage0" \
  SELF_OBJ_BOOTSTRAP_DRIVER_INPUT="src/backend/tooling/backend_driver_proof.cheng" \
  SELF_OBJ_BOOTSTRAP_PROOF_SIDECAR_MODE="$resolved_proof_sidecar_mode" \
  SELF_OBJ_BOOTSTRAP_PROOF_SIDECAR_BUNDLE="$resolved_proof_sidecar_bundle" \
  SELF_OBJ_BOOTSTRAP_PROOF_SIDECAR_COMPILER="$resolved_proof_sidecar_compiler" \
  SELF_OBJ_BOOTSTRAP_PROOF_SIDECAR_CHILD_MODE="$resolved_proof_sidecar_child_mode" \
  SELF_OBJ_BOOTSTRAP_PROOF_OUTER_COMPILER="$resolved_proof_sidecar_outer_compiler" \
  SELF_OBJ_BOOTSTRAP_PROOF_SIDECAR_REAL_DRIVER="$resolved_stage0_real_driver" \
  SELF_OBJ_BOOTSTRAP_STAGE0_COMPAT=0 \
  SELF_OBJ_BOOTSTRAP_STRICT_ALLOW_FAST_REUSE="$bootstrap_strict_allow_fast_reuse" \
  SELF_OBJ_BOOTSTRAP_STRICT_STAGE2_ALIAS="$bootstrap_strict_stage2_alias" \
  SELF_OBJ_BOOTSTRAP_SKIP_SMOKE="$bootstrap_skip_smoke" \
  sh "$root/src/tooling/cheng_tooling_embedded_scripts/verify_backend_selfhost_bootstrap_self_obj.sh"

proof_meta="$abs_out_dir/cheng.stage2.proof.meta"
stage2_bin="$abs_out_dir/cheng.stage2"
stage2_meta="$abs_out_dir/cheng.stage2.meta"
stage3_meta="$abs_out_dir/cheng.stage3.witness.meta"
proof_smoke_src="$root/tests/cheng/backend/fixtures/return_i64.cheng"
proof_bin="$abs_out_dir/cheng.stage2.proof"
proof_smoke_exe="$abs_out_dir/cheng.stage2.proof.smoke.exe"
proof_smoke_log="$abs_out_dir/cheng.stage2.proof.smoke.log"
proof_smoke_run_log="$abs_out_dir/cheng.stage2.proof.smoke.run.log"
proof_smoke_report="$abs_out_dir/cheng.stage2.proof.smoke.report.txt"
stage2_smoke_exe="$abs_out_dir/cheng.stage2.smoke.exe"
stage2_smoke_log="$abs_out_dir/cheng.stage2.smoke.log"
stage2_smoke_run_log="$abs_out_dir/cheng.stage2.smoke.run.log"
stage2_smoke_report="$abs_out_dir/cheng.stage2.smoke.report.txt"

if [ ! -x "$proof_bin" ]; then
  echo "[verify_backend_selfhost_currentsrc_proof] missing proof launcher: $proof_bin" 1>&2
  exit 1
fi
if [ ! -x "$stage2_bin" ]; then
  echo "[verify_backend_selfhost_currentsrc_proof] missing current-source stage2: $stage2_bin" 1>&2
  exit 1
fi
if [ ! -f "$stage2_meta" ]; then
  echo "[verify_backend_selfhost_currentsrc_proof] warn: missing current-source stage2 meta: $stage2_meta" 1>&2
  stage2_meta=""
fi
if [ ! -f "$proof_smoke_src" ]; then
  echo "[verify_backend_selfhost_currentsrc_proof] missing smoke fixture: $proof_smoke_src" 1>&2
  exit 1
fi

require_meta_absent_field() {
  meta="$1"
  field="$2"
  if [ ! -f "$meta" ]; then
    return 0
  fi
  if rg -q "^${field}=" "$meta"; then
    echo "[verify_backend_selfhost_currentsrc_proof] unexpected ${field} in published current-source proof meta: $meta" 1>&2
    sed -n '1,120p' "$meta" 1>&2 || true
    exit 1
  fi
}

require_meta_absent_field "$stage2_meta" "sidecar_compiler"
require_meta_absent_field "$stage2_meta" "exec_fallback_outer_driver"
require_meta_absent_field "$stage3_meta" "sidecar_compiler"
require_meta_absent_field "$stage3_meta" "exec_fallback_outer_driver"

if [ "$skip_direct_smoke" = "1" ]; then
  echo "[verify_backend_selfhost_currentsrc_proof] direct smoke skipped" 1>&2
  echo "verify_backend_selfhost_currentsrc_proof ok"
  exit 0
fi

run_with_timeout_log() {
  timeout="$1"
  log="$2"
  shift 2
  set +e
  perl -e '
  use POSIX qw(setsid WNOHANG);
  my $timeout = shift;
  my $log = shift;
  my $pid = fork();
  if (!defined $pid) { exit 127; }
  if ($pid == 0) {
    setsid();
    open(STDOUT, ">", $log) or exit 127;
    open(STDERR, ">&STDOUT") or exit 127;
    exec @ARGV;
    exit 127;
  }
  my $end = time + $timeout;
  while (1) {
    my $r = waitpid($pid, WNOHANG);
    if ($r == $pid) {
      my $status = $?;
      if (($status & 127) != 0) {
        exit(128 + ($status & 127));
      }
      exit($status >> 8);
    }
    if (time >= $end) {
      kill "TERM", -$pid;
      kill "TERM", $pid;
      select(undef, undef, undef, 1.0);
      kill "KILL", -$pid;
      kill "KILL", $pid;
      waitpid($pid, 0);
      exit 124;
    }
    select(undef, undef, undef, 0.1);
  }
' "$timeout" "$log" "$@"
  rc="$?"
  set -e
  return "$rc"
}

run_driver_smoke_compile() {
  compiler_bin="$1"
  compiler_meta="$2"
  smoke_exe="$3"
  smoke_log="$4"
  smoke_sidecar_mode="$(proof_meta_field "$compiler_meta" "sidecar_mode")"
  smoke_sidecar_bundle="$(proof_meta_field "$compiler_meta" "sidecar_bundle")"
  smoke_sidecar_compiler="$(proof_meta_field "$compiler_meta" "sidecar_compiler")"
  smoke_sidecar_child_mode="$(proof_meta_field "$compiler_meta" "sidecar_child_mode")"
  smoke_sidecar_outer_compiler="$(proof_meta_field "$compiler_meta" "sidecar_outer_compiler")"
  run_with_timeout_log "$proof_smoke_timeout_budget" "$smoke_log" \
    env \
      MM=orc \
      BACKEND_BUILD_TRACK=release \
      BACKEND_STAGE1_PARSE_MODE=full \
      BACKEND_FN_SCHED=ws \
      BACKEND_JOBS="${BACKEND_JOBS:-8}" \
      BACKEND_FN_JOBS="${BACKEND_FN_JOBS:-${BACKEND_JOBS:-8}}" \
      BACKEND_DIRECT_EXE=0 \
      BACKEND_LINKERLESS_INMEM=0 \
      BACKEND_LINKER=system \
      BACKEND_NO_RUNTIME_C=0 \
      BACKEND_CURRENTSRC_WRAPPER_PRESERVE_SIDECAR=1 \
      BACKEND_UIR_SIDECAR_MODE="${smoke_sidecar_mode:-}" \
      BACKEND_UIR_SIDECAR_BUNDLE="${smoke_sidecar_bundle:-}" \
      BACKEND_UIR_SIDECAR_COMPILER="${smoke_sidecar_compiler:-}" \
      BACKEND_UIR_SIDECAR_CHILD_MODE="${smoke_sidecar_child_mode:-}" \
      BACKEND_UIR_SIDECAR_OUTER_COMPILER="${smoke_sidecar_outer_compiler:-}" \
      BACKEND_INPUT="$proof_smoke_src" \
      BACKEND_OUTPUT="$smoke_exe" \
      "$compiler_bin"
}

run_compile_and_run_smoke() {
  label="$1"
  compiler_bin="$2"
  compiler_meta="$3"
  smoke_exe="$4"
  smoke_log="$5"
  smoke_run_log="$6"
  smoke_report="$7"

  rm -f "$smoke_exe" "$smoke_log" "$smoke_run_log" "$smoke_report"
  compile_rc=0
  run_rc=0

  if run_driver_smoke_compile "$compiler_bin" "$compiler_meta" "$smoke_exe" "$smoke_log"; then
    compile_rc=0
  else
    compile_rc="$?"
  fi

  if [ "$compile_rc" -eq 0 ] && [ -x "$smoke_exe" ]; then
    if run_with_timeout_log "$proof_smoke_run_timeout_budget" "$smoke_run_log" "$smoke_exe"; then
      run_rc=0
    else
      run_rc="$?"
    fi
  else
    run_rc=125
  fi

  {
    echo "label=$label"
    echo "compiler_bin=$compiler_bin"
    echo "compiler_meta=$compiler_meta"
    echo "smoke_src=$proof_smoke_src"
    echo "smoke_exe=$smoke_exe"
    echo "smoke_log=$smoke_log"
    echo "smoke_run_log=$smoke_run_log"
    echo "compile_rc=$compile_rc"
    echo "run_rc=$run_rc"
  } > "$smoke_report"

  if [ "$compile_rc" -ne 0 ] || [ ! -x "$smoke_exe" ] || [ "$run_rc" -ne 0 ]; then
    cat "$smoke_report" 1>&2
    if [ -f "$smoke_log" ]; then
      tail -n 200 "$smoke_log" 1>&2 || true
    fi
    if [ -f "$smoke_run_log" ]; then
      tail -n 200 "$smoke_run_log" 1>&2 || true
    fi
    echo "[verify_backend_selfhost_currentsrc_proof] $label smoke failed" 1>&2
    exit 1
  fi
}

run_compile_and_run_smoke \
  "cheng.stage2.proof" \
  "$proof_bin" \
  "$proof_meta" \
  "$proof_smoke_exe" \
  "$proof_smoke_log" \
  "$proof_smoke_run_log" \
  "$proof_smoke_report"

run_compile_and_run_smoke \
  "cheng.stage2" \
  "$stage2_bin" \
  "$stage2_meta" \
  "$stage2_smoke_exe" \
  "$stage2_smoke_log" \
  "$stage2_smoke_run_log" \
  "$stage2_smoke_report"

echo "verify_backend_selfhost_currentsrc_proof ok"
