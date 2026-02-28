# UIR 生产主链说明（自研后端，非 C-as-IR）

## 1) 架构定位
- `cheng` 当前生产链路不是“AST -> C 文本 -> Clang/GCC O3”的 C Backend 路线。
- 生产主链已收敛为：`Stage1(语义后 AST) -> UIR -> Machine -> Obj/Exe`。
- `backend_driver` 仅接受 `BACKEND_IR=uir`；非 `uir` 直接失败。
- Runtime C 的角色是“预编译 OS/ABI 胶水对象 + 必要运行时符号提供”，不是“把业务源码翻译成 C 再交给 C 编译器做主优化”。
- Release 轨默认通过 system linker 做最终物理收敛（包含链接期优化能力），但中间优化主导权在 UIR/Machine 侧。

## 2) 为什么不是 C-as-IR
在编译器工程里，C-as-IR 的优势是快速跨平台与工具链复用；代价是前端语义在降级到 C 语义后会损失控制力。  
`cheng` 选择 UIR 直出，目标是把优化与确定性控制放在自研 IR 管线中完成，再与 Runtime C 对象在链接阶段会师。

对应工程现实：
- 不支持 `emit-c` / `--backend:c` 生产路径。
- 对外主语义是 `emit=exe`。
- `emit=obj` 仅 internal gate 使用。

## 3) UIR 相对 C-as-IR 的五个工程维度

### 3.1 内存与别名信息保真（NoAlias 传递）
- C-as-IR 下，许多高层 noalias/ownership 证明会在 C 指针语义中被稀释，优化器更保守。
- UIR 直接消费前端/中间层的别名事实（如 noalias pass 结果），在 SROA、CSE、LICM、向量化等 pass 中可保持更激进但可证的优化前提。

### 3.2 内部调用约定可控（非公开边界）
- 对 C ABI 暴露边界必须守平台 ABI。
- 对模块内部调用，UIR/Machine 可按寄存器分配与目标机规则做更紧凑的调用布局，减少不必要栈往返。
- 该能力由 `machine_select_*` + `machine_regalloc` 路径承担，而不是依赖 C 编译器“猜测”。

### 3.3 栈帧与挂起点控制（异步/状态机）
- C-as-IR 常需要把高层异步/状态机改写成更保守的 C 结构，调度与寄存器活跃信息利用受限。
- UIR 在 lowering 到 machine 前可结合 CFG/liveness 做更细粒度布局，减少保存/恢复开销。
- 本质是“在 IR 层保留语义，再到机器层定制实现”，而不是先退化成 C 语义。

### 3.4 相位排序可控（优化与计费/插桩）
- C-as-IR 场景下，插桩与 O3 的相互作用受外部编译器相位控制，行为稳定性更难约束。
- UIR 路线可在自研 pass 管线里先完成主优化，再在后段做受控插桩/收尾，降低“插桩破坏优化”与“版本漂移导致行为波动”的风险。

### 3.5 双轨收敛（UIR 主优化 + system linker 物理缝合）
- Runtime C：提前编译为稳定对象，承担平台胶水。
- 业务代码：由 UIR/Machine 完成主要优化与裁剪。
- 最后交给系统链接器做物理链接与 LTO 收敛。
- 这个模型避免“所有优化都押给 C 编译器”带来的不可控相位耦合，同时保留成熟链接器生态。

## 4) Current Architecture
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

## 5) Defaults
- `BACKEND_OPT_LEVEL` 默认 `2`（未设置时）。
- `GENERIC_MODE` 默认 `dict`。
- `GENERIC_SPEC_BUDGET` 默认 `0`。
- `UIR_SIMD`：
  - 未设置：`optLevel>=3` 自动开启，`<3` 关闭。
  - 显式 `0/1`：强制关闭/开启。
- `UIR_SIMD_POLICY` 默认 `autovec`。

## 6) Diagnostics
- UIR profile：
  - `UIR_PROFILE=1`
  - 输出：`uir_profile\t<label>\tstep_ms=...\ttotal_ms=...`
- runtime probe 例外：
  - `verify_backend_noalias_opt` / `verify_backend_egraph_cost` / `verify_backend_dod_opt_regression` 的门禁探针默认固定 `UIR_PROFILE=0` + `BACKEND_PROFILE=0`，避免 in-memory self-link 场景的 profile 崩溃；这些 gate 以报告字段与 surface marker 验收。
- Generics report：
  - 输出：`generics_report\tir=uir\tmode=...\tspec_budget=...\tinstances_total=...\tinstances_reused=...\tinstances_specialized=...\tdict_calls=...\tms=...`

## 7) Optimization Pipeline
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

## 8) Removed Env Policy
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

## 9) Generics Policy
- `dict`：默认生产策略（编译时优先）。
- `hybrid`：预算内特化（`GENERIC_SPEC_BUDGET>0` 时启用特化，超预算回退字典路径）。

## 10) Production Closure Gates
命令前缀（避免 PATH 差异）：
- `TOOLING=artifacts/tooling_cmd/cheng_tooling`

必跑（阻断）：
- `$TOOLING verify_backend_no_legacy_refs`
- `$TOOLING verify_backend_opt2`
- `$TOOLING verify_backend_opt3`
- `$TOOLING verify_backend_multi_perf_regression`
- `$TOOLING verify_backend_simd`
- `$TOOLING verify_backend_uir_stability`
- `$TOOLING backend_prod_closure --no-publish`

fullspec 默认闭环口径（`BACKEND_RUN_FULLSPEC=1`）：
- `BACKEND_FULLSPEC_GENERIC_MODE=dict`
- `BACKEND_FULLSPEC_GENERIC_SPEC_BUDGET=0`
- `BACKEND_CLOSEDLOOP_FULLSPEC_MULTI=0`（串行）
- 编译后执行 `backend.closedloop_fullspec.symcheck`，阻断未解析 `seqBytesOf_T` 符号回归。

通过标准：以上门禁全部 `ok`，且 `backend_prod_closure` 输出 `backend_prod_closure ok`。

## 11) Scope Notes
- 本文档描述生产主链口径。
- internal 命名已完成收敛：`uir_internal`/`machine_internal`/`select_internal` 不再保留 `Mir/Lir` 与 `lr/lc/lo` 旧符号；`verify_backend_no_legacy_refs` 对 internal + non-internal 一并做硬阻断。
