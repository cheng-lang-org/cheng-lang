#!/usr/bin/env sh
. "$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)/env_prefix_bridge.sh"
set -eu
(set -o pipefail) 2>/dev/null && set -o pipefail

usage() {
  cat <<'USAGE'
Usage:
  src/tooling/build_backend_driver.sh [--name:<binName>]

Builds backend driver via stage0 selfhost rebuild:
  1) rebuild from a stage0 backend driver
  2) no seed/tar copy path

Output:
  <binName> (default: artifacts/backend_driver/cheng)

Env:
  BACKEND_BUILD_DRIVER_LINKER=system (default system; accepts self|system)
  BACKEND_BUILD_DRIVER_SELFHOST=1    (default 1)
  BACKEND_BUILD_DRIVER_STAGE0=<path> (optional stage driver override)
  BACKEND_BUILD_DRIVER_MULTI=0       (default 0; serial incremental)
  BACKEND_BUILD_DRIVER_MULTI_FORCE=0 (default 0)
  BACKEND_BUILD_DRIVER_INCREMENTAL=1 (default 1)
  BACKEND_BUILD_DRIVER_JOBS=0        (default 0=auto)
  BACKEND_BUILD_DRIVER_TIMEOUT=60    (default 60s per compile attempt)
  BACKEND_BUILD_DRIVER_SMOKE=0       (default 0; set 1 to run stage1 compile smoke)
  BACKEND_BUILD_DRIVER_REQUIRE_SMOKE=1 (default 1; require smoke for freshly rebuilt driver)
  BACKEND_BUILD_DRIVER_FRONTEND=stage1 (default stage1; frontend used for backend_driver selfbuild)
  BACKEND_BUILD_DRIVER_SMOKE_TARGETS=<csv> (default host)
  BACKEND_BUILD_DRIVER_MAX_STAGE0_ATTEMPTS=3 (default 3; 0=all candidates)
  BACKEND_BUILD_DRIVER_FORCE=0       (default 0; set 1 to skip reuse and force rebuild)
  BACKEND_BUILD_DRIVER_NO_RECOVER=0  (default 0; set 1 to fail hard when rebuild fails)
  BACKEND_IR=uir                     (default uir)
  GENERIC_MODE=dict                  (default dict)
  GENERIC_SPEC_BUDGET=0              (default 0)
USAGE
}

name="${BACKEND_LOCAL_DRIVER_REL:-artifacts/backend_driver/cheng}"
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

linker_mode="${BACKEND_BUILD_DRIVER_LINKER:-system}"
case "$linker_mode" in
  self|system)
    ;;
  *)
    echo "[Error] invalid BACKEND_BUILD_DRIVER_LINKER: $linker_mode (expected self|system)" 1>&2
    exit 2
    ;;
