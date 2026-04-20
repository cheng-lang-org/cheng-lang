# Lessons

## 总原则

- 单一真源优先：同一事实不要在 parser、typed、lowering、seed、wasm 各猜一份。
- 能显式 unknown 就显式 unknown，不要拿空串、布尔值或文本猜测冒充已知语义。
- 复合值优先按地址语义思考，不要在热路径上混用按值/按句柄/按临时槽。
- 验收看 fresh 闭环和正式 smoke，不看一次性现场。

## 当前稳定经验

- `call expr` 最稳的真源是结构化 fact：`callee + args_text + arg_count + prefix_style`。
- 这组 call 表面事实如果要跨 parser -> CSG -> lowering 传递，优先直接挂 `V3NormalizedExpr` 结构字段，不要继续塞进 `detail` 的 `key=value` 文本。
- 当 parser 节点已经有 `callSurfaceKind/resolved/reason/target_source/importc` 这类结构字段时，layer 统计、typed 构建、CSG hash 要一起切过去；不能留一半结构字段、一半 `detail` 回扫。
- parser 自己的 call 去重键、`exprId`、兼容 detail 也都要从结构字段现算，不要继续把存储里的 `expr.detail` 当 identity。
- 验收这条线最稳的强回归是：把 parser layer 里的所有 call `detail` 清空后再建 typed facts，结果必须完全不变。
- 同理，smoke/probe 也要跟着切到结构字段；否则主链已经单一真源，验收却还在喂旧真源。
- 校验函数里的 fast path 一定要看清 `continue` 落点；只要把 call 主体误短路掉，整条“typed 对表 parser”就会变成假 gate。
- `infer_expr_type` 不能做纯文本 cache；key 至少要带当前函数、源码行、local 数量。
- `V3ExprPrepScratch` 这类大对象不能直接搬栈；只能做 lazy/pool/拆分。
- `Bytes` 不能按类型直接判成 owning 或 borrowed；`bytesAlloc(...)` 和 `bytesFromString(...)` 共用同一表面，真正的 retain/release 语义现在由 runtime registry 判是不是托管块。
- 遇到 `Bytes` 生命周期问题时，要先分清“runtime registry 边界”和“owner 赋值路径”到底是哪一层在红；`bytes_parent_copy_orc_smoke` 这轮证明过，手工 `memRelease(...)` 再接 ORC 赋值会制造双释放假红点，不能把测试误用当编译器 bug。
- `cheng_v3_buffer_handle_from_raw_bridge(...)` 是复制语义，不是借用视图；源 `Bytes` 可以立刻 overwrite/release，这条边界现在要靠 `bytes_buffer_handle_copy_smoke` 钉住。
- 同一份源缓冲连续注册多个 `buffer handle` 时，快照时机在“注册 handle 当下”，不是后面的 decode；早注册的 handle 必须看到旧值，晚注册的 handle 必须看到新值，而且两次 decode 出来的 `Bytes` 也必须彼此独立。
- FFI/raw-handle 这层还要单独钉 generation：同槽复用后，新 handle 必须拿到 `generation + 1`，旧 handle 必须进 `detail=stale` trap，不能只停在“released”。
- `layout.byteBufToBytes(...)` 和 `layout.byteSpanToBytes(...)` 也必须维持 owning copy；要测这条边界时，`byteSpan` 侧要直接喂 borrowed `bytesSliceView(...)`，不要先 `byteSpanFromBytes(owningBytes)` 再把“中间持有者多 retain 一次”误判成 copy 语义回归。
- `build-ffi-handle` 这类需要最新 fixture 语义的命令，不能继续无脑 forward 给旧 `stage3`；当前稳定口径是在 `backend_driver_main` 本地直接编译/运行 fixture，而且要同时覆盖普通成功、generation reuse 成功、released trap、stale trap 四条边界。
- 只要发射期会临时 `sub sp` 去物化调用结果或 assignment clone，局部槽、`var` 字段、参数 helper 取址就必须走稳定 frame base，不能直接拿当前 `sp` 当真源；这条现在由 `bytebuf_view_smoke`、`bytebuf_len_probe_smoke`、`bytes_param_helper_smoke`、`wrapper_*_varparam_smoke` 钉住。
- fixed array 里的 `grid.items[i].payload = bytesAlloc(...)` 这种 `Bytes` lvalue materialize 现在还不在 ordinary 主链支持面上；容器写边界先别往这条形状硬推。
- seed/selfhost 现在不要写 `discard foo()`；这条写法会直接炸 seed lowering。
- parser 热循环里不要按值复制复合结构；直接按字段读。
- parser 里所有只依赖文本的判断，优先 exact/range，不要 `trim -> firstToken -> slice` 连环调用。
- call 行扫描必须先过硬条件，再切字符串；`hasCallParen` 前不要急着造 `callName/qualifier/dotted`。
- report 和 smoke 里的 call 统计不要再手写第二套枚举；直接复用 parser 的结构字段，不要回扫 `detail`。
- 但 `compiler_csg/lowering_plan` 不该长期直接回扫 `expr.detail`；`resolved/reason/target_source/target_importc` 这组 call 归因要前移进 typed fact，再由 report 从 typed fact 出数。
- 同一条原则继续往前推时，`call callee/target` 也应该进 typed fact；typed return inference 直接吃 fact 字段，比在各处再拆 `expr.detail` 稳。
- 再往前一步时，`qualifier` 也该进 typed fact；qualified external/member 的 typed 推断优先吃 `qualifier + callee`，不要继续把 `target` 当原始字符串来回切。
- 顶层函数声明、import 行、grouped import 这类固定语法，优先单次 trim 后原位判定。
- 非 import / 非 top-level fn 这种绝大多数行，不要先造整条 trimmed 字符串；先用 range 判定，命中后再切。
- qualified shadow 这类逻辑优先比较 range，不要先切 `rootQualifier` 子串。
- module path 这类路径文本，不要走 `split/trim/join`；exact builder 更稳。
- parser 里任何索引边界都不要赌短路；显式拆开判断。
- `V3ExprPrepScratch` 里像 `args[32][4096]` 这种 call-only 巨块，不要继续常驻内嵌；按需 slab + 线程本地 free-list 是稳定收益。
- wrapper 外层如果最终还是要把大对象放进 `scratch`，就直接写进 `scratch` 本体，不要先放一份局部再整份复制。
- 当 `prepare scratch` 已经把二次 infer、死字段、整块清零、allocator 抖动、call-only 巨块都收掉后，就该停；继续围着这条线磨，通常已经不是理论上的最大头。
- 正式 `system-link-exec` 的 phase 真源在 seed/C 命令路径，不在 Cheng `compiler_main` 旁路。
- 编译理论下界只能引用 `planner_total_ms <= compile_elapsed_ms` 的正式 `system-link-exec` phase 样本；当前稳定可引用的是 `object_native_link_plan_smoke`、`chain_node_smoke`、`content_stub_smoke`、`orc_perf_contract_smoke`。
- ORC alloc/free/live 计数如果在 `echo` 前还继续做字符串格式化，数字会被噪声污染；先冻结 delta，再输出。

