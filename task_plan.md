## 当前任务

| 项目 | 内容 |
|---|---|
| 目标 | 把 `v3` linkerless dev 轨从“只有文档边界”推进到“ordinary compile 真产出源码 line-map，panic/assert/bounds 真能反查回 `.cheng` 文件和行号”。 |
| 主线 | 这轮直接在 `v3/bootstrap/cheng_v3_seed.c` 里给 lowered function 补 `signature/body span`，并在成功链接后写出 `<out>.v3.map` sidecar；运行时侧不新造 provider 模块，而是复用已经在 ordinary compile 里链接的 `src/runtime/native/system_helpers.c`，让 `v3/runtime/native/{v3_program_argv_native.c,v3_tooling_argv_native.c}` 启动时自动注册 line-map。随后把 `panic/assert` 从裸 `puts + _exit` 收成统一走 `cheng_v3_panic_cstring_and_exit`，并补 `ordinary_panic_fixture + build_panic_trace_v3.sh` 做真验收。 |
| 文件 | `v3/bootstrap/cheng_v3_seed.c` `src/runtime/native/system_helpers.c` `v3/runtime/native/v3_program_argv_native.c` `v3/runtime/native/v3_tooling_argv_native.c` `v3/src/tests/ordinary_panic_fixture.cheng` `v3/tooling/build_panic_trace_v3.sh` `v3/tooling/build_zero_exit_v3.sh` `v3/tooling/run_slice_gate.sh` `v3/tooling/README.md` `task_plan.md` `progress.md` `findings.md` |
| 不做 | 不去碰 `DWARF/dSYM` 那条外部符号轨；不在 signal handler 里塞不安全的全量符号反查；不借这轮顺手扩 `chain_node` ordinary 语义子集；不回滚 `v3/bootstrap/cheng_v3_seed.c` 里已有的用户改动。 |
| 验收 | `cc -std=c11 -O0 -Wall -Wextra -pedantic v3/bootstrap/cheng_v3_seed.c -o /tmp/cheng_v3_seed_check` 必须通过；`build_backend_driver_v3.sh`、`build_zero_exit_v3.sh`、`build_call_chain_v3.sh`、`build_panic_trace_v3.sh` 必须前台通过；`panic` fixture 的 stderr 必须出现 `ordinary_panic_fixture.cheng:2-3`。 |

| 项目 | 内容 |
|---|---|
| 目标 | 把 `v3` 文档总口径收正成可执行硬边界，直接给后续实现让路：`LSMR` 不再把稳定地址绑死绝对地理，`FFI` 句柄明确锁成 `generation:index`，debug 收成“外部符号 + linkerless 内嵌行号表”双轨，`RWAD/UniMaker` 锁死“finalized reserve 才能进 NAV、商户标价与 RWAD 结算强制解耦”。 |
| 主线 | 这轮不碰 `v3/bootstrap` 和 ordinary compile 活根，只收正 `v3/docs/LSMR.md` 与 `v3/docs/cheng语言特性矩阵和开发计划.md` 的硬边界，同时把 `task_plan/progress/findings` 同步成新口径。重点不是再加愿景，而是删掉“物理上必然接近”“未 Allocated 也能折价进 NAV”“dev 轨只靠裸地址 backtrace”这类会误导后续实现的错口径。 |
| 文件 | `v3/docs/LSMR.md` `v3/docs/cheng语言特性矩阵和开发计划.md` `task_plan.md` `progress.md` `findings.md` |
| 不做 | 不改 `bagua/BPI` sidecar-only 边界；不碰 `v3` 编译器代码；不把热点前缀扩容写成“重写顶层地址”；不把 `DWARF/dSYM` 和 linkerless panic 反查做成二选一。 |
| 验收 | `LSMR.md` 必须明确写出“稳定前缀 + 动态延迟坐标 + 桶上限”口径；总览文档必须把 `FFI/debug/RWAD/UniMaker` 的硬规则写成正式矩阵；任务记录必须同步这轮收正规则。 |

| 项目 | 内容 |
|---|---|
| 目标 | 继续把 `v3` seed ordinary compile 从“标量/指针子集”推进到 `chain_node` 真主链，这一刀先补无注解 `let/var` 的类型反推和局部槽位 `type_text`，为后面的复合 ABI 铺路。 |
| 主线 | 这轮已经在 `v3/bootstrap/cheng_v3_seed.c` 接上 `expr -> type/abi` 反推：字符串字面量、标量字面量、top-level const、本地槽位、单参 cast、普通函数调用返回类型、首字母大写的 record 构造现在都能反推出 `type_text/abi_class`；`V3AsmLocalSlot` 也开始显式记录 `type_text`，无注解 `let/var` 不再一上来就要求源码显式标类型。前台验收上，`cc -std=c11 -O0 -Wall -Wextra -pedantic v3/bootstrap/cheng_v3_seed.c`、`build_backend_driver_v3.sh`、`build_program_selfhost_v3.sh` 继续通过，`chain_node` 首个 blocker 仍稳定是 `v3ChainNodeMainSelfTest` 的 `var server = node.v3ChainNodeInit(...)`。这说明“类型反推”这层已经补上了，下一刀该直接做固定布局表和复合 ABI，而不是再在 `let/var` 注解面打转。 |
| 文件 | `v3/bootstrap/cheng_v3_seed.c` `v3/src/project/chain_node_main.cheng` `v3/src/project/chain_node.cheng` `src/std/system.cheng` `src/std/result.cheng` `src/std/rawbytes.cheng` `src/std/seqs.cheng` `task_plan.md` `progress.md` `findings.md` |
| 不做 | 不把“无注解绑定”再退回必须手写类型；不拿 `program_selfhost` 继续通过来伪装 `chain_node` 已经接通；不把当前真阻塞改写成别的名字。 |
| 验收 | `cc -std=c11 -O0 -Wall -Wextra -pedantic v3/bootstrap/cheng_v3_seed.c` 必须通过；`build_backend_driver_v3.sh`、`build_program_selfhost_v3.sh` 必须继续通过；`build_chain_node_v3.sh` 必须继续稳定卡在 `v3ChainNodeMainSelfTest stmt_var`，证明类型反推已补上但复合 ABI 还没做。 |

| 项目 | 内容 |
|---|---|
| 目标 | 继续把 `v3` seed ordinary compile 从“标量/指针子集”推进到 `chain_node` 真主链，同时先清掉会误导判断的假 blocker。 |
| 主线 | 这轮已经在 `v3/bootstrap/cheng_v3_seed.c` 接上 top-level `const` 收集与标量常量解析，`v3LsmrDigitOk`、`v3BaguaValid` 这类 `return_expr/if` 假阻塞已从 `chain_node` 的 unsupported 列表里消失；与此同时，name-only 重载解析也已收紧成“单候选才放行”，不再允许 `rawmemAsVoid` 这类重载静默错绑到第一条函数体。当前 `build_backend_driver_v3.sh`、`scan_forbidden_hotpath.sh` 继续通过，`chain_node` 的首个真实阻塞没有再漂，仍然稳定是 `chain_node_main::v3ChainNodeMainSelfTest` 的 `var server = node.v3ChainNodeInit(...)`。这说明下一刀必须正面补 `复合 ABI + 无注解 let/var + 字段读写`，而不是继续在标量叶子、常量名或重载假点上打转。 |
| 文件 | `v3/bootstrap/cheng_v3_seed.c` `v3/src/project/chain_node_main.cheng` `v3/src/project/chain_node.cheng` `v3/src/chain/binary_types.cheng` `v3/src/chain/lsmr_types.cheng` `task_plan.md` `progress.md` `findings.md` |
| 不做 | 不把 `rawmemAsVoid` 的 name-only 错绑继续留着；不回头补旧 proof/sidecar；不把 `chain_node` 的主阻塞伪装成“还是 bool/const 叶子没通”。 |
| 验收 | `cc -std=c11 -O0 -Wall -Wextra -pedantic v3/bootstrap/cheng_v3_seed.c` 必须通过；`build_backend_driver_v3.sh` 必须通过；`scan_forbidden_hotpath.sh` 必须通过；`chain_node.compile.log` 里 `v3LsmrDigitOk`、`v3BaguaValid` 不能再出现在 `primary_object_unsupported_functions`；首个 blocker 必须继续稳定停在 `v3ChainNodeMainSelfTest stmt_var`，证明真根已经收窄到复合值 ABI。 |

| 项目 | 内容 |
|---|---|
| 目标 | 把 `v3` seed ordinary compile 从“只有 typed plan”推进到“最小 ordinary 子集真发 `primary .o`、真编 provider `.o`、真链接并真跑”，同时把 `program-selfhost` 和 `chain_node` 的阻塞收正成单一函数体语义缺口。 |
| 主线 | 这轮已经在 `v3/bootstrap/cheng_v3_seed.c` 里补上 strict body-kind 识别、arm64 汇编 primary object 物化、provider `.o` 编译和真实 native link。普通 program 入口 ABI 也已经和 `v3/runtime/native/v3_program_argv_native.c` 对齐，不再错导出 `_main`。结果是 `build_backend_driver_v3.sh`、`run_v3_host_smokes.sh`、`build_zero_exit_v3.sh` 全部前台通过；`build_program_selfhost_v3.sh` 和 `build_chain_node_v3.sh` 现在统一稳定报 `v3 compiler: primary object body semantics missing`。这说明 `.o` 和链接链路已经是真的，剩下只该扩 ordinary body 语义子集。 |
| 文件 | `v3/bootstrap/cheng_v3_seed.c` `v3/runtime/native/v3_program_argv_native.c` `v3/runtime/native/v3_tooling_argv_native.c` `v3/runtime/native/v3_core_runtime_stub.c` `v3/tooling/build_zero_exit_v3.sh` `v3/tooling/build_program_selfhost_v3.sh` `v3/tooling/build_chain_node_v3.sh` `v3/tooling/run_slice_gate.sh` `v3/src/tests/ordinary_zero_exit_fixture.cheng` `v3/src/tests/primary_object_codegen_smoke.cheng` `v3/docs/cheng语言特性矩阵和开发计划.md` `v3/README.md` `v3/tooling/README.md` `task_plan.md` `progress.md` `findings.md` |
| 不做 | 不回退到 bootstrap-only 壳；不把 `zero-exit` 的真通过伪装成 `program-selfhost/chain_node` 已闭合；不再回头修已经切掉的 argv/contract/report/linker 假阻塞。 |
| 验收 | `build_backend_driver_v3.sh` 必须通过；`run_v3_host_smokes.sh` 必须全绿；`build_zero_exit_v3.sh` 必须真通过；`build_program_selfhost_v3.sh` 与 `build_chain_node_v3.sh` 必须统一稳定报 `v3 compiler: primary object body semantics missing`；文档必须明确写出当前 object/link 已接通，下一刀只扩 ordinary body 语义子集。 |

| 项目 | 内容 |
|---|---|
| 目标 | 把 `v3` bootstrap seed 的 ordinary command 面彻底收口到“真实 typed plan + 诚实阻塞”，继续拆掉 `argv/contract/report/stack` 这些假崩点，让 `program-selfhost` 和 `chain_node` 都稳定停在同一个语义缺口上。 |
| 主线 | 这轮已经把 `v3/bootstrap/cheng_v3_seed.c` 的 ordinary 命令面真正接通：`status/print-build-plan/system-link-exec` 现在都走内嵌 runtime contract，不再把普通源码 `--in` 误当 bootstrap contract；CLI 也已经同时接受 `--flag value`、`--flag:value`、`--flag=value` 三种写法，和 `v3/tooling/*.sh`、`v2/bootstrap/Makefile` 一致。随后又把 `v3_system_link_plan_report` 的固定缓冲区 `strcat` 改成动态拼接，并把 `v3_collect_source_closure` 里每层递归都压栈的 `V3PlanImportEdge[256]` 大 scratch 改成 heap scratch，`chain_node` 的段错误已经被切掉。现在 `build_backend_driver_v3.sh` 已稳定产出带 `status/print-build-plan/system-link-exec` 的 canonical backend driver，`program-selfhost` 和 `chain_node` 也都会稳定走到同一个 typed plan 阻塞：`runtime_targets_not_lowered` 与 `runtime_provider_modules_not_selected`。`run_v3_host_smokes.sh` 已继续全绿，`run_slice_gate.sh` 的唯一真失败位也已经收敛到 `program-selfhost`。 |
| 文件 | `v3/bootstrap/cheng_v3_seed.c` `v3/tooling/build_backend_driver_v3.sh` `v3/tooling/build_program_selfhost_v3.sh` `v3/tooling/build_chain_node_v3.sh` `v3/tooling/run_slice_gate.sh` `task_plan.md` `progress.md` `findings.md` |
| 不做 | 不回退到旧 proof/sidecar；不把 `runtime_targets/provider_modules` 为空伪装成“lowering 已开始”；不再让普通编译死在 `argv`、contract 装载或 report 栈溢出这种假根上。 |
| 验收 | `build_backend_driver_v3.sh` 必须继续通过；`artifacts/v3_backend_driver/cheng status/print-build-plan/system-link-exec` 必须都可调用；`build_program_selfhost_v3.sh` 和 `build_chain_node_v3.sh` 必须统一报 `runtime_targets_not_lowered/runtime_provider_modules_not_selected`，不能再出现 `missing --in`、`invalid bootstrap line` 或段错误；`run_v3_host_smokes.sh` 必须继续全绿；`run_slice_gate.sh` 必须稳定停在 `program-selfhost` 这一处真阻塞。 |

| 项目 | 内容 |
|---|---|
| 目标 | 把 `v3` ordinary compile 的最小 typed plan 真落地到 `parser -> system_link_plan -> compiler_main`，并把 gate 收严成“`build_backend_driver_v3.sh` 必须产出带 `status/print-build-plan/system-link-exec` 的 ordinary compiler”，不再把 bootstrap 壳误报成 canonical backend driver。 |
| 主线 | 这轮已经把 `v3/src/lang/parser.cheng` 扩成真实 package/workspace root、owner module、import edge、closure path、entry symbol、`missing_reasons` 的 typed parser stub；`v3/src/backend/system_link_plan.cheng` 也已改成 typed `emit/module kind/source kind`、typed closure/report、typed missing reasons，`compiler_request.cheng` 还接上了 `--root` 的 package-root 语义。`run_v3_host_smokes.sh` 现已固定在仓库根执行，`compiler_runtime_smoke`、`compiler_pipeline_stub_smoke` 和整套 host smokes 全绿。与此同时，`build_backend_driver_v3.sh` 已被收严成必须验证 built artifact 真的支持 `status` 和 `print-build-plan`；因此 `run_slice_gate.sh` 现在会更早、更诚实地死在真正主根：`artifacts/v3_backend_driver/cheng` 仍然只是 `cheng_v3_seed`/`compile-bootstrap` 壳。 |
| 文件 | `v3/src/tooling/compiler_request.cheng` `v3/src/tooling/compiler_runtime.cheng` `v3/src/tooling/compiler_main.cheng` `v3/src/lang/parser.cheng` `v3/src/backend/system_link_plan.cheng` `v3/src/backend/system_link_exec.cheng` `v3/src/tests/compiler_runtime_smoke.cheng` `v3/src/tests/compiler_pipeline_stub_smoke.cheng` `v3/tooling/run_v3_host_smokes.sh` `v3/tooling/build_backend_driver_v3.sh` `task_plan.md` `progress.md` `findings.md` |
| 不做 | 不再把 `build_backend_driver_v3.sh` 的“构建成功”当成 ordinary compiler 已经存在；不继续拿 `program-selfhost` 之后的报错掩盖更早的 `backend driver 仍是 bootstrap 壳`；不回退 `typed plan` 到字符串壳。 |
| 验收 | `scan_forbidden_hotpath.sh` 必须继续通过；`run_v3_host_smokes.sh` 必须全绿；`run_slice_gate.sh` 必须在 `canonical bootstrap compiler` 这一步直接暴露 `built output is still bootstrap-only, missing ordinary status command`，不再假装 `build_backend_driver_v3.sh` 已闭合。 |

| 项目 | 内容 |
|---|---|
| 目标 | 把 `v3` 的正式 gate 从“看 backend driver 帮助面”收严成“backend driver 真编 ordinary program 和 chain_node artifact”，并把当前真阻塞明文钉死为“`v3` 还没有自己的普通编译入口源码”。 |
| 主线 | 这轮已经新增 `v3/src/project/chain_node_main.cheng`、`v3/tooling/build_program_selfhost_v3.sh`、`v3/tooling/build_chain_node_v3.sh`，并把 `v3/tooling/run_slice_gate.sh` 改成直接调用这两条真实构建脚本。随后又把 `v3/src/tooling/{compiler_main,compiler_runtime,compiler_request}.cheng`、`v3/src/lang/parser.cheng`、`v3/src/backend/system_link_exec.cheng` 落库，`build_plan.cheng` 也已经把 `entryPath` 切到 `compiler_main.cheng`；host compiler 现已能真编这个新入口，`help/status` 正常，`system-link-exec` 也会稳定经过 `parser stub -> backend system_link_exec stub` 再报 `v3 compiler: ordinary pipeline not implemented`。当前 main root 仍然是 `build_backend_driver_v3.sh` 继续 materialize `stage1_bootstrap.cheng`。 |
| 文件 | `v3/src/project/chain_node_main.cheng` `v3/src/tooling/compiler_main.cheng` `v3/src/tooling/compiler_runtime.cheng` `v3/src/tooling/compiler_request.cheng` `v3/src/lang/parser.cheng` `v3/src/backend/system_link_exec.cheng` `v3/tooling/build_program_selfhost_v3.sh` `v3/tooling/build_chain_node_v3.sh` `v3/tooling/bootstrap_bridge_v3.sh` `v3/tooling/build_backend_driver_v3.sh` `v3/tooling/run_slice_gate.sh` `v3/docs/cheng语言特性矩阵和开发计划.md` `v3/README.md` `v3/tooling/README.md` `task_plan.md` `progress.md` `findings.md` |
| 不做 | 不再拿 `--help` 或 `supported_commands` 冒充 `program-selfhost` 已接通；不把 `stage1_bootstrap.cheng` 这种合同文件误认成普通编译器实现；不回去补旧 proof/sidecar 链。 |
| 验收 | `run_slice_gate.sh` 必须在 `program-selfhost` 阶段直接调用真实 ordinary program 构建脚本，在 `chain_node` 阶段直接调用真实 artifact 构建脚本；`compiler_main` 必须能被 host compiler 真编并稳定给出 `help/status/not implemented`；文档必须明确写出当前真阻塞是“backend driver 还在吃 bootstrap manifest，而不是 compiler_main”。 |

