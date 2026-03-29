#!/usr/bin/env sh
set -eu

usage() {
  cat <<'USAGE'
Usage:
  backend_driver_currentsrc_sidecar_wrapper.sh <input.cheng> --frontend:stage1 --emit:obj|exe --target:<triple> --linker:<mode> --output:<path> [compile flags]
  compile flags include:
    --build-track:<dev|release> --mm:<orc> --fn-sched:<ws> --ldflags:<text>
    --module-cache:<path> --multi-module-cache|--multi-module-cache:0|1
    --module-cache-unstable-allow|--module-cache-unstable-allow:0|1

Explicit sidecar compiler contract adapter. It accepts the pure CLI compile
surface used by Cheng sidecar runtime and translates it into the legacy env
compile surface for the pinned real Cheng compiler recorded in <wrapper>.meta.
USAGE
}

meta_path="$0.meta"

canonical_path() {
  p="$1"
  if [ "$p" = "" ]; then
    printf '\n'
    return 0
  fi
  if [ -d "$p" ]; then
    (CDPATH= cd -- "$p" 2>/dev/null && pwd -P) || printf '%s\n' "$p"
    return 0
  fi
  dir="$(dirname -- "$p")"
  base="$(basename -- "$p")"
  abs_dir="$(CDPATH= cd -- "$dir" 2>/dev/null && pwd -P)" || {
    printf '%s\n' "$p"
    return 0
  }
  printf '%s/%s\n' "$abs_dir" "$base"
}

read_meta_field() {
  key="$1"
  if [ ! -f "$meta_path" ]; then
    printf '\n'
    return 0
  fi
  sed -n "s/^${key}=//p" "$meta_path" | head -n 1
}

strict_stage0_lineage_dir() {
  printf '%s\n' "${TOOLING_ROOT:-$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)}/artifacts/backend_selfhost_self_obj/probe_currentsrc_proof"
}

strict_published_stage0_surface() {
  driver_path="$(canonical_path "$1")"
  case "$driver_path" in
    "$(strict_stage0_lineage_dir)"/cheng.stage1|\
    "$(strict_stage0_lineage_dir)"/cheng.stage2|\
    "$(strict_stage0_lineage_dir)"/cheng.stage2.proof)
      return 0
      ;;
  esac
  return 1
}

strict_direct_export_surface() {
  driver_path="$(canonical_path "$1")"
  if [ "$driver_path" = "" ] || [ ! -x "$driver_path" ]; then
    return 1
  fi
  command -v nm >/dev/null 2>&1 || return 1
  command -v awk >/dev/null 2>&1 || return 1
  surface_log="$(mktemp "${TMPDIR:-/tmp}/backend_driver_currentsrc_sidecar_wrapper.nm.XXXXXX")"
  set +e
  (nm -gU "$driver_path" 2>/dev/null || nm "$driver_path" 2>/dev/null) >"$surface_log"
  nm_status="$?"
  set -e
  if [ "$nm_status" -ne 0 ]; then
    rm -f "$surface_log"
    return 1
  fi
  if ! awk '
    BEGIN { need1=0; need2=0; need3=0; legacy=0; }
    {
      sym=$NF;
      gsub(/^_+/, "", sym);
      if (sym == "driver_export_build_emit_obj_from_file_stage1_target_impl") need1=1;
      if (sym == "driver_export_prefer_sidecar_builds") need2=1;
      if (sym == "driver_export_buildModuleFromFileStage1TargetRetained") need3=1;
      if (sym ~ /^driverProof/) legacy=1;
    }
    END { exit((need1 && need2 && need3 && !legacy) ? 0 : 1); }
  ' "$surface_log"; then
    rm -f "$surface_log"
    return 1
  fi
  rm -f "$surface_log"
  return 0
}

real_driver="${TOOLING_BUILD_GLOBAL_CURRENTSOURCE_REAL_DRIVER:-}"
if [ "$real_driver" = "" ]; then
  real_driver="$(read_meta_field sidecar_real_driver)"
fi
wrapper_self_path="$(canonical_path "$0")"
real_driver_path="$(canonical_path "$real_driver")"
if [ "$real_driver_path" != "" ] && [ "$wrapper_self_path" != "" ] && [ "$real_driver_path" = "$wrapper_self_path" ]; then
  echo "[backend_driver_currentsrc_sidecar_wrapper] invalid self-referential real sidecar driver contract: $real_driver" 1>&2
  exit 1
fi
if [ "$real_driver" = "" ] || [ ! -x "$real_driver" ]; then
  echo "[backend_driver_currentsrc_sidecar_wrapper] missing real sidecar driver contract: ${real_driver:-<unset>}" 1>&2
  exit 1
