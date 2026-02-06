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
                                     [--no-stress] [--no-bundle]
                                     [--no-sign]
                                     [--no-ssa]
                                     [--no-selfhost]
                                     [--no-fullchain]
                                     [--fullchain-legacy]
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
  - Default fullchain runs in obj-only mode (no stage1->C fixed-point gate).
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
run_fullchain="1"
fullchain_legacy=""
seed=""
seed_id=""
seed_tar=""
require_seed="0"
run_obj="1"
run_obj_det="1"
run_exe_det="1"
run_sanitizer="1"
run_stress="1"
run_bundle="1"
run_sign="1"
run_mm="1"
run_self_obj_writer="1"
run_publish="1"
only_self_obj_bootstrap=""
allow_skip="1"
manifest="artifacts/backend_prod/release_manifest.json"
bundle="artifacts/backend_prod/backend_release.tar.gz"
debug_explicit="0"

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
    --no-fullchain)
      run_fullchain=""
      ;;
    --fullchain-legacy)
      fullchain_legacy="1"
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

root="$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)"
cd "$root"

if [ "$debug_explicit" = "0" ] && [ "$(uname -s 2>/dev/null || echo unknown)" = "Darwin" ]; then
  run_debug="1"
fi

backend_driver_explicit="0"
if [ "${CHENG_BACKEND_DRIVER:-}" != "" ]; then
  backend_driver_explicit="1"
fi
backend_driver_from_seed="0"
if [ "$backend_driver_explicit" = "1" ] && [ "${CHENG_CLEAN_BACKEND_MVP_DRIVER_LOCAL:-}" = "" ]; then
  export CHENG_CLEAN_BACKEND_MVP_DRIVER_LOCAL=0
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
  extracted="$out_dir/backend_mvp_driver"
  if [ ! -f "$extracted" ]; then
    echo "[Error] seed tar missing backend_mvp_driver: $tar_path" 1>&2
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
  if [ "$seed" = "" ]; then
    if [ "$seed_tar" != "" ]; then
      seed_path="$(seed_from_tar "$seed_tar")"
    else
      if [ "$seed_id" = "" ] && [ -f "dist/releases/current_id.txt" ]; then
        seed_id="$(cat dist/releases/current_id.txt | tr -d '\r\n')"
      fi
      if [ "$seed_id" = "" ] && [ -f "dist/backend/current_id.txt" ]; then
        seed_id="$(cat dist/backend/current_id.txt | tr -d '\r\n')"
      fi
      if [ "$seed_id" != "" ]; then
        for try_tar in \
          "dist/releases/$seed_id/backend_release.tar.gz" \
          "dist/backend/releases/$seed_id/backend_release.tar.gz"; do
          if [ -f "$try_tar" ]; then
            seed_tar="$try_tar"
            seed_path="$(seed_from_tar "$seed_tar")"
            break
          fi
        done
      fi
    fi
  else
    seed_path="$seed"
  fi

  if [ "$seed_path" = "" ] && [ "$require_seed" = "1" ]; then
    echo "[Error] missing seed: pass --seed:<path> or provide dist/releases/current_id.txt (or legacy dist/backend/current_id.txt, --seed-id/--seed-tar)" 1>&2
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
    if [ "$backend_driver_explicit" != "1" ] && [ "${CHENG_BACKEND_DRIVER:-}" = "" ]; then
      export CHENG_BACKEND_DRIVER="$seed_path"
      backend_driver_from_seed="1"
    fi
  fi
fi

run_optional() {
  label="$1"
  shift
  set +e
  "$@"
  status="$?"
  set -e
  if [ "$status" -eq 0 ]; then
    :
  elif [ "$status" -eq 2 ]; then
    if [ "$allow_skip" != "" ]; then
      echo "== $label (skip) =="
    else
      echo "[backend_prod_closure] $label requested skip, but --strict is enabled" 1>&2
      exit 1
    fi
  else
    exit "$status"
  fi
}

if [ "$allow_skip" = "" ]; then
  export CHENG_BACKEND_MATRIX_STRICT=1
fi

if [ "$only_self_obj_bootstrap" != "" ]; then
  if [ "$seed_path" != "" ]; then
    run_optional "backend.selfhost_bootstrap_self_obj" env CHENG_SELF_OBJ_BOOTSTRAP_STAGE0="$seed_path" \
      sh src/tooling/verify_backend_selfhost_bootstrap_self_obj.sh
  else
    run_optional "backend.selfhost_bootstrap_self_obj" sh src/tooling/verify_backend_selfhost_bootstrap_self_obj.sh
  fi
  echo "backend_prod_closure ok"
  exit 0
fi