| 项目 | 内容 |
|---|---|
| 目标 | 承认 `src/tooling/cheng_tooling.cheng` 属于旧链后，把 `v3` 的主控、bootstrap 合同、backend build plan、性能 gate 全部落回 `v3` 目录；`run_slice_gate.sh` 只认 `v3/tooling` 的 bridge/build 入口，不再把旧总控当 `v3` 主链。 |
| 主线 | 这轮已经开始把 `v3/src/tooling/{path,host_ops,bootstrap_contracts,hotpath_scan,perf_gate,gate_main}.cheng`、`v3/src/backend/build_plan.cheng`、`v3/src/tests/{bootstrap_contracts_smoke,perf_gate_smoke}.cheng` 全部落库，并新增 `v3/tooling/{cheng_v3,bootstrap_bridge_v3,build_backend_driver_v3}.sh`。`bootstrap_bridge_v3.sh` 仍只把旧脚本当 bootstrap bring-up 外根，`build_backend_driver_v3.sh` 直接用 bootstrap driver + `chengc.sh` 编 `src/backend/tooling/backend_driver.cheng`，不再走旧总控子命令。当前还没跑通纯 Cheng `gate_main`，因为仓库现有 backend driver compile 仍会在更早位置炸掉。 |
| 文件 | `v3/src/tooling/path.cheng` `v3/src/tooling/host_ops.cheng` `v3/src/tooling/bootstrap_contracts.cheng` `v3/src/tooling/hotpath_scan.cheng` `v3/src/tooling/perf_gate.cheng` `v3/src/tooling/gate_main.cheng` `v3/src/backend/build_plan.cheng` `v3/src/tests/bootstrap_contracts_smoke.cheng` `v3/src/tests/perf_gate_smoke.cheng` `v3/tooling/cheng_v3.sh` `v3/tooling/bootstrap_bridge_v3.sh` `v3/tooling/build_backend_driver_v3.sh` `v3/tooling/run_slice_gate.sh` `v3/README.md` `v3/tooling/README.md` `task_plan.md` `progress.md` `findings.md` |
| 不做 | 不再把 `src/tooling/cheng_tooling.cheng` 视为 `v3` 入口；不把旧 sidecar wrapper/outer driver 整包搬进 `v3`；不把 seed binary 误说成 `v3` 源码主控。 |
| 验收 | `run_slice_gate.sh` 必须改成走 `v3/tooling/bootstrap_bridge_v3.sh` 和 `v3/tooling/build_backend_driver_v3.sh`；`v3/src/tooling` 必须出现真类型化合同和真 perf gate 代码；当前失败点必须继续暴露真实 backend/bootstrap 阻塞，而不是回退到旧命令分发层。 |

| 项目 | 内容 |
|---|---|
| 目标 | 清掉 `src/tooling` 和 `src/backend/tooling` 里已经确认无引用的 sidecar 死残片，同时把 `v3` 编译工具链入口改成“源码 launcher + strict bootstrap bridge”，不再默认绑死旧 binary 和缺失的 `cheng.stage2{,.proof}`。 |
| 主线 | 这轮已经做了三件事：一是删掉 32 个已跟踪的 `src/tooling/cheng_sidecar_rewrite_*.cheng`、`src/tooling/cheng_tooling_embedded_stable.cheng`、`src/backend/tooling/backend_driver_uir_sidecar_direct_build.cheng`，并清掉源码树里残留的未跟踪 sidecar 重写文件和 `.o/.DS_Store`；二是把 `src/tooling/cheng_tooling.cheng`、`verify_backend_sidecar_cheng_fresh.sh`、`resolve_backend_sidecar_defaults.sh`、`backend_driver_currentsrc_sidecar_wrapper.sh`、`cheng_tooling.sh` 一起改成接受 `probe_currentsrc_proof/cheng_stage0_currentsrc.proof` 作为严格 bootstrap bridge；三是把 `v3/tooling/run_slice_gate.sh` 默认工具切到源码 launcher，并先跑 fresh sidecar bridge 再跑 `build-backend-driver`。当前 fresh gate 已经不再卡“published strict stage0 surface / real sidecar driver missing strict direct-export surface / missing strict sidecar mode contract”这些旧死点，但 wrapper-source build 还没闭合，日志目前为空。 |
| 文件 | `src/tooling/cheng_tooling.cheng` `src/tooling/cheng_tooling_embedded_scripts/backend_driver_currentsrc_sidecar_wrapper.sh` `src/tooling/cheng_tooling_embedded_scripts/verify_backend_sidecar_cheng_fresh.sh` `src/tooling/cheng_tooling_embedded_scripts/resolve_backend_sidecar_defaults.sh` `src/tooling/cheng_tooling_embedded_scripts/cheng_tooling.sh` `v3/tooling/run_slice_gate.sh` `v3/tooling/README.md` `v3/README.md` `task_plan.md` `progress.md` `findings.md` |
| 不做 | 不把 bootstrap bridge 包装成最终主链；不回退去改 `backend_driver_symbol_bridge.c` 热路径；不再保留 sidecar 随机重写残片污染源码树。 |
| 验收 | `src/tooling` 顶层不再残留任何 `cheng_sidecar_rewrite_*.cheng`；`v3/tooling/run_slice_gate.sh` 默认必须走源码 launcher；fresh sidecar gate 必须至少前进到 wrapper-source 真编译阶段，不再死在旧合同识别上。 |

| 项目 | 内容 |
|---|---|
| 目标 | 继续打穿 `build-backend-driver` 真阻塞，把 `stage0 capability` source list 和 `system/seqs` 导入环一起收口，不再让旧 launcher/旧 sidecar 编译器把 `v3` gate 卡死在假缺项上。 |
| 主线 | 这轮已经做了三件硬事：`src/std/system.cheng` 删掉无效 `import std/seqs`；`src/tooling/cheng_tooling.cheng` 的 `tooling_stage0CapabilitySourceList()` 改成“固定主链 + 运行时扫描 `uir_core_builder*.cheng` + 静态名单回落”；同时确认 `cheng_tooling.cheng` 默认输出是 launcher，只有 `TOOLING_EMIT_SELFHOST_LAUNCHER=0` 才会尝试直出 native。本轮还没把 canonical tooling native binary 真重编成功，当前强制 native compile 仍在 `artifacts/backend_driver/cheng` 上 `rc=223`，而且旧执行面继续把假 source-list 缺项带出来。 |
| 文件 | `src/std/system.cheng` `src/tooling/cheng_tooling.cheng` `src/tooling/README.md` `task_plan.md` `progress.md` `findings.md` |
| 不做 | 不把 `TOOLING_STAGE0_CAPABILITY_PREFLIGHT=0` 当最终方案；不把 launcher 壳误算成当前源码 native binary；不回退 `v3` 的固定布局/二进制接口。 |
| 验收 | `system/seqs` 导入环必须从源码面消失；`tooling_stage0CapabilitySourceList()` 必须改成 builder 文件运行时扫描优先；记录必须明确写出“`cheng_tooling.cheng` 默认产物是 launcher，强制 native compile 仍未闭合，当前 canonical tooling 执行面仍旧”。 |

| 项目 | 内容 |
|---|---|
| 目标 | 在 `v3` 把冷路径字符串 interner、`HIR/MIR/LIR` 固定 ID 骨架、冻结 C 基线和脚本化验收入口一起落库，让“不能退回字符串热路径”从口头约束变成文件和命令。 |
| 主线 | 这轮不碰仓库现有 backend driver 活根，也不把它误算成 `v3` 代码问题；先把 `v3/src/lang/intern.cheng`、`v3/src/ir/core_types.cheng`、`v3/src/tests/ir_core_smoke.cheng`、`v3/tooling/{scan_forbidden_hotpath,compare_bench,run_slice_gate}.sh`、`v3/bench/c_ref/baseline_arm64_apple_darwin.txt` 落库，再用 `run_slice_gate.sh` 证明 `scan + c_ref` 已闭合、当前真实阻塞继续在仓库全局 `build-backend-driver`。 |
| 文件 | `v3/src/lang/intern.cheng` `v3/src/ir/core_types.cheng` `v3/src/tests/ir_core_smoke.cheng` `v3/tooling/scan_forbidden_hotpath.sh` `v3/tooling/compare_bench.sh` `v3/tooling/run_slice_gate.sh` `v3/bench/c_ref/baseline_arm64_apple_darwin.txt` `v3/README.md` `v3/lang/README.md` `v3/ir/README.md` `v3/tooling/README.md` `task_plan.md` `progress.md` `findings.md` |
| 不做 | 不去改当前 backend driver/source-list/import-cycle 老根；不把脚本写成只会报喜不报错的壳；不把 `v3` 名字、类型、IR 再退回 `str` 驱动。 |
| 验收 | `v3/tooling/scan_forbidden_hotpath.sh` 必须直接执行通过；`v3/tooling/compare_bench.sh` 必须能对拍冻结基线和最新 C 结果；`v3/tooling/run_slice_gate.sh` 必须稳定先过 `scan + c_ref + frozen-vs-latest`，再在现有 `build-backend-driver` 真故障点失败并留下日志。 |

| 项目 | 内容 |
|---|---|
| 目标 | 在 `v3` 先落固定布局字节面、固定 256 位 crypto 接口、二进制链帧和同机 C 基线，先把“性能地基”和“文本协议切除”真正变成文件与命令。 |
| 主线 | 这轮不碰 `v2` 活体热链，也不假装一口气做完整自举；先把 `v3/cheng-package.toml`、`v3/src/std/...`、`v3/src/chain/...`、`v3/bench/c_ref/*` 落库。当前 Cheng smoke 不是 `v3` 单点错误，而是仓库现有 canonical backend driver 全局 `rc=223`；`build-backend-driver` 继续向下暴露的是现有 `src/std/system.cheng -> src/std/seqs.cheng -> src/std/system.cheng` 导入环。 |
| 文件 | `v3/cheng-package.toml` `v3/README.md` `v3/src/std/bytes_layout.cheng` `v3/src/std/crypto/fixed256.cheng` `v3/src/chain/binary_types.cheng` `v3/src/chain/codec_binary.cheng` `v3/src/tests/fixed_surface_smoke.cheng` `v3/src/tests/chain_codec_binary_smoke.cheng` `v3/bench/c_ref/bench_ref.c` `v3/bench/c_ref/Makefile` `task_plan.md` `progress.md` `findings.md` |
| 不做 | 不把当前坏 driver 当成 `v3` 代码问题乱改；不把 `v3` 再塞回 `v2` 文本帧；不做 package import 兜底壳。 |
| 验收 | `v3/bench/c_ref` 必须真编真跑；`v3` 下固定布局类型、固定 256 位 crypto 接口、二进制链帧和 smoke 文件全部落库；记录必须明确写出“当前 Cheng smoke 被现有 driver 全局故障挡住，不是 `v3` 单点语义错误”。 |

| 项目 | 内容 |
|---|---|
| 目标 | 继续推进 `program` 热路径和值表示，但这轮已经先把 runtime 的 `array/record type_text` 收成全局 intern 稳定指针，并把 `program-selfhost` 扩成真实多进程 `chain_node serve-once/sync-once` gate。下一刀直接打固定布局 `slot/shape` 和 aggregate `field/index update copy`。 |
| 主线 | `system_helpers_stdio_bridge.c` 现在不再对每个 `array.elem_type_text / record.type_text` 做重复 `dup/free`，而是统一走全局 intern 表；`cheng_v2c_tooling.c` 的 `program-selfhost-check` 也已经正式加入 `chain_node_process_smoke`，固定要求 stage2 编译器真编真跑多进程 `mint -> serve-once -> sync-once -> balance`。完整前台 gate 已重新收口到 `manifest_fnv1a64=b55b66018e18ab44`。当前真主根没变，还是固定布局 `slot/shape` 和 aggregate `field/index update copy`。 |
| 文件 | `src/runtime/native/system_helpers_stdio_bridge.c` `v2/bootstrap/cheng_v2c_tooling.c` `v2/tests/contracts/compiler_core_system_link_exec.expected` `v2/tests/contracts/program_selfhost.expected` `v2/tests/contracts/full_selfhost.expected` `v2/docs/自举和性能.md` `task_plan.md` `progress.md` `findings.md` |
| 不做 | 不把这轮说成热层已完成；不回头碰已经闭合的 `fresh slot/nil slot/shared-shape/field ordinal`；不再把单进程 `msquic_chain_smoke` 当生产 gate |
| 验收 | 完整前台 gate `compiler-core-release -> full-selfhost` 必须继续通过；`program-selfhost` 必须继续真跑多进程 `chain_node_process_smoke`；记录必须明确写出“type_text intern 和真实多进程 gate 已收口，当前真根仍是固定布局 `slot/shape` 和 aggregate `field/index update copy`”。 |

| 项目 | 内容 |
|---|---|
| 目标 | 继续推进 `program` 热路径和值表示，但这轮先把 `fresh slot` 直写和 `nil slot` 新建 aggregate 的 temp 所有权收进 runtime，再继续打固定布局 `slot/shape` 和 aggregate field/index update copy。 |
| 主线 | 这轮已经把 `MAKE_ARRAY/MAKE_RECORD/array_push/add/setLen/reserve/field-index create_if_nil` 收成新槽位直写：新建 aggregate 不再先走通用 `assign_slot` 再深拷一次，`nil slot` 也不再白白 clone 一个刚创建的空 `array/record`。`compiler_core_system_link_exec/program-selfhost/full-selfhost` 已重新收口到 `manifest_fnv1a64=b8a740987bf39861`。前台 3 次中位数当前是 `pubkey=1.1300s`、`sign=1.6300s`、`mul_xonly=1.3200s`、`kinv=0.7000s`。下一刀不回头补这条已闭合路径，直接打固定布局 `slot/shape` 和 aggregate field/index update copy。 |
| 文件 | `src/runtime/native/system_helpers_stdio_bridge.c` `v2/tests/contracts/compiler_core_system_link_exec.expected` `v2/tests/contracts/program_selfhost.expected` `v2/tests/contracts/full_selfhost.expected` `v2/docs/自举和性能.md` `task_plan.md` `progress.md` `findings.md` |
| 不做 | 不把这轮说成热层已完成；不回头碰已收口的 `field ordinal + record slot`；不回到 probe 式乱试 `TrustedInto` 直连 |
| 验收 | `p256_fixed_core_probe` 必须继续 `probe=ok p_mul=1 n_mul=1 p_square=1 n_square=1`；`compiler_core_system_link_exec/program-selfhost/full-selfhost` 必须继续通过；记录必须明确写出“fresh slot/nil slot aggregate 白 clone 已切掉，当前真根仍是固定布局 `slot/shape` 和 aggregate field/index update copy”。 |

| 项目 | 内容 |
|---|---|
| 目标 | 继续推进 `program` 热路径和值表示，但这轮先把 `field ordinal + record slot` 这条固定布局主链在 source/stage0/runtime 三层收齐，再继续打固定布局 `slot/shape` 和 aggregate field/index update copy。 |
| 主线 | 这轮已经把 source 的 `stmtAssignTargetFieldOrdinals` 正式接进 stage0 facts/high_uir/low_uir mirror，`addr_of_field/load_field/store_*field` 不再只靠字段名；runtime 侧也把 `driver_c_prog_record_slot_at(...)` 收成正式 slot 入口，并把两处“已知 decl 顺序还按字段名找 slot”的路径改成按 ordinal 直达。完整前台 gate 已在这轮 fixed point 上收口到 `manifest_fnv1a64=29cea01991c4689b`，`program-selfhost/full-selfhost` 继续通过。下一刀不回头补这条已闭合路径，直接打固定布局 `slot/shape` 和 aggregate field/index update copy。 |
| 文件 | `v2/bootstrap/cheng_v2c_tooling.c` `src/runtime/native/system_helpers_stdio_bridge.c` `v2/tests/contracts/compiler_core_release_artifact.expected` `v2/tests/contracts/compiler_core_system_link_plan.expected` `v2/tests/contracts/compiler_core_system_link_exec.expected` `v2/tests/contracts/program_selfhost.expected` `v2/tests/contracts/full_selfhost.expected` `v2/docs/自举和性能.md` `task_plan.md` `progress.md` `findings.md` |
| 不做 | 不把这轮说成热层已完成；不回头碰已收口的 `field ordinal` 编译链；不把 `record slot` 名称查找当成最终方案继续扩散 |
| 验收 | 完整前台 gate 必须继续通过；记录必须明确写出“field ordinal + record slot 已三层收齐，当前真根还是固定布局 `slot/shape` 和 aggregate field/index update copy”。 |

| 项目 | 内容 |
|---|---|
| 目标 | 继续推进 `program` 热路径和值表示，但这轮先把 aggregate 同对象自拷贝和 shape 重建这两处真根收口，再继续打固定布局 slot/shape 和 aggregate field/index update copy。 |
| 主线 | 这轮已经把 `driver_c_prog_assign_slot` 里的“同一 array/record 又写回原 slot”这条纯自拷贝切掉，同时把 `driver_c_prog_clone_value_deep` 收成“保留 array cap / record cap / record lookup shape，再递归 clone 值”。前台 3 次中位数现在是 `pubkey=1.1700s`、`sign=1.6700s`、`mul_xonly=1.3400s`、`kinv=0.7200s`；`p256_fixed_core_probe`、`program-selfhost`、`full-selfhost` 和完整主 gate 都继续通过。下一刀不再回头碰这两处已闭合路径，直接打固定布局 slot/shape 和 aggregate field/index update copy。 |
| 文件 | `src/runtime/native/system_helpers_stdio_bridge.c` `v2/tests/contracts/compiler_core_release_artifact.expected` `v2/tests/contracts/compiler_core_system_link_plan.expected` `v2/tests/contracts/compiler_core_system_link_exec.expected` `v2/tests/contracts/program_selfhost.expected` `v2/tests/contracts/full_selfhost.expected` `v2/docs/自举和性能.md` `task_plan.md` `progress.md` `findings.md` |
| 不做 | 不再重试 `MAKE_RECORD` 槽位缓存；不再回头碰 `nil slot -> fresh aggregate` 直写；不把这轮小收益写成热层已完成 |
| 验收 | `p256_fixed_core_probe` 继续 `ok`；完整前台 gate 继续通过；记录必须明确写出“同对象自拷贝和 shape 重建已切掉，当前真根是固定布局 slot/shape 和 aggregate field/index update copy”。 |

