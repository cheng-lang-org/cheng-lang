#!/usr/bin/env sh
set -eu

cc="${CC:-cc}"
stage1_cflags="${CHENG_STAGE1_CFLAGS:-}"
if [ -z "${stage1_cflags}" ]; then
  stage1_cflags="${CFLAGS:-}"
fi
if [ -z "${stage1_cflags}" ]; then
  stage1_cflags="-O2"
fi
if [ "${CHENG_BOOTSTRAP_FAST:-0}" = "1" ]; then
  stage1_cflags="${CHENG_BOOTSTRAP_FAST_CFLAGS:--O0}"
fi

current_pid=""
current_pgid=""

cleanup_child() {
  if [ "${current_pid:-}" != "" ]; then
    if kill -0 "$current_pid" 2>/dev/null; then
      if [ "${current_pgid:-}" != "" ]; then
        kill "-$current_pgid" 2>/dev/null || true
      else
        kill "$current_pid" 2>/dev/null || true
      fi
      wait "$current_pid" 2>/dev/null || true
    fi
  fi
}

run_bg() {
  current_pid=""
  current_pgid=""
  if command -v setsid >/dev/null 2>&1; then
    setsid "$@" &
    current_pid=$!
    current_pgid=$current_pid
  else
    "$@" &
    current_pid=$!
  fi
  if wait "$current_pid"; then
    status=0
  else
    status=$?
  fi
  current_pid=""
  current_pgid=""
  return "$status"
}

trap 'cleanup_child; exit 130' INT
trap 'cleanup_child; exit 143' TERM
trap 'cleanup_child' EXIT

usage() {
  cat <<'EOF'
Usage:
 src/tooling/bootstrap.sh [--fullspec]

Notes:
  - Run the full Stage0(C) -> Stage1 bootstrap (generates a bootstrap runner) and verify Stage1 output determinism.
  - Dependencies:
    - System C compiler
  - Stage1 compilation uses internal compile by default (no Stage0 fallback).

Options:
  --fullspec  Also compile and run the fullspec regression sample (should output `fullspec ok`)
  --internal  Legacy flag (internal compile is now default)
  --skip-determinism  Skip determinism check (same as CHENG_BOOTSTRAP_SKIP_DETERMINISM=1)
Env:
  CHENG_BOOTSTRAP_FAST=1        Use fast CFLAGS for bootstrap builds (default: -O0)
  CHENG_BOOTSTRAP_FAST_CFLAGS=  Override fast CFLAGS (default: -O0)
  CHENG_BOOTSTRAP_DET_REUSE_BOOTSTRAP=1  Reuse bootstrap output for determinism (default: 1)
EOF
}

fullspec="0"
skip_determinism="0"
while [ "${1:-}" != "" ]; do
  case "$1" in
    --help|-h)
      usage
      exit 0
      ;;
    --fullspec)
      fullspec="1"
      ;;
    --internal)
      # no-op: internal compile is now the default
      ;;
    --skip-determinism)
      skip_determinism="1"
      ;;
    *)
      echo "[Error] unknown arg: $1" 1>&2
      usage
      exit 2
      ;;
  esac
  shift || true
done

here="$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)"
root="$(CDPATH= cd -- "$here/../.." && pwd)"
cd "$root"

tmp="/tmp"
stage1_c="$tmp/stage1_runner.c"
bootstrap_c="$tmp/stage1_bootstrap_runner.c"
fullspec_c="$tmp/stage1_codegen_fullspec.c"
bootstrap_cache="${CHENG_BOOTSTRAP_CACHE:-${CHENG_CACHE:-0}}"
runtime_inc="${CHENG_RT_INC:-${CHENG_RUNTIME_INC:-$root/runtime/include}}"
stage1_includes="-I$runtime_inc -I$root/src/runtime/native"
force_determinism="${CHENG_BOOTSTRAP_FORCE_DETERMINISM:-0}"
bootstrap_pkg_roots="$root"
if [ -n "${CHENG_PKG_ROOTS:-}" ]; then
  bootstrap_pkg_roots="$bootstrap_pkg_roots:$CHENG_PKG_ROOTS"
fi
export CHENG_PKG_ROOTS="$bootstrap_pkg_roots"

if [ "$fullspec" = "1" ] && [ -z "${CHENG_BOOTSTRAP_CACHE:-}" ] && [ "$bootstrap_cache" != "0" ]; then
  bootstrap_cache="$(mktemp -d "/tmp/cheng_bootstrap_cache.XXXXXX")"
  echo "== Bootstrap cache (fullspec isolation): $bootstrap_cache =="
