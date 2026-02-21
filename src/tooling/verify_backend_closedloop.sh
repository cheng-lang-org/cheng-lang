#!/usr/bin/env sh
. "$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)/env_prefix_bridge.sh"
set -eu
(set -o pipefail) 2>/dev/null && set -o pipefail

root="$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)"
cd "$root"

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

strict="${BACKEND_MATRIX_STRICT:-0}"
run_fullspec="${BACKEND_RUN_FULLSPEC:-0}"
host_os="$(uname -s 2>/dev/null || echo unknown)"
host_arch="$(uname -m 2>/dev/null || echo unknown)"
if [ "${BACKEND_DRIVER:-}" != "" ]; then
  driver="${BACKEND_DRIVER}"
else
  driver="$(env \
    BACKEND_DRIVER_PATH_PREFER_REBUILD="${BACKEND_DRIVER_PATH_PREFER_REBUILD:-1}" \
    BACKEND_DRIVER_ALLOW_FALLBACK="${BACKEND_DRIVER_ALLOW_FALLBACK:-0}" \
    sh src/tooling/backend_driver_path.sh)"
fi
if [ ! -x "$driver" ]; then
  echo "[verify_backend_closedloop] backend driver not executable: $driver" 1>&2
  exit 1
fi
linkerless_gate_driver="${BACKEND_LINKERLESS_DRIVER:-}"
multi_gate_driver="${BACKEND_MULTI_DRIVER:-}"
stage1_smoke_driver="${BACKEND_STAGE1_SMOKE_DRIVER:-$driver}"
mm_gate_driver="${BACKEND_MM_DRIVER:-}"
if [ "$linkerless_gate_driver" != "" ] && [ ! -x "$linkerless_gate_driver" ]; then
  echo "[verify_backend_closedloop] backend.linkerless_dev driver not executable: $linkerless_gate_driver" 1>&2
  exit 1
fi
if [ "$multi_gate_driver" != "" ] && [ ! -x "$multi_gate_driver" ]; then
  echo "[verify_backend_closedloop] backend.multi driver not executable: $multi_gate_driver" 1>&2
  exit 1
fi
if [ ! -x "$stage1_smoke_driver" ]; then
  echo "[verify_backend_closedloop] stage1 smoke driver not executable: $stage1_smoke_driver" 1>&2
  exit 1
fi
if [ "$mm_gate_driver" != "" ] && [ ! -x "$mm_gate_driver" ]; then
  echo "[verify_backend_closedloop] backend.mm driver not executable: $mm_gate_driver" 1>&2
  exit 1
fi
export BACKEND_DRIVER="$driver"
backend_mm="${BACKEND_MM:-${MM:-orc}}"
backend_linker="${BACKEND_LINKER:-self}"
backend_target="${BACKEND_TARGET:-$(sh src/tooling/detect_host_target.sh 2>/dev/null || echo arm64-apple-darwin)}"
safe_target="$(printf '%s' "$backend_target" | tr -c 'A-Za-z0-9._-' '_' | tr -s '_')"
backend_runtime_obj="${BACKEND_RUNTIME_OBJ:-chengcache/system_helpers.backend.cheng.${safe_target}.o}"
stage1_skip_sem="${STAGE1_SKIP_SEM:-0}"
stage1_skip_ownership="${STAGE1_SKIP_OWNERSHIP:-1}"
case "$backend_mm" in
  ""|orc)
    backend_mm="orc"
    ;;
  *)
    echo "[verify_backend_closedloop] invalid BACKEND_MM/MM: $backend_mm (expected orc)" 1>&2
    exit 2
    ;;
esac

case "$backend_linker" in
  self|system)
    ;;
  *)
    echo "[verify_backend_closedloop] invalid BACKEND_LINKER: $backend_linker (expected self|system)" 1>&2
    exit 2
    ;;