| 项目 | 内容 |
|---|---|
| 目标 | 继续推进 `program` 热路径和值表示，但先把 `EPHEMERAL_AGGREGATE root-only clear` 这条实验正式收口，再直接打固定布局 slot/shape 和 aggregate update copy。 |
| 主线 | 这轮已经验证 `driver_c_prog_value_clear_ephemeral_flag_root` 可以保留：`p256_fixed_core_probe`、`compiler-core-system-link-exec`、`program-selfhost`、`full-selfhost` 全部继续闭合，前台 3 次中位数更新为 `pubkey=1.1700s`、`sign=1.6800s`、`mul_xonly=1.3500s`、`kinv=0.7300s`。这说明 recursive clear 已经不是语义必须成本，下一刀不再停在 flag 清理，直接改固定布局 slot/shape 和 aggregate update copy。 |
| 文件 | `src/runtime/native/system_helpers_stdio_bridge.c` `v2/tests/contracts/compiler_core_system_link_exec.expected` `v2/tests/contracts/program_selfhost.expected` `v2/tests/contracts/full_selfhost.expected` `v2/docs/自举和性能.md` `task_plan.md` `progress.md` `findings.md` |
| 不做 | 不把这条小收益说成热层已完成；不再怀疑 `root-only clear` 本身；不再尝试绕过 `assign_slot` 直接写 `nil slot -> fresh aggregate`；不回头碰已证伪的 `TrustedInto` 直连 |
| 验收 | `p256_fixed_core_probe` 继续 `ok`；完整前台 gate 继续通过；记录必须明确写出“root-only clear 已验证，当前真根是 slot/shape 和 aggregate update copy”。 |

| 项目 | 内容 |
|---|---|
| 目标 | 继续推进 `program` 热路径和值表示，但只保留真收益：拆掉 aggregate 深拷贝链里 nested `ZERO_PLAN` 的 eager materialize，再继续打固定布局 slot/shape。 |
| 主线 | 这轮已经把 `system_helpers_stdio_bridge.c` 里的 `driver_c_prog_clone_value_deep/driver_c_prog_value_clear_ephemeral_flags_deep` 改成“只解 `REF`，不提前把 nested `ZERO_PLAN` 物化成真实 aggregate”。这刀收正了值表示主链，也把运行时内存口径继续压低；前台 3 次中位数目前是 `pubkey=1.1900s`、`sign=1.7100s`、`mul_xonly=1.3600s`、`kinv=0.7300s`。下一刀不再赌 `ZERO_PLAN` 本身，而是直接改固定布局 slot/shape 和 aggregate update copy。 |
| 文件 | `src/runtime/native/system_helpers_stdio_bridge.c` `v2/tests/contracts/compiler_core_system_link_exec.expected` `v2/tests/contracts/program_selfhost.expected` `v2/tests/contracts/full_selfhost.expected` `v2/docs/自举和性能.md` `task_plan.md` `progress.md` `findings.md` |
| 不做 | 不把“临时 aggregate 直接接管”这种会留下嵌套别名风险的试刀塞进生产路径；不回头碰已闭合的 `comb6 correctness`；不放宽同机 C `1:1` 口径 |
| 验收 | `p256_fixed_core_probe` 必须继续 `ok`；`compiler-core-system-link-exec`、`program-selfhost`、`full-selfhost` 必须继续通过；记录必须明确写出“nested ZERO_PLAN eager materialize 已切掉，但当前真根已继续收缩到固定布局 slot/shape”。 |

| 项目 | 内容 |
|---|---|
| 目标 | 继续完成 `program` 热路径和值表示，但把“无收益试刀”和“真收益修正”分开。 |
| 主线 | 这轮先把 `MAKE_ARRAY/MAKE_RECORD` 的 fresh-slot move 试刀证伪并撤回，再把 `ZERO_PLAN` 的 record/array field/index 更新收成 lazy shell，避免 nested update 一上来就递归造整棵默认值。新 binary 前台重测后，`P-256` 四个 probe 的 3 次中位数是 `pubkey=1.1400s`、`sign=1.6700s`、`mul_xonly=1.3400s`、`kinv=0.7200s`，说明这条 lazy shell 修正没有把当前 crypto 热链再明显压下去。下一刀不再赌 `zero_plan` 壳本身，直接改固定布局 slot/shape。 |
| 文件 | `src/runtime/native/system_helpers_stdio_bridge.c` `v2/tests/contracts/compiler_core_system_link_exec.expected` `v2/tests/contracts/program_selfhost.expected` `v2/tests/contracts/full_selfhost.expected` `v2/docs/自举和性能.md` `task_plan.md` `progress.md` `findings.md` |
| 不做 | 不把这轮写成性能大收益；不保留 fresh-slot 试刀；不回头碰已证伪的 `TrustedInto` 直连 |
| 验收 | `compiler-core-system-link-exec`、`program-selfhost`、`full-selfhost` 必须继续通过；记录必须明确写出“lazy shell 保留，但当前真根已经切到固定布局 slot/shape”。 |

| 项目 | 内容 |
|---|---|
| 目标 | 继续完成 `program` 热路径和值表示，这轮先把 `lazy zero-plan + slot 级 materialize` 真收口，再继续打 aggregate field update 剩下的物化/拷贝。 |
| 主线 | 这轮已经把 `system_helpers_stdio_bridge.c` 里的 typed `param/local_decl` 默认零值改成 lazy `ZERO_PLAN`，ref load 改成 slot 级原地 materialize，`assign_slot` 会按 zero plan 原地 refine，不再先 eager 物化整块默认值。同时也修掉了这条新路径暴露出来的真 bug：如果 `ZERO_PLAN` slot 在 nested field store 里先物化成临时 record/bytes/str/array，但不写回 slot，写入会直接丢失，`lsmr_advanced_features_smoke` 就会把 `Result.err.msg` 写没。这条现在已经切正，`program-selfhost/full-selfhost` 都继续通过。新 runtime 上前台 3 次中位数已经压到 `pubkey=0.9900s`、`sign=1.4100s`、`mul_xonly=1.1500s`、`kinv=0.6200s`，单次前台 RSS 约 `148MB/332MB`。下一刀不回头碰已闭合路径，直接打 aggregate field update 剩下的物化/拷贝。 |
| 文件 | `src/runtime/native/system_helpers_stdio_bridge.c` `v2/tests/contracts/compiler_core_system_link_exec.expected` `v2/tests/contracts/program_selfhost.expected` `v2/tests/contracts/full_selfhost.expected` `v2/docs/自举和性能.md` `task_plan.md` `progress.md` `findings.md` |
| 不做 | 不把这轮说成热层已完成；不再用 `ZERO_PLAN` 临时值绕过 slot 写回；不回头再试已证伪的 `TrustedInto` 直连 |
| 验收 | `full-selfhost` 和 `program-selfhost` 必须继续通过；记录必须明确写出“lazy zero-plan 已进 active runtime、nested field store 丢写入已修、下一刀是 aggregate field update 物化/拷贝”。 |

| 项目 | 内容 |
|---|---|
| 目标 | 继续完成 `program` 热路径和值表示，这轮先把已经确认的真收益收口：小 aggregate 固定布局内联 + frame 级 `zero plan` 预解码，然后继续打 `zero_value/local_decl` 的固定布局原型化。 |
| 主线 | 这轮没有回头碰已证伪的 `TrustedInto` 直连，而是继续重建 `program` 热层本身：`DriverCProgArray/DriverCProgRecord` 现在都带小容量内联存储，`clone/zero plan` 也不再通过 `record_slot(...)` 逐字段二次搭壳；`DriverCProgFrame` 还新增了 `param/local zero plans` 预解码，`LOCAL_DECL` 不再每次重跑类型归一化和零值计划查找。新 runtime 上前台 3 次中位数已经压到 `pubkey=1.0700s`、`sign=1.5300s`、`mul_xonly=1.2300s`、`kinv=0.6800s`，单次前台 RSS 约 `148MB/332MB`。下一刀不再回头碰这轮已闭合路径，直接打固定布局零值原型和剩余物化/拷贝。 |
| 文件 | `src/runtime/native/system_helpers_stdio_bridge.c` `v2/tests/contracts/compiler_core_system_link_exec.expected` `v2/tests/contracts/full_selfhost.expected` `v2/tests/contracts/program_selfhost.expected` `v2/docs/自举和性能.md` `task_plan.md` `progress.md` `findings.md` |
| 不做 | 不把“秒级继续下降”误写成热层已完成；不回头再试已证伪的 `TrustedInto` 直连；不放宽同机 C `1:1` 口径 |
| 验收 | `full-selfhost` 和 `program-selfhost` 必须继续通过；记录必须明确写出“小 aggregate 已内联、frame 级 zero plan 已预解码、下一刀是固定布局零值原型和剩余物化/拷贝”。 |

| 项目 | 内容 |
|---|---|
| 目标 | 继续完成 `program` 热路径和值表示，但先把已经确认的 runtime 真收益收口，再继续打 `zero_value/local_decl` 这条活根。 |
| 主线 | 这轮已经把 `system_helpers_stdio_bridge.c` 里的 per-call frame 泄露切掉：`params/locals/stack/labels/loops` 现在会在 `eval_item` 退出前统一清理，小帧也已经内联到 `DriverCProgFrame`，不再每次都走堆分配。同时把 `MAKE_ARRAY/MAKE_RECORD/STORE_* / NEW_REF` 收成真正的临时 aggregate move，少掉一层重复 `materialize` 和深拷贝。新 runtime 上前台 3 次中位数已经压到 `pubkey=1.2500s`、`sign=1.7700s`，峰值 RSS 约 `147.9MB/331.6MB`。下一刀不再回头碰这轮已闭合路径，直接打 `zero_value_from_type/local_decl` 的固定布局原型化。 |
| 文件 | `src/runtime/native/system_helpers_stdio_bridge.c` `v2/tests/contracts/compiler_core_system_link_exec.expected` `v2/tests/contracts/full_selfhost.expected` `v2/tests/contracts/program_selfhost.expected` `v2/docs/自举和性能.md` `task_plan.md` `progress.md` `findings.md` |
| 不做 | 不把“RSS 大降 + 小幅变快”误写成热层已完成；不回头再试已证伪的 `TrustedInto` 直连；不放宽同机 C `1:1` 口径 |
| 验收 | `full-selfhost` 和 `program-selfhost` 必须继续通过；记录必须明确写出“帧泄露已切掉、小帧已内联、下一刀是 zero_value/local_decl” |

| 项目 | 内容 |
|---|---|
| 目标 | 保持“现有闭合主链上重建关键热层”这条主线，同时把已证伪的 `TrustedInto` 试刀撤回，不让回归混进 active 热链。 |
| 主线 | 这轮先验证了 `pointJacobianDoubleFixedInPlace/pointJacobianAddAffineFixedInPlace` 里 `double/triple/quad/eight` 改成 `TrustedInto` 后的 correctness 和 spot-check。`double/add/comb/repr` 对拍都继续全绿，但 `pubkey/sign` 单次前台样本回到 `1.50s/2.22s`，比当前稳定基线 `1.2830s/1.8210s` 更差，所以整刀已撤回。当前主线不变：继续重建 `program` 执行热层和值流，不做整仓重建。 |
| 文件 | `src/std/crypto/ecnist.cheng` `v2/docs/自举和性能.md` `task_plan.md` `progress.md` `findings.md` |
| 不做 | 不保留这条回归试刀；不因为 correctness 全绿就把慢路径混进 active；不整仓推倒 |
| 验收 | 撤回后代码回到稳定热链；记录明确写出“这条 `TrustedInto` 试刀证伪并撤回”；主线继续维持“关键热层重建”口径。 |

| 项目 | 内容 |
|---|---|
| 目标 | 把“不是整仓重建，而是在现有闭合主链上重建关键热层”写成正式口径，并据此继续推进。 |
| 主线 | 这轮先不盲跑编译，先把 `v2/docs/自举和性能.md` 和任务记录收成统一结论：保留现有 `full-selfhost / LSMR / chain_node / comb6 correctness`，只重建 `program` 执行热层和纯 Cheng 热核接口；不走整仓推倒，也不走补丁式乱试。 |
| 文件 | `v2/docs/自举和性能.md` `task_plan.md` `progress.md` `findings.md` |
| 不做 | 不改语言前端总架构；不整仓重写；不把“先零 C 自举”当当前最短路 |
| 验收 | 文档必须明确写出“整仓重建不做、关键热层重建才是最短路”；任务记录同步成同一口径。 |

| 项目 | 内容 |
|---|---|
| 目标 | 把进度记录口径改成“只写开始时间，完成时间由用户统计真实时间”。 |
| 主线 | 这轮不改代码、不编译，只同步记录规则：`lessons.md` 记用户规则，`progress.md` 顶部写明新口径并从当前条目开始生效，历史条目不追改；`task_plan.md` 和 `findings.md` 只做规则同步。 |
| 文件 | `lessons.md` `progress.md` `task_plan.md` `findings.md` |
| 不做 | 不回写历史进度条目；不改工程代码；不跑编译或 smoke |
| 验收 | `progress.md` 顶部必须明确“只写开始时间”；`lessons.md` 必须记住这条用户规则；历史记录保持原样。 |

| 项目 | 内容 |
|---|---|
| 目标 | 把“如何从第一天就保证自举后仍有 C 级编译和运行时性能”的原则正式写进文档，作为后续性能和自举决策的硬口径 |
| 主线 | 这轮不改代码、不跑编译，只把架构原则写进 `v2/docs/自举和性能.md`：语言必须是静态系统语义、普通程序必须 AOT 本地码、热核必须固定宽度专用实现、benchmark 必须从第一天就按同机 C `1:1`。同时把“当前仓库离这个目标还差什么”收成正式路线图，避免后面再把“先自举再补性能”当正确路径。 |
| 文件 | `v2/docs/自举和性能.md` `task_plan.md` `progress.md` `findings.md` |
| 不做 | 不改 `ecnist/p256_fixed`；不编译；不刷 gate；不改进度百分比 |
| 验收 | 文档里必须新增“从第一天就保证自举后仍有 C 级性能”和“当前仓库离这个目标还差什么”两节，口径与现有同机 C `1:1` 目标一致 |

| 项目 | 内容 |
|---|---|
| 目标 | 继续完成“纯 Cheng 第一梯队性能 + 近零 C 自举闭环”，主线保持在 correctness 已闭合的 active `comb6` 热链上继续压 `double/addAffine`。 |
| 主线 | 这轮保留下来的真改动是把 `p256_fixed` 低层 fake `Into` 收成真原地输出：`p256FixedModAddTrustedInto`、`p256FixedModSubTrustedInto`、raw add/sub 和条件减模不再先返回整块 `P256Fixed` 再赋值。`_tmp_p256_mul_double_cmp_probe`、`_tmp_p256_mul_add_cmp_probe`、`_tmp_p256_comb_cmp_probe`、`_tmp_p256_repr_sign_r_cmp_probe` 全部继续转绿，完整前台 gate 也已通过。当前 3 次前台中位数已更新到 `pubkey=1.2830s`、`sign=1.8210s`、`mul=1.4628s`、`kinv=0.7521s`。下一刀继续只压 active `comb6` 主循环里的非自乘乘法和值流，优先 `pointJacobianAddAffineFixedInPlace`。 |
| 文件 | `src/std/crypto/ecnist.cheng` `v2/tests/contracts/_tmp_p256_mul_double_cmp_probe.cheng` `v2/tests/contracts/_tmp_p256_mul_add_cmp_probe.cheng` `v2/tests/contracts/_tmp_p256_comb_cmp_probe.cheng` `v2/tests/contracts/_tmp_p256_repr_sign_r_cmp_probe.cheng` `v2/tests/contracts/_tmp_p256_pubkey_scalar_probe.cheng` `v2/tests/contracts/_tmp_ecdsa_sign_probe.cheng` `v2/tests/contracts/_tmp_ecdsa_mul_probe.cheng` `v2/tests/contracts/_tmp_ecdsa_kinv_probe.cheng` `v2/docs/自举和性能.md` `task_plan.md` `progress.md` `findings.md` |
| 不做 | 不回头怀疑 `comb6` correctness；不再尝试整段 trusted 直连；不碰 native crypto；不放宽同机 C `1:1` 目标；不跑后台命令 |
| 验收 | `double/add cmp`、`comb_cmp`、`repr_r_cmp` 必须继续全绿；完整前台 gate `make -j1 -C /Users/lbcheng/cheng-lang/v2/bootstrap compiler-core-release compiler-core-system-link compiler-core-system-link-exec tooling-selfhost lsmr-contracts selfhost full-selfhost` 必须通过；性能口径更新为 `pubkey=1.2830s`、`sign=1.8210s` |

| 项目 | 内容 |
|---|---|
| 目标 | 继续完成 `P-256 comb6` correctness，但先把真根从“数学/表常量”收紧到“当前函数形状下的代码生成问题”，不把任何未闭合 `comb6` 接回生产路径。 |
| 主线 | 这轮已经前台钉死三件事：`wnaf == Python`；`p256GComb6[idx]` 的常量表和可变索引本身是对的；真正坏的是“完整 `comb6` 函数形状”。最小双比特 probe 现在稳定显示：`single_col0` 和 `cross_row_same_col` 会塌成 infinity，`single_col1` 和 `top_row_cols01` 正常，`same_row_cols01` 会退化成 `digit=2` 的结果。与此同时，`_tmp_p256_comb_first_digit_probe` 已证明同样的 `find first nonzero row -> setFromAffine -> toAffine` 在小函数里是对的。下一刀不再猜表、不再猜数学，直接查为什么这段逻辑一进 `ecnist` 的完整函数形状就坏。 |
| 文件 | `src/std/crypto/ecnist.cheng` `v2/tests/contracts/_tmp_p256_comb_two_bit_probe.cheng` `v2/tests/contracts/_tmp_p256_comb_lookup_bug_probe.cheng` `v2/tests/contracts/_tmp_p256_comb_table_entry_probe.cheng` `v2/tests/contracts/_tmp_p256_comb_digit_lookup_probe.cheng` `v2/tests/contracts/_tmp_p256_comb_first_digit_probe.cheng` `task_plan.md` `progress.md` `findings.md` |
| 不做 | 不再把 `comb6` 接回 `publicKey/sign` active 路；不再相信旧二进制 probe；不碰 native crypto；不放宽同机 C `1:1` 目标；不跑后台命令 |
| 验收 | 生产路径保持回滚稳定；`_tmp_p256_comb_two_bit_probe_bin` 继续稳定复现低位塌缩；`_tmp_p256_comb_first_digit_probe_bin` 继续证明小函数形状是对的；完整前台 gate 继续通过 |

