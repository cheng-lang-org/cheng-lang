# Cheng 正式规范（语法与语义）

> 版本：2026-02-23

本文为 Cheng 语言最终规范，描述语法与核心语义。

---

## 0. 语义与内存管理

### 0.1 ORC/Ownership 闭环规则

- 内存模型开关：`MM=orc`（固定）；ORC **默认严格模式**（只读 ownership 标注，不回退启发式），可用 `MM_STRICT=0` 关闭或 `MM_STRICT=1` 强制开启。
- Ownership v1 输出：
  - `exprClass`：`Owned`（新值/构造/非借用调用）、`Borrowed`（ident/field/index/借用视图调用）、`Unmanaged`（数值/布尔/空值等）。
  - `moveHint`：`MoveFromIdent(name)`（同一语句列表内 last-use move；**同一作用域存在任意 `defer` 引用该 ident 时不标记 move**；pattern/复杂 LHS 走保守路径；**若 RHS 标识符在外层语句列表后续仍使用（如 if/loop 之后）则不标记 move**；循环体内仅对非 loop‑carried 标识符启用 move，loop‑carried 包括循环条件/迭代表达式使用的标识符与迭代内“先用后定义”的标识符）。
  - `escapeClass`：`NoEscape` / `ReturnEscape` / `GlobalEscape`。
  - `mustRetain`：保守为 true（Borrowed 或明确需要持有）。
- 借用/视图白名单集中在 ownership：`get/getPtr/getPointer/TableGet*/SeqGet*/StringView/dataPtr/&` 等返回非拥有视图的 API 统一标注为 Borrowed。
- ORC 插桩规则（受管类型域内）：
  - **assign/overwrite**：对 Borrowed 或 `mustRetain` 的 RHS 先 retain，再 store，最后 release(old LHS)；若后端启用 moveHint，可在 last-use 场景跳过 retain，并在 cleanup 中跳过 moved-from 的 release。
  - **alias 覆盖**：当 RHS 与现有引用别名相同（如 `b = a; a = ...`），retain 优先于 release，确保旧值仍被其他别名持有，不发生提前释放。
  - **容器存入**：写入全局或长生命周期容器时必须保证容器持有引用（优先由容器 API 内部 retain；调用侧仍可显式 `share/retain`）。当前标准库字符串容器写入（`TablePut[str]` / `addPtr_str` / `setStringAt` / `insert` / `delete`）已在容器实现内完成 retain/release 闭环，不再依赖 codegen 的 call-site 特判。
  - **return**：返回本地 ident 走 move（跳过 scope release）；返回借用值必 retain；新建 Owned 值不额外 retain；scope 退出先执行 defer，再执行 releaseAllExcept(return)。
  - **`?` 解包赋值**：Ok 分支按常规 assign；Err 分支直接 return/panic，不触发 LHS release。
  - **`?` 未初始化 LHS**：Err 分支禁止对未初始化 LHS 执行 release；Ok 分支正常跟踪与 release。
  - **expr-stmt**：Owned 临时值落地 `__tmp` 并 release。
  - **global assign**：视作 `share` 写入并在覆盖时 release。
- 运行时语义：`memRetain/memRelease` 仅改 refcount；refcount==0 立即释放并从管理链表中移除。**RC 默认原子实现**（可用 `MM_ATOMIC=0` 关闭以换取性能）；跨线程共享仍需 `share_mt`/`Arc[T]` 与 `Send/Sync` 边界检查。
- 循环引用最佳实践：**显式打断**（如手动置 `nil`/拆分容器/断开引用），不内建 tracing GC。
- FFI 边界：跨 FFI 传递所有权需显式 `memRetain/memRelease`，避免悬垂指针。
- 可观测性：运行时用 `MM_DIAG=1` 输出 retain/release 日志；计数器 API：`memRetainCount/memReleaseCount`（可用 `memDiagReset()` 清零）。编译期 ownership 诊断使用 `OWNERSHIP_DIAGS=1`。

#### 0.1.1 moveHint 闭环（生产级实现要点）

- 预分析边界：以“语句列表”为单位做 last-use 扫描；仅对 `let/var/const` 绑定与简单赋值的 `RHS ident` 生成 moveHint；pattern/解构/field/index 作为 LHS 一律走保守路径。
- 失效条件：同一作用域存在 `defer` 引用该 ident；外层语句列表后续仍使用（如 if/loop 之后）；loop-carried ident（循环条件/迭代表达式使用或循环体内“先用后定义”的标识符）。
- 分支规则：if/when/case 的 moveHint 必须通过“分支后续 use 检查”；只要存在任一分支不满足 last‑use，整体禁用 move。
- Codegen 绑定（目标）：命中 moveHint 时，赋值/声明可走 move（跳过 retain，并在 cleanup 中跳过 moved-from 的 release）；不命中则 retain+tracking。
- 实现状态（2026-02-18）：生产链路使用后端 driver 直出 `.o/.exe`。moveHint/retain/release 的最终绑定以当前后端实现与回归脚本为准。
- 运行时闭环：`memRelease` 在 refcount==0 时立即释放并从管理链表摘除。
- 验收与可观测性：`examples/test_orc_closedloop.cheng` 覆盖 overwrite/return-move/`defer`/loop/if use-after 等场景；配合 `MM_DIAG=1` 与计数器一致性检查。

#### 0.1.2 生产级代码落地闭环（实现映射与验收清单）

- 预分析实现：`src/stage1/ownership.cheng` 负责 moveHint 标注与 ownership 分类，核心入口为 `ownershipMarkMovesTree`/`ownershipAnalyze`。
- Codegen 注入：生产链路由后端 driver 直接生成 `.o/.exe`（见 `src/backend/tooling/backend_driver.cheng`）。Ownership 的最终落地位置以当前实现为准。
- 运行时与容器：`src/std/system.cheng` 提供 ORC API；`str[]` 与 `Table[str]` 的关键写入路径（`addPtr_str`/`setStringAt`/`insert`/`delete`/`TablePut[str]`）已在容器实现内完成 retain/release 语义；codegen 不再对这些容器 API 做字符串 call-site retain 特判。
- 开关与诊断：`src/stage1/frontend_lib.cheng` 按 `MM`/`MM_STRICT` 开启 ownership/ORC；编译期诊断用 `OWNERSHIP_DIAGS`；`STAGE1_OWNERSHIP_DEEP=1` 强制深度遍历（默认浅遍历以提升 stage1 性能，仅用于对照/排障）；运行时计数与日志用 `MM_DIAG`。
- 确定性与回归：`sh src/tooling/tooling_exec.sh bootstrap --fullspec` 与 `sh src/tooling/tooling_exec.sh bootstrap_pure --fullspec` 用于输出 hash 一致性检查；`examples/test_orc_closedloop.cheng` 作为 ORC 回归用例。
- 发布门槛：严格模式回归必须通过；retain/release/escape 计数一致；任何（已启用的）moveHint 退化或容器 escape 漏标禁止发布。

#### 0.1.3 生产级落地清单（工具链/CI/发布）

- 可复现构建：固定编译器版本与依赖；产物随附 build hash、`MM` 配置与 backend driver 版本信息。
- CI gating：全量跑 `sh src/tooling/tooling_exec.sh bootstrap --fullspec` / `sh src/tooling/tooling_exec.sh bootstrap_pure --fullspec`；ORC/escape 回归用例必须通过且计数一致。
- 发布包组成：`cheng`（backend driver）、`cheng.stage2` seed、`cheng-formal-spec.md` 对应版本号与验收 hash。
- 回滚策略：严格模式默认启用；不再支持 `MM=off` 退化；保留 `MM_DIAG` 作为线上诊断开关。
- 生产观测：按需启用 retain/release/escape 计数与日志；发布后进行计数基线对比与异常告警。

#### 0.1.4 构建与缓存一致性（生产约束）

