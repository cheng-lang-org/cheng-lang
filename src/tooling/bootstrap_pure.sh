#!/usr/bin/env sh
set -eu

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
  src/tooling/bootstrap_pure.sh [--fullspec] [--seed:<stage1_seed.c>]

Notes:
  - Pure Cheng bootstrap: no Stage0 dependency (bin/cheng).
  - Seed selection:
    - If `./stage1_runner` exists: use it directly as the seed compiler.
    - If `./stage1_runner` is missing: use `--seed:` (.c) to build a seed `stage1_runner` first (default below).
  - Dependencies:
    - ./stage1_runner (optional: used directly if present)
    - src/stage1/stage1_runner.seed.c (optional: used to build seed when stage1_runner is missing; override with --seed)
    - System C compiler

Options:
  --fullspec  Also compile and run the fullspec regression sample (should output `fullspec ok`)
  --seed:...  Stage1 seed `.c` path (used when ./stage1_runner is missing)
  --skip-determinism  Skip determinism check (same as CHENG_BOOTSTRAP_SKIP_DETERMINISM=1)
Env:
  CHENG_BOOTSTRAP_FAST=1        Use fast CFLAGS for bootstrap builds (default: -O0)
  CHENG_BOOTSTRAP_FAST_CFLAGS=  Override fast CFLAGS (default: -O0)
  CHENG_BOOTSTRAP_DET_REUSE_BOOTSTRAP=1  Reuse bootstrap output for determinism (default: 1)
EOF
}

fullspec="0"
seed_c="src/stage1/stage1_runner.seed.c"
skip_determinism="0"
force_seed="${CHENG_BOOTSTRAP_FORCE_SEED:-0}"
while [ "${1:-}" != "" ]; do
  case "$1" in
    --help|-h)
      usage
      exit 0
      ;;
    --fullspec)
      fullspec="1"
      ;;
    --seed:*)
      seed_c="${1#--seed:}"
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
bootstrap_mm="${CHENG_BOOTSTRAP_MM:-off}"
skip_ownership="${CHENG_STAGE1_SKIP_OWNERSHIP:-0}"
skip_sem="${CHENG_STAGE1_SKIP_SEM:-0}"
bootstrap_cache="${CHENG_BOOTSTRAP_CACHE:-0}"
trace_bootstrap="${CHENG_BOOTSTRAP_TRACE:-0}"
stage1_cflags="${CHENG_STAGE1_CFLAGS:-${CFLAGS:-}}"
if [ "${CHENG_BOOTSTRAP_FAST:-0}" = "1" ]; then
  stage1_cflags="${CHENG_BOOTSTRAP_FAST_CFLAGS:--O0}"
fi
runtime_inc="${CHENG_RT_INC:-${CHENG_RUNTIME_INC:-$root/runtime/include}}"
stage1_includes="-I$runtime_inc -I$root/src/runtime/native"
force_determinism="${CHENG_BOOTSTRAP_FORCE_DETERMINISM:-0}"

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
  echo "stage1_seed=$(hash256 "$seed_c")"
  echo "compiler_version=${CHENG_COMPILER_VERSION:-}"
  echo "stage1_version=${CHENG_STAGE1_VERSION:-}"
  echo "target=${CHENG_TARGET:-}"
  echo "stage1_cflags=$stage1_cflags"
  echo "cc=${CC:-cc}"
  echo "mm=$bootstrap_mm"
  echo "mm_strict=${CHENG_MM_STRICT:-}"
  echo "cache=$bootstrap_cache"
  echo "cache_dir=${CHENG_CACHE_DIR:-}"
  echo "ide_root=${CHENG_IDE_ROOT:-}"
  echo "gui_root=${CHENG_GUI_ROOT:-}"
  echo "pkg_roots=${CHENG_PKG_ROOTS:-}"
  echo "pkg_lock=${CHENG_PKG_LOCK:-}"
  echo "skip_sem=$skip_sem"
  echo "skip_ownership=$skip_ownership"
}

