# Cheng 正式规范（语法与语义）

> 版本：2026-05-14

> 本文包含两类内容：  
> 1) `规范性要求`（决定语言语法与语义的行为边界）；  
> 2) `当前实现对齐`（用于与仓库现状同步，便于持续交付，**不作为语义层面兼容性要求**）。

本文为 Cheng 语言最终规范，描述语法与核心语义。

命令前缀约定（文内命令可直接执行）：
- `TOOLING=artifacts/tooling_cmd/cheng_tooling`
- 示例中的 `$TOOLING <subcmd>` 等价于直接调用 canonical tooling binary。

---

## 0.0 实现对齐注记（截至 2026-05-14）

- `verify_backend_ffi_slice_shim` 已完成脚本拼接到 native 路径的迁移，缩小脚本失败与构建超时耦合面。
- `artifacts/tooling_cmd/cheng_tooling` 与 `artifacts/backend_driver/cheng` 在 verify/closure 主链路中的地位是默认路径；脚本仅保留兼容或应急回退，不得成为默认发布入口。
- 当前高优先级工程缺陷：在修复前，部分输入下会出现 `driver_c_build_module_stage1 -> uirCoreBuildModule -> lowerExprValue* -> uirCoreFindTypeDeclByName` 的高频调用路径。该路径已收敛到一次性建表与可重入防护，不再作为阻塞缺陷；依旧要求在当前生产闭环 smoke 中保留可观测/回归门禁（如热点采样、调用计数、退化检测）。
- 工具链执行治理与超时（实践口径）建议：默认超时围栏为 60 秒；内存执行/编译快速路径以百毫秒级基线监控（默认非阻断，仅在 P0 门禁时作为失败条件）。
- 当前默认闭环口径仍为 dev/release 双轨：`cheng` 固定 dev 轨（host-only self-link + direct-exe）与 `release-compile` 的 system-link 发布轨；全局函数级并发 / 无链接器重构目前按专项计划推进，不视为默认标准行为。

#### 已完成

- `verify_backend_ffi_slice_shim` 等关键 path 已迁移为 native；脚本拼接残留已降到兼容和应急回退场景。
- `compile/chengc` 的主验证职责从 shell 入口剥离，工具主链路改走 canonical tooling 命令。
- no-pointer / Raw Pointer Safety 约束在发布链路中按 hard-fail 收口（ZRPC）。
- `uirCoreFindTypeDeclByName` 热点路径已加入一次性建表与可重入防护，避免已知死循环与重复扫描；配套的调用计数/超时抽样告警已持续保留。
- CSG v2 已收口为后端事实格式：public `emit-cold-csg-v2` 输出 canonical `CHENG_CSG_V2` facts；cold compiler 的 `system-link-exec --csg-in` 负责加载校验、object emit 与 direct-exe 生成。internal `CHENGCSG` 只用于显式 `system-link-exec --emit:csg-v2` 自检；完整 Cheng `PrimaryObjectPlan` 管线仍未闭合，cold_parser 的 Lowering/Primary materializer 必须写 missing reason，不能空 plan 成功。

#### 进行中

- 工具超时与资源围栏：统一默认 timeout 60s 与自举/执行耗时观测（百毫秒级预算，非默认阻断）。
- 关键热点观测：为 `driver` 编译阶段增加 call-path/采样可观测性，建立 CPU 热点报警。
- 产物可复现链路：持续对比当前生产闭环 smoke 与 `fullchain_bootstrap` 的 determinism 和回归门禁。

#### 待修 / 高优先级风险（非语义）

- 非确定性回归环境（陈旧 tooling binary / capability cache / 旧 sidecar）导致的分析偏差仍需排障治理。
- PURE-01 strict sidecar/proof contract：公开和 required 的 strict sidecar/proof active surface 只接受 Cheng strict-fresh contract，不得重新把 `backend_driver_c_sidecar_*`、`driver_c_build_module_stage1*`、`backend_driver_uir_sidecar_runtime_compat.c` 引回 active compile chain，也不得把 `emergency_c` 或 `dist/releases/current/cheng` 作为 fallback surface。
- 高负载场景下的资源告警（内存抖动、长时阻塞）在当前生产闭环 smoke 中需要保持可观测和硬失败阈值。

> 本节仅做实现对齐，不改变前述语法与语义条款的权威性定义。若与文本前后冲突，以各章节规范条款为准。

## 0. 语义与内存管理

### 0.1 ORC/Ownership 闭环规则

- 内存模型开关：`MM=orc`（固定）；ORC **默认严格模式**（只读 ownership 标注，不回退启发式），可用 `MM_STRICT=0` 关闭或 `MM_STRICT=1` 强制开启。
- Cheng 不内建 tracing GC；runtime 性能与诊断统一按 ORC retain/release 和 alloc/free/live 计数观察，不定义 tracing-GC pause contract。
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
- 默认公开安全边界：`cheng`、`release-compile` 与默认公开编译入口下，用户源码表面按 no-pointer + borrow/`Send/Sync` 规则 hard-fail 收口；这一定义的是“默认公开表面安全”，不等于 FFI/raw handle/环引用场景自动安全。
- 可观测性：运行时用 `MM_DIAG=1` 输出 retain/release 日志；计数器 API：`memRetainCount/memReleaseCount/memAllocCount/memFreeCount/memLiveCount`（可用 `memDiagReset()` 清零）。编译期 ownership 诊断使用 `OWNERSHIP_DIAGS=1`。

#### 0.1.1 moveHint 闭环（生产级实现要点）

- 预分析边界：以“语句列表”为单位做 last-use 扫描；仅对 `let/var/const` 绑定与简单赋值的 `RHS ident` 生成 moveHint；pattern/解构/field/index 作为 LHS 一律走保守路径。
- 失效条件：同一作用域存在 `defer` 引用该 ident；外层语句列表后续仍使用（如 if/loop 之后）；loop-carried ident（循环条件/迭代表达式使用或循环体内“先用后定义”的标识符）。
- 分支规则：if/when/case 的 moveHint 必须通过“分支后续 use 检查”；只要存在任一分支不满足 last‑use，整体禁用 move。
- Codegen 绑定（目标）：命中 moveHint 时，赋值/声明可走 move（跳过 retain，并在 cleanup 中跳过 moved-from 的 release）；不命中则 retain+tracking。
- 实现状态（2026-02-18）：生产链路使用后端 driver 直出 `.o/.exe`。moveHint/retain/release 的最终绑定以当前后端实现与回归脚本为准。
- 运行时闭环：`memRelease` 在 refcount==0 时立即释放并从管理链表摘除。
- 验收与可观测性：`examples/test_orc_closedloop.cheng` 覆盖 overwrite/return-move/`defer`/loop/if use-after 等场景；配合 `MM_DIAG=1` 与计数器一致性检查。

#### 0.1.2 生产级代码落地闭环（实现映射与验收清单）

