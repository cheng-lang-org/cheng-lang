#!/usr/bin/env sh
. "$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)/env_prefix_bridge.sh"
set -euo pipefail

usage() {
  cat <<'USAGE'
Usage:
  src/tooling/build_mobile_export.sh <file.cheng> [options]

Options:
  --name:<appName>            Export app name (default: basename of input)
  --out:<dir>                 Output directory (default: mobile_build/<name>)
  --assets:<dir>              Extra assets directory to copy
  --with-android-project      Export Android Studio project
  --with-ios-project          Export iOS project
  --with-harmony-project      Export Harmony project skeleton
  --with-asm-gui              Copy asm_gui folder if available
  --plugins:<dir1,dir2,...>   Plugin directories to weave into projects
  --mobile-root:<dir>         Override cheng-mobile package path
  --mm:<orc> | --orc          Accepted for compatibility (ignored)

Notes:
  - This is a compatibility shim for the removed legacy mobile C export pipeline.
  - It always generates a stable native stub `cheng_mobile_app.c` so mobile build
    pipelines remain runnable.
USAGE
}

if [ "${1:-}" = "" ] || [ "${1:-}" = "--help" ] || [ "${1:-}" = "-h" ]; then
  usage
  exit 0
fi

in="$1"
shift || true

if [ ! -f "$in" ]; then
  echo "[Error] input file not found: $in" 1>&2
  exit 2
fi

name=""
out=""
assets=""
plugins="${CHENG_MOBILE_PLUGINS:-}"
mobile_root=""
with_android=0
with_ios=0
with_harmony=0
with_asm_gui=0
mm=""

while [ "${1:-}" != "" ]; do
  case "$1" in
    --name:*)
      name="${1#--name:}"
      ;;
    --out:*)
      out="${1#--out:}"
      ;;
    --assets:*)
      assets="${1#--assets:}"
      ;;
    --plugins:*)
      plugins="${1#--plugins:}"
      ;;
    --mobile-root:*)
      mobile_root="${1#--mobile-root:}"
      ;;
    --with-android-project)
      with_android=1
      ;;
    --with-ios-project)
      with_ios=1
      ;;
    --with-harmony-project)
      with_harmony=1
      ;;
    --with-asm-gui)
      with_asm_gui=1
      ;;
    --orc)
      mm="orc"
      ;;
    --mm:*)
      mm="${1#--mm:}"
      ;;
    *)
      echo "[Error] unknown arg: $1" 1>&2
      usage 1>&2
      exit 2
      ;;
  esac
  shift || true
done

if [ "$mm" != "" ] && [ "$mm" != "orc" ]; then
  echo "[Error] invalid --mm:$mm (only orc is supported)" 1>&2
  exit 2
fi

if [ "$name" = "" ]; then
  base="$(basename "$in")"
  name="${base%.cheng}"
fi

root="$(cd "$(dirname "$0")/../.." && pwd)"

if [ "$out" = "" ]; then
  out="mobile_build/$name"
