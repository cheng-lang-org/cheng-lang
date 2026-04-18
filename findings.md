# Findings

| 项目 | 结论 |
|---|---|
| 前端真源 | `cheng v3` 真正的稳定点不是继续加语法，而是“单一规范化前端 + typed HIR/metadata 真源”。`parser -> compiler_csg -> lowering_plan` 现在必须共享同一份表达式身份和 lowering 标签。 |
| 语法表面 | `func/fn` 双表面、`Fmt/?:/range/result intrinsic` 这种分散表面会持续制造漂移；正式口径必须是一处规范化，其它层不再扫源码字符串猜语义。 |
| typed lowering | 在完整类型检查补齐前，未知路径必须明确写成 `polymorphic/deferred`，不能伪造“已经全类型化”。 |
| lowering rule | `ruleKey` 不够，真正稳定的是显式规则字段：`arg/result/materialize/copy/import`。报表、smoke、后端都该围着这组规则转。 |
| 复合值规则 | `str/rawbytes/seq/result` 当前最稳规则是“标量走寄存器，复合值走显式地址，复合 copy 走 region copy”。native/wasm 必须共用这套规则。 |
| browser ABI | browser wasm 侧当前最大重复面仍是手写 `input_len/copy/text/raw_handle`。下一步最值的是先抽统一 schema/helper，再逐步过渡到编译器生成桥。 |
| browser ABI 首刀 | 这轮已经坐实：先把 `bytes/text/optional/handle` 收成显式 `input/output schema`，比继续堆 `Required/Optional/Handle` 平行 helper 稳得多；这样后面才能把更多 browser ABI 模块接到同一套规则上。 |
| `Bytes[]` 真状态 | `Bytes[] add/setLen` 这条不能继续留在业务侧绕开，必须修在编译器里。当前最小 host smoke 已证明这条链不再被它本身卡死。 |
| 显式导出规则 | `@exportc` 不能只在 `shared/obj` 保活；`exe` 里只要入口源码显式声明导出，也必须进 reachable roots，否则会出现“同源定义却被当成外部未定义符号”的假链接错误。 |
| provider 冲突 | runtime provider 的默认 bridge 不能和入口源码显式导出同名共存。正确规则是入口显式导出优先，provider 自动退成内部符号，而不是靠弱符号或链接顺序碰运气。 |
| 关键字运算 | `xor` 这种关键字二元运算不能只在某一层偷偷支持；至少 `const/infer/prepare/native/wasm` 这些热路径要同时认，否则就会退化成假 prefix-call。 |
| 全局 len | `len(str/Bytes/seq)` 不能只认局部或字段；真正稳的规则是统一走 lvalue/global 地址，再按 ABI 布局取长度槽。 |
| 已收平真洞 | 把 `Bytes[]`、`@exportc bridge`、`xor return_expr`、全局 `len(str)` 收平后，browser host native/runtime 与 wasm 验收已经重新一致。 |
| 验收方式 | 不要再靠单点 smoke 补洞。`lowering_matrix_smoke` 这种矩阵验收比症状回归更可靠。 |
| stage0 使用面 | 裸 `cheng.stage0` 不能直接当 host smoke 编译器；它缺 `embedded bootstrap contract`，适合做 bootstrap/cc 自检，不适合当 live host smoke compiler。 |
| 文档记录 | 长流水账会掩盖真实状态。账本应该只保留当前主线、最近里程碑、耐久结论和下一步。 |
