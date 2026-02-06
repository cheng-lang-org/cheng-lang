#!/usr/bin/env sh
set -euo pipefail

usage() {
  cat <<'EOF'
Usage:
  src/tooling/chengc.sh <file.cheng> [--name:<exeName>] [--jobs:<N>]
                        [--manifest:<path>] [--lock:<path>] [--registry:<path>]
                        [--package:<id>] [--channel:<edge|stable|lts>]
                        [--lock-format:<toml|yaml|json>] [--meta-out:<path>]
                        [--buildmeta:<path>]
                        [--emit-c] [--emit-c-modules] [--modules-out:<path>] [--c-out:<path>] [--backend:c]
                        [--backend:obj] [--emit-obj] [--obj-out:<path>]
                        [--linker:<self|system>]
                        [--target:<triple>]
                        [--pkg-cache:<dir>] [--skip-pkg] [--verify] [--ledger:<path>]
                        [--source-listen:<addr>] [--source-peer:<addr>]
                        [--mm:<orc|off>] [--orc|--off]

Notes:
  - Default (`CHENG_MM=orc`): uses the self-developed backend (`--backend:obj`) and emits an executable.
  - Direct C pipeline (`--backend:c`) requires `./stage1_runner` as the frontend and a system C compiler.
  - `--jobs:<N>` sets module parallelism (defaults to logical cores; parallel is on by default when modules emit).
  - `CHENG_MODULE_JOBS_CAP` caps module parallelism (defaults to 4 when `CHENG_BUILD_TIMEOUT_TOTAL` is set).
  - `CHENG_BUILD_MODULE_TIMEOUT` sets per-module timeout (defaults to `CHENG_BUILD_TIMEOUT`).
  - `CHENG_C_MODULES_SINGLEPASS=1` uses a single stage1 pass to emit all C modules (falls back if unsupported).
  - `CHENG_C_MODULES_WRITE_KEYS=0` disables key file generation for single-pass C modules.
  - `CHENG_BUILD_TIMEOUT_TOTAL` bounds the single-pass module emit timeout.
  - `--backend:c` defaults to `--emit-c-modules` (set `CHENG_EMIT_C_MODULES=0` to disable).
  - `CHENG_DEPS_MODE=deps|deps-list` controls dependency output; C backend defaults to `deps-list`.
  - When `--manifest/--lock` is provided, build metadata is generated (default `chengcache/<name>.buildmeta.toml`).
  - `--skip-pkg` skips `cheng_pkg` generation/verification and uses existing lock/pkgmeta files.
  - When lock is present, dependencies are fetched into package cache and `CHENG_PKG_ROOTS` is set.
  - `--verify` validates buildmeta/lock/pkgmeta/snapshot; `--ledger` optionally verifies the ledger.
  - `--source-peer`/`--source-listen` forward to cheng_pkg_fetch for source-format packages.
  - Memory model can be set via `--mm:<orc|off>` (default orc; set `--off`/`CHENG_MM=off` only for fallback/diagnostics).
  - `--emit-c` emits C via stage1 (`stage1_runner --mode:c`) and skips link (default output: `chengcache/<name>.c`).
  - `--backend:c` uses the direct C backend (stage1 emit C + cc).
  - `--backend:obj` uses the self-developed backend (stage1 -> MIR -> obj). `--emit-obj` emits a relocatable `.o` and skips link.
    Target defaults to `auto` (detected from current host platform); override via `--target:<triple>` or `CHENG_BACKEND_TARGET`.
  - `--emit-c-modules` emits per-module C into `chengcache/<name>.modules` (override with `--modules-out`).
  - `CHENG_MODSYM_CHECK=0` disables module symbol duplicate checking.
  - ASM/HRT/stage0c and `--backend:hybrid` have been removed from this entrypoint.
EOF
}

run_with_timeout() {
  seconds="$1"
  shift || true
  case "$seconds" in
    ''|*[!0-9]*)
      "$@"
      return $?
      ;;
  esac
  if [ "$seconds" -le 0 ] 2>/dev/null; then
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
    124|137|142|143) return 0 ;;
    *) return 1 ;;
  esac
}

module_jobs_cap="${CHENG_MODULE_JOBS_CAP:-}"
if [ -z "$module_jobs_cap" ] && [ -n "${CHENG_BUILD_TIMEOUT_TOTAL:-}" ]; then
  module_jobs_cap="4"
fi
include_system_module="1"
case "${CHENG_C_SYSTEM:-}" in
  0|off|false|no)
    include_system_module="0"
    ;;
esac

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
    ''|*[!0-9]*)
      jobs="4"
      ;;
  esac
  if [ "$jobs" -lt 1 ] 2>/dev/null; then
    jobs="4"
  fi
  if [ -n "$module_jobs_cap" ] && [ "$module_jobs_cap" -gt 0 ] 2>/dev/null; then
    if [ "$jobs" -gt "$module_jobs_cap" ] 2>/dev/null; then
      jobs="$module_jobs_cap"
    fi
  fi
  printf '%s\n' "$jobs"
}

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
emit_asm=""
emit_hrt=""
emit_c=""
explicit_emit_c=""
emit_obj=""
emit_modules=""
layout_out=""
asm_out=""
c_out=""
obj_out=""
asm_flags_cli=""
asm_target=""
asm_abi=""
backend=""
linker=""
modules_out=""
emit_c_modules=""
hybrid_map=""
hybrid_default="asm"
while [ "${1:-}" != "" ]; do
  case "$1" in
    --name:*)
      name="${1#--name:}"
      ;;
    --jobs:*)
      jobs="${1#--jobs:}"
      ;;
    --manifest:*)
      manifest="${1#--manifest:}"
      ;;
    --lock:*)
      lock="${1#--lock:}"
      ;;
    --registry:*)
      registry="${1#--registry:}"
      ;;
    --package:*)
      package_id="${1#--package:}"
      ;;
    --channel:*)
      channel="${1#--channel:}"
      ;;
    --lock-format:*)
      lock_format="${1#--lock-format:}"
      ;;
    --meta-out:*)
      meta_out="${1#--meta-out:}"
      ;;
    --buildmeta:*)
      buildmeta="${1#--buildmeta:}"
      ;;
    --emit-asm)
      echo "[Error] --emit-asm has been removed; use --backend:obj/--emit-obj or --backend:c/--emit-c" 1>&2
      exit 2
      ;;
    --emit-hrt)
      echo "[Error] --emit-hrt has been removed; use --backend:obj (default)" 1>&2
      exit 2
      ;;
    --emit-c)
      emit_c="1"
      explicit_emit_c="1"
      ;;
    --emit-c-modules)
      emit_c="1"
      emit_c_modules="1"
      explicit_emit_c="1"
      ;;
    --emit-obj)
      emit_obj="1"
      backend="obj"
      ;;
    --emit-asm-modules)
      echo "[Error] --emit-asm-modules has been removed; use --emit-c-modules or --backend:obj" 1>&2
      exit 2
      ;;
    --emit-hrt-modules)
      echo "[Error] --emit-hrt-modules has been removed; use --emit-c-modules or --backend:obj" 1>&2
      exit 2
      ;;
    --backend:hrt)
      echo "[Error] --backend:hrt has been removed; use --backend:obj (default)" 1>&2
      exit 2
      ;;
    --backend:c)
      backend="c"
      emit_c="1"
      ;;
    --backend:hybrid)
      echo "[Error] --backend:hybrid has been removed; use --backend:obj (default) or --backend:c" 1>&2
      exit 2
      ;;
    --backend:obj)
      backend="obj"
      ;;
    --linker:*)
      linker="${1#--linker:}"
      ;;
    --asm-out:*)
      echo "[Error] --asm-out has been removed; use --obj-out or --c-out" 1>&2
      exit 2
      ;;
    --c-out:*)
      c_out="${1#--c-out:}"
      ;;
    --obj-out:*)
      obj_out="${1#--obj-out:}"
      ;;
    --asm-flags:*)
      echo "[Error] --asm-flags has been removed; use CHENG_BACKEND_CFLAGS/CHENG_BACKEND_LDFLAGS or CFLAGS" 1>&2
      exit 2
      ;;
    --asm-target:*)
      echo "[Error] --asm-target has been removed; use --target:<triple>" 1>&2
      exit 2
      ;;
    --asm-abi:*)
      echo "[Error] --asm-abi has been removed; target ABI is inferred from --target" 1>&2
      exit 2
      ;;
    --modules-out:*)
      modules_out="${1#--modules-out:}"
      ;;
    --hybrid-map:*)
      echo "[Error] --hybrid-map has been removed with --backend:hybrid" 1>&2
      exit 2
      ;;
    --hybrid-default:*)
      echo "[Error] --hybrid-default has been removed with --backend:hybrid" 1>&2
      exit 2
      ;;
    --layout-out:*)
      echo "[Error] --layout-out has been removed with ASM/HRT pipeline" 1>&2
      exit 2
      ;;
    --target:*)
      asm_target="${1#--target:}"
      ;;
    --abi:*)
      echo "[Error] --abi has been removed; target ABI is inferred from --target" 1>&2
      exit 2
      ;;
    --pkg-cache:*)
      pkg_cache="${1#--pkg-cache:}"
      ;;
    --skip-pkg)
      skip_pkg="1"
      ;;
    --verify)
      verify_meta="1"
      ;;
    --ledger:*)
      ledger="${1#--ledger:}"
      ;;
    --source-listen:*)
      source_listen="${1#--source-listen:}"
      ;;
    --source-peer:*)
      source_peer_args="$source_peer_args --source-peer:${1#--source-peer:}"
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

