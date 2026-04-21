# 当前发现

- provider object 物化之前是真实编译缝隙，不是 planner 理论下界。
  - 当前已经用 provider object cache 收掉热路径秒级成本，最新 gate 中五个样本均为 `provider_cache_hits=5 provider_cache_misses=0`。
  - 命中后 `provider_objects_ms` 稳定在 `13-19ms`；`cheng_provider_compile_ms/c_provider_compile_ms` 为 `0` 时表示这轮没有重新编 provider object。
  - cache key 必须吃 source CID、target、package/root、compiler path、codegen CID 和 suppressed exports；不能只靠输出 object 路径。

- no-handoff 核心 smoke 暴露的 `cstring(dataPtr0)` 失败属实，根因是 parser constructor 分类过宽。
  - `cstring(x)` 这类标量/指针 cast 不是复合构造，不能进入 `ConstructorExpr`。
  - 正确处理是让 `v3ParserConstructorTypeStatus(...)` 在入口排除标量/指针类型；typed 的 `type-call fact must materialize composite` 校验必须保留硬失败。
  - `lowering_plan_smoke` 的空字段读取也要修，否则 smoke helper panic 会遮住真正的 typed/parser 缺口。

- 这轮真实热路径缺口不是先调阈值，而是通用字节搬运还在逐字节跨 helper。
  - `bytesConcat/bytesConcat3`、SHA-256 输入拷贝、P-256 deterministic concat/fill、公钥/签名字节组装都会反复走 `bytesGet/bytesSet`。
  - 当前正确修法是复用已有 `RawmemCopy/RawmemSet` 做固定长度 bulk copy/set；不要新增 C bridge、不要靠放宽 perf 阈值掩盖。
  - `bytesCopyInto` 这种低层 helper 必须 let-it-crash：nil 或越界直接断言，不能静默 return。

- `perf_memory_contract_smoke` 现在能同时回答四件事。
  - 编译理论下界看 `*_compile_exec_phase_summary.planner_total_ms`。
  - planner 外编译缝隙看 `*_compile_gap_breakdown`，当前 provider object 热路径已由 cache 命中压到十几毫秒，剩余主耗时是真实 primary object emit，不再混进理论下界。
  - 内存/生命周期看 `orc_perf_contract` 的 retain/release 与 alloc/free/live。
  - crypto 热核看 `crypto_hot_kernel_contract` 的 SHA-256、X25519 pubkey、P-256 pubkey、P-256 sign 单次 ns。

- `100ms` 编译和二进制原地更新不能按 release/system-link 口径解释。
  - 当前只允许说 dev host-only `self-link + direct-exe + host runner hotpatch` 的 dedicated witness。
  - `dev_hotpatch_100ms_scope_contract_smoke` 已把 formal spec、README、tooling README、repo skill、home skill 里的这句话固定成门禁。

- `lower generic function` 不能只按原始文本或首字母跳过。
  - `normalize_type_text` 之后，像 `elemSize[T]()` 这类 generic function 可能长得像 bracketed type；如果不先看“是不是已知类型”，就会被误打成 `seq/fixed-array T()`。
  - 当前正确口径是：先规格化，再排除真实类型面，最后才把剩余 lowercase bracket call 当成 generic function。

- ordinary parser 上这类 helper 也要考虑 seed no-handoff 编译形状。
  - `v3ParserTypeExprLooksLikeLowerGenericFunction(...)` 之前那种重复 `Trim/FindFirst/IdentPrefix` 的超长表达式，会把 `typed_expr_call_validate_probe` 炸成 `prepare binding infer type failed`。
  - 这种 helper 保持单次 trim + 线性循环最稳，不要再堆重复 helper call。

- `parser_path_smoke` 里的 normalized layer 是 closure 级，不是单文件级。
  - 断言 `elemSize[int32]()` 不被误判时，不能用全局 constructor/default-init count；要按 `sourcePath + lineNumber` 精确过滤当前 fixture 行。

- `stage3_system_link_exec_handoff_smoke` 读的是全局 `artifacts/v3_forward/backend_driver.forward.log`。
  - 它不能和其它会写 forward log 的 smoke 并跑；否则路由本身已经成功，断言也会被后续日志覆盖误伤。

- seed 里真正多余的不是 `V3TypeCallSurface` 本身，而是 `v3_parse_default_expr_text_with_context(...)` 这层 default-init 专用薄壳。
  - 它之前让 const-eval、infer、native、wasm 各自再走一遍“expr 文本 -> default type”专线，虽然底下最终还是调同一个 `v3_classify_type_call_surface(...)`。
  - 现在这层已删；后面 seed 再出 type-call 漂移，优先查 `v3_classify_expr_type_call_surface(...)` 和 `v3_classify_type_call_surface(...)`，不要再补第三个 expr helper。

