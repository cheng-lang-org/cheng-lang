# LLVM-like Unified IR 一次替换方案（自研，不接 LLVM）

## Summary
- 目标架构改为 `Stage1(语义后 AST) -> 统一 SSA IR(UIR) -> 目标输出(.o/.exe)`，不再保留独立 `MIR/LIR` 主链路。
- 采用 LLVM 思路但不引入 LLVM 依赖：统一 IR 承担类型化表达、优化、泛型 lowering、平台无关变换。
- 优先级按你选择执行：编译速度优先、一次替换、去掉 LIR。

## 落地状态（2026-02-13）
- 已落地（生产可用）：
  - 新增 `src/backend/uir/uir_types.cheng`、`src/backend/uir/uir_builder.cheng`、`src/backend/uir/uir_opt.cheng`，建立统一 IR API 入口。
  - `src/backend/tooling/backend_driver.cheng` 默认并仅支持 `CHENG_BACKEND_IR=uir`（非 `uir` 直接报错），主入口已收敛到 UIR API。
  - 新增 `CHENG_UIR_PROFILE=1`，输出 `uir_profile\t<label>\tstep_ms=...\ttotal_ms=...`。
  - 新增 `CHENG_GENERIC_MODE`（默认 `hybrid`）与 `CHENG_GENERIC_SPEC_BUDGET`（默认 `0`）读取与 `generics_report\t...` 诊断输出。
  - 自举/构建脚本默认导出 UIR 口径：`src/tooling/verify_backend_selfhost_bootstrap_self_obj.sh`、`src/tooling/build_backend_driver.sh`。
- 当前实现策略：
  - UIR API 先由 MIR 实现承载（兼容层），保证生产链路与门禁可运行、可观测、可回归。
- 下一阶段（未完成）：
  - 原生 `UIR -> machine emit` 与 MIR/LIR 物理移除；
  - 泛型“字典传递 + 预算特化”从 stage1 前置单态化迁移到 UIR 内核。

## Public APIs / Interfaces
- 新增环境变量：`CHENG_BACKEND_IR=uir`、`CHENG_UIR_PROFILE=1`、`CHENG_GENERIC_MODE=hybrid`、`CHENG_GENERIC_SPEC_BUDGET=<N>`。
- 下线环境变量：`CHENG_MIR_PROFILE`、`CHENG_BACKEND_SSA`、旧单态化开关（对应旧 MIR/LIR/单态化路径）。
- 新增诊断输出：`uir_profile\t<label>\tstep_ms=...\ttotal_ms=...`、`generics_report\t...`。
- `backend_driver` 入口收敛到 UIR 路径：修改 `/Users/lbcheng/cheng-lang/src/backend/tooling/backend_driver.cheng`。
- 文档接口更新：`/Users/lbcheng/cheng-lang/docs/cheng-formal-spec.md`、`/Users/lbcheng/cheng-lang/docs/cheng-backend-arch.md`、`/Users/lbcheng/cheng-lang/src/tooling/README.md`。

## Implementation Plan
1. 定义 UIR 规范与数据结构：新增 `/Users/lbcheng/cheng-lang/src/backend/uir/uir_types.cheng`，固定值语义、控制流、内存模型、调用约定、泛型表示与验证规则。
2. 实现 UIR 构建器：新增 `/Users/lbcheng/cheng-lang/src/backend/uir/uir_builder.cheng`，从语义后 AST 直接生成 SSA UIR，替代当前 `/Users/lbcheng/cheng-lang/src/backend/mir/mir_builder.cheng` 主职责。
3. 泛型重构到 UIR：新增 `instance key` 与跨模块缓存，默认“字典传递 + 预算内特化”，移除 stage1 前置大规模单态化依赖。
4. 实现统一优化管线：新增 `/Users/lbcheng/cheng-lang/src/backend/uir/uir_opt.cheng`，包含 CSE、GVN、SROA-lite、inlining budget、dead code elimination，并以编译速度为默认调参目标。
5. 目标输出直连 UIR：将 AArch64/x86_64 指令选择改为 `UIR -> machine emit`，逐步删除 `/Users/lbcheng/cheng-lang/src/backend/lir/lir_types.cheng` 与 `/Users/lbcheng/cheng-lang/src/backend/isel/*` 的 LIR 依赖层。
6. 一次替换主入口：在 `/Users/lbcheng/cheng-lang/src/backend/tooling/backend_driver.cheng` 移除 MIR/LIR 主分支，默认只走 UIR；保留最小兼容错误提示而非运行双轨。
7. 清理与收口：移除无主路径引用的 MIR/LIR 开关与脚本分支，同步 profile、门禁脚本与开发文档。

## Test Cases & Acceptance
1. 语义一致性：`examples/stage1_codegen_fullspec.cheng` 与核心后端 fixtures 在新链路输出行为与旧链路一致。
2. 泛型正确性：覆盖隐式/显式泛型、trait 约束、跨模块实例复用、失败诊断定位，确保不出现重复实例爆炸。
3. 编译性能：以当前基线为对照，`build_module` 总时长下降目标 `>=25%`，泛型阶段耗时下降目标 `>=40%`。
4. 产物与运行：关键 smoke 用例运行时性能回退不超过 `5%`，体积增长不超过 `10%`。
5. 确定性：继续通过 determinism 严格门禁（obj/exe 两口径），并新增 UIR 序列化稳定性校验。
6. 平台矩阵：Darwin/Linux/Windows 目标的 obj 产物门禁保持可通过，Darwin `exe+run` 保持闭环。

## Assumptions & Defaults
- 不引入 LLVM 库与 LLVM IR 作为生产依赖，仅借鉴其统一 IR/SSA/优化管线设计。
- 语言语义不变；变更限定在编译器内部表示与管线。
- 因选择“一次替换”，默认允许短期开发窗口冻结高风险合并以降低切换风险。
- 默认泛型策略为 `hybrid` 且偏编译速度：字典优先、预算特化，避免全量单态化导致的编译膨胀。
