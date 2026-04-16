#!/usr/bin/env sh
:
set -eu
(set -o pipefail) 2>/dev/null && set -o pipefail

usage() {
  cat <<'USAGE'
Usage:
  src/tooling/cheng_tooling_embedded_scripts/verify_backend_sidecar_cheng_fresh.sh

Builds a strict-fresh pure Cheng sidecar bundle/compiler contract into stable
repo paths and verifies it through the native sidecar outer driver.
USAGE
}

detect_host_target() {
  host_os="$(uname -s 2>/dev/null || echo unknown)"
  host_arch="$(uname -m 2>/dev/null || echo unknown)"
  case "$host_os/$host_arch" in
    Darwin/arm64)
      printf '%s\n' "arm64-apple-darwin"
      ;;
    Darwin/x86_64)
      printf '%s\n' "x86_64-apple-darwin"
      ;;
    Linux/aarch64|Linux/arm64)
      printf '%s\n' "aarch64-unknown-linux-gnu"
      ;;
    Linux/x86_64)
      printf '%s\n' "x86_64-unknown-linux-gnu"
      ;;
    *)
      printf '\n'
      ;;
  esac
}

run_timeout() {
  seconds="$1"
  shift || true
  perl -e 'alarm shift @ARGV; exec @ARGV' "$seconds" "$@"
}

run_timeout_capture_sample() {
  seconds="$1"
  sample_secs="$2"
  sample_prefix="$3"
  out_file="$4"
  shift 4 || true
  timeout_flag="${sample_prefix}.timed_out"
  parent_sample="${sample_prefix}.parent.sample.txt"
  rm -f "$timeout_flag" "$parent_sample"
  "$@" >"$out_file" 2>&1 &
  cmd_pid="$!"
  (
    sleep "$seconds"
    if kill -0 "$cmd_pid" 2>/dev/null; then
      : >"$timeout_flag"
      if [ -x /usr/bin/sample ]; then
        /usr/bin/sample "$cmd_pid" "$sample_secs" -file "$parent_sample" >/dev/null 2>&1 || true
      fi
      kill -TERM "$cmd_pid" 2>/dev/null || true
    fi
  ) &
  watchdog_pid="$!"
  rc=0
  wait "$cmd_pid" || rc="$?"
  kill "$watchdog_pid" 2>/dev/null || true
  wait "$watchdog_pid" 2>/dev/null || true
  if [ -f "$timeout_flag" ]; then
    rm -f "$timeout_flag"
    return 124
  fi
  return "$rc"
}

strict_sidecar_exit_kind() {
  rc="$1"
  case "$rc" in
    124)
      printf '%s\n' "timeout"
      ;;
    126)
      printf '%s\n' "not_executable"
      ;;
    127)
      printf '%s\n' "missing_executable"
      ;;
    223)
      printf '%s\n' "deterministic_exit_223"
      ;;
    *)
      if [ "$rc" -ge 129 ] 2>/dev/null && [ "$rc" -le 192 ] 2>/dev/null; then
        printf '%s\n' "signal"
      else
        printf '%s\n' "exit"
      fi
      ;;
  esac
}

strict_sidecar_exit_hint() {
  rc="$1"
  case "$rc" in
    124)
      printf '%s\n' "wrapper_source_build_timed_out_waiting_for_bootstrap_proof_driver"
      ;;
    126)
      printf '%s\n' "wrapper_source_compiler_not_executable"
      ;;
    127)
      printf '%s\n' "wrapper_source_compiler_missing"
      ;;
    223)
      printf '%s\n' "child_exited_223_directly_non_posix_signal"
      ;;
    *)
      printf '\n'
      ;;
  esac
}

root="${TOOLING_ROOT:-$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)}"
cd "$root"

currentsrc_proof_real_driver_path() {
  printf '%s\n' "$root/artifacts/backend_selfhost_self_obj/probe_currentsrc_proof/cheng_stage0_currentsrc.proof"
}

strict_stage0_lineage_dir() {
  printf '%s\n' "$root/artifacts/backend_selfhost_self_obj/probe_currentsrc_proof"
}

