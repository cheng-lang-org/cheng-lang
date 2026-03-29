#!/usr/bin/env sh
:
set -euo pipefail

root="${TOOLING_ROOT:-$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)}"
clean_env_path="${PATH:-/usr/bin:/bin:/usr/sbin:/sbin}"
clean_env_home="${HOME:-$root}"
clean_env_tmpdir="${TMPDIR:-/tmp}"

usage() {
  cat <<'USAGE'
Usage:
  ${TOOLING_SELF_BIN:-artifacts/tooling_cmd/cheng_tooling} chengc <file.cheng> [--name:<exeName>] [--jobs:<N>] [--fn-jobs:<N>]
                        [--manifest:<path>] [--lock:<path>] [--registry:<path>]
                        [--package:<id>] [--channel:<edge|stable|lts>]
                        [--lock-format:<toml|yaml|json>] [--meta-out:<path>]
                        [--buildmeta:<path>]
                        [--build-track:<dev|release>]
                        [--linker:<self|system>]
                        [--target:<triple>]
                        [--whole-program|--whole-program:0|1]
                        [--opt|--opt:0|1] [--opt2|--opt2:0|1] [--opt-level:0|1|2|3]
                        [--ldflags:<text>] [--fn-sched:<ws>]
                        [--no-runtime-c|--no-runtime-c:0|1] [--runtime-obj:<path>]
                        [--sidecar-mode:cheng] [--sidecar-bundle:<path>] [--sidecar-compiler:<path>]
                        [--sidecar-child-mode:<cli|outer_cli>] [--sidecar-outer-compiler:<path>]
                        [--module-cache:<path>] [--multi-module-cache|--multi-module-cache:0|1]
                        [--module-cache-unstable-allow|--module-cache-unstable-allow:0|1]
                        [--generic-mode:dict] [--generic-spec-budget:<N>] [--generic-lowering:mir_dict]
                        [--pkg-cache:<dir>] [--skip-pkg] [--verify] [--ledger:<path>]
                        [--source-listen:<addr>] [--source-peer:<addr>]
                        [--mm:<orc>] [--orc]
                        [--release]
                        [--emit:<exe>] [--frontend:<stage1>]
                        [--out:<path>] [--multi|--multi:0|1] [--multi-force:0|1]
                        [--incremental:0|1] [--pkg-roots:<p1[:p2:...]>]
                        [--run] [--run:file] [--run:host]
  ${TOOLING_SELF_BIN:-artifacts/tooling_cmd/cheng_tooling} chengc run <android|ios|harmony> <file.cheng> [mobile-run-options]

Notes:
  - Backend-only entrypoint (exe only).
  - Default build track is dev.
  - `--release` is an alias of `--build-track:release` and defaults linker to system.
  - Optional compile daemon: set `CHENGC_DAEMON=1` to route builds through `chengc_daemon`.
  - `--run` defaults to host runner (`--run:host`); use `--run:file` for legacy file-exec mode.
  - Default `MM=orc`.
  - Executable output for bare `--name:<bin>` defaults to `artifacts/chengc/<bin>`.
  - `--jobs:<N>` / `--fn-jobs:<N>` / `--ldflags:<text>` / `--fn-sched:<ws>` control backend compile surface.
  - `--whole-program` / `--opt*` / `--generic-*` / `--sidecar-*` / `--module-cache*` / `--no-runtime-c` / `--runtime-obj`
    are explicit compile-surface flags for exe builds.
  - No-pointer policy is fixed by the compiler/toolchain default and is not part of the public `chengc` flag surface.
  - Only `--fn-sched:ws` is supported.
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

ensure_chengc_output_exists() {
  emit_mode="$1"
  out_path_check="$2"
  case "$emit_mode" in
    obj)
      if [ ! -s "$out_path_check" ]; then
        echo "[Error] chengc: missing object output: $out_path_check" 1>&2
        exit 1
      fi
      ;;
    exe)
      if [ ! -x "$out_path_check" ]; then
        echo "[Error] chengc: missing executable output: $out_path_check" 1>&2
        exit 1
      fi
      ;;
    *)
      echo "[Error] chengc: unsupported emit mode for output check: $emit_mode" 1>&2
      exit 1
      ;;
  esac
}

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

append_token() {
  base="$1"
  token="$2"
  if [ "$token" = "" ]; then
    printf '%s\n' "$base"
    return
  fi
  if [ "$base" = "" ]; then
    printf '%s\n' "$token"
    return
  fi
  printf '%s %s\n' "$base" "$token"
}

read_env_default() {
  name="$1"
  eval "printf '%s\\n' \"\${$name:-}\""
}

