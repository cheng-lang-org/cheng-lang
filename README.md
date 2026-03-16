# Cheng 语言介绍

> 本文是对 `docs/` 目录现有文档的介绍性提炼，面向“准备使用 Cheng 写代码”的读者。  
> 它不是新的规范；若与其它文档冲突，一律以 [docs/cheng-formal-spec.md](/Users/lbcheng/cheng-lang/docs/cheng-formal-spec.md) 为准。

## 1. Cheng 是什么

Cheng 是一门面向原生执行、静态编译和确定性运行场景设计的语言。它的几个核心取向是：

- 静态类型，语法接近现代系统语言，但尽量保持表达直接。
- 默认值语义与默认 move，配合 Ownership + ORC（引用计数）完成内存管理。
- 并发是显式的：跨线程共享必须经过 `Arc[T]` / `share_mt(...)` 这类受约束入口。
- 生产口径收敛到“零裸指针语言表面”，把 C ABI 互操作放到编译器的影子桥接层完成。
- 模块、包、导入、构建入口都要求规范化，避免“能跑但不可复现”的工程状态。

如果只用一句话概括，Cheng 想要的是：保留系统语言的控制力，同时尽量把内存、并发、FFI 和构建入口的危险边界收口成可验证规则。

## 2. Cheng 编译器的特色

如果只看语法，Cheng 会像一门强调 Ownership 的静态语言；但它真正有辨识度的地方，其实在编译器路线。

### 2.1 不是 “C as IR”，而是自研原生后端

Cheng 当前生产主链不是“前端翻译成 C，再交给 Clang/GCC 做主优化”。

它的主链路已经收敛为：

```text
Stage1 -> UIR -> Machine -> Obj/Exe
```

这意味着：

- 主优化发生在 Cheng 自己的 UIR / Machine 管线里。
- Runtime substrate 只承担平台胶水和必要运行时符号，不承担业务代码的主优化。
- 编译器能直接保留 Cheng 语言里的所有权、别名、借用等高层语义，而不是先退化成 C 指针语义再想办法恢复。

### 2.2 双轨编译：开发态追求速度，发布态追求稳态

Cheng 的编译器不是一条单一管线，而是明确区分 dev 和 release：

- `cheng`：dev 轨入口，强调快速构建、直接产出可执行、host runner 运行和增量体验。
- `release-compile`：release 轨入口，强调稳定发布，通过 system linker 做最终物理收敛。

这条路线的特点是：

- 开发时优先减少传统“产出很多中间文件再慢慢链接”的成本。
- 发布时继续借助成熟系统工具链完成最终收敛。
- 结果不是“为了纯自研而纯自研”，而是把自研后端和系统链接器放在各自最适合的位置。

### 2.3 编译器内部强调 DOD / SoA，而不是对象指针森林

Cheng 编译器当前的一个重要收口方向，是把 AST、UIR、基本块等内部表示组织成：

- `Arena + SoA`
- 连续存储
- 跨节点引用用 `int32` 索引，而不是长期持有裸指针

这背后的目标很明确：

- 提高编译时的缓存局部性
- 降低大规模 IR 遍历和优化的内存抖动
- 让编译器内部表示也符合“零裸指针生产闭环”的设计方向

所以 Cheng 的“零裸指针”不只是用户代码层面的限制，也是在逼编译器自身向更稳定、更可验证的内部结构演进。

### 2.4 优化建立在语言语义上，而不只是通用 IR 小技巧

Cheng 的优势不是“也有一个优化器”，而是它可以把语言语义直接变成优化前提。

最核心的一条链路是：

- 前端做 Ownership / Borrow / No-Alias 证明
- UIR 保留这些事实
- 后端在 `SROA`、`CSE`、`LICM`、向量化等阶段消费这些信息

这和传统依赖 C/C++ 指针别名分析的路线不同。  
Cheng 编译器的目标是：先在高层语义上证明，再把证明结果下发给后端，而不是等到了低层 IR 才被迫保守推断。

### 2.5 热补丁和直接执行是开发体验的一部分

Cheng 当前 dev 轨不是单纯的“编译然后执行”，而是有明确的运行态优化目标：

- 支持 direct-exe 风格的快速产物生成
- 支持 `Trampoline + Append-Only` 的热补丁策略
- 布局变化过大时，不做危险补丁，而是受控重启