determinism_inputs_key() {
  echo "stage1_seed=$(hash256 "$seed_c")"
  echo "compiler_version=${CHENG_COMPILER_VERSION:-}"
  echo "stage1_version=${CHENG_STAGE1_VERSION:-}"
  echo "target=${CHENG_TARGET:-}"
  echo "stage1_cflags=$stage1_cflags"
  echo "cc=${CC:-cc}"
  echo "mm=$bootstrap_mm"
  echo "mm_strict=${CHENG_MM_STRICT:-}"
  echo "cache=determinism"
  echo "cache_dir=determinism"
  echo "ide_root=${CHENG_IDE_ROOT:-}"
  echo "gui_root=${CHENG_GUI_ROOT:-}"
  echo "pkg_roots=${CHENG_PKG_ROOTS:-}"
  echo "pkg_lock=${CHENG_PKG_LOCK:-}"
  echo "skip_sem=$skip_sem"
  echo "skip_ownership=$skip_ownership"
}

require_exec() {
  if [ ! -x "$1" ]; then
    echo "[Error] missing executable: $1" 1>&2
    exit 2
  fi
}

use_seed="0"
if [ "$force_seed" = "1" ]; then
  use_seed="1"
elif [ ! -x ./stage1_runner ]; then
  use_seed="1"
fi
if [ "$use_seed" = "1" ]; then
  if [ ! -f "$seed_c" ]; then
    echo "[Error] missing seed compiler: ./stage1_runner" 1>&2
    echo "[Error] missing seed C: $seed_c" 1>&2
    echo "  tip: run src/tooling/bootstrap.sh once, or provide --seed:<path>" 1>&2
    exit 2
  fi

  echo "== Build seed stage1_runner from C =="
  cc="${CC:-cc}"
  mkdir -p chengcache
  "$cc" $stage1_cflags $stage1_includes -c "$seed_c" -o chengcache/stage1_runner.o
  "$cc" $stage1_cflags $stage1_includes -c src/runtime/native/system_helpers.c -o chengcache/stage1_runner.system_helpers.o
  "$cc" $stage1_cflags $stage1_includes chengcache/stage1_runner.o chengcache/stage1_runner.system_helpers.o -o stage1_runner
fi
require_exec ./stage1_runner
mkdir -p chengcache
cc="${CC:-cc}"

bootstrap_flags_file="chengcache/pure_bootstrap.bootstrap.flags"
bootstrap_flags="$(cat <<EOF
cc=$cc
stage1_cflags=$stage1_cflags
mm=$bootstrap_mm
skip_sem=$skip_sem
skip_ownership=$skip_ownership
cache=$bootstrap_cache
runtime_inc=$runtime_inc
EOF
)"
bootstrap_inputs_mtime="$(max_mtime src/stage1 src/std "$runtime_inc")"
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
if [ "$bootstrap_flags_ok" = "1" ] && [ -f "$bootstrap_c" ] && [ -x ./stage1_bootstrap_runner ]; then
  if [ "$bootstrap_outputs_mtime" -ge "$bootstrap_inputs_mtime" ]; then
    bootstrap_up_to_date="1"
  fi
fi

if [ "$bootstrap_up_to_date" = "1" ]; then
  echo "== Bootstrap runner up-to-date (skip codegen/build) =="
  if [ "$bootstrap_flags_missing" = "1" ]; then
    printf '%s\n' "$bootstrap_flags" > "$bootstrap_flags_file"
  fi
else
  echo "== Pure Cheng: Stage1(seed) -> bootstrap runner C =="
  if [ "$trace_bootstrap" = "1" ]; then
    CHENG_CACHE="$bootstrap_cache" CHENG_MM="$bootstrap_mm" \
    CHENG_STAGE1_TRACE=1 CHENG_STAGE1_TRACE_FLUSH=1 CHENG_STAGE1_TRACE_EMIT=1 \
    CHENG_STAGE1_SKIP_OWNERSHIP="$skip_ownership" CHENG_STAGE1_SKIP_SEM="$skip_sem" \
      ./stage1_runner --mode:c --file:src/stage1/frontend_bootstrap.cheng --out:"$bootstrap_c"
  else
    CHENG_CACHE="$bootstrap_cache" CHENG_MM="$bootstrap_mm" \
    CHENG_STAGE1_SKIP_OWNERSHIP="$skip_ownership" CHENG_STAGE1_SKIP_SEM="$skip_sem" \
      ./stage1_runner --mode:c --file:src/stage1/frontend_bootstrap.cheng --out:"$bootstrap_c" >/dev/null
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

