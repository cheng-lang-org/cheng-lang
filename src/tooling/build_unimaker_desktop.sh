#!/usr/bin/env sh
set -eu

usage() {
  cat <<'EOF_INNER'
Usage:
  src/tooling/build_unimaker_desktop.sh [--out:<path>] [--name:<prog>] [--compiler:auto|stage1] [--mm:<orc|off>] [--orc|--off]

Notes:
  - Build the Unimaker desktop demo (examples/unimaker_desktop/main.cheng).
  - If the example is missing, use CHENG_UNIMAKER_DESKTOP_ROOT/main.cheng.
  - Defaults to stage1_runner (use --compiler:auto to retry without stage0 fallback).
  - Env: CHENG_BUILD_VERBOSE=1 enables stage1 trace output.
  - Env: CHENG_BUILD_FAST=1 skips stage1 semantics/ownership (faster, less validation).
  - Env: CHENG_BUILD_MODULES=1 uses stage1 C modules for incremental/parallel builds (default).
  - Env: CHENG_BUILD_JOBS=N sets module/compile parallelism.
  - Env: CHENG_BUILD_TIMEOUT=60 sets per-step timeout (default 60s).
  - Env: CHENG_BUILD_WARMUP=1 enables auto warmup (3x timeout when unset).
  - Env: CHENG_BUILD_WARMUP_TIMEOUT=... allows a one-time longer C emit to seed cache.
  - Env: CHENG_BUILD_MODULE_TIMEOUT=60 sets per-module timeout (defaults to CHENG_BUILD_TIMEOUT).
  - Env: CHENG_STAGE1_TOKEN_CACHE=1 enables token cache (default on here).
  - Env: CHENG_DEPS_PREWARM=1 prewarms deps cache before stage1 (set 0 to skip).
  - Env: CHENG_DEPS_FRONTEND=... overrides deps frontend for chengc/prewarm.

Deps:
  - ./stage1_runner (stage1)
  - C compiler (macOS uses clang)
EOF_INNER
}

prog="unimaker_desktop"
out="./examples/unimaker_desktop/unimaker_desktop"
out_default="1"
compiler="stage1"
mm=""
while [ "${1:-}" != "" ]; do
  case "$1" in
    --help|-h)
      usage
      exit 0
      ;;
    --out:*)
      out="${1#--out:}"
      out_default="0"
      ;;
    --name:*)
      prog="${1#--name:}"
      ;;
    --compiler:*)
      compiler="${1#--compiler:}"
      ;;
    --orc)
      mm="orc"
      ;;
    --off)
      mm="off"
      ;;
    --mm:*)
      mm="${1#--mm:}"
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

ide_root="${CHENG_IDE_ROOT:-}"
gui_root="${CHENG_GUI_ROOT:-}"
if [ -z "$ide_root" ] && [ -d "$root/../cheng-ide" ]; then
  ide_root="$root/../cheng-ide"
fi
if [ -z "$gui_root" ] && [ -d "$root/../cheng-gui" ]; then
  gui_root="$root/../cheng-gui"
fi
if [ -z "$gui_root" ] && [ -n "$ide_root" ] && [ -d "$ide_root/gui" ]; then
  gui_root="$ide_root/gui"
fi
if [ -z "$ide_root" ] || [ ! -d "$ide_root" ]; then
  echo "[Error] missing IDE root (set CHENG_IDE_ROOT to /path/to/cheng-ide)" 1>&2
  exit 2
fi
if [ -z "$gui_root" ] || [ ! -d "$gui_root" ]; then
  echo "[Error] missing GUI root (set CHENG_GUI_ROOT to /path/to/cheng-gui)" 1>&2
  exit 2
fi
export CHENG_IDE_ROOT="$ide_root"
export CHENG_GUI_ROOT="$gui_root"
runtime_inc="${CHENG_RT_INC:-${CHENG_RUNTIME_INC:-$root/runtime/include}}"
pkg_roots="${CHENG_PKG_ROOTS:-}"
if [ -z "$pkg_roots" ] && [ -d "$HOME/.cheng-packages" ]; then
  pkg_roots="$HOME/.cheng-packages"
