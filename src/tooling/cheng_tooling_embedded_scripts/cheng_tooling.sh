#!/usr/bin/env sh
:
set -eu
(set -o pipefail) 2>/dev/null && set -o pipefail

root="$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)"
cd "$root"

src="src/tooling/cheng_tooling.cheng"
bin="${TOOLING_BIN:-$root/artifacts/tooling_cmd/cheng_tooling}"
force_build="${TOOLING_FORCE_BUILD:-0}"
tooling_linker="${TOOLING_LINKER:-system}"
build_driver="${TOOLING_BUILD_DRIVER:-}"
scripts_dir="$root/src/tooling/cheng_tooling_embedded_scripts"
core_bin="$root/artifacts/tooling_bundle/core/cheng_tooling_global"
canonical_wrapper_src="$scripts_dir/cheng_tooling_canonical.sh"
canonical_wrapper_bin="$root/artifacts/tooling_cmd/cheng_tooling"
real_launcher_src="$scripts_dir/cheng_tooling_real.sh"
real_launcher_bin="$root/artifacts/tooling_cmd/cheng_tooling.real"
real_primary_src="$scripts_dir/cheng_tooling_real_bin.sh"
real_primary_bin="$root/artifacts/tooling_cmd/cheng_tooling.real.bin"
list_bin="${TOOLING_LIST_BIN:-}"
repo_gate_support_src="$scripts_dir/cheng_tooling_repo_gate_support.sh"

sync_canonical_wrapper() {
  if [ ! -f "$canonical_wrapper_src" ]; then
    return 0
  fi
  mkdir -p "$(dirname "$canonical_wrapper_bin")"
  if [ -f "$canonical_wrapper_bin" ] && cmp -s "$canonical_wrapper_src" "$canonical_wrapper_bin"; then
    chmod +x "$canonical_wrapper_bin" 2>/dev/null || true
    return 0
  fi
  cp "$canonical_wrapper_src" "$canonical_wrapper_bin"
  chmod +x "$canonical_wrapper_bin"
}

sync_real_launcher() {
  if [ ! -f "$real_launcher_src" ]; then
    return 0
  fi
  mkdir -p "$(dirname "$real_launcher_bin")"
  if [ -f "$real_launcher_bin" ] && cmp -s "$real_launcher_src" "$real_launcher_bin"; then
    chmod +x "$real_launcher_bin" 2>/dev/null || true
    return 0
  fi
  cp "$real_launcher_src" "$real_launcher_bin"
  chmod +x "$real_launcher_bin"
}

sync_real_primary() {
  if [ ! -f "$real_primary_src" ]; then
    return 0
  fi
  mkdir -p "$(dirname "$real_primary_bin")"
  if [ -f "$real_primary_bin" ] && cmp -s "$real_primary_src" "$real_primary_bin"; then
    chmod +x "$real_primary_bin" 2>/dev/null || true
    return 0
  fi
  cp "$real_primary_src" "$real_primary_bin"
  chmod +x "$real_primary_bin"
}

exec_script_file() {
  script_path="$1"
  shift || true
  first_line="$(sed -n '1p' "$script_path" 2>/dev/null || true)"
  case "$first_line" in
    '#!'*bash*)
      exec bash "$script_path" "$@"
      ;;
  esac
  exec sh "$script_path" "$@"
}

if [ ! -f "$repo_gate_support_src" ]; then
  echo "[cheng_tooling] repo gate support helper missing: $repo_gate_support_src" 1>&2
  exit 1
fi
. "$repo_gate_support_src"

if [ ! -f "$src" ]; then
  echo "[cheng_tooling] source not found: $src" 1>&2
  exit 1
fi

sync_real_launcher
sync_real_primary
sync_canonical_wrapper

if run_build_global_canonical_sync "$@"; then
  exit 0
fi

if [ "${1:-}" = "install" ]; then
  export TOOLING_ROOT="$root"
  run_augmented_install "$@"
  exit 0
fi

if [ "${1:-}" = "bundle" ]; then
  export TOOLING_ROOT="$root"
  run_augmented_bundle "$@"
  exit 0
fi

if is_list_cmd "${1:-}"; then
  export TOOLING_ROOT="$root"
  run_augmented_list "$@"
  exit 0
fi

if [ "${BACKEND_DRIVER:-}" != "" ] && [ "${1:-}" = "cheng" ]; then
  export TOOLING_ROOT="$root"
  shift || true
  exec sh "$scripts_dir/chengc.sh" "$@"
