#!/usr/bin/env sh
:
set -euo pipefail

usage() {
  cat <<'USAGE'
Usage:
  ${TOOLING_SELF_BIN:-artifacts/tooling_cmd/cheng_tooling} chengc <file.cheng> [--name:<exeName>] [--jobs:<N>]
                        [--manifest:<path>] [--lock:<path>] [--registry:<path>]
                        [--package:<id>] [--channel:<edge|stable|lts>]
                        [--lock-format:<toml|yaml|json>] [--meta-out:<path>]
                        [--buildmeta:<path>]
                        [--abi:<v2_noptr>]
                        [--linker:<self|system>]
                        [--target:<triple>]
                        [--pkg-cache:<dir>] [--skip-pkg] [--verify] [--ledger:<path>]
                        [--source-listen:<addr>] [--source-peer:<addr>]
                        [--mm:<orc>] [--orc]
                        [--release]
                        [--emit:<exe>] [--frontend:<stage1>]
                        [--out:<path>] [--multi] [--pkg-roots:<p1[:p2:...]>]
                        [--run] [--run:file] [--run:host]
  ${TOOLING_SELF_BIN:-artifacts/tooling_cmd/cheng_tooling} chengc run <android|ios|harmony> <file.cheng> [mobile-run-options]

Notes:
  - Backend-only entrypoint (exe only).
  - Default build track is dev (`BACKEND_BUILD_TRACK=dev`).
  - `--release` maps to `BACKEND_BUILD_TRACK=release` and defaults linker to system.
  - Optional compile daemon: set `CHENGC_DAEMON=1` to route builds through `chengc_daemon`.
  - `--run` defaults to host runner (`--run:host`); use `--run:file` for legacy file-exec mode.
  - Default `MM=orc`.
  - Executable output for bare `--name:<bin>` defaults to `artifacts/chengc/<bin>`.
  - `--jobs:<N>` forwards to `BACKEND_JOBS`.
  - Legacy `chengb.sh` flags are accepted for compatibility.
  - `run <platform>` forwards to mobile host runners:
      android -> ${TOOLING_SELF_BIN:-artifacts/tooling_cmd/cheng_tooling} mobile_run_android
      ios     -> ${TOOLING_SELF_BIN:-artifacts/tooling_cmd/cheng_tooling} mobile_run_ios
      harmony -> ${TOOLING_SELF_BIN:-artifacts/tooling_cmd/cheng_tooling} mobile_run_harmony
USAGE
}

usage_run() {
  cat <<'USAGE'
Usage:
  ${TOOLING_SELF_BIN:-artifacts/tooling_cmd/cheng_tooling} chengc run android <file.cheng> [--name:<appName>] [--out:<dir>] [--assets:<dir>] [--plugins:<csv>] [--serial:<adbSerial>] [--native] [--app-arg:<k=v>]... [--app-args-json:<abs_path>] [--no-build] [--no-install] [--no-run] [--mm:<orc>] [--orc]
  ${TOOLING_SELF_BIN:-artifacts/tooling_cmd/cheng_tooling} chengc run ios <file.cheng> [--name:<appName>] [--out:<dir>] [--assets:<dir>] [--plugins:<csv>] [--mm:<orc>] [--orc]
  ${TOOLING_SELF_BIN:-artifacts/tooling_cmd/cheng_tooling} chengc run harmony <file.cheng> [--name:<appName>] [--out:<dir>] [--assets:<dir>] [--plugins:<csv>] [--mm:<orc>] [--orc]

Examples:
  ${TOOLING_SELF_BIN:-artifacts/tooling_cmd/cheng_tooling} chengc run android examples/mobile_hello.cheng
  ${TOOLING_SELF_BIN:-artifacts/tooling_cmd/cheng_tooling} chengc run ios examples/mobile_hello.cheng --name:demo_ios
  ${TOOLING_SELF_BIN:-artifacts/tooling_cmd/cheng_tooling} chengc run harmony examples/mobile_hello.cheng
USAGE
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
    ''|*[!0-9]*) jobs="4" ;;
  esac
  if [ "$jobs" -lt 1 ] 2>/dev/null; then
    jobs="4"
  fi
  printf '%s\n' "$jobs"
}

cleanup_backend_exe_sidecar() (
  cleanup_out="$1"
  case "${BACKEND_KEEP_EXE_OBJ:-0}" in
    1|true|TRUE|yes|YES|on|ON)
      return 0
      ;;
  esac
  cleanup_sidecar="${cleanup_out}.o"
  if [ -f "$cleanup_sidecar" ]; then
    rm -f "$cleanup_sidecar"
  fi
)

is_true_flag() {
  case "${1:-}" in
    1|true|TRUE|yes|YES|on|ON)
      return 0
      ;;
  esac
  return 1
}

has_fuse_ld_flag() {
  flags="$1"
  case "$flags" in
    *-fuse-ld=*) return 0 ;;
  esac
  return 1
}

resolve_named_exe_out() {
  name_in="$1"
  case "$name_in" in
    /*|*/*)
      printf '%s\n' "$name_in"
      return
      ;;
  esac
  printf 'artifacts/chengc/%s\n' "$name_in"
}

normalize_output_path() {
  raw="$1"
  case "$raw" in
    ''|-) printf '%s\n' "$raw" ;;
    /*|*/*|*\\*) printf '%s\n' "$raw" ;;
    *) printf 'artifacts/chengc/%s\n' "$raw" ;;
  esac
}

