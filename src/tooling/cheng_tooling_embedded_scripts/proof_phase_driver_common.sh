#!/usr/bin/env sh

proof_phase_driver_extract_stamp_field() {
  stamp_file="$1"
  key="$2"
  if [ ! -f "$stamp_file" ]; then
    printf '\n'
    return 0
  fi
  awk -F= -v key="$key" '$1 == key { print substr($0, index($0, "=") + 1); exit }' "$stamp_file"
}

proof_phase_driver_summary_flat() {
  if [ "${proof_phase_driver_summary:-}" = "" ]; then
    printf '\n'
    return 0
  fi
  printf '%s' "$proof_phase_driver_summary" | tr '\n' '|' | sed 's/|$//'
}

proof_phase_driver_published_surface_ok() {
  cand="$1"
  meta="${cand}.meta"
  stamp="${cand}.compile_stamp.txt"
  if [ ! -x "$cand" ] || [ ! -s "$meta" ] || [ ! -s "$stamp" ]; then
    return 1
  fi
  eff="$(proof_phase_driver_extract_stamp_field "$stamp" "stage1_skip_ownership_effective")"
  def="$(proof_phase_driver_extract_stamp_field "$stamp" "stage1_skip_ownership_default")"
  phase="$(proof_phase_driver_extract_stamp_field "$stamp" "uir_phase_contract_version")"
  if [ "$eff" != "0" ] || [ "$def" != "0" ]; then
    return 1
  fi
  if [ "$phase" != "p4_phase_v1" ]; then
    return 1
  fi
  case "$cand" in
    "$root"/artifacts/backend_selfhost_self_obj/probe_currentsrc_proof/cheng.stage2|\
    "$root"/artifacts/backend_selfhost_self_obj/probe_currentsrc_proof/cheng.stage3.witness)
      if rg -q '^(sidecar_compiler)=' "$meta"; then
        return 1
      fi
      ;;
  esac
  return 0
}

proof_phase_driver_first_error() {
  log_file="$1"
  if [ ! -f "$log_file" ]; then
    printf '\n'
    return 0
  fi
  rg -n "Cannot write while borrowed|invalid GENERIC_LOWERING|invalid emit mode|mir_borrow_skipped|ownership_skipped|Segmentation fault|Bus error|Unexpected token" "$log_file" -n | head -n 1 | cut -d: -f3- || true
}

proof_phase_driver_label_safe() {
  printf '%s' "$1" | tr -c 'A-Za-z0-9._-' '_'
}

proof_phase_driver_sidecar_meta_field() {
  compiler="$1"
  key="$2"
  meta="${compiler}.meta"
  if [ "$compiler" = "" ] || [ ! -f "$meta" ]; then
    printf '\n'
    return 0
  fi
  sed -n "s/^${key}=//p" "$meta" | head -n 1
}

proof_phase_driver_append_candidate_line() {
  cand="$1"
  label="$2"
  sidecar_compiler="$3"
  [ "$cand" = "" ] && return 0
  if [ ! -x "$cand" ]; then
    return 0
  fi
  printf '%s\t%s\t%s\n' "$cand" "$label" "$sidecar_compiler"
}

proof_phase_driver_default_sidecar() {
  resolved="$(sh "$root/src/tooling/cheng_tooling_embedded_scripts/resolve_backend_sidecar_defaults.sh" --root:"$root" --field:compiler 2>/dev/null || true)"
  if [ "$resolved" != "" ] && [ -x "$resolved" ]; then
    printf '%s\n' "$resolved"
    return 0
  fi
  printf '\n'
}

proof_phase_driver_default_sidecar_mode() {
  explicit="${BACKEND_UIR_SIDECAR_MODE:-}"
  case "$explicit" in
    cheng)
      printf '%s\n' "$explicit"
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
  printf '\n'
}

proof_phase_driver_default_sidecar_bundle() {
  explicit="${BACKEND_UIR_SIDECAR_BUNDLE:-}"
  if [ "$explicit" != "" ] && [ -f "$explicit" ]; then
    printf '%s\n' "$explicit"
    return 0
  fi
  resolved="$(sh "$root/src/tooling/cheng_tooling_embedded_scripts/resolve_backend_sidecar_defaults.sh" --root:"$root" --field:bundle 2>/dev/null || true)"
  if [ "$resolved" != "" ] && [ -f "$resolved" ]; then
    printf '%s\n' "$resolved"
    return 0
  fi
  printf '\n'
}