fi

build_timeout="${CHENG_BUILD_TIMEOUT:-60}"
if [ -z "${CHENG_BUILD_TIMEOUT:-}" ]; then
  export CHENG_BUILD_TIMEOUT="$build_timeout"
fi
use_cache="${CHENG_BUILD_USE_CACHE:-}"
case "$build_timeout" in
  ''|*[!0-9]*)
    ;;
  *)
    if [ -z "$use_cache" ] && [ "$build_timeout" -le 60 ] 2>/dev/null; then
      use_cache="1"
    fi
    ;;
esac

uname_s="$(uname -s)"
cflags=""
if [ -z "${CHENG_DEFINES:-}" ]; then
  case "$uname_s" in
    Darwin)
      export CHENG_DEFINES="macos,macosx"
      cflags="-Wno-incompatible-library-redeclaration -Wno-builtin-requires-header"
      ;;
    Linux)
      export CHENG_DEFINES="linux"
      ;;
    MINGW*|MSYS*|CYGWIN*|Windows_NT)
      export CHENG_DEFINES="windows,Windows"
      ;;
  esac
fi
cc="${CC:-cc}"

if [ -z "${CHENG_STAGE1_TOKEN_CACHE:-}" ]; then
  export CHENG_STAGE1_TOKEN_CACHE=1
fi

if [ -n "$mm" ]; then
  export CHENG_MM="$mm"
fi

entry="examples/unimaker_desktop/main.cheng"
unimaker_root=""
if [ ! -f "$root/$entry" ]; then
  unimaker_root="${CHENG_UNIMAKER_DESKTOP_ROOT:-}"
  if [ -z "$unimaker_root" ] && [ -f "$root/../unimaker_desktop/main.cheng" ]; then
    unimaker_root="$root/../unimaker_desktop"
  fi
  if [ -n "$unimaker_root" ] && [ -f "$unimaker_root/main.cheng" ]; then
    entry="$unimaker_root/main.cheng"
    if [ "$out_default" = "1" ]; then
      out="$unimaker_root/unimaker_desktop"
    fi
  fi
fi
if [ -n "$unimaker_root" ]; then
  case ":$pkg_roots:" in
    *":$unimaker_root:"*) ;;
    *)
      if [ -n "$pkg_roots" ]; then
        pkg_roots="$pkg_roots,$unimaker_root"
      else
        pkg_roots="$unimaker_root"
      fi
      ;;
  esac
fi
if [ -d "$HOME/RWAD-blockchain" ]; then
  case ":$pkg_roots:" in
    *":$HOME/RWAD-blockchain:"*) ;;
    *)
      if [ -n "$pkg_roots" ]; then
        pkg_roots="$pkg_roots,$HOME/RWAD-blockchain"
      else
        pkg_roots="$HOME/RWAD-blockchain"
      fi
      ;;
  esac
fi
if [ -d "$HOME/.cheng-packages/RWAD-blockchain" ]; then
  case ":$pkg_roots:" in
    *":$HOME/.cheng-packages/RWAD-blockchain:"*) ;;
    *)
      if [ -n "$pkg_roots" ]; then
        pkg_roots="$pkg_roots,$HOME/.cheng-packages/RWAD-blockchain"
      else
        pkg_roots="$HOME/.cheng-packages/RWAD-blockchain"
      fi
      ;;
  esac
fi
if [ -n "$pkg_roots" ]; then
  export CHENG_PKG_ROOTS="$pkg_roots"
fi

module_build="${CHENG_BUILD_MODULES:-1}"
modules_cache="chengcache/${prog}.modules"
modules_cache_ready="0"
if [ -d "$modules_cache" ]; then
  if find "$modules_cache" -maxdepth 1 -type f -name '*.key' -print -quit | grep -q .; then
    modules_cache_ready="1"
  fi
fi
if [ -z "${CHENG_DEPS_PREWARM:-}" ] && [ "$module_build" != "0" ]; then
  case "$build_timeout" in
    ''|*[!0-9]*)
      export CHENG_DEPS_PREWARM=1
      ;;
    *)
      if [ "$build_timeout" -le 60 ] 2>/dev/null; then
        export CHENG_DEPS_PREWARM=0
      else
        export CHENG_DEPS_PREWARM=1
      fi
      ;;
  esac
