#!/usr/bin/env sh
. "$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)/env_prefix_bridge.sh"
set -eu
(set -o pipefail) 2>/dev/null && set -o pipefail

usage() {
  cat <<'EOF'
  Usage:
  src/tooling/backend_prod_closure.sh [--no-validate] [--debug] [--no-debug] [--no-ffi] [--no-determinism-strict]
                                     [--no-opt] [--no-exe-determinism]
                                      [--no-opt2] [--no-opt3] [--uir-aggressive] [--no-uir-aggressive]
                                      [--uir-stability] [--no-uir-stability]
                                      [--uir-aggressive-iters:<n>]
                                      [--uir-opt2-iters:<n>] [--uir-opt3-iters:<n>] [--uir-opt3-cleanup-iters:<n>]
                                      [--uir-cfg-canon-iters:<n>] [--uir-simd] [--no-uir-simd]
                                      [--uir-simd-max-width:<n>] [--uir-simd-policy:<autovec|copy|loop|slp|none>]
                                      [--uir-inline-iters:<n>]
                                      [--no-sanitizer]
                                     [--stress|--no-stress] [--no-bundle]
                                     [--no-sign]
                                     [--release-rollback-drill|--no-release-rollback-drill]
                                     [--no-ssa]
                                     [--selfhost|--no-selfhost]
                                     [--multi-perf|--no-multi-perf]
                                     [--selfhost-parallel-perf|--no-selfhost-parallel-perf]
                                     [--driver-selfbuild-smoke|--no-driver-selfbuild-smoke]
                                     [--self-linker-gates|--no-self-linker-gates]
                                     [--selfhost-fast|--selfhost-strict]
                                     [--selfhost-strict-gate|--no-selfhost-strict-gate]
                                     [--stage0-no-compat-gate|--no-stage0-no-compat-gate]
                                     [--selfhost-strict-noreuse-probe|--no-selfhost-strict-noreuse-probe]
                                     [--fullchain|--no-fullchain]
                                     [--seed:<path>] [--seed-id:<id>] [--seed-tar:<path>] [--require-seed]
                                     [--no-mm]
                                     [--no-publish]
                                     [--strict] [--allow-skip]
                                     [--manifest:<path>] [--bundle:<path>]

Notes:
  - Runs the self-hosted backend production closure (includes best-effort target matrix checks).
  - Default includes backend validation (BACKEND_VALIDATE=1) and emits a release manifest.
  - Default enables closedloop fullspec gate (`BACKEND_RUN_FULLSPEC=1`); set `BACKEND_RUN_FULLSPEC=0`
    only for temporary local triage.
  - `--no-ssa` disables the UIR generic-mode compare gate (dict vs hybrid) in verify_backend_ssa.sh.
  - Gate timeout defaults to 60s (`BACKEND_PROD_GATE_TIMEOUT`; set 0 to disable).
  - Selfhost bootstrap timeout defaults to 120s (`BACKEND_PROD_SELFHOST_TIMEOUT`).
  - Selfhost bootstrap gate timeout defaults to selfhost timeout (`BACKEND_PROD_SELFHOST_GATE_TIMEOUT`).
  - Timeout diagnostics are enabled by default (`BACKEND_PROD_TIMEOUT_DIAG=1`).
  - Selfhost performance regression gate is enabled by default (`BACKEND_RUN_SELFHOST_PERF=1`).
  - Selfhost performance baseline defaults to `src/tooling/selfhost_perf_baseline.env` (`SELFHOST_PERF_BASELINE`).
  - Multi compile performance regression gate is enabled by default (`BACKEND_RUN_MULTI_PERF=1`).
  - Multi perf baseline defaults to `src/tooling/multi_perf_baseline.env` (`MULTI_PERF_BASELINE`).
  - Selfhost serial-vs-parallel perf gate is enabled by default (`BACKEND_RUN_SELFHOST_PARALLEL_PERF=1`).
  - Selfhost parallel perf timeout defaults to strict probe timeout (`BACKEND_PROD_SELFHOST_PARALLEL_PERF_TIMEOUT`).
  - Selfhost parallel perf gate timeout defaults to 190s (`BACKEND_PROD_SELFHOST_PARALLEL_PERF_GATE_TIMEOUT`).
  - Driver selfbuild smoke gate is enabled by default (`BACKEND_RUN_DRIVER_SELFBUILD_SMOKE=1`).
  - Driver selfbuild smoke gate timeout defaults to 60s (`BACKEND_PROD_DRIVER_SELFBUILD_SMOKE_TIMEOUT`).
  - Driver selfbuild smoke build timeout defaults to 55s (`BACKEND_PROD_DRIVER_SELFBUILD_SMOKE_BUILD_TIMEOUT`).
  - Self-linker专项 gate 默认开启（`BACKEND_RUN_SELF_LINKER_GATES=1`），并以 required 方式覆盖 Linux/Windows smoke。
  - Release publish+rollback 演练 gate 默认开启（`BACKEND_RUN_RELEASE_ROLLBACK_DRILL=1`）。
  - Fullchain bootstrap gate is opt-in (`--fullchain` or `BACKEND_RUN_FULLCHAIN=1`).
  - Default is strict: any step that exits with skip code (2) fails the closure.
  - Use `--allow-skip` to permit optional steps to skip.
  - `--require-seed` requires explicit `--seed`/`--seed-id`/`--seed-tar`.
  - Publish path requires explicit seed (`--seed`/`--seed-id`/`--seed-tar`).
  - Selfhost bootstrap is opt-in (`--selfhost` or `BACKEND_RUN_SELFHOST=1`).
  - `--no-publish` 默认启用稳定收口参数集（`BACKEND_PROD_NO_PUBLISH_STABLE_PROFILE=1`）：
    自动关闭 `determinism_strict/opt/opt2/opt3/uir_stability/ssa/ffi/sanitizer/debug/exe_determinism/multi_perf`
    等可选波动 gate，仅保留 required 收口链路；可设 `BACKEND_PROD_NO_PUBLISH_STABLE_PROFILE=0` 恢复完整可选 gate。
  - Default selfhost mode is `fast` when selfhost is enabled (`BACKEND_PROD_SELFHOST_MODE` / `SELF_OBJ_BOOTSTRAP_MODE`).
  - `--uir-aggressive` runs the opt3 fixture pack once in aggressive full-pass mode (`UIR_AGGRESSIVE=1`).
    Default from `BACKEND_PROD_UIR_AGGRESSIVE` (fallback `0`), and
    `BACKEND_PROD_UIR_FULL_ITERS` controls aggressive pass rounds (default `2`).
  - `--uir-stability` enables repeated-mode/object determinism checks for UIR using `verify_backend_uir_stability.sh`.
    It is enabled by default (`BACKEND_RUN_UIR_STABILITY=1`; `--no-uir-stability` to disable).
  - `--uir-aggressive-iters:<n>` overrides `BACKEND_PROD_UIR_FULL_ITERS` (accepted range `1..16`) for this run.
  - `--uir-opt2-iters:<n>` controls `UIR_OPT2_ITERS` (default `5`).
  - `--uir-opt3-iters:<n>` controls `UIR_OPT3_ITERS` (default `4`).
  - `--uir-opt3-cleanup-iters:<n>` controls `UIR_OPT3_CLEANUP_ITERS` (default `3`).
  - `--uir-cfg-canon-iters:<n>` controls `UIR_CFG_CANON_ITERS` (default `1`).
  - `--uir-simd` / `--no-uir-simd` toggles `UIR_SIMD` (default `auto`: enabled when `opt3` gate runs).
  - `--uir-simd-max-width:<n>` sets `UIR_SIMD_MAX_WIDTH` (default `0`).
  - `--uir-simd-policy:<policy>` sets `UIR_SIMD_POLICY` (default `autovec`).
  - `--uir-inline-iters:<n>` controls `UIR_INLINE_ITERS` (default `4`).
  - `BACKEND_PROD_TARGET` controls target triple for `backend.opt/opt2/opt3/simd/uir_stability`.
    When unset, non-darwin ambient `BACKEND_TARGET` is ignored and fallback target is `arm64-apple-darwin`.
  - `BACKEND_OPT_DRIVER` can override `backend.opt/opt2/multi_lto/multi_perf/opt3/simd/uir_stability`
    gate driver without changing the main required-gates driver.
  - Production closure uses explicit linker per gate:
    dev-path gates pin `BACKEND_LINKER=self`,
    release-path gate pins `BACKEND_LINKER=system`.
  - Production closure now enforces `backend.no_legacy_refs` / `backend.profile_schema` /
    `backend.rawptr_contract` / `backend.rawptr_surface_forbid` / `backend.ffi_slice_shim` / `backend.ffi_outptr_tuple` / `backend.ffi_handle_sandbox` / `backend.ffi_borrow_bridge` / `backend.mem_contract` / `backend.dod_contract` / `backend.mem_image_core` / `backend.mem_exe_emit` /
    `backend.profile_baseline` / `backend.dual_track` / `backend.linkerless_dev` / `backend.hotpatch_meta` /
    `backend.hotpatch_inplace` / `backend.incr_patch_fastpath` / `backend.mem_patch_regression` /
    `backend.hotpatch` / `backend.release_system_link` / `backend.plugin_isolation` / `backend.linker_abi_core` /
    `backend.self_linker.elf` / `backend.self_linker.coff` /
    `backend.noptr_exemption_scope` / `backend.no_obj_artifacts` as required gates.
  - `BACKEND_LINKERLESS_DRIVER` can override only `backend.linkerless_dev` gate driver.
  - Strict selfhost gate is enabled by default when selfhost is enabled (`BACKEND_RUN_SELFHOST_STRICT=0` to disable).
  - Stage0 no-compat gate is enabled and blocking by default when selfhost is enabled (`BACKEND_RUN_STAGE0_NO_COMPAT_GATE=0` to disable).
  - Stage0 no-compat gate timeout defaults to 60s (`BACKEND_PROD_STAGE0_NO_COMPAT_GATE_TIMEOUT`).
  - Strict no-reuse probe is enabled and blocking by default when selfhost is enabled (`BACKEND_RUN_SELFHOST_STRICT_NOREUSE_PROBE=0` to disable).
  - Strict no-reuse probe defaults to `gate=110s` and `probe=90s`; override with
    `BACKEND_PROD_SELFHOST_STRICT_NOREUSE_GATE_TIMEOUT` / `BACKEND_PROD_SELFHOST_STRICT_NOREUSE_PROBE_TIMEOUT`.
  - Alias-off strict no-reuse probe supports latency profile knobs:
    `BACKEND_PROD_SELFHOST_STRICT_NOREUSE_ALIAS_OFF_FAST` (default `1`),
    `BACKEND_PROD_SELFHOST_STRICT_NOREUSE_REQUIRE_RUNNABLE`,
    `BACKEND_PROD_SELFHOST_STRICT_NOREUSE_SKIP_SMOKE`,
    `BACKEND_PROD_SELFHOST_STRICT_NOREUSE_VALIDATE`.
EOF
}

prod_closure_normalize_int() {
  raw="$1"
  default="$2"
  minv="$3"
  maxv="$4"
  case "$raw" in
    ''|*[!0-9]*)
      echo "$default"
      return
      ;;
  esac
  if [ "$raw" -lt "$minv" ]; then
    echo "$minv"
    return
  fi
  if [ "$raw" -gt "$maxv" ]; then
    echo "$maxv"
    return
  fi
  echo "$raw"
}

validate="1"
run_debug=""
run_ffi="1"
run_det_strict="1"
run_opt="1"
run_opt2="1"
run_opt3="1"
run_ssa="1"
run_selfhost=""
if [ "${BACKEND_RUN_SELFHOST:-0}" = "1" ]; then
  run_selfhost="1"
fi
run_fullchain=""
if [ "${BACKEND_RUN_FULLCHAIN:-}" = "1" ]; then
  run_fullchain="1"
fi
seed=""
seed_id=""
seed_tar=""
require_seed="0"
run_obj=""
run_obj_det=""
run_exe_det="1"
run_sanitizer="1"
run_uir_stability="${BACKEND_RUN_UIR_STABILITY:-1}"
run_uir_aggressive="${BACKEND_PROD_UIR_AGGRESSIVE:-}"
uir_aggressive_iters="${BACKEND_PROD_UIR_FULL_ITERS:-2}"
uir_opt2_iters="${BACKEND_PROD_UIR_OPT2_ITERS:-5}"
uir_opt3_iters="${BACKEND_PROD_UIR_OPT3_ITERS:-4}"
uir_opt3_cleanup_iters="${BACKEND_PROD_UIR_OPT3_CLEANUP_ITERS:-3}"
uir_cfg_canon_iters="${BACKEND_PROD_UIR_CFG_CANON_ITERS:-1}"
uir_inline_iters="${BACKEND_PROD_UIR_INLINE_ITERS:-4}"
uir_simd="${UIR_SIMD:-auto}"
uir_simd_max_width="${UIR_SIMD_MAX_WIDTH:-0}"
uir_simd_policy="${UIR_SIMD_POLICY:-autovec}"
prod_target="${BACKEND_PROD_TARGET:-}"
run_stress=""
if [ "${BACKEND_RUN_STRESS:-}" = "1" ]; then
  run_stress="1"
