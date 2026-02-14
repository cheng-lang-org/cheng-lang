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
  <binName> (default: artifacts/backend_driver/cheng)

Env:
  CHENG_BACKEND_BUILD_DRIVER_LINKER=self   (required; default self)
  CHENG_BACKEND_BUILD_DRIVER_SELFHOST=1    (default 1)
  CHENG_BACKEND_BUILD_DRIVER_STAGE0=<path> (optional stage driver override)
  CHENG_BACKEND_BUILD_DRIVER_ALLOW_STAGE0_FALLBACK=0 (default 0; strict mode)
  CHENG_BACKEND_BUILD_DRIVER_MULTI=1       (default 1; parallel incremental)
  CHENG_BACKEND_BUILD_DRIVER_MULTI_FORCE=0 (default 0)
  CHENG_BACKEND_BUILD_DRIVER_INCREMENTAL=1 (default 1)
  CHENG_BACKEND_BUILD_DRIVER_JOBS=0        (default 0=auto)
  CHENG_BACKEND_IR=uir                     (default uir)
  CHENG_GENERIC_MODE=dict                  (default dict)
  CHENG_GENERIC_SPEC_BUDGET=0              (default 0)
EOF
}

name="${CHENG_BACKEND_LOCAL_DRIVER_REL:-artifacts/backend_driver/cheng}"
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
        # First try process-group kill, then direct pid kill as fallback.
        # Some seed drivers may leave descendants outside expected pgid.
        kill "TERM", -$pid;
        kill "TERM", $pid;
        my $grace_end = time + 1;
        while (time < $grace_end) {
          my $r = waitpid($pid, WNOHANG);
          if ($r == $pid) {
            my $status = $?;
            if (($status & 127) != 0) {
              exit(128 + ($status & 127));
            }
            exit($status >> 8);
          }
          select(undef, undef, undef, 0.1);
        }
        kill "KILL", -$pid;
        kill "KILL", $pid;
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