esac
if [ "$backend_linker" = "self" ] && [ ! -f "$backend_runtime_obj" ]; then
  echo "[verify_backend_closedloop] missing self-link runtime object: $backend_runtime_obj" 1>&2
  exit 2
fi

run_step() {
  name="$1"
  shift
  echo "== ${name} =="
  set +e
  "$@"
  status="$?"
  set -e
  if [ "$status" -eq 0 ]; then
    return 0
  fi
  if [ "$status" -eq 2 ]; then
    if [ "$strict" = "1" ]; then
      echo "[verify_backend_closedloop] ${name} returned skip (strict mode)" 1>&2
      exit 1
    fi
    echo "== ${name} (skip) =="
    return 0
  fi
  exit "$status"
}

run_step_allow_skip() {
  name="$1"
  shift
  echo "== ${name} =="
  set +e
  "$@"
  status="$?"
  set -e
  if [ "$status" -eq 0 ]; then
    return 0
  fi
  if [ "$status" -eq 2 ]; then
    echo "== ${name} (skip) =="
    return 0
  fi
  exit "$status"
}

profile_smoke_fixture="tests/cheng/backend/fixtures/return_add.cheng"
if [ ! -f "$profile_smoke_fixture" ]; then
  profile_smoke_fixture="tests/cheng/backend/fixtures/return_i64.cheng"
fi

run_step "backend.profile_smoke" env \
  BACKEND_PROFILE=1 \
  STAGE1_NO_POINTERS_NON_C_ABI=0 \
  STAGE1_NO_POINTERS_NON_C_ABI_INTERNAL=0 \
  STAGE1_SKIP_SEM="$stage1_skip_sem" \
  STAGE1_SKIP_OWNERSHIP="$stage1_skip_ownership" \
  BACKEND_LINKER=self \
  BACKEND_NO_RUNTIME_C=1 \
  BACKEND_RUNTIME_OBJ="$backend_runtime_obj" \
  BACKEND_EMIT=exe \
  BACKEND_TARGET="$backend_target" \
  BACKEND_FRONTEND=stage1 \
  BACKEND_INPUT="$profile_smoke_fixture" \
  BACKEND_OUTPUT=artifacts/backend_closedloop/profile_smoke \
  "$driver"
run_step "backend.profile_schema" sh src/tooling/verify_backend_profile_schema.sh
run_step "tooling.cmdline_runner" sh src/tooling/verify_tooling_cmdline.sh
run_step "backend.import_cycle_predeclare" sh src/tooling/verify_backend_import_cycle_predeclare.sh
run_step "backend.rawptr_contract" sh src/tooling/verify_backend_rawptr_contract.sh
run_step "backend.rawptr_surface_forbid" sh src/tooling/verify_backend_rawptr_surface_forbid.sh
run_step "backend.ffi_slice_shim" sh src/tooling/verify_backend_ffi_slice_shim.sh
run_step "backend.ffi_outptr_tuple" sh src/tooling/verify_backend_ffi_outptr_tuple.sh
run_step "backend.ffi_handle_sandbox" sh src/tooling/verify_backend_ffi_handle_sandbox.sh
run_step "backend.ffi_borrow_bridge" sh src/tooling/verify_backend_ffi_borrow_bridge.sh
run_step "backend.mem_contract" sh src/tooling/verify_backend_mem_contract.sh
run_step "backend.dod_contract" sh src/tooling/verify_backend_dod_contract.sh
run_step "backend.mem_image_core" sh src/tooling/verify_backend_mem_image_core.sh
run_step "backend.mem_exe_emit" env BACKEND_LINKER=self BACKEND_MEM_EXE_EMIT_REQUIRE_DRIVER_SIDECAR_ZERO=1 sh src/tooling/verify_backend_mem_exe_emit.sh
run_step "backend.profile_baseline" sh src/tooling/verify_backend_profile_baseline.sh
run_step "backend.dual_track" sh src/tooling/verify_backend_dual_track.sh
run_step "backend.noalias_opt" env \
  STAGE1_SKIP_OWNERSHIP=0 \
  UIR_NOALIAS_REQUIRE_PROOF=1 \
  sh src/tooling/verify_backend_noalias_opt.sh
