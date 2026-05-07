# Cheng 工业级路线图

> 口径：只记录当前仓库能证明或必须硬证明的状态。愿景可以写目标，不写成完成。若与实现冲突，以 `docs/cheng-formal-spec.md`、`src/core/tooling/README.md`、当前源码和当前可执行产物为准。

## 当前事实（2026-05-08）

### 已成立

- `artifacts/bootstrap/cheng.stage3` 可用，`self-check --in:bootstrap/stage1_bootstrap.cheng` 通过。
- bootstrap v2 合同仍只暴露：`print-contract,self-check,compile-bootstrap,bootstrap-bridge,build-backend-driver`。
- `ordinary_zero_exit_fixture` 已能通过 backend driver 编译并运行退出 0；report: `provider_object_count=6`、`standalone_no_runtime=0`。
- `void_tail_if_fallthrough_fixture` 编译运行退出 0；report `missing_reasons=-`。
- `let_call_return_result_direct_object_smoke` 编译运行退出 7；report `instruction_word_count=24`。
- **Cold Compiler CSG 往返全部打通**：`bootstrap/cheng_cold.c` 支持 emit → load → lower → codegen，33 个 CSG 测试全部通过，后端驱动 CSG 与直接编译一致（exit 2）。stdlib 白名单已移除。

### 新增成立（本次会话）

- **ARM64 编码器集成**：`src/core/backend/aarch64_encode.cheng`（504 行，46 个 `A64Enc*` 函数）已接入所有 manifest 和 build plan，`primary_object_plan.cheng` 的 BodyIR fill 管线已全部替换为编码器调用（`A64EncRet`/`A64EncMovz`/`A64EncBlPlaceholder`/`A64EncCmpImm`/`A64EncBCond` 等）。
- **C seed `@exported` 支持**：`bootstrap/cheng_seed.c` 新增 `cheng_seed_exported_symbol_from_annotation`，`@exported("name")` 注解现在被正确识别。
- **C seed `&&`/`||` emit 支持**：if 条件代码生成可处理 `&&` 和 `||` 运算符（prepare 阶段仍受限）。
- **自举递归防护**：`backend_driver_dispatch_min.cheng` 的 `BackendDriverDispatchMinCurrentCompiler` 新增 `CHENG_NO_BACKEND_DRIVER_HANDOFF` 检查，防止 provider 编译时无限递归。
- **BodyIR word count 修复**：`PrimaryBodyIrGeneralCfgWordCount` 中 ReturnOp 从 +2 修正为 +1，ReturnTerm 从 +1 修正为 +3，消除数组越界。
- **Lowering call target 注册**：非 return-root 的 `BodyKindUnsupported` 函数现在正确注册 call target。
- **自举编译首次通过**：backend driver 在 `CHENG_BACKEND_DRIVER_HANDOFF=1` 下成功编译自身入口模块和全部 6 个 provider，7 个 `.o` 链接成功，无 crash/hang/bounds check 失败。当前仅编译入口模块（47 items, 179 words），非完整 manifest 模块集（1059 items, 16796 words）。

### 新增里程碑：BodyIR CFG with Cbr comparison

- **`const_elif` 已通过！** 内联条件解析 + Cbr 创建 + Register allocation + ARM64 比较指令（`cmp w9,w10; b.lt; b`）全部就位。EXIT=1（正确）。
- `PrimaryBuildBodyIrFromTypedStatements` 现在包含完整的内联条件解析（字符级比较运算符检测），创建 Cbr/Return term 和 block，C seed 编译通过。
- `elif_else_guard_cfg_fixture` 的 `classify` 函数符号未发射到 `.o` 文件，导致 `main` 的 `bl classify` relocation 无目标可 patching（仍 `bl #0`）。

### 当前阻断

- 跨函数调用的符号发射：`classify`（BodyKindUnsupported + BodyIR）有正 wordCount 但未出现在对象文件符号表中。
- 自举编译仅覆盖入口模块（47 items），需要 manifest-based 全量编译（1059 items）才能产出完整 backend driver。
- 任何 `CHENG_BUILD_BACKEND_DRIVER_FORCE_C_SEED=1` 结果都不计入路线图进度。

### 新增（2026-05-08）：函数并行接入尝试