normalize_bool_text() {
  raw="$1"
  label="$2"
  case "$raw" in
    "")
      printf '\n'
      ;;
    1|true|TRUE|yes|YES|on|ON)
      printf '1\n'
      ;;
    0|false|FALSE|no|NO|off|OFF)
      printf '0\n'
      ;;
    *)
      echo "[Error] invalid $label:$raw (expected 0|1)" 1>&2
      exit 2
      ;;
  esac
}

normalize_jobs_text() {
  raw="$1"
  label="$2"
  case "$raw" in
    "" )
      printf '\n'
      ;;
    *[!0-9]*)
      echo "[Error] invalid $label:$raw (expected positive integer)" 1>&2
      exit 2
      ;;
    *)
      if [ "$raw" -lt 1 ] 2>/dev/null; then
        echo "[Error] invalid $label:$raw (expected positive integer)" 1>&2
        exit 2
      fi
      printf '%s\n' "$raw"
      ;;
  esac
}

normalize_nonneg_int_text() {
  raw="$1"
  label="$2"
  case "$raw" in
    "")
      printf '\n'
      ;;
    *[!0-9]*)
      echo "[Error] invalid $label:$raw (expected non-negative integer)" 1>&2
      exit 2
      ;;
    *)
      printf '%s\n' "$raw"
      ;;
  esac
}

normalize_opt_level_text() {
  raw="$1"
  case "$raw" in
    "")
      printf '\n'
      ;;
    0|1|2|3)
      printf '%s\n' "$raw"
      ;;
    *)
      echo "[Error] invalid --opt-level:$raw (expected 0|1|2|3)" 1>&2
      exit 2
      ;;
  esac
}

normalize_build_track_text() {
  raw="$1"
  case "$raw" in
    ""|dev|release)
      printf '%s\n' "$raw"
      ;;
    *)
      echo "[Error] invalid --build-track:$raw (expected dev|release)" 1>&2
      exit 2
      ;;
  esac
}

normalize_parse_mode_text() {
  raw="$1"
  label="$2"
  case "$raw" in
    ""|outline|full)
      printf '%s\n' "$raw"
      ;;
    *)
      echo "[Error] invalid $label:$raw (expected outline|full)" 1>&2
      exit 2
      ;;
  esac
}

normalize_fn_sched_text() {
  raw="$1"
  case "$raw" in
    ""|ws)
      printf '%s\n' "$raw"
      ;;
    *)
      echo "[Error] invalid --fn-sched:$raw (expected ws)" 1>&2
      exit 2
      ;;
  esac
}

normalize_sidecar_mode_text() {
  raw="$1"
  case "$raw" in
    ""|cheng)
      printf '%s\n' "$raw"
      ;;
    *)
      echo "[Error] invalid --sidecar-mode:$raw (expected cheng)" 1>&2
      exit 2
      ;;
  esac
}

normalize_sidecar_child_mode_text() {
  raw="$1"
  case "$raw" in
    ""|cli|outer_cli)
      printf '%s\n' "$raw"
      ;;
    *)
      echo "[Error] invalid --sidecar-child-mode:$raw (expected cli|outer_cli)" 1>&2
      exit 2
      ;;
  esac
}

normalize_generic_mode_text() {
  raw="$1"
  case "$raw" in
    ""|dict)
      printf '%s\n' "$raw"
      ;;
    *)
      echo "[Error] invalid --generic-mode:$raw (expected dict)" 1>&2
      exit 2
      ;;
  esac
}

normalize_generic_lowering_text() {
  raw="$1"
  case "$raw" in
    ""|mir_dict)
      printf '%s\n' "$raw"
      ;;
    *)
      echo "[Error] invalid --generic-lowering:$raw (expected mir_dict)" 1>&2
      exit 2
      ;;
  esac
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

tooling_selfhost_source_is_tooling_main() {
  case "${1:-}" in
    src/tooling/cheng_tooling.cheng|*/src/tooling/cheng_tooling.cheng)
      return 0
      ;;
  esac
  return 1
}

tooling_emit_selfhost_launcher_enabled() {
  case "${TOOLING_EMIT_SELFHOST_LAUNCHER:-1}" in
    0|false|FALSE|no|NO|off|OFF)
      return 1
      ;;
  esac
  return 0
}

tooling_selfhost_native_exec_path() {
  printf '%s.repo_native_exec.bin\n' "$1"
}