- 缓存键必须包含：编译器/标准库版本或 hash、`MM`/`MM_STRICT`、目标平台、规范版本。
- 建议使用 `COMPILER_VERSION`/`STAGE1_VERSION` 与 `TARGET` 显式传入版本与目标平台标识，写入缓存键与产物元数据。
- CI/发布构建建议使用独立 `CACHE_DIR` 或 `CACHE=0`，避免跨版本污染。
- 产物元数据需记录：manifest+lock 版本、编译器版本、`MM` 配置与 determinism hash。
- determinism hash 输入集合至少包含：backend driver/seed 版本、编译参数、目标平台、版本标识（`COMPILER_VERSION`/`STAGE1_VERSION`/`TARGET`）、关键环境变量（`MM`/`MM_STRICT`/`STAGE1_OWNERSHIP_DEEP`/`CACHE`/`IDE_ROOT`/`GUI_ROOT` 等）与 manifest/lock 内容（如 `PKG_LOCK`）。

#### 0.1.5 性能与优化建议（生产实践）

- 逃逸分析：非逃逸 Owned 值可省略 retain；返回路径与局部容器写入优先判定 escape。
- 简单表达式免 retain：对字面量/纯计算/可证明不逃逸的表达式跳过 retain。
- 容器写入路径优化：全局/长生命周期容器写入时，复用已有“临时落地 + retain/release”路径并记录基准。
- 建议基准：统计 retain/release 总量、escape 计数与编译耗时；对比 `MM_STRICT=0/1` 差异作为优化回归阈值。
- 性能剖析：`STAGE1_PROFILE=1` 输出 sem/mono/ownership/cgen 的统计与 top-k 节点耗时，用于定位 determinism/自举热点。

### 0.2 默认 move + var/共享 语义

- **默认 move**：赋值、参数传递、返回默认移动所有权；原值在语义层视为“已移走”，后续使用需显式 `share(x)` 或重新赋值。
- **可变借用**：`var` 形参/绑定表示可变借用（不转移所有权、不增加引用计数），生命周期限定在调用/作用域内，不得逃逸；**调用侧对 `var` 形参自动取址**（lvalue 会被降级为 `&`），显式 `&` 仍用于指针互操作与特殊场景。
- **共享**：`share(x)` 明确增加引用计数并返回可共享引用，用于跨作用域/容器写入；跨线程共享必须使用 `share_mt(x)`/`Arc[T]`（原子 RC）；**不做隐式拷贝**。
- **容器写入**：写入全局或长生命周期容器时必须保证被写入值可共享（`share` 或 Owned 新值）；不允许将 `var` 借用值直接存入全局容器。
- **取址/指针**：`&x` 仅为取址与指针交互语义，不作为内存管理借用语法。
- **指针与 `var`**：`T*` 与 `void*` 为原始指针，默认可读写；`var` 仅用于可变借用语义（非指针）。禁止 `var T*` 与 `var void*` 组合。
- **指针成员访问**：`T*` 的成员访问统一使用 `->`，等价于 `(*p).field`。
- **no-pointer 生产门禁**：在 `ABI=v2_noptr` 且 `STAGE1_NO_POINTERS_NON_C_ABI=1` 的口径下，非 C ABI 模块默认禁指针；仅 C ABI bridge 模块可按策略豁免。
  - **禁用指针类型**：`T*`、`void*`、`ref T`、`ptr[T]`。
  - **禁用指针操作**：解引用（`*`/`->`）、取址（`&`）、`dataPtr/getPointer`、`ptr_add/load_ptr/store_ptr`、`copyMem/setMem/zeroMem`、`alloc/dealloc`。
  - **违规诊断**：语义阶段报 `no-pointer policy`。
  - **默认 CLI 入口**：`chengc` 在 `ABI=v2_noptr` 下默认注入 `STAGE1_NO_POINTERS_NON_C_ABI=1` 与 `STAGE1_NO_POINTERS_NON_C_ABI_INTERNAL=1`。

#### 0.2.1 编译期借用证明（v1 落地）

- **追踪范围**：`var` 形参作为借用源；`let/var` 绑定或赋值若 RHS 为借用视图，则新变量被标记为借用（同作用域内 reborrow）。
- **借用独占**：借用活跃期内禁止对借用源进行读取/写入/再次借用；借用结束（作用域结束或变量被覆盖为非借用）后解除。
- **逃逸规则**：借用值禁止 `return/yield`、禁止写入全局、`share(x)` 禁止接收借用值；借用值不得传给非 `var` 形参（默认 move），仅当目标函数显式标注 `@borrows` 时允许；标注 `@escapes` 的函数在调用处仍拒绝借用值。
- **借用视图识别**：`ident/deref` 直接视为借用；`field/index` 本身不标记为借用，但作为借用视图调用的“来源”参与传播；`get/getPtr/TableGet*/SeqGet*/StringView` 仅在入参已是借用来源时传播借用；`&/dataPtr/getPointer` 视为显式指针借用；其它调用默认视为 Owned。
- **显式指针**：`&x`/`dataPtr(...)`/`getPointer(...)` 视为显式低级指针视图；禁止返回/`share` 逃逸，作为参数传递视为绕过借用证明（需调用方保证生命周期）。
- **能力边界（已工程化约束）**：跨过程默认要求显式摘要（`@borrows/@escapes`）；未知/未解析调用在“借用活跃”场景下按编译期硬错误处理（需提供可解析签名或显式注解）；类型细分（Copy/RC/原始指针）遵循当前 `Send/Sync + no-pointer` 门禁口径并持续细化。

### 0.3 多线程内存安全

- 目标：默认 ORC 保持原子 RC；跨线程显式、可检查、心智负担最小；不引入新关键词，仅新增标准库类型与编译器边界检查。
- 原则：单线程路径仍鼓励 `share` 明确语义；跨线程必须显式转换为 `Arc`/`share_mt` 并通过 `Send/Sync` 约束。

#### 0.3.1 标准库与运行时语义

- `Arc[T]`：原子引用计数的共享容器，跨线程共享的唯一入口；`Arc` 本身不可变借用，避免隐式可变共享。
- `share_mt(x)`：将 Owned `T` 或 `Arc[T]` 转换/克隆为 `Arc[T]`；内部使用原子 retain/release，最后一次 release 才析构。
- `Mutex[T]`/`RwLock[T]`：跨线程可变共享的显式通道；只允许通过锁获得可变视图。
- `Atomic[T]`：仅支持基础数值/指针/布尔类型；提供原子读写与 CAS。
- 原子 RC 语义：retain 采用 relaxed；release 采用 release；当 refcount 归零时执行 acquire fence 再析构，保证跨线程可见性。

#### 0.3.2 编译期约束与 `Send/Sync`

- 线程边界：当前白名单为 `spawn/chanI32Send/chanI32Recv`；调用实参必须满足 `Send/Sync` 约束。
- 线程边界当前通过 `@thread_boundary` 注解声明（无新增关键字），用于标记标准库/业务边界 API；未标注视为非边界；若声明了白名单并发入口但缺失标注，将在函数声明/调用处报错。
- 构造器白名单：`chanI32New` 的返回值视为 `Send/Sync`（可跨线程传递的 channel 句柄）。
- 函数指针：`&fnName`（函数取址）视为 `Send/Sync`；对数据取址 `&x` 仍属于原始指针，默认 `!Send/!Sync`。
- `Send`：可跨线程 move；Borrowed/`var` 借用仍为 `!Send`。
- `Sync`：可跨线程共享只读；类型递归满足；`Arc[T]` 为 `Send+Sync` 当且仅当 `T: Send+Sync`。
- `share` 仅用于同线程共享；跨线程必须使用 `share_mt`/`Arc[T]`，否则在边界处报错。
- `T*`/`void*` 与 FFI 句柄默认 `!Send/!Sync`，仅允许在库级封装并由调用方承担生命周期与同步责任。

#### 0.3.3 生产级落地闭环（实现与验收）

