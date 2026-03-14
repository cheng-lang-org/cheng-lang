# Cheng Full Plan 并行任务矩阵（Linkerless + DOD + 语义特化）

要在工业级编译器的传统赛道上（即追求横跨几十种 CPU 架构的通用性、以及极限的 O3 级微指令压榨）通过纯自研去**正面全面击败** LLVM 和 mold/lld，对任何独立团队来说几乎是不可能的。因为它们背后是科技巨头数万人年和数百亿美金的投入，并且 mold 已经把操作系统的多核 I/O 压榨到了物理极限。

**但是！如果你改变游戏规则，进行“降维打击”（Paradigm Shift）**，采用下一代编译器的架构范式，你完全可以在**【编译速度】、【增量开发体验】以及【利用特定语言语义的优化】**上，将 LLVM 和 mold 远远甩在身后。

目前在最前沿的系统级语言（如 **Zig, Jai, Roc, Cranelift**）中，已经验证了这套自研破局方案。以下是为你量身定制的架构蓝图，可以让你自研的 `cheng` 语言大放异彩：

命令前缀约定（文内命令可直接执行）：
- `TOOLING=artifacts/tooling_cmd/cheng_tooling`
- 示例中的 `$TOOLING <subcmd>` 等价于直接调用 canonical tooling binary。

---

### 一、 碾压 mold / lld：走向“无链接器”（Linkerless）架构

mold 为什么快？因为它极致优化了读取 `.o` 文件、匹配字符串符号、重定位并写回磁盘的过程。但**只要你还在走“生成 .o 文件再链接合并”的老路，你就永远受限于磁盘 I/O，不可能比它快**。

#### 1. 全局内存直出二进制 (Monolithic Direct-to-Executable)

既然 `cheng` 拥有自己完全掌控的前后端生态，为什么还要遵守 C 语言上世纪 70 年代的碎片化 `.o` 目标文件标准？

* **超越方案**：**废弃 `.o` 文件的概念，彻底消灭链接器**。编译器将整个项目（包含依赖包）一次性解析进内存，构建全局的 UIR 图。
* **做法**：符号决议直接在内存中通过指针或数组索引完成（毫无字符串 Hash 匹配开销）。代码生成时，直接在内存里排布 `.text` 和 `.data` 段，计算好虚拟内存偏移量（VA）写死进机器码。最后加上 ELF/PE 头，**通过一次 `mmap` 或 `write` 系统调用，直接从内存吐出最终的可执行文件**。
* **战果**：将“链接”时间直接降维归零（在毫秒级内完成）。天才程序员 Jonathan Blow 的 **Jai** 语言以及 **V 语言** 采用的正是这种架构，编译数十万行代码只需零点几秒，碾压 C++ + mold。

#### 2. 终极神技：原地二进制热补丁 (In-Place Binary Patching)

* **超越方案**：Dev 轨采用 `Trampoline + Append-Only + Host Runner`，避免直接覆写旧函数体。
* **做法**：调用点固定经 `thunk` 路由；热更时把新代码追加到代码池尾部，最后原子切换 `target slot`。布局哈希变化时不做危险补丁，直接受控冷重启。
* **战果**：增量提交与运行态切换保持毫秒级，并且把“函数变长导致覆盖相邻代码”的崩溃风险降到可控重启路径。

---

### 二、 碾压 LLVM 的编译速度：数据导向设计（DOD）

LLVM 编译速度极慢、极其吃内存的根本原因是：它的 AST 和 IR 采用了极度面向对象（OOP）的设计，一条指令就是一个 C++ 对象，内存中散落着海量的细粒度节点，用无数的指针（Use-Def 链）串联，导致 CPU 缓存命中率（Cache Miss）极低。

#### 1. 数组化的线性中间表示 (Struct of Arrays, SoA)

* **超越方案**：彻底抛弃基于树（Tree）或链表（Linked List）的对象指针结构，重构你的 `uir_core_types.cheng`。
* **做法**：使用 Arena 内存池。将所有的 AST 节点、UIR 指令、基本块全部平铺在**巨大的、连续的结构体数组中**。指令之间的引用绝对不用指针，而是用 **32 位整数索引（Index）** 指向数组位置。
* **战果**：这能让编译器在遍历和优化代码时，完美契合现代 CPU 的 L1/L2 Cache 预取机制。前端解析和 IR 生成速度可以达到 LLVM 的 **10 倍到 50 倍以上**。

