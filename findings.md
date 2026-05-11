# Findings (2026-05-06)

## Cold compiler current gaps (2026-05-06)
- 冷编译器不需要支持 Cheng 全部语法，只需支持真实编译器源码实际用到的 bootstrap kernel 子集。
- 本轮已补齐：默认 object 写法、`&/*/>>/|/^/<<` 基础算术、hex 指令字、标量转换、`str[index]` byte load、字符字面量、`uint8/char` 标量、string literal escape decode、bool value expression CFG lowering、单行 suite、object field assignment、index assignment、sequence escape、结构化 facts v2、parser statement sequence scanner、statement facts writer/reader、expr token facts writer/reader、phase arena、dense layout cursor、work-stealing CAS model、非 int32 泛型 ADT payload、`var int32[]` 参数、`add(int32[], int32)`、object 字段上的动态序列引用。
- 真实源码派生的 AArch64 encode kernel 已通过 source-direct、source->CSG、facts direct；这证明冷路径能消费编译器实际用的 bit encoding 公式，但还不是完整 bootstrap kernel 闭包。
- 真实源码派生的 frontend scan kernel 已通过 source-direct、source->CSG、facts direct；这证明冷路径能消费 lexer/outline parser 实际需要的 byte scanner 形状。
- 真实源码派生的 combined kernel 已通过 source-direct、source->CSG、facts direct；这证明多个真实 kernel 可以在同一冷编译输入中共享类型表、函数表、CFG、对象状态更新、outline parser 扫描和 Mach-O emit。
- 剩余缺口：更多真实 CSG stmt/expr facts、约 2000 行真实 bootstrap closure、必要的 object/sequence 逃逸语义；v2 facts 已消除 tab-separated payload 边界，但 source-direct 仍未支持真实多行字符串 statement。

## Cold outline/parser closure
- source->CSG 的注释处理必须先剥离 `///#/block comment`，并且不能进入字符串和字符字面量；否则真实 parser 源码里的注释会被 statement scanner 当成代码。
- 续行判断和条件拆分都必须识别字符字面量；`'('`、`'['`、`'{'` 是 byte 常量，不是语法括号。
- `match/if` 条件目标扫描需要同时跟踪 `()` 与 `[]`，因为真实签名和泛型类型经常在条件表达式里出现。
- direct Mach-O 的 `__TEXT` segment 不能固定一页；code words 增长后必须按实际 text size 对齐，否则 dyld 会在加载时拒绝 `__text` section 越界。

## Cold AArch64 encode kernel
- 大指令常量不能用单条 `movz w`；必须 `movz + movk` 写满 32-bit word，否则 `0x94000000` 这类 branch opcode 会被截断。
- 真实 encode 公式需要 `uint32(...)` 这类类型转换，但当前 cold subset 只承诺 int32-compatible bit pattern，不承诺完整 64-bit 算术语义。
- bool expression detector 必须跳过 `<<`/`>>`；否则 shift 表达式会被误拆成比较条件，导致编码公式被错误 lowering。
- source->CSG 续行合并是为了消费真实源码形状；它只能作为 bootstrap kernel 过渡合同，不能继续替代结构化 parser facts。

## Cold div/mod
- `/` 与 `%` 是正式 Cheng 算术表面，真实 runtime/debug/encoding 代码大量使用；cold subset 缺它们会卡住 2000 行 bootstrap closure。
- ARM64 `sdiv` 对除数 0 不会自动 trap，因此 cold codegen 必须在发 `sdiv` 前显式比较并 `brk`，不能静默产出 0。
- `%` 不需要新 runtime helper；用 `sdiv quotient` 后 `msub remainder = dividend - quotient * divisor`，与 signed division 的截断语义保持一致。