fi
case "$out" in
  /*) ;;
  *) out="$root/$out" ;;
esac

if [ "$mobile_root" = "" ]; then
  if [ -n "${MOBILE_ROOT:-}" ]; then
    mobile_root="$MOBILE_ROOT"
  elif [ -d "$HOME/.cheng-packages/cheng-mobile" ]; then
    mobile_root="$HOME/.cheng-packages/cheng-mobile"
  elif [ -d "$root/../cheng-mobile" ]; then
    mobile_root="$root/../cheng-mobile"
  fi
fi

if [ "$mobile_root" = "" ] || [ ! -d "$mobile_root" ]; then
  echo "[Error] cheng-mobile root not found; set --mobile-root or MOBILE_ROOT" 1>&2
  exit 2
fi

if [ "$with_android" -eq 0 ] && [ "$with_ios" -eq 0 ] && [ "$with_harmony" -eq 0 ]; then
  with_android=1
  with_ios=1
  with_harmony=1
fi

runtime_c="$root/src/runtime/native/system_helpers.c"
runtime_h="$root/src/runtime/native/system_helpers.h"
runtime_stb_image_h="$root/src/runtime/native/stb_image.h"
if [ ! -f "$runtime_c" ] || [ ! -f "$runtime_h" ]; then
  echo "[Error] runtime helpers not found under src/runtime/native" 1>&2
  exit 2
fi

mkdir -p "$out"

app_c="$out/cheng_mobile_app.c"
cat > "$app_c" <<APP_C
#include "cheng_mobile_bridge.h"
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#if !defined(_WIN32)
#include <unistd.h>
#else
#include <windows.h>
#endif

static void app_sleep_ms(int ms) {
#if defined(_WIN32)
  Sleep((DWORD)(ms < 0 ? 0 : ms));
#else
  if (ms > 0) {
    usleep((unsigned int)ms * 1000u);
  }
#endif
}

static uint32_t pack_rgba(uint32_t r, uint32_t g, uint32_t b) {
  return 0xFF000000u | ((r & 0xFFu) << 16) | ((g & 0xFFu) << 8) | (b & 0xFFu);
}

static void fill_demo(uint32_t* px, int w, int h, int frame) {
  if (px == NULL || w <= 0 || h <= 0) return;
  for (int y = 0; y < h; y++) {
    for (int x = 0; x < w; x++) {
      uint32_t r = (uint32_t)((x + frame) % 255);
      uint32_t g = (uint32_t)((y + (frame * 2)) % 255);
      uint32_t b = (uint32_t)((x + y + (frame * 3)) % 255);
      px[y * w + x] = pack_rgba(r, g, b);
    }
  }
}

void cheng_mobile_app_main(void) {
  ChengMobileConfig cfg;
  memset(&cfg, 0, sizeof(cfg));
#if defined(__ANDROID__)
  cfg.platform = MOBILE_ANDROID;
#elif defined(__APPLE__)
  cfg.platform = MOBILE_IOS;
#else
  cfg.platform = MOBILE_HARMONY;
#endif
  cfg.resourceRoot = cheng_mobile_host_default_resource_root();
  cfg.title = "APP_TITLE_PLACEHOLDER";
  cfg.width = 1080;
  cfg.height = 1920;
  cfg.highDpi = 1;

  (void)cheng_mobile_host_init(&cfg);
  (void)cheng_mobile_host_open_window(&cfg);

  int width = cfg.width > 0 ? cfg.width : 1080;
  int height = cfg.height > 0 ? cfg.height : 1920;
  int stride = width * 4;
  uint32_t* pixels = (uint32_t*)malloc((size_t)stride * (size_t)height);
  if (pixels == NULL) {
    cheng_mobile_host_shutdown("alloc_failed");
    return;
  }

  int running = 1;
  int frame = 0;
  while (running) {
    ChengMobileEvent ev;
    int gotAny = 0;
    while (cheng_mobile_host_poll_event(&ev) != 0) {
      gotAny = 1;
      if (ev.kind == MRE_RUNTIME_STOPPED) {
        running = 0;
        break;
      }
      if (ev.kind == MRE_WINDOW_OPENED) {
        if (cfg.width > 0) width = cfg.width;
        if (cfg.height > 0) height = cfg.height;
        stride = width * 4;
      }
    }

    if (!running) {
      break;
    }

    fill_demo(pixels, width, height, frame);
    cheng_mobile_host_present(pixels, width, height, stride);
    frame = (frame + 1) % 100000;

    if (!gotAny) {
      app_sleep_ms(16);
    }
  }

  free(pixels);
  cheng_mobile_host_shutdown("exit");
}
APP_C

# Inject title and record source for traceability.
esc_name="$(printf '%s' "$name" | sed 's/[\\/&]/\\&/g')"
sed -i.bak "s/APP_TITLE_PLACEHOLDER/${esc_name}/g" "$app_c"
rm -f "$app_c.bak"

echo "/* source: $in */" >> "$app_c"

copy_assets_into() {
  src_dir="$1"
  dst_dir="$2"
  entry_src="$3"

  mkdir -p "$dst_dir"
  if [ -n "$src_dir" ] && [ -d "$src_dir" ]; then
    cp -R "$src_dir/." "$dst_dir/"
  fi

  entry_name="$(basename "$entry_src")"
  cp "$entry_src" "$dst_dir/$entry_name"

  manifest="$dst_dir/assets_manifest.txt"
  {
    echo "# relpath<TAB>size_bytes"
    (cd "$dst_dir" && find . -type f ! -name assets_manifest.txt | sort) | while IFS= read -r rel; do
      rel="${rel#./}"
      size="$(wc -c < "$dst_dir/$rel" | tr -d ' ')"
      printf '%s\t%s\n' "$rel" "$size"
    done
  } > "$manifest"
}