fi
run_bundle="1"
run_sign="1"
run_release_rollback_drill="${BACKEND_RUN_RELEASE_ROLLBACK_DRILL:-1}"
run_mm="1"
run_publish="1"
allow_skip=""
manifest="artifacts/backend_prod/release_manifest.json"
bundle="artifacts/backend_prod/backend_release.tar.gz"
debug_explicit="0"
selfhost_timeout="${BACKEND_PROD_SELFHOST_TIMEOUT:-120}"
selfhost_gate_timeout="${BACKEND_PROD_GATE_TIMEOUT:-60}"
selfhost_bootstrap_gate_timeout="${BACKEND_PROD_SELFHOST_GATE_TIMEOUT:-$selfhost_timeout}"
stage0_no_compat_gate_timeout="${BACKEND_PROD_STAGE0_NO_COMPAT_GATE_TIMEOUT:-60}"
selfhost_strict_noreuse_gate_timeout="${BACKEND_PROD_SELFHOST_STRICT_NOREUSE_GATE_TIMEOUT:-110}"
selfhost_strict_noreuse_probe_timeout="${BACKEND_PROD_SELFHOST_STRICT_NOREUSE_PROBE_TIMEOUT:-105}"
selfhost_parallel_perf_timeout="${BACKEND_PROD_SELFHOST_PARALLEL_PERF_TIMEOUT:-$selfhost_strict_noreuse_probe_timeout}"
selfhost_parallel_perf_gate_timeout="${BACKEND_PROD_SELFHOST_PARALLEL_PERF_GATE_TIMEOUT:-190}"
driver_selfbuild_smoke_gate_timeout="${BACKEND_PROD_DRIVER_SELFBUILD_SMOKE_TIMEOUT:-60}"
driver_selfbuild_smoke_build_timeout="${BACKEND_PROD_DRIVER_SELFBUILD_SMOKE_BUILD_TIMEOUT:-55}"
selfhost_reuse="${BACKEND_PROD_SELFHOST_REUSE:-${SELF_OBJ_BOOTSTRAP_REUSE:-1}}"
selfhost_session="${BACKEND_PROD_SELFHOST_SESSION:-${SELF_OBJ_BOOTSTRAP_SESSION:-prod}}"
selfhost_mode="${BACKEND_PROD_SELFHOST_MODE:-${SELF_OBJ_BOOTSTRAP_MODE:-fast}}"
run_selfhost_strict="${BACKEND_RUN_SELFHOST_STRICT:-1}"
run_stage0_no_compat_gate="${BACKEND_RUN_STAGE0_NO_COMPAT_GATE:-1}"
run_selfhost_strict_noreuse_probe="${BACKEND_RUN_SELFHOST_STRICT_NOREUSE_PROBE:-1}"
run_selfhost_parallel_perf="${BACKEND_RUN_SELFHOST_PARALLEL_PERF:-1}"
run_driver_selfbuild_smoke="${BACKEND_RUN_DRIVER_SELFBUILD_SMOKE:-1}"
run_self_linker_gates="${BACKEND_RUN_SELF_LINKER_GATES:-1}"
run_selfhost_perf="${BACKEND_RUN_SELFHOST_PERF:-1}"
run_multi_perf="${BACKEND_RUN_MULTI_PERF:-1}"
no_publish_stable_profile="${BACKEND_PROD_NO_PUBLISH_STABLE_PROFILE:-1}"
selfhost_perf_use_strict_session="${BACKEND_SELFHOST_PERF_USE_STRICT_SESSION:-1}"
timeout_diag_enabled="${BACKEND_PROD_TIMEOUT_DIAG:-1}"
timeout_diag_seconds="${BACKEND_PROD_TIMEOUT_DIAG_SECONDS:-5}"
timeout_diag_dir="${BACKEND_PROD_TIMEOUT_DIAG_DIR:-chengcache/backend_timeout_diag}"
timeout_diag_tool="${BACKEND_PROD_TIMEOUT_DIAG_TOOL:-sample}"
timeout_diag_summary_enabled="${BACKEND_PROD_TIMEOUT_DIAG_SUMMARY:-1}"
timeout_diag_summary_top="${BACKEND_PROD_TIMEOUT_DIAG_SUMMARY_TOP:-12}"
timeout_diag_last_file=""

while [ "${1:-}" != "" ]; do
  case "$1" in
    --no-validate)
      validate=""
      ;;
    --debug)
      run_debug="1"
      debug_explicit="1"
      ;;
    --no-debug)
      run_debug=""
      debug_explicit="1"
      ;;
    --no-ffi)
      run_ffi=""
      ;;
    --no-determinism-strict)
      run_det_strict=""
      ;;
    --no-opt)
      run_opt=""
      ;;
    --no-opt2)
      run_opt2=""
      run_opt3=""
      ;;
    --no-opt3)
      run_opt3=""
      run_uir_aggressive=""
      ;;
    --uir-aggressive)
      run_uir_aggressive="1"
      ;;
    --uir-aggressive-iters:*)
      run_uir_aggressive="1"
      uir_aggressive_iters="${1#--uir-aggressive-iters:}"
      ;;
    --uir-opt2-iters:*)
      uir_opt2_iters="${1#--uir-opt2-iters:}"
      ;;
    --uir-opt3-iters:*)
      uir_opt3_iters="${1#--uir-opt3-iters:}"
      ;;
    --uir-opt3-cleanup-iters:*)
      uir_opt3_cleanup_iters="${1#--uir-opt3-cleanup-iters:}"
      ;;
    --uir-cfg-canon-iters:*)
      uir_cfg_canon_iters="${1#--uir-cfg-canon-iters:}"
      ;;
    --uir-inline-iters:*)
      uir_inline_iters="${1#--uir-inline-iters:}"
      ;;
    --uir-simd)
      uir_simd="1"
      ;;
    --no-uir-simd)
      uir_simd="0"
      ;;
    --uir-simd-max-width:*)
      uir_simd_max_width="${1#--uir-simd-max-width:}"
      ;;
    --uir-simd-policy:*)
      uir_simd_policy="${1#--uir-simd-policy:}"
      ;;
    --no-uir-aggressive)
      run_uir_aggressive=""
      ;;
    --uir-stability)
      run_uir_stability="1"
      ;;
    --no-uir-stability)
      run_uir_stability=""
      ;;
    --no-ssa)
      run_ssa=""
      ;;
    --selfhost)
      run_selfhost="1"
      ;;
    --no-selfhost)
      run_selfhost=""
      ;;
    --multi-perf)
      run_multi_perf="1"
      ;;
    --no-multi-perf)
      run_multi_perf="0"
      ;;
    --selfhost-fast)
      run_selfhost="1"
      selfhost_mode="fast"
      ;;
    --selfhost-strict)
      run_selfhost="1"
      selfhost_mode="strict"
      ;;
    --selfhost-strict-gate)
      run_selfhost="1"
      run_selfhost_strict="1"
      ;;
    --no-selfhost-strict-gate)
      run_selfhost_strict="0"
      ;;
    --stage0-no-compat-gate)
      run_selfhost="1"
      run_stage0_no_compat_gate="1"
      ;;
    --no-stage0-no-compat-gate)
      run_stage0_no_compat_gate="0"
      ;;
    --selfhost-strict-noreuse-probe)
      run_selfhost="1"
      run_selfhost_strict_noreuse_probe="1"
      ;;
    --no-selfhost-strict-noreuse-probe)
      run_selfhost_strict_noreuse_probe="0"
      ;;
    --selfhost-parallel-perf)
      run_selfhost="1"
      run_selfhost_parallel_perf="1"
      ;;
    --no-selfhost-parallel-perf)
      run_selfhost_parallel_perf="0"
      ;;
    --driver-selfbuild-smoke)
      run_selfhost="1"
      run_driver_selfbuild_smoke="1"
      ;;
    --no-driver-selfbuild-smoke)
      run_driver_selfbuild_smoke="0"
      ;;
    --self-linker-gates)
      run_self_linker_gates="1"
      ;;
    --no-self-linker-gates)
      run_self_linker_gates="0"
      ;;
    --no-fullchain)
      run_fullchain=""
      ;;
    --fullchain)
      run_fullchain="1"
      ;;
    --seed:*)
      seed="${1#--seed:}"
      ;;
    --seed-id:*)
      seed_id="${1#--seed-id:}"
      ;;
    --seed-tar:*)
      seed_tar="${1#--seed-tar:}"
      ;;
    --require-seed)
      require_seed="1"
      ;;
    --only-self-obj-bootstrap|--no-obj|--no-obj-determinism|--no-self-obj-writer)
      echo "[backend_prod_closure] note: $1 is deprecated and ignored (obj/self-obj flow removed)" 1>&2
      ;;
    --no-exe-determinism)
      run_exe_det=""
      ;;
    --no-sanitizer)
      run_sanitizer=""
      ;;
    --no-stress)
      run_stress=""
      ;;
    --stress)
      run_stress="1"
      ;;
    --no-bundle)
      run_bundle=""
      ;;
    --no-sign)
      run_sign=""
      ;;
    --release-rollback-drill)
      run_release_rollback_drill="1"
      ;;
    --no-release-rollback-drill)
      run_release_rollback_drill=""
      ;;
    --no-mm)
      run_mm=""
      ;;
    --no-publish)
      run_publish=""
      ;;
    --strict)
      allow_skip=""
      ;;
    --allow-skip)
      allow_skip="1"
      ;;
    --manifest:*)
      manifest="${1#--manifest:}"
      ;;
    --bundle:*)
      bundle="${1#--bundle:}"
      ;;
    --help|-h)
      usage
      exit 0
      ;;
    *)
      echo "[Error] unknown arg: $1" 1>&2
      usage
      exit 2
      ;;
  esac
  shift || true
done

if [ "$selfhost_mode" != "strict" ] && [ "$selfhost_mode" != "fast" ]; then
  echo "[Error] invalid selfhost mode: $selfhost_mode (expected strict|fast)" 1>&2
  exit 2
fi

case "$no_publish_stable_profile" in
  0|1)
    ;;
  *)
    echo "[Error] invalid BACKEND_PROD_NO_PUBLISH_STABLE_PROFILE: $no_publish_stable_profile (expected 0|1)" 1>&2
    exit 2
    ;;
esac

if [ "$run_publish" = "" ] && [ "$no_publish_stable_profile" = "1" ]; then
  run_det_strict=""
  run_opt=""
  run_opt2=""
  run_opt3=""
  run_uir_aggressive=""
  run_uir_stability=""
  run_ssa=""
  run_ffi=""
  run_sanitizer=""
  run_exe_det=""
  run_multi_perf="0"
  if [ "$debug_explicit" = "0" ]; then
    run_debug=""
  fi
fi

root="$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)"
cd "$root"

# Keep local backend driver for the whole closure; many sub-gates rely on it.
export CLEAN_CHENG_LOCAL=0
if [ "${BACKEND_DRIVER_ALLOW_FALLBACK:-}" = "" ]; then
  export BACKEND_DRIVER_ALLOW_FALLBACK=0
fi

if [ "$debug_explicit" = "0" ] && [ "$(uname -s 2>/dev/null || echo unknown)" = "Darwin" ] && ! { [ "$run_publish" = "" ] && [ "$no_publish_stable_profile" = "1" ]; }; then
  run_debug="1"
fi

backend_driver_explicit="0"
if [ "${BACKEND_DRIVER:-}" != "" ]; then
  backend_driver_explicit="1"
fi

seed_from_tar() {
  tar_path="$1"
  if [ ! -f "$tar_path" ]; then
    echo "[Error] seed tar not found: $tar_path" 1>&2
    exit 2
  fi
  out_dir="chengcache/backend_seed_prod_$$"
  mkdir -p "$out_dir"
  tar -xzf "$tar_path" -C "$out_dir"
  extracted="$out_dir/cheng"
  if [ ! -f "$extracted" ]; then
    echo "[Error] seed tar missing cheng: $tar_path" 1>&2
    exit 2
  fi
  chmod +x "$extracted" 2>/dev/null || true
  printf "%s\n" "$extracted"
}

seed_path=""
seed_requested="0"
if [ "$require_seed" = "1" ] || [ "$seed" != "" ] || [ "$seed_id" != "" ] || [ "$seed_tar" != "" ]; then
  seed_requested="1"
fi

