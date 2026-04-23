# Cheng

> 一门面向原生执行、静态编译和确定性运行的系统语言。

Cheng 想解决的，不只是“怎么写出能跑的代码”，而是“怎么把内存、并发、FFI、构建和发布这些高风险边界收口成可验证规则”。

如果用一句话概括 Cheng 的路线，就是：

**保留系统语言的控制力，但尽量不给用户暴露不必要的危险表面。**

## Cheng 为什么不一样

### 1. 不是“前端转 C”，而是自研原生后端

Cheng 的主链路不是把源码先退化成 C，再交给别的编译器兜底。

它当前的生产主链路是：

```text
Stage1 -> UIR -> Machine -> Obj/Exe
```

这意味着：

- Cheng 的优化发生在自己的中间表示和机器后端里。
- 编译器可以直接保留 Ownership、Borrow、No-Alias 这类语言语义。
- 后端不是在 C 指针语义的废墟上“猜测优化”，而是消费前端已经证明过的事实。

### 1.1 UIR是 Cheng 能力落地的核心

UIR 是 Cheng 后端唯一规范 IR 家族，不是一个“中间过渡格式”。

它当前的几个关键能力是：

- 单一 IR 双相模型：`High-UIR` 保留 CFG、类型/布局、borrow/noalias 等高层事实；`Low-UIR` 在同一套 IR 容器上继续 lowering 到 SSA、机器选择和产物生成。
- proof-backed 优化：所有权、借用、别名证明先在高层完成，再交给 `noalias`、`SSU`、`egraph`、`SROA/CSE/LICM`、向量化等 pass 消费。
- SoA / index 存储：UIR 主表示按 `Arena + SoA + int32 id` 组织，避免把对象裸指针当成长期关联键。
- FFI lowering 能力：`@ffi_map`、`@ffi_out_ptrs`、`@ffi_handle`、`@importc + var T` 这类桥接都在 UIR call-site ABI lowering 层完成，而不是靠 C 文本回退。
- 确定性并行：函数任务允许并行执行，但结果按稳定顺序 merge，最终产物不依赖 worker 数。
- 可观测和可验收：`UIR_PROFILE`、`generics_report`、`soa_report`、`noalias_report` 以及 `verify_backend_opt2_impl_surface`、`verify_backend_uir_soa_surface` 这类 gate，都把 UIR 的能力边界直接暴露出来。

这很重要，因为 Cheng 的很多特色并不是“前端说自己支持”，而是通过 UIR 真正进入优化、ABI lowering 和产物生成链路。

### 2. 内存管理不是 GC，而是 Ownership + ORC

Cheng 不是 tracing GC 语言。

它的内存模型强调：

- 默认值语义
- 默认 move
- ORC（引用计数）
- 显式共享

这带来几个很重要的结果：

- 值的所有权流向在源码里是可见的。
- 不依赖隐藏拷贝来“碰运气”维持程序正确性。
- 编译器可以把所有权信息直接下发给后端做优化。
- 运行时性能口径看的是 ORC retain/release 与 alloc/free/live，不是 tracing GC 停顿。

当前公开的运行时计数器已经包括：

- `memRetainCount`
- `memReleaseCount`
- `memAllocCount`
- `memFreeCount`
- `memLiveCount`
- `memDiagReset()`

默认公开表面是内存安全优先，不是“全域自动安全”：

- 用户源码默认走 no-pointer + borrow/Send/Sync 检查
- FFI、句柄边界和环引用仍需显式管理
- 所以不能把 Cheng 说成“已经没有任何 unsafe boundary”

### 3. 并发建立在所有权规则上

Cheng 不是“先把共享做出来，再去补线程安全规则”。

它的并发模型从一开始就要求：

- 同线程共享用 `share(x)`
- 跨线程共享用 `share_mt(x)` 或 `Arc[T]`
- 可变共享用 `Mutex[T]` / `RwLock[T]`
- 线程边界执行 `Send/Sync` 检查

也就是说，Cheng 的并发不是“想传就传”，而是“先证明能安全传”。

### 4. 用户表面默认收口到“零裸指针”

Cheng 的一个非常鲜明的方向是：**当前默认公开编译口径下，用户源码表面不把裸指针当成主要编程模型。**

也就是说，`cheng`、`release-compile` 与默认 `chengc` 都按 no-pointer 生产策略检查用户源码；旧的
旧兼容名 `ABI=v2_noptr`、`STAGE1_NO_POINTERS_NON_C_ABI=*` 只保留给内部实现/兼容 gate，不再是新的用户编译开关。