if [ ! -x ./stage1_bootstrap_runner ]; then
  echo "[Error] missing executable: ./stage1_bootstrap_runner" 1>&2
  exit 2
fi

stage1_flags_file="chengcache/pure_bootstrap.stage1.flags"
stage1_flags="$(cat <<EOF
cc=$cc
stage1_cflags=$stage1_cflags
mm=$bootstrap_mm
skip_sem=$skip_sem
skip_ownership=$skip_ownership
cache=$bootstrap_cache
runtime_inc=$runtime_inc
fullspec=$fullspec
EOF
)"
stage1_inputs_mtime="$(max_mtime ./stage1_bootstrap_runner src/stage1 src/std "$runtime_inc")"
fullspec_inputs_mtime="$stage1_inputs_mtime"
fullspec_extra_mtime="$(max_mtime examples/stage1_codegen_fullspec.cheng examples/foo)"
if [ "$fullspec_extra_mtime" -gt "$fullspec_inputs_mtime" ]; then
  fullspec_inputs_mtime="$fullspec_extra_mtime"
fi
stage1_outputs_mtime="$(max_mtime ./stage1_runner)"
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
if [ "$stage1_up_to_date" = "1" ]; then
  echo "== Stage1 artifacts up-to-date (skip runner/build) =="
  if [ "$stage1_flags_missing" = "1" ]; then
    printf '%s\n' "$stage1_flags" > "$stage1_flags_file"
  fi
else
  echo "== Run bootstrap runner (generate stage1 runner) =="
  rm -f "$stage1_c" "$fullspec_c"
  if [ "$trace_bootstrap" = "1" ]; then
    CHENG_CACHE="$bootstrap_cache" CHENG_MM="$bootstrap_mm" \
    CHENG_STAGE1_TRACE=1 CHENG_STAGE1_TRACE_FLUSH=1 CHENG_STAGE1_TRACE_EMIT=1 \
    CHENG_STAGE1_SKIP_OWNERSHIP="$skip_ownership" CHENG_STAGE1_SKIP_SEM="$skip_sem" \
    CHENG_STAGE1_FULLSPEC="$fullspec" run_bg ./stage1_bootstrap_runner
  else
    CHENG_CACHE="$bootstrap_cache" CHENG_MM="$bootstrap_mm" \
    CHENG_STAGE1_SKIP_OWNERSHIP="$skip_ownership" CHENG_STAGE1_SKIP_SEM="$skip_sem" \
  CHENG_STAGE1_FULLSPEC="$fullspec" run_bg ./stage1_bootstrap_runner >/dev/null
  fi
  did_regen_stage1="1"
  # no post-processing (legacy python fix removed)
  if [ "$fullspec" = "1" ] && [ ! -f "$fullspec_c" ]; then
    echo "== Fullspec C missing after runner; retry once =="
    CHENG_CACHE="$bootstrap_cache" CHENG_MM="$bootstrap_mm" \
    CHENG_STAGE1_SKIP_OWNERSHIP="$skip_ownership" CHENG_STAGE1_SKIP_SEM="$skip_sem" \
    CHENG_STAGE1_FULLSPEC="$fullspec" run_bg ./stage1_bootstrap_runner
  fi
fi

if [ "$fullspec" = "1" ]; then
  if [ "$did_regen_stage1" = "1" ]; then
    echo "== (optional) Build & run fullspec =="
    if [ ! -f "$fullspec_c" ]; then
      echo "[Error] missing generated C file: $fullspec_c" 1>&2
      exit 2
    fi
    mkdir -p chengcache
    "$cc" $stage1_cflags $stage1_includes -c "$fullspec_c" -o chengcache/stage1_codegen_fullspec.o
    "$cc" $stage1_cflags $stage1_includes -c src/runtime/native/system_helpers.c -o chengcache/stage1_codegen_fullspec.system_helpers.o
    "$cc" $stage1_cflags $stage1_includes chengcache/stage1_codegen_fullspec.o chengcache/stage1_codegen_fullspec.system_helpers.o -o stage1_codegen_fullspec
  else
    echo "== (optional) Run cached fullspec =="
  fi
  ./stage1_codegen_fullspec
fi

