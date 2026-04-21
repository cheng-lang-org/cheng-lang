# 当前发现

- 新确认
  - `compiler_csg` 里凡是只是 `typed_expr` 的 clone/equal/validate/filter/count 纯转发 helper，都该直接删。
  - 这类 helper 看起来只是方便，实际会把 `typed_expr` 真源重新复制成第二套 API 面；report、smoke、plan 很快又会绑回中间层。

- 新确认
  - `system_link_plan` 这类正式热路径不要再包 `target_matrix` 的一层本地转发壳。
  - target triple 判定真源已经在 `target_matrix`，本地再包一层既重复，又会平白扩大 ordinary/seed 的调用解析面。

- 新确认
  - `render_compare` 这条页面级对拍链路也不能靠“缺参数时猜目录里有什么产物”来凑闭环。
  - 稳定口径是两条显式模式并存：
    - 正常全链模式继续自己产 `native/truth` 产物
    - 直接对拍模式必须显式传 `--native-summary --truth-summary --native-screenshot --truth-screenshot`
  - 只要进入直接模式，就四个参数全必填；不要混搭半自动发现。

- 新确认
  - 页面级像素对拍 smoke 不需要每次都把浏览器 truth/native runtime 全链重跑一遍；最稳的回归是最小 summary + screenshot 直接钉 report/summary 文档。
  - 这样能把 `render_compare` 自己的字段合同和真正的运行链路问题拆开，不会再让重型链条噪音盖住 report 漏口。

- 新确认
  - `typed_expr` 这种 typed truth-source 模块里已经有 generic count/text helper 时，`compiler_csg` 不该再平行包一套 `v3TypedExprAbiClassText / FactCountAbiClass / AbiScalarCount` 这种 API。
  - 稳定分层是：
    - `typed_expr` 持有 text/count 真语义
    - `compiler_csg` 只保留 parser -> typed 的桥接 helper 和 CSG 自己的领域逻辑
    - `lowering_plan` / smoke / probe 直接问 `typed_expr`
  - 如果测试侧担心“模块 enum 常量直接出现在 main 里”不稳，就把一格一个 helper 收进 `typed_expr` 真模块，不要继续把这层 convenience helper 留在 `compiler_csg`。

- 新确认
  - `lowering_plan` 不该保存 browser ABI 的派生视图层；如果它只是把 `linkPlan -> compiler_csg 收集 -> browser_abi_rule 过滤/格式化` 再包一遍，就该直接删。
  - `compiler_csg` 里同理也只该保留“从 linkPlan 收集 rules/specs/plans”这层；`manifest/source artifact` 这种纯派生文本应该直接回真模块现算。

- 新确认
  - `system_link_exec` 这种消费 browser bridge plan 的正式热路径，不该继续挂 `compiler_csg` 的 manifest/source/format/unique_key 薄壳。
  - 正确分层是：
    - `compiler_csg` 负责从 `linkPlan` 收集 `rules/specs/plans`
    - `browser_abi_rule` 负责 plan 的 key/manifest/source/emit/count 真语义
    - `system_link_exec` 只消费 plan，不再替 browser ABI 保第二套 helper 面

- 新确认
  - `browser_abi_rule` 这条线之前虽然已经有 schema/rule/plan 结构，但真语义仍然挂在 `inputKindText/outputKindText/...` 这些文本字段上；这会让 rule、codegen、smoke 各自再长一套字符串判断。
  - 这条线收口以后，稳定形状应该是：
    - 结构体里只存 enum
    - key/manifest/emit line 只在边界现算文本
    - schema 构造器直接产 enum
    - smoke 直接比 enum
    - `compiler_csg` 不要再额外包一层 browser ABI count helper；这类纯转发 wrapper 也是重复表面。

- 新确认
  - `browser_abi_rule_smoke` 这种要验完整 codec schema 的测试，必须显式把 `entryPath` 钉到 codec 源文件；不然编译器侧会走“按外部触发裁剪”分支，结果测到的是裁剪行为，不是完整规则矩阵。

- 新确认
  - `compiler_csg` 这类中间层最容易自己长出第二套有限域表面，不是靠字符串，而是靠“一格一个薄壳 helper”。
  - 如果同一事实已经能用“统一 enum 计数”表达，就不要再保留 `LocalCount / ExternalCount / ...`、`UnknownQualifiedTargetCount / ...` 这种固定格子 API。
  - 这类 helper 不删，主 smoke 很快又会跟着绑死一套平行接口。

- 新确认
  - 对已经完全切到 enum 真源的有限域，最容易留下的脏尾巴不是主逻辑，而是“没人再调的字符串兼容入口”；这轮 parser 里的 `CallKindCount(str)`、`HasExpr(str)`、`SurfaceCallKindFromText(...)`、`CallReasonFromText(...)` 就属于这类死口。
  - 这类口子不删，后面很容易又有人顺手把字符串喂回主链。

