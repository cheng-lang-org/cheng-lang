#!/usr/bin/env sh
set -euo pipefail

usage() {
  cat <<'USAGE'
Usage:
  src/tooling/chengc.sh <file.cheng> [--name:<exeName>] [--jobs:<N>]
                        [--manifest:<path>] [--lock:<path>] [--registry:<path>]
                        [--package:<id>] [--channel:<edge|stable|lts>]
                        [--lock-format:<toml|yaml|json>] [--meta-out:<path>]
                        [--buildmeta:<path>]
                        [--backend:obj] [--emit-obj] [--obj-out:<path>]
                        [--abi:<v1|v2_noptr>]
                        [--linker:<self>]
                        [--target:<triple>]
                        [--pkg-cache:<dir>] [--skip-pkg] [--verify] [--ledger:<path>]
                        [--source-listen:<addr>] [--source-peer:<addr>]
                        [--mm:<orc|off>] [--orc|--off]

Notes:
  - Backend-only entrypoint (obj/exe).
  - Default `CHENG_MM=orc`.
  - `--jobs:<N>` forwards to `CHENG_BACKEND_JOBS`.
  - `--backend:obj` is the only supported backend.
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
  case "${CHENG_BACKEND_KEEP_EXE_OBJ:-0}" in
    1|true|TRUE|yes|YES|on|ON)
      return 0
      ;;
  esac
  cleanup_sidecar="${cleanup_out}.o"
  if [ -f "$cleanup_sidecar" ]; then
    rm -f "$cleanup_sidecar"
  fi
)

if [ "${1:-}" = "" ] || [ "${1:-}" = "--help" ] || [ "${1:-}" = "-h" ]; then
  usage
  exit 0
fi

in="$1"
shift || true

name=""
jobs=""
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

while [ "${1:-}" != "" ]; do
  case "$1" in
    --name:*) name="${1#--name:}" ;;
    --jobs:*) jobs="${1#--jobs:}" ;;
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
    --pkg-cache:*) pkg_cache="${1#--pkg-cache:}" ;;
    --skip-pkg) skip_pkg="1" ;;
    --verify) verify_meta="1" ;;
    --ledger:*) ledger="${1#--ledger:}" ;;
    --source-listen:*) source_listen="${1#--source-listen:}" ;;
    --source-peer:*) source_peer_args="$source_peer_args --source-peer:${1#--source-peer:}" ;;
    --orc) mm="orc" ;;
    --off) mm="off" ;;
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
  ""|v1|v2_noptr) ;;
  *)
    echo "[Error] invalid --abi:$abi (expected v1|v2_noptr)" 1>&2
    exit 2
    ;;
esac

if [ "$mm" = "" ] && [ -z "${CHENG_MM:-}" ]; then
  mm="orc"
fi
if [ -n "$mm" ]; then
  export CHENG_MM="$mm"
fi

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

if [ "$backend" = "" ]; then
  case "${CHENGC_DEFAULT_BACKEND:-}" in
    ""|auto|obj)
      backend="obj"
      ;;
    *)
      echo "[Error] invalid CHENGC_DEFAULT_BACKEND: ${CHENGC_DEFAULT_BACKEND} (expected auto|obj)" 1>&2
      exit 2
      ;;
  esac
fi

if [ "$backend" != "obj" ]; then
  echo "[Error] unsupported backend: $backend (only --backend:obj is supported)" 1>&2
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
  if [ -x "./cheng_pkg" ] && [ "src/tooling/cheng_pkg.cheng" -ot "./cheng_pkg" ]; then
    return
  fi
  echo "[pkg] build cheng_pkg"
  if ! sh src/tooling/chengb.sh src/tooling/cheng_pkg.cheng --frontend:stage1 --emit:exe --out:./cheng_pkg >/dev/null; then
    echo "[Error] failed to build cheng_pkg via backend obj/exe pipeline" 1>&2
    exit 2
  fi
  if [ ! -x "./cheng_pkg" ]; then
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
  ./cheng_pkg resolve --manifest:"$manifest" --registry:"$registry" --out:"$lock" --format:"$lock_format"
fi