fi
deps_prewarm_done="0"

if [ "$module_build" != "0" ] && [ -z "${CHENG_C_MODULES_SINGLEPASS:-}" ]; then
  if [ "$modules_cache_ready" = "1" ]; then
    export CHENG_C_MODULES_SINGLEPASS=0
  else
    export CHENG_C_MODULES_SINGLEPASS=1
  fi
fi
if [ "$module_build" != "0" ] && [ -z "${CHENG_C_MODULE_ENTRY:-}" ]; then
  case "${CHENG_C_MODULES_SINGLEPASS:-1}" in
    0|false|no|off)
      export CHENG_C_MODULE_ENTRY=module
      ;;
  esac
fi
if [ "$module_build" != "0" ] && [ -z "${CHENG_C_AUTO_TYPE:-}" ]; then
  export CHENG_C_AUTO_TYPE=1
fi
if [ "$module_build" != "0" ] && [ -z "${CHENG_BUILD_WARMUP_TIMEOUT:-}" ]; then
  case "${CHENG_BUILD_WARMUP:-}" in
    1|true|yes)
      case "$build_timeout" in
        ''|*[!0-9]*)
          ;;
        *)
          export CHENG_BUILD_WARMUP_TIMEOUT="$((build_timeout * 3))"
          ;;
      esac
      ;;
  esac
fi

if [ -x "./stage1_runner" ] && [ -f "src/stage1/c_codegen.cheng" ]; then
  mm_mode="${CHENG_MM:-}"
  if [ -z "$mm_mode" ]; then
    mm_mode="orc"
  fi
  if [ "$mm_mode" != "off" ] && [ "src/stage1/c_codegen.cheng" -nt "./stage1_runner" ]; then
    echo "[Warn] CHENG_MM=$mm_mode but ./stage1_runner is older than src/stage1/c_codegen.cheng; consider running src/tooling/bootstrap.sh" 1>&2
  fi
fi

if [ -n "${CHENG_DEPS_PREWARM:-}" ] && [ "${CHENG_DEPS_PREWARM}" != "0" ] && [ "$module_build" = "0" ]; then
  src/tooling/prewarm_deps_parallel.sh "$entry"
  deps_prewarm_done="1"
fi

run_with_timeout() {
  seconds="$1"
  shift || true
  case "$seconds" in
    ''|*[!0-9]*)
      "$@"
      return $?
      ;;
  esac
  if [ "$seconds" -le 0 ] 2>/dev/null; then
    return 124
  fi
  if [ "${CHENG_TIMEOUT_PGROUP:-1}" = "0" ]; then
    if command -v timeout >/dev/null 2>&1; then
      timeout "$seconds" "$@"
      return $?
    fi
    if command -v gtimeout >/dev/null 2>&1; then
      gtimeout "$seconds" "$@"
      return $?
    fi
    perl -e 'alarm shift; exec @ARGV' "$seconds" "$@"
    return $?
  fi
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

is_timeout_status() {
  case "$1" in
    124|137|142|143) return 0 ;;
    *) return 1 ;;
  esac
}

deps_list="chengcache/${prog}.deps.list"
c_ok="chengcache/${prog}.c.ok"

cache_is_fresh() {
  cfile="$1"
  deps="$2"
  if [ -z "$use_cache" ]; then
    return 1
  fi
  if [ ! -s "$cfile" ] || [ ! -s "$deps" ]; then
    return 1
  fi
  if [ ! -s "$c_ok" ]; then
    return 1
  fi
  if [ "$c_ok" -ot "$cfile" ]; then
    return 1
  fi
  while IFS= read -r dep; do
    if [ -z "$dep" ]; then
      continue
    fi
    if [ ! -f "$dep" ]; then
      return 1
    fi
    if [ "$dep" -nt "$cfile" ]; then
      return 1
    fi
  done <"$deps"
  return 0
}

clear_c_ok() {
  rm -f "$c_ok"
}