这说明 Cheng 编译器关注的不只是“最后能不能生成二进制”，还包括：

- 增量改代码时能不能快速反馈
- 运行态切换时能不能尽量少打断
- 出现布局变化时能不能优先保证安全而不是赌热更成功

### 2.6 面向确定性与可验证闭环

Cheng 编译器还有一个很少见、但非常关键的特征：它强调“机器可校验的生产闭环”。

当前更接近正式规范和 tooling 文档的几项收口是：

- canonical tooling / canonical driver：`artifacts/tooling_cmd/cheng_tooling`、`artifacts/backend_driver/cheng`
- 生产闭环入口：`verify_backend_closedloop`、`backend_prod_closure`、`backend-prod-publish`
- native-contract 前端收口：`BACKEND_NATIVE_CONTRACT=1`
- 用户表面与 Raw Pointer contract 收口：`ABI=v2_noptr`、`ZRPC`

不管这些术语背后的实现细节有多深，它们传达出的编译器取向很明确：

- Cheng 不是把编译器只当成“把源码变成机器码”的黑盒。
- 它把分发、执行、确定性和门禁验证也纳入了编译器设计的一部分。
- 这使 Cheng 更适合原生执行、受控运行和去中心化/计量类场景，而不是只追求传统意义上的“像 C 一样编出来就行”。

## 3. 一个最小 Cheng 程序

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

从这个例子可以直接看出几条 Cheng 的稳定语法：

- 顶层函数使用 `fn`。
- 绑定语句使用 `let`、`var`、`const`。
- 分支语法使用 `if / elif / else`。
- 返回值类型写在参数列表后面：`fn add(...): int32 = ...`。
- 块结构基于缩进。

## 4. 语法风格总览

### 4.1 绑定与声明

- `let`：用于不可变绑定。
- `var`：用于可变绑定，或表示“可变借用”。
- `const`：编译期常量。
- 顶层例程只有 `fn` 和 `iterator`。
- 其它顶层能力还包括 `type`、`trait`、`concept`、`template`、`macro`。

### 4.2 调用与类型转换

单参数调用支持两种写法：

```cheng
f x
f(x)
```

但下面这种写法非法：

```cheng
f (x)
```

类型转换只允许紧邻括号形式：

```cheng
int32(123)
```

### 4.3 控制流

Cheng 支持常见的控制流结构：

```cheng
if cond:
    work()
elif other:
    fallback()
else:
    stop()
```

```cheng
for x in xs:
    consume(x)

while i < n:
    i = i + 1
```

除此之外还有：

- `when`：编译期条件分支风格的表达式/语句结构。
- `case`：分支匹配。
- `defer`：作用域退出前执行。
- `block`：具名或匿名块。
- `yield`：配合 `iterator` 使用。

### 4.4 运算符

稳定写法如下：

- 逻辑运算：`&&`、`||`、`!`
- 按位异或：`^`
- 除法与取模：`/`、`%`
- 字符串拼接：`+`
- 条件表达式：`cond ? a : b`
- Result/Option 风格解包：`expr?`

要特别注意，三目 `?:` 和 postfix `?` 是两套不同语义：

```cheng
let x = okValue?
let y = cond ? a : b
```

## 5. 类型系统与数据模型

### 5.1 基础类型

常见内建类型包括：

- 布尔：`bool`
- 整数：`int8/int16/int32/int64/int`、`uint8/uint16/uint32/uint64/uint`
- 浮点：`float32/float64`
- 字符：`char`
- 字符串：`str`、`cstring`
- 关键字空值：`nil`

这里有两个很重要的稳定约束：

- 内建字符串类型只有 `str` 和 `cstring`。
- `string` 不是合法内建类型名，当前编译器会明确报错。

`str` 采用非空字符串语义：省略初始化时默认是 `""`，而不是 `nil`。  
因此 `str = nil`、`x == nil` 这类写法在编译期就会报错。  
如果需要判断空字符串，应显式写 `len(s) == 0`。

### 5.2 容器语法

Cheng 的容器语法已经统一为后缀式：

- `T[]`：动态序列
- `T[N]`：仍然是动态序列，只是“带初始 reserve 提示”的类型标注写法

