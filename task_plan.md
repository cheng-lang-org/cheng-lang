# 当前任务

| 项目 | 内容 |
|---|---|
| 目标 | 继续把 `cheng v3` 收成“单一规范化前端 + typed lowering 真源 + 统一复合值/ABI 规则”。当前主攻已经推进到 browser ABI 的 `rule -> bridge spec -> source-aware emitter`。 |
| 状态 | 进行中。`normalized expr`、`typed expr fact`、`lowering rule`、复合值地址优先规则已落地；`Bytes[] add/setLen`、`exe @exportc`、`xor`、全局 `len(str/Bytes/seq)` 已打通；browser codec 现已把 schema、generic bridge、export/error 模板和 source-aware bridge emitter 都收进共享 codegen，并且 `compiler_csg/lowering_plan/codegen` 已共用同一份 browser ABI 真源。 |
| 当前主线 | 1. 前端已统一到单一规范化声明/表达式层。2. `compiler_csg + lowering_plan` 已共用 `typed fact + lowering rule`。3. seed/native/wasm 已开始共用“复合值显式地址 + region copy”规则。4. browser `content codec` 现已把 `bytes/text/optional/handle/result/error` 的 schema、读输入、写输出、error/export 和 source-aware bridge emitter 都收进共享 codegen。5. browser ABI 现已新增通用 `bridge spec` 生成层，`compiler_csg/lowering_plan/codegen` 都吃这同一份生成结果。 |
| 本轮动作 | 在 `browser_abi_rule` 里新增 per-bridge emitter 行；`browser_content_codec_codegen` 新增 `sourcePath + ruleName` 的标量 bridge key/helper/emit API；`browser_abi_rule_smoke` 改成通过 source-aware emitter 对齐 `compiler_csg`，不再直接搬复合 bridge spec。 |
| 涉及文件 | `task_plan.md` `progress.md` `findings.md` `lessons.md` `v3/src/lang/browser_abi_rule.cheng` `v3/src/libp2p/browser/browser_content_codec_rule_schema.cheng` `v3/src/libp2p/browser/browser_content_codec_codegen.cheng` `v3/src/tooling/compiler_csg.cheng` `v3/src/backend/lowering_plan.cheng` `v3/src/tests/browser_content_codec_rule_schema_smoke.cheng` `v3/src/tests/browser_content_codec_codegen_smoke.cheng` `v3/src/tests/browser_abi_rule_smoke.cheng` |
| 不做 | 不新开 worktree；不碰无关脏改；不把 browser ABI 假装成“已自动生成”；不把 `Bytes[]` 缺口继续留在业务侧绕开。 |
| 验收 | `artifacts/v3_bootstrap/cheng.stage3 run-host-smokes --compiler:artifacts/v3_bootstrap/cheng.stage3 --label:browser_codegen browser_content_codec_rule_schema_smoke browser_content_codec_codegen_smoke browser_abi_rule_smoke`、`artifacts/v3_bootstrap/cheng.stage3 run-host-smokes --compiler:artifacts/v3_bootstrap/cheng.stage3 --label:browser_codec_contract browser_host_native_contract_smoke`、`artifacts/v3_bootstrap/cheng.stage3 run-browser-host-wasm-smoke --compiler:artifacts/v3_bootstrap/cheng.stage3 --label:expr_surface_abi`、`git diff --check` |

# 最近完成