fi
if ! strict_published_stage0_surface "$real_driver" && ! strict_direct_export_surface "$real_driver"; then
  echo "[backend_driver_currentsrc_sidecar_wrapper] real sidecar driver missing strict direct-export surface: ${real_driver:-<unset>}" 1>&2
  exit 1
fi

input_path=""
frontend="stage1"
emit_mode=""
target=""
linker_mode="system"
build_track="dev"
mm="orc"
fn_sched="ws"
ldflags=""
module_cache=""
multi_module_cache="0"
module_cache_unstable_allow="0"
output_path=""
allow_no_main="0"
whole_program="0"
multi="0"
multi_force="0"
incremental="0"
jobs="${BACKEND_JOBS:-1}"
fn_jobs="${BACKEND_FN_JOBS:-$jobs}"
opt="0"
opt2="0"
opt_level="0"
generic_mode="dict"
generic_spec_budget="0"
generic_lowering="mir_dict"
sidecar_child_mode=""
sidecar_mode=""
sidecar_bundle=""
sidecar_compiler=""
sidecar_outer_compiler=""
no_runtime_c="${BACKEND_NO_RUNTIME_C:-0}"
runtime_obj="${BACKEND_RUNTIME_OBJ:-}"
wrapper_mode="cli"
env_sidecar_mode=""
env_sidecar_bundle=""
env_sidecar_compiler=""
env_sidecar_child_mode=""
env_sidecar_outer_compiler=""
env_sidecar_disable=""
env_sidecar_prefer=""
env_sidecar_force=""

while [ "${1:-}" != "" ]; do
  case "$1" in
    --help|-h)
      usage
      exit 0
      ;;
    --frontend:*)
      frontend="${1#--frontend:}"
      ;;
    --emit:*)
      emit_mode="${1#--emit:}"
      ;;
    --target:*)
      target="${1#--target:}"
      ;;
    --linker:*)
      linker_mode="${1#--linker:}"
      ;;
    --build-track:*)
      build_track="${1#--build-track:}"
      ;;
    --mm:*)
      mm="${1#--mm:}"
      ;;
    --fn-sched:*)
      fn_sched="${1#--fn-sched:}"
      ;;
    --ldflags:*)
      ldflags="${1#--ldflags:}"
      ;;
    --module-cache:*)
      module_cache="${1#--module-cache:}"
      ;;
    --output:*)
      output_path="${1#--output:}"
      ;;
    --out:*)
      output_path="${1#--out:}"
      ;;
    --allow-no-main)
      allow_no_main="1"
      ;;
    --whole-program)
      whole_program="1"
      ;;
    --no-whole-program)
      whole_program="0"
      ;;
    --multi)
      multi="1"
      ;;
    --no-multi)
      multi="0"
      ;;
    --multi-force)
      multi_force="1"
      ;;
    --multi-module-cache)
      multi_module_cache="1"
      ;;
    --multi-module-cache:*)
      multi_module_cache="${1#--multi-module-cache:}"
      ;;
    --no-multi-module-cache)
      multi_module_cache="0"
      ;;
    --module-cache-unstable-allow)
      module_cache_unstable_allow="1"
      ;;
    --module-cache-unstable-allow:*)
      module_cache_unstable_allow="${1#--module-cache-unstable-allow:}"
      ;;
    --no-module-cache-unstable-allow)
      module_cache_unstable_allow="0"
      ;;
    --no-multi-force)
      multi_force="0"
      ;;
    --incremental)
      incremental="1"
      ;;
    --no-incremental)
      incremental="0"
      ;;
    --jobs:*)
      jobs="${1#--jobs:}"
      ;;
    --fn-jobs:*)
      fn_jobs="${1#--fn-jobs:}"
      ;;
    --opt)
      opt="1"
      ;;
    --no-opt)
      opt="0"
      ;;
    --opt2)
      opt2="1"
      ;;
    --no-opt2)
      opt2="0"
      ;;
    --opt-level:*)
      opt_level="${1#--opt-level:}"
      ;;
    --generic-mode:*)
      generic_mode="${1#--generic-mode:}"
      ;;
    --generic-spec-budget:*)
      generic_spec_budget="${1#--generic-spec-budget:}"
      ;;
    --generic-lowering:*)
      generic_lowering="${1#--generic-lowering:}"
      ;;
    --sidecar-mode:*)
      sidecar_mode="${1#--sidecar-mode:}"
      ;;
    --sidecar-bundle:*)
      sidecar_bundle="${1#--sidecar-bundle:}"
      ;;
    --sidecar-compiler:*)
      sidecar_compiler="${1#--sidecar-compiler:}"
      ;;
    --sidecar-child-mode:*)
      sidecar_child_mode="${1#--sidecar-child-mode:}"
      ;;
    --sidecar-outer-compiler:*)
      sidecar_outer_compiler="${1#--sidecar-outer-compiler:}"
      ;;
    --no-runtime-c)
      no_runtime_c="1"
      ;;
    --no-runtime-c:*)
      no_runtime_c="${1#--no-runtime-c:}"
      ;;
    --runtime-obj:*)
      runtime_obj="${1#--runtime-obj:}"
      ;;
    -*)
      echo "[backend_driver_currentsrc_sidecar_wrapper] unknown arg: $1" 1>&2
      exit 2
      ;;
    *)
      if [ "$input_path" = "" ]; then
        input_path="$1"
      else
        echo "[backend_driver_currentsrc_sidecar_wrapper] multiple input paths are not supported" 1>&2
        exit 2
      fi
      ;;
  esac
  shift || true