if [ "$seed_requested" = "1" ]; then
  if [ "$seed" != "" ]; then
    seed_path="$seed"
  elif [ "$seed_tar" != "" ]; then
    seed_path="$(seed_from_tar "$seed_tar")"
  elif [ "$seed_id" != "" ]; then
    try_tar="dist/releases/$seed_id/backend_release.tar.gz"
    if [ ! -f "$try_tar" ]; then
      echo "[Error] missing seed tar for --seed-id:$seed_id ($try_tar)" 1>&2
      exit 2
    fi
    seed_tar="$try_tar"
    seed_path="$(seed_from_tar "$seed_tar")"
  elif [ "$require_seed" = "1" ]; then
    echo "[Error] --require-seed requires explicit --seed/--seed-id/--seed-tar" 1>&2
    exit 2
  fi

  if [ "$seed_path" = "" ] && [ "$require_seed" = "1" ]; then
    echo "[Error] missing seed: pass --seed/--seed-id/--seed-tar" 1>&2
    exit 2
  fi

  if [ "$seed_path" != "" ]; then
    case "$seed_path" in
      /*) ;;
      *) seed_path="$root/$seed_path" ;;
    esac
    if [ ! -x "$seed_path" ]; then
      echo "[Error] seed driver is not executable: $seed_path" 1>&2
      exit 2
    fi
  fi
fi

if [ "$run_publish" != "" ] && [ "$seed_path" = "" ]; then
  echo "[Error] publish requires explicit seed: pass --seed/--seed-id/--seed-tar (or use --no-publish)" 1>&2
  exit 2
fi

mkdir -p chengcache
timing_file="chengcache/backend_prod_closure.timings.$$"
: > "$timing_file"

cleanup_timing_file() {
  rm -f "$timing_file"
}
trap cleanup_timing_file EXIT

timestamp_now() {
  date +%s
}

record_timing() {
  printf '%s\t%s\t%s\n' "$1" "$2" "$3" >>"$timing_file"
}

print_timing_summary() {
  if [ ! -s "$timing_file" ]; then
    return
  fi
  tab="$(printf '\t')"
  echo "== backend_prod_closure.timing_top =="
  sort -t "$tab" -k3,3nr "$timing_file" | head -n 12 | while IFS="$tab" read -r label status duration; do
    [ "$label" = "" ] && continue
    echo "  ${duration}s [$status] $label"
  done
}

sanitize_diag_label() {
  printf '%s' "$1" | tr -cs 'A-Za-z0-9._-' '_'
}

prepare_timeout_diag() {
  label="$1"
  timeout_diag_last_file=""
  case "$timeout_diag_enabled" in
    1|true|TRUE|yes|YES|on|ON)
      ;;
    *)
      return 0
      ;;
  esac
  if ! command -v "$timeout_diag_tool" >/dev/null 2>&1; then
    return 0
  fi
  mkdir -p "$timeout_diag_dir"
  safe_label="$(sanitize_diag_label "$label")"
  if [ "$safe_label" = "" ]; then
    safe_label="gate"
  fi
  stamp="$(date +%Y%m%dT%H%M%S)"
  timeout_diag_last_file="$timeout_diag_dir/${stamp}_${safe_label}.sample.txt"
  export TIMEOUT_DIAG_FILE="$timeout_diag_last_file"
  export TIMEOUT_DIAG_SECONDS="$timeout_diag_seconds"
  export TIMEOUT_DIAG_TOOL="$timeout_diag_tool"
}

finish_timeout_diag() {
  status="$1"
  label="$2"
  if [ "$status" -eq 124 ] && [ "$timeout_diag_last_file" != "" ]; then
    echo "[backend_prod_closure] timeout diag ($label): $timeout_diag_last_file" 1>&2
    case "$timeout_diag_summary_enabled" in
      1|true|TRUE|yes|YES|on|ON)
        if [ -f "src/tooling/summarize_timeout_diag.sh" ]; then
          set +e
          sh src/tooling/summarize_timeout_diag.sh --file:"$timeout_diag_last_file" --top:"$timeout_diag_summary_top"
          set -e
        fi
        ;;
    esac
  fi
  unset TIMEOUT_DIAG_FILE TIMEOUT_DIAG_SECONDS TIMEOUT_DIAG_TOOL
  timeout_diag_last_file=""
}

run_with_timeout_labeled() {
  label="$1"
  seconds="$2"
  shift 2
  prepare_timeout_diag "$label"
  run_with_timeout "$seconds" "$@"
  status="$?"
  finish_timeout_diag "$status" "$label"
  return "$status"
}

run_required() {
  label="$1"
  shift
  run_required_timeout "$label" "$selfhost_gate_timeout" "$@"
}

run_required_timeout() {
  label="$1"
  timeout_sec="$2"
  shift 2
  start="$(timestamp_now)"
  set +e
  if [ "$timeout_sec" -gt 0 ] 2>/dev/null; then
    run_with_timeout_labeled "$label" "$timeout_sec" "$@"
  else
    "$@"
  fi
  status="$?"
  set -e
  end="$(timestamp_now)"
  duration=$((end - start))
  if [ "$status" -eq 0 ]; then
    record_timing "$label" "ok" "$duration"
  else
    if [ "$status" -eq 124 ]; then
      echo "[backend_prod_closure] $label timed out after ${timeout_sec}s" 1>&2
    fi
    record_timing "$label" "fail" "$duration"
    exit "$status"
  fi
}

run_optional() {
  label="$1"
  shift
  run_optional_timeout "$label" "$selfhost_gate_timeout" "$@"
}

run_optional_timeout() {
  label="$1"
  timeout_sec="$2"
  shift 2
  start="$(timestamp_now)"
  set +e
  if [ "$timeout_sec" -gt 0 ] 2>/dev/null; then
    run_with_timeout_labeled "$label" "$timeout_sec" "$@"
  else
    "$@"
  fi
  status="$?"
  set -e
  end="$(timestamp_now)"
  duration=$((end - start))
  if [ "$status" -eq 0 ]; then
    record_timing "$label" "ok" "$duration"
  elif [ "$status" -eq 2 ]; then
    if [ "$allow_skip" != "" ]; then
      echo "== $label (skip) =="
      record_timing "$label" "skip" "$duration"
    else
      echo "[backend_prod_closure] $label requested skip, but --strict is enabled" 1>&2
      record_timing "$label" "fail" "$duration"
      exit 1
    fi
  else
    if [ "$status" -eq 124 ]; then
      echo "[backend_prod_closure] $label timed out after ${timeout_sec}s" 1>&2
    fi
    record_timing "$label" "fail" "$duration"
    exit "$status"
  fi
}

run_with_timeout() {
  seconds="$1"
  shift
  perl -e '
    use POSIX qw(setsid WNOHANG);
    my $timeout = shift;
    my $diag_file = $ENV{"TIMEOUT_DIAG_FILE"} // "";
    my $diag_tool = $ENV{"TIMEOUT_DIAG_TOOL"} // "sample";
    my $diag_secs = $ENV{"TIMEOUT_DIAG_SECONDS"} // 5;
    if ($diag_secs !~ /^\d+$/ || $diag_secs <= 0) {
      $diag_secs = 5;
    } elsif ($diag_secs > 30) {
      $diag_secs = 30;
    }
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
        $res = waitpid($pid, WNOHANG);
        if ($res == $pid) {
          my $status = $?;
          if (($status & 127) != 0) {
            exit(128 + ($status & 127));
          }
          exit($status >> 8);
        }
        if ($diag_file ne "" && $^O eq "darwin") {
          system($diag_tool, "$pid", "$diag_secs", "-mayDie", "-file", $diag_file);
        }
        $res = waitpid($pid, WNOHANG);
        if ($res == $pid) {
          my $status = $?;
          if (($status & 127) != 0) {
            exit(128 + ($status & 127));
          }
          exit($status >> 8);
        }
        kill "TERM", -$pid;
        kill "TERM", $pid;
        my $grace_end = time + 1;
        while (time < $grace_end) {
          $res = waitpid($pid, WNOHANG);
          if ($res == $pid) {
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

resolve_runtime_obj_for_target() {
  target="$1"
  if [ "${BACKEND_RUNTIME_OBJ:-}" != "" ]; then
    printf '%s\n' "${BACKEND_RUNTIME_OBJ}"
    return
  fi
  safe_target="$(printf '%s' "$target" | tr -c 'A-Za-z0-9._-' '_' | tr -s '_')"
  printf '%s\n' "chengcache/system_helpers.backend.cheng.${safe_target}.o"
}

driver_stage0_probe_ok() {
  cand="$1"
  [ "$cand" != "" ] || return 1
  [ -x "$cand" ] || return 1
  cand_hash=""
  if command -v shasum >/dev/null 2>&1; then
    cand_hash="$(shasum -a 256 "$cand" 2>/dev/null | awk '{print $1}')"
  elif command -v sha256sum >/dev/null 2>&1; then
    cand_hash="$(sha256sum "$cand" 2>/dev/null | awk '{print $1}')"
  fi
  case "$cand_hash" in
    # Known-bad release artifacts that can wedge in UE state during stage0 probe.
    08b9888a214418a32a468f1d9155c9d21d1789d01579cf84e7d9d6321366e382|\
    d059d1d84290dac64120dc78f0dbd9cb24e0e4b3d5a9045e63ad26232373ed1a)
      return 1
      ;;
  esac
  resolved="$cand"
  probe_mode="${BACKEND_PROD_STAGE0_PROBE_MODE:-}"
  if [ "$probe_mode" = "" ]; then
    if [ "$run_selfhost" != "" ] || [ "$run_selfhost_strict_noreuse_probe" = "1" ] || [ "$run_selfhost_parallel_perf" = "1" ]; then
      probe_mode="light"
    else
      probe_mode="path"
    fi
  fi
  case "$probe_mode" in
    path)
      printf "%s\n" "$resolved"
      return 0
      ;;
    light)
      probe_input="${BACKEND_PROD_STAGE0_PROBE_INPUT:-tests/cheng/backend/fixtures/return_add.cheng}"
      if [ ! -f "$probe_input" ]; then
        printf "%s\n" "$resolved"
        return 0
      fi
      probe_target="$(sh src/tooling/detect_host_target.sh 2>/dev/null || echo auto)"
      probe_runtime_obj="$(resolve_runtime_obj_for_target "$probe_target")"
      if [ ! -f "$probe_runtime_obj" ]; then
        return 1
      fi
      probe_out="chengcache/.backend_prod_stage0_probe_light_$$.bin"
      probe_timeout="${BACKEND_PROD_STAGE0_PROBE_TIMEOUT:-20}"
      rm -f "$probe_out"
      set +e
      run_with_timeout_labeled "backend.stage0_probe.light" "$probe_timeout" env \
        MM=orc \
        CACHE=0 \
        BACKEND_IR=uir \
        BACKEND_MULTI=0 \
        BACKEND_MULTI_FORCE=0 \
        BACKEND_INCREMENTAL=0 \
        BACKEND_VALIDATE=0 \
        BACKEND_WHOLE_PROGRAM=1 \
        BACKEND_LINKER=self \
        BACKEND_DRIVER="$resolved" \
        BACKEND_NO_RUNTIME_C=1 \
        BACKEND_RUNTIME_OBJ="$probe_runtime_obj" \
        BACKEND_EMIT=exe \
        BACKEND_TARGET="$probe_target" \
        BACKEND_FRONTEND=stage1 \
        BACKEND_INPUT="$probe_input" \
        BACKEND_OUTPUT="$probe_out" \
        "$resolved" >/dev/null 2>&1
      status="$?"
      set -e
      if [ "$status" -ne 0 ] || [ ! -s "$probe_out" ]; then
        rm -f "$probe_out"
        return 1
      fi
      rm -f "$probe_out"
      ;;
    full)
      probe_target="$(sh src/tooling/detect_host_target.sh 2>/dev/null || echo auto)"
      probe_runtime_obj="$(resolve_runtime_obj_for_target "$probe_target")"
      if [ ! -f "$probe_runtime_obj" ]; then
        return 1
      fi
      probe_out="chengcache/.backend_prod_stage0_probe_$$.bin"
      probe_timeout="${BACKEND_PROD_STAGE0_PROBE_TIMEOUT:-$selfhost_timeout}"
      rm -f "$probe_out"
      set +e
      run_with_timeout_labeled "backend.stage0_probe.full" "$probe_timeout" env \
        STAGE1_SKIP_SEM=1 \
        GENERIC_MODE=dict \
        GENERIC_SPEC_BUDGET=0 \
        STAGE1_SKIP_OWNERSHIP=1 \
        MM=orc \
        CACHE=chengcache \
        BACKEND_MULTI=0 \
        BACKEND_MULTI_FORCE=0 \
        BACKEND_INCREMENTAL=1 \
        BACKEND_JOBS=0 \
        BACKEND_VALIDATE=1 \
        BACKEND_WHOLE_PROGRAM=1 \
        BACKEND_LINKER=self \
        BACKEND_NO_RUNTIME_C=1 \
        BACKEND_RUNTIME_OBJ="$probe_runtime_obj" \
        BACKEND_EMIT=exe \
        BACKEND_TARGET="$probe_target" \
        BACKEND_FRONTEND=stage1 \
        BACKEND_INPUT=src/backend/tooling/backend_driver.cheng \
        BACKEND_OUTPUT="$probe_out" \
        "$resolved" >/dev/null 2>&1
      status="$?"
      set -e
      if [ "$status" -ne 0 ] || [ ! -s "$probe_out" ]; then
        rm -f "$probe_out"
        return 1
      fi
      rm -f "$probe_out"
      ;;
    *)
      echo "[Error] invalid BACKEND_PROD_STAGE0_PROBE_MODE: $probe_mode (expected path|light|full)" 1>&2
      return 1
      ;;
  esac
  printf "%s\n" "$resolved"
  return 0
}

if [ "$allow_skip" = "" ]; then
  export BACKEND_MATRIX_STRICT=1
fi
if [ "${ABI:-}" != "" ] && [ "${ABI}" != "v2_noptr" ]; then
  echo "[backend_prod_closure] only ABI=v2_noptr is supported (got: ${ABI})" 1>&2
  exit 2
fi
export ABI=v2_noptr
if [ "${BORROW_IR:-}" = "" ]; then
  export BORROW_IR=mir
fi
if [ "${GENERIC_LOWERING:-}" = "" ]; then
  export GENERIC_LOWERING=mir_hybrid
fi
if [ "${STAGE1_STD_NO_POINTERS:-}" = "" ]; then
  export STAGE1_STD_NO_POINTERS=1
fi
if [ "${STAGE1_STD_NO_POINTERS_STRICT:-}" = "" ]; then
  export STAGE1_STD_NO_POINTERS_STRICT=0
fi
if [ "${STAGE1_NO_POINTERS_NON_C_ABI:-}" = "" ]; then
  export STAGE1_NO_POINTERS_NON_C_ABI=1
fi
if [ "${STAGE1_NO_POINTERS_NON_C_ABI_INTERNAL:-}" = "" ]; then
  export STAGE1_NO_POINTERS_NON_C_ABI_INTERNAL=1
fi

main_allow_selfhost_driver="${BACKEND_MAIN_ALLOW_SELFHOST_DRIVER:-auto}"
if [ "$main_allow_selfhost_driver" = "auto" ]; then
  if [ "${STAGE1_NO_POINTERS_NON_C_ABI:-0}" = "1" ]; then
    main_allow_selfhost_driver="0"
  else
    main_allow_selfhost_driver="1"
  fi
fi
case "$main_allow_selfhost_driver" in
  0|1)
    ;;
  *)
    echo "[Error] invalid BACKEND_MAIN_ALLOW_SELFHOST_DRIVER: $main_allow_selfhost_driver (expected auto|0|1)" 1>&2
    exit 2
    ;;
esac

selfhost_stage0="${seed_path:-}"
if [ "$run_selfhost" != "" ]; then
  if [ "$selfhost_stage0" = "" ] && [ "$backend_driver_explicit" = "1" ]; then
    if [ -x "${BACKEND_DRIVER:-}" ]; then
      selfhost_stage0="${BACKEND_DRIVER}"
    fi
  fi
  if [ "$selfhost_stage0" = "" ]; then
    for cand in \
      "artifacts/backend_selfhost_self_obj/cheng_stage0_prod" \
      "artifacts/backend_selfhost_self_obj/cheng_stage0_default" \
      "artifacts/backend_selfhost_self_obj/cheng.stage2" \
      "artifacts/backend_selfhost_self_obj/cheng.stage1" \
      "artifacts/backend_driver/cheng" \
      "dist/releases/current/cheng" \
      dist/releases/*/cheng \
      "artifacts/backend_seed/cheng.stage2"; do
      echo "[backend_prod_closure] stage0 probe: $cand" 1>&2
      resolved=""
      if resolved="$(driver_stage0_probe_ok "$cand")"; then
        selfhost_stage0="$resolved"
        break
      fi
    done
  fi
  if [ "$selfhost_stage0" = "" ] && [ "$seed_requested" != "1" ] && [ "$backend_driver_explicit" != "1" ]; then
    fallback_stage0="$(env BACKEND_DRIVER_PATH_ALLOW_SELFHOST=1 sh src/tooling/backend_driver_path.sh 2>/dev/null || true)"
    resolved=""
    if resolved="$(driver_stage0_probe_ok "$fallback_stage0")"; then
      selfhost_stage0="$resolved"
    fi
  fi
  if [ "$selfhost_stage0" = "" ]; then
    echo "[backend_prod_closure] no healthy selfhost stage0 driver found" 1>&2
    echo "  hint: provide --seed:<path> or BACKEND_DRIVER=<path>" 1>&2
    exit 1
  fi
