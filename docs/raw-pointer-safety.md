# Cheng：默认无 Raw Pointer 的内存安全 + 高性能方案（ORC + var 借用 + 安全容器 + Typed spawn）

## Status（2026-02-12）

这份文档目前是“目标/设计规格 + 落地清单”，不是对现状的完全描述。对照当前仓库代码：

- Stage1 已接入 ABI 门禁：`CHENG_ABI=v2_noptr` 默认启用 std no-pointer 扫描；可用 `CHENG_STAGE1_STD_NO_POINTERS=0` 显式切到兼容口径（常用于闭环中的非专项 gate）。
- 新增非 C ABI 门禁开关：`CHENG_STAGE1_NO_POINTERS_NON_C_ABI=1` 时，非 C ABI 对接模块会禁用指针类型与指针操作（`*`/`&`/deref/`ptr_*` 等）。
- `CHENG_STAGE1_NO_POINTERS_NON_C_ABI_INTERNAL` 默认按开启处理（仅显式设为 `0` 时放宽），因此编译器内部实现（`src/stage1`/`src/backend`/`src/tooling`）默认也纳入同一门禁。
- 兼容开关仍保留：`CHENG_STAGE1_STD_NO_POINTERS`/`CHENG_STAGE1_STD_NO_POINTERS_STRICT` 可单独控制 std 策略；`CHENG_ABI=v1` 仅保留在兼容/对照脚本中。
- tooling 已提供专项回归：`src/tooling/verify_backend_abi_v2_noptr.sh`（`CHENG_BACKEND_ABI_V2_NOPTR_ONLY=1` 时走 only-v2 口径；non-C-ABI 子门禁会显式设 `CHENG_STAGE1_STD_NO_POINTERS=0` 以隔离 non-C-ABI 诊断）。
- 默认并发接口已移除 raw spawn：`src/std/async_rt.cheng` 只保留 typed spawn，raw 入口仅在 `src/std/async_rt_legacy.cheng` 显式提供。
- 当前 stage1+backend 口径下，`fn()` 入口调用已稳定为 `spawn(entry)`。
- backend mvp 前端尚未与 stage1 在 raw pointer 门禁上完全对齐。

建议动作：

- 若目标是“全量 std 零指针”，优先从 `src/std/system.cheng`、`src/std/seqs.cheng`、`src/std/hashmaps.cheng` 的 runtime 接缝改造开始，再收紧豁免集合并保持自举回归。
- 当前生产链路（`src/tooling/backend_prod_closure.sh`）仅接受 `CHENG_ABI=v2_noptr`；`v1` 不再作为生产闭环默认路径。

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

 Cheng 绝对零指针重构计划（ABI 断兼容版）


  ### Summary


  - 目标：实现“全链路源码与 IR 不出现指针类型/指针操作”，包括 std、compiler、backend、runtime。
  - 前提已锁定：接受 ABI 断兼容，接受阶段性性能回退。
  - 交付方式：分 4 个里程碑，先重建语义与运行时模型，再恢复性能。


  ### Public API / Interface Changes


  - 新增内建抽象类型：
      - addr（地址值类型，非指针，不支持解引用/算术运算）
      - handle[T]（受管句柄，替代 ref T 与 T* 语义）
      - span[T]（边界化视图，替代裸缓冲区访问）
  - 语言层禁用：
      - T*、void*、ptr_add、load/store、显式解引用语法
  - FFI 改造：
      - 仅允许 extern "c" shim 层使用 C 指针（放在独立 abi_c 子系统）
      - Cheng 主代码通过 addr/handle/span 与 shim 交互
  - 容器 API 统一：
      - T[]、T[N] 保留
      - 内部实现改为句柄化内存池，不暴露地址字段


  ### Implementation Plan


  1. IR 与类型系统重构


  - 在 HIR/MIR/LIR 引入 AddrKind 与 HandleKind，移除 PointerKind。
  - 所有 load/store/gep 风格节点改为：
      - mem_read(handle, index, size)
      - mem_write(handle, index, value)
      - mem_slice(handle, start, len)
  - 代码生成阶段将 addr/handle 映射为目标机地址寄存器与偏移，不在 IR 层暴露指针类型。


  2. Runtime 内存模型替换


  - 新建 runtime/mem_pool：
      - 句柄分配、回收、代际校验、边界检查
      - realloc 改为“新块+复制+句柄重绑定”
  - seq/table/hashmap/json/string 全部改为句柄实现：
      - 结构体中禁止 buffer: void*，改 storage: handle[byte] + metadata。


  3. Std 与编译器源码去指针


  - 扫描并替换 src/std, src/stage1, src/backend 的 *、void*、ptr_*、load/store。
  - 所有泛型容器操作统一走 []/[]= 与 .len/.cap。
  - system.cheng 仅保留非指针抽象 API；C 指针入口全部下沉到 abi_c shim。


  4. ABI 与工具链切换


  - 新建 CHENG_ABI=v2_noptr。
  - backend_driver 增加 ABI 版本门控：v1（兼容）/v2（零指针）。
  - 生产闭环默认并强制 v2；v1 仅保留兼容对照门禁。


  ### Test Cases & Acceptance


  - 编译期约束
      - 全仓 rg '\*|void\*|ptr_add|load_ptr|store_ptr' 在允许目录外必须为 0。
      - 禁止在 src/std、src/stage1、src/backend 出现指针 AST 节点。
  - 功能回归
      - verify_backend_selfhost_bootstrap_self_obj.sh（v2）全绿。
      - verify_backend_obj.sh、verify_backend_multi.sh、verify_backend_opt2.sh 全绿。
      - 容器专项：seq/table/hashmap/json/string 全套读写、扩容、边界检查。
  - 性能门槛（阶段性）
      - M1/M2 允许退化；M3 开始建立基线；M4 回到现有 1.5x 以内。
  - 自举闭环
      - stage0 -> stage1 -> stage2 在 v2 ABI 下可重复构建且产物一致性通过。


  ### Rollout


  - Phase A（2-3 周）：IR 类型重构 + runtime 句柄池最小可用。
  - Phase B（2-4 周）：std 容器与 system 去指针，v2 编译通过。
  - Phase C（2-3 周）：backend/codegen 全链路、selfhost 跑通。
  - Phase D（持续）：性能回补（对象池、批量 copy、逃逸优化、句柄内联缓存）。


  ### Assumptions / Defaults


  - 默认启用 CHENG_ABI=v2_noptr；生产闭环不再接受 v1。
  - 允许 ABI 破坏，不保证旧产物与旧工具互通。
  - 允许前两阶段性能显著下降，以语义闭环优先。
  - C 指针仅存在于 abi_c shim（仓内唯一白名单目录）。
