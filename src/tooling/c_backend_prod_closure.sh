#!/usr/bin/env sh
set -euo pipefail

usage() {
  cat <<'EOF'
Usage:
  src/tooling/c_backend_prod_closure.sh [--frontend:<path>] [--no-bootstrap]
                                        [--determinism] [--no-full]
                                        [--no-system] [--no-fullspec]
                                        [--no-async] [--no-ffi]
                                        [--no-closure] [--no-modules]
                                        [--no-mixed]

Notes:
  - Runs the C-backend production closure in one shot.
  - Defaults to building stage1_runner (bootstrap --skip-determinism).
EOF
}

frontend=""
bootstrap="1"
bootstrap_args="--skip-determinism"
run_full="1"
run_system="1"
run_fullspec="1"
run_async="1"
run_ffi="1"
run_closure="1"
run_modules="1"
run_mixed="1"

while [ "${1:-}" != "" ]; do
  case "$1" in
    --frontend:*)
      frontend="${1#--frontend:}"
      ;;
    --no-bootstrap)
      bootstrap=""
      ;;
    --determinism)
      bootstrap_args=""
      ;;
    --no-full)
      run_full=""
      ;;
    --no-system)
      run_system=""
      ;;
    --no-fullspec)
      run_fullspec=""
      ;;
    --no-async)
      run_async=""
      ;;
    --no-ffi)
      run_ffi=""
      ;;
    --no-closure)
      run_closure=""
      ;;
    --no-modules)
      run_modules=""
      ;;
    --no-mixed)
      run_mixed=""
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

if [ "$bootstrap" != "" ]; then
  sh src/tooling/bootstrap.sh $bootstrap_args
fi

if [ "$frontend" != "" ]; then
  export CHENG_C_FRONTEND="$frontend"
else
  if [ -x "$root/stage1_runner" ]; then
    export CHENG_C_FRONTEND="./stage1_runner"
  else
    echo "[Error] missing stage1_runner; set --frontend:<path>" 1>&2
    exit 2
  fi
fi

if [ "$run_full" != "" ]; then
  sh src/tooling/verify_c_backend_full.sh
fi
if [ "$run_system" != "" ]; then
  sh src/tooling/verify_c_backend_system.sh
fi
if [ "$run_fullspec" != "" ]; then
  sh src/tooling/verify_c_backend_fullspec.sh
fi
if [ "$run_async" != "" ]; then
  sh src/tooling/verify_c_backend_async_runtime.sh
fi
if [ "$run_ffi" != "" ]; then
  sh src/tooling/verify_c_backend_importc_ffi.sh
fi
if [ "$run_closure" != "" ]; then
  sh src/tooling/verify_c_backend_closure_callback.sh
fi
if [ "$run_modules" != "" ]; then
  sh src/tooling/verify_c_backend_modules.sh
fi
if [ "$run_mixed" != "" ]; then
  sh src/tooling/verify_mixed_backend.sh --out:artifacts/hybrid/metrics.env
fi

echo "c_backend_prod_closure ok"
