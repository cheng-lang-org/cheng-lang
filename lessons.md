# Lessons

- 热核优化先找跨样本共享的字节搬运边界；已有 `RawmemCopy/RawmemSet` 时，不要新增 C bridge，不要调松阈值，也不要继续让固定长度 copy 走逐字节 `bytesGet/bytesSet`。

- 低层 bulk-copy helper 不能静默失败；nil、负 offset、越界必须断言暴露，否则 perf 修复会把真实内存错误藏起来。

- perf/memory 报告要把口径分开：编译理论下界看 `planner_total_ms`，运行时内存看 ORC retain/release 与 alloc/free/live，crypto 热核看 `crypto_hot_kernel_contract` 的 ns/op；Cheng 没有 tracing GC，别再写 GC perf。

- `lower generic function` 的排除不能只靠原始文本启发式；必须在规格化后先确认“这不是已知类型面”，否则会把 `elemSize[T]()` 误杀成 `seq/fixed-array T()`，或者反过来把真实 `T[]()/T[N]()` 放走。

- ordinary parser 里会被 seed no-handoff 直编的 helper，尽量写成单次 trim + 线性循环；不要堆重复 `Trim/FindFirst/IdentPrefix` 大表达式，否则很容易炸在 `prepare binding infer type failed`。

- parser normalized layer 可能覆盖整个 source closure；写针对单个 fixture 的断言时，用 `sourcePath + lineNumber` 过滤，不要用全局 count 判断某一行没有被误分类。

- 会检查 backend-driver handoff 的 smoke 必须同时传 `CHENG_V3_NO_BACKEND_DRIVER_HANDOFF=0` 和 `CHENG_V3_BACKEND_DRIVER_FORWARD_LOG`，写独立 artifact；不要再读全局 `artifacts/v3_forward/backend_driver.forward.log` 来证明路由。

- seed 里如果需要从表达式文本认 `default-init/constructor/ref-object/scalar-type-call`，只能保留一条 `expr -> type-call surface` 入口；不要再长出 `parse_default_expr_text_with_context(...)` 这种 default-init 专用薄壳。

- `v3/bootstrap/stage1_bootstrap.cheng` 只是 bootstrap 合同清单，不是 helper 源码；需要固定 seed 行为时，应重编 `cheng.stage0` / `bootstrap-bridge`，不要误改 manifest 期待同步逻辑。

- `DefaultInitExpr/ConstructorExpr` 进了 typed 层以后，还要补一条显式 `typeCallKind`；只靠 `kind` 不够，因为 lowering rule key、CSG 序列化和 report 统计都会重新分叉。

- seed 里的 `default-init` 和 `constructor-like` 不能分两套 helper；duplicate-field、infer/materialize、wasm analyze/emit、removed-default 诊断都要复用同一个 type-call 分类结果。

- bootstrap 阶段如果还必须保留 `default[T]` / 标量 `T()` / `ref object T()` 的文本前置拒绝，只能保留一处 source 级校验；不要再散回 lowering function collect 里逐函数兜底。

- 用户可见的 `artifacts/v3_bootstrap/cheng.stage3 system-link-exec` 只要 backend driver 已 fresh/ready，就应该先 handoff 到 `artifacts/v3_backend_driver/cheng` 做 parser 真源判定；seed 里的同类 source 校验只保留给 `CHENG_V3_NO_BACKEND_DRIVER_HANDOFF=1` 或 backend driver 未就绪的内部路径。

- 这类命令路由修复不能只靠口头约定；要像 `stage3_system_link_exec_handoff_smoke` 一样拿实际 forward log 和唯一输出路径做门禁，防止后面有人改分发表时悄悄回退。

- `cheng语言` home skill mirror 改了，仓内 `docs/cheng-skill/SKILL.md` 必须同句同步；否则 `cheng_skill_consistency_smoke` 会直接红。

- `ConstructorExpr` 只认 composite `T(args)`；`Ok[T]` / `Err[T]` 继续属于 result intrinsic，不能混进 constructor 统计。

- fixed-array 识别不能把任意 `Foo[Bar]` 都当成数组；只认顶层尾部数字下标这类真 fixed-array 后缀。

- 同一组 typed/call/lowering 报告字段只能有一个生成出口；现在出口放在 `typed_expr`，消费者只能追加 `v3TypedExprReportText(...)`。

- CSG 和 lowering plan 只负责自己的图、计划和缺失原因；不要在这两层重新展开 typed finite-domain 统计。