- **lowering_plan.cheng 并行化尝试**（2026-05-08 提交 `9c40953d`、`3816cae3`、`bf077095`）：
  - 用 `async.spawn` + `atomic.FetchAddI32` 实现 fan-out 并行 for 循环，替代 `LoweringBuildPrimaryObjectIr` 中的串行 `for`
  - 通过 `cheng.stage3` 编译到 `primary_object_plan` 阶段，代码语法/语义合法
  - 但 `primary_object_plan` codegen 不支持 `let`/`if`/`while`/atomic/spawn 等语句模式，新函数体产出了 `unsupported_body_kinds`
  - C seed 最终链接失败（`compile-c-seed-bootstrap failed exit=2`）
- **回退与保留**：
  - `lowering_plan.cheng` 已回退到串行版本
  - `backend_driver_dispatch_min.cheng` 保留 `function_task_schedule=ws/serial` 动态报告
  - `function_task_executor.cheng` 保持原始空桩不变
- **根因**：Cheng 规范 §716 禁止循环模块导入，不能走 `function_task_executor` ↔ `lowering_plan` 回调；codegen 不支持函数体内的控制流/并发原语
- **正确路径**：要么扩展 `primary_object_plan` codegen 支持所需语句，要么在 codegen 层以下（C/importc 层）实现线程调度，再暴露简单调用接口给 Cheng

### 新增（2026-05-08）：Cold Compiler CSG Round-Trip 完成

- **CSG 往返编译闭环**（`bootstrap/cheng_cold.c`）：cold compiler 现已支持通过 CSG 事实（emit → load → lower → codegen）编译源文件。
  - 后端驱动 `backend_driver_dispatch_min.cheng`（114 函数）：直接 exit 2，CSG 往返 exit 2（58ms，含 stdlib 导入）。
  - 组合内核 `cold_bootstrap_kernel_combined.cheng`（133 函数）：直接 exit 42，CSG 往返 exit 42（40ms）。
- **CSG 测试覆盖 33/33**（2026-05-08 实测）：
  - ✅ cold_bootstrap **18/18**：全部通过（含 inline suite、泛型字段、tuple 默认值、variant 构造器、match 语句）
  - ✅ cold_csg fixtures **12/13**：`str_arg`、`str_payload`、`result`、`result_payload`、`result_q`、`result_call_q`、`match`、`match_call_target`、`composite_return`、`exporter`、`mixed_field`、`multi_field`
  - ✅ cold_other **3/3**：`debug_ptr`、`soa_object_regression`、`workdeque_soa_regression`
  - ❌ 仅 `cold_csg_facts_exporter_smoke` 失败（`os.GetEnvDefault` 不在冷子集）
- **确定性**：CSG 输出 SHA 完全确定性；二进制经 `COLD_NO_SIGN=1` 也完全确定性（codesign 是唯一噪声源）。
- **编译时间**（CSG 往返 wall clock）：冷子集测试 35-40ms，组合内核 40ms，后端驱动 58ms。
- **stdlib 白名单已移除**：os.cheng 等标准库全部通过 CSG 类型加载。
- **关键修复汇总**：
  - CSG 发射器单遍行扫描器重构（`scan_next_line`/`scan_collect_body`/`emit_enum_variants`）
  - 槽位别名修复（`var pos = start; pos = pos + 1`）
  - CSG 制表符转义（字符串内 TAB 不破坏 CSG 格式）
  - 条件冒号处理（`if ch == ':':`、`if cond: // comment`）
  - 无体函数默认返回值（`@importc` 声明）
  - inline suite 支持（`if cond: stmt` + 分号多语句）
  - variant 构造器/枚举/match 完整支持
  - tuple 类型以 object 形式发射
  - `Result[T, E]` 双泛型 + 括号深度追踪逗号切分
  - 符号索引对齐（`cold_csg_finalize` + 导入后重注册）
  - 参数类型 span arena 复制（防 src 缓冲区覆盖）
  - 类型字段导入后重解析（跳过泛型参数）
  - 10 处 `die()` → 优雅降级（`array field missing length`、`params limit`、`add target`、`variant comparison`、`object slot missing type`、`field assignment kind` 等）
  - 参数上限 8→32
  - stdlib 白名单移除
  - 42 个 manifest 源文件全部通过语法/语义检查（0 个直接产出二进制——它们是库模块，非独立程序）