| 时间 | 事项 |
|---|---|
| `2026-04-19` | `browser_content_codec_abi` 已把 `text/bytes` 成功导出、错误发布和 out-buffer handle 写入收进同一套 export template；`moq_fountain build/rebuild` 的 bytes/text 路径现已共用 schema。 |
| `2026-04-19` | `browser_host_wasm_probe` 已显式暴露 `browserMoqFountainBuildBytes/RebuildBytes`，browser native/runtime 与 browser wasm audit 都开始真实消费 raw/text 双桥。 |
| `2026-04-19` | `cheng_v3_seed.c` 已把 `V3AsmLocalSlot` 收成带 `scope_start_line/scope_end_line` 的真词法槽；native 和 wasm 现在都按当前源码行与块尾范围找局部。 |
| `2026-04-19` | 新增 `wasm_shadowed_local_scope_smoke`，开始专门守 `if/elif/while/for` 里的同名局部换类型场景。 |
| `2026-04-19` | live `backend_driver` 已重建，`run-wasm-smokes --label:shadow_scope` 已把新 smoke 接进真实 host+wasm 链并跑绿。 |
| `2026-04-19 04:55 +0800` | `typed_expr` 已补齐 `arg/result/materialize/copy/import` 五类 lowering rule 字段；`compiler_csg` 与 `lowering_plan` 已共用同一份 rule。 |
| `2026-04-19 04:55 +0800` | `cheng_v3_seed.c` 已把 `str/rawbytes/seq/result` 的复合值路径收进共享地址优先/region copy 规则。 |
| `2026-04-19 04:55 +0800` | `lowering_matrix_smoke` 已进默认 host smoke，并覆盖 `arg/result/materialize/copy/import` 五类规则。 |
| `2026-04-19 06:xx +0800` | `Bytes[] add/setLen` 这条 native lowering 已进入真值回归；`browser_host_native_min_smoke` 不再死在未定义 bridge，而是已重新通过。 |
| `2026-04-19 06:xx +0800` | `exe` 路径显式 `@exportc` 现已保活，provider 默认 bridge 遇到入口同名导出会自动退成内部符号，消除了 duplicate symbol。 |
| `2026-04-19 06:xx +0800` | `browser_host_native_runtime_smoke` 与 `browser_host_native_contract_smoke` 已重新通过；`xor` return expr 和全局 `len(str)` 条件也已打通。 |
| `2026-04-19 05:xx +0800` | `browser_content_codec_abi` 已新增统一 `input/output schema` helper，`bytes/text/optional/handle` 不再继续散着各写一套桥。 |
| `2026-04-19 06:06 +0800` | live `backend_driver` 的 `mobile-shell`、export-visibility 与 browser wasm 主线已重新跑绿；`moq_fountain` 现已不再卡 `run-browser-host-wasm-smoke` 和 `runtime_zero_c_surface_smoke`。 |
| `2026-04-19 06:xx +0800` | `std/os::splitFile` 已改成纯索引 + 直接 `SplitFileResult(...)` 返回的稳定形状，`runtime_link_input_surface_smoke` 不再死在 loop 变量回写和复合局部。 |
| `2026-04-19 06:xx +0800` | live `backend_driver` 与 `gate_main` 已把 `runtime_zero_c_surface_smoke` 提升到显式长时 timeout；`runtime_link_input_surface_smoke + runtime_zero_c_surface_smoke` 现在在 live/stage0 两边都已重新通过。 |
| `2026-04-19 06:xx +0800` | `stage0 run-browser-host-wasm-smoke --label:final_browser_stage0` 已重新通过；这轮 `stage0` host/browser smoke 不再是独立红面。 |
| `2026-04-19 07:xx +0800` | `browser_content_codec_abi` 已把 `moq_fountain build/rebuild` 的 text/bytes 导出 schema 收成单一 `browserCodecMoqFountainExportSchema + browserCodecExportMoqFountain{Text,Bytes}Result`；live browser、heavy host、stage0 browser 三条验收重新通过。 |
| `2026-04-19 07:xx +0800` | `browser_host_wasm_probe` 已去掉裸 `cheng_v3_buffer_handle_from_text_bridge` 调用，统一走 `codecabi.browserBufferHandleFromTextRequired(...)`；`browser_host_native_runtime_smoke` 也已补上 long-wire 和 droplet-csv handle 真值断言并重新跑绿。 |
| `2026-04-19 07:xx +0800` | `browser_content_codec_rule_schema` 的 canonical ruleName 已追到当前源码：`export_moq_fountain_text_result`、`export_moq_fountain_bytes_result`、`moq_fountain_last_error_handle`；live/stage0 browser smoke 重新通过。 |
| `2026-04-19 07:xx +0800` | `browser_content_codec_abi_min` 已不再自己拼裸 browser bridge 流程，而是直接复用 codec helper；最小 browser smoke 现已补上 text handle 路径，并在 live/stage0 两边重新通过。 |
| `2026-04-19 07:xx +0800` | 新增 `browser_content_codec_rule_schema_smoke`，开始直接硬卡 `browser_content_codec_rule_schema` 的 canonical ruleName，不再只看数量；live/stage0 host smoke 重新通过。 |
| `2026-04-19 08:xx +0800` | browser ABI 规则已正式拆成 `browser_abi_rule + browser_content_codec_rule_schema` 两层；`compiler_csg` 已补齐 `ruleName` wrapper，`compiler_csg_smoke/lowering_plan_smoke + browser_content_codec_rule_schema_smoke + browser_abi_rule_smoke` 已一起跑绿。 |
| `2026-04-19 08:xx +0800` | `browser_abi_rule_smoke` 现改成手工最小 `V3SystemLinkPlanStub` 验收，不再走真实 `v3BuildSystemLinkPlanStub`；browser ABI 规则验证已与 parser bounds 噪声解耦。 |
| `2026-04-19 08:xx +0800` | `browser_content_codec_codegen` 已落地，`browser_content_codec_abi` 通过 canonical `ruleName` 绑定 `input/output/error/export schema`，不再自己维护 kind 常量和本地 schema constructor。 |
| `2026-04-19 09:xx +0800` | `browser_content_codec_codegen` 已继续收进 generic bridge：`input_len/copy/raw_handle/text_handle`、读输入、写输出、lift、error/export 全部进入共享 codegen；`browser_content_codec_abi` 现在只剩业务逻辑和兼容 wrapper。 |
| `2026-04-19 09:xx +0800` | `compiler_csg_smoke` 与 `lowering_plan_smoke` 的输出目录已按 `CHENG_V3_SMOKE_LABEL` 隔离；live/stage0 现可并跑，不再互踩 `artifacts/v3_compiler_csg` 与 `artifacts/v3_program_selfhost`。 |
| `2026-04-19 09:xx +0800` | `compiler_pipeline_stub`、`compiler_world`、`compiler_migration`、`lowering_matrix` 这批编译器 smoke 的输出目录也已按 `CHENG_V3_SMOKE_LABEL` 隔离；`compiler_world_libp2p/head/receipt` 的本地 probe 端口也已按 label 派生，live/stage0 并跑重新通过。 |
| `2026-04-19 09:xx +0800` | `tests/smoke_ports` 已落地；`chain_node_tailnet/libp2p`、`pin_runtime_host`、`content_runtime`、`libp2p_tailnet_transport/derp`、`libp2p_resource/scheduler`、`tailnet_control_core`、`content_stub`、`libp2p_protocols` 这批默认 host smoke 的本地端口现已按 label 派生，live/stage0 并跑重新通过。 |
| `2026-04-18` | 规范化前端、typed expr fact、lowering rule、runtime zero-c、shared wasm/browser smoke、`cheng_node` 第二阶段等主线已收平。 |

# 下一步

| 顺序 | 事项 |
|---|---|
| 1 | 把这套 `browser abi rule -> bridge spec -> source-aware emitter` 从 `browser_content_codec` 扩到更多 browser 模块，继续清掉模块侧 ABI wrapper。 |
| 2 | 把当前按模块调用的 shared codegen 再往编译器输出层推进，让 bridge emitter 真正落到导入/导出桥产物，而不只停在 helper API。 |
| 3 | 让 browser 模块只声明 rule/schema，不再自己写 `codegen` helper。 |