- 编译期：类型检查阶段增加 `Send/Sync` 推导；线程边界 API 增加静态校验；借用证明与 ownership 保持一致。
- 运行时：新增原子 RC 路径并与现有 ORC 共存；诊断开关复用 `MM_DIAG` 记录原子 retain/release。
- 标准库：提供 `Arc/Mutex/RwLock/Atomic`；线程与 channel API 明确声明 `Send/Sync` 约束。
- 回归用例：新增跨线程传参/捕获/共享/锁保护与错误用例；覆盖 use-after-free 与数据竞争误用的拒绝路径。

### 0.4 HRT Profile（已移除）

自 `2026-02-06` 起，官方生产工具链已移除 ASM/HRT 与 legacy C bootstrap 入口。  
`bin/cheng-stage0 --mode:hrt`、`ASM_RT`、`HRT_PROFILE` 不再属于现行支持范围。  
历史约束文档（如 `docs/hrt-profile-v1.md`）仅保留为存档，不再作为生产规范与验收门禁。

### 0.5 匿名函数与函数指针（语义差异）

- **匿名函数（lambda/closure）**可以捕获外部变量，值语义包含“环境”；编译期可能生成 env 结构与 trampoline。
- **函数指针**仅包含代码地址，不携带环境；ABI 更直接，适合 `importc` 回调与 C 交互。
- 不捕获的匿名函数可视作函数指针使用，但仍需确保签名与回调形参匹配。

### 0.5.1 工程状态与剩余改进项（工程视角）

- **语法一致性**：`TypeExpr(expr)` 仅用于类型转换；默认初始化仅通过“带类型标注且省略初始化”的隐式默认值实现；禁止引入 `TypeExpr()` 或 `TypeExpr expr` 变体。
- **匿名函数书写**：显式函数类型 + `fn(...) =` 语法偏冗长；可考虑提供语法糖（如 `fn(...) => expr`）但需明确闭包/函数指针 ABI 差异。
- **泛型/trait 诊断（已工程化并持续增强）**：语义阶段默认前移并阻断关键约束错误；`where`/`Self` 的最小定位信息已纳入生产门禁口径，后续仅做可读性增强。
- **FFI/ABI 边界（已工程化）**：`importc` 聚合返回必须 wrapper/out 参数；`varargs` 仅允许受限签名（darwin/arm64 已回归直连 i32 子集，其它平台建议 wrapper）；捕获型回调必须 `ctx` 承载环境，并由 `verify_backend_ffi_abi` 等门禁阻断回归。
  - `varargs` call lowering 需维护 16B 栈对齐，且遵循目标 ABI（x86_64 `AL=0`；AArch64 Darwin 8B 槽 + i32 扩展）。
  - `var` out 参数调用点按“自动取址”规则传递（源码 `foo(x)`，ABI 传 `&x`）。
- **泛型实例化（已工程化基线）**：跨模块实例缓存/去重以 determinism + UIR 稳定性门禁持续约束，禁止出现“实例膨胀但无门禁可观测”的回退。
- **ORC/所有权**：跨线程边界、闭包捕获与析构顺序需更明确的规范与诊断。
- **构建确定性（已收口）**：缓存键需覆盖 fullspec/示例输入；determinism 校验必须包含 seed、编译参数、目标平台与关键环境变量；发布打包链路要求确定性产物（稳定 mtime/排序 + `gzip -n`，不再 best-effort）。
- **标准库一致性/性能**：高频容器统一到 `hashmap/hashset` 等高性能实现，并保持 `[]`/`[]=` 语义一致。
- **工具链体验（持续收敛）**：错误提示格式统一；将后端报错前移到语义阶段（例如“借用活跃 + unresolved call”改为语义硬错误）；并持续完善最小可复现输出模式。
- **包管理/供应链（已默认强制）**：依赖锁、签名校验、registry 一致性在生产口径默认硬失败，不再作为建议项；仅显式 `PKG_INSECURE_ALLOW_NO_VERIFY=1` 允许本地诊断放宽（不得用于 CI/发布）。
- **诊断可用性**：错误消息需包含变量名/模块路径/关键类型摘要，避免“静默降级或后端才报错”。

### 0.6 改进建议（实现同步清单，优先级）

#### 0.6.1 性能与编译（优先）

- **moveHint**：引入更结构化的 def-use/SSA 或缓存化数据流摘要，减少分支/循环/`defer` 触发的保守回退，并提供 `why-not-move` 诊断。
- **retain/release 优化**：对 `NoEscape` 与纯值表达式跳过 retain；对局部 Owned 临时值提供 fast-path，降低 RC 压力。
- **构建确定性**：统一产物 metadata schema，强制记录 `MM/MM_STRICT/profile/target/seed`；提供缓存命中统计与失效原因。

#### 0.6.2 语义与安全（后续）

- **借用证明**：跨过程调用默认要求显式摘要（`@borrows/@escapes`）；未知/未解析调用在借用活跃路径必须阻断，后续优化聚焦“自动摘要推断”而非“允许跳过证明”。
- **ORC/循环引用**：标准库容器统一实现 `=trace`；提供可选 cycle collector 或 Arena/Region 一等公民化方案，并明确启用开关与性能基线。
- **线程边界**：标准库线程/任务/通道 API 内建白名单；`@thread_boundary` 未标注视为错误；`T*`/FFI 句柄默认 `!Send/!Sync` 且需显式封装。
- **已移除子集清理**：剔除 HRT/ASM/legacy C bootstrap 相关门禁与入口，统一生产语义到 backend-only 主链路。

##### 落地说明（v0.6.2 语义闭环草案）

- `@borrows`：允许借用值作为非 `var` 形参传入；返回值视作借用视图（若存在借用来源），禁止跨越调用栈持久化。
- `@escapes`：显式声明参数可能逃逸；传入借用值时在调用处报错，要求调用方转为 Owned 或显式复制。
- `@borrows` 与 `@escapes` 互斥；同一函数或同一参数同时标注视为错误。
- 线程边界白名单（如 `spawn/chanI32Send/chanI32Recv`）必须标注 `@thread_boundary`；未标注会在声明/调用处报错并给出最小迁移建议。
- `T*`/FFI 句柄默认 `!Send/!Sync`；跨线程传递必须显式封装为可发送/可共享的安全句柄类型。
- 生产模式下保持统一语义：`panic` 与 `?` 按主语言规则处理，不再区分 HRT 特化分支。

最小示例（语义层）：

```cheng
type Buffer = object
    data: void*
    len: int32

var globalBuf: Buffer

@borrows
fn view(buf: Buffer): Buffer =
    return buf

@escapes
fn store(buf: Buffer): int32 =
    globalBuf = buf
    return 0

@thread_boundary
fn spawn(entry: fn(void*), ctx: void*): int32 =
    return 0

fn taskMain(ctx: void*): int32 =
    return 0

fn demo(): int32 =
    let b: Buffer = globalBuf
    let v: Buffer = view(b)
    store(b) # ok: Owned 进入 @escapes
    # spawn(taskMain, b)  # error: Cross-thread argument must be Send/Sync: spawn argument #2
    # spawn(taskMain, v)  # error: Borrowed value cannot be passed across threads: spawn argument #2
    return 0
```

常见误用与诊断（草案，语义阶段）：

1) 借用值进入 `@escapes`（逃逸）：
```cheng
@borrows
fn view(buf: Buffer): Buffer =
    return buf

@escapes
fn store(buf: Buffer): int32 =
    globalBuf = buf
    return 0

fn badEscapes(): int32 =
    let v: Buffer = view(globalBuf)
    store(v) # error: Borrowed value cannot escape via call store: globalBuf
    return 0
```

2) 线程边界未标注 / 非 Send 值跨线程：
```cheng
fn spawn(entry: fn(void*), ctx: void*): int32 =
    return 0
# error: thread boundary requires @thread_boundary: spawn (声明/调用处均可触发)

fn taskMain(ctx: void*): int32 =
    return 0

fn badThread(): int32 =
    # spawn(taskMain, globalBuf) # error: Cross-thread argument must be Send/Sync: spawn argument #2
    return 0
```

