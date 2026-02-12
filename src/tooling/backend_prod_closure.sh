#!/usr/bin/env sh
set -eu
(set -o pipefail) 2>/dev/null && set -o pipefail

usage() {
  cat <<'EOF'
Usage:
  src/tooling/backend_prod_closure.sh [--no-validate] [--debug] [--no-debug] [--no-ffi] [--no-determinism-strict]
                                     [--no-opt] [--no-obj] [--no-obj-determinism] [--no-exe-determinism]
                                     [--no-opt2]
                                     [--no-sanitizer]
                                     [--stress|--no-stress] [--no-bundle]
                                     [--no-sign]
                                     [--no-ssa]
                                     [--no-selfhost]
                                     [--selfhost-fast|--selfhost-strict]
                                     [--fullchain|--no-fullchain]
                                     [--seed:<path>] [--seed-id:<id>] [--seed-tar:<path>] [--require-seed]
                                     [--only-self-obj-bootstrap]
                                     [--no-self-obj-writer]
                                     [--no-mm]
                                     [--no-publish]
                                     [--strict] [--allow-skip]
                                     [--manifest:<path>] [--bundle:<path>]

Notes:
  - Runs the self-hosted backend production closure (includes best-effort target matrix checks).
  - Default includes MIR validation (CHENG_BACKEND_VALIDATE=1) and emits a release manifest.
  - Fullchain bootstrap gate is opt-in (`--fullchain` or `CHENG_BACKEND_RUN_FULLCHAIN=1`).
  - Default is strict: any step that exits with skip code (2) fails the closure.
  - Use `--allow-skip` to permit optional steps to skip.
  - `--require-seed` requires explicit `--seed`/`--seed-id`/`--seed-tar`.
  - Publish path requires explicit seed (`--seed`/`--seed-id`/`--seed-tar`).
  - Optional fast selfhost mode: `CHENG_BACKEND_PROD_SELFHOST_MODE=fast` (or `CHENG_SELF_OBJ_BOOTSTRAP_MODE=fast`).
EOF
}

validate="1"
run_debug=""
run_ffi="1"
run_det_strict="1"
run_opt="1"
run_opt2="1"
run_ssa="1"
run_selfhost="1"
run_fullchain=""
if [ "${CHENG_BACKEND_RUN_FULLCHAIN:-}" = "1" ]; then
  run_fullchain="1"
fi
seed=""
seed_id=""
seed_tar=""
require_seed="0"
run_obj="1"
run_obj_det="1"
run_exe_det="1"
run_sanitizer="1"
run_stress=""
if [ "${CHENG_BACKEND_RUN_STRESS:-}" = "1" ]; then
  run_stress="1"
fi
run_bundle="1"
run_sign="1"
run_mm="1"
run_self_obj_writer="1"
run_publish="1"
only_self_obj_bootstrap=""
allow_skip=""
manifest="artifacts/backend_prod/release_manifest.json"
bundle="artifacts/backend_prod/backend_release.tar.gz"
debug_explicit="0"
selfhost_timeout="${CHENG_BACKEND_PROD_SELFHOST_TIMEOUT:-60}"
selfhost_reuse="${CHENG_BACKEND_PROD_SELFHOST_REUSE:-${CHENG_SELF_OBJ_BOOTSTRAP_REUSE:-1}}"
selfhost_session="${CHENG_BACKEND_PROD_SELFHOST_SESSION:-${CHENG_SELF_OBJ_BOOTSTRAP_SESSION:-prod}}"
selfhost_mode="${CHENG_BACKEND_PROD_SELFHOST_MODE:-${CHENG_SELF_OBJ_BOOTSTRAP_MODE:-strict}}"

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
      ;;
    --no-ssa)
      run_ssa=""
      ;;
    --no-selfhost)
      run_selfhost=""
      ;;
    --selfhost-fast)
      selfhost_mode="fast"
      ;;
    --selfhost-strict)
      selfhost_mode="strict"
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
    --only-self-obj-bootstrap)
      only_self_obj_bootstrap="1"
      run_debug=""
      run_ffi=""
      run_det_strict=""
      run_opt=""
      run_opt2=""
      run_ssa=""
      run_fullchain=""
      run_obj=""
      run_obj_det=""
      run_exe_det=""
      run_sanitizer=""
      run_stress=""
      run_bundle=""
      run_sign=""
      run_mm=""
      run_self_obj_writer=""
      run_publish=""
      ;;
    --no-obj)
      run_obj=""
      ;;
    --no-obj-determinism)
      run_obj_det=""
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
    --no-self-obj-writer)
      run_self_obj_writer=""
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