fi

stat_style="gnu"
if stat -f %m "$root/AGENTS.md" >/dev/null 2>&1; then
  stat_style="bsd"
fi

stat_mtime() {
  if [ "$stat_style" = "bsd" ]; then
    stat -f %m "$1"
    return 0
  fi
  stat -c %Y "$1"
}

dir_mtimes() {
  if [ "$stat_style" = "bsd" ]; then
    find "$1" -type f -exec stat -f %m {} \;
    return 0
  fi
  find "$1" -type f -exec stat -c %Y {} \;
}

max_mtime() {
  max="0"
  for path in "$@"; do
    if [ -d "$path" ]; then
      for mt in $(dir_mtimes "$path"); do
        if [ "$mt" -gt "$max" ]; then
          max="$mt"
        fi
      done
      continue
    fi
    if [ -e "$path" ]; then
      mt="$(stat_mtime "$path")"
      if [ "$mt" -gt "$max" ]; then
        max="$mt"
      fi
    fi
  done
  printf '%s\n' "$max"
}

hash256() {
  if command -v shasum >/dev/null 2>&1; then
    set -- $(shasum -a 256 "$1")
    printf '%s\n' "$1"
    return 0
  fi
  if command -v sha256sum >/dev/null 2>&1; then
    set -- $(sha256sum "$1")
    printf '%s\n' "$1"
    return 0
  fi
  echo "[Error] missing shasum/sha256sum for determinism check" 1>&2
  exit 2
}

normalized_hash() {
  if ! grep -q "__cheng_case_" "$1" 2>/dev/null; then
    hash256 "$1"
    return 0
  fi
  norm_tmp="$(mktemp "/tmp/cheng_norm.XXXXXX")"
  sed -E 's/__cheng_case_[0-9]+/__cheng_case_X/g' "$1" > "$norm_tmp"
  norm_hash="$(hash256 "$norm_tmp")"
  rm -f "$norm_tmp"
  printf '%s\n' "$norm_hash"
}

normalized_hash_cached() {
  in="$1"
  cache="$2"
  if [ "$cache" = "" ]; then
    normalized_hash "$in"
    return 0
  fi
  raw_hash="$(hash256 "$in")"
  if [ -f "$cache" ]; then
    cached_raw="$(sed -n '1p' "$cache")"
    cached_norm="$(sed -n '2p' "$cache")"
    if [ "$cached_raw" = "$raw_hash" ] && [ -n "$cached_norm" ]; then
      printf '%s\n' "$cached_norm"
      return 0
    fi
  fi
  norm_hash="$(normalized_hash "$in")"
  printf '%s\n%s\n' "$raw_hash" "$norm_hash" > "$cache"
  printf '%s\n' "$norm_hash"
}

determinism_inputs() {
  echo "stage1_seed=$(hash256 src/stage1/stage1_runner.seed.c)"
  echo "compiler_version=${CHENG_COMPILER_VERSION:-}"
  echo "stage1_version=${CHENG_STAGE1_VERSION:-}"
  echo "target=${CHENG_TARGET:-}"
  echo "stage1_cflags=$stage1_cflags"
  echo "cc=$cc"
  echo "mm=${CHENG_BOOTSTRAP_MM:-off}"
  echo "mm_strict=${CHENG_MM_STRICT:-}"
  echo "stage1_src_mtime=$(max_mtime src/stage1 src/std \"$runtime_inc\")"
  echo "cache=${bootstrap_cache}"
  echo "cache_dir=${CHENG_CACHE_DIR:-}"
  echo "ide_root=${CHENG_IDE_ROOT:-}"
  echo "gui_root=${CHENG_GUI_ROOT:-}"
  echo "pkg_roots=${CHENG_PKG_ROOTS:-}"
  echo "pkg_lock=${CHENG_PKG_LOCK:-}"
  echo "skip_sem=${CHENG_STAGE1_SKIP_SEM:-0}"
  echo "skip_ownership=${CHENG_STAGE1_SKIP_OWNERSHIP:-0}"
}