if [ "$emit_asm" != "" ] || [ "$emit_hrt" != "" ] || [ "$emit_modules" != "" ] || \
   [ "$backend" = "hrt" ] || [ "$backend" = "hybrid" ] || \
   [ "$asm_out" != "" ] || [ "$asm_flags_cli" != "" ] || [ "$layout_out" != "" ] || \
   [ "$hybrid_map" != "" ] || [ "$hybrid_default" != "asm" ]; then
  echo "[Error] ASM/HRT/stage0c and --backend:hybrid have been removed; use --backend:obj (default) or --backend:c" 1>&2
  exit 2
fi

if [ "$mm" = "" ] && [ -z "${CHENG_MM:-}" ]; then
  mm="orc"
fi

if [ "$backend" = "c" ] && [ "$emit_c_modules" = "" ]; then
  if [ "${CHENG_EMIT_C_MODULES:-1}" != "0" ]; then
    emit_c_modules="1"
  fi
fi

# Under tight per-step budgets, module emission is counterproductive because
# each module compile still runs a full frontend pass. Prefer single-pass C.
if [ "$backend" = "c" ] && [ "$emit_c_modules" != "" ] && [ -z "${CHENG_FORCE_C_MODULES:-}" ]; then
  case "${CHENG_BUILD_TIMEOUT:-}" in
    ''|*[!0-9]*)
      ;;
    *)
      if [ "${CHENG_BUILD_TIMEOUT:-0}" -gt 0 ] && [ "${CHENG_BUILD_TIMEOUT:-0}" -le 60 ]; then
        emit_c_modules=""
        echo "[Warn] CHENG_BUILD_TIMEOUT<=60; disabling C modules (set CHENG_FORCE_C_MODULES=1 to override)" 1>&2
      fi
      ;;
  esac
fi

if [ "$backend" = "obj" ] && { [ "$emit_c" != "" ] || [ "$emit_c_modules" != "" ]; }; then
  echo "[Error] --backend:obj cannot combine with --emit-c/--emit-c-modules" 1>&2
  exit 2
fi

if [ "$emit_c_modules" != "" ] && [ -z "${CHENG_C_MODSYM_REQUIRED:-}" ]; then
  export CHENG_C_MODSYM_REQUIRED=1
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

if [ "$backend" = "" ] && [ "$emit_c" = "" ] && [ "$emit_c_modules" = "" ]; then
  case "${CHENGC_DEFAULT_BACKEND:-}" in
    ""|auto)
      backend="obj"
      ;;
    c|obj)
      backend="${CHENGC_DEFAULT_BACKEND}"
      ;;
    *)
      echo "[Error] invalid CHENGC_DEFAULT_BACKEND: ${CHENGC_DEFAULT_BACKEND} (expected auto|c|obj)" 1>&2
      exit 2
      ;;
  esac
fi

use_c_backend=""
if [ "$backend" = "obj" ]; then
  use_c_backend=""
else
  use_c_backend="1"
fi

c_frontend="${CHENG_C_FRONTEND:-}"
frontend="${CHENG_FRONTEND:-}"
stage1_fresh="0"
if [ -x "./stage1_runner" ]; then
  stage1_fresh="1"
  for src in \
    src/stage1/frontend_stage1.cheng \
    src/stage1/frontend_lib.cheng \
    src/stage1/parser.cheng \
    src/stage1/lexer.cheng \
    src/stage1/ast.cheng \
    src/stage1/semantics.cheng \
    src/stage1/monomorphize.cheng \
    src/stage1/ownership.cheng \
    src/stage1/c_codegen.cheng \
    src/stage1/diagnostics.cheng; do
    if [ -f "$src" ] && [ "$src" -nt "./stage1_runner" ]; then
      stage1_fresh="0"
      break
    fi
  done
fi
needs_c_frontend=""
if [ "$use_c_backend" != "" ] || [ "$emit_c" != "" ] || [ "$emit_c_modules" != "" ]; then
  needs_c_frontend="1"
fi
if [ "$needs_c_frontend" != "" ] && [ "$c_frontend" = "" ]; then
  if [ -x "./stage1_runner" ]; then
    if [ "$stage1_fresh" != "1" ]; then
      echo "[Warn] ./stage1_runner is older than stage1 sources; using it for C backend (consider src/tooling/bootstrap.sh)" 1>&2
    fi
    c_frontend="./stage1_runner"
  else
    echo "[Error] C backend requires ./stage1_runner (stage0 fallback removed)" 1>&2
    exit 2
  fi
fi
if [ "$use_c_backend" != "" ]; then
  if [ "$c_frontend" != "" ]; then
    frontend="$c_frontend"
  fi
fi
if [ "$needs_c_frontend" != "" ]; then
  if [ "$frontend" = "" ]; then
    frontend="$c_frontend"
  fi
  if [ ! -x "$frontend" ]; then
    echo "[Error] frontend not found or not executable: $frontend" 1>&2
    echo "  tip: build stage1_runner via src/tooling/bootstrap.sh, or set CHENG_C_FRONTEND=./stage1_runner" 1>&2
    exit 2
  fi
fi

runtime_inc="${CHENG_RT_INC:-${CHENG_RUNTIME_INC:-runtime/include}}"
cc="${CC:-cc}"
if [ -z "${CFLAGS:-}" ]; then
  cflags="-O2"
else
  cflags="$CFLAGS"
fi
cflags="$cflags -Wno-incompatible-pointer-types -Wno-incompatible-pointer-types-discards-qualifiers"
cflags_args=""
if [ -n "$cflags" ]; then
  for flag in $cflags; do
    cflags_args="$cflags_args \"$flag\""
  done
fi

asm_cc="${CHENG_ASM_CC:-$cc}"
asm_flags_env="${CHENG_ASM_FLAGS:-}"
asm_target_env="${CHENG_ASM_TARGET:-}"
if [ -n "$asm_target" ]; then
  asm_target_env="$asm_target"
fi
asm_flags="$cflags"
if [ -n "$asm_target_env" ]; then
  asm_flags="$asm_flags --target=$asm_target_env"
fi
if [ -n "$asm_flags_env" ]; then
  asm_flags="$asm_flags $asm_flags_env"
fi
if [ -n "$asm_flags_cli" ]; then
  asm_flags="$asm_flags $asm_flags_cli"
fi
asm_flags_args=""
if [ -n "$asm_flags" ]; then
  for flag in $asm_flags; do
    asm_flags_args="$asm_flags_args \"$flag\""
  done
fi

if [ -n "$mm" ]; then
  export CHENG_MM="$mm"
fi

warn_mm_stale() {
  if [ "${frontend:-}" != "./stage1_runner" ]; then
    return
  fi
  local mm_mode=""
  if [ -n "${CHENG_MM:-}" ]; then
    mm_mode="${CHENG_MM}"
  else
    mm_mode="orc"
  fi
  if [ "$mm_mode" != "off" ] && [ -x "./stage1_runner" ] && [ -f "src/stage1/c_codegen.cheng" ]; then
    if [ "src/stage1/c_codegen.cheng" -nt "./stage1_runner" ]; then
      echo "[Warn] CHENG_MM=$mm_mode but ./stage1_runner is older than src/stage1/c_codegen.cheng; consider running src/tooling/bootstrap.sh" 1>&2
    fi
  fi
}
warn_mm_stale

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

sha256_text() {
  if command -v shasum >/dev/null 2>&1; then
    printf '%s' "$1" | shasum -a 256 | awk '{print $1}'
    return
  fi
  if command -v sha256sum >/dev/null 2>&1; then
    printf '%s' "$1" | sha256sum | awk '{print $1}'
    return
  fi
  echo ""
}

write_key_file() {
  out="$1"
  tmp="${out}.tmp"
  cat >"$tmp"
  if [ -f "$out" ] && cmp -s "$tmp" "$out"; then
    rm -f "$tmp"
  else
    mv "$tmp" "$out"
  fi
}