strict_published_stage2_driver_path() {
  proof_driver="$(strict_stage0_lineage_dir)/cheng.stage2.proof"
  if [ -x "$proof_driver" ] && [ -f "${proof_driver}.meta" ]; then
    printf '%s\n' "$proof_driver"
    return 0
  fi
  published_driver="$(strict_stage0_lineage_dir)/cheng.stage2"
  if [ -x "$published_driver" ] && [ -f "${published_driver}.meta" ]; then
    printf '%s\n' "$published_driver"
    return 0
  fi
  printf '%s\n' "$published_driver"
}

strict_runtime_real_driver_path() {
  published_driver="$(strict_published_stage2_driver_path)"
  if [ -x "$published_driver" ] && [ -f "${published_driver}.meta" ]; then
    printf '%s\n' "$published_driver"
    return 0
  fi
  printf '%s\n' "$(currentsrc_proof_real_driver_path)"
}

strict_stage0_meta_field() {
  meta_path="$1"
  key="$2"
  if [ ! -f "$meta_path" ]; then
    printf '\n'
    return 0
  fi
  awk -F= -v key="$key" '$1 == key { print substr($0, index($0, "=") + 1); exit }' "$meta_path"
}

strict_stage0_published_surface() {
  driver_path="$1"
  case "$driver_path" in
    "$(strict_stage0_lineage_dir)"/cheng.stage1|\
    "$(strict_stage0_lineage_dir)"/cheng.stage2|\
    "$(strict_stage0_lineage_dir)"/cheng.stage2.proof)
      return 0
      ;;
  esac
  return 1
}

strict_stage0_bootstrap_surface() {
  driver_path="$1"
  case "$driver_path" in
    "$(currentsrc_proof_real_driver_path)")
      return 0
      ;;
  esac
  return 1
}

strict_stage0_expected_label() {
  driver_path="$1"
  case "$driver_path" in
    */cheng_stage0_currentsrc.proof)
      printf '%s\n' "currentsrc.proof.bootstrap"
      return 0
      ;;
    */cheng.stage1)
      printf '%s\n' "stage1"
      return 0
      ;;
    */cheng.stage2)
      printf '%s\n' "stage2"
      return 0
      ;;
    */cheng.stage2.proof)
      printf '%s\n' "stage2.proof"
      return 0
      ;;
  esac
  printf '\n'
}

strict_stage0_expected_input() {
  driver_path="$1"
  case "$(strict_stage0_expected_label "$driver_path")" in
    currentsrc.proof.bootstrap)
      printf '%s\n' "src/backend/tooling/backend_driver_proof.cheng"
      return 0
      ;;
    stage1|stage2|stage2.proof)
      printf '%s\n' "src/backend/tooling/backend_driver.cheng"
      return 0
      ;;
  esac
  printf '\n'
}

strict_stage0_direct_export_driver_path() {
  driver_path="$1"
  case "$driver_path" in
    *.proof)
      outer_driver="$(strict_stage0_meta_field "${driver_path}.meta" "outer_driver")"
      if [ "$outer_driver" != "" ]; then
        printf '%s\n' "$outer_driver"
        return 0
      fi
      ;;
  esac
  printf '%s\n' "$driver_path"
}