- 预分析实现：当前 Cheng 侧 ownership/borrow 主线落在 `src/core/analysis/ownership.cheng`、`src/core/analysis/borrow_checker.cheng` 与 `src/core/analysis/borrow_ir.cheng`；moveHint/retain/release 的生产绑定仍以后端 driver 与 seed 中已启用路径为准。
- Codegen 注入：生产链路由 `src/core/tooling/backend_driver_main.cheng` 调度 `system-link-exec`，并经 `src/core/backend/lowering_plan.cheng`、`src/core/backend/primary_object_plan.cheng`、direct object/exe 或 system linker 路径生成 `.o/.exe`。Ownership 的最终落地位置以当前实现为准。
- 运行时与容器：`src/std/system.cheng` 提供 ORC API；`str[]` 与 `Table[str]` 的关键写入路径（`addPtr_str`/`setStringAt`/`insert`/`delete`/`TablePut[str]`）已在容器实现内完成 retain/release 语义；codegen 不再对这些容器 API 做字符串 call-site retain 特判。
- 开关与诊断：`MM` 固定为 `orc`；编译期 ownership 诊断用 `OWNERSHIP_DIAGS`，运行时计数与日志用 `MM_DIAG`。历史 `src/stage1/frontend_lib.cheng` 路径不再作为当前源码索引使用。
- 性能/内存门禁入口：`artifacts/backend_driver/cheng run-host-smokes perf_memory_contract_smoke`；报告默认写到 `artifacts/perf_memory_contract/<label>/perf_memory_contract.report.txt`。`perf_memory_contract_smoke` 默认优先测 `artifacts/backend_driver/cheng`；只有显式 `CHENG_SMOKE_COMPILER` 才覆盖。Darwin 正式内存比较值优先用 `peak memory footprint`；`maximum resident set size` 只保留原始观测，不作为稳定合同阈值。
- ORC 可观测项：报告中的 `orc_perf_contract` 记录 ORC runtime retain/release 与 alloc/free/live 合同，`*_compile_exec_phase_summary` 记录正式 `system-link-exec` 编译报告里的 phase 摘要，`*_compile_gap_breakdown` 记录 planner 之外的 object materialize/native link/line-map 真耗时。
- 编译理论下界当前只允许引用严格成立的样本：口径只认满足 `planner_total_ms <= compile_elapsed_ms` 的正式 `system-link-exec` 报告样本；现阶段稳定样本是 `object_native_link_plan_smoke`、`chain_node_smoke`、`content_stub_smoke`、`orc_perf_contract_smoke`。
- 确定性与回归：`$TOOLING bootstrap --fullspec` 与 `$TOOLING bootstrap-pure --fullspec` 用于输出 hash 一致性检查；`examples/test_orc_closedloop.cheng` 作为 ORC 回归用例。
- 发布门槛：严格模式回归必须通过；retain/release/escape 计数一致；任何（已启用的）moveHint 退化或容器 escape 漏标禁止发布。

#### 0.1.3 生产级落地清单（工具链/CI/发布）

- 可复现构建：固定编译器版本与依赖；产物随附 build hash、`MM` 配置与 backend driver 版本信息。
- CI gating：全量跑 `$TOOLING bootstrap --fullspec` / `$TOOLING bootstrap-pure --fullspec`；ORC/escape 回归用例必须通过且计数一致。
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
- **no-pointer 生产门禁**：在当前默认公开编译口径（`cheng`、`release-compile`、默认 `chengc`）下，用户源码模块默认禁指针；`@importc/@exportc` 等 C ABI 声明不再豁免。旧兼容名 `ABI=v2_noptr` 与 `STAGE1_NO_POINTERS_NON_C_ABI*` 仅保留为内部实现/兼容标识，不构成新的用户编译开关。
  - **禁用指针类型**：`T*`、`void*`、`ref T`、`ptr[T]`。
  - **禁用指针操作**：解引用（`*`/`->`）、取址（`&`）、`dataPtr/getPointer`、`ptr_add/load_ptr/store_ptr`、`copyMem/setMem/zeroMem`、`alloc/dealloc`。
  - **违规诊断**：语义阶段报 `no-pointer policy`。
  - **默认 CLI 入口**：用户无需再显式传 `--abi:v2_noptr`、`--std-noptr`、`--std-noptr-strict`、`--noptr-non-c-abi`、`--noptr-non-c-abi-internal`；这些公开开关已从默认入口移除。

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
- `Thread`：真实 OS 线程句柄；`thread.Start(&fn)`/`thread.StartPtr(&fn, ctx)` 返回可 `Join` 的句柄，`thread.Spawn`/`thread.SpawnPtr` 是显式 detach 入口。
- `Pool`：固定 worker 数的任务池；`ParallelFor(pool, count, &body, ctx)` 按原子 work counter 分配任务，body ABI 为 `fn(ctx: int64, index: int32, worker: int32): int32`，主线程 join 所有 worker 后返回。
- 原子 RC 语义：retain 采用 relaxed；release 采用 release；当 refcount 归零时执行 acquire fence 再析构，保证跨线程可见性。

#### 0.3.2 编译期约束与 `Send/Sync`

- 线程边界：当前白名单为 `thread.Start/thread.StartPtr/thread.Spawn/thread.SpawnPtr/thread.ParallelFor/chanI32Send/chanI32Recv`；调用实参必须满足 `Send/Sync` 约束。
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
历史约束文档仅保留为存档，不再作为生产规范与验收门禁。

### 0.5 匿名函数与函数指针（语义差异）

- **匿名函数（lambda/closure）**可以捕获外部变量，值语义包含“环境”；编译期可能生成 env 结构与 trampoline。
- **函数指针**仅包含代码地址，不携带环境；ABI 更直接，适合 `importc` 回调与 C 交互。
- 不捕获的匿名函数可视作函数指针使用，但仍需确保签名与回调形参匹配。

## 1. 形式文法（BNF）

### 1.1 词法元素

| 类别 | 关键字或符号 |
|------|--------------|
| 关键字 | `module`, `const`, `let`, `var`, `type`, `concept`, `trait`, `fn`, `iterator`, `macro`, `template`, `async`, `mut`, `if`, `elif`, `else`, `for`, `while`, `break`, `continue`, `return`, `yield`, `defer`, `await`, `import`, `as`, `in`, `when`, `case`, `of`, `where`, `true`, `false`, `nil`, `block`, `enum`, `ref`, `tuple`, `set`, `str`, `is`, `notin` |
| 分隔符 | `(` `)` `[` `]` `{` `}` `,` `:` `;` `.` `=` `=>` `@` |
| 运算符 | `+` `-` `*` `/` `%` `==` `!=` `<` `<=` `>` `>=` `<<` `>>` `..` `..<` `&` <code>&#124;</code> `^` `~` `!` `&&` <code>&#124;&#124;</code> `$` `?` `?:` `->` |
| 字面量 | 整型（十进制/十六进制/二进制/八进制）、浮点（允许下划线）、布尔（`true`/`false`）、字符串（短字符串 `"..."` 与多行字符串 `"""..."""`；`Fmt` 前缀支持短/多行插值）、字符（`'a'`，支持常见反斜杠转义） |
| 标识符 | 正则 `[A-Za-z_][A-Za-z0-9_]*`，严格大小写敏感 |

说明：`mut`/`is` 为保留字，当前语义未启用；不出现在正式语法产生式中。`proc`/`method` 已移除，统一使用 `fn` 声明函数。

词法器保证行首自动计算缩进：同一逻辑块会触发 `INDENT`/`DEDENT` 辅助 token。

指针类型语法采用 `T*`。