write_asm_key() {
  out="$1"
  frontend_sha=""
  lock_sha=""
  meta_sha=""
  if [ -n "$frontend" ] && [ -f "$frontend" ]; then
    frontend_sha="$(sha256_file "$frontend")"
  fi
  if [ -n "$lock" ] && [ -f "$lock" ]; then
    lock_sha="$(sha256_file "$lock")"
  fi
  if [ -n "$meta_out" ] && [ -f "$meta_out" ]; then
    meta_sha="$(sha256_file "$meta_out")"
  fi
  {
    echo "frontend=$frontend"
    echo "frontend_sha256=$frontend_sha"
    echo "asm_target=$asm_target_env"
    echo "asm_abi=${CHENG_ASM_ABI:-}"
    echo "asm_rt=${CHENG_ASM_RT:-}"
    echo "hard_rt=${CHENG_HARD_RT:-}"
    echo "mm=${CHENG_MM:-}"
    echo "mm_strict=${CHENG_MM_STRICT:-}"
    echo "ownership_diags=${CHENG_OWNERSHIP_DIAGS:-}"
    echo "ownership_hints=${CHENG_OWNERSHIP_HINTS:-}"
    echo "compiler_version=${CHENG_COMPILER_VERSION:-}"
    echo "stage1_version=${CHENG_STAGE1_VERSION:-}"
    echo "stage1_hash=${CHENG_STAGE1_HASH:-}"
    echo "cheng_version=${CHENG_VERSION:-}"
    echo "target=${CHENG_TARGET:-}"
    echo "deps_resolve=${CHENG_DEPS_RESOLVE:-}"
    echo "pkg_roots=${CHENG_PKG_ROOTS:-}"
    echo "ide_root=${CHENG_IDE_ROOT:-}"
    echo "gui_root=${CHENG_GUI_ROOT:-}"
    echo "stage1_use_seed=${CHENG_STAGE1_USE_SEED:-}"
    echo "stage1_skip_sem=${CHENG_STAGE1_SKIP_SEM:-}"
    echo "stage1_skip_mono=${CHENG_STAGE1_SKIP_MONO:-}"
    echo "stage1_skip_ownership=${CHENG_STAGE1_SKIP_OWNERSHIP:-}"
    if [ -n "$manifest" ]; then
      echo "manifest=$manifest"
    fi
    if [ -n "$lock" ]; then
      echo "lock=$lock"
      echo "lock_sha256=$lock_sha"
    fi
    if [ -n "$meta_out" ]; then
      echo "pkgmeta=$meta_out"
      echo "pkgmeta_sha256=$meta_sha"
    fi
  } | write_key_file "$out"
}

write_build_key() {
  out="$1"
  frontend_sha=""
  lock_sha=""
  meta_sha=""
  if [ -n "$frontend" ] && [ -f "$frontend" ]; then
    frontend_sha="$(sha256_file "$frontend")"
  fi
  if [ -n "$lock" ] && [ -f "$lock" ]; then
    lock_sha="$(sha256_file "$lock")"
  fi
  if [ -n "$meta_out" ] && [ -f "$meta_out" ]; then
    meta_sha="$(sha256_file "$meta_out")"
  fi
  {
    echo "frontend=$frontend"
    echo "frontend_sha256=$frontend_sha"
    echo "cc=$cc"
    echo "cflags=$cflags"
    echo "runtime_inc=$runtime_inc"
    echo "deps_mode=$deps_mode"
    echo "deps_frontend=${CHENG_DEPS_FRONTEND:-}"
    echo "mm=${CHENG_MM:-}"
    echo "mm_strict=${CHENG_MM_STRICT:-}"
    echo "ownership_diags=${CHENG_OWNERSHIP_DIAGS:-}"
    echo "ownership_hints=${CHENG_OWNERSHIP_HINTS:-}"
    echo "compiler_version=${CHENG_COMPILER_VERSION:-}"
    echo "stage1_version=${CHENG_STAGE1_VERSION:-}"
    echo "stage1_hash=${CHENG_STAGE1_HASH:-}"
    echo "cheng_version=${CHENG_VERSION:-}"
    echo "target=${CHENG_TARGET:-}"
    echo "deps_resolve=${CHENG_DEPS_RESOLVE:-}"
    echo "pkg_roots=${CHENG_PKG_ROOTS:-}"
    echo "ide_root=${CHENG_IDE_ROOT:-}"
    echo "gui_root=${CHENG_GUI_ROOT:-}"
    echo "stage1_use_seed=${CHENG_STAGE1_USE_SEED:-}"
    echo "stage1_skip_sem=${CHENG_STAGE1_SKIP_SEM:-}"
    echo "stage1_skip_mono=${CHENG_STAGE1_SKIP_MONO:-}"
    echo "stage1_skip_ownership=${CHENG_STAGE1_SKIP_OWNERSHIP:-}"
    if [ -n "$manifest" ]; then
      echo "manifest=$manifest"
    fi
    if [ -n "$lock" ]; then
      echo "lock=$lock"
      echo "lock_sha256=$lock_sha"
    fi
    if [ -n "$meta_out" ]; then
      echo "pkgmeta=$meta_out"
      echo "pkgmeta_sha256=$meta_sha"
    fi
  } | write_key_file "$out"
}

write_c_key() {
  out="$1"
  key_frontend="${2:-}"
  if [ "$key_frontend" = "" ]; then
    key_frontend="$frontend"
  fi
  frontend_sha=""
  lock_sha=""
  meta_sha=""
  if [ -n "$key_frontend" ] && [ -f "$key_frontend" ]; then
    frontend_sha="$(sha256_file "$key_frontend")"
  fi
  if [ -n "$lock" ] && [ -f "$lock" ]; then
    lock_sha="$(sha256_file "$lock")"
  fi
  if [ -n "$meta_out" ] && [ -f "$meta_out" ]; then
    meta_sha="$(sha256_file "$meta_out")"
  fi
  {
    echo "frontend=$key_frontend"
    echo "frontend_sha256=$frontend_sha"
    echo "cc=$cc"
    echo "cflags=$cflags"
    echo "runtime_inc=$runtime_inc"
    echo "deps_mode=$deps_mode"
    echo "mm=${CHENG_MM:-}"
    echo "mm_strict=${CHENG_MM_STRICT:-}"
    echo "ownership_diags=${CHENG_OWNERSHIP_DIAGS:-}"
    echo "ownership_hints=${CHENG_OWNERSHIP_HINTS:-}"
    echo "c_system=${CHENG_C_SYSTEM:-}"
    echo "compiler_version=${CHENG_COMPILER_VERSION:-}"
    echo "stage1_version=${CHENG_STAGE1_VERSION:-}"
    echo "stage1_hash=${CHENG_STAGE1_HASH:-}"
    echo "cheng_version=${CHENG_VERSION:-}"
    echo "target=${CHENG_TARGET:-}"
    echo "deps_resolve=${CHENG_DEPS_RESOLVE:-}"
    echo "pkg_roots=${CHENG_PKG_ROOTS:-}"
    echo "ide_root=${CHENG_IDE_ROOT:-}"
    echo "gui_root=${CHENG_GUI_ROOT:-}"
    echo "stage1_use_seed=${CHENG_STAGE1_USE_SEED:-}"
    echo "stage1_skip_sem=${CHENG_STAGE1_SKIP_SEM:-}"
    echo "stage1_skip_mono=${CHENG_STAGE1_SKIP_MONO:-}"
    echo "stage1_skip_ownership=${CHENG_STAGE1_SKIP_OWNERSHIP:-}"
    if [ -n "$manifest" ]; then
      echo "manifest=$manifest"
    fi
    if [ -n "$lock" ]; then
      echo "lock=$lock"
      echo "lock_sha256=$lock_sha"
    fi
    if [ -n "$meta_out" ]; then
      echo "pkgmeta=$meta_out"
      echo "pkgmeta_sha256=$meta_sha"
    fi
  } | write_key_file "$out"
}

