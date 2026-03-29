#!/usr/bin/env sh
:
set -eu
(set -o pipefail) 2>/dev/null && set -o pipefail

root="$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)"
cd "$root"
clean_env_path="${PATH:-/usr/bin:/bin:/usr/sbin:/sbin}"
clean_env_home="${HOME:-$root}"
clean_env_tmpdir="${TMPDIR:-/tmp}"
currentsrc_lineage_dir_rel="${SELF_OBJ_BOOTSTRAP_CURRENTSRC_LINEAGE_DIR:-artifacts/backend_selfhost_self_obj/probe_currentsrc_proof}"

to_abs() {
  p="$1"
  case "$p" in
    /*)
      printf "%s\n" "$p"
      return
      ;;
    *)
      printf "%s/%s\n" "$root" "$p"
      return
      ;;
  esac
}

case "$currentsrc_lineage_dir_rel" in
  /*) currentsrc_lineage_dir="$currentsrc_lineage_dir_rel" ;;
  *) currentsrc_lineage_dir="$root/$currentsrc_lineage_dir_rel" ;;
esac

currentsrc_bootstrap_stage0_path() {
  printf '%s\n' "$currentsrc_lineage_dir/cheng_stage0_currentsrc.proof"
}

currentsrc_host_target() {
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

currentsrc_bootstrap_stage0_outer_driver_path() {
  host_target="$(currentsrc_host_target)"
  printf '%s\n' "$root/chengcache/backend_driver_sidecar/backend_driver_sidecar_outer.$host_target"
}

currentsrc_bootstrap_stage0_sidecar_compiler_path() {
  printf '%s\n' "$currentsrc_lineage_dir/cheng_stage0_currentsrc.sidecar.sh"
}

currentsrc_stage2_proof_sidecar_compiler_path() {
  printf '%s\n' "$currentsrc_lineage_dir/cheng.stage2.proof.sidecar.sh"
}

proof_sidecar_real_driver_from_stage0() {
  cand="${SELF_OBJ_BOOTSTRAP_STAGE0:-}"
  driver_direct_export_surface_driver_path "$cand"
}

driver_surface_real_path() {
  cand="$1"
  if [ "$cand" = "" ]; then
    printf '\n'
    return 0
  fi
  cand="$(to_abs "$cand")"
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
  if [ -x "$cand" ]; then
    printf '%s\n' "$cand"
    return 0
  fi
  printf '\n'
}

# Keep local driver during bootstrap to avoid recursive worker invocations
# deleting the active stage0 driver mid-build.
export CLEAN_CHENG_LOCAL=0
if [ "${ABI:-}" = "" ]; then
  export ABI=v2_noptr
fi
if [ "${ABI}" != "v2_noptr" ]; then
  echo "[verify_backend_selfhost_bootstrap_self_obj] only ABI=v2_noptr is supported (got: ${ABI})" 1>&2
  exit 2
fi
if [ "${STAGE1_STD_NO_POINTERS:-}" = "" ]; then
  export STAGE1_STD_NO_POINTERS=0
fi
if [ "${STAGE1_STD_NO_POINTERS_STRICT:-}" = "" ]; then
  export STAGE1_STD_NO_POINTERS_STRICT=0
fi
if [ "${BACKEND_IR:-}" = "" ]; then
  export BACKEND_IR=uir
fi
if [ "${GENERIC_MODE:-}" = "" ]; then
  # Keep bootstrap on the published strict proof contract by default.
  export GENERIC_MODE=dict
fi
if [ "${GENERIC_SPEC_BUDGET:-}" = "" ]; then
  export GENERIC_SPEC_BUDGET=0
fi
if [ "${GENERIC_LOWERING:-}" = "" ]; then
  export GENERIC_LOWERING=mir_dict
fi
# Stage1 frontend pass toggles: keep selfhost bootstrap path stable by default.
if [ "${STAGE1_SEM_FIXED_0:-}" = "" ]; then
  export STAGE1_SEM_FIXED_0=0
fi
if [ "${STAGE1_OWNERSHIP_FIXED_0:-}" = "" ]; then
  export STAGE1_OWNERSHIP_FIXED_0=0
fi
if [ "${SELF_OBJ_BOOTSTRAP_STRICT_STAGE2_ALIAS_ON_SANITY:-}" = "" ]; then
  export SELF_OBJ_BOOTSTRAP_STRICT_STAGE2_ALIAS_ON_SANITY=0
fi
if [ "${SELF_OBJ_BOOTSTRAP_STRICT_STAGE2_ALIAS_ON_TIMEOUT:-}" = "" ]; then
  export SELF_OBJ_BOOTSTRAP_STRICT_STAGE2_ALIAS_ON_TIMEOUT=0
fi
if [ "${SELF_OBJ_BOOTSTRAP_STRICT_STAGE2_ALIAS_ON_OOM:-}" = "" ]; then
  export SELF_OBJ_BOOTSTRAP_STRICT_STAGE2_ALIAS_ON_OOM=0
fi
if [ "${SELF_OBJ_BOOTSTRAP_SAMPLE_ON_TIMEOUT:-}" = "" ]; then
  export SELF_OBJ_BOOTSTRAP_SAMPLE_ON_TIMEOUT=0
fi
if [ "${SELF_OBJ_BOOTSTRAP_TIMEOUT_SAMPLE_SECS:-}" = "" ]; then
  export SELF_OBJ_BOOTSTRAP_TIMEOUT_SAMPLE_SECS=1
fi

bootstrap_timeout_budget_with_sample() {
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

strict_fresh_stage0_driver() {
  resolved="$(sh "$root/src/tooling/cheng_tooling_embedded_scripts/resolve_backend_sidecar_defaults.sh" --root:"$root" --field:driver 2>/dev/null || true)"
  if [ "$resolved" != "" ] && [ -x "$resolved" ]; then
    printf '%s\n' "$resolved"
    return 0
  fi
  printf '\n'
}

canonical_stage0_bridge_driver() {
  cand="$root/artifacts/backend_driver/cheng"
  if [ -x "$cand" ]; then
    printf '%s\n' "$cand"
    return 0
  fi
  printf '\n'
}

driver_meta_field() {
  meta_path="$1"
  key="$2"
  if [ ! -f "$meta_path" ]; then
    printf '\n'
    return 0
  fi
  sed -n "s/^${key}=//p" "$meta_path" | head -n 1
}

driver_probe_stamp_field() {
  stamp_file="$1"
  key="$2"
  if [ ! -f "$stamp_file" ]; then
    printf '\n'
    return 0
  fi
  awk -F= -v key="$key" '$1 == key { print substr($0, index($0, "=") + 1); exit }' "$stamp_file"
}

driver_probe_internal_ownership_fixed_0_field() {
  stamp_file="$1"
  suffix="$2"
  value="$(driver_probe_stamp_field "$stamp_file" "stage1_ownership_fixed_0_${suffix}")"
  if [ "$value" = "" ]; then
    value="$(driver_probe_stamp_field "$stamp_file" "stage1_skip_ownership_${suffix}")"
  fi
  printf '%s\n' "$value"
}

driver_expected_generic_mode() {
  printf '%s\n' "dict"
}

driver_expected_generic_lowering() {
  printf '%s\n' "${GENERIC_LOWERING:-mir_dict}"
}

driver_published_stage0_surface() {
  probe_compiler="$1"
  probe_real="$probe_compiler"
  case "$probe_compiler" in
    *.sh)
      probe_real="$(driver_surface_real_path "$probe_compiler")"
      if [ "$probe_real" = "" ]; then
        probe_real="$probe_compiler"
      fi
      ;;
  esac
  case "$probe_real" in
    */artifacts/backend_selfhost_self_obj/cheng.stage1|\
    */artifacts/backend_selfhost_self_obj/cheng.stage2|\
    */artifacts/backend_selfhost_self_obj/cheng.stage2.proof|\
    */artifacts/backend_selfhost_self_obj/cheng.stage3.witness|\
    */artifacts/backend_selfhost_self_obj/cheng.stage3.witness.proof|\
    "$currentsrc_lineage_dir"/cheng.stage1|\
    "$currentsrc_lineage_dir"/cheng.stage2|\
    "$currentsrc_lineage_dir"/cheng.stage2.proof)
      return 0
      ;;
  esac
  return 1
}

driver_bootstrap_stage0_surface() {
  probe_compiler="$1"
  probe_real="$probe_compiler"
  case "$probe_compiler" in
    *.sh)
      probe_real="$(driver_surface_real_path "$probe_compiler")"
      if [ "$probe_real" = "" ]; then
        probe_real="$probe_compiler"
      fi
      ;;
  esac
  case "$probe_real" in
    "$currentsrc_lineage_dir"/cheng_stage0_currentsrc.proof)
      return 0
      ;;
  esac
  return 1
}

driver_surface_requires_sidecar_exec_contract() {
  probe_compiler="$1"
  probe_real="$probe_compiler"
  case "$probe_compiler" in
    *.sh)
      probe_real="$(driver_surface_real_path "$probe_compiler")"
      if [ "$probe_real" = "" ]; then
        probe_real="$probe_compiler"
      fi
      ;;
  esac
  meta_path="$(driver_stage0_meta_path_for_real_driver "$probe_real")"
  expected_label="$(driver_stage0_expected_label_for_real_driver "$probe_real")"
  [ "$meta_path" != "" ] || return 1
  [ "$expected_label" != "" ] || return 1
  [ -f "$meta_path" ] || return 1
  [ "$(driver_meta_field "$meta_path" "driver_input")" = "$(driver_stage0_expected_input_for_label "$expected_label")" ] || return 1
  [ "$(driver_meta_field "$meta_path" "sidecar_mode")" = "cheng" ] || return 1
  [ "$(driver_meta_field "$meta_path" "sidecar_bundle")" != "" ] || return 1
  [ "$(driver_meta_field "$meta_path" "sidecar_compiler")" != "" ] || return 1
  return 0
}