root="$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)"
cd "$root"

# Keep local backend driver for the whole closure; many sub-gates rely on it.
export CHENG_CLEAN_CHENG_LOCAL=0

if [ "$debug_explicit" = "0" ] && [ "$(uname -s 2>/dev/null || echo unknown)" = "Darwin" ]; then
  run_debug="1"
fi

backend_driver_explicit="0"
if [ "${CHENG_BACKEND_DRIVER:-}" != "" ]; then
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

run_required() {
  label="$1"
  shift
  start="$(timestamp_now)"
  set +e
  "$@"
  status="$?"
  set -e
  end="$(timestamp_now)"
  duration=$((end - start))
  if [ "$status" -eq 0 ]; then
    record_timing "$label" "ok" "$duration"
  else
    record_timing "$label" "fail" "$duration"
    exit "$status"
  fi
}

run_optional() {
  label="$1"
  shift
  start="$(timestamp_now)"
  set +e
  "$@"
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
    record_timing "$label" "fail" "$duration"
    exit "$status"
  fi
}

has_any_lld() {
  command -v lld-link >/dev/null 2>&1 && return 0
  command -v llvm-lld >/dev/null 2>&1 && return 0
  command -v ld.lld >/dev/null 2>&1 && return 0
  command -v lld >/dev/null 2>&1 && return 0
  return 1
}

if [ "$allow_skip" = "" ]; then
  export CHENG_BACKEND_MATRIX_STRICT=1
fi
if [ "${CHENG_ABI:-}" = "" ]; then
  export CHENG_ABI=v2_noptr
fi
if [ "${CHENG_ABI}" = "v2_noptr" ]; then
  if [ "${CHENG_STAGE1_STD_NO_POINTERS:-}" = "" ]; then
    export CHENG_STAGE1_STD_NO_POINTERS=1
  fi
  if [ "${CHENG_STAGE1_STD_NO_POINTERS_STRICT:-}" = "" ]; then
    export CHENG_STAGE1_STD_NO_POINTERS_STRICT=1
  fi
fi

selfhost_stage0="${seed_path:-}"
if [ "$selfhost_stage0" = "" ] && [ "$backend_driver_explicit" = "1" ]; then
  if [ -x "${CHENG_BACKEND_DRIVER:-}" ]; then
    selfhost_stage0="${CHENG_BACKEND_DRIVER}"
  fi
fi
if [ "$selfhost_stage0" = "" ] && [ -x "artifacts/backend_seed/cheng.stage2" ]; then
  selfhost_stage0="artifacts/backend_seed/cheng.stage2"
fi
if [ "$selfhost_stage0" = "" ] && [ -x "artifacts/backend_selfhost_self_obj/cheng.stage2" ]; then
  selfhost_stage0="artifacts/backend_selfhost_self_obj/cheng.stage2"
fi
if [ "$selfhost_stage0" = "" ] && [ -x "artifacts/backend_selfhost_self_obj/cheng.stage1" ]; then
  selfhost_stage0="artifacts/backend_selfhost_self_obj/cheng.stage1"
fi