| 项目 | 内容 |
|---|---|
| 目标 | 继续完成“纯 Cheng 第一梯队性能 + 近零 C 自举闭环”里的最短热核，但先把 `comb6` 的 correctness 收平，不再把未闭合的 active 接线算进生产路径。 |
| 主线 | 这轮已经确认两件事：`_tmp_p256_jacobian_valueflow_probe` 全绿，旧的 imported/local fixed-record 值流老根已死；`comb6` 的完整 affine 仍未闭合，`_tmp_p256_comb_cmp_probe` 继续 `neq`，而且代表性标量 `repr_r_cmp` 也还不等价，所以上一轮临时 active 接线已经撤回。回滚后重新量准的安全前台基线是 `pubkey=4.47s`、`sign=6.98s`、`mul_xonly=3.67s`、`kinv=0.95s`。下一刀只查 `comb6` 完整 affine 为什么和 `wnaf` 不等价，不再把 `comb6` 接回 active 热链。 |
| 文件 | `src/std/crypto/ecnist.cheng` `v2/tests/contracts/_tmp_p256_pubkey_scalar_probe.cheng` `v2/tests/contracts/_tmp_ecdsa_sign_probe.cheng` `v2/tests/contracts/_tmp_ecdsa_mul_probe.cheng` `v2/tests/contracts/_tmp_ecdsa_kinv_probe.cheng` `v2/tests/contracts/_tmp_p256_comb_cmp_probe.cheng` `v2/tests/contracts/_tmp_p256_sign_r_comb_cmp_probe.cheng` `v2/tests/contracts/_tmp_p256_repr_sign_r_cmp_probe.cheng` `v2/tests/contracts/_tmp_p256_jacobian_valueflow_probe.cheng` `v2/docs/自举和性能.md` `task_plan.md` `progress.md` `findings.md` |
| 不做 | 不再把 imported/local record-return 值流当主根；不在完整 affine 未对拍前把 `comb6` 接回 `publicKey` 或签名 active 路径；不碰 native crypto；不放宽同机 C `1:1` 目标；不跑后台命令 |
| 验收 | `_tmp_p256_jacobian_valueflow_probe_bin` 必须继续全绿；`_tmp_p256_comb_cmp_probe_bin` 和 `_tmp_p256_repr_sign_r_cmp_probe_bin` 的边界必须被进一步压缩；回滚后的安全基线和完整前台 gate 必须继续通过 |

| 项目 | 内容 |
|---|---|
| 目标 | 把 `LSMR` 的真实联网验收彻底收成“多进程 `chain_node serve-once/sync-once` 正式合同”，不再把单进程 `msquic_chain_smoke` 当生产 gate。 |
| 主线 | 这轮已经连续钉死了三条真根：`msquicNativeDial` 因 `if-expr` body shape 掉成 outline、`chainReadExact` 把非阻塞 app recv 误当流结束、`chain_node_process_smoke` 仍沿用过时 `12s` timeout。现在 `chain_node_test` 已前台真跑通 `serve-once/sync-once -> synced=1 -> balance=11`。下一刀只收 Makefile、文档和总 gate，不再回头猜 TLS/链算法。 |
| 文件 | `v2/cheng-quic/src/native_runtime.cheng` `v2/cheng-quic/src/connection.cheng` `v2/bootstrap/Makefile` `v2/docs/LSMR.md` `v2/docs/cheng-chain-mvp.md` `task_plan.md` `progress.md` `findings.md` |
| 不做 | 不再把单进程 `listener+client` 共存模型当生产证明；不对 TLS 慢路径做兜底；不跑后台命令 |
| 验收 | `make -j1 -C /Users/lbcheng/cheng-lang/v2/bootstrap lsmr-contracts` 必须前台通过；`chain_node_process_smoke.expected` 必须稳定；文档必须改成“真实联网证明看多进程 `chain_node`” |

| 项目 | 内容 |
|---|---|
| 目标 | 把 `LSMR` 的真实联网 smoke 正式收进 contract 和主 gate，让文档、代码、验收三者完全一致。 |
| 主线 | 这轮已经把 `FrameData` 真实长度、`FrameData` 精确切片、app stream `offset` 重组三条运行时活根切掉；`chain_state_tree_sync_smoke` 和 `msquic_chain_smoke` 都已经前台真跑通过。下一刀不再猜 packet/TLS/链算法，直接把这两条 smoke 接进 `lsmr-contracts`，然后再回普通 `program` 运行面和性能主线。 |
| 文件 | `v2/bootstrap/Makefile` `v2/tests/contracts/chain_state_tree_sync_smoke.expected` `v2/tests/contracts/msquic_chain_smoke.expected` `v2/docs/LSMR.md` `v2/docs/cheng-chain-mvp.md` `task_plan.md` `progress.md` `findings.md` |
| 不做 | 不再回头打开已经收口的 `packet too small`、`stream reassembly gap`、TLS 重传 transcript 假根；不做后台长跑；不把 smoke 成功伪装成“只算算法层通过” |
| 验收 | `lsmr-contracts` 必须前台真跑通过；`chain_state_tree_sync_smoke` 和 `msquic_chain_smoke` 必须有正式 expected；`LSMR.md` 和 `cheng-chain-mvp.md` 必须改成真实状态口径 |

| 项目 | 内容 |
|---|---|
| 目标 | 继续把 `v2/docs/LSMR.md` 里的链侧新技术往可运行版本推进，先落 `LSH 双重寻址` 和 `edge/regional/global` 三层状态快照，不假装一口气做完整个愿景层。 |
| 主线 | 这轮不碰 VM，不碰大握手路径，只在 `v2/cheng-quic/src/chain/{types,lsmr}.cheng` 里补 `LsmrLocalityCid/LsmrStateCell/LsmrStateLayerForest`，再用 `lsmr_locality_storage_smoke` 把 `token 顺序不敏感 + 三层状态树投影 + ChainIndex 三层派生` 钉死。 |
| 文件 | `v2/cheng-quic/src/chain/types.cheng` `v2/cheng-quic/src/chain/lsmr.cheng` `v2/cheng-quic/src/tests/lsmr_locality_storage_smoke.cheng` `v2/docs/cheng-chain-mvp.md` `task_plan.md` `progress.md` `findings.md` |
| 不做 | 不假装把 `LSMR.md` 全部愿景一口气做完；不混进 QUIC 握手/runtime 老根；不碰 VM 合约；不放宽任何 gate |
| 验收 | `lsmr_locality_storage_smoke` 必须前台真编过并真运行通过；`LocalityCid` 需稳定区分不同语义集，`LsmrStateLayerForest` 需稳定产出 `edge/regional/global` 三棵树和 `localOpCount/regionalBatchCount/globalAnchorCount` |

| 项目 | 内容 |
|---|---|
| 目标 | 继续完成“纯 Cheng 第一梯队性能 + 近零 C 自举闭环”里 `ECDSA` 的最短热核，但先把新暴露出的 `program/runtime` 值流老根钉死：非 owner 模块里，直接接 imported 函数返回的 `EcPointJacobianFixed` 会把结果打成 `inf + zero z`。当前理论目标仍是 `ecdsa p256 sign = 11.9109us/op`，`ecdh p256/pubkey = 24.4003us/op`。 |
| 主线 | 这轮不再碰 active `ecnist` 热链。最小复现已经落在 `v2/tests/contracts/_tmp_p256_jacobian_valueflow_probe.cheng`：`inline`、`local_nocopy`、`local_return_nocopy`、`imported`、`imported_return` 都通过；只有 `local_copy` 和 `local_return` 会稳定变成 `inf=1 z=[0..]`。这说明真根不是 `comb` 算法，也不是 return 本身，而是“非 owner 模块里把 imported 函数返回的 `EcPointJacobianFixed` 直接赋给本地变量”这条值流。下一刀先查 `program` 轨这条 record-return/assign 语义，不再继续压 `comb`。 |
| 文件 | `src/runtime/native/system_helpers_stdio_bridge.c` `src/std/crypto/ecnist.cheng` `v2/tests/contracts/_tmp_p256_jacobian_valueflow_probe.cheng` `v2/tests/contracts/_tmp_p256_generator_comb6_step_probe.cheng` `v2/docs/自举和性能.md` `task_plan.md` `progress.md` `findings.md` |
| 不做 | 不碰 VM；不引入 native crypto；不放宽 gate；不再起后台探测；不把未闭合的 `comb` 路接回 active 热链；不再继续猜 `ECDSA` 数学或窗口表；不在没钉死 record-return 值流前继续改 `pointMulGenerator...Comb6` |
| 验收 | `_tmp_p256_generator_comb6_step_probe` 必须继续 `ok`；`_tmp_p256_jacobian_valueflow_probe` 必须稳定复现“`local_copy/local_return` 失败而其余路径通过”；`p256_fixed_core_probe` 必须继续 `p_mul=1 n_mul=1 p_square=1 n_square=1`；完整前台 gate `make -j1 -C /Users/lbcheng/cheng-lang/v2/bootstrap compiler-core-release compiler-core-system-link compiler-core-system-link-exec tooling-selfhost lsmr-contracts selfhost full-selfhost` 必须继续通过 |

| 项目 | 内容 |
|---|---|
| 目标 | 继续完成“纯 Cheng 第一梯队性能 + 近零 C 自举闭环”里的最短热核，把 `ECDSA/RSA` 的 `bigModMul/bigModExp` 从位串行取模切到 Montgomery，并把真瓶颈继续压缩到普通程序执行面 |
| 主线 | 这轮已经把 `std/crypto/bigint.cheng` 加上 `BigMontgomeryContext + Montgomery mul/modexp`，并让 `std/crypto/ecnist.cheng` 复用 `P-256 P/N` 的上下文，不再走旧的 `bigMul -> bigMod(bit-by-bit)`。下一刀不碰 VM，不碰 v1，不引入 native crypto，直接继续消掉普通程序执行面对大对象值传递的热路径拷贝。 |
| 文件 | `src/std/crypto/bigint.cheng` `src/std/crypto/ecnist.cheng` `src/runtime/native/system_helpers_stdio_bridge.c` `v2/tests/contracts/_tmp_ecdsa_sign_probe.cheng` `task_plan.md` `progress.md` `findings.md` |
| 不做 | 不做 VM 合约；不碰 `v1`；不引入 native crypto/内建；不放宽 gate；不靠 bridge 或解释器兜底热路径；不再直接探测 `cheng_v2_system_link_exec` |
| 验收 | `_tmp_ecdsa_sign_probe` 前台真编译；新 Montgomery 路径真进入运行面；同时记录 runtime 真瓶颈，不用猜 |

| 项目 | 内容 |
|---|---|
| 目标 | 从通用 `BigInt` 继续下沉到专用 `P-256 8x32` 固定宽度 Montgomery 内核，确认热核慢点到底是 Cheng 执行面拷贝，还是新专用内核本身的算术实现还有 bug |
| 主线 | `p256_fixed` 已继续压缩到最小断点：`_tmp_p256_fixed_stage_probe` 证明第一拍 `a*R^2` 的乘法低位和 `mWord` 都对，真正炸的是同一拍的 `+ mWord * modulus`。因此下一刀不再尝试把它直接接进 `ecnist`，而是先查普通 program 轨的 `uint64` 热算子语义。 |
| 文件 | `src/std/crypto/p256_fixed.cheng` `src/std/crypto/ecnist.cheng` `v2/tests/contracts/p256_fixed_core_probe.cheng` `task_plan.md` `progress.md` `findings.md` |
| 不做 | 这刀不把未闭合的 `p256_fixed` 接进 `ecdsaSign`；不碰 VM；不引入 native crypto；不放宽 gate；不再拿整签名 probe 盲跑 |
| 验收 | `p256_fixed_core_probe` 和最小 stage probe 都能把断点压到固定的 `mul reduce i=0 / carry overflow offset=0 idx=17`；`ecnist` active 路径保持回到稳定版 Jacobian + generic Montgomery |

| 项目 | 内容 |
|---|---|
| 目标 | 修正普通 program runtime 把 `uint64` 偷偷当成 `int64` 的热算子语义错误，并把 `p256_fixed` 的 Montgomery 末尾正规化成带高字条件减模 |
| 主线 | 这轮不碰 VM，不引入 native crypto，也不再猜 `ecdsaSignBytes` 公式；先把 `system_helpers_stdio_bridge.c` 补成真实 `u32/u64` 值语义，再让 `p256_fixed_core_probe` 前台真跑到 `p_mul=1 n_mul=1`，最后把顶层 gate 收回绿色 |
| 文件 | `src/runtime/native/system_helpers_stdio_bridge.c` `src/std/crypto/p256_fixed.cheng` `v2/tests/contracts/_tmp_p256_runtime_probe.cheng` `v2/tests/contracts/p256_fixed_core_probe.cheng` `v2/tests/contracts/compiler_core_system_link_exec.expected` `v2/tests/contracts/full_selfhost.expected` `task_plan.md` `progress.md` `findings.md` |
| 不做 | 不改 `ecnist` active 接线；不放宽 gate；不再跑会留下孤儿进程的后台探测；不碰别的脏改 |
| 验收 | `_tmp_p256_runtime_probe` 必须恢复 `mul_hi_ok=1`；`p256_fixed_core_probe` 必须输出 `probe=ok p_mul=1 n_mul=1`；`make -j1 -C /Users/lbcheng/cheng-lang/v2/bootstrap compiler-core-release compiler-core-system-link compiler-core-system-link-exec tooling-selfhost lsmr-contracts selfhost full-selfhost` 必须前台通过 |

| 项目 | 内容 |
|---|---|
| 目标 | 修正普通 program runtime 在泛型零初始化里把“声明模块作用域”和“实例化模块作用域”混成一个的错误，并把 `ecdsa` 真瓶颈压缩到 program 热路径解释执行 |
| 主线 | 这轮不再猜 `pfix.P256Fixed` alias、本体或 `Result[T]` 定义对不对；直接把 `driver_c_prog_zero_value_from_type(_item)` 改成双作用域解析，先让 `Result[pfix.P256Fixed]` 真能跑，再验证 `ecdsaSignBytes` 是否已经从“类型解析崩”推进到“纯计算过慢” |
| 文件 | `src/runtime/native/system_helpers_stdio_bridge.c` `src/std/crypto/ecnist.cheng` `v2/tests/contracts/compiler_core_system_link_exec.expected` `v2/tests/contracts/full_selfhost.expected` `task_plan.md` `progress.md` `findings.md` |
| 不做 | 不碰 VM；不引入 native crypto；不再误把 `ecdsa` 慢归因成数学公式错；不再留临时 probe 文件；不再起会悬挂的后台进程 |
| 验收 | `_tmp_ecdsa_sign_probe` 必须不再报 `missing type item for zero init type=pfix.P256Fixed`；完整 `make -j1 -C /Users/lbcheng/cheng-lang/v2/bootstrap compiler-core-release compiler-core-system-link compiler-core-system-link-exec tooling-selfhost lsmr-contracts selfhost full-selfhost` 必须前台通过；结论必须收敛到“热路径仍在 `program_local_payload_entry`”这个架构根 |

| 项目 | 内容 |
|---|---|
| 目标 | 继续把 `ECDSA` 真热根从“初始化 + 乘法混在一起”压缩成“只剩固定点 `mul` 主循环”，并把启动时现算生成器表彻底移出运行面 |
| 主线 | 这轮已经把 `P/N/B/G` 和 `1..15*G` 的 `Montgomery` 固定表固化成源码常量，`p256EnsureInit()` 不再做 `hexDecode + bigFromBytes + Jacobian 建表`；同时给 `pointJacobianToAffineFixed()` 加了 `z==1` 快路，并让 `ecdsaSign()` 直接走 `pointMulGeneratorFixed()` 取 `x`，不再把没用的 `y` 也转回 `BigInt`。下一刀不再碰初始化，也不回头试窗口/comb 花样，只打 `pointMulGeneratorWnaf()` 的固定点 `double/add` 主循环。 |
| 文件 | `src/std/crypto/ecnist.cheng` `v2/tests/contracts/_tmp_p256_pubkey_probe.cheng` `v2/tests/contracts/_tmp_p256_pubkey_twice_probe.cheng` `v2/tests/contracts/_tmp_ecdsa_sign_probe.cheng` `v2/tests/contracts/_tmp_ecdsa_sign_stage_probe.cheng` `task_plan.md` `progress.md` `findings.md` |
| 不做 | 不碰 VM；不引入 native crypto；不新增更大生成器表；不靠探针常驻主线；不再把 `mul` 之前的初始化成本和点乘成本混在一起看 |
| 验收 | `_tmp_p256_pubkey_probe_bin` 前台真跑要显著低于之前的 `7.55s`；`_tmp_ecdsa_sign_probe_bin` 前台必须在限时内真完成，不再只是 `stage=mul` 超时；完整 `make -j1 -C /Users/lbcheng/cheng-lang/v2/bootstrap compiler-core-release compiler-core-system-link compiler-core-system-link-exec tooling-selfhost lsmr-contracts selfhost full-selfhost` 必须继续前台通过 |

