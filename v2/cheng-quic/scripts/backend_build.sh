#!/usr/bin/env bash
set -euo pipefail

REPO_ROOT="$(cd "$(dirname "$0")/.." && pwd)"

resolve_tooling_bin() {
  if [ -n "${CHENG_TOOLING_BIN:-}" ] && [ -x "${CHENG_TOOLING_BIN}" ]; then
    printf '%s\n' "${CHENG_TOOLING_BIN}"
    return 0
  fi
  if [ -x "/Users/lbcheng/cheng-lang/artifacts/tooling_cmd/cheng_tooling" ]; then
    printf '%s\n' "/Users/lbcheng/cheng-lang/artifacts/tooling_cmd/cheng_tooling"
    return 0
  fi
  if [ -x "${HOME}/.cheng/toolchain/cheng-lang/artifacts/tooling_cmd/cheng_tooling" ]; then
    printf '%s\n' "${HOME}/.cheng/toolchain/cheng-lang/artifacts/tooling_cmd/cheng_tooling"
    return 0
  fi
  return 1
}

read_snapshot_field() {
  local snapshot="$1"
  local key="$2"
  sed -n "s/^${key}=//p" "$snapshot" | head -n 1
}

append_pkg_root() {
  local list="$1"
  local item="$2"
  if [ -z "$item" ] || [ ! -d "$item" ]; then
    printf '%s' "$list"
    return
  fi
  if [ -z "$list" ]; then
    printf '%s' "$item"
    return
  fi
  case ",$list," in
    *,"$item",*) printf '%s' "$list" ;;
    *) printf '%s,%s' "$list" "$item" ;;
  esac
}

usage() {
  cat <<'USAGE'
Usage:
  scripts/backend_build.sh <entry.cheng> [--name:<binName>] [--emit:exe]
                           [--target:<triple>] [--frontend:stage1] [--jobs:<n>]

Notes:
  - Single canonical backend driver only (resolved by `cheng_tooling driver-path`).
  - `emit` only supports `exe`.
USAGE
}

tooling_bin="$(resolve_tooling_bin || true)"
if [ -z "$tooling_bin" ]; then
  echo "[backend_build] missing cheng_tooling binary" 1>&2
  exit 1
fi

toolchain_root="$("$tooling_bin" toolchain-root | awk -F= '/^toolchain_root=/{print $2; exit}')"
if [ -z "$toolchain_root" ]; then
  echo "[backend_build] failed to resolve toolchain root via cheng_tooling" 1>&2
  exit 1
fi

strict_sidecar_snapshot="$toolchain_root/artifacts/backend_sidecar_cheng_fresh/verify_backend_sidecar_cheng_fresh.snapshot.env"
if [ ! -f "$strict_sidecar_snapshot" ]; then
  echo "[backend_build] missing strict-fresh sidecar contract snapshot: $strict_sidecar_snapshot" 1>&2
  echo "[backend_build] run: cheng_tooling verify_backend_sidecar_cheng_fresh" 1>&2
  exit 2
fi

strict_sidecar_status="$(read_snapshot_field "$strict_sidecar_snapshot" "backend_sidecar_cheng_fresh_status")"
strict_sidecar_mode="$(read_snapshot_field "$strict_sidecar_snapshot" "backend_sidecar_cheng_fresh_mode")"
strict_sidecar_bundle="$(read_snapshot_field "$strict_sidecar_snapshot" "backend_sidecar_cheng_fresh_bundle")"
strict_sidecar_compiler="$(read_snapshot_field "$strict_sidecar_snapshot" "backend_sidecar_cheng_fresh_compiler")"
strict_sidecar_driver="$(read_snapshot_field "$strict_sidecar_snapshot" "backend_sidecar_cheng_fresh_driver")"
strict_sidecar_real_driver="$(read_snapshot_field "$strict_sidecar_snapshot" "backend_sidecar_cheng_fresh_real_driver")"
strict_sidecar_child_mode="$(read_snapshot_field "$strict_sidecar_snapshot" "backend_sidecar_cheng_fresh_child_mode")"
strict_sidecar_outer_companion="$(read_snapshot_field "$strict_sidecar_snapshot" "backend_sidecar_cheng_fresh_outer_companion")"

if [ "$strict_sidecar_status" != "ok" ] || [ "$strict_sidecar_mode" != "cheng" ]; then
  echo "[backend_build] strict-fresh sidecar contract invalid: $strict_sidecar_snapshot" 1>&2
  echo "[backend_build] run: cheng_tooling verify_backend_sidecar_cheng_fresh" 1>&2
  exit 2
fi
if [ -z "$strict_sidecar_bundle" ] || [ ! -s "$strict_sidecar_bundle" ]; then
  echo "[backend_build] strict-fresh sidecar bundle missing: $strict_sidecar_bundle" 1>&2
  echo "[backend_build] run: cheng_tooling verify_backend_sidecar_cheng_fresh" 1>&2
  exit 2
fi
if [ -z "$strict_sidecar_compiler" ] || [ ! -x "$strict_sidecar_compiler" ]; then
  echo "[backend_build] strict-fresh sidecar compiler missing: $strict_sidecar_compiler" 1>&2
  echo "[backend_build] run: cheng_tooling verify_backend_sidecar_cheng_fresh" 1>&2
  exit 2
