# 当前任务

| 项目 | 内容 |
|---|---|
| 目标 | 继续把 `cheng v3` 收成“单一规范化前端 + typed lowering 真源 + 统一复合值/ABI 规则”。当前主攻 browser wasm ABI，把 `str/rawbytes/seq/result` 先从手写桥收成统一 schema/helper。 |
| 状态 | 进行中。`normalized expr`、`typed expr fact`、`lowering rule`、native/wasm 复合值地址优先规则已落地；browser 侧本轮已收第一刀 schema/helper，但还没到编译器自动生成桥。 |
| 当前主线 | 1. 前端已统一到单一规范化声明/表达式层。2. `compiler_csg + lowering_plan` 已共用 `typed fact + lowering rule`。3. seed/native/wasm 已开始共用“复合值显式地址 + region copy”规则。4. browser `content codec` 已把 `bytes/text/optional/handle` 输入输出桥先抽成统一 schema/helper。5. 下一刀是把这层 schema 扩到更多 browser ABI，并继续逼近自动生成桥。 |
| 本轮动作 | 压缩四个账本，只保留当前有效状态；同时把 `browser_content_codec_abi` 里的输入/输出桥抽成统一 schema/helper，减少手写分叉。 |
| 涉及文件 | `task_plan.md` `progress.md` `findings.md` `lessons.md` `v3/src/libp2p/browser/browser_content_codec_abi.cheng` `v3/src/tests/browser_content_codec_abi_min.cheng` |
| 不做 | 不新开 worktree；不碰无关脏改；不把 browser ABI 假装成“已自动生成”；不回退已经收平的 typed lowering 主线。 |
| 验收 | `browser_host_native_min_smoke`、`run-browser-host-wasm-smoke --label:expr_surface_abi`、`git diff --check` |

# 最近完成

| 时间 | 事项 |
|---|---|
| `2026-04-19 04:55 +0800` | `typed_expr` 已补齐 `arg/result/materialize/copy/import` 五类 lowering rule 字段；`compiler_csg` 与 `lowering_plan` 已共用同一份 rule。 |
| `2026-04-19 04:55 +0800` | `cheng_v3_seed.c` 已把 `str/rawbytes/seq/result` 的复合值路径收进共享地址优先/region copy 规则。 |
| `2026-04-19 04:55 +0800` | `lowering_matrix_smoke` 已进默认 host smoke，并覆盖 `arg/result/materialize/copy/import` 五类规则。 |
| `2026-04-19 05:xx +0800` | `browser_content_codec_abi` 已新增统一 `input/output schema` helper，`bytes/text/optional/handle` 不再继续散着各写一套桥。 |
| `2026-04-18` | 规范化前端、typed expr fact、lowering rule、runtime zero-c、shared wasm/browser smoke、`cheng_node` 第二阶段等主线已收平。 |

# 下一步

| 顺序 | 事项 |
|---|---|
| 1 | 把这层 schema 继续接到更多 browser ABI 模块，减少业务模块手写 `input_len/copy/raw_handle/text_handle`。 |
| 2 | 把 `result/error/out-buffer` 也纳入同一套 browser ABI schema。 |
| 3 | 再往前收“编译器按 ABI schema 生成桥”，不再手写 browser codec glue。 |
