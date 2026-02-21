#!/usr/bin/env sh
. "$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)/env_prefix_bridge.sh"
set -eu
(set -o pipefail) 2>/dev/null && set -o pipefail

root="$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)"
cd "$root"

if ! command -v rg >/dev/null 2>&1; then
  echo "[verify_backend_no_legacy_refs] rg is required" 1>&2
  exit 2
fi

# Hard guard for production entrypoints + all UIR source files.
bad_core="$(rg -n "import[[:space:]]+cheng/backend/(mir|lir|isel|regalloc)" \
  src/backend/tooling/backend_driver.cheng \
  src/backend/machine -g '*.cheng' \
  src/backend/uir -g '*.cheng' || true)"

if [ "$bad_core" != "" ]; then
  echo "[verify_backend_no_legacy_refs] forbidden legacy import in production UIR path:" 1>&2
  echo "$bad_core" 1>&2
  exit 1
fi

# Hard guard: no stale compat import roots after compat directory removal.
bad_compat="$(rg -n "import[[:space:]]+cheng/backend/compat/" src/backend -g '*.cheng' || true)"
if [ "$bad_compat" != "" ]; then
  echo "[verify_backend_no_legacy_refs] forbidden removed compat import path:" 1>&2
  echo "$bad_compat" 1>&2
  exit 1
fi

# Internal MIR/LIR/isel/regalloc layers are only allowed in dedicated machine/uir
# implementation boundaries, never through removed compat shims.
bad_internal_surface="$(rg -n "import[[:space:]]+cheng/backend/(uir/uir_internal|machine/(machine_internal|select_internal|regalloc_internal))" \
  src/backend/uir src/backend/machine -g '*.cheng' \
  -g '!src/backend/uir/uir_internal/**' \
  -g '!src/backend/machine/machine_internal/**' \
  -g '!src/backend/machine/select_internal/**' \
  -g '!src/backend/machine/regalloc_internal/**' \
  -g '!src/backend/uir/uir_types.cheng' \
  -g '!src/backend/uir/uir_frontend.cheng' \
  -g '!src/backend/uir/uir_validate.cheng' \
  -g '!src/backend/uir/uir_opt.cheng' \
  -g '!src/backend/uir/uir_noalias_pass.cheng' \
  -g '!src/backend/uir/uir_egraph_rewrite.cheng' \
  -g '!src/backend/machine/machine_types.cheng' \
  -g '!src/backend/machine/machine_select_aarch64.cheng' \
  -g '!src/backend/machine/machine_select_x86_64.cheng' \
  -g '!src/backend/machine/machine_select_riscv64.cheng' \
  -g '!src/backend/machine/machine_regalloc.cheng' || true)"
if [ "$bad_internal_surface" != "" ]; then
  echo "[verify_backend_no_legacy_refs] forbidden direct internal backend import outside allowed facades:" 1>&2
  echo "$bad_internal_surface" 1>&2
  exit 1
fi

# Removed compat files must not reappear.
for removed in \
  src/backend/uir/uir_compat_bridge.cheng \
  src/backend/uir/uir_compat_types.cheng
do
  if [ -f "$removed" ]; then
    echo "[verify_backend_no_legacy_refs] forbidden removed compat file present: $removed" 1>&2
    exit 1
  fi
done

# Hard-cut guard: removed legacy backend source files/directories must stay absent.
for removed_legacy in \
  src/backend/mir \
  src/backend/lir \
  src/backend/isel \
  src/backend/regalloc/linear_scan.cheng
do
  if [ -e "$removed_legacy" ]; then
    echo "[verify_backend_no_legacy_refs] forbidden legacy backend source present: $removed_legacy" 1>&2
    exit 1
  fi
done

# Legacy scaffold dirs must not carry source files on production backend.
for removed_scaffold in \
  src/backend/hir \
  src/backend/frame \
  src/backend/emit
do
  if [ -d "$removed_scaffold" ]; then
    legacy_scaffold_files="$(find "$removed_scaffold" -name '*.cheng' -type f 2>/dev/null | head -n 1 || true)"
    if [ "$legacy_scaffold_files" != "" ]; then
      echo "[verify_backend_no_legacy_refs] forbidden legacy scaffold source present: $legacy_scaffold_files" 1>&2
      exit 1
    fi
  fi
done

# select internal implementation must consume UIR facade, not mir_types directly.
bad_isel_mir_types="$(rg -n "import[[:space:]]+cheng/backend/uir/uir_internal/uir_core_types" \
  src/backend/machine/select_internal -g '*.cheng' || true)"
