---
name: cheng语言
description: Cheng 语言语法与语义、所有权/ORC、并发与模块导入的编程指南。用于解释或生成 Cheng 代码、排查语法/语义问题、核对正式规范并给出最小示例。
---

# Cheng 编程（稳定版）

## 维护元数据
- `last_verified_date`: `2026-02-21`
- `last_verified_commit`: `workspace-local`
- `upstream_spec`: `docs/cheng-formal-spec.md`

## 权威优先级（冲突处理）
1. `docs/cheng-formal-spec.md`（语言语法/语义唯一权威）
2. 本 `SKILL.md` 的“关键语法约束”与“任务流”
3. `references/*.md`（速查，不是权威）
4. `AGENTS.md`（工程流程/协作规范，不定义语言语义）

> 结论：若出现冲突，一律以 `cheng-formal-spec.md` 为准。

## 使用准则
- 使用本技能回答 Cheng 语言语法、语义、所有权/ORC、并发与模块导入问题。
- 优先给最小可编译示例；示例必须避免旧语法。
- 如引用旧写法，必须显式标注“已移除/不再支持”。

## 关键语法约束（稳定）
- 逻辑运算：`&&` / `||` / `!`；按位异或：`^`。
- 整除/取模：`/` 与 `%`；`div/mod` 已移除。
- 字符串拼接：`+`；`concat` 已移除。
- 单参数调用：`f x` 或 `f(x)`；禁止 `f (x)`。
- 类型转换：仅 `TypeExpr(expr)`；禁止 `TypeExpr expr`、`TypeExpr (expr)`、`TypeExpr()`。
- `cast[T](x)` 与 `(T)(x)` 已移除。
- `nil` 为关键字空值。
- 内建字符串类型仅 `str` 与 `cstring`；`string` 不属于内建类型名，生产代码不应使用。
- 编译器行为：后端前端统一按 `stage1` 口径处理（旧前端别名已移除），并会对 `string` 类型名报错（提示改用 `str`/`cstring`）。
- 顶层例程统一使用 `fn` / `iterator`；`method/proc/converter` 已移除。
- 导入仅允许归一化模块路径（如 `std/os`、`libp2p/...`）；禁止字符串/相对/绝对路径导入、`from import`、`import A, B`。
- 导入推荐写法为 `<pkg>/<path>`；`cheng/<pkg>/<path>` 仅作为兼容别名。
- 导入支持前缀合并：`import libp2p/[crypto,transport]`；分组不支持 `as`。
- 容器类型语法：动态序列 `T[]`，定长数组 `T[N]`；旧 `seq[T]`/`array[T,N]` 已移除（会报错）。
- 序列字面量：仅 `[]` / `[a, b, c]`；重置序列用 `xs = []`；`@[]` 已移除（会报错）。
- 序列初始化：标准库不再提供 `newSeq/newSeqWithCap` 作为初始化入口；使用带类型标注省略初始化（零值）+ `reserve/setLen` 等 API。
- `std/cmdline` 已升级为统一命令行解析入口：支持 `programName/argCount/argStr/findFlag/hasFlag/readFlagValue/readFlagValueAt2/parseBool/parseInt32`，并兼容 `--k:v`、`--k=v`、`--k v` 三种取值格式。
- no-pointer 生产口径（`ABI=v2_noptr` + `STAGE1_NO_POINTERS_NON_C_ABI=1`）：非 C ABI 模块默认禁指针（C ABI bridge 按策略豁免）。
  - 禁用指针类型：`T*`、`void*`、`ref T`、`ptr[T]`。
  - 禁用指针操作：`&`、`*`、`->`、`dataPtr/getPointer`、`ptr_add/load_ptr/store_ptr`、`copyMem/setMem/zeroMem`、`alloc/dealloc`。
  - 违规则报 `no-pointer policy` 诊断。
