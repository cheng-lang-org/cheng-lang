# Cheng：默认无 Raw Pointer 的内存安全 + 高性能方案（ORC + var 借用 + 安全容器 + Typed spawn）

## Status（2026-02-11）

这份文档目前是“目标/设计规格 + 落地清单”，不是对现状的完全描述。对照当前仓库代码：

- Stage1 已接入 ABI 门禁：`CHENG_ABI=v2_noptr` 会强制启用 std no-pointer 扫描（等价于开启 `CHENG_STAGE1_STD_NO_POINTERS=1` 且按 strict 口径禁用豁免）。
- 兼容开关仍保留：`CHENG_STAGE1_STD_NO_POINTERS`/`CHENG_STAGE1_STD_NO_POINTERS_STRICT` 可单独控制策略；`CHENG_ABI=v1` 为历史兼容口径。
- tooling 已提供专项回归：`src/tooling/verify_backend_abi_v2_noptr.sh`（同一 `src/std` 探针在 `v1` 可编、`v2_noptr` 必须编译期拒绝）。
- `src/std/async_rt.cheng` 仍对外暴露 `spawn(fn_ptr: void*, ctx: void*)`，Typed spawn 方案尚未迁移完成。
- backend mvp 前端尚未与 stage1 在 raw pointer 门禁上完全对齐。

建议动作：

- 若目标是“全量 std 零指针”，优先从 `src/std/system.cheng`、`src/std/seqs.cheng`、`src/std/hashmaps.cheng` 的 runtime 接缝改造开始，再收紧豁免集合并保持自举回归。
- 若近期目标是自举/性能优先，可先在 `CHENG_ABI=v1` 口径下验证性能，再并行推进 `v2_noptr` 违规模块治理。

## Summary

- 目标：业务代码（默认安全模块）完全不使用 `void*`/`T*` 与 `&`/`*`/`->`/`ptr_add`... 等指针操作，同时保持接近指针的性能（通过 `var` 借用、`T[]`/`T[N]` 容器、边界检查与后端优化）。
- 手段：把 Raw Pointer 收口到 std 和显式 FFI 模块；编译器默认强制门禁；标准库补齐“指针零暴露”的并发入口（Typed spawn），并提供迁移逃生门。
- 交付：编译器门禁 + `async_rt.spawn` API 迁移 + 回归脚本/fixture 更新 + 负向用例。

## Public API / Interface Changes

### 标准库并发 API（替换现有 Raw Pointer spawn）

- 在 `src/std/async_rt.cheng` 移除对外暴露的 `spawn(fn_ptr: void*, ctx: void*)`。
- 新增对外暴露的安全接口（无 `void*`、无 `&`）：

```text
@thread_boundary fn spawn(entry: fn())
@thread_boundary fn spawn[T](entry: fn(T), ctx: T)
```

- 保留底层运行时符号，但改为内部名（仅 std 内部使用）：

```text
@importc("cheng_spawn") fn __cheng_spawn_raw(fn_ptr: void*, ctx: void*)
```

- 说明：不加 `@thread_boundary`，避免 std 内部实现被 Send/Sync 规则卡死。

### 编译器开关（迁移逃生门）

- 新增环境变量：`CHENG_ALLOW_RAW_PTR=1`，全局关闭 Raw Pointer 门禁（用于迁移与保留的指针回归用例）。

## Safety Policy（“默认安全模块”定义与规则）

### 默认安全模块

- 定义：不在 std/编译器内部目录，且不包含 `@importc` 的模块。

### Raw Pointer 允许范围

- 允许：std（如 `/src/std/`）、编译器内部（如 `/src/stage1/`、`/src/backend/`、`/src/runtime/`）、显式 FFI 模块（文件内出现任意 `@importc`）。
- 禁止：其它所有模块（默认）。
- 逃生门：任意模块在 `CHENG_ALLOW_RAW_PTR=1` 时允许。

### 默认安全模块中的禁止项（必须同时在 stage1 与 backend mvp 前端生效）

- 禁止出现指针类型节点：任何 `T*`/`void*`（AST：`nkPtrTy`）。
- 禁止显式指针操作：
  - `&x`（AST：`nkCall` 且归一化名为 `__addr`）。
  - `*p`（AST：`nkBracketExpr` 且 `kidCount == 1`）。
  - `p->field`（AST：`nkHiddenDeref` / `nkDerefExpr`）。
- 指针视图/算术 API：`dataPtr`/`getPointer` 与所有 `ptr_*`（按 `callName` 归一化匹配）。
- 禁止“指针泄露”：在有类型信息的前端里（backend mvp、stage1 monomorphize 后）若任意表达式推导出的 `typeKey` 以 `ptr_` 开头，则报错（覆盖“从 std 类型字段读出指针”“调用返回指针的函数但未写指针语法”等漏洞）。

## Compiler Implementation（两条前端都做门禁）

### 1) Stage1 前端（语义/单态化管线）

- 文件路径上下文修复（为了按“模块”粒度判定 std/ffi，同时让诊断落到正确文件）：
  - 在 `src/stage1/semantics.cheng` 的 `checkSemantics` / `semAnalyzeStmt` 路径中，在分析每个顶层 stmt 前用 `stmt.strVal` 更新 `ctx->filename`（`tagModuleChildren` 已在 stage1 pipeline 标注每个顶层节点来源文件）。
- 门禁实现位置：
  - 语义前（最早失败）：在 `checkSemantics(...)` 开始分析前，对当前模块 AST 做一次 Raw Pointer 语法级扫描（拦 `nkPtrTy`/`__addr`/`*`/`->`/`ptr_*` 等）。
  - 单态化后（防“指针泄露”）：在 stage1 pipeline 的 `monomorphize(...)` 之后新增一次扫描：对每个表达式/字段访问，若 `n.typeCacheValid && typeKey(n.typeCache)` 为 `ptr_*`，则按当前模块策略报错。