| 项目 | 内容 |
|---|---|
| 目标 | 继续把生成器固定点乘的主循环从 `BigInt` 重编码彻底切到固定 `8x32` 标量路径，并继续压 `double/add` 本身 |
| 主线 | 这轮已经把 `pointMulGeneratorWnaf()` 从 `BigInt` 的 `bigIsOdd/bigGetBit/bigAdd/bigSub/bigShiftRight1` 改成固定 `8x32` 标量原地 `odd/low5/+small/-small/>>1`，同时预计算了 `p256GWindow4Neg`，主循环不再现算负点。下一刀只剩 `pointJacobianDoubleFixed()` 和 `pointJacobianAddAffineFixed()` 的乘法条数与数据流压缩。 |
| 文件 | `src/std/crypto/ecnist.cheng` `task_plan.md` `progress.md` `findings.md` |
| 不做 | 不回退到 `BigInt` 重编码；不加更大窗口；不碰 `deterministicK/nModInv`；不碰 VM |
| 验收 | `_tmp_p256_pubkey_probe_bin` 进入亚秒级；`_tmp_ecdsa_sign_probe_bin` 继续下降；完整主 gate 继续前台通过 |

| 项目 | 内容 |
|---|---|
| 目标 | 把 `pointJacobianAddAffineFixedInPlace` 的 trusted `add/sub` 基础层先做正确，再继续压 `x3/y3/z3` 的 trusted 值流 |
| 主线 | 这轮只用逐拍对拍 probe，不碰 active `ecnist`。`_tmp_p256_mul_add_stage_cmp_probe` 已把 trusted 路第一批真根压到 `p256FixedModAddTrustedInto/p256FixedModSubTrustedInto` 的 reduction/borrow 分支；这两条现在都收成了和 value-return 同构。下一刀直接盯 `x3`，不再回头查 `h/i/yDiff/r`。 |
| 文件 | `src/std/crypto/p256_fixed.cheng` `v2/tests/contracts/_tmp_p256_mul_add_stage_cmp_probe.cheng` `v2/tests/contracts/_tmp_p256_mul_add_cmp_probe.cheng` `task_plan.md` `progress.md` `findings.md` |
| 不做 | 不把未闭合的 trusted `addAffine` 接回 `ecnist` active 路；不刷 fixed-point；不碰 `kinv`；不碰窗口表和初始化 |
| 验收 | `_tmp_p256_mul_add_stage_cmp_probe_bin` 的第一处错位推进到 `x3`；`_tmp_p256_mul_add_cmp_probe_bin` 继续 `ok`；`p256_fixed_core_probe_bin` 继续 `probe=ok p_mul=1 n_mul=1 p_square=1 n_square=1`；完整主 gate 前台通过 |

| 项目 | 内容 |
|---|---|
| 目标 | 先把 `msquic` 握手内存爆涨根切掉，不编译，只做静态修正 |
| 主线 | 已确认 311GB 真根不是递归，而是 `Initial/Handshake` 重传包被重复喂进 TLS，导致 transcript 被重复追加，再被 `ByteBuffer.appendBytes` 整块复制放大。当前已在 `native_runtime` 把握手 `CRYPTO` 输入切到真正的 `offset` 重组，不再直接把 `frame.data` 喂给 `msquicTls13HandshakeFeed`；同时在 `handshake13` 加了 transcript/buffer 的硬上限，后续即使再有漏网也会直接 fail-fast，不再吃光内存。下一刀再补更细的去重和 transcript 表示优化。 |
| 文件 | `v2/cheng-quic/src/native_runtime.cheng` `v2/cheng-quic/src/core/crypto_stream.cheng` `v2/cheng-quic/src/tls/handshake13.cheng` `task_plan.md` `progress.md` `findings.md` |
| 不做 | 这轮不编译，不跑 smoke，不继续拉起会吃内存的握手路径 |
| 验收 | 先完成静态修正和记录收口；下一轮再前台小窗口验证，不再允许无去重握手输入进入 TLS |

| 项目 | 内容 |
|---|---|
| 目标 | 继续压 `pointJacobianAddAffineFixed`，但不再整段接 trusted active 路；先把对拍和真实回归边界钉死 |
| 主线 | 这轮已经把 `_tmp_p256_mul_add_stage_cmp_probe` 全部收绿，并证明 `fieldSubTrustedInto` 这层 probe 包装是假根；同时也证伪了两条更重的接线：`pointJacobianAddAffineFixedInPlace` 全 trusted 直连会把 `pubkey/sign` 拉回 `19s/23s+`，`Crash -> trusted` 直连更会把 `pubkey` 拉到 `30.73s`。下一刀不能再整段接线，只能在 compare probe 保护下继续拆 `addAffine` 最重的非自乘乘法和值拷贝。 |
| 文件 | `v2/tests/contracts/_tmp_p256_mul_add_stage_cmp_probe.cheng` `v2/tests/contracts/_tmp_p256_mul_add_cmp_probe.cheng` `src/std/crypto/ecnist.cheng` `task_plan.md` `progress.md` `findings.md` |
| 不做 | 不再整段切 `pointJacobianAddAffineFixedInPlace`；不再把 `Crash` 包装层整体换成 trusted；不刷 fixed-point；不碰 `kinv`；不碰窗口表和初始化 |
| 验收 | `_tmp_p256_mul_add_stage_cmp_probe_bin` 继续 `ok`；`_tmp_p256_mul_add_cmp_probe_bin` 继续 `ok`；完整主 gate 前台通过；active 热路径不接受任何比当前基线更慢的接线 |

| 项目 | 内容 |
|---|---|
| 目标 | 继续压 `ECDSA/P-256`，但彻底停止在 active `ecnist` 上试 `trusted scratch mul` 直连 |
| 主线 | 这轮又证伪了两条更窄的路：`pointJacobianAddAffineFixedInPlace` 里只换 7 次 `mul` 到 shared-scratch trusted 路，correctness 虽然全绿，但 `mul_add` 和整链都明显回归；再把这 7 次 `mul` 直接内联到底层 `pfix` 也一样更慢。`pointJacobianDoubleFixedInPlace` 的同类 3 次 `mul` 更是直接把 compare probe 跑崩。下一刀不能再碰 active `trusted scratch`，只该在 pure `Crash/Value` 路内继续拆 `addAffine/double` 的值流和临时对象。 |
| 文件 | `src/std/crypto/ecnist.cheng` `v2/tests/contracts/_tmp_p256_mul_add_probe.cheng` `v2/tests/contracts/_tmp_p256_mul_add_cmp_probe.cheng` `v2/tests/contracts/_tmp_p256_mul_add_stage_cmp_probe.cheng` `v2/tests/contracts/_tmp_p256_mul_double_cmp_probe.cheng` `task_plan.md` `progress.md` `findings.md` |
| 不做 | 不再给 `addAffine/double` 接任何 `trusted scratch mul`；不再用 helper 版或 inline 版重试同一思路；不刷 fixed-point；不碰 `kinv`；不碰窗口表和初始化 |
| 验收 | `_tmp_p256_mul_add_cmp_probe_bin` 和 `_tmp_p256_mul_double_cmp_probe_bin` 都继续 `ok`；完整主 gate 继续前台通过；下一刀只有在 pure `Crash/Value` 路出现真实净收益时才保留 |

| 项目 | 内容 |
|---|---|
| 目标 | 继续压 `ECDSA/P-256`，但停止在 pure 热链里试 `out-parameter` fixed-record 路 |
| 主线 | 这轮试了 `p256_fixed/ecnist` 的 pure `Into/out-parameter` 路，想直接砍掉 `Result[P256Fixed] + Value(res) + record return`。结果已经证伪：字段级 direct `add/sub/mul/square` 对拍本身能过，但一旦进入组合层 `double/quad/eight` 和 `pointJacobianDoubleFixedInPlace/pointJacobianAddAffineFixedInPlace`，就会出现 `step=0` 偏差，补完自别名拆解后还会直接崩。下一刀不能再碰这条 `out-parameter` 热路，要回算法级最短路。 |
| 文件 | `src/std/crypto/p256_fixed.cheng` `src/std/crypto/ecnist.cheng` `v2/tests/contracts/_tmp_p256_mul_double_cmp_probe.cheng` `v2/tests/contracts/_tmp_p256_mul_add_cmp_probe.cheng` `task_plan.md` `progress.md` `findings.md` |
| 不做 | 不再尝试 `p256FieldFixed*CrashInto`、`p256Fixed*Into` 作为 active 热路；不再赌当前普通程序执行面会把 fixed-record `out-parameter` 自动编快 |
| 验收 | 生产代码整块撤回，`_tmp_p256_mul_double_cmp_probe_bin` 和 `_tmp_p256_mul_add_cmp_probe_bin` 回到 `ok`，完整主 gate 继续前台通过；下一步改成更强的固定基点算法评估与落地 |

| 项目 | 内容 |
|---|---|
| 目标 | 继续完成非 VM 链节点真实运行，先把 `cheng-quic` 在正确 `var` 语义下重新编通，再把挂住的状态树同步定位到普通程序运行面真根 |
| 主线 | 这轮已经把 `v2/cheng-quic/src/connection.cheng` 里只读 `copyBytesRange` 的 `Bytes/var Bytes` 双重载收成单一 `Bytes` 版本，`chain_node.cheng` 和 `chain_state_tree_sync_smoke.cheng` 都重新 `system-link-exec` 真链过了，`chain_node_test` 也已经真跑通 `mint/balance`。下一刀不再猜链算法，直接沿 `chain_state_tree_sync_smoke_bin` 的运行挂点继续查普通 `program` 轨。 |
| 文件 | `v2/cheng-quic/src/connection.cheng` `v2/cheng-quic/src/project/chain_node.cheng` `v2/cheng-quic/src/tests/chain_state_tree_sync_smoke.cheng` `task_plan.md` `progress.md` `findings.md` |
| 不做 | 不放宽 `var` 语义；不恢复 `Bytes/var Bytes` 假重载；不把链算法问题和普通程序运行时问题混在一起；不再用会留下悬挂进程的后台探测 |
| 验收 | `chain_node.cheng` 和 `chain_state_tree_sync_smoke.cheng` 必须继续前台真链过；`chain_node_test balance/mint` 必须真跑；完整主 gate 必须前台通过；然后再只盯 `chain_state_tree_sync_smoke_bin` 的真实运行挂点 |

| 项目 | 内容 |
|---|---|
| 目标 | 继续压普通 `program` 运行面，把 `chain_state_tree_sync_smoke` 的热点从名字解析大链继续往下推 |
| 主线 | 这轮已经在 `src/runtime/native/system_helpers_stdio_bridge.c` 把 `top_level_tag`、可见项 cache、`op.kind_tag` 都接进去了；最新一刀再把 `builtin` 例程落成正式 `builtin_tag` 数据面，并且只让 `builtin/importc` 进入 `driver_c_prog_try_builtin`。普通 Cheng 函数现在不再白扫整串 builtin 字符串。下一刀直接打 `driver_c_prog_zero_value_from_type/driver_c_prog_zero_value_from_type_decl` 的预解码和零值原型，不再回头碰 `try_builtin`。 |
| 文件 | `src/runtime/native/system_helpers_stdio_bridge.c` `v2/tests/contracts/compiler_core_system_link_exec.expected` `task_plan.md` `progress.md` `findings.md` |
| 不做 | 不再让普通函数进入 `try_builtin`；不再用后台 smoke；不把 `zero_value` 和链算法问题混在一起 |
| 验收 | `chain_node_test balance` 必须继续返回正确值；`chain_state_tree_sync_smoke.cheng` 必须继续前台真链过；完整主 gate 必须前台通过；然后再用前台 sample 确认热点已从 `try_builtin` 继续下沉 |

| 项目 | 内容 |
|---|---|
| 目标 | 继续压纯 Cheng `P-256/ECDSA` 热核，但只保留真正同时改善 `pubkey/sign` 的改动 |
| 主线 | 这轮已经前台证伪 `pointMulGeneratorJacobianComb6FixedInto(...)` 的 `digits[]` 预计算：correctness 全绿，但 3 次中位数变成 `pubkey=1.3587s`、`sign=2.0194s`、`mul=1.5392s`、`kinv=0.7741s`、`comb=1.1910s`。和稳定基线 `pubkey=1.2830s`、`sign=1.8210s`、`mul=1.4628s`、`kinv=0.7521s` 比，只有 `comb` 局部更快，总链路反而更慢，所以已整块撤回，生产路径回到稳定版。下一刀不再做“预计算数组 + 起始扫描”，而是直接压 `p256ScalarFixedComb6Digit(...)` 本体和 `comb6` 热循环里的 digit 提取成本。 |
| 文件 | `src/std/crypto/ecnist.cheng` `v2/tests/contracts/_tmp_p256_pubkey_scalar_probe.cheng` `v2/tests/contracts/_tmp_ecdsa_sign_probe.cheng` `v2/tests/contracts/_tmp_ecdsa_mul_probe.cheng` `v2/tests/contracts/_tmp_ecdsa_kinv_probe.cheng` `v2/tests/contracts/_tmp_ecdsa_mul_comb_probe.cheng` `task_plan.md` `progress.md` `findings.md` |
| 不做 | 不把 `digits[]` 预计算继续留在 active 路径；不因为 `comb` 局部更快就忽略 `pubkey/sign` 总回归；不先刷主 gate；不碰 `kinv` 支线 |
| 验收 | correctness probe 继续全绿；任何新改动都必须让 `pubkey/sign/mul` 至少不差于稳定基线后才允许保留 |

| 项目 | 内容 |
|---|---|
| 目标 | 先把 `chain_state_tree_sync_smoke` 当前的 `var arg not ref caller=chainAntiEntropyEventLines callee=std/seqs.add` 真根静态收掉，再决定是否进入前台小窗口验证 |
| 主线 | 真根不在链算法，也不在 source lowering，而在 `stage0` 的 `compiler_core_program_lower_expr(call)`：它之前按“正在生成中的 `low_plan`”反查 callee 参数签名，callee 只要排在后面，`var_param` 就会丢成空串，最后把本地数组按值降成 `load_local + materialize`，运行时才炸 `var arg not ref`。这轮先把 stage0 改成按完整 `program/itemId` 查真实 routine 参数签名。 |
| 文件 | `v2/bootstrap/cheng_v2c_tooling.c` `task_plan.md` `progress.md` `findings.md` |
| 不做 | 先不编译；先不跑 smoke；不在 runtime 层做自动 ref 兜底；不把这条问题继续误判成 `anti_entropy/node` 算法问题 |
| 验收 | 静态上 `compiler_core_program_lower_expr(call)` 不再依赖半成品 `low_plan` 取 `param_kind`；记录同步后，再决定是否前台小窗口验证 |

| 项目 | 内容 |
|---|---|
| 目标 | 把 `LSMR.md` 同步成“愿景 + 当前实现状态”正式文档，并继续沿 `cheng-quic` 真实运行路径收根 |
| 主线 | 这轮已经真跑通 `chain_state_tree_sync_smoke`，并把 `anti_entropy` 的裸 `len(...)` 收成显式 `strings.len(...)`；`msquic_chain_smoke` 也已经前台真链接并越过 `lsmr/dispersal/broadcast/anti_entropy/consensus` 五段算法，新的最小根因为 `lsmr.cheng` 里整批裸 `add(...)` 还停在 `load_name|add`。同时已把 [LSMR.md](/Users/lbcheng/cheng-lang/v2/docs/LSMR.md) 顶部同步成“已完成/部分完成/未完成/代码落点/验证”的正式状态表。 |
| 文件 | `v2/cheng-quic/src/chain/anti_entropy.cheng` `v2/cheng-quic/src/tests/chain_state_tree_sync_smoke.cheng` `v2/cheng-quic/src/chain/lsmr.cheng` `v2/docs/LSMR.md` `task_plan.md` `progress.md` `findings.md` |
| 不做 | 不再把 `LSMR` 文档写成纯愿景；不对 `msquic_chain_smoke` 做兜底跳过；不把裸 `add/len` 这类可静态确定的解析问题继续留给 runtime |
| 验收 | [chain_state_tree_sync_smoke.cheng](/Users/lbcheng/cheng-lang/v2/cheng-quic/src/tests/chain_state_tree_sync_smoke.cheng) 前台真跑必须 `rc=0`；[msquic_chain_smoke.cheng](/Users/lbcheng/cheng-lang/v2/cheng-quic/src/tests/msquic_chain_smoke.cheng) 至少要稳定越过算法层并暴露下一处最小 runtime 根；[LSMR.md](/Users/lbcheng/cheng-lang/v2/docs/LSMR.md) 必须明确写出当前实现状态而不是只保留理论叙述 |

| 项目 | 内容 |
|---|---|
| 目标 | 把 [LSMR.md](/Users/lbcheng/cheng-lang/v2/docs/LSMR.md) 里剩下的 `大衍流转 PubSub / CSG 子图过滤 / 空间证明` 一口气补成正式算法模块和正式 smoke |
| 主线 | 这轮已经新增 `v2/cheng-quic/src/chain/pubsub.cheng`、`v2/cheng-quic/src/chain/csg.cheng`、`v2/cheng-quic/src/chain/location_proof.cheng`，并在 `v2/cheng-quic/src/tests/lsmr_advanced_features_smoke.cheng` 里把三块一起前台真编真跑通过。`v2/bootstrap/Makefile` 的 `lsmr-contracts` 也已经接入这条新 smoke，`make -j1 -C /Users/lbcheng/cheng-lang/v2/bootstrap lsmr-contracts` 前台通过。 |
| 文件 | `v2/cheng-quic/src/chain/types.cheng` `v2/cheng-quic/src/chain/pubsub.cheng` `v2/cheng-quic/src/chain/csg.cheng` `v2/cheng-quic/src/chain/location_proof.cheng` `v2/cheng-quic/src/tests/lsmr_advanced_features_smoke.cheng` `v2/tests/contracts/lsmr_advanced_features_smoke.expected` `v2/bootstrap/Makefile` `v2/docs/LSMR.md` `v2/docs/cheng-chain-mvp.md` `task_plan.md` `progress.md` `findings.md` |
| 不做 | 不把这三块硬塞进当前未全闭合的 QUIC runtime；不碰 VM 合约；不把“算法层已完成”伪装成“真实联网已经全部闭环” |
| 验收 | `lsmr_advanced_features_smoke_bin` 必须前台输出 `lsmr_advanced_features_smoke=ok`；`make -j1 -C /Users/lbcheng/cheng-lang/v2/bootstrap lsmr-contracts` 必须前台通过；[LSMR.md](/Users/lbcheng/cheng-lang/v2/docs/LSMR.md) 的状态表必须同步成“技术面已闭合、联网面仍部分完成” |