driver_stage1_smoke_ok() {
  bin="$1"
  if [ ! -x "$bin" ]; then
    return 1
  fi
  smoke_src="$root/tests/cheng/backend/fixtures/return_add.cheng"
  if [ ! -f "$smoke_src" ]; then
    return 0
  fi
  smoke_out="$root/chengcache/.build_backend_driver.stage1_smoke.o"
  rm -f "$smoke_out"
  set +e
  run_with_timeout 15 env \
    CHENG_MM=orc \
    CHENG_BACKEND_VALIDATE=0 \
    CHENG_BACKEND_EMIT=obj \
    CHENG_BACKEND_TARGET="$target" \
    CHENG_BACKEND_FRONTEND=stage1 \
    CHENG_BACKEND_INPUT="$smoke_src" \
    CHENG_BACKEND_OUTPUT="$smoke_out" \
    "$bin" >/dev/null 2>&1
  status=$?
  set -e
  if [ "$status" -ne 0 ]; then
    return 1
  fi
  [ -s "$smoke_out" ] || return 1
  return 0
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
    "$root/artifacts/backend_driver/cheng" \
    "$root/dist/releases/current/cheng" \
    "$root/artifacts/backend_selfhost_self_obj/cheng_stage0_prod" \
    "$root/artifacts/backend_selfhost_self_obj/cheng_stage0_default" \
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

driver_multi="${CHENG_BACKEND_BUILD_DRIVER_MULTI:-1}"
driver_multi_force="${CHENG_BACKEND_BUILD_DRIVER_MULTI_FORCE:-0}"
driver_incremental="${CHENG_BACKEND_BUILD_DRIVER_INCREMENTAL:-1}"
driver_jobs="${CHENG_BACKEND_BUILD_DRIVER_JOBS:-0}"
build_timeout="${CHENG_BACKEND_BUILD_DRIVER_TIMEOUT:-60}"
mm="${CHENG_BACKEND_BUILD_DRIVER_MM:-${CHENG_MM:-orc}}"
allow_stage0_fallback="${CHENG_BACKEND_BUILD_DRIVER_ALLOW_STAGE0_FALLBACK:-0}"
if [ "${CHENG_BACKEND_IR:-}" = "" ]; then
  export CHENG_BACKEND_IR=uir
fi
if [ "${CHENG_GENERIC_MODE:-}" = "" ]; then
  # Fast/default path: skip stage1 monomorphize in bootstrap builds.
  export CHENG_GENERIC_MODE=dict
fi
if [ "${CHENG_GENERIC_SPEC_BUDGET:-}" = "" ]; then
  export CHENG_GENERIC_SPEC_BUDGET=0
fi
# Stage1 frontend currently keeps semantics/mono/ownership behind explicit
# toggles in production scripts to avoid seed-compiler crashes on this path.
if [ "${CHENG_STAGE1_SKIP_SEM:-}" = "" ]; then
  export CHENG_STAGE1_SKIP_SEM=0
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
    CHENG_BACKEND_FRONTEND=mvp \
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
      CHENG_BACKEND_FRONTEND=mvp \
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
case "$tmp_bin" in
  /*)
    tmp_bin_abs="$tmp_bin"
    ;;
  *)
    tmp_bin_abs="$root/$tmp_bin"
    ;;
esac
tmp_obj_abs="$tmp_bin_abs.o"
case "$name" in
  /*)
    name_abs="$name"
    ;;
  *)
    name_abs="$root/$name"
    ;;
esac
name_obj_abs="${name_abs}.o"
name_dir="$(dirname "$name_abs")"
if [ "$name_dir" != "" ] && [ ! -d "$name_dir" ]; then
  mkdir -p "$name_dir"
fi
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
if [ "$status" -ne 0 ] && [ "$driver_multi" != "0" ] && [ "$status" -ne 124 ]; then
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
  if driver_stage1_smoke_ok "$tmp_bin_abs"; then
    mv "$tmp_bin_abs" "$name_abs"
    if [ -s "$tmp_obj_abs" ]; then
      mv "$tmp_obj_abs" "$name_obj_abs"
    fi
    echo "build_backend_driver ok (selfhost_self_link)"
    exit 0
  fi
  if [ "$allow_stage0_fallback" = "1" ] && driver_sanity_ok "$stage0" && driver_stage1_smoke_ok "$stage0"; then
    rm -f "$tmp_bin_abs" "$tmp_obj_abs"
    cp "$stage0" "$name_abs"
    chmod +x "$name_abs" || true
    echo "build_backend_driver warn: selfhost binary failed stage1 smoke; fallback to stage0"
    exit 0
  fi
  rm -f "$tmp_bin_abs" "$tmp_obj_abs"
  echo "[Error] build_backend_driver produced non-runnable stage1 binary (selfhost path)" 1>&2
  exit 1
fi

rm -f "$tmp_bin_abs" "$tmp_obj_abs"

if [ "$status" -eq 124 ] && [ "$allow_stage0_fallback" = "1" ] &&
   driver_sanity_ok "$stage0" && driver_stage1_smoke_ok "$stage0"; then
  cp "$stage0" "$name_abs"
  chmod +x "$name_abs" || true
  echo "build_backend_driver warn: selfhost compile timed out; fallback to stage0"
  exit 0
fi

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
if [ "$status" -ne 0 ] && [ "$driver_multi" != "0" ] && [ "$status" -ne 124 ]; then
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
  if driver_stage1_smoke_ok "$tmp_bin_abs"; then
    mv "$tmp_bin_abs" "$name_abs"
    if [ -s "$tmp_obj_abs" ]; then
      mv "$tmp_obj_abs" "$name_obj_abs"
    fi
    echo "build_backend_driver ok (selfhost_self_link, stage0-compat)"
    exit 0
  fi
  if [ "$allow_stage0_fallback" = "1" ] && driver_sanity_ok "$stage0" && driver_stage1_smoke_ok "$stage0"; then
    rm -f "$tmp_bin_abs" "$tmp_obj_abs"
    cp "$stage0" "$name_abs"
    chmod +x "$name_abs" || true
    echo "build_backend_driver warn: stage0-compat selfhost binary failed stage1 smoke; fallback to stage0"
    exit 0
  fi
  rm -f "$tmp_bin_abs" "$tmp_obj_abs"
  echo "[Error] build_backend_driver produced non-runnable stage1 binary (stage0-compat path)" 1>&2
  exit 1
fi

rm -f "$tmp_bin_abs" "$tmp_obj_abs"
if [ "$allow_stage0_fallback" = "1" ] && driver_sanity_ok "$stage0" && driver_stage1_smoke_ok "$stage0"; then
  cp "$stage0" "$name_abs"
  chmod +x "$name_abs" || true
  if [ "$status" -eq 124 ]; then
    echo "build_backend_driver warn: stage0-compat selfhost compile timed out; fallback to stage0"
  else
    echo "build_backend_driver warn: stage0-compat selfhost compile failed; fallback to stage0"
  fi
  exit 0
fi
if [ "$status" -eq 124 ]; then
  echo "[Error] build_backend_driver timed out (${build_timeout}s per compile attempt)" 1>&2
fi
echo "[Error] build_backend_driver selfhost_self_link failed (native and stage0-compat; stage0=$stage0)" 1>&2
echo "  hint: verify stage0 is runnable and can compile src/backend/tooling/backend_driver.cheng" 1>&2
exit 1