逻辑运算使用 C 风格的 `&&` `||` `!`；其中 `&&` 与 `||` 按从左到右短路求值。按位异或用 `^`。

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
fieldDecl      ::= ident ":" typeExpr [ "=" expression ] ;
conceptDecl    ::= "concept" ident [ typeParamList ] ":" suite ;
traitDecl      ::= "trait" ident [ typeParamList ] ":" suite ;

typeParamList  ::= "[" typeParam { ("," | ";") typeParam } "]" ;
typeParam      ::= ident [ ":" typeExpr ] [ "=" typeExpr ] ;
paramList      ::= "(" [ param { ("," | ";") param } ] ")" ;
param          ::= ident [ ":" typeExpr ] [ "=" expression ] ;

typeExpr       ::= procType
                  | tupleType
                  | setType
                  | enumType
                  | refType
                  | varType
                  | algebraicType
                  | typePostfix ;

algebraicType  ::= variantType { "|" variantType } ;
variantType    ::= ident [ "(" [ fieldDecl { ("," | ";") fieldDecl } ] ")" ] ;

procType       ::= "fn" paramList [ ":" typeExpr ] ;
tupleType      ::= "tuple" "[" tupleElem { ("," | ";") tupleElem } "]" ;
tupleElem      ::= [ ident ":" ] typeExpr [ "=" expression ] ;
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

- `suite ::= statement` 为稳定语义，表示允许单行 suite；例如 `if x < 0: return 0` 是合法写法。

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
                  | matchStmt
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

matchStmt      ::= "match" expression ":" NEWLINE
                   INDENT matchArm { NEWLINE matchArm } DEDENT
                 | "match" expression ":" matchArm ;
matchArm       ::= pattern "=>" suite ;

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
                  | "{" pattern { "," pattern } "}"
                  | rangePattern
                  | objectPattern
                  | variantPattern ;

variantPattern ::= ident [ "(" [ ident { "," ident } ] ")" ] ;

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
                  | ifExpr
                  | caseExpr ;

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

#### 隐式类型参数（语法糖，编译期）

除显式 `typeParamList`（如 `fn f[T](x: T): T = ...`）外，Cheng 还支持**隐式类型参数**的简写形式：

- 适用范围：`fn` / `iterator` / `template` / `macro` 的例程声明（不含 lambda 字面量）。
- 触发条件：例程头部省略 `typeParamList`，但在**参数类型**或**返回类型**里出现了自由的单字母大写标识符（`T`/`U`/`K`/`V`/...）。
- 语义：编译器按签名从左到右的首次出现顺序补全类型参数列表；例如
  - `fn len(xs: T[]): int32 = xs.len` 等价于 `fn len[T](xs: T[]): int32 = xs.len`
  - `template mapIt(xs: T[], body: untyped): U[] = ...` 等价于 `template mapIt[T, U](...) = ...`
- 限制：只识别 ASCII 单字母大写；限定名（如 `mod.T`）不引入隐式类型参数；若同名类型已在作用域内定义，则按该类型解析（不当作隐式类型参数）。

### 1.2.1 语义约束（补充）

- 表达式语句允许忽略返回值；语法级 `discard` 已移除（`discard x`/`discard(x)` 旧写法会报错），默认允许 unused-value。
- `[]` / `[a, b, c]` 是列表字面量（历史序列字面量 `@[ ... ]` 已移除）：用于构造 `T[]`（动态序列）或 `T[N]`（固定长度数组）。**空 `[]`** 必须有类型上下文：允许用于已有类型的赋值（`xs = []`）、`return []`、以及实参位置（参数类型已知）；禁止作为无类型上下文的独立表达式语句。
- 调用语法统一使用小括号：`f()`、`f(x)`、`f(a, b)`、`f(name: value)`。空格单参调用 `f x` 属于迁移期旧表面；在 `CHENG_STRICT_CALL_SYNTAX=1` 的优雅语法门禁下直接报错，迁移提示为 `use f(x)`。字符串格式化只保留 Nim 风格 `Fmt"..."`；多行拼接使用 `Lines(...)`。
- `Fmt` 为当前稳定公开格式化表面：`Fmt"label={expr}"` 与 `Fmt"""..."""` 用于插值字符串；无分隔动态数组拼接使用 `std/strutils.Join(parts, "")`，按换行拼接使用 `Lines(parts)` 或 `std/strutils.Join(parts, "\n")`。`Fmt(parts)`、`Fmt expr`、`Lines expr` 不再支持，小写 `fmt/lines` 不导出。
- 多行字符串语法固定为 `"""` 后立刻换行，结束 `"""` 独占一行；结束符缩进作为公共缩进并从每个非空正文行剥离，正文缩进浅于结束符时编译期报错。开头换行和结束符前的分隔换行不进入字符串；源码换行统一归一为 `\n`。普通 `"""..."""` 不处理反斜杠转义也不插值；`Fmt"""..."""` 只处理 `{expr}` 插值，`{{` / `}}` 表示字面量大括号。
- 字符串拼接操作符 `+` 已移除；字符串组合必须使用 `Fmt"..."`、`std/strutils.Join(...)` 或 `Lines(...)`。数值加法仍使用 `+`。
- 列表生成式：`[expr for pat in iter if cond]`（`if` 可选；当前仅支持 1 个 `for` + 可选 1 个 `if`）。生成式的结果类型为 `T[]`，且 `pat` 引入的名字仅在生成式内部可见。当前后端仅支持出现在“可落地”的位置（绑定初始化/简单赋值/`return`/全局初始化）；不支持直接嵌入复杂子表达式（例如直接作为函数实参），需先绑定到临时变量再使用。
- 计数型迭代规范：源码中形如 `var i = start; while i < end: ...; i = i + 1`、`while i <= end`、`while i > end`、`while i >= end` 的自增/自减计数循环必须改写为 `for ... in range` 迭代（含 guard-for 等价写法）。
- 规范要求（MUST）：凡可归约为单调索引计数（循环变量仅做 `i = i + 1` / `i = i - 1`）的循环，一律使用 `for ... in ...`；`while` 只用于非计数型条件循环。
- `for ... in ...` 的 `in` 表达式支持：range 字面值（`a..<b` / `a..b`）、数组/Table/HashMap 的字面值、常量与变量；Table/HashMap 支持 `for k, v in tableOrMap` 键值迭代。
- 容器/数组类型语法（统一后缀）：
  - `T[]`：动态序列（运行时布局为 `len/cap/buffer`，可扩容；零值为“空序列”；带类型标注省略初始化即可得到空序列，标准库不再提供 `newSeq/newSeqWithCap` 作为初始化入口）。
  - `T[N]`：固定长度数组。`N` 是编译期常量，类型身份与 ABI 都包含长度；`T[N]` 与 `T[]` 不等价。
- 指针与 `[]/[N]` 的后缀允许交错组合：`T*[]`、`T[]*`、`T*[N]`、`T[N]*`。
- 旧容器语法已移除/禁用并在编译期报错：`seq[T]`、`openArray[T]`、`array[...]`、`seq_fixed[T, N]`（仅允许作为内部 lowering 目标）、`Table_fixed[V, N]`、`Table[V, N]`（容量不得作为类型参数）。
- FFI 影子桥接（Raw Pointer Safety）：用户层不得显式暴露 `ptr + len`/`out-ptr`/`void*`。
  - 切片桥接：优先 `T[]` + `@ffi_map`，由后端降级为 `(ptr,len)`。
  - 出参桥接：优先 `@ffi_out_ptrs`，由后端回收为 tuple 返回。
  - 句柄桥接：优先 `@ffi_handle`，由 runtime 负责 `void* <-> handle(u32/u64)` 映射。
  - 借用桥接：优先 `@importc + var T`，在借用校验通过后桥接到 `T*`。