determinism_inputs_key() {
  echo "stage1_seed=$(hash256 src/stage1/stage1_runner.seed.c)"
  echo "compiler_version=${CHENG_COMPILER_VERSION:-}"
  echo "stage1_version=${CHENG_STAGE1_VERSION:-}"
  echo "target=${CHENG_TARGET:-}"
  echo "stage1_cflags=$stage1_cflags"
  echo "cc=$cc"
  echo "mm=${CHENG_BOOTSTRAP_MM:-off}"
  echo "mm_strict=${CHENG_MM_STRICT:-}"
  echo "stage1_src_mtime=$(max_mtime src/stage1 src/std \"$runtime_inc\")"
  # Keep cache fields stable so the key can drive a deterministic cache dir.
  echo "cache=determinism"
  echo "cache_dir=determinism"
  echo "ide_root=${CHENG_IDE_ROOT:-}"
  echo "gui_root=${CHENG_GUI_ROOT:-}"
  echo "pkg_roots=${CHENG_PKG_ROOTS:-}"
  echo "pkg_lock=${CHENG_PKG_LOCK:-}"
  echo "skip_sem=${CHENG_STAGE1_SKIP_SEM:-0}"
  echo "skip_ownership=${CHENG_STAGE1_SKIP_OWNERSHIP:-0}"
}

require_cmd() {
  if ! command -v "$1" >/dev/null 2>&1; then
    echo "[Error] missing command: $1" 1>&2
    exit 2
  fi
}

require_exec() {
  if [ ! -x "$1" ]; then
    echo "[Error] missing executable: $1" 1>&2
    exit 2
  fi
}

stage0_bin="bin/cheng"
echo "== Stage0: build bin/cheng (stage0c) =="
src/tooling/build_stage0c.sh
if [ "${CHENG_STAGE0_C:-}" = "1" ]; then
  stage0_bin="bin/cheng_c"
fi

use_existing_bootstrap="${CHENG_BOOTSTRAP_USE_EXISTING_BOOTSTRAP:-0}"
trace_bootstrap="${CHENG_BOOTSTRAP_TRACE:-0}"
seed_c="${CHENG_BOOTSTRAP_USE_SEED_C:-0}"
skip_examples="${CHENG_BOOTSTRAP_SKIP_EXAMPLES:-0}"
skip_ownership="${CHENG_STAGE1_SKIP_OWNERSHIP:-0}"
skip_sem="${CHENG_STAGE1_SKIP_SEM:-0}"
bootstrap_mm="${CHENG_BOOTSTRAP_MM:-off}"
stage0_core="${CHENG_STAGE0C_CORE:-1}"
stage0_core_fallback="${CHENG_STAGE0C_CORE_ALLOW_FALLBACK:-0}"
cache_enabled="0"
if [ "${bootstrap_cache:-0}" != "0" ] && [ "${bootstrap_cache:-}" != "" ]; then
  cache_enabled="1"
fi
mkdir -p chengcache

bootstrap_flags_file="chengcache/bootstrap.bootstrap.flags"
bootstrap_flags="$(cat <<EOF
cc=$cc
stage1_cflags=$stage1_cflags
mm=$bootstrap_mm
skip_sem=$skip_sem
skip_ownership=$skip_ownership
cache_enabled=$cache_enabled
runtime_inc=$runtime_inc
stage0_core=$stage0_core
stage0_core_fallback=$stage0_core_fallback
EOF
)"
bootstrap_inputs_mtime="$(max_mtime src/stage0c src/stage1 src/std "$runtime_inc")"
bootstrap_outputs_mtime="$(max_mtime "$bootstrap_c" ./stage1_bootstrap_runner)"
bootstrap_flags_ok="0"
bootstrap_flags_missing="0"
if [ -f "$bootstrap_flags_file" ]; then
  if [ "$(cat "$bootstrap_flags_file")" = "$bootstrap_flags" ]; then
    bootstrap_flags_ok="1"
  fi
else
  bootstrap_flags_ok="1"
  bootstrap_flags_missing="1"
fi

bootstrap_up_to_date="0"
if [ "$use_existing_bootstrap" = "1" ] && [ -x ./stage1_bootstrap_runner ]; then
  bootstrap_up_to_date="1"
elif [ "$bootstrap_flags_ok" = "1" ] && [ -f "$bootstrap_c" ] && [ -x ./stage1_bootstrap_runner ]; then
  if [ "$bootstrap_outputs_mtime" -ge "$bootstrap_inputs_mtime" ]; then
    bootstrap_up_to_date="1"
  fi
fi