emit_tooling_selfhost_launcher() {
  out_path="$1"
  real_bin_path="$2"
  repo_root="$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)"
  real_bin_name="$(basename -- "$real_bin_path")"
  mkdir -p "$(dirname "$out_path")"
  cat >"$out_path" <<EOF
#!/usr/bin/env sh
:
set -eu
(set -o pipefail) 2>/dev/null && set -o pipefail

root="$repo_root"
script_dir="\$(CDPATH= cd -- "\$(dirname -- "\$0")" && pwd)"
real_bin="\$script_dir/$real_bin_name"
if [ ! -x "\$real_bin" ]; then
  echo "[cheng_tooling] missing tooling native companion: \$real_bin" 1>&2
  exit 1
fi
export TOOLING_ROOT="\$root"
export TOOLING_BIN="\$real_bin"
export TOOLING_SELF_BIN="\$real_bin"
export TOOLING_REPO_NATIVE_BIN="\$real_bin"
unset TOOLING_FORCE_BUILD 2>/dev/null || true
unset TOOLING_LIST_BIN 2>/dev/null || true
exec "\$real_bin" "\$@"
EOF
  chmod +x "$out_path"
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

resolve_backend_sidecar_defaults_field() {
  field="$1"
  target_raw="$2"
  resolver="$root/src/tooling/cheng_tooling_embedded_scripts/resolve_backend_sidecar_defaults.sh"
  if [ ! -f "$resolver" ]; then
    return 1
  fi
  sh "$resolver" --root:"$root" --target:"$target_raw" --field:"$field"
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
fn_jobs_raw=""
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
build_track_arg=""
ldflags_raw=""
fn_sched_raw=""
whole_program_raw=""
opt_raw=""
opt2_raw=""
opt_level_raw=""
no_runtime_c_raw=""
runtime_obj_raw=""
sidecar_mode_raw=""
sidecar_bundle_raw=""
sidecar_compiler_raw=""
sidecar_child_mode_raw=""
sidecar_outer_compiler_raw=""
module_cache_raw=""
multi_module_cache_raw=""
module_cache_unstable_allow_raw=""
generic_mode_raw=""
generic_spec_budget_raw=""
generic_lowering_raw=""
legacy_emit=""
legacy_frontend=""
legacy_out=""
legacy_multi=""
multi_raw=""
multi_force_raw=""
incremental_raw=""
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
    --fn-jobs:*) fn_jobs_raw="${1#--fn-jobs:}" ;;
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
    --build-track:*) build_track_arg="${1#--build-track:}" ;;
    --linker:*) linker="${1#--linker:}" ;;
    --target:*) target_arg="${1#--target:}" ;;
    --release) build_track_arg="release" ;;
    --whole-program) whole_program_raw="1" ;;
    --whole-program:*) whole_program_raw="${1#--whole-program:}" ;;
    --opt) opt_raw="1" ;;
    --opt:*) opt_raw="${1#--opt:}" ;;
    --opt2) opt2_raw="1" ;;
    --opt2:*) opt2_raw="${1#--opt2:}" ;;
    --opt-level:*) opt_level_raw="${1#--opt-level:}" ;;
    --ldflags:*) ldflags_raw="${1#--ldflags:}" ;;
    --fn-sched:*) fn_sched_raw="${1#--fn-sched:}" ;;
    --no-runtime-c) no_runtime_c_raw="1" ;;
    --no-runtime-c:*) no_runtime_c_raw="${1#--no-runtime-c:}" ;;
    --runtime-obj:*) runtime_obj_raw="${1#--runtime-obj:}" ;;
    --sidecar-mode:*) sidecar_mode_raw="${1#--sidecar-mode:}" ;;
    --sidecar-bundle:*) sidecar_bundle_raw="${1#--sidecar-bundle:}" ;;
    --sidecar-compiler:*) sidecar_compiler_raw="${1#--sidecar-compiler:}" ;;
    --sidecar-child-mode:*) sidecar_child_mode_raw="${1#--sidecar-child-mode:}" ;;
    --sidecar-outer-compiler:*) sidecar_outer_compiler_raw="${1#--sidecar-outer-compiler:}" ;;
    --module-cache:*) module_cache_raw="${1#--module-cache:}" ;;
    --multi-module-cache) multi_module_cache_raw="1" ;;
    --multi-module-cache:*) multi_module_cache_raw="${1#--multi-module-cache:}" ;;
    --no-multi-module-cache) multi_module_cache_raw="0" ;;
    --module-cache-unstable-allow) module_cache_unstable_allow_raw="1" ;;
    --module-cache-unstable-allow:*) module_cache_unstable_allow_raw="${1#--module-cache-unstable-allow:}" ;;
    --no-module-cache-unstable-allow) module_cache_unstable_allow_raw="0" ;;
    --generic-mode:*) generic_mode_raw="${1#--generic-mode:}" ;;
    --generic-spec-budget:*) generic_spec_budget_raw="${1#--generic-spec-budget:}" ;;
    --generic-lowering:*) generic_lowering_raw="${1#--generic-lowering:}" ;;
    --emit:*) legacy_emit="${1#--emit:}" ;;
    --frontend:*) legacy_frontend="${1#--frontend:}" ;;
    --out:*) legacy_out="${1#--out:}" ;;
    --multi)
      legacy_multi="1"
      multi_raw="1"
      ;;
    --multi:*) multi_raw="${1#--multi:}" ;;
    --multi-force:*) multi_force_raw="${1#--multi-force:}" ;;
    --incremental:*) incremental_raw="${1#--incremental:}" ;;
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
if [ "$obj_requested" = "1" ] && [ "$obj_out" = "" ]; then
  echo "[Error] --emit-obj requires --obj-out:<path>" 1>&2
  exit 2