## 1. 形式文法（BNF）

### 1.1 词法元素

| 类别 | 关键字或符号 |
|------|--------------|
| 关键字 | `module`, `const`, `let`, `var`, `type`, `concept`, `trait`, `fn`, `iterator`, `macro`, `template`, `async`, `mut`, `if`, `elif`, `else`, `for`, `while`, `break`, `continue`, `return`, `yield`, `defer`, `await`, `import`, `as`, `in`, `when`, `case`, `of`, `where`, `true`, `false`, `nil`, `block`, `enum`, `ref`, `tuple`, `set`, `str`, `is`, `notin` |
| 分隔符 | `(` `)` `[` `]` `{` `}` `,` `:` `;` `.` `=` `=>` `@` |
| 运算符 | `+` `-` `*` `/` `%` `==` `!=` `<` `<=` `>` `>=` `<<` `>>` `..` `..<` `&` <code>&#124;</code> `^` `~` `!` `&&` <code>&#124;&#124;</code> `$` `?` `?:` `->` |
| 字面量 | 整型（十进制/十六进制/二进制/八进制）、浮点（允许下划线）、布尔（`true`/`false`）、字符串（支持 `r"..."`/`f"..."` 前缀与 `"""..."""` 形式）、字符（`'a'`，支持常见反斜杠转义） |
| 标识符 | 正则 `[A-Za-z_][A-Za-z0-9_]*`，严格大小写敏感 |

说明：`mut`/`is` 为保留字，当前语义未启用；不出现在正式语法产生式中。`proc`/`method` 已移除，统一使用 `fn` 声明函数。

词法器保证行首自动计算缩进：同一逻辑块会触发 `INDENT`/`DEDENT` 辅助 token。

指针类型语法采用 `T*`。

逻辑运算使用 C 风格的 `&&` `||` `!`；按位异或用 `^`。

### 1.2 语法定义（EBNF）

以下为语法定义（EBNF）。