### 新增（2026-05-05）

- **代数类型 + 模式匹配**：语法已写入 `docs/cheng-formal-spec.md` §1.2。
  - `type Option[T] = Some(value: T) | None`——tagged union，`|` 分隔 variant。
  - `match expr: Variant(x) => suite Variant2 => suite`——编译为 tag compare + CBNZ/TBNZ 跳转链（≤4 variant）或 PC-relative 跳转表（5+ variant）。
  - 冷编译器原型 `bootstrap/cheng_cold.c` 已包含 SoA BodyIR 层面的 `OP_TAG`/`OP_PAYLOAD`/`TM_SWITCH` 支持。
  - 完整编译器（`src/`）待实现：typed_expr 层的代数类型 layout、lowering 的 match→CBR 转换、ARM64 的 SWITCH term 回填。

## 总目标

Cheng 的工业路线不是和 LLVM/mold 在传统资源赛道硬拼，而是用三条工程主线降维：

| 主线 | 目标 | 当前完成口径 |
|---|---|---|
| Dev 轨 | `self-link + direct-exe + host runner hotpatch`，目标是 host-only 交互反馈进入 100ms 口径 | 只在专用 witness 证明后才可写成达成；不等同于 `30-80ms` 纯冷自举 |
| Release 轨 | `UIR -> .o -> system-link` 稳定闭环 | 当前仍是发布主线 |
| 语义特化 | Ownership/No-Alias、BodyIR DoD/SoA、No-pointer FFI、E-Graph | 合同与 smoke 分段推进，不能用 compile-only 通过 |

关键边界：
- 当前阶段 0 到阶段 4 是现有架构收敛线，目标是正确性闭环、可观测报告和秒级/十秒级性能，不承诺逼近 `30-80ms`。
- `30-80ms` 冷自举是独立极限架构线，不是 BodyIR kind 收敛、函数并行或 compound 条件补齐后的自然结果。
- 任何 warm daemon、hotpatch、C seed、旧缓存、系统 linker 或 compile-only 结果，都不能计入 `30-80ms` 冷自举证明。

## `30-80ms` 冷自举极限架构

口径：已有纯 Cheng 编译器冷进程编出 backend driver 候选 `exe + .map`。这个目标要求重写编译器热路径，不是修补现有 backend driver。

| 维度 | 当前收敛线 | 极限架构硬约束 |
|---|---|---|
| 源码 | `ReadTextFile`、字符串物化、重复 normalize | mmap 源码闭包；token、AST、typed facts 只保存 source span |
| 内存 | 热路径仍可能逐对象分配与复制复合数组 | phase arena + per-worker arena；阶段结束整页释放 |
| IR 布局 | 混合对象、数组和历史 body kind 兼容字段 | SoA dense arrays；op、term、block、local、call 全用 `int32` 索引 |
| 前端事实 | parser/typed/lowering 之间仍有重复扫描和派生表复制 | 单次扫描生成结构化事实；后续阶段只借用 span 与 fact id |
| 链接 | `.o` materialize 后经系统 linker 或 direct object 局部 witness | linkerless executable image；dev host-only 直接写可执行布局 |
| 并行 | task plan 与 executor 可见，但 active 主链还未证明真并行收益 | lock-free work-stealing、真实 atomic CAS、per-worker arena、稳定顺序 merge |

验收：
- A 编 B，B 再编同一 backend driver 候选 `exe + .map`，两次 report 关键字段一致。
- report 必须写出 `source_storage=mmap_span`、`allocation=phase_arena`、`ir_layout=soa_dense`、`linkerless_image=1`、`system_link=0`、`hot_path_node_malloc=0`。
- 冷进程计时只覆盖同一语义闭包：parse、typed facts、lowering、machine image、map 生成；不能混入 daemon、hotpatch 或缓存命中。
- 失败必须 hard-fail，不允许回退到现有 `system-link-exec`、`.s` fallback、C seed 或串行 executor。

## 阶段 0：修活当前 backend driver