target_supports_self_linker() {
  t="$1"
  case "$t" in
    *darwin*)
      case "$t" in
        *arm64*|*aarch64*|*x86_64*|*amd64*) return 0 ;;
      esac
      return 1
      ;;
    *linux*|*android*)
      case "$t" in
        *arm64*|*aarch64*|*riscv64*) return 0 ;;
      esac
      return 1
      ;;
    *windows*|*msvc*)
      case "$t" in
        *arm64*|*aarch64*) return 0 ;;
      esac
      return 1
      ;;
  esac
  return 1
}

if [ "${1:-}" = "" ] || [ "${1:-}" = "--help" ] || [ "${1:-}" = "-h" ]; then
  usage
  exit 0
fi

if [ "${CHENGC_DAEMON:-0}" = "1" ] && [ "${CHENGC_DAEMON_ACTIVE:-0}" != "1" ] && [ "${1:-}" != "run" ]; then
  exec ${TOOLING_SELF_BIN:-artifacts/tooling_cmd/cheng_tooling} chengc_daemon run -- "$@"
fi

if [ "${1:-}" = "run" ]; then
  shift || true
  mobile_platform="${1:-}"
  if [ "$mobile_platform" = "" ] || [ "$mobile_platform" = "--help" ] || [ "$mobile_platform" = "-h" ]; then
    usage_run
    exit 0
  fi
  shift || true
  root="$(cd "$(dirname "$0")/../.." && pwd)"
  case "$mobile_platform" in
    android)
      exec "${TOOLING_SELF_BIN:-artifacts/tooling_cmd/cheng_tooling}" mobile_run_android "$@"
      ;;
    ios)
      exec "${TOOLING_SELF_BIN:-artifacts/tooling_cmd/cheng_tooling}" mobile_run_ios "$@"
      ;;
    harmony)
      exec "${TOOLING_SELF_BIN:-artifacts/tooling_cmd/cheng_tooling}" mobile_run_harmony "$@"
      ;;
    *)
      echo "[Error] unknown mobile platform: $mobile_platform" 1>&2
      usage_run 1>&2
      exit 2
      ;;
  esac
fi

in="$1"
shift || true

name=""
jobs=""
jobs_explicit="0"
manifest=""
lock=""
registry=""
package_id=""
channel="stable"
lock_format="toml"
meta_out=""
buildmeta=""
pkg_cache=""
skip_pkg=""
verify_meta=""
ledger=""
source_listen=""
source_peer_args=""
mm=""
emit_obj=""
obj_out=""
backend=""
linker=""
target_arg=""
abi=""
build_track_arg=""
legacy_emit=""
legacy_frontend=""
legacy_out=""
legacy_multi=""
legacy_pkg_roots=""
legacy_run=""
run_mode=""
pkg_tool="${PKG_TOOL:-chengcache/tools/cheng_pkg}"

while [ "${1:-}" != "" ]; do
  case "$1" in
    --name:*) name="${1#--name:}" ;;
    --jobs:*)
      jobs="${1#--jobs:}"
      jobs_explicit="1"
      ;;
    --manifest:*) manifest="${1#--manifest:}" ;;
    --lock:*) lock="${1#--lock:}" ;;
    --registry:*) registry="${1#--registry:}" ;;
    --package:*) package_id="${1#--package:}" ;;
    --channel:*) channel="${1#--channel:}" ;;
    --lock-format:*) lock_format="${1#--lock-format:}" ;;
    --meta-out:*) meta_out="${1#--meta-out:}" ;;
    --buildmeta:*) buildmeta="${1#--buildmeta:}" ;;
    --backend:obj) backend="obj" ;;
    --emit-obj)
      emit_obj="1"
      backend="obj"
      ;;
    --obj-out:*) obj_out="${1#--obj-out:}" ;;
    --abi:*) abi="${1#--abi:}" ;;
    --linker:*) linker="${1#--linker:}" ;;
    --target:*) target_arg="${1#--target:}" ;;
    --release) build_track_arg="release" ;;
    --emit:*) legacy_emit="${1#--emit:}" ;;
    --frontend:*) legacy_frontend="${1#--frontend:}" ;;
    --out:*) legacy_out="${1#--out:}" ;;
    --multi) legacy_multi="1" ;;
    --pkg-roots:*) legacy_pkg_roots="${1#--pkg-roots:}" ;;
    --run)
      legacy_run="1"
      if [ "$run_mode" = "" ]; then
        run_mode="host"
      fi
      ;;
    --run:file)
      legacy_run="1"
      run_mode="file"
      ;;
    --run:host)
      legacy_run="1"
      run_mode="host"
      ;;
    --pkg-cache:*) pkg_cache="${1#--pkg-cache:}" ;;
    --skip-pkg) skip_pkg="1" ;;
    --verify) verify_meta="1" ;;
    --ledger:*) ledger="${1#--ledger:}" ;;
    --source-listen:*) source_listen="${1#--source-listen:}" ;;
    --source-peer:*) source_peer_args="$source_peer_args --source-peer:${1#--source-peer:}" ;;
    --orc) mm="orc" ;;
    --mm:*) mm="${1#--mm:}" ;;
    *)
      echo "[Error] unknown arg: $1" 1>&2
      usage
      exit 2
      ;;
  esac
  shift || true
done

case "$abi" in
  ""|v2_noptr) ;;
  *)
    echo "[Error] invalid --abi:$abi (expected v2_noptr; enforced by ZRPC/零裸指针生产闭环规范)" 1>&2
    exit 2
    ;;
esac

case "$legacy_emit" in
  ""|obj|exe) ;;
  *)
    echo "[Error] invalid --emit:$legacy_emit (expected obj|exe)" 1>&2
    exit 2
    ;;
esac