这意味着 `T[N]` 不是传统意义上的定长数组类型，它与 `T[]` 类型等价，不改变 ABI，只影响省略初始化时的初始容量行为。

序列字面量使用：

```cheng
[]
[1, 2, 3]
```

空字面量 `[]` 必须有类型上下文，例如：

```cheng
let xs: int32[] = []
```

### 5.3 复合类型

Cheng 支持：

- `tuple[...]`
- `enum`
- `set[...]`
- `ref`
- 自定义 `type`
- 对象类型（object 风格字段集合）

不过，对准备写新代码的读者，需要把“语言完整语法”与“当前默认生产口径”区分开：在 `ABI=v2_noptr` 且 `STAGE1_NO_POINTERS_NON_C_ABI=1` 下，`ref`、`T*`、`void*`、`ptr[T]` 不属于推荐的用户源码表面；C ABI 互操作应优先走 `@ffi_map`、`@ffi_out_ptrs`、`@ffi_handle` 这类影子桥接。

泛型既可以显式写，也支持“隐式类型参数”语法糖。只要签名里出现自由的单字母大写类型名，编译器会自动引入类型参数：

```cheng
fn first(xs: T[]): T =
    return xs[0]
```

它等价于：

```cheng
fn first[T](xs: T[]): T =
    return xs[0]
```

### 5.4 默认初始化

如果绑定上写了类型，但省略初始化表达式，那么 Cheng 会使用该类型的隐式默认值：

```cheng
let ok: bool
let n: int32
let s: str
let xs: int32[]
```

默认值规则可以记成：

- `bool` -> `false`
- 整数/枚举 -> `0`
- 浮点 -> `0.0`
- `char` -> `'\0'`
- `str/cstring` -> `""`
- `T*` / `ref T` / `var T` / `void*` -> `nil`
- 复合类型 -> zero-init

`default[T]` 仍然可用于表达式位置，但不建议拿它替代“带类型标注且省略初始化”的写法。

## 6. 函数、模式与常见表达能力

### 6.1 函数

函数定义的基本形式是：

```cheng
fn name(arg1: T1, arg2: T2): R =
    return expr
```

还支持：

- `async fn`
- `iterator`
- 匿名 `fn`
- `where` 约束
- 注解 `@annot(...)`

### 6.2 模式与 `case`

模式可以出现在 `for`、`case`、绑定等位置，支持：

- 标识符模式
- `_` 通配
- 字面量模式
- tuple/list/set 模式
- 范围模式
- object 模式

例如：

```cheng
case code:
    of 0:
        return "ok"
    of 1:
        return "retry"
    else:
        return "error"
```

### 6.3 列表生成式

Cheng 支持 Python 风格的列表生成式：

```cheng
[expr for pat in iter]
[expr for pat in iter if cond]
```

例如：

```cheng
fn doubleNotThree(xs: int32[]): int32[] =
    return [x * 2 for x in xs if x != 3]
```

当前稳定限制有两条：

- 结果类型必须能在出现位置被推导出来。
- 目前它应出现在“可落地”的位置：绑定初始化、简单赋值、`return`、全局初始化。

如果要把生成式放进更复杂的表达式里，先绑定到临时变量再使用。

## 7. 所有权、ORC 与借用

这是 Cheng 和很多脚本语言、GC 语言最不同的地方。

### 7.1 固定内存模型

Cheng 当前正式规范固定使用 ORC（引用计数）：

- `MM=orc`
- 严格模式默认开启
- 运行时通过 `memRetain` / `memRelease` 维护引用计数
- 引用计数归零时立即释放

这不是 tracing GC；循环引用需要开发者显式打断。

### 7.2 默认 move

Cheng 的默认语义不是“隐式拷贝”，而是“默认 move”：

- 赋值默认移动所有权
- 参数传递默认移动所有权
- 返回默认移动所有权

如果一个值在 move 之后还需要继续共享使用，必须显式调用：

- `share(x)`：同线程共享
- `share_mt(x)`：跨线程共享

这使得所有权流向在源码里是清楚的，而不是依赖隐藏拷贝。

### 7.3 `var` 的语义

`var` 在 Cheng 中有两层用途：

- 可变绑定
- 可变借用

这里最容易误解的一点是：`var` 不是指针语法。  
它表示“在当前作用域内，对某个值进行可变借用”，并且借用值不能逃逸：