```ebnf
module         ::= { NEWLINE }
                    [ moduleHeader { NEWLINE } ]
                    { importDecl { NEWLINE } }
                    { topLevelDecl { NEWLINE } }
                    EOF ;
moduleHeader   ::= "module" ident NEWLINE ;

importDecl     ::= "import" modulePath [ "as" ident ]
                  | "import" modulePath "/[" modulePath { "," modulePath } "]" ;
modulePath     ::= ident { "/" ident } ;

topLevelDecl   ::= annotations topLevelCore ;
topLevelCore   ::= bindingDecl
                  | fnDecl
                  | iteratorDecl
                  | macroDecl
                  | templateDecl
                  | conceptDecl
                  | traitDecl
                  | typeDecl
                  | exprDecl ;
exprDecl       ::= expression ;

bindingDecl    ::= storage bindingEntry
                  | storage NEWLINE INDENT bindingEntry { NEWLINE bindingEntry } DEDENT ;
bindingEntry   ::= pattern [ ":" typeExpr ] [ "=" expression ] ;
storage        ::= "let" | "var" | "const" ;

annotations    ::= { annotation } ;
annotation     ::= "@" ident [ annotationArgs ] ;
annotationArgs ::= "(" [ annotationArg { ("," | ";") annotationArg } ] ")" ;
annotationArg  ::= ident
                  | numberLiteral
                  | stringLiteral
                  | charLiteral
                  | boolLiteral
                  | annotationList
                  | annotationDict ;
annotationList ::= "[" [ annotationArg { "," annotationArg } [ "," ] ] "]" ;
annotationDict ::= "{" [ annotationEntry { "," annotationEntry } [ "," ] ] "}" ;
annotationEntry ::= annotationKey ( ":" | "=" ) annotationArg ;
annotationKey  ::= ident | stringLiteral | numberLiteral | boolLiteral ;

routineHead    ::= ident [ typeParamList ] paramList
                   [ ":" typeExpr ]
                   [ "where" expression ] ;

fnDecl         ::= [ "async" ] "fn" routineHead "=" suite
                  | "fn" NEWLINE INDENT fnEntry { NEWLINE fnEntry } DEDENT ;
fnEntry        ::= [ "async" ] [ "fn" ] routineHead "=" suite ;

iteratorDecl   ::= "iterator" routineHead "=" suite ;

macroDecl      ::= "macro" ident [ typeParamList ] paramList
                   ":" typeExpr
                   [ "where" expression ]
                   "=" suite ;

templateDecl   ::= "template" ident [ typeParamList ] paramList
                   [ ":" typeExpr ]
                   [ "where" expression ]
                   "=" templateBody ;
templateBody   ::= suite | expression ;

typeDecl       ::= "type" ident [ typeParamList ]
                   [ "where" expression ]
                   "=" ( typeExpr | objectType )
                  | "type" NEWLINE INDENT typeEntry { NEWLINE typeEntry } DEDENT ;
typeEntry      ::= ident [ typeParamList ]
                   [ "where" expression ]
                   "=" ( typeExpr | objectType ) ;

objectType     ::= [ "of" typeExpr ] [ ":" ] objectFields
                  | [ "of" typeExpr ] ; /* 允许空对象 */
objectFields   ::= NEWLINE INDENT fieldDecl { NEWLINE fieldDecl } DEDENT ;
fieldDecl      ::= ident ":" typeExpr ;
conceptDecl    ::= "concept" ident [ typeParamList ] ":" suite ;
traitDecl      ::= "trait" ident [ typeParamList ] ":" suite ;

typeParamList  ::= "[" typeParam { ("," | ";") typeParam } "]" ;
typeParam      ::= ident [ ":" typeExpr ] [ "=" typeExpr ] ;

#### 隐式类型参数（语法糖，编译期）

除显式 `typeParamList`（如 `fn f[T](x: T): T = ...`）外，Cheng 还支持**隐式类型参数**的简写形式：

- 适用范围：`fn` / `iterator` / `template` / `macro` 的例程声明（不含 lambda 字面量）。
- 触发条件：例程头部省略 `typeParamList`，但在**参数类型**或**返回类型**里出现了自由的单字母大写标识符（`T`/`U`/`K`/`V`/...）。
- 语义：编译器按签名从左到右的首次出现顺序补全类型参数列表；例如
  - `fn len(xs: T[]): int32 = xs.len` 等价于 `fn len[T](xs: T[]): int32 = xs.len`
  - `template mapIt(xs: T[], body: untyped): U[] = ...` 等价于 `template mapIt[T, U](...) = ...`
- 限制：只识别 ASCII 单字母大写；限定名（如 `mod.T`）不引入隐式类型参数；若同名类型已在作用域内定义，则按该类型解析（不当作隐式类型参数）。

paramList      ::= "(" [ param { ("," | ";") param } ] ")" ;
param          ::= ident [ ":" typeExpr ] [ "=" expression ] ;

typeExpr       ::= procType
                  | tupleType
                  | setType
                  | enumType
                  | refType
                  | varType
                  | typePostfix ;

procType       ::= "fn" paramList [ ":" typeExpr ] ;
tupleType      ::= "tuple" "[" tupleElem { ("," | ";") tupleElem } "]" ;
tupleElem      ::= [ ident ":" ] typeExpr ;
setType        ::= "set" "[" typeExpr "]" ;
enumType       ::= "enum"
                  [ ":" enumFields
                  | NEWLINE INDENT enumFields DEDENT ] ;
enumFields     ::= enumField { NEWLINE enumField } ;
enumField      ::= ident [ "=" expression ] ;
refType        ::= "ref" ( objectType | typeExpr ) ;
varType        ::= "var" typeExpr ;
typePostfix    ::= typePrimary { "." ident
                                | "[" [ typeArg { ("," | ";") typeArg } ] "]"
                                | "?"
                                | "*"
                                } ;
typePrimary    ::= ident
                  | "(" typeExpr ")" ;
typeArg        ::= typeExpr | numberLiteral | ident ;

suite          ::= NEWLINE INDENT { statement } DEDENT
                  | statement ;

statement      ::= annotations statementCore ;
statementCore  ::= bindingDecl
                  | typeDecl
                  | assignStmt
                  | returnStmt
                  | yieldStmt
                  | breakStmt
                  | continueStmt
                  | deferStmt
                  | ifStmt
                  | whileStmt
                  | forStmt
                  | caseStmt
                  | whenStmt
                  | blockStmt
                  | macroStmt
                  | templateStmt
                  | conceptStmt
                  | traitStmt
                  | fnStmt
                  | iteratorStmt
                  | expressionStmt ;

assignStmt     ::= lvalue "=" expression ;
lvalue         ::= postfix ; /* 语义层限制为可写 lvalue */

returnStmt     ::= "return" [ expression ] ;
yieldStmt      ::= "yield" [ expression ] ;
breakStmt      ::= "break" ;
continueStmt   ::= "continue" ;
deferStmt      ::= "defer" ":" suite ;

ifStmt         ::= "if" expression ":" suite
                   { "elif" expression ":" suite }
                   [ "else" ":" suite ] ;

whileStmt      ::= "while" expression ":" suite ;

forStmt        ::= "for" pattern { "," pattern } "in" expression ":" suite ;

caseStmt       ::= "case" expression [ ":" ]
                   ( suite | NEWLINE [ INDENT { caseBranch } DEDENT | { caseBranch } ] ) ;
caseBranch     ::= "of" caseArm [ "if" expression ] ":" suite
                  | "else" ":" suite ;
caseArm        ::= caseEntry { "," caseEntry } ;
caseEntry      ::= pattern | expression ;

whenStmt       ::= "when" expression ":" suite
                   { "elif" expression ":" suite }
                   [ "else" ":" suite ] ;

blockStmt      ::= "block" [ ident ] ":" suite ;

fnStmt        ::= [ "async" ] "fn" routineHead "=" suite ;

templateStmt   ::= templateDecl ;
macroStmt      ::= macroDecl ;
conceptStmt    ::= conceptDecl ;
traitStmt      ::= traitDecl ;

iteratorStmt   ::= "iterator" routineHead "=" suite ;

expressionStmt ::= expression ;

pattern        ::= ident [ ":" typeExpr ]
                  | "_" [ ":" typeExpr ]
                  | literalPattern
                  | "(" pattern { "," pattern } ")"
                  | "[" [ pattern { "," pattern } ] "]"
                  | "{" setElem { "," setElem } "}"
                  | rangePattern
                  | objectPattern ;

rangePattern   ::= pattern (".." | "..<") pattern ;
objectPattern  ::= ident "(" patternArg { "," patternArg } ")" ;
patternArg     ::= pattern | ident ":" pattern ;

literalPattern ::= numberLiteral | stringLiteral | boolLiteral | charLiteral ;

expression     ::= conditionalExpr ;

conditionalExpr ::= logicalOr [ "?" expression ":" conditionalExpr ] ;

logicalOr      ::= logicalAnd { "||" logicalAnd } ;
logicalAnd     ::= bitwiseOr { "&&" bitwiseOr } ;

bitwiseOr      ::= bitwiseXor { "|" bitwiseXor } ;
bitwiseXor     ::= bitwiseAnd { "^" bitwiseAnd } ;
bitwiseAnd     ::= equality { "&" equality } ;

equality       ::= comparison { ("==" | "!=") comparison } ;
comparison     ::= membership { ("<" | "<=" | ">" | ">=") membership } ;
membership     ::= rangeExpr { ("in" | "notin") rangeExpr } ;
rangeExpr      ::= sum { (".." | "..<") sum } ;
sum            ::= term { ("+" | "-") term } ;
term           ::= factor { ("*" | "/" | "%") factor } ;

factor         ::= spaceCall
                  | postfix
                  | whenExpr
                  | ifExpr ;

postfix        ::= unary { "." ident
                         | "->" ident
                         | callSuffix
                         | "[" expression "]"
                         | "[" expression (".." | "..<") expression "]"
                         | "?"
                         } ;

callSuffix     ::= "(" [ callArg { "," callArg } ] ")" ;
callArg        ::= [ ident ( "=" | ":" ) ] expression ;
spaceCall      ::= postfix spaceAtom ; 
spaceAtom      ::= spacePrimary { "." ident
                                | callSuffix
                                | "[" expression "]"
                                | "[" expression (".." | "..<") expression "]"
                                | "?"
                                } ;
spacePrimary   ::= ident
                  | numberLiteral
                  | stringLiteral
                  | charLiteral
                  | boolLiteral
                  | tupleLiteral
                  | listLiteral
                  | "{" [ expression { "," expression } [ "," ] ] "}"
                  | fnLiteral
                  | iteratorLiteral
                  | "(" expression ")" ;

unary          ::= primary
                  | ("+" | "-" | "!" | "~" | "$" | "^" | "%") unary
                  | "await" unary
                  | "*" unary
                  | "&" unary
                  | comprehension ;

comprehension  ::= "for" pattern "in" expression
                   [ "if" expression ]
                   ":" expression ;

primary        ::= ident
                  | numberLiteral
                  | stringLiteral
                  | charLiteral
                  | boolLiteral
                  | tupleLiteral
                  | listLiteral
                  | fnLiteral
                  | iteratorLiteral
                  | "(" expression ")" ;

tupleLiteral   ::= "(" tupleElement { "," tupleElement } [ "," ] ")" ;
tupleElement   ::= [ ident ":" ] expression ;

listLiteral     ::= "[" listLiteralBody "]" ;
listLiteralBody ::= listComprehension
                  | [ expression { "," expression } [ "," ] ] ;
listComprehension ::= expression "for" pattern { "," pattern } "in" expression
                      [ "if" expression ] ;

whenExpr       ::= "when" expression ":" expression
                   { "elif" expression ":" expression }
                   [ "else" ":" expression ] ;

ifExpr         ::= "if" expression ":" expression
                   { "elif" expression ":" expression }
                   "else" ":" expression ;

caseExpr       ::= "case" expression [ ":" ]
                   ( expression
                   | NEWLINE INDENT { caseExprBranch } DEDENT )
                   [ "else" ":" expression ] ;
caseExprBranch ::= "of" caseEntry [ "if" expression ] ":" expression ;

fnLiteral      ::= "fn" [ ident ] [ typeParamList ] paramList
                   [ ":" typeExpr ]
                   [ "where" expression ]
                   "=" suite ;

iteratorLiteral ::= "iterator" [ ident ] [ typeParamList ] paramList
                    [ ":" typeExpr ]
                    [ "where" expression ]
                    "=" suite ;

boolLiteral    ::= "true" | "false" ;
charLiteral    ::= `'` CHARACTER `'` ;
```

### 1.2.1 语义约束（补充）