- 新确认
  - parser 既然已经把普通 expr detail 收成 `detailKind`、compat 文本改成现算，就不要再保留“手工清空 call detail 文本再比 typed facts”这种旧回归；那不是在验证真语义，只是在绑旧存储形状。
  - 这轮 `call_hir_matrix_smoke` 的 `blankCallDetails(...)` 就属于这类过期回归，继续留着只会把不存在的旧字段重新变成维护负担。
  - 当前 full host smoke 上真正拦路的还是既有 ordinary 红点：
    - `cheng/v3/lang/typed_expr::v3TypedExprComprehensionFilterIf`
    - `cheng/v3/lang/typed_expr::v3TypedExprBuildFact`
    - 以及由此牵出的 `compiler_csg_smoke / call_hir_matrix_smoke` 主发射失败
  - 所以这轮 parser 枚举化的正确验收口径要先看 parser smoke，不要把 unrelated typed_expr 红点误记成这次改动回归。

- 新确认
  - `call reason` 内部已经 enum 化，但只要 report 少吐两格，矩阵验收仍然是不完整的；这轮补上 `unknown_external_target` 和 `unknown_call_target` 后，CSG/lowering plan 的 call reason surface 才算真正闭环。
  - 当前 seed/ordinary 对“模块 enum 常量直接出现在 smoke main 的 `let` 绑定或函数实参里”仍有真实缺口。
    - 这不是 typed rule 设计问题。
    - 稳定修法是把枚举判断收进模块 helper，再让 smoke 问布尔事实，不要在 test main 里直接传模块 enum 常量。
  - `unknown_external_target/unknown_call_target` 目前在真实 parser surface 上基本是冷分支，所以这轮额外用 `v3ParserResolveCallTarget(...)` 的直探针把它们钉住，避免 report 字段虽然补了但枚举值本身没人验。

- 新确认
  - `compiler_main` 现在不适合直接并进正式节点主闭包；它自己仍会在 ordinary 编译链上撞闭包问题。统一节点里正确路径是直接收编稳定可库化模块，不是把旧 main 当库拖进来。
  - `libp2p surface` 的 handler 真相在 `host.supportedProtocols.len`，不是不存在的 `host.handlers`；surface 字段必须从 runtime 真结构取。
  - compiler 这条线现在已经能在节点内自产：
    - `CSG`
    - `export surface`
    - `compile receipt`
    - `world-sync`
    - `fresh-node-selfhost`
    - `equivalence proof`
    - `publish decision`
  - 当前还没闭的是 ordinary 后段真发射，不是节点统一入口；`compiler print-build-plan` 已把这个边界稳定暴露成 `primary_object_machine_words_missing / object_plan_not_ready_for_native_link`。

- 新确认
  - `callSurfaceKind` 这类有限域事实一旦还以字符串挂在 parser 节点上，typed/CSG/report 就会继续各自做一次文本解码；最稳的口径是 parser/typed 内部直接用 enum，文本只留给 compat 和报告出口。
  - 基础 parser helper 的签名漂移会把上层语法整片吞掉；这轮 `v3ParserFindMatchingPair(str,str)` 和 wrapper `char/char` 不一致，直接让 `Fmt(...)` 和列表生成式从 normalized expr layer 消失。
  - parser 索引边界不能赌短路求值；`i == 0 || text[i - 1] ...` 这类写法在热路径上会真实越界，必须拆成显式 `leftOk/rightOk`。

- 新确认
  - Harmony 这条线不能只靠 XComponent callback 自己猜资源入口；正式链路必须由 ArkTS 显式把 `getContext(this).resourceManager` 注入 native。
  - `route_state` 这类启动参数必须在 `cheng_app_init()` 前注入 runtime；只要错过初始化时刻，后补就是错误语义，不是等价实现。
  - `rawfile/mobile_shell_launch_args.{kv,json}` 可以保留为宿主常量真源，但真正 `OH_ResourceManager_OpenRawFile(...)` 前必须先剥掉 `rawfile/` 前缀。
  - Harmony 目前最小正确闭环已经固定：
    - ArkTS `aboutToAppear()`
    - `setResourceManager(...)`
    - native rawfile 读 sidecar
    - `cheng_mobile_host_runtime_set_launch_args(...)`
    - `cheng_app_init()`

- 新确认
  - compat 文本如果只是历史协议输出，不必继续先造 `str` 再写 `ByteBuf`；直接流式写出老字节协议更稳，也更省。
  - 这种“协议不变、实现换成流式”的改动必须补字节完全一致回归，不能只看最终 smoke 过不过。

- 新确认
  - 真要把 call `detail` 退出热路径，只改 parser append 还不够；`exprId` 也必须直接按结构字段生成，不然还是会在热路径里绕回 `CallExprDetailExact(...)`。
  - 当 call `detail` 已经退成兼容层后，最干净的口径就是 parser 产物里直接存空串，compat 文本只在显式输出点现算。

- 新确认
  - call `detail` 真要退成兼容层，不能只改 typed/report；parser 自己的去重键、`exprId`、CSG 兼容输出也都要从结构字段现算。
  - 最稳的强回归不是“字段有值”，而是“把 parser layer 里的所有 call `detail` 清空后，typed facts 仍然完全一致”。
  - 同一位置的 call 是否重复，应该由结构化 call fact 决定，不该继续由 `detail` 文本决定。

- 新确认
  - 当 call 结构字段已经成真源以后，smoke/probe 也必须同步切过去；如果验收还盯 `detail`，就等于把旧第二真源继续保活。

- 新确认
  - call 归因这组事实一旦已经存在 parser 结构节点上，layer 统计、typed fact 构建、CSG hash 都应该直接吃 `expr.call*`；继续回扫 `detail` 只会制造第二真源。
  - `detail` 现在更适合留作兼容文本和人工 probe，不该再承担 typed/lowering 主链语义。

