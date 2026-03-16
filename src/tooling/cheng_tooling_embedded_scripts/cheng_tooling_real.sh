#!/usr/bin/env sh
:
set -eu
(set -o pipefail) 2>/dev/null && set -o pipefail

script_dir="$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)"
primary_bin="$script_dir/cheng_tooling.real.bin"
core_bin="$script_dir/../tooling_bundle/core/cheng_tooling_global"
full_bin="$script_dir/../tooling_bundle/full/cheng_tooling"
force_candidate="${TOOLING_REAL_CANDIDATE:-}"
first_arg="${1:-}"

resolve_root() {
  start_dir="$1"
  cur="$start_dir"
  while [ "$cur" != "/" ] && [ "$cur" != "." ] && [ "$cur" != "" ]; do
    if [ -f "$cur/src/tooling/cheng_tooling_embedded_scripts/cheng_tooling.sh" ] && \
       [ -x "$cur/artifacts/tooling_cmd/cheng_tooling" ]; then
      printf '%s\n' "$cur"
      return 0
    fi
    cur="$(dirname -- "$cur")"
  done
  return 1
}

script_is_self_trampoline() {
  subcmd="$1"
  script_path="$2"
  grep -Eq "^[[:space:]]*exec[[:space:]].*(TOOLING_SELF_BIN|\\\$tool).*[[:space:]]$subcmd([[:space:]]|$)" "$script_path" 2>/dev/null
}

resolve_repo_script() {
  root="$1"
  subcmd="$2"
  if [ "$subcmd" = "" ] || [ "$subcmd" = "cheng_tooling" ]; then
    return 1
  fi
  script_path="$root/src/tooling/cheng_tooling_embedded_scripts/$subcmd.sh"
  if [ ! -s "$script_path" ]; then
    return 1
  fi
  if script_is_self_trampoline "$subcmd" "$script_path"; then
    return 1
  fi
  printf '%s\n' "$script_path"
}

root="$(resolve_root "$script_dir" || true)"
repo_wrapper=""
primary_launcher_src=""
if [ "$root" != "" ]; then
  repo_wrapper="$root/src/tooling/cheng_tooling_embedded_scripts/cheng_tooling.sh"
  primary_launcher_src="$root/src/tooling/cheng_tooling_embedded_scripts/cheng_tooling_real_bin.sh"
fi

sync_primary_launcher() {
  if [ "$primary_launcher_src" = "" ] || [ ! -f "$primary_launcher_src" ]; then
    return 0
  fi
  mkdir -p "$(dirname -- "$primary_bin")"
  if [ -f "$primary_bin" ] && cmp -s "$primary_launcher_src" "$primary_bin"; then
    chmod +x "$primary_bin" 2>/dev/null || true
    return 0
  fi
  cp "$primary_launcher_src" "$primary_bin"
  chmod +x "$primary_bin" 2>/dev/null || true
}

sync_primary_launcher

if [ "$repo_wrapper" != "" ] && [ "$(resolve_repo_script "$root" "$first_arg" || true)" != "" ]; then
  exec env TOOLING_BIN="$primary_bin" sh "$repo_wrapper" "$@"
fi

run_one() {
  bin="$1"
  shift || true
  [ -x "$bin" ] || return 127
  "$bin" "$@"
}

run_with_fallback() {
  first="$1"
  second="$2"
  shift 2 || true
  if [ ! -x "$first" ]; then
    run_one "$second" "$@"
    return $?
  fi
  set +e
  run_one "$first" "$@"
  rc=$?
  set -e
  case "$rc" in
    0)
      return 0
      ;;
    126|127|139|223)
      if [ -x "$second" ]; then
        printf '%s\n' "[cheng_tooling.real] fallback: $first -> $second (rc=$rc)" 1>&2
        run_one "$second" "$@"
        return $?
      fi
      ;;
  esac
  return "$rc"
}

case "$force_candidate" in
  bin)
    exec "$primary_bin" "$@"
    ;;
  core)
    exec "$core_bin" "$@"
    ;;
  full)
    exec "$full_bin" "$@"
    ;;
  "")
    ;;
  *)
    printf '%s\n' "[cheng_tooling.real] invalid TOOLING_REAL_CANDIDATE=$force_candidate (expected bin|core|full)" 1>&2
    exit 2
    ;;
esac

if [ -x "$primary_bin" ]; then
  run_with_fallback "$primary_bin" "$core_bin" "$@"
  exit $?
fi

if [ -x "$core_bin" ]; then
  exec "$core_bin" "$@"
fi

exec "$full_bin" "$@"