copy_common_cpp_sources() {
  dst_cpp="$1"
  mkdir -p "$dst_cpp"

  cp "$app_c" "$dst_cpp/cheng_mobile_app.c"
  cp "$runtime_c" "$dst_cpp/system_helpers.c"
  cp "$runtime_h" "$dst_cpp/system_helpers.h"
  if [ -f "$runtime_stb_image_h" ]; then
    cp "$runtime_stb_image_h" "$dst_cpp/stb_image.h"
  fi

  cp "$mobile_root/bridge/cheng_mobile_bridge.h" "$dst_cpp/cheng_mobile_bridge.h"
  cp "$mobile_root/bridge/cheng_mobile_host_api.c" "$dst_cpp/cheng_mobile_host_api.c"
  cp "$mobile_root/bridge/cheng_mobile_host_api.h" "$dst_cpp/cheng_mobile_host_api.h"
  cp "$mobile_root/bridge/cheng_mobile_host_core.c" "$dst_cpp/cheng_mobile_host_core.c"
  cp "$mobile_root/bridge/cheng_mobile_host_core.h" "$dst_cpp/cheng_mobile_host_core.h"
}

weave_plugins_android() {
  project_dir="$1"
  [ -z "$plugins" ] && return 0

  plugins_kotlin="$project_dir/app/src/main/kotlin/com/cheng/mobile/plugins"
  plugins_cpp="$project_dir/app/src/main/cpp/plugins"
  mkdir -p "$plugins_kotlin" "$plugins_cpp"

  lock_file="$out/mobile_plugins.lock"
  : > "$lock_file"

  old_ifs="$IFS"
  IFS=','
  for p in $plugins; do
    p="$(printf '%s' "$p" | sed 's/^ *//;s/ *$//')"
    [ -z "$p" ] && continue
    if [ ! -f "$p/mobile_plugin.toml" ]; then
      echo "[Warn] skip plugin (missing mobile_plugin.toml): $p" 1>&2
      continue
    fi
    plugin_name="$(basename "$p")"
    echo "$plugin_name\t$p" >> "$lock_file"

    if [ -d "$p/android" ]; then
      mkdir -p "$plugins_kotlin/$plugin_name"
      cp -R "$p/android/." "$plugins_kotlin/$plugin_name/"
    fi
    if [ -d "$p/android_cpp" ]; then
      mkdir -p "$plugins_cpp/$plugin_name"
      cp -R "$p/android_cpp/." "$plugins_cpp/$plugin_name/"
    fi
    if [ -d "$p/cpp" ]; then
      mkdir -p "$plugins_cpp/$plugin_name"
      cp -R "$p/cpp/." "$plugins_cpp/$plugin_name/"
    fi
  done
  IFS="$old_ifs"
}

weave_plugins_ios() {
  project_dir="$1"
  [ -z "$plugins" ] && return 0

  plugins_dst="$project_dir/ChengMobileApp/Plugins"
  mkdir -p "$plugins_dst"

  old_ifs="$IFS"
  IFS=','
  for p in $plugins; do
    p="$(printf '%s' "$p" | sed 's/^ *//;s/ *$//')"
    [ -z "$p" ] && continue
    [ ! -f "$p/mobile_plugin.toml" ] && continue
    plugin_name="$(basename "$p")"
    mkdir -p "$plugins_dst/$plugin_name"
    if [ -d "$p/ios" ]; then
      cp -R "$p/ios/." "$plugins_dst/$plugin_name/"
    fi
    if [ -d "$p/cheng" ]; then
      mkdir -p "$plugins_dst/$plugin_name/cheng"
      cp -R "$p/cheng/." "$plugins_dst/$plugin_name/cheng/"
    fi
  done
  IFS="$old_ifs"
}