下面这些能力默认不属于推荐用户表面：

- `T*`
- `void*`
- `ref T`
- `ptr[T]`
- `&` / `*` / `->`
- `ptr_add/load_ptr/store_ptr`
- `copyMem/setMem/zeroMem`

这并不意味着 Cheng 不能做 C ABI 互操作。相反，它的做法是把危险细节压缩到受控桥接层：

- `@ffi_map`：把安全参数桥接成 `(ptr, len)`
- `@ffi_out_ptrs`：把 out-ptr 风格桥接成 tuple 返回
- `@ffi_handle`：把 `void*` 风格句柄桥接成 handle
- `@importc + var T`：做结构体借用桥接

Cheng 的目标不是“物理世界再也没有地址”，而是“不把裸指针暴露成用户的主编程模型”。

### 5. 编译器内部也按 DOD / SoA 收敛

Cheng 的“零裸指针”不只是在用户代码层面发力，也在倒逼编译器自己的实现方式。

当前一个很有辨识度的方向是：

- AST / UIR / 基本块按 `Arena + SoA` 组织
- 节点尽量连续存储
- 跨节点关系优先用 `int32` 索引，而不是长期裸指针

这样做的直接收益是：

- 更好的缓存局部性
- 更低的编译期内存抖动
- 更稳定、更可校验的编译器内部结构

### 6. Dev 和 Release 是两条明确不同的轨道

Cheng 不把“开发时的快速反馈”和“发布时的稳定收敛”混成一条管线。

当前入口明确区分为：

- `cheng`：dev 轨，强调快速构建、直接执行、host runner、增量体验
- `release-compile`：release 轨，强调稳定产物，通过 system linker 做最终收敛

这条路线的核心不是“自研一切”，而是：

- 开发态把速度和反馈做到前面
- 发布态把稳定性和可复现做到前面

### 7. Cheng 关心“可验证闭环”

Cheng 不把编译器只当成“源码转机器码”的黑盒。

它还把下面这些事情纳入主设计目标：

- canonical tooling / canonical driver
- verify gate
- backend production closure
- deterministic build and publish path

这使它特别适合：

- 原生可执行场景
- 受控运行时
- 对构建可复现和行为边界要求很高的系统
- 去中心化 / 计量 / 合约式执行场景

## 一个最小 Cheng 程序

```cheng
module hello

fn add(a: int32, b: int32): int32 =
    return a + b

fn main(): int32 =
    let total: int32 = add(1, 2)
    if total == 3:
        return 0
    return 1

main()
```

从这个例子里可以直接看出 Cheng 的基础风格：

- 顶层函数用 `fn`
- 绑定用 `let` / `var` / `const`
- 分支用 `if / elif / else`
- 返回类型写在参数列表后面
- 块结构基于缩进

## Cheng 的语言表面

### 语法风格

Cheng 的整体语法接近现代静态语言，但尽量保持直接：

- 调用支持 `f x` 和 `f(x)`
- 类型转换固定写成 `Type(expr)`
- 逻辑运算用 `&&` / `||` / `!`
- 除法和取模用 `/` / `%`
- 字符串拼接用 `+`
- 条件表达式支持 `cond ? a : b`
- Result/Option 风格解包支持 postfix `expr?`

### 类型与容器

常见内建类型包括：

- `bool`
- `int8/int16/int32/int64/int`
- `uint8/uint16/uint32/uint64/uint`
- `float32/float64`
- `char`
- `str`
- `cstring`
- `nil`

容器语法统一为后缀式：

- `T[]`：动态序列
- `T[N]`：带初始 reserve 提示的序列类型写法

序列字面量也统一为：

```cheng
[]
[1, 2, 3]
```

### 泛型和表达能力

Cheng 支持显式泛型，也支持隐式类型参数语法糖：

```cheng
fn first(xs: T[]): T =
    return xs[0]
```

它等价于：

```cheng
fn first[T](xs: T[]): T =
    return xs[0]
```

Cheng 还支持列表生成式：

```cheng
fn doubleNotThree(xs: int32[]): int32[] =
    return [x * 2 for x in xs if x != 3]
```

## 所有权、借用与并发

这是 Cheng 最核心的特色之一。

### 默认 move

Cheng 的默认语义不是隐式拷贝，而是默认 move：

- 赋值默认移动所有权
- 参数传递默认移动所有权
- 返回默认移动所有权

如果值需要继续共享，必须显式写出来：

