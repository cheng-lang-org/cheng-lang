#!/usr/bin/env sh
set -euo pipefail

usage() {
  cat <<'EOF'
Usage:
  src/tooling/build_mobile_export.sh <file.cheng> [--name:<appName>] [--out:<dir>] [--with-bridge] [--with-android-project] [--with-ios-project] [--with-projects] [--with-asm-gui] [--with-gui-native] [--without-gui-native] [--assets:<dir>] [--mm:<orc|off>] [--orc|--off]

Notes:
  - Generate C artifacts for mobile (.cheng -> .c) for Android NDK / iOS Xcode integration.
  - Uses ./stage1_runner by default (stage0 fallback removed).
  - Default output directory is mobile_build/<appName>.
  - --with-bridge copies cheng-mobile/bridge into the output directory for native integration.
  - --with-android-project generates an Android Studio template project (Kotlin).
  - --with-asm-gui copies asm_gui sources into the Android template (optional).
  - --with-ios-project generates an iOS template project skeleton.
  - --with-projects generates both Android and iOS templates.
  - --assets:<dir> copies the assets directory and writes assets_manifest.txt.
EOF
}

if [ "${1:-}" = "" ] || [ "${1:-}" = "--help" ] || [ "${1:-}" = "-h" ]; then
  usage
  exit 0
fi

in="$1"
shift || true

name=""
out=""
with_bridge="0"
with_android_project="0"
with_ios_project="0"
with_asm_gui="0"
with_gui_native="auto"
assets=""
mm=""
while [ "${1:-}" != "" ]; do
  case "$1" in
    --name:*)
      name="${1#--name:}"
      ;;
    --out:*)
      out="${1#--out:}"
      ;;
    --with-bridge)
      with_bridge="1"
      ;;
    --with-android-project)
      with_android_project="1"
      ;;
    --with-ios-project)
      with_ios_project="1"
      ;;
    --with-asm-gui)
      with_asm_gui="1"
      ;;
    --with-gui-native)
      with_gui_native="1"
      ;;
    --without-gui-native|--no-gui-native)
      with_gui_native="0"
      ;;
    --with-projects)
      with_android_project="1"
      with_ios_project="1"
      ;;
    --assets:*)
      assets="${1#--assets:}"
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

if [ ! -f "$in" ]; then
  echo "[Error] file not found: $in" 1>&2
  exit 2
fi

if [ "$name" = "" ]; then
  base="$(basename "$in")"
  name="${base%.cheng}"
fi

root="$(cd "$(dirname "$0")/../.." && pwd)"
runtime_inc="${CHENG_RT_INC:-${CHENG_RUNTIME_INC:-$root/runtime/include}}"
mobile_root="${CHENG_MOBILE_ROOT:-}"
if [ -z "$mobile_root" ]; then
  if [ -d "$root/mobile" ]; then
    mobile_root="$root/mobile"
  elif [ -d "$HOME/.cheng-packages/cheng-mobile" ]; then
    mobile_root="$HOME/.cheng-packages/cheng-mobile"
  fi
fi
if [ "$out" = "" ]; then
  out="mobile_build/$name"
