#!/usr/bin/env sh
set -eu
(set -o pipefail) 2>/dev/null && set -o pipefail

usage() {
  cat <<'EOF'
Usage:
  src/tooling/build_backend_driver.sh [--name:<binName>]

Builds backend driver via self-link rebuild only:
  1) rebuild from a stage0 backend driver
  2) no seed/tar copy path

Output:
  ./<binName> (default: cheng)

Env:
  CHENG_BACKEND_BUILD_DRIVER_LINKER=self   (required; default self)
  CHENG_BACKEND_BUILD_DRIVER_SELFHOST=1    (default 1)
  CHENG_BACKEND_BUILD_DRIVER_STAGE0=<path> (optional stage driver override)
  CHENG_BACKEND_BUILD_DRIVER_MULTI=0       (default 0; stable selfhost path)
  CHENG_BACKEND_BUILD_DRIVER_MULTI_FORCE=0 (default 0)
  CHENG_BACKEND_BUILD_DRIVER_INCREMENTAL=1 (default 1)
  CHENG_BACKEND_BUILD_DRIVER_JOBS=0        (default 0=auto)
EOF
}

name="cheng"
while [ "${1:-}" != "" ]; do
  case "$1" in
    --help|-h)
      usage
      exit 0
      ;;
    --name:*)
      name="${1#--name:}"
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

target=""
case "$host_os/$host_arch" in
  Darwin/arm64)
    target="arm64-apple-darwin"
    if ! command -v codesign >/dev/null 2>&1; then
      echo "[Error] build_backend_driver requires codesign on Darwin/arm64" 1>&2
      exit 2
    fi
    ;;
  Linux/aarch64|Linux/arm64)
    target="aarch64-unknown-linux-gnu"
    ;;
  *)
    echo "build_backend_driver skip: unsupported host=$host_os/$host_arch" 1>&2
    exit 2
    ;;
esac

linker_mode="${CHENG_BACKEND_BUILD_DRIVER_LINKER:-self}"
if [ "$linker_mode" != "self" ]; then
  echo "[Error] CHENG_BACKEND_BUILD_DRIVER_LINKER must be self (cc path removed)" 1>&2
  exit 2
fi