- 设计结论：只保留 `T[]` 与 `T[N]`；`T[]` 表示动态序列，`T[N]` 表示固定长度数组。
- no-pointer 门禁（生产口径）：当前默认公开编译口径下，用户源码模块默认禁指针；`@importc/@exportc` 等 C ABI 声明不再豁免。旧兼容名 `ABI=v2_noptr` 与 `STAGE1_NO_POINTERS_NON_C_ABI*` 仅保留为内部实现/兼容标识，不构成新的用户编译开关。
  - 禁用指针类型：`T*`、`void*`、`ref T`、`ptr[T]`。
  - 禁用指针操作：解引用（`*`/`->`）、取址（`&`）、`dataPtr/getPointer`、`ptr_add/load_ptr/store_ptr`、`copyMem/setMem/zeroMem`、`alloc/dealloc`。
  - 违规则在语义阶段报 `no-pointer policy` 诊断。
  - 默认 CLI 入口：用户无需再显式传 `--abi:v2_noptr`、`--std-noptr`、`--std-noptr-strict`、`--noptr-non-c-abi`、`--noptr-non-c-abi-internal`；这些公开开关已从默认入口移除。
- `std/cmdline` 约束：`main(argc, argv)` 入口统一调用 `cmdline.CaptureCmdLine(argc, argv)` 将参数转交 runtime 接口；`cmdline` 通过 `__cheng_rt_paramCount/__cheng_rt_paramStr/__cheng_rt_paramStrCopy` 获取并缓存参数。语言层禁止暴露 `argv:void*` 与指针算术语义。

类型转换与调用约定：
- 显式类型转换仅使用紧邻小括号：`TypeExpr(expr)`（包含 `T*`、`ref T`、`var T` 或泛型类型等复杂类型）；不允许 `TypeExpr (expr)`。
- 类型转换不属于函数调用语法；只能写 `TypeExpr(expr)`。
- 类型表达式不参与空格单参调用；`TypeExpr expr` 非法。
- `TypeExpr()` 空参形式只用于 `object/tuple/Bytes` 这类复合值默认值表达式；`T[]()` / `T[N]()` 非法，需改用带类型标注的省略初始化或字面量；简单类型同样不支持 `int32()`/`bool()`/`str()` 这类零参默认值写法。
- 带类型标注的绑定省略初始化表达式时，使用该类型的**隐式默认值**；省略初始化表达式时必须提供类型。
- 隐式默认值（稳定语义）：
  - `bool` -> `false`
  - `int*`/`uint*`/`int`/`uint`/`enum` -> `0`
  - `float*` -> `0.0`
  - `char` -> `'\0'`
  - `str`/`cstring` -> `""`
  - `T*`/`ref T`/`var T`/`void*` -> `nil`
  - 复合类型（`tuple/object/T[]/T[N]/Table/...` 等）-> 先按该类型的零值（zero-init）递归初始化；其中 `T[]/T[N]` 的零值为“空序列”（`len=0 cap=0 buffer=nil`）
- `object` / `ref object` / `tuple[...]` 的字段或元素声明可携带默认值（如 `a: int32 = 1`、`tags: str[] = ["a", "b"]`、`fixed: int32[3] = [1, 2, 3]`、`tuple[a: int32 = 1, b: int32[]]`）；复合值初始化顺序固定为：类型零值 -> 声明顺序应用字段/元素默认值 -> 显式构造实参覆盖。
  - `object/tuple/Bytes` 的 `T()`、`new(Type)`、省略初始化的 `let/var x: T`、以及 object/ref object 构造缺失字段，统一复用同一套隐式默认值物化规则。
- 字段/元素默认值表达式必须是稳态表达式：字面量（含 `T[]/T[N]` 上下文下的 `[]` 与 `[a, b, ...]`）、`object/tuple/Bytes` 的 `T()`、`new(Type)`、纯类型构造，以及 `if/?:` 组合。
- 字段/元素默认值表达式禁止引用同一对象/tuple 的其他字段，禁止副作用调用，禁止依赖求值顺序的写法；违者编译期报错。
- 与隐式默认值一致的显式初始化是编译期硬错误：带类型标注的 `let/var` 禁止写 `= false` / `= 0` / `= ""` / `= []` / `= T()`，字段/元素默认值也禁止重复声明同样的零值；只保留真正改变默认语义的值。
- `object` / `ref object` 构造器的具名字段必须唯一；unknown field 与 duplicate field 都是编译期硬错误。
- `object` / `ref object` 构造器支持 `T(field: value)` 与 `T { field: value }` 两种表面写法；二者共享同一隐式默认值物化、字段唯一性和 unknown field 诊断规则。
- `tuple` 本轮仅支持 typed implicit init 与 `T()` 物化；tuple 字面量/tuple 类型构造仍要求显式写全所有元素，不支持省略元素后用默认值自动补齐。
- 字符串类型命名约束：内建字符串类型仅 `str` 与 `cstring`。
- 字符串 nil 语义（稳定口径）：
  - `str` 省略初始化默认 `""`。
  - `str = nil`、`let/var x: str = nil`、`x == nil`、`x != nil` 在编译阶段直接报错。
  - `str` 判空请显式写 `len(s) == 0` 或 `len(s) > 0`。
  - `cstring` 作为 C ABI 指针语义保留 `nil` 比较（`== nil` / `!= nil`）。
- string ABI contract markers：
  - `string_abi_contract.version=1`
  - `string_abi_contract.scheme.id=SABI`
  - `string_abi_contract.scheme.name=backend_string_abi_contract`
  - `string_abi_contract.scheme.normative=1`
  - `string_abi_contract.enforce.mode=default_verify`
  - `string_abi_contract.closure=required`
  - `string_abi_contract.language.str=value_semantics`
  - `string_abi_contract.abi.cstring=ffi_boundary`
  - `string_abi_contract.nil_compare.cstring_only=1`
  - `string_abi_contract.formal_spec.synced=1`
- `str ABI/cstring` 已在 default `verify` + 当前生产闭环 smoke 收口；当前实现以 selector-owned cstring lowering 为准，若 payload 缺失则 codegen 直接失败。
- 实现约束：编译器后端前端统一按 `stage1` 口径处理（旧前端别名已移除），并会显式拒绝 `string`（报错提示改用 `str`/`cstring`）。
- 类型表达式允许点限定名 `Module.Type` 用于消歧；语义等价于 `Type`，模块前缀在语义/后端 lowering 时消解。
- `T()` 是正常表达式；仅对 `object/tuple/Bytes` 会按隐式默认值规则物化一个新的 `T` 值。`T[]/T[N]` 与简单类型默认值都不写 `T()`，旧 `default[T]` 写法已移除。
- 如果只是复合类型零值/默认值构造，不再维护 `FooZero()` 这类无参镜像 helper；直接使用 `var x: Foo`、`Foo()` 或 `new(Foo)`。
- `new` 的唯一合法表面写法为 `new(TypeExpr)`，返回 zero-init 的 `ref` 值；旧 `new x` / `new(x)` 本地变量分配写法已移除。
- 空的 `[]` 需要类型上下文（例如已有类型的 `xs = []` / `return []` / 作为实参），否则会报“缺少元素类型”。
- 类型与值命名空间分离；当 callee 解析为类型且只有一个位置参数时，`T(x)` 表示类型转换；对象构造使用具名字段参数 `T(field: ...)` 或 brace 形式 `T { field: value }`。
- C 风格 `(T)(x)` 已移除。
- `cast[T](x)` 已移除。
- 单参数调用统一写 `f(x)`；`f x` 只作为迁移期旧表面保留，不属于优雅语法。`f (x)` 非法。
- 零参调用使用小括号形式（`f()` / `obj.m()`）；当 callee 解析为 `object/tuple/Bytes` 这类复合值类型时，`T()` 表示默认值物化。