- Raw Pointer Safety FFI 影子桥接约束：用户层优先 `@ffi_map`（`slice -> ptr/len`）、`@ffi_out_ptrs`（`out-ptr -> tuple`）、`@ffi_handle`（`void* <-> handle`）与 `@importc + var T`（borrow struct bridge），不再推荐显式暴露 `ptr + len/out-ptr/void*`。
- 隐式泛型（语法糖）：例程声明可省略 `[T]`，由签名中的单字母大写类型名（`T/U/K/V`）自动引入类型参数；仍为编译期单态化静态分发。调用侧优先写 `f(x)`，推断失败再显式写 `f[T](x)`。
- 列表生成式：`[expr for pat in iter if cond]`（`if` 可选，编译期 lowering 为循环追加）。
- 计数型迭代规范：源码中形如 `var i = start; while i < end: ...; i = i + 1`、`while i <= end`、`while i > end`、`while i >= end` 的自增/自减计数循环，建议优先改写为 `for in` 迭代（含 guard-for 等价写法）；若存在多条件判断或改写后明显更复杂，可保留 `while`。
- 规范建议（SHOULD）：凡可归约为单调索引计数（循环变量仅做 `i = i + 1` / `i = i - 1`）的循环，建议优先使用 `for ... in ...`；若 `while` 含多条件判断或改写后可读性变差，可继续使用 `while`。
- `for ... in ...` 的 `in` 表达式支持：range 字面值（`a..<b` / `a..b`）、数组/Table/HashMap 的字面值、常量与变量；Table/HashMap 支持 `for k, v in tableOrMap` 键值迭代。
- 带类型标注可省略初始化并走隐式默认值；`default[T]` 仅用于表达式/返回/实参位置。
- 隐式默认值速记：`bool=false`，整数/枚举=0，浮点=0.0，`char='\0'`，`str/cstring=""`，指针/`ref`/`var`/`void*`=`nil`，复合类型为 zero-init。
- 符号重载分发是**编译期静态分发**：按静态类型/泛型实例化选定目标；不做运行时动态分派。
- 下标赋值优先级：`a[b] = v` 优先匹配 ``[]=``；读取 `a[b]` 走 ``[]``（或内建容器 lowering）。
- 当前常用容器的 `[]/[]=`（`str`、`T[]`、`T[N]`、`HashMap`、`JsonNode`、`Table` 等）要求编译期可确定分派目标（静态分发，且带边界检查）。

## 后端生产链路（2026-02）
- 双轨默认策略：
  - Dev 轨（默认）：`BACKEND_BUILD_TRACK=dev`，优先 `BACKEND_LINKER=self`（目标支持时）。
  - Release 轨：`chengc --release`（等价 `BACKEND_BUILD_TRACK=release`）默认 `BACKEND_LINKER=system`，并固定 `BACKEND_NO_RUNTIME_C=0`。
  - 运行模式：`chengc --run` 默认 `host runner`（可显式 `--run:host`），`--run:file` 保留兼容路径。
  - linker 优先级：`--linker` > `BACKEND_LINKER` > `BACKEND_BUILD_TRACK` 默认策略。
  - system linker 优先级变量：`BACKEND_SYSTEM_LINKER_PRIORITY`（默认 `mold,lld,default`）。
  - Dev 热更默认：`BACKEND_HOTPATCH_MODE=trampoline`、`BACKEND_HOSTRUNNER_POOL_MB=512`、`BACKEND_HOSTRUNNER_PAGE_POLICY=rw_rx`、`BACKEND_HOTPATCH_LAYOUT_HASH_MODE=full_program`、`BACKEND_HOTPATCH_ON_LAYOUT_CHANGE=restart`。
  - `emit=obj` 为 internal gate 兼容入口，需显式 `BACKEND_INTERNAL_ALLOW_EMIT_OBJ=1`；公共入口 `chengc.sh` 固定拒绝 obj 输出。