- 不能 `return`
- 不能写入全局
- 不能 `share(...)`

调用 `var` 形参时，编译器会在调用侧做自动取址式 lowering，但这属于编译器实现，不意味着语言鼓励直接暴露裸指针。

### 7.4 Borrowed、Owned、Unmanaged

编译器在 ownership 分析时会区分：

- `Owned`：拥有值
- `Borrowed`：借用视图
- `Unmanaged`：标量等非托管值

对 Cheng 开发者来说，最重要的实践规则是：

- 默认优先使用 `let`
- 只有需要可变或借用语义时才使用 `var`
- 不要把 borrowed 值返回、持久化或跨线程传递
- 写入长生命周期容器前，确保值是 owned 或显式 shared

## 8. 并发模型

Cheng 的并发模型建立在所有权规则之上，而不是绕开它。

### 8.1 跨线程共享必须显式

- 同线程共享：`share(x)`
- 跨线程共享：`share_mt(x)` 或 `Arc[T]`
- 可变共享：`Mutex[T]`、`RwLock[T]`
- 原子值：`Atomic[T]`

### 8.2 `Send` / `Sync`

线程边界 API 会进行 `Send/Sync` 检查：

- Borrowed 值与 `var` 借用默认不是 `Send`
- 原始指针和 FFI 句柄默认不是 `Send/Sync`
- `Arc[T]` 只有在 `T` 本身满足条件时才是 `Send + Sync`

所以 Cheng 的并发不是“想传就传”，而是“先证明能安全传”。

## 9. 模块、导入与包

### 9.1 模块与可见性

Cheng 模块可以写 `module` 头，也可以省略。  
可见性采用“首字母大写导出”规则：

- 首字母大写：导出
- 首字母小写或 `_`：模块私有

### 9.2 导入规则

导入只允许规范化模块路径：

```cheng
import std/os
import std/json as stdjson
import libp2p/swarm
import libp2p/[crypto,transport]
```

稳定约束如下：

- 推荐写法：`<pkg>/<path>`
- 标准库写法：`std/<path>`
- 禁止字符串路径导入
- 禁止相对路径导入
- 禁止绝对路径导入
- 禁止 `from import`
- 禁止 `import A, B`
- 分组合并导入不支持 `as`

模块导入图不允许出现环。

### 9.3 包清单

包根目录固定使用 `cheng-package.toml`，最小形式如下：

```toml
package_id = "pkg://cheng/app-demo"

[[dependencies]]
package_id = "pkg://cheng/fs"
channel = "stable"
```

其中：

- `package_id` 是包标识
- 依赖至少应声明 `package_id` 和 `channel`
- 锁文件默认是 `cheng.lock.toml`

包解析与导入的设计目标是：导入路径语义稳定，实际包内容由 manifest + lock 决定，而不是在编译期随意漂移。

## 10. FFI 与“零裸指针”设计

在当前生产口径里，这里已经不是“尽量不暴露裸指针”的问题，而是用户可见源码层的裸指针语义已经被硬收口。

更准确地说，Cheng 现在有两层同时生效的约束：

- 用户源码层：`ABI=v2_noptr` + `STAGE1_NO_POINTERS_NON_C_ABI=1` 下，裸指针语法是 hard-fail。
- 编译器内部层：ZRPC 要求 AST/UIR/基本块走 `Arena + SoA + int32` 索引，不允许把对象裸指针作为优化和门禁接口中的长期关联键。

但这不等于“实现层完全没有物理地址”。
运行时 substrate、ABI lowering 和影子桥接里仍可以存在底层物理地址细节，但这些细节不再作为用户可见语义暴露出来。

### 10.1 生产口径

在生产口径 `ABI=v2_noptr` 且 `STAGE1_NO_POINTERS_NON_C_ABI=1` 下，用户源码层不暴露这些能力，C ABI 通过影子桥接表达：

- 指针类型：`T*`、`void*`、`ref T`、`ptr[T]`
- 指针操作：`&`、`*`、`->`
- 原始内存 API：`dataPtr/getPointer`、`ptr_add/load_ptr/store_ptr`
- 低级内存函数：`copyMem/setMem/zeroMem`、`alloc/dealloc`

这条规则的目标不是削弱能力，而是把“必须脏”的 C ABI 物理细节收敛到编译器和运行时里。