strict_stage0_direct_export_surface_ok() {
  driver_path="$1"
  export_driver="$(strict_stage0_direct_export_driver_path "$driver_path")"
  [ "$export_driver" != "" ] || return 1
  [ -x "$export_driver" ] || return 1
  command -v nm >/dev/null 2>&1 || return 1
  command -v awk >/dev/null 2>&1 || return 1
  surface_log="$(mktemp "${TMPDIR:-/tmp}/strict_stage0_nm.XXXXXX")"
  set +e
  (nm -gU "$export_driver" 2>/dev/null || nm "$export_driver" 2>/dev/null) >"$surface_log"
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

strict_stage0_meta_ok() {
  driver_path="$1"
  if ! strict_stage0_published_surface "$driver_path" && ! strict_stage0_bootstrap_surface "$driver_path"; then
    return 1
  fi
  meta_path="${driver_path}.meta"
  expected_label="$(strict_stage0_expected_label "$driver_path")"
  [ "$expected_label" != "" ] || return 1
  [ -f "$meta_path" ] || return 1
  [ "$(strict_stage0_meta_field "$meta_path" "meta_contract_version")" = "2" ] || return 1
  [ "$(strict_stage0_meta_field "$meta_path" "label")" = "$expected_label" ] || return 1
  meta_outer_driver="$(strict_stage0_meta_field "$meta_path" "outer_driver")"
  case "$expected_label" in
    currentsrc.proof.bootstrap)
      [ "$meta_outer_driver" != "" ] || return 1
      [ -x "$meta_outer_driver" ] || return 1
      [ "$(strict_stage0_meta_field "$meta_path" "sidecar_mode")" = "cheng" ] || return 1
      [ "$(strict_stage0_meta_field "$meta_path" "sidecar_bundle")" != "" ] || return 1
      [ "$(strict_stage0_meta_field "$meta_path" "sidecar_compiler")" != "" ] || return 1
      ;;
    stage2.proof)
      [ "$meta_outer_driver" != "" ] || return 1
      [ -x "$meta_outer_driver" ] || return 1
      [ "$(strict_stage0_meta_field "$meta_path" "sidecar_mode")" = "cheng" ] || return 1
      [ "$(strict_stage0_meta_field "$meta_path" "sidecar_bundle")" != "" ] || return 1
      [ "$(strict_stage0_meta_field "$meta_path" "sidecar_compiler")" != "" ] || return 1
      ;;
    *)
      [ "$meta_outer_driver" = "$driver_path" ] || return 1
      ;;
  esac
  [ "$(strict_stage0_meta_field "$meta_path" "driver_input")" = "$(strict_stage0_expected_input "$driver_path")" ] || return 1
  [ "$(strict_stage0_meta_field "$meta_path" "stage1_ownership_fixed_0_effective")" = "0" ] || return 1
  [ "$(strict_stage0_meta_field "$meta_path" "stage1_ownership_fixed_0_default")" = "0" ] || return 1
  [ "$(strict_stage0_meta_field "$meta_path" "generic_mode")" = "dict" ] || return 1
  [ "$(strict_stage0_meta_field "$meta_path" "generic_lowering")" = "mir_dict" ] || return 1
  strict_stage0_direct_export_surface_ok "$driver_path" || return 1
  return 0
}

strict_stage0_sources_newer_than() {
  out="$1"
  [ -e "$out" ] || return 0
  search_dirs=""
  for d in src/backend src/stage1 src/std src/core src/system; do
    if [ -d "$d" ]; then
      search_dirs="$search_dirs $d"
    fi
  done
  if [ "$search_dirs" = "" ]; then
    return 1
  fi
  # shellcheck disable=SC2086
  find $search_dirs -type f \( -name '*.cheng' -o -name '*.c' -o -name '*.h' \) \
    -newer "$out" -print -quit | grep -q .
}

strict_stage0_current_enough() {
  driver_path="$1"
  strict_stage0_published_surface "$driver_path" || return 0
  [ -x "$driver_path" ] || return 1
  if strict_stage0_sources_newer_than "$driver_path"; then
    return 1
  fi
  return 0
}