if [ "$bad_isel_mir_types" != "" ]; then
  echo "[verify_backend_no_legacy_refs] forbidden direct mir_types import in select_internal:" 1>&2
  echo "$bad_isel_mir_types" 1>&2
  exit 1
fi

# select internal implementation should depend on machine facade, not machine_internal directly.
bad_isel_machine_internal_import="$(rg -n "import[[:space:]]+cheng/backend/machine/machine_internal/" \
  src/backend/machine/select_internal -g '*.cheng' || true)"
if [ "$bad_isel_machine_internal_import" != "" ]; then
  echo "[verify_backend_no_legacy_refs] forbidden direct machine_internal import in select_internal:" 1>&2
  echo "$bad_isel_machine_internal_import" 1>&2
  exit 1
fi

# select internal public/internal signatures should stay on UIR facade symbols.
bad_isel_mir_symbols="$(rg -n "\\bMir[A-Za-z0-9_]+\\b" src/backend/machine/select_internal -g '*.cheng' || true)"
if [ "$bad_isel_mir_symbols" != "" ]; then
  echo "[verify_backend_no_legacy_refs] forbidden Mir* symbol usage in select_internal:" 1>&2
  echo "$bad_isel_mir_symbols" 1>&2
  exit 1
fi

# select internal type constructors should come from UIR facade wrappers.
bad_isel_mir_type_ctor="$(rg -n "\\bmirType[A-Za-z0-9_]+\\b" src/backend/machine/select_internal -g '*.cheng' || true)"
if [ "$bad_isel_mir_type_ctor" != "" ]; then
  echo "[verify_backend_no_legacy_refs] forbidden mirType* constructor usage in select_internal:" 1>&2
  echo "$bad_isel_mir_type_ctor" 1>&2
  exit 1
fi

# select internal should use Machine* type surface (not Lir* type names).
bad_isel_lir_type_symbols="$(rg -n "\\bLir(Reg|Cond|Op|Inst|Func|CString|Global|Module)\\b" \
  src/backend/machine/select_internal -g '*.cheng' || true)"
if [ "$bad_isel_lir_type_symbols" != "" ]; then
  echo "[verify_backend_no_legacy_refs] forbidden Lir* type usage in select_internal:" 1>&2
  echo "$bad_isel_lir_type_symbols" 1>&2
  exit 1
fi

# regalloc facade is the only production surface allowed to import linear_scan directly.
bad_linear_scan_surface="$(rg -n "import[[:space:]]+cheng/backend/machine/regalloc_internal/linear_scan" \
  src/backend -g '*.cheng' \
  -g '!src/backend/machine/machine_regalloc.cheng' \
  -g '!src/backend/machine/regalloc_internal/**' || true)"
if [ "$bad_linear_scan_surface" != "" ]; then
  echo "[verify_backend_no_legacy_refs] forbidden direct linear_scan import outside machine_regalloc facade:" 1>&2
  echo "$bad_linear_scan_surface" 1>&2
  exit 1
fi

# Production-facing machine/object surfaces should use Machine* types instead of legacy Lir* aliases.
bad_lir_surface="$(rg -n "\\bLir(Reg|Cond|Op|Inst|Func|CString|Global|Module)\\b" \
  src/backend/obj \
  src/backend/machine -g '*.cheng' \
  -g '!src/backend/machine/machine_types.cheng' \
  -g '!src/backend/machine/machine_internal/**' \
  -g '!src/backend/machine/select_internal/**' \
  -g '!src/backend/machine/regalloc_internal/**' || true)"
if [ "$bad_lir_surface" != "" ]; then
  echo "[verify_backend_no_legacy_refs] forbidden Lir* symbol usage on production machine/object surface:" 1>&2
  echo "$bad_lir_surface" 1>&2
  exit 1
fi

# Production machine/object/backend surfaces should call machine* constructors, not lir*.
bad_lir_ctor_surface="$(rg -n "\\blir[A-Z][A-Za-z0-9_]*\\b" \
  src/backend/obj \
  src/backend/machine \
  src/backend/uir \
  src/backend/tooling -g '*.cheng' \
  -g '!src/backend/machine/machine_internal/**' \
  -g '!src/backend/uir/uir_internal/**' || true)"