if [ "$bootstrap_up_to_date" = "1" ]; then
  if [ "$use_existing_bootstrap" = "1" ]; then
    echo "== Use existing stage1_bootstrap_runner =="
  else
    echo "== Bootstrap runner up-to-date (skip codegen/build) =="
    if [ "$bootstrap_flags_missing" = "1" ]; then
      printf '%s\n' "$bootstrap_flags" > "$bootstrap_flags_file"
    fi
  fi
else
  echo "== Stage0 -> bootstrap: generate runner C =="
  if [ "$trace_bootstrap" = "1" ]; then
    CHENG_CACHE="$bootstrap_cache" CHENG_MM="$bootstrap_mm" CHENG_C_SYSTEM=1 \
    CHENG_STAGE0C_CORE="$stage0_core" CHENG_STAGE0C_CORE_ALLOW_FALLBACK="$stage0_core_fallback" \
    CHENG_STAGE1_TRACE=1 CHENG_STAGE1_TRACE_FLUSH=1 CHENG_STAGE1_TRACE_EMIT=1 \
      CHENG_STAGE1_SKIP_OWNERSHIP="$skip_ownership" CHENG_STAGE1_SKIP_SEM="$skip_sem" \
      "$stage0_bin" --mode:c --file:src/stage1/frontend_bootstrap.cheng --out:"$bootstrap_c"
  else
    CHENG_CACHE="$bootstrap_cache" CHENG_MM="$bootstrap_mm" CHENG_C_SYSTEM=1 \
    CHENG_STAGE0C_CORE="$stage0_core" CHENG_STAGE0C_CORE_ALLOW_FALLBACK="$stage0_core_fallback" \
    CHENG_STAGE1_SKIP_OWNERSHIP="$skip_ownership" CHENG_STAGE1_SKIP_SEM="$skip_sem" \
      "$stage0_bin" --mode:c --file:src/stage1/frontend_bootstrap.cheng --out:"$bootstrap_c" >/dev/null
  fi

  echo "== Build bootstrap runner =="
  if [ ! -f "$bootstrap_c" ]; then
    echo "[Error] missing generated C file: $bootstrap_c" 1>&2
    exit 2
  fi
  "$cc" $stage1_cflags $stage1_includes -c "$bootstrap_c" -o chengcache/stage1_bootstrap_runner.o
  "$cc" $stage1_cflags $stage1_includes -c src/runtime/native/system_helpers.c -o chengcache/stage1_bootstrap_runner.system_helpers.o
  "$cc" $stage1_cflags $stage1_includes chengcache/stage1_bootstrap_runner.o chengcache/stage1_bootstrap_runner.system_helpers.o -o stage1_bootstrap_runner
  printf '%s\n' "$bootstrap_flags" > "$bootstrap_flags_file"
fi

stage1_flags_file="chengcache/bootstrap.stage1.flags"
stage1_flags="$(cat <<EOF
cc=$cc
stage1_cflags=$stage1_cflags
mm=$bootstrap_mm
skip_sem=$skip_sem
skip_ownership=$skip_ownership
cache_enabled=$cache_enabled
runtime_inc=$runtime_inc
seed_c=$seed_c
skip_examples=$skip_examples
EOF
)"
stage1_inputs_mtime="$(max_mtime ./stage1_bootstrap_runner src/stage1 src/std "$runtime_inc")"
fullspec_inputs_mtime="$stage1_inputs_mtime"
fullspec_extra_mtime="$(max_mtime examples/stage1_codegen_fullspec.cheng examples/foo)"
if [ "$fullspec_extra_mtime" -gt "$fullspec_inputs_mtime" ]; then
  fullspec_inputs_mtime="$fullspec_extra_mtime"
fi
stage1_outputs_mtime="$(max_mtime "$stage1_c" ./stage1_runner)"
stage1_flags_ok="0"
stage1_flags_missing="0"
if [ -f "$stage1_flags_file" ]; then
  if [ "$(cat "$stage1_flags_file")" = "$stage1_flags" ]; then
    stage1_flags_ok="1"
  fi
else
  stage1_flags_ok="1"
  stage1_flags_missing="1"
fi

stage1_up_to_date="0"
if [ "$stage1_flags_ok" = "1" ] && [ -f "$stage1_c" ] && [ -x ./stage1_runner ]; then
  if [ "$stage1_outputs_mtime" -ge "$stage1_inputs_mtime" ]; then
    stage1_up_to_date="1"
  fi
