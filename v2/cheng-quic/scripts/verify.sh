#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"

usage() {
  cat <<'TXT'
Usage:
  scripts/verify.sh [--mode:<backend_smoke|test|smoke|quic|bootstrap|synccast|synccast_closedloop>]
                    [--emit:<exe|obj>] [--runner:<path>] [--list] [--help]

Notes:
  - unified runtime mode: quic.
  - --mode:msquic is accepted only as a compatibility alias and is normalized to quic.
  - verify includes no-pointer syntax gate on src + src/tests.
TXT
}

VERIFY_MODE="${VERIFY_MODE:-backend_smoke}"
VERIFY_EMIT="${VERIFY_EMIT:-exe}"
VERIFY_RUNNER="${VERIFY_RUNNER:-}"
VERIFY_LIST=0

while [ "${1:-}" != "" ]; do
  case "$1" in
    --mode:*|--suite:*) VERIFY_MODE="${1#*:}" ;;
    --emit:*) VERIFY_EMIT="${1#--emit:}" ;;
    --runner:*) VERIFY_RUNNER="${1#--runner:}" ;;
    --list) VERIFY_LIST=1 ;;
    --help|-h) usage; exit 0 ;;
    *)
      echo "[verify] unknown arg: $1" 1>&2
      usage
      exit 2
      ;;
  esac
  shift || true
done

if [ "$VERIFY_MODE" = "msquic" ]; then
  echo "[verify] --mode:msquic merged into --mode:quic" 1>&2
  VERIFY_MODE="quic"
fi

case "$VERIFY_EMIT" in
  exe|obj) ;;
  *) echo "[verify] invalid --emit:$VERIFY_EMIT" 1>&2; exit 2 ;;
esac

zero_rawptr_require_or_set() {
  local var_name="$1"
  local expected="$2"
  eval "local has_value=\${$var_name+x}"
  local value
  if [ "${has_value:-}" = "x" ]; then
    eval "value=\${$var_name}"
  else
    value="$expected"
    eval "export $var_name=\"$expected\""
  fi
  if [ "$value" != "$expected" ]; then
    echo "[verify] zero-rawptr closure requires $var_name=$expected (got: $value)" 1>&2
    exit 2
  fi
}

enforce_zero_rawptr_compiler_closure() {
  zero_rawptr_require_or_set "BACKEND_ZERO_RAWPTR_CLOSURE" "1"
  zero_rawptr_require_or_set "ABI" "v2_noptr"
  zero_rawptr_require_or_set "STAGE1_STD_NO_POINTERS" "1"
  zero_rawptr_require_or_set "STAGE1_STD_NO_POINTERS_STRICT" "0"
  zero_rawptr_require_or_set "STAGE1_NO_POINTERS_NON_C_ABI" "1"
  zero_rawptr_require_or_set "STAGE1_NO_POINTERS_NON_C_ABI_INTERNAL" "1"
  zero_rawptr_require_or_set "STAGE1_SKIP_SEM" "0"
}

enforce_zero_rawptr_compiler_closure

if command -v rg >/dev/null 2>&1; then
  FORBIDDEN_IMPORTS="$(rg -n '^import cheng/libp2p/' "$ROOT/src" 2>/dev/null | rg -v '/src/tests/' || true)"
  if [ -n "$FORBIDDEN_IMPORTS" ]; then
    echo "[verify] forbidden import detected: import cheng/libp2p/..." 1>&2
    printf '%s\n' "$FORBIDDEN_IMPORTS" 1>&2
    exit 1
  fi
fi

if [ -z "${PKG_ROOTS:-}" ]; then
  PKG_ROOTS="$ROOT"
else
  case ",$PKG_ROOTS," in
    *,"$ROOT",*) ;;
    *) PKG_ROOTS="$ROOT,$PKG_ROOTS" ;;
  esac
fi
for dep in "$ROOT/../cheng-libp2p" "$ROOT/../cheng-observability"; do
  if [ -d "$dep" ]; then
    case ",$PKG_ROOTS," in
      *,"$dep",*) ;;
      *) PKG_ROOTS="$PKG_ROOTS,$dep" ;;
    esac
  fi
done
export PKG_ROOTS

RUNNER_BASE="$ROOT/src/tests"

