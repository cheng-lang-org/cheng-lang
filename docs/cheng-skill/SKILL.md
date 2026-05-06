---
name: cheng语言
description: Cheng 语言语法与语义、所有权/ORC、并发与模块导入的编程指南。用于解释或生成 Cheng 代码、排查语法/语义问题、核对正式规范并给出最小示例。
---

# Cheng 编程（稳定版）

## 维护元数据
- `last_verified_date`: `2026-04-25`
- `last_verified_commit`: `workspace-local`
- `upstream_spec`: `docs/cheng-formal-spec.md`

## 权威优先级
1. `docs/cheng-formal-spec.md`：语言语法/语义唯一权威。
2. 本 `SKILL.md`：稳定公开表面、任务流、排障入口。
3. `references/*.md`：速查材料，不是权威。
4. `AGENTS.md`：工程流程，不定义语言语义。

冲突时一律以 `docs/cheng-formal-spec.md` 为准。

## 使用准则
- 回答 Cheng 语法、语义、所有权/ORC、并发、模块导入、FFI/no-pointer、工具链排障时使用本技能。
- 优先给当前支持写法和最小可编译例子；引用旧写法必须明确标注“已移除/不再支持”。
- Cheng 没有 tracing GC；内存问题按 ORC retain/release、alloc/free/live、borrow 生命周期解释。
- 默认公开表面是 no-pointer + borrow/`Send/Sync` 检查；FFI、handle、环引用仍是显式边界，不能说成全域自动安全。

## 稳定公开表面
- 模块与包：支持 `module`、`import`、`import ... as ...`、前缀分组导入 `import pkg/[a,b]`、包根 `cheng-package.toml`、`<pkg>/<path>` 模块路径、仓内 `src/` 回退解析；导出规则是 Go 风格首字母大写导出。
- 包解析与锁定：支持 `package_id`、依赖 channel、`cheng.lock.toml`、内容寻址包缓存、本地包根搜索与源码直发记录；编译期导入不联网，只消费本地解析结果。
- 顶层声明：支持 `let/var/const`、`type`、`fn`、`iterator`、`macro`、`template`、`concept`、`trait`；旧 `proc/method/converter` 已移除。
- 例程与类型：支持普通函数、`async fn`、迭代器、匿名函数、闭包、函数指针、泛型/`where`、具名参数、默认参数；支持 `bool`、整数、浮点、`char`、`str`、`cstring`、`nil`、`enum`、代数类型/tagged union（如 `type Option[T] = Some(value: T) | None`）、`set[T]`、`tuple[...]`、`object/ref object`、`var T` 借用、`fn(...)`、点限定类型名、`new(Type)`、`T()` 复合零值物化。
- 容器与数据：动态序列 `T[]`，定长数组 `T[N]`，字面量 `[]/[a,b]`，列表生成式 `[expr for pat in iter if cond]`，对象字段默认值，tuple 元素默认值，隐式默认值初始化，Table/HashMap 键值迭代。
- 表达式：成员访问、下标/切片、小括号调用、具名实参、区间 `a..b`/`a..<b`、三目 `?:`、`if/when/case` 表达式、postfix `expr?` 解包、`$x` 字符串化、静态运算符重载、`TypeExpr(expr)` 显式类型转换。
- 控制流：`if/elif/else`、`match` 模式匹配、`while`、`for ... in ...`、`case/of`、`when`、`block`、`break`、`continue`、`return`、`yield`、`defer`；支持单行 `suite ::= statement`。
- 字符串与格式化：内建字符串类型只有 `str/cstring`；支持短字符串 `"..."` 与多行字符串 `"""..."""`；只保留 `Fmt"..."` / `Fmt"""..."""` 插值；动态数组拼接用 `std/strutils.Join(parts, "")`，按行拼接用 `Lines(parts)`；`str` 默认非空值语义，判空用 `len(s)`。
- 所有权与并发：固定 `MM=orc`；支持 ownership 分类、moveHint、Borrowed/Owned/Unmanaged、`share/share_mt`、`Arc[T]`、`Mutex[T]`、`RwLock[T]`、`Atomic[T]`；跨线程边界按 `@thread_boundary + Send/Sync` 检查。
- FFI 与 ABI：支持 `@importc/@exportc`、`@ffi_map`、`@ffi_out_ptrs`、`@ffi_handle`、`@borrows`、`@escapes`；公开生产口径默认 no-pointer，按 `ZRPC` 禁止裸指针表面和指针算术。