- 表达式语句允许忽略返回值；语法级 `discard` 已移除（`discard x`/`discard(x)` 旧写法会报错），默认允许 unused-value。
- `[]` / `[a, b, c]` 是列表字面量（历史序列字面量 `@[ ... ]` 已移除）：用于构造 `T[]`（动态序列）；`T[N]` 不是定长数组，而是 `T[]` 的“初始化 reserve 容量提示”（cap hint）类型标注。**空 `[]`** 必须有类型上下文：允许用于带类型标注的绑定初始化（`let xs: T[] = []`）、赋值（`xs = []`）、`return []`、以及实参位置（参数类型已知）；禁止作为无类型上下文的独立表达式语句。
- 空格单参调用仅用于**值/函数调用**；当 callee 解析为类型表达式或类型构造时，视为非法并提示使用 `TypeExpr(expr)`。
- 列表生成式：`[expr for pat in iter if cond]`（`if` 可选；当前仅支持 1 个 `for` + 可选 1 个 `if`）。生成式的结果类型为 `T[]`，且 `pat` 引入的名字仅在生成式内部可见。当前后端仅支持出现在“可落地”的位置（绑定初始化/简单赋值/`return`/全局初始化）；不支持直接嵌入复杂子表达式（例如直接作为函数实参），需先绑定到临时变量再使用。
- 计数型迭代规范：源码中形如 `var i = start; while i < end: ...; i = i + 1`、`while i <= end`、`while i > end`、`while i >= end` 的自增/自减计数循环，建议优先改写为 `for in` 迭代（含 guard-for 等价写法）；若存在多条件判断或改写后明显更复杂，可保留 `while`。
- 规范建议（SHOULD）：凡可归约为单调索引计数（循环变量仅做 `i = i + 1` / `i = i - 1`）的循环，建议优先使用 `for ... in ...`；若 `while` 含多条件判断或改写后可读性变差，可继续使用 `while`。
- `for ... in ...` 的 `in` 表达式支持：range 字面值（`a..<b` / `a..b`）、数组/Table/HashMap 的字面值、常量与变量；Table/HashMap 支持 `for k, v in tableOrMap` 键值迭代。
- 容器/数组类型语法（统一后缀）：
  - `T[]`：动态序列（运行时布局为 `len/cap/buffer`，可扩容；零值为“空序列”；带类型标注省略初始化即可得到空序列，标准库不再提供 `newSeq/newSeqWithCap` 作为初始化入口）。
  - `T[N]`：动态序列（`T[]`）的“初始化 reserve 容量提示”（cap hint）。`N` 为整数表达式（可为字面量/常量/变量表达式），在运行时求值；仅影响“省略初始化表达式”的隐式默认值初始化（额外执行一次 `reserve(..., N)`），不改变类型身份与 ABI（`T[N]` 与 `T[]` 类型等价）。
- 指针与 `[]/[N]` 的后缀允许交错组合：`T*[]`、`T[]*`、`T*[N]`、`T[N]*`。
- 旧容器语法已移除/禁用并在编译期报错：`seq[T]`、`openArray[T]`、`array[...]`、`seq_fixed[T, N]`（仅允许作为内部 lowering 目标）、`Table_fixed[V, N]`、`Table[V, N]`（容量不得作为类型参数）。
- FFI 影子桥接（Raw Pointer Safety）：用户层不再推荐显式 `ptr + len`/`out-ptr`/`void*` 暴露。
  - 切片桥接：优先 `T[]` + `@ffi_map`，由后端降级为 `(ptr,len)`。
  - 出参桥接：优先 `@ffi_out_ptrs`，由后端回收为 tuple 返回。
  - 句柄桥接：优先 `@ffi_handle`，由 runtime 负责 `void* <-> handle(u32/u64)` 映射。
  - 借用桥接：优先 `@importc + var T`，在借用校验通过后桥接到 `T*`。
- 设计结论：只保留 `T[]` 与 `T[N]`；`T[N]` 不改变类型/ABI，只影响“隐式默认值初始化”时的初始 reserve。
- no-pointer 门禁（生产口径）：`ABI=v2_noptr` + `STAGE1_NO_POINTERS_NON_C_ABI=1` 时，非 C ABI 模块默认禁指针；仅 C ABI bridge 模块可按策略豁免。
  - 禁用指针类型：`T*`、`void*`、`ref T`、`ptr[T]`。
  - 禁用指针操作：解引用（`*`/`->`）、取址（`&`）、`dataPtr/getPointer`、`ptr_add/load_ptr/store_ptr`、`copyMem/setMem/zeroMem`、`alloc/dealloc`。
  - 违规则在语义阶段报 `no-pointer policy` 诊断。
  - 默认 CLI 入口：`chengc` 在 `ABI=v2_noptr` 下默认注入 `STAGE1_NO_POINTERS_NON_C_ABI=1` 与 `STAGE1_NO_POINTERS_NON_C_ABI_INTERNAL=1`。

类型转换与调用约定：
- 显式类型转换仅使用紧邻小括号：`TypeExpr(expr)`（包含 `T*`、`ref T`、`var T` 或泛型类型等复杂类型）；不允许 `TypeExpr (expr)`。
- 类型转换不属于函数调用语法，不参与空格单参调用规则。
- 类型表达式不参与空格单参调用；`TypeExpr expr` 视为非法（不走空格单参调用）。
- `TypeExpr()` 空参形式已移除，不作为默认值初始化语法。
- 带类型标注的绑定省略初始化表达式时，使用该类型的**隐式默认值**；省略初始化表达式时必须提供类型。
- 隐式默认值（稳定语义）：
  - `bool` -> `false`
  - `int*`/`uint*`/`int`/`uint`/`enum` -> `0`
  - `float*` -> `0.0`
  - `char` -> `'\0'`
  - `str`/`cstring` -> `""`
  - `T*`/`ref T`/`var T`/`void*` -> `nil`
  - 复合类型（`tuple/object/T[]/T[N]/Table/...` 等）-> 该类型的零值（zero-init）；其中 `T[]/T[N]` 的零值为“空序列”（`len=0 cap=0 buffer=nil`）
- 字符串类型命名约束：内建字符串类型仅 `str` 与 `cstring`；`string` 不属于内建类型名，生产代码不应使用。
- 字符串 nil 语义（稳定口径）：
  - `str` 省略初始化默认 `""`。
  - `str = nil`、`let/var x: str = nil`、`x == nil`、`x != nil` 在编译阶段直接报错。
  - `str` 判空请显式写 `len(s) == 0` 或 `len(s) > 0`。
  - `cstring` 作为 C ABI 指针语义保留 `nil` 比较（`== nil` / `!= nil`）。
- 实现约束：编译器后端前端统一按 `stage1` 口径处理（旧前端别名已移除），并会显式拒绝 `string`（报错提示改用 `str`/`cstring`）。
- 类型表达式允许点限定名 `Module.Type` 用于消歧；语义等价于 `Type`，模块前缀在语义/后端 lowering 时消解。
- `default[T]` 仅用于表达式/返回/实参位置；禁止写 `let/var/const x: T = default[T]`，应省略初始化表达式。
- 空的 `[]` 需要类型上下文（例如 `let xs: T[] = []` / `xs = []` / `return []` / 作为实参），否则会报“缺少元素类型”。
- 类型与值命名空间分离；当 callee 解析为类型且只有一个位置参数时，`T(x)` 表示类型转换；对象构造使用具名字段参数 `T(field: ...)`。
- C 风格 `(T)(x)` 已移除。
- `cast[T](x)` 已移除。
- 单参数调用可使用空格或小括号：`f x` / `f(x)`；空格省略仅建议用于最外层，嵌套/作为参数时建议使用 `f(x)`；不允许 `f (x)`；小括号仍用于零参、多参或具名参数。
- 零参调用使用小括号形式（`f()` / `obj.m()`）；类型表达式不支持零参调用。

### 1.2.2 破坏性语法升级流程（工程约束，非语义）

- 当需要移除/替换源码层语法（例如 `seq[T]` -> `T[]`、`@[...]` -> `[...]`、引入列表生成式等）时，推荐按以下最小流程落地，避免自举/文档/门禁漂移：
  1. 规范与文档先行：先更新本规范与相关设计文档（如 `docs/container-refactor.md`、`docs/list-comprehension.md`），并同步 `docs/cheng-skill/` 与 `$HOME/.codex/skills/cheng语言/`；跑 `sh src/tooling/tooling_exec.sh verify_cheng_skill_consistency`。
  2. 实现新语法：parser 支持 + type/expr lowering 到内部 canonical；尽量保持后端/标准库的稳定面，必要时加 parse recovery。
  3. 禁用旧语法：旧写法一律硬错误并给迁移提示；旧语法只允许作为内部 lowering 目标存在。
  4. 自举兼容：生产链路不再支持 stage0 overlay；若 seed stage0 不支持新语法，需刷新 stage0/seed，并保证 `backend.stage0_no_compat` 门禁通过。
  5. 回归与 seed 更新：补最小正/反例 tests；跑 `sh verify.sh` 或相关 gate；需要刷新 seed 时用 `sh src/tooling/tooling_exec.sh bootstrap_pure` 并设置 `BOOTSTRAP_UPDATE_SEED=1`。

