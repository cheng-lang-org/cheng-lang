# Findings (2026-05-06)

## Cold compiler current gaps (2026-05-06)
- 冷编译器不需要支持 Cheng 全部语法，只需支持真实编译器源码实际用到的 bootstrap kernel 子集。
- 本轮已补齐：默认 object 写法、`&/*/>>` 基础算术、非 int32 泛型 ADT payload、`var int32[]` 参数、`add(int32[], int32)`、object 字段上的动态序列引用。
- 剩余缺口：对象字段赋值、`str == str` 比较、泛型字段 facts 的结构化格式、真实 bootstrap kernel 成片迁入。

## Cold compiler source-direct parser fixes (2026-05-06)
- 新增默认 object 类型声明支持：`parse_type` 中对 `type A =` 后缩进 `name: Type` 字段行创建 `ObjectDef`，新增 `object_finalize_fields` 计算 slot layout；不要求 `object` 关键字
- 新增 `*` `>>` `&` 操作符：新增 `BODY_OP_I32_MUL=20` `BODY_OP_I32_ASR=21` `BODY_OP_I32_AND=22` 三个 BodyIR opcode，ARM64 编码 `a64_mul_reg` `a64_asr_reg` `a64_and_reg`
- 新增 `>>` tokenizer 支持（双字符 token 识别）
- `&` 和 `*` 已验证通过 source-direct 路径；`>>` 已进入 tokenizer/codegen，需随真实 kernel 切片继续扩大覆盖

## Cold var/generic completion
- `Result[ObjType, Diag]` 已通过 source-direct、source->CSG、facts direct 三条路径，object payload/error 按实际 layout 传递、返回和 match。
- `var int32[]` 参数和 `add(xs, value)` 已通过 source-direct、source->CSG、facts direct 三条路径；局部序列和 object 字段序列都能被 `add` 原地修改。
- `add` 当前只承诺 `int32[]`，不承诺泛型 `T[]`；大范围开放前必须先把元素类型、扩容和拷贝规则结构化。

## Architecture
- Cold compiler (`cheng_cold.c` ~8000loc C) + full compiler (`src/` Cheng) coexistence model validated.
- `bootstrap-bridge` path works (stage3→stage2). C seed cold-start has known limits (thread/memRetain/atomic).
- Mach-O direct write: 12 load commands in `macho_direct.h` (280loc). Codesign page alignment is the remaining gap.

## BodyIR CFG
- `&&` compound conditions: factorized `EmitConditionOps` eliminated 200 lines of duplicated inline parsing.
- `pendingCompoundFalseTerms` chain correctly resolves elif/else/end-of-function false blocks.
- 9 body kinds converged to BodyIR CFG. `buildBodyIR` generalized to `wordCounts[i]<=0` auto-trigger.
- `BackendDriverDispatchMinRunCommand` and `BackendDriverDispatchMinCommandCode` both released to StatementSequence.

## Parallelization
- Chunked fill infrastructure ready: `PrimaryFillFunctionChunk` + `PrimaryFillInstructionWordsChunked`.
- Each function's `instructionWords` range is non-overlapping → zero-contention parallel write.
- `thread.Spawn` activation requires stage3 compilation; C seed cold-start takes serial path.
- 4-phase parallel: lowering function classification + BodyIR build + instruction fill + CSG source processing.

## Cold compiler
- `return <int>` verified (exit 42/77/88/99).
- Multi-line function body with indentation works.
- `if/else` IR construction correct; branch patching: true block resolved, false block index off by 1.
- Algebraic type parser (`type X = A | B`) with variant counting.
- `match`/`let`/`if`/`else` keywords recognized; body parsing stubbed for non-return statements.

## Docs
- Algebraic type + `match` syntax added to `docs/cheng-formal-spec.md` §1.2.
- `algebraicType ::= variantType { "|" variantType }`, `matchStmt`, `variantPattern`.

# Findings (2026-05-06)

## Cold CSG
- `--csg-out` 是当前冷启动路径的正确下一层：让冷编译器生产 facts，再消费 facts 降 BodyIR，避免用手写 fixture 证明自身。
- 原 CFG bug 不是 branch patch 偏一位，而是 fallthrough block 未在切换到 false/else 分支前封口，导致 true block 的 op_count 覆盖后续分支 op。
- 生成 facts 后再 `--csg-in` 能稳定复现同一退出值，说明事实格式和 lowerer 已形成可回归合同。

