#!/usr/bin/env sh
set -eu
(set -o pipefail) 2>/dev/null && set -o pipefail

root="$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)"
cd "$root"

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
        exit ($? >> 8);
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

if [ "${CHENG_CLEAN_BACKEND_MVP_DRIVER_LOCAL:-0}" = "1" ] && [ "${CHENG_TOOLING_CLEANUP_DEPTH:-0}" = "0" ] && [ "${CHENG_SELF_OBJ_BOOTSTRAP_STAGE0:-}" = "" ]; then
  export CHENG_TOOLING_CLEANUP_DEPTH=1
  cleanup_backend_driver_on_exit() {
    status=$?
    set +e
    sh src/tooling/cleanup_backend_mvp_driver_local.sh
    exit "$status"
  }
  trap cleanup_backend_driver_on_exit EXIT
fi

host_os="$(uname -s 2>/dev/null || echo unknown)"
host_arch="$(uname -m 2>/dev/null || echo unknown)"

target=""
case "$host_os" in
  Darwin)
    case "$host_arch" in
      arm64)
        target="arm64-apple-darwin"
        ;;
    esac
    ;;
  Linux)
    case "$host_arch" in
      aarch64|arm64)
        target="aarch64-unknown-linux-gnu"
        ;;
    esac
    ;;
esac

if [ "$target" = "" ]; then
  echo "verify_backend_selfhost_bootstrap_self_obj skip: host=$host_os/$host_arch" 1>&2
  exit 2
fi

linker_mode="${CHENG_SELF_OBJ_BOOTSTRAP_LINKER:-}"
if [ "$linker_mode" = "" ]; then
  if [ "$host_os" = "Darwin" ]; then
    linker_mode="self"
  else
    linker_mode="cc"
  fi
fi

if [ "$linker_mode" = "self" ] && [ "$host_os" = "Darwin" ]; then
  if ! command -v codesign >/dev/null 2>&1; then
    echo "verify_backend_selfhost_bootstrap_self_obj skip: missing codesign" 1>&2
    exit 2
  fi
fi
if [ "$linker_mode" != "self" ] && ! command -v cc >/dev/null 2>&1; then
  echo "verify_backend_selfhost_bootstrap_self_obj skip: missing cc" 1>&2
  exit 2
fi

# Runtime selection:
# - default Darwin => pure Cheng runtime object (no `system_helpers.c` compilation)
# - default Linux  => keep `system_helpers.c` for now
runtime_mode="${CHENG_SELF_OBJ_BOOTSTRAP_RUNTIME:-}"
if [ "$runtime_mode" = "" ]; then
  if [ "$host_os" = "Darwin" ]; then
    runtime_mode="cheng"
  else
    runtime_mode="c"
  fi
fi
runtime_c="src/runtime/native/system_helpers.c"
runtime_cheng_src="src/std/system_helpers_backend.cheng"
runtime_obj=""

extra_ldflags=""
if [ "$host_os" = "Linux" ]; then
  extra_ldflags="-lm"
fi

out_dir="artifacts/backend_selfhost_self_obj"
mkdir -p "$out_dir"
mkdir -p chengcache
runtime_obj="$out_dir/system_helpers.backend.cheng.o"
build_timeout="${CHENG_SELF_OBJ_BOOTSTRAP_TIMEOUT:-60}"

stage0_env="${CHENG_SELF_OBJ_BOOTSTRAP_STAGE0:-}"
if [ "$stage0_env" != "" ]; then
  stage0="$stage0_env"
  if [ ! -x "$stage0" ]; then
    echo "[verify_backend_selfhost_bootstrap_self_obj] missing stage0 driver (CHENG_SELF_OBJ_BOOTSTRAP_STAGE0): $stage0" 1>&2
    exit 1
  fi
else
  stage0="./backend_mvp_driver"
  stage0_name="$(basename "$stage0")"
  if [ ! -x "$stage0" ]; then
    echo "== backend.selfhost_self_obj.build_stage0_driver ($stage0_name) =="
    bash src/tooling/build_backend_driver.sh --name:"$stage0_name" >/dev/null
  fi
  if [ ! -x "$stage0" ]; then
    echo "[verify_backend_selfhost_bootstrap_self_obj] missing stage0 driver: $stage0" 1>&2
    exit 1
  fi
fi