driver_legacy_bootstrap_surface() {
  probe_compiler="$1"
  probe_real="$probe_compiler"
  case "$probe_compiler" in
    *.sh)
      probe_real="$(driver_surface_real_path "$probe_compiler")"
      if [ "$probe_real" = "" ]; then
        return 1
      fi
      ;;
  esac
  [ -x "$probe_real" ] || return 1
  driver_published_stage0_surface "$probe_compiler" && return 1
  driver_bootstrap_stage0_surface "$probe_compiler" && return 1
  meta_path="$(driver_stage0_meta_path_for_real_driver "$probe_real")"
  if [ "$meta_path" != "" ] && [ -f "$meta_path" ]; then
    return 1
  fi
  command -v strings >/dev/null 2>&1 || return 1
  strings "$probe_real" 2>/dev/null | rg -q 'STAGE1_SKIP_SEM' || return 1
  strings "$probe_real" 2>/dev/null | rg -q 'STAGE1_SKIP_OWNERSHIP' || return 1
  strings "$probe_real" 2>/dev/null | rg -q 'removed env STAGE1_SKIP_SEM' && return 1
  strings "$probe_real" 2>/dev/null | rg -q 'removed env STAGE1_SKIP_OWNERSHIP' && return 1
  return 0
}

driver_stage0_sem_env_name() {
  probe_compiler="$1"
  if driver_legacy_bootstrap_surface "$probe_compiler"; then
    printf '%s\n' "STAGE1_SKIP_SEM"
    return 0
  fi
  printf '%s\n' "STAGE1_SEM_FIXED_0"
}

driver_stage0_ownership_env_name() {
  probe_compiler="$1"
  if driver_legacy_bootstrap_surface "$probe_compiler"; then
    printf '%s\n' "STAGE1_SKIP_OWNERSHIP"
    return 0
  fi
  printf '%s\n' "STAGE1_OWNERSHIP_FIXED_0"
}

driver_backend_env_prefix() {
  probe_compiler="$1"
  probe_real="$probe_compiler"
  case "$probe_compiler" in
    *.sh)
      probe_real="$(driver_surface_real_path "$probe_compiler")"
      if [ "$probe_real" = "" ]; then
        probe_real="$probe_compiler"
      fi
      ;;
  esac
  if [ ! -x "$probe_real" ]; then
    printf '%s\n' "BACKEND"
    return 0
  fi
  if command -v strings >/dev/null 2>&1 &&
     strings "$probe_real" 2>/dev/null | rg -q '^CHENG_BACKEND_OUTPUT$'; then
    printf '%s\n' "CHENG_BACKEND"
    return 0
  fi
  printf '%s\n' "BACKEND"
}

driver_backend_env_assign_with_prefix() {
  env_prefix="$1"
  env_key="$2"
  env_value="$3"
  printf '%s=%s\n' "${env_prefix}_${env_key}" "$env_value"
}

driver_stage0_meta_path_for_real_driver() {
  real_driver="$1"
  case "$real_driver" in
    */cheng.stage1|*/cheng.stage2|*/cheng.stage2.proof|*/cheng.stage3.witness|*/cheng.stage3.witness.proof|*/cheng_stage0_currentsrc.proof)
      printf '%s.meta\n' "$real_driver"
      return 0
      ;;
  esac
  printf '\n'
}

driver_stage0_expected_label_for_real_driver() {
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
    */cheng.stage3.witness)
      printf '%s\n' "stage3.witness"
      return 0
      ;;
    */cheng.stage3.witness.proof)
      printf '%s\n' "stage3.witness.proof"
      return 0
      ;;
  esac
  printf '\n'
}

driver_stage0_expected_input_for_label() {
  label="$1"
  case "$label" in
    currentsrc.proof.bootstrap)
      printf '%s\n' "${SELF_OBJ_BOOTSTRAP_BOOTSTRAP_DRIVER_INPUT_CANONICAL:-src/backend/tooling/backend_driver_proof.cheng}"
      return 0
      ;;
    stage1|stage2|stage2.proof|stage3.witness|stage3.witness.proof)
      printf '%s\n' "${SELF_OBJ_BOOTSTRAP_PUBLISHED_DRIVER_INPUT_CANONICAL:-src/backend/tooling/backend_driver.cheng}"
      return 0
      ;;
  esac
  printf '\n'
}

driver_direct_export_surface_driver_path() {
  probe_compiler="$1"
  probe_real="$probe_compiler"
  case "$probe_compiler" in
    *.sh)
      probe_real="$(driver_surface_real_path "$probe_compiler")"
      if [ "$probe_real" = "" ]; then
        printf '\n'
        return 0
      fi
      ;;
  esac
  case "$probe_real" in
    *.proof)
      outer_driver="$(driver_meta_field "${probe_real}.meta" "outer_driver")"
      if [ "$outer_driver" != "" ]; then
        printf '%s\n' "$outer_driver"
        return 0
      fi
      ;;
  esac
  printf '%s\n' "$probe_real"
}

driver_direct_export_surface_ok() {
  probe_compiler="$1"
  export_driver="$(driver_direct_export_surface_driver_path "$probe_compiler")"
  [ "$export_driver" != "" ] || return 1
  [ -x "$export_driver" ] || return 1
  command -v nm >/dev/null 2>&1 || return 1
  command -v awk >/dev/null 2>&1 || return 1
  surface_log="$(mktemp "${TMPDIR:-/tmp}/driver_stage0_nm.XXXXXX")"
  set +e
  (nm -gU "$export_driver" 2>/dev/null || nm "$export_driver" 2>/dev/null) >"$surface_log"
  nm_status="$?"
  set -e
  if [ "$nm_status" -ne 0 ]; then
    rm -f "$surface_log"
    return 1
  fi
  if ! awk '
    BEGIN { need1=0; need2=0; need3=0; legacy=0; }
    {
      sym=$NF;
      gsub(/^_+/, "", sym);
      if (sym == "driver_export_build_emit_obj_from_file_stage1_target_impl") need1=1;
      if (sym == "driver_export_prefer_sidecar_builds") need2=1;
      if (sym == "driver_export_buildModuleFromFileStage1TargetRetained") need3=1;
      if (sym ~ /^driverProof/) legacy=1;
    }
    END { exit((need1 && need2 && need3 && !legacy) ? 0 : 1); }
  ' "$surface_log"; then
    rm -f "$surface_log"
    return 1
  fi
  rm -f "$surface_log"
  return 0
}

driver_stage0_surface_meta_ok() {
  probe_compiler="$1"
  probe_real="$probe_compiler"
  case "$probe_compiler" in
    *.sh)
      probe_real="$(driver_surface_real_path "$probe_compiler")"
      if [ "$probe_real" = "" ]; then
        return 1
      fi
      ;;
  esac
  meta_path="$(driver_stage0_meta_path_for_real_driver "$probe_real")"
  expected_label="$(driver_stage0_expected_label_for_real_driver "$probe_real")"
  [ "$meta_path" != "" ] || return 1
  [ "$expected_label" != "" ] || return 1
  [ -f "$meta_path" ] || return 1
  [ "$(driver_meta_field "$meta_path" "meta_contract_version")" = "2" ] || return 1
  [ "$(driver_meta_field "$meta_path" "label")" = "$expected_label" ] || return 1
  meta_outer_driver="$(driver_meta_field "$meta_path" "outer_driver")"
  case "$expected_label" in
    stage2.proof|stage3.witness.proof)
      [ "$meta_outer_driver" != "" ] || return 1
      [ -x "$meta_outer_driver" ] || return 1
      [ "$(driver_meta_field "$meta_path" "sidecar_mode")" = "cheng" ] || return 1
      [ "$(driver_meta_field "$meta_path" "sidecar_bundle")" != "" ] || return 1
      [ "$(driver_meta_field "$meta_path" "sidecar_compiler")" != "" ] || return 1
      ;;
    *)
      [ "$meta_outer_driver" = "$probe_real" ] || return 1
      ;;
  esac
  [ "$(driver_meta_field "$meta_path" "driver_input")" = "$(driver_stage0_expected_input_for_label "$expected_label")" ] || return 1
  [ "$(driver_meta_field "$meta_path" "stage1_ownership_fixed_0_effective")" = "0" ] || return 1
  [ "$(driver_meta_field "$meta_path" "stage1_ownership_fixed_0_default")" = "0" ] || return 1
  [ "$(driver_meta_field "$meta_path" "generic_mode")" = "$(driver_expected_generic_mode)" ] || return 1
  [ "$(driver_meta_field "$meta_path" "generic_lowering")" = "$(driver_expected_generic_lowering)" ] || return 1
  return 0
}

driver_published_stage0_surface_meta_ok() {
  probe_compiler="$1"
  driver_published_stage0_surface "$probe_compiler" || return 1
  driver_stage0_surface_meta_ok "$probe_compiler" || return 1
  driver_direct_export_surface_ok "$probe_compiler" || return 1
  return 0
}

