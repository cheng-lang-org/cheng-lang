#!/usr/bin/env bash
set -euo pipefail

root="$(CDPATH= cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$root"

run_cmd() {
  local name="$1"
  shift
  echo "== ${name} =="
  "$@"
}

if [ "${CHENG_CLOSEDLOOP_PROD:-0}" = "1" ]; then
  if [ "${CHENG_CLOSEDLOOP_STRICT:-}" = "" ]; then
    export CHENG_CLOSEDLOOP_STRICT=1
  fi
  if [ "${CHENG_CLOSEDLOOP_BACKEND_PROD:-}" = "" ]; then
    export CHENG_CLOSEDLOOP_BACKEND_PROD=1
  fi
  export CHENG_STAGE1_FULLSPEC=1
  export CHENG_BACKEND_CLOSEDLOOP=1
  export CHENG_C_BACKEND_CLOSURE=1
  export CHENG_C_BACKEND_CLOSURE_ARGS="--no-bootstrap ${CHENG_C_BACKEND_CLOSURE_ARGS:-}"
  export CHENG_BOOTSTRAP_FORCE_DETERMINISM=1
fi

if [ "${CHENG_CLOSEDLOOP_DETERMINISM:-0}" = "1" ]; then
  export CHENG_BOOTSTRAP_FORCE_DETERMINISM=1
fi

if [ "${CHENG_CLOSEDLOOP_SKIP_BOOTSTRAP:-0}" != "1" ]; then
  run_cmd "bootstrap.fullspec" sh src/tooling/bootstrap.sh --fullspec
fi

if [ "${CHENG_CLOSEDLOOP_SKIP_VERIFY:-0}" != "1" ]; then
  if [ "${CHENG_CLOSEDLOOP_STRICT:-0}" != "1" ]; then
    export CHENG_HRT_ALLOW_SKIP=1
  fi
  run_cmd "verify.core" sh ./verify.sh
  if [ "${CHENG_CLOSEDLOOP_BACKEND_PROD:-0}" = "1" ]; then
    run_cmd "backend.prod_closure" sh src/tooling/backend_prod_closure.sh ${CHENG_CLOSEDLOOP_BACKEND_PROD_ARGS:-}
  fi
fi

if [ "${CHENG_CLOSEDLOOP_LIBP2P:-0}" = "1" ]; then
  libp2p_root="${CHENG_LIBP2P_ROOT:-$HOME/.cheng-packages/cheng-libp2p}"
  if [ -x "$libp2p_root/scripts/verify.sh" ]; then
    run_cmd "verify.libp2p.smoke" sh "$libp2p_root/scripts/verify.sh" --mode:smoke
  else
    echo "[closedloop] missing libp2p verify.sh at $libp2p_root/scripts/verify.sh" 1>&2
    exit 1
  fi
fi

echo "closedloop ok"
