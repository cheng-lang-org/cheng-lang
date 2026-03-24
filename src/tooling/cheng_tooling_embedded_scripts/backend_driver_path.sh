#!/usr/bin/env sh
:
set -eu
(set -o pipefail) 2>/dev/null && set -o pipefail

root="$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)"
cd "$root"

usage() {
  cat <<'EOF'
Usage:
  src/tooling/cheng_tooling_embedded_scripts/backend_driver_path.sh [--path-only]

Notes:
  - Resolves a healthy backend driver from the repo-local artifacts first.
  - Health probe is intentionally conservative: `--help` + system-link smoke.
EOF
}

to_abs() {
  p="$1"
  case "$p" in
    /*) ;;
    *) p="$root/$p" ;;
  esac
  d="$(CDPATH= cd -- "$(dirname -- "$p")" && pwd)"
  printf '%s/%s\n' "$d" "$(basename -- "$p")"
}

run_with_timeout() {
  seconds="$1"
  shift || true
  perl -e '
    use POSIX qw(setsid WNOHANG);
    my $timeout = shift;
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
        kill "TERM", -$pid;
        select(undef, undef, undef, 0.5);
        kill "KILL", -$pid;
        exit 124;
      }
      select(undef, undef, undef, 0.1);
    }
  ' "$seconds" "$@"
}

driver_help_ok() {
  bin="$1"
  [ -x "$bin" ] || return 1
  set +e
  run_with_timeout 5 "$bin" --help >/dev/null 2>&1
  status=$?
  set -e
  case "$status" in
    0|1|2) return 0 ;;
  esac
  return 1
}

driver_compile_smoke_ok() {
  bin="$1"
  [ -x "$bin" ] || return 1
  smoke_src="$root/tests/cheng/backend/fixtures/return_add.cheng"
  [ -f "$smoke_src" ] || return 0
  smoke_out="$root/chengcache/.backend_driver_path.smoke.bin"
  smoke_linker="${BACKEND_DRIVER_PATH_LINKER:-system}"
  mkdir -p "$root/chengcache"
  rm -f "$smoke_out"
  set +e
  run_with_timeout "${BACKEND_DRIVER_PATH_SMOKE_TIMEOUT:-20}" \
    env BACKEND_VALIDATE=0 \
    "$bin" \
    "$smoke_src" \
    --emit:exe \
    --target:auto \
    --frontend:stage1 \
    --linker:"$smoke_linker" \
    --output:"$smoke_out" >/dev/null 2>&1
  status=$?
  set -e
  [ "$status" -eq 0 ] || return 1
  [ -s "$smoke_out" ] || return 1
  return 0
}

driver_ok() {
  bin="$1"
  driver_help_ok "$bin" && driver_compile_smoke_ok "$bin"
}

find_fallback_driver() {
  allow_selfhost="${BACKEND_DRIVER_PATH_ALLOW_SELFHOST:-0}"
  for cand in \
    "$root/artifacts/backend_driver/cheng" \
    "$root/artifacts/backend_driver/cheng.fixed3" \
    "$root/dist/releases/current/cheng" \
    "$root/src/tooling/backend_driver_exec.sh"; do
    if driver_ok "$cand"; then
      printf '%s\n' "$cand"
      return 0
    fi
  done
  if [ "$allow_selfhost" != "1" ]; then
    return 1
  fi
  for cand in \
    "$root/artifacts/backend_selfhost_self_obj/cheng.stage2" \
    "$root/artifacts/backend_selfhost_self_obj/cheng.stage1"; do
    if driver_ok "$cand"; then
      printf '%s\n' "$cand"
      return 0
    fi
  done
  return 1
}

path_only="0"
while [ "${1:-}" != "" ]; do
  case "$1" in
    --path-only)
      path_only="1"
      ;;
    --help|-h)
      usage
      exit 0
      ;;
    *)
      echo "[Error] backend_driver_path: unknown arg: $1" 1>&2
      usage 1>&2
      exit 2
      ;;
  esac
  shift || true
done

driver="${BACKEND_DRIVER:-}"
explicit_driver_fallback="${BACKEND_DRIVER_ALLOW_FALLBACK:-0}"
if [ "$driver" != "" ]; then
  abs="$(to_abs "$driver")"
  if driver_ok "$abs"; then
    printf '%s\n' "$abs"
    exit 0
  fi
  if [ "$explicit_driver_fallback" != "1" ]; then
    if [ ! -x "$abs" ]; then
      echo "[Error] BACKEND_DRIVER is not executable: $abs" 1>&2
    else
      echo "[Error] BACKEND_DRIVER failed health probe: $abs" 1>&2
    fi
    exit 1
  fi
  if [ ! -x "$abs" ]; then
    echo "[Warn] BACKEND_DRIVER is not executable, fallback auto-select: $abs" 1>&2
  else
    echo "[Warn] BACKEND_DRIVER failed health probe, fallback auto-select: $abs" 1>&2
  fi
fi

resolved="$(find_fallback_driver || true)"
if [ "$resolved" = "" ] && [ "${BACKEND_DRIVER_PATH_PREFER_REBUILD:-0}" = "1" ]; then
  tool="$root/src/tooling/cheng_tooling_embedded_scripts/cheng_tooling.sh"
  if [ -x "$tool" ]; then
    set +e
    sh "$tool" build_backend_driver --name:artifacts/backend_driver/cheng >/dev/null 2>&1
    rebuild_status=$?
    set -e
    if [ "$rebuild_status" -eq 0 ]; then
      resolved="$(find_fallback_driver || true)"
    fi
  fi
fi

if [ "$resolved" = "" ]; then
  echo "[Error] backend_driver_path: no healthy backend driver found" 1>&2
  exit 1
fi

if [ "$path_only" = "1" ]; then
  printf '%s\n' "$resolved"
  exit 0
fi
printf '%s\n' "$resolved"