---

### 三、 在特定性能上追平/反超 LLVM：高阶语义降维

LLVM 的优化是通用且保守的。它最难做好的优化是“别名分析”（Alias Analysis）。面对 C/C++ 的指针，LLVM 必须保守地假设“任何两个指针都可能指向同一块内存”，导致它**不敢**激进地进行指令重排、内存合并和自动向量化。

#### 1. 榨取所有权（Ownership）的红利

* **超越方案**：`cheng` 拥有类似 Rust 的 `ownership` 系统。通过借用检查器，你在前端就拥有了**绝对的无别名保证（No-Alias）**（明确知道哪些是独占的 `&mut`）。
* **做法**：将这种数学证明级别的保证直接下发给你的自研后端。有了这个底气，你的自研后端可以像脱缰的野马一样，进行 LLVM 绝不敢做的**极限内存提升（Memory-to-Register Promotion）和深度的 SIMD 循环向量化**。在安全代码下，你生成的汇编比 LLVM 更短、更快。

#### 2. E-Graphs（等价饱和优化）

* **超越方案**：LLVM 的优化是链式线性的，存在“相位排序问题”（先做 A 优化还是先做 B 优化会互相干扰，错失最优解）。
* **做法**：将你的优化器升级为 **E-Graphs**（参考 Rust 社区的 `egg` 库或 Cranelift 引擎）。在编译时，同时在内存中保留一条指令的所有数学等价形式（如 `a * 2` 和 `a << 1`），利用代价模型（Cost Model）一次性搜索出特定 CPU 架构下的全局最优指令序列。用极少的代码量实现“超级优化”。

---

### 给 Cheng 语言的务实落地建议

从当前代码库的落点看，真正需要收敛的不是“再发明一个通用 LLVM”，而是三条明确的工程主线：

1. Dev 轨：继续把 `writer + direct-exe + linkerless` 做成极速开发主链。
2. Release 轨：固定 `UIR -> .o -> system linker(mold|lld|default)`，把发布极限收敛交给成熟工具链。
3. 语义特化：把 Ownership/No-Alias、Metering、E-Graph 这类语言护城河落实到可验收的后端能力，而不是停留在概念层。

后文的并行矩阵、UIR 分层和跨端集成，都按这三条主线展开。


传统安全语言（如 Rust、Zig、Go）在面对 C ABI 时，最后都无奈地选择了向现实妥协：在语言中暴露出丑陋且危险的裸指针（`*mut T`, `void*`）和指针算术运算。这就像在一个无菌手术室里开了一扇通往臭水沟的暗门。

特别是对于 `cheng` 语言来说，你们的终极愿景包含**去中心化网络（Decentralized）和智能合约的沙盒计费**。一旦在语言层面暴露出裸指针和 `ptr + 1` 的运算，就会彻底摧毁你的“无别名（No-Alias）”假设，导致 E-Graphs 优化失效，智能合约的安全沙盒也会被轻易击穿。

如果结合我们之前推演的 **“C Backend 降维打击”** 和 **“DOD 数据导向”** 架构，你**完全可以在语言表面 100% 消灭裸指针语法，并且依然与 C ABI 完美丝滑互操作**。

这套方案的底层哲学是：**“指针仅仅是机器寻址的物理载体，绝不应该作为高层语义暴露给开发者。”**

以下是实现“绝对零指针 FFI”的 4 大核心设计（我称之为 **语义影子桥接 Semantic Shadow Bridge**）：

---

### 核心一：用“胖切片 (Slice)”彻底替代“指针+长度”

C 语言最常见的指针场景是传递数组或缓冲区：`void process_data(uint8_t* buf, size_t len);`。手动管理两者极易引发缓冲区溢出。

* **Cheng 语言侧（完全零指针）**：
在 `cheng` 中，开发者只能使用安全的切片借用，如 `&mut [u8]` 或内置的 `Buffer`。语言层完全没有指针加减法。
* **后端的“影子垫片 (Shim)”降级**：
得益于你采用了 **C Backend**，编译器在生成 C 代码时，会自动把安全的切片“物理拆包”，生成一个不可见的 C 包装器。

