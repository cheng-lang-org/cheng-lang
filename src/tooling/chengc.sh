#!/usr/bin/env sh
. "$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)/env_prefix_bridge.sh"
set -euo pipefail

usage() {
  cat <<'USAGE'
Usage:
  src/tooling/chengc.sh <file.cheng> [--name:<exeName>] [--jobs:<N>]
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

Notes:
  - Backend-only entrypoint (exe only).
  - Default build track is dev (`BACKEND_BUILD_TRACK=dev`).
  - `--release` maps to `BACKEND_BUILD_TRACK=release` and defaults linker to system.
  - `--run` defaults to host runner (`--run:host`); use `--run:file` for legacy file-exec mode.
  - Default `MM=orc`.
  - Executable output for bare `--name:<bin>` defaults to `artifacts/chengc/<bin>`.
  - `--jobs:<N>` forwards to `BACKEND_JOBS`.
  - Legacy `chengb.sh` flags are accepted for compatibility.
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
    echo "[Error] invalid --abi:$abi (expected v2_noptr)" 1>&2
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
if [ "$emit_obj" != "" ] || [ "$backend" = "obj" ] || [ "$legacy_emit" = "obj" ] || [ "$obj_out" != "" ]; then
  echo "[Error] obj output path has been removed from production chain; use --emit:exe" 1>&2
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

if [ "$backend" != "exe" ]; then
  echo "[Error] unsupported backend: $backend (only --emit:exe is supported)" 1>&2
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
  if ! sh src/tooling/chengc.sh src/tooling/cheng_pkg.cheng --frontend:stage1 --emit:exe --out:"$pkg_tool" --skip-pkg >/dev/null; then
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
      pkg_roots="$(sh src/tooling/cheng_pkg_fetch.sh --lock:"$lock" --cache:"$pkg_cache" --print-roots \
        $source_listen_arg $source_peer_args)"
    else
      pkg_roots="$(sh src/tooling/cheng_pkg_fetch.sh --lock:"$lock" --print-roots \
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
if [ "$frontend" = "stage1" ] && [ "${STAGE1_SKIP_OWNERSHIP:-}" = "" ]; then
  export STAGE1_SKIP_OWNERSHIP=1
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
    sh src/tooling/verify_buildmeta.sh --buildmeta:"$buildmeta" --ledger:"$ledger"
  else
    sh src/tooling/verify_buildmeta.sh --buildmeta:"$buildmeta"
  fi
fi

if [ "${CLEAN_CHENG_LOCAL:-1}" = "1" ] && [ "${TOOLING_CLEANUP_DEPTH:-0}" = "0" ]; then
  export TOOLING_CLEANUP_DEPTH=1
  cleanup_backend_driver_on_exit() {
    status=$?
    set +e
    sh src/tooling/cleanup_cheng_local.sh
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
  driver="$(sh src/tooling/backend_driver_path.sh)"
fi
driver_exec="$driver"
driver_real_env=""
if [ "${BACKEND_DRIVER_USE_WRAPPER:-1}" != "0" ] && [ -x "src/tooling/backend_driver_exec.sh" ]; then
  case "$driver" in
    *"/backend_driver_exec.sh"|src/tooling/backend_driver_exec.sh)
      ;;
    *)
      driver_exec="src/tooling/backend_driver_exec.sh"
      driver_real_env="BACKEND_DRIVER_REAL=$driver"
      ;;
  esac
fi

backend_emit="exe"
out_path="$exe_out_path"
if [ "$legacy_out" != "" ]; then
  out_path="$legacy_out"
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
  backend_target="$(sh src/tooling/detect_host_target.sh)"
fi

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
if [ "$backend_emit" = "exe" ] && [ "$backend_linker" = "self" ] && [ "$backend_runtime_obj" = "" ]; then
  safe_target="$(printf '%s' "$backend_target" | tr -c 'A-Za-z0-9._-' '_' | tr -s '_')"
  backend_runtime_obj="chengcache/system_helpers.backend.cheng.${safe_target}.o"
fi
if [ "$backend_emit" = "exe" ] && [ "$backend_linker" = "self" ] && [ ! -f "$backend_runtime_obj" ]; then
  echo "[Error] missing self-link runtime object: $backend_runtime_obj" 1>&2
  echo "        set BACKEND_RUNTIME_OBJ or prepare runtime object for target=$backend_target" 1>&2
  exit 2
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
  echo "[Error] ABI only supports v2_noptr (got: $abi_effective)" 1>&2
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
      sh src/tooling/resolve_system_linker.sh)"
    resolver_kind="$(printf '%s\n' "$resolver_out" | sed -n 's/^kind=//p' | head -n 1)"
    resolver_ldflags="$(printf '%s\n' "$resolver_out" | sed -n 's/^ldflags=//p' | head -n 1)"
    if [ "$resolver_ldflags" != "" ]; then
      backend_ldflags_final="$(append_token "$backend_ldflags_final" "$resolver_ldflags")"
    fi
  fi
fi

backend_multi_env="${BACKEND_MULTI:-0}"
backend_inc_env="${BACKEND_INCREMENTAL:-1}"
backend_whole_program_env="${BACKEND_WHOLE_PROGRAM:-1}"
jobs_env=""
jobs_unset_env="-u BACKEND_JOBS"
if [ "$jobs_explicit" = "1" ]; then
  jobs_env="BACKEND_JOBS=$jobs"
  jobs_unset_env=""
fi
if [ "$legacy_multi" = "1" ]; then
  backend_multi_env="1"
fi

if [ -n "$backend_target" ]; then
  # shellcheck disable=SC2086
  env $jobs_unset_env $runtime_env $link_env $abi_env $abi_std_noptr_env $abi_std_noptr_strict_env $abi_no_ptr_non_c_abi_env $abi_no_ptr_non_c_abi_internal_env $jobs_env $driver_real_env \
    BACKEND_BUILD_TRACK="$build_track" \
    BACKEND_LDFLAGS="$backend_ldflags_final" \
    BACKEND_TARGET="$backend_target" \
    BACKEND_MULTI="$backend_multi_env" \
    BACKEND_INCREMENTAL="$backend_inc_env" \
    BACKEND_WHOLE_PROGRAM="$backend_whole_program_env" \
    BACKEND_EMIT="$backend_emit" \
    BACKEND_FRONTEND="$frontend" \
    BACKEND_INPUT="$in" \
    BACKEND_OUTPUT="$out_path" \
    "$driver_exec"
else
  # shellcheck disable=SC2086
  env $jobs_unset_env $runtime_env $link_env $abi_env $abi_std_noptr_env $abi_std_noptr_strict_env $abi_no_ptr_non_c_abi_env $abi_no_ptr_non_c_abi_internal_env $jobs_env $driver_real_env \
    BACKEND_BUILD_TRACK="$build_track" \
    BACKEND_LDFLAGS="$backend_ldflags_final" \
    BACKEND_MULTI="$backend_multi_env" \
    BACKEND_INCREMENTAL="$backend_inc_env" \
    BACKEND_WHOLE_PROGRAM="$backend_whole_program_env" \
    BACKEND_EMIT="$backend_emit" \
    BACKEND_FRONTEND="$frontend" \
    BACKEND_INPUT="$in" \
    BACKEND_OUTPUT="$out_path" \
    "$driver_exec"
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
  sh src/tooling/backend_host_runner.sh run-host --exe:"$out_path"
  exit $?
fi

echo "[chengc] run(file): $out_path" >&2
"$out_path"
