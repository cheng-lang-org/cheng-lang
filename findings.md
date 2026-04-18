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
| 验收方式 | 不要再靠单点 smoke 补洞。`lowering_matrix_smoke` 这种矩阵验收比症状回归更可靠。 |
| stage0 使用面 | 裸 `cheng.stage0` 不能直接当 host smoke 编译器；它缺 `embedded bootstrap contract`，适合做 bootstrap/cc 自检，不适合当 live host smoke compiler。 |
| 文档记录 | 长流水账会掩盖真实状态。账本应该只保留当前主线、最近里程碑、耐久结论和下一步。 |