## 验证口径

- 改 seed/backend 后，固定流程是：
  - `stage0 compile`
  - `bootstrap-bridge`
  - fresh `stage2 no-handoff` host smokes
- 跑 `perf_memory_contract_smoke` 时，当前主线必须显式钉：
  - `CHENG_V3_SMOKE_COMPILER=/Users/lbcheng/cheng-lang/artifacts/v3_bootstrap/cheng.stage2`
- 文档或本地 skill 镜像一旦改了，必须立刻跑：
  - `artifacts/v3_backend_driver/cheng run-host-smokes cheng_skill_consistency_smoke`
- 需要定热点时直接用真实可执行的 `/usr/bin/time -l` 和 `sample`，不要靠感觉猜。
- sample 挪走热点不等于正式 gate 就一定更好；只要 perf gate 回弹，实验必须立即撤回。
- 顶层 `scalar function` 发射和 `v3_emit_non_if_statement(...)` 不能机械并线；哪怕核心 smoke 过了，`perf_memory_contract_smoke` 也会在 `host_ops/os_host_process` 这条正式链路上把回归炸出来。先补齐逐类 parity，再谈收口。
- analysis smoke 如果只验 `exprLayer/typedExprFacts/export surface`，就直接构最小 CSG；不要在 smoke helper 里重复做 source bundle hash 和整图 CID。
- ordinary 当前对“`if ... else` 直接作为复合实参传给函数”这类形状还不稳；像 CSG append/hash 这种热路径，先拆成局部字符串再传，别把发射红点误判成模型设计问题。