fi

if [ "$mm" = "" ]; then
  mm="orc"
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

if [ "$jobs_explicit" != "1" ]; then
  jobs="$(read_env_default BACKEND_JOBS)"
fi
jobs="$(normalize_jobs_text "$jobs" "--jobs")"
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

build_track="$(normalize_build_track_text "$build_track_arg")"
if [ "$build_track" = "" ]; then
  build_track="dev"
fi

backend_parse_mode_env="$(normalize_parse_mode_text "${BACKEND_STAGE1_PARSE_MODE:-}" "BACKEND_STAGE1_PARSE_MODE")"
if [ "$backend_parse_mode_env" = "" ]; then
  if [ "$build_track" = "release" ]; then
    backend_parse_mode_env="full"
  else
    backend_parse_mode_env="outline"
  fi
fi

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

driver_is_script_shim() {
  bin="$1"
  if [ ! -f "$bin" ]; then
    return 1
  fi
  first_line="$(sed -n '1p' "$bin" 2>/dev/null || true)"
  case "$first_line" in
    '#!'*) return 0 ;;
  esac
  return 1
}

driver_current_contract_ok() {
  bin="$1"
  if [ ! -x "$bin" ]; then
    return 1
  fi
  if driver_is_script_shim "$bin"; then
    return 0
  fi
  if ! command -v strings >/dev/null 2>&1; then
    return 0
  fi
  if ! command -v rg >/dev/null 2>&1; then
    return 0
  fi
  set +e
  strings "$bin" 2>/dev/null | rg -q '^BACKEND_OUTPUT$'
  has_backend_output="$?"
  strings "$bin" 2>/dev/null | rg -q '^CHENG_BACKEND_OUTPUT$'
  has_legacy_output="$?"
  strings "$bin" 2>/dev/null | rg -q 'backend_driver: output path required'
  has_output_msg="$?"
  set -e
  if [ "$has_output_msg" -eq 0 ] && [ "$has_legacy_output" -eq 0 ] && [ "$has_backend_output" -ne 0 ]; then
    return 1
  fi
  return 0
}

tooling_selfhost_stage0_for_input() {
  input_path="$1"
  case "$input_path" in
    src/tooling/cheng_tooling.cheng|*/src/tooling/cheng_tooling.cheng)
      cand="${CHENG_TOOLING_SELFHOST_STAGE0:-artifacts/backend_selfhost_self_obj/probe_currentsrc_proof/cheng.stage2}"
      if [ -x "$cand" ] && driver_can_run "$cand"; then
        printf '%s\n' "$cand"
        return 0
      fi
      ;;
  esac
  return 1
}

if [ -n "${BACKEND_DRIVER:-}" ]; then
  driver="$BACKEND_DRIVER"
elif selfhost_stage0="$(tooling_selfhost_stage0_for_input "$in" 2>/dev/null || true)" && [ "$selfhost_stage0" != "" ]; then
  driver="$selfhost_stage0"
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
if [ ! -x "$driver" ]; then
  echo "[Error] backend driver not executable: $driver" 1>&2
  exit 1
fi
if ! driver_can_run "$driver"; then
  echo "[Error] backend driver failed help probe: $driver" 1>&2
  exit 1
fi
if ! driver_current_contract_ok "$driver"; then
  echo "[Error] backend driver uses rejected legacy CHENG_BACKEND_* contract: $driver" 1>&2
  exit 1
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
host_target="$(${TOOLING_SELF_BIN:-artifacts/tooling_cmd/cheng_tooling} detect_host_target)"
case "$backend_target" in
  ""|auto|native|host)
    backend_target="$host_target"
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

backend_sidecar_mode="$sidecar_mode_raw"
if [ "$backend_sidecar_mode" = "" ]; then
  backend_sidecar_mode="$(resolve_backend_sidecar_defaults_field mode "$backend_target" 2>/dev/null || true)"
fi
backend_sidecar_mode="$(normalize_sidecar_mode_text "$backend_sidecar_mode")"
if [ "$backend_sidecar_mode" = "" ]; then
  echo "[Error] missing strict fresh Cheng sidecar mode; run verify_backend_sidecar_cheng_fresh" 1>&2
  exit 1
fi