- 生产闭环入口 `src/tooling/backend_prod_closure.sh` 仅接受 `ABI=v2_noptr`。
- 主闭环默认 no-pointer 兼容口径（`STAGE1_STD_NO_POINTERS=1`、`STAGE1_STD_NO_POINTERS_STRICT=0`、`STAGE1_NO_POINTERS_NON_C_ABI=1`、`STAGE1_NO_POINTERS_NON_C_ABI_INTERNAL=1`），strict `std` 门禁由 `verify_backend_abi_v2_noptr.sh` 专项覆盖。
- `verify_backend_abi_v2_noptr.sh` 固定仅校验 `v2_noptr`；其 non-C-ABI 子门禁会显式设 `STAGE1_STD_NO_POINTERS=0` 以隔离诊断，且默认 `BACKEND_ABI_V2_NOPTR_NON_C_ABI_STRICT=1`（阻断）。
- `verify_backend_closedloop.sh` 默认执行 `backend.spawn_api_gate`（v2 友好 fixture，默认 API 禁 raw spawn、legacy 显式入口可用）。
- `BACKEND_DRIVER` 未显式设置时，`backend_prod_closure.sh` 优先复用 `artifacts/backend_selfhost_self_obj/cheng.stage2`（其次 `cheng.stage1`、`dist/releases/current/cheng`、`artifacts/backend_seed/cheng.stage2`），再回落 `backend_driver_path.sh`。
- `backend_prod_closure.sh` 的 stage0 探针与 selfhost 口径对齐（`STAGE1_NO_POINTERS_NON_C_ABI=0`、`STAGE1_NO_POINTERS_NON_C_ABI_INTERNAL=0`、`STAGE1_SKIP_SEM=1`、`GENERIC_MODE=dict`、`GENERIC_SPEC_BUDGET=0`、`STAGE1_SKIP_OWNERSHIP=1`），避免误选不稳定 stage0。
- `backend_prod_closure.sh` 在 `STAGE1_NO_POINTERS_NON_C_ABI=1` 下默认不把主门禁切到 selfhost driver（保持 stable driver）；可用 `BACKEND_MAIN_ALLOW_SELFHOST_DRIVER=1` 强制切换。
- `backend_prod_closure.sh` 的 selfhost 自举步骤默认会显式设置 `STAGE1_NO_POINTERS_NON_C_ABI=0` 与 `STAGE1_NO_POINTERS_NON_C_ABI_INTERNAL=0`；non-C-ABI no-pointer 收敛由后续 `backend.closedloop`/`backend.abi_v2_noptr` 门禁负责。
- `backend.abi_v2_noptr` 在 `backend_prod_closure.sh` 中默认优先使用本地 `artifacts/backend_driver/cheng`（要求具备 non-C-ABI no-pointer 诊断字符串）；其次当前 `BACKEND_DRIVER`，再到 selfhost `cheng.stage2/stage1`；可用 `BACKEND_ABI_V2_DRIVER` 显式覆盖。
- 非 C ABI no-pointer 收口建议显式跑：`sh src/tooling/verify_backend_abi_v2_noptr.sh`。
- 优化语义边界：
  - `DOD/SoA`、`Memory-Exe/Hotpatch`、`E-Graph` 以 `Low-UIR` 为主战场。
  - `Ownership/No-Alias` 必须先在 `High-UIR(MIR语义)` 证明，再下沉到 `Low-UIR` 供 noalias/egraph pass 消费（proof-backed）。

## 任务流（修正）

### 解释语法或语义
- 先查 `cheng-formal-spec.md` 对应章节，再用 `references/grammar.md` 做摘要。
- 答复中优先给“当前支持写法 + 一条反例（旧写法）”。

### 生成 Cheng 代码
- 先按 `cheng-formal-spec.md` 约束生成；`references/grammar.md` 仅作为速查。
- 默认使用 `let`；确有可变语义再用 `var`。
- 类型不明确时显式标注（如 `int32`、`str`、`int32[]`/`T[]`、`T[N]`）。