write_deps_list() {
  deps_file="$1"
  deps_list="$2"
  in_file="$3"
  tmp="${deps_list}.tmp"
  if [ -f "$deps_file" ]; then
    awk 'NF {print $0}' "$deps_file" >"$tmp"
  else
    : >"$tmp"
  fi
  printf '%s\n' "$in_file" >>"$tmp"
  norm_tmp="${tmp}.norm"
  : >"$norm_tmp"
  abs_path() {
    if command -v realpath >/dev/null 2>&1; then
      realpath "$1" 2>/dev/null || printf '%s' "$1"
      return
    fi
    case "$1" in
      /*) printf '%s' "$1" ;;
      *) printf '%s/%s' "$(pwd)" "$1" ;;
    esac
  }
  while IFS= read -r dep; do
    if [ -z "$dep" ]; then
      continue
    fi
    if [ -f "$dep" ]; then
      dep="$(abs_path "$dep")"
    fi
    printf '%s\n' "$dep" >>"$norm_tmp"
  done <"$tmp"
  sort -u "$norm_tmp" >"${tmp}.sorted"
  if [ -f "$deps_list" ] && cmp -s "${tmp}.sorted" "$deps_list"; then
    rm -f "$tmp" "$norm_tmp" "${tmp}.sorted"
  else
    mv "${tmp}.sorted" "$deps_list"
    rm -f "$tmp" "$norm_tmp"
  fi
}

pick_deps_frontend() {
  if [ -n "${CHENG_DEPS_FRONTEND:-}" ]; then
    printf '%s\n' "$CHENG_DEPS_FRONTEND"
    return 0
  fi
  if [ -n "${1:-}" ]; then
    printf '%s\n' "$1"
    return 0
  fi
  if [ -n "${c_frontend:-}" ] && [ -x "${c_frontend:-}" ]; then
    printf '%s\n' "$c_frontend"
    return 0
  fi
  if [ -x "./stage1_runner" ]; then
    printf '%s\n' "./stage1_runner"
    return 0
  fi
  return 1
}

module_tag() {
  mod_path="$1"
  base="$(basename "$mod_path")"
  base="${base%.cheng}"
  path_hash="$(sha256_text "$mod_path")"
  if [ -z "$path_hash" ]; then
    path_hash="$(printf '%s' "$mod_path" | tr '/:' '__' | tr -c 'A-Za-z0-9._-' '_')"
  fi
  printf '%s-%s' "$base" "$path_hash"
}

hybrid_backend_for() {
  mod_path="$1"
  default_backend="$hybrid_default"
  if [ "$default_backend" = "" ]; then
    default_backend="asm"
  fi
  backend="$default_backend"
  if [ "$hybrid_map" != "" ]; then
    if [ ! -f "$hybrid_map" ]; then
      echo "[Error] hybrid map not found: $hybrid_map" 1>&2
      exit 2
    fi
    backend="$(awk -v m="$mod_path" '
      /^[[:space:]]*#/ {next}
      NF >= 2 && $1 == m {print $2; exit}
      NF >= 2 && m ~ ("/" $1 "$") {print $2; exit}
      NF >= 2 && $1 ~ ("/" m "$") {print $2; exit}
    ' "$hybrid_map")"
    if [ "$backend" = "" ]; then
      backend="$default_backend"
    fi
  fi
  backend="$(printf "%s" "$backend" | tr 'A-Z' 'a-z')"
  case "$backend" in
    c|asm)
      ;;
    *)
      echo "[Error] invalid hybrid backend for $mod_path: $backend" 1>&2
      exit 2
      ;;
  esac
  printf "%s" "$backend"
}

compile_module_asm() {
  mod_path="$1"
  mod_out="$2"
  key_out="$3"
  modsym_out="${4:-}"
  obj_out="${5:-}"
  mod_frontend="${6:-$frontend}"
  key_tmp="${key_out}.tmp"
  asm_prefix=""
  if [ ! -f "$mod_path" ]; then
    echo "[Error] module not found: $mod_path" 1>&2
    rm -f "$key_tmp"
    return 1
  fi
  module_sha="$(sha256_file "$mod_path")"
  asm_key_sha="$(sha256_file "$asm_key")"
  {
    echo "module=$mod_path"
    echo "module_sha256=$module_sha"
    echo "asm_key=$asm_key"
    echo "asm_key_sha256=$asm_key_sha"
  } >"$key_tmp"
  if [ -f "$key_out" ] && cmp -s "$key_tmp" "$key_out" && [ -s "$mod_out" ]; then
    if [ -n "$modsym_out" ] && [ ! -s "$modsym_out" ]; then
      rm -f "$key_tmp"
    elif [ -n "$obj_out" ] && [ ! -s "$obj_out" ]; then
      rm -f "$key_tmp"
    else
      rm -f "$key_tmp"
      return 0
    fi
  fi
  rm -f "$mod_out"
  if [ -n "$modsym_out" ]; then
    rm -f "$modsym_out"
    rc="0"
    mod_timeout="${CHENG_BUILD_MODULE_TIMEOUT:-${CHENG_BUILD_TIMEOUT:-}}"
    run_with_timeout "$mod_timeout" env CHENG_ASM_MODULE="$mod_path" CHENG_ASM_MODSYM_OUT="$modsym_out" \
      CHENG_ASM_MODULE_PREFIX="$asm_prefix" "$mod_frontend" --mode:"$emit_mode" --file:"$in" --out:"$mod_out" || rc="$?"
    if [ "$rc" -ne 0 ]; then
      if is_timeout_status "$rc"; then
        echo "[Warn] asm module timed out: $mod_path" 1>&2
      else
        echo "[Error] asm module failed ($rc): $mod_path" 1>&2
      fi
      rm -f "$key_tmp"
      return "$rc"
    fi
  else
  rc="0"
  mod_timeout="${CHENG_BUILD_MODULE_TIMEOUT:-${CHENG_BUILD_TIMEOUT:-}}"
  run_with_timeout "$mod_timeout" env CHENG_ASM_MODULE="$mod_path" CHENG_ASM_MODULE_PREFIX="$asm_prefix" \
    "$mod_frontend" --mode:"$emit_mode" --file:"$in" --out:"$mod_out" || rc="$?"
  if [ "$rc" -ne 0 ]; then
    if is_timeout_status "$rc"; then
      echo "[Warn] asm module timed out: $mod_path" 1>&2
    else
      echo "[Error] asm module failed ($rc): $mod_path" 1>&2
    fi
    rm -f "$key_tmp"
    return "$rc"
  fi
  fi
  if [ ! -s "$mod_out" ]; then
    echo "[Error] missing asm output: $mod_out" 1>&2
    rm -f "$key_tmp"
    return 1
  fi
  if [ -n "$modsym_out" ] && [ ! -s "$modsym_out" ]; then
    if [ -n "${CHENG_ASM_MODSYM_REQUIRED:-}" ]; then
      echo "[Error] missing module symbol output: $modsym_out" 1>&2
      rm -f "$key_tmp"
      return 1
    fi
    echo "[Warn] missing module symbol output: $modsym_out" 1>&2
  fi
  if [ -n "$obj_out" ]; then
    rm -f "$obj_out"
    "$asm_cc" $asm_flags -c "$mod_out" -o "$obj_out"
    if [ ! -s "$obj_out" ]; then
      echo "[Error] missing object output: $obj_out" 1>&2
      rm -f "$key_tmp"
      return 1
    fi
  fi
  mv "$key_tmp" "$key_out"
  return 0
}

compile_module_c() {
  mod_path="$1"
  mod_out="$2"
  obj_out="$3"
  key_out="$4"
  modsym_out="${5:-}"
  mod_frontend="${6:-$frontend}"
  entry_path="$in"
  case "${CHENG_C_MODULE_ENTRY:-}" in
    1|true|yes|module|mod)
      entry_path="$mod_path"
      ;;
  esac
  key_tmp="${key_out}.tmp"
  had_ok="0"
  if [ -s "$key_out" ] && [ -s "$mod_out" ]; then
    had_ok="1"
  fi
  if [ ! -f "$mod_path" ]; then
    echo "[Error] module not found: $mod_path" 1>&2
    rm -f "$key_tmp"
    return 1
  fi
  module_sha="$(sha256_file "$mod_path")"
  c_key_sha="$(sha256_file "$c_key")"
  {
    echo "module=$mod_path"
    echo "module_sha256=$module_sha"
    echo "c_key=$c_key"
    echo "c_key_sha256=$c_key_sha"
  } >"$key_tmp"
  if [ -f "$key_out" ] && cmp -s "$key_tmp" "$key_out" && [ -s "$mod_out" ]; then
    if [ -n "$modsym_out" ] && [ ! -s "$modsym_out" ]; then
      rm -f "$key_tmp"
    elif [ -z "$obj_out" ] || [ -s "$obj_out" ]; then
      rm -f "$key_tmp"
      return 0
    fi
    rm -f "$key_tmp"
  fi
  rm -f "$mod_out"
  if [ -n "$obj_out" ]; then
    rm -f "$obj_out"
  fi
  if [ -n "$modsym_out" ]; then
    rm -f "$modsym_out"
  fi
  if [ -n "${CHENG_MODULE_TRACE:-}" ]; then
    echo "[Info] c module: $mod_path" 1>&2
  fi
  rc="0"
  mod_timeout="${CHENG_BUILD_MODULE_TIMEOUT:-${CHENG_BUILD_TIMEOUT:-}}"
  warmup_timeout="${CHENG_BUILD_MODULE_WARMUP_TIMEOUT:-${CHENG_BUILD_WARMUP_TIMEOUT:-}}"
  case "$warmup_timeout" in
    ''|*[!0-9]*)
      warmup_timeout=""
      ;;
  esac
  if [ -n "$modsym_out" ]; then
    run_with_timeout "$mod_timeout" env CHENG_C_MODULE="$mod_path" CHENG_C_MODSYM_OUT="$modsym_out" \
      "$mod_frontend" --mode:c --file:"$entry_path" --out:"$mod_out" || rc="$?"
  else
    run_with_timeout "$mod_timeout" env CHENG_C_MODULE="$mod_path" \
      "$mod_frontend" --mode:c --file:"$entry_path" --out:"$mod_out" || rc="$?"
  fi
  if [ "$rc" -ne 0 ] && is_timeout_status "$rc" && [ "$had_ok" = "0" ] && [ -n "$warmup_timeout" ]; then
    case "$mod_timeout" in
      ''|*[!0-9]*)
        ;;
      *)
        if [ "$warmup_timeout" -le "$mod_timeout" ] 2>/dev/null; then
          warmup_timeout=""
        fi
        ;;
    esac
    if [ -n "$warmup_timeout" ]; then
      echo "[Info] warmup c module (${warmup_timeout}s): $mod_path" 1>&2
      rc="0"
      if [ -n "$modsym_out" ]; then
        run_with_timeout "$warmup_timeout" env CHENG_C_MODULE="$mod_path" CHENG_C_MODSYM_OUT="$modsym_out" \
          "$mod_frontend" --mode:c --file:"$entry_path" --out:"$mod_out" || rc="$?"
      else
        run_with_timeout "$warmup_timeout" env CHENG_C_MODULE="$mod_path" \
          "$mod_frontend" --mode:c --file:"$entry_path" --out:"$mod_out" || rc="$?"
      fi
    fi
  fi
  if [ "$rc" -ne 0 ]; then
    if is_timeout_status "$rc"; then
      echo "[Warn] c module timed out: $mod_path" 1>&2
    else
      echo "[Error] c module failed ($rc): $mod_path" 1>&2
    fi
    rm -f "$key_tmp"
    return "$rc"
  fi
  if [ ! -s "$mod_out" ]; then
    echo "[Error] missing C output: $mod_out" 1>&2
    rm -f "$key_tmp"
    return 1
  fi
  if [ -n "$modsym_out" ] && [ ! -s "$modsym_out" ]; then
    if [ -n "${CHENG_C_MODSYM_REQUIRED:-}" ]; then
      echo "[Error] missing module symbol output: $modsym_out" 1>&2
      rm -f "$key_tmp"
      return 1
    fi
    echo "[Warn] missing module symbol output: $modsym_out" 1>&2
  fi
  if [ -n "$obj_out" ]; then
    "$cc" $cflags -I"$runtime_inc" -Isrc/runtime/native -c "$mod_out" -o "$obj_out"
    if [ ! -s "$obj_out" ]; then
      echo "[Error] missing object output: $obj_out" 1>&2
      rm -f "$key_tmp"
      return 1
    fi
  fi
  if [ ! -f "$key_tmp" ]; then
    {
      echo "module=$mod_path"
      echo "module_sha256=$module_sha"
      echo "c_key=$c_key"
      echo "c_key_sha256=$c_key_sha"
    } >"$key_tmp"
  fi
  mv "$key_tmp" "$key_out"
  return 0
}

check_modsym_duplicates() {
  modules_map="$1"
  modsym_check="$(printf '%s' "${CHENG_MODSYM_CHECK:-1}" | tr 'A-Z' 'a-z')"
  case "$modsym_check" in
    0|false|no|off)
      return 0
      ;;
  esac
  if [ -z "$modules_map" ] || [ ! -s "$modules_map" ]; then
    return 0
  fi
  tmp="$(mktemp)"
  obj_tmp="$(mktemp)"
  while IFS="$(printf '\t')" read -r col1 col2 col3 col4 col5; do
    mod_path="$col1"
    modsym_out="$col5"
    if [ -z "$modsym_out" ]; then
      modsym_out="$col4"
    fi
    if [ -z "$modsym_out" ]; then
      modsym_out="$col3"
    fi
    obj_out=""
    if [ -n "$col5" ]; then
      obj_out="$col4"
    elif [ -n "$col4" ] && [ -n "$col3" ]; then
      obj_out="$col3"
    fi
    if [ -n "$obj_out" ]; then
      printf '%s\t%s\n' "$mod_path" "$obj_out" >>"$obj_tmp"
    fi
    if [ -z "$modsym_out" ] || [ ! -s "$modsym_out" ]; then
      continue
    fi
    awk -v mod="$mod_path" '
      /^func / { sym=substr($0,6); if (sym!="") print sym "\tfunc\t" mod; next }
      /^global / { sym=substr($0,8); if (sym!="") print sym "\tglobal\t" mod; next }
    ' "$modsym_out" >>"$tmp"
  done <"$modules_map"
  if [ ! -s "$tmp" ]; then
    rm -f "$tmp" "$obj_tmp"
    return 0
  fi
  dup="$(awk -F '\t' '
    {
      sym=$1; kind=$2; mod=$3
      if (sym=="" || mod=="") next
      if (!(sym in first_mod)) {
        first_mod[sym]=mod
        first_kind[sym]=kind
        next
      }
      if (first_mod[sym] != mod) {
        if (!(sym in mods)) {
          mods[sym]=first_mod[sym]
          kinds[sym]=first_kind[sym]
        }
        if (index("\t" mods[sym] "\t", "\t" mod "\t") == 0) {
          mods[sym]=mods[sym] "\t" mod
        }
        if (kind != "" && index("," kinds[sym] ",", "," kind ",") == 0) {
          kinds[sym]=kinds[sym] "," kind
        }
      }
    }
    END {
      for (sym in mods) {
        printf "%s\t%s\t%s\n", sym, kinds[sym], mods[sym]
      }
    }
  ' "$tmp")"
  rm -f "$tmp"
  if [ -n "$dup" ]; then
    echo "[Error] duplicate module symbols detected (modsym):" 1>&2
    printf '%s\n' "$dup" | awk -F '\t' -v obj_file="$obj_tmp" '
      BEGIN {
        while ((getline < obj_file) > 0) {
          obj[$1]=$2
        }
        close(obj_file)
      }
      {
        sym=$1; kind=$2
        printf "  %s (%s) in", sym, kind
        for (i=3; i<=NF; i++) {
          mod=$i
          if (mod=="") continue
          if (mod in obj && obj[mod] != "") {
            printf " %s[%s]", mod, obj[mod]
          } else {
            printf " %s", mod
          }
        }
        printf "\n"
      }
    ' 1>&2
    rm -f "$obj_tmp"
    return 1
  fi
  rm -f "$obj_tmp"
  return 0
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
  pkg_frontend="${CHENG_C_FRONTEND:-}"
  if [ "$pkg_frontend" = "" ]; then
    pkg_frontend="$c_frontend"
  fi
  if [ "$pkg_frontend" = "" ] && [ -x "./stage1_runner" ]; then
    pkg_frontend="./stage1_runner"
  fi
  if [ "$pkg_frontend" = "" ] || [ ! -x "$pkg_frontend" ]; then
    echo "[Error] building cheng_pkg requires stage1_runner (set CHENG_C_FRONTEND=./stage1_runner)" 1>&2
    exit 2
  fi
  mkdir -p chengcache
  "$pkg_frontend" --mode:c --file:src/tooling/cheng_pkg.cheng --out:chengcache/cheng_pkg.c
  pkg_c="chengcache/cheng_pkg.c"
  if [ ! -f "$pkg_c" ]; then
    echo "[Error] missing generated C file: $pkg_c" 1>&2
    exit 2
  fi
  "$cc" $cflags -I"$runtime_inc" -c "$pkg_c" -o chengcache/cheng_pkg.o
  "$cc" $cflags -c src/runtime/native/system_helpers.c -o chengcache/cheng_pkg.system_helpers.o
  "$cc" $cflags chengcache/cheng_pkg.o chengcache/cheng_pkg.system_helpers.o -o cheng_pkg
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

if [ "$buildmeta" = "" ] && [ "$lock" != "" -o "$meta_out" != "" ]; then
  buildmeta="chengcache/${name}.buildmeta.toml"
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

mkdir -p chengcache
cfile="chengcache/${name}.c"
obj="chengcache/${name}.o"
helpers_obj="chengcache/${name}.system_helpers.o"
if [ "$c_out" != "" ]; then
  cfile="$c_out"
fi
if [ "$backend" = "obj" ] && [ "$emit_obj" != "" ] && [ "$obj_out" != "" ]; then
  obj="$obj_out"
fi
if [ "$emit_asm" != "" ]; then
  if [ "$asm_out" = "" ]; then
    asm_out="chengcache/${name}.s"
  fi
  asm_dir="$(dirname "$asm_out")"
  if [ -n "$asm_dir" ]; then
    mkdir -p "$asm_dir"
  fi
fi

deps_mode="${CHENG_DEPS_MODE:-deps-list}"
deps_ext=""
case "$deps_mode" in
  list|deps-list|text|deps)
    deps_mode="deps-list"
    deps_ext="deps.list"
    ;;
  *)
    echo "[Error] invalid CHENG_DEPS_MODE: $deps_mode (use deps-list)" 1>&2
    exit 2
    ;;
esac

deps_file=""
deps_list=""
asm_key=""
build_key=""
deps_inputs=""
if [ "$emit_asm" != "" ]; then
  deps_file="chengcache/${name}.${deps_ext}"
  deps_list="chengcache/${name}.deps.list"
  asm_key="chengcache/${name}.asm.key"
  deps_frontend="$(pick_deps_frontend "$frontend")"
  if [ "$deps_frontend" = "$frontend" ] && [ "$backend" = "hybrid" ] && [ -x "$asm_frontend" ]; then
    deps_frontend="$asm_frontend"
  fi
  if [ ! -x "$deps_frontend" ]; then
    echo "[Error] deps frontend not found or not executable: $deps_frontend" 1>&2
    exit 2
  fi
  run_with_timeout "${CHENG_BUILD_TIMEOUT:-}" env CHENG_DEPS_RESOLVE=1 \
    "$deps_frontend" --mode:"$deps_mode" --file:"$in" --out:"$deps_file"
  status="$?"
  if [ "$status" -ne 0 ]; then
    if is_timeout_status "$status"; then
      echo "[Warn] deps resolve timed out after ${CHENG_BUILD_TIMEOUT:-}s (frontend: $deps_frontend)" 1>&2
    else
      echo "[Error] deps resolve failed (frontend: $deps_frontend)" 1>&2
    fi
    exit 2
  fi
  write_deps_list "$deps_file" "$deps_list" "$in"
  write_asm_key "$asm_key"
  while IFS= read -r dep; do
    if [ -n "$dep" ] && [ "$dep" != "$in" ]; then
      deps_inputs="${deps_inputs}    (input \"$dep\")
"
    fi
  done <"$deps_list"
  deps_inputs="${deps_inputs}    (input \"$deps_list\")
"
  deps_inputs="${deps_inputs}    (input \"$asm_key\")
"
else
  deps_file="chengcache/${name}.${deps_ext}"
  deps_list="chengcache/${name}.deps.list"
  build_key="chengcache/${name}.build.key"
  deps_frontend="$(pick_deps_frontend "$frontend")"
  if [ "$deps_frontend" = "$frontend" ] && [ "$backend" = "hybrid" ] && [ -x "$asm_frontend" ]; then
    deps_frontend="$asm_frontend"
  fi
  run_with_timeout "${CHENG_BUILD_TIMEOUT:-}" env CHENG_DEPS_RESOLVE=1 \
    "$deps_frontend" --mode:"$deps_mode" --file:"$in" --out:"$deps_file"
  status="$?"
  if [ "$status" -ne 0 ]; then
    if is_timeout_status "$status"; then
      echo "[Warn] deps resolve timed out after ${CHENG_BUILD_TIMEOUT:-}s (frontend: $deps_frontend)" 1>&2
    else
      echo "[Error] deps resolve failed (frontend: $deps_frontend)" 1>&2
    fi
    exit 2
  fi
  write_deps_list "$deps_file" "$deps_list" "$in"
  build_key_tmp="${build_key}.tmp"
  build_key_prev_ts="0"
  if [ -f "$build_key" ]; then
    build_key_prev_ts="$(stat -f %m "$build_key" 2>/dev/null || echo 0)"
  fi
  write_build_key "$build_key_tmp"
  build_key_same="0"
  if [ -f "$build_key" ] && cmp -s "$build_key_tmp" "$build_key"; then
    build_key_same="1"
  fi
  mv "$build_key_tmp" "$build_key"
  while IFS= read -r dep; do
    if [ -n "$dep" ] && [ "$dep" != "$in" ]; then
      deps_inputs="${deps_inputs}    (input \"$dep\")
"
    fi
  done <"$deps_list"
  deps_inputs="${deps_inputs}    (input \"$deps_list\")
"
  deps_inputs="${deps_inputs}    (input \"$build_key\")
"
fi

c_key=""
if [ "$emit_c_modules" != "" ] || [ "$backend" = "hybrid" ]; then
  c_key="chengcache/${name}.c.key"
  write_c_key "$c_key" "$c_frontend"
fi

if [ "$backend" = "obj" ]; then
  if [ "${CHENG_CLEAN_BACKEND_MVP_DRIVER_LOCAL:-1}" = "1" ] && [ "${CHENG_TOOLING_CLEANUP_DEPTH:-0}" = "0" ]; then
    export CHENG_TOOLING_CLEANUP_DEPTH=1
    cleanup_backend_driver_on_exit() {
      status=$?
      set +e
      sh src/tooling/cleanup_backend_mvp_driver_local.sh
      exit "$status"
    }
    trap cleanup_backend_driver_on_exit EXIT
  fi
  driver="$(sh src/tooling/backend_driver_path.sh)"
  backend_emit="exe"
  out_path="$name"
  if [ "$emit_obj" != "" ]; then
    backend_emit="obj"
    out_path="$obj"
    out_dir="$(dirname "$out_path")"
    if [ "$out_dir" != "" ] && [ ! -d "$out_dir" ]; then
      mkdir -p "$out_dir"
    fi
  fi
  backend_frontend="${CHENG_BACKEND_FRONTEND:-stage1}"
  backend_target_env="${CHENG_BACKEND_TARGET:-}"
  backend_target="$backend_target_env"
  if [ -n "$asm_target" ]; then
    backend_target="$asm_target"
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
    ""|self|system) ;;
    *)
      echo "[Error] invalid --linker:$backend_linker (expected self|system)" 1>&2
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
    rt_target="$backend_target"
    safe_target="$(printf '%s' "$rt_target" | tr -c 'A-Za-z0-9._-' '_' | tr -s '_')"
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
        CHENG_BACKEND_TARGET="$rt_target" \
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

  link_env=""
  if [ "$backend_linker" = "self" ] && [ "$backend_emit" = "exe" ]; then
    link_env="CHENG_BACKEND_LINKER=self CHENG_BACKEND_NO_RUNTIME_C=1"
  elif [ "$backend_linker" = "system" ]; then
    link_env="CHENG_BACKEND_LINKER=system"
  fi

  if [ -n "$backend_target" ]; then
    # shellcheck disable=SC2086
    env $runtime_env $link_env \
      CHENG_BACKEND_TARGET="$backend_target" \
      CHENG_BACKEND_EMIT="$backend_emit" \
      CHENG_BACKEND_FRONTEND="$backend_frontend" \
      CHENG_BACKEND_INPUT="$in" \
      CHENG_BACKEND_OUTPUT="$out_path" \
      CHENG_BACKEND_CC="${CHENG_BACKEND_CC:-$cc}" \
      CHENG_BACKEND_CFLAGS="${CHENG_BACKEND_CFLAGS:-$cflags}" \
      "$driver"
  else
    # shellcheck disable=SC2086
    env $runtime_env $link_env \
      CHENG_BACKEND_EMIT="$backend_emit" \
      CHENG_BACKEND_FRONTEND="$backend_frontend" \
      CHENG_BACKEND_INPUT="$in" \
      CHENG_BACKEND_OUTPUT="$out_path" \
      CHENG_BACKEND_CC="${CHENG_BACKEND_CC:-$cc}" \
      CHENG_BACKEND_CFLAGS="${CHENG_BACKEND_CFLAGS:-$cflags}" \
      "$driver"
  fi
  if [ "$backend_emit" = "obj" ]; then
    echo "chengc obj ok: $out_path"
  else
    echo "chengc obj ok: ./$name"
  fi
  exit 0
fi

if [ "$use_c_backend" != "" ]; then
  link_c_backend="1"
  if [ "$explicit_emit_c" != "" ]; then
    link_c_backend=""
  fi

  bin_newer_than_deps() {
    bin="$1"
    deps="$2"
    if [ ! -f "$bin" ] || [ ! -f "$deps" ]; then
      return 1
    fi
    while IFS= read -r dep; do
      if [ -z "$dep" ]; then
        continue
      fi
      if [ -f "$dep" ] && [ "$dep" -nt "$bin" ]; then
        return 1
      fi
    done <"$deps"
    return 0
  }

  if [ -n "${build_key_same:-}" ] && [ "$build_key_same" = "1" ] && \
     [ -f "$name" ] && bin_newer_than_deps "$name" "$deps_list"; then
    name_ts="$(stat -f %m "$name" 2>/dev/null || echo 0)"
    if [ "${build_key_prev_ts:-0}" -gt 0 ] && [ "$name_ts" -ge "${build_key_prev_ts:-0}" ]; then
      echo "chengc c cached: ./$name"
      exit 0
    fi
  fi
  if [ -n "${build_key_same:-}" ] && [ "$build_key_same" = "1" ] && \
     [ -f "$name" ] && bin_newer_than_deps "$name" "$deps_list" && [ "${build_key_prev_ts:-0}" -le 0 ]; then
    echo "chengc c cached: ./$name"
    exit 0
  fi
  if [ "$emit_c_modules" != "" ]; then
    if [ "$modules_out" = "" ]; then
      modules_out="chengcache/${name}.modules"
    fi
    mkdir -p "$modules_out"
    modules_map="${modules_out}/modules.map"
    singlepass_ok="0"
    if [ "${CHENG_C_MODULES_SINGLEPASS:-0}" != "0" ] && [ "$c_frontend" = "./stage1_runner" ] && [ -x "$c_frontend" ]; then
      modsym_dir="${CHENG_C_MODSYM_DIR:-}"
      if [ -z "$modsym_dir" ]; then
        modsym_dir="${modules_out}/modsym"
      fi
      modules_timeout="${CHENG_BUILD_TIMEOUT_TOTAL:-${CHENG_BUILD_TIMEOUT:-}}"
      rm -f "$modules_map"
      if run_with_timeout "$modules_timeout" env CHENG_C_MODULES_LIST="$deps_list" CHENG_C_MODULES_MAP="$modules_map" \
        CHENG_C_MODSYM_DIR="$modsym_dir" "$c_frontend" --mode:c-modules --file:"$in" --out:"$modules_out"; then
        if [ -s "$modules_map" ]; then
          singlepass_ok="1"
        fi
      fi
      if [ "$singlepass_ok" = "1" ] && [ -s "$modules_map" ]; then
        case "${CHENG_C_MODULES_WRITE_KEYS:-1}" in
          0|false|no|off)
            ;;
          *)
            if [ -n "$c_key" ] && [ -f "$c_key" ]; then
              c_key_sha="$(sha256_file "$c_key")"
              while IFS="$(printf '\t')" read -r mod_path mod_out mod_obj modsym; do
                if [ -z "$mod_path" ] || [ -z "$mod_out" ]; then
                  continue
                fi
                if [ ! -f "$mod_path" ] || [ ! -s "$mod_out" ]; then
                  continue
                fi
                case "$mod_out" in
                  *.c)
                    key_out="${mod_out%.c}.key"
                    ;;
                  *)
                    tag="$(module_tag "$mod_path")"
                    key_out="${modules_out}/${tag}.key"
                    ;;
                esac
                module_sha="$(sha256_file "$mod_path")"
                {
                  echo "module=$mod_path"
                  echo "module_sha256=$module_sha"
                  echo "c_key=$c_key"
                  echo "c_key_sha256=$c_key_sha"
                } >"$key_out"
              done <"$modules_map"
            fi
            ;;
        esac
      fi
      if [ "$singlepass_ok" != "1" ]; then
        echo "[Warn] c-modules single-pass failed; falling back to per-module frontend" 1>&2
        rm -f "$modules_map"
      fi
    fi
    if [ "$singlepass_ok" != "1" ]; then
      modules_map_tmp="${modules_map}.tmp"
      : >"$modules_map_tmp"
      jobs_limit="${jobs:-1}"
      case "$jobs_limit" in
        ''|*[!0-9]*)
          jobs_limit=1
          ;;
      esac
      if [ "$jobs_limit" -lt 1 ]; then
        jobs_limit=1
      fi
      pids=""
      running=0
      fail=0
      wait_one() {
        if [ "$running" -eq 0 ]; then
          return
        fi
        set -- $pids
        pid="$1"
        shift || true
        pids="$*"
        running=$((running - 1))
        if ! wait "$pid"; then
          fail=1
        fi
      }
      while IFS= read -r mod; do
        if [ "$fail" -ne 0 ]; then
          break
        fi
        if [ -z "$mod" ]; then
          continue
        fi
        if [ "$include_system_module" != "1" ]; then
          case "$mod" in
            */system.cheng)
              continue
              ;;
          esac
        fi
        tag="$(module_tag "$mod")"
        mod_out="${modules_out}/${tag}.c"
        key_out="${modules_out}/${tag}.key"
        obj_out=""
        if [ "$backend" = "c" ] && [ "$link_c_backend" != "" ]; then
          obj_out="${modules_out}/${tag}.o"
        fi
        modsym_out="${modules_out}/${tag}.modsym"
        printf '%s\t%s\t%s\t%s\n' "$mod" "$mod_out" "$obj_out" "$modsym_out" >>"$modules_map_tmp"
        compile_module_c "$mod" "$mod_out" "$obj_out" "$key_out" "$modsym_out" &
        pid="$!"
        pids="$pids $pid"
        running=$((running + 1))
        if [ "$running" -ge "$jobs_limit" ]; then
          wait_one
        fi
      done <"$deps_list"
      while [ "$running" -gt 0 ]; do
        wait_one
      done
      if [ "$fail" -ne 0 ]; then
        rm -f "$modules_map_tmp"
        exit 2
      fi
      mv "$modules_map_tmp" "$modules_map"
    fi
    modules_map_norm="${modules_map}.norm"
    : >"$modules_map_norm"
    while IFS="$(printf '\t')" read -r mod_path mod_out mod_obj modsym; do
      if [ -z "$mod_path" ] || [ -z "$mod_out" ]; then
        continue
      fi
      if [ -z "$modsym" ] && [ -n "$mod_obj" ]; then
        case "$mod_obj" in
          *.modsym)
            modsym="$mod_obj"
            mod_obj=""
            ;;
        esac
      fi
      if [ -z "$mod_obj" ]; then
        mod_obj="${mod_out%.c}.o"
      fi
      printf '%s\t%s\t%s\t%s\n' "$mod_path" "$mod_out" "$mod_obj" "$modsym" >>"$modules_map_norm"
    done <"$modules_map"
    mv "$modules_map_norm" "$modules_map"
    if ! check_modsym_duplicates "$modules_map"; then
      exit 2
    fi
    obj_list="$(mktemp)"
    compile_list="$(mktemp)"
    : >"$obj_list"
    : >"$compile_list"
    while IFS="$(printf '\t')" read -r mod_path mod_out mod_obj modsym; do
      if [ -z "$mod_out" ] || [ -z "$mod_obj" ]; then
        continue
      fi
      printf '%s\n' "$mod_obj" >>"$obj_list"
      if [ ! -s "$mod_obj" ] || [ "$mod_out" -nt "$mod_obj" ]; then
        printf '%s\t%s\n' "$mod_out" "$mod_obj" >>"$compile_list"
      fi
    done <"$modules_map"
    if [ -s "$compile_list" ]; then
      while IFS="$(printf '\t')" read -r cfile ofile; do
        "$cc" $cflags -I"$runtime_inc" -Isrc/runtime/native -c "$cfile" -o "$ofile"
      done <"$compile_list"
    fi
    obj_inputs="$(tr '\n' ' ' < "$obj_list")"
    rm -f "$obj_list" "$compile_list"
    if [ "$obj_inputs" = "" ]; then
      echo "[Error] missing module objects in $modules_map" 1>&2
      exit 2
    fi
    if [ "$backend" = "c" ] && [ "$link_c_backend" != "" ]; then
      helpers_obj="chengcache/${name}.system_helpers.o"
      "$cc" $cflags -I"$runtime_inc" -Isrc/runtime/native -c src/runtime/native/system_helpers.c -o "$helpers_obj"
      "$cc" $cflags $obj_inputs "$helpers_obj" -o "$name"
      if [ ! -f "$name" ]; then
        exit 2
      fi
      echo "chengc c modules ok: ./$name"
    else
      echo "chengc c modules ok: $modules_map"
    fi
    exit 0
  fi

  c_dir="$(dirname "$cfile")"
  if [ -n "$c_dir" ]; then
    mkdir -p "$c_dir"
  fi
  rm -f "$cfile"
  frontend_rc="0"
  run_with_timeout "${CHENG_BUILD_TIMEOUT:-}" "$frontend" --mode:c --file:"$in" --out:"$cfile" || frontend_rc="$?"
  if [ "$frontend_rc" -ne 0 ]; then
    if is_timeout_status "$frontend_rc"; then
      echo "[Error] frontend timed out after ${CHENG_BUILD_TIMEOUT:-}s: $frontend" 1>&2
    else
      echo "[Error] frontend failed ($frontend_rc): $frontend" 1>&2
    fi
    exit 2
  fi
  if [ ! -s "$cfile" ]; then
    echo "[Error] missing C output: $cfile" 1>&2
    exit 2
  fi
  if [ "$link_c_backend" != "" ]; then
    rm -f "$obj" "$helpers_obj"
    "$cc" $cflags -I"$runtime_inc" -Isrc/runtime/native -c "$cfile" -o "$obj"
    "$cc" $cflags -I"$runtime_inc" -Isrc/runtime/native -c src/runtime/native/system_helpers.c -o "$helpers_obj"
    "$cc" $cflags "$obj" "$helpers_obj" -o "$name"
    if [ ! -f "$name" ]; then
      exit 2
    fi
    echo "chengc c ok: ./$name"
  else
    echo "chengc c ok: $cfile"
  fi
  exit 0