driver_bootstrap_stage0_surface_meta_ok() {
  probe_compiler="$1"
  driver_bootstrap_stage0_surface "$probe_compiler" || return 1
  probe_real="$probe_compiler"
  case "$probe_compiler" in
    *.sh)
      probe_real="$(driver_surface_real_path "$probe_compiler")"
      if [ "$probe_real" = "" ]; then
        return 1
      fi
      ;;
  esac
  meta_path="$(driver_stage0_meta_path_for_real_driver "$probe_real")"
  [ "$meta_path" != "" ] || return 1
  [ -f "$meta_path" ] || return 1
  [ "$(driver_meta_field "$meta_path" "meta_contract_version")" = "2" ] || return 1
  [ "$(driver_meta_field "$meta_path" "label")" = "currentsrc.proof.bootstrap" ] || return 1
  meta_outer_driver="$(driver_meta_field "$meta_path" "outer_driver")"
  [ "$meta_outer_driver" != "" ] || return 1
  [ -x "$meta_outer_driver" ] || return 1
  [ "$(driver_meta_field "$meta_path" "driver_input")" = "$(driver_stage0_expected_input_for_label "currentsrc.proof.bootstrap")" ] || return 1
  [ "$(driver_meta_field "$meta_path" "sidecar_mode")" = "cheng" ] || return 1
  [ "$(driver_meta_field "$meta_path" "sidecar_bundle")" != "" ] || return 1
  [ "$(driver_meta_field "$meta_path" "sidecar_compiler")" != "" ] || return 1
  [ "$(driver_meta_field "$meta_path" "stage1_ownership_fixed_0_effective")" = "0" ] || return 1
  [ "$(driver_meta_field "$meta_path" "stage1_ownership_fixed_0_default")" = "0" ] || return 1
  [ "$(driver_meta_field "$meta_path" "generic_mode")" = "$(driver_expected_generic_mode)" ] || return 1
  [ "$(driver_meta_field "$meta_path" "generic_lowering")" = "$(driver_expected_generic_lowering)" ] || return 1
  driver_direct_export_surface_ok "$probe_compiler" || return 1
  return 0
}

sidecar_bundle_export_surface_ok() {
  bundle_path="$1"
  [ "$bundle_path" != "" ] || return 1
  [ -f "$bundle_path" ] || return 1
  command -v nm >/dev/null 2>&1 || return 1
  command -v awk >/dev/null 2>&1 || return 1
  surface_log="$(mktemp "${TMPDIR:-/tmp}/backend_sidecar_bundle_nm.XXXXXX")"
  set +e
  (nm -gU "$bundle_path" 2>/dev/null || nm "$bundle_path" 2>/dev/null) >"$surface_log"
  nm_status="$?"
  set -e
  if [ "$nm_status" -ne 0 ]; then
    rm -f "$surface_log"
    return 1
  fi
  if ! awk '
    BEGIN { need1=0; need2=0; need3=0; need4=0; }
    {
      sym=$NF;
      gsub(/^_+/, "", sym);
      if (sym == "driver_export_buildActiveModulePtrs") need1=1;
      if (sym == "driver_export_buildModuleFromFileStage1TargetRetained") need2=1;
      if (sym == "driver_export_emit_obj_from_module_default_impl") need3=1;
      if (sym == "driver_export_build_emit_obj_from_file_stage1_target_impl") need4=1;
    }
    END { exit((need1 && need2 && need3 && need4) ? 0 : 1); }
  ' "$surface_log"; then
    rm -f "$surface_log"
    return 1
  fi
  rm -f "$surface_log"
  return 0
}

pick_strict_sidecar_bundle() {
  for cand in "$@"; do
    if [ "$cand" != "" ] && sidecar_bundle_export_surface_ok "$cand"; then
      printf '%s\n' "$cand"
      return 0
    fi
  done
  printf '\n'
}