**【Cheng 源码】（极其安全）：**

```cheng
// 告诉编译器：C ABI 的参数 0 对应切片的 ptr，参数 1 对应 len
@ffi_map(ptr = arg0, len = arg1)
importc fn process_data(data: &mut [u8])

fn main() =
    var buf = [1, 2, 3]
    process_data(&mut buf) // 传安全切片，借用检查器保证生命周期安全


```

**【C Backend 自动生成的底层代码】（脏活全由编译器代劳）：**

```c
extern void process_data(uint8_t* ptr, size_t len); // 真实的 C 函数

// 编译器自动生成的垫片函数
static inline void cheng_ffi_process_data(ChengSlice_u8 data) {
    process_data(data.ptr, data.len); // 在物理层完美拆包对接 C ABI
}

```

### 核心二：用“多返回值 (Tuple)”彻底消灭“出参指针 (Out-Ptr)”

C 语言没有多返回值，遇到需要返回多个状态时，必须传入指针：`int get_size(int input, int* out_w, int* out_h);`。

* **Cheng 语言侧（优雅的元组）**：
利用现代语言的元组（Tuple）。语言层完全禁止声明类似 `*int` 这样的未初始化输出类型。
* **后端的“影子垫片”降级**：
通过编译器注解，让 C Backend 自动在栈帧上分配局部变量，并**隐式地取地址（`&`）** 传递给 C 函数，最后打包成元组返回。

**【Cheng 源码】（优雅的多返回值）：**

```cheng
// 注解告诉编译器：第2和第3个物理参数是出参指针，不用暴露在函数签名里
@ffi_out_ptrs(arg1, arg2)
importc fn get_size(input: int32) -> (int32, int32, int32)

fn main() {
    let (status, w, h) = get_size(1080) // 纯粹的值语义，没有指针满天飞
}

```

**【C Backend 自动生成的底层代码】：**

```c
int get_size(int input, int* w, int* h);

cheng_tuple_i32_3 cheng_ffi_get_size(int32_t input) {
    int32_t out_w, out_h;
    // 编译器偷偷帮你写了 '&' 满足 C ABI 的变态要求
    int32_t status = get_size(input, &out_w, &out_h);
    return (cheng_tuple_i32_3){status, out_w, out_h};
}

```

### 核心三：用“DOD 强类型句柄 (Handle)”替代 `void*` 和不透明指针

C 库极其喜欢返回你不该触碰的黑盒指针，比如 `sqlite3*` 或 `GLFWwindow*`。一旦暴露出这种指针，开发者如果错误地执行强转或释放，就会引发段错误。

* **Cheng 语言侧（DOD 的终极胜利）**：
在 `cheng` 中，所有的 C 句柄统统定义为 **纯数字 ID（u32 或 u64）**。
* **后端的“影子垫片”降级（绝对安全的沙盒映射法）**：
这正是 DOD 架构的灵魂！在 Cheng 的 Runtime 中，维护一个由 Arena 分配的 `void*` 全局数组。C 返回的指针存入数组，把数组的 `Index (u32)` 返回给 Cheng。Cheng 拿着 `Index` 去操作时，Runtime 进行 `O(1)` 的查表还原出 `void*` 给 C。

**【Cheng 源码】（只暴露数字 ID）：**

```cheng
// 语言层只知道它是个纯数字句柄（Token ID），绝对无法解引用
type SqliteDb = distinct u32

importc fn sqlite3_close(db: SqliteDb)

```

**逆天优势**：这直接消灭了 C FFI 中最可怕的 **Use-After-Free（释放后重用）** 漏洞。如果 C 结构体被释放了，运行时把数组对应的槽位标记为“过期”。如果 Cheng 开发者拿着过期的 `Handle ID` 去调 C 函数，**程序只会安全地 Panic，而绝对不会引发恶性的 C 级别内存崩溃**。黑客根本无从下手！

### 核心四：结构体指针 —— 全面拥抱“借用 (Borrow)”

当 C 需要传入一个复杂结构体的指针以避免值拷贝时（如 `void update_config(Config* cfg);`）。