- 新确认
  - `call args_text / arg_count / prefix_style` 这种会跨 parser -> CSG -> lowering plan 传递的表面事实，不能再塞进 `detail` 这类半结构化字符串；直接挂在 `V3NormalizedExpr` 上更稳。
  - `v3TypedExprFactsValidateCallResolution(...)` 之前那条无条件 `continue` 让 call 对表校验实际是空壳；这轮修掉后，parser/typed 漂移才会真被 gate 抓住。
  - call expr 一旦把右括号位置带进 `endColumnNumber`，expr layer 的 span 真相就不再只停在 callee 文本长度上。
  - 零参、多参、嵌套实参这三格现在已经有正式 smoke；typed call 真源不再只覆盖“单参普通调用”。

- 新确认
  - `callQualifier` 现在也已经进入 typed fact/rule 和 CSG hash；qualified external/member 的 typed 推断不再需要靠 `callTarget` 再切一遍点号。
  - `call_hir_matrix_smoke` 这轮已经把 typed `qualifier/callee/target/target_source/target_importc` 作为同一组结构化字段直接验掉，call typed 真源不再只是数数。
  - typed call 真源又往前收了一层：`callCallee/callTarget` 现在已经进入 `V3TypedExprFact/V3TypedExprLoweringRule`，typed return inference 直接吃这些字段，不再在推断阶段反复读 parser detail。
  - 这两个新字段已经进入 `compiler_csg` 的 append/hash；现在它们是 CSG 真内容，不是只挂在运行时结构里的附属字段。
  - `call_hir_matrix_smoke` 这轮已经直接钉住几条真实 call 的 typed `callee/target/target_source/target_importc`，新字段不再只是通过“总数一致”间接覆盖。
  - `compiler_csg append` 这轮又暴露了一个 ordinary 形状边界：`if ... else` 直接作为复合实参传递会把主链拉红；拆成局部字符串后就恢复，不是 typed 真源设计有问题。
  - `compiler_csg/lowering_plan` 的 call report 已经不再直接读 parser `expr.detail`；`resolved/reason/target_source/target_importc` 现在先进入 typed fact，再由 report 从 typed fact 统计。
  - 这次为了保持 ordinary 主链可编译，call 统计 helper 不能继续走“复合 facts + 枚举实参”形状；要收成专用单参计数函数，语义照样来自 typed fact，但调用形状必须贴当前 backend 支持面。
  - `call_hir_matrix_smoke / compiler_csg_smoke / lowering_plan_smoke` 现在都不只验 report 字段，还会直接校验 typed facts 和 parser expr layer 的 call resolved/unresolved、surface kind、reason、target importc 一致。
  - `bytes_parent_copy_orc_smoke` 之前那条“还剩 parent copy lowering 红点”的结论已经过期；这轮确认是测试里手工 release 和 ORC 赋值叠在一起制造的假红，不是当前主线 blocker。
  - call HIR 的 unresolved reason 现在已经不只停在 parser smoke；`call_hir_matrix_smoke` 已把四种 reason 分别接到 `compiler_csg/lowering_plan` 正式 report。
  - `Bytes` 当前真实语义不是“type=Bytes 就一定 owning”也不是“type=Bytes 就一定 borrowed”；同一表面同时承载 `bytesAlloc(...)` 的托管块和 `bytesFromString(...)` 的借用视图，retain/release 是否生效由 runtime registry 决定。
  - 上一轮 `parser_path_smoke` 暴露出来的 `std/os::joinPath -> std/buffer::appendBytes -> cheng_mem_retain` 崩点，本质上就是这条语义边界没被文档和验收钉牢，而不是 call HIR/report 本身坏了。
  - fresh `stage2` 现在能过 `parser_path_smoke`，说明更底层 `joinPath` 崩溃这轮不是当前主线 blocker。
  - call HIR 的 `resolved/unresolved/kind/reason/target_importc` 之前只存在 parser detail 和单点 smoke 里，没有进入 `compiler_csg/lowering_plan` 的正式 report；现在已经补成统一观测面。
  - `v3_emit_non_if_statement(...)` 之前缺了 `indirect_value` 局部赋值分支；这会让统一语句发射天然不等价。
  - builtin `assert(...)` 之前只吃字面量消息；`str` 模板产物、拼接结果、桥接指针都没进入同一条 bridge。
  - 顶层 `scalar function` 的 `return/call/scalar stmt` 看起来能直接复用 `v3_emit_non_if_statement(...)`，但 formal gate 证明现在还不能。回归不是 parser smoke 级别，而是 `perf_memory_contract_smoke` 里真实 `host exec` 热链路。

- 关于“编译理论下界”：
  - 现在可以直接拿正式 `system-link-exec` 的 `*.compile.report.txt` 当真源；phase 字段的真实出口在 seed/C 命令路径，不在 Cheng `compiler_main` 旁路。
  - 严格口径仍然只引用满足 `planner_total_ms <= compile_elapsed_ms` 的样本；当前稳定成立的是 `object_native_link_plan_smoke`、`chain_node_smoke`、`content_stub_smoke`、`orc_perf_contract_smoke`。