## Cold frontend scan kernel
- `str[index]` 是 lexer/parser kernel 的硬前提；实现必须是 bounds check 后按 byte load，不能把 `str` 当 int32 array 或展开成后处理。
- 字符字面量可以先按 byte 值降为 i32；`uint8/char` 在 cold subset 中只承诺 scanner byte 语义，不承诺完整字符模型。
- binding initializer 不能直接复用 initializer slot；`var pos = start` 必须分配新 slot 并 copy，否则局部变量赋值会反写参数。
- string literal parser 必须先解码 escape 再生成 `ptr,len`；`"a\n"` 的运行时长度是 2，不是源码 span 长度 4。
- 当前 facts 是 tab-separated 文本，字符串 literal 内的 raw tab/newline 无法安全承载；escape decode 只能修源码 literal 语义，真实 bootstrap kernel 应消费结构化 facts。

## Cold sequence escape
- 非空 `int32[]` 字面量不能把 header buffer 指向生成字面量的栈上 backing array；一旦该序列进入 object 字段或 sret 复合返回，就会形成悬空地址。
- 当前冷路径把非空 `int32[]` 字面量直接 materialize 到 mmap buffer；空序列保持零 header，第一次 `add` 仍走现有扩容路径。
- object 构造和字段赋值必须用字段/目标类型给 `[..]` 提供上下文；否则 `Object(items: [1,2])` 会被误降成固定数组而不是动态序列。

## Cold inline suite
- `src/core/lang/outline_parser.cheng` 这类真实 parser 源码大量使用 `if cond: return ...`、`elif cond: stmt`、`else: break`、`stmt; stmt`；不支持单行 suite 会让 bootstrap closure 被源码形状卡住。
- source-direct 现在直接解析 inline suite；source->CSG 先把 inline body 展开成缩进 rows，再交给同一个 CSG lowerer，避免 direct/facts 路径分叉。
- 分号在 `if cond: a; b` 中属于 inline suite body，不是外层同级 statement；拆分必须先识别 header colon/`=>`。

## Cold combined bootstrap kernel
- combined kernel 已把 frontend scanner 与 AArch64 encoder 放入单一源文件验证，避免两个孤立 fixture 共享不了符号/slot/CFG 的假进度。
- combined kernel 已并入 `ScanState` 对象状态更新，覆盖本地 object 默认初始化、`var object` 参数、字段 bool 短路赋值、str 字段赋值和固定数组字段赋值。
- combined kernel 已并入 outline parser 扫描闭包，覆盖块注释、字符串/字符跳过、括号/中括号平衡、签名中跳过注释里的 `=`。
- combined kernel 已并入 parser statement sequence 扫描闭包：逐行计算 indent/token/payload span，分类为 int32 `kindCode`，checksum 不复制 statement 字符串。
- combined kernel 已并入 statement facts writer/reader 闭包：scanner 输出直接写成 v2 `cold_csg_stmt` records，numeric field 在 writer/reader 两端都不经中间字符串。
- combined kernel 已达到 2022 行：新增 expression token scanner/facts、phase arena reset、dense layout cursor 和 work-stealing CAS 顺序模型，三条冷路径均 exit 42。
- combined kernel 已推进到 2035 行：FactRecordView 恢复 `fieldInt/fieldIsDecimal` SoA arrays，WorkDeque 恢复 `taskFn/taskRange` SoA arrays，并三条冷路径 exit 42。
- `return <bool expr>` 和 typed bool binding 必须走 CFG lowering；把 bool 当 int32 表达式直接算会丢掉 `&&/||` 短路语义。
- 约 2000 行 closure 的下一层不应继续靠文本续行和 tab facts 扩展；需要结构化 facts 承载 statement、expr、type layout 和 string bytes。
- `range - start * 100` 暴露 cold parser 旧二元表达式全左结合 bug；冷路径必须按 `* / %`、`+ -`、shift、bitwise 层级解析，否则 backend driver 真实算术会被错降。
- 复合 slot/字段拷贝必须精确到 4 字节尾部；`int32[3]` 这类非 8 字节倍数固定数组不能用盲目 64-bit loop 复制。