* **Cheng 语言侧**：直接使用所有权机制中的借用引用 `&mut Config` 或 `&Config`。
* **后端处理**：前端利用 `&` 完成了极其严苛的生命周期和别名验证。一旦验证通过，下发到 C Backend 时，直接翻译为 C 的普通指针 `Config*`。**因为 Cheng 语法禁止对 `&Config` 进行指针算术运算，所以只要传进去了，它就只代表这个对象的固定内存，永远无法越界到其他内存。**

---

### 总结：为什么这套方案能让 Cheng 语言直接“封神”？

1. **让优化器（E-Graphs）和所有权系统发挥 100% 威力**
由于你从表面语法中抹除了裸指针，UIR 里流转的全部是**绝对没有别名（No-Alias）**的切片、值和整数 Handle。此时编译器拥有了完美的数学确定性，可以毫无顾忌地做极限内存提升和自动向量化。
2. **与 Web/Decentralized (WASM) 物理模型 100% 同构**
在 WebAssembly 和智能合约的沙盒里，**本来就不存在物理内存指针**。Wasm 的沙盒模型里只有一条线性内存（大数组），所谓的 C 指针在 WASM 里本来就是个 `i32` 的偏移量索引。你这套 Handle + Slice 的抽象，天生就是为去中心化虚拟机量身定制的！
3. **把 ABI 脏活留在后端，不把裸指针还给用户**
无论最终采用概念上的 C 垫片，还是生产上的 UIR call-site lowering / thunk synthesis，危险的 ABI 细节都应留在编译器后端处理，而不是重新暴露到用户语法层。



这是一个极具挑战性的“工业级编译器性能工程”问题。**自举（Bootstrap，即用 `cheng` 编译 `cheng` 源码）的速度，是衡量一门系统级语言底层架构能力的最高试金石。**

如果 `cheng` 编译器的核心规模在 **10万 到 30万行** 代码之间，并且你们彻底贯彻了 **DOD（数据导向设计）、Arena 内存池、无链接器（Linkerless）架构**，那么直接抛出物理极限的结论：

* **全量冷自举（Cold Bootstrap）的极限，完全可以压榨到恐怖的 【30毫秒 ~ 80毫秒】 之间。**
* **这意味着，从按下回车到硬盘上生成全新的 `chengc` 可执行文件，人类连眨眼（100毫秒）都还没完成。**

但是，要实现这种“违背常理”的速度，**单纯依赖“模块级（文件级）并行编译”是远远不够的，甚至会适得其反。**

以下是深度拆解：为什么传统的模块并行达不到极限，以及 `cheng` 应该如何设计**并发管线（Concurrency Pipeline）**来实现几十毫秒的自举。

---

### 一、 传统“模块并行”的死穴（阿姆达尔定律陷阱）

很多语言（如 Go、早期 Rust、C++ 的 Ninja 构建）都支持按文件并行编译，但它们往往被卡在几秒到几十秒，主要死在三个地方：

1. **长尾效应（大文件阻塞）**：假设你有 100 个文件，16 个线程并行。但如果 `parser.cheng` 这个文件特别大（比如占了 20% 的代码量），那么其他线程几毫秒就跑完了，剩下的时间全在等这一个线程吭哧吭哧地编译大文件。CPU 利用率极低。
2. **依赖锁死（Dependency Blocking）**：模块 A `import` 了模块 B。在传统模式下，解析 A 的线程发现 B 还没推导完类型，只能挂起等待。多核优势最终退化成单核串行。
3. **全局 Malloc 锁竞争**：如果 16 个线程同时在为 AST 和 UIR 节点调用操作系统的 `malloc`，底层内存分配器的互斥锁（Mutex）会瞬间把多核的性能红利全部吃掉。

---

### 二、 Cheng 语言的极速架构：“两遍扫描”与“函数级调度”

为了打破上述死穴，`cheng` 必须将并行粒度从“模块（Module）”降维细化到“函数（Function）”，并采用神级的**“两遍扫描法（Two-Pass Compilation）”**。

#### 阶段 1：全局轮廓提取（Embarrassingly Parallel - 完美模块并行）