## 关键语法约束（稳定）
- 逻辑运算：`&&` / `||` / `!`，其中 `&&` / `||` 按从左到右短路求值；按位异或：`^`。
- 条件分支语法固定为 `if/elif/else`；`else if` 为非法写法（需改为 `elif`）。
- `suite ::= statement` 为稳定语义，因此 `if x < 0: return 0` 这类单行 suite 是合法写法。
- 代数类型使用 `|` 分隔 variant，variant payload 写字段列表；模式匹配写 `match value:` 后缩进 arms，arm 分隔符固定为 `=>`，例如 `Some(x) => return x`、`None => return 0`。
- 条件表达式支持三目：`cond ? thenExpr : elseExpr`（右结合）。注意与 postfix `expr?`（Result/Option 解包）是不同语义。
- 整除/取模：`/` 与 `%`；`div/mod` 已移除。
- 字符串拼接操作符 `+` 已移除；字符串组合使用 `Fmt"..."`、`std/strutils.Join(...)` 或 `Lines(...)`。数值加法仍使用 `+`；`concat` 已移除。
- `Fmt` 是当前稳定公开格式化表面：`Fmt"label={expr}"` 与 `Fmt"""..."""` 用于插值字符串；无分隔动态数组拼接使用 `std/strutils.Join(parts, "")`，按换行拼接使用 `Lines(parts)` 或 `std/strutils.Join(parts, "\n")`。`Fmt(parts)`、`Fmt expr`、`Lines expr` 不再支持，小写 `fmt/lines` 不导出。
- 多行字符串必须写成 `"""` 后换行、结束 `"""` 独占一行；结束符缩进会从每个非空正文行剥离，正文缩进浅于结束符时报错；源码换行统一为 `\n`。
- 单参数调用统一写 `f(x)`；`f x` 属于迁移期旧表面，`CHENG_STRICT_CALL_SYNTAX=1` 下报错；禁止 `f (x)`。
- 类型转换仅 `TypeExpr(expr)`；禁止 `TypeExpr expr`、`TypeExpr (expr)`、`cast[T](x)`、`(T)(x)`。
- 内建字符串类型仅 `str` 与 `cstring`；`string` 不属于内建类型名，生产代码不应使用。
- `str` 采用非空字符串语义；`str = nil`、`x == nil` 在编译期报错，应显式判断 `len(s) == 0`。
- 顶层例程统一使用 `fn` / `iterator`；`method/proc/converter` 已移除。
- 导入仅允许归一化模块路径（如 `std/os`、`libp2p/...`）；禁止字符串/相对/绝对路径导入、`from import`、`import A, B`。
- 导入推荐写法为 `<pkg>/<path>`；`cheng/<pkg>/<path>` 仅作为兼容别名。
- 单模块导入支持 `as`；导入支持前缀合并：`import libp2p/[crypto,transport]`；分组不支持 `as`。
- 导出采用 Go 风格：标识符首字符为 ASCII 大写字母即导出；小写字母或 `_` 开头保持模块私有。
- 容器类型语法：动态序列 `T[]`，定长数组 `T[N]`；旧 `seq[T]`/`array[T,N]` 已移除。
- 序列字面量仅 `[]` / `[a, b, c]`；重置序列用 `xs = []`；`@[]` 已移除。
- 序列初始化使用带类型标注省略初始化（零值）+ `reserve/setLen`；标准库不再提供 `newSeq/newSeqWithCap` 作为公开入口。
- `std/cmdline` 是统一命令行解析入口：支持 `programName/argCount/argStr/findFlag/hasFlag/readFlagValue/readFlagValueAt2/parseBool/parseInt32`，兼容 `--k:v`、`--k=v`、`--k v`。
- `var` 形参/绑定表示可变借用，不是指针语法；借用值禁止 `return/yield`、禁止写入全局、禁止 `share(...)`。
- 隐式泛型：例程声明可省略 `[T]`，由签名中的单字母大写类型名（`T/U/K/V`）自动引入类型参数；仍为编译期单态化静态分发。
- 计数型循环必须写成 `for ... in ...`；可归约为 `i = i + 1` / `i = i - 1` 的 `while` 计数循环必须改写，`while` 只用于非计数型条件循环。
- 符号重载是编译期静态分发；下标赋值 `a[b] = v` 优先匹配 ``[]=``，读取 `a[b]` 走 ``[]``。

