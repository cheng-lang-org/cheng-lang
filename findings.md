# Findings

| 项目 | 结论 |
|---|---|
| 前端真源 | `cheng v3` 真正的稳定点不是继续加语法，而是“单一规范化前端 + typed HIR/metadata 真源”。`parser -> compiler_csg -> lowering_plan` 现在必须共享同一份表达式身份和 lowering 标签。 |
| 语法表面 | `func/fn` 双表面、`Fmt/?:/range/result intrinsic` 这种分散表面会持续制造漂移；正式口径必须是一处规范化，其它层不再扫源码字符串猜语义。 |
| typed lowering | 在完整类型检查补齐前，未知路径必须明确写成 `polymorphic/deferred`，不能伪造“已经全类型化”。 |
| lowering rule | `ruleKey` 不够，真正稳定的是显式规则字段：`arg/result/materialize/copy/import`。报表、smoke、后端都该围着这组规则转。 |
| 复合值规则 | `str/rawbytes/seq/result` 当前最稳规则是“标量走寄存器，复合值走显式地址，复合 copy 走 region copy”。native/wasm 必须共用这套规则。 |
| 局部作用域真根 | 这轮坐实了：flat local table + first-match 查找就是错根。只要同名 `let item` 在不同块里复用，后面的字段推导就会串到前一个槽上。正确规则是“局部槽带 `source line` 可见区间，并在 `dedent / elif / else` 切换时封口”。 |
| native/wasm 一致性 | `V3AsmLocalSlot` 一旦新增字段，native 和 wasm 必须同时初始化和消费；只修一边，很快就会在另一边读到脏 `scope`。 |
| browser ABI | browser wasm 侧当前最大重复面仍是手写 `input_len/copy/text/raw_handle`。下一步最值的是先抽统一 schema/helper，再逐步过渡到编译器生成桥。 |
| browser ABI rule 验收 | `v3CompilerBrowserAbiRulesForLinkPlan` 这类规则 helper 的第一性最小验收，不该再走真实 `v3BuildSystemLinkPlanStub`。真正需要的输入只有 `workspaceRoot/packageRoot/sourceClosurePaths`；手工最小 `V3SystemLinkPlanStub` 更稳，也能避开无关 parser bounds 噪声。 |
| browser ABI 首刀 | 这轮已经坐实：先把 `bytes/text/optional/handle` 收成显式 `input/output schema`，比继续堆 `Required/Optional/Handle` 平行 helper 稳得多；这样后面才能把更多 browser ABI 模块接到同一套规则上。 |
| browser codegen 下一层 | browser ABI 再往前推时，先把“canonical ruleName -> typed schema”抽成共享 codegen，比直接在业务 ABI 文件里写 kind 常量稳得多。这样业务层只绑定 ruleName，不再维护第二套数字真源。 |
| browser codegen 收口 | 只抽 schema 还不够；`input_len/copy/raw_handle/text_handle` 这些 generic bridge、以及读输入/写输出/lift/error/export 模板也必须一起进共享 codegen。否则业务 ABI 文件会长期停在“半共享半手写”的尴尬状态，第二套真源还是会回长。 |
| browser bridge spec | 真正能把 `compiler_csg/lowering_plan/codegen` 串成一条线的，不是继续共享几组 schema helper，而是显式的 `rule -> bridge spec`。先把 stage、input schema、output schema、error schema 这些生成结果固定成对象，再让 codegen 和后续 emitter 去消费，会比继续散着按 `ruleName` 写 if 分支稳得多。 |
| browser bridge owner identity | `bridge key` 不含 source，所以只看 key 很容易误以为 codegen 和 compiler 已对齐；真到 emitter 行时，`modulePath` 和源码绝对路径会马上分叉。bridge emitter 必须显式接收 `sourcePath`，不能偷用模块常量。 |
| browser ABI 导出模板 | 只抽 `input/output schema` 还不够；真正会反复冒洞的是成功导出、错误发布、out-buffer handle 三段逻辑各写一份。现在应固定成统一 `export handle result` 模板，再让 text/bytes 只是不同 lift。 |
| 编译器 smoke 并跑隔离 | 会自己物化 `artifacts/...`、写 probe 文件或占本地 TCP 端口的 smoke，不能再假设“输出路径固定也没事”。live/stage0 一起跑时，固定目录会互相覆盖，固定端口会互相抢占，表面上就会长成完全无关的编译/运行时错误。正确做法是统一按 `CHENG_V3_SMOKE_LABEL` 分目录，固定端口也按 label 派生。 |
| libp2p smoke 端口隔离 | 默认 host smoke 里真正会互撞的不是一条，而是一串：`6201/6211/6307/6308/6316/6317/6321/6322/6421-6429/6431/7101/7102/7202`。最稳解法不是手工给 live/stage0 分两套新端口，而是统一从 `CHENG_V3_SMOKE_LABEL` 派生本地端口，让每条 smoke 保留原始相对布局。 |
| `oracle_runtime_host_smoke` | 这条现在仍然会在单跑时直接 `bounds idx=256 len=256`，不是本轮端口隔离引入的回归。它需要单独查真根，不能再混进“并跑互踩”这一类问题里。 |
| `Bytes[]` 真状态 | `Bytes[] add/setLen` 这条不能继续留在业务侧绕开，必须修在编译器里。当前最小 host smoke 已证明这条链不再被它本身卡死。 |
| 显式导出规则 | `@exportc` 不能只在 `shared/obj` 保活；`exe` 里只要入口源码显式声明导出，也必须进 reachable roots，否则会出现“同源定义却被当成外部未定义符号”的假链接错误。 |
| provider 冲突 | runtime provider 的默认 bridge 不能和入口源码显式导出同名共存。正确规则是入口显式导出优先，provider 自动退成内部符号，而不是靠弱符号或链接顺序碰运气。 |
| 关键字运算 | `xor` 这种关键字二元运算不能只在某一层偷偷支持；至少 `const/infer/prepare/native/wasm` 这些热路径要同时认，否则就会退化成假 prefix-call。 |
| 全局 len | `len(str/Bytes/seq)` 不能只认局部或字段；真正稳的规则是统一走 lvalue/global 地址，再按 ABI 布局取长度槽。 |
| 已收平真洞 | 把 `Bytes[]`、`@exportc bridge`、`xor return_expr`、全局 `len(str)` 收平后，browser host native/runtime 与 wasm 验收已经重新一致。 |
| 新回归面 | `wasm_shadowed_local_scope_smoke` 这种最小块级遮蔽用例比只盯 `moq_fountain` 更稳，因为它直接守住了“同名局部换类型换字段”这个根，而不是守某一个业务症状。 |
| 验收方式 | 不要再靠单点 smoke 补洞。`lowering_matrix_smoke` 这种矩阵验收比症状回归更可靠。 |
| stage0 使用面 | 裸 `cheng.stage0` 不能直接当 host smoke 编译器；它缺 `embedded bootstrap contract`，适合做 bootstrap/cc 自检，不适合当 live host smoke compiler。 |
| 命令面收口 | live `backend_driver` 的命令面不能继续靠自递归转发凑合。`mobile-shell` 要直连稳定 helper，export-visibility 要本地直接跑 host smoke，不然很容易在 capture/tail 上挂住。 |
| browser wasm 收口 | `moq_fountain` 这类 browser wasm 热路径，先把构建逻辑收成稳定 helper/getter，再复用已编绿的 build result 填充规则，比在大函数里继续堆 `Fmt + IntToStr + member access` 混合循环稳得多。 |
| `std/os::splitFile` | 这类基础 helper 一旦同时混入复合局部、倒序索引回写和 loop 变量复用，就很容易踩 ordinary lowering 旧红点。更稳的形状是只保留整数索引，再直接 `return SplitFileResult(...)`。 |
| host smoke timeout | `runtime_zero_c_surface_smoke` 这种会套跑 audit、browser、wasm 的重型 smoke，不能跟普通 unit smoke 共用 60 秒统一上限；正确做法是保留默认紧口径，再给已坐实的重型 smoke 单独显式 timeout。 |
| browser export schema | `result/error/out-buffer` 这层真正该固定的不是更多平行 helper，而是“一个 error schema + 一个 output kind + 一个最终 export helper”。先把 `moq_fountain` 的 text/bytes 导出收成这套，后面扩到别的 browser 模块会稳得多。 |
| browser probe 层 | `browser_host_wasm_probe` 这种探针层也不能继续直接碰裸 `text_handle bridge`。一旦 probe 直接 import bridge，后面就很容易绕开刚收好的 codec schema。正确做法是 probe 也统一走 codec helper，再让 smoke 直接守 probe handle 真值。 |
| browser 规则表 | `browser_content_codec_rule_schema` 不能长期挂旧 helper 名。规则表哪怕现在主要用来报表和后续生成桥，只要名字落后于源码，就会再次制造“双真源”。正确做法是把 canonical ruleName 始终追到当前 helper。 |
| browser 规则消费面 | browser ABI rule 最稳的结构是“两层真源”：`browser_abi_rule` 负责规则字段与计数/equality，`browser_content_codec_rule_schema` 负责某一模块的 canonical 规则列表；`compiler_csg` 和 `lowering_plan` 只做消费与转发，不再把规则塞回大对象里。 |
| wasm codegen 限制 | browser wasm 热路径里，模块级 `const` 访问在部分 prepare 路径上还不稳。共享真源仍应保留在 codegen 模块里，但运行时判断优先走共享 getter 函数，不要直接在热路径里比模块 `const`。 |
| 最小 ABI smoke | `browser_content_codec_abi_min` 这种最小回归也不能留一套手写裸桥流程。最小 smoke 最适合直接复用正式 codec helper，再补上 bytes/text 两条最小 handle 断言，这样才能真守住“没有第二套 ABI 口径”。 |
| ruleName 回归 | 只守 `browser abi` 数量和 kind 计数不够，`ruleName` 漂回旧名字也能蒙混过关。最稳做法是单独补一条最小 `rule schema smoke`，直接检查 canonical ruleName 是否存在。 |
| 文档记录 | 长流水账会掩盖真实状态。账本应该只保留当前主线、最近里程碑、耐久结论和下一步。 |