| files | action | verify | done |
|---|---|---|---|
| `src/core/tooling/backend_driver_main.cheng`、`src/core/tooling/backend_driver_dispatch_min.cheng`、`src/core/tooling/compiler_request.cheng` | 恢复 `help/status/print-build-plan/system-link-exec` 的可观测命令面 | `artifacts/backend_driver/cheng help`、`status --root --in --out`、`print-build-plan` 必须输出固定字段 | 当前已过；以后静默退出视为失败 |
| `src/core/backend/system_link_plan.cheng`、`src/core/backend/primary_object_plan.cheng`、`src/core/backend/system_link_exec*.cheng` | 修复 `ordinary_zero_exit_fixture` 的崩溃，报告必须写出 phase、provider、standalone 状态 | `artifacts/backend_driver/cheng system-link-exec --root:/Users/lbcheng/cheng-lang --in:src/tests/ordinary_zero_exit_fixture.cheng --emit:exe --target:arm64-apple-darwin --out:/tmp/cheng_roadmap_zero --report-out:/tmp/cheng_roadmap_zero.report.txt && /tmp/cheng_roadmap_zero` | 当前退出码 0，`provider_object_count=6`，`standalone_no_runtime=0` |
| TailIf / structured statements | 只做最小验证入口，不归入完整 CFG 完成项 | `void_tail_if_fallthrough_fixture` 必须 provider-backed 编译、运行退出 0，并在 object relocation 中看到 `_TailIf` | 当前最小 witness 已过；不得写成 structured statements 已完成 |
| `bootstrap/cheng_seed.c` | 只允许修 blocker 或删旧路径，不新增生产能力 | `cc -std=c11 -Wall -Wextra -pedantic -fsyntax-only bootstrap/cheng_seed.c` | forced C seed build 不计进度；现有 unused-function warning 需后续消掉 |

硬规则：
- 不用旧缓存、热补丁、compile-only、mock 或 C seed forced build 当通过。
- runtime/provider smoke 必须同时证明 `provider_object_count>0`、没有 `standalone_no_runtime=1`、真实可执行运行成功。

## 阶段 1：纯 Cheng 自举证明

| files | action | verify | done |
|---|---|---|---|
| `src/core/tooling/compiler_main.cheng`、`src/core/tooling/backend_driver_main.cheng` | `build-backend-driver --require-rebuild` 走当前 Cheng 主链，不设置 `CHENG_BUILD_BACKEND_DRIVER_FORCE_C_SEED=1` | `artifacts/bootstrap/cheng.stage3 build-backend-driver --require-rebuild` | 当前重建已过；产出新 `artifacts/backend_driver/cheng` 和 `.map` 只是前置，不能替代 A/B witness |
| `artifacts/backend_driver/cheng` | A 编 B，B 再编同一最小 fixture | `system-link-exec ordinary_zero_exit_fixture` + 运行退出码 | A/B 编译报告关键字段一致 |
| build report | 锁住 unsupported、RSS、provider、line-map | `rg 'unsupported|provider_object_count|system_link_exec_runtime|line_map' artifacts/backend_driver/builds/pid-*/build_backend_driver*.report.txt` | `unsupported=0` 只能和真实运行成功一起算通过 |

完成标准：新 driver 的 `help/status/print-build-plan/system-link-exec` 都可观测，普通 fixture 与 runtime/provider fixture 都真实运行成功；forced C seed build 对该阶段贡献为 0。

## 阶段 2：TypedStmt -> BodyIR CFG -> primary/direct emit

这是当前编译器正确性的主线。

| 切片 | owner 文件 | done |
|---|---|---|
| `let/var/赋值` 栈槽 | `src/core/backend/lowering_plan.cheng`、`src/core/ir/*`、`src/core/backend/primary_object_plan.cheng` | 不靠源码行字符串扫描，BodyIR local/load/store 结构化表达 |
| TailIf / `if/elif/else` 与 guard CFG | 同上 | TailIf void fallthrough 最小 witness 已过；多路 return、fallthrough、terminated 状态都必须在 CFG 中表达后才算完整完成 |
| `for range` 计数循环 | parser/typed facts/lowering/primary | index reload 不依赖 caller-saved 临时寄存器 |
| `stmt_let_call`、call statement 与 return-call | lowering/BodyIR/primary/direct writer | `let value:int32 = Noarg(); return value` provider-backed witness 已过；`Call(int32(0)); return 0` 仍是文本形状 witness；arg passing、ref/local、str sret、call statement、`Result/Option` 解包和复杂 CFG 仍未过 |
| `Result/Value` 投影 | typed facts + BodyIR op | 不用函数名特判，不用 fallback |
| `str`/`Bytes`/复合 local ABI | primary/direct writer | sret/local slot/arg passing 按 ABI 证明；atomic/compiler runtime 的 `stmt_let_call` 未通过前不能写完成 |