if [ "$lock" != "" ] && [ "$skip_pkg" = "" ]; then
  if [ ! -f "$lock" ]; then
    echo "[Error] lock not found: $lock" 1>&2
    exit 2
  fi
  echo "[pkg] verify lock"
  ./cheng_pkg verify --lock:"$lock" --registry:"$registry"

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
      export CHENG_PKG_ROOTS="$pkg_roots"
    fi
  fi
fi

if [ "$lock" != "" ] && [ "$package_id" != "" ] && [ "$skip_pkg" = "" ]; then
  if [ "$meta_out" = "" ]; then
    meta_out="chengcache/${name}.pkgmeta.toml"
  fi
  echo "[pkg] build meta: $meta_out"
  ./cheng_pkg meta --lock:"$lock" --package:"$package_id" --channel:"$channel" --registry:"$registry" --out:"$meta_out" --format:"toml"
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

if [ "$buildmeta" = "" ] && { [ "$lock" != "" ] || [ "$meta_out" != "" ]; }; then
  buildmeta="chengcache/${name}.buildmeta.toml"
fi

frontend="${CHENG_BACKEND_FRONTEND:-stage1}"

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
    echo "output = \"$name\""
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

if [ "${CHENG_CLEAN_CHENG_LOCAL:-1}" = "1" ] && [ "${CHENG_TOOLING_CLEANUP_DEPTH:-0}" = "0" ]; then
  export CHENG_TOOLING_CLEANUP_DEPTH=1
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

if [ -n "${CHENG_BACKEND_DRIVER:-}" ]; then
  driver="$CHENG_BACKEND_DRIVER"
elif [ "${CHENG_BACKEND_DRIVER_DIRECT:-1}" != "0" ] && [ -x "./cheng" ] && driver_can_run "./cheng"; then
  driver="./cheng"
else
  driver="$(sh src/tooling/backend_driver_path.sh)"
fi

backend_emit="exe"
out_path="$name"
if [ "$emit_obj" != "" ]; then
  backend_emit="obj"
  if [ "$obj_out" != "" ]; then
    out_path="$obj_out"
  else
    out_path="chengcache/${name}.o"
  fi
  out_dir="$(dirname "$out_path")"
  if [ "$out_dir" != "" ] && [ ! -d "$out_dir" ]; then
    mkdir -p "$out_dir"
  fi
fi

backend_target="${CHENG_BACKEND_TARGET:-}"
if [ -n "$target_arg" ]; then
  backend_target="$target_arg"
fi
if [ -z "$backend_target" ]; then
  backend_target="$(sh src/tooling/detect_host_target.sh)"
fi

backend_linker="$linker"
if [ "$backend_linker" = "" ]; then
  backend_linker="${CHENG_BACKEND_LINKER:-}"
fi
if [ "$backend_linker" = "" ] && [ "$backend_emit" = "exe" ]; then
  backend_linker="self"
fi
case "$backend_linker" in
  ""|self) ;;
  *)
    echo "[Error] invalid --linker:$backend_linker (expected self)" 1>&2
    exit 2
    ;;
esac

backend_runtime_obj="${CHENG_BACKEND_RUNTIME_OBJ:-}"
backend_runtime_mode="${CHENG_BACKEND_RUNTIME:-}"
backend_runtime_src="src/std/system_helpers_backend.cheng"
case "${CHENG_BACKEND_ELF_PROFILE:-}/$backend_target" in
  nolibc/aarch64-*-linux-*|nolibc/arm64-*-linux-*)
    backend_runtime_src="src/std/system_helpers_backend_nolibc_linux_aarch64.cheng"
    ;;
esac
if [ "$backend_runtime_mode" = "" ] && [ "$backend_emit" = "exe" ] && [ "$backend_linker" = "self" ]; then
  backend_runtime_mode="cheng"
