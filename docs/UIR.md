# UIR 生产主链说明（自研后端，非 C-as-IR）

## 1) 架构定位
- `cheng` 当前生产链路不是“AST -> C 文本 -> Clang/GCC O3”的 C-as-IR 路线。
- 生产主链已收敛为：`Stage1(语义后 AST) -> UIR -> Machine -> Obj/Exe`。
- `backend_driver` 仅接受 `BACKEND_IR=uir`；非 `uir` 直接失败。
- 不再存在独立的 `MIR/LIR` 生产链路；`UIR` 是后端唯一规范 IR 家族。
- Runtime C 的角色是“预编译 OS/ABI 胶水对象 + 必要运行时符号提供”，不是“把业务源码翻译成 C 再交给 C 编译器做主优化”。
- Dev 轨固定走 `self-link + direct-exe` 的快速主链；Release 轨固定走 `system-link` 做最终物理收敛。
- 对外公开输出通道为 `cheng --emit:exe` 与 `release-compile --emit:exe|shared|static`；非 release 可执行构建不对外开放 `emit=obj`。
- 公开轨道固定口径：
  - `cheng` 固定 `BACKEND_STAGE1_PARSE_MODE=outline`、`BACKEND_FN_SCHED=ws`、`BACKEND_DIRECT_EXE=1`、`BACKEND_LINKERLESS_INMEM=1`、`BACKEND_FAST_FALLBACK_ALLOW=0`。
  - `release-compile` 固定 `BACKEND_STAGE1_PARSE_MODE=full`、`BACKEND_FN_SCHED=ws`、`BACKEND_DIRECT_EXE=0`、`BACKEND_LINKERLESS_INMEM=0`、`BACKEND_FAST_FALLBACK_ALLOW=0`、`BACKEND_LINKER=system`。
  - `BACKEND_JOBS` 是唯一公开 worker 数控制面；`BACKEND_FN_SCHED=serial` 仅保留给内部诊断、perf 对照与低内存 bring-up。
- 函数任务确定性约束：UIR/isel/direct-exe 的函数任务允许并行执行，但结果必须按稳定声明顺序 merge；最终 `.o/.exe` 字节流不得依赖 `BACKEND_JOBS`。

## 2) 单一 IR 双相模型（High-UIR / Low-UIR）
`UIR` 不是“单层拍平 IR”。当前生产口径已经按“单一 SoA 存储、双相语义执行”来理解：

- `High-UIR`（MIR 语义阶段）：
  - 保留 CFG、较高层类型/布局、借用与别名事实。
  - `ownership/borrow/noalias` 证明在这一相位完成，而不是等降级到纯 load/store 之后再猜。
  - 证明与回退状态需要可审计，相关观测体现在 `generics_report` / semantic protocol 中的 `high_uir_checked_funcs`、`high_uir_fallback_funcs`、`phaseFallbackReason` 等字段。
- `Low-UIR`（优化与产物阶段）：
  - 在同一套 UIR/SoA 容器上继续 lowering，进入 SSA、拍平布局、机器相关优化与 codegen。
  - 消费 `High-UIR` 下沉的 proof-backed 事实，驱动 `noalias`、`SSU`、`egraph`、`SROA/CSE/LICM`、向量化等 pass。
- 工程约束：
  - 不引入第二份独立 MIR 容器；主表示仍是 UIR SoA/index。
  - `High-UIR -> Low-UIR` 的事实传递必须可观测、可验收、可回退。

## 3) 为什么不是 C-as-IR
在编译器工程里，C-as-IR 的优势是快速跨平台与工具链复用；代价是高层语义降级到 C 语义后会显著损失控制力。  
`cheng` 选择 UIR 直出，目标是把优化、确定性和语义边界控制放在自研 IR 管线中完成，再与 Runtime C 对象在链接阶段会师。

对应工程现实：
- 生产主链仍是 `emit=exe`（UIR -> machine -> obj/exe）。
- `release-compile --emit:shared|static` 走 release object-first 打包，不改变“主优化在 UIR/Machine 侧完成”的原则。
- `emit=obj` 仅保留 internal gate / allow-no-main 工件生成通道。

## 4) UIR 相对 C-as-IR 的五个工程维度

### 4.1 内存与别名信息保真（High-UIR 证明 -> Low-UIR 消费）
- C-as-IR 下，许多高层 noalias/ownership 证明会在 C 指针语义中被稀释，优化器只能更保守。
- UIR 路线把 `High-UIR` 的别名/借用证明下沉到 `Low-UIR`，使 `SROA`、`CSE`、`LICM`、`egraph`、向量化等 pass 建立在 proof-backed 前提上，而不是纯启发式推断上。

