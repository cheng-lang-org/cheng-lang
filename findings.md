# 当前发现

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