| 项目 | 内容 |
|---|---|
| 目标 | 先把 `msquic_chain_smoke` 当前 `packet too small` 这条假死根静态收掉，不编译、不跑 smoke，只做正确分片和队列建模 |
| 主线 | 真根已经收敛到 `msquicConnImplQueueData(...)` 把整块 `Bytes` 直接塞成单个 `FrameData`，而 `msquicFrameSize(...)` 还只算裸 payload，连 varint 头都没算。现在已改成两层精确模型：`frame_model` 先按真实 QUIC varint 编码长度计算 `MsQuicFrame` 大小；`connection_impl` 再按 `maxPacketSize - 16` 的真实帧预算把 `FrameData` 切片，并在 `native_runtime` 的 `pipeWrite` 上层按“当前 frame 队列还能装下多少字节”分批入队、每批立刻 flush+pump，避免一次写大 payload 先撞单帧上限，再撞 `128` 帧队列上限。 |
| 文件 | `v2/cheng-quic/src/core/frame_model.cheng` `v2/cheng-quic/src/core/connection_impl.cheng` `v2/cheng-quic/src/native_runtime.cheng` `task_plan.md` `progress.md` `findings.md` |
| 不做 | 这轮不编译；不跑 `msquic_chain_smoke`；不做基于报错重试的启发式切片；不放大 `maxPacketSize` 或 `msQuicMaxQueuedFrames` 去掩盖模型错误 |
| 验收 | 静态上 `FrameData` 编码长度必须和 `msquicPacketPayloadEncode(...)` 同构；`msquicConnImplQueueData(...)` 不能再生成单个必定塞不进包的 frame；`msquicNativePipeWrite(...)` 不能再一次性把超大 payload 全压进本地队列。下一轮再前台小窗口验证。 |

| 项目 | 内容 |
|---|---|
| 目标 | 把 `compiler-core-release -> full-selfhost` 重新收回全绿，并把这轮真实通过的 LSMR runtime smoke 固化进 contract |
| 主线 | 这轮真根不是 LSMR 算法，也不是 runtime 递归，而是 `stage0` 对 module-qualified overloaded `system.panic(...)` 的 imported-field 调用解析不稳。最短路已经落地：在 `src/std/system.cheng` 增非重载入口 `panicStr`，并把 `v2/src/compiler` 全量切到 `system.panicStr(...)`。这样不用继续给每个 compiler 文件补本地 wrapper。随后已把 `compiler_core_release/system_link/system_link_exec/system_link_exec_smoke/tooling_release/tooling_shared_plan/topology_shared_plan/network_selfhost/tooling_selfhost/selfhost_shared_plan/full_selfhost` 这批新 fixed-point 全部刷新，并前台重新跑完整 gate 收口。 |
| 文件 | `src/std/system.cheng` `v2/src/compiler/frontend/v2_source_parser.cheng` `v2/src/compiler/frontend/compiler_core_surface_ir_v2.cheng` `v2/src/compiler/semantic_facts/compiler_core_facts_v2.cheng` `v2/src/compiler/driver/release_artifact_v2.cheng` `v2/src/compiler/driver/manifest_resolver_v2.cheng` `v2/src/compiler/low_uir/compiler_core_lowering_v2.cheng` `v2/src/compiler/obj/obj_file_v2.cheng` `v2/bootstrap/Makefile` `v2/tests/contracts/*.expected` `task_plan.md` `progress.md` `findings.md` |
| 不做 | 不回头改 compiler overload 机制；不再给单个文件各自补 panic wrapper；不跑后台命令；不把 LSMR runtime smoke 再降回“只算算法层通过” |
| 验收 | `make -j1 -C /Users/lbcheng/cheng-lang/v2/bootstrap compiler-core-release compiler-core-system-link compiler-core-system-link-exec tooling-selfhost lsmr-contracts selfhost full-selfhost` 必须前台通过；`msquic_chain_smoke.expected` 和 `chain_state_tree_sync_smoke.expected` 必须继续稳定匹配；然后再继续打普通 program runtime 的下一条活根。 |

| 项目 | 内容 |
|---|---|
| 目标 | 把 `full-selfhost` 扩成正式 `program-selfhost`，让普通程序也由 Cheng 产物自己编自己跑，不再只证明“编译器能编自己” |
| 主线 | 这轮已经把 `program-selfhost-check` 贯穿 source/runtime/native/stage0 四层，并在 `Makefile` 里挂成正式 `program-selfhost` 目标，再纳入 `full-selfhost`。当前 gate 固定要求：stage2 编译器必须真编真跑 `lsmr_advanced_features_smoke`、`chain_state_tree_sync_smoke` 和 `chain_node balance/mint/balance`；同时继续证明 stage2/stage3 release-plan-exec-binary fixed point 相等，以及 `compiler_core` 运行面不再依赖外部 C provider。 |
| 文件 | `v2/src/tooling/cheng_tooling_v2.cheng` `v2/src/runtime/compiler_core_runtime_v2.cheng` `v2/src/runtime/compiler_core_native_dispatch.c` `src/runtime/native/system_helpers.h` `src/runtime/native/system_helpers_stdio_bridge.c` `v2/src/compiler/machine/machine_pipeline_v2.cheng` `v2/bootstrap/cheng_v2c_tooling.c` `v2/bootstrap/Makefile` `v2/tests/contracts/program_selfhost.expected` `v2/tests/contracts/compiler_core_release_artifact.expected` `v2/tests/contracts/compiler_core_system_link_plan.expected` `v2/tests/contracts/compiler_core_system_link_exec.expected` `v2/tests/contracts/full_selfhost.expected` `v2/docs/自举和性能.md` `task_plan.md` `progress.md` `findings.md` |
| 不做 | 不把 `program-selfhost` 降成单文件 probe；不在 runtime 层兜底外部 C provider；不把 `full-selfhost` 继续误当成“整条链路自举已完成” |
| 验收 | `make -j1 -C /Users/lbcheng/cheng-lang/v2/bootstrap program-selfhost` 必须前台通过；`full-selfhost` 必须继续前台通过；`program_selfhost.expected` 和下游 fixed-point 必须稳定匹配；然后下一刀只打 `program` 热路径和纯 Cheng 热核，不回头补语义闭环。 |

| 项目 | 内容 |
|---|---|
| 目标 | 继续完成 `program` 热路径和值表示，先把 typed `record` 的固定布局 lookup 从声明面一路接到运行面 |
| 主线 | 这轮已经在 `src/runtime/native/system_helpers_stdio_bridge.c` 里把 typed `record` 的固定布局初始化继续接到 runtime：`TypeDecl/ZeroPlan` 共享 `field_lookup` 会被 `zero_value_from_plan()/zero_record_shell_from_plan()` 直接复用，`MAKE_RECORD` 也会直接按 decl shape 初始化并在顺序一致时跳过字段名查找。随后 `compiler_core_system_link_exec/program_selfhost/full_selfhost` 已在当前 tree manifest `manifest_fnv1a64=fee6dad00582e08f` 下前台重新收口。下一刀不再碰这条已闭合链，直接打固定布局 `slot/shape` 和 aggregate `field/index update copy`。 |
| 文件 | `src/runtime/native/system_helpers_stdio_bridge.c` `v2/tests/contracts/compiler_core_system_link_exec.expected` `v2/tests/contracts/program_selfhost.expected` `v2/tests/contracts/full_selfhost.expected` `v2/docs/自举和性能.md` `task_plan.md` `progress.md` `findings.md` |
| 不做 | 不回头再做 `field name lookup` 兜底；不继续 rebuild typed record lookup；不把已过的 `program-selfhost/full-selfhost` 再当成当前主根 |
| 验收 | `make -j1 -C /Users/lbcheng/cheng-lang/v2/bootstrap compiler-core-release compiler-core-system-link compiler-core-system-link-exec tooling-selfhost lsmr-contracts selfhost full-selfhost` 必须前台通过；`pubkey/sign/mul/kinv` probe 必须以前台重编后的二进制重新量到真实中位数。 |

| 项目 | 内容 |
|---|---|
| 目标 | 继续把 `v3` 自举入口和 fresh bootstrap bridge 变成可诊断、可收敛的真工具链，不再允许 `rc=223` 和 `wrapper-source build` 失败变成黑盒 |
| 主线 | 这轮先把 [cheng_v3.sh](/Users/lbcheng/cheng-lang/v3/tooling/cheng_v3.sh) 的外层日志从 seed 内层 `out.compile.log` 同名冲突里拆出来，再把 [verify_backend_sidecar_cheng_fresh.sh](/Users/lbcheng/cheng-lang/src/tooling/cheng_tooling_embedded_scripts/verify_backend_sidecar_cheng_fresh.sh) 的 `wrapper-source build` 失败显式打印成 `rc/kind/hint/log`。随后继续顺着 fresh bridge 拆根：proof launcher 现在会尊重 `BACKEND_UIR_SIDECAR_DISABLE=1`，`backend_driver_currentsrc_sidecar_wrapper.sh` 也不再对 bootstrap proof surface 偷开 `wrapper_preserve_sidecar=1`。结果是 `v3` gate 已从“静默 timeout”推进成确定性的 `rc=223 kind=deterministic_exit_223`，并稳定暴露下一处真阻塞 `backend_driver sidecar: missing strict sidecar mode contract`。 |
| 文件 | `v3/tooling/cheng_v3.sh` `v3/tooling/build_backend_driver_v3.sh` `v3/tooling/README.md` `src/tooling/cheng_tooling_embedded_scripts/verify_backend_sidecar_cheng_fresh.sh` `src/tooling/cheng_tooling_embedded_scripts/backend_driver_currentsrc_sidecar_wrapper.sh` `src/tooling/cheng_tooling_embedded_scripts/verify_backend_selfhost_currentsrc_proof.sh` `src/tooling/cheng_tooling_embedded_scripts/verify_backend_selfhost_bootstrap_self_obj.sh` `artifacts/backend_selfhost_self_obj/probe_currentsrc_proof/cheng_stage0_currentsrc.proof` `task_plan.md` `progress.md` `findings.md` |
| 不做 | 不把 `223` 继续伪装成 `signal`；不再让 bootstrap bridge 用 timeout 吞掉真实错误；不回退到旧 `src/tooling/cheng_tooling.cheng` 当 `v3` 主入口 |
| 验收 | `sh /Users/lbcheng/cheng-lang/v3/tooling/cheng_v3.sh print-bootstrap` 必须直接打印 `rc=223 kind=deterministic_exit_223` 和 seed 原始日志；`sh /Users/lbcheng/cheng-lang/v3/tooling/bootstrap_bridge_v3.sh` 与 `sh /Users/lbcheng/cheng-lang/v3/tooling/run_slice_gate.sh` 必须稳定打印 `rc=223 kind=deterministic_exit_223` 和 `missing strict sidecar mode contract`，不再只报 timeout 黑盒。 |

| 项目 | 内容 |
|---|---|
| 目标 | 把 `v2` 已经踩过的性能坑和自举坑正式写进 `v3/docs`，并收成 `v3` 当前开发计划的硬约束 |
| 主线 | 这轮已新增 [v3/docs/README.md](/Users/lbcheng/cheng-lang/v3/docs/README.md)、[v3/docs/v2已踩性能和自举坑.md](/Users/lbcheng/cheng-lang/v3/docs/v2已踩性能和自举坑.md)、[v3/docs/自举和性能开发计划.md](/Users/lbcheng/cheng-lang/v3/docs/自举和性能开发计划.md)，把 `v2/docs/自举和性能.md`、`v2/docs/baguaCSG.md`、`v2/docs/LSMR.md`、当前 `findings.md` 和现有实现里的坑统一收成 `v3` 硬禁令和里程碑；[v3/README.md](/Users/lbcheng/cheng-lang/v3/README.md) 也已接上文档入口。 |
| 文件 | `v3/docs/README.md` `v3/docs/v2已踩性能和自举坑.md` `v3/docs/自举和性能开发计划.md` `v3/README.md` `lessons.md` `task_plan.md` `progress.md` `findings.md` |
| 不做 | 不把长篇计划继续塞进 `v3/README.md` 或 `v3/tooling/README.md`；不再用 `v3/doc` 这种错误目录名 |
| 验收 | `v3/docs` 目录必须存在且三份文档都能直接打开；计划文档必须显式写出 `stage2/stage3`、同机 C `1:1`、AOT-only、bagua/BPI sidecar-only 和 `program-selfhost + chain_node` 这些约束。 |

| 项目 | 内容 |
|---|---|
| 目标 | 按用户要求把 `v3` 之前的编译产物和旧编译工具链全部移除，然后只靠干净 seed 重新拉起 `v3` 的自举入口和性能基线 |
| 主线 | 这轮已经删除根目录 `artifacts/chengcache/build/dist`、根旧二进制 `backend_closedloop_fullspec`，并把 `v2/artifacts` 也整目录清空后重编。干净环境下，`make -C /Users/lbcheng/cheng-lang/v2/bootstrap all` 已重新产出 `v2/artifacts/bootstrap/cheng_v2_bootstrap` 和 `v2/artifacts/bootstrap/cheng_v2c`；`make -C /Users/lbcheng/cheng-lang/v3/bench/c_ref clean run` 和 `v3/tooling/scan_forbidden_hotpath.sh` 也已从零重跑通过。现在 `v3/tooling/bootstrap_bridge_v3.sh` 在纯净环境下直接失败为 `missing executable bridge driver: artifacts/backend_selfhost_self_obj/probe_currentsrc_proof/cheng_stage0_currentsrc.proof`，同时干净 `v2` seed 再跑 `tooling-selfhost-host` 仍然稳定卡 `release stdout mismatch`，手拆 `tooling_stage1_bootstrap` 继续会撞上 `program_entry_exec_plan_missing local_payload_*`。这说明旧产物之前确实遮住了真根，而真根正是 `v2` 老 bootstrap 里那条 `local_payload/exec_plan` 路。 |
| 文件 | `src/tooling/cheng_tooling_embedded_scripts/backend_seed_pure.sh` `src/tooling/cheng_tooling_embedded_scripts/verify_backend_selfhost_currentsrc_proof.sh` `src/tooling/cheng_tooling_embedded_scripts/verify_backend_selfhost_bootstrap_self_obj.sh` `v3/src/tooling/bootstrap_contracts.cheng` `task_plan.md` `progress.md` `findings.md` |
| 不做 | 不恢复任何旧 `artifacts` 或 `chengcache`；不拿旧 `probe_currentsrc_proof` 产物蒙混过关；不对 `tooling-selfhost-host` 的 `release stdout mismatch` 和 `local_payload` 断点做兜底跳过 |
| 验收 | `v2/artifacts/bootstrap/cheng_v2_bootstrap` 和 `v2/artifacts/bootstrap/cheng_v2c` 必须从零重编；`v3` C 基线必须从零重跑；`v3` bootstrap 必须在纯净环境下给出真实缺口，而不是继续吃旧 bridge driver。 |

| 项目 | 内容 |
|---|---|
| 目标 | 把 `v3/docs/cheng-plan-full.md`、`v3/docs/LSMR.md`、`v3/docs/baguaCSG.md`、`v3/docs/自举和性能.md` 收成一份统一的 `cheng v3` 特性矩阵和开发计划 |
| 主线 | 新总览必须把四份文档的边界彻底收正：`HIR/MIR/LIR` 才是权威语义，`bagua/BPI` 只做 sidecar；`LSMR/CSG/反熵/共识` 直接迁语义和 smoke，不直接搬 `v2` 的字符串实现壳；`自举/性能` 只认 `stage2/stage3 + 同机 C 1:1 + AOT-only + program-selfhost + chain_node`。同时把当前发现的结构冲突明写出来，尤其是 `v3` 现有链地址 digit 还写成 `0..7`，和洛书 `1..9` 语义不一致。 |
| 文件 | `v3/docs/cheng语言特性矩阵和开发计划.md` `v3/docs/README.md` `task_plan.md` `progress.md` `findings.md` |
| 不做 | 不删除原始四份文档；不把 `bagua/BPI` 重新提升成新 IR；不把 `v2` 的 canonical text、`payloadText`、`topicCid/payloadSummary` 文本壳带进 `v3` 计划 |
| 验收 | `v3/docs/cheng语言特性矩阵和开发计划.md` 必须明确写出特性矩阵、统一边界、阶段计划、正式 gate、禁止回流项，并被 `v3/docs/README.md` 收进索引。 |

| 项目 | 内容 |
|---|---|
| 目标 | 把 `v3` bootstrap/tooling 主线彻底切到 `v3/bootstrap` 极小 seed，并让整条 `v3` slice gate 在不依赖旧 `probe_currentsrc_proof/sidecar/tooling_cmd` 的前提下真跑通过 |
| 主线 | 这轮已经把 `v3/tooling/{bootstrap_bridge_v3,build_backend_driver_v3,run_slice_gate,cheng_v3}.sh`、`v3/src/tooling/bootstrap_contracts.cheng`、`v3/src/backend/build_plan.cheng`、`v3/src/tests/bootstrap_contracts_smoke.cheng` 全部切到新主线。当前真实链路是：`cc` 先编 `v3/bootstrap/cheng_v3_seed.c` 得到 `cheng.stage0`，再由 `stage0 -> stage1 -> stage2 -> stage3` 连续 `compile-bootstrap`；`build_backend_driver_v3.sh` 直接用 `stage2` 物化 `artifacts/v3_backend_driver/cheng`；`run_slice_gate.sh` 只认 `scan + C baseline + bootstrap bridge + backend driver + bootstrap subset self-check + contract equivalence`，不再回落到旧 `tooling_cmd/cheng_tooling`。 |
| 文件 | `v3/bootstrap/cheng_v3_seed.c` `v3/tooling/cheng_v3.sh` `v3/tooling/bootstrap_bridge_v3.sh` `v3/tooling/build_backend_driver_v3.sh` `v3/tooling/run_slice_gate.sh` `v3/src/tooling/bootstrap_contracts.cheng` `v3/src/backend/build_plan.cheng` `v3/src/tests/bootstrap_contracts_smoke.cheng` `task_plan.md` `progress.md` `findings.md` |
| 不做 | 不再依赖 `probe_currentsrc_proof/cheng_stage0_currentsrc.proof`；不再调用旧 `verify_backend_sidecar_cheng_fresh.sh`；不再用旧 `tooling_cmd/cheng_tooling` 跑 `v3` 主 gate；不把 Mach-O 可执行字节硬比当 Darwin 下的 fixed-point 唯一口径 |
| 验收 | `sh /Users/lbcheng/cheng-lang/v3/tooling/bootstrap_bridge_v3.sh` 必须通过；`sh /Users/lbcheng/cheng-lang/v3/tooling/build_backend_driver_v3.sh` 必须通过；`sh /Users/lbcheng/cheng-lang/v3/tooling/run_slice_gate.sh` 必须通过；`sh /Users/lbcheng/cheng-lang/v3/tooling/cheng_v3.sh print-bootstrap` 和 `print-build-plan` 必须直接输出新 `v3/bootstrap` 路径，不再出现旧 proof 术语。 |