publish_lineage_sidecar_compiler() {
  src_compiler="$1"
  real_driver="$2"
  out_compiler="$3"
  out_meta="${out_compiler}.meta"
  tmp_compiler="${out_compiler}.tmp.$$"
  tmp_meta="${out_meta}.tmp.$$"
  [ "$src_compiler" != "" ] || return 1
  [ -f "$src_compiler" ] || return 1
  [ "$real_driver" != "" ] || return 1
  [ -x "$real_driver" ] || return 1
  mkdir -p "$(dirname -- "$out_compiler")"
  cp "$src_compiler" "$tmp_compiler"
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

refresh_currentsrc_bootstrap_stage0_meta() {
  if bootstrap_driver_input_internal_transitional "${driver_input:-}"; then
    return 0
  fi
  currentsrc_bootstrap_stage0="$(currentsrc_bootstrap_stage0_path)"
  currentsrc_bootstrap_stage0_meta="${currentsrc_bootstrap_stage0}.meta"
  currentsrc_bootstrap_stage0_outer="$(currentsrc_bootstrap_stage0_outer_driver_path)"
  currentsrc_bootstrap_sidecar_compiler="$(currentsrc_bootstrap_stage0_sidecar_compiler_path)"
  currentsrc_bootstrap_stage0_stamp="$currentsrc_lineage_dir/stage1.native.after.compile_stamp.txt"
  proof_surface_fixture_local="${SELF_OBJ_BOOTSTRAP_PROOF_FIXTURE:-tests/cheng/backend/fixtures/return_add.cheng}"
  compile_stamp_field() {
    stamp_path="$1"
    stamp_key="$2"
    sed -n "s/^${stamp_key}=//p" "$stamp_path" | head -n 1
  }
  compile_ownership_fixed_field() {
    stamp_path="$1"
    suffix="$2"
    value="$(compile_stamp_field "$stamp_path" "stage1_ownership_fixed_0_${suffix}")"
    if [ "$value" = "" ]; then
      value="$(compile_stamp_field "$stamp_path" "stage1_skip_ownership_${suffix}")"
    fi
    printf '%s\n' "$value"
  }
  if [ ! -s "$currentsrc_bootstrap_stage0_stamp" ]; then
    currentsrc_bootstrap_stage0_stamp="$currentsrc_lineage_dir/stage1.native.compile_stamp.txt"
  fi
  if [ ! -x "$currentsrc_bootstrap_stage0" ] || [ ! -x "$currentsrc_bootstrap_stage0_outer" ] || [ ! -s "$currentsrc_bootstrap_stage0_stamp" ]; then
    return 1
  fi
  if [ "$(compile_stamp_field "$currentsrc_bootstrap_stage0_stamp" "input")" != "$driver_input" ] || \
     [ "$(compile_stamp_field "$currentsrc_bootstrap_stage0_stamp" "frontend")" != "stage1" ] || \
     [ "$(compile_stamp_field "$currentsrc_bootstrap_stage0_stamp" "whole_program")" != "1" ] || \
     [ "$(compile_ownership_fixed_field "$currentsrc_bootstrap_stage0_stamp" "effective")" != "0" ] || \
     [ "$(compile_ownership_fixed_field "$currentsrc_bootstrap_stage0_stamp" "default")" != "0" ] || \
     [ "$(compile_stamp_field "$currentsrc_bootstrap_stage0_stamp" "generic_mode")" != "$(driver_expected_generic_mode)" ] || \
     [ "$(compile_stamp_field "$currentsrc_bootstrap_stage0_stamp" "generic_lowering")" != "$(driver_expected_generic_lowering)" ]; then
    rm -f "$currentsrc_bootstrap_stage0_meta" 2>/dev/null || true
    return 1
  fi
  {
    bootstrap_sidecar_compiler="$currentsrc_bootstrap_sidecar_compiler"
    bootstrap_sidecar_bundle="${bootstrap_surface_sidecar_bundle:-${proof_surface_sidecar_bundle:-${SELF_OBJ_BOOTSTRAP_PROOF_SIDECAR_BUNDLE:-${BACKEND_UIR_SIDECAR_BUNDLE:-}}}}"
    if [ "$bootstrap_sidecar_bundle" = "" ] || ! sidecar_bundle_export_surface_ok "$bootstrap_sidecar_bundle"; then
      bootstrap_sidecar_bundle="$(pick_strict_sidecar_bundle \
        "$bootstrap_sidecar_bundle" \
        "$root/chengcache/backend_driver_sidecar/backend_driver_uir_sidecar.${target}.bundle.current" \
        "$root/chengcache/backend_driver_sidecar/backend_driver_uir_sidecar.${target}.bundle" \
        "$root/chengcache/backend_driver_sidecar/backend_driver_uir_sidecar.cheng_cmd2.${target}.bundle" \
        "$root/chengcache/backend_driver_sidecar/backend_driver_uir_sidecar.cheng_min.${target}.bundle" \
        "$root/chengcache/backend_driver_sidecar/backend_driver_uir_sidecar.wrapper.${target}.bundle" \
        "$root/artifacts/backend_sidecar_fresh_bundle3/backend_driver_uir_sidecar.${target}.bundle" \
        "$root/artifacts/backend_sidecar_fresh_bundle2/backend_driver_uir_sidecar.${target}.bundle" \
        "$root/artifacts/backend_sidecar_fresh_bundle/backend_driver_uir_sidecar.${target}.bundle")"
    fi
    bootstrap_sidecar_mode=""
    if [ "$bootstrap_sidecar_bundle" != "" ] && [ -f "$bootstrap_sidecar_bundle" ]; then
      bootstrap_sidecar_mode="cheng"
    fi
    if [ "$bootstrap_sidecar_compiler" = "" ] || [ ! -x "$bootstrap_sidecar_compiler" ]; then
      bootstrap_sidecar_compiler="$currentsrc_bootstrap_sidecar_compiler"
    fi
    bootstrap_sidecar_child_mode="${bootstrap_surface_sidecar_child_mode:-${proof_surface_sidecar_child_mode:-${SELF_OBJ_BOOTSTRAP_PROOF_SIDECAR_CHILD_MODE:-${BACKEND_UIR_SIDECAR_CHILD_MODE:-cli}}}}"
    echo "meta_contract_version=2"
    echo "label=currentsrc.proof.bootstrap"
    echo "outer_driver=$currentsrc_bootstrap_stage0_outer"
    echo "driver_input=$driver_input"
    echo "sidecar_mode=$bootstrap_sidecar_mode"
    echo "sidecar_bundle=$bootstrap_sidecar_bundle"
    echo "sidecar_compiler=$bootstrap_sidecar_compiler"
    echo "sidecar_child_mode=$bootstrap_sidecar_child_mode"
    echo "fixture=$proof_surface_fixture_local"
    echo "stage1_ownership_fixed_0_effective=$(compile_ownership_fixed_field "$currentsrc_bootstrap_stage0_stamp" "effective")"
    echo "stage1_ownership_fixed_0_default=$(compile_ownership_fixed_field "$currentsrc_bootstrap_stage0_stamp" "default")"
    echo "uir_phase_contract_version=$(compile_stamp_field "$currentsrc_bootstrap_stage0_stamp" "uir_phase_contract_version")"
    echo "generic_lowering=$(compile_stamp_field "$currentsrc_bootstrap_stage0_stamp" "generic_lowering")"
    echo "generic_mode=$(compile_stamp_field "$currentsrc_bootstrap_stage0_stamp" "generic_mode")"
  } > "${currentsrc_bootstrap_stage0_meta}.tmp.$$"
  mv "${currentsrc_bootstrap_stage0_meta}.tmp.$$" "$currentsrc_bootstrap_stage0_meta"
  return 0
}

proof_surface_sidecar_published_real_driver_candidate() {
  preferred="$1"
  for cand in \
    "$preferred" \
    "$currentsrc_lineage_dir/cheng.stage2" \
    "$currentsrc_lineage_dir/cheng.stage1"
  do
    if [ "$cand" != "" ] && [ -x "$cand" ] && driver_published_stage0_surface_meta_ok "$cand"; then
      printf '%s\n' "$cand"
      return 0
    fi
  done
  printf '\n'
}

refresh_proof_surface_sidecar_published_contract() {
  preferred="$1"
  target_real_driver="$(proof_surface_sidecar_published_real_driver_candidate "$preferred")"
  [ "$target_real_driver" != "" ] || return 1
  [ "${proof_surface_sidecar:-}" != "" ] || return 1
  [ -x "${proof_surface_sidecar:-}" ] || return 1
  proof_surface_sidecar_is_wrapper_contract "$proof_surface_sidecar" || return 0
  materialize_proof_surface_sidecar_real_driver \
    "$proof_surface_sidecar" \
    "$target_real_driver" \
    "$proof_surface_sidecar"
}

driver_stage0_sources_newer_than() {
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

driver_stage0_surface_current_enough() {
  probe_compiler="$1"
  probe_real="$(driver_surface_real_path "$probe_compiler")"
  [ "$probe_real" != "" ] || return 1
  [ -x "$probe_real" ] || return 1
  if driver_published_stage0_surface "$probe_compiler"; then
    if driver_stage0_sources_newer_than "$probe_real"; then
      return 1
    fi
  fi
  return 0
}

driver_remove_file_if_exists() {
  p="$1"
  if [ -e "$p" ] || [ -L "$p" ]; then
    rm -f "$p"
  fi
}

driver_remove_surface_family() {
  surface="$1"
  driver_remove_file_if_exists "$surface"
  driver_remove_file_if_exists "${surface}.meta"
  driver_remove_file_if_exists "${surface}.compile_stamp.txt"
  driver_remove_file_if_exists "${surface}.check.log"
  driver_remove_file_if_exists "${surface}.mode"
  driver_remove_file_if_exists "${surface}.o"
  driver_remove_file_if_exists "${surface}.smoke.exe"
  driver_remove_file_if_exists "${surface}.smoke.log"
  driver_remove_file_if_exists "${surface}.smoke.run.log"
  driver_remove_file_if_exists "${surface}.smoke.report.txt"
}

driver_wrapper_real_driver_path() {
  wrapper_path="$1"
  meta_path="${wrapper_path}.meta"
  if [ ! -f "$meta_path" ]; then
    printf '\n'
    return 0
  fi
  sed -n 's/^sidecar_real_driver=//p' "$meta_path" | head -n 1
}

driver_prune_wrapper_if_invalid() {
  wrapper_path="$1"
  if [ ! -e "$wrapper_path" ] && [ ! -e "${wrapper_path}.meta" ]; then
    return 0
  fi
  real_driver="$(driver_wrapper_real_driver_path "$wrapper_path")"
  if [ "$real_driver" = "" ]; then
    driver_remove_file_if_exists "$wrapper_path"
    driver_remove_file_if_exists "${wrapper_path}.meta"
    return 0
  fi
  if driver_published_stage0_surface "$real_driver"; then
    if ! driver_stage0_surface_meta_ok "$real_driver" || ! driver_stage0_surface_current_enough "$real_driver"; then
      driver_remove_file_if_exists "$wrapper_path"
      driver_remove_file_if_exists "${wrapper_path}.meta"
    fi
    return 0
  fi
  if driver_bootstrap_stage0_surface "$real_driver"; then
    if ! driver_bootstrap_stage0_surface_meta_ok "$real_driver"; then
      driver_remove_file_if_exists "$wrapper_path"
      driver_remove_file_if_exists "${wrapper_path}.meta"
    fi
    return 0
  fi
  driver_remove_file_if_exists "$wrapper_path"
  driver_remove_file_if_exists "${wrapper_path}.meta"
}

driver_prune_unusable_currentsrc_lineage() {
  for surface in \
    "$currentsrc_lineage_dir/cheng.stage1" \
    "$currentsrc_lineage_dir/cheng.stage2" \
    "$currentsrc_lineage_dir/cheng.stage2.proof" \
    "$currentsrc_lineage_dir/cheng.stage3.witness" \
    "$currentsrc_lineage_dir/cheng.stage3.witness.proof"
  do
    if [ ! -e "$surface" ] && [ ! -e "${surface}.meta" ]; then
      continue
    fi
    if ! driver_stage0_surface_meta_ok "$surface" || ! driver_stage0_surface_current_enough "$surface"; then
      driver_remove_surface_family "$surface"
    fi
  done
  if ! bootstrap_driver_input_internal_transitional "${driver_input:-}"; then
    bootstrap_surface="$(currentsrc_bootstrap_stage0_path)"
    if [ -e "$bootstrap_surface" ] || [ -e "${bootstrap_surface}.meta" ]; then
      if ! driver_bootstrap_stage0_surface_meta_ok "$bootstrap_surface"; then
        driver_remove_surface_family "$bootstrap_surface"
      fi
    fi
  fi
  driver_prune_wrapper_if_invalid "$currentsrc_lineage_dir/cheng.stage1.wrapper"
  driver_prune_wrapper_if_invalid "$currentsrc_lineage_dir/cheng.published_stage1.wrapper"
  driver_remove_file_if_exists "$currentsrc_lineage_dir/cheng_stage0_default"
}

run_with_timeout() {
  seconds="$1"
  shift
  perl -e '
    use POSIX qw(setsid WNOHANG);
    my $timeout = shift;
    my $sample_on_timeout = $ENV{"SELF_OBJ_BOOTSTRAP_SAMPLE_ON_TIMEOUT"} // "0";
    my $sample_secs = $ENV{"SELF_OBJ_BOOTSTRAP_TIMEOUT_SAMPLE_SECS"} // "1";
    my $sample_prefix = $ENV{"SELF_OBJ_BOOTSTRAP_TIMEOUT_SAMPLE_PREFIX"} // "";
    my $pid = fork();
    if (!defined $pid) { exit 127; }
    if ($pid == 0) {
      setsid();
      exec @ARGV;
      exit 127;
    }
    my $end = time + $timeout;
    while (1) {
      my $res = waitpid($pid, WNOHANG);
      if ($res == $pid) {
        my $status = $?;
        if (($status & 127) != 0) {
          exit(128 + ($status & 127));
        }
        exit($status >> 8);
      }
      if (time >= $end) {
        if ($sample_on_timeout ne "0" && $sample_prefix ne "" && -x "/usr/bin/sample") {
          my $parent_file = $sample_prefix . ".parent.sample.txt";
          my $child_file = $sample_prefix . ".child.sample.txt";
          my $pid_file = $sample_prefix . ".pids.txt";
          unlink $parent_file;
          unlink $child_file;
          unlink $pid_file;
          system("/usr/bin/sample", $pid, $sample_secs, "-file", $parent_file);

          my $deepest = 0;
          my @chain = ();
          my $deadline = time + (($sample_secs < 5) ? $sample_secs : 5);
          while (time <= $deadline && !$deepest) {
            if (open my $ps, "-|", "ps", "-Ao", "pid=,ppid=") {
              my %kids = ();
              while (my $line = <$ps>) {
                $line =~ s/^\s+//;
                $line =~ s/\s+$//;
                my ($cand, $ppid) = split /\s+/, $line, 2;
                next if !defined $cand || !defined $ppid;
                push @{$kids{$ppid}}, $cand;
              }
              close $ps;
              my $cur = $pid;
              my @cur_chain = ($pid);
              my $depth = 0;
              while (exists $kids{$cur} && @{$kids{$cur}}) {
                my @sorted = sort { $a <=> $b } @{$kids{$cur}};
                $cur = $sorted[-1];
                push @cur_chain, $cur;
                $depth++;
              }
              if ($depth > 0 && $cur ne $pid) {
                $deepest = $cur;
                @chain = @cur_chain;
              }
            }
            if (!$deepest) {
              select(undef, undef, undef, 0.05);
            }
          }

          if ($deepest) {
            system("/usr/bin/sample", $deepest, $sample_secs, "-file", $child_file);
            if (open(my $pfh, ">", $pid_file)) {
              print $pfh "parent=$pid\n";
              print $pfh "child=$deepest\n";
              print $pfh "chain=" . join(">", @chain) . "\n";
              close($pfh);
            }
          }
        }
        # First try process-group kill, then direct pid kill fallback.
        kill "TERM", -$pid;
        kill "TERM", $pid;
        my $grace_end = time + 1;
        while (time < $grace_end) {
          my $r = waitpid($pid, WNOHANG);
          if ($r == $pid) {
            my $status = $?;
            if (($status & 127) != 0) {
              exit(128 + ($status & 127));
            }
            exit($status >> 8);
          }
          select(undef, undef, undef, 0.1);
        }
        kill "KILL", -$pid;
        kill "KILL", $pid;
        exit 124;
      }
      select(undef, undef, undef, 0.1);
    }
  ' "$seconds" "$@"
}

driver_sanity_ok() {
  bin="$1"
  if [ ! -x "$bin" ]; then
    return 1
  fi
  set +e
  run_with_timeout 5 "$bin" --help >/dev/null 2>&1
  status=$?
  set -e
  case "$status" in
    0|1|2) return 0 ;;
  esac
  return 1
}

driver_blocked_stage0() {
  bin="$1"
  [ "$bin" != "" ] || return 1
  [ -x "$bin" ] || return 1
  h=""
  if command -v shasum >/dev/null 2>&1; then
    h="$(shasum -a 256 "$bin" 2>/dev/null | awk '{print $1}')"
  elif command -v sha256sum >/dev/null 2>&1; then
    h="$(sha256sum "$bin" 2>/dev/null | awk '{print $1}')"
  fi
  case "$h" in
    # Known-bad local/release drivers that can wedge during stage0 probing.
    08b9888a214418a32a468f1d9155c9d21d1789d01579cf84e7d9d6321366e382|\
    d059d1d84290dac64120dc78f0dbd9cb24e0e4b3d5a9045e63ad26232373ed1a)
      return 0
      ;;
  esac
  return 1
}