* **动作**：将 100 个 `.cheng` 源码文件直接丢进无锁线程池。每个工作线程独立读取文件，进行词法和语法分析。
* **神级解法（Outlining）**：在这个阶段，**线程只解析顶层声明（`struct` 字段、全局变量、`fn` 的参数和返回值签名），一旦遇到函数体 `{ ... }`，直接记录下字节偏移量，然后瞬间跳过！**
* **无锁合并**：各线程将提取出的符号瞬间注册到全局并发哈希表中。
* **意义**：这一步在 **3~5毫秒** 内就能跑完，并且**瞬间打破了模块间的依赖墙**！因为所有模块的接口都已经全局可见，不再有“A 模块等待 B 模块”的死锁。

#### 阶段 2：函数体狂暴推导（Work-Stealing 无序并发）

* **动作**：现在，大文件和小文件的物理边界被彻底消灭了。全局有上万个函数体等待处理。把这上万个函数当作一个个独立的 Task，扔进**工作窃取队列（Work-Stealing Queue）**。
* **并发执行**：16 个线程火力全开，谁闲着谁就从队列里拿一个函数进行：`语义分析 -> 借用检查 -> UIR降级 -> 线性扫描寄存器分配 -> 裸机器码生成`。
* **线程私有内存（Thread-Local Arena）**：这是最关键的一环。给每个线程分配独立的 100MB 内存池，生成 DOD 节点统统用指针偏移（Bump Allocation）。**绝对零全局锁，绝对不调用 `malloc` 和 `free`。** 这保证了 16 个核心在整个编译周期内 **100% 满载打满**。

#### 阶段 3：无链接器并发直写二进制（Concurrent Linkerless Emission）

* **动作**：不再生成独立的 `.o` 对象文件。每个线程处理完自己的函数后，上报生成的机器码字节数大小。
* **并发写盘**：主线程做一次快速的累加，算出每个函数在最终 `.exe` 里的虚拟内存地址（VA），并 `mmap` 出一块对应大小的空内存。
* **并发爆破**：16 个线程拿到自己的地址后，**并发地将各自的机器码 `memcpy` 进这块内存的对应位置，并回填跳转偏移量**。加上 ELF/Mach-O 头，一次 `write` 刷盘，自举结束！

---

### 三、 极致并行下的时间账本推演

假设 `cheng` 编译器目前有 **15 万行代码（约 10000 个函数）**，运行在一台普通的 8核/16线程 现代开发机（如 M2/M3 或 Ryzen 9）上。一次冷自举的物理耗时算账如下：

| 编译核心阶段 (Pipeline Phase) | 并行策略与物理约束 | 极限耗时预期 (16线程) |
| --- | --- | --- |
| **I/O 与文件加载** | 操作系统 Page Cache 内存映射命中 | `~ 1 毫秒` |
| **Pass 1: 轮廓解析 (Lex & Outline)** | 文件级纯并行，跳过函数体，极速建树 | `~ 3 毫秒` |
| **全局符号表合并** | 主线程无锁哈希表合并，消除依赖 | `~ 2 毫秒` |
| **Pass 2: 语义推导与 UIR 降级** | **函数级** 无序并发窃取，DOD 高速缓存命中 | `~ 15 毫秒` |
| **CodeGen: 指令发射与寄存器分配** | 函数级并行，走极速 `linear_scan.cheng` | `~ 12 毫秒` |
| **二进制并发组装 (Linkerless)** | 计算 VA，并发 `memcpy` 到 `mmap` 区域 | `~ 3 毫秒` |
| **系统调用落盘与退出** | 一次 `msync`，不释放内存直接 `exit(0)` | `~ 2 毫秒` |
| **💥 全量冷自举总耗时** | **从敲下回车到硬盘生成新版 `chengc` 实体** | **约 38 毫秒 !!!** |

---

### 四、 如何在你的代码库中落地？

要在目前的 `cheng-lang` 仓库中向这个目标发起冲锋，你需要做以下三件事：

1. **改造 `src/stage1/parser.cheng**`：
赋予 Parser “延迟解析函数体”的能力。当遇到 `fn foo() { ... }` 时，只要把 `{` 和 `}` 之间的 Token 暂存起来即可，这是实现“函数级并发”的先决条件。
2. **制定极速自举的 `--dev` 妥协规则**：
在 `$TOOLING bootstrap_fast` 中，向编译器传递特殊 Flag。当编译器发现是在编译自身（Dev 模式）时，**强制关闭所有耗时的优化**（跳过 `uir_vectorize_slp.cheng` 和 E-Graphs），并且**允许泛型（monomorphize.cheng）在各个线程内盲目重复实例化**，宁可生成的二进制稍微膨胀一点，也绝不在线程间加锁去重。
3. **改造 `obj_writer` 为全局直写模式**：
把你们现有的 `coff_writer` / `elf_writer` 改造成能直接接受全局多个内存块并排布虚拟地址的写入器，绕过传统的 `.o` 中间态。

