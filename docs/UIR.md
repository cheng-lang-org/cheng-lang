# UIR 生产主链说明（自研，不依赖 LLVM）

## Summary
- 生产主链已收敛为：`Stage1(语义后 AST) -> UIR -> Machine -> Obj/Exe`。
- `backend_driver` 仅接受 `BACKEND_IR=uir`；非 `uir` 直接失败。
- 编译器仍是自研实现：借鉴 LLVM 的统一 IR/SSA 思路，但不引入 LLVM IR/LLVM 库作为生产依赖。

## Current Architecture
- UIR 门面：
  - `src/backend/uir/uir_frontend.cheng`
  - `src/backend/uir/uir_types.cheng`
  - `src/backend/uir/uir_opt.cheng`
  - `src/backend/uir/uir_validate.cheng`
  - `src/backend/uir/uir_codegen.cheng`
- UIR internal：
  - `src/backend/uir/uir_internal/uir_core_builder.cheng`
  - `src/backend/uir/uir_internal/uir_core_opt.cheng`
  - `src/backend/uir/uir_internal/uir_core_opt2.cheng`
  - `src/backend/uir/uir_internal/uir_core_ssa.cheng`
  - `src/backend/uir/uir_internal/uir_core_types.cheng`
- Machine 层：
  - `src/backend/machine/machine_types.cheng`
  - `src/backend/machine/machine_internal/machine_core_types.cheng`
  - `src/backend/machine/select_internal/*`
- 目标写出：`src/backend/obj/*` 消费 `MachineModule`。

## Defaults
- `BACKEND_OPT_LEVEL` 默认 `2`（未设置时）。
- `GENERIC_MODE` 默认 `dict`。
- `GENERIC_SPEC_BUDGET` 默认 `0`。
- `UIR_SIMD`：
  - 未设置：`optLevel>=3` 自动开启，`<3` 关闭
  - 显式 `0/1`：强制关闭/开启
- `UIR_SIMD_POLICY` 默认 `autovec`。

## Removed Env Policy
以下变量已下线，设置即失败：
- `MIR_PROFILE`
- `BACKEND_SSA`
- `STAGE1_SKIP_MONO`
- `BACKEND_SKIP_MONO`
- `BACKEND_SKIP_MONO_AFTER_CPROFILE`
- `BACKEND_SKIP_MONO_DICT`
- `STAGE1_SKIP_MONO_AFTER_CPROFILE`
- `STAGE1_SKIP_MONO_DICT`

报错格式：`backend_driver: removed env <NAME>, use <NEW_NAME>`。

## Diagnostics
- UIR profile：
  - `UIR_PROFILE=1`
  - 输出：`uir_profile\t<label>\tstep_ms=...\ttotal_ms=...`
- Generics report：
  - 输出：`generics_report\tir=uir\tmode=...\tspec_budget=...\tinstances_total=...\tinstances_reused=...\tinstances_specialized=...\tdict_calls=...\tms=...`

## Optimization Pipeline
- `O1`：
  - const-fold（`uir_opt.const_fold`）
- `O2`（默认）：
  - `uir_opt2` 收敛 + SROA/CSE/LICM + cleanup + CFG canonical
- `O3`：
  - `uir_opt3.pre_ssa_opt`
  - `uir_opt3.ssa.lower`
  - `uir_opt3.post_ssa_opt` / `final_cleanup`
  - SIMD late vector passes（启用时）

可调参数：
- `UIR_OPT2_ITERS`
- `UIR_OPT3_ITERS`
- `UIR_OPT3_CLEANUP_ITERS`
- `UIR_CFG_CANON_ITERS`
- `UIR_INLINE_ITERS`
- `UIR_AGGRESSIVE`
- `UIR_FULL_ITERS`

## Generics Policy
- `dict`：默认生产策略（编译时优先）。
- `hybrid`：预算内特化（`GENERIC_SPEC_BUDGET>0` 时启用特化，超预算回退字典路径）。

## Production Closure Gates
必跑（阻断）：
- `sh src/tooling/tooling_exec.sh verify_backend_no_legacy_refs`
- `sh src/tooling/tooling_exec.sh verify_backend_opt2`
- `sh src/tooling/tooling_exec.sh verify_backend_opt3`
- `sh src/tooling/tooling_exec.sh verify_backend_multi_perf_regression`
- `sh src/tooling/tooling_exec.sh verify_backend_simd`
- `sh src/tooling/tooling_exec.sh verify_backend_uir_stability`
- `sh src/tooling/tooling_exec.sh backend_prod_closure --no-publish`

fullspec 默认闭环口径（`BACKEND_RUN_FULLSPEC=1`）：
- `BACKEND_FULLSPEC_GENERIC_MODE=dict`
- `BACKEND_FULLSPEC_GENERIC_SPEC_BUDGET=0`
- `BACKEND_CLOSEDLOOP_FULLSPEC_MULTI=0`（串行）
- 编译后执行 `backend.closedloop_fullspec.symcheck`，阻断未解析 `seqBytesOf_T` 符号回归。

通过标准：以上门禁全部 `ok`，且 `backend_prod_closure` 输出 `backend_prod_closure ok`。

## Scope Notes
- 本文档描述生产主链口径。
- internal 命名已完成收敛：`uir_internal`/`machine_internal`/`select_internal` 不再保留 `Mir/Lir` 与 `lr/lc/lo` 旧符号；`verify_backend_no_legacy_refs` 对 internal + non-internal 一并做硬阻断。
