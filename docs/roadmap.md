# Cheng 工业级路线图

> 口径：只记录当前仓库能证明或必须硬证明的状态。愿景可以写目标，不写成完成。若与实现冲突，以 `docs/cheng-formal-spec.md`、`src/core/tooling/README.md`、当前源码和当前可执行产物为准。

## 当前事实（2026-05-02）

### 已成立

- `artifacts/bootstrap/cheng.stage3` 可用，`self-check --in:bootstrap/stage1_bootstrap.cheng` 通过。
- bootstrap v2 合同仍只暴露：`print-contract,self-check,compile-bootstrap,bootstrap-bridge,build-backend-driver`。
- `src/core/tooling` 是当前命令面、bootstrap bridge、backend driver、gate 与发布闭环的唯一源码位置。
- Wasm 后端核心源码已迁入 `src/core/backend/wasm_module_emit.cheng`，不再把 Wasm 主实现继续堆进 C seed。
- 当前安装的 `artifacts/backend_driver/cheng` 命令面已恢复可观测：`help` 输出 usage，`status --root --in --out` 写出 argv/CFG 字段，`print-build-plan` 可运行。
- `ordinary_zero_exit_fixture` 已能通过当前 backend driver 的 provider-backed `system-link-exec` 编译并运行退出 0；报告写出 `system_link_exec_runtime_standalone_no_runtime=0`、`provider_object_count=6`、`object_link_input_count=7`。
- `void_tail_if_fallthrough_fixture` 已覆盖真实 BodyIR call：主对象有 `_TailIf` BR26 relocation，main 保存/恢复 `x30` 与 `sp`，可执行运行退出 0。
- `artifacts/bootstrap/cheng.stage3 build-backend-driver --require-rebuild` 在未设置 `CHENG_BUILD_BACKEND_DRIVER_FORCE_C_SEED=1` 时可刷新并安装 driver；这还不是 A/B 纯 Cheng 自举完成证明。
- `TypedStmt -> BodyIR CFG -> primary/direct emit` 是当前核心缺口；路线图不得再用入口特判、源码字符串扫描或旧 artifact 热补丁伪装完成。
- 函数任务数据模型与执行器源码已存在：`src/core/ir/function_task.cheng`、`src/core/ir/function_task_executor.cheng`。`BACKEND_JOBS=1` 仍是串行 oracle，`BACKEND_JOBS>1` 必须用产物 SHA、report、marker 和真实 worker 错误传播证明。

### 当前阻断

- 复杂 runtime smoke 仍是假绿：`atomic_i32_runtime_smoke` 与 `compiler_runtime_smoke` 已 provider-linked，但主函数仍会被降成 `mov w0,#0; ret`，没有执行 assert/echo/runtime 调用，不能当线程/原子/ORC runtime 通过。
- BodyIR call argument、Result 投影、复杂 statement CFG 仍未完整证明；当前只证明了无参/未用参 call 的 relocation 与返回帧恢复。
- 当前 driver 可用不等于纯自举完成；还缺 A 编 B、B 再编同一 witness、关键 report 字段一致的闭环。
- roadmap 不能宣称“纯 Cheng 自举完成”“work-stealing 默认可用”“100ms 已达成”。
- `docs/function-parallelism.md` 明确写着：真实 `ws` 源码路径已接入，但当前安装 artifact 尚未形成可验收证明。
- `compiler_csg` 侧已有 canonical egraph 合同；UIR egraph 当前仍是 unavailable，不能写成后端优化已接入。

## 总目标

Cheng 的工业路线不是和 LLVM/mold 在传统资源赛道硬拼，而是用三条工程主线降维：

| 主线 | 目标 | 当前完成口径 |
|---|---|---|
| Dev 轨 | `self-link + direct-exe + host runner hotpatch`，最终冷反馈进入 100ms 口径 | 只在专用 witness 证明后才可写成达成 |
| Release 轨 | `UIR -> .o -> system-link` 稳定闭环 | 当前仍是发布主线 |
| 语义特化 | Ownership/No-Alias、BodyIR DoD/SoA、No-pointer FFI、E-Graph | 合同与 smoke 分段推进，不能用 compile-only 通过 |

## 阶段 0：修活当前 backend driver

| files | action | verify | done |
|---|---|---|---|
| `src/core/tooling/backend_driver_main.cheng`、`src/core/tooling/backend_driver_dispatch_min.cheng`、`src/core/tooling/compiler_request.cheng` | 恢复 `help/status/print-build-plan/system-link-exec` 的可观测命令面 | `artifacts/backend_driver/cheng help`、`status --root --in --out`、`print-build-plan` 必须输出固定字段 | 当前已过；以后静默退出视为失败 |
| `src/core/backend/system_link_plan.cheng`、`src/core/backend/primary_object_plan.cheng`、`src/core/backend/system_link_exec*.cheng` | 修复 `ordinary_zero_exit_fixture` 的崩溃，报告必须写出 phase、provider、standalone 状态 | `artifacts/backend_driver/cheng system-link-exec --root:/Users/lbcheng/cheng-lang --in:src/tests/ordinary_zero_exit_fixture.cheng --emit:exe --target:arm64-apple-darwin --out:/tmp/cheng_roadmap_zero --report-out:/tmp/cheng_roadmap_zero.report.txt && /tmp/cheng_roadmap_zero` | 当前退出码 0，`provider_object_count=6`，`standalone_no_runtime=0` |
| `bootstrap/cheng_seed.c` | 只允许修 blocker 或删旧路径，不新增生产能力 | `cc -std=c11 -Wall -Wextra -pedantic -fsyntax-only bootstrap/cheng_seed.c` | 不得把 C seed forced build 算作完成；现有 unused-function warning 需后续消掉 |