## Cold structured facts v2
- `--csg-out` 默认输出 `cold_csg_version=2`，row payload 统一为 `field_count + len:hexbytes`；tab/newline 不再是字段分隔语义的一部分。
- v2 loader 按版本硬分流；legacy v1 只在文件显式 `cold_csg_version=1` 时读取，避免把旧格式当新格式猜。
- 原始 tab 字节字符串 `"a<TAB>b"` 已通过 source->CSG 和 facts direct，证明 string/expression field 不再被 tab 拆裂。
- combined kernel 已并入真实 byte-buffer writer/reader/loader slice：writer 逐字段输出 v2 record，reader 保存 encoded field span/len，不复制字段字符串；loader 单次扫描时对 kind 字段做流式 exact 匹配并产出 `kindCode`，decimal 字段 packed 到 `fieldInt`，expr token facts 同样走 v2 record，避免 hash 或二次字符串副本充当相等判定。
- 嵌套 call 不能边解析边写 `call_arg` side table；`FactHexValue(FactReadByte(reader))` 会让外层参数起点指到内层 `reader`。现在函数调用统一本地收集参数后追加 dense table。
- source->CSG 发 statement row 必须跳过多行函数签名续行；只靠“顶层 fn 行 + 后续缩进行”会把 `hexStart: int32...` 当成赋值语句。
- object 布局必须按字段实际 size 对齐；`int32[N]` 字段放在 scalar 后若只按 kind=4 字节对齐，会被 64-bit payload copy 读写错位。
- cold facts loader 的 record kind 分类应在读取第 0 字段时同步完成；用 `hexStart` 做二次随机解码会扩大到 var object + sequence field 的额外 ABI 面，不符合单次扫描生成结构化 facts 的目标。

## Cold backend dispatch surface
- `backend_driver_dispatch_min.cheng` 的第一层真实缺口不是 int32 ABI，而是导入后的类型表：`alias.Type`、`Result[alias.Type]`、`var str`、`str[]`、`var str[]` 必须有独立 slot/ABI，不能压成 int32。
- imported module surface 只加载签名，不生成本地 body；一旦 reachable call 真要落到 imported 函数，仍必须走外部 ABI/linkerless image 方案，不能 no-op。
- imported overload 不能按 name+arity 压扁；`std/system.panic(ptr)` 和 `std/system.panic(str)` 已证明必须按参数 kind 做解析。
- `backend_driver_dispatch_min.cheng` 当前 source-direct 已越过参数/import/qualified call 层、`Fmt"..."`、`Result[T]` object helpers、stdio、真实 argv surface、path bridge 和 `SystemLinkPlanStub` materializer；新的硬 blocker 是重后端 CSG 构建：`ccsg.BuildCompilerCsgInto`。
- `cold_bootstrap_backend_dispatch_type_surface --csg-out` 当前仍失败：旧 CSG statement scanner 会把多行函数签名续行当 statement，CSG writer 必须按 `ColdFunctionSymbol.body` 边界发 rows。

## Cold compiler source-direct parser fixes (2026-05-06)
- 新增默认 object 类型声明支持：`parse_type` 中对 `type A =` 后缩进 `name: Type` 字段行创建 `ObjectDef`，新增 `object_finalize_fields` 计算 slot layout；不要求 `object` 关键字
- 新增 `*` `>>` `&` 操作符：新增 `BODY_OP_I32_MUL=20` `BODY_OP_I32_ASR=21` `BODY_OP_I32_AND=22` 三个 BodyIR opcode，ARM64 编码 `a64_mul_reg` `a64_asr_reg` `a64_and_reg`
- 新增 `>>` tokenizer 支持（双字符 token 识别）
- `&` 和 `*` 已验证通过 source-direct 路径；`>>` 已进入 tokenizer/codegen，需随真实 kernel 切片继续扩大覆盖

## Cold var/generic completion
- `Result[ObjType, Diag]` 已通过 source-direct、source->CSG、facts direct 三条路径，object payload/error 按实际 layout 传递、返回和 match。
- `Box = Wrapped(Result[ObjType, Diag]) | Empty` 已通过 source-direct、source->CSG、facts direct 三条路径，证明 CSG variant row 不再按裸逗号误切泛型字段类型。
- `var int32[]` 参数和 `add(xs, value)` 已通过 source-direct、source->CSG、facts direct 三条路径；局部序列和 object 字段序列都能被 `add` 原地修改。
- `target[index] = value` 已通过 source-direct、source->CSG、facts direct 三条路径；本地 `int32[N]`/`int32[]` 和 object 字段数组/序列均写回原存储，不写临时副本。
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
- object 字段写不能复用字段读取的临时 slot；必须对原 object 或 `var object` 参数地址发 `PAYLOAD_STORE`，否则只是改了拷贝。当前已覆盖 int32/bool/str/int32[N] 字段写回。

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

