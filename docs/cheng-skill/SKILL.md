---
name: cheng语言
description: Cheng 语言语法与语义、所有权/ORC、并发与模块导入的编程指南。用于解释或生成 Cheng 代码、排查语法/语义问题、核对正式规范并给出最小示例。
---

# Cheng 编程（稳定版）

## 维护元数据
- `last_verified_date`: `2026-02-12`
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
- 编译器行为：`stage1` 与后端 `mvp` 路径都会对 `string` 类型名报错（提示改用 `str`/`cstring`）。
- 顶层例程统一使用 `fn` / `iterator`；`method/proc/converter` 已移除。
- 导入仅允许归一化模块路径（如 `std/os`、`cheng/<pkg>/...`）；禁止字符串/相对/绝对路径导入、`from import`、`import A, B`。
- 导入支持前缀合并：`import cheng/libp2p/[crypto,transport]`；分组不支持 `as`。
- 容器类型语法：动态序列 `T[]`，定长数组 `T[N]`；旧 `seq[T]`/`array[T,N]` 已移除（会报错）。
- 序列字面量：仅 `[]` / `[a, b, c]`；重置序列用 `xs = []`；`@[]` 已移除（会报错）。
- 序列初始化：标准库不再提供 `newSeq/newSeqWithCap` 作为初始化入口；使用带类型标注省略初始化（零值）+ `reserve/setLen` 等 API。
- 隐式泛型（语法糖）：例程声明可省略 `[T]`，由签名中的单字母大写类型名（`T/U/K/V`）自动引入类型参数；仍为编译期单态化静态分发。调用侧优先写 `f(x)`，推断失败再显式写 `f[T](x)`。
- 列表生成式：`[expr for pat in iter if cond]`（`if` 可选，编译期 lowering 为循环追加）。
- 计数型迭代规范：源码中形如 `var i = start; while i < end: ...; i = i + 1`、`while i <= end`、`while i > end`、`while i >= end` 的自增/自减计数循环，应统一改写为 `for in` 迭代（含 guard-for 等价写法）；`while` 保留给非计数型条件循环。
- `for ... in ...` 的 `in` 表达式支持：range 字面值（`a..<b` / `a..b`）、数组/Table/HashMap 的字面值、常量与变量；Table/HashMap 支持 `for k, v in tableOrMap` 键值迭代。
- 带类型标注可省略初始化并走隐式默认值；`default[T]` 仅用于表达式/返回/实参位置。
- 隐式默认值速记：`bool=false`，整数/枚举=0，浮点=0.0，`char='\0'`，`str/cstring=""`，指针/`ref`/`var`/`void*`=`nil`，复合类型为 zero-init。
- 符号重载分发是**编译期静态分发**：按静态类型/泛型实例化选定目标；不做运行时动态分派。
- 下标赋值优先级：`a[b] = v` 优先匹配 ``[]=``；读取 `a[b]` 走 ``[]``（或内建容器 lowering）。
- 当前常用容器的 `[]/[]=`（`str`、`T[]`、`T[N]`、`HashMap`、`JsonNode`、`Table` 等）要求编译期可确定分派目标（静态分发，且带边界检查）。