if [ "$bad_lir_ctor_surface" != "" ]; then
  echo "[verify_backend_no_legacy_refs] forbidden lir* constructor usage outside internal layers:" 1>&2
  echo "$bad_lir_ctor_surface" 1>&2
  exit 1
fi

# Production-facing diagnostics should not expose legacy LIR wording.
bad_lir_wording="$(rg -n "unsupported lir op|float lir op unsupported" \
  src/backend/obj \
  src/backend/machine -g '*.cheng' || true)"
if [ "$bad_lir_wording" != "" ]; then
  echo "[verify_backend_no_legacy_refs] forbidden legacy lir wording on production machine/object surface:" 1>&2
  echo "$bad_lir_wording" 1>&2
  exit 1
fi

# Production machine/object/select surfaces should avoid legacy enum tokens.
bad_legacy_enum_surface="$(rg -n "\\b(lr(W[0-8]|X(0|1|2|3|4|5|6|7|8|16|17|19|29|30)|Sp|D[01]|E(ax|cx|dx|bx|si|di)|R(8d|9d|10d|11d|12d|13d|14d|15d|ax|cx|dx|bx|si|di|bp|8|9|10|11|12|13|14|15))|lc(Eq|Ne|Mi|Lt|Le|Gt|Ge|Lo|Ls|Hi|Hs)|lo(Label|MovImm|MovReg|Sxtw|Adrp|AddPageOff|Add|Sub|Mul|Sdiv|Udiv|Msub|And|Orr|Eor|Lsl|Lsr|Asr|Cmp|BCond|B|Bl|Blr|FmovDx|FmovXd|FaddD|FsubD|FmulD|FdivD|FnegD|FcmpD|ScvtfDx|Svc|Ret|Str|Ldr|Strb|Ldrb|Ldrsb|Strh|Ldrh|Ldrsh|SubSp|AddSp))\\b" \
  src/backend/obj \
  src/backend/machine/select_internal -g '*.cheng' || true)"
if [ "$bad_legacy_enum_surface" != "" ]; then
  echo "[verify_backend_no_legacy_refs] forbidden legacy enum token usage on production machine/object/select surface:" 1>&2
  echo "$bad_legacy_enum_surface" 1>&2
  exit 1
fi

# Non-internal production surface must not leak Mir/Lir symbol names.
bad_legacy_symbol_surface="$(rg -n "\\b(Mir[A-Za-z0-9_]+|Lir[A-Za-z0-9_]+|mir[A-Z][A-Za-z0-9_]*|lir[A-Z][A-Za-z0-9_]*)\\b" \
  src/backend -g '*.cheng' \
  -g '!src/backend/uir/uir_internal/**' \
  -g '!src/backend/machine/machine_internal/**' \
  -g '!src/backend/machine/select_internal/**' || true)"
if [ "$bad_legacy_symbol_surface" != "" ]; then
  echo "[verify_backend_no_legacy_refs] forbidden Mir/Lir symbol leakage on non-internal backend surface:" 1>&2
  echo "$bad_legacy_symbol_surface" 1>&2
  exit 1
fi

# Internal implementation naming is also converged: no Mir/Lir symbols remain.
bad_legacy_symbol_internal="$(rg -n "\\b(Mir[A-Za-z0-9_]+|Lir[A-Za-z0-9_]+|mir[A-Z][A-Za-z0-9_]*|lir[A-Z][A-Za-z0-9_]*)\\b" \
  src/backend/uir/uir_internal \
  src/backend/machine/machine_internal \
  src/backend/machine/select_internal -g '*.cheng' || true)"
if [ "$bad_legacy_symbol_internal" != "" ]; then
  echo "[verify_backend_no_legacy_refs] forbidden Mir/Lir symbol usage in internal backend implementation:" 1>&2
  echo "$bad_legacy_symbol_internal" 1>&2
  exit 1
fi

# Machine internal enum tokens are converged to machineCore* namespace.
bad_legacy_machine_enum_internal="$(rg -n "\\b(lr[A-Z][A-Za-z0-9_]*|lc[A-Z][A-Za-z0-9_]*|lo[A-Z][A-Za-z0-9_]*)\\b" \
  src/backend/machine/machine_internal -g '*.cheng' || true)"
if [ "$bad_legacy_machine_enum_internal" != "" ]; then
  echo "[verify_backend_no_legacy_refs] forbidden legacy lr/lc/lo enum token in machine_internal:" 1>&2
  echo "$bad_legacy_machine_enum_internal" 1>&2
  exit 1