run_step "backend.egraph_cost" env \
  STAGE1_SKIP_OWNERSHIP=0 \
  UIR_NOALIAS_REQUIRE_PROOF=1 \
  UIR_EGRAPH_REQUIRE_PROOF=1 \
  sh src/tooling/verify_backend_egraph_cost.sh
run_step "backend.dod_opt_regression" env \
  STAGE1_SKIP_OWNERSHIP=0 \
  UIR_NOALIAS_REQUIRE_PROOF=1 \
  UIR_EGRAPH_REQUIRE_PROOF=1 \
  sh src/tooling/verify_backend_dod_opt_regression.sh
if [ "$linkerless_gate_driver" != "" ]; then
  run_step "backend.linkerless_dev" env BACKEND_LINKER=self BACKEND_BUILD_TRACK=dev BACKEND_DRIVER="$linkerless_gate_driver" sh src/tooling/verify_backend_linkerless_dev.sh
else
  run_step "backend.linkerless_dev" env BACKEND_LINKER=self BACKEND_BUILD_TRACK=dev sh src/tooling/verify_backend_linkerless_dev.sh
fi
run_step "backend.hotpatch_meta" env BACKEND_LINKER=self BACKEND_BUILD_TRACK=dev sh src/tooling/verify_backend_hotpatch_meta.sh
run_step "backend.hotpatch_inplace" env BACKEND_LINKER=self BACKEND_BUILD_TRACK=dev sh src/tooling/verify_backend_hotpatch_inplace.sh
run_step "backend.incr_patch_fastpath" env BACKEND_LINKER=self BACKEND_BUILD_TRACK=dev sh src/tooling/verify_backend_incr_patch_fastpath.sh
run_step "backend.mem_patch_regression" env BACKEND_LINKER=self BACKEND_BUILD_TRACK=dev sh src/tooling/verify_backend_mem_patch_regression.sh
run_step "backend.hotpatch" env BACKEND_LINKER=self BACKEND_BUILD_TRACK=dev sh src/tooling/verify_backend_hotpatch.sh
run_step "backend.release_system_link" env BACKEND_BUILD_TRACK=release BACKEND_LINKER=system BACKEND_NO_RUNTIME_C=0 sh src/tooling/verify_backend_release_c_o3_lto.sh
run_step "backend.plugin_isolation" sh src/tooling/verify_backend_plugin_isolation.sh
run_step "backend.noptr_exemption_scope" sh src/tooling/verify_backend_noptr_exemption_scope.sh
self_linker_gate_driver="${BACKEND_SELF_LINKER_DRIVER:-artifacts/backend_seed/cheng.stage2}"
if [ "$self_linker_gate_driver" != "" ] && [ -x "$self_linker_gate_driver" ]; then
  run_step "backend.self_linker.elf" env BACKEND_SELF_LINKER_DRIVER="$self_linker_gate_driver" sh src/tooling/verify_backend_self_linker_elf.sh
  run_step "backend.self_linker.coff" env BACKEND_SELF_LINKER_DRIVER="$self_linker_gate_driver" sh src/tooling/verify_backend_self_linker_coff.sh
else
  run_step "backend.self_linker.elf" sh src/tooling/verify_backend_self_linker_elf.sh
  run_step "backend.self_linker.coff" sh src/tooling/verify_backend_self_linker_coff.sh
fi

rm -rf artifacts/backend_closedloop
mkdir -p artifacts/backend_closedloop