fi
if [ "$fullspec" = "1" ]; then
  if [ ! -f "$fullspec_c" ] || [ ! -x ./stage1_codegen_fullspec ]; then
    stage1_up_to_date="0"
  else
    fullspec_outputs_mtime="$(max_mtime "$fullspec_c" ./stage1_codegen_fullspec)"
    if [ "$fullspec_outputs_mtime" -lt "$fullspec_inputs_mtime" ]; then
      stage1_up_to_date="0"
    fi
  fi
fi

did_regen_stage1="0"
stage1_hash_before=""
stage1_hash_after=""
stage1_same_output="0"
if [ "$stage1_up_to_date" = "1" ]; then
  echo "== Stage1 artifacts up-to-date (skip runner/build) =="
  if [ "$stage1_flags_missing" = "1" ]; then
    printf '%s\n' "$stage1_flags" > "$stage1_flags_file"
  fi
else
  echo "== Run bootstrap runner (generate stage1 runner) =="
  if [ -f "$stage1_c" ]; then
    stage1_hash_before="$(normalized_hash "$stage1_c")"
  fi
  rm -f "$stage1_c" "$fullspec_c"
  if [ "$trace_bootstrap" = "1" ]; then
    CHENG_STAGE1_USE_SEED_C="$seed_c" CHENG_BOOTSTRAP_SKIP_EXAMPLES="$skip_examples" CHENG_CACHE="$bootstrap_cache" CHENG_MM="$bootstrap_mm" \
    CHENG_STAGE1_TRACE=1 CHENG_STAGE1_TRACE_FLUSH=1 CHENG_STAGE1_TRACE_EMIT=1 \
      CHENG_STAGE1_SKIP_OWNERSHIP="$skip_ownership" \
      CHENG_STAGE1_SKIP_SEM="$skip_sem" \
      CHENG_STAGE1_FULLSPEC="$fullspec" run_bg ./stage1_bootstrap_runner
  else
    CHENG_STAGE1_USE_SEED_C="$seed_c" CHENG_BOOTSTRAP_SKIP_EXAMPLES="$skip_examples" CHENG_CACHE="$bootstrap_cache" CHENG_MM="$bootstrap_mm" \
  CHENG_STAGE1_SKIP_OWNERSHIP="$skip_ownership" \
  CHENG_STAGE1_SKIP_SEM="$skip_sem" \
  CHENG_STAGE1_FULLSPEC="$fullspec" run_bg ./stage1_bootstrap_runner
  fi
  did_regen_stage1="1"
  if [ -f "$stage1_c" ]; then
    stage1_hash_after="$(normalized_hash "$stage1_c")"
    if [ -n "$stage1_hash_before" ] && [ "$stage1_hash_after" = "$stage1_hash_before" ] && \
       [ "$stage1_flags_ok" = "1" ] && [ -x ./stage1_runner ]; then
      stage1_same_output="1"
    fi
  fi
fi

if [ "$fullspec" = "1" ]; then
  if [ "$did_regen_stage1" = "1" ]; then
    echo "== (optional) Build & run fullspec =="
    if [ ! -f "$fullspec_c" ]; then
      echo "[Error] missing generated C file: $fullspec_c" 1>&2
      exit 2
    fi
    "$cc" $stage1_cflags $stage1_includes -c "$fullspec_c" -o chengcache/stage1_codegen_fullspec.o
    "$cc" $stage1_cflags $stage1_includes -c src/runtime/native/system_helpers.c -o chengcache/stage1_codegen_fullspec.system_helpers.o
    "$cc" $stage1_cflags $stage1_includes chengcache/stage1_codegen_fullspec.o chengcache/stage1_codegen_fullspec.system_helpers.o -o stage1_codegen_fullspec
  else
    echo "== (optional) Run cached fullspec =="
  fi
  CHENG_MM="${CHENG_BOOTSTRAP_MM:-off}" ./stage1_codegen_fullspec
fi

if [ "$did_regen_stage1" = "1" ]; then
  if [ "$stage1_same_output" = "1" ]; then
    echo "== Stage1 C unchanged (skip stage1_runner build) =="
    if [ "$stage1_flags_missing" = "1" ]; then
      printf '%s\n' "$stage1_flags" > "$stage1_flags_file"
    fi
  else
    echo "== Build stage1_runner =="
    if [ ! -f "$stage1_c" ]; then
      echo "[Error] missing generated C file: $stage1_c" 1>&2
      exit 2
    fi
    "$cc" $stage1_cflags $stage1_includes -c "$stage1_c" -o chengcache/stage1_runner.o
    "$cc" $stage1_cflags $stage1_includes -c src/runtime/native/system_helpers.c -o chengcache/stage1_runner.system_helpers.o
    "$cc" $stage1_cflags $stage1_includes chengcache/stage1_runner.o chengcache/stage1_runner.system_helpers.o -o stage1_runner
    printf '%s\n' "$stage1_flags" > "$stage1_flags_file"
  fi
