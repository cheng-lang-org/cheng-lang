#!/usr/bin/env sh
. "$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)/env_prefix_bridge.sh"
set -eu

(set -o pipefail) 2>/dev/null && set -o pipefail

usage() {
  cat <<'EOF'
Usage:
  src/tooling/verify_libp2p_frontier.sh [--help]

Env:
  FRONTIER_ONLY=<csv>          Run only selected probes (e.g. mdns_smoke)
  BACKEND_DRIVER=<path>        Explicit backend driver path
  FRONTIER_TIMEOUT=<seconds>   Stage1 probe timeout (default: 180)
  FRONTIER_IMPORT_TIMEOUT=<seconds> Import probe timeout (default: 20)
  FRONTIER_TIMEOUT_DIAG=<0|1>  Capture timeout sample diag (default: 1)
  FRONTIER_TIMEOUT_DIAG_SECONDS=<seconds> sample duration on timeout (default: 5)
  FRONTIER_TIMEOUT_DIAG_DIR=<path> timeout diag directory (default: <logs>/timeout_diag)
  FRONTIER_TIMEOUT_DIAG_SUMMARY=<0|1> print summary for timeout sample (default: 1)
  FRONTIER_TIMEOUT_DIAG_SUMMARY_TOP=<n> top frames to print (default: 12)
  FRONTIER_IMPORT_FRONTEND=<stage1> Import probe frontend (default: stage1)
  FRONTIER_GENERIC_MODE=<dict> Generic mode for frontier probes (default: dict)
  FRONTIER_GENERIC_SPEC_BUDGET=<n>    Generic spec budget (default: 0)
  FRONTIER_CACHE=<path>        Cache root (default: chengcache/libp2p_frontier)
  CACHE_DIR=<path>             Stage1 token cache dir (default: <CACHE>/stage1)
  FRONTIER_SKIP_SEM=<0|1>      Stage1 skip semantics (default: 1)
  FRONTIER_SKIP_OWNERSHIP=<0|1> Stage1 skip ownership (default: 1)
  FRONTIER_VALIDATE=<0|1>      Backend UIR validate (default: 0)
EOF
}

while [ "${1:-}" != "" ]; do
  case "$1" in
    --help|-h)
      usage
      exit 0
      ;;
    *)
      echo "[frontier] unknown arg: $1" 1>&2
      usage 1>&2
      exit 2
      ;;
  esac
  shift || true
done

ROOT="$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)"
cd "$ROOT"

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
        if ($diag_file ne "" && $^O eq "darwin") {
          system($diag_tool, "$pid", "$diag_secs", "-mayDie", "-file", $diag_file);
        }
        kill "TERM", -$pid;
        select(undef, undef, undef, 0.5);
        kill "KILL", -$pid;
        exit 124;
      }
      select(undef, undef, undef, 0.1);
    }
  ' "$seconds" "$@"
}

if [ -n "${BACKEND_DRIVER:-}" ]; then
  DRIVER="$(to_abs "$BACKEND_DRIVER")"
else
  if [ -x "$ROOT/dist/releases/current/cheng" ]; then
    DRIVER="$ROOT/dist/releases/current/cheng"
  else
    DRIVER="$("$ROOT/src/tooling/backend_driver_path.sh")"
  fi
fi
DRIVER="$(to_abs "$DRIVER")"
if [ ! -x "$DRIVER" ]; then
  echo "[frontier] backend driver not executable: $DRIVER" 1>&2
  exit 1
fi

LIBP2P_ROOT="$(resolve_repo \
  "$ROOT/../cheng-libp2p" \
  "$HOME/cheng-libp2p" \
  "$HOME/.cheng-packages/cheng-libp2p" || true)"
QUIC_ROOT="$(resolve_repo \
  "$ROOT/../cheng-quic" \
  "$HOME/cheng-quic" \
  "$HOME/.cheng-packages/cheng-quic" || true)"
NET_ROOT="$(resolve_repo \
  "$ROOT/../cheng-net" \
  "$HOME/cheng-net" \
  "$HOME/.cheng-packages/cheng-net" || true)"