### 排查编译或运行错误
- 先最小化复现，再对照 `references/ownership.md` 与正式规范。
- 导入错误优先检查是否用了字符串/相对路径导入。
- 需要更多上下文时可用 `DIAG_CONTEXT=1`。
- 编译性能/自举定位：优先启用 `BACKEND_PROFILE=1` / `STAGE1_PROFILE=1` / `UIR_PROFILE=1`，必要时配合 `src/tooling/profile_backend_sample.sh` 做采样定位热点。
- `sample` 若 call graph 大量 `???`，优先用 `BACKEND_LINKER_SYMTAB=all`（或 `--linker-symtab=all`）重建目标二进制再采样。
- 采样编译器自身热点时，先用 `--linker-symtab=all` 重建一个带符号表的编译器副本（否则闭环默认产物可能只有 `_main` 符号导致 `???`）。

### 破坏性语法升级（Checklist）

当需要从源码层面移除/替换语法（例如 `seq[T]` -> `T[]`、`@[...]` -> `[...]`、引入列表生成式等）时，按以下最小流程推进，避免自举/文档/门禁漂移：

1. 文档先行：先更新 `docs/cheng-formal-spec.md` 与相关设计文档（如 `docs/container-refactor.md`、`docs/list-comprehension.md`），并同步 `docs/cheng-skill/` 与 `$HOME/.codex/skills/cheng语言/`；跑 `sh src/tooling/verify_cheng_skill_consistency.sh`。
2. 实现新语法：parser 支持 + lowering 到内部 canonical（尽量保持后端稳定），必要时加 parse recovery。
3. 禁用旧语法：旧写法一律硬错误并给迁移提示；旧语法只允许作为内部 lowering 目标存在。
4. 自举兼容：生产链路不再支持 stage0 overlay；若 seed stage0 不支持新语法，需刷新 stage0/seed，并保证 `backend.stage0_no_compat` 门禁通过。
5. 回归与 seed：补最小正/反例 tests，并跑 `sh verify.sh`（或最小相关 gate）；需要刷新 seed 时用 `sh src/tooling/bootstrap_pure.sh` 并加 `BOOTSTRAP_UPDATE_SEED=1`。

## 包与导入（稳定约束）
- 包根包含 `cheng-package.toml`，推荐 `package_id = "pkg://cheng/<name>"`。
- 导入建议统一 `<pkg>/<path>`（标准库仍为 `std/<path>`）；兼容旧别名 `cheng/<pkg>/<path>`。
- 仓库内源码模块支持从 `<workspace>/src/<module>.cheng` 回退解析（非相对/非绝对路径）。
- 编译器 `import` 阶段不联网，只读本地缓存；拉取由工具链完成。

## 易变状态（只链接，不内嵌状态）
- 自研后端进展：`docs/cheng-backend-arch.md`
- 生产链路总览：`docs/cheng-build-any-platform.md`
- Linux AArch64 no-libc profile：`src/tooling/verify_backend_nolibc_linux_aarch64.sh`（`BACKEND_ELF_PROFILE=nolibc`；Darwin 仅静态验收，Linux aarch64 含运行 smoke）

## 一致性检查
- 本地校验脚本：`src/tooling/verify_cheng_skill_consistency.sh`
- CI 镜像根：`docs/cheng-skill`
- CI 抽样模板：`tests/cheng/skill/hello_cheng_ci_sample.cheng`
- 镜像资源要求：`references/ownership.md` 必须同步存在于 skill 与 CI 镜像。
- 目标：扫描禁用语法、检查权威优先级与资源完整性、抽样编译 `assets/hello-cheng/main.cheng`，并运行后端 smoke 用例（优先 `tests/cheng/backend/fixtures/return_add.cheng`，缺失时回退 `tests/cheng/backend/fixtures/return_i64.cheng`）。

## 资源
- `references/grammar.md`
- `references/ownership.md`
- `references/stdlib.md`
- `assets/hello-cheng/main.cheng`