fi

if [ "$skip_determinism" = "1" ] || [ "${CHENG_BOOTSTRAP_SKIP_DETERMINISM:-}" = "1" ]; then
  if [ "${CHENG_BOOTSTRAP_UPDATE_SEED:-}" = "1" ]; then
    cp "$stage1_c" src/stage1/stage1_runner.seed.c
    echo "== Update seed: src/stage1/stage1_runner.seed.c (skip determinism) =="
  fi
  echo "== Skip determinism check (CHENG_BOOTSTRAP_SKIP_DETERMINISM=1) =="
  echo "WARNING: determinism check skipped; avoid in CI" 1>&2
  exit 0
fi

if [ "$did_regen_stage1" = "0" ] && [ "$force_determinism" != "1" ]; then
  echo "== Determinism check skipped (artifacts up-to-date) =="
  echo "  tip: set CHENG_BOOTSTRAP_FORCE_DETERMINISM=1 to force it"
  exit 0
fi

det_inputs_file="chengcache/determinism.inputs"
mkdir -p chengcache
det_cache_enabled="${CHENG_BOOTSTRAP_DET_CACHE:-1}"
det_key_file="chengcache/determinism.inputs.key"
determinism_inputs_key > "$det_key_file"
det_key_hash="$(hash256 "$det_key_file")"
if [ "$det_cache_enabled" = "1" ] && [ "${bootstrap_cache:-0}" = "0" ] && [ -z "${CHENG_CACHE_DIR:-}" ]; then
  det_cache_dir="/tmp/cheng_detcache.$det_key_hash"
  mkdir -p "$det_cache_dir"
  bootstrap_cache="1"
  export CHENG_CACHE_DIR="$det_cache_dir"
  echo "== Determinism cache dir: $det_cache_dir =="
fi
determinism_inputs > "$det_inputs_file"
det_inputs_hash="$(hash256 "$det_inputs_file")"
det_hash_cache_dir=""
det_hash_cache_file=""
det_cached_hash=""
if [ "$det_cache_enabled" = "1" ]; then
  det_hash_cache_dir="chengcache/determinism"
  mkdir -p "$det_hash_cache_dir"
  det_hash_cache_file="$det_hash_cache_dir/$det_inputs_hash.hash"
  if [ -f "$det_hash_cache_file" ]; then
    det_cached_hash="$(cat "$det_hash_cache_file")"
  fi
fi
echo "== Determinism inputs hash: $det_inputs_hash =="
echo "== Determinism check: stage1_runner regenerates stage1_runner.c =="
before_stage0="$(normalized_hash_cached "$stage1_c" "chengcache/stage1_runner.c.normhash")"
if [ "${CHENG_BOOTSTRAP_SKIP_DETERMINISM:-0}" = "1" ]; then
  echo "== Skip determinism (CHENG_BOOTSTRAP_SKIP_DETERMINISM=1) =="
  echo "bootstrap ok: $before_stage0"
  exit 0
fi
det_parallel="${CHENG_BOOTSTRAP_DET_PARALLEL:-1}"
det_cache_root="${CHENG_CACHE_DIR:-}"
det_out1="$tmp/stage1_runner.det1.c"
det_out2="$tmp/stage1_runner.det2.c"
rm -f "$det_out1" "$det_out2"
det_skip_run="0"
det_reuse_bootstrap="${CHENG_BOOTSTRAP_DET_REUSE_BOOTSTRAP:-1}"
if [ -n "$det_cached_hash" ] && [ "$before_stage0" = "$det_cached_hash" ]; then
  echo "== Determinism cache hit: skip runs =="
  after_stage1="$before_stage0"
  after_stage1_2="$before_stage0"
  det_skip_run="1"