bootstrap_driver_input_requires_direct_exports() {
  input_path="$1"
  input_base="$(basename -- "$input_path")"
  case "$input_base" in
    backend_driver.cheng|backend_driver_uir_sidecar_wrapper.cheng)
      return 0
      ;;
  esac
  return 1
}

bootstrap_driver_input_internal_transitional() {
  input_path="$1"
  input_base="$(basename -- "$input_path")"
  case "$input_base" in
    backend_driver_seed_bridge.cheng)
      return 0
      ;;
  esac
  return 1
}

bootstrap_driver_input_prefers_cli_contract() {
  input_path="$1"
  input_base="$(basename -- "$input_path")"
  case "$input_base" in
    backend_driver.cheng|backend_driver_uir_sidecar_wrapper.cheng)
      return 0
      ;;
  esac
  return 1
}

driver_compile_probe_ok() {
  probe_compiler="$1"
  probe_sem_env="$(driver_stage0_sem_env_name "$probe_compiler")"
  probe_ownership_env="$(driver_stage0_ownership_env_name "$probe_compiler")"
  probe_stamp=""
  probe_ownership_effective=""
  probe_ownership_default=""
  if [ "$probe_compiler" = "" ] || [ ! -x "$probe_compiler" ]; then
    return 1
  fi
  if driver_bootstrap_stage0_surface "$probe_compiler"; then
    return 1
  fi
  if driver_published_stage0_surface "$probe_compiler" &&
     ! driver_published_stage0_surface_meta_ok "$probe_compiler"; then
    return 1
  fi
  if ! driver_legacy_bootstrap_surface "$probe_compiler" &&
     ! driver_stage0_surface_current_enough "$probe_compiler"; then
    return 1
  fi
  if driver_blocked_stage0 "$probe_compiler"; then
    return 1
  fi
  probe_input_base="$(basename -- "$driver_input")"
  probe_timeout_sample_prefix="$out_dir/selfhost_stage0_probe.timeout"
  rm -f "${probe_timeout_sample_prefix}".*.sample.txt "${probe_timeout_sample_prefix}".*.pids.txt 2>/dev/null || true
  if bootstrap_driver_input_requires_direct_exports "$driver_input"; then
    probe_target="${target:-}"
    probe_mm="${SELF_OBJ_BOOTSTRAP_MM:-${MM:-orc}}"
    if [ "$probe_target" = "" ]; then
      host_os_probe="$(uname -s 2>/dev/null || echo unknown)"
      host_arch_probe="$(uname -m 2>/dev/null || echo unknown)"
      case "$host_os_probe/$host_arch_probe" in
        Darwin/arm64)
          probe_target="arm64-apple-darwin"
          ;;
        Darwin/x86_64)
          probe_target="x86_64-apple-darwin"
          ;;
        Linux/aarch64|Linux/arm64)
          probe_target="aarch64-unknown-linux-gnu"
          ;;
        Linux/x86_64)
          probe_target="x86_64-unknown-linux-gnu"
          ;;
      esac
    fi
    [ "$probe_target" != "" ] || return 1
    probe_src="$driver_input"
    probe_out="chengcache/.selfhost_stage0_probe_$$.o"
    probe_log="$out_dir/selfhost_stage0_probe.log"
    probe_stamp="$out_dir/selfhost_stage0_probe.compile_stamp.txt"
    probe_real_driver_override=""
    probe_cli_contract="0"
    probe_backend_prefix="$(driver_backend_env_prefix "$probe_compiler")"
    if bootstrap_driver_input_prefers_cli_contract "$driver_input"; then
      probe_cli_contract="1"
    fi
    case "$probe_compiler" in
      *.sh)
        probe_real_driver_override=""
        probe_cli_contract="1"
        ;;
    esac
    rm -f "$probe_out" "$probe_log" "$probe_stamp"
    set +e
    if [ "$probe_cli_contract" = "1" ]; then
      SELF_OBJ_BOOTSTRAP_SAMPLE_ON_TIMEOUT=1 \
      SELF_OBJ_BOOTSTRAP_TIMEOUT_SAMPLE_PREFIX="$probe_timeout_sample_prefix" \
      run_with_timeout "$stage0_probe_timeout_budget" env -i \
        PATH="$clean_env_path" \
        HOME="$clean_env_home" \
        TMPDIR="$clean_env_tmpdir" \
        TOOLING_ROOT="$root" \
        BACKEND_STAGE1_PARSE_MODE=outline \
        BACKEND_UIR_SIDECAR_DISABLE=1 \
        BACKEND_UIR_PREFER_SIDECAR=0 \
        BACKEND_UIR_FORCE_SIDECAR=0 \
        MM="$probe_mm" \
        CACHE=0 \
        STAGE1_AUTO_SYSTEM=0 \
        BACKEND_VALIDATE=0 \
        BACKEND_COMPILE_STAMP_OUT="$probe_stamp" \
        "$probe_sem_env=0" \
        "$probe_ownership_env=0" \
        STAGE1_SKIP_CPROFILE=1 \
        "$probe_compiler" "$probe_src" \
        --frontend:stage1 \
        --emit:obj \
        --target:"$probe_target" \
        --linker:system \
        --allow-no-main \
        --no-whole-program \
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
      SELF_OBJ_BOOTSTRAP_SAMPLE_ON_TIMEOUT=1 \
      SELF_OBJ_BOOTSTRAP_TIMEOUT_SAMPLE_PREFIX="$probe_timeout_sample_prefix" \
      run_with_timeout "$stage0_probe_timeout_budget" env -i \
        PATH="$clean_env_path" \
        HOME="$clean_env_home" \
        TMPDIR="$clean_env_tmpdir" \
        TOOLING_ROOT="$root" \
        ${probe_real_driver_override:+TOOLING_BUILD_GLOBAL_CURRENTSOURCE_REAL_DRIVER="$probe_real_driver_override"} \
        BACKEND_STAGE1_PARSE_MODE=outline \
        BACKEND_UIR_SIDECAR_DISABLE=1 \
        BACKEND_UIR_PREFER_SIDECAR=0 \
        BACKEND_UIR_FORCE_SIDECAR=0 \
        MM="$probe_mm" \
        CACHE=0 \
        STAGE1_AUTO_SYSTEM=0 \
        BACKEND_MULTI=0 \
        BACKEND_FN_SCHED=ws \
        BACKEND_FN_JOBS=1 \
        BACKEND_INCREMENTAL=1 \
        BACKEND_JOBS=1 \
        BACKEND_VALIDATE=0 \
        BACKEND_COMPILE_STAMP_OUT="$probe_stamp" \
        "$probe_sem_env=0" \
        "$probe_ownership_env=0" \
        STAGE1_SKIP_CPROFILE=1 \
        GENERIC_MODE=dict \
        GENERIC_SPEC_BUDGET=0 \
        GENERIC_LOWERING=mir_dict \
        "$(driver_backend_env_assign_with_prefix "$probe_backend_prefix" INTERNAL_ALLOW_EMIT_OBJ 1)" \
        "$(driver_backend_env_assign_with_prefix "$probe_backend_prefix" EMIT obj)" \
        "$(driver_backend_env_assign_with_prefix "$probe_backend_prefix" TARGET "$probe_target")" \
        "$(driver_backend_env_assign_with_prefix "$probe_backend_prefix" FRONTEND stage1)" \
        "$(driver_backend_env_assign_with_prefix "$probe_backend_prefix" INPUT "$probe_src")" \
        "$(driver_backend_env_assign_with_prefix "$probe_backend_prefix" OUTPUT "$probe_out")" \
        "$probe_compiler" >"$probe_log" 2>&1
    fi
    probe_status="$?"
    set -e
    probe_ownership_effective="$(driver_probe_internal_ownership_fixed_0_field "$probe_stamp" "effective")"
    probe_ownership_default="$(driver_probe_internal_ownership_fixed_0_field "$probe_stamp" "default")"
    if [ "$probe_status" -eq 0 ] && [ -s "$probe_out" ] &&
       [ "$probe_ownership_effective" = "0" ] &&
       [ "$probe_ownership_default" = "0" ]; then
      return 0
    fi
    return 1
  fi
  if [ "$probe_input_base" = "backend_driver_proof.cheng" ]; then
    probe_target="${target:-}"
    probe_mm="${SELF_OBJ_BOOTSTRAP_MM:-${MM:-orc}}"
    if [ "$probe_target" = "" ]; then
      host_os_probe="$(uname -s 2>/dev/null || echo unknown)"
      host_arch_probe="$(uname -m 2>/dev/null || echo unknown)"
      case "$host_os_probe/$host_arch_probe" in
        Darwin/arm64)
          probe_target="arm64-apple-darwin"
          ;;
        Darwin/x86_64)
          probe_target="x86_64-apple-darwin"
          ;;
        Linux/aarch64|Linux/arm64)
          probe_target="aarch64-unknown-linux-gnu"
          ;;
        Linux/x86_64)
          probe_target="x86_64-unknown-linux-gnu"
          ;;
      esac
    fi
    [ "$probe_target" != "" ] || return 1
    probe_src="$driver_input"
    probe_out="chengcache/.selfhost_stage0_probe_$$.o"
    probe_log="$out_dir/selfhost_stage0_probe.log"
    probe_stamp="$out_dir/selfhost_stage0_probe.compile_stamp.txt"
    probe_real_driver_override=""
    probe_cli_contract="0"
    probe_backend_prefix="$(driver_backend_env_prefix "$probe_compiler")"
    if bootstrap_driver_input_prefers_cli_contract "$driver_input"; then
      probe_cli_contract="1"
    fi
    case "$probe_compiler" in
      *.sh)
        probe_real_driver_override=""
        probe_cli_contract="1"
        ;;
    esac
    rm -f "$probe_out" "$probe_log" "$probe_stamp"
    probe_requires_sidecar_exec="0"
    if driver_surface_requires_sidecar_exec_contract "$probe_compiler"; then
      probe_requires_sidecar_exec="1"
    fi
    set +e
    if [ "$probe_cli_contract" = "1" ]; then
      if [ "$probe_requires_sidecar_exec" = "1" ]; then
        SELF_OBJ_BOOTSTRAP_SAMPLE_ON_TIMEOUT=1 \
        SELF_OBJ_BOOTSTRAP_TIMEOUT_SAMPLE_PREFIX="$probe_timeout_sample_prefix" \
        run_with_timeout "$stage0_probe_timeout_budget" env -i \
          PATH="$clean_env_path" \
          HOME="$clean_env_home" \
          TMPDIR="$clean_env_tmpdir" \
          TOOLING_ROOT="$root" \
          BACKEND_CURRENTSRC_WRAPPER_PRESERVE_SIDECAR=1 \
          BACKEND_STAGE1_PARSE_MODE=outline \
          BACKEND_UIR_SIDECAR_MODE="$proof_surface_sidecar_mode" \
          BACKEND_UIR_SIDECAR_BUNDLE="$proof_surface_sidecar_bundle" \
          BACKEND_UIR_SIDECAR_COMPILER="$proof_surface_sidecar" \
          BACKEND_UIR_SIDECAR_CHILD_MODE="$proof_surface_sidecar_child_mode" \
          BACKEND_UIR_SIDECAR_OUTER_COMPILER="$proof_surface_sidecar_outer_companion" \
          MM="$probe_mm" \
          CACHE=0 \
          STAGE1_AUTO_SYSTEM=0 \
          BACKEND_VALIDATE=0 \
          BACKEND_COMPILE_STAMP_OUT="$probe_stamp" \
          "$probe_sem_env=0" \
          "$probe_ownership_env=0" \
          STAGE1_SKIP_CPROFILE=1 \
          "$probe_compiler" "$probe_src" \
          --frontend:stage1 \
          --emit:obj \
          --target:"$probe_target" \
          --linker:system \
          --allow-no-main \
          --no-whole-program \
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
      SELF_OBJ_BOOTSTRAP_SAMPLE_ON_TIMEOUT=1 \
      SELF_OBJ_BOOTSTRAP_TIMEOUT_SAMPLE_PREFIX="$probe_timeout_sample_prefix" \
      run_with_timeout "$stage0_probe_timeout_budget" env -i \
        PATH="$clean_env_path" \
        HOME="$clean_env_home" \
        TMPDIR="$clean_env_tmpdir" \
        TOOLING_ROOT="$root" \
        BACKEND_STAGE1_PARSE_MODE=outline \
        BACKEND_UIR_SIDECAR_DISABLE=1 \
        BACKEND_UIR_PREFER_SIDECAR=0 \
        BACKEND_UIR_FORCE_SIDECAR=0 \
        MM="$probe_mm" \
        CACHE=0 \
        STAGE1_AUTO_SYSTEM=0 \
        BACKEND_VALIDATE=0 \
        BACKEND_COMPILE_STAMP_OUT="$probe_stamp" \
        "$probe_sem_env=0" \
        "$probe_ownership_env=0" \
        STAGE1_SKIP_CPROFILE=1 \
        "$probe_compiler" "$probe_src" \
        --frontend:stage1 \
        --emit:obj \
        --target:"$probe_target" \
        --linker:system \
        --allow-no-main \
        --no-whole-program \
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
    else
      if [ "$probe_requires_sidecar_exec" = "1" ]; then
        SELF_OBJ_BOOTSTRAP_SAMPLE_ON_TIMEOUT=1 \
        SELF_OBJ_BOOTSTRAP_TIMEOUT_SAMPLE_PREFIX="$probe_timeout_sample_prefix" \
        run_with_timeout "$stage0_probe_timeout_budget" env -i \
          PATH="$clean_env_path" \
          HOME="$clean_env_home" \
          TMPDIR="$clean_env_tmpdir" \
          TOOLING_ROOT="$root" \
          ${probe_real_driver_override:+TOOLING_BUILD_GLOBAL_CURRENTSOURCE_REAL_DRIVER="$probe_real_driver_override"} \
          BACKEND_BUILD_TRACK=dev \
          BACKEND_STAGE1_PARSE_MODE=outline \
          MM="$probe_mm" \
          CACHE=0 \
          STAGE1_AUTO_SYSTEM=0 \
          BACKEND_MULTI=0 \
          BACKEND_FN_SCHED=ws \
          BACKEND_FN_JOBS=1 \
          BACKEND_INCREMENTAL=1 \
          BACKEND_JOBS=1 \
          BACKEND_VALIDATE=0 \
          BACKEND_COMPILE_STAMP_OUT="$probe_stamp" \
          "$probe_sem_env=0" \
          "$probe_ownership_env=0" \
          STAGE1_SKIP_CPROFILE=1 \
          GENERIC_MODE=dict \
          GENERIC_SPEC_BUDGET=0 \
          GENERIC_LOWERING=mir_dict \
          "$(driver_backend_env_assign_with_prefix "$probe_backend_prefix" INTERNAL_ALLOW_EMIT_OBJ 1)" \
          BACKEND_UIR_SIDECAR_MODE="$proof_surface_sidecar_mode" \
          BACKEND_UIR_SIDECAR_BUNDLE="$proof_surface_sidecar_bundle" \
          BACKEND_UIR_SIDECAR_COMPILER="$proof_surface_sidecar" \
          BACKEND_UIR_SIDECAR_CHILD_MODE="$proof_surface_sidecar_child_mode" \
          BACKEND_UIR_SIDECAR_OUTER_COMPILER="$proof_surface_sidecar_outer_companion" \
          "$(driver_backend_env_assign_with_prefix "$probe_backend_prefix" EMIT obj)" \
          "$(driver_backend_env_assign_with_prefix "$probe_backend_prefix" TARGET "$probe_target")" \
          "$(driver_backend_env_assign_with_prefix "$probe_backend_prefix" FRONTEND stage1)" \
          "$(driver_backend_env_assign_with_prefix "$probe_backend_prefix" INPUT "$probe_src")" \
          "$(driver_backend_env_assign_with_prefix "$probe_backend_prefix" OUTPUT "$probe_out")" \
          "$probe_compiler" >"$probe_log" 2>&1
      else
      SELF_OBJ_BOOTSTRAP_SAMPLE_ON_TIMEOUT=1 \
      SELF_OBJ_BOOTSTRAP_TIMEOUT_SAMPLE_PREFIX="$probe_timeout_sample_prefix" \
      run_with_timeout "$stage0_probe_timeout_budget" env -i \
        PATH="$clean_env_path" \
        HOME="$clean_env_home" \
        TMPDIR="$clean_env_tmpdir" \
        TOOLING_ROOT="$root" \
        ${probe_real_driver_override:+TOOLING_BUILD_GLOBAL_CURRENTSOURCE_REAL_DRIVER="$probe_real_driver_override"} \
        BACKEND_BUILD_TRACK=dev \
        BACKEND_STAGE1_PARSE_MODE=outline \
        MM="$probe_mm" \
        CACHE=0 \
        STAGE1_AUTO_SYSTEM=0 \
        BACKEND_MULTI=0 \
        BACKEND_FN_SCHED=ws \
        BACKEND_FN_JOBS=1 \
        BACKEND_INCREMENTAL=1 \
        BACKEND_JOBS=1 \
        BACKEND_VALIDATE=0 \
        BACKEND_COMPILE_STAMP_OUT="$probe_stamp" \
        "$probe_sem_env=0" \
        "$probe_ownership_env=0" \
        STAGE1_SKIP_CPROFILE=1 \
        GENERIC_MODE=dict \
        GENERIC_SPEC_BUDGET=0 \
        GENERIC_LOWERING=mir_dict \
        "$(driver_backend_env_assign_with_prefix "$probe_backend_prefix" INTERNAL_ALLOW_EMIT_OBJ 1)" \
        BACKEND_UIR_SIDECAR_DISABLE=1 \
        BACKEND_UIR_PREFER_SIDECAR=0 \
        BACKEND_UIR_FORCE_SIDECAR=0 \
        "$(driver_backend_env_assign_with_prefix "$probe_backend_prefix" EMIT obj)" \
        "$(driver_backend_env_assign_with_prefix "$probe_backend_prefix" TARGET "$probe_target")" \
        "$(driver_backend_env_assign_with_prefix "$probe_backend_prefix" FRONTEND stage1)" \
        "$(driver_backend_env_assign_with_prefix "$probe_backend_prefix" INPUT "$probe_src")" \
        "$(driver_backend_env_assign_with_prefix "$probe_backend_prefix" OUTPUT "$probe_out")" \
        "$probe_compiler" >"$probe_log" 2>&1
      fi
    fi
    probe_status="$?"
    set -e
    probe_ownership_effective="$(driver_probe_internal_ownership_fixed_0_field "$probe_stamp" "effective")"
    probe_ownership_default="$(driver_probe_internal_ownership_fixed_0_field "$probe_stamp" "default")"
    if [ "$probe_status" -eq 0 ] && [ -s "$probe_out" ] &&
       [ "$probe_ownership_effective" = "0" ] &&
       [ "$probe_ownership_default" = "0" ]; then
      return 0
    fi
    return 1
  fi
  probe_src="tests/cheng/backend/fixtures/return_add.cheng"
  if [ ! -f "$probe_src" ]; then
    probe_src="tests/cheng/backend/fixtures/hello_puts.cheng"
  fi
  if [ ! -f "$probe_src" ]; then
    return 0
  fi
  probe_out="chengcache/.selfhost_stage0_probe_$$"
  probe_log="$out_dir/selfhost_stage0_probe.log"
  probe_stamp="$out_dir/selfhost_stage0_probe.compile_stamp.txt"
  probe_backend_prefix="$(driver_backend_env_prefix "$probe_compiler")"
  rm -f "$probe_out" "$probe_log" "$probe_stamp"
  set +e
  SELF_OBJ_BOOTSTRAP_SAMPLE_ON_TIMEOUT=1 \
  SELF_OBJ_BOOTSTRAP_TIMEOUT_SAMPLE_PREFIX="$probe_timeout_sample_prefix" \
  run_with_timeout "$stage0_probe_timeout_budget" env -i \
    PATH="$clean_env_path" \
    HOME="$clean_env_home" \
    TMPDIR="$clean_env_tmpdir" \
    TOOLING_ROOT="$root" \
    CACHE=0 \
    STAGE1_AUTO_SYSTEM=0 \
    BACKEND_MULTI=0 \
    BACKEND_FN_SCHED=ws \
    BACKEND_FN_JOBS=1 \
    BACKEND_INCREMENTAL=1 \
    BACKEND_JOBS=1 \
    BACKEND_VALIDATE=0 \
    BACKEND_COMPILE_STAMP_OUT="$probe_stamp" \
    "$probe_sem_env=0" \
    "$probe_ownership_env=0" \
    STAGE1_SKIP_CPROFILE=1 \
    GENERIC_MODE=dict \
    GENERIC_SPEC_BUDGET=0 \
    GENERIC_LOWERING=mir_dict \
    "$(driver_backend_env_assign_with_prefix "$probe_backend_prefix" BUILD_TRACK dev)" \
    "$(driver_backend_env_assign_with_prefix "$probe_backend_prefix" EMIT exe)" \
    "$(driver_backend_env_assign_with_prefix "$probe_backend_prefix" TARGET "$target")" \
    "$(driver_backend_env_assign_with_prefix "$probe_backend_prefix" FRONTEND stage1)" \
    "$(driver_backend_env_assign_with_prefix "$probe_backend_prefix" INPUT "$probe_src")" \
    "$(driver_backend_env_assign_with_prefix "$probe_backend_prefix" OUTPUT "$probe_out")" \
    "$probe_compiler" >"$probe_log" 2>&1
  probe_status="$?"
  set -e
  probe_ownership_effective="$(driver_probe_internal_ownership_fixed_0_field "$probe_stamp" "effective")"
  probe_ownership_default="$(driver_probe_internal_ownership_fixed_0_field "$probe_stamp" "default")"
  if [ "$probe_status" -eq 0 ] && [ -x "$probe_out" ] &&
     [ "$probe_ownership_effective" = "0" ] &&
     [ "$probe_ownership_default" = "0" ]; then
    return 0
  fi
  return 1
}

