# 进度

| 规则 | 内容 |
|---|---|
| 记录口径 | 只保留当前仍有决策价值的近况；历史细节已压缩。 |

| 开始时间 | 状态 | 事项 |
|---|---|---|
| `2026-04-19` | 已完成 | `browser_content_codec_abi` 已新增统一 `browserCodecExportHandleResult`，`browserCodecExportTextResult/BytesResult` 不再各自手写清错、发错和 handle 输出。 |
| `2026-04-19` | 已完成 | `browserMoqFountainBuildBytes/RebuildBytes` 已接进 `browser_host_wasm_probe`；`browser_host_native_runtime_smoke`、`browser_host_native_contract_smoke`、`expr_surface_abi` 已一起重新跑绿。 |
| `2026-04-19` | 已完成 | `cheng_v3_seed.c` 已把 `V3AsmLocalSlot` 升成带 `scope_start_line/scope_end_line` 的真词法槽；native 递归 prepare 与 wasm flat prepare stack 都会在 `dedent / elif / else` 切换时封口。 |
| `2026-04-19` | 已完成 | `browser_host_native_runtime_smoke`、`browser_host_native_contract_smoke`、`expr_surface_abi` 在这轮局部作用域修复后仍然一起为绿。 |
| `2026-04-19` | 已完成 | 新增 `wasm_shadowed_local_scope_smoke`，手工 host 编译/运行和 wasm 编译/instantiate 已通过。 |
| `2026-04-19` | 已完成 | live `backend_driver` 已重建，`run-wasm-smokes --label:shadow_scope` 已把 `wasm_shadowed_local_scope_smoke` 接进真实链路并跑绿。 |
| `2026-04-19 04:55 +0800` | 已完成 | `typed_expr` 已补齐 `argPassKind/resultPassKind/materializeKind/copyKind/importKind`，`compiler_csg` 与 `lowering_plan` 直接共享 lowering rule。 |
| `2026-04-19 04:55 +0800` | 已完成 | `cheng_v3_seed.c` 已把 `str/rawbytes/seq/result` 复合值路径统一到“显式地址优先 + region copy”。 |
| `2026-04-19 04:55 +0800` | 已完成 | `lowering_matrix_smoke` 已接进默认 host smoke，开始硬卡规则字段而不只是看 key。 |
| `2026-04-19 06:xx +0800` | 已完成 | `exe` 路径的显式 `@exportc` 已加入 reachable roots，`browser_host_native_min_smoke` 不再死在缺失 bridge。 |
| `2026-04-19 06:xx +0800` | 已完成 | provider 编译阶段已按入口显式导出名抑制同名默认 bridge，`duplicate symbol _cheng_v3_buffer_handle_from_raw_bridge` 已消失。 |
| `2026-04-19 06:xx +0800` | 已完成 | `xor` 关键字二元运算已贯通 `const/infer/prepare/native/wasm` 主要热路径；全局 `len(str/Bytes/seq)` 也已切到通用 lvalue 地址。 |
| `2026-04-19 06:xx +0800` | 已完成 | `Bytes[] add/setLen` 不再是当前 host 链路首个阻塞点；`browser_host_native_runtime_smoke`、`browser_host_native_contract_smoke`、`expr_surface_abi` 已一起跑绿。 |
| `2026-04-19 06:06 +0800` | 已完成 | live `backend_driver` 的 `mobile-shell help`、`verify-mobile-shell-build-probe`、`verify-backend-driver-command-surface` 已重新通过。 |
| `2026-04-19 06:06 +0800` | 已完成 | `v3/src/libp2p/content/moq_fountain.cheng` 已把 build/rebuild wire 收成 browser wasm 可编形状；live `run-browser-host-wasm-smoke`、`runtime_zero_c_surface_smoke`、`host_process_pipe_spawn_api_smoke`、`verify-orphan-guard` 已一起跑绿。 |
| `2026-04-19 06:xx +0800` | 已完成 | `std/os::splitFile` 已改成纯索引/直接返回形状，`runtime_link_input_surface_smoke` 现已重新通过。 |
| `2026-04-19 06:xx +0800` | 已完成 | `run-host-smokes` 已从 seed 旧实现收回 passthrough，live `backend_driver` 与 `gate_main` 也已给 `runtime_zero_c_surface_smoke` 单独长时 timeout；live/stage0 两边的 `runtime_link_input_surface_smoke + runtime_zero_c_surface_smoke` 现已一起跑绿。 |
| `2026-04-19 06:xx +0800` | 已完成 | `stage0 run-browser-host-wasm-smoke --label:final_browser_stage0` 已重新通过，`stage0` host/browser smoke 这轮已和 live 对齐。 |
| `2026-04-19 07:xx +0800` | 已完成 | `browser_content_codec_abi` 已把 `moq_fountain` 的 text/bytes 导出收成单一 `browserCodecMoqFountainExportSchema + browserCodecExportMoqFountain{Text,Bytes}Result`；live `run-browser-host-wasm-smoke`、live `runtime_zero_c_surface_smoke`、stage0 `run-browser-host-wasm-smoke` 已重新通过。 |
| `2026-04-19 07:xx +0800` | 已完成 | `browser_host_wasm_probe` 已不再直接碰 `cheng_v3_buffer_handle_from_text_bridge`；long-wire、droplet-csv、error-text 这批 probe handle 统一改走 codec helper，`browser_host_native_runtime_smoke` 与 live/stage0 browser smoke 已重新通过。 |
| `2026-04-19 07:xx +0800` | 已完成 | `browser_content_codec_rule_schema` 已把过时 ruleName 追到当前 helper 名；live/stage0 `run-browser-host-wasm-smoke` 已重新通过，规则表与源码不再各说一套。 |
| `2026-04-19 07:xx +0800` | 已完成 | `browser_content_codec_abi_min` 已改成直接复用 `browser_content_codec_abi` helper，`browser_host_native_min_smoke` 现已同时守 bytes/text 两条最小 handle 桥；live/stage0 browser smoke 重新通过。 |
| `2026-04-19 07:xx +0800` | 已完成 | 新增 `browser_content_codec_rule_schema_smoke`；`browser_content_codec_rule_schema` 的 canonical ruleName 现在有独立 host smoke 硬卡，不再只靠计数侧面覆盖。 |
| `2026-04-19 08:xx +0800` | 已完成 | `browser_abi_rule` 已从大 struct 外独立成共享规则层，`compiler_csg` 也已补齐 `v3BrowserAbiRuleCountRuleName/HasRuleName` wrapper。 |
| `2026-04-19 08:xx +0800` | 已完成 | 新增最小 `browser_abi_rule_smoke`：手工构 `V3SystemLinkPlanStub`，直接验证 `compiler_csg/lowering_plan/schema` 三方同一份 browser ABI 规则，不再走真实 parser/closure 链。 |
| `2026-04-19 08:xx +0800` | 已完成 | `compiler_csg_smoke`、`lowering_plan_smoke`、`browser_content_codec_rule_schema_smoke`、`browser_abi_rule_smoke` 已作为一组 browser ABI rule 验收通过；`browser_host_native_contract_smoke` 与 `expr_surface_abi` 也再次通过。 |
| `2026-04-19 08:xx +0800` | 已完成 | 新增 `browser_content_codec_codegen`，把 `input/output/error/export schema` 和 canonical `ruleName -> schema` 绑定从业务 ABI 文件里抽成共享 codegen 层。 |
| `2026-04-19 08:xx +0800` | 已完成 | `browser_content_codec_abi` 已切到共享 codegen，不再自己维护 kind 常量和本地 schema constructor；`browser_content_codec_codegen_smoke` 也已接进默认 host smoke。 |
| `2026-04-19 08:xx +0800` | 已完成 | wasm 侧已确认不能稳定吃模块 `const`；共享 codegen 现已补成 getter 函数，`expr_surface_abi` 与 `browser_host_native_contract_smoke` 重新通过。 |
| `2026-04-19 09:xx +0800` | 已完成 | `browser_content_codec_codegen` 已继续收进 `input_len/copy/raw_handle/text_handle` 相关 importc、读输入、写输出、lift 和 `error/export` 模板；`browser_content_codec_abi` 只剩业务组装与兼容 wrapper。 |
| `2026-04-19 09:xx +0800` | 已完成 | `browser_content_codec_rule_schema_smoke + browser_content_codec_codegen_smoke + browser_abi_rule_smoke + browser_host_native_contract_smoke + expr_surface_abi` 已再次一起跑绿，说明共享 codegen 没有打坏 native/wasm/browser 现有链路。 |
| `2026-04-19 09:xx +0800` | 已完成 | `browser_abi_rule` 已新增通用 `V3BrowserAbiBridgeSpec` 与 `rule -> bridge spec` 生成；`browser_content_codec_rule_schema` 收成单点 `ruleByName`。 |
| `2026-04-19 09:xx +0800` | 已完成 | `browser_content_codec_codegen` 现已按 bridge spec 生成 schema，不再自己硬编码 `ruleName -> schema` 映射；`compiler_csg/lowering_plan` 也已正式暴露 browser bridge spec API。 |
| `2026-04-19 09:xx +0800` | 已完成 | `browser_content_codec_rule_schema_smoke + browser_content_codec_codegen_smoke + browser_abi_rule_smoke + browser_host_native_contract_smoke + expr_surface_abi` 在 bridge spec 接线后再次一起通过。 |
| `2026-04-19 10:xx +0800` | 已完成 | `browser_abi_rule` 已新增 per-bridge emitter 行，`browser_content_codec_codegen` 新增 `sourcePath + ruleName` 的 bridge key/helper/emit 标量 API；`browser_abi_rule_smoke` 不再直接搬复合 bridge spec。 |
| `2026-04-19 10:xx +0800` | 已完成 | `browser_content_codec_rule_schema_smoke + browser_content_codec_codegen_smoke + browser_abi_rule_smoke + browser_host_native_contract_smoke + expr_surface_abi + git diff --check` 已在 source-aware emitter 接线后再次一起通过。 |
| `2026-04-19 09:xx +0800` | 已完成 | `compiler_csg_smoke`、`compiler_pipeline_stub`、`compiler_world`、`compiler_migration`、`lowering_matrix` 这批编译器 smoke 的输出目录都已按 `CHENG_V3_SMOKE_LABEL` 分开；`compiler_world_libp2p/head/receipt` 的本地 probe 端口也已按 label 派生，live/stage0 并跑重新通过。 |
| `2026-04-19 09:xx +0800` | 已完成 | 新增 `tests/smoke_ports` 共享 helper；`chain_node_tailnet/libp2p`、`pin_runtime_host`、`content_runtime`、`libp2p_tailnet_transport/derp`、`libp2p_resource/scheduler`、`tailnet_control_core`、`content_stub`、`libp2p_protocols` 这批默认 host smoke 的本地端口现已按 label 派生，live/stage0 并跑重新通过。 |
| `2026-04-18 22:10 +0800` | 已完成 | 规范化表达式补上 `exprId + source span`，`typed fact` 开始跨 `parser -> compiler_csg -> lowering_plan` 共享。 |
| `2026-04-18 20:29 +0800` | 已完成 | `cheng_node` 统一宿主骨架、`MoQ/喷泉码` actor、legacy 导入和状态重放已打通。 |
| `2026-04-18 03:57 +0800` | 已完成 | `r2c-react-v3` full truth v4 与 43 route matrix 已收平。 |
| `2026-04-17` | 已完成 | runtime zero-c、`backend_driver` 正式命令面、nested package import、`v3/codex` 第一阶段等主线已收平。 |
| `2026-04-19 05:xx +0800` | 已完成 | 四个账本已压成短版；`browser_content_codec_abi` 也已先把 `bytes/text/optional/handle` 输入输出桥抽成统一 schema/helper，并已通过 `browser_host_native_contract_smoke` 与 `run-browser-host-wasm-smoke --label:expr_surface_abi`。 |
| `2026-04-19 10:xx +0800` | 进行中 | 把这套 `browser abi rule -> bridge spec -> source-aware emitter` 从 `browser_content_codec` 扩到更多 browser 模块，继续清掉模块侧 ABI wrapper。 |
| `2026-04-19 10:xx +0800` | 进行中 | 把当前的 shared codegen/emitter 再往编译器输出层推进，让 bridge 直接成为导入/导出桥产物。 |