OBS_ROOT="$(resolve_repo \
  "$ROOT/../cheng-observability" \
  "$HOME/cheng-observability" \
  "$HOME/.cheng-packages/cheng-observability" || true)"

if [ -z "$LIBP2P_ROOT" ]; then
  echo "[frontier] cheng-libp2p repo not found" 1>&2
  exit 1
fi

FRONTIER_CACHE="${FRONTIER_CACHE:-$ROOT/chengcache/libp2p_frontier}"
CACHE="${CACHE:-$FRONTIER_CACHE/cache}"
CACHE_DIR="${CACHE_DIR:-$CACHE/stage1}"
OUT_DIR="$FRONTIER_CACHE/out"
LOG_DIR="$FRONTIER_CACHE/logs"
PROBE_TIMEOUT="${FRONTIER_TIMEOUT:-180}"
IMPORT_TIMEOUT="${FRONTIER_IMPORT_TIMEOUT:-20}"
IMPORT_FRONTEND="${FRONTIER_IMPORT_FRONTEND:-stage1}"
FRONTIER_SKIP_SEM="${FRONTIER_SKIP_SEM:-1}"
FRONTIER_SKIP_OWNERSHIP="${FRONTIER_SKIP_OWNERSHIP:-1}"
FRONTIER_VALIDATE="${FRONTIER_VALIDATE:-0}"
FRONTIER_GENERIC_MODE="${FRONTIER_GENERIC_MODE:-dict}"
FRONTIER_GENERIC_SPEC_BUDGET="${FRONTIER_GENERIC_SPEC_BUDGET:-0}"
FRONTIER_ONLY_RAW="${FRONTIER_ONLY:-}"
FRONTIER_TIMEOUT_DIAG="${FRONTIER_TIMEOUT_DIAG:-1}"
FRONTIER_TIMEOUT_DIAG_SECONDS="${FRONTIER_TIMEOUT_DIAG_SECONDS:-5}"
FRONTIER_TIMEOUT_DIAG_DIR="${FRONTIER_TIMEOUT_DIAG_DIR:-$LOG_DIR/timeout_diag}"
FRONTIER_TIMEOUT_DIAG_TOOL="${FRONTIER_TIMEOUT_DIAG_TOOL:-sample}"
FRONTIER_TIMEOUT_DIAG_SUMMARY="${FRONTIER_TIMEOUT_DIAG_SUMMARY:-1}"
FRONTIER_TIMEOUT_DIAG_SUMMARY_TOP="${FRONTIER_TIMEOUT_DIAG_SUMMARY_TOP:-12}"
timeout_diag_last_file=""
mkdir -p "$CACHE" "$CACHE_DIR" "$OUT_DIR" "$LOG_DIR"

FRONTIER_ONLY="$(printf '%s' "$FRONTIER_ONLY_RAW" | tr ' ' ',' | tr -s ',' | sed 's/^,*//;s/,*$//')"

should_run_probe() {
  probe="$1"
  if [ -z "$FRONTIER_ONLY" ]; then
    return 0
  fi
  case ",$FRONTIER_ONLY," in
    *,"$probe",*) return 0 ;;
  esac
  return 1
}

if [ "$FRONTIER_GENERIC_MODE" != "dict" ]; then
  echo "[frontier] unsupported FRONTIER_GENERIC_MODE: $FRONTIER_GENERIC_MODE" 1>&2
  echo "[frontier] production closure requires FRONTIER_GENERIC_MODE=dict" 1>&2
  exit 2
fi
if [ "$IMPORT_FRONTEND" != "stage1" ]; then
  echo "[frontier] unsupported FRONTIER_IMPORT_FRONTEND: $IMPORT_FRONTEND" 1>&2
  echo "[frontier] production closure requires FRONTIER_IMPORT_FRONTEND=stage1" 1>&2
  exit 2
fi

sanitize_diag_label() {
  printf '%s' "$1" | tr -cs 'A-Za-z0-9._-' '_'
}