## Gate
- `cold_csg_facts_exporter_smoke` 这种重型 exporter driver 不进入默认 host smoke；当前默认口径是轻量 `cold_csg_sidecar_smoke`，直接验证 backend exporter、cold facts path 和最终可执行退出码。

## Cold ADT/match
- ADT/match 的直接源码 parser 已存在，真正缺口是 facts 合同：没有 type/match/case row 时，`--csg-out -> --csg-in` 无法证明冷路径支持代数类型。
- 冷 CSG 的正确边界是把 variant 声明先进入 `Symbols`，statement lowering 只消费 tag/payload 信息；这样 facts direct 路径和源码直编共享同一个 BodyIR/codegen 后半段。
- 单 payload word 只够证明 ADT/match 形状；多字段 payload 必须走稳定布局表和 payload offset。

## Cold multi-payload
- 多字段 payload 不需要新对象模型；在 SoA BodyIR 上给 op 增加第三操作数，用现有 dense arg side table 存 payload slot 列表即可。
- payload 布局必须按父类型最大 variant 字段数分配 slot，而不是按当前 variant 分配；否则同一 local 的 tag 分支后 payload offset 会不稳定。
- 多字段 payload 已扩到 int32/str；复合返回值要进入冷路径，下一步必须补 ABI return/call 规则，不能把 str 当 int32。

## Cold str payload
- `str` 进入冷路径后，source mmap 生命周期必须覆盖 codegen；BodyIR 内的 literal span 指向 mmap 源码，提前 `munmap` 会在 `code_append_bytes` 处崩溃。
- facts 格式不能只保存字段数量；`Variant:count:kinds` 是最小稳定合同，旧 `Variant:count` 保持按 int32 读取，避免打断旧 facts。
- 当前只支持 string literal 和 `len(str)`；真正复合返回值还缺 ABI 规则，不能把 `fn -> str` 当普通 i32 return-call 处理。

## Cold composite ABI
- 复合返回值不能复用 i32 返回寄存器；Darwin arm64 下 `str` 和 ADT slot 必须通过 hidden sret `x8` 传 return buffer，caller/callee 两边都要显式建模。
- 返回类型解析必须 hard-fail；未知类型静默按 i32 会生成可链接但 ABI 错误的机器码。
- call 指标必须消费同一 op 集合；只统计 `BODY_OP_CALL_I32` 会让 composite call 在报告中消失，性能数据不可信。

## Cold str arg ABI
- `str` 参数是两个 machine word，不能按参数序号直接映射寄存器；后续参数必须按 ABI word cursor 顺延。
- CSG function 参数字段需要携带最小 kind 信息：`name:s`/`name:i`。旧 `name` 仍按 i32 读取，source-to-CSG 默认输出 typed form。

## Cold Result question flow
- `?` 不能做成后处理补丁；必须在 CFG 中生成 Err 早返回 block 和 Ok continuation block。
- `let_q` 的最小可靠语义是 Result tag==Err 时返回原 variant，tag==Ok 时按 payload offset 加载值继续执行。
- `call_q` 是丢弃 Ok payload 的同一套控制流；它必须和普通 call 一样计入 call count。
- `return expr?` 不能静默返回原 Result；按当前 Cheng 语义 `expr?` 是解包值，cold path 在 parser/exporter 入口 hard-fail，等类型系统明确 `return` 自动包装规则后再考虑新语法。
- CSG `let_q/call_q` 不能固定 offset 8/int32；必须从 Result 的 Ok variant layout 读取 payload kind/offset，并要求 `?` 目标类型等于当前函数返回 Result 类型。

## Cold bootstrap slice types
- 多行 ADT 不能按单行 parser 处理；source->CSG 必须把 `type X =` 后续缩进行合并为同一个 type row。
- Brace 风格 `match value {` 必须在 source->CSG 阶段剥掉 `{`；lowerer 不应接收被污染的 match target。
- 嵌套 ADT payload 需要显式 kind code；`v` 只表达字段 kind，不足以表达函数 ABI 的真实 type size。
- bootstrap slice 的完成度必须用真实组合语义计算：多行 ADT + 嵌套 match + 跨函数复合返回，比单点 fixture 更接近 cold seed 替代路径。