- 关于“内存和 GC perf”：
  - Cheng 没有 tracing GC，所以不存在 tracing-GC pause perf 这条线。
  - 当前已经做的是 ORC runtime perf，真指标是 retain/release 和 alloc/free/live。

- 关于“是不是实现了内存安全”：
  - 默认公开表面已经是 no-pointer + borrow/`Send/Sync` 约束。
  - 但 FFI、handle/raw pointer 桥接和环引用打断仍然需要显式管理，所以不能宣称成全域自动内存安全。

- `prepare scratch` 这条线现在已经把最大三块浪费都收掉了：
  - 同一表达式的二次 `infer_expr_type(...)` 已删。
  - 死字段、整块 `memset(0)`、`malloc/free` 抖动已删。
  - `args[32][4096]` 已改成 call-only lazy slab，非 call 路径不再白背这块巨内存。
- `v3_prepare_expr_call_state(...)` 外层原来还多背着一份本地 `V3ExprSurface`，再整份拷进 `scratch`；这轮已经改成直接 classify 到 `scratch->surface`。
- 这轮 fresh gate 的稳定形态是：
  - 第一次 `compile=4530ms/run=6420ms/chain=3380ms/content=7300ms`
  - 第二次 `compile=4430ms/run=6610ms/chain=3320ms/content=7340ms`
  - 说明 `prepare scratch` 这条线已经接近收益边界，再继续抠不会还是最大头。
- 下一步边界：
  - `prepare scratch` 先停。
  - 直接回 fresh 采样，按新的第一热点继续，不靠感觉猜。

- 真瓶颈已经从“seed 里再猜一次 call/type”转到“parser 还在做太多文本体操”。

- 已证实有效的规律
  - 只要 helper 只依赖文本，不依赖作用域和类型，就应该走 exact/range，不要反复 `V3PathTrim/FirstToken/TextSlice`。
  - call 行扫描里，必须先过 `hasCallParen` 这类硬条件，再决定要不要切 `callName/qualifier/dotted`。
  - 顶层函数声明和 import 行这种固定语法，最值的做法是单次 trim 后原位判定，不是 `trim -> startsWith -> trim -> firstToken -> identPrefix`。
  - qualified shadow 这类判断不要先切字符串再比较；直接拿 target 原文本 range 更稳。
  - 热循环里不能按值复制复合结构。`profile.decls[i]` 这种看起来只是取一项，实际会把 `str` 字段一起带成 `copyMem` 热点。
  - grouped import/module path 这类路径文本，不能再走 `split/trim/join`；exact builder 更稳。

- 当前剩余热点
  - `v3ParserResolveUnqualifiedExternalCallTarget(...)`
  - `v3ParserCallExprDetailExact(...)`
  - `v3ParserMaxQualifiedCallDepthLines(...)`
  - `v3ParserPrepareCodeLines(...)`
  - `v3ParserReadImportEdgesLines(...)` 里剩余的 `SourceExists/FileSize` 和 path concat

- 当前边界
  - `Bytes[] add/setLen` 那条已知高风险支线先不碰。
  - `V3ExprPrepScratch` 仍然大，不能直接改栈对象。
  - `infer_expr_type` 不能退化成纯文本 cache；它受函数/locals/源码行影响。

- `cheng_node` 第三阶段这轮确认下来的点：
  - `bytesFromString(...)` 只是 `str` 视图，不是 owning copy。凡是要进 `libp2p host store` 或跨函数长期存活的 payload，必须再做一次真实 `Bytes` 拷贝。
  - `identify` 是公开接入面的基础协议，不能错误绑在 `trustedView` 上；信任视图只该影响地址可见性，不该把节点自己的基础资源导出彻底封死。
  - 当前 ordinary 发码对 `while + 显式局部变量` 形状明显更稳；`for` 变量越作用域和 `return Err(...长串拼接...)` 这类大表达式，仍然容易把 primary object 拉红。
  - 同一个 state 文件的 `ctl` 写操作不能并发验收；并发写会互相覆盖，正式验证必须串行。
  - 这轮 fresh 编译时间已经恢复到稳定档：
    - `cheng_node_transport_vpn_smoke real 26.69s`
    - `cheng_node_main real 28.42s`
  - `MaxQualifiedCallDepth -> raw line` 这条想法已经试过，正式 gate 回弹，不能硬留。
  - 当前重新 bootstrap 后，`perf_memory_contract_smoke` 会在 `perfMemoryRequiredOutputInt64` 暴露 `stmt_let` lowering 缺口；这条要单独查，不要和 parser 微优化混在一起。

- `cheng_node` 第四五阶段这轮新确认：
  - DiLoCo 线协议状态文本的真实字段名是下划线风格，不是点号风格；正确口径是 `job_current_outer_step`、`lineage_count`。
  - `cheng_node ctl` 这种 ordinary 主程序里，带可选别名的 flag 解析优先写成 `readFlagOrDefault -> 空串判定`，不要再把 `Result + IsOk/Value` 分支塞进同一个局部初始化；当前 lowering 很容易在这类形状上炸 `prepare binding infer type failed`。
  - `moq_fountain` 现在属于共享代码，不是纯移动代码；只要它引用了 `cheng_mobile_host_emit_log`，host runtime 就必须同步导出 stub，否则普通主机 smoke 会直接链接失败。
  - `diloco-merge` 控制面用 `--peer-seed` 兼容 `--leader-seed` 是必要收口；不然同一族命令只有 merge 一个名字特殊，正式验收很容易误用。
  - `cheng_node_main` 全集成后的最终编译时间已经重新测准：
    - `real 41.79s`