fi

# Transitional compat aliases are fully removed; any usage is forbidden.
bad_uir_compat_surface="$(rg -n "\\bUirCompat(Type|TypeKind|Block)\\b" \
  src/backend -g '*.cheng' || true)"
if [ "$bad_uir_compat_surface" != "" ]; then
  echo "[verify_backend_no_legacy_refs] forbidden UirCompat* alias usage:" 1>&2
  echo "$bad_uir_compat_surface" 1>&2
  exit 1
fi

# Object writers must go through machine facade instead of internal MIR/LIR/isel/regalloc layers.
bad_obj="$(rg -n "import[[:space:]]+cheng/backend/(uir/uir_internal|machine/(machine_internal|select_internal|regalloc_internal))" \
  src/backend/obj -g '*.cheng' || true)"
if [ "$bad_obj" != "" ]; then
  echo "[verify_backend_no_legacy_refs] forbidden legacy import in object writers:" 1>&2
  echo "$bad_obj" 1>&2
  exit 1
fi

# Tooling scripts should not force legacy knobs.
bad_tooling="$(rg -n "MIR_PROFILE|BACKEND_SSA|STAGE1_SKIP_MONO|BACKEND_SKIP_MONO|C_SYSTEM" src/tooling -g '*.sh' -g '!verify_backend_no_legacy_refs.sh' || true)"
if [ "$bad_tooling" != "" ]; then
  echo "[verify_backend_no_legacy_refs] forbidden legacy env usage in tooling scripts:" 1>&2
  echo "$bad_tooling" 1>&2
  exit 1
fi

# Stage0 compat overlay has been removed from production tooling.
bad_stage0_compat_generator="$(rg -n "gen_stage0_compat_src\\.py" src/tooling -g '*.sh' -g '!verify_backend_no_legacy_refs.sh' || true)"
if [ "$bad_stage0_compat_generator" != "" ]; then
  echo "[verify_backend_no_legacy_refs] forbidden stage0 compat overlay generator usage in tooling scripts:" 1>&2
  echo "$bad_stage0_compat_generator" 1>&2
  exit 1
fi

bad_stage0_compat_default="$(rg -n "CHENG_(SELF_OBJ_BOOTSTRAP|BACKEND_BUILD_DRIVER)_STAGE0_COMPAT:-1" src/tooling -g '*.sh' -g '!verify_backend_no_legacy_refs.sh' || true)"
if [ "$bad_stage0_compat_default" != "" ]; then
  echo "[verify_backend_no_legacy_refs] forbidden stage0 compat default=1 in tooling scripts:" 1>&2
  echo "$bad_stage0_compat_default" 1>&2
  exit 1
fi

# Backend source files must not read removed legacy env knobs directly.
bad_backend_removed_env_reads="$(rg -n "MIR_PROFILE|BACKEND_SSA|STAGE1_SKIP_MONO|BACKEND_SKIP_MONO|C_SYSTEM" \
  src/backend -g '*.cheng' \
  -g '!src/backend/tooling/backend_driver.cheng' || true)"
if [ "$bad_backend_removed_env_reads" != "" ]; then
  echo "[verify_backend_no_legacy_refs] forbidden removed legacy env reads in backend source:" 1>&2
  echo "$bad_backend_removed_env_reads" 1>&2
  exit 1
fi

# Stage1 source files must not read removed legacy frontend env knobs.
bad_stage1_removed_env_reads="$(rg -n "C_SYSTEM" src/stage1 -g '*.cheng' || true)"
if [ "$bad_stage1_removed_env_reads" != "" ]; then
  echo "[verify_backend_no_legacy_refs] forbidden removed frontend env reads in stage1 source:" 1>&2
  echo "$bad_stage1_removed_env_reads" 1>&2
  exit 1
fi

# ABI v1 is removed from production tooling/backend surfaces.
bad_abi_v1_surface="$(rg -n "ABI[^\\n]*v1|--abi:v1" \
  src/backend src/tooling scripts verify.sh \
  -g '*.cheng' -g '*.sh' -g '!src/tooling/verify_backend_no_legacy_refs.sh' || true)"
if [ "$bad_abi_v1_surface" != "" ]; then
  echo "[verify_backend_no_legacy_refs] forbidden ABI v1 usage in production surfaces:" 1>&2
  echo "$bad_abi_v1_surface" 1>&2
  exit 1
fi

echo "verify_backend_no_legacy_refs ok"