mark_c_ok() {
  printf 'ok\n' >"$c_ok"
}

compile_to_c() {
  mode="$1"
  cfile="$2"
  if [ "$mode" = "stage1" ]; then
    if [ ! -x ./stage1_runner ]; then
      return 1
    fi
    stage1_env=""
    if [ -n "${CHENG_BUILD_FAST:-}" ]; then
      stage1_env="CHENG_STAGE1_SKIP_SEM=1 CHENG_STAGE1_SKIP_OWNERSHIP=1"
    fi
    if [ -n "${CHENG_BUILD_VERBOSE:-}" ]; then
      if [ -n "$stage1_env" ]; then
        stage1_env="$stage1_env CHENG_STAGE1_TRACE=1 CHENG_STAGE1_TRACE_FLUSH=1"
      else
        stage1_env="CHENG_STAGE1_TRACE=1 CHENG_STAGE1_TRACE_FLUSH=1"
      fi
    else
      if [ -n "${CHENG_BUILD_FAST:-}" ]; then
        echo "[Info] stage1_runner (fast mode, no trace); set CHENG_BUILD_VERBOSE=1 for trace" 1>&2
      else
        echo "[Info] stage1_runner (may take a while); set CHENG_BUILD_VERBOSE=1 for trace" 1>&2
      fi
    fi
  if cache_is_fresh "$cfile" "$deps_list"; then
    echo "[Info] using cached C: $cfile" 1>&2
    return 0
  fi
  had_ok="0"
  if [ -s "$c_ok" ]; then
    had_ok="1"
  fi
  clear_c_ok
  rm -f "$cfile"
  timeout="${CHENG_BUILD_TIMEOUT:-$build_timeout}"
  warmup_timeout="${CHENG_BUILD_WARMUP_TIMEOUT:-}"
  case "$warmup_timeout" in
    ''|*[!0-9]*)
      ;;
    *)
      if [ "$had_ok" = "0" ] && [ -n "$use_cache" ] && [ "$warmup_timeout" -gt "$timeout" ] 2>/dev/null; then
        timeout="$warmup_timeout"
        echo "[Info] warmup compile to seed cache (${timeout}s)" 1>&2
      fi
      ;;
  esac
    rc=0
    if [ -n "${CHENG_BUILD_VERBOSE:-}" ]; then
      if [ -n "$stage1_env" ]; then
        run_with_timeout "$timeout" env $stage1_env ./stage1_runner --mode:c --file:"$entry" --out:"$cfile" || rc="$?"
      else
        run_with_timeout "$timeout" ./stage1_runner --mode:c --file:"$entry" --out:"$cfile" || rc="$?"
      fi
    else
      if [ -n "$stage1_env" ]; then
        run_with_timeout "$timeout" env $stage1_env ./stage1_runner --mode:c --file:"$entry" --out:"$cfile" >/dev/null || rc="$?"
      else
        run_with_timeout "$timeout" ./stage1_runner --mode:c --file:"$entry" --out:"$cfile" >/dev/null || rc="$?"
      fi
    fi
    if [ "$rc" -ne 0 ]; then
      if is_timeout_status "$rc"; then
        echo "[Error] stage1_runner timed out after ${timeout}s" 1>&2
      fi
      rm -f "$cfile"
      clear_c_ok
      return 1
    fi
    [ -f "$cfile" ] || return 1
    mark_c_ok
    return 0
  fi
  echo "[Error] stage0 C backend is not available; stage1_runner is required" 1>&2
  return 1
}

detect_jobs() {
  jobs=""
  if command -v getconf >/dev/null 2>&1; then
    jobs="$(getconf _NPROCESSORS_ONLN 2>/dev/null || true)"
  fi
  if [ -z "$jobs" ] && command -v nproc >/dev/null 2>&1; then
    jobs="$(nproc 2>/dev/null || true)"
  fi
  if [ -z "$jobs" ] && command -v sysctl >/dev/null 2>&1; then
    jobs="$(sysctl -n hw.logicalcpu 2>/dev/null || sysctl -n hw.ncpu 2>/dev/null || true)"
  fi
  case "$jobs" in
    ''|*[!0-9]*)
      jobs="4"
      ;;
  esac
  if [ "$jobs" -lt 1 ] 2>/dev/null; then
    jobs="4"
  fi
  printf '%s\n' "$jobs"
}

