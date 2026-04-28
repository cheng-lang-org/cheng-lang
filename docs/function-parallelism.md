# Darwin arm64 函数级并行任务矩阵

## 当前事实
- 真实函数级 `ws` 主路径尚未完成。
- 当前已经落地的是纯数据函数任务计划的起点：`serial_task_plan + job_count=1`。它用于锁住输出不变，并给后续 work-stealing executor 留接入口。
- 当前源码里可见的任务模型是 `src/core/ir/function_task.cheng` 的 `FunctionTask/FunctionTaskResult/FunctionTaskPlan`，执行器是 `src/core/ir/function_task_executor.cheng` 的串行 runner。
- 当前 primary codegen 报告由 `src/core/backend/primary_object_plan.cheng` 输出 `primary_object_function_task_schedule`、`primary_object_function_task_job_count`、`primary_object_function_task_count`。
- `UirFnTask/UirFnTaskResult` 的最小纯数据模型已落地；`UirCoreSharedSnapshot`、`DirectExePlan` 仍是终局接口名和任务切片，不代表当前已经实现。

## 终局合同
- dev 与 release 轨最终都走真实函数级 `ws`；切换必须一次性通过 determinism gate、perf witness、失败语义 gate 和 release/system-link 验证。
- `BACKEND_JOBS` 继续作为唯一公开 worker 数控制面。不得新增第二套公开调度环境变量。
- `serial` 只保留给内部 bring-up、诊断、低内存工具链和 perf 对照；`ws` 出错必须硬失败，不能让 `serial` 接管并算成功。
- 并行执行只允许改变执行时序，不能改变 `.o`、`.exe`、report、profile 的确定性语义。
- 函数任务只保存函数索引、声明序、源码 span、成本提示、泛型 epoch 等标量元数据；不能把 `BodyIR` 或完整函数体复制进每个 task。

## 终局数据模型
- `UirFnTask`：`func_index`、`decl_ordinal`、`origin_file`、`body_tok_start`、`body_tok_end`、`cost_hint`、`generic_epoch`、`entry_flag`、`owner_module_path`。
- `UirFnTaskResult`：`func_index`、`decl_ordinal`、`lowered_func_id`、`machine_func_id`、`cstrings`、`obj_alias_updates`、`generic_requests`、`diag_bundle`、`profile_counters`、`panic_state`。
- `UirCoreSharedSnapshot`：冻结后的函数签名、全局符号、类型布局、对象布局、overload 表、entry path、import edge、target ABI 表。worker 只读访问。
- `DirectExePlan`：有序 `function_chunks`、`reloc_patches`、`cstrings`、`globals`、`imports`、`section_layout`。布局串行确定，函数 text/reloc patch 可并发回填，最终 Mach-O 组装串行。