- `cheng_node` 第六阶段这轮新确认：
  - 仓库里当前没有找到独立旧 `VPN/DiLoCo` durable state 文件真源；现阶段只能先把 `chain/oracle/compiler` 三条确定迁移链正式落地，不能硬编一份旧格式。
  - `keyed query` 的正式返回面是 `payload_text_hex`，不是明文 payload；凡是要验证 receipt/target/oracle 的对象正文，应该直接读 object store，不要对 query 输出做明文包含断言。
  - 旧模块一旦被并进 `cheng_node` 正式 ordinary 闭包，就必须把旧模块本身也修到 ordinary 可编译；不能继续默认“旧主程序单独能跑就行”。
  - compiler 的 `target` 索引之前只是注册了 group，没有真实写入路径；这轮 receipt 锚定补齐后，`compiler/target` 才第一次变成真实 O(1) 可查状态。

- v3 ORC / backend driver 这轮新确认：
  - `v3_emit_clone_slot_to_address_for_assignment(...)` 不能复用调用者正在用的 address spill 区；在额外 `sub sp, #16` 的形态下，helper 里的 `call_arg_base + 16` 会和调用者保存的目标地址落到同一个绝对槽位，结果 `retain/release` 都会打到 `rhs` 自己，旧 payload 永远停在 `rc=1`。
  - 对共享 `Bytes` owner，不能再写“手工 `memRelease(field.data)` 然后 `field = emptyBytes()`”；第二步现在已经是正式 ORC 赋值，会再 release 一次活跃 payload。共享 owner 的正确收口面就是直接 `field = emptyBytes()`。
  - runtime registry + assignment 专用 clone + 独立 spill 槽位补齐后，`bytes_overwrite_orc_smoke` 已经全绿，而且同一轮 fresh `build-backend-driver` / `artifacts/v3_backend_driver/cheng status` 也恢复正常，说明这不是单个 smoke 的旁路修补，而是主链问题。
  - `cheng_v3_buffer_handle_from_raw_bridge(...)` 现在已经确认是复制语义，不是借用；源 `Bytes` 改写或 release 以后，handle 侧快照必须不变，这条边界现在由 `bytes_buffer_handle_copy_smoke` 单独钉住。
  - 同一份源缓冲如果连续注册两个 `buffer handle`，语义也已经收实：snapshot 在注册 handle 当下完成，不在后续 decode 时才取值；所以前一个 handle 必须看到旧值，后一个 handle 必须看到改写后的新值，而且两次 decode 回来的 `Bytes` 不得 alias。
  - `release_raw_bridge` 这条线上也已经把 generation 复用收实：同槽复用后，新 handle 必须拿到 `generation + 1`，旧 handle 必须走 `detail=stale`，不能只落到 `detail=released`。
  - `build-ffi-handle` 不能继续依赖 backend driver 默认 forward 到旧 `stage3`；那条链会吞掉最新 fixture 语义。当前稳定做法是 backend driver 自己本地编译/运行四份 fixture，把普通成功、generation reuse 成功、released trap、stale trap 一次收全。
  - `layout.byteBufToBytes(...)` 和 `layout.byteSpanToBytes(...)` 现在也已经单独钉住 owning copy 语义；源缓冲后续改写或 release 不能影响副本，不然 `cheng_node` 这类 wire/state 主链会出现“看起来能跑、实际 payload 被悄悄掏空”的假正常。
  - 测 `byteSpanToBytes(...)` 时不能先 `byteSpanFromBytes(owningBytes)` 再断言源 rc 不变；`ByteSpan` 自己会持有那份 owning `Bytes`。要测 no-alias copy 边界，必须直接喂 borrowed `bytesSliceView(...)`。
  - `bytes_view_orc_registry_smoke`、`bytes_buffer_handle_copy_smoke`、`bytes_parent_copy_orc_smoke`、`bytes_overwrite_orc_smoke` 这四条现在都适合走默认成功型 `run-host-smokes`；它们分别钉住 registry 边界、raw-handle copy 边界、父对象复制、以及 local/field/parent overwrite 闭环。
  - 只要发射期在 composite call result / assignment clone 中临时 `sub sp`，局部槽取址就不能再拿当前 `sp` 当真源；`byteBufView(...)`、`Bytes` helper 参数、wrapper varparam 结果转发之前一起发红，根因都是这条局部地址漂移。
  - 这条线修好以后，`bytebuf_view_smoke`、`bytebuf_len_probe_smoke`、`bytes_param_helper_smoke`、`wrapper_result_forward_varparam_smoke`、`wrapper_rebuild_result_forward_varparam_smoke` 都恢复正常，说明问题在 frame materialize 真源，不在 `byteBuf` 或 wrapper 语义层。
  - `ffi_handle_stale_trap_smoke` 不能直接塞进默认 `run-host-smokes`；默认执行器把 trap 视为失败，这条必须继续挂在 `build-ffi-handle` 专用 gate 下验“成功样本 + stale trap”成对闭环。
  - fixed array 容器写这轮也摸清了一个硬边界：`grid.items[i].payload = bytesAlloc(...)` 会在 primary object 落到 `emit lvalue composite materialize failed`，所以当前 ordinary 主链还不能用这个形状去补 `Bytes` 容器 overwrite gate。