### 4.2 内部调用约定可控（非公开边界）
- 对 C ABI 暴露边界必须守平台 ABI。
- 对模块内部调用，UIR/Machine 可按寄存器分配与目标机规则做更紧凑的调用布局，减少不必要栈往返。
- 该能力由 `machine_select_*` + `machine_regalloc` 路径承担，而不是依赖外部 C 编译器“猜测”。

### 4.3 栈帧与挂起点控制（异步/状态机）
- C-as-IR 常需要把高层异步/状态机改写成更保守的 C 结构，调度与寄存器活跃信息利用受限。
- UIR 在 lowering 到 machine 前可结合 CFG / liveness / phase facts 做更细粒度布局，减少保存/恢复开销。

### 4.4 相位排序可控（优化与计费/插桩）
- C-as-IR 场景下，插桩与 O3 的相互作用受外部编译器相位控制，行为稳定性更难约束。
- UIR 路线可在自研 pass 管线里先完成主优化，再在后段做受控插桩/收尾，降低“插桩破坏优化”与“版本漂移导致行为波动”的风险。

### 4.5 双轨收敛（UIR 主优化 + linker 物理缝合）
- Runtime C：提前编译为稳定对象，承担平台胶水。
- 业务代码：由 UIR/Machine 完成主要优化与裁剪。
- Release 末端由 system linker 完成物理链接与可用的链接期收尾优化。
- 这个模型避免“所有优化都押给 C 编译器”带来的不可控相位耦合，同时保留成熟链接器生态。

## 5) Current Architecture（按职责分层，不列全量文件）
以下为生产主链相关的代表性层次；不是穷举文件清单。

- Semantic protocol / 前端语义接入：
  - `src/backend/uir/uir_semantic_types.cheng`
  - `src/backend/uir/uir_semantic_stage1.cheng`
  - `src/backend/uir/uir_semantic_lowering.cheng`
  - `src/backend/uir/uir_semantic_validate.cheng`
  - `src/backend/uir/uir_frontend.cheng`
- UIR facade / core storage：
  - `src/backend/uir/uir_types.cheng`
  - `src/backend/uir/uir_builder.cheng`
  - `src/backend/uir/uir_internal/uir_core_types.cheng`
  - `src/backend/uir/uir_internal/uir_core_builder*.cheng`
  - `uir_core_types` 提供 SoA/index surface：`UirExprId/UirStmtId/UirBlockId/UirFuncId`、`UirCoreSoa`、`uirCoreSoaNew/Append*`、`uirCoreSoaBuildFromFunc`。
  - 执行路径有效性检查统一使用 `uirCoreExprIdValid/uirCoreStmtIdValid/uirCoreBlockIdValid`；模块函数集合访问统一使用 `uirCoreModuleFuncsLen/uirCoreModuleFuncAt`。
- 分析与优化：
  - `src/backend/uir/uir_opt.cheng`
  - `src/backend/uir/uir_pass_manager.cheng`
  - `src/backend/uir/uir_noalias_pass.cheng`
  - `src/backend/uir/uir_egraph_rewrite.cheng`
  - `src/backend/uir/uir_egraph_cost.cheng`
  - `src/backend/uir/uir_vectorize_loop.cheng`
  - `src/backend/uir/uir_vectorize_slp.cheng`
  - `src/backend/uir/uir_internal/uir_core_opt.cheng`
  - `src/backend/uir/uir_internal/uir_core_opt2.cheng`
  - `src/backend/uir/uir_internal/uir_core_ssu.cheng`
  - `src/backend/uir/uir_internal/uir_core_ssa.cheng`
- Lowering / Machine：
  - `src/backend/uir/uir_codegen.cheng`
  - `src/backend/machine/machine_types.cheng`
  - `src/backend/machine/machine_select_aarch64.cheng`
  - `src/backend/machine/machine_select_x86_64.cheng`
  - `src/backend/machine/machine_select_riscv64.cheng`
  - `src/backend/machine/machine_regalloc.cheng`
  - `src/backend/machine/machine_internal/*`
  - `src/backend/machine/select_internal/*`
- 目标写出：
  - `src/backend/obj/*` 消费 `MachineModule`。

## 6) Defaults
- `BACKEND_OPT_LEVEL` 默认 `2`（未设置时）。
- `GENERIC_MODE` 默认 `dict`。
- `GENERIC_SPEC_BUDGET` 默认 `0`。
- `BORROW_IR` 默认 `mir`。
- `GENERIC_LOWERING` 默认且唯一生产 lowering 为 `mir_dict`。
- `UIR_NOALIAS` 默认开启。
- `UIR_NOALIAS_NJVL_LITE` 默认开启。
- `UIR_SSU` 默认开启。
- `UIR_EGRAPH_ITERS` 默认 `2`，`UIR_EGRAPH_GOAL` 默认 `balanced`。
- `UIR_SIMD`：
  - 未设置：`optLevel>=3` 自动开启，`<3` 关闭。
  - 显式 `0/1`：强制关闭/开启。