### 1.2.2 破坏性语法升级流程（工程约束，非语义）

- 当需要移除/替换源码层语法（例如 `seq[T]` -> `T[]`、`@[...]` -> `[...]`、引入列表生成式等）时，推荐按以下最小流程落地，避免自举/文档/门禁漂移：
  1. 规范与文档先行：先更新本规范与相关设计文档（如 `docs/list-comprehension.md`），并同步 `docs/cheng-skill/` 与 `$HOME/.codex/skills/cheng语言/`；跑 `artifacts/backend_driver/cheng run-host-smokes cheng_skill_consistency_smoke`。
  2. 实现新语法：parser 支持 + type/expr lowering 到内部 canonical；尽量保持后端/标准库的稳定面，必要时加 parse recovery。
  3. 禁用旧语法：旧写法一律硬错误并给迁移提示；旧语法只允许作为内部 lowering 目标存在。
  4. 自举兼容：生产链路不再支持 stage0 overlay；若 seed stage0 不支持新语法，需刷新 stage0/seed，并保证 `backend.stage0_no_compat` 门禁通过。该 gate 与 `build-backend-driver` 应以当前 `src/core/tooling/backend_driver_main.cheng`、`src/core/lang/*`、`src/core/analysis/*`、`src/core/ir/*`、`src/core/backend/*` 和相关 `src/std/*` 真实源码闭包为准；capability/preflight 列表不得继续引用已移除的 `src/stage1/*`、`src/backend/*` 或 `src/tooling/cheng_tooling.cheng` 旧树。若运行中的 tooling/driver binary 陈旧，应先重建 canonical tooling/driver 再判断 capability 漂移。
  5. 回归与 seed 更新：补最小正/反例 tests；跑 `$TOOLING verify_backend_closedloop` 或相关 gate；需要刷新 seed 时用 `$TOOLING bootstrap-pure` 并设置 `BOOTSTRAP_UPDATE_SEED=1`。

### 1.3 运算符优先级

| 优先级 | 运算符/结构 | 说明 |
|--------|-------------|------|
| 120 | `.` `->` | 成员访问 |
| 110 | `()` `[]` postfix `?` | 调用、下标/切片、Result 解包 |
| 100 | 单目 `+ - * & ! ~ $ ^ await` | 前缀运算 |
| 90  | `* / %` | 乘除取模 |
| 80  | `+ -` | 数值加减；字符串不得使用 `+` 拼接，改用 `Fmt` / `Lines` |
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

#### 1.3.4 条件表达式 `?:` 与 postfix `?` 区分

- Cheng 支持三目条件表达式：`cond ? thenExpr : elseExpr`。
- `?:` 为右结合，语义遵循 `conditionalExpr ::= logicalOr [ "?" expression ":" conditionalExpr ]`。
- postfix `expr?` 表示 Result/Option 风格解包语义（失败分支提前返回/传播）；它与三目 `?:` 是两套独立语义。
- `return expr?` 非法；需要先 `let value = expr?` 再 `return Ok(value)` 或返回其它显式构造值，语言层不做隐式 Ok 包装。
- 推荐实践：当表达式中同时出现两类 `?`（例如 `x? ? a : b`）时，使用括号显式分组以避免可读性歧义。
- 例：`flag ? 1 : false ? 2 : 0` 按右结合解析为 `flag ? 1 : (false ? 2 : 0)`。

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
- **循环导入约束**：模块导入图不允许出现环；命中环路时编译器必须报错并输出链路（`Import cycle detected: A -> ... -> A`）。
- **无前置声明口径**：同一编译单元允许“先用后定义”，语义阶段先收集全局符号/函数签名再分析函数体。
- **导入参与检测**：`import` 语句一经解析即进入导入图；即使当前模块未直接引用该导入符号，也必须参与循环检测。
- **实现约束（active trace）**：循环检测应基于“活动导入栈（active import trace）”，且入栈/出栈覆盖整个递归加载生命周期，避免提前出栈导致漏检。
- **插件导入覆盖（可选）**：`PLUGIN_ENABLE!=0` 且命中插件前缀时，可在标准解析前按 `PLUGIN_PATHS`（兼容 `PLUGIN_PATH`）覆盖解析；此能力属于编译器可配置导入面，不改变语法约束本身。
- `@importc` 指令与参数之间不允许空格（例如 `@importc("printf")`）。
- 生产环境要求解析可复现并与 lock 一致；规范化后不得越出允许根目录。

## 附录 A 自研后端与 UIR 摘要

本附录记录自研后端落地的实现约束与 UIR 摘要；完整实现细节见 `src/core/tooling/README.md`，总体路线见 `docs/cheng-plan-full.md`。