fix_seq_string_typedef() {
  file="$1"
  if [ ! -f "$file" ]; then
    return 0
  fi
  if grep -q "typedef VectorQ_string_0 seqQ_string_0;" "$file"; then
    perl -0pi -e 's/\ntypedef VectorQ_string_0 seqQ_string_0;\n/\n/' "$file"
  fi
  if ! grep -q "typedef VectorQ_string_0 seqQ_string_0;" "$file"; then
    perl -0pi -e 's/(typedef struct VectorQ_string_0\\{.*?\\}\\s*VectorQ_string_0;)/$1\\ntypedef VectorQ_string_0 seqQ_string_0;/s' "$file"
  fi
}

build_stage1_modules() {
  if [ ! -x ./stage1_runner ]; then
    return 1
  fi
  modules_out="chengcache/${prog}.modules"
  modules_map="${modules_out}/modules.map"
  jobs_arg=""
  if [ -n "${CHENG_BUILD_JOBS:-}" ]; then
    jobs_arg="--jobs:${CHENG_BUILD_JOBS}"
  fi
  deps_frontend="${CHENG_DEPS_FRONTEND:-}"
  if [ -z "$deps_frontend" ]; then
    if [ -x "./stage1_runner" ]; then
      deps_frontend="./stage1_runner"
    fi
  fi
  if [ -n "${CHENG_DEPS_PREWARM:-}" ] && [ "${CHENG_DEPS_PREWARM}" != "0" ] && [ "$deps_prewarm_done" != "1" ]; then
    deps_prewarm_done="1"
    CHENG_DEPS_FRONTEND="$deps_frontend" src/tooling/prewarm_deps_parallel.sh "$entry"
  fi
  if [ -n "$deps_frontend" ]; then
    export CHENG_DEPS_FRONTEND="$deps_frontend"
  fi
  if [ -n "${CHENG_BUILD_FAST:-}" ]; then
    export CHENG_STAGE1_SKIP_SEM=1
    export CHENG_STAGE1_SKIP_OWNERSHIP=1
  fi
  if [ -n "${CHENG_BUILD_VERBOSE:-}" ]; then
    export CHENG_STAGE1_TRACE=1
    export CHENG_STAGE1_TRACE_FLUSH=1
  fi
  if ! CHENG_C_FRONTEND="./stage1_runner" CHENG_FORCE_C_MODULES=1 \
    src/tooling/chengc.sh "$entry" --emit-c-modules --modules-out:"$modules_out" $jobs_arg; then
    return 1
  fi
  if [ ! -s "$modules_map" ]; then
    echo "[Error] missing modules map: $modules_map" 1>&2
    return 1
  fi
  obj_list="${modules_out}/modules.obj.list"
  compile_list="${modules_out}/modules.compile.list"
  : >"$obj_list"
  : >"$compile_list"
  while IFS="$(printf '\t')" read -r mod_path mod_out mod_obj modsym; do
    if [ -z "$mod_out" ]; then
      continue
    fi
    fix_seq_string_typedef "$mod_out"
    obj_out="${mod_out%.c}.o"
    printf '%s\n' "$obj_out" >>"$obj_list"
    if [ ! -f "$obj_out" ] || [ "$mod_out" -nt "$obj_out" ]; then
      printf '%s\n' "$mod_out" >>"$compile_list"
    fi
  done <"$modules_map"
  jobs_compile="${CHENG_BUILD_JOBS:-}"
  if [ -z "$jobs_compile" ]; then
    jobs_compile="$(detect_jobs)"
  fi
  if [ -s "$compile_list" ]; then
    CHENG_BUILD_CC="$cc" CHENG_BUILD_CFLAGS="$cflags" CHENG_BUILD_RT_INC="$runtime_inc" \
      tr '\n' '\0' < "$compile_list" | xargs -0 -P "$jobs_compile" -I{} sh -c '
        file="$1"
        out="${file%.c}.o"
        "$CHENG_BUILD_CC" $CHENG_BUILD_CFLAGS -I"$CHENG_BUILD_RT_INC" -Isrc/runtime/native -c "$file" -o "$out"
      ' _ {}
  fi
  module_objects="$(tr '\n' ' ' < "$obj_list")"
  if [ -z "$module_objects" ]; then
    echo "[Error] no module objects produced" 1>&2
    return 1
  fi
  export CHENG_BUILD_MODULE_OBJECTS="$module_objects"
  return 0
}