mm="${CHENG_SELF_OBJ_BOOTSTRAP_MM:-${CHENG_MM:-orc}}"
cache="${CHENG_SELF_OBJ_BOOTSTRAP_CACHE:-0}"
reuse="${CHENG_SELF_OBJ_BOOTSTRAP_REUSE:-1}"

is_rebuild_required() {
  out="$1"
  shift
  if [ ! -e "$out" ]; then
    return 0
  fi
  for dep in "$@"; do
    if [ "$dep" = "" ]; then
      continue
    fi
    if [ ! -e "$dep" ]; then
      continue
    fi
    if [ "$dep" -nt "$out" ]; then
      return 0
    fi
  done
  return 1
}

backend_sources_newer_than() {
  out="$1"
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

build_runtime_obj() {
  rt_compiler="$1"
  rt_out_obj="$2"
  rt_log="$out_dir/runtime.build.txt"
  tmp_obj="$rt_out_obj.tmp.$$"

  if [ "$runtime_mode" != "cheng" ]; then
    return 0
  fi
  if [ ! -f "$runtime_cheng_src" ]; then
    echo "[verify_backend_selfhost_bootstrap_self_obj] missing runtime source: $runtime_cheng_src" 1>&2
    exit 1
  fi
  if [ -f "$rt_out_obj" ] && [ "$runtime_cheng_src" -ot "$rt_out_obj" ]; then
    return 0
  fi

  set +e
  run_with_timeout "$build_timeout" env \
    CHENG_MM="$mm" \
    CHENG_CACHE="$cache" \
    CHENG_BACKEND_VALIDATE=1 \
    CHENG_BACKEND_ALLOW_NO_MAIN=1 \
    CHENG_BACKEND_WHOLE_PROGRAM=1 \
    CHENG_BACKEND_EMIT=obj \
    CHENG_BACKEND_TARGET="$target" \
    CHENG_BACKEND_FRONTEND=mvp \
    CHENG_BACKEND_INPUT="$runtime_cheng_src" \
    CHENG_BACKEND_OUTPUT="$tmp_obj" \
    "$rt_compiler" >"$rt_log" 2>&1
  status="$?"
  set -e
  if [ "$status" -ne 0 ]; then
    if [ "$status" -eq 124 ]; then
      echo "[verify_backend_selfhost_bootstrap_self_obj] runtime build timed out after ${build_timeout}s" >&2
    fi
    echo "[verify_backend_selfhost_bootstrap_self_obj] runtime build failed (status=$status)" >&2
    tail -n 200 "$rt_log" >&2 || true
    exit 1
  fi
  if [ ! -s "$tmp_obj" ]; then
    echo "[verify_backend_selfhost_bootstrap_self_obj] missing runtime obj output: $tmp_obj" 1>&2
    exit 1
  fi
  mv "$tmp_obj" "$rt_out_obj"
}

build_obj() {
  stage="$1"
  compiler="$2"
  input="$3"
  out_obj="$4"
  build_log="$out_dir/${stage}.build.txt"
  tmp_obj="$out_obj.tmp.$$"

  set +e
  run_with_timeout "$build_timeout" env \
    CHENG_MM="$mm" \
    CHENG_CACHE="$cache" \
    CHENG_BACKEND_VALIDATE=1 \
    CHENG_BACKEND_EMIT=obj \
    CHENG_BACKEND_TARGET="$target" \
    CHENG_BACKEND_FRONTEND=stage1 \
    CHENG_BACKEND_INPUT="$input" \
    CHENG_BACKEND_OUTPUT="$tmp_obj" \
    "$compiler" >"$build_log" 2>&1
  status="$?"
  set -e
  if [ "$status" -ne 0 ]; then
    if [ "$status" -eq 124 ]; then
      echo "[verify_backend_selfhost_bootstrap_self_obj] compiler timed out after ${build_timeout}s (stage=$stage)" >&2
    fi
    echo "[verify_backend_selfhost_bootstrap_self_obj] compiler failed (stage=$stage, status=$status)" >&2
    tail -n 200 "$build_log" >&2 || true
    exit 1
  fi

  if [ ! -s "$tmp_obj" ]; then
    echo "[verify_backend_selfhost_bootstrap_self_obj] missing obj output: $tmp_obj" 1>&2
    exit 1
  fi
  mv "$tmp_obj" "$out_obj"
}

build_exe_self() {
  stage="$1"
  compiler="$2"
  input="$3"
  tmp_exe="$4"
  out_exe="$5"
  build_log="$out_dir/${stage}.build.txt"

  exe_obj="$out_exe.o"
  tmp_exe_obj="$tmp_exe.o"

  if [ "$runtime_mode" != "cheng" ]; then
    echo "[verify_backend_selfhost_bootstrap_self_obj] self linker requires runtime_mode=cheng" 1>&2
    exit 1
  fi

  build_runtime_obj "$stage0" "$runtime_obj"

  set +e
  run_with_timeout "$build_timeout" env \
    CHENG_MM="$mm" \
    CHENG_CACHE="$cache" \
    CHENG_BACKEND_VALIDATE=1 \
    CHENG_BACKEND_LINKER=self \
    CHENG_BACKEND_NO_RUNTIME_C=1 \
    CHENG_BACKEND_RUNTIME_OBJ="$runtime_obj" \
    CHENG_BACKEND_EMIT=exe \
    CHENG_BACKEND_TARGET="$target" \
    CHENG_BACKEND_FRONTEND=stage1 \
    CHENG_BACKEND_INPUT="$input" \
    CHENG_BACKEND_OUTPUT="$tmp_exe" \
    "$compiler" >"$build_log" 2>&1
  exit_code="$?"
  set -e
  if [ "$exit_code" -ne 0 ]; then
    if [ "$exit_code" -eq 124 ]; then
      echo "[verify_backend_selfhost_bootstrap_self_obj] compiler timed out after ${build_timeout}s (stage=$stage)" >&2
    fi
    echo "[verify_backend_selfhost_bootstrap_self_obj] compiler failed (stage=$stage, status=$exit_code)" >&2
    tail -n 200 "$build_log" >&2 || true
    exit 1
  fi
  if [ ! -x "$tmp_exe" ]; then
    echo "[verify_backend_selfhost_bootstrap_self_obj] missing exe output: $tmp_exe" 1>&2
    exit 1
  fi
  if [ ! -s "$tmp_exe_obj" ]; then
    echo "[verify_backend_selfhost_bootstrap_self_obj] missing obj output: $tmp_exe_obj" 1>&2
    exit 1
  fi

  mv "$tmp_exe" "$out_exe"
  mv "$tmp_exe_obj" "$exe_obj"
}

link_exe() {
  stage="$1"
  in_obj="$2"
  out_exe="$3"
  log="$out_dir/${stage}.link.txt"

  set +e
  if [ "$runtime_mode" = "cheng" ]; then
    build_runtime_obj "$stage0" "$runtime_obj"
    cc "$in_obj" "$runtime_obj" -o "$out_exe" $extra_ldflags >"$log" 2>&1
  else
    cc "$in_obj" "$runtime_c" -o "$out_exe" $extra_ldflags >"$log" 2>&1
  fi
  status="$?"
  set -e
  if [ "$status" -ne 0 ]; then
    echo "[verify_backend_selfhost_bootstrap_self_obj] link failed (stage=$stage, status=$status)" >&2
    tail -n 200 "$log" >&2 || true
    exit 1
  fi
  if [ ! -x "$out_exe" ]; then
    echo "[verify_backend_selfhost_bootstrap_self_obj] missing exe output: $out_exe" 1>&2
    exit 1
  fi
}

run_smoke() {
  stage="$1"
  compiler="$2"
  fixture="$3"
  expect="$4"
  out_base="$5"

  if [ "$linker_mode" = "self" ]; then
    base="$(basename "$out_base")"
    tmp="$(printf '%s' "$base" | tr '.' '_')"
    tmp_exe="$out_dir/${tmp}_$$"
    build_exe_self "$stage.smoke" "$compiler" "$fixture" "$tmp_exe" "$out_base"
  else
    build_obj "$stage.smoke" "$compiler" "$fixture" "$out_base.o"
    link_exe "$stage.smoke" "$out_base.o" "$out_base"
  fi
  "$out_base" >"$out_dir/${stage}.smoke.run.txt"
  grep -Fq "$expect" "$out_dir/${stage}.smoke.run.txt"
}

obj_compare_note=""

compare_obj_fixedpoint() {
  a="$1"
  b="$2"
  if cmp -s "$a" "$b"; then
    return 0
  fi
  if ! command -v nm >/dev/null 2>&1; then
    return 1
  fi
  n1="$out_dir/.objcmp.$$.a.txt"
  n2="$out_dir/.objcmp.$$.b.txt"
  set +e
  nm -j "$a" | sed -E 's/__cheng_mod_[0-9a-f]+//g' | sort >"$n1"
  s1="$?"
  nm -j "$b" | sed -E 's/__cheng_mod_[0-9a-f]+//g' | sort >"$n2"
  s2="$?"
  if [ "$s1" -eq 0 ] && [ "$s2" -eq 0 ] && cmp -s "$n1" "$n2"; then
    set -e
    rm -f "$n1" "$n2"
    obj_compare_note="normalized-symbols"
    return 0
  fi
  set -e
  rm -f "$n1" "$n2"
  return 1
}

stage1_exe="$out_dir/backend_mvp_driver.stage1"
stage2_exe="$out_dir/backend_mvp_driver.stage2"
stage1_obj="$stage1_exe.o"
stage2_obj="$stage2_exe.o"
stage1_tmp="$out_dir/backend_mvp_driver_stage1_$$"
stage2_tmp="$out_dir/backend_mvp_driver_stage2_$$"

stage1_rebuild="1"
stage2_rebuild="1"
if [ "$reuse" = "1" ] && [ -x "$stage1_exe" ] && [ -s "$stage1_obj" ]; then
  if ! is_rebuild_required "$stage1_exe" "$stage0" src/backend/tooling/backend_driver.cheng && \
     ! backend_sources_newer_than "$stage1_exe"; then
    stage1_rebuild="0"
  fi
fi
if [ "$reuse" = "1" ] && [ -x "$stage2_exe" ] && [ -s "$stage2_obj" ] && [ "$stage1_rebuild" = "0" ]; then
  if ! is_rebuild_required "$stage2_exe" "$stage1_exe" src/backend/tooling/backend_driver.cheng && \
     ! backend_sources_newer_than "$stage2_exe"; then
    stage2_rebuild="0"
  fi
fi

if [ "$stage1_rebuild" = "1" ]; then
  echo "== backend.selfhost_self_obj.stage1 =="
  if [ "$linker_mode" = "self" ]; then
    build_exe_self "stage1" "$stage0" "src/backend/tooling/backend_driver.cheng" "$stage1_tmp" "$stage1_exe"
  else
    build_runtime_obj "$stage0" "$runtime_obj"
    build_obj "stage1" "$stage0" "src/backend/tooling/backend_driver.cheng" "$stage1_obj"
    link_exe "stage1" "$stage1_obj" "$stage1_exe"
  fi
else
  echo "== backend.selfhost_self_obj.stage1 (reuse) =="
fi

if [ "$stage2_rebuild" = "1" ]; then
  echo "== backend.selfhost_self_obj.stage2 =="
  if [ "$linker_mode" = "self" ]; then
    build_exe_self "stage2" "$stage1_exe" "src/backend/tooling/backend_driver.cheng" "$stage2_tmp" "$stage2_exe"
  else
    build_obj "stage2" "$stage1_exe" "src/backend/tooling/backend_driver.cheng" "$stage2_obj"
    link_exe "stage2" "$stage2_obj" "$stage2_exe"
  fi
else
  echo "== backend.selfhost_self_obj.stage2 (reuse) =="
fi

compare_obj_fixedpoint "$stage1_obj" "$stage2_obj" || {
  echo "[verify_backend_selfhost_bootstrap_self_obj] compiler obj mismatch: $stage1_obj vs $stage2_obj" >&2
  exit 1
}

fixture="tests/cheng/backend/fixtures/hello_puts.cheng"
hello1="$out_dir/hello_puts.stage1"
hello2="$out_dir/hello_puts.stage2"

echo "== backend.selfhost_self_obj.smoke.stage1 =="
run_smoke "stage1" "$stage1_exe" "$fixture" "hello from cheng backend" "$hello1"
echo "== backend.selfhost_self_obj.smoke.stage2 =="
run_smoke "stage2" "$stage2_exe" "$fixture" "hello from cheng backend" "$hello2"

compare_obj_fixedpoint "$hello1.o" "$hello2.o" || {
  echo "[verify_backend_selfhost_bootstrap_self_obj] smoke obj mismatch: $hello1.o vs $hello2.o" >&2
  exit 1
}

if [ "$obj_compare_note" = "normalized-symbols" ]; then
  echo "[verify_backend_selfhost_bootstrap_self_obj] note: fixed-point matched after normalizing __cheng_mod_ symbol suffixes"
fi

echo "verify_backend_selfhost_bootstrap_self_obj ok"