- mobile-shell / r2c 这轮新确认：
  - iOS 导出之前不是“文件没生成”，而是路径被大字符串串坏，实际写进了仓库根下的 `</string>/ios/...` 和 `\";/ios/...`。
  - `project.pbxproj` 这类大文本不能再和目标路径一起走同一层通用写文件 helper；当前 ordinary 下这会把路径值污染成文本片段。
  - 稳定形状是：
    - 固定 `rootDir + relPath`
    - 先把目标路径和父目录定死
    - 再生成大文本并写入
  - `os.Open(full, os.FmWrite)` 这条在当前 ordinary lowering 仍不稳；`os.openImplWrite(full)` 是已验证可过的同语义稳定面。
  - `mobile_shell_codegen_smoke` 重新跑绿后，runtime contract 导出面和三端骨架仍然完整，说明这次是主线修复，不是删功能换绿。
  - `Info.plist` 也属于会触发路径串台的 XML 文本；即使 manifest 记成已生成，真实文件也可能被错误写进仓库根下的 `</string>/ios/Info.plist`。
  - `build-probe` 之前的 rc=139 不是平台构建器本身崩，而是失败路径里的 summary 拼接/写出先炸，盖住了真实的 iOS 缺口；把失败路径 summary 拔掉后，真实错误才收敛到 `Info.plist` 丢失。
  - 当前 `mobile-shell export --platform ios`、`mobile-shell build-probe`、`verify-mobile-shell-build-probe`、`mobile_shell_build_probe_smoke` 都已重新全绿，说明“路径串台 + verify 链”这条已经正式收口。
  - runtime contract 真正要给平台宿主消费时，不能只留在导出根目录；必须同步镜像进：
    - Android `assets`
    - iOS `Resources`
    - Harmony `rawfile`
  - iOS 一旦把 runtime payload / bundle payload 加进 `Resources` build phase，默认导出也必须稳定生成这两个文件；没有外部输入时就写 stub JSON，不然 `xcodebuild` 会在复制资源阶段直接失败。
  - `mobile_shell_codegen_smoke` 里的 runtime manifest 常量断言必须跟平台真实资源相对路径一致：
    - iOS 是 `runtime/mobile_shell_runtime_contract_v1.json`
    - Harmony 是 `rawfile/mobile_shell_runtime_contract_v1.json`
  - 当前 ordinary 下，别在 `mobile_shell_codegen` 主线上直接追加大段 launch-args 常量或 JSON 拼装；这轮实测会把 `mobile_shell_tool` 自己打成 `cheng seq_set_grow: corrupt header`。这条要单独拆 probe 再查，不能混在 `export/build-probe` 主链里继续推进。
  - launch args 这条现在已经有稳定中间层：
    - root sidecar：`runtime/mobile_shell_launch_args.kv/json`
    - Android 镜像：`assets/runtime/mobile_shell_launch_args.kv/json`
    - iOS 镜像：`ios/runtime/mobile_shell_launch_args.kv/json`
    - Harmony 镜像：`rawfile/mobile_shell_launch_args.kv/json`
  - 两条独立 probe 都已确认：
    - launch args 文本本身稳定
    - launch args 文本 + `v3MobileShellWriteFile(...)` + 三端 old-shape 组合也稳定
  - 所以当前真正不稳的，不是 launch args sidecar 或文本内容，而是“把 launch args 直接内联进原 `mobile_shell_codegen` 宿主生成函数”那条具体模块形状。
  - 安全推进方式已经明确：
    - 先让三端宿主显式持有 launch args sidecar 相对路径常量
    - 再单独接宿主读取 sidecar
    - 不要再回到“直接把 launch args 大段文本内联进宿主生成函数”这条路
  - 这轮已经把安全推进方式落实到正式主线：
    - Android 宿主从 `assets/runtime/mobile_shell_launch_args.kv/json` 读取 sidecar 文本
    - iOS 宿主从 bundle 的 `runtime/mobile_shell_launch_args.kv/json` 读取 sidecar 文本
    - 两端都会在 runtime 初始化前调用 `cheng_mobile_host_runtime_set_launch_args(...)`
  - 结果上这条改动没有把正式链路打红；`mobile_shell_codegen_smoke`、`mobile-shell build-probe --platform all`、`verify-mobile-shell-build-probe`、`mobile_shell_build_probe_smoke` 都继续保持全绿。

- cheng_node_main 统一入口这轮新确认：
  - `layout.byteBufToBytes(...)` 和 `layout.byteSpanToBytes(...)` 如果不做 raw copy owning bytes，`cheng_node` 这类大状态路径会出现非常隐蔽的假正常：
    - 旧链状态文件 `payload_hex=` 直接写空
    - fixed32 解码报 `v3 bytes_layout: need 32 bytes`
    - payload CID 退化成空串哈希
  - `codec_binary.v3ReadFixed32(...)` 这种固定宽度读取，最稳的方案是直接逐字节读进 `FixedBytes32`，不要再绕 `ByteSpan -> Bytes -> FixedBytes32` 中间层。
  - state reload 的 stored-event replay 不该重放 live actor dispatch；恢复时只该重建账本、proof、索引和 runtime 状态。把恢复和运行时派发混在一起，会把无关 actor 路径变成持久化加载硬依赖。
  - 这轮 `cheng_node_main` fresh 编译时间已经重新测准：
    - `real 60.71s`