assert_strict_fresh_bootstrap_driver() {
  bootstrap_driver_path="$1"
  case "$bootstrap_driver_path" in
    /*)
      ;;
    *)
      echo "[verify_backend_sidecar_cheng_fresh] bootstrap sidecar driver must be absolute: $bootstrap_driver_path" 1>&2
      exit 1
      ;;
  esac
  if [ ! -x "$bootstrap_driver_path" ]; then
    echo "[verify_backend_sidecar_cheng_fresh] missing bootstrap sidecar driver: $bootstrap_driver_path" 1>&2
    exit 1
  fi
  case "$bootstrap_driver_path" in
    "$root"/*)
      ;;
    *)
      echo "[verify_backend_sidecar_cheng_fresh] unstable bootstrap sidecar driver path: $bootstrap_driver_path" 1>&2
      exit 1
      ;;
  esac
  if ! strict_stage0_published_surface "$bootstrap_driver_path" && ! strict_stage0_bootstrap_surface "$bootstrap_driver_path"; then
    echo "[verify_backend_sidecar_cheng_fresh] strict bootstrap sidecar driver must be a current-source proof surface: $bootstrap_driver_path" 1>&2
    exit 1
  fi
  if ! strict_stage0_meta_ok "$bootstrap_driver_path"; then
    echo "[verify_backend_sidecar_cheng_fresh] strict bootstrap sidecar driver missing contract: $bootstrap_driver_path" 1>&2
    exit 1
  fi
  if ! strict_stage0_current_enough "$bootstrap_driver_path"; then
    echo "[verify_backend_sidecar_cheng_fresh] strict bootstrap sidecar driver is stale against current sources: $bootstrap_driver_path" 1>&2
    exit 1
  fi
}

strict_fresh_bootstrap_driver_ok() {
  bootstrap_driver_path="$1"
  case "$bootstrap_driver_path" in
    /*) ;;
    *) return 1 ;;
  esac
  [ -x "$bootstrap_driver_path" ] || return 1
  case "$bootstrap_driver_path" in
    "$root"/*) ;;
    *) return 1 ;;
  esac
  strict_stage0_published_surface "$bootstrap_driver_path" || strict_stage0_bootstrap_surface "$bootstrap_driver_path" || return 1
  strict_stage0_meta_ok "$bootstrap_driver_path" || return 1
  strict_stage0_current_enough "$bootstrap_driver_path" || return 1
  return 0
}

assert_strict_fresh_real_driver() {
  real_driver_path="$1"
  case "$real_driver_path" in
    /*)
      ;;
    *)
      echo "[verify_backend_sidecar_cheng_fresh] real sidecar driver must be absolute: $real_driver_path" 1>&2
      exit 1
      ;;
  esac
  if [ ! -x "$real_driver_path" ]; then
    echo "[verify_backend_sidecar_cheng_fresh] missing real sidecar driver: $real_driver_path" 1>&2
    exit 1
  fi
  case "$real_driver_path" in
    "$root"/*)
      ;;
    *)
      echo "[verify_backend_sidecar_cheng_fresh] unstable real sidecar driver path: $real_driver_path" 1>&2
      exit 1
      ;;
  esac
  if ! strict_stage0_published_surface "$real_driver_path" && ! strict_stage0_bootstrap_surface "$real_driver_path"; then
    echo "[verify_backend_sidecar_cheng_fresh] strict real sidecar driver must be a current-source proof surface: $real_driver_path" 1>&2
    exit 1
  fi
  if ! strict_stage0_meta_ok "$real_driver_path"; then
    echo "[verify_backend_sidecar_cheng_fresh] strict real sidecar driver missing contract: $real_driver_path" 1>&2
    exit 1
  fi
  if ! strict_stage0_current_enough "$real_driver_path"; then
    echo "[verify_backend_sidecar_cheng_fresh] strict real sidecar driver is stale against current sources: $real_driver_path" 1>&2
    exit 1
  fi
}

case "${1:-}" in
  --help|-h)
    usage
    exit 0
    ;;
  "")
    ;;
  *)
    echo "[verify_backend_sidecar_cheng_fresh] unknown arg: $1" 1>&2
    usage 1>&2
    exit 2
    ;;
esac

compile_timeout="${TOOLING_COMPILE_TIMEOUT:-60}"
run_timeout_sec="${TOOLING_SIDE_CAR_RUN_TIMEOUT:-10}"
compile_timeout_budget="$compile_timeout"
if [ "$compile_timeout_budget" -gt 2 ] 2>/dev/null; then
  compile_timeout_budget="$((compile_timeout_budget - 2))"
fi
run_timeout_budget="$run_timeout_sec"
if [ "$run_timeout_budget" -gt 2 ] 2>/dev/null; then
  run_timeout_budget="$((run_timeout_budget - 2))"
fi
target="${BACKEND_TARGET:-$(detect_host_target)}"
if [ "$target" = "" ]; then
  echo "[verify_backend_sidecar_cheng_fresh] failed to detect host target" 1>&2
  exit 1
fi

cache_dir="$root/chengcache/backend_driver_sidecar"
out_dir="$root/artifacts/backend_sidecar_cheng_fresh"
compiler_template="$root/src/tooling/cheng_tooling_embedded_scripts/backend_driver_currentsrc_sidecar_wrapper.sh"
wrapper_source="$root/src/backend/tooling/backend_driver_uir_sidecar_wrapper.cheng"
helper_source="$root/src/backend/tooling/backend_driver_uir_sidecar_bundle.c"
outer_source="$root/src/backend/tooling/backend_driver_sidecar_outer_main.c"
outer_exports_source="$root/src/backend/tooling/backend_driver_sidecar_outer_exports.c"
runtime_source="$root/src/runtime/native/system_helpers_selflink_shim.c"

compiler_path="$cache_dir/backend_driver_currentsrc_sidecar_wrapper.$target.sh"
compiler_meta="$compiler_path.meta"
outer_driver="$cache_dir/backend_driver_sidecar_outer.$target"
obj_path="$cache_dir/backend_driver_uir_sidecar.$target.o"
helper_obj="$cache_dir/backend_driver_uir_sidecar_helper.$target.o"
bundle_path="$cache_dir/backend_driver_uir_sidecar.$target.bundle"
report_path="$out_dir/verify_backend_sidecar_cheng_fresh.report.txt"
snapshot_path="$out_dir/verify_backend_sidecar_cheng_fresh.snapshot.env"

sidecar_child_mode="${BACKEND_UIR_SIDECAR_CHILD_MODE:-cli}"
case "$sidecar_child_mode" in
  cli|outer_cli)
    ;;
  *)
    echo "[verify_backend_sidecar_cheng_fresh] invalid sidecar child mode: $sidecar_child_mode" 1>&2
    exit 1
    ;;
esac

sidecar_outer_companion=""
if [ "$sidecar_child_mode" = "outer_cli" ]; then
  sidecar_outer_companion="${BACKEND_UIR_SIDECAR_OUTER_COMPILER:-}"
  if [ "$sidecar_outer_companion" = "" ]; then
    echo "[verify_backend_sidecar_cheng_fresh] missing explicit outer companion for child_mode=outer_cli" 1>&2
    exit 1
  fi
fi

mkdir -p "$cache_dir" "$out_dir"
rm -f "$obj_path" "$helper_obj" "$bundle_path" "$report_path" "$snapshot_path"

cp "$compiler_template" "$compiler_path"
chmod +x "$compiler_path"

strict_bootstrap_driver_path() {
  explicit_bootstrap="${BACKEND_UIR_SIDECAR_BOOTSTRAP_DRIVER:-}"
  if [ "$explicit_bootstrap" != "" ]; then
    case "$explicit_bootstrap" in
      /*)
        printf '%s\n' "$explicit_bootstrap"
        ;;
      *)
        printf '%s\n' "$root/$explicit_bootstrap"
        ;;
    esac
    return 0
  fi
  published_driver="$(strict_published_stage2_driver_path)"
  if strict_fresh_bootstrap_driver_ok "$published_driver"; then
    printf '%s\n' "$published_driver"
    return 0
  fi
  printf '%s\n' "$(currentsrc_proof_real_driver_path)"
}

resolve_strict_bootstrap_driver() {
  candidate="$(strict_bootstrap_driver_path)"
  [ "$candidate" != "" ] || {
    echo "[verify_backend_sidecar_cheng_fresh] missing strict bootstrap sidecar driver path" 1>&2
    exit 1
  }
  assert_strict_fresh_bootstrap_driver "$candidate"
  printf '%s\n' "$candidate"
  return 0
}

refresh_bootstrap_sidecar_compiler_contract() {
  bootstrap_driver_path="$1"
  case "$bootstrap_driver_path" in
    "$(currentsrc_proof_real_driver_path)")
      bootstrap_compiler="$(strict_stage0_meta_field "${bootstrap_driver_path}.meta" "sidecar_compiler")"
      if [ "$bootstrap_compiler" = "" ]; then
        echo "[verify_backend_sidecar_cheng_fresh] bootstrap sidecar compiler contract missing in meta: $bootstrap_driver_path" 1>&2
        exit 1
      fi
      cp "$compiler_template" "$bootstrap_compiler"
      chmod +x "$bootstrap_compiler"
      ;;
  esac
}

bootstrap_driver="$(resolve_strict_bootstrap_driver)"
refresh_bootstrap_sidecar_compiler_contract "$bootstrap_driver"

real_driver="${BACKEND_UIR_SIDECAR_REAL_DRIVER:-$(strict_runtime_real_driver_path)}"
case "$real_driver" in
  /*)
    ;;
  *)
    real_driver="$root/$real_driver"
    ;;
esac
assert_strict_fresh_real_driver "$real_driver"

cat >"$compiler_meta" <<EOF
sidecar_contract_version=1
sidecar_child_mode=$sidecar_child_mode
sidecar_outer_companion=$sidecar_outer_companion
sidecar_real_driver=$bootstrap_driver
EOF

run_timeout "$compile_timeout_budget" cc \
  -fPIC \
  -c "$helper_source" \
  -o "$helper_obj"
[ -s "$helper_obj" ] || {
  echo "[verify_backend_sidecar_cheng_fresh] missing helper object: $helper_obj" 1>&2
  exit 1
}

run_timeout "$compile_timeout_budget" cc \
  -std=c11 \
  -Wno-deprecated-declarations \
  -O0 \
  -Wl,-export_dynamic \
  "$outer_source" \
  "$outer_exports_source" \
  "$runtime_source" \
  "$helper_obj" \
  -o "$outer_driver"
[ -x "$outer_driver" ] || {
  echo "[verify_backend_sidecar_cheng_fresh] missing outer driver output: $outer_driver" 1>&2
  exit 1
}

wrapper_compile_log="$out_dir/wrapper_source.compile.log"
wrapper_sample_prefix="$out_dir/wrapper_source.timeout"
rm -f "$wrapper_compile_log"
set +e
run_timeout_capture_sample "$compile_timeout_budget" 1 "$wrapper_sample_prefix" "$wrapper_compile_log" env \
  TOOLING_ROOT="$root" \
  MM=orc \
  CACHE=0 \
  BACKEND_BUILD_TRACK=dev \
  BACKEND_PROOF_PRIMARY_TIMEOUT="$compile_timeout_budget" \
  BACKEND_CURRENTSRC_WRAPPER_PRESERVE_SIDECAR=0 \
  BACKEND_UIR_SIDECAR_DISABLE=1 \
  BACKEND_UIR_PREFER_SIDECAR=0 \
  BACKEND_UIR_FORCE_SIDECAR=0 \
  "$compiler_path" "$wrapper_source" \
  --frontend:stage1 \
  --emit:obj \
  --target:"$target" \
  --linker:system \
  --allow-no-main \
  --no-whole-program \
  --no-multi \
  --no-multi-force \
  --no-incremental \
  --jobs:8 \
  --fn-jobs:8 \
  --opt-level:0 \
  --no-opt \
  --no-opt2 \
  --generic-mode:dict \
  --generic-spec-budget:0 \
  --generic-lowering:mir_dict \
  --output:"$obj_path"
wrapper_rc="$?"
set -e
[ "$wrapper_rc" = "0" ] || {
  wrapper_kind="$(strict_sidecar_exit_kind "$wrapper_rc")"
  wrapper_hint="$(strict_sidecar_exit_hint "$wrapper_rc")"
  echo "[verify_backend_sidecar_cheng_fresh] strict bootstrap sidecar driver failed wrapper-source build: $bootstrap_driver rc=$wrapper_rc kind=$wrapper_kind log=$wrapper_compile_log" 1>&2
  if [ "$wrapper_hint" != "" ]; then
    echo "[verify_backend_sidecar_cheng_fresh] hint=$wrapper_hint" 1>&2
  fi
  if [ "$wrapper_rc" = "124" ] && [ -f "${wrapper_sample_prefix}.parent.sample.txt" ]; then
    echo "[verify_backend_sidecar_cheng_fresh] timeout sample: ${wrapper_sample_prefix}.parent.sample.txt" 1>&2
  fi
  if [ -s "$wrapper_compile_log" ]; then
    sed -n '1,120p' "$wrapper_compile_log" 1>&2 || true
  else
    echo "[verify_backend_sidecar_cheng_fresh] wrapper-source build produced no stderr/stdout before exit" 1>&2
  fi
  exit 1
}
[ -s "$obj_path" ] || {
  echo "[verify_backend_sidecar_cheng_fresh] missing wrapper object: $obj_path" 1>&2
  exit 1
}

run_timeout "$compile_timeout_budget" cc \
  -bundle \
  -undefined dynamic_lookup \
  "$obj_path" \
  "$helper_obj" \
  -o "$bundle_path"
[ -s "$bundle_path" ] || {
  echo "[verify_backend_sidecar_cheng_fresh] missing sidecar bundle: $bundle_path" 1>&2
  exit 1
}

cat >"$compiler_meta" <<EOF
sidecar_contract_version=1
sidecar_child_mode=$sidecar_child_mode
sidecar_outer_companion=$sidecar_outer_companion
sidecar_real_driver=$real_driver
EOF

fixture_reports=""
verify_fixture() {
  fixture_path="$1"
  fixture_name="$(basename -- "$fixture_path" .cheng)"
  compile_log="$out_dir/${fixture_name}.compile.log"
  run_log="$out_dir/${fixture_name}.run.log"
  exe_path="$cache_dir/verify_backend_sidecar_cheng_fresh.${fixture_name}.$target.bin"
  rm -f "$compile_log" "$run_log" "$exe_path"
  set -- env \
    PATH="${PATH:-/usr/bin:/bin:/usr/sbin:/sbin}" \
    HOME="${HOME:-$root}" \
    TMPDIR="${TMPDIR:-/tmp}" \
    TOOLING_ROOT="$root" \
    CACHE=0 \
    STAGE1_AUTO_SYSTEM=0 \
    BACKEND_VALIDATE=0 \
    perl -e 'alarm shift @ARGV; exec @ARGV' "$compile_timeout_budget" \
    "$outer_driver" "$fixture_path" \
    --emit:exe \
    --target:"$target" \
    --frontend:stage1 \
    --linker:system \
    --build-track:dev \
    --mm:orc \
    --fn-sched:ws \
    --jobs:8 \
    --fn-jobs:8 \
    --opt-level:0 \
    --no-opt \
    --no-opt2 \
    --no-multi \
    --no-multi-force \
    --no-incremental \
    --sidecar-mode:cheng \
    --sidecar-bundle:"$bundle_path" \
    --sidecar-compiler:"$compiler_path" \
    --sidecar-child-mode:"$sidecar_child_mode"
  if [ "$sidecar_outer_companion" != "" ]; then
    set -- "$@" "--sidecar-outer-compiler:$sidecar_outer_companion"
  fi
  set -- "$@" "--output:$exe_path"
  set +e
  "$@" >"$compile_log" 2>&1
  compile_rc="$?"
  set -e
  if [ "$compile_rc" != "0" ]; then
    echo "[verify_backend_sidecar_cheng_fresh] fixture compile failed: $fixture_name rc=$compile_rc" 1>&2
    sed -n '1,120p' "$compile_log" 1>&2 || true
    exit 1
  fi
  [ -x "$exe_path" ] || {
    echo "[verify_backend_sidecar_cheng_fresh] compile produced no exe for $fixture_name" 1>&2
    sed -n '1,120p' "$compile_log" 1>&2 || true
    exit 1
  }
  run_timeout "$run_timeout_budget" "$exe_path" >"$run_log" 2>&1
  if [ "$fixture_reports" = "" ]; then
    fixture_reports="${fixture_name}:ok"
  else
    fixture_reports="${fixture_reports},${fixture_name}:ok"
  fi
}

verify_fixture "$root/tests/cheng/backend/fixtures/return_add.cheng"
verify_fixture "$root/tests/cheng/backend/fixtures/return_while_sum.cheng"
verify_fixture "$root/tests/cheng/backend/fixtures/hello_importc_puts.cheng"

cat >"$report_path" <<EOF
status=ok
gate=verify_backend_sidecar_cheng_fresh
sidecar_mode=cheng
compiler=$compiler_path
compiler_bootstrap_driver=$bootstrap_driver
compiler_real_driver=$real_driver
driver=$outer_driver
driver_mode=native_sidecar_outer
target=$target
object=$obj_path
bundle=$bundle_path
child_mode=$sidecar_child_mode
outer_companion=$sidecar_outer_companion
fixtures=$fixture_reports
EOF

cat >"$snapshot_path" <<EOF
backend_sidecar_cheng_fresh_status=ok
backend_sidecar_cheng_fresh_compiler=$compiler_path
backend_sidecar_cheng_fresh_real_driver=$real_driver
backend_sidecar_cheng_fresh_child_mode=$sidecar_child_mode
backend_sidecar_cheng_fresh_outer_companion=$sidecar_outer_companion
backend_sidecar_cheng_fresh_driver=$outer_driver
backend_sidecar_cheng_fresh_driver_mode=native_sidecar_outer
backend_sidecar_cheng_fresh_target=$target
backend_sidecar_cheng_fresh_mode=cheng
backend_sidecar_cheng_fresh_bundle=$bundle_path
backend_sidecar_cheng_fresh_report=$report_path
backend_sidecar_cheng_fresh_fixtures=$fixture_reports
EOF

echo "verify_backend_sidecar_cheng_fresh ok"