driver_trusted_stage0_surface() {
  probe_compiler="$1"
  if driver_published_stage0_surface "$probe_compiler"; then
    driver_published_stage0_surface_meta_ok "$probe_compiler" || return 1
    driver_stage0_surface_current_enough "$probe_compiler"
    return $?
  fi
  if driver_bootstrap_stage0_surface "$probe_compiler"; then
    driver_bootstrap_stage0_surface_meta_ok "$probe_compiler"
    return $?
  fi
  return 1
}

driver_stage1_probe_ok() {
  probe_compiler="$1"
  probe_sem_env="$(driver_stage0_sem_env_name "$probe_compiler")"
  probe_ownership_env="$(driver_stage0_ownership_env_name "$probe_compiler")"
  if [ "$probe_compiler" = "" ] || [ ! -x "$probe_compiler" ]; then
    return 1
  fi
  if ! bootstrap_driver_input_internal_transitional "$driver_input" &&
     ! driver_direct_export_surface_ok "$probe_compiler"; then
    printf '%s\n' "[verify_backend_selfhost_bootstrap_self_obj] missing direct export surface: $probe_compiler" >"$out_dir/selfhost_stage1_probe.log"
    return 1
  fi
  if [ "$(basename "$driver_input")" = "backend_driver_proof.cheng" ]; then
    : >"$out_dir/selfhost_stage1_probe.log"
    return 0
  fi
  probe_src="tests/cheng/backend/fixtures/return_add.cheng"
  if [ ! -f "$probe_src" ]; then
    return 0
  fi
  probe_out="chengcache/.selfhost_stage1_probe_$$"
  probe_log="$out_dir/selfhost_stage1_probe.log"
  rm -f "$probe_out" "$probe_log"
  set +e
  run_with_timeout "$stage1_probe_timeout_budget" env -i \
    PATH="$clean_env_path" \
    HOME="$clean_env_home" \
    TMPDIR="$clean_env_tmpdir" \
    TOOLING_ROOT="$root" \
    MM="$mm" \
    CACHE=0 \
    STAGE1_AUTO_SYSTEM=0 \
    BACKEND_MULTI=0 \
    BACKEND_FN_SCHED=ws \
    BACKEND_FN_JOBS=1 \
    BACKEND_INCREMENTAL=1 \
    BACKEND_JOBS=1 \
    BACKEND_VALIDATE=0 \
    STAGE1_STD_NO_POINTERS=0 \
    STAGE1_STD_NO_POINTERS_STRICT=0 \
    "$probe_sem_env=0" \
    "$probe_ownership_env=0" \
    STAGE1_SKIP_CPROFILE=1 \
    GENERIC_MODE=dict \
    GENERIC_SPEC_BUDGET=0 \
    BACKEND_EMIT=exe \
    BACKEND_TARGET="$target" \
    BACKEND_FRONTEND=stage1 \
    BACKEND_INPUT="$probe_src" \
    BACKEND_OUTPUT="$probe_out" \
    "$probe_compiler" >"$probe_log" 2>&1
  probe_status="$?"
  set -e
  if [ "$probe_status" -eq 0 ] && [ -x "$probe_out" ]; then
    return 0
  fi
  return 1
}