fi
if [ -z "$strict_sidecar_real_driver" ] || [ ! -x "$strict_sidecar_real_driver" ]; then
  echo "[backend_build] strict-fresh sidecar real driver missing: $strict_sidecar_real_driver" 1>&2
  echo "[backend_build] run: cheng_tooling verify_backend_sidecar_cheng_fresh" 1>&2
  exit 2
fi
if [ -z "$strict_sidecar_child_mode" ]; then
  echo "[backend_build] strict-fresh sidecar child mode missing: $strict_sidecar_snapshot" 1>&2
  echo "[backend_build] run: cheng_tooling verify_backend_sidecar_cheng_fresh" 1>&2
  exit 2
fi
if [ "$strict_sidecar_child_mode" = "outer_cli" ] && { [ -z "$strict_sidecar_outer_companion" ] || [ ! -x "$strict_sidecar_outer_companion" ]; }; then
  echo "[backend_build] strict-fresh sidecar outer companion missing: $strict_sidecar_outer_companion" 1>&2
  echo "[backend_build] run: cheng_tooling verify_backend_sidecar_cheng_fresh" 1>&2
  exit 2
fi

export BACKEND_UIR_SIDECAR_MODE="$strict_sidecar_mode"
export BACKEND_UIR_SIDECAR_BUNDLE="$strict_sidecar_bundle"
export BACKEND_UIR_SIDECAR_COMPILER="$strict_sidecar_compiler"
export BACKEND_UIR_SIDECAR_CHILD_MODE="$strict_sidecar_child_mode"
export BACKEND_UIR_SIDECAR_REAL_DRIVER="$strict_sidecar_real_driver"
export TOOLING_BUILD_GLOBAL_CURRENTSOURCE_REAL_DRIVER="$strict_sidecar_real_driver"
if [ -n "$strict_sidecar_outer_companion" ]; then
  export BACKEND_UIR_SIDECAR_OUTER_COMPILER="$strict_sidecar_outer_companion"
fi
if [ -n "$strict_sidecar_driver" ] && [ -x "$strict_sidecar_driver" ]; then
  export BACKEND_DRIVER="$strict_sidecar_driver"
fi

entry=""
bin_name=""
emit_mode="${BACKEND_EMIT:-exe}"
target="${BACKEND_TARGET:-}"
frontend="${BACKEND_FRONTEND:-stage1}"
jobs="${BACKEND_JOBS:-1}"

for arg in "$@"; do
  case "$arg" in
    --help|-h)
      usage
      exit 0
      ;;
    --name:*)
      bin_name="${arg#--name:}"
      ;;
    --emit:*)
      emit_mode="${arg#--emit:}"
      ;;
    --target:*)
      target="${arg#--target:}"
      ;;
    --frontend:*)
      frontend="${arg#--frontend:}"
      ;;
    --jobs:*)
      jobs="${arg#--jobs:}"
      ;;
    -*)
      echo "[backend_build] unknown arg: $arg" 1>&2
      usage
      exit 2
      ;;
    *)
      if [ -z "$entry" ]; then
        entry="$arg"
      else
        echo "[backend_build] unexpected positional arg: $arg" 1>&2
        usage
        exit 2
      fi
      ;;
  esac
done

if [ -z "$entry" ]; then
  usage
  exit 2
fi
if [ ! -f "$entry" ] && [ -f "$REPO_ROOT/$entry" ]; then
  entry="$REPO_ROOT/$entry"
fi
if [ ! -f "$entry" ]; then
  echo "[backend_build] entry not found: $entry" 1>&2
  exit 2
fi

if [ "$emit_mode" != "exe" ]; then
  echo "[backend_build] unsupported --emit:$emit_mode (only exe)" 1>&2
  exit 2
fi
if [ "$frontend" != "stage1" ]; then
  echo "[backend_build] unsupported --frontend:$frontend (only stage1)" 1>&2
  exit 2
fi

if [ -z "$bin_name" ]; then
  base="$(basename "$entry")"
  bin_name="${base%.cheng}"
fi
if [[ "$bin_name" = /* ]]; then
  out_path="$bin_name"
else
  out_path="$REPO_ROOT/$bin_name"
fi

pkg_roots="${PKG_ROOTS:-}"
pkg_roots="$(append_pkg_root "$pkg_roots" "$REPO_ROOT")"
pkg_roots="$(append_pkg_root "$pkg_roots" "$toolchain_root")"
if [ -d "${HOME}/.cheng-packages" ]; then
  pkg_roots="$(append_pkg_root "$pkg_roots" "${HOME}/.cheng-packages")"
fi
export PKG_ROOTS="$pkg_roots"
export BACKEND_JOBS="$jobs"

compile_cmd="cheng"
compile_args=()
if [ "${BACKEND_BUILD_TRACK:-dev}" = "release" ]; then
  compile_cmd="release-compile"
else
  compile_cmd="cheng"
fi
compile_args=("$compile_cmd" --in:"$entry" --out:"$out_path")
if [ -n "$target" ]; then
  compile_args+=(--target:"$target")
fi
if [ "${BACKEND_BUILD_TRACK:-dev}" != "release" ]; then
  compile_args+=(--dev)
fi

"$tooling_bin" "${compile_args[@]}"
