# 进度

| 规则 | 内容 |
|---|---|
| 记录口径 | 只保留当前仍有决策价值的近况；历史细节已压缩。 |

| 开始时间 | 状态 | 事项 |
|---|---|---|
| `2026-04-19 04:55 +0800` | 已完成 | `typed_expr` 已补齐 `argPassKind/resultPassKind/materializeKind/copyKind/importKind`，`compiler_csg` 与 `lowering_plan` 直接共享 lowering rule。 |
| `2026-04-19 04:55 +0800` | 已完成 | `cheng_v3_seed.c` 已把 `str/rawbytes/seq/result` 复合值路径统一到“显式地址优先 + region copy”。 |
| `2026-04-19 04:55 +0800` | 已完成 | `lowering_matrix_smoke` 已接进默认 host smoke，开始硬卡规则字段而不只是看 key。 |
| `2026-04-18 22:10 +0800` | 已完成 | 规范化表达式补上 `exprId + source span`，`typed fact` 开始跨 `parser -> compiler_csg -> lowering_plan` 共享。 |
| `2026-04-18 20:29 +0800` | 已完成 | `cheng_node` 统一宿主骨架、`MoQ/喷泉码` actor、legacy 导入和状态重放已打通。 |
| `2026-04-18 03:57 +0800` | 已完成 | `r2c-react-v3` full truth v4 与 43 route matrix 已收平。 |
| `2026-04-17` | 已完成 | runtime zero-c、`backend_driver` 正式命令面、nested package import、`v3/codex` 第一阶段等主线已收平。 |
| `2026-04-19 05:xx +0800` | 已完成 | 四个账本已压成短版；`browser_content_codec_abi` 也已先把 `bytes/text/optional/handle` 输入输出桥抽成统一 schema/helper，并已通过 `browser_host_native_contract_smoke` 与 `run-browser-host-wasm-smoke --label:expr_surface_abi`。 |
| `2026-04-19 05:xx +0800` | 进行中 | 把这层 browser ABI schema 继续扩到更多 browser 模块，并把 `result/error/out-buffer` 也并进同一规则面。 |