fi

main_driver_path_allow_selfhost="${BACKEND_PROD_MAIN_DRIVER_PATH_ALLOW_SELFHOST:-}"
if [ "$main_driver_path_allow_selfhost" = "" ]; then
  if [ "$run_selfhost" != "" ] && [ "$main_allow_selfhost_driver" = "1" ]; then
    main_driver_path_allow_selfhost="1"
  else
    main_driver_path_allow_selfhost="0"
  fi
fi
case "$main_driver_path_allow_selfhost" in
  0|1)
    ;;
  *)
    echo "[Error] invalid BACKEND_PROD_MAIN_DRIVER_PATH_ALLOW_SELFHOST: $main_driver_path_allow_selfhost (expected 0|1)" 1>&2
    exit 2
    ;;
esac

driver_runnable() {
  cand="$1"
  [ "$cand" != "" ] || return 1
  [ -x "$cand" ] || return 1
  set +e
  env BACKEND_DRIVER="$cand" BACKEND_DRIVER_PATH_STAGE1_STRICT_SMOKE=0 BACKEND_DRIVER_PATH_STAGE1_DICT_SMOKE=1 \
    sh src/tooling/backend_driver_path.sh >/dev/null 2>&1
  status="$?"
  set -e
  if [ "$status" -eq 0 ]; then
    return 0
  fi
  if driver_help_ok "$cand"; then
    return 0
  fi
  if driver_stage1_compile_ok "$cand"; then
    return 0
  fi
  return 1
}

driver_help_ok() {
  cand="$1"
  [ "$cand" != "" ] || return 1
  [ -x "$cand" ] || return 1
  help_timeout="${BACKEND_PROD_DRIVER_HELP_TIMEOUT:-5}"
  set +e
  run_with_timeout "$help_timeout" "$cand" --help >/dev/null 2>&1
  status="$?"
  set -e
  case "$status" in
    0|1|2) return 0 ;;
  esac
  return 1
}

driver_has_non_c_abi_diag() {
  cand="$1"
  [ "$cand" != "" ] || return 1
  [ -x "$cand" ] || return 1
  if ! command -v strings >/dev/null 2>&1; then
    return 0
  fi
  tmp_strings="$(mktemp "${TMPDIR:-/tmp}/cheng_driver_strings.XXXXXX" 2>/dev/null || true)"
  if [ "$tmp_strings" = "" ]; then
    return 1
  fi
  set +e
  strings "$cand" 2>/dev/null >"$tmp_strings"
  strings_status="$?"
  grep -Fq "no-pointer policy: pointer types are forbidden outside C ABI modules" "$tmp_strings"
  status="$?"
  set -e
  rm -f "$tmp_strings" 2>/dev/null || true
  if [ "$strings_status" -ne 0 ] && [ "$status" -ne 0 ]; then
    return 1
  fi
  [ "$status" -eq 0 ]
}

driver_stage1_compile_ok() {
  cand="$1"
  [ "$cand" != "" ] || return 1
  [ -x "$cand" ] || return 1
  probe_input="${BACKEND_MAIN_DRIVER_PROBE_INPUT:-tests/cheng/backend/fixtures/return_add.cheng}"
  if [ ! -f "$probe_input" ]; then
    probe_input="tests/cheng/backend/fixtures/return_i64.cheng"
  fi
  probe_skip_sem="${STAGE1_SKIP_SEM:-0}"
  probe_skip_ownership="${STAGE1_SKIP_OWNERSHIP:-1}"
  if [ ! -f "$probe_input" ]; then
    return 1
  fi
  probe_target="$(sh src/tooling/detect_host_target.sh 2>/dev/null || echo auto)"
  probe_runtime_obj="$(resolve_runtime_obj_for_target "$probe_target")"
  if [ ! -f "$probe_runtime_obj" ]; then
    return 1
  fi
  probe_out="chengcache/.backend_main_driver_probe_$$.bin"
  probe_timeout="${BACKEND_MAIN_DRIVER_PROBE_TIMEOUT:-20}"
  rm -f "$probe_out"
  set +e
  run_with_timeout_labeled "backend.main_driver_probe" "$probe_timeout" env \
    MM=orc \
    BACKEND_VALIDATE=0 \
    STAGE1_NO_POINTERS_NON_C_ABI=0 \
    STAGE1_NO_POINTERS_NON_C_ABI_INTERNAL=0 \
    STAGE1_SKIP_SEM="$probe_skip_sem" \
    STAGE1_SKIP_OWNERSHIP="$probe_skip_ownership" \
    BACKEND_LINKER=self \
    BACKEND_NO_RUNTIME_C=1 \
    BACKEND_RUNTIME_OBJ="$probe_runtime_obj" \
    BACKEND_EMIT=exe \
    BACKEND_TARGET="$probe_target" \
    BACKEND_FRONTEND=stage1 \
    BACKEND_INPUT="$probe_input" \
    BACKEND_OUTPUT="$probe_out" \
    "$cand" >/dev/null 2>&1
  status="$?"
  set -e
  if [ "$status" -ne 0 ] || [ ! -s "$probe_out" ]; then
    rm -f "$probe_out"
    return 1
  fi
  rm -f "$probe_out"
  return 0
}