- full smoke 如果红在 primary-object 复合值语义，不能改测试绕过。`str`、`ByteBuffer`、`seq`、`result` 这类复合值必须走显式地址语义。

- 当前 primary-object 红点的稳定形状是：复合返回绑定、复合局部槽、`NewByteBuffer()` 局部物化、typed report entry 里的字符串规范化 helper。

- ordinary 热路径里不要保留“字段取值到局部再单次消费”的形状；能直接内联到唯一消费点就直接内联。

- 中间层薄壳只要没有新增语义就删；真源模块已经有 helper 时，不要在 CSG、lowering、gate、system-link 里再包一层 convenience API。

- `Bytes` 不能只按类型判断 owning/borrowed；`Bytes[] add/setLen` 暂时不要混入当前主线。

- 改完文档或代码后固定先跑 `git diff --check`，再跑对应 smoke；失败要记录首个真实缺口，不要把 unrelated 红点写成当前改动回归。

- “禁用显式默认值初始化”指的是禁用把类型零值再手写一遍（`= []` / `= false` / `= 0` / `= ""`），不是移除字段默认值语法本身；有真实业务语义的 `field: T = expr` 要保留。

- `T[]()` / `T[N]()` 不是合法默认值构造表面；序列和定长数组只能走带类型标注的省略初始化或字面量，不能塞进 `T()` 口径。

- `r2c_react_v3_*` 这类会串多次生成和原生编译的 GUI smoke 不能套默认 60 秒宿主时限；backend driver 必须给长时预算。

- seed 里的 `parse for header` 这类普通语法回退不是错误，不能往 stderr 打 `[cheng_v3_seed] ...` 调试行；否则所有 selftest 编译日志都会被误判成内部故障。

- 如果 `Codex app-server` 留下旧的 `cheng/cheng.stage3` 会话，先杀掉这些旧 top-level 任务再跑当前重 smoke；不清现场就会出现 `rc=137`、空日志和无意义的高内存并发。

- 如果 smoke 形状突然漂成“源码不支持但 fresh seed 又能过”，先怀疑 `artifacts/v3_backend_driver/cheng` 过期；先重建当前 backend driver，再判断是不是源码真实回归。

- 测试里不要留 `V3WriteTextFile(...)` 这类临时调试副作用；它会把 smoke 从真实语义断言拖偏到 seed 的局部推断缺口。

- bootstrap-bridge 选 live compiler 不能只看“谁存在”；必须先过 bootstrap 输入新鲜度。源码已经升级 contract 时，旧 `stage3` 会把 `stage0_self_check` 伪造成 primary-object 回归。

- `verify-export-visibility-parallel` 这类会落固定 run-host-smokes 目录的命令，label 不能共享；重复执行或残留并发会把 provider object 踩成空文件。

- Cheng 里带下标/切片的条件判断不要依赖 `&&` / `||` 短路；像 `i > 0 && line[i-1]` 这种写法会把 `-1` 真读出去，必须拆成显式分支。

- 如果 smoke 为了喂 JSON 给 `V3WriteTextFile(...)` 直接塞超长 `\"...\" + \"...\"` 实参，seed 现阶段可能把它炸成 `prepare call arg state failed`；先改成普通局部变量分步拼接，不要把测试写成 primary-object 的已知缺口。

- `r2c-react-v3` 的 controller 帮助文案如果允许 `--route-catalog` 单独成立，Node helper 就必须同样允许；这种契约不一致要直接修 helper，不能靠测试额外补 `--tsx-ast` 绕过去。

- `default[T]` 这类已移除语法不能只在 parser smoke 里前端拒绝就算完；只要真实编译链路还会绕过那层，seed 就必须在 lowering 收集入口同步前置报错，但不要把同类兜底散落回 lowering/codegen 晚期。

- ordinary body 如果现有真实 smoke 已经能稳定覆盖 `stmt_let` / `stmt_if` / 复合 `var/out` 写回，就先把这些 witness 挂进默认 gate；不要还没查清就重复造 probe 或重写 `body_kind` 判断。

- 聚合回归入口一旦在 README 里列成正式合同，gate 代码就不能再手写另一套漂移列表；像 `verify-r2c-react-v3-surface` 这种已有正式 verify 命令的集合，命令分发和 `run-production-regression` 应该共用同一个 helper。

- `cheng.stage3` 这种用户可见子命令先看 seed C 真入口，不要只改 ordinary `gate_main` 就以为主线收完；像 `verify-r2c-react-v3-surface` 这类命令，seed 壳如果还停在旧 smoke 集合，就会把正式回归伪造成假绿。