if [ "$only_self_obj_bootstrap" != "" ]; then
  if [ "$selfhost_stage0" != "" ]; then
    run_optional "backend.selfhost_bootstrap_self_obj" env CHENG_SELF_OBJ_BOOTSTRAP_TIMEOUT="$selfhost_timeout" CHENG_SELF_OBJ_BOOTSTRAP_REUSE="$selfhost_reuse" CHENG_SELF_OBJ_BOOTSTRAP_SESSION="$selfhost_session" CHENG_SELF_OBJ_BOOTSTRAP_MODE="$selfhost_mode" CHENG_SELF_OBJ_BOOTSTRAP_STAGE0="$selfhost_stage0" \
      sh src/tooling/verify_backend_selfhost_bootstrap_self_obj.sh
  else
    run_optional "backend.selfhost_bootstrap_self_obj" env CHENG_SELF_OBJ_BOOTSTRAP_TIMEOUT="$selfhost_timeout" CHENG_SELF_OBJ_BOOTSTRAP_REUSE="$selfhost_reuse" CHENG_SELF_OBJ_BOOTSTRAP_SESSION="$selfhost_session" CHENG_SELF_OBJ_BOOTSTRAP_MODE="$selfhost_mode" sh src/tooling/verify_backend_selfhost_bootstrap_self_obj.sh
  fi
  print_timing_summary
  echo "backend_prod_closure ok"
  exit 0
fi

if [ "$run_selfhost" != "" ]; then
  if [ "$selfhost_stage0" != "" ]; then
    run_optional "backend.selfhost_bootstrap" env CHENG_SELF_OBJ_BOOTSTRAP_TIMEOUT="$selfhost_timeout" CHENG_SELF_OBJ_BOOTSTRAP_REUSE="$selfhost_reuse" CHENG_SELF_OBJ_BOOTSTRAP_SESSION="$selfhost_session" CHENG_SELF_OBJ_BOOTSTRAP_MODE="$selfhost_mode" CHENG_SELF_OBJ_BOOTSTRAP_STAGE0="$selfhost_stage0" \
      sh src/tooling/verify_backend_selfhost_bootstrap.sh
  else
    run_optional "backend.selfhost_bootstrap" env CHENG_SELF_OBJ_BOOTSTRAP_TIMEOUT="$selfhost_timeout" CHENG_SELF_OBJ_BOOTSTRAP_REUSE="$selfhost_reuse" CHENG_SELF_OBJ_BOOTSTRAP_SESSION="$selfhost_session" CHENG_SELF_OBJ_BOOTSTRAP_MODE="$selfhost_mode" sh src/tooling/verify_backend_selfhost_bootstrap.sh
  fi
fi

if [ "${CHENG_BACKEND_DRIVER:-}" = "" ] && [ -x "artifacts/backend_selfhost_self_obj/cheng.stage2" ]; then
  export CHENG_BACKEND_DRIVER="artifacts/backend_selfhost_self_obj/cheng.stage2"
elif [ "${CHENG_BACKEND_DRIVER:-}" = "" ] && [ -x "artifacts/backend_seed/cheng.stage2" ]; then
  export CHENG_BACKEND_DRIVER="artifacts/backend_seed/cheng.stage2"
fi

if [ "$validate" != "" ]; then
  run_required "backend.closedloop" env CHENG_BACKEND_VALIDATE=1 sh src/tooling/verify_backend_closedloop.sh
else
  run_required "backend.closedloop" sh src/tooling/verify_backend_closedloop.sh
fi

run_required "backend.abi_v2_noptr" sh src/tooling/verify_backend_abi_v2_noptr.sh

run_optional "backend.obj_fullspec_gate" env CHENG_MM=orc sh src/tooling/verify_backend_obj_fullspec_gate.sh

if [ "$run_fullchain" != "" ]; then
  run_optional "backend.fullchain_bootstrap.obj_only" env CHENG_FULLCHAIN_OBJ_ONLY=1 sh src/tooling/verify_fullchain_bootstrap.sh
fi

if [ "$run_det_strict" != "" ]; then
  run_required "backend.determinism_strict" sh src/tooling/verify_backend_determinism_strict.sh
  run_optional "backend.obj_determinism_strict" sh src/tooling/verify_backend_obj_determinism_strict.sh
  run_optional "backend.exe_determinism_strict" sh src/tooling/verify_backend_exe_determinism_strict.sh
fi