- `share(x)`：同线程共享
- `share_mt(x)`：跨线程共享

### `var` 不是“指针语法”

在 Cheng 里，`var` 很重要，但它表达的是：

- 可变绑定
- 可变借用

它不是鼓励用户直接暴露裸指针的语法糖。

借用值不能随便逃逸：

- 不能 `return`
- 不能写入全局
- 不能跨线程传递
- 不能直接 `share(...)`

### 并发是显式、安全边界先行

Cheng 的并发能力建立在所有权证明之上：

- Borrowed 值和 `var` 借用默认不是 `Send`
- 原始指针和 FFI 句柄默认不是 `Send/Sync`
- `Arc[T]` 只有在 `T` 自己满足条件时才是 `Send + Sync`

这使 Cheng 的并发模型更接近“能证明才允许”，而不是“先开放，再靠约定自律”。

## FFI 的路线：桥接，而不是裸露

Cheng 不回避和 C ABI 的互操作，但它尽量不把 ABI 细节直接抬升成用户语义。

推荐路线是：

- 安全参数在桥接层降级
- out 参数收口成 tuple
- 句柄收口成整数 handle
- 借用桥接通过显式注解表达

这样做的直接好处是：

- 用户层保持更稳定的安全表面
- 编译器保留更多高层语义信息
- 后端优化不容易被原始指针语义破坏

## 编译器与工具链特色

Cheng 当前最有辨识度的编译器特性包括：

- `Stage1 -> UIR -> Machine -> Obj/Exe` 的自研原生后端
- UIR 的 `High-UIR -> Low-UIR` 双相模型
- 把 Ownership / Borrow / No-Alias 证明下发到后端
- `O2` 默认走 `noalias + SSU + opt2 + cleanup + egraph`
- `O3` 继续进入 SSA、后 SSA 优化和更强的清理收敛
- 编译器内部向 `Arena + SoA + int32 index` 收敛
- UIR call-site ABI lowering 负责 slice / out-ptr / handle / borrow bridge
- 自举契约把编译器并行固定为 `two_pass + function_scheduler + thread_local_arena`，用户程序默认仍是确定性单线程
- dev 轨支持 direct-exe 和更快的反馈闭环
- release 轨强调稳定发布和系统链接器收敛
- tooling 强调 canonical 入口和 verify gate

当前几个关键入口是：

- `artifacts/bootstrap/cheng.stage3`
- `artifacts/backend_driver/cheng`
- `artifacts/bootstrap/cheng.stage3 bootstrap-bridge / build-backend-driver`
- `artifacts/bootstrap/cheng.stage3 run-production-regression`
- `artifacts/bootstrap/cheng.stage3 profile-run / profile-report`
- `artifacts/backend_driver/cheng status / run-host-smokes`
- `artifacts/backend_driver/cheng run-host-smokes cheng_skill_consistency_smoke`
- `artifacts/backend_driver/cheng system-link-exec`

外层 shell wrapper 不再是 README 主入口。

## 性能目标与当前状态

Cheng 不把“先自举成功，性能以后再补”当成正确路线。

- 运行时性能目标：核心运行时、热路径程序和数值内核按同机 C `1:1` 对拍
- 编译性能目标：编译吞吐和关键构建主链不低于同机 C 编译器
- 当前实际情况：`stage0 -> stage1 -> stage2 -> stage3`、`program-selfhost` 和 `chain_node` 主链已经打通，但性能闭环还没完全收口；现在能写成硬目标，不能写成“已经达到”

也就是说，今天的 Cheng 已经不是“只能 bootstrap 的壳”，但也还不能在 README 里把运行时性能或编译性能写成既成事实。

## 快速上手

```sh
./artifacts/bootstrap/cheng.stage3 bootstrap-bridge
./artifacts/bootstrap/cheng.stage3 build-backend-driver
./artifacts/bootstrap/cheng.stage3 run-production-regression
./artifacts/backend_driver/cheng status
./artifacts/backend_driver/cheng run-host-smokes perf_memory_contract_smoke
./artifacts/backend_driver/cheng run-host-smokes cheng_skill_consistency_smoke
```

可以把它们理解成三类入口：

- `bootstrap-bridge`：刷新 `stage0 -> stage3` 自举链
- `build-backend-driver` / `run-production-regression` / `system-link-exec`：生成并验证当前编译器主线
  其中 `run-production-regression` 现在固定包含 `dev_hotpatch_100ms_scope_contract_smoke`、`explicit_default_init_positive_smoke`、`explicit_default_init_negative_smoke`、`explicit_default_init_gate_smoke`、`composite_zero_helper_gate_smoke` 和 `verify-r2c-react-surface`