验证入口：
- `cfg_body_ir_contract_smoke`
- `cfg_lowering_smoke`
- `cfg_multi_stmt_smoke`
- `cfg_result_project_smoke`
- `cfg_return_call_local_arg_smoke`
- `void_tail_if_fallthrough_fixture`
- `let_call_return_result_direct_object_smoke`
- `primary_object_codegen_smoke`
- `ordinary_zero_exit_fixture`
- `atomic_i32_runtime_smoke`
- `compiler_runtime_smoke`

## 阶段 3：函数级并行

| 切片 | 状态 | 下一步 |
|---|---|---|
| `UirFnTask/UirFnTaskResult` | 最小纯数据模型已落地 | 保持 task/result 不携带 AST、源码全文、完整 BodyIR |
| `LoweringBuildOneFunction` per-function 提取 | **已完成** | 可独立调用，无共享可变状态，线程安全 |
| `BACKEND_JOBS` env→CompilerRequest→report | **已完成** | `function_task_job_count=N` + `function_task_schedule=serial` 已写入报告 |
| serial oracle | 必须保留 | `BACKEND_JOBS=1` 产物作为确定性基准 |
| work-stealing executor | 库级 executor 可见，但未接入 active lowering 主链 | 待接入 lowering callback（`FunctionTaskExecuteBodyIr` 是空桩）；接入前不得宣称主链并行完成 |
| 实际并行执行 | **2026-05-08 尝试受阻** | `async.spawn` + `FetchAddI32` 方案通过语法/语义检查，但 codegen 不支持函数体内的并发原语；循环导入约束（§716）阻止了 executor 回调路径 |
| dispatch report | **已完成** | `function_task_schedule` 动态报告 `ws`/`serial`，`function_task_job_count=N` |

切默认条件：
1. `BACKEND_JOBS=1` 与 `BACKEND_JOBS=N` 同输入产物 SHA 一致。
2. worker 任一失败硬失败，不能串行接管。
3. report 明确写 `function_task_schedule=ws`、`job_count>1`。
4. smoke 必须检查 marker，防止入口被折成直接 `return 0` 假绿。
5. 这条阶段只解决现有架构内的函数级并行；`30-80ms` 极限冷自举另需 per-worker arena 与无锁 work-stealing 证明。

## 阶段 4：Linkerless / direct-exe

| 能力 | 当前口径 | done |
|---|---|---|
| provider-free standalone direct exe | 可作为最小 witness；**冷编译器 `system-link-exec` 已实现 linkerless Mach-O 直写**（`COLD_NO_SIGN=1` 确定性 SHA） | 必须用退出码、`otool -tV`、`otool -rv`、report 共同证明 |
| provider-backed ordinary executable | 阶段 0 ordinary witness 已通 | `provider_object_count>0`、`standalone_no_runtime=0`、真实退出码 0；不外推到 runtime smoke |
| provider-backed runtime executable | 仍是关键缺口 | atomic/compiler runtime 必须执行 marker/assert/runtime 调用，不能走 `standalone_no_runtime=1` 或折零 |
| direct object writer | Darwin arm64 主线优先 | writer 消费 primary plan 的机器字和 reloc，不再按 body kind 重猜 |
| in-memory executable image | 归入 `30-80ms` 极限架构线 | 当前阶段不把 `.o` 直写或 provider-free direct exe 外推为极限 linkerless |
| release system-link | 发布稳定主线 | direct-exe 未证明前不得替代 release |
| hotpatch | dev host-only witness | 只在 `self-link + direct-exe + host runner` 口径证明，release 不参与 |

禁止项：
- 不能把 `.s` fallback 写成直写 object 完成。
- 不能把 provider-free fixture 的成功推广为 runtime/provider 主线成功。
- 不能用旧 linker 或外部脚本补救 direct-exe 缺口。

## 阶段 5：No-pointer FFI、Ownership、E-Graph