proof_phase_driver_sidecar_child_mode() {
  compiler="$1"
  explicit="${BACKEND_UIR_SIDECAR_CHILD_MODE:-}"
  case "$explicit" in
    cli|outer_cli)
      printf '%s\n' "$explicit"
      return 0
      ;;
  esac
  default_sidecar="$(proof_phase_driver_default_sidecar)"
  if [ "$compiler" != "" ] && [ "$compiler" = "$default_sidecar" ]; then
    resolved="$(sh "$root/src/tooling/cheng_tooling_embedded_scripts/resolve_backend_sidecar_defaults.sh" --root:"$root" --field:child_mode 2>/dev/null || true)"
    case "$resolved" in
      cli|outer_cli)
        printf '%s\n' "$resolved"
        return 0
        ;;
    esac
  fi
  meta_mode="$(proof_phase_driver_sidecar_meta_field "$compiler" "sidecar_child_mode")"
  case "$meta_mode" in
    cli|outer_cli)
      printf '%s\n' "$meta_mode"
      return 0
      ;;
  esac
  printf '\n'
}

proof_phase_driver_probe_candidate() {
  cand="$1"
  label="$2"
  sidecar_compiler="$3"
  target="$4"
  out_dir="$5"
  fixture="$6"

  proof_phase_driver_probe_rc=""
  proof_phase_driver_probe_effective=""
  proof_phase_driver_probe_default=""
  proof_phase_driver_probe_reason=""
  proof_phase_driver_probe_log="$out_dir/.proof_phase_driver.${label}.log"
  proof_phase_driver_probe_stamp="$out_dir/.proof_phase_driver.${label}.compile_stamp.txt"
  proof_phase_driver_probe_obj="$out_dir/.proof_phase_driver.${label}.o"
  proof_phase_driver_probe_error=""
  proof_phase_driver_probe_published="0"

  rm -f "$proof_phase_driver_probe_log" "$proof_phase_driver_probe_stamp" "$proof_phase_driver_probe_obj"

  if [ "$cand" = "" ] || [ ! -x "$cand" ]; then
    proof_phase_driver_probe_reason="missing"
    return 1
  fi

  if proof_phase_driver_published_surface_ok "$cand"; then
    proof_phase_driver_probe_published="1"
  fi

  set +e
  if [ "$sidecar_compiler" != "" ]; then
    sidecar_mode="$(proof_phase_driver_default_sidecar_mode)"
    sidecar_bundle="$(proof_phase_driver_default_sidecar_bundle)"
    sidecar_child_mode="$(proof_phase_driver_sidecar_child_mode "$sidecar_compiler")"
    case "$sidecar_mode:$sidecar_bundle:$sidecar_child_mode" in
      cheng:/*:cli|cheng:/*:outer_cli)
        ;;
      cli|outer_cli)
        ;;
      *)
        set -e
        proof_phase_driver_probe_reason="missing_sidecar_contract"
        return 1
        ;;
    esac
    if [ "$sidecar_child_mode" = "outer_cli" ]; then
      env \
        BACKEND_UIR_SIDECAR_MODE="$sidecar_mode" \
        BACKEND_UIR_SIDECAR_BUNDLE="$sidecar_bundle" \
        BACKEND_UIR_SIDECAR_COMPILER="$sidecar_compiler" \
        BACKEND_UIR_SIDECAR_CHILD_MODE="$sidecar_child_mode" \
        BACKEND_UIR_SIDECAR_OUTER_COMPILER="$cand" \
        BACKEND_UIR_SIDECAR_DISABLE=0 \
        BACKEND_UIR_PREFER_SIDECAR=1 \
        BACKEND_UIR_FORCE_SIDECAR=1 \
        BACKEND_INTERNAL_ALLOW_EMIT_OBJ=1 \
        BACKEND_COMPILE_STAMP_OUT="$proof_phase_driver_probe_stamp" \
        UIR_PROFILE=0 \
        BORROW_IR=mir \
        GENERIC_LOWERING=mir_hybrid \
        GENERIC_MODE=dict \
        GENERIC_SPEC_BUDGET=0 \
        STAGE1_SEM_FIXED_0=0 \
        STAGE1_OWNERSHIP_FIXED_0=0 \
        BACKEND_EMIT=obj \
        BACKEND_TARGET="$target" \
        BACKEND_FRONTEND=stage1 \
        BACKEND_INPUT="$fixture" \
        BACKEND_OUTPUT="$proof_phase_driver_probe_obj" \
        "$cand" >"$proof_phase_driver_probe_log" 2>&1
      proof_phase_driver_probe_rc="$?"
    else
      env \
        BACKEND_UIR_SIDECAR_MODE="$sidecar_mode" \
        BACKEND_UIR_SIDECAR_BUNDLE="$sidecar_bundle" \
        BACKEND_UIR_SIDECAR_COMPILER="$sidecar_compiler" \
        BACKEND_UIR_SIDECAR_CHILD_MODE="$sidecar_child_mode" \
        BACKEND_UIR_SIDECAR_DISABLE=0 \
        BACKEND_UIR_PREFER_SIDECAR=1 \
        BACKEND_UIR_FORCE_SIDECAR=1 \
        BACKEND_INTERNAL_ALLOW_EMIT_OBJ=1 \
        BACKEND_COMPILE_STAMP_OUT="$proof_phase_driver_probe_stamp" \
        UIR_PROFILE=0 \
        BORROW_IR=mir \
        GENERIC_LOWERING=mir_hybrid \
        GENERIC_MODE=dict \
        GENERIC_SPEC_BUDGET=0 \
        STAGE1_SEM_FIXED_0=0 \
        STAGE1_OWNERSHIP_FIXED_0=0 \
        BACKEND_EMIT=obj \
        BACKEND_TARGET="$target" \
        BACKEND_FRONTEND=stage1 \
        BACKEND_INPUT="$fixture" \
        BACKEND_OUTPUT="$proof_phase_driver_probe_obj" \
        "$cand" >"$proof_phase_driver_probe_log" 2>&1
      proof_phase_driver_probe_rc="$?"
    fi
  else
    env \
      BACKEND_INTERNAL_ALLOW_EMIT_OBJ=1 \
      BACKEND_COMPILE_STAMP_OUT="$proof_phase_driver_probe_stamp" \
      UIR_PROFILE=0 \
      BORROW_IR=mir \
      GENERIC_LOWERING=mir_hybrid \
      GENERIC_MODE=dict \
      GENERIC_SPEC_BUDGET=0 \
      STAGE1_SEM_FIXED_0=0 \
      STAGE1_OWNERSHIP_FIXED_0=0 \
      BACKEND_EMIT=obj \
      BACKEND_TARGET="$target" \
      BACKEND_FRONTEND=stage1 \
      BACKEND_INPUT="$fixture" \
      BACKEND_OUTPUT="$proof_phase_driver_probe_obj" \
      "$cand" >"$proof_phase_driver_probe_log" 2>&1
    proof_phase_driver_probe_rc="$?"
  fi
  set -e

  proof_phase_driver_probe_effective="$(proof_phase_driver_extract_stamp_field "$proof_phase_driver_probe_stamp" "stage1_skip_ownership_effective")"
  proof_phase_driver_probe_default="$(proof_phase_driver_extract_stamp_field "$proof_phase_driver_probe_stamp" "stage1_skip_ownership_default")"
  proof_phase_driver_probe_error="$(proof_phase_driver_first_error "$proof_phase_driver_probe_log")"

  if [ "$proof_phase_driver_probe_rc" = "0" ] &&
     [ "$proof_phase_driver_probe_effective" = "0" ] &&
     [ "$proof_phase_driver_probe_default" = "0" ]; then
    proof_phase_driver_probe_reason="usable"
    return 0
  fi

  if [ "$proof_phase_driver_probe_effective" = "0" ] &&
     rg -q "Cannot write while borrowed" "$proof_phase_driver_probe_log"; then
    proof_phase_driver_probe_reason="borrow_regression"
    return 1
  fi
  if [ "$proof_phase_driver_probe_effective" = "0" ]; then
    proof_phase_driver_probe_reason="ownership_on_compile_failed"
    return 1
  fi
  if [ "$proof_phase_driver_probe_effective" = "1" ] ||
     [ "$proof_phase_driver_probe_default" = "1" ]; then
    proof_phase_driver_probe_reason="phase_off"
    return 1
  fi
  if rg -q "invalid emit mode" "$proof_phase_driver_probe_log"; then
    proof_phase_driver_probe_reason="emit_obj_blocked"
    return 1
  fi
  if rg -q "invalid GENERIC_LOWERING" "$proof_phase_driver_probe_log"; then
    proof_phase_driver_probe_reason="generic_lowering_incompatible"
    return 1
  fi
  proof_phase_driver_probe_reason="unknown"
  return 1
}

proof_phase_driver_pick() {
  gate_name="$1"
  canonical_driver="$2"
  target="$3"
  out_dir="$4"
  fixture="$5"

  proof_phase_driver_path=""
  proof_phase_driver_surface=""
  proof_phase_driver_env=""
  proof_phase_driver_sidecar_compiler=""
  proof_phase_driver_summary=""
  proof_phase_driver_proof_path=""
  proof_phase_driver_proof_surface=""
  proof_phase_driver_proof_sidecar_compiler=""
  currentsrc_outer_driver="$root/artifacts/backend_selfhost_self_obj/probe_currentsrc_proof/cheng.stage2"
  currentsrc_stage3_witness="$root/artifacts/backend_selfhost_self_obj/probe_currentsrc_proof/cheng.stage3.witness"
  currentsrc_outer_driver_proof="$root/artifacts/backend_selfhost_self_obj/probe_currentsrc_proof/cheng.stage2.proof"
  currentsrc_stage3_witness_proof="$root/artifacts/backend_selfhost_self_obj/probe_currentsrc_proof/cheng.stage3.witness.proof"
  proof_surface_sidecar="$(proof_phase_driver_default_sidecar)"

  if [ ! -f "$fixture" ]; then
    echo "[$gate_name] proof phase preflight fixture missing: $fixture" 1>&2
    exit 1
  fi

  candidates_tmp="$(mktemp "${TMPDIR:-/tmp}/proof_phase_driver_candidates.XXXXXX")"
  if [ "$proof_phase_driver_proof_path" = "" ] && proof_phase_driver_published_surface_ok "$currentsrc_outer_driver"; then
    proof_phase_driver_proof_path="$currentsrc_outer_driver"
    proof_phase_driver_proof_surface="probe_currentsrc_proof"
    proof_phase_driver_proof_sidecar_compiler=""
  fi
  if [ "$proof_phase_driver_proof_path" = "" ] && proof_phase_driver_published_surface_ok "$currentsrc_stage3_witness"; then
    proof_phase_driver_proof_path="$currentsrc_stage3_witness"
    proof_phase_driver_proof_surface="probe_currentsrc_stage3_witness_proof"
    proof_phase_driver_proof_sidecar_compiler=""
  fi
  if [ "$proof_phase_driver_proof_path" = "" ] && proof_phase_driver_published_surface_ok "$currentsrc_outer_driver_proof"; then
    proof_phase_driver_proof_path="$currentsrc_outer_driver_proof"
    proof_phase_driver_proof_surface="probe_currentsrc_proof"
    proof_phase_driver_proof_sidecar_compiler=""
  fi
  if [ "$proof_phase_driver_proof_path" = "" ] && proof_phase_driver_published_surface_ok "$currentsrc_stage3_witness_proof"; then
    proof_phase_driver_proof_path="$currentsrc_stage3_witness_proof"
    proof_phase_driver_proof_surface="probe_currentsrc_stage3_witness_proof"
    proof_phase_driver_proof_sidecar_compiler=""
  fi
  selected_candidate="${BACKEND_PROOF_PHASE_DRIVER:-}"
  selected_label="env"
  selected_sidecar="${BACKEND_UIR_SIDECAR_COMPILER:-}"
  if [ "$selected_candidate" = "" ]; then
    selected_candidate="$currentsrc_outer_driver"
    selected_label="probe_currentsrc_proof"
    selected_sidecar="$proof_surface_sidecar"
  fi
  if [ "$selected_sidecar" = "" ] || [ ! -x "$selected_sidecar" ]; then
    echo "[$gate_name] missing strict proof sidecar compiler: ${selected_sidecar:-<unset>}" 1>&2
    exit 1
  fi
  if [ "$selected_candidate" = "" ] || [ ! -x "$selected_candidate" ]; then
    echo "[$gate_name] missing explicit/current-source proof phase driver: ${selected_candidate:-<unset>}" 1>&2
    exit 1
  fi
  {
    proof_phase_driver_append_candidate_line "$selected_candidate" "$selected_label" "$selected_sidecar"
  } >"$candidates_tmp"

  while IFS="$(printf '\t')" read -r cand label sidecar_compiler; do
    [ "$cand" = "" ] && continue
    proof_phase_driver_probe_candidate "$cand" "$label" "$sidecar_compiler" "$target" "$out_dir" "$fixture" || true
    entry="$label:rc=${proof_phase_driver_probe_rc:-missing}:effective=${proof_phase_driver_probe_effective:-}:default=${proof_phase_driver_probe_default:-}:reason=${proof_phase_driver_probe_reason:-}:path=$cand"
    if [ "$sidecar_compiler" != "" ]; then
      entry="$entry:sidecar_compiler=$sidecar_compiler"
    fi
    if [ "${proof_phase_driver_probe_error:-}" != "" ]; then
      entry="$entry:error=${proof_phase_driver_probe_error}"
    fi
    if [ "$proof_phase_driver_summary" = "" ]; then
      proof_phase_driver_summary="$entry"
    else
      proof_phase_driver_summary="$proof_phase_driver_summary
$entry"
    fi
    if [ "${proof_phase_driver_probe_published:-0}" = "1" ] && [ "${proof_phase_driver_proof_path:-}" = "" ]; then
      proof_phase_driver_proof_path="$cand"
      proof_phase_driver_proof_surface="$label"
      proof_phase_driver_proof_sidecar_compiler="$sidecar_compiler"
    fi
    if [ "${proof_phase_driver_probe_reason:-}" = "usable" ]; then
      proof_phase_driver_path="$cand"
      proof_phase_driver_surface="$label"
      proof_phase_driver_sidecar_compiler="$sidecar_compiler"
      if [ "${proof_phase_driver_proof_path:-}" = "" ]; then
        proof_phase_driver_proof_path="$cand"
        proof_phase_driver_proof_surface="$label"
        proof_phase_driver_proof_sidecar_compiler="$sidecar_compiler"
      fi
      if [ "$sidecar_compiler" != "" ]; then
        sidecar_child_mode="$(proof_phase_driver_sidecar_child_mode "$sidecar_compiler")"
        if [ "$sidecar_child_mode" = "outer_cli" ]; then
          proof_phase_driver_env="BACKEND_UIR_SIDECAR_COMPILER=$sidecar_compiler BACKEND_UIR_SIDECAR_CHILD_MODE=$sidecar_child_mode BACKEND_UIR_SIDECAR_OUTER_COMPILER=$cand"
        else
          proof_phase_driver_env="BACKEND_UIR_SIDECAR_COMPILER=$sidecar_compiler BACKEND_UIR_SIDECAR_CHILD_MODE=$sidecar_child_mode"
        fi
      fi
      break
    fi
  done <"$candidates_tmp"
  rm -f "$candidates_tmp"

  if [ "$proof_phase_driver_path" != "" ] && [ "${proof_phase_driver_proof_path:-}" = "" ]; then
    proof_phase_driver_proof_path="$proof_phase_driver_path"
    proof_phase_driver_proof_surface="$proof_phase_driver_surface"
    proof_phase_driver_proof_sidecar_compiler="$proof_phase_driver_sidecar_compiler"
  fi

  if [ "$proof_phase_driver_path" != "" ]; then
    return 0
  fi

  echo "[$gate_name] no usable ownership-on proof phase driver" 1>&2
  printf '%s\n' "$proof_phase_driver_summary" | while IFS= read -r line; do
    [ "$line" = "" ] && continue
    echo "  $line" 1>&2
  done
  if printf '%s\n' "$proof_phase_driver_summary" | rg -q 'reason=borrow_regression'; then
    echo "[$gate_name] ownership-on surfaces are blocked by compiler borrow regression" 1>&2
  fi
  exit 1
}