fi

if [ "$emit_asm" != "" ]; then
  if [ -n "$asm_target" ]; then
    export CHENG_ASM_TARGET="$asm_target"
  fi
  if [ -n "$asm_abi" ]; then
    export CHENG_ASM_ABI="$asm_abi"
  fi
  emit_mode="asm"
  if [ "$emit_hrt" != "" ]; then
    emit_mode="hrt"
  fi
fi

if [ "$backend" = "hybrid" ]; then
  hybrid_default="$(printf "%s" "$hybrid_default" | tr 'A-Z' 'a-z')"
  case "$hybrid_default" in
    c|asm)
      ;;
    *)
      echo "[Error] invalid --hybrid-default: $hybrid_default (use c|asm)" 1>&2
      exit 2
      ;;
  esac
  if [ "$modules_out" = "" ]; then
    modules_out="chengcache/${name}.hybrid.modules"
  fi
  mkdir -p "$modules_out"
  if ! command -v "$cc" >/dev/null 2>&1; then
    echo "[Error] missing C compiler: $cc" 1>&2
    exit 2
  fi
  if ! command -v "$asm_cc" >/dev/null 2>&1; then
    echo "[Error] missing ASM compiler: $asm_cc" 1>&2
    exit 2
  fi
  modules_map="${modules_out}/modules.map"
  modules_map_tmp="${modules_map}.tmp"
  : >"$modules_map_tmp"
  jobs_limit="${jobs:-1}"
  case "$jobs_limit" in
    ''|*[!0-9]*)
      jobs_limit=1
      ;;
  esac
  if [ "$jobs_limit" -lt 1 ]; then
    jobs_limit=1
  fi
  if [ -z "${CHENG_FORCE_HYBRID_JOBS:-}" ]; then
    case "${CHENG_BUILD_TIMEOUT:-}" in
      ''|*[!0-9]*)
        ;;
      *)
        if [ "${CHENG_BUILD_TIMEOUT:-0}" -gt 0 ] && [ "${CHENG_BUILD_TIMEOUT:-0}" -le 60 ]; then
          jobs_limit=1
          echo "[Warn] CHENG_BUILD_TIMEOUT<=60; forcing hybrid jobs=1 (set CHENG_FORCE_HYBRID_JOBS=1 to override)" 1>&2
        fi
        ;;
    esac
  fi
  pids=""
  running=0
  fail=0
  wait_one() {
    if [ "$running" -eq 0 ]; then
      return
    fi
    set -- $pids
    pid="$1"
    shift || true
    pids="$*"
    running=$((running - 1))
    if ! wait "$pid"; then
      fail=1
    fi
  }
    while IFS= read -r mod; do
      if [ "$fail" -ne 0 ]; then
        break
      fi
      if [ -z "$mod" ]; then
        continue
      fi
    if [ "$include_system_module" != "1" ]; then
      case "$mod" in
        */system.cheng)
          continue
          ;;
      esac
    fi
    backend_choice="$(hybrid_backend_for "$mod")"
    tag="$(module_tag "$mod")"
    key_out="${modules_out}/${tag}.key"
    modsym_out="${modules_out}/${tag}.modsym"
    if [ "$backend_choice" = "c" ]; then
      mod_out="${modules_out}/${tag}.c"
      obj_out="${modules_out}/${tag}.o"
      printf '%s\t%s\t%s\t%s\t%s\n' "$mod" "$backend_choice" "$mod_out" "$obj_out" "$modsym_out" >>"$modules_map_tmp"
      compile_module_c "$mod" "$mod_out" "$obj_out" "$key_out" "$modsym_out" "$c_frontend" &
    else
      mod_out="${modules_out}/${tag}.s"
      obj_out="${modules_out}/${tag}.o"
      printf '%s\t%s\t%s\t%s\t%s\n' "$mod" "$backend_choice" "$mod_out" "$obj_out" "$modsym_out" >>"$modules_map_tmp"
      compile_module_asm "$mod" "$mod_out" "$key_out" "$modsym_out" "$obj_out" "$asm_frontend" &
    fi
    pid="$!"
    pids="$pids $pid"
    running=$((running + 1))
    if [ "$running" -ge "$jobs_limit" ]; then
      wait_one
    fi
  done <"$deps_list"
  while [ "$running" -gt 0 ]; do
    wait_one
  done
  if [ "$fail" -ne 0 ]; then
    rm -f "$modules_map_tmp"
    exit 2
  fi
  mv "$modules_map_tmp" "$modules_map"
  if ! check_modsym_duplicates "$modules_map"; then
    exit 2
  fi
  helpers_obj="${modules_out}/system_helpers.o"
  "$cc" $cflags -I"$runtime_inc" -Isrc/runtime/native -c src/runtime/native/system_helpers.c -o "$helpers_obj"
  obj_inputs="$(awk -F '\t' '{print $4}' "$modules_map" | tr '\n' ' ')"
  if [ "$obj_inputs" = "" ]; then
    echo "[Error] missing module objects in $modules_map" 1>&2
    exit 2
  fi
  "$cc" $cflags $obj_inputs "$helpers_obj" -o "$name"
  if [ ! -f "$name" ]; then
    exit 2
  fi
  echo "chengc hybrid ok: ./$name"
  exit 0
