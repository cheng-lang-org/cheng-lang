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
      if rg -q '^(sidecar_compiler|exec_fallback_outer_driver)=' "$meta"; then
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
  preferred_release="$root/dist/releases/2026-02-23T09_54_03Z_e84f22d_14/cheng"
  legacy_proofseed="$root/artifacts/backend_selfhost_self_obj/probe_proofseed14/cheng_stage0_proofseed14"
  if [ -x "$preferred_release" ]; then
    printf '%s\n' "$preferred_release"
    return 0
  fi
  if [ -x "$legacy_proofseed" ]; then
    printf '%s\n' "$legacy_proofseed"
    return 0
  fi
  printf '\n'
}

proof_phase_driver_append_archive_candidates() {
  current_release="$root/dist/releases/current/cheng"
  for cand in \
    "$root"/artifacts/backend_selfhost_self_obj/probe_*/cheng_stage0_* \
    "$root"/dist/releases/*/cheng
  do
    [ "$cand" = "$current_release" ] && continue
    [ -x "$cand" ] || continue
    parent="$(basename "$(dirname "$cand")")"
    label="archive_$(proof_phase_driver_label_safe "$parent")"
    proof_phase_driver_append_candidate_line "$cand" "$label" ""
  done
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
    env \
      BACKEND_UIR_SIDECAR_COMPILER="$sidecar_compiler" \
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
  strict_outer_driver="$root/artifacts/backend_selfhost_self_obj/probe_prod.strict.noreuse/cheng.stage2"
  strict_stage3_witness="$root/artifacts/backend_selfhost_self_obj/probe_prod.strict.noreuse/cheng.stage3.witness"
  strict_outer_driver_proof="$root/artifacts/backend_selfhost_self_obj/probe_prod.strict.noreuse/cheng.stage2.proof"
  strict_stage3_witness_proof="$root/artifacts/backend_selfhost_self_obj/probe_prod.strict.noreuse/cheng.stage3.witness.proof"
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
  if [ "$proof_phase_driver_proof_path" = "" ] && proof_phase_driver_published_surface_ok "$strict_outer_driver_proof"; then
    proof_phase_driver_proof_path="$strict_outer_driver_proof"
    proof_phase_driver_proof_surface="probe_prod_strict_noreuse_proof"
    proof_phase_driver_proof_sidecar_compiler=""
  fi
  if [ "$proof_phase_driver_proof_path" = "" ] && proof_phase_driver_published_surface_ok "$strict_stage3_witness_proof"; then
    proof_phase_driver_proof_path="$strict_stage3_witness_proof"
    proof_phase_driver_proof_surface="probe_prod_strict_stage3_witness_proof"
    proof_phase_driver_proof_sidecar_compiler=""
  fi
  {
    proof_phase_driver_append_candidate_line "${BACKEND_PROOF_PHASE_DRIVER:-}" "env" "${BACKEND_UIR_SIDECAR_COMPILER:-}"
    proof_phase_driver_append_candidate_line "$currentsrc_outer_driver" "probe_currentsrc_proof" ""
    proof_phase_driver_append_candidate_line "$currentsrc_stage3_witness" "probe_currentsrc_stage3_witness_proof" ""
    proof_phase_driver_append_candidate_line "$currentsrc_outer_driver_proof" "probe_currentsrc_proof" ""
    proof_phase_driver_append_candidate_line "$currentsrc_stage3_witness_proof" "probe_currentsrc_stage3_witness_proof" ""
    proof_phase_driver_append_candidate_line "$strict_outer_driver_proof" "probe_prod_strict_noreuse_proof" ""
    proof_phase_driver_append_candidate_line "$strict_stage3_witness_proof" "probe_prod_strict_stage3_witness_proof" ""
    proof_phase_driver_append_candidate_line "$strict_outer_driver" "probe_prod_strict_noreuse_proofrelease" "$proof_surface_sidecar"
    proof_phase_driver_append_candidate_line "$strict_stage3_witness" "probe_prod_strict_stage3_witness_proofrelease" "$proof_surface_sidecar"
    proof_phase_driver_append_candidate_line "$strict_outer_driver" "probe_prod_strict_noreuse" "$root/dist/releases/current/cheng"
    proof_phase_driver_append_candidate_line "$strict_stage3_witness" "probe_prod_strict_stage3_witness" "$root/dist/releases/current/cheng"
    proof_phase_driver_append_archive_candidates
    proof_phase_driver_append_candidate_line "$root/dist/releases/current/cheng" "release_current" ""
    proof_phase_driver_append_candidate_line "$root/artifacts/backend_selfhost_self_obj/probe_prod.noreuse/cheng.stage2" "probe_prod_noreuse" ""
    proof_phase_driver_append_candidate_line "$root/artifacts/backend_selfhost_self_obj/probe_prod.noreuse.parallel_perf.serial/cheng.stage2" "probe_prod_parallel_serial" ""
    proof_phase_driver_append_candidate_line "$root/artifacts/backend_selfhost_self_obj/cheng_stage0_default" "stage0_default" ""
    proof_phase_driver_append_candidate_line "$canonical_driver" "canonical_driver" ""
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
        proof_phase_driver_env="BACKEND_UIR_SIDECAR_COMPILER=$sidecar_compiler"
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