prepare_timeout_diag() {
  label="$1"
  timeout_diag_last_file=""
  case "$FRONTIER_TIMEOUT_DIAG" in
    1|true|TRUE|yes|YES|on|ON)
      ;;
    *)
      return 0
      ;;
  esac
  if ! command -v "$FRONTIER_TIMEOUT_DIAG_TOOL" >/dev/null 2>&1; then
    return 0
  fi
  mkdir -p "$FRONTIER_TIMEOUT_DIAG_DIR"
  safe_label="$(sanitize_diag_label "$label")"
  if [ "$safe_label" = "" ]; then
    safe_label="probe"
  fi
  stamp="$(date +%Y%m%dT%H%M%S)"
  timeout_diag_last_file="$FRONTIER_TIMEOUT_DIAG_DIR/${stamp}_${safe_label}.sample.txt"
  export TIMEOUT_DIAG_FILE="$timeout_diag_last_file"
  export TIMEOUT_DIAG_SECONDS="$FRONTIER_TIMEOUT_DIAG_SECONDS"
  export TIMEOUT_DIAG_TOOL="$FRONTIER_TIMEOUT_DIAG_TOOL"
}

finish_timeout_diag() {
  status="$1"
  label="$2"
  if [ "$status" -eq 124 ] && [ "$timeout_diag_last_file" != "" ]; then
    echo "[frontier] timeout diag ($label): $timeout_diag_last_file"
    case "$FRONTIER_TIMEOUT_DIAG_SUMMARY" in
      1|true|TRUE|yes|YES|on|ON)
        if [ -f "src/tooling/summarize_timeout_diag.sh" ]; then
          set +e
          sh src/tooling/summarize_timeout_diag.sh --file:"$timeout_diag_last_file" --top:"$FRONTIER_TIMEOUT_DIAG_SUMMARY_TOP"
          set -e
        fi
        ;;
    esac
  fi
  unset TIMEOUT_DIAG_FILE TIMEOUT_DIAG_SECONDS TIMEOUT_DIAG_TOOL
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

PKG_ROOTS="${PKG_ROOTS:-}"
add_pkg_root() {
  root="$1"
  if [ -z "$root" ] || [ ! -d "$root" ]; then
    return
  fi
  root="$(to_abs "$root")"
  case ",$PKG_ROOTS," in
    *,"$root",*) ;;
    *) PKG_ROOTS="${PKG_ROOTS}${PKG_ROOTS:+,}$root" ;;
  esac
}

add_pkg_root "$ROOT"
add_pkg_root "$LIBP2P_ROOT"
add_pkg_root "$QUIC_ROOT"
add_pkg_root "$NET_ROOT"
add_pkg_root "$OBS_ROOT"
export PKG_ROOTS
export CACHE
export CACHE_DIR

if [ -z "${PKG_ROOT:-}" ]; then
  parent_guess="$(dirname "$LIBP2P_ROOT")"
  if [ -d "$parent_guess" ]; then
    PKG_ROOT="$parent_guess"
    export PKG_ROOT
  fi
fi

MDNS_SMOKE_SRC="$ROOT/tests/cheng/libp2p_frontier/mdns_smoke.cheng"
if [ ! -f "$MDNS_SMOKE_SRC" ]; then
  MDNS_SMOKE_SRC="$LIBP2P_ROOT/src/tests/mdns_smoke.cheng"
fi