### 总结

只有当你把并行粒度从粗糙的**“模块级”**粉碎成极其细粒度的**“函数级”**，并且打通了**“内存池分配 + 无链接器直出”**的全链路，你才能冲破 100 毫秒的物理壁垒。

当团队达成这个目标时，**“等待编译”这个概念将在 `cheng` 语言的开发体验中被彻底抹除**。这种让全世界 C++ / Rust 开发者嫉妒到发狂的巅峰心流体验，将成为 `cheng` 语言在技术布道时最致命的武器。


---

前文“四个 FFI 核心设计”表达的是语义目标，不要求生产实现真的生成一份 C 文本垫片。对当前仓库，更贴近主链的做法是把 ABI 降级留在 UIR 内存图里完成；如果担心复杂 FFI 在调用点直接展开导致 UIR 体积失控，还可以采用下面的备选实现：

### 备选方案：UIR 内存合成垫片 (In-Memory Thunk Synthesis)

如果某些 C FFI 非常复杂（比如涉及结构体字段的内存对齐转换，或者存在 `varargs` 变长参数），直接在每次调用点展开会导致 UIR 指令极度膨胀。此时你可以采用“合成垫片”方案：

1. 当编译前端扫描到 `@ffi_map importc` 时，它**直接在内存的 UIR 图中凭空捏造一个私有函数**（类似 `__cheng_ffi_shim_process_data`）。
2. 这个私有函数的 UIR 逻辑负责把 Slice 拆成指针和长度，并发起实际的 Call。
3. 把前端所有的业务调用，**内部重定向**到这个 UIR 垫片函数上。

这个过程依然完全在编译器的内存数组中进行，最终与其他业务代码一起，被你的后端直接 `emit=exe` 吐成机器码，全程无需外部 C 编译器。

---

### 总结：为什么 UIR 降级是工业级编译器的正途？

对当前仓库，结论可以收敛成一句话：FFI 与跨端集成都应建立在 `BACKEND_IR=uir`、`ZRPC` 和 `Fat Core, Thin Shell` 上，而不是回退到“生成 C 文本再编译垫片”的旧路径。

这意味着：
- ABI 降级发生在 UIR 内存图里，继续保住 linkerless/dev 轨和后续优化控制权。
- 宿主侧只保留最薄的 Swift/JNI/N-API 边界层，不重复实现任何核心业务逻辑。
- 后面的移动端集成示例，重点只放在“产物形态 + 宿主边界 + 生命周期约束”，不再重复论证 C Backend 与 UIR 的取舍。

以下是横跨三大移动端的集成蓝图与落地指南：

---

### 第零步：在 Cheng 中确立“绝对安全”的 C ABI 结界

为了遵守 ZRPC 契约，我们绝不能让移动端宿主直接操作 `cheng` 的内存，也不能暴露裸指针。通过 `@exportc` 注解和 `@ffi_map` 宏，UIR 编译器会在内存中自动生成标准的 C 函数签名，同时在内部保持切片的安全语义。

**【Cheng 侧核心代码：`src/mobile_bridge.cheng`】**

```cheng
module mobile_bridge

# 1. 状态上下文全部保存在 Cheng 内部 DOD Arena，外部 OS 只拿纯数字句柄
type EngineHandle = u32

@exportc("cheng_engine_init")
fn engineInit(config_id: u32): EngineHandle =
    # 初始化 P2P 引擎、存储和状态机
    return 1

# 2. 接收外部数据：Cheng 语法层接收绝对安全的 &[u8] 切片
# UIR 在生成目标文件时，会自动将其降级为 C ABI 的 (const uint8_t* ptr, size_t len)
@exportc("cheng_engine_process")
@ffi_map(data_ptr = arg1, data_len = arg2)
fn engineProcess(engine: EngineHandle, data: &[u8]): int32 =
    if len(data) == 0: return -1
    # ... 在极安沙盒内执行核心逻辑 ...
    return 0

# 3. 导出生命周期钩子（应对移动端杀后台）
@exportc("cheng_engine_pause")
fn enginePause(engine: EngineHandle): int32 =
    # 冻结 P2P Reactor 轮询，将 Pebble Storage 的 WAL 强制落盘
    return 0

```