- cheng_node_main 入口口径这轮新确认：
  - 现在正式默认入口应该写成 `cheng_node_main <subcmd>`；`ctl` 只是兼容前缀，不该继续在帮助文本和默认 smoke 里当主口径。
  - `cheng_node_ctl_main` 里旧的 `V3ChengNodeCtlCommand*` 包装已经没有真实调用价值；保留它们只会让入口面继续双轨。

- cheng_node_main chain 多级子命令这轮新发现：
  - `cheng_node_ctl_main` 如果直接 import `bio_did_chain_node`，fresh ordinary 编译会立刻报：
    - `invalid or duplicate type head: .../v3/src/project/bio_reed_solomon.cheng`
  - 所以 `bio-did operator-lookup` 这条旧命令不能靠“直接拖回主闭包”解决；那样会把统一节点主程序重新打红。
  - `v3/src/tooling/gate_main.cheng` 里现在还保留大量 `chain-...` 旧形状脚本调用；这轮主程序已经有隐藏迁移别名，所以功能没断，但仓库口径还没完全改净。
  - 直接用当前 ordinary fresh 编译 `v3/src/tooling/backend_driver_main.cheng`，会落到一串历史遗留的 private import / primary object 缺口；这和本轮 `chain` 命令改动不是同一条问题线。

- mobile-shell runtime manifest / truth 资源这轮新发现：
  - `WalkDirRec + RelativePath` 在 truth 目录复制这条绝对路径组合上不稳。
  - 实际症状有两种：
    - 把整条绝对路径塞进 `runtime/truth/...`
    - rel suffix 直接丢空，truth 文件完全不落盘
  - 正式修法不是补条件，而是改成递归 `os.ListDir(...)`，直接沿目录树构造相对路径。
  - `.rgba` 这类 binary fixture 绝不能再走会补换行的文本镜像 helper。
  - Android / iOS / Harmony 三端宿主已经全部接上 `cheng_mobile_host_runtime_set_manifest_payloads(...)`，runtime manifest 不再只是导出副产物。
  - 现在新的主线缺口已经从“宿主没注入 payload”切到“runtime 已注入也要自己认账”。
  - `src/runtime/mobile/cheng_mobile_exports_shared.c` 这轮已经补上这条真源：
    - bundle 必须真的是 `native_gui_bundle_v1`
    - contract 必须真的是 `native_gui_runtime_contract_v1`
    - route / semantic / layout / render command / viewport / interactive 这些核心计数会直接进 runtime 上下文
    - runtime 没拿到 launch args 路由时，会先吃 bundle route，再考虑 semantic default
  - 这让后面的页面 1:1 对拍不再依赖“宿主写没写对文件”这种外围迹象，而能直接从 runtime frame reason 和 side effect 看 payload 是否真的被消费。
  - 但如果控制面还只盯 `runtime_contract_payload_ready=true`，那仍然只能证明“文本被注入”，不能证明“runtime 真认账”。
  - 所以 `cheng_mobile_host_runtime_state_json()` 也必须同步升级，把 runtime 解析出来的 bundle/contract ready 和计数显式吐出去。

- cheng_node_main compiler `system-link-exec` 这轮新发现：
  - `bootstrap_contracts` 不能直接并进 `cheng_node_compiler_domain` 主闭包；它会把 parser/bootstrap 重依赖链一起拖进 unified node，fresh ordinary 编译马上从“可过”退化成大面积 seed 语义缺口。
  - 这条线上真正需要的只有 bridge compiler 路径，不是 bootstrap 全能力；正式修法就是节点域里自己做纯路径探测，不要 import 整个 bootstrap contract 模块。
  - 当前 `cheng_node_main` fresh 编译虽然已经重新恢复通过，但 report 里仍会吐一条 seed 诊断：
    - `cheng/v3/lang/parser::v3NormalizedExprLayerAppendSpan`
  - 这条没有阻断产物生成，`/tmp/cheng_node_main_fullstack4` 和节点内 `compiler system-link-exec` 都已实跑通过；但它说明 parser 这块 ordinary 发码边界还没有彻底干净，后面如果继续把更多编译器内部能力并进主闭包，优先盯这条函数形状。
- `callReason` 这种有限域只要继续用 `str` 穿 parser -> typed -> csg，仓库里就会天然再长一份“文本真源”。
  - 这轮把内部口径改成 enum 后，`call reason drift` 终于只剩一份事实。
  - 同时也实锤一个现有实现边界：seed 对“返回 `str` 的 helper 直接参与 `==`”还不稳，所以 smoke 内部断言也应该直接比 enum，文本只留给 report/日志输出。