if [ "$run_selfhost" != "" ]; then
  if [ "$seed_path" != "" ]; then
    run_optional "backend.selfhost_bootstrap" env CHENG_SELF_OBJ_BOOTSTRAP_STAGE0="$seed_path" \
      sh src/tooling/verify_backend_selfhost_bootstrap.sh
  else
    run_optional "backend.selfhost_bootstrap" sh src/tooling/verify_backend_selfhost_bootstrap.sh
  fi
fi

if [ "$backend_driver_explicit" != "1" ]; then
  stage2_self="artifacts/backend_selfhost_self_obj/backend_mvp_driver.stage2"
  stage2_legacy="artifacts/backend_selfhost/backend_mvp_driver.stage2"
  if [ -x "$stage2_self" ]; then
    export CHENG_BACKEND_DRIVER="$stage2_self"
  elif [ -x "$stage2_legacy" ]; then
    export CHENG_BACKEND_DRIVER="$stage2_legacy"
  fi
fi

if [ "$validate" != "" ]; then
  CHENG_BACKEND_VALIDATE=1 sh src/tooling/verify_backend_closedloop.sh
else
  sh src/tooling/verify_backend_closedloop.sh
fi

run_optional "backend.obj_fullspec_gate" env CHENG_MM=orc sh src/tooling/verify_backend_obj_fullspec_gate.sh

if [ "$run_fullchain" != "" ]; then
  if [ "$fullchain_legacy" != "" ]; then
    run_optional "backend.fullchain_bootstrap.legacy" env CHENG_FULLCHAIN_OBJ_ONLY=0 sh src/tooling/verify_fullchain_bootstrap.sh
  else
    run_optional "backend.fullchain_bootstrap.obj_only" env CHENG_FULLCHAIN_OBJ_ONLY=1 sh src/tooling/verify_fullchain_bootstrap.sh
  fi
fi

if [ "$run_det_strict" != "" ]; then
  sh src/tooling/verify_backend_determinism_strict.sh
  run_optional "backend.obj_determinism_strict" sh src/tooling/verify_backend_obj_determinism_strict.sh
  run_optional "backend.exe_determinism_strict" sh src/tooling/verify_backend_exe_determinism_strict.sh
fi

if [ "$run_opt" != "" ]; then
  sh src/tooling/verify_backend_opt.sh
fi

if [ "$run_opt2" != "" ]; then
  sh src/tooling/verify_backend_opt2.sh
  sh src/tooling/verify_backend_multi_lto.sh
fi

if [ "$run_ssa" != "" ]; then
  sh src/tooling/verify_backend_ssa.sh
fi

if [ "$run_ffi" != "" ]; then
  sh src/tooling/verify_backend_ffi_abi.sh
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
  run_optional "backend.coff_lld_link" sh src/tooling/verify_backend_coff_lld_link.sh
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

stage2_driver="artifacts/backend_selfhost_self_obj/backend_mvp_driver.stage2"
if [ ! -x "$stage2_driver" ]; then
  stage2_driver="artifacts/backend_selfhost/backend_mvp_driver.stage2"
fi
manifest_args=""
bundle_args=""
if [ -x "$stage2_driver" ]; then
  manifest_args="$manifest_args --driver:$stage2_driver"
  bundle_args="$bundle_args --driver:$stage2_driver"
fi

stage1_backend="artifacts/fullchain/stage1_runner.backend"
if [ -f "$stage1_backend" ]; then
  manifest_args="$manifest_args --stage1-backend:$stage1_backend"
  bundle_args="$bundle_args --extra:$stage1_backend"
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
  "src/tooling/verify_backend_obj_fullspec_gate.sh" \
  "examples/backend_obj_fullspec.cheng"; do
  if [ -f "$extra" ]; then
    bundle_args="$bundle_args --extra:$extra"
  fi
done

# shellcheck disable=SC2086
sh src/tooling/backend_release_manifest.sh --out:"$manifest" $manifest_args
if [ "$run_bundle" != "" ]; then
  # shellcheck disable=SC2086
  sh src/tooling/backend_release_bundle.sh --out:"$bundle" --manifest:"$manifest" $bundle_args
  if [ "$run_sign" != "" ]; then
    run_optional "backend.release_sign" sh src/tooling/backend_release_sign.sh --manifest:"$manifest" --bundle:"$bundle"
    run_optional "backend.release_verify" sh src/tooling/backend_release_verify.sh --manifest:"$manifest" --bundle:"$bundle"
    if [ "$run_publish" != "" ]; then
      sh src/tooling/backend_release_publish.sh --manifest:"$manifest" --bundle:"$bundle" --dst:"${CHENG_BACKEND_RELEASE_DST:-dist/releases}"
    fi
  fi
fi
if [ "$run_stress" != "" ]; then
  sh src/tooling/verify_backend_stress.sh
  sh src/tooling/verify_backend_concurrency_stress.sh
fi
if [ "$run_mm" != "" ]; then
  sh src/tooling/verify_backend_mm.sh
fi

echo "backend_prod_closure ok"