fi

if [ "$emit_modules" != "" ]; then
  if [ "$modules_out" = "" ]; then
    modules_out="chengcache/${name}.modules"
  fi
  mkdir -p "$modules_out"
  if [ "$layout_out" != "" ]; then
    layout_tmp="${modules_out}/layout_tmp.s"
    rm -f "$layout_tmp"
    if ! CHENG_ASM_LAYOUT_OUT="$layout_out" "$frontend" --mode:"$emit_mode" --file:"$in" --out:"$layout_tmp"; then
      exit 2
    fi
    rm -f "$layout_tmp"
  fi
  modules_map="${modules_out}/modules.map"
  modules_map_tmp="${modules_map}.tmp"
  : >"$modules_map_tmp"
  jobs_limit="${jobs:-1}"
  case "$jobs_limit" in
    ''|*[!0-9]*)
      jobs_limit=1
      ;;
  esac
  if [ "$jobs_limit" -lt 1 ]; then
    jobs_limit=1
  fi
  pids=""
  running=0
  fail=0
  wait_one() {
    if [ "$running" -eq 0 ]; then
      return
    fi
    set -- $pids
    pid="$1"
    shift || true
    pids="$*"
    running=$((running - 1))
    if ! wait "$pid"; then
      fail=1
    fi
  }
  while IFS= read -r mod; do
    if [ "$fail" -ne 0 ]; then
      break
    fi
    if [ -z "$mod" ]; then
      continue
    fi
    tag="$(module_tag "$mod")"
    mod_out="${modules_out}/${tag}.s"
    key_out="${modules_out}/${tag}.key"
    modsym_out="${modules_out}/${tag}.modsym"
    printf '%s\t%s\t%s\n' "$mod" "$mod_out" "$modsym_out" >>"$modules_map_tmp"
    compile_module_asm "$mod" "$mod_out" "$key_out" "$modsym_out" &
    pid="$!"
    pids="$pids $pid"
    running=$((running + 1))
    if [ "$running" -ge "$jobs_limit" ]; then
      wait_one
    fi
  done <"$deps_list"
  while [ "$running" -gt 0 ]; do
    wait_one
  done
  if [ "$fail" -ne 0 ]; then
    rm -f "$modules_map_tmp"
    exit 2
  fi
  mv "$modules_map_tmp" "$modules_map"
  if ! check_modsym_duplicates "$modules_map"; then
    exit 2
  fi
  if [ "$emit_hrt" != "" ]; then
    echo "chengc hrt modules ok: $modules_map"
  else
    echo "chengc asm modules ok: $modules_map"
  fi
  exit 0
fi

if [ "$emit_asm" != "" ]; then
  rm -f "$asm_out"
  if [ "$layout_out" != "" ]; then
    CHENG_ASM_LAYOUT_OUT="$layout_out" "$frontend" --mode:"$emit_mode" --file:"$in" --out:"$asm_out"
  else
    "$frontend" --mode:"$emit_mode" --file:"$in" --out:"$asm_out"
  fi
  if [ ! -f "$asm_out" ]; then
    echo "[Error] missing asm output: $asm_out" 1>&2
    exit 2
  fi
  if [ ! -s "$asm_out" ]; then
    echo "[Error] empty asm output: $asm_out" 1>&2
    exit 2
  fi
  if [ "$emit_hrt" != "" ]; then
    echo "chengc hrt ok: $asm_out"
  else
    echo "chengc asm ok: $asm_out"
  fi
  exit 0
fi


echo "[Error] unsupported backend: legacy IR pipeline removed" 1>&2
echo "  tip: use --backend:obj (default), --emit-obj, --backend:c, or --emit-c" 1>&2
exit 2