## 默认值与复合类型
- 带类型标注可省略初始化并走隐式默认值；`T()` 只用于 `object/tuple/Bytes` 默认值物化，`T[]/T[N]` 改用省略初始化或字面量，简单类型不要写 `int32()`/`bool()`/`str()`。
- `object/ref object/tuple` 字段或元素支持 `name: Type = expr` 默认值语法；初始化优先级固定为“显式实参 > 字段默认值 > 类型零值”。
- 字段默认值只允许稳态表达式：字面量（含 `T[]/T[N]` 上下文下的 `[]` 与 `[a, b, ...]`）、复合类型 `T()`、`new(Type)`、纯类型构造与 `if/?:` 组合；禁止引用同一对象其他字段，禁止副作用调用。
- tuple 这轮只支持 typed implicit init 与 `T()`；tuple 字面量/tuple 类型构造必须显式写全所有元素，不支持省略元素自动补齐。
- 隐式默认值速记：`bool=false`，整数/枚举=0，浮点=0.0，`char='\0'`，`str/cstring=""`，指针/`ref`/`var`/`void*`=`nil`，复合类型先 zero-init，再应用字段默认值。
- 和隐式默认值一致的显式初始化是编译期硬错误：`bool` 不写 `= false`，整数不写 `= 0`，`str` 不写 `= ""`，`T[]/T[N]` 不写 `= []`，带类型标注的复合值不写 `= T()`。
- 业务代码优先把“偏离隐式默认值”的稳定默认值写回复合类型定义，再用 `var x: Foo`、`Foo()`、`Foo(changed: ...)`；不要再维护 `FooZero()` 这类镜像初始化 helper。

```cheng
type
    RunResult =
        exitCode: int32 = -1
        outputText: str
        tags: str[]
        retries: int32[3] = [1, 2, 3]

var a: RunResult
let b = RunResult()
let c = RunResult(outputText: "ok")
```

## no-pointer / ZRPC
- no-pointer 生产口径：当前默认公开编译入口（`cheng`、`release-compile`）下，用户源码模块默认禁指针，`@importc/@exportc` 等 C ABI 声明与普通源码入口同口径 hard-fail；旧 `ABI=v2_noptr` 与 `STAGE1_NO_POINTERS_NON_C_ABI*` 只保留为内部实现/兼容标识。
- 禁用指针类型：`T*`、`void*`、`ref T`、`ptr[T]`。
- 禁用指针操作：`&`、`*`、`->`、`dataPtr/getPointer`、`ptr_add/load_ptr/store_ptr`、`copyMem/setMem/zeroMem`、`alloc/dealloc`。
- 用户层 FFI 优先 `@ffi_map`（`slice -> ptr/len`）、`@ffi_out_ptrs`（`out-ptr -> tuple`）、`@ffi_handle`（`void* <-> handle`）与 `@importc + var T`（borrow struct bridge），不推荐显式暴露 `ptr + len/out-ptr/void*`。
- Raw Pointer Safety 规范名固定为 `ZRPC`（`zero_rawptr_production_closure`），属于发布链路 hard-fail 约束；no-pointer 相关诊断应携带该规范标识。
- ZRPC 生产口径要求编译器内部 AST/UIR/基本块采用 `Arena + SoA` 连续存储，跨节点引用使用 `int32` 索引，不得将对象裸指针暴露为优化/门禁接口中的长期关联键。