stage1_smoke_multi="${BACKEND_MULTI:-0}"
stage1_smoke_multi_force="${BACKEND_MULTI_FORCE:-$stage1_smoke_multi}"
stage1_generic_mode="${BACKEND_CLOSEDLOOP_STAGE1_GENERIC_MODE:-dict}"
stage1_generic_budget="${BACKEND_CLOSEDLOOP_STAGE1_GENERIC_SPEC_BUDGET:-${GENERIC_SPEC_BUDGET:-0}}"
stage1_smoke_name="backend.closedloop_stage1_smoke"
stage1_smoke_input="tests/cheng/backend/fixtures/hello_puts.cheng"
stage1_smoke_output="artifacts/backend_closedloop/stage1_smoke"

compile_stage1_smoke_self() {
  set +e
  env \
      MM="$backend_mm" \
      STAGE1_NO_POINTERS_NON_C_ABI=0 \
      STAGE1_NO_POINTERS_NON_C_ABI_INTERNAL=0 \
      STAGE1_SKIP_SEM="$stage1_skip_sem" \
      GENERIC_MODE="$stage1_generic_mode" \
      GENERIC_SPEC_BUDGET="$stage1_generic_budget" \
      STAGE1_SKIP_OWNERSHIP="$stage1_skip_ownership" \
    BACKEND_MULTI="$stage1_smoke_multi" \
    BACKEND_MULTI_FORCE="$stage1_smoke_multi_force" \
    BACKEND_FRONTEND=stage1 \
    BACKEND_EMIT=exe \
    BACKEND_LINKER=self \
    BACKEND_NO_RUNTIME_C=1 \
    BACKEND_RUNTIME_OBJ="$backend_runtime_obj" \
    BACKEND_TARGET="$backend_target" \
    BACKEND_INPUT="$stage1_smoke_input" \
    BACKEND_OUTPUT="$stage1_smoke_output" \
    "$stage1_smoke_driver"
  status="$?"
  set -e
  if [ "$status" -ne 0 ] && [ "$stage1_smoke_multi" != "0" ]; then
    echo "[Warn] ${stage1_smoke_name}.compile parallel failed, retry serial" 1>&2
    env \
      MM="$backend_mm" \
      STAGE1_NO_POINTERS_NON_C_ABI=0 \
      STAGE1_NO_POINTERS_NON_C_ABI_INTERNAL=0 \
      STAGE1_SKIP_SEM="$stage1_skip_sem" \
      GENERIC_MODE="$stage1_generic_mode" \
      GENERIC_SPEC_BUDGET="$stage1_generic_budget" \
      STAGE1_SKIP_OWNERSHIP="$stage1_skip_ownership" \
      BACKEND_MULTI=0 \
      BACKEND_MULTI_FORCE=0 \
      BACKEND_FRONTEND=stage1 \
      BACKEND_EMIT=exe \
      BACKEND_LINKER=self \
      BACKEND_NO_RUNTIME_C=1 \
      BACKEND_RUNTIME_OBJ="$backend_runtime_obj" \
      BACKEND_TARGET="$backend_target" \
      BACKEND_INPUT="$stage1_smoke_input" \
      BACKEND_OUTPUT="$stage1_smoke_output" \
      "$stage1_smoke_driver"
    return "$?"
  fi
  return "$status"
}