## 当前不要碰

- `Bytes[] add/setLen` 高风险支线
- 大对象上栈
- parser probe 级微调
- 任何降级、兜底、启发式补丁

## cheng_node / libp2p / VPN

- `bytesFromString(...)` 是借用视图，不是 owning copy；只要 payload 要进 host store、跨 actor、跨函数或跨事件账本存活，就必须立刻复制成真正的 `Bytes`。
- `identify` 这种基础接入协议不要再绑 `trustedView`；trust 只影响可见地址和高权限面，不该把公开接入节点自己的基础资源导出封死。
- ordinary 当前对 `while` 和显式局部变量更稳；`for` 变量出循环后继续用、或者 `Err(...)` 里内联长串拼接，都会更容易踩 primary object 红面。
- 验收 `cheng_node ctl` 这种会写 state 的命令时，不能并发打同一个 state 文件；必须串行，不然结果只是在测最后一个写入者。
- `cheng_node ctl` 里可选 flag 别名优先走 `readFlagOrDefault(...) + 空串判定`；不要把 `Result + IsOk/Value` 分支塞进同一个局部初始化，ordinary lowering 在这类形状上不稳。
- 共享模块一旦 `@importc` 到移动宿主符号，host runtime 也必须同步补导出 stub；否则不是运行时才炸，而是普通主机链接直接失败。
- DiLoCo wire/status 的字段名口径是下划线展开，不是点号路径；写 smoke 和 keyed query 断言时必须按真实 wire 名称来。
- 旧独立模块并进 `cheng_node` 普通主闭包时，要先确认它自己也能走 ordinary 编译；如果 closure 因旧模块红掉，直接修旧模块，不要把它继续当“外部黑盒”。
- `V3ChengNodeKeyedQueryText(...)` 用来验 `found/proof/payload_present` 很合适，但它返回的是 `payload_text_hex`；要核对 payload 正文时，直接用 index entry 的 `valueCid` 去读 object store。
- `mobile_shell_codegen` 里只要文本内容本身含有 `</string>`、`<group>`、`\";` 这类片段，就不能再把“目标路径 + 大文本”一起交给通用写文件 helper；当前 ordinary 下很容易把路径串成文本片段目录。
- iOS `project.pbxproj` / workspace 这类大文本文件，稳定做法是：
  - 固定 `rootDir + relPath`
  - 先建父目录
  - 再生成文本并写入
  - 不要先拼绝对路径再把它和大字符串一起传递。