if [ "$run_selfhost" != "" ]; then
  selfhost_label="backend.selfhost_bootstrap"
  if [ "$selfhost_mode" = "fast" ]; then
    selfhost_label="backend.selfhost_bootstrap.fast"
  else
    selfhost_label="backend.selfhost_bootstrap.strict"
  fi
  if [ "$selfhost_stage0" != "" ]; then
    run_optional_timeout "$selfhost_label" "$selfhost_bootstrap_gate_timeout" env STAGE1_NO_POINTERS_NON_C_ABI=0 STAGE1_NO_POINTERS_NON_C_ABI_INTERNAL=0 SELF_OBJ_BOOTSTRAP_TIMEOUT="$selfhost_timeout" SELF_OBJ_BOOTSTRAP_REUSE="$selfhost_reuse" SELF_OBJ_BOOTSTRAP_SESSION="$selfhost_session" SELF_OBJ_BOOTSTRAP_MODE="$selfhost_mode" SELF_OBJ_BOOTSTRAP_MULTI=0 SELF_OBJ_BOOTSTRAP_MULTI_FORCE=0 SELF_OBJ_BOOTSTRAP_REQUIRE_RUNNABLE=0 SELF_OBJ_BOOTSTRAP_STAGE0_COMPAT=0 SELF_OBJ_BOOTSTRAP_STAGE0="$selfhost_stage0" \
      sh src/tooling/verify_backend_selfhost_bootstrap.sh
  else
    run_optional_timeout "$selfhost_label" "$selfhost_bootstrap_gate_timeout" env STAGE1_NO_POINTERS_NON_C_ABI=0 STAGE1_NO_POINTERS_NON_C_ABI_INTERNAL=0 SELF_OBJ_BOOTSTRAP_TIMEOUT="$selfhost_timeout" SELF_OBJ_BOOTSTRAP_REUSE="$selfhost_reuse" SELF_OBJ_BOOTSTRAP_SESSION="$selfhost_session" SELF_OBJ_BOOTSTRAP_MODE="$selfhost_mode" SELF_OBJ_BOOTSTRAP_MULTI=0 SELF_OBJ_BOOTSTRAP_MULTI_FORCE=0 SELF_OBJ_BOOTSTRAP_REQUIRE_RUNNABLE=0 SELF_OBJ_BOOTSTRAP_STAGE0_COMPAT=0 sh src/tooling/verify_backend_selfhost_bootstrap.sh
  fi
  if [ "$run_selfhost_strict" = "1" ] && [ "$selfhost_mode" = "fast" ]; then
    strict_session="${BACKEND_PROD_SELFHOST_STRICT_SESSION:-$selfhost_session}"
    strict_allow_fast_reuse="${BACKEND_PROD_SELFHOST_STRICT_ALLOW_FAST_REUSE:-1}"
    if [ "$selfhost_stage0" != "" ]; then
      run_optional_timeout "backend.selfhost_bootstrap.strict" "$selfhost_bootstrap_gate_timeout" env STAGE1_NO_POINTERS_NON_C_ABI=0 STAGE1_NO_POINTERS_NON_C_ABI_INTERNAL=0 SELF_OBJ_BOOTSTRAP_TIMEOUT="$selfhost_timeout" SELF_OBJ_BOOTSTRAP_REUSE="$selfhost_reuse" SELF_OBJ_BOOTSTRAP_SESSION="$strict_session" SELF_OBJ_BOOTSTRAP_MODE=strict SELF_OBJ_BOOTSTRAP_MULTI=0 SELF_OBJ_BOOTSTRAP_MULTI_FORCE=0 SELF_OBJ_BOOTSTRAP_REQUIRE_RUNNABLE=0 SELF_OBJ_BOOTSTRAP_STRICT_ALLOW_FAST_REUSE="$strict_allow_fast_reuse" SELF_OBJ_BOOTSTRAP_STAGE0_COMPAT=0 SELF_OBJ_BOOTSTRAP_STAGE0="$selfhost_stage0" \
        sh src/tooling/verify_backend_selfhost_bootstrap.sh
    else
      run_optional_timeout "backend.selfhost_bootstrap.strict" "$selfhost_bootstrap_gate_timeout" env STAGE1_NO_POINTERS_NON_C_ABI=0 STAGE1_NO_POINTERS_NON_C_ABI_INTERNAL=0 SELF_OBJ_BOOTSTRAP_TIMEOUT="$selfhost_timeout" SELF_OBJ_BOOTSTRAP_REUSE="$selfhost_reuse" SELF_OBJ_BOOTSTRAP_SESSION="$strict_session" SELF_OBJ_BOOTSTRAP_MODE=strict SELF_OBJ_BOOTSTRAP_MULTI=0 SELF_OBJ_BOOTSTRAP_MULTI_FORCE=0 SELF_OBJ_BOOTSTRAP_REQUIRE_RUNNABLE=0 SELF_OBJ_BOOTSTRAP_STRICT_ALLOW_FAST_REUSE="$strict_allow_fast_reuse" SELF_OBJ_BOOTSTRAP_STAGE0_COMPAT=0 sh src/tooling/verify_backend_selfhost_bootstrap.sh
    fi
  fi
  if [ "$run_stage0_no_compat_gate" = "1" ]; then
    stage0_no_compat_session="${BACKEND_PROD_STAGE0_NO_COMPAT_SESSION:-${selfhost_session}.stage0_no_compat}"
    stage0_no_compat_mode="${BACKEND_PROD_STAGE0_NO_COMPAT_MODE:-fast}"
    stage0_no_compat_timeout="${BACKEND_PROD_STAGE0_NO_COMPAT_TIMEOUT:-$selfhost_timeout}"
    stage0_no_compat_stage0="${BACKEND_PROD_STAGE0_NO_COMPAT_STAGE0:-$selfhost_stage0}"
    stage0_no_compat_reuse="${BACKEND_PROD_STAGE0_NO_COMPAT_REUSE:-0}"
    stage0_no_compat_validate="${BACKEND_PROD_STAGE0_NO_COMPAT_VALIDATE:-0}"
    stage0_no_compat_skip_smoke="${BACKEND_PROD_STAGE0_NO_COMPAT_SKIP_SMOKE:-1}"
    stage0_no_compat_require_runnable="${BACKEND_PROD_STAGE0_NO_COMPAT_REQUIRE_RUNNABLE:-0}"
    stage0_no_compat_stage1_probe_required="${BACKEND_PROD_STAGE0_NO_COMPAT_STAGE1_PROBE_REQUIRED:-$stage0_no_compat_require_runnable}"
    if [ "$stage0_no_compat_stage0" != "" ]; then
      run_required_timeout "backend.stage0_no_compat" "$stage0_no_compat_gate_timeout" env STAGE1_NO_POINTERS_NON_C_ABI=0 STAGE1_NO_POINTERS_NON_C_ABI_INTERNAL=0 STAGE0_NO_COMPAT_SESSION="$stage0_no_compat_session" STAGE0_NO_COMPAT_MODE="$stage0_no_compat_mode" STAGE0_NO_COMPAT_TIMEOUT="$stage0_no_compat_timeout" STAGE0_NO_COMPAT_REUSE="$stage0_no_compat_reuse" STAGE0_NO_COMPAT_VALIDATE="$stage0_no_compat_validate" STAGE0_NO_COMPAT_SKIP_SMOKE="$stage0_no_compat_skip_smoke" STAGE0_NO_COMPAT_REQUIRE_RUNNABLE="$stage0_no_compat_require_runnable" STAGE0_NO_COMPAT_STAGE1_PROBE_REQUIRED="$stage0_no_compat_stage1_probe_required" STAGE0_NO_COMPAT_STAGE0="$stage0_no_compat_stage0" sh src/tooling/verify_backend_stage0_no_compat.sh
    else
      run_required_timeout "backend.stage0_no_compat" "$stage0_no_compat_gate_timeout" env STAGE1_NO_POINTERS_NON_C_ABI=0 STAGE1_NO_POINTERS_NON_C_ABI_INTERNAL=0 STAGE0_NO_COMPAT_SESSION="$stage0_no_compat_session" STAGE0_NO_COMPAT_MODE="$stage0_no_compat_mode" STAGE0_NO_COMPAT_TIMEOUT="$stage0_no_compat_timeout" STAGE0_NO_COMPAT_REUSE="$stage0_no_compat_reuse" STAGE0_NO_COMPAT_VALIDATE="$stage0_no_compat_validate" STAGE0_NO_COMPAT_SKIP_SMOKE="$stage0_no_compat_skip_smoke" STAGE0_NO_COMPAT_REQUIRE_RUNNABLE="$stage0_no_compat_require_runnable" STAGE0_NO_COMPAT_STAGE1_PROBE_REQUIRED="$stage0_no_compat_stage1_probe_required" sh src/tooling/verify_backend_stage0_no_compat.sh
    fi
  fi
  if [ "$run_selfhost_strict_noreuse_probe" = "1" ]; then
    strict_probe_timeout="$selfhost_strict_noreuse_probe_timeout"
    gate_timeout_numeric="1"
    probe_timeout_numeric="1"
    case "$selfhost_strict_noreuse_gate_timeout" in
      ''|*[!0-9]*)
        gate_timeout_numeric="0"
        ;;
    esac
    case "$strict_probe_timeout" in
      ''|*[!0-9]*)
        probe_timeout_numeric="0"
        ;;
    esac
    if [ "$gate_timeout_numeric" = "1" ] && [ "$probe_timeout_numeric" = "1" ]; then
      if [ "$selfhost_strict_noreuse_gate_timeout" -gt 0 ] && [ "$strict_probe_timeout" -ge "$selfhost_strict_noreuse_gate_timeout" ]; then
        strict_probe_timeout=$((selfhost_strict_noreuse_gate_timeout - 5))
        if [ "$strict_probe_timeout" -lt 1 ]; then
          strict_probe_timeout=1
        fi
      fi
    fi
    strict_probe_session_base="${BACKEND_PROD_SELFHOST_STRICT_SESSION:-$selfhost_session}"
    strict_probe_session="${BACKEND_PROD_SELFHOST_STRICT_NOREUSE_SESSION:-${strict_probe_session_base}.noreuse}"
    strict_probe_require="${BACKEND_SELFHOST_STRICT_NOREUSE_PROBE_REQUIRE:-1}"
    strict_probe_validate="${BACKEND_PROD_SELFHOST_STRICT_NOREUSE_VALIDATE:-0}"
    strict_probe_skip_smoke="${BACKEND_PROD_SELFHOST_STRICT_NOREUSE_SKIP_SMOKE:-1}"
    strict_probe_require_runnable="${BACKEND_PROD_SELFHOST_STRICT_NOREUSE_REQUIRE_RUNNABLE:-1}"
    strict_probe_stage1_probe_required="${BACKEND_PROD_SELFHOST_STRICT_NOREUSE_STAGE1_PROBE_REQUIRED:-0}"
    strict_probe_alias_off_fast="${BACKEND_PROD_SELFHOST_STRICT_NOREUSE_ALIAS_OFF_FAST:-1}"
    if [ "$strict_probe_alias_off_fast" = "1" ]; then
      if [ "${BACKEND_PROD_SELFHOST_STRICT_NOREUSE_REQUIRE_RUNNABLE:-}" = "" ]; then
        strict_probe_require_runnable="0"
      fi
      if [ "${BACKEND_PROD_SELFHOST_STRICT_NOREUSE_STAGE1_PROBE_REQUIRED:-}" = "" ]; then
        strict_probe_stage1_probe_required="0"
      fi
      if [ "${BACKEND_PROD_SELFHOST_STRICT_NOREUSE_SKIP_SMOKE:-}" = "" ]; then
        strict_probe_skip_smoke="1"
      fi
      if [ "${BACKEND_PROD_SELFHOST_STRICT_NOREUSE_VALIDATE:-}" = "" ]; then
        strict_probe_validate="0"
      fi
      echo "[backend_prod_closure] info: strict noreuse alias-off fast profile enabled (frontend=stage1, require_runnable=${strict_probe_require_runnable})" 1>&2
    fi
    strict_probe_stage0="${BACKEND_PROD_SELFHOST_STRICT_NOREUSE_STAGE0:-}"
    strict_probe_prefer_stable_stage0="${BACKEND_PROD_SELFHOST_STRICT_NOREUSE_PREFER_STABLE_STAGE0:-1}"
    if [ "$strict_probe_stage0" = "" ] && [ "$strict_probe_prefer_stable_stage0" = "1" ]; then
      for strict_probe_cand in \
        "artifacts/backend_selfhost_self_obj/cheng_stage0_prod" \
        "artifacts/backend_selfhost_self_obj/cheng_stage0_default" \
        "artifacts/backend_selfhost_self_obj/cheng.stage2" \
        "artifacts/backend_selfhost_self_obj/cheng.stage1" \
        "artifacts/backend_driver/cheng" \
        "dist/releases/current/cheng" \
        dist/releases/*/cheng \
        "artifacts/backend_seed/cheng.stage2" \
        "$selfhost_stage0"; do
        [ "$strict_probe_cand" != "" ] || continue
        strict_probe_resolved=""
        if strict_probe_resolved="$(driver_stage0_probe_ok "$strict_probe_cand")"; then
          strict_probe_stage0="$strict_probe_resolved"
          break
        fi
      done
    fi
    if [ "$strict_probe_stage0" = "" ]; then
      strict_probe_stage0="$selfhost_stage0"
    fi
    if [ "$strict_probe_stage0" != "" ]; then
      run_required_timeout "backend.selfhost_strict_noreuse_probe" "$selfhost_strict_noreuse_gate_timeout" env STAGE1_NO_POINTERS_NON_C_ABI=0 STAGE1_NO_POINTERS_NON_C_ABI_INTERNAL=0 SELFHOST_STRICT_PROBE_SESSION="$strict_probe_session" SELFHOST_STRICT_PROBE_TIMEOUT="$strict_probe_timeout" SELFHOST_STRICT_PROBE_REQUIRE="$strict_probe_require" SELFHOST_STRICT_PROBE_REUSE=0 SELFHOST_STRICT_PROBE_STRICT_ALLOW_FAST_REUSE=0 SELFHOST_STRICT_PROBE_VALIDATE="$strict_probe_validate" SELFHOST_STRICT_PROBE_SKIP_SMOKE="$strict_probe_skip_smoke" SELFHOST_STRICT_PROBE_REQUIRE_RUNNABLE="$strict_probe_require_runnable" SELFHOST_STRICT_PROBE_STAGE1_PROBE_REQUIRED="$strict_probe_stage1_probe_required" SELFHOST_STRICT_PROBE_STAGE0="$strict_probe_stage0" sh src/tooling/verify_backend_selfhost_strict_noreuse_probe.sh
    else
      run_required_timeout "backend.selfhost_strict_noreuse_probe" "$selfhost_strict_noreuse_gate_timeout" env STAGE1_NO_POINTERS_NON_C_ABI=0 STAGE1_NO_POINTERS_NON_C_ABI_INTERNAL=0 SELFHOST_STRICT_PROBE_SESSION="$strict_probe_session" SELFHOST_STRICT_PROBE_TIMEOUT="$strict_probe_timeout" SELFHOST_STRICT_PROBE_REQUIRE="$strict_probe_require" SELFHOST_STRICT_PROBE_REUSE=0 SELFHOST_STRICT_PROBE_STRICT_ALLOW_FAST_REUSE=0 SELFHOST_STRICT_PROBE_VALIDATE="$strict_probe_validate" SELFHOST_STRICT_PROBE_SKIP_SMOKE="$strict_probe_skip_smoke" SELFHOST_STRICT_PROBE_REQUIRE_RUNNABLE="$strict_probe_require_runnable" SELFHOST_STRICT_PROBE_STAGE1_PROBE_REQUIRED="$strict_probe_stage1_probe_required" sh src/tooling/verify_backend_selfhost_strict_noreuse_probe.sh
    fi
    if [ "$run_selfhost_parallel_perf" = "1" ]; then
      parallel_probe_stage0="$strict_probe_stage0"
      if [ "$parallel_probe_stage0" = "" ]; then
        parallel_probe_stage0="$selfhost_stage0"
      fi
      parallel_probe_session="${BACKEND_PROD_SELFHOST_PARALLEL_PERF_SESSION:-${strict_probe_session}.parallel_perf}"
      parallel_max_slowdown="${BACKEND_PROD_SELFHOST_PARALLEL_PERF_MAX_SLOWDOWN_SEC:-12}"
      parallel_probe_timeout="$selfhost_parallel_perf_timeout"
      parallel_gate_timeout="$selfhost_parallel_perf_gate_timeout"
      case "$parallel_probe_timeout:$parallel_gate_timeout" in
        *[!0-9:]*)
          :
          ;;
        *)
          min_parallel_gate="$((parallel_probe_timeout * 2 + 10))"
          if [ "$parallel_gate_timeout" -gt 0 ] && [ "$parallel_gate_timeout" -lt "$min_parallel_gate" ]; then
            parallel_gate_timeout="$min_parallel_gate"
          fi
          ;;
      esac
      if [ "$parallel_probe_stage0" != "" ]; then
        run_required_timeout "backend.selfhost_parallel_perf" "$parallel_gate_timeout" env STAGE1_NO_POINTERS_NON_C_ABI=0 STAGE1_NO_POINTERS_NON_C_ABI_INTERNAL=0 SELFHOST_PARALLEL_PERF_TIMEOUT="$parallel_probe_timeout" SELFHOST_PARALLEL_PERF_STAGE0="$parallel_probe_stage0" SELFHOST_PARALLEL_PERF_BASE_SESSION="$parallel_probe_session" SELFHOST_PARALLEL_PERF_MAX_SLOWDOWN_SEC="$parallel_max_slowdown" SELFHOST_PARALLEL_PERF_SKIP_SMOKE="$strict_probe_skip_smoke" SELFHOST_PARALLEL_PERF_REQUIRE_RUNNABLE="$strict_probe_require_runnable" SELFHOST_PARALLEL_PERF_VALIDATE="$strict_probe_validate" SELFHOST_PARALLEL_PERF_SKIP_SEM=1 SELFHOST_PARALLEL_PERF_SKIP_OWNERSHIP=1 SELFHOST_PARALLEL_PERF_SKIP_CPROFILE=1 SELFHOST_PARALLEL_PERF_GENERIC_MODE=dict SELFHOST_PARALLEL_PERF_GENERIC_SPEC_BUDGET=0 sh src/tooling/verify_backend_selfhost_parallel_perf.sh
      else
        run_required_timeout "backend.selfhost_parallel_perf" "$parallel_gate_timeout" env STAGE1_NO_POINTERS_NON_C_ABI=0 STAGE1_NO_POINTERS_NON_C_ABI_INTERNAL=0 SELFHOST_PARALLEL_PERF_TIMEOUT="$parallel_probe_timeout" SELFHOST_PARALLEL_PERF_BASE_SESSION="$parallel_probe_session" SELFHOST_PARALLEL_PERF_MAX_SLOWDOWN_SEC="$parallel_max_slowdown" SELFHOST_PARALLEL_PERF_SKIP_SMOKE="$strict_probe_skip_smoke" SELFHOST_PARALLEL_PERF_REQUIRE_RUNNABLE="$strict_probe_require_runnable" SELFHOST_PARALLEL_PERF_VALIDATE="$strict_probe_validate" SELFHOST_PARALLEL_PERF_SKIP_SEM=1 SELFHOST_PARALLEL_PERF_SKIP_OWNERSHIP=1 SELFHOST_PARALLEL_PERF_SKIP_CPROFILE=1 SELFHOST_PARALLEL_PERF_GENERIC_MODE=dict SELFHOST_PARALLEL_PERF_GENERIC_SPEC_BUDGET=0 sh src/tooling/verify_backend_selfhost_parallel_perf.sh
      fi
    fi
  fi
fi

if [ "$run_driver_selfbuild_smoke" = "1" ]; then
  driver_selfbuild_stage0="${BACKEND_PROD_DRIVER_SELFBUILD_SMOKE_STAGE0:-}"
  if [ "$driver_selfbuild_stage0" = "" ]; then
    driver_selfbuild_stage0="$selfhost_stage0"
  fi
  if [ "$driver_selfbuild_stage0" = "" ] && [ "${BACKEND_DRIVER:-}" != "" ]; then
    driver_selfbuild_stage0="${BACKEND_DRIVER}"
  fi
  driver_selfbuild_session_base="${BACKEND_PROD_SELFHOST_STRICT_NOREUSE_SESSION:-${BACKEND_PROD_SELFHOST_STRICT_SESSION:-$selfhost_session}}"
  driver_selfbuild_session="${BACKEND_PROD_DRIVER_SELFBUILD_SMOKE_SESSION:-${driver_selfbuild_session_base}.driver_selfbuild}"
  driver_selfbuild_session_safe="$(printf '%s' "$driver_selfbuild_session" | tr -c 'A-Za-z0-9._-' '_')"
  driver_selfbuild_report="${BACKEND_PROD_DRIVER_SELFBUILD_SMOKE_REPORT:-artifacts/backend_driver_selfbuild_smoke/selfbuild_smoke_${driver_selfbuild_session_safe}.tsv}"
  driver_selfbuild_output="${BACKEND_PROD_DRIVER_SELFBUILD_SMOKE_OUTPUT:-artifacts/backend_driver/cheng.selfbuild_smoke}"
  driver_selfbuild_require_rebuild="${BACKEND_PROD_DRIVER_SELFBUILD_SMOKE_REQUIRE_REBUILD:-0}"
  driver_selfbuild_max_stage0_attempts="${BACKEND_PROD_DRIVER_SELFBUILD_SMOKE_MAX_STAGE0_ATTEMPTS:-1}"
  driver_selfbuild_multi="${BACKEND_PROD_DRIVER_SELFBUILD_SMOKE_MULTI:-0}"
  driver_selfbuild_multi_force="${BACKEND_PROD_DRIVER_SELFBUILD_SMOKE_MULTI_FORCE:-0}"
  driver_selfbuild_incremental="${BACKEND_PROD_DRIVER_SELFBUILD_SMOKE_INCREMENTAL:-1}"
  driver_selfbuild_jobs="${BACKEND_PROD_DRIVER_SELFBUILD_SMOKE_JOBS:-0}"
  if [ "$driver_selfbuild_stage0" != "" ]; then
    run_required_timeout "backend.driver_selfbuild_smoke" "$driver_selfbuild_smoke_gate_timeout" env STAGE1_NO_POINTERS_NON_C_ABI=0 STAGE1_NO_POINTERS_NON_C_ABI_INTERNAL=0 DRIVER_SELFBUILD_SMOKE_STAGE0="$driver_selfbuild_stage0" DRIVER_SELFBUILD_SMOKE_TIMEOUT="$driver_selfbuild_smoke_build_timeout" DRIVER_SELFBUILD_SMOKE_REPORT="$driver_selfbuild_report" DRIVER_SELFBUILD_SMOKE_OUTPUT="$driver_selfbuild_output" DRIVER_SELFBUILD_SMOKE_REQUIRE_REBUILD="$driver_selfbuild_require_rebuild" DRIVER_SELFBUILD_SMOKE_MAX_STAGE0_ATTEMPTS="$driver_selfbuild_max_stage0_attempts" DRIVER_SELFBUILD_SMOKE_MULTI="$driver_selfbuild_multi" DRIVER_SELFBUILD_SMOKE_MULTI_FORCE="$driver_selfbuild_multi_force" DRIVER_SELFBUILD_SMOKE_INCREMENTAL="$driver_selfbuild_incremental" DRIVER_SELFBUILD_SMOKE_JOBS="$driver_selfbuild_jobs" sh src/tooling/verify_backend_driver_selfbuild_smoke.sh
  else
    run_required_timeout "backend.driver_selfbuild_smoke" "$driver_selfbuild_smoke_gate_timeout" env STAGE1_NO_POINTERS_NON_C_ABI=0 STAGE1_NO_POINTERS_NON_C_ABI_INTERNAL=0 DRIVER_SELFBUILD_SMOKE_TIMEOUT="$driver_selfbuild_smoke_build_timeout" DRIVER_SELFBUILD_SMOKE_REPORT="$driver_selfbuild_report" DRIVER_SELFBUILD_SMOKE_OUTPUT="$driver_selfbuild_output" DRIVER_SELFBUILD_SMOKE_REQUIRE_REBUILD="$driver_selfbuild_require_rebuild" DRIVER_SELFBUILD_SMOKE_MAX_STAGE0_ATTEMPTS="$driver_selfbuild_max_stage0_attempts" DRIVER_SELFBUILD_SMOKE_MULTI="$driver_selfbuild_multi" DRIVER_SELFBUILD_SMOKE_MULTI_FORCE="$driver_selfbuild_multi_force" DRIVER_SELFBUILD_SMOKE_INCREMENTAL="$driver_selfbuild_incremental" DRIVER_SELFBUILD_SMOKE_JOBS="$driver_selfbuild_jobs" sh src/tooling/verify_backend_driver_selfbuild_smoke.sh
  fi
fi

if [ "$run_selfhost" != "" ] && [ "$run_selfhost_perf" = "1" ]; then
  selfhost_perf_session="$selfhost_session"
  if [ "$selfhost_mode" = "fast" ] && [ "$run_selfhost_strict" = "1" ]; then
    case "$selfhost_perf_use_strict_session" in
      1|true|TRUE|yes|YES|on|ON)
        selfhost_perf_session="${BACKEND_PROD_SELFHOST_STRICT_SESSION:-$selfhost_session}"
        ;;
    esac
  fi
  run_required "backend.selfhost_perf_regression" env \
    SELFHOST_PERF_SESSION="$selfhost_perf_session" \
    SELFHOST_PERF_AUTO_BUILD=0 \
    sh src/tooling/verify_backend_selfhost_perf_regression.sh
fi

if [ "${BACKEND_DRIVER:-}" = "" ] && [ "$backend_driver_explicit" != "1" ] && [ "$run_selfhost" != "" ] && [ "$main_allow_selfhost_driver" = "1" ]; then
  required_main_driver="artifacts/backend_selfhost_self_obj/cheng.stage2"
  if [ ! -x "$required_main_driver" ]; then
    echo "[backend_prod_closure] missing required selfhost main driver: $required_main_driver" 1>&2
    exit 1
  fi
  if ! driver_runnable "$required_main_driver" || ! driver_stage1_compile_ok "$required_main_driver"; then
    echo "[backend_prod_closure] required selfhost main driver failed health probe: $required_main_driver" 1>&2
    exit 1
  fi
  export BACKEND_DRIVER="$required_main_driver"
  echo "[backend_prod_closure] main gates driver: $BACKEND_DRIVER" 1>&2
fi
if [ "${BACKEND_DRIVER:-}" = "" ] && [ "$backend_driver_explicit" != "1" ] && [ "${driver_selfbuild_output:-}" != "" ] && [ -x "${driver_selfbuild_output}" ]; then
  if driver_runnable "$driver_selfbuild_output" && driver_stage1_compile_ok "$driver_selfbuild_output"; then
    export BACKEND_DRIVER="$driver_selfbuild_output"
    echo "[backend_prod_closure] info: use selfbuild smoke driver for main gates: $BACKEND_DRIVER" 1>&2
  fi
fi
if [ "${BACKEND_DRIVER:-}" = "" ] && [ "$run_selfhost" != "" ] && [ "$main_allow_selfhost_driver" = "0" ]; then
  echo "[backend_prod_closure] info: keep stable driver for main gates (strict non-C-ABI policy); selfhost is used by abi_v2 gate" 1>&2
fi
if [ "${BACKEND_DRIVER:-}" = "" ]; then
  export BACKEND_DRIVER="$(env \
    BACKEND_DRIVER_PATH_ALLOW_SELFHOST="$main_driver_path_allow_selfhost" \
    BACKEND_DRIVER_PATH_PREFER_REBUILD="${BACKEND_DRIVER_PATH_PREFER_REBUILD:-1}" \
    BACKEND_DRIVER_ALLOW_FALLBACK="${BACKEND_DRIVER_ALLOW_FALLBACK:-0}" \
    sh src/tooling/backend_driver_path.sh 2>/dev/null || true)"
fi
if ! driver_runnable "${BACKEND_DRIVER:-}" || ! driver_stage1_compile_ok "${BACKEND_DRIVER:-}"; then
  echo "[backend_prod_closure] selected main driver failed health probe: ${BACKEND_DRIVER:-<empty>}" 1>&2
  exit 1
fi

# Main driver has been validated above; avoid repeated stage1 smoke in each gate.
if [ "${BACKEND_DRIVER_PATH_STAGE1_STRICT_SMOKE:-}" = "" ]; then
  export BACKEND_DRIVER_PATH_STAGE1_STRICT_SMOKE=0
fi

opt_gate_driver="${BACKEND_OPT_DRIVER:-}"
opt_gate_driver_env=""
if [ "$opt_gate_driver" != "" ]; then
  if [ ! -x "$opt_gate_driver" ]; then
    echo "[backend_prod_closure] opt gate driver missing: $opt_gate_driver" 1>&2
    exit 1
  fi
  if ! driver_help_ok "$opt_gate_driver"; then
    echo "[backend_prod_closure] opt gate driver failed health probe: $opt_gate_driver" 1>&2
    exit 1
  fi
  opt_gate_driver_env="BACKEND_DRIVER=$opt_gate_driver"
  echo "[backend_prod_closure] opt gates driver: $opt_gate_driver" 1>&2
fi

closedloop_fullspec="${BACKEND_RUN_FULLSPEC:-1}"
closedloop_timeout="$selfhost_gate_timeout"
if [ "$closedloop_fullspec" = "1" ]; then
  closedloop_timeout="${BACKEND_PROD_CLOSEDLOOP_TIMEOUT:-120}"
fi

# normalize UIR iteration knobs for a stable and bounded closure profile
uir_opt2_iters="$(prod_closure_normalize_int "$uir_opt2_iters" 5 1 32)"
uir_opt3_iters="$(prod_closure_normalize_int "$uir_opt3_iters" 4 1 32)"
uir_opt3_cleanup_iters="$(prod_closure_normalize_int "$uir_opt3_cleanup_iters" 3 1 32)"
uir_cfg_canon_iters="$(prod_closure_normalize_int "$uir_cfg_canon_iters" 1 1 16)"
uir_aggressive_iters="$(prod_closure_normalize_int "$uir_aggressive_iters" 2 1 16)"
uir_inline_iters="$(prod_closure_normalize_int "$uir_inline_iters" 4 1 16)"
uir_simd_max_width="$(prod_closure_normalize_int "$uir_simd_max_width" 0 0 256)"
if [ "$uir_simd" = "auto" ]; then
  if [ "$run_opt3" != "" ]; then
    uir_simd="1"
  else
    uir_simd="0"
  fi
fi
case "$uir_simd" in
  0|1) ;;
  *)
    echo "[backend_prod_closure] invalid SIMD toggle (expected auto|0|1): $uir_simd" 1>&2
    exit 2
    ;;
esac

if [ "$prod_target" = "" ]; then
  ambient_target="${BACKEND_TARGET:-}"
  case "$ambient_target" in
    ""|arm64-apple-darwin|x86_64-apple-darwin)
      if [ "$ambient_target" = "" ]; then
        prod_target="arm64-apple-darwin"
      else
        prod_target="$ambient_target"
      fi
      ;;
    *)
      prod_target="arm64-apple-darwin"
      echo "[backend_prod_closure] info: ignore ambient BACKEND_TARGET=$ambient_target for opt/exe gates; use $prod_target (set BACKEND_PROD_TARGET to override)" 1>&2
      ;;
  esac
fi

if [ "$validate" != "" ]; then
  run_required_timeout "backend.closedloop" "$closedloop_timeout" env ABI=v2_noptr STAGE1_STD_NO_POINTERS=1 STAGE1_STD_NO_POINTERS_STRICT=0 STAGE1_NO_POINTERS_NON_C_ABI=1 STAGE1_NO_POINTERS_NON_C_ABI_INTERNAL=1 BACKEND_LINKER=self BACKEND_RUN_FULLSPEC="$closedloop_fullspec" BACKEND_VALIDATE=1 sh src/tooling/verify_backend_closedloop.sh
else
  run_required_timeout "backend.closedloop" "$closedloop_timeout" env ABI=v2_noptr STAGE1_STD_NO_POINTERS=1 STAGE1_STD_NO_POINTERS_STRICT=0 STAGE1_NO_POINTERS_NON_C_ABI=1 STAGE1_NO_POINTERS_NON_C_ABI_INTERNAL=1 BACKEND_LINKER=self BACKEND_RUN_FULLSPEC="$closedloop_fullspec" sh src/tooling/verify_backend_closedloop.sh
fi
linker_abi_core_driver="${BACKEND_LINKER_ABI_CORE_DRIVER:-}"
if [ "$linker_abi_core_driver" = "" ]; then
  for cand in \
    "artifacts/backend_seed/cheng.stage2" \
    "${BACKEND_DRIVER:-}" \
    "artifacts/backend_driver/cheng" \
    "dist/releases/current/cheng"; do
    [ "$cand" != "" ] || continue
    if [ -x "$cand" ]; then
      linker_abi_core_driver="$cand"
      break
    fi
  done
fi
if [ "$linker_abi_core_driver" != "" ]; then
  run_required "backend.linker_abi_core" env \
    BACKEND_LINKER_ABI_CORE_DRIVER="$linker_abi_core_driver" \
    BACKEND_LINKER_ABI_CORE_ALLOW_SELFHOST=1 \
    STAGE1_NO_POINTERS_NON_C_ABI=0 \
    STAGE1_NO_POINTERS_NON_C_ABI_INTERNAL=0 \
    BACKEND_MULTI=0 \
    BACKEND_MULTI_FORCE=0 \
    sh src/tooling/verify_backend_linker_abi_core.sh
else
  run_required "backend.linker_abi_core" env \
    BACKEND_LINKER_ABI_CORE_ALLOW_SELFHOST=1 \
    STAGE1_NO_POINTERS_NON_C_ABI=0 \
    STAGE1_NO_POINTERS_NON_C_ABI_INTERNAL=0 \
    BACKEND_MULTI=0 \
    BACKEND_MULTI_FORCE=0 \
    sh src/tooling/verify_backend_linker_abi_core.sh
fi
run_required "backend.no_legacy_refs" sh src/tooling/verify_backend_no_legacy_refs.sh
run_required "backend.profile_schema" sh src/tooling/verify_backend_profile_schema.sh
run_required "tooling.cmdline_runner" sh src/tooling/verify_tooling_cmdline.sh
run_required "backend.import_cycle_predeclare" sh src/tooling/verify_backend_import_cycle_predeclare.sh
run_required "backend.rawptr_contract" sh src/tooling/verify_backend_rawptr_contract.sh
run_required "backend.rawptr_surface_forbid" sh src/tooling/verify_backend_rawptr_surface_forbid.sh
run_required "backend.ffi_slice_shim" sh src/tooling/verify_backend_ffi_slice_shim.sh
run_required "backend.ffi_outptr_tuple" sh src/tooling/verify_backend_ffi_outptr_tuple.sh
run_required "backend.ffi_handle_sandbox" sh src/tooling/verify_backend_ffi_handle_sandbox.sh
run_required "backend.ffi_borrow_bridge" sh src/tooling/verify_backend_ffi_borrow_bridge.sh
run_required "backend.mem_contract" sh src/tooling/verify_backend_mem_contract.sh
run_required "backend.dod_contract" sh src/tooling/verify_backend_dod_contract.sh
run_required "backend.mem_image_core" sh src/tooling/verify_backend_mem_image_core.sh
run_required "backend.mem_exe_emit" env BACKEND_LINKER=self BACKEND_MEM_EXE_EMIT_REQUIRE_DRIVER_SIDECAR_ZERO=1 sh src/tooling/verify_backend_mem_exe_emit.sh
run_required "backend.profile_baseline" sh src/tooling/verify_backend_profile_baseline.sh
run_required "backend.dual_track" sh src/tooling/verify_backend_dual_track.sh
run_required "backend.noalias_opt" env \
  STAGE1_SKIP_OWNERSHIP=0 \
  UIR_NOALIAS_REQUIRE_PROOF=1 \
  sh src/tooling/verify_backend_noalias_opt.sh
run_required "backend.egraph_cost" env \
  STAGE1_SKIP_OWNERSHIP=0 \
  UIR_NOALIAS_REQUIRE_PROOF=1 \
  UIR_EGRAPH_REQUIRE_PROOF=1 \
  sh src/tooling/verify_backend_egraph_cost.sh
run_required "backend.dod_opt_regression" env \
  STAGE1_SKIP_OWNERSHIP=0 \
  UIR_NOALIAS_REQUIRE_PROOF=1 \
  UIR_EGRAPH_REQUIRE_PROOF=1 \
  sh src/tooling/verify_backend_dod_opt_regression.sh
linkerless_gate_driver="${BACKEND_LINKERLESS_DRIVER:-}"
if [ "$linkerless_gate_driver" != "" ]; then
  if [ ! -x "$linkerless_gate_driver" ]; then
    echo "[backend_prod_closure] backend.linkerless_dev driver missing: $linkerless_gate_driver" 1>&2
    exit 1
  fi
  if ! driver_help_ok "$linkerless_gate_driver"; then
    echo "[backend_prod_closure] backend.linkerless_dev driver failed health probe: $linkerless_gate_driver" 1>&2
    exit 1
  fi
  run_required "backend.linkerless_dev" env BACKEND_LINKER=self BACKEND_BUILD_TRACK=dev BACKEND_DRIVER="$linkerless_gate_driver" sh src/tooling/verify_backend_linkerless_dev.sh
else
  run_required "backend.linkerless_dev" env BACKEND_LINKER=self BACKEND_BUILD_TRACK=dev sh src/tooling/verify_backend_linkerless_dev.sh
fi
run_required "backend.hotpatch_meta" env BACKEND_LINKER=self BACKEND_BUILD_TRACK=dev sh src/tooling/verify_backend_hotpatch_meta.sh
run_required "backend.hotpatch_inplace" env BACKEND_LINKER=self BACKEND_BUILD_TRACK=dev sh src/tooling/verify_backend_hotpatch_inplace.sh
run_required "backend.incr_patch_fastpath" env BACKEND_LINKER=self BACKEND_BUILD_TRACK=dev sh src/tooling/verify_backend_incr_patch_fastpath.sh
run_required "backend.mem_patch_regression" env BACKEND_LINKER=self BACKEND_BUILD_TRACK=dev sh src/tooling/verify_backend_mem_patch_regression.sh
run_required "backend.hotpatch" env BACKEND_LINKER=self BACKEND_BUILD_TRACK=dev sh src/tooling/verify_backend_hotpatch.sh
run_required "backend.release_system_link" env BACKEND_BUILD_TRACK=release BACKEND_LINKER=system BACKEND_NO_RUNTIME_C=0 sh src/tooling/verify_backend_release_c_o3_lto.sh
run_required "backend.plugin_isolation" sh src/tooling/verify_backend_plugin_isolation.sh
run_required "backend.noptr_exemption_scope" sh src/tooling/verify_backend_noptr_exemption_scope.sh
# ABI/no-pointer dedicated compile-only gates still exist for legacy obj coverage,
# but are no longer part of the linkerless production required closure.

if [ "$run_fullchain" != "" ]; then
  run_optional "backend.fullchain_bootstrap.obj_only" env FULLCHAIN_OBJ_ONLY=1 sh src/tooling/verify_fullchain_bootstrap.sh
fi

if [ "$run_det_strict" != "" ]; then
  # shellcheck disable=SC2086
  run_required "backend.determinism_strict" env $opt_gate_driver_env BACKEND_TARGET="$prod_target" sh src/tooling/verify_backend_determinism_strict.sh
  # shellcheck disable=SC2086
  run_optional "backend.exe_determinism_strict" env $opt_gate_driver_env BACKEND_TARGET="$prod_target" sh src/tooling/verify_backend_exe_determinism_strict.sh
fi

if [ "$run_opt" != "" ]; then
  # shellcheck disable=SC2086
  run_required "backend.opt" env $opt_gate_driver_env BACKEND_TARGET="$prod_target" sh src/tooling/verify_backend_opt.sh
fi

if [ "$run_opt2" != "" ]; then
  # shellcheck disable=SC2086
  run_required "backend.opt2" env \
    $opt_gate_driver_env \
    BACKEND_TARGET="$prod_target" \
    UIR_OPT2_ITERS="$uir_opt2_iters" \
    UIR_INLINE_ITERS="$uir_inline_iters" \
    UIR_SIMD="$uir_simd" \
    UIR_SIMD_MAX_WIDTH="$uir_simd_max_width" \
    UIR_SIMD_POLICY="$uir_simd_policy" \
    UIR_CFG_CANON_ITERS="$uir_cfg_canon_iters" \
    sh src/tooling/verify_backend_opt2.sh
  # shellcheck disable=SC2086
  run_required "backend.multi_lto" env $opt_gate_driver_env BACKEND_TARGET="$prod_target" sh src/tooling/verify_backend_multi_lto.sh
  if [ "$run_multi_perf" = "1" ]; then
    # shellcheck disable=SC2086
    run_required "backend.multi_perf_regression" env \
      $opt_gate_driver_env \
      BACKEND_TARGET="$prod_target" \
      MULTI_PERF_TIMEOUT="${BACKEND_PROD_MULTI_PERF_TIMEOUT:-$selfhost_gate_timeout}" \
      sh src/tooling/verify_backend_multi_perf_regression.sh
  fi
  if [ "$run_opt3" != "" ]; then
    # shellcheck disable=SC2086
    run_required "backend.opt3" env \
      $opt_gate_driver_env \
      BACKEND_TARGET="$prod_target" \
      UIR_OPT3_ITERS="$uir_opt3_iters" \
      UIR_OPT3_CLEANUP_ITERS="$uir_opt3_cleanup_iters" \
      UIR_INLINE_ITERS="$uir_inline_iters" \
      UIR_SIMD="$uir_simd" \
      UIR_SIMD_MAX_WIDTH="$uir_simd_max_width" \
      UIR_SIMD_POLICY="$uir_simd_policy" \
      UIR_CFG_CANON_ITERS="$uir_cfg_canon_iters" \
      sh src/tooling/verify_backend_opt3.sh
    # shellcheck disable=SC2086
    run_required "backend.simd" env \
      $opt_gate_driver_env \
      BACKEND_TARGET="$prod_target" \
      UIR_OPT3_ITERS="$uir_opt3_iters" \
      UIR_OPT3_CLEANUP_ITERS="$uir_opt3_cleanup_iters" \
      UIR_INLINE_ITERS="$uir_inline_iters" \
      UIR_CFG_CANON_ITERS="$uir_cfg_canon_iters" \
      UIR_SIMD=1 \
      UIR_SIMD_MAX_WIDTH="${BACKEND_PROD_UIR_SIMD_MAX_WIDTH:-16}" \
      UIR_SIMD_POLICY="$uir_simd_policy" \
      sh src/tooling/verify_backend_simd.sh
    if [ "$run_uir_aggressive" = "1" ]; then
      # shellcheck disable=SC2086
      run_required "backend.opt3.aggressive" env \
        $opt_gate_driver_env \
        BACKEND_TARGET="$prod_target" \
        UIR_AGGRESSIVE=1 \
        UIR_FULL_ITERS="$uir_aggressive_iters" \
        UIR_OPT3_ITERS="$uir_opt3_iters" \
        UIR_OPT3_CLEANUP_ITERS="$uir_opt3_cleanup_iters" \
        UIR_INLINE_ITERS="$uir_inline_iters" \
        UIR_SIMD="$uir_simd" \
        UIR_SIMD_MAX_WIDTH="$uir_simd_max_width" \
        UIR_SIMD_POLICY="$uir_simd_policy" \
        UIR_CFG_CANON_ITERS="$uir_cfg_canon_iters" \
        sh src/tooling/verify_backend_opt3.sh
    fi
  fi
fi

if [ "$run_uir_stability" != "" ]; then
  # shellcheck disable=SC2086
  run_required "backend.uir_stability" env \
    $opt_gate_driver_env \
    BACKEND_TARGET="$prod_target" \
    UIR_STABILITY_ITERS="${BACKEND_PROD_UIR_STABILITY_ITERS:-3}" \
    UIR_OPT2_ITERS="$uir_opt2_iters" \
    UIR_OPT3_ITERS="$uir_opt3_iters" \
    UIR_OPT3_CLEANUP_ITERS="$uir_opt3_cleanup_iters" \
    UIR_CFG_CANON_ITERS="$uir_cfg_canon_iters" \
    UIR_INLINE_ITERS="$uir_inline_iters" \
    UIR_SIMD="$uir_simd" \
    UIR_SIMD_MAX_WIDTH="$uir_simd_max_width" \
    UIR_SIMD_POLICY="$uir_simd_policy" \
    sh src/tooling/verify_backend_uir_stability.sh
fi

if [ "$run_ssa" != "" ]; then
  # shellcheck disable=SC2086
  run_required "backend.ssa" env $opt_gate_driver_env BACKEND_TARGET="$prod_target" sh src/tooling/verify_backend_ssa.sh
fi

if [ "$run_ffi" != "" ]; then
  # shellcheck disable=SC2086
  run_required "backend.ffi_abi" env $opt_gate_driver_env BACKEND_TARGET="$prod_target" sh src/tooling/verify_backend_ffi_abi.sh
fi

if [ "$run_self_linker_gates" = "1" ]; then
  self_linker_gate_driver="${BACKEND_SELF_LINKER_DRIVER:-artifacts/backend_seed/cheng.stage2}"
  if [ "$self_linker_gate_driver" != "" ] && [ -x "$self_linker_gate_driver" ]; then
    run_required "backend.self_linker.elf" env BACKEND_SELF_LINKER_DRIVER="$self_linker_gate_driver" sh src/tooling/verify_backend_self_linker_elf.sh
    run_required "backend.self_linker.coff" env BACKEND_SELF_LINKER_DRIVER="$self_linker_gate_driver" sh src/tooling/verify_backend_self_linker_coff.sh
  else
    run_required "backend.self_linker.elf" sh src/tooling/verify_backend_self_linker_elf.sh
    run_required "backend.self_linker.coff" sh src/tooling/verify_backend_self_linker_coff.sh
  fi
else
  echo "== backend.self_linker (skip by config: BACKEND_RUN_SELF_LINKER_GATES=0) =="
fi
run_optional "backend.coff_lld_link" sh src/tooling/verify_backend_coff_lld_link.sh

if [ "$run_exe_det" != "" ]; then
  # shellcheck disable=SC2086
  run_optional "backend.exe_determinism" env $opt_gate_driver_env BACKEND_TARGET="$prod_target" sh src/tooling/verify_backend_exe_determinism.sh
fi

if [ "$run_sanitizer" != "" ]; then
  # shellcheck disable=SC2086
  run_optional "backend.sanitizer" env $opt_gate_driver_env BACKEND_TARGET="$prod_target" sh src/tooling/verify_backend_sanitizer.sh
fi

if [ "$run_debug" != "" ]; then
  # shellcheck disable=SC2086
  run_optional "backend.debug" env $opt_gate_driver_env BACKEND_TARGET="$prod_target" sh src/tooling/verify_backend_debug.sh
fi

stage2_driver="artifacts/backend_selfhost_self_obj/cheng.stage2"
release_driver="${BACKEND_RELEASE_DRIVER:-${BACKEND_DRIVER:-}}"
if [ "$release_driver" = "" ] && [ -x "$stage2_driver" ]; then
  release_driver="$stage2_driver"
fi
manifest_args=""
bundle_args=""
if [ "$release_driver" != "" ] && [ -x "$release_driver" ]; then
  manifest_args="$manifest_args --driver:$release_driver"
  bundle_args="$bundle_args --driver:$release_driver"
fi

fullchain_bin="artifacts/fullchain/bin"
for extra in "$fullchain_bin/cheng_pkg_source" "$fullchain_bin/cheng_pkg" "$fullchain_bin/cheng_storage"; do
  if [ -f "$extra" ]; then
    bundle_args="$bundle_args --extra:$extra"
  fi
done
for extra in \
  "src/tooling/chengb.sh" \
  "src/tooling/chengc.sh" \
  "src/tooling/detect_host_target.sh" \
  "src/tooling/backend_link_env.sh" \
  "src/tooling/summarize_timeout_diag.sh" \
  "src/tooling/verify_backend_selfhost_perf_regression.sh" \
  "src/tooling/verify_backend_multi_perf_regression.sh" \
  "src/tooling/verify_backend_stage0_no_compat.sh" \
  "src/tooling/verify_backend_selfhost_strict_noreuse_probe.sh" \
  "src/tooling/verify_backend_selfhost_parallel_perf.sh" \
  "src/tooling/verify_backend_mir_borrow.sh" \
  "src/tooling/build_backend_profile_schema.sh" \
  "src/tooling/build_backend_mem_contract.sh" \
  "src/tooling/build_backend_rawptr_contract.sh" \
  "src/tooling/build_backend_dod_contract.sh" \
  "src/tooling/build_backend_profile_baseline.sh" \
  "src/tooling/verify_backend_profile_schema.sh" \
  "src/tooling/verify_backend_import_cycle_predeclare.sh" \
  "src/tooling/verify_backend_mem_contract.sh" \
  "src/tooling/verify_backend_rawptr_contract.sh" \
  "src/tooling/verify_backend_rawptr_surface_forbid.sh" \
  "src/tooling/verify_backend_ffi_slice_shim.sh" \
  "src/tooling/verify_backend_ffi_outptr_tuple.sh" \
  "src/tooling/verify_backend_dod_contract.sh" \
  "src/tooling/verify_backend_mem_image_core.sh" \
  "src/tooling/verify_backend_mem_exe_emit.sh" \
  "src/tooling/cleanup_backend_obj_like_artifacts.sh" \
  "src/tooling/verify_backend_no_obj_artifacts.sh" \
  "src/tooling/verify_backend_profile_baseline.sh" \
  "src/tooling/resolve_system_linker.sh" \
  "src/tooling/verify_backend_dual_track.sh" \
  "src/tooling/verify_backend_release_rollback_drill.sh" \
  "src/tooling/verify_backend_linkerless_dev.sh" \
  "src/tooling/verify_backend_hotpatch_meta.sh" \
  "src/tooling/verify_backend_hotpatch_inplace.sh" \
  "src/tooling/verify_backend_incr_patch_fastpath.sh" \
  "src/tooling/verify_backend_mem_patch_regression.sh" \
  "src/tooling/backend_hotpatch_apply.sh" \
  "src/tooling/verify_backend_hotpatch.sh" \
  "src/tooling/verify_backend_dod_soa.sh" \
  "src/tooling/verify_backend_noalias_opt.sh" \
  "src/tooling/verify_backend_egraph_cost.sh" \
  "src/tooling/verify_backend_dod_opt_regression.sh" \
  "src/tooling/backend_dod_opt_regression_baseline.env" \
  "src/tooling/backend_dod_soa_baseline.env" \
  "src/tooling/backend_profile_schema.env" \
  "src/tooling/backend_mem_contract.env" \
  "src/tooling/backend_dod_contract.env" \
  "src/tooling/backend_profile_baseline.env" \
  "docs/backend-mem-hotpatch-contract.md" \
  "src/tooling/verify_backend_driver_selfbuild_smoke.sh" \
  "src/tooling/selfhost_perf_baseline.env" \
  "src/tooling/multi_perf_baseline.env" \
  "src/tooling/verify_libp2p_frontier.sh" \
  "src/tooling/verify_backend_selfhost_bootstrap_fast.sh" \
  "tests/cheng/backend/fixtures/ffi_importc_slice_seq_i32.cheng" \
  "tests/cheng/backend/fixtures/ffi_outptr_tuple_importc_pair_i32.cheng" \
  "tests/cheng/backend/fixtures/ffi_outptr_tuple_importc_status_i32_objonly.cheng" \
  "tests/cheng/backend/fixtures/compile_fail_ffi_outptr_tuple_arity_mismatch.cheng" \
  "tests/cheng/backend/fixtures/return_forward_decl_call.cheng" \
  "tests/cheng/backend/fixtures/compile_fail_import_cycle_entry.cheng" \
  "tests/cheng/backend/fixtures/import_cycle_a.cheng" \
  "tests/cheng/backend/fixtures/import_cycle_b.cheng" \
  "tests/cheng/backend/fixtures/compile_fail_ffi_importc_slice_openarray_i32.cheng" \
  "tests/cheng/backend/fixtures/compile_fail_ffi_slice_user_raw_ptr_surface.cheng" \
  "tests/cheng/backend/fixtures/hotpatch_slot_v1.cheng" \
  "tests/cheng/backend/fixtures/hotpatch_slot_v2.cheng" \
  "tests/cheng/backend/fixtures/hotpatch_slot_v3_overflow.cheng"; do
  if [ -f "$extra" ]; then
    bundle_args="$bundle_args --extra:$extra"
  fi
done

# shellcheck disable=SC2086
run_required "backend.release_manifest" sh src/tooling/backend_release_manifest.sh --out:"$manifest" $manifest_args
if [ "$run_bundle" != "" ]; then
  # shellcheck disable=SC2086
  run_required "backend.release_bundle" sh src/tooling/backend_release_bundle.sh --out:"$bundle" --manifest:"$manifest" $bundle_args
  if [ "$run_sign" != "" ]; then
    run_optional "backend.release_sign" sh src/tooling/backend_release_sign.sh --manifest:"$manifest" --bundle:"$bundle"
    run_optional "backend.release_verify" sh src/tooling/backend_release_verify.sh --manifest:"$manifest" --bundle:"$bundle"
    if [ "$run_release_rollback_drill" != "" ]; then
      run_required "backend.release_rollback_drill" sh src/tooling/verify_backend_release_rollback_drill.sh --manifest:"$manifest" --bundle:"$bundle" --dst:"${BACKEND_RELEASE_DST:-dist/releases}"
    fi
    if [ "$run_publish" != "" ]; then
      run_required "backend.release_publish" sh src/tooling/backend_release_publish.sh --manifest:"$manifest" --bundle:"$bundle" --dst:"${BACKEND_RELEASE_DST:-dist/releases}"
    fi
  fi
fi
if [ "$run_stress" != "" ]; then
  run_required "backend.stress" sh src/tooling/verify_backend_stress.sh
  run_required "backend.concurrency_stress" sh src/tooling/verify_backend_concurrency_stress.sh
fi
if [ "$run_mm" != "" ]; then
  mm_gate_driver="${BACKEND_MM_DRIVER:-}"
  if [ "$mm_gate_driver" != "" ]; then
    if [ ! -x "$mm_gate_driver" ]; then
      echo "[backend_prod_closure] backend.mm driver missing: $mm_gate_driver" 1>&2
      exit 1
    fi
    if ! driver_help_ok "$mm_gate_driver"; then
      echo "[backend_prod_closure] backend.mm driver failed health probe: $mm_gate_driver" 1>&2
      exit 1
    fi
    run_required "backend.mm" env \
      BACKEND_DRIVER="$mm_gate_driver" \
      STAGE1_NO_POINTERS_NON_C_ABI=0 \
      STAGE1_NO_POINTERS_NON_C_ABI_INTERNAL=0 \
      sh src/tooling/verify_backend_mm.sh
  else
    run_required "backend.mm" env \
      STAGE1_NO_POINTERS_NON_C_ABI=0 \
      STAGE1_NO_POINTERS_NON_C_ABI_INTERNAL=0 \
      sh src/tooling/verify_backend_mm.sh
  fi
fi

run_required "backend.no_obj_cleanup" sh src/tooling/cleanup_backend_obj_like_artifacts.sh
run_required "backend.no_obj_artifacts" sh src/tooling/verify_backend_no_obj_artifacts.sh

print_timing_summary
echo "backend_prod_closure ok"