## Backend dispatch_min cold surface
- `backend_driver_dispatch_min.cheng` 的第一层缺口不是表达式，而是签名类型面：模块限定类型、`Result[alias.Type]`、`var str`、`str[]/var str[]`、以及 8 参数上限会在进入函数体前硬失败。
- `var T` 在签名预收集阶段不能要求本地 object/type 已解析；类型 block 还没进 symbols，必须先按引用槽位记录，等真正 parse/type layout 阶段再收紧。
- qualified call 必须在赋值判断前识别；否则 `os.WriteLine(...)` 会被旧 parser 当成 `os` 字段赋值，错误位置偏离真实导入闭包。
- CSG statement scanner 必须区分 bodyless `@importc fn` 和有函数体 `fn =`；无函数体声明只进函数符号表，不能把后续 `const` block 的缩进行挂到前一个函数。
- 当前签名面、qualified/import surface、`os.GetStderr/Get_stdout/WriteLine`、argv/envp bridge、path ABI、常用 strutils 和 `CompilerCsgTextSet` 已过；`slplan.BuildSystemLinkPlanStubWithFields` 已由冷端 materializer 接住，`backend_driver_dispatch_min.cheng` 可被 cold 编成 Mach-O。
- `CompilerCsgTextSet` 冷端不能依赖尚未进入符号表的 `parser.ParserTextLookup` 完整布局；当前实现只占用 `CompilerCsgTextSet.lookup` 首段作为自管 str-pair set，保持 Insert 的去重语义，后续源码闭包接入 ParserTextLookup 后应收回为正式 layout。
- `std/result` 的当前语义是 object layout：`ok/value/err(ErrorInfo)`，不是旧 ADT `Ok|Err`；冷路径的 `IsErr/IsOk/Value/Error` 必须按 object field offset lowering，并让 `Result[T]` 实例参与真实复合 ABI。
- Mach-O 入口 wrapper 调用真实 `main` 前会覆盖 `LR`；如果要保存 `argc/argv` 到 callee-saved register，必须同时保存入口 `LR`，否则 wrapper `ret` 会跳回自身形成死循环。
- `SystemLinkPlanStub` materializer 当前只承诺 status/driver 消费所需字段；sourceClosure/runtime/provider 序列先保持空序列，避免未稳定的运行态 `str[] add` 污染 plan。
- cold probe 的 `DirectObjectEmitWriteObject` cmdline 短路会把普通 `system-link-exec` 伪装成成功；全编译器接入必须移除该短路，让生成物真实走 `system_link_plan -> compiler_csg -> lowering`。
- `ccsg.BuildCompilerCsgInto` 未接入时必须结构化失败并写 report sidecar；禁止返回空 `CompilerCsg` 或假成功去推进 lowering。
- `lower.BuildLoweringPlanStubFromCompilerCsg` 当前必须结构化返回 false+error，不能 trap、空 report、成功对象或空对象继续执行；下一步要用真实 Cheng body/materializer 替换这个错误边界。
- `WriteTextFile(root,path,text)` 只修 root/path 解析不够；pre-CSG 错误报告能写时，后续不能继续在路径层打补丁，要查参数/局部槽/错误路径。
- 当前 probe 最大函数帧已到 `22032`，虽然 AfterCsg 自身只有 `1600`，但 ARM64 prologue/local address 的大立即数编码仍不能继续拖；完整自举前必须支持大栈帧和大偏移 load/store，不能靠当前小函数路径偶然通过。
- object field store 必须检查 `field.offset + field.size <= object slot size`；materializer 写结构化 CSG/lowering/primary plan 时，越界应在冷编译阶段硬失败，不能等生成物运行时污染相邻槽。
- `pobj/direct` 当前仍必须运行时 `brk` 暴露，不能返回成功对象或空对象继续执行；等 lowering 真正接通后再逐层替换为真实 Cheng body/materializer。
- cold 编出 `backend_driver_dispatch_min_probe` 后必须跑生成物 `status`；只看 cold compiler 输出 `system_link_exec=1` 不足以证明 plan materializer 可运行。
- source-direct 导入加载不能混用旧 CSG type-row loader；旧 loader 不带 alias，会把 `result.ErrorInfo` 这类导入布局按短名写进全局 `ErrorInfo`，破坏 Result/Error intrinsic。添加/import 必须精确查找，resolve 才允许短名回退。
- direct import body 编译后，linkerless image 不能全量 codegen 所有有 body 的导入函数；必须从入口 BodyIR call 图计算可达闭包，否则未被运行路径使用的 std/backend 函数会把冷端尚未支持的语义带入 codegen。
- `setjmp` 恢复点必须从当前函数下一行继续找下一个 top-level decl；从函数起点调用 `cold_next_top_level_decl` 会回到同一个 `fn`，形成无限重试和 stderr/arena 膨胀。
- provider/system linker 不能作为普通 `emit=exe` 的 import 兜底；是否链接 provider 必须由显式 `--link-providers` 决定，否则会把 linker 成功/失败和 linkerless codegen 正确性混在一起。
- object writer 的固定符号/重定位表会制造顺序敏感 bug；Mach-O object 必须按真实 name/reloc 数动态分配，超范围直接失败，不能静默截断。
- import-mode unresolved call 返回 0 slot 会把缺失签名伪装成常量路径；现在必须 hard-fail，再由入口可达闭包保证只解析真实需要的 imported body。
- 生成版 `backend_driver_dispatch_min` 的真实进展必须由生成物再次编译源码证明；现在 `ordinary_zero` 与 `import_use` 已作为 self-linkerless 回归钉子进入 `tools/cold_regression_test.sh`。
- `cold_subset_coverage` 已成为生成版 self-linkerless 的下一层门禁；它比 `ordinary_zero/import_use` 更接近冷编译器真实语义面，能暴露 ptr/mmap/file IO/variant/object/CFG 的生成物退化。
- 未声明函数自动注册 external 会把真实 ABI 缺失伪装成可链接对象；例如未声明 `puts` 会按 `str` 双寄存器 ABI 调 libc，输出污染。冷路径必须要求显式 C ABI 签名，`cstring` 按单指针传参。
- upfront import body 编译会把未可达函数的解析错误和 codegen 缺口带进主路径；正确边界是入口 body 先成图，然后只对缺失的可达 import 目标按 alias+签名精确编译。这个路径不能带 setjmp 恢复、SIGSEGV 兜底或 composite skip。