done

if [ "$input_path" = "" ] && [ "$emit_mode" = "" ] && [ "$target" = "" ] && [ "$output_path" = "" ] && [ "${BACKEND_INPUT:-}" != "" ]; then
  wrapper_mode="env"
  input_path="${BACKEND_INPUT:-}"
  frontend="${BACKEND_FRONTEND:-$frontend}"
  emit_mode="${BACKEND_EMIT:-}"
  target="${BACKEND_TARGET:-}"
  linker_mode="${BACKEND_LINKER:-$linker_mode}"
  build_track="${BACKEND_BUILD_TRACK:-$build_track}"
  mm="${MM:-$mm}"
  fn_sched="${BACKEND_FN_SCHED:-$fn_sched}"
  ldflags="${BACKEND_LDFLAGS:-$ldflags}"
  module_cache="${BACKEND_MODULE_CACHE:-$module_cache}"
  multi_module_cache="${BACKEND_MULTI_MODULE_CACHE:-$multi_module_cache}"
  module_cache_unstable_allow="${BACKEND_MODULE_CACHE_UNSTABLE_ALLOW:-$module_cache_unstable_allow}"
  output_path="${BACKEND_OUTPUT:-}"
  allow_no_main="${BACKEND_ALLOW_NO_MAIN:-$allow_no_main}"
  whole_program="${BACKEND_WHOLE_PROGRAM:-$whole_program}"
  multi="${BACKEND_MULTI:-$multi}"
  multi_force="${BACKEND_MULTI_FORCE:-$multi_force}"
  incremental="${BACKEND_INCREMENTAL:-$incremental}"
  jobs="${BACKEND_JOBS:-$jobs}"
  fn_jobs="${BACKEND_FN_JOBS:-$fn_jobs}"
  opt="${BACKEND_OPT:-$opt}"
  opt2="${BACKEND_OPT2:-$opt2}"
  opt_level="${BACKEND_OPT_LEVEL:-$opt_level}"
  generic_mode="${GENERIC_MODE:-$generic_mode}"
  generic_spec_budget="${GENERIC_SPEC_BUDGET:-$generic_spec_budget}"
  generic_lowering="${GENERIC_LOWERING:-$generic_lowering}"
  no_runtime_c="${BACKEND_NO_RUNTIME_C:-$no_runtime_c}"
  runtime_obj="${BACKEND_RUNTIME_OBJ:-$runtime_obj}"
  env_sidecar_mode="${BACKEND_UIR_SIDECAR_MODE:-}"
  env_sidecar_bundle="${BACKEND_UIR_SIDECAR_BUNDLE:-}"
  env_sidecar_compiler="${BACKEND_UIR_SIDECAR_COMPILER:-}"
  env_sidecar_child_mode="${BACKEND_UIR_SIDECAR_CHILD_MODE:-}"
  env_sidecar_outer_compiler="${BACKEND_UIR_SIDECAR_OUTER_COMPILER:-}"
  env_sidecar_disable="${BACKEND_UIR_SIDECAR_DISABLE:-}"
  env_sidecar_prefer="${BACKEND_UIR_PREFER_SIDECAR:-}"
  env_sidecar_force="${BACKEND_UIR_FORCE_SIDECAR:-}"
fi

if [ "$input_path" = "" ]; then
  echo "[backend_driver_currentsrc_sidecar_wrapper] missing input path" 1>&2
  exit 2
fi
if [ "$output_path" = "" ]; then
  echo "[backend_driver_currentsrc_sidecar_wrapper] missing --output" 1>&2
  exit 2