### 10.2 影子桥接

当前正式推荐的 FFI 桥接方式是：

- `@ffi_map`：把 `T[]` 这类安全参数桥接成 C ABI 的 `(ptr, len)`
- `@ffi_out_ptrs`：把 out-ptr 风格函数桥接成 tuple 返回
- `@ffi_handle`：把 `void*` 风格句柄桥接成 `u32/u64` handle
- `@importc + var T`：对结构体借用做桥接

因此，ZRPC 和 SoA 解决的不是“物理世界里以后再也没有指针”，而是：

- 用户代码不再以裸指针为编程模型
- 编译器内部不再把裸指针当成长期结构关联键
- ABI 必需的指针细节被压缩到受控的影子桥接层

## 11. 工具链入口

当前仓库已经把编译入口统一到 `cheng_tooling`：

```sh
TOOLING=artifacts/tooling_cmd/cheng_tooling

$TOOLING cheng --in:docs/cheng-skill/assets/hello-cheng/main.cheng --out:artifacts/chengc/hello.dev
$TOOLING release-compile --in:docs/cheng-skill/assets/hello-cheng/main.cheng --out:artifacts/chengc/hello.rel
```

应记住两点：

- `cheng` 是 dev 轨入口
- `release-compile` 是 release 轨入口

再补两条和“编译器特色”直接相关的理解：

- dev 轨更靠近 Cheng 自研后端与 direct-exe / host runner 的开发体验目标。
- release 轨更靠近稳定发布口径，由系统链接器做最终收敛。

也就是说，当前推荐实践不是混用旧命令，而是从一开始就按 canonical 入口工作。

## 12. 新代码应遵守的稳定写法

如果你刚开始写 Cheng，优先记住下面这些规则：

- 顶层例程使用 `fn` / `iterator`
- 分支使用 `if / elif / else`
- 字符串类型使用 `str` / `cstring`
- 容器类型使用 `T[]` / `T[N]`
- 序列字面量使用 `[]` / `[a, b, c]`
- 显式类型转换使用 `Type(expr)`
- 字符串拼接使用 `+`
- 除法与取模使用 `/` 和 `%`
- 导入使用 `std/...` 或 `<pkg>/<path>` 这类规范路径
- 绑定优先使用 `let`，需要可变或借用语义时使用 `var`

## 13. 继续深入时该看什么

这份介绍文档对应的原始资料可以按下面顺序继续阅读：

- 语言权威规范：[docs/cheng-formal-spec.md](/Users/lbcheng/cheng-lang/docs/cheng-formal-spec.md)
- 编译主链与 UIR：[docs/UIR.md](/Users/lbcheng/cheng-lang/docs/UIR.md)
- 生产闭环与 tooling gate：[src/tooling/README.md](/Users/lbcheng/cheng-lang/src/tooling/README.md)
- 列表生成式：[docs/list-comprehension.md](/Users/lbcheng/cheng-lang/docs/list-comprehension.md)
- 零裸指针与 FFI 设计：[docs/raw-pointer-safety.md](/Users/lbcheng/cheng-lang/docs/raw-pointer-safety.md)
- 包清单示例：[docs/cheng-package-manifest.toml](/Users/lbcheng/cheng-lang/docs/cheng-package-manifest.toml)
- 当前编译入口：[docs/backend-driver-cli.md](/Users/lbcheng/cheng-lang/docs/backend-driver-cli.md)

## 14. 总结

Cheng 的语言与编译器路线可以概括成六件事：

1. 它不是“前端套 C 编译器”的路线，而是 `Stage1 -> UIR -> Machine -> Obj/Exe` 的自研原生后端。
2. 语法是现代静态语言风格，当前稳定写法已经统一。
3. 内存管理不是 GC，而是 Ownership + ORC，默认 move，显式 share。
4. 并发建立在 `Send/Sync` 和 `Arc/Mutex` 之上，而不是绕开所有权。
5. 模块、包和构建入口强调规范化与可复现，并采用 dev/release 双轨编译策略。
6. FFI 方向不是暴露更多指针，而是通过影子桥接和零裸指针收口保留安全语言表面。

如果你把这六件事掌握住，再配合正式规范和 UIR 文档查细节，就已经不只是理解 Cheng 的语法，而是能理解它为什么要这样设计编译器了。
