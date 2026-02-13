#!/usr/bin/env bash
set -euo pipefail

root="$(CDPATH= cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$root"

export CHENG_BACKEND_PROD_GATE_TIMEOUT="${CHENG_BACKEND_PROD_GATE_TIMEOUT:-60}"
export CHENG_BACKEND_PROD_SELFHOST_TIMEOUT="${CHENG_BACKEND_PROD_SELFHOST_TIMEOUT:-60}"
export CHENG_BACKEND_PROD_TIMEOUT_DIAG="${CHENG_BACKEND_PROD_TIMEOUT_DIAG:-1}"
export CHENG_BACKEND_PROD_TIMEOUT_DIAG_SUMMARY="${CHENG_BACKEND_PROD_TIMEOUT_DIAG_SUMMARY:-1}"
export CHENG_BACKEND_RUN_SELFHOST_PERF="${CHENG_BACKEND_RUN_SELFHOST_PERF:-1}"

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
  run_cmd "bootstrap.fullspec" sh src/tooling/bootstrap_pure.sh --fullspec
fi

if [ "${CHENG_CLOSEDLOOP_SKIP_VERIFY:-0}" != "1" ]; then
  run_cmd "verify.core" sh ./verify.sh
  if [ "${CHENG_CLOSEDLOOP_BACKEND_PROD:-0}" = "1" ]; then
    backend_prod_args="${CHENG_CLOSEDLOOP_BACKEND_PROD_ARGS:-}"
    if [ "$backend_prod_args" = "" ]; then
      backend_prod_args="--no-publish"
    fi
    # shellcheck disable=SC2086
    run_cmd "backend.prod_closure" sh src/tooling/backend_prod_closure.sh $backend_prod_args
  fi
fi

frontier_mode="${CHENG_CLOSEDLOOP_FRONTIER:-auto}"
run_frontier="0"
case "$frontier_mode" in
  1|true|TRUE|yes|YES|on|ON)
    run_frontier="1"
    ;;
  auto)
    if [ -d "$root/../cheng-libp2p" ] || [ -d "$HOME/cheng-libp2p" ] || [ -d "$HOME/.cheng-packages/cheng-libp2p" ]; then
      run_frontier="1"
    fi
    ;;
  0|false|FALSE|no|NO|off|OFF)
    run_frontier="0"
    ;;
  *)
    echo "[closedloop] invalid CHENG_CLOSEDLOOP_FRONTIER=$frontier_mode (expected auto|0|1)" 1>&2
    exit 2
    ;;
esac

if [ "$run_frontier" = "1" ]; then
  if [ -x "artifacts/backend_selfhost_self_obj/cheng.stage2" ] && [ "${CHENG_BACKEND_DRIVER:-}" = "" ]; then
    export CHENG_BACKEND_DRIVER="artifacts/backend_selfhost_self_obj/cheng.stage2"
  fi
  frontier_soft_auto="${CHENG_CLOSEDLOOP_FRONTIER_SOFT_AUTO:-1}"
  case "$frontier_mode/$frontier_soft_auto" in
    auto/1|auto/true|auto/TRUE|auto/yes|auto/YES|auto/on|auto/ON)
      echo "== verify.libp2p_frontier (auto, non-blocking) =="
      set +e
      sh src/tooling/verify_libp2p_frontier.sh
      frontier_rc="$?"
      set -e
      if [ "$frontier_rc" -ne 0 ]; then
        echo "== verify.libp2p_frontier (auto-soft-fail rc=$frontier_rc) =="
      fi
      ;;
    *)
      run_cmd "verify.libp2p_frontier" sh src/tooling/verify_libp2p_frontier.sh
      ;;
  esac
elif [ "$frontier_mode" = "auto" ]; then
  echo "== verify.libp2p_frontier (skip: cheng-libp2p repo not found) =="
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

if [ "${CHENG_CLOSEDLOOP_TIMEOUT_SUMMARY:-1}" = "1" ] && [ -f "src/tooling/summarize_timeout_diag.sh" ]; then
  set +e
  run_cmd "diag.timeout_summary" sh src/tooling/summarize_timeout_diag.sh --latest:3 --top:12
  rc_diag="$?"
  set -e
  if [ "$rc_diag" -ne 0 ]; then
    echo "== diag.timeout_summary (skip: no timeout diag files) =="
  fi
fi

echo "closedloop ok"