esac

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
  smoke_targets="${BACKEND_BUILD_DRIVER_SMOKE_TARGETS:-host}"
  old_ifs="$IFS"
  IFS=','
  for smoke_target in $smoke_targets; do
    IFS="$old_ifs"
    case "$smoke_target" in
      ""|host|auto)
        smoke_target="$target"
        ;;
    esac
    safe_target="$(printf '%s' "$smoke_target" | tr -c 'A-Za-z0-9._-' '_' | tr -s '_')"
    smoke_out="$root/chengcache/.build_backend_driver.stage1_smoke.${safe_target}.bin"
    rm -f "$smoke_out" "$smoke_out.tmp" "$smoke_out.tmp.linkobj" "$smoke_out.o"
    rm -rf "$smoke_out.objs" "$smoke_out.objs.lock"
    set +e
    run_with_timeout 40 env \
      MM=orc \
      CHENG_MM=orc \
      STAGE1_NO_POINTERS_NON_C_ABI=0 \
      CHENG_STAGE1_NO_POINTERS_NON_C_ABI=0 \
      STAGE1_NO_POINTERS_NON_C_ABI_INTERNAL=0 \
      CHENG_STAGE1_NO_POINTERS_NON_C_ABI_INTERNAL=0 \
      BACKEND_VALIDATE=0 \
      CHENG_BACKEND_VALIDATE=0 \
      STAGE1_SKIP_SEM=1 \
      CHENG_STAGE1_SKIP_SEM=1 \
      STAGE1_SKIP_CPROFILE=1 \
      CHENG_STAGE1_SKIP_CPROFILE=1 \
      GENERIC_MODE=dict \
      CHENG_GENERIC_MODE=dict \
      GENERIC_SPEC_BUDGET=0 \
      CHENG_GENERIC_SPEC_BUDGET=0 \
      STAGE1_SKIP_OWNERSHIP=1 \
      CHENG_STAGE1_SKIP_OWNERSHIP=1 \
      BACKEND_LINKER=self \
      CHENG_BACKEND_LINKER=self \
      BACKEND_NO_RUNTIME_C=1 \
      CHENG_BACKEND_NO_RUNTIME_C=1 \
      BACKEND_RUNTIME_OBJ= \
      CHENG_BACKEND_RUNTIME_OBJ= \
      BACKEND_EMIT=exe \
      CHENG_BACKEND_EMIT=exe \
      BACKEND_TARGET="$smoke_target" \
      CHENG_BACKEND_TARGET="$smoke_target" \
      BACKEND_FRONTEND=stage1 \
      CHENG_BACKEND_FRONTEND=stage1 \
      BACKEND_INPUT="$smoke_src" \
      CHENG_BACKEND_INPUT="$smoke_src" \
      BACKEND_OUTPUT="$smoke_out" \
      CHENG_BACKEND_OUTPUT="$smoke_out" \
      "$bin" >/dev/null 2>&1
    status=$?
    set -e
    if [ "$status" -ne 0 ] || [ ! -s "$smoke_out" ]; then
      IFS="$old_ifs"
      return 1
    fi
  done
  IFS="$old_ifs"
  return 0
}

backend_sources_newer_than() {
  out="$1"
  [ -e "$out" ] || return 0
  search_dirs=""
  for d in src/backend src/stage1 src/std src/core src/system; do
    if [ -d "$d" ]; then
      search_dirs="$search_dirs $d"
    fi
  done
  if [ "$search_dirs" = "" ]; then
    return 1
  fi
  # shellcheck disable=SC2086
  find $search_dirs -type f \( -name '*.cheng' -o -name '*.c' -o -name '*.h' \) \
    -newer "$out" -print -quit | grep -q .
}

