# Cheng 所有权与 ORC（精简）

主要来源：`docs/cheng-formal-spec.md`（第 0 章）。

## 运行时模型
- 默认并固定内存模型为 ORC（引用计数），`MM=orc`。
- 严格模式默认启用：`MM_STRICT=0` 关闭，`MM_STRICT=1` 强制开启。
- 严格模式只读 ownership 标注，不回退启发式。
- `memRetain/memRelease` 仅改 refcount；refcount==0 立即释放并移除。
- RC 默认原子实现（可用 `MM_ATOMIC=0` 关闭以换取性能/单线程）；跨线程共享仍需 `share_mt/Arc[T]` 与 `Send/Sync` 边界检查。
- no-pointer 生产口径（`ABI=v2_noptr` + `STAGE1_NO_POINTERS_NON_C_ABI=1`）下，非 C ABI 模块默认禁指针；仅 C ABI bridge 模块按策略豁免。
- 禁用指针类型：`T*`、`void*`、`ref T`、`ptr[T]`。
- 禁用指针操作：`&`、`*`、`->`、`dataPtr/getPointer`、`ptr_add/load_ptr/store_ptr`、`copyMem/setMem/zeroMem`、`alloc/dealloc`。

## 默认 move 语义
- 赋值、参数传递与返回默认移动所有权。
- move 之后，原值视为已消耗。
- 同线程共享使用 `share(x)` 增加引用计数，不做隐式拷贝。
- 跨线程共享必须用 `share_mt(x)` 或 `Arc[T]`。

## 编译期所有权标注
- `exprClass`：`Owned` / `Borrowed` / `Unmanaged`。
- `moveHint`：`MoveFromIdent(name)` 用于 last-use move。
- `escapeClass`：`NoEscape` / `ReturnEscape` / `GlobalEscape`。
- `mustRetain`：Borrowed 或明确需要持有的值。

## moveHint 关键规则
- 以“语句列表”为单位做 last-use 扫描，仅对 `let/var/const` 绑定与简单赋值的 `RHS ident` 生效。
- pattern/解构/field/index 作为 LHS 一律走保守路径。
- 同一作用域存在 `defer` 引用该 ident 时禁用 move。
- 外层语句列表后续仍使用（如 if/loop 之后）则禁用 move。
- `if/when/case` 需通过分支后续 use 检查，任一分支不满足则整体禁用。
- 循环体内对 loop-carried ident 禁用 move（循环条件/迭代表达式使用或先用后定义）。

## `var` 借用
- `var` 形参与绑定表示可变借用。
- 借用值禁止逃逸（不能 return、不能写全局、不能 `share`）。
- 编译器在借用生命周期内强制独占。
- 调用侧对 `var` 形参自动取址（lvalue 会被降级为 `&`）。
- `var` 不是指针语义；`var T*`/`var void*` 不允许。
- 指针成员访问统一使用 `->`。

## Owned / Borrowed / Unmanaged
- Owned：新构造值或 move 目标。
- Borrowed：对既有数据的视图（ident/deref 或借用视图调用）。
- Unmanaged：基础标量（数值、布尔、nil）。

## 借用/视图识别（白名单）
- `ident/deref` 直接视为 Borrowed。
- `field/index` 本身不标记 Borrowed，但可作为借用视图传播的来源。
- `get/getPtr/TableGet*/SeqGet*/StringView` 仅在入参已是借用来源时传播 Borrowed。
- `&/dataPtr/getPointer` 为显式指针视图：禁止 return/`share` 逃逸，参数传递视为绕过借用证明。
- 其它调用默认 Owned。

## ORC 插桩行为
- 赋值/覆盖：对 borrowed 或 must-retain 值先 retain，再 store，最后释放旧 LHS（alias 场景 retain 优先于 release）。
- `return`：返回本地 ident 走 move（跳过 scope cleanup）；返回 borrowed 需 retain；新建 owned 值不额外 retain。
- 表达式语句的 owned 临时值落 `__tmp` 并 release。
- 写入全局/长生命周期容器：用 `share`/retain 防止提前释放；当前字符串容器关键路径已在容器 API 内完成 retain/release 闭环，不依赖 codegen 的 call-site 特判。
- alias 覆盖时先 retain 再 release，避免别名提前释放。
- `?` 解包赋值：Err 分支直接 return/panic，Ok 分支正常；未初始化 LHS 不 release。
- 若后端启用 moveHint，可在 last-use 场景跳过 retain，并在 cleanup 中跳过 moved-from 的 release（以当前后端实现为准）。
- 全局赋值视作 `share` 写入，并在覆盖时 release。

## 多线程
- `share` 仅用于同线程；跨线程必须用 `share_mt(x)`/`Arc[T]`。
- `Arc[T]` 为原子 RC 容器；`Mutex[T]`/`RwLock[T]` 为可变共享通道；`Atomic[T]` 仅限基础数值/指针/布尔。
- 线程边界 API 通过 `@thread_boundary` 注解声明，编译期强制 `Send/Sync`。
- Borrowed/`var` 借用、非原子 RC 值为 `!Send`；`T*`/`void*` 与 FFI 句柄默认 `!Send/!Sync`。

## 诊断
- `OWNERSHIP_DIAGS=1`：编译期所有权诊断。
- `MM_DIAG=1`：运行时 retain/release 日志与计数器。
- `memRetainCount`/`memReleaseCount` 与 `memDiagReset()` 可用于计数一致性检查。

## FFI 与循环引用
- 跨 FFI 传递所有权需显式 `memRetain/memRelease`。
- 循环引用需显式打断（例如置空或拆分容器）。

## 实用建议
- 默认用 `let`，仅在需要可变或借用语义时用 `var`。
- 避免返回或存储 borrowed 值；必要时转 owned 或 `share`。
- 全局/长生命周期容器需确保写入值已拥有或明确共享。