- `UIR_SIMD_POLICY` 默认 `autovec`。

## 7) Diagnostics
- UIR profile：
  - `UIR_PROFILE=1`
  - 输出：`uir_profile\t<label>\tstep_ms=...\ttotal_ms=...`
- `generics_report`：
  - 当前生产口径除 `ir/mode/spec_budget/instances_*` 外，还会输出 `borrow_ir`、`generic_lowering`、`high_uir_checked_funcs`、`low_uir_lowered_funcs`、`high_uir_fallback_funcs`、`phase_contract_version`。
  - 这组字段是 High-UIR / Low-UIR 相位契约的直接观测面。
- `soa_report`：
  - `UIR_SOA_REPORT` 默认 `0`（仅 gate/诊断显式开启）。
  - `UIR_SOA_ENFORCE` 默认 `0`（避免日常编译误阻断；SoA gate 会显式设为 `1`）。
  - 输出：`soa_report\tenabled=...\tenforce=...\ttracked_funcs=...\texpr_ids=...\tstmt_ids=...\tblock_ids=...\tsucc_edges=...\tpred_edges=...\tbalance_ok=...`
- `noalias_report` / `ssu_report`：
  - `opt2` 默认会跑 `noalias + SSU`。
  - `UIR_NOALIAS_NJVL_LITE=1` 时，`noalias_report` 会附带 `unknown_slot_clobbers`、`unknown_global_clobbers`、`kill_events` 等扩展字段。
- runtime probe 例外：
  - `verify_backend_noalias_opt` / `verify_backend_egraph_cost` / `verify_backend_dod_opt_regression` 的门禁探针默认固定 `UIR_PROFILE=0` + `BACKEND_PROFILE=0`，避免 in-memory self-link 场景的 profile 崩溃；这些 gate 以报告字段与 surface marker 验收。

## 8) Optimization Pipeline
- `O1`：
  - `uir_opt.const_fold`
  - 若 `UIR_AGGRESSIVE=1`，可额外进入 full-pass 序列。
- `O2`（默认）：
  - `noalias` 预处理与 proof-backed 约束准备。
  - `SSU`。
  - `SoA` runtime report/check（显式开启时）。
  - `uir_core_opt2` 收敛。
  - safe copy-prop marker。
  - cleanup。
  - `egraph + cost model`（当 `UIR_EGRAPH_ITERS>0`）。
- `O3`：
  - `uir_opt3.pre_ssa_opt`
  - `uir_opt3.ssa.lower`
  - `uir_opt3.post_ssa_opt`
  - `uir_opt3.post_ssa_cleanup`
  - `uir_opt3.post_ssa_canonical`
  - `uir_opt3.final_cleanup`（含额外 `SROA/LICM/CSE/CFG canonical` 收尾）
- SIMD：
  - late vector passes 允许在 `opt2+` 执行。
  - 自动启用策略仍由 `UIR_SIMD` 默认规则控制；未设置时只在 `optLevel>=3` 自动开启。

可调参数：
- `UIR_OPT2_ITERS`
- `UIR_OPT3_ITERS`
- `UIR_OPT3_CLEANUP_ITERS`
- `UIR_CFG_CANON_ITERS`
- `UIR_INLINE_ITERS`
- `UIR_FULL_ITERS`
- `UIR_AGGRESSIVE`
- `UIR_EGRAPH_ITERS`
- `UIR_EGRAPH_GOAL`
- `UIR_NOALIAS_REQUIRE_PROOF`
- `UIR_EGRAPH_REQUIRE_PROOF`

## 9) Compatibility / Generics Policy
Removed env policy：
- 以下变量已下线，设置即失败：
  - `MIR_PROFILE`
  - `BACKEND_SSA`
  - `STAGE1_SKIP_MONO`
  - `BACKEND_SKIP_MONO`
  - `BACKEND_SKIP_MONO_AFTER_CPROFILE`
  - `BACKEND_SKIP_MONO_DICT`
  - `STAGE1_SKIP_MONO_AFTER_CPROFILE`
  - `STAGE1_SKIP_MONO_DICT`
- 报错格式：`backend_driver: removed env <NAME>, use <NEW_NAME>`。

Generics / borrow policy：
- `dict`：默认生产策略（编译时优先）。
- `hybrid`：已从执行路径移除；设置为 `hybrid` 会被 `cheng_tooling/backend_driver` 拒绝。
- `BORROW_IR=mir|stage1` 都被接受，但生产默认是 `mir`。
- `GENERIC_LOWERING` 当前仅支持 `mir_dict`。