fi
case "$frontend" in
  stage1)
    ;;
  *)
    echo "[backend_driver_currentsrc_sidecar_wrapper] invalid --frontend:$frontend" 1>&2
    exit 2
    ;;
esac
case "$emit_mode" in
  obj|exe)
    ;;
  *)
    echo "[backend_driver_currentsrc_sidecar_wrapper] invalid --emit:$emit_mode" 1>&2
    exit 2
    ;;
esac
case "$target" in
  "")
    echo "[backend_driver_currentsrc_sidecar_wrapper] missing --target" 1>&2
    exit 2
    ;;
esac
case "$linker_mode" in
  self|system|auto)
    ;;
  *)
    echo "[backend_driver_currentsrc_sidecar_wrapper] invalid --linker:$linker_mode" 1>&2
    exit 2
    ;;
esac
case "$build_track" in
  dev|release)
    ;;
  *)
    echo "[backend_driver_currentsrc_sidecar_wrapper] invalid --build-track:$build_track" 1>&2
    exit 2
    ;;
esac
case "$mm" in
  orc)
    ;;
  *)
    echo "[backend_driver_currentsrc_sidecar_wrapper] invalid --mm:$mm" 1>&2
    exit 2
    ;;
esac
case "$fn_sched" in
  ws)
    ;;
  *)
    echo "[backend_driver_currentsrc_sidecar_wrapper] invalid --fn-sched:$fn_sched" 1>&2
    exit 2
    ;;
esac
case "$sidecar_child_mode" in
  ""|cli|outer_cli)
    ;;
  *)
    echo "[backend_driver_currentsrc_sidecar_wrapper] invalid --sidecar-child-mode:$sidecar_child_mode" 1>&2
    exit 2
    ;;
esac
case "$multi_module_cache" in
  0|1)
    ;;
  *)
    echo "[backend_driver_currentsrc_sidecar_wrapper] invalid --multi-module-cache:$multi_module_cache" 1>&2
    exit 2
    ;;
esac
case "$module_cache_unstable_allow" in
  0|1)
    ;;
  *)
    echo "[backend_driver_currentsrc_sidecar_wrapper] invalid --module-cache-unstable-allow:$module_cache_unstable_allow" 1>&2
    exit 2
    ;;
esac

internal_allow_emit_obj="0"
if [ "$emit_mode" = "obj" ]; then
  internal_allow_emit_obj="1"
fi

wrapper_preserve_sidecar="${BACKEND_CURRENTSRC_WRAPPER_PRESERVE_SIDECAR:-0}"

if [ "$wrapper_preserve_sidecar" = "1" ]; then
  if [ "$sidecar_mode" = "" ]; then
    sidecar_mode="${BACKEND_UIR_SIDECAR_MODE:-${env_sidecar_mode:-}}"
  fi
  if [ "$sidecar_bundle" = "" ]; then
    sidecar_bundle="${BACKEND_UIR_SIDECAR_BUNDLE:-${env_sidecar_bundle:-}}"
  fi
  if [ "$sidecar_compiler" = "" ]; then
    sidecar_compiler="${BACKEND_UIR_SIDECAR_COMPILER:-${env_sidecar_compiler:-}}"
  fi
  if [ "$sidecar_child_mode" = "" ]; then
    sidecar_child_mode="${BACKEND_UIR_SIDECAR_CHILD_MODE:-${env_sidecar_child_mode:-}}"
  fi
  if [ "$sidecar_outer_compiler" = "" ]; then
    sidecar_outer_compiler="${BACKEND_UIR_SIDECAR_OUTER_COMPILER:-${env_sidecar_outer_compiler:-}}"
  fi
elif [ "$wrapper_mode" = "env" ]; then
  sidecar_mode=""
  sidecar_bundle=""
  sidecar_compiler=""
  sidecar_child_mode=""
  sidecar_outer_compiler=""
fi