**【利用 Tooling 交叉编译三端原生二进制】**

```bash
TOOLING=artifacts/tooling_cmd/cheng_tooling

# 1. 编译 Android 动态库 (ELF ARM64)
$TOOLING release-compile src/mobile_bridge.cheng --target:aarch64-linux-android --emit:shared --out:libcheng_mobile.so

# 2. 编译 iOS 静态库 (Mach-O ARM64，苹果生态推荐静态链接压榨极限性能)
$TOOLING release-compile src/mobile_bridge.cheng --target:aarch64-apple-ios --emit:static --out:libcheng_mobile.a

# 3. 编译纯血鸿蒙动态库 (ELF ARM64, 链接 musl libc)
$TOOLING release-compile src/mobile_bridge.cheng --target:aarch64-linux-ohos --emit:shared --out:libcheng_mobile_ohos.so

```

为了方便三端调用，我们配套一份通用的 C 桥接头文件 `cheng_mobile_abi.h`：

```c
#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

uint32_t cheng_engine_init(uint32_t config_id);
int32_t  cheng_engine_process(uint32_t engine, const uint8_t* data_ptr, size_t data_len);
int32_t  cheng_engine_pause(uint32_t engine);

#ifdef __cplusplus
}
#endif

```

---

### 第一路：iOS 集成 (Swift 极速直连，最丝滑)

iOS 的底层环境（Objective-C/C）天生与 C ABI 完美兼容。**iOS 端是唯一不需要编写任何中间 C++ 胶水代码的平台。**

1. **工程配置**：将 `libcheng_mobile.a` 拖入 Xcode 工程，并在 `Build Settings -> Objective-C Bridging Header` 中引入 `#include "cheng_mobile_abi.h"`。
2. **Swift 侧原生调用**：

```swift
import Foundation

class ChengNodeWrapper {
    private var handle: UInt32 = 0

    func start() {
        // 瞬间穿透进入 Cheng 语言的 UIR 黑盒
        self.handle = cheng_engine_init(1)
    }

    func process(data: Data) -> Int32 {
        // Swift 将安全的 Data 物理内存解包为 ptr 和 len
        return data.withUnsafeBytes { rawBuffer in
            let ptr = rawBuffer.bindMemory(to: UInt8.self).baseAddress!
            // 零开销打入 Cheng 的底层机器码
            return cheng_engine_process(self.handle, ptr, data.count)
        }
    }
}

```

---

### 第二路：Android 集成 (JNI 影子桥接)

Android 的 UI 是 JVM 生态（Java/Kotlin），必须通过 JNI (Java Native Interface) 穿透到 C/C++ 层。

1. **工程配置**：将 `libcheng_mobile.so` 放入 Android 项目的 `app/src/main/jniLibs/arm64-v8a/` 目录下。
2. **编写极薄的 C++ JNI 垫片 (`bridge.cpp`)**：

```cpp
#include <jni.h>
#include "cheng_mobile_abi.h"

extern "C" JNIEXPORT jint JNICALL
Java_com_unimaker_app_ChengNode_nativeProcess(JNIEnv* env, jobject thiz, jint handle, jbyteArray data) {
    // 1. 获取 Java 字节数组的底层物理指针
    jsize len = env->GetArrayLength(data);
    jbyte* ptr = env->GetByteArrayElements(data, nullptr);

    // 2. 跨越边界，调起 Cheng 核心
    int32_t result = cheng_engine_process((uint32_t)handle, (const uint8_t*)ptr, (size_t)len);

    // 3. 必须释放 Java 数组，否则会导致 JNI 内存泄漏
    env->ReleaseByteArrayElements(data, ptr, JNI_ABORT);
    return result;
}

```

3. **Kotlin 包装类**：