case "$legacy_frontend" in
  ""|stage1) ;;
  *)
    echo "[Error] invalid --frontend:$legacy_frontend (expected stage1)" 1>&2
    exit 2
    ;;
esac

if [ "$legacy_emit" = "exe" ] && [ "$emit_obj" != "" ]; then
  echo "[Error] conflicting emit flags: --emit:exe with --emit-obj/--obj-out" 1>&2
  exit 2
fi
if [ "$legacy_emit" = "obj" ]; then
  emit_obj="1"
  backend="obj"
fi
obj_requested="0"
if [ "$emit_obj" != "" ] || [ "$backend" = "obj" ] || [ "$legacy_emit" = "obj" ] || [ "$obj_out" != "" ]; then
  obj_requested="1"
fi
allow_emit_obj="${BACKEND_INTERNAL_ALLOW_EMIT_OBJ:-${BACKEND_INTERNAL_ALLOW_EMIT_OBJ:-0}}"
if [ "$obj_requested" = "1" ] && [ "$allow_emit_obj" != "1" ]; then
  echo "[Error] obj output path has been removed from production chain; use --emit:exe" 1>&2
  echo "[Hint] set BACKEND_INTERNAL_ALLOW_EMIT_OBJ=1 for internal tooling only" 1>&2
  exit 2
fi
if [ "$obj_requested" = "1" ] && [ "$obj_out" = "" ]; then
  echo "[Error] --emit-obj requires --obj-out:<path>" 1>&2
  exit 2
fi

if [ "$mm" = "" ]; then
  mm="${MM:-orc}"
fi
case "$mm" in
  ""|orc)
    mm="orc"
    ;;
  *)
    echo "[Error] invalid --mm:$mm (only orc is supported)" 1>&2
    exit 2
    ;;
esac
export MM="$mm"

if [ "$jobs" = "" ]; then
  jobs="$(detect_jobs)"
fi

if [ ! -f "$in" ]; then
  echo "[Error] file not found: $in" 1>&2
  exit 2
fi

if [ "$name" = "" ]; then
  base="$(basename "$in")"
  name="${base%.cheng}"
fi
exe_out_path="$(resolve_named_exe_out "$name")"
buildmeta_output="$exe_out_path"
if [ "$legacy_out" != "" ]; then
  buildmeta_output="$legacy_out"
fi

if [ "$backend" = "" ]; then
  case "${CHENGC_DEFAULT_BACKEND:-}" in
    ""|auto|exe)
      backend="exe"
      ;;
    *)
      echo "[Error] invalid CHENGC_DEFAULT_BACKEND: ${CHENGC_DEFAULT_BACKEND} (expected auto|exe)" 1>&2
      exit 2
      ;;
  esac
fi

if [ "$backend" != "exe" ] && [ "$backend" != "obj" ]; then
  echo "[Error] unsupported backend: $backend (expected exe|obj)" 1>&2
  exit 2
fi
if [ "$backend" = "obj" ] && [ "$allow_emit_obj" != "1" ]; then
  echo "[Error] unsupported backend: obj (only --emit:exe is supported)" 1>&2
  echo "[Hint] set BACKEND_INTERNAL_ALLOW_EMIT_OBJ=1 for internal tooling only" 1>&2
  exit 2
fi

if [ "$manifest" != "" ] || [ "$lock" != "" ] || [ "$package_id" != "" ]; then
  if [ "$registry" = "" ]; then
    registry="build/cheng_registry/registry.jsonl"
  fi
fi

sha256_file() {
  if command -v shasum >/dev/null 2>&1; then
    shasum -a 256 "$1" | awk '{print $1}'
    return
  fi
  if command -v sha256sum >/dev/null 2>&1; then
    sha256sum "$1" | awk '{print $1}'
    return
  fi
  echo ""
}

toml_string() {
  key="$1"
  file="$2"
  if [ ! -f "$file" ]; then
    echo ""
    return
  fi
  sed -n "s/^[[:space:]]*$key[[:space:]]*=[[:space:]]*\"\\(.*\\)\"[[:space:]]*$/\\1/p" "$file" | head -n 1
}

toml_int() {
  key="$1"
  file="$2"
  if [ ! -f "$file" ]; then
    echo ""
    return
  fi
  sed -n "s/^[[:space:]]*$key[[:space:]]*=[[:space:]]*\\([0-9][0-9]*\\)[[:space:]]*$/\\1/p" "$file" | head -n 1
}

ensure_pkg_tool() {
  if [ -x "$pkg_tool" ] && [ "src/tooling/cheng_pkg.cheng" -ot "$pkg_tool" ]; then
    return
  fi
  pkg_tool_dir="$(dirname "$pkg_tool")"
  if [ "$pkg_tool_dir" != "" ] && [ ! -d "$pkg_tool_dir" ]; then
    mkdir -p "$pkg_tool_dir"
  fi
  echo "[pkg] build cheng_pkg"
  if ! ${TOOLING_SELF_BIN:-artifacts/tooling_cmd/cheng_tooling} chengc src/tooling/cheng_pkg.cheng --frontend:stage1 --emit:exe --out:"$pkg_tool" --skip-pkg >/dev/null; then
    echo "[Error] failed to build cheng_pkg via backend obj/exe pipeline" 1>&2
    exit 2
  fi
  if [ ! -x "$pkg_tool" ]; then
    echo "[Error] missing cheng_pkg executable after build" 1>&2
    exit 2
  fi
}

if [ "$skip_pkg" = "" ]; then
  if [ "$manifest" != "" ] || [ "$lock" != "" ] || [ "$package_id" != "" ]; then
    ensure_pkg_tool
  fi
fi