find_named_runner() {
  local name="$1"
  if [ ! -d "$RUNNER_BASE" ]; then
    echo ""
    return
  fi
  local path=""
  if command -v rg >/dev/null 2>&1; then
    path="$(rg --files -g "$name" "$RUNNER_BASE" 2>/dev/null | head -n 1 || true)"
  else
    path="$(find "$RUNNER_BASE" -name "$name" -print 2>/dev/null | head -n 1 || true)"
  fi
  echo "$path"
}

find_runner_by_kind() {
  local kind="$1"
  if [ "$kind" = "smoke" ]; then
    local r="$(find_named_runner "smoke_main_runner.cheng")"
    [ -n "$r" ] || r="$(find_named_runner "smoke_runner.cheng")"
    [ -n "$r" ] || r="$(find_named_runner "test_runner.cheng")"
    echo "$r"
    return
  fi
  local r2="$(find_named_runner "test_runner.cheng")"
  [ -n "$r2" ] || r2="$(find_named_runner "smoke_runner.cheng")"
  echo "$r2"
}

if [ "$VERIFY_LIST" = "1" ]; then
  echo "verify modes:"
  echo "backend_smoke: $(find_named_runner "msquic_verify_runner.cheng")"
  echo "quic: $(find_named_runner "msquic_verify_runner.cheng")"
  echo "test: $(find_runner_by_kind test)"
  echo "smoke: $(find_runner_by_kind smoke)"
  echo "bootstrap: $(find_named_runner "bootstrap_runner.cheng")"
  echo "synccast: $(find_named_runner "synccast_verify_runner.cheng")"
  echo "synccast_closedloop: $(find_named_runner "synccast_closedloop_runner.cheng")"
  exit 0
fi