- `run-host-smokes` / `status`：校验闭环和查看当前主线状态

其中 `system-link-exec` 的正式入口仍然优先认 `artifacts/backend_driver/cheng`。如果你直接跑 `artifacts/bootstrap/cheng.stage3 system-link-exec`，在 backend driver fresh/ready 时它现在也会先 handoff 到 backend driver，前端语义口径保持 parser 真源一致。

调试和性能入口也已经固定：

- `artifacts/bootstrap/cheng.stage3 debug-report`
- `artifacts/bootstrap/cheng.stage3 print-asm`
- `artifacts/bootstrap/cheng.stage3 crash-report --in:/abs/path/app.run.log --out:/tmp/app.crash-report.txt`
- `artifacts/bootstrap/cheng.stage3 profile-run --in:/abs/path/file.cheng --target:arm64-apple-darwin --out:/tmp/app`
- `artifacts/bootstrap/cheng.stage3 profile-report --in:/tmp/app.profile.raw.txt --out:/tmp/app.profile.txt`
- `artifacts/backend_driver/cheng run-host-smokes perf_memory_contract_smoke`
- `artifacts/backend_driver/cheng run-host-smokes cheng_skill_consistency_smoke`

排障顺序也已经固定：

- 先看 Cheng 内建调试面：`debug-report`、`print-asm`、`print-line-map`、`print-symbols`、`print-object`
- 再看真实产物：`*.compile.log`、`*.run.log`、`*.map`、`*.primary.o.s`
- 运行崩溃优先对 `*.run.log` 跑 `crash-report`
- 性能优先用 `profile-run/profile-report` 和 `perf_memory_contract_smoke`
- 只有问题已经落到宿主 C runtime、系统 linker、`libSystem` 或内建产物不足时，才补用 `lldb/gdb/sample`

`perf_memory_contract_smoke` 的报告默认落在 `artifacts/perf_memory_contract/<label>/perf_memory_contract.report.txt`。

- `perf_memory_contract_smoke` 默认优先测 `artifacts/backend_driver/cheng`；只有显式 `CHENG_SMOKE_COMPILER` 才覆盖。
- Darwin 正式内存比较值优先用 `peak memory footprint`；`maximum resident set size` 只保留原始观测，不作为稳定合同阈值。

这份报告现在同时给四类事实：

- `orc_perf_contract`：ORC runtime retain/release 和 alloc/free/live 闭环
- `*_compile_exec_phase_summary`：正式 `system-link-exec` 编译报告里的 phase 下界摘要
- `*_compile_gap_breakdown`：planner 之外的 object materialize、provider cache、native link、line-map 真耗时
- `crypto_hot_kernel_contract`：SHA-256、X25519 pubkey、P-256 pubkey、P-256 sign 的热核 ns/op

`100ms` 编译与二进制原地更新都只属于 dev host-only `self-link + direct-exe + host runner hotpatch` 的 dedicated witness 口径；release 主线仍然是 `system-link`。

当前严格可引用的编译理论下界稳定样本是 `object_native_link_plan_smoke`、`chain_node_smoke`、`content_stub_smoke`、`orc_perf_contract_smoke`、`crypto_hot_kernel_perf_smoke`。provider object 热路径有独立 cache 字段和 hit/miss 计数；口径只认满足 `planner_total_ms <= compile_elapsed_ms` 的正式 `system-link-exec` 报告样本。

## Cheng 适合什么

Cheng 特别适合那些既想要系统语言控制力，又不想把危险边界无限外露的场景：

- 原生可执行程序
- 需要稳定 ABI 和受控 FFI 的系统组件
- 对可复现构建和验证闭环要求高的工具链
- 希望把并发、所有权和运行边界收口的基础设施项目
- 去中心化、计量式执行、受控 runtime 这类对行为边界敏感的场景

## 总结

Cheng 的核心不是“再做一门看起来像系统语言的语言”，而是把下面几件事合在一起做：

- 现代静态语法
- Ownership + ORC
- 显式并发边界
- 零裸指针生产表面
- 自研原生后端
- 可验证、可收敛的工具链闭环

如果你在找的是一门只追求“语法像某门老语言”的语言，Cheng 可能不是那个方向。

但如果你在找的是一门把**语言语义、编译器实现、运行边界和发布闭环**一起当成设计对象的语言，Cheng 的路线会很有辨识度。