- `Info.plist` 这种 XML 文本也一样；只要正文里有 `</string>`，就必须和 `project.pbxproj` 一样走绝对路径直写，不能回到 `V3WriteTextFile(rootDir, relPath, payload)` 这条通用 helper。
- 当前 ordinary 对 `os.Open(full, os.FmWrite)` 仍有 lowering 缺口；同语义场景优先用已经验证稳定的 `os.openImplWrite(full)`。
- `mobile_shell_codegen_smoke` 如果看到“iOS 文件不存在”，先查 `/Users/lbcheng/cheng-lang` 根下有没有新冒出来的 `<`、`\";` 之类目录；这通常不是导出没跑，而是路径串台了。
- `mobile-shell build-probe` 如果先报 rc=139，不要先怀疑平台构建器；先把失败路径里的 summary 拼接/写文件拿掉，不然 runtime 自己先炸，真实的 xcodebuild/gradle/hvigor 错误会被完全盖住。
- `mobile_shell_runtime_contract_v1.json` 不能只留在导出根目录；平台宿主真正消费时，必须同步镜像进 Android `assets`、iOS `runtime/Resources`、Harmony `rawfile` 这三处真实打包目录。
- iOS 只要把 runtime payload / bundle payload 写进 `Resources` build phase，就必须保证默认导出也有对应文件；没有外部输入时直接落 stub JSON，别把缺文件留给 `xcodebuild` 复制阶段才炸。
- smoke 里校验 runtime manifest 常量时，必须按平台真实相对路径断言；iOS 是 `runtime/mobile_shell_runtime_contract_v1.json`，Harmony 是 `rawfile/mobile_shell_runtime_contract_v1.json`，不能再偷用导出根目录那份名字。
- `mobile_shell_codegen` 主线里不要顺手塞“大段 launch-args 常量 + JSON 拼装”这种新字符串形状；这轮会把 `mobile_shell_tool` 自己打成 `cheng seq_set_grow: corrupt header`。要查这条，必须先拆成独立最小 probe，不要拿正式 `export/build-probe` 当试验场。
- launch args 这条先走 sidecar 文件是稳定面：先生成 `runtime/mobile_shell_launch_args.kv/json`，再镜像进 Android `assets`、iOS `runtime/Resources`、Harmony `rawfile`，不要急着直接内联进宿主源码。
- 如果下一步要接宿主读取 launch args，先把 sidecar 相对路径常量单独写进宿主源码；这条是稳定的，也不会像内联大段 launch args 文本那样把 `mobile_shell_tool` 打红。
- launch args probe 如果已经证明“文本本身”“`v3MobileShellWriteFile(...)`”“三端 old-shape 组合”都稳定，那剩下就不是 launch args 内容问题，而是原 `mobile_shell_codegen` 模块的具体组合形状问题。
- Android / iOS 宿主接 launch args 时，稳定形状是“宿主自己从打包资源读 sidecar 文本，再调用 `cheng_mobile_host_runtime_set_launch_args(...)`”；不要把 launch args 正文重新拼回导出期大字符串里。
- Android 这条稳定口径已经是：
  - Kotlin `assets.open(...).bufferedReader().use { it.readText() }`
  - `nativeCreate(..., launchArgsKv, launchArgsJson)`
  - C host 里 `GetStringUTFChars / ReleaseStringUTFChars`
- iOS 这条稳定口径已经是：
  - `NSBundle mainBundle` + `pathForResource:ofType:inDirectory:`
  - `NSString stringWithContentsOfFile`
  - 然后把 UTF-8 文本直接交给 `cheng_mobile_host_runtime_set_launch_args(...)`
- 同一个 host smoke 不要并行跑两份；它们会抢同一个 `artifacts/v3_hostrun/<smoke>` 目录，能制造 `primary.o is empty` 这种假红。
- `byteBufToBytes(...)` / `byteSpanToBytes(...)` 这种公共 copy helper 不能偷懒走切片视图；只要后面要写文件、做 CID、做 fixed32/fixed64 解码，就必须是 raw copy owning bytes，不然很容易出现 `payload_hex=` 空、空串哈希、`need 32 bytes` 这类假象。
- 固定宽度二进制读取优先直接填目标结构；像 `v3ReadFixed32(...)` 这种路径，逐字节写 `FixedBytes32` 比 `ByteSpan -> Bytes -> FixedBytes32` 多一道中转稳得多。
- state reload / migration replay 不是 live event dispatch；恢复路径只负责重建账本、proof、索引和 runtime 状态，不该再触发 actor dispatch、副作用发布或实时控制面逻辑。
- 入口收口时，默认帮助和 smoke 口径也必须一起切到正式宿主；现在应该固定成 `cheng_node_main <subcmd>`，`ctl` 只保留兼容，不要继续把双入口写成主设计。