## Cold large ADT ABI
- 函数参数 facts 不能把用户 ADT 压成 `v`；必须保留 `name:TypeName`，否则 `TypeInfo` 这类大 slot 会退化成截断 ABI。
- 大于 16 bytes 的 ADT 参数必须按地址传递，callee 复制完整 slot；继续用两个寄存器传前 16 bytes 会产生“测试偶然过、payload 实际丢失”的假绿。
- `MAKE_VARIANT` 必须先清零整个 variant slot，再写 tag/payload；否则嵌套 variant 扩到固定 slot 后会把未初始化栈字节带进后续 copy/return。
- `match f():` 的正确 lower 方式是先把目标表达式降成 variant slot，再对该 slot 读 tag 建 switch；source->CSG facts 中保留表达式文本即可。
- ASan/UBSan 已暴露 `span_trim({0})` 的 `NULL + 0` 未定义行为；空 Span 必须原样返回。

## Cold object/field/index
- object 不能复用 ADT row；需要独立 `cold_csg_object`，因为 object 字段没有 tag，布局从 offset 0 开始。
- `int32[N]` 是固定 slot，facts 必须携带长度；只用 `i` 会丢失 array bound，后续 `array[index]` 无法做 hard-fail 边界检查。
- constructor 不能边解析字段边写全局 `call_arg` side table；嵌套 array/object/variant 会插入自己的 payload，打断父 constructor 的 dense payload 列表。正确做法是本地收集 slots/offsets，解析完成后一次性写入 side table。
- object 返回和参数传递复用 composite ABI：返回走 sret `x8`，大 object 参数走 byref，callee 复制完整 slot。

## Cold tuple/default init
- `type` block 扫描必须按行首边界回退；object 字段扫描如果吞掉下一个同级 entry，后续 tuple/ADT row 会静默丢失。
- tuple 在 cold bootstrap subset 里可以先降为 object layout：无 tag、字段从 offset 0 开始，constructor/field load/composite ABI 复用同一套机制。
- typed default init 不能靠 expression parser 猜；source->CSG 需要显式 `default` statement row，lowerer 再按 type table 分配 slot 并清零。
- 当前 default init 已覆盖 `int32`、`str/cstring`、ADT、object/tuple、`int32[N]`；动态序列 `T[]` 还没有 slot layout，必须作为下一层结构化能力补齐。

## Cold dynamic sequence local
- `int32[N]` 是固定长度数组，不是容量提示；动态序列必须用 `int32[]`，两者 slot layout 和 ABI 不能混用。
- 局部 `int32[]` 可以用栈内 backing buffer 证明 `len/cap/buffer` header、`.len` 和常量下标；但返回值、object 字段、ADT payload 不能指向当前函数栈，必须先有逃逸/堆分配语义再开放。
- typed binding facts 是动态序列的硬前提；`var xs [..]` 没有类型上下文时空 `[]` 无法严格 lower。
- `parser_take` 失败会消费 token；默认初始化判断必须用 `parser_peek`，否则 `let x: Type` 会吞掉下一行语句头。

## Backend importc self-compile
- provider object 不能靠源码文本二次扫描恢复 `@importc`；`TypedExprBuildIrWithFactsAndExprLayer` 会清空 `sourceTexts.text`，lowering 后扫文本会得到空表。正确做法是 Typed IR 持有结构化 importc target symbol。
- `void` importc 也必须进入 importc symbol table；只记录有返回类型的 importc 会漏掉 `c_free/raw_libc_free/raw_libc_exit/native_cheng_register_line_map_from_argv0_runtime` 这类关键 runtime bridge。
- BodyIR call sequence 对 importc 应保存真实 C 符号；reachability drift 检查也必须按真实 C 符号比较，不能拿 Cheng 本地声明名比较。
- `run-host-smokes cold_csg_sidecar_smoke` 已进入当前 `dispatch_min` 命令面；它只承诺这一条 cold sidecar gate，缺失或未知 smoke 必须 hard-fail，不能用空 runner 或通用占位 runner 冒充 host smoke 通过。

## Backend cold facts ADT/match
- `ParserReadNormalizedExprLayer...` 不是 backend driver 主线；`CompilerCsgExprLayerForProfile` 才是 `compiler_csg` 当前热路径。新增语法能力必须接到 profile path，否则 facts 导出会只看到 return/assign，漏掉 match/case。
- `=>` 不能被 assignment parser 当成 `=`；否则 `Some(value) =>` 会污染 assign statement，后续 exporter 会报 variant constructor 类型错误。
- ADT/match 的默认 gate 不能只检查 facts 文本；必须继续跑 `cheng_cold --csg-in` 和最终可执行退出码，才能证明 `cold_csg_type/match/case -> BodyIR switch -> Mach-O` 全链路真实可用。