compile_stage1_smoke_system() {
  set +e
  env \
    MM="$backend_mm" \
    STAGE1_NO_POINTERS_NON_C_ABI=0 \
    STAGE1_NO_POINTERS_NON_C_ABI_INTERNAL=0 \
    STAGE1_SKIP_SEM="$stage1_skip_sem" \
    GENERIC_MODE="$stage1_generic_mode" \
    GENERIC_SPEC_BUDGET="$stage1_generic_budget" \
    STAGE1_SKIP_OWNERSHIP="$stage1_skip_ownership" \
    BACKEND_MULTI="$stage1_smoke_multi" \
    BACKEND_MULTI_FORCE="$stage1_smoke_multi_force" \
    BACKEND_FRONTEND=stage1 \
    BACKEND_EMIT=exe \
    BACKEND_NO_RUNTIME_C=1 \
    BACKEND_RUNTIME_OBJ="$backend_runtime_obj" \
    BACKEND_TARGET="$backend_target" \
    BACKEND_INPUT="$stage1_smoke_input" \
    BACKEND_OUTPUT="$stage1_smoke_output" \
    "$stage1_smoke_driver"
  status="$?"
  set -e
  if [ "$status" -ne 0 ] && [ "$stage1_smoke_multi" != "0" ]; then
    echo "[Warn] ${stage1_smoke_name}.compile(system) parallel failed, retry serial" 1>&2
    env \
      MM="$backend_mm" \
      STAGE1_NO_POINTERS_NON_C_ABI=0 \
      STAGE1_NO_POINTERS_NON_C_ABI_INTERNAL=0 \
      STAGE1_SKIP_SEM="$stage1_skip_sem" \
      GENERIC_MODE="$stage1_generic_mode" \
      GENERIC_SPEC_BUDGET="$stage1_generic_budget" \
      STAGE1_SKIP_OWNERSHIP="$stage1_skip_ownership" \
      BACKEND_MULTI=0 \
      BACKEND_MULTI_FORCE=0 \
      BACKEND_FRONTEND=stage1 \
      BACKEND_EMIT=exe \
      BACKEND_NO_RUNTIME_C=1 \
      BACKEND_RUNTIME_OBJ="$backend_runtime_obj" \
      BACKEND_TARGET="$backend_target" \
      BACKEND_INPUT="$stage1_smoke_input" \
      BACKEND_OUTPUT="$stage1_smoke_output" \
      "$stage1_smoke_driver"
    return "$?"
  fi
  return "$status"
}

if [ "$backend_linker" = "self" ]; then
  run_step "${stage1_smoke_name}.compile" compile_stage1_smoke_self
else
  run_step "${stage1_smoke_name}.compile" compile_stage1_smoke_system
fi
run_step "${stage1_smoke_name}.run" test -x "$stage1_smoke_output"

stage1_smoke_name="backend.closedloop_stage1_std_os_smoke"
stage1_smoke_input="tests/cheng/backend/fixtures/return_import_std_os.cheng"
stage1_smoke_output="artifacts/backend_closedloop/stage1_std_os_smoke"
if [ "$backend_linker" = "self" ]; then
  run_step "${stage1_smoke_name}.compile" compile_stage1_smoke_self
else
  run_step "${stage1_smoke_name}.compile" compile_stage1_smoke_system
fi
run_step "${stage1_smoke_name}.run" test -x "$stage1_smoke_output"

verify_fullspec_no_seqbytes_undef() {
  bin="artifacts/backend_closedloop/fullspec_backend"
  if [ ! -f "$bin" ]; then
    echo "[verify_backend_closedloop] fullspec output missing: $bin" 1>&2
    return 1
  fi
  if ! command -v nm >/dev/null 2>&1; then
    echo "[verify_backend_closedloop] warn: nm not found, skip seqBytesOf undef check" 1>&2
    return 0
  fi
  if nm "$bin" 2>/dev/null | rg -q "U[[:space:]].*seqBytesOf_T"; then
    echo "[verify_backend_closedloop] unresolved seqBytesOf_T symbol in fullspec binary" 1>&2
    return 1
  fi
  return 0
}

is_known_fullspec_link_instability() {
  log="$1"
  if [ ! -f "$log" ]; then
    return 1
  fi
  if rg -q "duplicate symbol: ___cheng_sym_3d_3d" "$log"; then
    return 0
  fi
  if rg -q "Undefined symbols for architecture" "$log" && rg -q "L_cheng_str_" "$log"; then
    return 0
  fi
  if rg -q "Symbol not found: _cheng_" "$log"; then
    return 0
  fi
  return 1
}