to_abs() {
  p="$1"
  case "$p" in
    /*) ;;
    *) p="$root/$p" ;;
  esac
  d="$(dirname -- "$p")"
  b="$(basename -- "$p")"
  if [ -d "$d" ]; then
    d="$(CDPATH= cd -- "$d" && pwd)"
  fi
  printf "%s/%s\n" "$d" "$b"
}

clear_stale_lock_dir() {
  lock_dir="$1"
  if [ "$lock_dir" = "" ] || [ ! -d "$lock_dir" ]; then
    return 0
  fi
  owner_file="$lock_dir/owner.pid"
  if [ -f "$owner_file" ]; then
    owner_pid="$(cat "$owner_file" 2>/dev/null || true)"
    if [ "$owner_pid" != "" ] && kill -0 "$owner_pid" 2>/dev/null; then
      return 0
    fi
  fi
  rm -rf "$lock_dir" 2>/dev/null || true
  return 0
}

runtime_obj_has_required_symbols() {
  obj="$1"
  if [ ! -f "$obj" ]; then
    return 1
  fi
  if ! command -v nm >/dev/null 2>&1; then
    return 0
  fi
  set +e
  nm_out="$(nm -g "$obj" 2>/dev/null)"
  status="$?"
  set -e
  if [ "$status" -ne 0 ] || [ "$nm_out" = "" ]; then
    return 1
  fi
  case "$nm_out" in
    *" _reserve_ptr_void"*|*" T _reserve_ptr_void"*|*" t _reserve_ptr_void"*)
      return 0
      ;;
  esac
  return 1
}

prepare_driver_symbol_bridge_obj() {
  src="$1"
  out="$2"
  if [ ! -f "$src" ]; then
    return 1
  fi
  out_dir="$(dirname -- "$out")"
  if [ "$out_dir" != "" ] && [ ! -d "$out_dir" ]; then
    mkdir -p "$out_dir"
  fi
  if [ ! -f "$out" ] || [ "$src" -nt "$out" ]; then
    cc_bin="${CC:-cc}"
    "$cc_bin" -std=c11 -O2 -c "$src" -o "$out" >/dev/null 2>&1 || return 1
  fi
  return 0
}

mkdir -p chengcache
stage0_candidates_file="$root/chengcache/.build_backend_driver.stage0_candidates.$$"
: >"$stage0_candidates_file"

cleanup_stage0_candidates_file() {
  rm -f "$stage0_candidates_file" 2>/dev/null || true
}
trap cleanup_stage0_candidates_file EXIT INT TERM

append_stage0_candidate() {
  cand="$1"
  if [ "$cand" = "" ]; then
    return 0
  fi
  abs="$(to_abs "$cand")"
  if ! driver_sanity_ok "$abs"; then
    return 0
  fi
  if grep -Fx -- "$abs" "$stage0_candidates_file" >/dev/null 2>&1; then
    return 0
  fi
  printf '%s\n' "$abs" >>"$stage0_candidates_file"
  return 0
}

stage0_explicit="${BACKEND_BUILD_DRIVER_STAGE0:-}"
stage0_strict="0"
if [ "$stage0_explicit" != "" ]; then
  stage0_abs="$(to_abs "$stage0_explicit")"
  if ! driver_sanity_ok "$stage0_abs"; then
    if [ -x "$stage0_abs" ]; then
      echo "[Error] stage0 driver is not runnable: $stage0_abs" 1>&2
    else
      echo "[Error] stage0 driver is not executable: $stage0_abs" 1>&2
    fi
    exit 1
  fi
  printf '%s\n' "$stage0_abs" >"$stage0_candidates_file"
  stage0_strict="1"
else
  append_stage0_candidate "${BACKEND_DRIVER:-}"
  append_stage0_candidate "$root/artifacts/backend_seed/cheng.stage2"
  append_stage0_candidate "$root/artifacts/backend_selfhost_self_obj/cheng_stage0_default"
  append_stage0_candidate "$root/artifacts/backend_selfhost_self_obj/cheng_stage0_prod"
  append_stage0_candidate "$root/artifacts/backend_selfhost_self_obj/cheng.stage2"
  append_stage0_candidate "$root/artifacts/backend_selfhost_self_obj/cheng.stage1"
  append_stage0_candidate "$root/artifacts/backend_driver/cheng"
  append_stage0_candidate "$root/dist/releases/current/cheng"
  append_stage0_candidate "$root/cheng"
fi

if [ ! -s "$stage0_candidates_file" ]; then
  echo "[Error] build_backend_driver requires stage0 driver (set BACKEND_BUILD_DRIVER_STAGE0 or BACKEND_DRIVER)" 1>&2
  exit 1
fi

selfhost="${BACKEND_BUILD_DRIVER_SELFHOST:-1}"
if [ "$selfhost" = "0" ]; then
  echo "[Error] BACKEND_BUILD_DRIVER_SELFHOST=0 is not supported (seed copy path removed)" 1>&2
  exit 2
fi

driver_multi="${BACKEND_BUILD_DRIVER_MULTI:-0}"
driver_multi_force="${BACKEND_BUILD_DRIVER_MULTI_FORCE:-0}"
driver_incremental="${BACKEND_BUILD_DRIVER_INCREMENTAL:-1}"
driver_jobs="${BACKEND_BUILD_DRIVER_JOBS:-0}"
build_timeout="${BACKEND_BUILD_DRIVER_TIMEOUT:-60}"
driver_smoke="${BACKEND_BUILD_DRIVER_SMOKE:-0}"
driver_require_smoke="${BACKEND_BUILD_DRIVER_REQUIRE_SMOKE:-1}"
driver_frontend="${BACKEND_BUILD_DRIVER_FRONTEND:-stage1}"
max_stage0_attempts="${BACKEND_BUILD_DRIVER_MAX_STAGE0_ATTEMPTS:-3}"
build_force="${BACKEND_BUILD_DRIVER_FORCE:-0}"
build_no_recover="${BACKEND_BUILD_DRIVER_NO_RECOVER:-0}"
case "$max_stage0_attempts" in
  ''|*[!0-9]*)
    max_stage0_attempts=3
    ;;
esac
mm="${BACKEND_BUILD_DRIVER_MM:-${MM:-orc}}"
stage0_compat_allowed="${BACKEND_BUILD_DRIVER_STAGE0_COMPAT:-0}"
if [ "$stage0_compat_allowed" != "0" ]; then
  echo "[Error] build_backend_driver removed BACKEND_BUILD_DRIVER_STAGE0_COMPAT (only 0 is supported)" 1>&2
  exit 2
fi
if [ "${BACKEND_IR:-}" = "" ]; then
  export BACKEND_IR=uir
fi
if [ "${GENERIC_MODE:-}" = "" ]; then
  export GENERIC_MODE=dict
fi
if [ "${GENERIC_SPEC_BUDGET:-}" = "" ]; then
  export GENERIC_SPEC_BUDGET=0
fi
if [ "${STAGE1_SKIP_SEM:-}" = "" ]; then
  export STAGE1_SKIP_SEM=1
fi
if [ "${STAGE1_SKIP_OWNERSHIP:-}" = "" ]; then
  export STAGE1_SKIP_OWNERSHIP=1
fi
if [ "${STAGE1_SKIP_CPROFILE:-}" = "" ]; then
  export STAGE1_SKIP_CPROFILE=1
fi

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
tmp_bin_abs="${name_abs}.tmp"
tmp_obj_abs="${tmp_bin_abs}.o"

clear_stale_lock_dir "${name_abs}.objs.lock"
clear_stale_lock_dir "${tmp_bin_abs}.objs.lock"

if [ "$build_force" != "1" ] && [ -x "$name_abs" ]; then
  if ! backend_sources_newer_than "$name_abs" && driver_sanity_ok "$name_abs"; then
    if [ "$driver_smoke" = "1" ] && ! driver_stage1_smoke_ok "$name_abs"; then
      :
    else
      if [ "$driver_smoke" = "1" ]; then
        echo "build_backend_driver ok (reuse+smoke)"
      else
        echo "build_backend_driver ok (reuse)"
      fi
      exit 0
    fi
  fi
fi

runtime_src="src/std/system_helpers_backend.cheng"
runtime_obj="chengcache/system_helpers.backend.cheng.${target}.o"
runtime_obj_abs="$root/$runtime_obj"
driver_symbol_bridge_src="$root/src/backend/tooling/backend_driver_symbol_bridge.c"
driver_symbol_bridge_obj_abs="$root/chengcache/backend_driver_symbol_bridge.o"
log_root="$root/chengcache/build_backend_driver"
attempt_report="$log_root/attempts.$$.tsv"
module_cache_path="$root/chengcache/build_backend_driver.module_cache.tsv"
mkdir -p "$log_root"
: >"$attempt_report"

driver_ldflags_base="${BACKEND_BUILD_DRIVER_LDFLAGS:-${BACKEND_LDFLAGS:-}}"
driver_ldflags="$driver_ldflags_base"
if [ "$linker_mode" = "system" ]; then
  if ! prepare_driver_symbol_bridge_obj "$driver_symbol_bridge_src" "$driver_symbol_bridge_obj_abs"; then
    echo "[Error] failed to build backend driver symbol bridge object: $driver_symbol_bridge_src" 1>&2
    exit 1
  fi
  if [ "$driver_ldflags" = "" ]; then
    driver_ldflags="$driver_symbol_bridge_obj_abs"
  else
    driver_ldflags="$driver_ldflags $driver_symbol_bridge_obj_abs"
  fi
fi

stage0_tag() {
  raw="$1"
  tag="$(printf '%s' "$raw" | tr '/: ' '___' | tr -cd 'A-Za-z0-9._-')"
  if [ "$tag" = "" ]; then
    tag="stage0"
  fi
  printf '%s\n' "$tag"
}

run_driver_compile_once() {
  compiler="$1"
  out_bin="$2"
  multi_now="$3"
  multi_force_now="$4"
  log_file="$5"
  if [ "$linker_mode" = "self" ]; then
    run_with_timeout "$build_timeout" env \
      MM="$mm" \
      CHENG_MM="$mm" \
      STAGE1_SKIP_SEM="${STAGE1_SKIP_SEM:-1}" \
      CHENG_STAGE1_SKIP_SEM="${CHENG_STAGE1_SKIP_SEM:-${STAGE1_SKIP_SEM:-1}}" \
      STAGE1_SKIP_OWNERSHIP="${STAGE1_SKIP_OWNERSHIP:-1}" \
      CHENG_STAGE1_SKIP_OWNERSHIP="${CHENG_STAGE1_SKIP_OWNERSHIP:-${STAGE1_SKIP_OWNERSHIP:-1}}" \
      STAGE1_SKIP_CPROFILE="${STAGE1_SKIP_CPROFILE:-1}" \
      CHENG_STAGE1_SKIP_CPROFILE="${CHENG_STAGE1_SKIP_CPROFILE:-${STAGE1_SKIP_CPROFILE:-1}}" \
      STAGE1_AUTO_SYSTEM=0 \
      CHENG_STAGE1_AUTO_SYSTEM=0 \
      BACKEND_MULTI="$multi_now" \
      CHENG_BACKEND_MULTI="$multi_now" \
      BACKEND_MULTI_FORCE="$multi_force_now" \
      CHENG_BACKEND_MULTI_FORCE="$multi_force_now" \
      BACKEND_INCREMENTAL="$driver_incremental" \
      CHENG_BACKEND_INCREMENTAL="$driver_incremental" \
      BACKEND_JOBS="$driver_jobs" \
      CHENG_BACKEND_JOBS="$driver_jobs" \
      BACKEND_VALIDATE=0 \
      CHENG_BACKEND_VALIDATE=0 \
      BACKEND_WHOLE_PROGRAM=1 \
      CHENG_BACKEND_WHOLE_PROGRAM=1 \
      BACKEND_LINKER=self \
      CHENG_BACKEND_LINKER=self \
      BACKEND_NO_RUNTIME_C=1 \
      CHENG_BACKEND_NO_RUNTIME_C=1 \
      BACKEND_RUNTIME_OBJ="$runtime_obj_abs" \
      CHENG_BACKEND_RUNTIME_OBJ="$runtime_obj_abs" \
      BACKEND_LDFLAGS="$driver_ldflags" \
      CHENG_BACKEND_LDFLAGS="$driver_ldflags" \
      BACKEND_EMIT=exe \
      CHENG_BACKEND_EMIT=exe \
      BACKEND_TARGET="$target" \
      CHENG_BACKEND_TARGET="$target" \
      BACKEND_FRONTEND="$driver_frontend" \
      CHENG_BACKEND_FRONTEND="$driver_frontend" \
      BACKEND_INPUT="src/backend/tooling/backend_driver.cheng" \
      CHENG_BACKEND_INPUT="src/backend/tooling/backend_driver.cheng" \
      BACKEND_OUTPUT="$out_bin" \
      CHENG_BACKEND_OUTPUT="$out_bin" \
      "$compiler" >>"$log_file" 2>&1
    return
  fi
  run_with_timeout "$build_timeout" env \
    MM="$mm" \
    CHENG_MM="$mm" \
    STAGE1_SKIP_SEM="${STAGE1_SKIP_SEM:-1}" \
    CHENG_STAGE1_SKIP_SEM="${CHENG_STAGE1_SKIP_SEM:-${STAGE1_SKIP_SEM:-1}}" \
    STAGE1_SKIP_OWNERSHIP="${STAGE1_SKIP_OWNERSHIP:-1}" \
    CHENG_STAGE1_SKIP_OWNERSHIP="${CHENG_STAGE1_SKIP_OWNERSHIP:-${STAGE1_SKIP_OWNERSHIP:-1}}" \
    STAGE1_SKIP_CPROFILE="${STAGE1_SKIP_CPROFILE:-1}" \
    CHENG_STAGE1_SKIP_CPROFILE="${CHENG_STAGE1_SKIP_CPROFILE:-${STAGE1_SKIP_CPROFILE:-1}}" \
    STAGE1_AUTO_SYSTEM=0 \
    CHENG_STAGE1_AUTO_SYSTEM=0 \
    BACKEND_MULTI="$multi_now" \
    CHENG_BACKEND_MULTI="$multi_now" \
    BACKEND_MULTI_FORCE="$multi_force_now" \
    CHENG_BACKEND_MULTI_FORCE="$multi_force_now" \
    BACKEND_INCREMENTAL="$driver_incremental" \
    CHENG_BACKEND_INCREMENTAL="$driver_incremental" \
    BACKEND_JOBS="$driver_jobs" \
    CHENG_BACKEND_JOBS="$driver_jobs" \
    BACKEND_VALIDATE=0 \
    CHENG_BACKEND_VALIDATE=0 \
    BACKEND_WHOLE_PROGRAM=1 \
    CHENG_BACKEND_WHOLE_PROGRAM=1 \
    BACKEND_LINKER=system \
    CHENG_BACKEND_LINKER=system \
    BACKEND_NO_RUNTIME_C=0 \
    CHENG_BACKEND_NO_RUNTIME_C=0 \
    BACKEND_RUNTIME_OBJ= \
    CHENG_BACKEND_RUNTIME_OBJ= \
    BACKEND_LDFLAGS="$driver_ldflags" \
    CHENG_BACKEND_LDFLAGS="$driver_ldflags" \
    BACKEND_EMIT=exe \
    CHENG_BACKEND_EMIT=exe \
    BACKEND_TARGET="$target" \
    CHENG_BACKEND_TARGET="$target" \
    BACKEND_FRONTEND="$driver_frontend" \
    CHENG_BACKEND_FRONTEND="$driver_frontend" \
    BACKEND_INPUT="src/backend/tooling/backend_driver.cheng" \
    CHENG_BACKEND_INPUT="src/backend/tooling/backend_driver.cheng" \
    BACKEND_OUTPUT="$out_bin" \
    CHENG_BACKEND_OUTPUT="$out_bin" \
    "$compiler" >>"$log_file" 2>&1
}

attempt_build_with_stage0() {
  stage0="$1"
  attempt_idx="$2"
  stage0_tag_s="$(stage0_tag "$stage0")"
  attempt_log="$log_root/attempt_${attempt_idx}_${stage0_tag_s}.log"
  : >"$attempt_log"
  printf 'stage0=%s\n' "$stage0" >>"$attempt_log"

  runtime_needs_rebuild="0"
  if [ "$linker_mode" = "self" ]; then
    if [ ! -f "$runtime_obj_abs" ]; then
      runtime_needs_rebuild="1"
    elif ! runtime_obj_has_required_symbols "$runtime_obj_abs"; then
      runtime_needs_rebuild="1"
    fi
  fi
  if [ "$runtime_needs_rebuild" = "1" ]; then
    printf 'missing runtime_obj=%s (self-link requires prebuilt runtime object)\n' "$runtime_obj_abs" >>"$attempt_log"
    printf 'hint: prepare runtime object before driver selfbuild (source=%s)\n' "$runtime_src" >>"$attempt_log"
    attempt_status=1
    return 1
  fi

  rm -f "$tmp_bin_abs" "$tmp_obj_abs"
  set +e
  run_driver_compile_once "$stage0" "$tmp_bin_abs" "$driver_multi" "$driver_multi_force" "$attempt_log"
  status=$?
  set -e
  if [ "$status" -ne 0 ] && [ "$driver_multi" != "0" ] && [ "$status" -ne 124 ]; then
    rm -f "$tmp_bin_abs" "$tmp_obj_abs"
    set +e
    run_driver_compile_once "$stage0" "$tmp_bin_abs" "0" "0" "$attempt_log"
    status=$?
    set -e
  fi
  if [ "$status" -eq 0 ] && [ -x "$tmp_bin_abs" ] && driver_sanity_ok "$tmp_bin_abs"; then
    smoke_required="$driver_require_smoke"
    if [ "$driver_smoke" = "1" ]; then
      smoke_required="1"
    fi
    smoke_ok="1"
    if [ "$smoke_required" = "1" ] && ! driver_stage1_smoke_ok "$tmp_bin_abs"; then
      smoke_ok="0"
    fi
    if [ "$smoke_ok" = "1" ]; then
      mode_tag="system_link"
      if [ "$linker_mode" = "self" ]; then
        mode_tag="self_link"
      fi
      mv "$tmp_bin_abs" "$name_abs"
      if [ -s "$tmp_obj_abs" ]; then
        mv "$tmp_obj_abs" "$name_obj_abs"
      fi
      if [ "$smoke_required" = "1" ]; then
        echo "build_backend_driver ok (selfhost_${mode_tag}+smoke; stage0=$stage0)"
      else
        echo "build_backend_driver ok (selfhost_${mode_tag}; stage0=$stage0)"
      fi
      return 0
    fi
    attempt_status=86
    rm -f "$tmp_bin_abs" "$tmp_obj_abs"
    status="$attempt_status"
  else
    attempt_status="$status"
    rm -f "$tmp_bin_abs" "$tmp_obj_abs"
    if [ "$attempt_status" -eq 0 ]; then
      attempt_status=1
    fi
  fi

  rm -f "$tmp_bin_abs" "$tmp_obj_abs"
  return 1
}

attempt_idx=0
while IFS= read -r stage0; do
  if [ "$stage0" = "" ]; then
    continue
  fi
  attempt_idx=$((attempt_idx + 1))
  if [ "$max_stage0_attempts" -gt 0 ] && [ "$attempt_idx" -gt "$max_stage0_attempts" ]; then
    break
  fi
  attempt_status=1
  attempt_log=""
  if attempt_build_with_stage0 "$stage0" "$attempt_idx"; then
    exit 0
  fi
  printf '%s\tstatus=%s\tlog=%s\n' "$stage0" "$attempt_status" "$attempt_log" >>"$attempt_report"
  if [ "$stage0_strict" = "1" ]; then
    break
  fi
done <"$stage0_candidates_file"

echo "[Error] build_backend_driver selfhost_${linker_mode}_link failed for all stage0 candidates" 1>&2
if [ -s "$attempt_report" ]; then
  echo "  attempts:" 1>&2
  sed 's/^/  - /' "$attempt_report" 1>&2
fi
if [ "$build_no_recover" = "1" ]; then
  echo "  hint: hard-fail enabled (BACKEND_BUILD_DRIVER_NO_RECOVER=1), no fallback to existing driver" 1>&2
  echo "  hint: inspect logs under $log_root" 1>&2
  exit 1
fi
recover_smoke_required="$driver_require_smoke"
if [ "$driver_smoke" = "1" ]; then
  recover_smoke_required="1"
fi
if [ -x "$name_abs" ] && driver_sanity_ok "$name_abs"; then
  recover_ok="1"
  if [ "$recover_smoke_required" = "1" ] && ! driver_stage1_smoke_ok "$name_abs"; then
    recover_ok="0"
  fi
  if [ "$recover_ok" = "1" ]; then
    echo "[Warn] build_backend_driver rebuild failed; keep existing healthy driver: $name_abs" 1>&2
    exit 0
  fi
fi
echo "  hint: inspect logs under $log_root" 1>&2
exit 1