- 后端定位：保持语言语义不变，编译器内部统一使用 UIR：生产主路径为 `Stage1 -> UIR -> Machine -> Obj/Exe`（`BACKEND_IR=uir`）；不再存在独立 MIR/LIR 生产链路。
- UIR 生产默认：`BACKEND_OPT_LEVEL` 未设置时默认 `2`；`UIR_SIMD` 未设置时在 `optLevel>=3` 自动开启。
- 兼容开关策略：`MIR_PROFILE`、`BACKEND_SSA`、旧单态化跳过开关已移除；设置后编译器直接报错。
- IR 语义显式化：整数运算与移位使用 `sdiv/udiv`、`smod/umod`、`lshr/ashr` 等显式操作，避免 C 语义歧义。
- 产物策略：生产默认 `emit=exe`（self/system linker 双轨）；`release-compile` 支持 `emit=exe|shared|static`；非 release 可执行构建禁止 `emit=obj`，`.o/.obj` 仅保留内部 `allow-no-main` 工件生成通道（需显式 `BACKEND_INTERNAL_ALLOW_EMIT_OBJ=1`）。
- Dev 运行策略：`cheng --run` 默认进入 host runner（`--run:host`）；`--run:file` 为兼容执行路径。
- Dev 编译策略：`cheng` 固定为 canonical dev 轨入口（`compile/chengc` 已移除并返回 `rc=2`；默认 `BACKEND_INCREMENTAL=1`、`CHENGC_DEV_MULTI_DEFAULT=1`）；`BACKEND_MULTI_MODULE_CACHE` 默认关闭（`0`），stable driver 当前固定禁用 module-cache load 路径（不作为生产配置面）。release 轨改用显式入口 `release-compile`（默认 `CHENGC_RELEASE_MULTI_DEFAULT=0`，是否启用多单元并发与函数任务调度解耦）。
- linker 参数收口：`cheng` 与 `release-compile` 均不接受 `--linker:*`，也不接受 `BACKEND_LINKER` 环境覆盖；dev 轨固定 `self-link + direct-exe`，release 轨固定 `system-link`。
- `emit=shared|static` 语义：采用 release object-first 打包（后端先产 `obj`，再由系统工具链打包库）。`--emit:shared|static` 下禁止 `--run/--run:*`（命令配置错误，`rc=2`）。
- Dev 快速自举管线（host-only）：
  - 前端解析模式：`cheng`/`selfhost` dev 入口固定 `BACKEND_STAGE1_PARSE_MODE=outline`（不再接受外部覆盖）；`release-compile` 固定 `full`。
  - 函数任务调度：`cheng`/`selfhost` dev 入口固定 `BACKEND_FN_SCHED=ws`（不再接受外部覆盖）；`release-compile` 也固定 `BACKEND_FN_SCHED=ws`。`BACKEND_JOBS` 是唯一公开 worker 数控制面；`serial` 仅保留给内部诊断、perf 对照与低内存 bring-up。
    - `fn_parallel_contract.schedule=ws`
    - `fn_parallel_contract.serial_internal_only=1`
    - `fn_parallel_contract.perf_gate=verify_backend_selfhost_parallel_perf`
    - `fn_parallel_contract.dedicated_host_only=1`
  - 函数并行技术债在规范层已按 contract 口径收口；当前 darwin/arm64 enforced perf witness 见 `artifacts/backend_selfhost_self_obj/selfhost_parallel_perf_dedicated_witness.tsv`，不再视为当前规范漂移。`verify_backend_selfhost_parallel_perf` 默认仅报告 slowdown；需要刷新 witness 时，再在目标 perf 主机上显式升级为阻断。
  - 直写可执行：dev 轨固定 `BACKEND_DIRECT_EXE=1`、`BACKEND_LINKERLESS_INMEM=1`；`BACKEND_DIRECT_EXE=1` 在 host darwin/arm64 + self-link 口径走 `macho_direct_exe_writer`。release 轨固定 `BACKEND_DIRECT_EXE=0`、`BACKEND_LINKERLESS_INMEM=0`。两条轨道都固定 `BACKEND_FAST_FALLBACK_ALLOW=0`。
  - 确定性边界：函数任务允许并行执行，但 `UirFnTask`/`IselFuncTask`/direct-exe 函数块结果必须按稳定声明顺序 merge；最终 `.o/.exe` 字节流不得依赖 `BACKEND_JOBS`。