if [ "$manifest" != "" ] && [ "$skip_pkg" = "" ]; then
  if [ ! -f "$manifest" ]; then
    echo "[Error] manifest not found: $manifest" 1>&2
    exit 2
  fi
  if [ "$lock" = "" ]; then
    lock="chengcache/${name}.lock.${lock_format}"
  fi
  echo "[pkg] resolve lock: $lock"
  "$pkg_tool" resolve --manifest:"$manifest" --registry:"$registry" --out:"$lock" --format:"$lock_format"
fi

if [ "$lock" != "" ] && [ "$skip_pkg" = "" ]; then
  if [ ! -f "$lock" ]; then
    echo "[Error] lock not found: $lock" 1>&2
    exit 2
  fi
  echo "[pkg] verify lock"
  "$pkg_tool" verify --lock:"$lock" --registry:"$registry"

  if [ -f "src/tooling/cheng_pkg_fetch.sh" ]; then
    source_listen_arg=""
    if [ "$source_listen" != "" ]; then
      source_listen_arg="--source-listen:$source_listen"
    fi
    if [ "$pkg_cache" != "" ]; then
      pkg_roots="$(${TOOLING_SELF_BIN:-artifacts/tooling_cmd/cheng_tooling} cheng_pkg_fetch --lock:"$lock" --cache:"$pkg_cache" --print-roots \
        $source_listen_arg $source_peer_args)"
    else
      pkg_roots="$(${TOOLING_SELF_BIN:-artifacts/tooling_cmd/cheng_tooling} cheng_pkg_fetch --lock:"$lock" --print-roots \
        $source_listen_arg $source_peer_args)"
    fi
    if [ "$pkg_roots" != "" ]; then
      export PKG_ROOTS="$pkg_roots"
    fi
  fi
fi

if [ "$lock" != "" ] && [ "$package_id" != "" ] && [ "$skip_pkg" = "" ]; then
  if [ "$meta_out" = "" ]; then
    meta_out="chengcache/${name}.pkgmeta.toml"
  fi
  echo "[pkg] build meta: $meta_out"
  "$pkg_tool" meta --lock:"$lock" --package:"$package_id" --channel:"$channel" --registry:"$registry" --out:"$meta_out" --format:"toml"
fi

if [ "$skip_pkg" != "" ]; then
  if [ "$manifest" != "" ] && [ "$lock" = "" ]; then
    echo "[Error] --skip-pkg requires --lock when --manifest is set" 1>&2
    exit 2
  fi
  if [ "$lock" != "" ] && [ ! -f "$lock" ]; then
    echo "[Error] lock not found: $lock" 1>&2
    exit 2
  fi
  if [ "$package_id" != "" ]; then
    if [ "$meta_out" = "" ]; then
      meta_out="chengcache/${name}.pkgmeta.toml"
    fi
    if [ ! -f "$meta_out" ]; then
      echo "[Error] pkgmeta not found: $meta_out" 1>&2
      exit 2
    fi
  fi
fi

if [ "$legacy_pkg_roots" != "" ]; then
  export PKG_ROOTS="$legacy_pkg_roots"
fi

if [ "$buildmeta" = "" ] && { [ "$lock" != "" ] || [ "$meta_out" != "" ]; }; then
  buildmeta="chengcache/${name}.buildmeta.toml"
fi

frontend="${BACKEND_FRONTEND:-stage1}"
if [ "$legacy_frontend" != "" ]; then
  frontend="$legacy_frontend"
fi
if [ "$frontend" = "stage1" ] && [ "${STAGE1_OWNERSHIP_FIXED_0:-}" = "" ]; then
  export STAGE1_OWNERSHIP_FIXED_0=0
fi

build_track="${BACKEND_BUILD_TRACK:-}"
if [ "$build_track_arg" = "release" ]; then
  build_track="release"
fi
if [ "$build_track" = "" ]; then
  build_track="dev"
fi
case "$build_track" in
  dev|release) ;;
  *)
    echo "[Error] invalid BACKEND_BUILD_TRACK: $build_track (expected dev|release)" 1>&2
    exit 2
    ;;
esac

if [ "$buildmeta" != "" ]; then
  buildmeta_dir="$(dirname "$buildmeta")"
  if [ "$buildmeta_dir" != "" ] && [ ! -d "$buildmeta_dir" ]; then
    mkdir -p "$buildmeta_dir"
  fi
  lock_sha=""
  if [ "$lock" != "" ] && [ -f "$lock" ]; then
    lock_sha="$(sha256_file "$lock")"
  fi
  meta_sha=""
  if [ "$meta_out" != "" ] && [ -f "$meta_out" ]; then
    meta_sha="$(sha256_file "$meta_out")"
  fi
  snap_author="$(toml_string author_id "$meta_out")"
  snap_cid="$(toml_string cid "$meta_out")"
  snap_sig="$(toml_string signature "$meta_out")"
  snap_pubkey="$(toml_string pub_key "$meta_out")"
  snap_epoch="$(toml_int epoch "$meta_out")"
  ts="$(date -u +%Y-%m-%dT%H:%M:%SZ)"
  {
    echo "[build]"
    echo "source = \"$in\""
    echo "frontend = \"$frontend\""
    echo "output = \"$buildmeta_output\""
    echo "created_ts = \"$ts\""
    if [ "$manifest" != "" ]; then
      echo "manifest = \"$manifest\""
    fi
    if [ "$registry" != "" ]; then
      echo "registry = \"$registry\""
    fi
    if [ "$package_id" != "" ]; then
      echo "package_id = \"$package_id\""
    fi
    if [ "$channel" != "" ]; then
      echo "channel = \"$channel\""
    fi
    if [ "$lock" != "" ]; then
      echo ""
      echo "[lock]"
      echo "path = \"$lock\""
      if [ "$lock_format" != "" ]; then
        echo "format = \"$lock_format\""
      fi
      if [ "$lock_sha" != "" ]; then
        echo "sha256 = \"$lock_sha\""
      fi
    fi
    if [ "$meta_out" != "" ]; then
      echo ""
      echo "[pkgmeta]"
      echo "path = \"$meta_out\""
      if [ "$meta_sha" != "" ]; then
        echo "sha256 = \"$meta_sha\""
      fi
    fi
    if [ "$snap_cid" != "" ] || [ "$snap_author" != "" ] || [ "$snap_sig" != "" ]; then
      echo ""
      echo "[snapshot]"
      if [ "$snap_epoch" != "" ]; then
        echo "epoch = $snap_epoch"
      fi
      if [ "$snap_cid" != "" ]; then
        echo "cid = \"$snap_cid\""
      fi
      if [ "$snap_author" != "" ]; then
        echo "author_id = \"$snap_author\""
      fi
      if [ "$snap_pubkey" != "" ]; then
        echo "pub_key = \"$snap_pubkey\""
      fi
      if [ "$snap_sig" != "" ]; then
        echo "signature = \"$snap_sig\""
      fi
    fi
  } >"$buildmeta"
