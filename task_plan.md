# 当前任务

| 项目 | 内容 |
|---|---|
| 目标 | 继续把 `cheng v3` 收成“单一规范化前端 + typed lowering 真源 + 统一复合值/ABI 规则”。当前主攻两条并行主线：browser ABI schema 收口，以及 native seed 把 `Bytes[] add/setLen + @exportc bridge` 真正打通。 |
| 状态 | 进行中。`normalized expr`、`typed expr fact`、`lowering rule`、native/wasm 复合值地址优先规则已落地；`Bytes[] add/setLen`、`exe @exportc`、`xor` 关键字运算、全局 `len(str/Bytes/seq)` 已在 seed 链路打通。 |
| 当前主线 | 1. 前端已统一到单一规范化声明/表达式层。2. `compiler_csg + lowering_plan` 已共用 `typed fact + lowering rule`。3. seed/native/wasm 已开始共用“复合值显式地址 + region copy”规则。4. browser `content codec` 已把 `bytes/text/optional/handle` 输入输出桥先抽成统一 schema/helper。5. native `exe` 现已支持入口显式导出覆盖 provider 默认 bridge。6. `Bytes[] add/setLen` 这条已不再是 host/browser 首个阻塞点。 |
| 本轮动作 | 修正 `exe` 路径 reachability，让显式 `@exportc` 不再只在 `shared/obj` 保活；provider 编译阶段开始按入口显式导出名抑制同名默认 bridge；继续把 `xor` 当成真二元运算贯通 `const/infer/prepare/native/wasm`，并让 `len(...)` 统一走通用 lvalue 地址。 |
| 涉及文件 | `task_plan.md` `progress.md` `findings.md` `lessons.md` `v3/bootstrap/cheng_v3_seed.c` `v3/src/tests/browser_host_native_runtime_smoke.cheng` `v3/src/libp2p/browser/browser_content_codec_abi.cheng` `v3/src/libp2p/content/moq_fountain.cheng` |
| 不做 | 不新开 worktree；不碰无关脏改；不把 browser ABI 假装成“已自动生成”；不把 `Bytes[]` 缺口继续留在业务侧绕开。 |
| 验收 | `browser_host_native_min_smoke`、`browser_host_native_runtime_smoke`、`browser_host_native_contract_smoke`、`run-browser-host-wasm-smoke --label:expr_surface_abi`、`git diff --check` |

# 最近完成

| 时间 | 事项 |
|---|---|
| `2026-04-19 04:55 +0800` | `typed_expr` 已补齐 `arg/result/materialize/copy/import` 五类 lowering rule 字段；`compiler_csg` 与 `lowering_plan` 已共用同一份 rule。 |
| `2026-04-19 04:55 +0800` | `cheng_v3_seed.c` 已把 `str/rawbytes/seq/result` 的复合值路径收进共享地址优先/region copy 规则。 |
| `2026-04-19 04:55 +0800` | `lowering_matrix_smoke` 已进默认 host smoke，并覆盖 `arg/result/materialize/copy/import` 五类规则。 |
| `2026-04-19 06:xx +0800` | `Bytes[] add/setLen` 这条 native lowering 已进入真值回归；`browser_host_native_min_smoke` 不再死在未定义 bridge，而是已重新通过。 |
| `2026-04-19 06:xx +0800` | `exe` 路径显式 `@exportc` 现已保活，provider 默认 bridge 遇到入口同名导出会自动退成内部符号，消除了 duplicate symbol。 |
| `2026-04-19 06:xx +0800` | `browser_host_native_runtime_smoke` 与 `browser_host_native_contract_smoke` 已重新通过；`xor` return expr 和全局 `len(str)` 条件也已打通。 |
| `2026-04-19 05:xx +0800` | `browser_content_codec_abi` 已新增统一 `input/output schema` helper，`bytes/text/optional/handle` 不再继续散着各写一套桥。 |
| `2026-04-18` | 规范化前端、typed expr fact、lowering rule、runtime zero-c、shared wasm/browser smoke、`cheng_node` 第二阶段等主线已收平。 |

# 下一步

| 顺序 | 事项 |
|---|---|
| 1 | 把这层 schema 继续接到更多 browser ABI 模块，减少业务模块手写 `input_len/copy/raw_handle/text_handle`。 |
| 2 | 把 `result/error/out-buffer` 也纳入同一套 browser ABI schema。 |
| 3 | 再往前收“编译器按 ABI schema 生成桥”，不再手写 browser codec glue。 |