backend_sidecar_bundle="$sidecar_bundle_raw"
if [ "$backend_sidecar_bundle" = "" ]; then
  backend_sidecar_bundle="$(resolve_backend_sidecar_defaults_field bundle "$backend_target" 2>/dev/null || true)"
fi
if [ "$backend_sidecar_bundle" = "" ]; then
  echo "[Error] missing strict fresh Cheng sidecar bundle; run verify_backend_sidecar_cheng_fresh" 1>&2
  exit 1
fi

backend_sidecar_compiler="$sidecar_compiler_raw"
if [ "$backend_sidecar_compiler" = "" ]; then
  backend_sidecar_compiler="$(resolve_backend_sidecar_defaults_field compiler "$backend_target" 2>/dev/null || true)"
fi
if [ "$backend_sidecar_compiler" = "" ]; then
  echo "[Error] missing strict fresh Cheng sidecar compiler; run verify_backend_sidecar_cheng_fresh" 1>&2
  exit 1
fi

backend_sidecar_driver=""
backend_sidecar_real_driver="${BACKEND_UIR_SIDECAR_REAL_DRIVER:-${TOOLING_BUILD_GLOBAL_CURRENTSOURCE_REAL_DRIVER:-}}"
if [ "${BACKEND_DRIVER:-}" = "" ] && [ "$backend_sidecar_mode" = "cheng" ]; then
  backend_sidecar_driver="$(resolve_backend_sidecar_defaults_field driver "$backend_target" 2>/dev/null || true)"
  if [ "$backend_sidecar_driver" != "" ] && [ -x "$backend_sidecar_driver" ]; then
    driver="$backend_sidecar_driver"
    driver_exec="$driver"
    driver_real_env=""
  fi
fi
backend_sidecar_child_mode="$sidecar_child_mode_raw"
backend_sidecar_outer_companion=""
if [ "$backend_sidecar_mode" = "cheng" ]; then
  if [ "$backend_sidecar_real_driver" = "" ]; then
    backend_sidecar_real_driver="$(resolve_backend_sidecar_defaults_field real_driver "$backend_target" 2>/dev/null || true)"
  fi
  if [ "$backend_sidecar_real_driver" = "" ]; then
    echo "[Error] missing strict fresh Cheng sidecar real driver; set TOOLING_BUILD_GLOBAL_CURRENTSOURCE_REAL_DRIVER or run verify_backend_sidecar_cheng_fresh" 1>&2
    exit 1
  fi
  if [ "$backend_sidecar_child_mode" = "" ]; then
    backend_sidecar_child_mode="$(resolve_backend_sidecar_defaults_field child_mode "$backend_target" 2>/dev/null || true)"
  fi
  backend_sidecar_child_mode="$(normalize_sidecar_child_mode_text "$backend_sidecar_child_mode")"
  if [ "$backend_sidecar_child_mode" = "" ]; then
    echo "[Error] missing strict fresh Cheng sidecar child mode; run verify_backend_sidecar_cheng_fresh" 1>&2
    exit 1
  fi
  if [ "$backend_sidecar_child_mode" = "outer_cli" ]; then
    backend_sidecar_outer_companion="$sidecar_outer_compiler_raw"
    if [ "$backend_sidecar_outer_companion" = "" ]; then
      backend_sidecar_outer_companion="$(resolve_backend_sidecar_defaults_field outer_companion "$backend_target" 2>/dev/null || true)"
    fi
    if [ "$backend_sidecar_outer_companion" = "" ]; then
      echo "[Error] missing strict fresh Cheng sidecar outer companion; run verify_backend_sidecar_cheng_fresh" 1>&2
      exit 1
    fi
  elif [ "$sidecar_outer_compiler_raw" != "" ]; then
    echo "[Error] --sidecar-outer-compiler requires --sidecar-child-mode:outer_cli" 1>&2
    exit 2
  fi
fi

backend_whole_program="$whole_program_raw"
if [ "$backend_whole_program" = "" ]; then
  backend_whole_program="$(read_env_default BACKEND_WHOLE_PROGRAM)"
fi
backend_whole_program="$(normalize_bool_text "$backend_whole_program" "--whole-program")"

backend_opt="$opt_raw"
if [ "$backend_opt" = "" ]; then
  backend_opt="$(read_env_default BACKEND_OPT)"
fi
backend_opt="$(normalize_bool_text "$backend_opt" "--opt")"

backend_opt2="$opt2_raw"
if [ "$backend_opt2" = "" ]; then
  backend_opt2="$(read_env_default BACKEND_OPT2)"
fi
backend_opt2="$(normalize_bool_text "$backend_opt2" "--opt2")"

backend_opt_level="$opt_level_raw"
if [ "$backend_opt_level" = "" ]; then
  backend_opt_level="$(read_env_default BACKEND_OPT_LEVEL)"
fi
backend_opt_level="$(normalize_opt_level_text "$backend_opt_level")"