- 可选常驻编译 worker：`CHENGC_DAEMON=1` 时，`cheng` 请求经 `chengc_daemon` 本地队列执行（`start/status/stop`），用于降低频繁冷启动开销。
- Dev 热补丁策略：`BACKEND_HOTPATCH_MODE=trampoline` + append-only code pool；`BACKEND_HOTPATCH_LAYOUT_HASH_MODE=full_program` 且 `BACKEND_HOTPATCH_ON_LAYOUT_CHANGE=restart`。
- 当前实现：UIR internal 采用表达式树 + `ret/br/cbr` 终结指令，machine internal 覆盖 AArch64 基本算术/比较/分支/栈操作；`mod` 以 `sdiv+msub` 降级。
- SIMD 能力说明：当前闭环未要求向量化；SSA 与 SIMD 并非替代关系。SSA 负责值语义化优化（当前最小闭环主侧），SIMD 是向量化优化能力，需要在后续 UIR 阶段增加向量类型、并行化合法性分析与后端寄存器映射后分阶段接入。
- 验证入口迁移：`verify_*` 已并入 `cheng_tooling` 原生子命令；统一执行口径为 `$TOOLING <verify_id>`，历史 shell 包装入口不计入规范主口径。
- tooling 可执行落点：命令名 `cheng_tooling` 对应 canonical 二进制 `artifacts/tooling_cmd/cheng_tooling`（未配置 PATH 时请直接使用该路径）。
- 入口迁移（非 verify）：tooling 主链路已迁移为 `cheng_tooling` 原生子命令分发；embedded 文本仅保留兼容回退面（运行时使用临时脚本文件，不保留 `.embedded.sh` 常驻缓存）。
- 验证入口：`$TOOLING verify_backend_float` 负责生成并执行最小闭环样例（`emit=exe` 口径）。
- 跨平台矩阵（可验收）：`$TOOLING verify_backend_targets_matrix` 覆盖 darwin/ios(Mach‑O `.o`) + android/linux(ELF `.o`) + windows(COFF `.obj`)。
- 全语义回归入口：`$TOOLING verify_backend_closedloop` 固定使用 canonical backend driver（`$TOOLING driver-path --path-only`，默认 `artifacts/backend_driver/cheng`）编译并运行 `examples/backend_closedloop_fullspec.cheng`，要求运行返回码为 `0`（默认并固定 `MM=orc`；ORC/Ownership 专项回归见 `examples/test_orc_closedloop.cheng`；跨目标 `self_linker(ELF/COFF)` 与 `linker_abi_core` 门禁默认强制阻断，默认固定 stable driver 口径且不再自动回退 seed/selfhost/release 候选，已移除 prebuilt-obj/link-only 降级路径，不允许 skip 与 compile-only 回退）。
- 当前活入口收口到两个 Cheng 二进制：`artifacts/bootstrap/cheng.stage3` 负责 bootstrap/tooling/gate，`artifacts/backend_driver/cheng` 负责 ordinary compile、`system-link-exec` 与 host smokes。
- 当前常用生产回归入口：`artifacts/bootstrap/cheng.stage3 run-production-regression`；它聚合 `build-backend-driver`、`run-host-smokes stage3_command_surface_smoke backend_driver_command_surface_smoke build_backend_driver_report_smoke function_task_contract_smoke function_task_executor_contract_smoke body_ir_dod_soa_contract_smoke body_ir_noalias_proof_smoke cfg_body_ir_contract_smoke compiler_budget_contract_smoke runtime_c_baseline_contract_smoke thread_atomic_orc_runtime_gate_smoke perf_memory_gate_contract_smoke perf_memory_contract_smoke cheng_skill_consistency_smoke dev_hotpatch_100ms_scope_contract_smoke explicit_default_init_positive_smoke explicit_default_init_negative_smoke explicit_default_init_gate_smoke composite_zero_helper_gate_smoke latest_snapshot_port_preserves_live_str_smoke list_literal_nested_call_depth_smoke libp2p_quic_twoproc_server_pre_quic_smoke`、`verify-r2c-react-surface`、`run-cross-target-smokes` 与 `run-stage23-libp2p-smokes`。需要拆分排障时再分别跑子命令。
- CSG v2 后端事实格式的最小确定性门禁为 `tools/cold_csg_v2_roundtrip_test.sh`；它要求同一 facts 经 cold reader/object writer 两次产物字节一致，并检查 writer/reader report、facts 大小预算和坏输入 hard-fail。
- production regression smoke contract: `build_backend_driver_report_smoke function_task_contract_smoke function_task_executor_contract_smoke body_ir_dod_soa_contract_smoke body_ir_noalias_proof_smoke cfg_body_ir_contract_smoke compiler_budget_contract_smoke runtime_c_baseline_contract_smoke thread_atomic_orc_runtime_gate_smoke perf_memory_gate_contract_smoke perf_memory_contract_smoke cheng_skill_consistency_smoke dev_hotpatch_100ms_scope_contract_smoke explicit_default_init_positive_smoke explicit_default_init_negative_smoke explicit_default_init_gate_smoke composite_zero_helper_gate_smoke latest_snapshot_port_preserves_live_str_smoke list_literal_nested_call_depth_smoke libp2p_quic_twoproc_server_pre_quic_smoke`
- 调试优先级固定为“先内建、后外部”：先用 `artifacts/bootstrap/cheng.stage3 debug-report`、`print-asm`、`print-line-map`、`print-symbols`、`print-object`，再看真实产物 `*.compile.log`、`*.run.log`、`*.map`、`*.primary.o.s`；运行崩溃先对 `*.run.log` 跑 `crash-report`，性能先用 `profile-run/profile-report`。只有问题已经落到宿主 C runtime、系统 linker、`libSystem` 或内建产物不足时，才补用 `lldb/gdb/sample`。
- 当前 live 子命令面不再暴露 `backend_prod_closure`、`backend-prod-publish`、`verify_backend_abi_v2_noptr`、`verify_backend_selfhost_100ms_host`；这些名字只保留在归档 backend-only 文档或内部兼容记录里，不应再当作现行命令。
- 当前公开 ABI 已收口为单一默认 no-pointer 表面；用户不再选择 `v2`，旧兼容名 `ABI=v2_noptr` 仅用于内部实现与历史兼容。
- `build-backend-driver` 是当前 driver 自举入口；零脚本约束现在体现在 Cheng 二进制主链，不再以外层 shell 作为主入口。
- `perf_memory_contract_smoke` 是当前正式性能/内存 smoke；`runtime_c_baseline_contract_smoke` 固定 Cheng runtime 对真实 C baseline 的 ratio 上界，case 固定为 `orc_retain_release`、`atomic_i32_add`、`thread_spawn_join`、`slice_bounds_loop`、`hash_lookup`、`syscall_write`。`perf_memory_contract_smoke` 必须读取 `CHENG_RUNTIME_C_BASELINE_REPORT` 指向的真实成对基线报告并把 `runtime_baseline=c runtime_case=cheng runtime_case_name=<case> c_ns_per_op=<n> cheng_ns_per_op=<n> runtime_perf_ratio_x1000=<n> limit_runtime_perf_ratio_x1000=<n>` 写入 `perf_memory_contract.report.txt`。缺失报告、缺失 case 或 ratio 超限都必须 hard-fail，报告中写 `runtime_c_baseline_hard_fail=1`；`orc_perf_contract` 记录 ORC retain/release 与 alloc/free/live 配平，`*_compile_exec_phase_summary` 记录正式编译 phase 摘要，`*_compile_gap_breakdown` 记录 planner 之外的 object materialize/native link/line-map 真耗时，`crypto_hot_kernel_contract` 记录 SHA-256、X25519 pubkey、P-256 pubkey、P-256 sign 的热核 ns/op。
- `perf_memory_contract_smoke` 默认优先测 `artifacts/backend_driver/cheng`；只有显式 `CHENG_SMOKE_COMPILER` 才覆盖。
- Darwin 正式内存比较值优先用 `peak memory footprint`；`maximum resident set size` 只保留原始观测，不作为稳定合同阈值。
- 需要回答编译理论下界时，优先引用 `perf_memory_contract_smoke` 报告里的 `planner_total_ms`，不是整次 `elapsed_ms`；当前稳定样本是 `object_native_link_plan_smoke`、`chain_node_smoke`、`content_stub_smoke`、`orc_perf_contract_smoke`。
- 编译入口并发收口：`cheng` 默认移除 stage0 全局锁包装，固定为 `stage0 quarantine + preflight + timeout` 语义，不再通过 compile-path lock 串行化并发任务。
- driver 自举 smoke 口径：`backend.driver_selfbuild_smoke` 为可选 gate（默认关闭，需显式 `--driver-selfbuild-smoke` 或 `BACKEND_RUN_DRIVER_SELFBUILD_SMOKE=1` 开启）；默认输出路径与主 driver 统一为 `artifacts/backend_driver/cheng`。启用后强制 `DRIVER_SELFBUILD_SMOKE_SKIP_SEM=0` 与 `DRIVER_SELFBUILD_SMOKE_SKIP_OWNERSHIP=0`，防止语义降级。
- native-contract autosystem 口径：当 `BACKEND_NATIVE_CONTRACT=1` 时，stage1 前端必须强制关闭自动 `std/system` 导入（`stage1_autoSystemEnabled()` 返回 `false`），不得依赖 gate 脚本额外注入 `STAGE1_AUTO_SYSTEM=0`。
- driver 解析口径：`backend_driver_path` 默认使用稳定 driver `artifacts/backend_driver/cheng`，并带 stage0 编译探针；首选不健康时直接阻断（不再允许 `--stage0` 覆盖）。
- `build-backend-driver` 排障口径：若 `build-backend-driver --require-rebuild --debug-rebuild` 生成的新 attempt 在 `tests/cheng/backend/fixtures/return_add.cheng` 上仅报 `Unexpected token`，应先用 seed `artifacts/backend_selfhost_self_obj/cheng.stage2` 对同一 fixture 做 release-system smoke，再检查 `chengcache/backend_driver_build_tmp` 中保留的 attempt。当前已知回归现象是新 attempt 在 lexer 阶段把 token lexeme 全部物化为空串（典型日志为 `src_len=0 dst_len=0`），根因不在 import 解析。建议配合 `BACKEND_DEBUG_TOKEN_COPY=1`、`STAGE1_TRACE_TOKENS=1` 与 `BACKEND_DEBUG_PARSE_DIAG=1` 复现并保留日志。
- 全链自举门禁：`$TOOLING verify_fullchain_bootstrap`（stage2→tools；obj-only fullspec internal gate + 工具 `--help` smoke；失败即阻断）。
- `100ms` 编译与二进制原地更新都只属于 dev host-only `self-link + direct-exe + host runner hotpatch` 的 dedicated witness 口径；release 主线仍然是 `system-link`。
- 热补丁运行态门禁：`$TOOLING verify_backend_hotpatch` 与 `$TOOLING verify_backend_hotpatch_meta` 固定 `self-link` 口径并执行可运行探针，不接受 system-link 回退；required 口径下禁用 runnable 重试回退（`BACKEND_HOTPATCH_RUNNABLE_RETRIES>1` 直接失败）；unsupported target 直接失败（不再 `skip`）。
- 发布相关 live 命令面以 `src/core/tooling/README.md` 为准；旧 backend-only 名 `backend-prod-publish` 只保留为历史记录，不再作为当前子命令。
- stage0 quarantine 稳定性约束：默认开启“自动清理后短重检”（`TOOLING_STAGE0_QUARANTINE_BLOCK_RECHECKS=2`），用于降低“首轮已清理但仍阻断”的抖动。
- noalias/egraph/dod runtime probe 稳定性约束：probe 验收必须同时绑定 runtime surface 与 phase-contract surface。`verify_backend_noalias_opt` / `verify_backend_dod_opt_regression` 默认使用 `compile_stamp + generics_report + noalias_report`；`verify_backend_egraph_cost` 默认使用 `compile_stamp + generics_report + egraph/cost profile`。三者都要求 `single_ir_dual_phase + p4_phase_v1`，并显式审计 `high_uir_checked_funcs` / `high_uir_fallback_funcs` / `low_uir_lowered_funcs` 与 `stage1_ownership_fixed_0_effective`，禁止把 ownership proof 未进场的 fallback 路径误判为通过。
- proof-phase driver 收口：`verify_backend_mir_borrow` / `verify_backend_noalias_opt` / `verify_backend_egraph_cost` / `verify_backend_dod_opt_regression` 必须先探测可用的 ownership-on phase driver。strict selfhost required 产物必须发布 `artifacts/backend_selfhost_self_obj/probe_prod.strict.noreuse/cheng.stage2.proof`（以及可用时的 `cheng.stage3.witness.proof`），并附带 `*.proof.meta` / `*.proof.compile_stamp.txt` 证明 `stage1_ownership_fixed_0_effective=0/default=0`；current-source proof 现在必须额外发布 bare `artifacts/backend_selfhost_self_obj/probe_currentsrc_proof/cheng.stage2`（以及可用时的 `cheng.stage3.witness`）的邻接 `*.meta` / `*.compile_stamp.txt`，使 bare current-source surface 本身就是 published ownership-on witness。proof gate 默认优先使用这条更新鲜的 current-source bare proof surface，其次才回退到 `probe_prod.strict.noreuse` 的正式 proof 产物，再次才允许回退到 `BACKEND_PROOF_PHASE_DRIVER` 或其他历史 candidate。current-source bare `cheng.stage2` 默认应优先走 bundle sidecar，而不是当前进程导出的 heavy UIR builder；bare published witness 元数据不得出现 `sidecar_compiler` 或 `exec_fallback_outer_driver`。`verify_backend_selfhost_currentsrc_proof` 默认固定 canonical `artifacts/backend_driver/cheng` 作为 fast stage0；若 `probe_currentsrc_proof/cheng.stage2` 已存在，fast 默认允许复用这条 fresh current-source lineage，但仍必须重跑 bare `cheng.stage2` 与 `cheng.stage2.proof` 的 direct compile+run smoke。切到 strict 时，wrapper 默认应改用这份 fresh current-source surface 作为 stage0，并打开 `STRICT_ALLOW_FAST_REUSE=1 + STRICT_STAGE2_ALIAS=1 + SKIP_SMOKE=1`，只保留 wrapper 自己的 `return_i64` direct-compile smoke。若 ownership-on surface 仅因 `Cannot write while borrowed` 回归而不可用，而其余 surface 仍停在 `stage1_ownership_fixed_0_effective=1`，gate 必须统一以 `ownership-on surfaces are blocked by compiler borrow regression` 失败，不得再回退到 phase-off surface 误报 proof-backed 通过。
- proof-phase driver 执行面与见证面默认应收口到同一条 current-source bare surface：当 `artifacts/backend_selfhost_self_obj/probe_currentsrc_proof/cheng.stage2` 存在且其邻接 `cheng.stage2.meta` / `cheng.stage2.compile_stamp.txt` 合法时，preflight 应优先把它同时作为 `phase_driver`（实际 compile driver）与 `phase_proof_driver`（published witness）。`probe_currentsrc_proof/cheng.stage2.proof` 继续保留为兼容 launcher / fallback witness，但不再是默认 current-source witness。只有 bare current-source published surface 缺失时，才允许把 `phase_proof_driver` 回退到 launcher 或 strict selfhost proof 产物。
- self-link 读对象稳定性约束：`std/bytes.readFileBytes` 采用 `fileSize` 预分配 + 顺序读取并统一 `c_fclose` 关闭路径，以避免 `machoParseObj -> readFileBytes -> fclose` 段错误链路。
- NJVL 文档收口：独立 `docs/njvl*.md` 文档已下线；NJVL/SSU 的规范与验收口径统一以本规范、`docs/UIR.md` 与 `src/core/tooling/README.md` 为准。
- 语句支持：MVP 已支持 `let/var/赋值` 的栈槽降级与读取，用于闭环验证。