run_with_timeout() {
  seconds="$1"
  shift
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

driver_sanity_ok() {
  bin="$1"
  if [ ! -x "$bin" ]; then
    return 1
  fi
  cmd="$bin"
  case "$cmd" in
    /*|./*|../*) ;;
    *) cmd="./$cmd" ;;
  esac
  set +e
  run_with_timeout 5 "$cmd" --help >/dev/null 2>&1
  status=$?
  set -e
  case "$status" in
    0|1|2) return 0 ;;
  esac
  return 1
}

to_abs() {
  p="$1"
  case "$p" in
    /*) ;;
    *) p="$root/$p" ;;
  esac
  d="$(CDPATH= cd -- "$(dirname -- "$p")" && pwd)"
  printf "%s/%s\n" "$d" "$(basename -- "$p")"
}

stage0="${CHENG_BACKEND_BUILD_DRIVER_STAGE0:-}"
if [ "$stage0" = "" ]; then
  stage0_from_backend_driver="${CHENG_BACKEND_DRIVER:-}"
  if [ "$stage0_from_backend_driver" != "" ]; then
    stage0_try="$(to_abs "$stage0_from_backend_driver")"
    if driver_sanity_ok "$stage0_try"; then
      stage0="$stage0_try"
    fi
  fi
fi

if [ "$stage0" = "" ]; then
  for cand in \
    "$root/artifacts/backend_seed/cheng.stage2" \
    "$root/cheng" \
    "$root/artifacts/backend_selfhost_self_obj/cheng.stage2" \
    "$root/artifacts/backend_selfhost_self_obj/cheng.stage1"; do
    if driver_sanity_ok "$cand"; then
      stage0="$cand"
      break
    fi
  done
fi

if [ "$stage0" != "" ]; then
  stage0="$(to_abs "$stage0")"
fi

if [ "$stage0" = "" ]; then
  echo "[Error] build_backend_driver requires stage0 driver (set CHENG_BACKEND_BUILD_DRIVER_STAGE0 or CHENG_BACKEND_DRIVER, or prepare artifacts/backend_seed/cheng.stage2)" 1>&2
  exit 1
fi
if ! driver_sanity_ok "$stage0"; then
  if [ -x "$stage0" ]; then
    echo "[Error] stage0 driver is not runnable: $stage0" 1>&2
  else
    echo "[Error] stage0 driver is not executable: $stage0" 1>&2
  fi
  exit 1
fi

selfhost="${CHENG_BACKEND_BUILD_DRIVER_SELFHOST:-1}"
if [ "$selfhost" = "0" ]; then
  echo "[Error] CHENG_BACKEND_BUILD_DRIVER_SELFHOST=0 is not supported (seed copy path removed)" 1>&2
  exit 2
fi

driver_multi="${CHENG_BACKEND_BUILD_DRIVER_MULTI:-0}"
driver_multi_force="${CHENG_BACKEND_BUILD_DRIVER_MULTI_FORCE:-0}"
driver_incremental="${CHENG_BACKEND_BUILD_DRIVER_INCREMENTAL:-1}"
driver_jobs="${CHENG_BACKEND_BUILD_DRIVER_JOBS:-0}"
build_timeout="${CHENG_BACKEND_BUILD_DRIVER_TIMEOUT:-60}"
mm="${CHENG_BACKEND_BUILD_DRIVER_MM:-${CHENG_MM:-orc}}"
# Stage1 frontend currently keeps semantics/mono/ownership behind explicit
# toggles in production scripts to avoid seed-compiler crashes on this path.
if [ "${CHENG_STAGE1_SKIP_SEM:-}" = "" ]; then
  export CHENG_STAGE1_SKIP_SEM=0
fi
if [ "${CHENG_STAGE1_SKIP_MONO:-}" = "" ]; then
  export CHENG_STAGE1_SKIP_MONO=0
fi
if [ "${CHENG_STAGE1_SKIP_OWNERSHIP:-}" = "" ]; then
  export CHENG_STAGE1_SKIP_OWNERSHIP=1
fi

mkdir -p chengcache
runtime_src="src/std/system_helpers_backend.cheng"
runtime_obj="chengcache/system_helpers.backend.cheng.${target}.o"
runtime_obj_abs="$root/$runtime_obj"
compat_root="$root/chengcache/stage0_compat"

ensure_stage0_compat() {
  python3 "$root/scripts/gen_stage0_compat_src.py" --repo-root "$root" --out-root "$compat_root" >/dev/null
}

if [ ! -f "$runtime_obj_abs" ] || [ "$runtime_src" -nt "$runtime_obj_abs" ]; then
  set +e
  run_with_timeout "$build_timeout" env \
    CHENG_MM="$mm" \
    CHENG_C_SYSTEM=0 \
    CHENG_BACKEND_MULTI="$driver_multi" \
    CHENG_BACKEND_MULTI_FORCE="$driver_multi_force" \
    CHENG_BACKEND_INCREMENTAL="$driver_incremental" \
    CHENG_BACKEND_JOBS="$driver_jobs" \
    CHENG_BACKEND_ALLOW_NO_MAIN=1 \
    CHENG_BACKEND_WHOLE_PROGRAM=1 \
    CHENG_BACKEND_EMIT=obj \
    CHENG_BACKEND_TARGET="$target" \
    CHENG_BACKEND_FRONTEND=stage1 \
    CHENG_BACKEND_INPUT="$runtime_src" \
    CHENG_BACKEND_OUTPUT="$runtime_obj_abs" \
    "$stage0" >/dev/null 2>&1
  status=$?
  set -e
  if [ "$status" -ne 0 ] || [ ! -s "$runtime_obj_abs" ]; then
    if [ "$status" -eq 124 ]; then
      echo "[Error] build_backend_driver runtime obj compile timed out (${build_timeout}s)" 1>&2
    fi
    ensure_stage0_compat
    set +e
    (cd "$compat_root" && run_with_timeout "$build_timeout" env \
      CHENG_MM="$mm" \
      CHENG_C_SYSTEM=0 \
      CHENG_BACKEND_MULTI="$driver_multi" \
      CHENG_BACKEND_MULTI_FORCE="$driver_multi_force" \
      CHENG_BACKEND_INCREMENTAL="$driver_incremental" \
      CHENG_BACKEND_JOBS="$driver_jobs" \
      CHENG_BACKEND_ALLOW_NO_MAIN=1 \
      CHENG_BACKEND_WHOLE_PROGRAM=1 \
      CHENG_BACKEND_EMIT=obj \
      CHENG_BACKEND_TARGET="$target" \
      CHENG_BACKEND_FRONTEND=stage1 \
      CHENG_BACKEND_INPUT="$runtime_src" \
      CHENG_BACKEND_OUTPUT="$runtime_obj_abs" \
      "$stage0" >/dev/null 2>&1)
    compat_status=$?
    set -e
    if [ "$compat_status" -ne 0 ] || [ ! -s "$runtime_obj_abs" ]; then
      if [ "$compat_status" -eq 124 ]; then
        echo "[Error] build_backend_driver runtime obj compile timed out in stage0-compat (${build_timeout}s)" 1>&2
      fi
      echo "[Error] build_backend_driver failed to build backend runtime obj (stage0=$stage0)" 1>&2
      exit 1
    fi
  fi
fi

tmp_bin="${name}.tmp"
tmp_bin_abs="$root/$tmp_bin"
tmp_obj_abs="$root/$tmp_bin.o"
rm -f "$tmp_bin_abs" "$tmp_obj_abs"

set +e
run_with_timeout "$build_timeout" env \
  CHENG_MM="$mm" \
  CHENG_BACKEND_MULTI="$driver_multi" \
  CHENG_BACKEND_MULTI_FORCE="$driver_multi_force" \
  CHENG_BACKEND_INCREMENTAL="$driver_incremental" \
  CHENG_BACKEND_JOBS="$driver_jobs" \
  CHENG_BACKEND_VALIDATE=1 \
  CHENG_BACKEND_WHOLE_PROGRAM=1 \
  CHENG_BACKEND_LINKER=self \
  CHENG_BACKEND_NO_RUNTIME_C=1 \
  CHENG_BACKEND_RUNTIME_OBJ="$runtime_obj_abs" \
  CHENG_BACKEND_EMIT=exe \
  CHENG_BACKEND_TARGET="$target" \
  CHENG_BACKEND_FRONTEND=stage1 \
  CHENG_BACKEND_INPUT="src/backend/tooling/backend_driver.cheng" \
  CHENG_BACKEND_OUTPUT="$tmp_bin_abs" \
  "$stage0" >/dev/null 2>&1
status=$?
set -e
if [ "$status" -ne 0 ] && [ "$driver_multi" != "0" ]; then
  # Some seed drivers may crash in multi-worker mode; retry once in serial mode.
  rm -f "$tmp_bin_abs" "$tmp_obj_abs"
  set +e
  run_with_timeout "$build_timeout" env \
    CHENG_MM="$mm" \
    CHENG_BACKEND_MULTI=0 \
    CHENG_BACKEND_MULTI_FORCE=0 \
    CHENG_BACKEND_INCREMENTAL="$driver_incremental" \
    CHENG_BACKEND_JOBS="$driver_jobs" \
    CHENG_BACKEND_VALIDATE=1 \
    CHENG_BACKEND_WHOLE_PROGRAM=1 \
    CHENG_BACKEND_LINKER=self \
    CHENG_BACKEND_NO_RUNTIME_C=1 \
    CHENG_BACKEND_RUNTIME_OBJ="$runtime_obj_abs" \
    CHENG_BACKEND_EMIT=exe \
    CHENG_BACKEND_TARGET="$target" \
    CHENG_BACKEND_FRONTEND=stage1 \
    CHENG_BACKEND_INPUT="src/backend/tooling/backend_driver.cheng" \
    CHENG_BACKEND_OUTPUT="$tmp_bin_abs" \
    "$stage0" >/dev/null 2>&1
  status=$?
  set -e
fi
if [ "$status" -eq 0 ] && [ -x "$tmp_bin_abs" ] && driver_sanity_ok "$tmp_bin_abs"; then
  mv "$tmp_bin_abs" "$name"
  if [ -s "$tmp_obj_abs" ]; then
    mv "$tmp_obj_abs" "$name.o"
  fi
  echo "build_backend_driver ok (selfhost_self_link)"
  exit 0
fi

rm -f "$tmp_bin_abs" "$tmp_obj_abs"

# Stage0 seed drivers may not understand newer syntax (`T[]`, merged imports). Keep `src/` in the
# new syntax, but fall back to a stage0-compat overlay when bootstrapping from older seeds.
ensure_stage0_compat

set +e
(cd "$compat_root" && run_with_timeout "$build_timeout" env \
  CHENG_MM="$mm" \
  CHENG_BACKEND_MULTI="$driver_multi" \
  CHENG_BACKEND_MULTI_FORCE="$driver_multi_force" \
  CHENG_BACKEND_INCREMENTAL="$driver_incremental" \
  CHENG_BACKEND_JOBS="$driver_jobs" \
  CHENG_BACKEND_VALIDATE=1 \
  CHENG_BACKEND_WHOLE_PROGRAM=1 \
  CHENG_BACKEND_LINKER=self \
  CHENG_BACKEND_NO_RUNTIME_C=1 \
  CHENG_BACKEND_RUNTIME_OBJ="$runtime_obj_abs" \
  CHENG_BACKEND_EMIT=exe \
  CHENG_BACKEND_TARGET="$target" \
  CHENG_BACKEND_FRONTEND=stage1 \
  CHENG_BACKEND_INPUT="src/backend/tooling/backend_driver.cheng" \
  CHENG_BACKEND_OUTPUT="$tmp_bin_abs" \
  "$stage0" >/dev/null 2>&1)
status=$?
set -e
if [ "$status" -ne 0 ] && [ "$driver_multi" != "0" ]; then
  rm -f "$tmp_bin_abs" "$tmp_obj_abs"
  set +e
  (cd "$compat_root" && run_with_timeout "$build_timeout" env \
    CHENG_MM="$mm" \
    CHENG_BACKEND_MULTI=0 \
    CHENG_BACKEND_MULTI_FORCE=0 \
    CHENG_BACKEND_INCREMENTAL="$driver_incremental" \
    CHENG_BACKEND_JOBS="$driver_jobs" \
    CHENG_BACKEND_VALIDATE=1 \
    CHENG_BACKEND_WHOLE_PROGRAM=1 \
    CHENG_BACKEND_LINKER=self \
    CHENG_BACKEND_NO_RUNTIME_C=1 \
    CHENG_BACKEND_RUNTIME_OBJ="$runtime_obj_abs" \
    CHENG_BACKEND_EMIT=exe \
    CHENG_BACKEND_TARGET="$target" \
    CHENG_BACKEND_FRONTEND=stage1 \
    CHENG_BACKEND_INPUT="src/backend/tooling/backend_driver.cheng" \
    CHENG_BACKEND_OUTPUT="$tmp_bin_abs" \
    "$stage0" >/dev/null 2>&1)
  status=$?
  set -e
fi
if [ "$status" -eq 0 ] && [ -x "$tmp_bin_abs" ] && driver_sanity_ok "$tmp_bin_abs"; then
  mv "$tmp_bin_abs" "$name"
  if [ -s "$tmp_obj_abs" ]; then
    mv "$tmp_obj_abs" "$name.o"
  fi
  echo "build_backend_driver ok (selfhost_self_link, stage0-compat)"
  exit 0
fi

rm -f "$tmp_bin_abs" "$tmp_obj_abs"
if [ "$status" -eq 124 ]; then
  echo "[Error] build_backend_driver timed out (${build_timeout}s per compile attempt)" 1>&2
fi
echo "[Error] build_backend_driver selfhost_self_link failed (native and stage0-compat; stage0=$stage0)" 1>&2
echo "  hint: verify stage0 is runnable and can compile src/backend/tooling/backend_driver.cheng" 1>&2
exit 1