elif [ -n "$det_cached_hash" ]; then
  echo "== Determinism cache hit: single run =="
  det_cache_dir1=""
  if [ -n "$det_cache_root" ]; then
    det_cache_dir1="$det_cache_root/det1"
    mkdir -p "$det_cache_dir1"
  fi
  if [ -n "$det_cache_dir1" ]; then
    CHENG_CACHE_DIR="$det_cache_dir1" CHENG_STAGE1_USE_SEED_C="$seed_c" CHENG_CACHE="$bootstrap_cache" CHENG_MM="${CHENG_BOOTSTRAP_MM:-off}" \
      run_bg ./stage1_runner --mode:c --file:src/stage1/frontend_stage1.cheng --out:"$det_out1" >/dev/null
  else
    CHENG_STAGE1_USE_SEED_C="$seed_c" CHENG_CACHE="$bootstrap_cache" CHENG_MM="${CHENG_BOOTSTRAP_MM:-off}" \
      run_bg ./stage1_runner --mode:c --file:src/stage1/frontend_stage1.cheng --out:"$det_out1" >/dev/null
  fi
  after_stage1="$(normalized_hash "$det_out1")"
  after_stage1_2="$after_stage1"
  if [ "$after_stage1" != "$det_cached_hash" ]; then
    echo "== Determinism cache mismatch: rerun =="
    det_cache_dir2=""
    if [ -n "$det_cache_root" ]; then
      det_cache_dir2="$det_cache_root/det2"
      mkdir -p "$det_cache_dir2"
    fi
    if [ -n "$det_cache_dir2" ]; then
      CHENG_CACHE_DIR="$det_cache_dir2" CHENG_STAGE1_USE_SEED_C="$seed_c" CHENG_CACHE="$bootstrap_cache" CHENG_MM="${CHENG_BOOTSTRAP_MM:-off}" \
        run_bg ./stage1_runner --mode:c --file:src/stage1/frontend_stage1.cheng --out:"$det_out2" >/dev/null
    else
      CHENG_STAGE1_USE_SEED_C="$seed_c" CHENG_CACHE="$bootstrap_cache" CHENG_MM="${CHENG_BOOTSTRAP_MM:-off}" \
        run_bg ./stage1_runner --mode:c --file:src/stage1/frontend_stage1.cheng --out:"$det_out2" >/dev/null
    fi
    after_stage1_2="$(normalized_hash "$det_out2")"
  fi
else
  if [ "$det_reuse_bootstrap" = "1" ]; then
    echo "== Determinism runs: reuse bootstrap output + single stage1 run =="
    det_cache_dir1=""
    if [ -n "$det_cache_root" ]; then
      det_cache_dir1="$det_cache_root/det1"
      mkdir -p "$det_cache_dir1"
    fi
    if [ -n "$det_cache_dir1" ]; then
      CHENG_CACHE_DIR="$det_cache_dir1" CHENG_STAGE1_USE_SEED_C="$seed_c" CHENG_CACHE="$bootstrap_cache" CHENG_MM="${CHENG_BOOTSTRAP_MM:-off}" \
        run_bg ./stage1_runner --mode:c --file:src/stage1/frontend_stage1.cheng --out:"$det_out1" >/dev/null
    else
      CHENG_STAGE1_USE_SEED_C="$seed_c" CHENG_CACHE="$bootstrap_cache" CHENG_MM="${CHENG_BOOTSTRAP_MM:-off}" \
        run_bg ./stage1_runner --mode:c --file:src/stage1/frontend_stage1.cheng --out:"$det_out1" >/dev/null
    fi
    after_stage1="$(normalized_hash "$det_out1")"
    after_stage1_2="$after_stage1"
    if [ "$after_stage1" != "$before_stage0" ]; then
      echo "== Determinism mismatch: rerun =="
      det_cache_dir2=""
      if [ -n "$det_cache_root" ]; then
        det_cache_dir2="$det_cache_root/det2"
        mkdir -p "$det_cache_dir2"
      fi
      if [ -n "$det_cache_dir2" ]; then
        CHENG_CACHE_DIR="$det_cache_dir2" CHENG_STAGE1_USE_SEED_C="$seed_c" CHENG_CACHE="$bootstrap_cache" CHENG_MM="${CHENG_BOOTSTRAP_MM:-off}" \
          run_bg ./stage1_runner --mode:c --file:src/stage1/frontend_stage1.cheng --out:"$det_out2" >/dev/null
      else
        CHENG_STAGE1_USE_SEED_C="$seed_c" CHENG_CACHE="$bootstrap_cache" CHENG_MM="${CHENG_BOOTSTRAP_MM:-off}" \
          run_bg ./stage1_runner --mode:c --file:src/stage1/frontend_stage1.cheng --out:"$det_out2" >/dev/null
      fi
      after_stage1_2="$(normalized_hash "$det_out2")"
    fi
  elif [ "$det_parallel" = "1" ]; then
    echo "== Determinism runs: parallel =="
    det_script="$(cat <<'EOF'