## 附录 B Raw Pointer Safety 契约冻结（RPSPAR-01）

规范性约束（Normative）：
- 本契约的规范名为 `零裸指针生产闭环`（`Zero-RawPtr Production Closure`，`ZRPC`），属于发布门禁的规范性约束，不是建议项。
- 执行模式为 `hard_fail`：任一契约漂移或门禁缺失必须阻断闭环与发布。
- 语言表面禁止裸指针类型与指针算术；不得向用户暴露 `void*` 语义入口。
- 编译器内部表示（AST/UIR/基本块）在生产口径应采用 `Arena + SoA` 连续存储，跨节点引用应使用 `int32` 索引；不得以对象裸指针作为长期关联键暴露到优化与门禁接口。
- C ABI 互操作仅允许通过“影子桥接”完成，用户可见接口保持值/借用语义：
  - `ffi_map`：`slice -> (ptr,len)` 物理拆包。
  - `ffi_out_ptrs`：`out-ptr -> tuple` 语义回收。
  - `ffi_handle`：`void* <-> handle(u32/u64)` runtime 槽位映射。
  - `borrow`：`&T/&mut T -> T*` 仅在借用校验通过后桥接。
- 释放后句柄必须 fail-safe（panic/error），不得退化为 UAF/野指针访问。

验收约束（Acceptance）：
- `ffi_map` 合入条件：parser/语义属性表能记录映射；no-pointer gate 拒绝用户源码中的裸 `ptr/len` 表面；UIR ABI lowering 或内存 thunk 只在 ABI 边界拆包；正例至少覆盖 `ffi_importc_map_slice_ptr_len_i32.cheng`，反例至少覆盖用户裸指针切片表面。
- `ffi_out_ptrs` 合入条件：语义阶段校验 out 参数位置、数量、返回 tuple arity/type；UIR lowering 创建临时出参槽并回收为 tuple；正例至少覆盖 pair/status/string result 三类，反例至少覆盖 arity mismatch。
- `ffi_handle` 合入条件：runtime handle table 负责 register/resolve/invalidate/generation；用户层只暴露 integer handle；smoke 必须覆盖正常读写、generation reuse、stale trap 与 stale reuse trap。
- `borrow` 合入条件：`var T` 与 `@borrows/@escapes` 先通过借用证明；ABI lowering 只在证明通过后生成物理 `T*`；反例必须覆盖非 lvalue、活跃借用逃逸、未知调用和 `@escapes` 调用。
- 默认 no-pointer 合入条件：`cheng`、`release-compile` 与默认公开入口均启用同一语义门禁；`@importc/@exportc` 不得绕过；违规诊断携带 `ZRPC`/`no-pointer policy`。
- 工具门禁合入条件：发布报告不得把 C 脚本、C seed 强制构建、旧 C sidecar 或假实现列为通过依据；active compile chain 出现旧 C sidecar 符号族必须 hard-fail。

同步标记（供门禁脚本读取）：
- `rawptr_contract.scheme.id=ZRPC`
- `rawptr_contract.scheme.name=zero_rawptr_production_closure`
- `rawptr_contract.scheme.normative=1`
- `rawptr_contract.enforce.mode=hard_fail`
- `rawptr_contract.formal_spec.synced=1`