prog_c="chengcache/${prog}.c"
mkdir -p chengcache
echo "== Cheng -> C =="
module_objects=""
case "$compiler" in
  auto)
    if [ "$module_build" != "0" ] && build_stage1_modules; then
      module_objects="$CHENG_BUILD_MODULE_OBJECTS"
      echo "ok: stage1_runner (modules)"
    elif compile_to_c stage1 "$prog_c"; then
      echo "ok: stage1_runner"
    else
      echo "[Error] stage1_runner failed (no stage0 fallback available)" 1>&2
      exit 2
    fi
    ;;
  stage1)
    if [ "$module_build" != "0" ] && build_stage1_modules; then
      module_objects="$CHENG_BUILD_MODULE_OBJECTS"
      echo "ok: stage1_runner (modules)"
    else
      compile_to_c stage1 "$prog_c"
      echo "ok: stage1_runner"
    fi
    ;;
  stage0)
    echo "[Error] --compiler:stage0 is not supported (stage0 C backend removed)" 1>&2
    exit 2
    ;;
  *)
    echo "[Error] unknown --compiler: $compiler (use auto|stage1)" 1>&2
    exit 2
    ;;
esac
obj_main="chengcache/${prog}.o"
obj_sys="chengcache/${prog}.system_helpers.o"

obj_inputs=""
if [ -n "$module_objects" ]; then
  obj_inputs="$module_objects"
else
  if [ -f "$prog_c" ]; then
    fix_seq_string_typedef "$prog_c"
  fi
  if [ ! -f "$prog_c" ]; then
    echo "[Error] missing generated C: $prog_c" 1>&2
    exit 2
  fi
  echo "== Compile generated C =="
  "$cc" $cflags -I"$runtime_inc" -Isrc/runtime/native -c "$prog_c" -o "$obj_main"
  obj_inputs="$obj_main"
fi

echo "== Compile runtime helpers =="
"$cc" -I"$runtime_inc" -Isrc/runtime/native -c src/runtime/native/system_helpers.c -o "$obj_sys"

echo "== Link platform =="
case "$uname_s" in
  Darwin)
    if ! command -v clang >/dev/null 2>&1; then
      echo "[Error] macOS build requires clang" 1>&2
      exit 2
    fi
    obj_plat="chengcache/${prog}.macos_app.o"
    obj_text="chengcache/${prog}.text_macos.o"
    clang -fobjc-arc -c "$gui_root/platform/macos_app.m" -o "$obj_plat"
    clang -std=c11 -c "$gui_root/render/text_macos.c" -o "$obj_text"
    clang $obj_inputs "$obj_sys" "$obj_plat" "$obj_text" \
      -framework Cocoa -framework QuartzCore -framework CoreGraphics -framework CoreText -framework CoreFoundation \
      -o "$out"
    ;;
  Linux)
    obj_plat="chengcache/${prog}.x11_app.o"
    "$cc" -c "$gui_root/platform/x11_app.c" -o "$obj_plat"
    "$cc" $obj_inputs "$obj_sys" "$obj_plat" -lX11 -lXext -o "$out"
    ;;
  MINGW*|MSYS*|CYGWIN*|Windows_NT)
    obj_plat="chengcache/${prog}.win32_app.o"
    "$cc" -c "$gui_root/platform/win32_app.c" -o "$obj_plat"
    "$cc" $obj_inputs "$obj_sys" "$obj_plat" -luser32 -lgdi32 -limm32 -o "$out"
    ;;
  *)
    echo "[Error] unsupported platform: $uname_s" 1>&2
    exit 2
    ;;
esac

echo "ok: $out"