- `debug-report / print-symbols / print-line-map / print-asm` 之前失败的真根因已经钉死：
  - 不是 `cheng_node` 转发链，也不是 backend_driver 参数拼接。
  - 是 `v3/bootstrap/cheng_v3_seed.c` 里这四个命令把 `V3BootstrapContract / V3SystemLinkPlanStub / V3CompilerWorldArtifacts / V3PrimaryObjectPlanStub / V3ObjectPlanStub / V3NativeLinkPlanStub` 全压在栈上，Darwin 直接在 `___chkstk_darwin` 崩掉。
  - lldb 回溯已经实锤：
    - `v3_cmd_debug_report + 48`
    - `v3_cmd_print_symbols + 48`
    - `v3_cmd_print_line_map + 48`
    - `v3_cmd_print_asm + 48`
  - 正式修法就是 heap exec context；修完后：
    - `artifacts/v3_backend_driver/cheng run-host-smokes debug_tools_surface_smoke` 通过
    - `artifacts/v3_backend_driver/cheng debug-report ...` 直接恢复
    - `cheng_node_main compiler debug-report / print-symbols / print-line-map / print-asm / print-object / profile-report` 全部恢复

- `r2c` 页面级 native GUI 这轮新发现：
  - 只要 `bundle outDir` 和 `codegen manifest` 不在同一个目录，`bundle helper` 就必须把显式 `--codegen-manifest` 当真源；不能因为 `outDir/cheng_codegen` 或 `outDir/cheng_codegen_route_catalog_v1.json` 恰好存在就优先吃旧残留。
  - `static_surface` 这条 ordinary 红点的真根因不是语义层，而是写文件形状：
    - `r2cSurfaceWriteModuleInventory`
    - `r2cSurfaceWriteTailwindManifest`
    - `r2cSurfaceWriteAssetManifest`
    - 只要继续走 `os.Open(path, os.FmWrite)`，ordinary fresh compile 就会直接炸在 `stmt_let`
    - 改成 `v3path.V3WriteTextFile(...)` 后立刻恢复
  - `native_gui_run.mjs` 之前只把 `native_gui_run_report_v1` 打到 stdout，不写文件；这会让 smoke 和产物目录口径长期分叉。
  - 旧 `r2c-react-v3-fresh-clean-gate` 现在仍然绑着 legacy wrapper / Python compile 轨，不适合作为当前页面级 native GUI 主线 smoke 的验收真源。
  - 正式主线 smoke 口径已经明确：
    - `codegen_surface -> static_surface -> native_gui_bundle -> native_gui_run`
    - extra route 先用 fresh bundle 覆盖
    - 不再借道 legacy `fresh clean gate`
  - 当前 host smoke 时长边界也已经量出来：
    - `home_default run + content_detail bundle` 可过
    - 再叠 `publish_selector` 第三路 fresh bundle 会把默认 host smoke 顶到超时

- `r2c` 页面级 native GUI 这轮继续确认：
  - `publish_selector` 这种额外 route 不该继续往重型 smoke 里叠。
  - 正式修法是拆成独立轻量 bundle smoke，再由 `verify-r2c-react-v3-surface` 顺序串起来。
  - `verify-r2c-react-v3-surface` 这种多 route 验收，不适合继续复用“单 smoke helper 自动打印成功”那层壳。
    - 最稳口径是显式顺序跑完每条 smoke，最后只打一条总 `ok`。

- `r2c` 页面级 native GUI 这轮新确认：
  - controller fresh compile 不是“外层 wrapper 小问题”，而是正式主线 gate。
    - 只要 `v3/src/tooling/r2c_process.cheng` 继续保留第二套 raw capture/importc 路径，fresh compile 就会直接在 `r2cProcessCapture / r2cProcessRunProgramLogged` 上炸。
  - `r2c_react_v3_surface_main.cheng` 前面三个 manifest writer 改完还不够，`r2cSurfaceWriteRsg(...)` 和 `r2cSurfaceWriteBlockerReport(...)` 也必须一起退出 `os.Open(path, os.FmWrite)`。
    - 不然 `static_surface` 的 fresh compile 还是会被剩余写文件红点拦住。
  - `run-native-gui` controller smoke 不能直接调用 `artifacts/v3_bootstrap/cheng.stage3 r2c-react-v3 ...`。
    - 只要源码 mtime 漂了，这条路就会重新触发 `bootstrap-bridge` 噪音，把 smoke 结果和 controller 真问题混在一起。
  - 即使已经 fresh compile 出了 controller 可执行，当前 smoke 也不该继续走它内部隐式 `native-gui-bundle` 路径。
    - 最稳口径是外部先明确产出：
      - `codegen_surface`
      - `static_surface`
      - `native_gui_bundle_v1.json`
      - `native_gui_session_v1.json`
    - 再把 `--bundle-path / --session-path` 显式喂给 controller。
  - `verify-r2c-react-v3-surface` 现在已经不只是“页面 bundle smoke 集合”。
    - 它还正式覆盖了 controller report 面，开始验 runtime payload 被 native GUI runtime 真正消费后的字段。

- `r2c` 页面级 compare 这轮新确认：
  - `compare-truth` 之前虽然已经有 `truth_compare_v1`，但还只盯 `route/render_ready/semantic_nodes_count`。
  - 真正页面级 native gui 对拍要能看到 runtime 已经认账的 bundle/contract 真值，不然 compare 产物只能说明“exec snapshot 对上了”，说明不了“native gui runtime 真的吃进去了”。
  - 这条线最稳的补法不是重做第二套 compare 命令，而是在现有 `truth_compare_v1` / `compare_truth_report_v1` 上直接并入 `native_gui_run.summary.env` 真值字段。
  - `--native-gui-summary` 必须是显式硬输入；传了但文件不存在就直接失败。未显式传时，再自动尝试同目录 `native_gui_run.summary.env`。