| 项目 | 内容 |
|---|---|
| 目标 | 把 `v3` 里最后残留的旧 bootstrap/tooling 引用彻底清零，并让入口代码、bootstrap 合同、README、实际 gate 三者完全一致 |
| 主线 | 这轮已经把 `v3/src/tooling/gate_main.cheng` 的 `run-smokes` 改成只认 `artifacts/v3_bootstrap/cheng.stage0~3` 的 `self-check`，删掉旧 `artifacts/tooling_cmd/cheng_tooling` 默认值；同时把 `v3/bootstrap/cheng_v3_seed.c` 和 `v3/bootstrap/stage1_bootstrap.cheng` 里的旧 proof 禁词改成泛化的 `legacy_proof_surface/legacy_sidecar_mode`，并把 `v3/README.md`、`v3/tooling/README.md`、`v3/bootstrap/README.md` 全部收正到 `seed -> stage0 -> stage1 -> stage2 -> stage3` 主线。随后 `rg` 扫描 `v3` 已确认零命中旧 proof/tooling 词，`bootstrap_bridge_v3.sh`、`build_backend_driver_v3.sh`、`cheng_v3.sh run-smokes`、`run_slice_gate.sh` 也都前台通过。 |
| 文件 | `v3/src/tooling/gate_main.cheng` `v3/bootstrap/cheng_v3_seed.c` `v3/bootstrap/stage1_bootstrap.cheng` `v3/README.md` `v3/tooling/README.md` `v3/bootstrap/README.md` `task_plan.md` `progress.md` `findings.md` |
| 不做 | 不回头恢复任何旧 proof/path 兼容描述；不把 README 写成与实际 gate 不一致的“假入口”；不再让 `v3` 活入口偷偷保留旧 `tooling_cmd` 回退 |
| 验收 | `rg -n \"probe_currentsrc_proof|stage2_proof|stage3_witness|currentsrc_proof|tooling_cmd/cheng_tooling|cheng_stage0_currentsrc\\.proof|sidecar contract\" /Users/lbcheng/cheng-lang/v3 -S` 必须零命中；`sh /Users/lbcheng/cheng-lang/v3/tooling/bootstrap_bridge_v3.sh`、`sh /Users/lbcheng/cheng-lang/v3/tooling/build_backend_driver_v3.sh`、`sh /Users/lbcheng/cheng-lang/v3/tooling/cheng_v3.sh run-smokes`、`sh /Users/lbcheng/cheng-lang/v3/tooling/run_slice_gate.sh` 必须全部前台通过。 |

| 项目 | 内容 |
|---|---|
| 目标 | 按 `v3/docs/cheng语言特性矩阵和开发计划.md` 并行推进当前执行顺序 `1-5`，把 `v3` 链语义固定布局主线和正式 host smoke gate 一次性接起来 |
| 主线 | 这轮已经把 `v3/src/chain` 扩成固定布局语义核：`lsmr_types/lsmr/anti_entropy/csg/pubsub/location_proof/consensus` 全部落地；同时新增 `lsmr_types_smoke/lsmr_locality_storage_smoke/lsmr_bagua_prefix_tree_smoke/anti_entropy_smoke/csg_smoke/pubsub_smoke/location_proof_smoke/consensus_smoke`，再用 `v3/tooling/run_v3_host_smokes.sh` 收成统一 host smoke 入口，并把 `run_slice_gate.sh` 正式接上这条链。当前 `fixed_surface/csg/consensus/pubsub/location_proof` 已经 host-run 通过，`chain_codec_binary_smoke` 则稳定把真阻塞暴露成 `driver_c program runtime: missing type decl for zero init type=int32 inst_owner=v3/tests/chain_codec_binary_smoke`；`anti_entropy` 和 `lsmr_*` 也都已经真编过，下一刀只该收这个宿主 runtime 缺口，不再回头修旧 proof/sidecar 或字符串壳。 |
| 文件 | `v3/cheng-package.toml` `v3/src/chain/binary_types.cheng` `v3/src/chain/codec_binary.cheng` `v3/src/chain/lsmr_types.cheng` `v3/src/chain/lsmr.cheng` `v3/src/chain/anti_entropy.cheng` `v3/src/chain/csg.cheng` `v3/src/chain/pubsub.cheng` `v3/src/chain/location_proof.cheng` `v3/src/chain/consensus.cheng` `v3/src/tests/chain_codec_binary_smoke.cheng` `v3/src/tests/fixed_surface_smoke.cheng` `v3/src/tests/lsmr_types_smoke.cheng` `v3/src/tests/lsmr_locality_storage_smoke.cheng` `v3/src/tests/lsmr_bagua_prefix_tree_smoke.cheng` `v3/src/tests/anti_entropy_smoke.cheng` `v3/src/tests/csg_smoke.cheng` `v3/src/tests/pubsub_smoke.cheng` `v3/src/tests/location_proof_smoke.cheng` `v3/src/tests/consensus_smoke.cheng` `v3/tooling/run_v3_host_smokes.sh` `v3/tooling/run_slice_gate.sh` `v3/README.md` `v3/tooling/README.md` `v3/docs/cheng语言特性矩阵和开发计划.md` `task_plan.md` `progress.md` `findings.md` |
| 不做 | 不把 host smoke 失败包装成通过；不把 compile-only 伪装成运行通过；不把 `ir_core` 这条无关老坑放到链 gate 前面抢失败位；不把 `v2` 文本链壳、`payloadText`、`BigInt/Bytes/Seq` 热链拉回 `v3` |
| 验收 | `sh /Users/lbcheng/cheng-lang/v3/tooling/run_v3_host_smokes.sh` 必须先稳定跑过 `fixed_surface/csg/consensus/pubsub/location_proof`，再把 `chain_codec_binary_smoke` 的真失败点固定暴露；`sh /Users/lbcheng/cheng-lang/v3/tooling/run_slice_gate.sh` 也必须前台复现同一个失败位，而不是再回到旧 bridge/tooling 黑盒。 |

| 项目 | 内容 |
|---|---|
| 目标 | 把 `v3` ordinary compile 从 `lowering` 再推进两层到 `object/native-link`，同时守住 `scan + host smokes + bootstrap + backend driver` 全绿 |
| 主线 | 这轮已经在 `v3` 新主线上补了 `backend/object_plan.cheng`、`backend/native_link_plan.cheng`，把 `system_link_exec` 从只导出 `source closure + runtime targets/provider + lowering inventory` 推进到同时导出 `primary_object/provider_object_paths/object_link_inputs/native_link_inputs`。`stage1_bootstrap`、`bootstrap_contracts`、`build_plan`、seed C 也已同步加入 `backend_object_plan_source/backend_native_link_plan_source`，并把 `ordinary_pipeline_state` 收正到 `native_link_plan_stub_missing_codegen`。当前 `bootstrap_bridge_v3.sh`、`build_backend_driver_v3.sh`、`scan_forbidden_hotpath.sh`、`run_v3_host_smokes.sh` 全部前台通过；`build_program_selfhost_v3.sh` 和 `build_chain_node_v3.sh` 现在统一稳定失败在 `v3 compiler: object and native link plans ready, machine code emission and final link not implemented`，说明真阻塞已经从 lowering 推进到机器码和最终链接。 |
| 文件 | `v3/src/backend/system_link_exec.cheng` `v3/src/backend/object_plan.cheng` `v3/src/backend/native_link_plan.cheng` `v3/src/backend/build_plan.cheng` `v3/src/tooling/bootstrap_contracts.cheng` `v3/src/tooling/compiler_runtime.cheng` `v3/src/tooling/compiler_main.cheng` `v3/bootstrap/stage1_bootstrap.cheng` `v3/bootstrap/cheng_v3_seed.c` `v3/src/tests/lowering_plan_smoke.cheng` `v3/src/tests/object_native_link_plan_smoke.cheng` `v3/src/tests/compiler_runtime_smoke.cheng` `v3/tooling/run_v3_host_smokes.sh` `v3/docs/cheng语言特性矩阵和开发计划.md` `task_plan.md` `progress.md` `findings.md` |
| 不做 | 不伪造 `.o` 或可执行产物；不把新 `outputKind` 再写回 `kind: str`；不回头修旧 proof/tooling；不把 `provider/native-link` 重新做成字符串命令壳；不把 host runtime 的限制误当成 `v3` 语义设计错误 |
| 验收 | `sh /Users/lbcheng/cheng-lang/v3/tooling/scan_forbidden_hotpath.sh` 必须通过；`sh /Users/lbcheng/cheng-lang/v3/tooling/run_v3_host_smokes.sh` 必须通过；`sh /Users/lbcheng/cheng-lang/v3/tooling/bootstrap_bridge_v3.sh` 和 `sh /Users/lbcheng/cheng-lang/v3/tooling/build_backend_driver_v3.sh` 必须通过；`sh /Users/lbcheng/cheng-lang/v3/tooling/build_program_selfhost_v3.sh` 与 `sh /Users/lbcheng/cheng-lang/v3/tooling/build_chain_node_v3.sh` 必须稳定报同一条 `object and native link plans ready...`；`sh /Users/lbcheng/cheng-lang/v3/tooling/run_slice_gate.sh` 必须在 `program-selfhost` 处复现同一失败位。 |

| 项目 | 内容 |
|---|---|
| 目标 | 继续把 `v3` ordinary compile 从“能发 `.o` 和最小可执行”推进到真正的函数体语义子集，并先把 ordinary 入口桥的运行时自旋真根收掉 |
| 主线 | 这轮已经在 `v3/bootstrap/cheng_v3_seed.c` 里把 ordinary entry bridge 和 `return_call_noarg_i32` 从 `bl callee; ret` 改成直接 `b callee`，因为旧写法会覆盖 `LR`，最后在入口桥上自旋。随后重新清空 `artifacts/v3_bootstrap`、`artifacts/v3_backend_driver`、`artifacts/v3_zero_exit`、`artifacts/v3_call_chain` 并从零重编；`build_zero_exit_v3.sh` 和新加的 `build_call_chain_v3.sh` 已经真编、真链、真跑通过，`run_v3_host_smokes.sh` 也重新前台全绿。现在 `build_program_selfhost_v3.sh` 仍稳定报 `v3 compiler: primary object body semantics missing`；`build_chain_node_v3.sh` 也稳定报同一个大类错误，并额外带多处 `primary_object_call_target_missing`。这说明 `.o/link/argv bridge` 已经收完，下一刀只该扩 ordinary body 语义，不回头再修入口壳。 |
| 文件 | `v3/bootstrap/cheng_v3_seed.c` `v3/src/tests/ordinary_call_chain_fixture.cheng` `v3/tooling/build_call_chain_v3.sh` `v3/tooling/run_slice_gate.sh` `v3/README.md` `v3/tooling/README.md` `v3/docs/cheng语言特性矩阵和开发计划.md` `task_plan.md` `progress.md` `findings.md` |
| 不做 | 不把尾调用再写回 `bl ...; ret`；不把 host smoke 通过误报成 ordinary compile 已完成；不回头再查 `.o/link/argv/contract` 假阻塞 |
| 验收 | `sh /Users/lbcheng/cheng-lang/v3/tooling/build_zero_exit_v3.sh` 和 `sh /Users/lbcheng/cheng-lang/v3/tooling/build_call_chain_v3.sh` 必须前台通过；`sh /Users/lbcheng/cheng-lang/v3/tooling/run_v3_host_smokes.sh` 必须前台全绿；`sh /Users/lbcheng/cheng-lang/v3/tooling/build_program_selfhost_v3.sh` 和 `sh /Users/lbcheng/cheng-lang/v3/tooling/build_chain_node_v3.sh` 必须继续稳定暴露 `primary object body semantics missing` 这一类真实下一层语义缺口。 |

| 项目 | 内容 |
|---|---|
| 目标 | 继续把 `v3` ordinary compile 的真缺口收窄到最小可达函数集合，避免 `program_selfhost/chain_node` 再被整包导入噪音淹没 |
| 主线 | 这轮已经在 `v3/bootstrap/cheng_v3_seed.c` 里补了 `import alias` 解析、调用边采集和 entry-reachable 函数裁剪。结果是 `build_program_selfhost_v3.sh` 的 lowering 集合已经从 `598` 个函数压到 `16` 个，只剩 `program_selfhost_smoke -> bootstrap_contracts -> path` 这条最短链；`build_chain_node_v3.sh` 也从 `1120` 个函数压到 `112` 个，直接暴露出 `chain_node_main -> chain_node -> consensus/lsmr/anti_entropy/fixed256` 这条可达主链。`build_backend_driver_v3.sh`、`build_zero_exit_v3.sh`、`build_call_chain_v3.sh`、`run_v3_host_smokes.sh`、`scan_forbidden_hotpath.sh` 都已重新前台通过；当前普通编译仍统一停在 `primary object body semantics missing`，但现在已经只剩真实语义缺口。 |
| 文件 | `v3/bootstrap/cheng_v3_seed.c` `v3/docs/cheng语言特性矩阵和开发计划.md` `v3/README.md` `v3/tooling/README.md` `task_plan.md` `progress.md` `findings.md` |
| 不做 | 不把 reachability 裁剪退回整包 lowering；不把 `std/os`、`std/strutils` 这些 helper 再当成必须整体真编的噪音入口；不把还未实现的 body semantics 伪装成“gate 通过” |
| 验收 | `sh /Users/lbcheng/cheng-lang/v3/tooling/build_backend_driver_v3.sh`、`sh /Users/lbcheng/cheng-lang/v3/tooling/build_zero_exit_v3.sh`、`sh /Users/lbcheng/cheng-lang/v3/tooling/build_call_chain_v3.sh`、`sh /Users/lbcheng/cheng-lang/v3/tooling/run_v3_host_smokes.sh`、`sh /Users/lbcheng/cheng-lang/v3/tooling/scan_forbidden_hotpath.sh` 必须前台通过；`build_program_selfhost_v3.sh` 的 `lowering_function_count` 必须稳定是 `16`；`build_chain_node_v3.sh` 的 `lowering_function_count` 必须稳定是 `112`。 |

| 项目 | 内容 |
|---|---|
| 目标 | 继续把 `v3` ordinary compile 从“最小 tail-call 子集”推进到 `program_selfhost` 真编真跑，并把 `chain_node` 的失败位收敛成纯函数体语义缺口 |
| 主线 | 这轮先在 `v3/bootstrap/cheng_v3_seed.c` 收掉 consteval 的栈爆和源码缓冲所有权，再把 multi-line signature 的解析边界收正成“完整读文件后第二遍匹配”，并跳过 `importc fn` 这类无函数体声明。随后修掉 consteval 生成汇编里 `bl _puts` 覆盖 `LR/x30` 的活 bug，最终让 `build_program_selfhost_v3.sh` 真编、真链、真跑通过。并行还把 no-arg tail-call 的 callee 解析从“同模块裸名字匹配”改成复用 lowering 已采集的完整 `callee_symbols[0]`，于是 `build_chain_node_v3.sh` 和 `run_slice_gate.sh` 里的 `primary_object_call_target_missing` 已消失，统一只剩 `primary_object_body_semantics_missing`。当前整条 gate 真实状态是：`scan_forbidden_hotpath.sh` 通过，`build_backend_driver_v3.sh` 通过，`build_zero_exit_v3.sh` 和 `build_call_chain_v3.sh` 通过，`build_program_selfhost_v3.sh` 通过，`run_v3_host_smokes.sh` 前台全绿，`run_slice_gate.sh` 稳定只死在 `chain_node 未接通`。 |
| 文件 | `v3/bootstrap/cheng_v3_seed.c` `task_plan.md` `progress.md` `findings.md` |
| 不做 | 不把 consteval 死循环伪装成脚本卡住；不再用同模块裸函数名猜 tail-call callee；不回头修已收掉的 `.o/link/argv` 假阻塞；不把 `chain_node` 现在的纯函数体缺口包装成 runtime 或 call graph 问题 |
| 验收 | `sh /Users/lbcheng/cheng-lang/v3/tooling/build_program_selfhost_v3.sh` 必须前台输出 `v3 program_selfhost_smoke ok`；`sh /Users/lbcheng/cheng-lang/v3/tooling/build_chain_node_v3.sh` 必须不再出现 `primary_object_call_target_missing`；`sh /Users/lbcheng/cheng-lang/v3/tooling/run_slice_gate.sh` 必须稳定只在 `chain_node` 阶段失败，并把失败位固定成 `primary_object_body_semantics_missing`。 |