fi

if [ "$verify_meta" != "" ]; then
  if [ "$buildmeta" = "" ]; then
    echo "[Error] --verify requires --buildmeta (or lock/meta inputs)" 1>&2
    exit 2
  fi
  if [ "$ledger" != "" ]; then
    ${TOOLING_SELF_BIN:-artifacts/tooling_cmd/cheng_tooling} verify_buildmeta --buildmeta:"$buildmeta" --ledger:"$ledger"
  else
    ${TOOLING_SELF_BIN:-artifacts/tooling_cmd/cheng_tooling} verify_buildmeta --buildmeta:"$buildmeta"
  fi
fi

if [ "${CLEAN_CHENG_LOCAL:-1}" = "1" ] && [ "${TOOLING_CLEANUP_DEPTH:-0}" = "0" ]; then
  export TOOLING_CLEANUP_DEPTH=1
  cleanup_backend_driver_on_exit() {
    status=$?
    set +e
    ${TOOLING_SELF_BIN:-artifacts/tooling_cmd/cheng_tooling} cleanup_cheng_local
    exit "$status"
  }
  trap cleanup_backend_driver_on_exit EXIT
fi

driver_can_run() {
  bin="$1"
  if [ ! -x "$bin" ]; then
    return 1
  fi
  set +e
  "$bin" --help >/dev/null 2>&1
  status=$?
  set -e
  case "$status" in
    0|1|2) return 0 ;;
  esac
  return 1
}

if [ -n "${BACKEND_DRIVER:-}" ]; then
  driver="$BACKEND_DRIVER"
elif [ "${BACKEND_DRIVER_DIRECT:-1}" != "0" ] && [ -x "./artifacts/backend_driver/cheng" ] && driver_can_run "./artifacts/backend_driver/cheng"; then
  driver="./artifacts/backend_driver/cheng"
elif [ "${BACKEND_DRIVER_DIRECT:-1}" != "0" ] && [ -x "./cheng" ] && driver_can_run "./cheng"; then
  driver="./cheng"
else
  driver="$(${TOOLING_SELF_BIN:-artifacts/tooling_cmd/cheng_tooling} backend_driver_path)"
fi
driver_exec="$driver"
driver_real_env=""
if [ "${BACKEND_DRIVER_USE_WRAPPER:-0}" = "1" ]; then
  echo "[chengc] BACKEND_DRIVER_USE_WRAPPER=1 is deprecated; forcing direct driver path" 1>&2
fi

backend_emit="exe"
out_path="$exe_out_path"
if [ "$legacy_out" != "" ]; then
  out_path="$legacy_out"
fi
if [ "$obj_requested" = "1" ]; then
  backend_emit="obj"
  if [ "$obj_out" != "" ]; then
    out_path="$obj_out"
  elif [ "$legacy_out" != "" ]; then
    out_path="$legacy_out"
  else
    out_path="${name}.o"
  fi
fi
out_path="$(normalize_output_path "$out_path")"
out_dir="$(dirname "$out_path")"
if [ "$out_dir" != "" ] && [ ! -d "$out_dir" ]; then
  mkdir -p "$out_dir"
fi

backend_target="${BACKEND_TARGET:-}"
if [ -n "$target_arg" ]; then
  backend_target="$target_arg"
fi
if [ -z "$backend_target" ]; then
  backend_target="$(${TOOLING_SELF_BIN:-artifacts/tooling_cmd/cheng_tooling} detect_host_target)"
fi
case "$backend_target" in
  ""|auto|native|host)
    backend_target="$(${TOOLING_SELF_BIN:-artifacts/tooling_cmd/cheng_tooling} detect_host_target)"
    ;;
  darwin_arm64|darwin_aarch64)
    backend_target="arm64-apple-darwin"
    ;;
  darwin_x86_64|darwin_amd64)
    backend_target="x86_64-apple-darwin"
    ;;
  linux_arm64|linux_aarch64)
    backend_target="aarch64-unknown-linux-gnu"
    ;;
  linux_x86_64|linux_amd64)
    backend_target="x86_64-unknown-linux-gnu"
    ;;
  linux_riscv64)
    backend_target="riscv64-unknown-linux-gnu"
    ;;
  windows_arm64|windows_aarch64)
    backend_target="aarch64-pc-windows-msvc"
    ;;
  windows_x86_64|windows_amd64)
    backend_target="x86_64-pc-windows-msvc"
    ;;