weave_plugins_harmony() {
  project_dir="$1"
  [ -z "$plugins" ] && return 0

  plugins_dst="$project_dir/plugins"
  mkdir -p "$plugins_dst"

  old_ifs="$IFS"
  IFS=','
  for p in $plugins; do
    p="$(printf '%s' "$p" | sed 's/^ *//;s/ *$//')"
    [ -z "$p" ] && continue
    [ ! -f "$p/mobile_plugin.toml" ] && continue
    plugin_name="$(basename "$p")"
    mkdir -p "$plugins_dst/$plugin_name"
    if [ -d "$p/harmony" ]; then
      cp -R "$p/harmony/." "$plugins_dst/$plugin_name/"
    fi
    if [ -d "$p/cheng" ]; then
      mkdir -p "$plugins_dst/$plugin_name/cheng"
      cp -R "$p/cheng/." "$plugins_dst/$plugin_name/cheng/"
    fi
  done
  IFS="$old_ifs"
}

if [ "$with_android" -eq 1 ]; then
  android_project="$out/android_project"
  rm -rf "$android_project"
  cp -R "$mobile_root/android/project_template" "$android_project"

  android_cpp="$android_project/app/src/main/cpp"
  copy_common_cpp_sources "$android_cpp"

  cp "$mobile_root/android/cheng_mobile_host_android.c" "$android_cpp/cheng_mobile_host_android.c"
  cp "$mobile_root/android/cheng_mobile_host_android.h" "$android_cpp/cheng_mobile_host_android.h"
  cp "$mobile_root/android/cheng_mobile_android_jni.c" "$android_cpp/cheng_mobile_android_jni.c"
  cp "$mobile_root/android/cheng_mobile_android_ndk.c" "$android_cpp/cheng_mobile_android_ndk.c"
  cp "$mobile_root/android/cheng_mobile_android_gl.c" "$android_cpp/cheng_mobile_android_gl.c"
  cp "$mobile_root/android/cheng_mobile_android_gl.h" "$android_cpp/cheng_mobile_android_gl.h"
  cp "$mobile_root/android/cheng_gui_native_android.c" "$android_cpp/cheng_gui_native_android.c"
  cp "$mobile_root/android/stb_truetype.h" "$android_cpp/stb_truetype.h"

  # Optional Android host overrides from cheng-lang.
  mobile_android_override="$root/src/tooling/mobile/android"
  if [ -d "$mobile_android_override" ]; then
    [ -f "$mobile_android_override/cheng_mobile_host_android.c" ] && cp "$mobile_android_override/cheng_mobile_host_android.c" "$android_cpp/cheng_mobile_host_android.c"
    [ -f "$mobile_android_override/cheng_mobile_android_jni.c" ] && cp "$mobile_android_override/cheng_mobile_android_jni.c" "$android_cpp/cheng_mobile_android_jni.c"

    kotlin_dst="$android_project/app/src/main/kotlin/com/cheng/mobile"
    mkdir -p "$kotlin_dst"
    [ -f "$mobile_android_override/ChengActivity.kt" ] && cp "$mobile_android_override/ChengActivity.kt" "$kotlin_dst/ChengActivity.kt"
    [ -f "$mobile_android_override/ChengSurfaceView.kt" ] && cp "$mobile_android_override/ChengSurfaceView.kt" "$kotlin_dst/ChengSurfaceView.kt"
    [ -f "$mobile_android_override/AndroidHostServices.kt" ] && cp "$mobile_android_override/AndroidHostServices.kt" "$kotlin_dst/AndroidHostServices.kt"
  fi

  if [ "$with_asm_gui" -eq 1 ] && [ -d "$root/asm_gui" ]; then
    mkdir -p "$android_cpp/asm_gui"
    cp -R "$root/asm_gui/." "$android_cpp/asm_gui/"
  fi

  copy_assets_into "$assets" "$android_project/app/src/main/assets" "$in"
  weave_plugins_android "$android_project"
  echo "[mobile-export] android project: $android_project"
fi