## 任务矩阵
| 切片 | 状态 | owner 文件 | action | verify 命令 | 完成标准 | 不可做事项 |
| --- | --- | --- | --- | --- | --- | --- |
| 基线事实锁定 | 当前已存在 | `src/core/backend/primary_object_plan.cheng`、`src/core/ir/function_task.cheng`、`src/core/ir/function_task_executor.cheng` | 保持 `serial_task_plan + job_count=1` 报告可见，作为后续并行前的确定性基线。 | `rg -n 'serial_task_plan|FunctionTaskExecuteSerial|function_task_job_count' src/core/backend/primary_object_plan.cheng src/core/ir/function_task*.cheng` | 报告仍显示 serial 计划与单 job；文档不宣称真实 `ws` 已完成。 | 不得把 `serial_task_plan` 改名成 `ws`；不得只改 report 文案伪装并行。 |
| `UirFnTask`/`UirFnTaskResult` 合同 | 最小模型已落地 | `src/core/ir/function_task.cheng`、`src/tests/function_task_contract_smoke.cheng` | 已新增 UIR 专用任务/结果/只读快照数据模型；task 只保存标量索引、声明序、source span、成本、泛型 epoch、entry flag 和 owner module path；result 只返回 lowered/machine/artifact/cstring/alias/generic/diag/profile/panic 的标量 ID 或计数，当前仍保持 `serial + job_count=1` 合同报告。 | `artifacts/backend_driver/cheng system-link-exec --root:/Users/lbcheng/cheng-lang --in:src/tests/function_task_contract_smoke.cheng --emit:exe --target:arm64-apple-darwin --out:/tmp/function_task_contract_smoke && /tmp/function_task_contract_smoke` | `UirFnTask`/`UirFnTaskResult` 不含 `BodyIR`、AST、函数体文本或共享可变全局表；smoke 断言 serial、单 job、任务字段和 result 请求字段。 | 不得在 task/result 内复制函数体、AST、完整 `BodyIR` 或共享可变数组；不得把该切片写成 `ws` 完成。 |
| 只读快照 | 合同与 serial task-plan materializer 已落地，构建接入待实现 | `src/core/backend/lowering_plan.cheng`、`src/core/lang/typed_expr.cheng`、`src/core/ir/function_task.cheng` | `UirCoreSharedSnapshot` 只保存函数签名/符号/source span/owner module/import/ABI 等标量或字符串数组，合同报告固定 `body_ir_payload_count=0`、`mutable_payload_count=0`；`UirFnTaskPlanFromSnapshot` 已能从快照稳定生成 serial 标量 task plan。下一步从 `collect_globals + bind_imports + predeclare_top_level` 真实冻结生成。 | `artifacts/backend_driver/cheng system-link-exec --root:/Users/lbcheng/cheng-lang --in:src/tests/function_task_contract_smoke.cheng --emit:exe --target:arm64-apple-darwin --out:/tmp/function_task_contract_smoke && /tmp/function_task_contract_smoke` | 快照数组长度必须对齐；worker 只能消费 frozen snapshot 和标量 task，不能携带 `BodyIR`、AST 或共享可变表。 | 不得让 worker 直接改 AST SoA、全局 symbol table、type table 或 object layout。 |
| 串行 executor 作为 oracle | 当前已存在，需继续守住 | `src/core/ir/function_task_executor.cheng`、`src/core/backend/primary_object_plan.cheng` | `BACKEND_JOBS=1` 路径保持与旧 inline serial codegen 字节一致；primary codegen 已改为 task-local words 后按声明序 merge。 | `BACKEND_JOBS=1 artifacts/backend_driver/cheng system-link-exec --root:/Users/lbcheng/cheng-lang --in:src/tests/ordinary_zero_exit_fixture.cheng --emit:exe --target:arm64-apple-darwin --out:/tmp/cheng_fn_serial_oracle --report-out:/tmp/cheng_fn_serial_oracle.report.txt && rg -n 'primary_object_function_task_schedule|primary_object_function_task_job_count' /tmp/cheng_fn_serial_oracle.report.txt` | 单 job 仍是可复现 oracle；后续 `ws` 对照必须与它 SHA 一致。 | 不得把 oracle 作为用户侧成功路径接管 `ws` 失败。 |
| work-stealing executor | 阻塞：缺真实线程/原子/跨线程 ORC | `src/core/ir/function_task_executor.cheng`、`src/std/sync.cheng`、必要时 `src/core/runtime/*provider*.cheng` | 纯 Cheng 任务执行器：按 `cost_hint/body_tok_span` 稳定 seed，本地 LIFO，跨 worker FIFO steal，结果按 `decl_ordinal` merge；当前 `jobs>1` 只能 hard-fail 为 `function_task_worker_spawn_unavailable`。 | 新增 `src/tests/function_task_ws_determinism_smoke.cheng` 后跑 `BACKEND_JOBS=1 ...` 与 `BACKEND_JOBS=4 ...` 两次 system-link-exec，并比较 report 与产物 SHA。 | `function_task_schedule=ws`、`job_count>1` 只在真实 executor 生效后出现；worker panic、queue 损坏、merge 冲突全部硬失败。 | 不得新增 C helper；不得用全局大锁、协作 spawn 或单线程队列冒充并行；不得吞掉 worker 错误。 |
| 泛型 fixed-point 调度 | 待实现 | `src/core/backend/lowering_plan.cheng`、`src/core/lang/typed_expr.cheng`、`src/core/ir/function_task.cheng` | worker 只产出 `GenericInstanceKey` 请求；主线程按稳定 key 去重、建新 task，推进 fixed-point。 | 新增泛型跨文件 fixture 后跑 jobs=1/jobs=N determinism 对照。 | 泛型实例顺序不依赖 worker 完成顺序；重复实例只生成一次。 | 不得在 worker 内直接扩展全局实例表。 |
| determinism gate | 待实现 | `src/core/tooling/gate_main.cheng`、`src/core/tooling/perf_gate.cheng`、`src/core/backend/primary_object_plan.cheng` | 增加 `BACKEND_JOBS=1`、默认 jobs、扰动 `TMPDIR/TZ` 的 `.o/.exe/report` 对照。 | `artifacts/bootstrap/cheng.stage3 verify_backend_determinism_strict` 与 `artifacts/bootstrap/cheng.stage3 verify_backend_exe_determinism_strict`，若当前命令面未暴露则先补 gate 再切主线。 | SHA、report 关键字段、reloc 顺序、cstring/global 排布全部一致。 | 不得只比较退出码；不得跳过 report 和 reloc/cstring 排布。 |
| perf witness | 待实现 | `src/core/tooling/perf_gate.cheng`、`src/core/tooling/backend_driver_dispatch_min.cheng`、profile schema/baseline 相关文件 | 记录 `fn_tasks.enqueue/run/steal/wait/merge`、`isel_tasks`、`direct_exe_layout/fill`，生成专用主机 witness。 | `artifacts/bootstrap/cheng.stage3 verify_backend_selfhost_parallel_perf`，并产出 `artifacts/backend_selfhost_self_obj/selfhost_parallel_perf_dedicated_witness.tsv`。 | witness 含 `serial` 与 `ws` 同输入对照、jobs 数、RSS、wall time、task count、steal count、merge time；缺项即失败。 | 不得用旧缓存产物、单次偶然快照或无 RSS 数据作为发布依据。 |
| dev 切换条件 | 待实现 | `src/core/tooling/backend_driver_dispatch_min.cheng`、`src/core/tooling/compiler_request.cheng`、`src/core/tooling/README.md`、`docs/cheng-formal-spec.md` | 仅当 `UirFnTask`、`ws executor`、determinism gate、perf witness 都通过后，dev 默认才可切 `ws`。 | `artifacts/backend_driver/cheng print-build-plan` 必须暴露 `fn_parallel_contract.schedule=ws`；再跑 jobs=1/jobs=N 的 `ordinary_zero_exit_fixture`、`primary_object_codegen_smoke`、`program_selfhost_smoke`。 | dev host-only compile-stage1 witness 可复现；默认 jobs 与 jobs=1 产物一致。 | 不得通过文档或 env 默认先行宣布 dev 已切 `ws`。 |
| release 切换条件 | 待实现 | `src/core/tooling/backend_driver_dispatch_min.cheng`、`src/core/backend/system_link_exec.cheng`、`src/core/backend/direct_object_emit.cheng`、`src/core/backend/primary_object_emit.cheng` | release/system-link 在 dev 条件满足后再接入同一 task 模型；object-first 稳定链先保持。 | `BACKEND_JOBS=1` 与默认 jobs 分别跑 `system-link-exec --emit:exe`，再比较产物 SHA、`otool -rv`、实际退出码。 | `release-compile` 等价入口的 `.o/.exe` 字节稳定；native link、reloc、map/report 不漂移。 | 不得在 direct object writer 里重新猜 call/reloc；不得绕开 primary plan 合同。 |
| direct-exe 函数块计划 | 待实现 | `src/core/backend/direct_object_emit.cheng`、`src/core/backend/primary_object_emit.cheng`、`src/core/backend/primary_object_plan.cheng` | 先串行确定 `DirectExePlan` 布局，再并发填函数 text 和 reloc patch，最后串行写 Mach-O。 | `otool -tV /tmp/cheng_fn_ws_exe`、`otool -rv /tmp/cheng_fn_ws_exe`、实际运行退出码；jobs=1/jobs=N SHA 对照。 | 函数块顺序、reloc offset、cstring/data label 顺序稳定；direct-exe 不再吃单块 `mainObjBytes` 占位输入。 | 不得让 writer 按 body kind 二次计算 relocation；不得用 link-only 或 compile-only 当成功。 |

## dev/release 切换门槛
- dev 默认切 `ws` 前必须同时满足：`UirFnTask` 不复制大对象、`UirCoreSharedSnapshot` 只读、jobs=1/jobs=N determinism 通过、worker 失败硬失败、perf witness 有效。
- release 默认切 `ws` 前必须额外满足：dev 条件全部通过，同一 revision 下 object-first/system-link 产物 SHA 一致，`otool -rv` relocation 顺序一致，真实 executable 退出码一致。
- `print-build-plan`、formal spec、tooling README 只能在上述门槛满足后同步写 `ws` 默认合同。在此之前只能写“目标态”。

## 本 worker 本次边界
- 只更新 `docs/函数级并行.md` 与 `docs/cheng-plan-full.md`。
- 不修改 `src/core/backend/primary_object_plan.cheng`、`src/core/backend/direct_object_emit.cheng`、`src/core/backend/primary_object_emit.cheng` 或任何 writer 主链。
- 不新增 mock、不新增 C 源、不新增脚本成功路径。