- 逃生门：在门禁入口读取 `CHENG_ALLOW_RAW_PTR`，为真则跳过检查。

### 2) Backend MVP 前端（默认 `.o`/`.exe` 产线）

- 在 `src/backend/mir/mir_builder.cheng` 的 `mirBuildModuleFromFile(...)` 三遍 parse/consume 流程中，对每个 root（每个文件）执行门禁：
  - Parse 后、`mirConsumeTypeDecls` 前：语法级扫描（拒绝 `nkPtrTy`、`__addr`、`*`、`->`、`ptr_*`）。
  - Lower/类型推导阶段：在 `caseInferExprTypeKey` 可用处（例如 `let`/`var`/`init`、dot field、call 返回）对推导出的 `typeKey` 做 `ptr_*` 拦截，拒绝指针泄露。
- 选择“按文件”判定安全/不安全：
  - `filePath` 在 `mirConsumeTopLevelFromRoot(...)` 已作为参数传递；用于判断是否 std/ffi（ffi 通过预扫描 `@importc`）。

## Stdlib Implementation：Typed spawn（用户侧零指针）

### 修改 `src/std/async_rt.cheng`

- 保留底层运行时导入，但更名为内部符号：

```text
@importc("cheng_spawn") fn __cheng_spawn_raw(fn_ptr: void*, ctx: void*): void
```

- 新增 `SpawnBox` 与 trampoline（std 内部可用指针）：

```text
type SpawnBox0 = object entry: fn(): void
type SpawnBox[T] = object entry: fn(T): void; ctx: T

fn __spawn_trampoline0(raw: void*) =：cast -> 取出 entry -> dealloc -> entry()
fn __spawn_trampoline[T](raw: void*) =：cast -> 取出 entry/ctx -> dealloc -> entry(ctx)
```

- 分配必须 `zeroMem` 后再写字段，避免 ORC/赋值路径对“未初始化旧值”做 release（实现细节在 std 内部，允许用 `void*`/`T*`）。
- 实现对外 `spawn`：
  - `@thread_boundary fn spawn(entry: fn(): void): void`：分配 `SpawnBox0`，失败则直接调用 `entry()`，成功则 `__cheng_spawn_raw(&__spawn_trampoline0, raw)`。
  - `@thread_boundary fn spawn[T](entry: fn(T): void, ctx: T): void`：同理，失败则直接 `entry(ctx)`，成功则 `__cheng_spawn_raw(&__spawn_trampoline[T], raw)`。

### 兼容性说明

- 这是破坏性变更：旧代码 `fn producer(ctx: void*)` 与 `spawn(&producer, nil)` 必须迁移为 `fn producer()` + `spawn(producer)` 或 `spawn[T](producer, ctx)`。

## Tests / Tooling Updates

### 更新并发 fixture 为无指针版本

- `tests/cheng/backend/fixtures/return_spawn_chan_i32.cheng`：
  - `fn producer(ctx: void*) =` 改为 `fn producer() =`
  - `spawn(&producer, nil)` 改为 `spawn(producer)`

### 保留“指针回归” fixture，但在对应 verify 脚本为它们开启逃生门

- `src/tooling/verify_backend_opt2.sh`：编译 `return_opt2_sroa_deref.cheng` 时加 `CHENG_ALLOW_RAW_PTR=1`。
- `src/tooling/verify_backend_android.sh`：编译 `return_store_deref.cheng` 时加 `CHENG_ALLOW_RAW_PTR=1`。

### 新增负向验证（确保门禁真的生效）

- 新增 fixture：`compile_fail_raw_ptr_addr.cheng`（包含 `&x` 或 `int32*`）。
- 新增脚本：`verify_raw_ptr_gate.sh`，用 backend driver 编译该 fixture，断言失败且 stderr 含固定片段（例如 `raw pointer is disabled`）。

## Documentation Updates

### 更新 `docs/cheng-formal-spec.md`

- 新增“Raw Pointer Gate（默认安全模块）”小节，明确：
  - 默认禁用 `void*`/`T*` 与 `&`/`*`/`->`/`ptr_*`
  - 允许范围：std + 显式 `@importc` 模块 + `CHENG_ALLOW_RAW_PTR=1`
  - 推荐替代：`var` 可变借用、`T[]`/`T[N]` 容器、Typed spawn

## Acceptance Criteria

### 默认（不设 `CHENG_ALLOW_RAW_PTR`）

- `tests/cheng/backend/fixtures/return_spawn_chan_i32.cheng` 在所有包含它的 verify 脚本中能编译并运行通过。
- `compile_fail_raw_ptr_addr.cheng` 编译失败，且诊断指向正确文件/行列。

### 设 `CHENG_ALLOW_RAW_PTR=1`

- `return_opt2_sroa_deref.cheng`、`return_store_deref.cheng` 等指针回归 fixture 仍能编译运行（由脚本按需开启）。

### 行为一致性

- backend 默认 mvp 与 stage1 前端在“默认安全模块禁指针”规则上行为一致（至少语法级禁用一致，且能阻止指针泄露）。

## Assumptions / Defaults

- 编译器内部源码（`/src/stage1/`、`/src/backend/` 等）视为“受信任代码”，不纳入默认安全模块门禁范围。
- 现阶段 Send/Sync 规则保持既有实现；本计划优先实现“用户表面零指针 + 默认禁用”，Send/Sync 推导增强作为后续可选扩展。
