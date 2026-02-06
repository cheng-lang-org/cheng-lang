# 动态特性的硬实时（HRT）替代方案

硬实时（Hard Real-Time）并不意味着“放弃”现代特性，而是用**确定性**的方式重新实现它们。虽然我们不能使用任意的动态内存分配和 GC，但对于 JSON、UI、异步和内存管理，工业界早已发展出了成熟的 HRT 对应模式。

本文总结了针对四大“动态特性”的硬实时解决方案。

---

## 1. JSON 解析与数据交换

**主流做法**：构建一个动态的 DOM 树（`JObject`），节点包含 `Map<String, Value>`，会有成百上千次微小的内存分配。

**HRT 解决方案**：

### A. 零拷贝/游标解析 (Zero-Copy / Cursor Parsing)
不构建树，直接在原始字节流上操作。
- **原理**：`parse` 函数返回一个指向输入 Buffer 的“游标（Cursor）”。访问 `obj["key"]` 时，实际上是在原始 Buffer 中线性扫描或跳转。
- **内存**：0 分配。
- **代价**：多次访问同一字段可能比 DOM 慢（O(n) vs O(1)），但可以通过索引优化。
- **案例**：`simdjson` (On Demand 模式), Cheng 的 `JsonCursor`。

### B. 模式先行绑定 (Schema-First Binding)
不允许“任意 JSON”。必须先定义好结构体（Struct）。
- **原理**：定义 `struct Config { name: String[64]; port: int; }`。解析器读取 JSON 并直接填充到这个预分配的内存块中。如果字符串超过 64 字节，直接报错。
- **内存**：完全静态分配。
- **案例**：Protobuf (静态模式), FlatBuffers。

---

## 2. DOM 树与富 UI

**主流做法**：保留模式（Retained Mode）。内存中维护一棵巨大的对象树（Widget Tree），每个 Widget 都是堆上的对象。修改属性后由框架计算 Diff。

**HRT 解决方案**：

### A. 立即模式 GUI (IMGUI)
不保留任何 Widget 对象。
- **原理**：每一帧（Frame）从头开始执行 UI 代码。`if (button("Click Me")) { ... }`。UI 状态（如滚动条位置）存储在一个极小的哈希表或 ID 数组中。
- **内存**：仅需一个线性缓冲区（Vertex Buffer）用于渲染，每帧重置。0 对象分配。
- **案例**：`Dear ImGui`, 游戏内调试菜单。

### B. 数据导向设计 (ECS / Data-Oriented)
不使用“树”结构，而是使用“数组”。
- **原理**：UI 元素不是对象，而是 ID（整数）。属性存放在扁平数组 `PositionComponent[]`, `TextComponent[]` 中。父子关系通过 `Parent[]` 数组索引维护。
- **优势**：遍历速度极快（CPU 缓存友好），无 GC 压力。
- **案例**：现代游戏引擎的 UI 系统。

---

## 3. 异步 I/O (Async/Await)

**主流做法**：基于堆分配的 `Promise` 或 `Future` 对象链。运行时调度器（Executor）不确定何时唤醒任务。

**HRT 解决方案**：

### A. 静态状态机 (Static State Machines)
将异步逻辑编译为扁平的 `enum` 状态。
- **原理**：编译器将 `async fn` 展开为一个 `struct StateMachine { state: int; locals... }`。这个 Struct 的大小是固定的，可以在栈上或静态区预分配。
- **调度**：不需要复杂的调度器，只需要一个简单的 `poll()` 循环。
- **案例**：Rust 的 `async` (no-std/embedded), `Embassy` 框架。

### B. 环形缓冲区轮询 (Ring Buffer Polling)
- **原理**：网络包到达时，网卡（DMA）直接写入预分配的 Ring Buffer。用户态任务在周期（Cycle）开始时轮询 Buffer。
- **延迟**：延迟是确定的（最大为 1 个周期）。
- **案例**：`io_uring` (Fixed Buffer 模式), 高频交易网卡驱动。

---

## 4. 垃圾回收 (Garbage Collection)

**主流做法**：在堆上随意分配，由 GC 在后台扫描并回收。会产生随机的“世界暂停（Stop-The-World）”。

**HRT 解决方案**：

### A. 区域内存 / 帧内存 (Region / Arena)
最强大的 HRT 内存管理模式。
- **原理**：在任务开始时申请一大块内存（Arena）。任务执行过程中，分配内存只需移动“指针”（Bump Pointer），速度极快（几条指令）。任务结束（或一帧结束）时，直接把指针重置回 0。
- **效果**：不仅可以在 HRT 中使用“动态数据结构”（如链表、树），而且**不需要 free**，也**没有 GC 暂停**。
- **限制**：所有对象的生命周期必须相同（同生共死）。

### B. 对象池 (Object Pools)
针对长声明周期对象。
- **原理**：预先分配 `Object[1000]`。分配时从空闲链表拿一个，释放时还回去。
- **效果**：O(1) 分配与释放，无碎片。

---

## 总结表

| 动态特性 | HRT 替代方案 | 核心思想 |
| :--- | :--- | :--- |
| **JSON Parse** | Schema-First / Zero-Copy | **不要构建树**，直接填结构体或读字节流。 |
| **DOM Tree** | IMGUI / ECS | **不要用对象**，用数组和 ID。 |
| **Async I/O** | Static State Machine | **不要堆分配 Future**，编译器生成固定状态机。 |
| **GC** | Arena / Object Pool | **不要随机释放**，按帧/任务批量重置。 |

**结论**：硬实时并非“功能缺失”，而是通过**预规划（Pre-allocation）**和**批处理（Batching）**来换取确定性。