if [ "$with_ios" -eq 1 ]; then
  ios_project="$out/ios_project"
  rm -rf "$ios_project"
  cp -R "$mobile_root/ios/project_template" "$ios_project"

  ios_src="$ios_project/ChengMobileApp"

  cp "$mobile_root/ios/ChengViewController.h" "$ios_src/ChengViewController.h"
  cp "$mobile_root/ios/ChengViewController.m" "$ios_src/ChengViewController.m"
  cp "$mobile_root/ios/cheng_mobile_ios_glue.h" "$ios_src/cheng_mobile_ios_glue.h"
  cp "$mobile_root/ios/cheng_mobile_ios_glue.m" "$ios_src/cheng_mobile_ios_glue.m"
  cp "$mobile_root/ios/cheng_mobile_host_ios.m" "$ios_src/cheng_mobile_host_ios.m"

  cp "$app_c" "$ios_src/cheng_mobile_app.c"
  cp "$runtime_c" "$ios_src/system_helpers.c"
  cp "$runtime_h" "$ios_src/system_helpers.h"
  if [ -f "$runtime_stb_image_h" ]; then
    cp "$runtime_stb_image_h" "$ios_src/stb_image.h"
  fi

  cp "$mobile_root/bridge/cheng_mobile_bridge.h" "$ios_src/cheng_mobile_bridge.h"
  cp "$mobile_root/bridge/cheng_mobile_host_api.c" "$ios_src/cheng_mobile_host_api.c"
  cp "$mobile_root/bridge/cheng_mobile_host_api.h" "$ios_src/cheng_mobile_host_api.h"
  cp "$mobile_root/bridge/cheng_mobile_host_core.c" "$ios_src/cheng_mobile_host_core.c"
  cp "$mobile_root/bridge/cheng_mobile_host_core.h" "$ios_src/cheng_mobile_host_core.h"

  copy_assets_into "$assets" "$ios_src/Assets" "$in"
  weave_plugins_ios "$ios_project"
  echo "[mobile-export] ios project: $ios_project"
fi

if [ "$with_harmony" -eq 1 ]; then
  harmony_project="$out/harmony_project"
  rm -rf "$harmony_project"
  mkdir -p "$harmony_project"

  cp -R "$mobile_root/harmony/arkts_template/." "$harmony_project/"

  harmony_native="$harmony_project/native"
  mkdir -p "$harmony_native"

  cp "$mobile_root/harmony/cheng_mobile_harmony_bridge.c" "$harmony_project/cheng_mobile_harmony_bridge.c"
  cp "$mobile_root/harmony/cheng_mobile_harmony_bridge.h" "$harmony_project/cheng_mobile_harmony_bridge.h"
  cp "$mobile_root/harmony/cheng_mobile_host_harmony.c" "$harmony_project/cheng_mobile_host_harmony.c"
  cp "$mobile_root/harmony/cheng_mobile_host_harmony.h" "$harmony_project/cheng_mobile_host_harmony.h"
  cp "$mobile_root/harmony/cheng_gui_native_harmony.c" "$harmony_project/cheng_gui_native_harmony.c"
  cp "$mobile_root/harmony/CMakeLists.txt" "$harmony_project/CMakeLists.txt"

  cp "$app_c" "$harmony_project/cheng_mobile_app.c"
  cp "$runtime_c" "$harmony_project/system_helpers.c"
  cp "$runtime_h" "$harmony_project/system_helpers.h"
  if [ -f "$runtime_stb_image_h" ]; then
    cp "$runtime_stb_image_h" "$harmony_project/stb_image.h"
  fi

  cp "$mobile_root/bridge/cheng_mobile_bridge.h" "$harmony_project/cheng_mobile_bridge.h"
  cp "$mobile_root/bridge/cheng_mobile_host_api.c" "$harmony_project/cheng_mobile_host_api.c"
  cp "$mobile_root/bridge/cheng_mobile_host_api.h" "$harmony_project/cheng_mobile_host_api.h"
  cp "$mobile_root/bridge/cheng_mobile_host_core.c" "$harmony_project/cheng_mobile_host_core.c"
  cp "$mobile_root/bridge/cheng_mobile_host_core.h" "$harmony_project/cheng_mobile_host_core.h"

  # Fix include path for exported native NAPI source.
  if [ -f "$harmony_native/cheng_mobile_harmony_napi.c" ]; then
    sed -i.bak 's#"../../cheng_mobile_harmony_bridge.h"#"../cheng_mobile_harmony_bridge.h"#g' "$harmony_native/cheng_mobile_harmony_napi.c"
    rm -f "$harmony_native/cheng_mobile_harmony_napi.c.bak"
  fi

  mkdir -p "$harmony_project/assets"
  copy_assets_into "$assets" "$harmony_project/assets" "$in"
  weave_plugins_harmony "$harmony_project"
  echo "[mobile-export] harmony project: $harmony_project"
fi

echo "[mobile-export] shim mode active (generated native demo stub from $in)"
echo "[mobile-export] out=$out"