fi
case "$out" in
  /*) ;;
  *) out="$root/$out" ;;
esac

input_root="$(CDPATH= cd -- "$(dirname -- "$in")" && pwd)"
if [ -n "$input_root" ]; then
  pkg_roots="${CHENG_PKG_ROOTS:-}"
  default_pkg_root=""
  if [ -d "$HOME/.cheng-packages" ]; then
    default_pkg_root="$HOME/.cheng-packages"
  fi
  if [ -z "$pkg_roots" ] && [ -n "$default_pkg_root" ]; then
    pkg_roots="$default_pkg_root"
  fi
  if [ -n "$pkg_roots" ]; then
    case ",$pkg_roots," in
      *,"$input_root",*) ;;
      * ) pkg_roots="$pkg_roots,$input_root" ;;
    esac
  else
    pkg_roots="$input_root"
  fi
  if [ -n "$default_pkg_root" ]; then
    case ",$pkg_roots," in
      *,"$default_pkg_root",*) ;;
      * ) pkg_roots="$pkg_roots,$default_pkg_root" ;;
    esac
  fi
  export CHENG_PKG_ROOTS="$pkg_roots"
fi

assets_root=""
if [ "$assets" != "" ]; then
  assets_root="$assets"
  case "$assets_root" in
    /*) ;;
    *) assets_root="$root/$assets_root" ;;
  esac
  if [ ! -d "$assets_root" ]; then
    echo "[Error] assets dir not found: $assets_root" 1>&2
    exit 2
  fi
fi

frontend="${CHENG_FRONTEND:-}"
if [ "$frontend" = "" ]; then
  if [ -x "$root/stage1_runner" ]; then
    frontend="$root/stage1_runner"
  else
    echo "[Error] stage1_runner is required for C export (stage0 fallback removed)" 1>&2
    exit 2
  fi
fi
if [ ! -x "$frontend" ]; then
  echo "[Error] frontend not found or not executable: $frontend" 1>&2
  exit 2
fi

if [ -n "$mm" ]; then
  export CHENG_MM="$mm"
fi

# Mobile exports target the cheng-mobile host ABI (Android/iOS templates) and
# should enable `defined(mobile_host)` by default.
if [ -z "${CHENG_DEFINES:-}" ]; then
  export CHENG_DEFINES="mobile_host"
else
  case ",${CHENG_DEFINES}," in
    *,mobile_host,*) ;;
    *) export CHENG_DEFINES="${CHENG_DEFINES},mobile_host" ;;
  esac
fi

# The direct C backend requires explicit types for some local bindings.
# Mobile exports target clang/gcc toolchains, so enabling `__auto_type` keeps
# app builds ergonomic without editing large UI code.
if [ -z "${CHENG_C_AUTO_TYPE:-}" ] && [ -z "${CHENG_STAGE1_C_AUTO_TYPE:-}" ]; then
  export CHENG_C_AUTO_TYPE=1
fi

build_timeout="${CHENG_BUILD_TIMEOUT:-60}"
build_timeout_total="${CHENG_BUILD_TIMEOUT_TOTAL:-}"
build_start_ts=""
if [ -n "$build_timeout_total" ]; then
  build_start_ts="$(date +%s 2>/dev/null || true)"
fi
if [ -z "${CHENG_BUILD_TIMEOUT:-}" ]; then
  export CHENG_BUILD_TIMEOUT="$build_timeout"
fi
module_build="${CHENG_MOBILE_MODULE_BUILD:-1}"
module_jobs="${CHENG_MOBILE_MODULE_JOBS:-${CHENG_MODULE_JOBS:-}}"
module_jobs_cap="${CHENG_MOBILE_MODULE_JOBS_CAP:-}"
if [ -z "$module_jobs_cap" ] && [ -n "$build_timeout_total" ]; then
  module_jobs_cap="4"
fi

budget_for_step() {
  limit="$build_timeout"
  if [ -n "$build_timeout_total" ] && [ -n "$build_start_ts" ]; then
    now="$(date +%s 2>/dev/null || true)"
    if [ -n "$now" ]; then
      elapsed=$((now - build_start_ts))
      remaining=$((build_timeout_total - elapsed))
      if [ "$remaining" -lt "$limit" ]; then
        limit="$remaining"
      fi
    fi
  fi
  if [ "$limit" -le 0 ]; then
    echo "0"
  else
    echo "$limit"
  fi
}

run_with_timeout() {
  seconds="$1"
  shift || true
  if [ "$seconds" -le 0 ]; then
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
    124|137|142) return 0 ;;
    *) return 1 ;;
  esac
}

detect_jobs() {
  if [ -n "$module_jobs" ]; then
    echo "$module_jobs"
    return
  fi
  if command -v getconf >/dev/null 2>&1; then
    jobs="$(getconf _NPROCESSORS_ONLN 2>/dev/null || true)"
    if [ -n "$jobs" ]; then
      if [ -n "$module_jobs_cap" ] && [ "$module_jobs_cap" -gt 0 ] 2>/dev/null && [ "$jobs" -gt "$module_jobs_cap" ] 2>/dev/null; then
        echo "$module_jobs_cap"
      else
        echo "$jobs"
      fi
      return
    fi
  fi
  if command -v sysctl >/dev/null 2>&1; then
    jobs="$(sysctl -n hw.ncpu 2>/dev/null || true)"
    if [ -n "$jobs" ]; then
      if [ -n "$module_jobs_cap" ] && [ "$module_jobs_cap" -gt 0 ] 2>/dev/null && [ "$jobs" -gt "$module_jobs_cap" ] 2>/dev/null; then
        echo "$module_jobs_cap"
      else
        echo "$jobs"
      fi
      return
    fi
  fi
  echo "4"
}

compile_to_c_modules() {
  modules_out="$1"
  jobs="$2"
  tmp_name="${name}_modules"
  mkdir -p "$modules_out"
  set -- "$root/src/tooling/chengc.sh" "$in" \
    --emit-c-modules --modules-out:"$modules_out" --name:"$tmp_name"
  if [ -n "$jobs" ]; then
    set -- "$@" --jobs:"$jobs"
  fi
  module_env=""
  if [ -n "${CHENG_BUILD_FAST:-}" ]; then
    module_env="CHENG_STAGE1_SKIP_SEM=1 CHENG_STAGE1_SKIP_OWNERSHIP=1"
    if [ -n "${CHENG_BUILD_VERBOSE:-}" ]; then
      module_env="$module_env CHENG_STAGE1_TRACE=1 CHENG_STAGE1_TRACE_FLUSH=1"
    fi
  elif [ -n "${CHENG_BUILD_VERBOSE:-}" ]; then
    module_env="CHENG_STAGE1_TRACE=1 CHENG_STAGE1_TRACE_FLUSH=1"
  fi
  if [ -n "${CHENG_BUILD_SKIP_MONO:-}" ]; then
    if [ -n "$module_env" ]; then
      module_env="$module_env CHENG_STAGE1_SKIP_MONO=1"
    else
      module_env="CHENG_STAGE1_SKIP_MONO=1"
    fi
  fi
  if [ -n "$module_env" ]; then
    env $module_env "$@"
  else
    "$@"
  fi
  status=$?
  if [ "$status" -ne 0 ]; then
    if is_timeout_status "$status"; then
      echo "[Warn] chengc modules timed out after ${build_timeout}s" 1>&2
    fi
    return 1
  fi
  if [ -f "$root/$tmp_name" ]; then
    rm -f "$root/$tmp_name"
  fi
  return 0
}

if [ -x "$root/stage1_runner" ] && [ -f "$root/src/stage1/c_codegen.cheng" ]; then
  mm_mode="${CHENG_MM:-}"
  if [ -z "$mm_mode" ]; then
    mm_mode="orc"
  fi
  if [ "$mm_mode" != "off" ] && [ "$root/src/stage1/c_codegen.cheng" -nt "$root/stage1_runner" ]; then
    echo "[Warn] CHENG_MM=$mm_mode but $root/stage1_runner is older than src/stage1/c_codegen.cheng; consider running src/tooling/bootstrap.sh" 1>&2
  fi
fi

if [ -n "${CHENG_DEPS_PREWARM:-}" ]; then
  "$root/src/tooling/prewarm_deps_parallel.sh" "$in"
fi

mkdir -p "$out"
echo "[mobile] export $in -> $out"
echo "[mobile] build timeout: ${build_timeout}s"
if [ -n "$build_timeout_total" ]; then
  echo "[mobile] total timeout: ${build_timeout_total}s"
fi

cfile="$out/${name}.c"
modules_out=""
modules_map=""
if [ "$module_build" != "0" ]; then
  if [ -n "$build_timeout_total" ]; then
    remaining="$(budget_for_step)"
    if [ "$remaining" -le 0 ]; then
      echo "[Warn] total timeout budget exhausted; skipping modules" 1>&2
      module_build="0"
    elif [ "$remaining" -lt "$build_timeout" ]; then
      echo "[Warn] total timeout budget below per-step timeout; continuing module build" 1>&2
    fi
  fi
fi
if [ "$module_build" != "0" ]; then
  jobs="$(detect_jobs)"
  modules_out="$out/modules"
  modules_map="$modules_out/modules.map"
  echo "== Cheng -> C modules (jobs: ${jobs}) =="
  if compile_to_c_modules "$modules_out" "$jobs"; then
    if [ ! -s "$modules_map" ]; then
      echo "[Error] missing modules map: $modules_map" 1>&2
      exit 2
    fi
  else
    echo "[Warn] module build failed; fallback to single C file" 1>&2
    module_build="0"
  fi
fi
if [ "$module_build" != "0" ]; then
  cfile=""
fi
if [ "$module_build" = "0" ]; then
if [ "$frontend" = "$root/stage1_runner" ]; then
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
  fi
  if [ -n "${CHENG_BUILD_SKIP_MONO:-}" ]; then
    if [ -n "$stage1_env" ]; then
      stage1_env="$stage1_env CHENG_STAGE1_SKIP_MONO=1"
    else
      stage1_env="CHENG_STAGE1_SKIP_MONO=1"
    fi
  fi
  stage1_rc=0
  if [ -n "$stage1_env" ]; then
    budget="$(budget_for_step)"
    run_with_timeout "$budget" env $stage1_env "$frontend" --mode:c --file:"$in" --out:"$cfile" || stage1_rc=$?
  else
    budget="$(budget_for_step)"
    run_with_timeout "$budget" "$frontend" --mode:c --file:"$in" --out:"$cfile" || stage1_rc=$?
  fi
  if [ "$stage1_rc" -ne 0 ]; then
    if is_timeout_status "$stage1_rc"; then
      echo "[Warn] stage1_runner timed out after ${build_timeout}s" 1>&2
    fi
    if [ -z "${CHENG_BUILD_FAST:-}" ] && is_timeout_status "$stage1_rc"; then
      fast_env="CHENG_STAGE1_SKIP_SEM=1 CHENG_STAGE1_SKIP_OWNERSHIP=1"
      if [ -n "${CHENG_BUILD_VERBOSE:-}" ]; then
        fast_env="$fast_env CHENG_STAGE1_TRACE=1 CHENG_STAGE1_TRACE_FLUSH=1"
      fi
      echo "[Warn] retry stage1_runner in fast mode" 1>&2
      fast_rc=0
      budget="$(budget_for_step)"
      run_with_timeout "$budget" env $fast_env "$frontend" --mode:c --file:"$in" --out:"$cfile" || fast_rc=$?
      if [ "$fast_rc" -eq 0 ]; then
        stage1_rc=0
      else
        stage1_rc="$fast_rc"
      fi
    fi
  fi
  if [ "$stage1_rc" -ne 0 ]; then
    echo "[Error] stage1_runner failed; no stage0 fallback available" 1>&2
    exit 2
  fi
else
  frontend_rc=0
  budget="$(budget_for_step)"
  run_with_timeout "$budget" "$frontend" --mode:c --file:"$in" --out:"$cfile" || frontend_rc=$?
  if [ "$frontend_rc" -ne 0 ]; then
    if is_timeout_status "$frontend_rc"; then
      echo "[Error] frontend timed out after ${build_timeout}s" 1>&2
    fi
    exit 2
  fi
fi
fi

if [ "$with_bridge" = "1" ]; then
  if [ -z "$mobile_root" ] || [ ! -d "$mobile_root" ]; then
    echo "[Error] cheng-mobile package not found; set CHENG_MOBILE_ROOT" 1>&2
    exit 2
  fi
  mkdir -p "$out/bridge"
  cp "$mobile_root/bridge/"*.h "$out/bridge/"
  cp "$mobile_root/bridge/"*.c "$out/bridge/"
fi

copy_assets_tree() {
  src="$1"
  dest="$2"
  manifest="$3"
  if [ "$src" = "" ] || [ "$dest" = "" ] || [ "$manifest" = "" ]; then
    return
  fi
  mkdir -p "$dest"
  rm -f "$manifest"
  (
    cd "$src"
    find . -type f | while IFS= read -r f; do
      rel="${f#./}"
      if [ "$rel" = "" ]; then
        continue
      fi
      dir="$(dirname "$rel")"
      if [ "$dir" = "." ]; then
        dir=""
      fi
      mkdir -p "$dest/$dir"
      cp "$src/$rel" "$dest/$rel"
      size="$(wc -c < "$src/$rel" | tr -d ' ')"
      printf "%s\t%s\n" "$rel" "$size" >> "$manifest"
    done
  )
}

copy_assets_into() {
  src="$1"
  dest="$2"
  if [ "$src" = "" ] || [ ! -d "$src" ]; then
    return
  fi
  mkdir -p "$dest"
  cp -R "$src/." "$dest/"
}

assets_bundle=""
assets_manifest=""
if [ "$assets_root" != "" ]; then
  assets_bundle="$out/assets"
  assets_manifest="$out/assets_manifest.txt"
  copy_assets_tree "$assets_root" "$assets_bundle" "$assets_manifest"
fi

copy_android_project() {
  android_project_out="$out/android_project"
  if [ -z "$mobile_root" ] || [ ! -d "$mobile_root" ]; then
    echo "[Error] cheng-mobile package not found; set CHENG_MOBILE_ROOT" 1>&2
    exit 2
  fi
  android_template="$mobile_root/android/project_template"
  if [ ! -d "$android_template" ]; then
    echo "[Error] android template missing: $android_template" 1>&2
    exit 2
  fi
  rm -rf "$android_project_out"
  cp -R "$android_template" "$android_project_out"
  android_kotlin_dir="$android_project_out/app/src/main/java/com/cheng/mobile"
  if [ -d "$android_kotlin_dir" ]; then
    if [ -f "$root/src/tooling/mobile/android/ChengActivity.kt" ]; then
      cp "$root/src/tooling/mobile/android/ChengActivity.kt" "$android_kotlin_dir/ChengActivity.kt"
    fi
    if [ -f "$root/src/tooling/mobile/android/ChengSurfaceView.kt" ]; then
      cp "$root/src/tooling/mobile/android/ChengSurfaceView.kt" "$android_kotlin_dir/ChengSurfaceView.kt"
    fi
  fi
  android_cpp_dir="$android_project_out/app/src/main/cpp"
  mkdir -p "$android_cpp_dir"
  # When module build is enabled, prefer emitting multiple C sources.
  # The Android template historically built a single C file; keep that path as fallback.
  if [ -n "${modules_map:-}" ] && [ -s "${modules_map:-}" ]; then
    include_gui_native="$with_gui_native"
    mod_dir="$android_cpp_dir/cheng_modules"
    mkdir -p "$mod_dir"
    while IFS="$(printf '\t')" read -r mod_path mod_out mod_obj modsym; do
      if [ -z "$mod_out" ] || [ ! -f "$mod_out" ]; then
        continue
      fi
      cp "$mod_out" "$mod_dir/$(basename "$mod_out")"
    done <"$modules_map"
    if [ "$include_gui_native" = "auto" ]; then
      include_gui_native="1"
      if grep -R -E -q "C_CHENGCALL\\([^,]+,\\s*chengGuiNative[[:alnum:]_]*\\)\\([^\\)]*\\)\\s*\\{" "$mod_dir"; then
        include_gui_native="0"
        echo "[mobile] android: skip cheng_gui_native_android.c (app defines chengGuiNative*)" 1>&2
      fi
    fi
    cat >"$android_cpp_dir/cheng_mobile_app_includes.h" <<'EOF'
#ifndef CHENG_MOBILE_APP_INCLUDES_H
#define CHENG_MOBILE_APP_INCLUDES_H

#include <unistd.h>

// Android NDK/clang defines `unix` as a macro on some targets.
// Our generated C may legitimately use `unix` as an identifier (e.g. DateTime.unix).
#ifdef unix
#undef unix
#endif
#ifdef linux
#undef linux
#endif

// Keep the Cheng-generated C backend portable.
//
// Note: do NOT declare the `cheng_mobile_host_*` ABI here. Those functions are
// declared by the generated C sources themselves based on `@importc` signatures.
// Duplicating them here easily causes prototype mismatches (especially around
// config/event struct names and constness) when compiling multiple modules.

int cheng_mobile_host_android_width(void);
int cheng_mobile_host_android_height(void);

#endif
EOF
    cat >"$android_cpp_dir/CMakeLists.txt" <<'EOF'
cmake_minimum_required(VERSION 3.10)
project(cheng_mobile_template C ASM)

file(GLOB CHENG_APP_MODULES "${CMAKE_CURRENT_LIST_DIR}/cheng_modules/*.c")
set(CHENG_APP_C "${CMAKE_CURRENT_LIST_DIR}/cheng_mobile_app.c")
set(CHENG_USE_MODULES OFF)

if (CHENG_APP_MODULES)
  message(STATUS "Cheng mobile: using C modules")
  set(CHENG_APP_SOURCES ${CHENG_APP_MODULES})
  set(CHENG_USE_MODULES ON)
elseif (EXISTS "${CHENG_APP_C}")
  message(STATUS "Cheng mobile: using single C file")
  set(CHENG_APP_SOURCES ${CHENG_APP_C})
else()
  message(FATAL_ERROR "missing cheng_mobile_app.c and cheng_modules/*.c (run build_mobile_export.sh)")
endif()

add_library(cheng_mobile_host SHARED
  ${CHENG_APP_SOURCES}
  cheng_mobile_host_android.c
  cheng_mobile_android_jni.c
  cheng_mobile_android_ndk.c
  cheng_mobile_android_gl.c
EOF
    if [ "${include_gui_native:-1}" = "1" ]; then
      echo "  cheng_gui_native_android.c" >>"$android_cpp_dir/CMakeLists.txt"
    fi
    cat >>"$android_cpp_dir/CMakeLists.txt" <<'EOF'
  cheng_mobile_host_api.c
  cheng_mobile_host_core.c
  system_helpers.c
)

target_include_directories(cheng_mobile_host PRIVATE
  ${CMAKE_CURRENT_LIST_DIR}
)

if (CHENG_USE_MODULES)
  foreach(src ${CHENG_APP_MODULES})
    set_source_files_properties(${src} PROPERTIES
      COMPILE_OPTIONS "-include;${CMAKE_CURRENT_LIST_DIR}/cheng_mobile_app_includes.h"
    )
  endforeach()
else()
  set_source_files_properties(${CHENG_APP_C} PROPERTIES
    COMPILE_OPTIONS "-include;${CMAKE_CURRENT_LIST_DIR}/cheng_mobile_app_includes.h"
  )
endif()

target_link_libraries(cheng_mobile_host
  android
  log
  EGL
  GLESv2
)

option(CHENG_ENABLE_ASM_GUI "Enable Cheng ASM GUI bridge" OFF)
set(CHENG_GUI_ASM_ROOT "" CACHE PATH "Path to asm_gui sources")
if (CHENG_ENABLE_ASM_GUI)
  if (NOT CHENG_GUI_ASM_ROOT)
    set(CHENG_GUI_ASM_ROOT "${CMAKE_CURRENT_LIST_DIR}/asm_gui")
  endif()
  if (EXISTS "${CHENG_GUI_ASM_ROOT}/gui_android_arm64.s")
    target_sources(cheng_mobile_host PRIVATE
      ${CHENG_GUI_ASM_ROOT}/gui_android_arm64.s
    )
    target_include_directories(cheng_mobile_host PRIVATE
      ${CHENG_GUI_ASM_ROOT}
    )
    target_compile_definitions(cheng_mobile_host PRIVATE CHENG_ENABLE_ASM_GUI=1)
  else()
    message(WARNING "Cheng ASM GUI: CHENG_GUI_ASM_ROOT not found")
  endif()
endif()

set(CHENG_ANDROID_NDK_ROOT "")
if (DEFINED ANDROID_NDK)
  set(CHENG_ANDROID_NDK_ROOT "${ANDROID_NDK}")
elseif (DEFINED CMAKE_ANDROID_NDK)
  set(CHENG_ANDROID_NDK_ROOT "${CMAKE_ANDROID_NDK}")
elseif (DEFINED ENV{ANDROID_NDK_HOME})
  set(CHENG_ANDROID_NDK_ROOT "$ENV{ANDROID_NDK_HOME}")
endif()

if (CHENG_ANDROID_NDK_ROOT)
  target_sources(cheng_mobile_host PRIVATE
    ${CHENG_ANDROID_NDK_ROOT}/sources/android/native_app_glue/android_native_app_glue.c
  )
  target_include_directories(cheng_mobile_host PRIVATE
    ${CHENG_ANDROID_NDK_ROOT}/sources/android/native_app_glue
  )
  target_compile_definitions(cheng_mobile_host PRIVATE CHENG_MOBILE_ANDROID_NDK=1)
else()
  message(WARNING "Cheng Mobile: ANDROID_NDK not found; native_app_glue disabled.")
endif()
EOF
  else
    src_c="$out/chengcache/${name}.c"
    if [ ! -f "$src_c" ]; then
      src_c="$out/${name}.c"
    fi
    if [ ! -f "$src_c" ]; then
      echo "[Error] generated C not found: $out/chengcache/${name}.c or $out/${name}.c" 1>&2
      exit 2
    fi
    cp "$src_c" "$android_cpp_dir/cheng_mobile_app.c"
  fi
  cp "$root/src/runtime/native/system_helpers.c" "$android_cpp_dir/"
  cp "$root/src/runtime/native/system_helpers.h" "$android_cpp_dir/"
  cp "$root/src/runtime/native/stb_image.h" "$android_cpp_dir/"
  cp "$runtime_inc/chengbase.h" "$android_cpp_dir/"
  cp "$mobile_root/bridge/cheng_mobile_bridge.h" "$android_cpp_dir/"
  cp "$mobile_root/bridge/cheng_mobile_host_core.h" "$android_cpp_dir/"
  cp "$mobile_root/bridge/cheng_mobile_host_api.h" "$android_cpp_dir/"
  cp "$mobile_root/bridge/cheng_mobile_host_core.c" "$android_cpp_dir/"
  cp "$mobile_root/bridge/cheng_mobile_host_api.c" "$android_cpp_dir/"
  if [ -f "$root/src/tooling/mobile/android/cheng_mobile_host_android.c" ]; then
    cp "$root/src/tooling/mobile/android/cheng_mobile_host_android.c" "$android_cpp_dir/cheng_mobile_host_android.c"
  else
    cp "$mobile_root/android/cheng_mobile_host_android.c" "$android_cpp_dir/"
  fi
  cp "$mobile_root/android/cheng_mobile_host_android.h" "$android_cpp_dir/"
  if [ -f "$root/src/tooling/mobile/android/cheng_mobile_android_jni.c" ]; then
    cp "$root/src/tooling/mobile/android/cheng_mobile_android_jni.c" "$android_cpp_dir/cheng_mobile_android_jni.c"
  else
    cp "$mobile_root/android/cheng_mobile_android_jni.c" "$android_cpp_dir/"
  fi
  cp "$mobile_root/android/cheng_mobile_android_ndk.c" "$android_cpp_dir/"
  cp "$mobile_root/android/cheng_mobile_android_gl.c" "$android_cpp_dir/"
  cp "$mobile_root/android/cheng_mobile_android_gl.h" "$android_cpp_dir/"
  cp "$mobile_root/android/cheng_gui_native_android.c" "$android_cpp_dir/"
  if [ "$with_asm_gui" = "1" ]; then
    asm_gui_root="${CHENG_GUI_ASM_ROOT:-}"
    if [ -z "$asm_gui_root" ] && [ -n "${CHENG_GUI_ROOT:-}" ]; then
      asm_gui_root="${CHENG_GUI_ROOT}/asm_gui"
    fi
    if [ -z "$asm_gui_root" ]; then
      if [ -d "$root/../cheng-gui/asm_gui" ]; then
        asm_gui_root="$root/../cheng-gui/asm_gui"
      elif [ -d "$HOME/cheng-gui/asm_gui" ]; then
        asm_gui_root="$HOME/cheng-gui/asm_gui"
      fi
    fi
    if [ -n "$asm_gui_root" ] && [ -f "$asm_gui_root/gui_android_arm64.s" ]; then
      mkdir -p "$android_cpp_dir/asm_gui"
      cp "$asm_gui_root/gui_android_arm64.s" "$android_cpp_dir/asm_gui/"
      cp "$asm_gui_root/gui_api.h" "$android_cpp_dir/asm_gui/"
    else
      echo "[Warn] asm_gui not found; set CHENG_GUI_ASM_ROOT/CHENG_GUI_ROOT" 1>&2
    fi
  fi
  if [ "$assets_bundle" != "" ]; then
    copy_assets_into "$assets_bundle" "$android_project_out/app/src/main/assets"
    if [ -f "$assets_manifest" ]; then
      cp "$assets_manifest" "$android_project_out/app/src/main/assets/assets_manifest.txt"
    fi
  fi
}

copy_ios_project() {
  ios_project_out="$out/ios_project"
  if [ -z "$mobile_root" ] || [ ! -d "$mobile_root" ]; then
    echo "[Error] cheng-mobile package not found; set CHENG_MOBILE_ROOT" 1>&2
    exit 2
  fi
  ios_template="$mobile_root/ios/project_template"
  if [ ! -d "$ios_template" ]; then
    echo "[Error] ios template missing: $ios_template" 1>&2
    exit 2
  fi
  rm -rf "$ios_project_out"
  cp -R "$ios_template" "$ios_project_out"
  ios_src_dir="$ios_project_out/ChengMobileApp"
  ios_native_dir="$ios_src_dir/Native"
  mkdir -p "$ios_native_dir"
  cp "$mobile_root/ios/ChengViewController.h" "$ios_src_dir/"
  cp "$mobile_root/ios/ChengViewController.m" "$ios_src_dir/"
  src_c="$out/chengcache/${name}.c"
  if [ ! -f "$src_c" ]; then
    src_c="$out/${name}.c"
  fi
  if [ ! -f "$src_c" ]; then
    echo "[Error] generated C not found: $out/chengcache/${name}.c or $out/${name}.c" 1>&2
    exit 2
  fi
  cp "$src_c" "$ios_native_dir/cheng_mobile_app.c"
  cp "$root/src/runtime/native/system_helpers.c" "$ios_native_dir/"
  cp "$root/src/runtime/native/system_helpers.h" "$ios_native_dir/"
  cp "$root/src/runtime/native/stb_image.h" "$ios_native_dir/"
  cp "$runtime_inc/chengbase.h" "$ios_native_dir/"
  cp "$mobile_root/bridge/cheng_mobile_bridge.h" "$ios_native_dir/"
  cp "$mobile_root/bridge/cheng_mobile_host_core.h" "$ios_native_dir/"
  cp "$mobile_root/bridge/cheng_mobile_host_api.h" "$ios_native_dir/"
  cp "$mobile_root/bridge/cheng_mobile_host_core.c" "$ios_native_dir/"
  cp "$mobile_root/bridge/cheng_mobile_host_api.c" "$ios_native_dir/"
  cp "$mobile_root/ios/cheng_mobile_host_ios.m" "$ios_native_dir/"
  cp "$mobile_root/ios/cheng_mobile_ios_glue.h" "$ios_native_dir/"
  cp "$mobile_root/ios/cheng_mobile_ios_glue.m" "$ios_native_dir/"
  if [ "$assets_bundle" != "" ]; then
    copy_assets_into "$assets_bundle" "$ios_project_out/ChengMobileApp/Assets"
    if [ -f "$assets_manifest" ]; then
      cp "$assets_manifest" "$ios_project_out/ChengMobileApp/Assets/assets_manifest.txt"
    fi
  fi
}

if [ "$with_android_project" = "1" ]; then
  copy_android_project
  echo "  - Android project: $out/android_project"
fi

if [ "$with_ios_project" = "1" ]; then
  copy_ios_project
  echo "  - iOS project: $out/ios_project"
fi

echo "[mobile] ok: $out"
echo "  - C output: $out/chengcache/${name}.c"
if [ "$assets_bundle" != "" ]; then
  if [ -f "$assets_manifest" ]; then
    cp "$assets_manifest" "$assets_bundle/assets_manifest.txt"
  fi
  echo "  - Assets: $assets_bundle"
  echo "  - Manifest: $assets_manifest"
fi