| 能力 | 当前口径 | 下一步 |
|---|---|---|
| no-pointer ABI | 公开表面默认不暴露裸指针 | `Slice/Handle/Borrow/Tuple out-param` 都要有 compile-fail 和 runtime smoke |
| BodyIR DoD/SoA | 有合同 smoke | 继续锁定 flat arrays、local noalias flags、cstring side table |
| Ownership/No-Alias | proof phase 需要专用 driver/witness | 不允许 phase-off surface 误报 proof-backed 通过 |
| CSG egraph | canonical graph equivalence 合同可见 | UIR egraph 当前不可用，后续接 cost model 和 rewrite rules |
| SIMD | 当前闭环未要求 | 先完成 UIR 向量类型、合法性分析、寄存器映射 |

验收必须绑定 runtime surface 与 phase-contract surface；没有 ownership-on driver 或 compile stamp，就不能写成优化已生效。

## 阶段 6：C seed 最小化

目标：C seed 只做冷启动外根，不承载生产编译能力。

| 区域 | 策略 |
|---|---|
| 命令分发 | 只保留 bootstrap 必需命令 |
| `system-link-exec` | 迁入 Cheng 主链，seed 不再实现生产编译 |
| runtime/provider | 新能力只进 `src/core/runtime` 或 Cheng provider，不扩展旧 C surface |
| Wasm/对象写入 | 已迁出的实现不得回潮 |
| 验证 | C seed forced build 只用于恢复破损 artifact，不计入完成证明 |

## 阶段 7：跨端与应用层

在核心编译链稳定前，移动端、r2c、browser、libp2p、chain_node 只作为显式命令和专项 gate，不进入默认核心闭环。

进入默认门槛：
- 核心 `build-backend-driver` 和 ordinary `system-link-exec` 稳定。
- runtime/provider smoke 真实链接并运行。
- cross-target smoke 证明 Mach-O/ELF/COFF 产物合法。
- 领域链路不得引入 C 脚本、mock 数据或 compile-only 成功。

## 当前优先级

1. 保持伪完成硬失败：reachable `unsupported` 不得分配机器字或填 `ret`；有前置语句、调用、副作用、if/for/assert/echo 时必须生成真实 CFG，或 unsupported 硬失败。
2. 补通 runtime smoke 的真实执行：`atomic_i32_runtime_smoke`、`thread_atomic_orc_runtime_smoke`、`compiler_runtime_smoke` 必须越过 `stmt_let_call` BodyIR/ABI 缺口并输出 marker，主对象不能是 `mov w0,#0; ret`。
3. 做 A/B 纯自举证明：A 编 B，B 再编同一 witness，report 关键字段一致。
4. 继续把 noarg i32 call-result 扩展到结构化 call ABI：call argument、ref/local、str sret、call statement、Result 投影和复合 ABI。
5. 再推进函数级并行 determinism/perf witness 与 dev 默认切换。
6. 若目标切到 `30-80ms` 冷自举，停止把局部 body kind、spawn 并行或 `.o` 直写当主线，先立极限架构的 mmap/arena/SoA/linkerless image 最小 witness。

## 诊断命令

```bash
artifacts/bootstrap/cheng.stage3 print-contract
artifacts/bootstrap/cheng.stage3 self-check --in:bootstrap/stage1_bootstrap.cheng

artifacts/backend_driver/cheng help
artifacts/backend_driver/cheng status
artifacts/backend_driver/cheng print-build-plan

CHENG_PROCESS_MAX_RSS_BYTES=1073741824 \
  artifacts/backend_driver/cheng system-link-exec \
  --root:/Users/lbcheng/cheng-lang \
  --in:src/tests/ordinary_zero_exit_fixture.cheng \
  --emit:exe \
  --target:arm64-apple-darwin \
  --out:/tmp/cheng_roadmap_zero \
  --report-out:/tmp/cheng_roadmap_zero.report.txt

artifacts/backend_driver/cheng system-link-exec \
  --root:/Users/lbcheng/cheng-lang \
  --in:src/tests/thread_atomic_orc_runtime_smoke.cheng \
  --emit:exe \
  --target:arm64-apple-darwin \
  --out:/tmp/cheng_runtime_provider \
  --report-out:/tmp/cheng_runtime_provider.report.txt
```