esac

backend_linker="$linker"
if [ "$backend_linker" = "" ]; then
  backend_linker="${BACKEND_LINKER:-}"
fi
if [ "$backend_linker" = "" ] && [ "$backend_emit" = "exe" ]; then
  if [ "$build_track" = "release" ]; then
    backend_linker="system"
  else
    if target_supports_self_linker "$backend_target"; then
      backend_linker="self"
    else
      backend_linker="system"
    fi
  fi
fi
case "$backend_linker" in
  ""|self|system) ;;
  *)
    echo "[Error] invalid --linker:$backend_linker (expected self|system)" 1>&2
    exit 2
    ;;
esac

backend_runtime_obj="${BACKEND_RUNTIME_OBJ:-}"

runtime_nm_has_sym() {
  sym="$1"
  printf '%s\n' "$nm_out" | awk '{print $NF}' | sed 's/^_//' | grep -Fxq "$sym"
}

runtime_has_core_symbols() {
  obj="$1"
  [ -f "$obj" ] || return 1
  if ! command -v nm >/dev/null 2>&1; then
    return 0
  fi
  nm_out="$(nm -g "$obj" 2>/dev/null || true)"
  [ "$nm_out" != "" ] || return 1
  runtime_nm_has_sym cheng_strlen &&
  runtime_nm_has_sym cheng_memcpy &&
  runtime_nm_has_sym cheng_memset &&
  runtime_nm_has_sym cheng_malloc &&
  runtime_nm_has_sym cheng_free &&
  runtime_nm_has_sym cheng_mem_retain &&
  runtime_nm_has_sym cheng_mem_release &&
  runtime_nm_has_sym cheng_seq_get &&
  runtime_nm_has_sym cheng_seq_set &&
  runtime_nm_has_sym cheng_strcmp &&
  runtime_nm_has_sym cheng_f32_bits_to_i64 &&
  runtime_nm_has_sym cheng_f64_bits_to_i64
}

resolve_runtime_obj() {
  if [ "$backend_runtime_obj" != "" ]; then
    printf '%s\n' "$backend_runtime_obj"
    return 0
  fi
  candidates="
chengcache/runtime_selflink/system_helpers.backend.combined.${backend_target}.o
artifacts/backend_mm/system_helpers.backend.combined.${backend_target}.o
chengcache/system_helpers.backend.cheng.${backend_target}.o
artifacts/backend_selfhost_self_obj/stage1.native.runtime.dedup.o
artifacts/backend_selfhost_self_obj/system_helpers.backend.cheng.stage1shim.o
chengcache/system_helpers.backend.cheng.o
artifacts/backend_selfhost_self_obj/system_helpers.backend.cheng.o
"
  for cand in $candidates; do
    [ "$cand" != "" ] || continue
    if [ -f "$cand" ] && runtime_has_core_symbols "$cand"; then
      printf '%s\n' "$cand"
      return 0
    fi
  done
  for cand in $candidates; do
    [ "$cand" != "" ] || continue
    if [ -f "$cand" ]; then
      printf '%s\n' "$cand"
      return 0
    fi
  done
  return 1
}

if [ "$backend_emit" = "exe" ] && [ "$backend_linker" = "self" ]; then
  backend_runtime_obj="$(resolve_runtime_obj || true)"
  if [ "$backend_runtime_obj" = "" ] || [ ! -f "$backend_runtime_obj" ]; then
    echo "[Error] missing self-link runtime object for target=$backend_target" 1>&2
    echo "        set BACKEND_RUNTIME_OBJ or prepare chengcache/runtime_selflink/system_helpers.backend.combined.${backend_target}.o" 1>&2
    exit 2
  fi
  if ! runtime_has_core_symbols "$backend_runtime_obj"; then
    echo "[Warn] self-link runtime object may be incomplete: $backend_runtime_obj" 1>&2
  fi
fi
runtime_env=""
if [ "$backend_linker" = "self" ] && [ "$backend_runtime_obj" != "" ]; then
  runtime_env="BACKEND_RUNTIME_OBJ=$backend_runtime_obj"
fi

abi_env=""
abi_effective="$abi"
if [ -z "$abi_effective" ]; then
  abi_effective="${ABI:-}"
fi
if [ -z "$abi_effective" ]; then
  abi_effective="v2_noptr"
fi
if [ "$abi_effective" != "v2_noptr" ]; then
  echo "[Error] ABI only supports v2_noptr (got: $abi_effective; enforced by ZRPC/零裸指针生产闭环规范)" 1>&2
  exit 2
fi
abi_env="ABI=$abi_effective"
abi_std_noptr_env=""
abi_std_noptr_strict_env=""
abi_no_ptr_non_c_abi_env=""
abi_no_ptr_non_c_abi_internal_env=""
if [ "$abi_effective" = "v2_noptr" ]; then
  if [ -z "${STAGE1_STD_NO_POINTERS+x}" ]; then
    abi_std_noptr_env="STAGE1_STD_NO_POINTERS=1"
  else
    abi_std_noptr_env="STAGE1_STD_NO_POINTERS=$STAGE1_STD_NO_POINTERS"
  fi
  if [ -z "${STAGE1_STD_NO_POINTERS_STRICT+x}" ]; then
    abi_std_noptr_strict_env="STAGE1_STD_NO_POINTERS_STRICT=0"
  else
    abi_std_noptr_strict_env="STAGE1_STD_NO_POINTERS_STRICT=$STAGE1_STD_NO_POINTERS_STRICT"
  fi
  if [ -z "${STAGE1_NO_POINTERS_NON_C_ABI+x}" ]; then
    abi_no_ptr_non_c_abi_env="STAGE1_NO_POINTERS_NON_C_ABI=1"
  else
    abi_no_ptr_non_c_abi_env="STAGE1_NO_POINTERS_NON_C_ABI=$STAGE1_NO_POINTERS_NON_C_ABI"
  fi
  if [ -z "${STAGE1_NO_POINTERS_NON_C_ABI_INTERNAL+x}" ]; then
    abi_no_ptr_non_c_abi_internal_env="STAGE1_NO_POINTERS_NON_C_ABI_INTERNAL=1"
  else
    abi_no_ptr_non_c_abi_internal_env="STAGE1_NO_POINTERS_NON_C_ABI_INTERNAL=$STAGE1_NO_POINTERS_NON_C_ABI_INTERNAL"
  fi