- `v3/bootstrap/stage1_bootstrap.cheng` 不是 helper 真源码，只是 bootstrap 合同清单。
  - 所以下一步如果要固定 seed 收口，正确动作是重编 `cheng.stage0` 并跑 `bootstrap-bridge`，不是去改 manifest 试图同步 helper。

- ordinary 这边之前缺的不是 `DefaultInitExpr/ConstructorExpr` 节点，而是缺一条显式 `typeCallKind` typed 真相。
  - 没这个字段时，seed 已经有统一 `type-call` 分类，但 ordinary report / lowering rule / CSG 序列化还只能从 `kind` 间接猜，真源还是分叉。
  - 现在 `typeCallKind` 已进入 typed fact、lowering rule 和 report；后面这条线再漂，先查这一个字段，不要再去补第四套计数。

- seed 之前不是没有类型调用识别，而是 `default-init` 和 `constructor-like` 分成了两套 helper。
  - 这会让 duplicate-field、infer type、native/wasm composite materialize、removed-default 诊断各自漂移。
  - 现在已经改成统一 `V3TypeCallSurface`；后面如果这条线再出洞，应该先查这个中心分类，不要再加第三套判断。

- seed 里这条旧 guard 现在已经只剩一个 source 级前置校验，不再是多出口兜底。
  - `stage3 system-link-exec` 在 backend driver 就绪时已经优先 handoff 到 `backend_driver/cheng`，用户可见主路径先走 parser 真源。
  - seed `system-link-exec` 仍会对 `default[T]`、标量 `T()`、`ref object T()` 维持同一条硬错误文本，但这时只剩 no-handoff 或 backend driver 未就绪时的内部 fallback。
  - `stage3_system_link_exec_handoff_smoke` 已把这条 route 固定成正式门禁；以后只要 `backend_driver.forward.log` 不再落对应输出路径，就会直接红。
  - 但真正触发位置已经从 lowering function collect 收到 lowering 入口的单一 source 校验相位；后面不该再回流第二份同类扫描。

- `DefaultInitExpr/ConstructorExpr` 这条主线已经收绿；当前不再是阻塞。
  - 复合 `T()` 现在走 `DefaultInitExpr`，复合 `T(args)` 走 `ConstructorExpr`，typed/csg/lowering/report 已经统一消费这两类节点。
  - fixed-array 只能按顶层尾部数字下标识别，不能把泛型 `Foo[Bar]`、`Ok[T]`、`Err[T]` 误判成 fixed-array 或 constructor。

- ordinary 函数体这轮的真实缺口不是语义没实现，而是默认 host gate 漏挂 witness。
  - `if_enum_composite_return_smoke`、`out_param_writeback_smoke`、`nested_out_param_writeback_smoke`、`out_param_direct_call_writeback_smoke`、`var_out_param_writeback_probe`、`consensus_event_varparam_roundtrip_smoke`、`wrapper_*_varparam_smoke` 在当前 backend driver 已经能直接通过。
  - 所以下一刀先补 gate 是对的；如果以后这批再红，优先看真实回归，不要先假设 `stmt_let` / `stmt_if` / 复合 `out-param` 还是“未支持”。

- `compiler_csg` / `lowering_plan` 的 typed report 第二真源问题已经收掉；当前这条不再是阻塞。

- 旧自举编译器会伪造 contract 漂移。
  - 源码已经改成 `ready` 时，旧 `cheng.stage3` 之前会被 bootstrap-bridge 当作 live compiler 优先复用，结果 `stage0_self_check` 拿旧内嵌 contract 去验新 `stage1_bootstrap`。
  - 现在 live compiler 已按 bootstrap 输入新鲜度选择；再出现这类情况时，应该先重编 `stage0`，不是去怀疑 primary-object 又回退了。

- `default[T]` 这条旧语法已经从当前用户面收掉，不再是主阻塞。
  - parser 已经把它当成硬错误，`parser_path_smoke` 会直接验这条前端拒绝。
  - 真实编译链路暂时仍需要 seed 在 lowering 收集入口做同口径前置拒绝；只靠 parser smoke 不够，因为 active compile path 还可能在更晚的 body semantics 阶段才炸。
  - 当前已经只保留这一个前置扫描口，晚期 lowering/codegen 不再重复兜底报同类错误。

- 这条线在用户可见入口上已经收完。
  - 如果后面还要继续动，只剩内部 bootstrap fallback 自己直接消费 parser 真源这一件事；它不再是当前主线阻塞。