set -eu
out1="$DET_OUT1"
out2="$DET_OUT2"
cache_root="${DET_CACHE_ROOT:-}"
cache_dir1=""
cache_dir2=""
if [ -n "$cache_root" ]; then
  cache_dir1="$cache_root/det1"
  cache_dir2="$cache_root/det2"
  mkdir -p "$cache_dir1" "$cache_dir2"
fi
if [ -n "$cache_dir1" ]; then
  CHENG_CACHE_DIR="$cache_dir1" ./stage1_runner --mode:c --file:src/stage1/frontend_stage1.cheng --out:"$out1" >/dev/null &
else
  ./stage1_runner --mode:c --file:src/stage1/frontend_stage1.cheng --out:"$out1" >/dev/null &
fi
pid1=$!
if [ -n "$cache_dir2" ]; then
  CHENG_CACHE_DIR="$cache_dir2" ./stage1_runner --mode:c --file:src/stage1/frontend_stage1.cheng --out:"$out2" >/dev/null &
else
  ./stage1_runner --mode:c --file:src/stage1/frontend_stage1.cheng --out:"$out2" >/dev/null &
fi
pid2=$!
wait "$pid1"
wait "$pid2"
EOF
)"
    DET_OUT1="$det_out1" DET_OUT2="$det_out2" DET_CACHE_ROOT="$det_cache_root" \
    CHENG_STAGE1_USE_SEED_C="$seed_c" CHENG_CACHE="$bootstrap_cache" CHENG_MM="${CHENG_BOOTSTRAP_MM:-off}" \
      run_bg sh -c "$det_script"
  after_stage1="$(normalized_hash "$det_out1")"
  if cmp -s "$det_out1" "$det_out2"; then
    after_stage1_2="$after_stage1"
  else
    after_stage1_2="$(normalized_hash "$det_out2")"
  fi
  else
    echo "== Determinism runs: sequential =="
    CHENG_STAGE1_USE_SEED_C="$seed_c" CHENG_CACHE="$bootstrap_cache" CHENG_MM="${CHENG_BOOTSTRAP_MM:-off}" \
      run_bg ./stage1_runner --mode:c --file:src/stage1/frontend_stage1.cheng --out:"$det_out1" >/dev/null
    CHENG_STAGE1_USE_SEED_C="$seed_c" CHENG_CACHE="$bootstrap_cache" CHENG_MM="${CHENG_BOOTSTRAP_MM:-off}" \
      run_bg ./stage1_runner --mode:c --file:src/stage1/frontend_stage1.cheng --out:"$det_out2" >/dev/null
  after_stage1="$(normalized_hash "$det_out1")"
  if cmp -s "$det_out1" "$det_out2"; then
    after_stage1_2="$after_stage1"
  else
    after_stage1_2="$(normalized_hash "$det_out2")"
  fi
  fi
fi
if [ "$det_skip_run" = "0" ]; then
  cp "$det_out1" "$stage1_c"
  rm -f "$det_out1" "$det_out2"
fi
if [ -n "${after_stage1:-}" ]; then
  printf '%s\n%s\n' "$(hash256 "$stage1_c")" "$after_stage1" > "chengcache/stage1_runner.c.normhash"
fi

if [ "$after_stage1" != "$after_stage1_2" ]; then
  echo "[Error] stage1 output is not deterministic" 1>&2
  echo "  first : $after_stage1" 1>&2
  echo "  second: $after_stage1_2" 1>&2
  exit 1
fi
if [ "$det_cache_enabled" = "1" ] && [ "$det_hash_cache_file" != "" ]; then
  printf '%s\n' "$after_stage1" > "$det_hash_cache_file"
fi

if [ "$before_stage0" != "$after_stage1" ]; then
  echo "[Warn] stage0/stage1 C mismatch after normalization (stage1 deterministic)" 1>&2
  echo "  stage0: $before_stage0" 1>&2
  echo "  stage1: $after_stage1" 1>&2
fi

if [ "${CHENG_BOOTSTRAP_UPDATE_SEED:-}" = "1" ]; then
  cp "$stage1_c" src/stage1/stage1_runner.seed.c
  echo "== Update seed: src/stage1/stage1_runner.seed.c =="
fi

echo "bootstrap ok: $after_stage1"
