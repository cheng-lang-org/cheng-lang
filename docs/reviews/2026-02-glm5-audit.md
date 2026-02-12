# Cheng 设计审查客观核查与纠偏基线（2026-02）

更新时间：2026-02-12

## 1. 采信规则

- 规范优先：`docs/cheng-formal-spec.md`、`docs/manual.md`。
- 实现优先：当前源码（`src/`）高于二手描述。
- 结论标签：
  - `成立`：现状与问题描述一致。
  - `部分成立`：问题存在，但描述有偏差或已部分修复。
  - `不成立`：与规范/源码相反。

## 2. 核查矩阵（GLM5 报告逐条纠偏）

| 主题 | GLM5 观点 | 仓库证据 | 结论 | 处理 |
|---|---|---|---|---|
| Raw pointer / spawn | `std/async_rt` 暴露 `spawn(void*,void*)` | `src/std/async_rt.cheng:56`、`src/std/async_rt.cheng:68`、`src/std/async_rt.cheng:27`、`src/std/async_rt_legacy.cheng:4` | 部分成立（历史问题成立，当前分支已迁移） | 主接口已切到 typed spawn；raw 入口仅保留为内部符号与 legacy 模块。 |
| ABI 双轨 | `CHENG_ABI=v2_noptr` 与 `v1` 双轨增加复杂度 | `docs/raw-pointer-safety.md:7`、`docs/raw-pointer-safety.md:8` | 成立 | 继续按“两阶段窗口”推进，避免长期双轨。 |
| MIR 标量类型 | MIR 仅有整型，缺少 `f32/f64` | `src/backend/mir/mir_types.cheng:9`、`src/backend/mir/mir_types.cheng:252`、`src/backend/mir/mir_types.cheng:258` | 部分成立（历史成立，当前已补齐 float kind） | 已将 `f32/f64` 升级为显式 MIR kind。 |
| MIR 无符号类型 | 缺少 `u8/u16/u32/u64` | `src/backend/mir/mir_types.cheng:13`、`src/backend/mir/mir_types.cheng:222`、`src/backend/mir/mir_types.cheng:270` | 不成立 | 保持 `kind + isUnsigned` 设计，不新增独立 `u* kind`。 |
| MIR 聚合表示 | 缺少聚合类型 | `src/backend/mir/mir_types.cheng:97`、`src/backend/mir/mir_types.cheng:102`、`src/backend/mir/mir_types.cheng:111` | 不成立 | 维持 `MirObjType + 指针语义` 的现有方案。 |
| `T[N]` 语义 | 容量提示语义可能混淆 | `docs/manual.md:609`、`docs/manual.md:614` | 成立（语义明确但确有认知成本） | 本轮不改语义；若要真定长数组，走独立 RFC。 |
| Arc/share_mt 文档 | 原子 RC 与 fence 未明确 | `docs/manual.md:29`、`docs/manual.md:103`、`docs/manual.md:107` | 不成立 | 规范已明确默认原子 RC、release/acquire fence 语义。 |
| 并发 channel 泛型化 | 仅有 `chanI32`，缺少泛型 channel | `src/std/async_rt.cheng:107`、`src/std/async_rt.cheng:110`、`src/std/async_rt.cheng:113` | 成立 | 进入 P1：`Chan[T]` 设计与最小实现。 |
| FFI varargs 与 COFF | varargs 浮点未覆盖；COFF x64 无 shadow space | `docs/cheng-backend-arch.md:188`、`docs/cheng-backend-arch.md:338` | 成立 | 列入后续 ABI 收敛路线（非本轮 P0）。 |
| 自举性能口径 | 文档同时出现“70s 风险”与“24-27s 实测” | `docs/cheng-backend-arch.md:425`、`docs/cheng-backend-arch.md:455` | 成立（口径冲突） | P1 统一统计口径与采样条件。 |

## 3. P0 冻结接口（本轮）

- 并发 API：
  - `@thread_boundary fn spawn(entry: fn())`
  - `@thread_boundary fn spawn[T](entry: fn(T), ctx: T)`
  - 旧 `spawn(void*,void*)` 迁到 `std/async_rt_legacy`。
- MIR 类型：
  - 保持整数 `kind + isUnsigned`。
  - 新增 `mtF32`、`mtF64`。
  - 聚合继续走 `MirObjType`，本轮不引入“按值聚合返回”。

## 4. 分阶段实施状态（P0→P2）

- 阶段 A（审查纠偏）：
  - 已完成：本文件建立了“证据路径 + 结论标签 + 路线归档”。
- 阶段 B（P0-1：Typed spawn）：
  - B1 已完成：`std/async_rt` 对外 typed，raw 下沉为 `__cheng_spawn_raw`。
  - B2 已完成：新增 `src/std/async_rt_legacy.cheng` 暴露旧签名。
  - B3 已完成：示例/fixture 已迁移到 `spawn(entry)` 口径并通过回归。
  - B4 已完成：默认禁旧接口与 legacy 显式启用双向门禁已接入并通过（`verify_backend_spawn_api_gate`）。
- 阶段 C（P0-2：MIR 类型系统）：
  - C1/C2 已完成：`MirTypeKind` 与 builder 映射已补 float。
  - C3 已完成：x86_64/aarch64 isel 已补 float kind 路径（保持 bits intrinsic 策略）。
  - C4 进行中：已补齐并跑通 `return_float64_ops`、`return_float32_roundtrip`、`return_float_mixed_int_cast`、`return_float_compare_cast`、`return_float32_arith_chain`；源码已接入 `f64/f32 <-> int` 的 explicit cast helper lowering（含 runtime ABI 同步），但受本地 selfhost 重建阻塞，默认门禁暂维持“可逆 cast 子集”口径，严格语义用例待 driver 重建后转正。
- 阶段 D（P1/P2）：
  - 待执行：`why-not-move`、`Chan[T]`、`--generics-report`、容器泛型统一、错误码/LSP。

## 5. 非目标声明（本轮不做）

- 不引入 cycle collector。
- 不重写 COFF x64 ABI 全量实现。
- 不把 `T[N]` 改成真定长数组。
- 不改变 `isUnsigned` 设计路线。
