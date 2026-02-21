#!/usr/bin/env bash
. "$(CDPATH= cd -- "$(dirname -- "$0")/../src/tooling" && pwd)/env_prefix_bridge.sh"
set -euo pipefail

root="$(CDPATH= cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$root"

export BACKEND_PROD_GATE_TIMEOUT="${BACKEND_PROD_GATE_TIMEOUT:-60}"
export BACKEND_PROD_SELFHOST_TIMEOUT="${BACKEND_PROD_SELFHOST_TIMEOUT:-60}"
export BACKEND_PROD_TIMEOUT_DIAG="${BACKEND_PROD_TIMEOUT_DIAG:-1}"
export BACKEND_PROD_TIMEOUT_DIAG_SUMMARY="${BACKEND_PROD_TIMEOUT_DIAG_SUMMARY:-1}"
export BACKEND_RUN_SELFHOST_PERF="${BACKEND_RUN_SELFHOST_PERF:-1}"
# stage1 fullspec gate default timeout; keep consistent with verify_stage1_fullspec.sh.
export STAGE1_FULLSPEC_TIMEOUT="${STAGE1_FULLSPEC_TIMEOUT:-60}"

run_cmd() {
  local name="$1"
  shift
  echo "== ${name} =="
  "$@"
}

if [ "${CLOSEDLOOP_PROD:-0}" = "1" ]; then
  if [ "${CLOSEDLOOP_STRICT:-}" = "" ]; then
    export CLOSEDLOOP_STRICT=1
  fi
  if [ "${CLOSEDLOOP_BACKEND_PROD:-}" = "" ]; then
    export CLOSEDLOOP_BACKEND_PROD=1
  fi
  if [ "${STAGE1_FULLSPEC:-}" = "" ]; then
    export STAGE1_FULLSPEC="${CLOSEDLOOP_STAGE1_FULLSPEC_DEFAULT:-0}"
  fi
  export BACKEND_CLOSEDLOOP=1
  export C_BACKEND_CLOSURE=1
  export C_BACKEND_CLOSURE_ARGS="--no-bootstrap ${C_BACKEND_CLOSURE_ARGS:-}"
  export BOOTSTRAP_FORCE_DETERMINISM=1
fi

if [ "${CLOSEDLOOP_DETERMINISM:-0}" = "1" ]; then
  export BOOTSTRAP_FORCE_DETERMINISM=1
fi

if [ "${CLOSEDLOOP_SKIP_BOOTSTRAP:-0}" != "1" ]; then
  run_cmd "bootstrap.fullspec" sh src/tooling/bootstrap_pure.sh --fullspec
fi

if [ "${CLOSEDLOOP_SKIP_VERIFY:-0}" != "1" ]; then
  run_cmd "verify.core" sh ./verify.sh
  if [ "${CLOSEDLOOP_BACKEND_PROD:-0}" = "1" ]; then
    backend_prod_args="${CLOSEDLOOP_BACKEND_PROD_ARGS:-}"
    if [ "$backend_prod_args" = "" ]; then
      backend_prod_args="--no-publish"
    fi
    if [ "${CLOSEDLOOP_UIR_STABILITY:-0}" = "1" ] && \
       ! printf '%s\n' "$backend_prod_args" | grep -q -- "--uir-stability"; then
      backend_prod_args="$backend_prod_args --uir-stability"
    fi
    # shellcheck disable=SC2086
    run_cmd "backend.prod_closure" sh src/tooling/backend_prod_closure.sh $backend_prod_args
  fi
fi

frontier_mode="${CLOSEDLOOP_FRONTIER:-auto}"
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
    echo "[closedloop] invalid CLOSEDLOOP_FRONTIER=$frontier_mode (expected auto|0|1)" 1>&2
    exit 2
    ;;
esac

if [ "$run_frontier" = "1" ]; then
  if [ -x "artifacts/backend_selfhost_self_obj/cheng.stage2" ] && [ "${BACKEND_DRIVER:-}" = "" ]; then
    export BACKEND_DRIVER="artifacts/backend_selfhost_self_obj/cheng.stage2"
  fi
  frontier_soft_auto="${CLOSEDLOOP_FRONTIER_SOFT_AUTO:-1}"
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

if [ "${CLOSEDLOOP_LIBP2P:-0}" = "1" ]; then
  if [ -x "src/tooling/verify_libp2p_prod_closure.sh" ]; then
    run_cmd "verify.libp2p.prod_closure" env ROOT="$root" sh src/tooling/verify_libp2p_prod_closure.sh
  else
    echo "[closedloop] missing src/tooling/verify_libp2p_prod_closure.sh" 1>&2
    exit 1
  fi
fi

if [ "${CLOSEDLOOP_TIMEOUT_SUMMARY:-1}" = "1" ] && [ -f "src/tooling/summarize_timeout_diag.sh" ]; then
  set +e
  run_cmd "diag.timeout_summary" sh src/tooling/summarize_timeout_diag.sh --latest:3 --top:12
  rc_diag="$?"
  set -e
  if [ "$rc_diag" -ne 0 ]; then
    echo "== diag.timeout_summary (skip: no timeout diag files) =="
  fi
fi

if [ "${CLOSEDLOOP_STAGE1_TIMEOUT_SUMMARY:-1}" = "1" ] && [ -f "src/tooling/summarize_stage1_timeout_diag.sh" ]; then
  set +e
  run_cmd "diag.stage1_timeout_summary" sh src/tooling/summarize_stage1_timeout_diag.sh --latest:3
  rc_stage1_diag="$?"
  set -e
  if [ "$rc_stage1_diag" -ne 0 ]; then
    echo "== diag.stage1_timeout_summary (skip: no stage1 timeout diag files) =="
  fi
fi

echo "closedloop ok"