backend_generic_mode="$generic_mode_raw"
if [ "$backend_generic_mode" = "" ]; then
  backend_generic_mode="$(read_env_default GENERIC_MODE)"
fi
if [ "$backend_generic_mode" = "" ]; then
  backend_generic_mode="dict"
fi
backend_generic_mode="$(normalize_generic_mode_text "$backend_generic_mode")"

backend_generic_spec_budget="$generic_spec_budget_raw"
if [ "$backend_generic_spec_budget" = "" ]; then
  backend_generic_spec_budget="$(read_env_default GENERIC_SPEC_BUDGET)"
fi
if [ "$backend_generic_spec_budget" = "" ]; then
  backend_generic_spec_budget="0"
fi
backend_generic_spec_budget="$(normalize_nonneg_int_text "$backend_generic_spec_budget" "--generic-spec-budget")"

backend_generic_lowering="$generic_lowering_raw"
if [ "$backend_generic_lowering" = "" ]; then
  backend_generic_lowering="$(read_env_default GENERIC_LOWERING)"
fi
if [ "$backend_generic_lowering" = "" ]; then
  backend_generic_lowering="mir_dict"
fi
backend_generic_lowering="$(normalize_generic_lowering_text "$backend_generic_lowering")"

backend_multi_env="$multi_raw"
if [ "$backend_multi_env" = "" ]; then
  backend_multi_env="$(read_env_default BACKEND_MULTI)"
fi
if [ "$backend_multi_env" = "" ]; then
  if [ "$build_track" = "release" ]; then
    backend_multi_env="${CHENGC_RELEASE_MULTI_DEFAULT:-0}"
  else
    backend_multi_env="${CHENGC_DEV_MULTI_DEFAULT:-1}"
  fi
fi
backend_multi_env="$(normalize_bool_text "$backend_multi_env" "--multi")"

backend_multi_force_env="$multi_force_raw"
if [ "$backend_multi_force_env" = "" ]; then
  backend_multi_force_env="0"
fi
backend_multi_force_env="$(normalize_bool_text "$backend_multi_force_env" "--multi-force")"

backend_inc_env="$incremental_raw"
if [ "$backend_inc_env" = "" ]; then
  backend_inc_env="$(read_env_default BACKEND_INCREMENTAL)"
fi
if [ "$backend_inc_env" = "" ]; then
  backend_inc_env="1"
fi
backend_inc_env="$(normalize_bool_text "$backend_inc_env" "--incremental")"

backend_fn_jobs_env="$fn_jobs_raw"
if [ "$backend_fn_jobs_env" = "" ]; then
  backend_fn_jobs_env="$(read_env_default BACKEND_FN_JOBS)"
fi
backend_fn_jobs_env="$(normalize_jobs_text "$backend_fn_jobs_env" "--fn-jobs")"
if [ "$backend_fn_jobs_env" = "" ]; then
  backend_fn_jobs_env="$jobs"
fi

backend_no_runtime_c="$no_runtime_c_raw"
if [ "$backend_no_runtime_c" = "" ]; then
  backend_no_runtime_c="$(read_env_default BACKEND_NO_RUNTIME_C)"
fi
backend_no_runtime_c="$(normalize_bool_text "$backend_no_runtime_c" "--no-runtime-c")"

backend_runtime_obj="$runtime_obj_raw"
if [ "$backend_runtime_obj" = "" ]; then
  backend_runtime_obj="$(read_env_default BACKEND_RUNTIME_OBJ)"
fi

tooling_selfhost_launcher_requested="0"
tooling_selfhost_launcher_out_path=""
if [ "$backend_emit" = "exe" ] && \
   [ "$build_track" != "release" ] && \
   [ "$legacy_run" != "1" ] && \
   [ "$backend_target" = "$host_target" ] && \
   tooling_emit_selfhost_launcher_enabled && \
   tooling_selfhost_source_is_tooling_main "$in"; then
  tooling_selfhost_launcher_requested="1"
  tooling_selfhost_launcher_out_path="$out_path"
  out_path="$(tooling_selfhost_native_exec_path "$tooling_selfhost_launcher_out_path")"
fi

requested_linker="$linker"
if [ "$requested_linker" = "" ]; then
  requested_linker="$(read_env_default BACKEND_LINKER)"
fi
if [ "$requested_linker" = "" ] && [ "$backend_emit" = "exe" ]; then
  if [ "$build_track" = "release" ]; then
    requested_linker="system"
  else
    requested_linker="auto"
  fi
fi
case "$requested_linker" in
  ""|auto|self|system) ;;
  *)
    echo "[Error] invalid --linker:$requested_linker (expected self|system)" 1>&2
    exit 2
    ;;
esac