## 10) Production Closure（UIR 相关视图）
命令前缀（避免 PATH 差异）：
- `TOOLING=artifacts/tooling_cmd/cheng_tooling`

权威入口：
- `$TOOLING backend_prod_closure --no-publish`
- 这条命令才是完整 required gate 集合的单一权威；本节只记录与 UIR 主链直接相关、且最容易误解的部分，不重复抄录全部 closure 明细。

默认 required 的 UIR 相关 gate：
- `$TOOLING verify_backend_no_legacy_refs`
- `$TOOLING verify_backend_opt2`
- `$TOOLING verify_backend_opt3`
- `$TOOLING verify_backend_simd`
- `$TOOLING verify_backend_uir_stability`
- `$TOOLING verify_backend_opt2_impl_surface`
- `$TOOLING verify_backend_uir_soa_surface`
- `$TOOLING verify_backend_uir_soa_self_probe`
- `$TOOLING verify_stage1_ast_soa_surface`

与 UIR call-site ABI lowering 直接相关、通常也在 closure 中 required 的 gate：
- `$TOOLING verify_backend_rawptr_contract`
- `$TOOLING verify_backend_rawptr_surface_forbid`
- `$TOOLING verify_backend_ffi_slice_shim`
- `$TOOLING verify_backend_ffi_outptr_tuple`
- `$TOOLING verify_backend_ffi_handle_sandbox`
- `$TOOLING verify_backend_ffi_borrow_bridge`

closure 中与本文强相关、但不属于“UIR 专项优化”本身的基础 gate：
- `$TOOLING verify_backend_symbol_closure`
- `$TOOLING verify_backend_release_compile_stability`
- `$TOOLING verify_backend_zero_script_closure`
- `$TOOLING verify_backend_zero_script_residual`
- `$TOOLING verify_backend_compile_name_canonical`
- `$TOOLING verify_backend_dev_track_only`

默认不属于必跑阻断的项：
- `$TOOLING verify_backend_multi_perf_regression`
  - 这是专用 perf 机器上的 opt-in gate；`backend_prod_closure` 默认关闭，需显式 `--multi-perf` 或 `BACKEND_RUN_MULTI_PERF=1` 开启。

fullspec 默认闭环口径（`BACKEND_RUN_FULLSPEC=1`）：
- `BACKEND_FULLSPEC_GENERIC_MODE=dict`
- `BACKEND_FULLSPEC_GENERIC_SPEC_BUDGET=0`
- `BACKEND_CLOSEDLOOP_FULLSPEC_MULTI=0`（串行）
- 编译后执行 `backend.closedloop_fullspec.symcheck`，阻断未解析 `seqBytesOf_T` 符号回归。

SoA 收口门禁（surface + runtime）：
- `$TOOLING verify_stage1_ast_soa_surface`
  - 输出 `artifacts/backend_soa_hardcut_baseline/{stage1_ref_surface,uir_ref_surface,rawptr_surface}.txt`。
  - 对外生产开关是 `BACKEND_PROD_STAGE1_AST_SOA_ENFORCE=1`；tooling 会转发为 gate 局部环境。
- `$TOOLING verify_backend_uir_soa_surface`
  - runtime 口径为双次 `emit=exe + system-link` 探针，要求 `soa_report` 可观测且两次报告一致（determinism）。
  - source-surface fallback 已移除；若命中旧 driver 且缺失 `soa_report`，只接受 runtime `noalias_report + ssu_report` 的兼容路径。
  - 同时断言 `src/backend/uir` 不得出现 `uirCoreExprRefValid/uirCoreStmtRefValid/uirCoreBlockRefValid` 回流。
- `$TOOLING verify_backend_uir_soa_self_probe`
  - 固定开启并强制 self-link 子探针（等价 `BACKEND_UIR_SOA_SELF_PROBE=1` + `BACKEND_UIR_SOA_SELF_PROBE_ENFORCE=1`）。
  - `backend_prod_closure` required 默认同时执行 `backend.uir_soa_surface` 与 `backend.uir_soa_self_probe`。

## 11) Scope Notes
- 本文档描述生产主链口径，不记录全部历史兼容路径。
- 语言语义以 `docs/cheng-formal-spec.md` 为准；tooling 默认值、闭环命令和 gate 明细以 `src/tooling/README.md` 为准。
- internal 命名已完成收敛：`uir_internal` / `machine_internal` / `select_internal` 不再保留 `Mir/Lir` 与 `lr/lc/lo` 旧符号；`verify_backend_no_legacy_refs` 对 internal + non-internal 一并做硬阻断。