fi

link_env=""
if [ "$backend_emit" = "exe" ]; then
  if [ "$backend_linker" = "self" ]; then
    link_env="BACKEND_LINKER=self BACKEND_NO_RUNTIME_C=1"
  else
    if [ "${BACKEND_NO_RUNTIME_C+x}" = "x" ] && [ "${BACKEND_NO_RUNTIME_C}" != "" ]; then
      link_env="BACKEND_LINKER=system BACKEND_NO_RUNTIME_C=$BACKEND_NO_RUNTIME_C"
    elif [ "$build_track" = "release" ]; then
      link_env="BACKEND_LINKER=system BACKEND_NO_RUNTIME_C=0"
    else
      link_env="BACKEND_LINKER=system"
    fi
  fi
fi

backend_ldflags_final="${BACKEND_LDFLAGS:-}"
if [ "$backend_emit" = "exe" ] && [ "$backend_linker" = "system" ]; then
  if [ "${BACKEND_LD:-}" = "" ] && ! has_fuse_ld_flag "$backend_ldflags_final"; then
    resolver_out="$(env \
      BACKEND_SYSTEM_LINKER_PRIORITY="${BACKEND_SYSTEM_LINKER_PRIORITY:-mold,lld,default}" \
      ${TOOLING_SELF_BIN:-artifacts/tooling_cmd/cheng_tooling} resolve_system_linker)"
    resolver_kind="$(printf '%s\n' "$resolver_out" | sed -n 's/^kind=//p' | head -n 1)"
    resolver_ldflags="$(printf '%s\n' "$resolver_out" | sed -n 's/^ldflags=//p' | head -n 1)"
    if [ "$resolver_ldflags" != "" ]; then
      backend_ldflags_final="$(append_token "$backend_ldflags_final" "$resolver_ldflags")"
    fi
  fi
fi

backend_multi_env="${BACKEND_MULTI:-}"
if [ "$backend_multi_env" = "" ]; then
  if [ "$build_track" = "release" ]; then
    backend_multi_env="${CHENGC_RELEASE_MULTI_DEFAULT:-0}"
  else
    backend_multi_env="${CHENGC_DEV_MULTI_DEFAULT:-1}"
  fi
fi
backend_inc_env="${BACKEND_INCREMENTAL:-1}"
backend_multi_module_cache_env="${BACKEND_MULTI_MODULE_CACHE:-${BACKEND_MULTI_MODULE_CACHE:-0}}"
backend_module_cache_unstable_allow_env="${BACKEND_MODULE_CACHE_UNSTABLE_ALLOW:-${BACKEND_MODULE_CACHE_UNSTABLE_ALLOW:-0}}"
chengc_allow_unstable_multi_module_cache_env="${CHENGC_ALLOW_UNSTABLE_MULTI_MODULE_CACHE:-0}"
if is_true_flag "$backend_multi_module_cache_env"; then
  if ! is_true_flag "$chengc_allow_unstable_multi_module_cache_env" || ! is_true_flag "$backend_module_cache_unstable_allow_env"; then
    echo "[chengc] warn: BACKEND_MULTI_MODULE_CACHE is unstable and disabled by default; require CHENGC_ALLOW_UNSTABLE_MULTI_MODULE_CACHE=1 and BACKEND_MODULE_CACHE_UNSTABLE_ALLOW=1" 1>&2
    backend_multi_module_cache_env="0"
    backend_module_cache_unstable_allow_env="0"
  fi
fi
backend_multi_force_env="${BACKEND_MULTI_FORCE:-0}"
jobs_env=""
jobs_unset_env="-u BACKEND_JOBS"
module_cache_unset_env="-u BACKEND_MODULE_CACHE"
multi_module_cache_unset_env="-u BACKEND_MULTI_MODULE_CACHE"
if [ "$jobs_explicit" = "1" ]; then
  jobs_env="BACKEND_JOBS=$jobs"
  jobs_unset_env=""
fi
if [ "$legacy_multi" = "1" ]; then
  backend_multi_env="1"
  backend_multi_force_env="1"
fi

backend_module_cache_env="${BACKEND_MODULE_CACHE:-${BACKEND_MODULE_CACHE:-}}"
module_cache_env=""
if [ "$backend_module_cache_env" != "" ]; then
  module_cache_env="BACKEND_MODULE_CACHE=$backend_module_cache_env"
fi
backend_cstring_lowering_env="${BACKEND_CSTRING_LOWERING_REMOVED:-${BACKEND_CSTRING_LOWERING_REMOVED:-}}"
if [ "$backend_cstring_lowering_env" = "" ]; then
  backend_cstring_lowering_env="0"
  if [ "$backend_linker" = "system" ]; then
    no_runtime_c_effective="${BACKEND_NO_RUNTIME_C:-}"
    if [ "$no_runtime_c_effective" = "" ] && [ "$build_track" = "release" ]; then
      no_runtime_c_effective="0"
    fi
    case "$no_runtime_c_effective" in
      0|false|FALSE|off|OFF|no|NO)
        backend_cstring_lowering_env="1"
        ;;
    esac
  fi