硬规则：
- 不用旧缓存、热补丁、compile-only、mock 或 C seed forced build 当通过。
- runtime/provider smoke 必须同时证明 `provider_object_count>0`、没有 `standalone_no_runtime=1`、真实可执行运行成功。

## 阶段 1：纯 Cheng 自举证明

| files | action | verify | done |
|---|---|---|---|
| `src/core/tooling/compiler_main.cheng`、`src/core/tooling/backend_driver_main.cheng` | `build-backend-driver --require-rebuild` 走当前 Cheng 主链，不设置 `CHENG_BUILD_BACKEND_DRIVER_FORCE_C_SEED=1` | `artifacts/bootstrap/cheng.stage3 build-backend-driver --require-rebuild` | 产出新 `artifacts/backend_driver/cheng` 和 `.map` |
| `artifacts/backend_driver/cheng` | A 编 B，B 再编同一最小 fixture | `system-link-exec ordinary_zero_exit_fixture` + 运行退出码 | A/B 编译报告关键字段一致 |
| build report | 锁住 unsupported、RSS、provider、line-map | `rg 'unsupported|provider_object_count|system_link_exec_runtime|line_map' artifacts/backend_driver/builds/pid-*/build_backend_driver*.report.txt` | `unsupported=0` 只能和真实运行成功一起算通过 |

完成标准：新 driver 的 `help/status/print-build-plan/system-link-exec` 都可观测，普通 fixture 与 runtime/provider fixture 都真实运行成功。

## 阶段 2：TypedStmt -> BodyIR CFG -> primary/direct emit

这是当前编译器正确性的主线。

| 切片 | owner 文件 | done |
|---|---|---|
| `let/var/赋值` 栈槽 | `src/core/backend/lowering_plan.cheng`、`src/core/ir/*`、`src/core/backend/primary_object_plan.cheng` | 不靠源码行字符串扫描，BodyIR local/load/store 结构化表达 |
| `if/elif/else` 与 guard CFG | 同上 | 多路 return、fallthrough、terminated 状态都在 CFG 中表达 |
| `for range` 计数循环 | parser/typed facts/lowering/primary | index reload 不依赖 caller-saved 临时寄存器 |
| call statement 与 return-call | lowering/BodyIR/primary/direct writer | `BodyOpCallTag` 基础 relocation 已过；下一步证明 arg passing、call result、Result/Option 解包和复杂 CFG 不折零 |
| `Result/Value` 投影 | typed facts + BodyIR op | 不用函数名特判，不用 fallback |
| `str`/`Bytes`/复合 local ABI | primary/direct writer | sret/local slot/arg passing 按 ABI 证明 |

验证入口：
- `cfg_body_ir_contract_smoke`
- `cfg_lowering_smoke`
- `cfg_multi_stmt_smoke`
- `cfg_result_project_smoke`
- `primary_object_codegen_smoke`
- `ordinary_zero_exit_fixture`

## 阶段 3：函数级并行

当前只允许写成“源码路径与合同推进中”，不能写成默认完成。

| 切片 | 状态 | 下一步 |
|---|---|---|
| `UirFnTask/UirFnTaskResult` | 最小纯数据模型已落地 | 保持 task/result 不携带 AST、源码全文、完整 BodyIR |
| `UirCoreSharedSnapshot` | 合同与 serial task-plan materializer 已落地，构建接入待完成 | 从真实 typed/lowering 上下文冻结只读快照 |
| serial oracle | 必须保留 | `BACKEND_JOBS=1` 产物作为确定性基准 |
| work-stealing executor | 源码已接入，artifact 未证明 | jobs=1/jobs=4 对比 `.o/.exe/report` SHA、marker、错误传播 |
| determinism gate | 待实现/待接主线 | 比较 report、reloc、cstring、global 排布，不只看退出码 |
| perf witness | 待实现 | 写出 serial/ws 同输入对照、RSS、wall time、task/steal/wait/merge 计数 |

切默认条件：
1. `BACKEND_JOBS=1` 与 `BACKEND_JOBS=N` 同输入产物 SHA 一致。
2. worker 任一失败硬失败，不能串行接管。
3. report 明确写 `function_task_schedule=ws`、`job_count>1`。
4. smoke 必须检查 marker，防止入口被折成直接 `return 0` 假绿。

## 阶段 4：Linkerless / direct-exe

| 能力 | 当前口径 | done |
|---|---|---|
| provider-free standalone direct exe | 可作为最小 witness | 必须用退出码、`otool -tV`、`otool -rv`、report 共同证明 |
| provider-backed executable | 仍是关键缺口 | `provider_object_count>0`，不能走 `standalone_no_runtime=1` |
| direct object writer | Darwin arm64 主线优先 | writer 消费 primary plan 的机器字和 reloc，不再按 body kind 重猜 |
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

1. 禁止复杂主体假降级为 `return_zero_i32`：有前置语句、调用、副作用、if/for/assert/echo 时必须生成真实 CFG，或 unsupported 硬失败。
2. 补通 runtime smoke 的真实执行：`atomic_i32_runtime_smoke`、`thread_atomic_orc_runtime_smoke`、`compiler_runtime_smoke` 必须输出 marker，主对象不能是 `mov w0,#0; ret`。
3. 做 A/B 纯自举证明：A 编 B，B 再编同一 witness，report 关键字段一致。
4. 继续补 `TypedStmt -> BodyIR CFG -> primary/direct emit` 的结构化 IR 缺口，尤其是 call argument、call result、Result 投影和复合 ABI。
5. 再推进函数级并行 determinism/perf witness 与 dev 默认切换。

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