## Generated backend emit obj

- 生成版 `backend_driver_dispatch_min` 已能对 `src/tests/import_use.cheng` 执行 `system-link-exec --emit:obj`，产出 arm64 Mach-O object；该 object 经 `cc` 链接后运行 exit `3`。
- 回归新增 `dispatch_min_self_emit_obj_cross` 和 `dispatch_min_self_emit_obj_direct`，同时锁 `emit=obj/direct_macho=1/provider_object_count=0/system_link=0`，对象 writer 现在由生成物自己证明。

## Cold linkerless backend driver

- 生成版 `backend_driver_dispatch_min` 已能编译 `src/core/tooling/backend_driver_dispatch_min.cheng` 自身，二代产物 `status` 运行成功并输出 `bootstrap_mode=selfhost`、`flag_exec_edges=0`、`flag_exec_unresolved=0`。
- `cold_cmd_build_backend_driver` 已删除旧的 compile 非零后扫描 `.o` 并用 `cc` 链接路径；现在 `compile-bootstrap` 失败即失败。`build_backend_driver_no_cc_fallback` 回归锁住输出和报告中不出现 `cold_cc_link`。
- 本轮顺手修掉 `parse_call` import-mode 未解析调用返回 0 slot 的旧缺口；`parse_call` 与 `parse_call_from_args_span` 现在一致 hard-fail，`import_unresolved_hard_fail` 恢复稳定。