fi

run_backend_driver() {
  multi_now="$1"
  multi_force_now="$2"
  if [ -n "$backend_target" ]; then
    # shellcheck disable=SC2086
    env $jobs_unset_env $module_cache_unset_env $multi_module_cache_unset_env $runtime_env $link_env $abi_env $abi_std_noptr_env $abi_std_noptr_strict_env $abi_no_ptr_non_c_abi_env $abi_no_ptr_non_c_abi_internal_env $jobs_env $module_cache_env $driver_real_env \
      BACKEND_BUILD_TRACK="$build_track" \
      BACKEND_LDFLAGS="$backend_ldflags_final" \
      BACKEND_TARGET="$backend_target" \
      BACKEND_MULTI="$multi_now" \
      BACKEND_MULTI_FORCE="$multi_force_now" \
      BACKEND_MULTI_MODULE_CACHE="$backend_multi_module_cache_env" \
      BACKEND_MODULE_CACHE_UNSTABLE_ALLOW="$backend_module_cache_unstable_allow_env" \
      BACKEND_INCREMENTAL="$backend_inc_env" \
      BACKEND_CSTRING_LOWERING_REMOVED="$backend_cstring_lowering_env" \
      BACKEND_EMIT="$backend_emit" \
      BACKEND_FRONTEND="$frontend" \
      BACKEND_INPUT="$in" \
      BACKEND_OUTPUT="$out_path" \
      "$driver_exec"
    return
  fi
  # shellcheck disable=SC2086
  env $jobs_unset_env $module_cache_unset_env $multi_module_cache_unset_env $runtime_env $link_env $abi_env $abi_std_noptr_env $abi_std_noptr_strict_env $abi_no_ptr_non_c_abi_env $abi_no_ptr_non_c_abi_internal_env $jobs_env $module_cache_env $driver_real_env \
    BACKEND_BUILD_TRACK="$build_track" \
    BACKEND_LDFLAGS="$backend_ldflags_final" \
    BACKEND_MULTI="$multi_now" \
    BACKEND_MULTI_FORCE="$multi_force_now" \
    BACKEND_MULTI_MODULE_CACHE="$backend_multi_module_cache_env" \
    BACKEND_MODULE_CACHE_UNSTABLE_ALLOW="$backend_module_cache_unstable_allow_env" \
    BACKEND_INCREMENTAL="$backend_inc_env" \
    BACKEND_CSTRING_LOWERING_REMOVED="$backend_cstring_lowering_env" \
    BACKEND_EMIT="$backend_emit" \
    BACKEND_FRONTEND="$frontend" \
    BACKEND_INPUT="$in" \
    BACKEND_OUTPUT="$out_path" \
    "$driver_exec"
}

set +e
run_backend_driver "$backend_multi_env" "$backend_multi_force_env"
build_status="$?"
set -e
if [ "$build_status" -ne 0 ] && [ "$backend_multi_env" = "1" ] && [ "${CHENGC_MULTI_RETRY_SERIAL:-1}" = "1" ]; then
  echo "[chengc] warn: parallel compile failed (status=$build_status), retry serial once" 1>&2
  set +e
  run_backend_driver "0" "0"
  build_status="$?"
  set -e
fi
if [ "$build_status" -ne 0 ]; then
  exit "$build_status"
fi

if [ "$backend_emit" = "exe" ]; then
  cleanup_backend_exe_sidecar "$out_path"
fi

if [ "$backend_emit" = "obj" ]; then
  echo "chengc obj ok: $out_path"
else
  echo "chengc exe ok: $out_path"
fi

if [ "$legacy_run" != "1" ] || [ "$backend_emit" != "exe" ]; then
  exit 0
fi
if [ "$run_mode" = "" ]; then
  run_mode="host"
fi

case "$backend_target" in
  *android*)
    if [ "$run_mode" = "host" ]; then
      echo "[chengc] run(android) does not support host runner yet, fallback to file mode" >&2
    fi
    if ! command -v adb >/dev/null 2>&1; then
      echo "[chengc] missing tool: adb (required for --run on android targets)" 1>&2
      exit 2
    fi
    serial="${ANDROID_SERIAL:-}"
    if [ -z "$serial" ]; then
      serial="$(adb devices | awk 'NR>1 && $2 == "device" {print $1; exit}')"
    fi
    if [ -z "$serial" ]; then
      echo "[chengc] no Android device/emulator detected (adb devices)" 1>&2
      exit 2
    fi
    remote_dir="/data/local/tmp/chengc"
    remote_path="$remote_dir/$(basename "$out_path")"
    adb -s "$serial" shell "mkdir -p '$remote_dir'" >/dev/null
    adb -s "$serial" push "$out_path" "$remote_path" >/dev/null
    adb -s "$serial" shell "chmod 755 '$remote_path'" >/dev/null
    echo "[chengc] run(android): $remote_path ($serial)" >&2
    adb -s "$serial" shell "$remote_path"
    exit $?
    ;;
esac

if [ "$run_mode" = "host" ]; then
  echo "[chengc] run(host-runner): $out_path" >&2
  ${TOOLING_SELF_BIN:-artifacts/tooling_cmd/cheng_tooling} backend_host_runner run-host --exe:"$out_path"
  exit $?
fi

echo "[chengc] run(file): $out_path" >&2
"$out_path"