fi

if [ "${BACKEND_DRIVER:-}" != "" ] && [ "${1:-}" = "release-compile" ]; then
  export TOOLING_ROOT="$root"
  shift || true
  exec sh "$scripts_dir/chengc.sh" "$@" --release
fi

script_path="$(resolve_repo_script "${1:-}" || true)"
if [ "$script_path" != "" ]; then
  export TOOLING_ROOT="$root"
  shift || true
  exec_script_file "$script_path" "$@"
fi

tooling_selfhost_stage0_for_source() {
  src_path_raw="$1"
  case "$src_path_raw" in
    src/tooling/cheng_tooling.cheng|*/src/tooling/cheng_tooling.cheng)
      if [ "${CHENG_TOOLING_SELFHOST_STAGE0:-}" != "" ]; then
        cand="${CHENG_TOOLING_SELFHOST_STAGE0:-}"
        if [ -x "$cand" ]; then
          printf '%s\n' "$cand"
          return 0
        fi
      fi
      for cand in \
        "$root/artifacts/backend_selfhost_self_obj/probe_currentsrc_proof/cheng.stage2.proof" \
        "$root/artifacts/backend_selfhost_self_obj/probe_currentsrc_proof/cheng.stage2" \
        "$root/artifacts/backend_selfhost_self_obj/probe_currentsrc_proof/cheng_stage0_currentsrc.proof"
      do
        if [ -x "$cand" ]; then
          printf '%s\n' "$cand"
          return 0
        fi
      done
      ;;
  esac
  return 1
}

resolve_build_driver_path() {
  driver_raw="$1"
  [ "$driver_raw" != "" ] || return 1
  resolved="$(BACKEND_DRIVER="$driver_raw" BACKEND_DRIVER_ALLOW_FALLBACK=0 sh "$scripts_dir/backend_driver_path.sh" --path-only 2>/dev/null || true)"
  [ "$resolved" != "" ] || return 1
  printf '%s\n' "$resolved"
}

if [ "$bin" = "$canonical_wrapper_bin" ]; then
  export TOOLING_ROOT="$root"
  exec "$canonical_wrapper_bin" "$@"
fi

if [ "$bin" = "$real_launcher_bin" ] || [ "$bin" = "$real_primary_bin" ]; then
  export TOOLING_ROOT="$root"
  exec "$bin" "$@"
fi

need_build="0"
if [ ! -x "$bin" ]; then
  need_build="1"
elif [ "$src" -nt "$bin" ]; then
  need_build="1"
fi
case "$force_build" in
  1|true|TRUE|yes|YES|on|ON) need_build="1" ;;
esac

if [ "$need_build" = "1" ]; then
  mkdir -p "$(dirname "$bin")"
  if [ "$build_driver" = "" ]; then
    if build_driver="$(resolve_build_driver_path "${BACKEND_DRIVER:-}" 2>/dev/null || true)" && [ "$build_driver" != "" ]; then
      :
    elif [ "${BACKEND_DRIVER:-}" != "" ]; then
      echo "[cheng_tooling] explicit BACKEND_DRIVER rejected for tooling self-build: ${BACKEND_DRIVER:-}" 1>&2
      exit 1
    elif build_driver="$(tooling_selfhost_stage0_for_source "$src" 2>/dev/null || true)" && [ "$build_driver" != "" ]; then
      :
    elif [ -x "artifacts/backend_selfhost_self_obj/cheng.stage2" ]; then
      build_driver="artifacts/backend_selfhost_self_obj/cheng.stage2"
    else
      build_driver="$(sh "$scripts_dir/backend_driver_path.sh" --path-only)"
    fi
  elif build_driver="$(resolve_build_driver_path "$build_driver" 2>/dev/null || true)" && [ "$build_driver" != "" ]; then
    :
  else
    echo "[cheng_tooling] explicit TOOLING_BUILD_DRIVER rejected for tooling self-build: ${TOOLING_BUILD_DRIVER:-}" 1>&2
    exit 1
  fi
  env \
    BACKEND_DRIVER="$build_driver" \
  ${TOOLING_SELF_BIN:-artifacts/tooling_cmd/cheng_tooling} cheng \
    "$src" \
    --out:"$bin"
fi

export TOOLING_ROOT="$root"
exec "$bin" "$@"
