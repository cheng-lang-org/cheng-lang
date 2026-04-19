# Lessons

## 总原则

- 从单一真源往下推，不要在 parser、CSG、lowering、seed、wasm 各层各猜一份。
- 发现未知就显式标未知，不要假装类型或 ABI 已经确定。
- 复合值统一按地址语义思考；不要在热路径上混用“按值/按句柄/按临时槽”。
- 验收优先用矩阵和正式 smoke，不靠一次性日志或临时手工命令。

## 前端与 lowering

- 语法糖必须在前端一次吃掉，后端不再识别 `Fmt/?:/range/result intrinsic` 这类表面。
- 表达式一旦跨层流动，第一刀先补 `exprId + source span`。
- `compiler_csg` 和 `lowering_plan` 必须共用同一份 typed fact 与 lowering rule，不要双份统计。
- lowering 真规则至少要显式带 `arg/result/materialize/copy/import` 五类字段。
- 局部槽查找不能再用 flat table 的 first-match；必须至少带 `source line` 可见区间，并在 `dedent / elif / else` 时收紧块内局部。
- native 和 wasm 共用 `V3AsmLocalSlot` 时，新增字段必须两边一起初始化、一起消费；只修一边等于没修。

## ABI 与复合值

- `str/rawbytes/seq/result` 当前统一按“显式地址优先 + region copy”收。
- `Bytes[] add/setLen` 这种序列能力缺口要直接修进编译器，不要再在 host/browser 业务代码里兜桥或换数据结构绕过去。
- `xor` 这种关键字二元运算要成套收进 `const/infer/prepare/native/wasm`，不能只在某一层临时认一下，不然会被误判成 prefix-call。
- `len(str/Bytes/seq)` 不要只为局部变量写特判；统一走 lvalue/global 地址再按 ABI 布局取长度槽更稳。
- browser wasm ABI 不要长期手写 `input_len/copy/raw_handle/text_handle`；先抽 schema/helper，再走自动生成。
- browser codec 第一刀先收 `bytes/text/optional/handle` 这四类最常见桥，不要一上来就试图在业务函数里直接内联所有 browser ABI 细节。
- browser ABI 一旦开始收导出层，就把“清错 + 发错 + out-buffer handle 返回”锁进同一个 helper；不要让 `text` 和 `bytes` 再各写一遍成功/失败分支。
- browser ABI 导出层继续往前收时，优先固定“单一 error schema + output kind + 最终 export helper”这三个点；`moq_fountain` 的 text/bytes 导出已经证明，这样比保留 `TextHandleExportSchema/BytesHandleExportSchema` 这类平行 helper 更稳。
- browser probe/test 层也别直接 import 裸 handle bridge；统一从 codec helper 走，再让 smoke 直接比长度、首字节或关键字段，能少留一层隐藏回退口。
- browser ABI 规则表里的 `ruleName` 也要跟当前 helper 同步；哪怕暂时主要拿来报表，只要名字落后，后面做自动生成桥时就会重新长出第二套真源。
- `v3CompilerBrowserAbiRulesForLinkPlan` 这类规则 helper 的 smoke，不要默认走真实 `v3BuildSystemLinkPlanStub`；只要目的是验规则消费面，直接手工构最小 `V3SystemLinkPlanStub(workspaceRoot/packageRoot/sourceClosurePaths)` 更稳，也能避开无关 parser bounds 噪声。
- browser codec 再往前推时，优先抽“canonical `ruleName` -> typed schema”的共享 codegen 层，让业务 ABI 文件只绑定 ruleName，不再自己维护 kind 常量和本地 schema constructor。
- browser codec 一旦抽出了共享 codegen，就继续把 `input_len/copy/raw_handle/text_handle`、读输入、写输出、lift、error/export 模板一起收进去；不要停在“schema 在共享层，generic bridge 还散落在业务 ABI 文件”这种半截状态。
- browser ABI 再继续前推时，不要让 `compiler_csg`、`lowering_plan`、`codegen` 各自从 rule 现算一遍；先固定一层显式 `rule -> bridge spec`，再让三边都消费这同一份生成结果。
- browser bridge emitter 不能偷用模块常量当 owner identity；`bridge key` 就算一致，真正输出到 emitter 行时，`modulePath` 和源码绝对路径还是会分叉。凡是要比 bridge 文本，都要显式带 `sourcePath`。
- browser wasm 热路径里，不要直接依赖模块级 `const` 比较；共享真源留在 codegen 模块里，但运行时判断优先走共享 getter 函数，否则很容易重新踩 prepare 限制。
- 最小 ABI smoke 也别留手写裸桥流程；最稳的是直接复用正式 codec helper，再单独加一条最小 text handle 断言，把 bytes/text 两条桥一起守住。
- `browser abi` 这类规则表不要只守数量和 kind 分布；canonical `ruleName` 也要有独立 smoke 硬卡，不然旧名字回流时计数照样会绿。
- browser bytes/text 双桥的回归不要猜业务文本长什么样；最稳断言是同一输入下 `bytes` 导出与 `text` 导出结果逐字一致，再单独守错误句柄。
- 会编译/运行并落固定 `artifacts/...` 的 smoke，必须按 `CHENG_V3_SMOKE_LABEL` 或等价唯一键分目录；否则 live/stage0 并跑时会互踩，表面上像 parser 或 runtime bounds crash。
- 只要 smoke 自己会绑本地 TCP 端口，端口也必须跟 label 一起派生；目录隔离不够，固定端口一样会把并跑打红。
- 对一组本来就按端口序列工作的 smoke，不要手工拆两套新端口表；保留原相对端口，只让 label 决定统一偏移量，最稳也最省事。
- live `backend_driver` 的命令面不要再靠自递归转发凑合；`mobile-shell` 这类命令要直连稳定 helper，export-visibility 这类验收要本地直接跑 host smoke。
- `moq_fountain` 这类 browser wasm 热路径里，`member access + strings.IntToStr + Fmt` 混在一个大循环里很容易踩 primary object 缺口；先拆成 getter/helper，再复用已编绿的 result fill 规则更稳。
- 词法作用域这种编译器真根要直接补专门 smoke，最好用“同名局部在 `if/elif/while/for` 里换类型换字段”这种最小用例，不要只靠业务函数偶然覆盖到。
- `exe` 路径下的显式 `@exportc` 也必须进入 reachable roots；否则会伪装成“外部桥未定义”，把真问题藏住。
- runtime provider 遇到入口源码同名显式导出时，provider 那份外部符号要主动退掉，不能靠 duplicate symbol 再让链接器替你判。
- `stage0` 不是 live host smoke compiler，host/browser smoke 优先用 `stage3` 或 live backend driver。
- `std/os::splitFile` 这类基础 helper，要避免“复合局部 + 倒序索引回写 + loop 变量复用”叠在一起；更稳的是只算整数索引，最后直接 `return SplitFileResult(...)`。
- host smoke 的默认 run timeout 不能无脑一刀切；`runtime_zero_c_surface_smoke` 这种会套跑 audit/browser/wasm 的重型 smoke，要单独给显式长时 timeout，别让重 smoke 被 unit smoke 的口径误杀。

## 记录与流程

- 账本只写当前有效信息；旧历史压缩，不要让 8000 行文档遮住当前决策。
- 开新任务前先看 `lessons.md`，避免把已经踩实的坑重新踩一遍。