| 项目 | 内容 |
|---|---|
| 目标 | 把 `v3` seed 的 ordinary reachable 图修正成真实依赖图，停止让 bare import 调用漏边，并把 `chain_node` 的 unsupported 前沿从截断的 8 条假主根放大成完整可执行主根。 |
| 主线 | 这轮在 `v3/bootstrap/cheng_v3_seed.c` 里做了两件硬修正：一是把 `CHENG_V3_MAX_UNSUPPORTED_DETAILS` 从 `8` 放大到 `64`；二是把 lowering 的调用边收集改成“两阶段”，先收函数，再按完整 lowering 库重新解析每个函数体里的调用。新的 bare call 解析规则不再默认把 `bytesAlloc/bytesLen/bytesSet/intToStr/rawmemCopy` 这类裸调用错判成当前模块，而是优先同模块精确命中，否则在当前 import 集里按“唯一已收集符号”解析到真实模块。结果是 `build_chain_node_v3.sh` 的 `lowering_function_count` 从 `110` 升到 `126`，新暴露出的真实 reachable 集已经包含 `std/rawbytes::*`、`std/rawmem_support::*`、`std/strings::intToStr`、`std/system::str*` 这些此前被漏掉的依赖。当前 `chain_node` 的真实失败不再是“最前 8 个函数”，而是普通函数体子集整体还没接上；日志前 64 个 unsupported 已明确覆盖 `std/system`、`rawbytes`、`bytes_layout`、`anti_entropy`、`lsmr`、`sha256` 这几层。 |
| 文件 | `v3/bootstrap/cheng_v3_seed.c` `task_plan.md` `progress.md` `findings.md` |
| 不做 | 不再拿被漏边裁瘦的 reachable 集做决策；不再把 bare import 调用当成当前模块函数；不再让 unsupported 明细只露 8 条就开始补语义；不回头修已经通过的 `build_backend_driver_v3.sh`、`run_v3_host_smokes.sh`、`scan_forbidden_hotpath.sh`。 |
| 验收 | `cc -std=c11 -O0 -Wall -Wextra -pedantic /Users/lbcheng/cheng-lang/v3/bootstrap/cheng_v3_seed.c -o /tmp/cheng_v3_seed_check` 必须无 warning 通过；`sh /Users/lbcheng/cheng-lang/v3/tooling/build_backend_driver_v3.sh` 必须通过；`sh /Users/lbcheng/cheng-lang/v3/tooling/build_chain_node_v3.sh` 必须仍稳定失败在 `primary_object_body_semantics_missing`，但 `lowering_function_count` 必须扩大到 `126`，`primary_object_unsupported_function_count` 必须扩大到 `64`；`sh /Users/lbcheng/cheng-lang/v3/tooling/run_v3_host_smokes.sh` 和 `sh /Users/lbcheng/cheng-lang/v3/tooling/scan_forbidden_hotpath.sh` 必须继续通过。 |

| 项目 | 内容 |
|---|---|
| 目标 | 让新的 reachable 修正和句型分类真正进入 `stage2/stage3` 自举链，并把 `chain_node` 的 ordinary 主根从“函数名单”推进成“句型族名单”。 |
| 主线 | 这轮已经先重跑 [bootstrap_bridge_v3.sh](/Users/lbcheng/cheng-lang/v3/tooling/bootstrap_bridge_v3.sh) 刷新 `cheng.stage0~3`，再重跑 [build_backend_driver_v3.sh](/Users/lbcheng/cheng-lang/v3/tooling/build_backend_driver_v3.sh) 刷新 [artifacts/v3_backend_driver/cheng](/Users/lbcheng/cheng-lang/artifacts/v3_backend_driver/cheng)。刷新后 [chain_node.compile.log](/Users/lbcheng/cheng-lang/artifacts/v3_chain_node/chain_node.compile.log) 已经不再把前 64 项都标成 `unsupported`，而是分裂成 `stmt_call`、`stmt_var`、`stmt_let`、`stmt_if`、`stmt_for`、`return_expr`。这一步把下一刀从“继续猜 blocker 函数”收成了“按句型族补 ordinary body semantics”。 |
| 文件 | `v3/bootstrap/cheng_v3_seed.c` `v3/tooling/bootstrap_bridge_v3.sh` `v3/tooling/build_backend_driver_v3.sh` `artifacts/v3_chain_node/chain_node.compile.log` `task_plan.md` `progress.md` `findings.md` |
| 不做 | 不再修改 seed 后直接跳过 bootstrap 链刷新；不再把 compile log 里的 `body_kind` 全部当成一类；不再围着旧的 `print/panic/byteBuf*` 8 项做点状修补。 |
| 验收 | `sh /Users/lbcheng/cheng-lang/v3/tooling/bootstrap_bridge_v3.sh` 必须通过；`sh /Users/lbcheng/cheng-lang/v3/tooling/build_backend_driver_v3.sh` 必须通过；`sh /Users/lbcheng/cheng-lang/v3/tooling/build_chain_node_v3.sh` 的 `first unsupported function` 必须仍指向 `v3ChainNodeMainPrint`，但 `body_kind` 必须变成 `stmt_call`，且 `primary_object_unsupported_body_kinds` 必须出现 `stmt_call/stmt_var/stmt_let/stmt_if/stmt_for/return_expr` 这些真实句型。 |

| 项目 | 内容 |
|---|---|
| 目标 | 把 `chain_node` 的 ordinary blocker 再往前推一层，并把“缺哪种 ABI 形态”直接写进 compile log，停止只看函数名和句型。 |
| 主线 | 这轮在 `v3/bootstrap/cheng_v3_seed.c` 给 lowering 补了参数名、参数类型、返回类型和 ABI 分类，并把 `primary_object_unsupported_abi` 一起写进 `chain_node.compile.log`。随后重跑 `bootstrap_bridge_v3.sh` 和 `build_backend_driver_v3.sh`，新的 `build_chain_node_v3.sh` 已能直接报出 `first unsupported function=... abi=...`。同时顺手把 [chain_node_main.cheng](/Users/lbcheng/cheng-lang/v3/src/project/chain_node_main.cheng) 里的 `v3ChainNodeMainPrint` 空包装删掉，并把 [fixed256.cheng](/Users/lbcheng/cheng-lang/v3/src/std/crypto/fixed256.cheng) 里的 `panicStr` 收成 `panic`，让 blocker 直接前移到 `v3ChainNodeMainSelfTest`。当前真实前沿已经明确：`chain_node` 现在先卡 `stmt_var` 主体，ABI 主要先卡 `str/record/Bytes/ByteBuf/array` 这类 composite 形态；`program_selfhost` 继续通过，`run_v3_host_smokes.sh` 和 `scan_forbidden_hotpath.sh` 继续全绿。 |
| 文件 | `v3/bootstrap/cheng_v3_seed.c` `v3/src/project/chain_node_main.cheng` `v3/src/std/crypto/fixed256.cheng` `artifacts/v3_chain_node/chain_node.compile.log` `task_plan.md` `progress.md` `findings.md` |
| 不做 | 不再围着 `v3ChainNodeMainPrint` 这个 `str` 包装函数原地打转；不在 ABI 未定的情况下先碰 `Bytes/ByteSpan/ByteBuf/FixedBytes32/V3LsmrAddress` 的按值 wrapper；不把 host smoke 绿灯误当成 `chain_node` ordinary 已通。 |
| 验收 | `cc -std=c11 -O0 -Wall -Wextra -pedantic /Users/lbcheng/cheng-lang/v3/bootstrap/cheng_v3_seed.c -o /tmp/cheng_v3_seed_check` 必须无 warning；`sh /Users/lbcheng/cheng-lang/v3/tooling/bootstrap_bridge_v3.sh` 和 `sh /Users/lbcheng/cheng-lang/v3/tooling/build_backend_driver_v3.sh` 必须通过；`sh /Users/lbcheng/cheng-lang/v3/tooling/build_chain_node_v3.sh` 的首个 blocker 必须前移到 `v3ChainNodeMainSelfTest`，并打印 `abi=ret=int32/i32 params=-`；`sh /Users/lbcheng/cheng-lang/v3/tooling/run_v3_host_smokes.sh` 和 `sh /Users/lbcheng/cheng-lang/v3/tooling/scan_forbidden_hotpath.sh` 必须继续通过。 |

| 项目 | 内容 |
|---|---|
| 目标 | 把 `chain_node` 的 ordinary blocker 从“复合返回值错归属 + composite param 总拒绝”继续推进到真正的复合 ABI/body semantics 主层，同时守住 `bootstrap/backend_driver/program_selfhost/host_smokes` 全绿。 |
| 主线 | 这轮在 `v3/bootstrap/cheng_v3_seed.c` 先把 internal callee 的 `param_types/return_type` 改成按 callee 自己模块和 import alias 归一化，收掉了 `V3ChainNode -> chain_node_main::V3ChainNode` 的错归属；随后继续补了两层最小 ordinary 语义：一是 `composite` 参数槽位允许按地址进入函数体，不再在 emitter 入口直接拒绝；二是本地复合值的单层标量字段读取 `base.field` 已接通到类型推断和标量 codegen。现在 `build_chain_node_v3.sh` 已不再报 `V3ChainNode` layout/local slot 失败，也不再整批死在 `emit reject composite param`，新的最前沿已经收敛成 `std/crypto/sha256::getU32BE` 里的 `let b0: int64 = int64(bytesGet(data, offset))`。 |
| 文件 | `v3/bootstrap/cheng_v3_seed.c` `artifacts/v3_chain_node/chain_node.compile.log` `task_plan.md` `progress.md` `findings.md` |
| 不做 | 不再按 `Bytes/str/FixedBytes32` 各自发明一套 ABI；不回头修已经收掉的 `V3ChainNode` 错模块归一化；不把 host smoke 绿灯误当成 `chain_node` ordinary 已通。 |
| 验收 | `cc -std=c11 -O0 -Wall -Wextra -pedantic /Users/lbcheng/cheng-lang/v3/bootstrap/cheng_v3_seed.c -o /tmp/cheng_v3_seed_check` 必须无 warning；`sh /Users/lbcheng/cheng-lang/v3/tooling/bootstrap_bridge_v3.sh`、`sh /Users/lbcheng/cheng-lang/v3/tooling/build_backend_driver_v3.sh`、`sh /Users/lbcheng/cheng-lang/v3/tooling/build_program_selfhost_v3.sh`、`sh /Users/lbcheng/cheng-lang/v3/tooling/run_v3_host_smokes.sh` 必须前台通过；`sh /Users/lbcheng/cheng-lang/v3/tooling/build_chain_node_v3.sh` 必须稳定只暴露新的 `getU32BE -> bytesGet(data, offset)` 真阻塞。 |

| 项目 | 内容 |
|---|---|
| 目标 | 把 `assert/echo` 和“复合临时实参无地址”这两个 ordinary 真根直接收掉，并把 `chain_node` 的失败位推进到下一层真实语义缺口。 |
| 主线 | 这轮在 `v3/bootstrap/cheng_v3_seed.c` 做了三件硬改动：一是 ordinary 里的 bare `assert/echo` 不再走普通调用解析，而是直接做语句级 builtin lowering；`assert(cond, "msg")` 现在会先算布尔条件，失败时输出确定性字符串再 `_exit(1)`，`echo("msg")` 直接发 `_puts`。二是普通调用和 `call_into_slot` 都改成“先按 call-depth 把参数落到调用暂存区，再统一装入 `x0..x7` 调用”，不再让 nested call 冲掉前一个参数。三是给复合实参加了显式临时槽规划和 materialize 路径，`v3AntiEntropySignatureCid(local)`、`layout.fixedBytes32ToBytes(value)` 这类复合临时表达式现在会先落到可取址 slot 再传地址。结果是：`build_program_selfhost_v3.sh` 继续通过，`run_v3_host_smokes.sh` 继续全绿，`build_chain_node_v3.sh` 已经不再出现 `scalar call resolve failed ... callee=assert`，也不再出现 `scalar call composite-arg local missing ...`。当前 `chain_node` 的真实主根已经前移到更大一层：大量 `composite return function` 和 `composite field projection` 还没进 ordinary emitter，典型暴露面是 compile log 里仍然成片的 `ret=.../composite` 函数和 `mintCidRes.value`、`served.tipEventCid` 这类复合字段路径。 |
| 文件 | `v3/bootstrap/cheng_v3_seed.c` `artifacts/v3_chain_node/chain_node.compile.log` `task_plan.md` `progress.md` `findings.md` |
| 不做 | 不把 `assert/echo` 从 intrinsic 名单里删掉去碰重载解析；不再让普通调用边走边塞寄存器；不再让复合临时实参靠字符串精确命中 local 名字。 |
| 验收 | `cc -std=c11 -O0 -Wall -Wextra -pedantic /Users/lbcheng/cheng-lang/v3/bootstrap/cheng_v3_seed.c -o /tmp/cheng_v3_seed_check` 必须通过；`sh /Users/lbcheng/cheng-lang/v3/tooling/bootstrap_bridge_v3.sh`、`sh /Users/lbcheng/cheng-lang/v3/tooling/build_backend_driver_v3.sh`、`sh /Users/lbcheng/cheng-lang/v3/tooling/build_program_selfhost_v3.sh`、`sh /Users/lbcheng/cheng-lang/v3/tooling/run_v3_host_smokes.sh` 必须前台通过；`sh /Users/lbcheng/cheng-lang/v3/tooling/build_chain_node_v3.sh` 的 compile log 里必须不再出现 `callee=assert` 和 `composite-arg local missing`。 |

| 项目 | 内容 |
|---|---|
| 目标 | 把 `sret/x8`、复合本地槽位零初始化、赋值语句、复合 return 和字段路径读写接进 ordinary 主链，把 `chain_node` 的真阻塞从复合 ABI 前移到控制流。 |
| 主线 | 这轮在 `v3/bootstrap/cheng_v3_seed.c` 收了五层硬语义：一是复合返回统一改成 `x8 sret`，callee 进函数就把 `x8` spill 到隐藏本地槽位，不再把隐藏返回地址塞进 `x0` 冲掉用户参数；二是新增 `var/let` 无初始化声明解析和零初始化发码，复合本地槽位会按固定布局清零；三是新增普通赋值语句解析，已经支持 `local = expr`、`local.field = expr`，其中复合值会直接走 address materialize，不再只能走 call wrapper；四是补了 `Result[T]` 字段布局、`store_id` 字段别名和多层字段路径解析，`mintCidRes.ok/value`、`served.tipEventCid` 这类读取已经有统一数据面；五是复合表达式 materialize 现在支持 `local`、`local.field`、字符串字面量、复合调用、构造器和 `[]` 空序列。结果是：`bootstrap_bridge_v3.sh`、`build_backend_driver_v3.sh`、`build_program_selfhost_v3.sh`、`run_v3_host_smokes.sh`、`scan_forbidden_hotpath.sh` 全部继续通过；`build_chain_node_v3.sh` 的首个 blocker 已经稳定前移到 `std/system::strViewWithFlags` 的 `stmt_if`，说明 `chain_node` 当前第一堵墙已经不是复合 ABI，而是真控制流。 |
| 文件 | `v3/bootstrap/cheng_v3_seed.c` `artifacts/v3_chain_node/chain_node.compile.log` `task_plan.md` `progress.md` `findings.md` |
| 不做 | 不回退到 `x0` 伪隐藏参数；不再用裸字符串猜字段类型；不把复合 return 继续强塞进 `v3_codegen_expr_scalar`；不把 `if/for/while` 和复合 ABI 混成一个问题。 |
| 验收 | `cc -std=c11 -O0 -Wall -Wextra -pedantic /Users/lbcheng/cheng-lang/v3/bootstrap/cheng_v3_seed.c -o /tmp/cheng_v3_seed_check` 必须通过；`sh /Users/lbcheng/cheng-lang/v3/tooling/bootstrap_bridge_v3.sh`、`sh /Users/lbcheng/cheng-lang/v3/tooling/build_backend_driver_v3.sh`、`sh /Users/lbcheng/cheng-lang/v3/tooling/build_program_selfhost_v3.sh`、`sh /Users/lbcheng/cheng-lang/v3/tooling/run_v3_host_smokes.sh`、`sh /Users/lbcheng/cheng-lang/v3/tooling/scan_forbidden_hotpath.sh` 必须通过；`sh /Users/lbcheng/cheng-lang/v3/tooling/build_chain_node_v3.sh` 必须稳定把 `first unsupported function` 收敛到 `stmt_if`。 |

| 项目 | 内容 |
|---|---|
| 目标 | 把 `chain_node` 的 ordinary 真阻塞从“索引读写/索引复合返回”继续前移到复合临时实参、非空序列字面量和 `add(seq,val)`。 |
| 主线 | 这轮在 `v3/bootstrap/cheng_v3_seed.c` 新增了 `v3_parse_index_access_expr(...)` 和 `v3_emit_index_access_address(...)`，并把它们接进 `v3_prepare_expr_call_state(...)`、`v3_infer_expr_type(...)`、`v3_codegen_expr_scalar(...)`、`v3_materialize_composite_expr_into_address(...)`，同时给 indexed lvalue 赋值补了写回路径。前台重跑 `cc -std=c11 -O0 -Wall -Wextra -pedantic`、`bootstrap_bridge_v3.sh`、`build_backend_driver_v3.sh`、`build_chain_node_v3.sh` 后，[chain_node.compile.log](/Users/lbcheng/cheng-lang/artifacts/v3_chain_node/chain_node.compile.log) 已经不再出现 `index.accountHeads[pos]`、`index.balances[pos]`、`lt.v3AppendFixed32(buf, index.accountHeads[pos])` 这些索引错误，新的第一批明确失败已经前移成三组：`bytesFromString(...)` 作为 `layout.byteBufAppendBytes(...)` 的复合临时实参、`lt.v3HashInts(\"...\", [assetId, accountId])` 的非空序列字面量、以及 bare `add(seq,val)` 与 `std/seqs::{add,setLen}` 自身 ordinary 语义。 |
| 文件 | `v3/bootstrap/cheng_v3_seed.c` `artifacts/v3_chain_node/chain_node.compile.log` `task_plan.md` `progress.md` `findings.md` |
| 不做 | 不再把索引访问当主阻塞；不回头再改刚收住的 `x8/sret`；不为过当前日志写特判绕开 `bytesFromString/[...]/add` 的真实语义。 |
| 验收 | `cc -std=c11 -O0 -Wall -Wextra -pedantic /Users/lbcheng/cheng-lang/v3/bootstrap/cheng_v3_seed.c -o /tmp/cheng_v3_seed_check` 必须通过；`sh /Users/lbcheng/cheng-lang/v3/tooling/bootstrap_bridge_v3.sh` 和 `sh /Users/lbcheng/cheng-lang/v3/tooling/build_backend_driver_v3.sh` 必须通过；`sh /Users/lbcheng/cheng-lang/v3/tooling/build_chain_node_v3.sh` 的 compile log 必须不再出现 `index.accountHeads[pos]`、`index.balances[pos]`、`lt.v3AppendFixed32(buf, index.accountHeads[pos])`。 |