timestamp_now() {
  date +%s
}

detect_host_jobs() {
  jobs=""
  if command -v nproc >/dev/null 2>&1; then
    jobs="$(nproc 2>/dev/null || true)"
  fi
  if [ "$jobs" = "" ] && command -v sysctl >/dev/null 2>&1; then
    jobs="$(sysctl -n hw.logicalcpu 2>/dev/null || sysctl -n hw.ncpu 2>/dev/null || true)"
  fi
  case "$jobs" in
    ''|*[!0-9]*)
      jobs="1"
      ;;
  esac
  if [ "$jobs" -lt 1 ]; then
    jobs="1"
  fi
  printf '%s\n' "$jobs"
}

cleanup_local_driver_on_exit="0"
if [ "${CLEAN_CHENG_LOCAL:-0}" = "1" ] && [ "${TOOLING_CLEANUP_DEPTH:-0}" = "0" ] && [ "${SELF_OBJ_BOOTSTRAP_STAGE0:-}" = "" ]; then
  export TOOLING_CLEANUP_DEPTH=1
  cleanup_local_driver_on_exit="1"
fi

host_os="$(uname -s 2>/dev/null || echo unknown)"
host_arch="$(uname -m 2>/dev/null || echo unknown)"

target=""
case "$host_os" in
  Darwin)
    case "$host_arch" in
      arm64)
        target="arm64-apple-darwin"
        ;;
    esac
    ;;
  Linux)
    case "$host_arch" in
      aarch64|arm64)
        target="aarch64-unknown-linux-gnu"
        ;;
    esac
    ;;
esac

if [ "$target" = "" ]; then
  echo "verify_backend_selfhost_bootstrap_self_obj skip: host=$host_os/$host_arch" 1>&2
  exit 2
fi

linker_mode="${SELF_OBJ_BOOTSTRAP_LINKER:-}"
if [ "$linker_mode" = "" ]; then
  linker_mode="self"
fi
if [ "$linker_mode" != "self" ]; then
  echo "[Error] verify_backend_selfhost_bootstrap_self_obj requires SELF_OBJ_BOOTSTRAP_LINKER=self (cc path removed)" 1>&2
  exit 2
fi

if [ "$linker_mode" = "self" ] && [ "$host_os" = "Darwin" ]; then
  if ! command -v codesign >/dev/null 2>&1; then
    echo "verify_backend_selfhost_bootstrap_self_obj skip: missing codesign" 1>&2
    exit 2
  fi
fi

runtime_mode="${SELF_OBJ_BOOTSTRAP_RUNTIME:-}"
if [ "$runtime_mode" = "" ]; then
  runtime_mode="cheng"
fi
if [ "$runtime_mode" != "cheng" ]; then
  echo "[Error] verify_backend_selfhost_bootstrap_self_obj requires SELF_OBJ_BOOTSTRAP_RUNTIME=cheng (C runtime path removed)" 1>&2
  exit 2