resolve_runner_path() {
  local p="$1"
  if [ -z "$p" ]; then
    echo ""
    return
  fi
  case "$p" in
    /*) echo "$p" ;;
    *)
      if [ -f "$p" ]; then
        echo "$(cd "$(dirname "$p")" && pwd)/$(basename "$p")"
      elif [ -f "$ROOT/$p" ]; then
        echo "$(cd "$(dirname "$ROOT/$p")" && pwd)/$(basename "$ROOT/$p")"
      else
        echo "$p"
      fi
      ;;
  esac
}

RUNNER=""
if [ -n "$VERIFY_RUNNER" ]; then
  RUNNER="$(resolve_runner_path "$VERIFY_RUNNER")"
else
  case "$VERIFY_MODE" in
    quic|backend_smoke) RUNNER="$(find_named_runner "msquic_verify_runner.cheng")" ;;
    bootstrap) RUNNER="$(find_named_runner "bootstrap_runner.cheng")" ;;
    synccast) RUNNER="$(find_named_runner "synccast_verify_runner.cheng")" ;;
    synccast_closedloop|synccast-closedloop) RUNNER="$(find_named_runner "synccast_closedloop_runner.cheng")" ;;
    smoke) RUNNER="$(find_runner_by_kind smoke)" ;;
    test|*) RUNNER="$(find_runner_by_kind test)" ;;
  esac
fi

if [ -z "$RUNNER" ] || [ ! -f "$RUNNER" ]; then
  echo "[verify] runner not found for mode: $VERIFY_MODE" 1>&2
  exit 1
fi

check_msquic_full_mapping() {
  case "$VERIFY_MODE" in
    quic|backend_smoke) ;;
    *) return 0 ;;
  esac
  local shim="$ROOT/src/msquictransport.cheng"
  local full="$ROOT/src/msquictransport_full.cheng"
  local stub="$ROOT/src/msquictransport_stub.cheng"
  if [ ! -f "$shim" ] || [ ! -f "$full" ] || [ ! -f "$stub" ]; then
    echo "[verify] msquic mapping gate missing files" 1>&2
    return 1
  fi
  if ! grep -F "import cheng/quic/msquictransport_full" "$shim" >/dev/null 2>&1; then
    echo "[verify] msquic mapping gate failed: shim must import msquictransport_full" 1>&2
    return 1
  fi
  return 0
}

check_no_pointer_syntax() {
  local pattern='(void\*|:[[:space:]]*[A-Za-z_][A-Za-z0-9_]*\*|\bptr\[|(^|[^A-Za-z0-9_])(alloc|realloc|dealloc|copyMem|zeroMem|ptr_add)\(|@importc)'
  local targets=("$ROOT/src" "$ROOT/src/tests")
  if command -v rg >/dev/null 2>&1; then
    if rg -n -e "$pattern" "${targets[@]}" >/dev/null 2>&1; then
      echo "[verify] no-pointer syntax gate failed" 1>&2
      rg -n -e "$pattern" "${targets[@]}" 1>&2 || true
      return 1
    fi
    return 0
  fi
  if grep -R -n -E "$pattern" "${targets[@]}" >/dev/null 2>&1; then
    echo "[verify] no-pointer syntax gate failed" 1>&2
    grep -R -n -E "$pattern" "${targets[@]}" 1>&2 || true
    return 1
  fi
  return 0
}

if ! check_msquic_full_mapping; then
  echo "[verify] msquic full mapping gate failed" 1>&2
  exit 1
fi

if ! check_no_pointer_syntax; then
  exit 1
fi

BUILD_NAME="${VERIFY_BUILD_NAME:-cheng_libp2p_tests}"
BUILD_TIMEOUT="${VERIFY_BUILD_TIMEOUT:-${BUILD_TIMEOUT:-600}}"
RUN_TIMEOUT="${RUN_TIMEOUT:-180}"
LOG_TS="$(date -u '+%Y%m%d-%H%M%S' 2>/dev/null || date '+%Y%m%d-%H%M%S')"
VERIFY_LOG_DIR="${VERIFY_LOG_DIR:-$ROOT/artifacts/verify/$LOG_TS}"
mkdir -p "$VERIFY_LOG_DIR"
LOG_BUILD="$VERIFY_LOG_DIR/build.log"
LOG_TEST="$VERIFY_LOG_DIR/test.log"
LOG_META="$VERIFY_LOG_DIR/meta.txt"

write_meta() {
  {
    echo "timestamp: $LOG_TS"
    echo "verify_mode: $VERIFY_MODE"
    echo "msquic_compile_probe: on"
    echo "verify_emit: $VERIFY_EMIT"
    echo "runner: $RUNNER"
    echo "cheng_root: $ROOT"
    echo "backend_driver: ${BACKEND_DRIVER:-auto}"
    echo "backend_driver_path: ${BACKEND_DRIVER:-}"
    echo "backend_frontend: ${BACKEND_FRONTEND:-stage1}"
    echo "backend_jobs: 1"
    echo "backend_target: ${BACKEND_TARGET:-}"
    echo "build_name: $BUILD_NAME"
    echo "build_timeout: $BUILD_TIMEOUT"
    echo "run_timeout: $RUN_TIMEOUT"
    echo "msquic_light_compile_only: 0"
    echo "loopback_mode: native_udp"
    echo "tls_scope: full_real"
    echo "log_dir: $VERIFY_LOG_DIR"
    echo "pkg_roots: $PKG_ROOTS"
  } >"$LOG_META"
}

run_with_timeout() {
  local timeout_s="$1"
  local log_file="$2"
  shift 2
  if command -v python3 >/dev/null 2>&1; then
    LOG_FILE="$log_file" TIMEOUT_S="$timeout_s" python3 - "$@" <<'PY'
import os
import signal
import subprocess
import sys

timeout_s = int(os.environ.get("TIMEOUT_S", "0") or "0")
log_file = os.environ["LOG_FILE"]
cmd = sys.argv[1:]

with open(log_file, "a", encoding="utf-8") as f:
    f.write(f"[cmd] {cmd!r}\n")
    f.flush()
    proc = subprocess.Popen(
        cmd,
        stdout=subprocess.PIPE,
        stderr=subprocess.STDOUT,
        text=True,
        preexec_fn=os.setsid if hasattr(os, "setsid") else None,
    )
    timed_out = False
    output = ""
    try:
        while True:
            try:
                out, _ = proc.communicate(timeout=timeout_s if timeout_s > 0 else None)
                if out is not None:
                    output = out
                break
            except subprocess.TimeoutExpired as exc:
                timed_out = True
                partial = exc.stdout or ""
                if isinstance(partial, bytes):
                    partial = partial.decode("utf-8", "replace")
                output = partial
                try:
                    if hasattr(os, "killpg"):
                        os.killpg(proc.pid, signal.SIGKILL)
                    else:
                        proc.kill()
                except ProcessLookupError:
                    pass
                try:
                    remain, _ = proc.communicate(timeout=2)
                    if remain is not None:
                        if isinstance(remain, bytes):
                            remain = remain.decode("utf-8", "replace")
                        output += remain
                except subprocess.TimeoutExpired:
                    pass
                break
        if output:
            sys.stdout.write(output)
            sys.stdout.flush()
            f.write(output)
            f.flush()
        if timed_out:
            f.write("[timeout]\n")
            f.flush()
            raise SystemExit(124)
        raise SystemExit(proc.returncode or 0)
    finally:
        try:
            if proc.stdout:
                proc.stdout.close()
        except Exception:
            pass
PY
    return $?
  fi
  "$@" 2>&1 | tee -a "$log_file"
}

BACKEND_BUILD_SH="$ROOT/scripts/backend_build.sh"
if [ ! -f "$BACKEND_BUILD_SH" ]; then
  echo "[verify] missing build helper: $BACKEND_BUILD_SH" 1>&2
  exit 1
fi

write_meta

BIN_OUT="$ROOT/$BUILD_NAME"
OBJ_OUT="$ROOT/$BUILD_NAME.o"
rm -f "$BIN_OUT" "$OBJ_OUT" "$LOG_BUILD" "$LOG_TEST"

if [ "$VERIFY_EMIT" = "exe" ]; then
  echo "[verify] build: $RUNNER -> $BIN_OUT (emit=exe)"
  set +e
  run_with_timeout "$BUILD_TIMEOUT" "$LOG_BUILD" sh "$BACKEND_BUILD_SH" "$RUNNER" --name:"$BUILD_NAME" --emit:exe --frontend:stage1 --jobs:1
  BUILD_RC=$?
  set -e
  if [ "$BUILD_RC" -ne 0 ]; then
    echo "[verify] build failed (rc=$BUILD_RC). log: $LOG_BUILD" 1>&2
    exit "$BUILD_RC"
  fi
  if [ ! -x "$BIN_OUT" ]; then
    echo "[verify] missing built binary: $BIN_OUT" 1>&2
    exit 1
  fi
else
  echo "[verify] build: $RUNNER -> $OBJ_OUT (emit=obj)"
  set +e
  run_with_timeout "$BUILD_TIMEOUT" "$LOG_BUILD" sh "$BACKEND_BUILD_SH" "$RUNNER" --name:"$BUILD_NAME.o" --emit:obj --frontend:stage1 --jobs:1
  BUILD_RC=$?
  set -e
  if [ "$BUILD_RC" -ne 0 ]; then
    echo "[verify] build failed (rc=$BUILD_RC). log: $LOG_BUILD" 1>&2
    exit "$BUILD_RC"
  fi
  if [ ! -s "$OBJ_OUT" ]; then
    echo "[verify] missing built object: $OBJ_OUT" 1>&2
    exit 1
  fi

  SYS_HELPERS_O="${VERIFY_SYSTEM_HELPERS_O:-${BACKEND_SYSTEM_HELPERS_O:-}}"
  if [ -z "$SYS_HELPERS_O" ]; then
    for cand in \
      "$ROOT/chengcache/c_backend_system.system_helpers.o" \
      "$ROOT/chengcache/c_backend_fullspec.system_helpers.o" \
      "$ROOT/chengcache/c_backend_mixed_c.system_helpers.o" \
      "$ROOT/chengcache/c_backend_smoke.system_helpers.o"; do
      if [ -f "$cand" ]; then
        SYS_HELPERS_O="$cand"
        break
      fi
    done
  fi
  if [ -z "$SYS_HELPERS_O" ] || [ ! -f "$SYS_HELPERS_O" ]; then
    echo "[verify] missing system helpers object for obj link" 1>&2
    exit 1
  fi

  echo "[verify] link: $OBJ_OUT + $SYS_HELPERS_O -> $BIN_OUT"
  set +e
  run_with_timeout 60 "$LOG_BUILD" cc "$OBJ_OUT" "$SYS_HELPERS_O" -o "$BIN_OUT"
  LINK_RC=$?
  set -e
  if [ "$LINK_RC" -ne 0 ]; then
    echo "[verify] link failed (rc=$LINK_RC). log: $LOG_BUILD" 1>&2
    exit "$LINK_RC"
  fi
  if [ ! -x "$BIN_OUT" ]; then
    echo "[verify] missing linked binary: $BIN_OUT" 1>&2
    exit 1
  fi
fi

echo "[verify] run: $BIN_OUT"
set +e
run_with_timeout "$RUN_TIMEOUT" "$LOG_TEST" "$BIN_OUT"
RUN_RC=$?
set -e

if [ "$RUN_RC" -ne 0 ]; then
  echo "[verify] test failed (rc=$RUN_RC). log: $LOG_TEST" 1>&2
  exit "$RUN_RC"
fi

echo "[verify] ok"