## 后端与工具链口径
- 当前活入口只认两个二进制：`artifacts/bootstrap/cheng.stage3` 与 `artifacts/backend_driver/cheng`。
- 正式入口优先用 `artifacts/bootstrap/cheng.stage3` 和 `artifacts/backend_driver/cheng`；外层脚本只视为兼容壳，不再作为主入口。
- 常用命令：
  - `artifacts/bootstrap/cheng.stage3 run-production-regression`
  - `artifacts/bootstrap/cheng.stage3 build-backend-driver`
  - `artifacts/backend_driver/cheng run-host-smokes perf_memory_contract_smoke`
  - `artifacts/backend_driver/cheng run-host-smokes cheng_skill_consistency_smoke`
  - `artifacts/bootstrap/cheng.stage3 run-stage23-libp2p-smokes`
  - `artifacts/bootstrap/cheng.stage3 run-cross-target-smokes`
  - `artifacts/backend_driver/cheng system-link-exec --root:/abs/root --in:/abs/path/file.cheng --emit:exe --target:arm64-apple-darwin --out:/tmp/app`
- `cheng` 固定 dev-only；release 仅允许 `release-compile`。`cheng`/`release-compile` 不接受 `--linker` 或 `BACKEND_LINKER` 覆盖，链接器由命令轨道固定。
- Dev 默认 self-link/direct-exe/host runner hotpatch；Release 默认 system linker/substrate runtime。`100ms` 编译和二进制原地更新只属于 dev host-only dedicated witness。
- `BACKEND_JOBS` 是唯一公开 worker 数控制面；`BACKEND_FN_SCHED=serial` 只保留给内部诊断、perf 对照和低内存 bring-up。
- `run-production-regression` 是当前聚合回归入口；它会串起 driver 自举、stage0/stage3/backend_driver 命令面 smoke、driver build report 合同、function task 合同、BodyIR DoD/SoA/noalias/CFG 合同、perf gate 合同、线程/原子/ORC runtime provider 合同、文档/skill 一致性、显式默认值正反例与防回潮门禁、跨目标 smoke 和 stage2/stage3 libp2p smoke。
- `thread_atomic_orc_runtime_gate_smoke` 是线程/原子/ORC 真实 runtime provider 门禁；它必须同时检查 provider object、非 standalone no-runtime、真实符号引用和运行 marker。
- `perf_memory_contract_smoke` 是当前正式性能/内存门禁；`orc_perf_contract` 看 ORC retain/release 与 alloc/free/live，`*_compile_exec_phase_summary` 看编译 phase 摘要，`*_compile_gap_breakdown` 看 planner 之外的 object materialize/provider cache/native link/line-map 真耗时。
- 内建画像/门禁优先用 `artifacts/bootstrap/cheng.stage3 profile-run/profile-report` 与 `artifacts/backend_driver/cheng run-host-smokes perf_memory_contract_smoke`；不要再把外层脚本当默认路径。
- `perf_memory_contract_smoke` 默认优先测 `artifacts/backend_driver/cheng`；只有显式 `CHENG_SMOKE_COMPILER` 才覆盖。
- Darwin 正式内存比较值优先用 `peak memory footprint`；`maximum resident set size` 只保留原始观测，不作为稳定合同阈值。
- 需要回答“编译理论下界”时，优先看 `perf_memory_contract_smoke` 报告中的 `planner_total_ms`，再看 `*_compile_gap_breakdown`；前提是 `planner_total_ms <= compile_elapsed_ms`。
- `build-backend-driver` 的候选编译必须强制 `BACKEND_INCREMENTAL=0`、`BACKEND_MULTI_MODULE_CACHE=0`、`CHENG_DISABLE_PRIMARY_OBJECT_CACHE=1`，并默认注入 8GiB RSS 守卫与 `CHENG_PROGRESS=1`。
- backend driver 的 `system-link-exec` 不支持或 primary/object/native materializer 未就绪时必须硬失败并写报告，不能默认转发到 stage3 掩盖缺口。
- 纯 Cheng 自举主线不能继续用源码行字符串扫描扩展 statement 支持；下一步必须消费 parser/typed facts/NormalizedExpr 的结构化 statement/CFG/call sequence IR。
- `primary_object_emit` 的 `.s` 文本路径只保留为 fallback/debug 对拍；Darwin arm64 主线优先使用 direct object writer，真正生产缺口必须在 Mach-O/ELF/COFF object writer 或 direct-exe 主线补齐，不能用 `.s` fallback 冒充完成。

## 任务流