fi
driver_input="${SELF_OBJ_BOOTSTRAP_DRIVER_INPUT:-src/backend/tooling/backend_driver_proof.cheng}"
if [ ! -f "$driver_input" ]; then
  echo "[Error] verify_backend_selfhost_bootstrap_self_obj missing SELF_OBJ_BOOTSTRAP_DRIVER_INPUT: $driver_input" 1>&2
  exit 2
fi
bootstrap_driver_input="$driver_input"
published_driver_input="${SELF_OBJ_BOOTSTRAP_PUBLISHED_DRIVER_INPUT:-}"
if [ "$published_driver_input" = "" ]; then
  case "$(basename -- "$bootstrap_driver_input")" in
    backend_driver_proof.cheng)
      published_driver_input="src/backend/tooling/backend_driver.cheng"
      ;;
    *)
      published_driver_input="$bootstrap_driver_input"
      ;;
  esac
fi
if [ ! -f "$published_driver_input" ]; then
  echo "[Error] verify_backend_selfhost_bootstrap_self_obj missing SELF_OBJ_BOOTSTRAP_PUBLISHED_DRIVER_INPUT: $published_driver_input" 1>&2
  exit 2
fi
case "$bootstrap_driver_input" in
  "$root"/*)
    export SELF_OBJ_BOOTSTRAP_BOOTSTRAP_DRIVER_INPUT_CANONICAL="${bootstrap_driver_input#"$root"/}"
    ;;
  *)
    export SELF_OBJ_BOOTSTRAP_BOOTSTRAP_DRIVER_INPUT_CANONICAL="$bootstrap_driver_input"
    ;;
esac
case "$published_driver_input" in
  "$root"/*)
    export SELF_OBJ_BOOTSTRAP_PUBLISHED_DRIVER_INPUT_CANONICAL="${published_driver_input#"$root"/}"
    ;;
  *)
    export SELF_OBJ_BOOTSTRAP_PUBLISHED_DRIVER_INPUT_CANONICAL="$published_driver_input"
    ;;
esac
dual_publish_lineage="0"
if [ "$SELF_OBJ_BOOTSTRAP_BOOTSTRAP_DRIVER_INPUT_CANONICAL" = "src/backend/tooling/backend_driver_proof.cheng" ] &&
   [ "$SELF_OBJ_BOOTSTRAP_PUBLISHED_DRIVER_INPUT_CANONICAL" != "$SELF_OBJ_BOOTSTRAP_BOOTSTRAP_DRIVER_INPUT_CANONICAL" ]; then
  dual_publish_lineage="1"
fi
if [ "${SELF_OBJ_BOOTSTRAP_STRICT_STAGE2_ALIAS:-}" = "" ]; then
  case "$(basename -- "$driver_input")" in
    backend_driver_proof.cheng)
      export SELF_OBJ_BOOTSTRAP_STRICT_STAGE2_ALIAS=1
      ;;
    *)
      export SELF_OBJ_BOOTSTRAP_STRICT_STAGE2_ALIAS=0
      ;;
  esac
fi
runtime_cheng_src="src/std/system_helpers_backend.cheng"
runtime_obj=""
runtime_obj_prebuilt="$root/chengcache/system_helpers.backend.cheng.${target}.o"
cstring_link_retry="${SELF_OBJ_BOOTSTRAP_CSTRING_LINK_RETRY:-0}"

emit_prebuilt_runtime_obj_candidates() {
  explicit_runtime_obj="${SELF_OBJ_BOOTSTRAP_RUNTIME_OBJ:-}"
  if [ "$explicit_runtime_obj" != "" ]; then
    explicit_runtime_obj="$(to_abs "$explicit_runtime_obj")"
    if [ -f "$explicit_runtime_obj" ]; then
      printf '%s\n' "$explicit_runtime_obj"
    fi
  fi
  for cand in \
    "$root/chengcache/runtime_selflink/system_helpers.backend.fullcompat.${target}.o" \
    "$root/chengcache/runtime_selflink/system_helpers.backend.combined.${target}.o" \
    "$root/artifacts/backend_selfhost_self_obj/probe_prod.strict.noreuse/system_helpers.backend.cheng.o" \
    "$runtime_obj_prebuilt" \
    "$root/artifacts/backend_selfhost_self_obj/system_helpers.backend.cheng.o" \
    "$root/artifacts/backend_selfhost_self_obj/system_helpers.backend.cheng.stage1shim.o" \
    "$root/artifacts/backend_selfhost_self_obj/stage1.native.runtime.dedup.o" \
    "$root/artifacts/runtime/system_helpers.backend.combined.${target}.o" \
    "$root/artifacts/backend_mm/system_helpers.backend.combined.${target}.o"
  do
    if [ -f "$cand" ]; then
      printf '%s\n' "$cand"
    fi
  done
  return 0
}

pick_prebuilt_runtime_obj() {
  found=""
  while IFS= read -r cand; do
    if [ "$cand" != "" ]; then
      found="$cand"
      break
    fi
  done <<EOF
$(emit_prebuilt_runtime_obj_candidates)
EOF
  if [ "$found" != "" ]; then
    printf '%s\n' "$found"
    return 0
  fi
  printf '%s\n' "$runtime_obj_prebuilt"
  return 0
}

pick_prebuilt_runtime_obj_candidates() {
  emit_prebuilt_runtime_obj_candidates
}

out_dir_rel="${SELF_OBJ_BOOTSTRAP_OUT_DIR:-artifacts/backend_selfhost_self_obj}"
case "$out_dir_rel" in
  /*)
    out_dir="$out_dir_rel"
    ;;
  *)
    out_dir="$root/$out_dir_rel"
    ;;
esac
mkdir -p "$out_dir"
mkdir -p chengcache
runtime_obj="$out_dir/system_helpers.backend.cheng.o"
# Runtime cstring compat merge path has been removed; clear stale artifacts so
# timeout diagnostics don't accidentally report old retry logs.
rm -f "$out_dir/cstring_compat.build.txt" "$out_dir/cstring_compat.o" "$out_dir/cstring_compat.s" 2>/dev/null || true
build_timeout="${SELF_OBJ_BOOTSTRAP_TIMEOUT:-}"
smoke_timeout="${SELF_OBJ_BOOTSTRAP_SMOKE_TIMEOUT:-60}"
stage0_probe_timeout="${SELF_OBJ_BOOTSTRAP_STAGE0_PROBE_TIMEOUT:-60}"
stage1_probe_timeout="${SELF_OBJ_BOOTSTRAP_STAGE1_PROBE_TIMEOUT:-60}"
if [ "$build_timeout" = "" ]; then
  build_timeout="60"
fi
build_timeout_budget="$(bootstrap_timeout_budget_with_sample "$build_timeout" "${SELF_OBJ_BOOTSTRAP_TIMEOUT_SAMPLE_SECS:-0}")"
smoke_timeout_budget="$(bootstrap_timeout_budget_with_sample "$smoke_timeout" "${SELF_OBJ_BOOTSTRAP_TIMEOUT_SAMPLE_SECS:-0}")"
stage0_probe_timeout_budget="$(bootstrap_timeout_budget_with_sample "$stage0_probe_timeout" "${SELF_OBJ_BOOTSTRAP_TIMEOUT_SAMPLE_SECS:-0}")"
stage1_probe_timeout_budget="$(bootstrap_timeout_budget_with_sample "$stage1_probe_timeout" "${SELF_OBJ_BOOTSTRAP_TIMEOUT_SAMPLE_SECS:-0}")"
currentsrc_lineage_dir="/Users/lbcheng/cheng-lang/chengcache/backend_seed_stage0.a3d642e70455.fHlQDI/lineage"
probe="/Users/lbcheng/cheng-lang/chengcache/backend_seed_stage0.a3d642e70455.fHlQDI/lineage/cheng_stage0_currentsrc.proof"
printf 'probe=%s\n' "/Users/lbcheng/cheng-lang/chengcache/backend_seed_stage0.a3d642e70455.fHlQDI/lineage/cheng_stage0_currentsrc.proof"
printf 'meta_path=%s\n' "$(driver_stage0_meta_path_for_real_driver \"$probe\")"
printf 'bootstrap_surface=%s\n' "$([ \"$(currentsrc_bootstrap_stage0_path)\" = \"$probe\" ] && echo yes || echo no)"
printf 'meta_contract=%s\n' "$(driver_meta_field \"${probe}.meta\" meta_contract_version)"
printf 'label=%s\n' "$(driver_meta_field \"${probe}.meta\" label)"
printf 'outer=%s\n' "$(driver_meta_field \"${probe}.meta\" outer_driver)"
printf 'input=%s\n' "$(driver_meta_field \"${probe}.meta\" driver_input)"
printf 'sidecar_mode=%s\n' "$(driver_meta_field \"${probe}.meta\" sidecar_mode)"
printf 'sidecar_bundle=%s\n' "$(driver_meta_field \"${probe}.meta\" sidecar_bundle)"
printf 'sidecar_compiler=%s\n' "$(driver_meta_field \"${probe}.meta\" sidecar_compiler)"
printf 'own_eff=%s\n' "$(driver_meta_field \"${probe}.meta\" stage1_ownership_fixed_0_effective)"
printf 'own_def=%s\n' "$(driver_meta_field \"${probe}.meta\" stage1_ownership_fixed_0_default)"
printf 'gen_mode=%s\n' "$(driver_meta_field \"${probe}.meta\" generic_mode)"
printf 'gen_lower=%s\n' "$(driver_meta_field \"${probe}.meta\" generic_lowering)"
printf 'direct_export_ok=%s\n' "$({ driver_direct_export_surface_ok \"$probe\" && echo yes; } || echo no)"
printf 'meta_ok=%s\n' "$({ driver_bootstrap_stage0_surface_meta_ok \"$probe\" && echo yes; } || echo no)"