### 1.3 运算符优先级

| 优先级 | 运算符/结构 | 说明 |
|--------|-------------|------|
| 120 | `.` `->` | 成员访问 |
| 110 | `()` `[]` `?` | 调用、下标/切片、Result 解包 |
| 100 | 单目 `+ - * & ! ~ $ ^ await` | 前缀运算 |
| 90  | `* / %` | 乘除取模 |
| 80  | `+ -` | 加减 / 字符串拼接 |
| 77  | `<< >>` | 移位 |
| 75  | `.. ..<` | 区间运算 |
| 70  | `in` `notin` | 成员测试（区间/序列/字符串/set/Table/字面量容器） |
| 65  | `< <= > >=` | 比较 |
| 60  | `== !=` | 相等比较 |
| 50  | `&` | 按位与 |
| 45  | `^` | 按位异或 |
| 40  | <code>&#124;</code> | 按位或 |
| 30  | `&&` | 布尔与 |
| 20  | `||` | 布尔或 |
| 15  | `?:` | 条件运算 |

#### 1.3.1 `$`（stringify/toString）运算符（语义约定）

- `$x` 为前缀运算符，语义等价于对 `x` 调用一元操作符函数 `$`（可被重载）。
- 约定：`$` 用于 toString；可通过为类型实现操作符函数 `$` 来提供字符串化输出。
- 编译器对 `enum` 提供内建 `$`（默认使用字段名；若字段带显式字符串值则优先使用该值）。

#### 1.3.2 `<<` / `>>` 位移运算（语义约定）

- 适用范围：整数类型（`int8/int16/int32/int64/int`、`uint8/uint16/uint32/uint64/uint` 等）。
- 位移位数：设被移位值位宽为 `W`（例如 `int32` 为 32；`int`/`uint` 为 `INTBITS`），实际位移量 `k = rhs mod W`（结果落在 `[0, W-1]`）。
  - 该规则对任意整数 `rhs` 生效（包含负数与超范围值），用于避免 C 的 out-of-range shift 未定义行为。
- 左移 `a << rhs`：
  - 低位补 0，高位溢出丢弃（按位宽回绕），返回类型与 `a` 相同。
- 右移 `a >> rhs`：
  - 若 `a` 为有符号整数（`int*`/`int`），执行算术右移（符号扩展，IR: `ashr`）。
  - 若 `a` 为无符号整数（`uint*`/`uint`），执行逻辑右移（高位补 0，IR: `lshr`）。
- 例：`int32(1) << 33 == int32(2)`；`uint32(0xffffffff) >> 1 == uint32(0x7fffffff)`；`int32(-1) >> 1 == int32(-1)`。

#### 1.3.3 符号重载分发（编译期静态）

- Cheng 的符号重载采用**编译期静态分发**：候选函数选择由静态类型、泛型实例化与可见性在编译阶段确定。
- 不做运行时动态分发；编译产物中运算符调用应收敛为确定目标（内建 lowering 或已解析函数符号）。
- 下标运算遵循以下规则：
  - 读取 `a[b]`：优先命中内建容器路径（如 `str/T[]/T[N]` 等）；否则按操作符函数 ``[]`` 做静态重载解析。
  - 赋值 `a[b] = v`：若存在匹配的 ``[]=``，优先按 ``[]=`` 解析；否则按“先 `[]` 取值再赋值”的语义处理。
- 普通赋值 `lhs = rhs` 遵循静态分发：
  - 对复合类型（如 object/tuple/泛型容器实例化类型），若存在匹配的 ``=`` 重载（含泛型实例化后匹配），优先静态分发到该重载。
  - 未命中 ``=`` 重载时，回退到内建赋值语义。
- 标准容器（含 `hashmap/json/Table` 等）的 `[]/[]=` 也遵循同一静态分发规则。
- 若候选不唯一或不存在可用重载，必须在编译期报错（不得回退为运行时动态判别）。

### 1.4 模块导出与可见性

- Cheng 采用 Go 风格的“首字母大写导出”规则：标识符首字符为 ASCII 大写字母的符号视为导出。
- 首字符为小写字母或 `_` 的符号为模块私有。
- `import` 仅导入导出符号；未导出符号在模块外不可见。
- 不支持 `*` 导出标记。

### 1.5 包定义与导入解析（生产约束）

- 包根目录固定 `cheng-package.toml`；必须包含 `package_id`；依赖项至少声明 `package_id` + `channel`，并**应支持 SemVer 约束**（如 `version = "^1.2"`）与可选校验和。
- 锁文件默认 `cheng.lock.toml`；记录可验证快照（`cid/author_id/signature/epoch`）、解析后的版本/来源/校验和；`format = "source"` 表示源码直发。
- 导入仅接受归一化模块路径（例如 `import <pkg>/<path>`、`import std/...`、`import ide/...`、`import ide/gui/...`），不接受字符串/绝对/相对路径；路径中**不允许空格**。
- **路径规范化**：`<pkg>/<path>` 为当前推荐写法；`cheng/<pkg>/<path>` 作为兼容别名接受并在解析阶段等价归一化。
- **域名内容寻址（IPNS 风格）**：`<pkg>/<path>` 视为包域名入口（等价于 `pkg://cheng/<pkg>`）；解析由 registry/lock 固定 `cid`，本地从 `chengcache/packages/<cid>/` 加载。
- **包内模块根**：对已解析到的包根目录（含 `cheng-package.toml`），源码以 `src/` 为模块根：`import <pkg>/<path>` 映射为 `<pkgroot>/src/<path>.cheng`（`<pkg>` 只用于选包，不参与包内路径）。
- 包搜索根：优先 `PKG_ROOT/PKG_ROOTS`；未设置时默认 `~/.cheng-packages`，并优先尝试 `cheng-<pkg>` 容器目录。
- 标准库导入规范：业务/编译器代码统一使用 `std/<path>` 入口，并映射到仓库 `src/std/<path>.cheng`；`cheng/stdlib/bootstrap/*` 已移除，不作为可用导入路径。
- 迁移约束：`cheng/net/*` 与 `cheng/multiformats/*` 已上收至 `std/net/*` 与 `std/multiformats/*`；源码中禁止继续导入旧前缀。
- 支持相同前缀合并：`import libp2p/[crypto,transport,swarm]`（方括号内用逗号分隔，路径整体不含空格）。
- 导入分组不支持 `as`；需要别名时请拆成多行 `import ... as ...`。
- 模块路径解析由 manifest/lock 决定；团队规范统一使用 `/` 分隔的模块路径，禁止 `.` 或 `..` 片段。
- **源码根回退**：对非绝对/非相对的普通模块路径，解析器可回退尝试 `<workspace>/src/<module>.cheng`（用于仓库内源码模块导入）。
- **插件导入覆盖（可选）**：`PLUGIN_ENABLE!=0` 且命中插件前缀时，可在标准解析前按 `PLUGIN_PATHS`（兼容 `PLUGIN_PATH`）覆盖解析；此能力属于编译器可配置导入面，不改变语法约束本身。
- `@importc` 指令与参数之间不允许空格（例如 `@importc("printf")`）。
- 生产环境要求解析可复现并与 lock 一致；规范化后不得越出允许根目录。

## 附录 A 自研后端与 UIR 摘要

本附录记录自研后端落地的实现约束与 UIR 摘要；完整任务清单与文件结构见 `docs/cheng-backend-arch.md`。