- `verify-export-visibility-parallel` 之前有共享 label 踩产物问题。
  - 同时或重叠执行时，stage3/backend 两轮 export smoke 会复用固定输出目录，能把 provider object 踩成空文件。
  - 现在已经改成带 monotime 的独立 label；如果后面再红，就该按真实编译/链接日志排查，不用再怀疑是共享目录。

- `Bytes[] add/setLen` 仍是高风险支线，当前主线没有必要碰；不要把它和 typed report / primary-object composite 收口混在一轮里。

- `exec-route-matrix` helper 和 controller 契约之前不一致。
  - controller 明确允许 `--route-catalog | --tsx-ast` 二选一，但 helper 只有在同时拿到 `--tsx-ast` 时才肯继续，导致 `--route-catalog` 单独使用必定失败。
  - 这不是 smoke 特例，而是真实命令面缺口；已经在 helper 里修成“只有缺 `routeCatalog` 时才要求 `tsxAst`”。

- `r2c` 状态报告之前把薄壳 helper 和真实 blocker 混在一起。
  - `exec_route_matrix_helper` / `truth_route_helper` 仍然是 Node 薄壳，但它们不在 `remaining_non_cheng_blockers` 里；继续塞进 `active_node_helpers` 会让状态读起来前后矛盾。
  - 现在已经拆出 `thin_node_helpers`，剩余纯 Cheng 阻塞口径更清楚。

- `native_gui_bundle_helper` 不是纯打包 helper，不能直接宣称已薄壳化。
  - 它还负责 layout surface、style layout、native layout plan、render plan、compiled runtime launcher/session payload。
  - 当前正确切口是 Cheng controller 接管最终 bundle/summary/report 发布物，显式打 `cheng_controller_native_gui_bundle_finalizer_v1`；下一步再把重 payload 逐块搬进 Cheng。

- layout payload 这轮只能先做 Cheng 发布 sidecar，不能直接覆盖 Node 生成的真实 payload。
  - `native_gui_session` 和后续 run-native-gui 仍消费 Node helper 生成的 layout/native-layout 细节；如果现在把 `native_layout_plan_v1.json` 直接改成空 `items` 或机械摘要，会把 GUI 运行数据打坏。
  - 当前正确切口是 sidecar finalizer：Cheng 写 `style_layout_surface_controller_v1.json` / `native_layout_plan_controller_v1.json`，bundle/summary/report/status 记录 `cheng_controller_layout_payload_finalizer_v1`，但 `native_gui_bundle_helper` 继续留在 blocker 里。

- `native_layout_plan_controller` 不能靠 Cheng 字符串循环内联大数组推进。
  - 尝试把 `items[]` / `viewport_items[]` 全量或预览在 Cheng controller 内用循环拼接发布，会把当前 primary-object 生成推到不稳定边界。
  - 已改成 `cheng_controller_items_source_checked_v1`：Cheng controller 只硬校验原始 native layout plan 与两个数组字段存在，发布 source path、item/viewport 计数和 layout policy。
  - 这保持了 GUI 运行 payload 不被破坏，也避免重新引入大内存/空 object 风险；真正替换 `items[]` 的下一步应改成 Cheng typed layout item 生成器，不是继续拼 raw JSON 字符串。

- `cheng_candidate` 不能作为长串自测的稳定可执行路径假设。
  - 本轮 `artifacts/v3_backend_driver/cheng_candidate verify-r2c-react-v3-surface` 在第二个 selftest 前失败，日志为 `missing executable: /Users/lbcheng/cheng-lang/artifacts/v3_backend_driver/cheng_candidate`。
  - 这不是 layout finalizer 的语义失败；同一源码重建到标准 `artifacts/v3_backend_driver/cheng` 后，status 和 GUI controller smoke 都通过。后续要单独排查 candidate 自测过程中为什么会丢当前可执行。

- 生成 Cheng 的模板不能再写显式零值初始化。
  - `native_gui_runtime` 生成出的 `var out: str = ""`、`var count: int32 = 0` 会被当前编译器按 `redundant explicit default init` 硬失败。
  - truth compare 共享模板也有同类 `int32 = 0`，已经同步清掉，避免后续 controller smoke 再踩同类坑。

- `run-production-regression` 之前和 README 口径已经漂了。
  - 文档声称它会跑 `composite_zero_helper_gate_smoke`，但 gate 实现里漏掉了。
  - `r2c-react-v3` surface 已经有正式 verify 命令且实跑能过，但聚合回归没把它串进去，导致“手工过了”和“主线回归过了”不是一回事。
  - 更关键的是用户可见 `cheng.stage3 verify-r2c-react-v3-surface` 走的 seed C 真入口还是旧壳，只跑一个 `r2c_react_v3_surface_smoke`；如果只改 `gate_main`，用户主路径仍然是假的绿。