### 解释语法或语义
- 先查 `docs/cheng-formal-spec.md` 对应章节，再用 `references/grammar.md` 做摘要。
- 答复优先给当前支持写法；旧写法只作为反例出现。

### 生成 Cheng 代码
- 先按正式规范生成；`references/grammar.md` 仅作速查。
- 默认使用 `let`；确有可变语义再用 `var`。
- 类型不明确时显式标注，如 `int32`、`str`、`int32[]`、`T[]`、`T[N]`。
- 新代码不得使用字符串 `+` 或 callable `Fmt(...)`；字符串组合写 `Fmt"..."`、`std/strutils.Join(...)` 或 `Lines(...)`。

### 排查编译或运行错误
- 先最小化复现，再对照 `references/ownership.md` 与正式规范。
- 导入错误优先检查是否用了字符串/相对/绝对路径导入。
- 需要更多上下文时可用 `DIAG_CONTEXT=1`。
- 编译/语义问题先看 `artifacts/bootstrap/cheng.stage3 debug-report`、`print-asm`、`print-line-map`、`print-symbols`、`print-object`。
- host smoke / ordinary compile 产物先看 `*.compile.log`、`*.run.log`、`*.map`、`*.primary.o.s`。
- 运行崩溃先对 `*.run.log` 跑 `artifacts/bootstrap/cheng.stage3 crash-report --in:<run.log> --out:<report>`。
- 性能先用 `artifacts/bootstrap/cheng.stage3 profile-run/profile-report` 与 `artifacts/backend_driver/cheng run-host-smokes perf_memory_contract_smoke`。
- 只有问题落到宿主 C runtime、系统 linker、`libSystem`，或内建产物不足时，才补用 `lldb/gdb/sample`。
- 编译性能/自举定位优先启用 `BACKEND_PROFILE=1` / `STAGE1_PROFILE=1` / `UIR_PROFILE=1`；必要时配合 `$TOOLING profile_backend_sample`。
- `sample` 若 call graph 大量 `???`，先用 `BACKEND_LINKER_SYMTAB=all` 或 `--linker-symtab=all` 重建带符号表的目标。

### 破坏性语法升级
1. 文档先行：先更新 `docs/cheng-formal-spec.md` 与相关设计文档，并同步 `docs/cheng-skill/` 与 `$HOME/.codex/skills/cheng语言/`；跑 `artifacts/backend_driver/cheng run-host-smokes cheng_skill_consistency_smoke`。
2. 实现新语法：parser 支持 + lowering 到内部 canonical，必要时加 parse recovery。
3. 禁用旧语法：旧写法一律硬错误并给迁移提示；旧语法只允许作为内部 lowering 目标存在。
4. 自举兼容：生产链路不再支持 stage0 overlay；若 seed stage0 不支持新语法，需刷新 stage0/seed，并保证 `backend.stage0_no_compat` 门禁通过。
5. 回归与 seed：补最小正/反例 tests，并跑最小相关 gate；需要刷新 seed 时用 `$TOOLING bootstrap_pure` 并加 `BOOTSTRAP_UPDATE_SEED=1`。

## 包与导入
- 包根包含 `cheng-package.toml`，推荐 `package_id = "pkg://cheng/<name>"`。
- 导入建议统一 `<pkg>/<path>`（标准库仍为 `std/<path>`）；`cheng/<pkg>/<path>` 仅作兼容别名。
- 仓库内源码模块支持从 workspace 的 `src/<module>.cheng` 回退解析。
- 编译期导入不联网；包拉取由工具链完成。

## 一致性检查
- 本地校验命令：`artifacts/backend_driver/cheng run-host-smokes cheng_skill_consistency_smoke`
- CI 镜像根：`docs/cheng-skill`
- CI 抽样模板：`tests/cheng/skill/hello_cheng_ci_sample.cheng`
- 镜像资源要求：`references/ownership.md` 必须同步存在于 skill 与 CI 镜像。
- 目标：扫描禁用语法、检查权威优先级与资源完整性、抽样编译 `assets/hello-cheng/main.cheng`，并运行后端 smoke 用例。

## 资源
- `references/grammar.md`
- `references/ownership.md`
- `references/stdlib.md`
- `assets/hello-cheng/main.cheng`