set -- env \
  -u BACKEND_UIR_SIDECAR_MODE \
  -u BACKEND_UIR_SIDECAR_OBJ \
  -u BACKEND_UIR_SIDECAR_BUNDLE \
  -u BACKEND_UIR_SIDECAR_COMPILER \
  -u BACKEND_UIR_SIDECAR_CHILD_MODE \
  -u BACKEND_UIR_SIDECAR_OUTER_COMPILER \
  -u BACKEND_FRONTEND \
  -u BACKEND_EMIT \
  -u BACKEND_TARGET \
  -u BACKEND_LINKER \
  -u BACKEND_BUILD_TRACK \
  -u BACKEND_LDFLAGS \
  -u BACKEND_FN_SCHED \
  -u BACKEND_MODULE_CACHE \
  -u BACKEND_MULTI_MODULE_CACHE \
  -u BACKEND_MODULE_CACHE_UNSTABLE_ALLOW \
  -u BACKEND_INPUT \
  -u BACKEND_OUTPUT \
  -u MM \
  -u BACKEND_ALLOW_NO_MAIN \
  -u BACKEND_WHOLE_PROGRAM \
  -u BACKEND_MULTI \
  -u BACKEND_MULTI_FORCE \
  -u BACKEND_INCREMENTAL \
  -u BACKEND_JOBS \
  -u BACKEND_FN_JOBS \
  -u BACKEND_OPT \
  -u BACKEND_OPT2 \
  -u BACKEND_OPT_LEVEL \
  -u BACKEND_NO_RUNTIME_C \
  -u BACKEND_RUNTIME_OBJ \
  -u GENERIC_MODE \
  -u GENERIC_SPEC_BUDGET \
  -u GENERIC_LOWERING \
  -u BACKEND_INTERNAL_ALLOW_EMIT_OBJ

if [ "$wrapper_preserve_sidecar" = "1" ]; then
  if [ "$sidecar_mode" != "" ]; then
    set -- "$@" "BACKEND_UIR_SIDECAR_MODE=$sidecar_mode"
  fi
  if [ "$sidecar_bundle" != "" ]; then
    set -- "$@" "BACKEND_UIR_SIDECAR_BUNDLE=$sidecar_bundle"
  fi
  if [ "$sidecar_compiler" != "" ]; then
    set -- "$@" "BACKEND_UIR_SIDECAR_COMPILER=$sidecar_compiler"
  fi
  if [ "$sidecar_child_mode" != "" ]; then
    set -- "$@" "BACKEND_UIR_SIDECAR_CHILD_MODE=$sidecar_child_mode"
  fi
  if [ "$sidecar_outer_compiler" != "" ]; then
    set -- "$@" "BACKEND_UIR_SIDECAR_OUTER_COMPILER=$sidecar_outer_compiler"
  fi
else
  set -- "$@" \
    "BACKEND_UIR_SIDECAR_DISABLE=1" \
    "BACKEND_UIR_PREFER_SIDECAR=0" \
    "BACKEND_UIR_FORCE_SIDECAR=0"
fi

set -- "$@" \
  "BACKEND_INPUT=$input_path" \
  "BACKEND_OUTPUT=$output_path" \
  "BACKEND_FRONTEND=$frontend" \
  "BACKEND_EMIT=$emit_mode" \
  "BACKEND_TARGET=$target" \
  "BACKEND_LINKER=$linker_mode" \
  "BACKEND_BUILD_TRACK=$build_track" \
  "MM=$mm" \
  "BACKEND_FN_SCHED=$fn_sched" \
  "BACKEND_ALLOW_NO_MAIN=$allow_no_main" \
  "BACKEND_WHOLE_PROGRAM=$whole_program" \
  "BACKEND_MULTI=$multi" \
  "BACKEND_MULTI_FORCE=$multi_force" \
  "BACKEND_INCREMENTAL=$incremental" \
  "BACKEND_JOBS=$jobs" \
  "BACKEND_FN_JOBS=$fn_jobs" \
  "BACKEND_OPT=$opt" \
  "BACKEND_OPT2=$opt2" \
  "BACKEND_OPT_LEVEL=$opt_level" \
  "BACKEND_MULTI_MODULE_CACHE=$multi_module_cache" \
  "BACKEND_MODULE_CACHE_UNSTABLE_ALLOW=$module_cache_unstable_allow" \
  "GENERIC_MODE=$generic_mode" \
  "GENERIC_SPEC_BUDGET=$generic_spec_budget" \
  "GENERIC_LOWERING=$generic_lowering"

if [ "$ldflags" != "" ]; then
  set -- "$@" "BACKEND_LDFLAGS=$ldflags"
fi
if [ "$module_cache" != "" ]; then
  set -- "$@" "BACKEND_MODULE_CACHE=$module_cache"
fi
if [ "$no_runtime_c" = "1" ]; then
  set -- "$@" "BACKEND_NO_RUNTIME_C=1"
fi
if [ "$runtime_obj" != "" ]; then
  set -- "$@" "BACKEND_RUNTIME_OBJ=$runtime_obj"
fi
if [ "$internal_allow_emit_obj" = "1" ]; then
  set -- "$@" "BACKEND_INTERNAL_ALLOW_EMIT_OBJ=1"
fi

set -- "$@" "$real_driver"
exec "$@"