fi
if [ "$backend_emit" = "exe" ] && [ -z "$backend_runtime_obj" ] && [ "$backend_runtime_mode" = "cheng" ]; then
  safe_target="$(printf '%s' "$backend_target" | tr -c 'A-Za-z0-9._-' '_' | tr -s '_')"
  runtime_stem="$(basename "$backend_runtime_src" .cheng)"
  backend_runtime_obj="chengcache/${runtime_stem}.${safe_target}.o"
  if [ ! -f "$backend_runtime_src" ]; then
    echo "[Error] missing backend runtime source: $backend_runtime_src" 1>&2
    exit 2
  fi
  if [ ! -f "$backend_runtime_obj" ] || [ "$backend_runtime_src" -nt "$backend_runtime_obj" ]; then
    env \
      CHENG_BACKEND_ALLOW_NO_MAIN=1 \
      CHENG_BACKEND_WHOLE_PROGRAM=1 \
      CHENG_BACKEND_TARGET="$backend_target" \
      CHENG_BACKEND_EMIT=obj \
      CHENG_BACKEND_FRONTEND=mvp \
      CHENG_BACKEND_INPUT="$backend_runtime_src" \
      CHENG_BACKEND_OUTPUT="$backend_runtime_obj" \
      "$driver"
  fi
fi

runtime_env=""
if [ -n "$backend_runtime_obj" ]; then
  runtime_env="CHENG_BACKEND_RUNTIME_OBJ=$backend_runtime_obj"
fi

abi_env=""
abi_effective="$abi"
if [ -z "$abi_effective" ]; then
  abi_effective="${CHENG_ABI:-}"
fi
if [ -n "$abi_effective" ]; then
  abi_env="CHENG_ABI=$abi_effective"
fi
abi_std_noptr_env=""
abi_std_noptr_strict_env=""
if [ "$abi_effective" = "v2_noptr" ]; then
  abi_std_noptr_env="CHENG_STAGE1_STD_NO_POINTERS=1"
  abi_std_noptr_strict_env="CHENG_STAGE1_STD_NO_POINTERS_STRICT=1"
fi

link_env=""
if [ "$backend_linker" = "self" ] && [ "$backend_emit" = "exe" ]; then
  backend_no_runtime_c="${CHENG_BACKEND_NO_RUNTIME_C:-1}"
  link_env="CHENG_BACKEND_LINKER=self CHENG_BACKEND_NO_RUNTIME_C=$backend_no_runtime_c"
fi

backend_multi_env="${CHENG_BACKEND_MULTI:-0}"
backend_inc_env="${CHENG_BACKEND_INCREMENTAL:-1}"
backend_whole_program_env="${CHENG_BACKEND_WHOLE_PROGRAM:-1}"

if [ -n "$backend_target" ]; then
  # shellcheck disable=SC2086
  env $runtime_env $link_env $abi_env $abi_std_noptr_env $abi_std_noptr_strict_env \
    CHENG_BACKEND_TARGET="$backend_target" \
    CHENG_BACKEND_JOBS="$jobs" \
    CHENG_BACKEND_MULTI="$backend_multi_env" \
    CHENG_BACKEND_INCREMENTAL="$backend_inc_env" \
    CHENG_BACKEND_WHOLE_PROGRAM="$backend_whole_program_env" \
    CHENG_BACKEND_EMIT="$backend_emit" \
    CHENG_BACKEND_FRONTEND="$frontend" \
    CHENG_BACKEND_INPUT="$in" \
    CHENG_BACKEND_OUTPUT="$out_path" \
    "$driver"
else
  # shellcheck disable=SC2086
  env $runtime_env $link_env $abi_env $abi_std_noptr_env $abi_std_noptr_strict_env \
    CHENG_BACKEND_JOBS="$jobs" \
    CHENG_BACKEND_MULTI="$backend_multi_env" \
    CHENG_BACKEND_INCREMENTAL="$backend_inc_env" \
    CHENG_BACKEND_WHOLE_PROGRAM="$backend_whole_program_env" \
    CHENG_BACKEND_EMIT="$backend_emit" \
    CHENG_BACKEND_FRONTEND="$frontend" \
    CHENG_BACKEND_INPUT="$in" \
    CHENG_BACKEND_OUTPUT="$out_path" \
    "$driver"
fi

if [ "$backend_emit" = "exe" ]; then
  cleanup_backend_exe_sidecar "$out_path"
fi

if [ "$backend_emit" = "obj" ]; then
  echo "chengc obj ok: $out_path"
else
  echo "chengc obj ok: ./$name"
fi