## 后端生产链路（2026-02）
- 生产闭环入口 `src/tooling/backend_prod_closure.sh` 仅接受 `CHENG_ABI=v2_noptr`。
- 主闭环默认使用兼容口径（`CHENG_STAGE1_STD_NO_POINTERS=0`）；严格 no-pointer 由 `backend.abi_v2_noptr` 专项门禁覆盖。
- `verify_backend_abi_v2_noptr.sh` 支持 `CHENG_BACKEND_ABI_V2_NOPTR_ONLY=1`（only-v2）；其 non-C-ABI 子门禁会显式设 `CHENG_STAGE1_STD_NO_POINTERS=0` 以隔离诊断。
- `verify_backend_closedloop.sh` 默认执行 `backend.spawn_api_gate`（v2 友好 fixture，默认 API 禁 raw spawn、legacy 显式入口可用）。
- `CHENG_BACKEND_DRIVER` 未显式设置时，`backend_prod_closure.sh` 优先复用 `artifacts/backend_selfhost_self_obj/cheng.stage2`（其次 `cheng.stage1`、`artifacts/backend_seed/cheng.stage2`），再回落 `backend_driver_path.sh`。

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
- 需要更多上下文时可用 `CHENG_DIAG_CONTEXT=1`。
- 编译性能/自举定位：优先启用 `CHENG_BACKEND_PROFILE=1` / `CHENG_STAGE1_PROFILE=1` / `CHENG_MIR_PROFILE=1`，必要时配合 `src/tooling/profile_backend_sample.sh` 做采样定位热点。
- `sample` 若 call graph 大量 `???`，优先用 `CHENG_BACKEND_LINKER_SYMTAB=all`（或 `--linker-symtab=all`）重建目标二进制再采样。
- 采样编译器自身热点时，先用 `--linker-symtab=all` 重建一个带符号表的编译器副本（否则闭环默认产物可能只有 `_main` 符号导致 `???`）。

### 破坏性语法升级（Checklist）

当需要从源码层面移除/替换语法（例如 `seq[T]` -> `T[]`、`@[...]` -> `[...]`、引入列表生成式等）时，按以下最小流程推进，避免自举/文档/门禁漂移：

1. 文档先行：先更新 `docs/cheng-formal-spec.md` 与相关设计文档（如 `docs/container-refactor.md`、`docs/list-comprehension.md`），并同步 `docs/cheng-skill/` 与 `$HOME/.codex/skills/cheng语言/`；跑 `sh src/tooling/verify_cheng_skill_consistency.sh`。
2. 实现新语法：parser 支持 + lowering 到内部 canonical（尽量保持后端稳定），必要时加 parse recovery。
3. 禁用旧语法：旧写法一律硬错误并给迁移提示；旧语法只允许作为内部 lowering 目标存在。
4. 自举兼容：若 seed stage0 不支持新语法，在 `scripts/gen_stage0_compat_src.py` 中做最小 rewrite 并生成 `chengcache/stage0_compat/` overlay（仅用于 stage0 编译）；stage1/stage2 与日常编译必须回到新语法源树编译。
5. 回归与 seed：补最小正/反例 tests，并跑 `sh verify.sh`（或最小相关 gate）；需要刷新 seed 时用 `sh src/tooling/bootstrap_pure.sh` 并加 `CHENG_BOOTSTRAP_UPDATE_SEED=1`。

## 包与导入（稳定约束）
- 包根包含 `cheng-package.toml`，推荐 `package_id = "pkg://cheng/<name>"`。
- 导入建议统一 `cheng/<pkg>/<path>`，映射到 `<pkgroot>/src/<path>.cheng`。
- 编译器 `import` 阶段不联网，只读本地缓存；拉取由工具链完成。

## 易变状态（只链接，不内嵌状态）
- 自研后端进展：`docs/cheng-backend-arch.md`
- 生产链路总览：`docs/cheng-build-any-platform.md`
- Linux AArch64 no-libc profile：`src/tooling/verify_backend_nolibc_linux_aarch64.sh`（`CHENG_BACKEND_ELF_PROFILE=nolibc`；Darwin 仅静态验收，Linux aarch64 含运行 smoke）

## 一致性检查
- 本地校验脚本：`src/tooling/verify_cheng_skill_consistency.sh`
- CI 镜像根：`docs/cheng-skill`
- CI 抽样模板：`tests/cheng/skill/hello_cheng_ci_sample.cheng`
- 镜像资源要求：`references/ownership.md` 必须同步存在于 skill 与 CI 镜像。
- 目标：扫描禁用语法、检查权威优先级与资源完整性、抽样编译 `assets/hello-cheng/main.cheng`，并运行后端 smoke 用例 `tests/cheng/backend/fixtures/return_add.cheng`。

## 资源
- `references/grammar.md`
- `references/ownership.md`
- `references/stdlib.md`
- `assets/hello-cheng/main.cheng`
