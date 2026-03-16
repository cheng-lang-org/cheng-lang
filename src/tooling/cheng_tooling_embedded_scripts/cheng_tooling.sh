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
real_launcher_src="$scripts_dir/cheng_tooling_real.sh"
real_launcher_bin="$root/artifacts/tooling_cmd/cheng_tooling.real"
real_primary_src="$scripts_dir/cheng_tooling_real_bin.sh"
real_primary_bin="$root/artifacts/tooling_cmd/cheng_tooling.real.bin"

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

script_is_self_trampoline() {
  subcmd="$1"
  script_path="$2"
  grep -Eq "^[[:space:]]*exec[[:space:]].*(TOOLING_SELF_BIN|\\\$tool).*[[:space:]]$subcmd([[:space:]]|$)" "$script_path" 2>/dev/null
}

resolve_repo_script() {
  subcmd="$1"
  if [ "$subcmd" = "" ] || [ "$subcmd" = "cheng_tooling" ]; then
    return 1
  fi
  script_path="$scripts_dir/$subcmd.sh"
  if [ ! -s "$script_path" ]; then
    return 1
  fi
  if script_is_self_trampoline "$subcmd" "$script_path"; then
    return 1
  fi
  printf '%s\n' "$script_path"
}

if [ ! -f "$src" ]; then
  echo "[cheng_tooling] source not found: $src" 1>&2
  exit 1
fi

sync_real_launcher
sync_real_primary

script_path="$(resolve_repo_script "${1:-}" || true)"
if [ "$script_path" != "" ]; then
  export TOOLING_ROOT="$root"
  shift || true
  exec_script_file "$script_path" "$@"
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
    if [ -x "artifacts/backend_selfhost_self_obj/cheng.stage2" ]; then
      build_driver="artifacts/backend_selfhost_self_obj/cheng.stage2"
    elif [ -x "artifacts/backend_seed/cheng.stage2" ]; then
      build_driver="artifacts/backend_seed/cheng.stage2"
    else
      build_driver="$(${TOOLING_SELF_BIN:-artifacts/tooling_cmd/cheng_tooling} backend_driver_path)"
    fi
  fi
  # tooling launcher itself uses argv pointer entry; build it under non-C-ABI pointer exemption.
  STAGE1_NO_POINTERS_NON_C_ABI=0 \
  STAGE1_NO_POINTERS_NON_C_ABI_INTERNAL=0 \
  ${TOOLING_SELF_BIN:-artifacts/tooling_cmd/cheng_tooling} chengc \
    --in:"$src" \
    --out:"$bin" \
    --stage0:"$build_driver" \
    --linker:"$tooling_linker"
fi

export TOOLING_ROOT="$root"
exec "$bin" "$@"