run_probe() {
  name="$1"
  src="$2"
  frontend="$3"
  timeout_s="$4"
  log="$LOG_DIR/$name.log"
  out="$OUT_DIR/$name.o"
  echo "[frontier] probe: $name (frontend=$frontend timeout=${timeout_s}s)"
  if [ ! -f "$src" ]; then
    echo "FAIL  $name (missing source: $src)"
    printf '%s\tFAIL\tmissing source: %s\n' "$name" "$src" >>"$LOG_DIR/summary.tsv"
    return 1
  fi
  rm -f "$out"
  set +e
  run_with_timeout_labeled "$name" "$timeout_s" env \
    MM=orc \
    BACKEND_ALLOW_NO_MAIN="${BACKEND_ALLOW_NO_MAIN:-1}" \
    BACKEND_VALIDATE="$FRONTIER_VALIDATE" \
    BACKEND_EMIT=obj \
    BACKEND_TARGET=auto \
    BACKEND_FRONTEND="$frontend" \
    STAGE1_SKIP_SEM="$FRONTIER_SKIP_SEM" \
    STAGE1_SKIP_OWNERSHIP="$FRONTIER_SKIP_OWNERSHIP" \
    GENERIC_MODE="$FRONTIER_GENERIC_MODE" \
    GENERIC_SPEC_BUDGET="$FRONTIER_GENERIC_SPEC_BUDGET" \
    BACKEND_INPUT="$src" \
    BACKEND_OUTPUT="$out" \
    "$DRIVER" >"$log" 2>&1
  rc=$?
  set -e
  if [ "$rc" -eq 124 ]; then
    diag_path="${timeout_diag_last_file:-}"
    if [ "$diag_path" != "" ]; then
      echo "FAIL  $name (timeout=${timeout_s}s, log=$log, diag=$diag_path)"
      case "$FRONTIER_TIMEOUT_DIAG_SUMMARY" in
        1|true|TRUE|yes|YES|on|ON)
          if [ -f "src/tooling/summarize_timeout_diag.sh" ]; then
            set +e
            sh src/tooling/summarize_timeout_diag.sh --file:"$diag_path" --top:"$FRONTIER_TIMEOUT_DIAG_SUMMARY_TOP"
            set -e
          fi
          ;;
      esac
    else
      echo "FAIL  $name (timeout=${timeout_s}s, log=$log)"
    fi
    printf '%s\tFAIL\ttimeout=%ss diag=%s\t%s\n' "$name" "$timeout_s" "${diag_path:--}" "$log" >>"$LOG_DIR/summary.tsv"
    return 1
  fi
  if [ "$rc" -ne 0 ] || [ ! -s "$out" ]; then
    echo "FAIL  $name (rc=$rc, log=$log)"
    printf '%s\tFAIL\trc=%s\t%s\n' "$name" "$rc" "$log" >>"$LOG_DIR/summary.tsv"
    return 1
  fi
  echo "PASS  $name"
  printf '%s\tPASS\t0\t%s\n' "$name" "$log" >>"$LOG_DIR/summary.tsv"
  return 0
}

: >"$LOG_DIR/summary.tsv"

FAILS=0

if should_run_probe "cheng_probe_conn_import"; then
  run_probe "cheng_probe_conn_import" "$ROOT/tests/cheng/libp2p_frontier/cheng_probe_conn_import.cheng" "$IMPORT_FRONTEND" "$IMPORT_TIMEOUT" || FAILS=$((FAILS + 1))
fi
if should_run_probe "cheng_probe_multihash_import_only"; then
  run_probe "cheng_probe_multihash_import_only" "$ROOT/tests/cheng/libp2p_frontier/cheng_probe_multihash_import_only.cheng" "$IMPORT_FRONTEND" "$IMPORT_TIMEOUT" || FAILS=$((FAILS + 1))
fi
if should_run_probe "mdns_smoke"; then
  run_probe "mdns_smoke" "$MDNS_SMOKE_SRC" stage1 "$PROBE_TIMEOUT" || FAILS=$((FAILS + 1))
fi

if should_run_probe "msquic_transport_smoke"; then
  if [ -n "$QUIC_ROOT" ]; then
    run_probe "msquic_transport_smoke" "$QUIC_ROOT/tests/msquic_transport_smoke.cheng" stage1 "$PROBE_TIMEOUT" || FAILS=$((FAILS + 1))
  else
    echo "FAIL  msquic_transport_smoke (cheng-quic repo not found)"
    printf 'msquic_transport_smoke\tFAIL\tmissing cheng-quic\t-\n' >>"$LOG_DIR/summary.tsv"
    FAILS=$((FAILS + 1))
  fi
fi

echo
echo "[frontier] summary: logs=$LOG_DIR summary=$LOG_DIR/summary.tsv"

if [ "$FAILS" -ne 0 ]; then
  echo "[frontier] failed probes: $FAILS" 1>&2
  exit 1
fi

echo "[frontier] all probes passed"
