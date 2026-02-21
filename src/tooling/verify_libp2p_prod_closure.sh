#!/usr/bin/env sh
. "$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)/env_prefix_bridge.sh"
set -eu

(set -o pipefail) 2>/dev/null && set -o pipefail

usage() {
  cat <<'EOF'
Usage:
  src/tooling/verify_libp2p_prod_closure.sh [--help]

Env:
  ROOT=<path>                     Cheng repo root (default: script auto-detect)
  LIBP2P_ROOT=<path>              libp2p repo root (default: auto-detect)
  LIBP2P_BUILD_TIMEOUT=<seconds>  libp2p verify build timeout (default: 60)
  LIBP2P_RUN_TIMEOUT=<seconds>    libp2p verify run timeout (default: 60)
  LIBP2P_RUN_FRONTIER=<0|1>       Run frontier verify (default: 1)
  LIBP2P_CHECK_HYBRID_REJECT=<0|1> Check hybrid rejection guards (default: 1)
EOF
}

while [ "${1:-}" != "" ]; do
  case "$1" in
    --help|-h)
      usage
      exit 0
      ;;
    *)
      echo "[libp2p_prod_closure] unknown arg: $1" 1>&2
      usage 1>&2
      exit 2
      ;;
  esac
  shift || true
done

ROOT="$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)"
ROOT="${ROOT:-$ROOT}"

to_abs() {
  path="$1"
  case "$path" in
    /*) ;;
    *) path="$ROOT/$path" ;;
  esac
  if [ -e "$path" ]; then
    d="$(CDPATH= cd -- "$(dirname -- "$path")" && pwd -P)"
    printf "%s/%s\n" "$d" "$(basename -- "$path")"
  else
    printf "%s\n" "$path"
  fi
}

resolve_repo() {
  for cand in "$@"; do
    if [ -d "$cand" ]; then
      to_abs "$cand"
      return 0
    fi
  done
  return 1
}

run_cmd() {
  label="$1"
  shift
  echo "== $label =="
  "$@"
}

run_expect_reject() {
  label="$1"
  expect_substr="$2"
  shift 2
  echo "== $label =="
  set +e
  out="$("$@" 2>&1)"
  rc="$?"
  set -e
  printf "%s\n" "$out"
  if [ "$rc" -eq 0 ]; then
    echo "[libp2p_prod_closure] expected rejection but command succeeded" 1>&2
    exit 1
  fi
  printf "%s\n" "$out" | grep -F "$expect_substr" >/dev/null 2>&1 || {
    echo "[libp2p_prod_closure] reject output mismatch, expected: $expect_substr" 1>&2
    exit 1
  }
}

libp2p_root="${LIBP2P_ROOT:-}"
if [ -z "$libp2p_root" ]; then
  libp2p_root="$(resolve_repo \
    "$HOME/.cheng-packages/cheng-libp2p" \
    "$ROOT/../cheng-libp2p" \
    "$HOME/cheng-libp2p" || true)"
fi
if [ -z "$libp2p_root" ]; then
  echo "[libp2p_prod_closure] cheng-libp2p repo not found; set LIBP2P_ROOT" 1>&2
  exit 1
fi
libp2p_root="$(to_abs "$libp2p_root")"

if [ ! -x "$libp2p_root/scripts/verify.sh" ]; then
  echo "[libp2p_prod_closure] missing verify.sh: $libp2p_root/scripts/verify.sh" 1>&2
  exit 1
fi
if [ ! -x "$libp2p_root/scripts/backend_build.sh" ]; then
  echo "[libp2p_prod_closure] missing backend_build.sh: $libp2p_root/scripts/backend_build.sh" 1>&2
  exit 1
fi

build_timeout="${LIBP2P_BUILD_TIMEOUT:-60}"
run_timeout="${LIBP2P_RUN_TIMEOUT:-60}"
run_frontier="${LIBP2P_RUN_FRONTIER:-1}"
check_hybrid_reject="${LIBP2P_CHECK_HYBRID_REJECT:-1}"

run_cmd "libp2p.verify.stable" \
  env \
    ROOT="$ROOT" \
    VERIFY_BUILD_TIMEOUT="$build_timeout" \
    RUN_TIMEOUT="$run_timeout" \
    sh "$libp2p_root/scripts/verify.sh" --mode:backend_smoke --profile:stable

run_cmd "libp2p.verify.full" \
  env \
    ROOT="$ROOT" \
    VERIFY_BUILD_TIMEOUT="$build_timeout" \
    RUN_TIMEOUT="$run_timeout" \
    sh "$libp2p_root/scripts/verify.sh" --mode:backend_smoke --profile:full

case "$check_hybrid_reject" in
  1|true|TRUE|yes|YES|on|ON)
    run_expect_reject "libp2p.verify.hybrid_reject" \
      "production closure requires GENERIC_MODE=dict" \
      env \
        ROOT="$ROOT" \
        VERIFY_BUILD_TIMEOUT="$build_timeout" \
        RUN_TIMEOUT="$run_timeout" \
        GENERIC_MODE=hybrid \
        sh "$libp2p_root/scripts/verify.sh" --mode:backend_smoke --profile:full

    run_expect_reject "libp2p.backend_build.hybrid_reject" \
      "production closure requires GENERIC_MODE=dict" \
      env \
        ROOT="$ROOT" \
        GENERIC_MODE=hybrid \
        sh "$libp2p_root/scripts/backend_build.sh" src/tests/node_resource_sync_smoke.cheng --emit:obj --name:libp2p_prod_closure_probe
    ;;
esac

case "$run_frontier" in
  1|true|TRUE|yes|YES|on|ON)
    run_cmd "cheng.verify.libp2p_frontier" \
      env \
        ROOT="$ROOT" \
        sh "$ROOT/src/tooling/verify_libp2p_frontier.sh"
    case "$check_hybrid_reject" in
      1|true|TRUE|yes|YES|on|ON)
        run_expect_reject "cheng.verify.libp2p_frontier.hybrid_reject" \
          "production closure requires FRONTIER_GENERIC_MODE=dict" \
          env \
            ROOT="$ROOT" \
            FRONTIER_GENERIC_MODE=hybrid \
            sh "$ROOT/src/tooling/verify_libp2p_frontier.sh"
        ;;
    esac
    ;;
esac

echo "verify_libp2p_prod_closure ok"