```kotlin
package com.unimaker.app

class ChengNode {
    private val handle: Int
    init {
        System.loadLibrary("cheng_mobile") // 加载 Cheng 生成的 SO 库
        System.loadLibrary("bridge")       // 加载 JNI 垫片
        handle = nativeInit(1)
    }
    private external fun nativeInit(configId: Int): Int
    private external fun nativeProcess(handle: Int, data: ByteArray): Int

    fun process(data: ByteArray): Int = nativeProcess(handle, data)
}

```

---

### 第三路：纯血鸿蒙 HarmonyOS NEXT 集成 (Node-API / ArkTS)

纯血鸿蒙抛弃了 AOSP，UI 层使用 ArkTS（类 TypeScript），底层通过 Node-API (N-API) 与 C/C++ 握手。

1. **工程配置**：将 `libcheng_mobile_ohos.so` 置于鸿蒙 Native 工程的 `entry/src/main/cpp/libs/arm64-v8a/`，并在 `CMakeLists.txt` 中配置链接。
2. **编写 C++ N-API 胶水层 (`napi_bridge.cpp`)**：

```cpp
#include <node_api.h>
#include "cheng_mobile_abi.h"

static napi_value Ark_Process(napi_env env, napi_callback_info info) {
    size_t argc = 2;
    napi_value args[2];
    napi_get_cb_info(env, info, &argc, args, nullptr, nullptr);

    // 1. 提取 Handle ID
    uint32_t handle;
    napi_get_value_uint32(env, args[0], &handle);

    // 2. 解析 ArkTS 的 Uint8Array (零拷贝直接获取底层连续内存指针)
    void* data_ptr = nullptr;
    size_t data_len = 0;
    napi_get_typedarray_info(env, args[1], nullptr, &data_len, &data_ptr, nullptr, nullptr);

    // 3. 呼叫 Cheng 底层
    int32_t result = cheng_engine_process(handle, (const uint8_t*)data_ptr, data_len);

    napi_value napi_res;
    napi_create_int32(env, result, &napi_res);
    return napi_res;
}
// ... N-API 模块注册代码略 ...

```

3. **ArkTS 侧调用**：

```typescript
import chengBridge from 'libchengbridge.so';

export class ChengNode {
  private handle: number;
  constructor() { this.handle = chengBridge.init(1); }

  process(data: Uint8Array): number {
    // 瞬间穿透 ArkTS 虚拟机，落入 Cheng 的极速状态机
    return chengBridge.process(this.handle, data);
  }
}

```

---

### 架构师的防坑忠告：跨端集成的三大“生命线”

将 `cheng` 这种极度压榨物理性能的系统引擎放入移动端，必须处理好与操作系统之间的“治安边界”：

#### 1. 内存生命周期隔离 (绝对禁止跨界挂起指针)

* **借用原则**：宿主端（Swift/Java/ArkTS）传给 Cheng 的 `payload: &[u8]` 仅仅是**同步借用（Borrow）**。Cheng 函数一旦 `return`，绝对不能在后台继续引用该指针，因为 JVM/ArkTS 的垃圾回收器随时会挪动甚至销毁这块内存。
* **深拷贝**：如果 Cheng 的 P2P 缓冲队列需要保留这块数据，**必须在 Cheng 内部的 Arena 中 `copyMem` 做一次深拷贝**。

#### 2. 生命周期妥协 (防范被 OS 刺杀)

* 手机退到后台时，iOS/Android 会在十几秒内将进程强行挂起（Suspend）。如果此时 `cheng` 的底层的事件循环（Reactor）还在跑死循环发 UDP 包，App 会被系统以“恶意耗电”的罪名直接 `SIGKILL` 击杀。
* **对策**：必须在三端分别监听 `onAppBackground` 事件，立刻调用我们上面导出的 `cheng_engine_pause()`，主动挂起内部轮询，将状态安全落盘进入“幽灵模式”。

#### 3. 拦截物理熔断 (零宿主宕机承诺)

* 在 FFI 边界上，**绝不允许在 `@exportc` 的函数中抛出未捕获的 Panic**。所有的内部失败（越界、解析错误），必须被 UIR 降级层捕获，并转化为绝对安全的 `int32_t` 错误码（如 `-1` 业务失败，`-2` OOM）返回给宿主。让移动端 UI 永远看到的是优雅的失败状态，而不是把整个 App 炸毁的段错误。