- 后端定位：保持语言语义不变，编译器内部统一使用 UIR：生产主路径为 `Stage1 -> UIR -> Machine -> Obj/Exe`（`BACKEND_IR=uir`）；不再存在独立 MIR/LIR 生产链路。
- UIR 生产默认：`BACKEND_OPT_LEVEL` 未设置时默认 `2`；`UIR_SIMD` 未设置时在 `optLevel>=3` 自动开启。
- 兼容开关策略：`MIR_PROFILE`、`BACKEND_SSA`、旧单态化跳过开关已移除；设置后编译器直接报错。
- IR 语义显式化：整数运算与移位使用 `sdiv/udiv`、`smod/umod`、`lshr/ashr` 等显式操作，避免 C 语义歧义。
- 产物策略：生产默认 `emit=exe`（self/system linker 双轨）；`.o/.obj` 仅保留 internal gate（需显式 `BACKEND_INTERNAL_ALLOW_EMIT_OBJ=1`）。
- Dev 快速自举管线（host-only）：
  - `BACKEND_STAGE1_PARSE_MODE=outline|full`（dev 默认 `outline`，release 默认 `full`）。
  - `BACKEND_FN_SCHED=ws|serial` + `BACKEND_FN_JOBS`（dev 默认 `ws`，release 默认 `serial`）。
  - `BACKEND_DIRECT_EXE=1` 在 host darwin/arm64 + self-link 口径走 `macho_direct_exe_writer`；默认失败阻断（`BACKEND_FAST_FALLBACK_ALLOW=0`）。
- 当前实现：UIR internal 采用表达式树 + `ret/br/cbr` 终结指令，machine internal 覆盖 AArch64 基本算术/比较/分支/栈操作；`mod` 以 `sdiv+msub` 降级。
- SIMD 能力说明：当前闭环未要求向量化；SSA 与 SIMD 并非替代关系。SSA 负责值语义化优化（当前最小闭环主侧），SIMD 是向量化优化能力，需要在后续 UIR 阶段增加向量类型、并行化合法性分析与后端寄存器映射后分阶段接入。
- 验证入口迁移：`verify_*` 已并入 `cheng_tooling` 原生子命令；统一执行口径为 `sh src/tooling/tooling_exec.sh <verify_id>`（或 `cheng_tooling <verify_id>`）。文中历史 `sh src/tooling/tooling_exec.sh verify_xxx` 写法视为等价调用。
- 验证入口：`sh src/tooling/tooling_exec.sh verify_backend_float` 负责生成并执行最小闭环样例（`emit=exe` 口径）。
- 跨平台矩阵（可验收）：`sh src/tooling/tooling_exec.sh verify_backend_targets_matrix` 覆盖 darwin/ios(Mach‑O `.o`) + android/linux(ELF `.o`) + windows(COFF `.obj`)。
- 全语义回归入口：`sh src/tooling/tooling_exec.sh verify_backend_closedloop` 会使用后端 driver（默认 `artifacts/backend_driver/cheng`；可用 `BACKEND_DRIVER=<path>` 显式指定，未命中可运行 driver 直接失败）编译并运行 `examples/backend_closedloop_fullspec.cheng`，要求运行返回码为 `0`（默认并固定 `MM=orc`；ORC/Ownership 专项回归见 `examples/test_orc_closedloop.cheng`；跨目标 `self_linker(ELF/COFF)` 与 `linker_abi_core` 门禁默认强制阻断，且已移除 prebuilt-obj/link-only 降级路径，不允许 skip 与 compile-only 回退）。
- 除平台生产闭环入口：`sh src/tooling/tooling_exec.sh backend_prod_closure`（默认启用 `BACKEND_VALIDATE=1`，聚合 determinism‑strict、opt、SSA、FFI/ABI matrix（含 out 参数）、obj 校验+obj determinism、exe determinism、debug(dSYM)、sanitizer（可选）、`backend.selfhost_perf_regression`、`backend.multi_perf_regression`、release manifest+bundle+sign/verify（OpenSSL/Ed25519；若环境不支持 Ed25519 则自动降级 RSA‑SHA256，`sign/verify` 在发布链路默认 required）与 mm 回归；默认包含后端 selfhost（产出 stage2），`fullchain/stress` 需显式 `--fullchain/--stress`（或 `BACKEND_RUN_FULLCHAIN=1` / `BACKEND_RUN_STRESS=1`）开启；一旦开启 `fullchain`，对应 gate 按 required 语义执行，不允许 best-effort/skip 降级；manifest/bundle 默认记录并打包 stage2 driver（如存在），并可附带全链产物）。
- 零脚本生产闭环：`backend_prod_closure` 默认要求 `TOOLING_EXEC_BUNDLE_PROFILE=full` + `TOOLING_EXEC_REQUIRE_BUNDLE=1` + `TOOLING_EXEC_BUNDLE_CORE_AUTO_BUILD=0`，并阻断 `backend.zero_script_closure`，禁止闭环链路直调 `sh src/tooling/<id>.sh`（`tooling_exec.sh` / `env_prefix_bridge.sh` 除外）；`cheng/chengc` 核心入口必须走 `cheng_tooling` 原生命令路由，不允许 `cheng_tooling chengc` 回落 embedded shell payload。
- driver 自举 smoke 口径：`backend.driver_selfbuild_smoke` 为 required gate，默认输出路径与主 driver 统一为 `artifacts/backend_driver/cheng`；生产收口口径固定统一 driver（`BACKEND_DRIVER=artifacts/backend_driver/cheng` + `BACKEND_DRIVER_ALLOW_FALLBACK=0`），不依赖自动回退候选；seed/selfhost 仅允许显式覆盖用于排障。闭环强制 `DRIVER_SELFBUILD_SMOKE_SKIP_SEM=0` 与 `DRIVER_SELFBUILD_SMOKE_SKIP_OWNERSHIP=0`，防止语义降级。
- 全链自举门禁：`sh src/tooling/tooling_exec.sh verify_fullchain_bootstrap`（stage2→tools；obj-only fullspec internal gate + 工具 `--help` smoke；失败即阻断）。
- 专用机 100ms 自举门禁：`sh src/tooling/tooling_exec.sh verify_backend_selfhost_100ms_host`（基线文件 `src/tooling/selfhost_perf_100ms_host.env`；非目标主机默认报告模式）。
- 热补丁运行态门禁：`sh src/tooling/tooling_exec.sh verify_backend_hotpatch` 与 `sh src/tooling/tooling_exec.sh verify_backend_hotpatch_meta` 固定 `self-link` 口径并执行可运行探针，不接受 system-link 回退；unsupported target 直接失败（不再 `skip`）。
- 本地发布/回滚：`sh src/tooling/tooling_exec.sh backend_prod_publish`（验收后发布到 `dist/releases`），`sh src/tooling/tooling_exec.sh backend_release_rollback`（切换 `dist/releases/current` 回滚）。
- 语句支持：MVP 已支持 `let/var/赋值` 的栈槽降级与读取，用于闭环验证。

## 附录 B Raw Pointer Safety 契约冻结（RPSPAR-01）

规范性约束（Normative）：
- 本契约的规范名为 `零裸指针生产闭环`（`Zero-RawPtr Production Closure`，`ZRPC`），属于发布门禁的规范性约束，不是建议项。
- 执行模式为 `hard_fail`：任一契约漂移或门禁缺失必须阻断闭环与发布。
- 语言表面禁止裸指针类型与指针算术；不得向用户暴露 `void*` 语义入口。
- C ABI 互操作仅允许通过“影子桥接”完成，用户可见接口保持值/借用语义：
  - `ffi_map`：`slice -> (ptr,len)` 物理拆包。
  - `ffi_out_ptrs`：`out-ptr -> tuple` 语义回收。
  - `ffi_handle`：`void* <-> handle(u32/u64)` runtime 槽位映射。
  - `borrow`：`&T/&mut T -> T*` 仅在借用校验通过后桥接。
- 释放后句柄必须 fail-safe（panic/error），不得退化为 UAF/野指针访问。

同步标记（供门禁脚本读取）：
- `rawptr_contract.scheme.id=ZRPC`
- `rawptr_contract.scheme.name=zero_rawptr_production_closure`
- `rawptr_contract.scheme.normative=1`
- `rawptr_contract.enforce.mode=hard_fail`
- `rawptr_contract.formal_spec.synced=1`