resolved_linker="$requested_linker"
resolved_no_runtime_c="$backend_no_runtime_c"
resolved_runtime_obj="$backend_runtime_obj"
resolved_runtime_obj_assigned="0"
if [ "$resolved_runtime_obj" != "" ]; then
  resolved_runtime_obj_assigned="1"
fi

resolve_link_env() {
  link_env="$(sh "$root/src/tooling/cheng_tooling_embedded_scripts/backend_link_env.sh" \
    --driver:"$driver_exec" \
    --target:"$backend_target" \
    --linker:"$requested_linker")"
  resolved_linker=""
  resolved_no_runtime_c=""
  resolved_runtime_obj=""
  resolved_runtime_obj_assigned="0"
  for entry in $link_env; do
    case "$entry" in
      BACKEND_LINKER=*)
        resolved_linker="${entry#BACKEND_LINKER=}"
        ;;
      BACKEND_NO_RUNTIME_C=*)
        resolved_no_runtime_c="${entry#BACKEND_NO_RUNTIME_C=}"
        ;;
      BACKEND_RUNTIME_OBJ=*)
        resolved_runtime_obj="${entry#BACKEND_RUNTIME_OBJ=}"
        resolved_runtime_obj_assigned="1"
        ;;
    esac
  done
  if [ "$resolved_linker" = "" ]; then
    echo "[Error] chengc: backend_link_env missing BACKEND_LINKER" 1>&2
    exit 1
  fi
}

if [ "$backend_emit" = "exe" ]; then
  resolve_link_env
  if [ "$backend_no_runtime_c" != "" ]; then
    resolved_no_runtime_c="$backend_no_runtime_c"
  fi
  if [ "$backend_runtime_obj" != "" ]; then
    resolved_runtime_obj="$backend_runtime_obj"
    resolved_runtime_obj_assigned="1"
  fi
  if [ "$resolved_runtime_obj_assigned" = "1" ] && [ "$resolved_no_runtime_c" = "0" ]; then
    echo "[Error] --runtime-obj requires --no-runtime-c:1" 1>&2
    exit 2
  fi
  if [ "$resolved_runtime_obj_assigned" = "1" ] && [ "$resolved_no_runtime_c" = "" ]; then
    resolved_no_runtime_c="1"
  fi
  if [ "$resolved_linker" = "self" ] && [ "$resolved_no_runtime_c" = "0" ]; then
    echo "[Error] self linker requires --no-runtime-c:1" 1>&2
    exit 2
  fi
  if [ "$resolved_linker" = "self" ] && [ "$resolved_runtime_obj_assigned" != "1" ]; then
    echo "[Error] self linker requires a runtime object" 1>&2
    exit 2
  fi
fi
backend_linker="$resolved_linker"

backend_ldflags_final="$ldflags_raw"
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

backend_fn_sched_env="$(normalize_fn_sched_text "$fn_sched_raw")"
if [ "$backend_fn_sched_env" = "" ]; then
  backend_fn_sched_env="ws"
fi

backend_module_cache="$module_cache_raw"
backend_multi_module_cache="$(normalize_bool_text "$multi_module_cache_raw" "--multi-module-cache")"
if [ "$backend_multi_module_cache" = "" ]; then
  backend_multi_module_cache="0"
fi
backend_module_cache_unstable_allow="$(normalize_bool_text "$module_cache_unstable_allow_raw" "--module-cache-unstable-allow")"
if [ "$backend_module_cache_unstable_allow" = "" ]; then
  backend_module_cache_unstable_allow="0"
fi
if [ "$backend_multi_module_cache" = "1" ] && [ "$backend_module_cache_unstable_allow" != "1" ]; then
  echo "[Error] --multi-module-cache requires --module-cache-unstable-allow:1" 1>&2
  exit 2
fi

