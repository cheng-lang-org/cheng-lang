#!/usr/bin/env sh
. "$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)/env_prefix_bridge.sh"
set -eu
(set -o pipefail) 2>/dev/null && set -o pipefail

usage() {
  cat <<'EOF'
Usage:
  src/tooling/verify_backend_ci_obj_only.sh [--seed:<path>] [--seed-id:<id>] [--seed-tar:<path>]
                                            [--require-seed]
                                            [--strict]

Notes:
  - Runs the backend .o-only bootstrap + fullchain tool smoke.
  - Backend-only CI gate (C frontend path disabled).
  - Includes selfhost performance regression gate (`verify_backend_selfhost_perf_regression.sh`).
  - Selfhost perf limits default to `src/tooling/selfhost_perf_baseline.env` (override via `SELFHOST_PERF_BASELINE` / `SELFHOST_PERF_MAX_*`).
  - Includes multi compile performance regression gate (`verify_backend_multi_perf_regression.sh`).
  - Multi perf limits default to `src/tooling/multi_perf_baseline.env` (override via `MULTI_PERF_BASELINE` / `MULTI_PERF_MAX_*`).
  - Also validates the self-linker output format for ELF/COFF (best-effort; use --strict to forbid skip).
  - Requires:
    - Darwin/arm64 host
    - `codesign` (self-link output on macOS 15+ must be signed)
    - a seed backend driver binary (typically from dist/releases)

Defaults:
  - If no seed is provided, uses:
      dist/releases/current_id.txt -> dist/releases/<id>/backend_release.tar.gz
  - Does not fall back to local artifacts seed paths automatically.
EOF
}

seed=""
seed_id=""
seed_tar=""
require_seed="0"
strict="0"
while [ "${1:-}" != "" ]; do
  case "$1" in
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
    --strict)
      strict="1"
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

host_os="$(uname -s 2>/dev/null || echo unknown)"
host_arch="$(uname -m 2>/dev/null || echo unknown)"
case "$host_os/$host_arch" in
  Darwin/arm64)
    ;;
  *)
    echo "verify_backend_ci_obj_only skip: host=$host_os/$host_arch" 1>&2
    exit 2
    ;;
esac

if ! command -v codesign >/dev/null 2>&1; then
  echo "verify_backend_ci_obj_only skip: missing codesign" 1>&2
  exit 2
fi

run_step() {
  name="$1"
  shift
  echo "== ${name} =="
  set +e
  "$@"
  status="$?"
  set -e
  if [ "$status" -eq 0 ]; then
    return 0
  fi
  if [ "$status" -eq 2 ]; then
    if [ "$strict" = "1" ]; then
      echo "[verify_backend_ci_obj_only] ${name} returned skip (strict mode)" 1>&2
      exit 1
    fi
    echo "== ${name} (skip) =="
    return 0
  fi
  exit "$status"
}

seed_from_tar() {
  tar_path="$1"
  if [ ! -f "$tar_path" ]; then
    echo "[Error] seed tar not found: $tar_path" 1>&2
    exit 2
  fi
  out_dir="chengcache/backend_seed_ci_$$"
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

if [ "$seed" = "" ]; then
  if [ "$seed_tar" != "" ]; then
    seed="$(seed_from_tar "$seed_tar")"
  else
    if [ "$seed_id" = "" ] && [ -f "dist/releases/current_id.txt" ]; then
      seed_id="$(cat dist/releases/current_id.txt | tr -d '\r\n')"
    fi
    if [ "$seed_id" != "" ]; then
      for try_tar in "dist/releases/$seed_id/backend_release.tar.gz"; do
        if [ -f "$try_tar" ]; then
          seed_tar="$try_tar"
          seed="$(seed_from_tar "$seed_tar")"
          break
        fi
      done
    fi
  fi
fi

if [ "$seed" = "" ] && [ "$require_seed" = "1" ]; then
  echo "[Error] missing seed: pass --seed:<path> or provide dist/releases/current_id.txt" 1>&2
  exit 2
fi

if [ "$seed" = "" ]; then
  echo "[Error] missing seed: pass --seed/--seed-id/--seed-tar or provide dist/releases/current_id.txt" 1>&2
  exit 2
fi

seed_path="$seed"
case "$seed_path" in
  /*) ;;
  *) seed_path="$root/$seed_path" ;;
esac
if [ ! -x "$seed_path" ]; then
  echo "[Error] seed driver is not executable: $seed_path" 1>&2
  exit 2
fi

if [ "${CLEAN_CHENG_LOCAL:-1}" = "1" ] && [ "${TOOLING_CLEANUP_DEPTH:-0}" = "0" ] && [ "$seed_path" = "$root/cheng" ]; then
  export TOOLING_CLEANUP_DEPTH=1
  cleanup_backend_driver_on_exit() {
    status=$?
    set +e
    sh src/tooling/cleanup_cheng_local.sh
    exit "$status"
  }
  trap cleanup_backend_driver_on_exit EXIT
fi

run_step "backend.ci.selfhost_bootstrap_self_obj" env \
  SELF_OBJ_BOOTSTRAP_STAGE0="$seed_path" \
  sh src/tooling/verify_backend_selfhost_bootstrap_self_obj.sh

perf_session="${SELFHOST_PERF_SESSION:-${SELF_OBJ_BOOTSTRAP_SESSION:-default}}"
run_step "backend.ci.selfhost_perf_regression" env \
  SELFHOST_PERF_SESSION="$perf_session" \
  SELFHOST_PERF_AUTO_BUILD=0 \
  sh src/tooling/verify_backend_selfhost_perf_regression.sh

run_step "backend.ci.multi_perf_regression" sh src/tooling/verify_backend_multi_perf_regression.sh

stage2="artifacts/backend_selfhost_self_obj/cheng.stage2"
if [ ! -x "$stage2" ]; then
  echo "[Error] missing stage2 backend driver: $stage2" 1>&2
  exit 1
fi
export BACKEND_DRIVER="$stage2"

run_step "backend.ci.self_linker.elf" sh src/tooling/verify_backend_self_linker_elf.sh
run_step "backend.ci.self_linker.coff" sh src/tooling/verify_backend_self_linker_coff.sh

run_step "backend.ci.fullchain_obj_only" env \
  FULLCHAIN_OBJ_ONLY=1 \
  sh src/tooling/verify_fullchain_bootstrap.sh

run_step "backend.ci.obj_fullspec_gate" env MM=orc sh src/tooling/verify_backend_obj_fullspec_gate.sh

echo "verify_backend_ci_obj_only ok"