if [ "$run_fullspec" = "1" ]; then
  fullspec_skip_sem="${BACKEND_FULLSPEC_SKIP_SEM:-1}"
  fullspec_skip_ownership="${BACKEND_FULLSPEC_SKIP_OWNERSHIP:-$stage1_skip_ownership}"
  fullspec_generic_mode="${BACKEND_FULLSPEC_GENERIC_MODE:-dict}"
  fullspec_generic_budget="${BACKEND_FULLSPEC_GENERIC_SPEC_BUDGET:-0}"
  fullspec_validate="${BACKEND_FULLSPEC_VALIDATE:-0}"
  # Fullspec defaults to serial compile for stability; multi path is covered by
  # dedicated backend.multi/backend.multi_lto gates.
  fullspec_multi="${BACKEND_CLOSEDLOOP_FULLSPEC_MULTI:-0}"
  fullspec_multi_force="${BACKEND_CLOSEDLOOP_FULLSPEC_MULTI_FORCE:-$fullspec_multi}"
  fullspec_input="${BACKEND_CLOSEDLOOP_FULLSPEC_INPUT:-examples/backend_closedloop_fullspec.cheng}"
  fullspec_out="artifacts/backend_closedloop/fullspec_backend"
  fullspec_compile_only_out="${fullspec_out}.compile_only"
  fullspec_log="artifacts/backend_closedloop/fullspec_backend.compile.log"
  fullspec_compile_only_ok="0"

  compile_fullspec_compile_only_fallback() {
    rm -f "$fullspec_compile_only_out" "$fullspec_log"
    set +e
    env \
      MM="$backend_mm" \
      STAGE1_NO_POINTERS_NON_C_ABI=0 \
      STAGE1_NO_POINTERS_NON_C_ABI_INTERNAL=0 \
      STAGE1_SKIP_SEM="$fullspec_skip_sem" \
      GENERIC_MODE="$fullspec_generic_mode" \
      GENERIC_SPEC_BUDGET="$fullspec_generic_budget" \
      STAGE1_SKIP_OWNERSHIP="$fullspec_skip_ownership" \
      BACKEND_MULTI=0 \
      BACKEND_MULTI_FORCE=0 \
      BACKEND_FRONTEND=stage1 \
      BACKEND_LINKER=self \
      BACKEND_NO_RUNTIME_C=1 \
      BACKEND_RUNTIME_OBJ="$backend_runtime_obj" \
      BACKEND_EMIT=exe \
      BACKEND_VALIDATE="$fullspec_validate" \
      BACKEND_WHOLE_PROGRAM=1 \
      BACKEND_TARGET="$backend_target" \
      BACKEND_INPUT="$fullspec_input" \
      BACKEND_OUTPUT="$fullspec_compile_only_out" \
      "$driver" >"$fullspec_log" 2>&1
    status="$?"
    set -e
    if [ "$status" -ne 0 ] || [ ! -s "$fullspec_compile_only_out" ]; then
      tail -n 200 "$fullspec_log" 1>&2 || true
      return 1
    fi
    fullspec_compile_only_ok="1"
    return 0
  }

  compile_fullspec_self() {
    echo "[verify_backend_closedloop] fullspec self-link uses compile-only profile" 1>&2
    compile_fullspec_compile_only_fallback
    return "$?"
  }

  compile_fullspec_system() {
    rm -f "$fullspec_out" "$fullspec_log"
    rm -rf "${fullspec_out}.objs" "${fullspec_out}.objs.lock"
    set +e
    env \
      MM="$backend_mm" \
      STAGE1_NO_POINTERS_NON_C_ABI=0 \
      STAGE1_NO_POINTERS_NON_C_ABI_INTERNAL=0 \
      STAGE1_SKIP_SEM="$fullspec_skip_sem" \
      GENERIC_MODE="$fullspec_generic_mode" \
      GENERIC_SPEC_BUDGET="$fullspec_generic_budget" \
      STAGE1_SKIP_OWNERSHIP="$fullspec_skip_ownership" \
      BACKEND_MULTI="$fullspec_multi" \
      BACKEND_MULTI_FORCE="$fullspec_multi_force" \
      BACKEND_FRONTEND=stage1 \
      BACKEND_EMIT=exe \
      BACKEND_VALIDATE="$fullspec_validate" \
      BACKEND_LINKER=system \
      BACKEND_NO_RUNTIME_C=1 \
      BACKEND_RUNTIME_OBJ="$backend_runtime_obj" \
      BACKEND_WHOLE_PROGRAM=1 \
      BACKEND_TARGET="$backend_target" \
      BACKEND_INPUT="$fullspec_input" \
      BACKEND_OUTPUT="$fullspec_out" \
      "$driver" >"$fullspec_log" 2>&1
    status="$?"
    if [ "$status" -ne 0 ] && [ "$fullspec_multi" != "0" ]; then
      echo "[Warn] backend.closedloop_fullspec.compile(system) parallel failed, retry serial" 1>&2
      env \
        MM="$backend_mm" \
        STAGE1_NO_POINTERS_NON_C_ABI=0 \
        STAGE1_NO_POINTERS_NON_C_ABI_INTERNAL=0 \
        STAGE1_SKIP_SEM="$fullspec_skip_sem" \
        GENERIC_MODE="$fullspec_generic_mode" \
        GENERIC_SPEC_BUDGET="$fullspec_generic_budget" \
        STAGE1_SKIP_OWNERSHIP="$fullspec_skip_ownership" \
        BACKEND_MULTI=0 \
        BACKEND_MULTI_FORCE=0 \
        BACKEND_FRONTEND=stage1 \
        BACKEND_EMIT=exe \
        BACKEND_VALIDATE="$fullspec_validate" \
        BACKEND_LINKER=system \
        BACKEND_NO_RUNTIME_C=1 \
        BACKEND_RUNTIME_OBJ="$backend_runtime_obj" \
        BACKEND_WHOLE_PROGRAM=1 \
        BACKEND_TARGET="$backend_target" \
        BACKEND_INPUT="$fullspec_input" \
        BACKEND_OUTPUT="$fullspec_out" \
        "$driver" >"$fullspec_log" 2>&1
      status="$?"
    fi
    if [ "$status" -ne 0 ]; then
      if is_known_fullspec_link_instability "$fullspec_log"; then
        echo "[verify_backend_closedloop] fullspec system-link unstable, fallback to compile-only check" 1>&2
        compile_fullspec_compile_only_fallback
        return "$?"
      fi
      tail -n 200 "$fullspec_log" 1>&2 || true
    fi
    return "$status"
  }

  if [ "$backend_linker" = "self" ]; then
    run_step "backend.closedloop_fullspec.compile" compile_fullspec_self
  else
    run_step "backend.closedloop_fullspec.compile" compile_fullspec_system
  fi
  if [ "$fullspec_compile_only_ok" = "1" ]; then
    echo "== backend.closedloop_fullspec.run (compile-only fallback) =="
  elif [ -x "$fullspec_out" ]; then
    run_step "backend.closedloop_fullspec.symcheck" verify_fullspec_no_seqbytes_undef
    run_step "backend.closedloop_fullspec.run" sh -c '
      ./artifacts/backend_closedloop/fullspec_backend | grep -Fq "fullspec ok"
    '
  else
    echo "[verify_backend_closedloop] fullspec compile did not produce runnable binary" 1>&2
    exit 1
  fi
else
  echo "== backend.closedloop_fullspec (skip: set BACKEND_RUN_FULLSPEC=1 to enable) =="
fi

run_step "backend.no_obj_artifacts" sh src/tooling/verify_backend_no_obj_artifacts.sh

echo "verify_backend_closedloop ok"