if [ "$did_regen_stage1" = "1" ]; then
  echo "== Build stage1_runner =="
  if [ ! -f "$stage1_c" ]; then
    echo "[Error] missing generated C file: $stage1_c" 1>&2
    exit 2
  fi
  mkdir -p chengcache
  "$cc" $stage1_cflags $stage1_includes -c "$stage1_c" -o chengcache/stage1_runner.o
  "$cc" $stage1_cflags $stage1_includes -c src/runtime/native/system_helpers.c -o chengcache/stage1_runner.system_helpers.o
  "$cc" $stage1_cflags $stage1_includes chengcache/stage1_runner.o chengcache/stage1_runner.system_helpers.o -o stage1_runner
  printf '%s\n' "$stage1_flags" > "$stage1_flags_file"
fi

if [ "$skip_determinism" = "1" ] || [ "${CHENG_BOOTSTRAP_SKIP_DETERMINISM:-}" = "1" ]; then
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
echo "== Determinism inputs hash: $det_inputs_hash =="
echo "== Determinism check: stage1_runner regenerates stage1_runner.c =="
before="$(normalized_hash_cached "$stage1_c" "chengcache/stage1_runner.c.normhash")"
det_parallel="${CHENG_BOOTSTRAP_DET_PARALLEL:-1}"
det_cache_root="${CHENG_CACHE_DIR:-}"
det_out1="$tmp/stage1_runner.det1.c"
det_out2="$tmp/stage1_runner.det2.c"
rm -f "$det_out1" "$det_out2"
det_reuse_bootstrap="${CHENG_BOOTSTRAP_DET_REUSE_BOOTSTRAP:-1}"
if [ "$det_reuse_bootstrap" = "1" ]; then
  echo "== Determinism runs: reuse bootstrap output + single stage1 run =="
  CHENG_CACHE="$bootstrap_cache" CHENG_MM="$bootstrap_mm" \
    run_bg ./stage1_runner --mode:c --file:src/stage1/frontend_stage1.cheng --out:"$det_out1" >/dev/null
  after="$(normalized_hash "$det_out1")"
  after2="$after"
  if [ "$after" != "$before" ]; then
    echo "== Determinism mismatch: rerun =="
    CHENG_CACHE="$bootstrap_cache" CHENG_MM="$bootstrap_mm" \
      run_bg ./stage1_runner --mode:c --file:src/stage1/frontend_stage1.cheng --out:"$det_out2" >/dev/null
    if cmp -s "$det_out1" "$det_out2"; then
      after2="$after"
    else
      after2="$(normalized_hash "$det_out2")"
    fi
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
  CHENG_CACHE="$bootstrap_cache" CHENG_MM="$bootstrap_mm" \
    run_bg sh -c "$det_script"
  after="$(normalized_hash "$det_out1")"
  if cmp -s "$det_out1" "$det_out2"; then
    after2="$after"
  else
    after2="$(normalized_hash "$det_out2")"
  fi
else
  echo "== Determinism runs: sequential =="
  CHENG_CACHE="$bootstrap_cache" CHENG_MM="$bootstrap_mm" \
    run_bg ./stage1_runner --mode:c --file:src/stage1/frontend_stage1.cheng --out:"$det_out1" >/dev/null
  CHENG_CACHE="$bootstrap_cache" CHENG_MM="$bootstrap_mm" \
    run_bg ./stage1_runner --mode:c --file:src/stage1/frontend_stage1.cheng --out:"$det_out2" >/dev/null
  after="$(normalized_hash "$det_out1")"
  if cmp -s "$det_out1" "$det_out2"; then
    after2="$after"
  else
    after2="$(normalized_hash "$det_out2")"
  fi
fi
cp "$det_out1" "$stage1_c"
rm -f "$det_out1" "$det_out2"
if [ -n "${after:-}" ]; then
  printf '%s\n%s\n' "$(hash256 "$stage1_c")" "$after" > "chengcache/stage1_runner.c.normhash"
fi

if [ "$after" != "$after2" ]; then
  echo "[Error] stage1 output is not deterministic" 1>&2
  echo "  first : $after" 1>&2
  echo "  second: $after2" 1>&2
  exit 1
fi

if [ "$before" != "$after" ]; then
  echo "[Error] stage1 output mismatches baseline after normalization" 1>&2
  echo "  before: $before" 1>&2
  echo "  after : $after" 1>&2
  exit 1
fi

echo "pure bootstrap ok: $before"