if [ "$run_opt" != "" ]; then
  run_required "backend.opt" sh src/tooling/verify_backend_opt.sh
fi

if [ "$run_opt2" != "" ]; then
  run_required "backend.opt2" sh src/tooling/verify_backend_opt2.sh
  run_required "backend.multi_lto" sh src/tooling/verify_backend_multi_lto.sh
fi

if [ "$run_ssa" != "" ]; then
  run_required "backend.ssa" sh src/tooling/verify_backend_ssa.sh
fi

if [ "$run_ffi" != "" ]; then
  run_required "backend.ffi_abi" sh src/tooling/verify_backend_ffi_abi.sh
fi

if [ "$run_self_obj_writer" != "" ]; then
  run_optional "backend.self_obj_writer.elf" sh src/tooling/verify_backend_self_obj_writer.sh
  run_optional "backend.self_obj_writer.elf_determinism" sh src/tooling/verify_backend_self_obj_writer_elf_determinism.sh
  run_optional "backend.self_linker.elf" sh src/tooling/verify_backend_self_linker_elf.sh
  run_optional "backend.self_obj_writer.macho" sh src/tooling/verify_backend_self_obj_writer_macho.sh
  run_optional "backend.self_obj_writer.macho_determinism" sh src/tooling/verify_backend_self_obj_writer_macho_determinism.sh
  run_optional "backend.self_obj_writer.coff" sh src/tooling/verify_backend_self_obj_writer_coff.sh
  run_optional "backend.self_obj_writer.coff_determinism" sh src/tooling/verify_backend_self_obj_writer_coff_determinism.sh
  run_optional "backend.self_linker.coff" sh src/tooling/verify_backend_self_linker_coff.sh
  if has_any_lld; then
    run_optional "backend.coff_lld_link" sh src/tooling/verify_backend_coff_lld_link.sh
  else
    echo "== backend.coff_lld_link (skip: missing lld-link/llvm-lld/ld.lld/lld) =="
    record_timing "backend.coff_lld_link" "skip" "0"
  fi
fi

if [ "$run_obj" != "" ]; then
  run_optional "backend.obj" sh src/tooling/verify_backend_obj.sh
fi

if [ "$run_obj_det" != "" ]; then
  run_optional "backend.obj_determinism" sh src/tooling/verify_backend_obj_determinism.sh
fi

if [ "$run_exe_det" != "" ]; then
  run_optional "backend.exe_determinism" sh src/tooling/verify_backend_exe_determinism.sh
fi

if [ "$run_sanitizer" != "" ]; then
  run_optional "backend.sanitizer" sh src/tooling/verify_backend_sanitizer.sh
fi

if [ "$run_debug" != "" ]; then
  run_optional "backend.debug" sh src/tooling/verify_backend_debug.sh
fi

stage2_driver="artifacts/backend_selfhost_self_obj/cheng.stage2"
manifest_args=""
bundle_args=""
if [ -x "$stage2_driver" ]; then
  manifest_args="$manifest_args --driver:$stage2_driver"
  bundle_args="$bundle_args --driver:$stage2_driver"
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
  "src/tooling/verify_backend_selfhost_bootstrap_fast.sh" \
  "src/tooling/verify_backend_obj_fullspec_gate.sh" \
  "examples/backend_obj_fullspec.cheng"; do
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
    if [ "$run_publish" != "" ]; then
      run_required "backend.release_publish" sh src/tooling/backend_release_publish.sh --manifest:"$manifest" --bundle:"$bundle" --dst:"${CHENG_BACKEND_RELEASE_DST:-dist/releases}"
    fi
  fi
fi
if [ "$run_stress" != "" ]; then
  run_required "backend.stress" sh src/tooling/verify_backend_stress.sh
  run_required "backend.concurrency_stress" sh src/tooling/verify_backend_concurrency_stress.sh
fi
if [ "$run_mm" != "" ]; then
  run_required "backend.mm" sh src/tooling/verify_backend_mm.sh
fi

print_timing_summary
echo "backend_prod_closure ok"