run_backend_driver() {
  set -- env -i \
    "PATH=$clean_env_path" \
    "HOME=$clean_env_home" \
    "TMPDIR=$clean_env_tmpdir" \
    "TOOLING_ROOT=$root" \
    "BACKEND_STAGE1_PARSE_MODE=$backend_parse_mode_env"
  if [ "$driver_real_env" != "" ]; then
    set -- "$@" "$driver_real_env"
  fi
  if [ "$backend_sidecar_real_driver" != "" ]; then
    set -- "$@" "TOOLING_BUILD_GLOBAL_CURRENTSOURCE_REAL_DRIVER=$backend_sidecar_real_driver"
  fi
  if [ "${PKG_ROOTS:-}" != "" ]; then
    set -- "$@" "PKG_ROOTS=$PKG_ROOTS"
  fi
  set -- "$@" \
    "$driver_exec" "$in" \
    "--emit:$backend_emit" \
    "--frontend:$frontend" \
    "--build-track:$build_track" \
    "--mm:$mm" \
    "--fn-sched:$backend_fn_sched_env" \
    "--target:$backend_target" \
    "--generic-mode:$backend_generic_mode" \
    "--generic-spec-budget:$backend_generic_spec_budget" \
    "--generic-lowering:$backend_generic_lowering" \
    "--jobs:$jobs" \
    "--fn-jobs:$backend_fn_jobs_env"
  if [ "$backend_whole_program" != "" ]; then
    set -- "$@" "--whole-program:$backend_whole_program"
  fi
  if [ "$backend_ldflags_final" != "" ]; then
    set -- "$@" "--ldflags:$backend_ldflags_final"
  fi
  if [ "$backend_module_cache" != "" ]; then
    set -- "$@" "--module-cache:$backend_module_cache"
  fi
  if [ "$backend_multi_module_cache" = "1" ]; then
    set -- "$@" "--multi-module-cache"
  else
    set -- "$@" "--no-multi-module-cache"
  fi
  if [ "$backend_module_cache_unstable_allow" = "1" ]; then
    set -- "$@" "--module-cache-unstable-allow"
  else
    set -- "$@" "--no-module-cache-unstable-allow"
  fi
  if [ "$backend_sidecar_mode" != "" ]; then
    set -- "$@" "--sidecar-mode:$backend_sidecar_mode"
  fi
  if [ "$backend_sidecar_bundle" != "" ]; then
    set -- "$@" "--sidecar-bundle:$backend_sidecar_bundle"
  fi
  if [ "$backend_sidecar_compiler" != "" ]; then
    set -- "$@" "--sidecar-compiler:$backend_sidecar_compiler"
  fi
  if [ "$backend_sidecar_child_mode" != "" ]; then
    set -- "$@" "--sidecar-child-mode:$backend_sidecar_child_mode"
  fi
  if [ "$backend_sidecar_outer_companion" != "" ]; then
    set -- "$@" "--sidecar-outer-compiler:$backend_sidecar_outer_companion"
  fi
  if [ "$backend_emit" = "exe" ]; then
    set -- "$@" "--linker:$backend_linker"
  fi
  if [ "$backend_multi_env" = "1" ]; then
    set -- "$@" "--multi"
  else
    set -- "$@" "--no-multi"
  fi
  if [ "$backend_multi_force_env" = "1" ]; then
    set -- "$@" "--multi-force"
  else
    set -- "$@" "--no-multi-force"
  fi
  if [ "$backend_inc_env" = "1" ]; then
    set -- "$@" "--incremental"
  else
    set -- "$@" "--no-incremental"
  fi
  if [ "$backend_opt" = "1" ]; then
    set -- "$@" "--opt"
  elif [ "$backend_opt" = "0" ]; then
    set -- "$@" "--no-opt"
  fi
  if [ "$backend_opt2" = "1" ]; then
    set -- "$@" "--opt2"
  elif [ "$backend_opt2" = "0" ]; then
    set -- "$@" "--no-opt2"
  fi
  if [ "$backend_opt_level" != "" ]; then
    set -- "$@" "--opt-level:$backend_opt_level"
  fi
  if [ "$backend_emit" = "exe" ] && [ "$resolved_no_runtime_c" = "1" ]; then
    set -- "$@" "--no-runtime-c"
  fi
  if [ "$backend_emit" = "exe" ] && [ "$resolved_runtime_obj_assigned" = "1" ]; then
    set -- "$@" "--runtime-obj:$resolved_runtime_obj"
  fi
  set -- "$@" "--output:$out_path"
  "$@"
}

set +e
run_backend_driver
build_status="$?"
set -e
if [ "$build_status" -ne 0 ]; then
  exit "$build_status"
fi

ensure_chengc_output_exists "$backend_emit" "$out_path"

if [ "$backend_emit" = "exe" ]; then
  cleanup_backend_exe_sidecar "$out_path"
fi

reported_out_path="$out_path"
run_out_path="$out_path"
if [ "$backend_emit" = "exe" ] && [ "$tooling_selfhost_launcher_requested" = "1" ]; then
  emit_tooling_selfhost_launcher "$tooling_selfhost_launcher_out_path" "$out_path"
  reported_out_path="$tooling_selfhost_launcher_out_path"
  ensure_chengc_output_exists "exe" "$reported_out_path"
fi

if [ "$backend_emit" = "obj" ]; then
  echo "chengc obj ok: $out_path"
else
  echo "chengc exe ok: $reported_out_path"
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
  echo "[chengc] run(host-runner): $run_out_path" >&2
  ${TOOLING_SELF_BIN:-artifacts/tooling_cmd/cheng_tooling} backend_host_runner run-host --exe:"$run_out_path"
  exit $?
fi

echo "[chengc] run(file): $run_out_path" >&2
"$run_out_path"
