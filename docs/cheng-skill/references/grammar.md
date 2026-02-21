# Cheng 语法速查（稳定对齐版）

权威来源：`docs/cheng-formal-spec.md`（版本 2026-02-21）。
本文件只做速查；如有冲突，一律以正式规范为准。

## 关键结论
- 顶层例程仅使用 `fn` 与 `iterator`。
- 逻辑运算使用 `&&` `||` `!`；按位异或使用 `^`。
- 单参数调用允许 `f x` 或 `f(x)`；禁止 `f (x)`。
- 类型转换仅允许 `TypeExpr(expr)`。
- 导入仅允许归一化模块路径（`<pkg>/<path>`、`std/<name>`）。
- `cheng/<pkg>/<path>` 为兼容别名，推荐迁移为 `<pkg>/<path>`。
- 对非相对/非绝对模块路径，解析器可回退尝试 `<workspace>/src/<module>.cheng`。
- 内建字符串类型仅 `str`、`cstring`（`string` 非内建类型名）。
- 编译器 `stage1` 主链路会显式拒绝 `string` 类型名。
- no-pointer 生产口径（`ABI=v2_noptr` + `STAGE1_NO_POINTERS_NON_C_ABI=1`）下，非 C ABI 模块默认禁指针（C ABI bridge 按策略豁免）。
- 禁用指针类型：`T*`、`void*`、`ref T`、`ptr[T]`。
- 禁用指针操作：`&`、`*`、`->`、`dataPtr/getPointer`、`ptr_add/load_ptr/store_ptr`、`copyMem/setMem/zeroMem`、`alloc/dealloc`。
- FFI 影子桥接：优先 `@ffi_map`（`T[] -> ptr/len`）、`@ffi_out_ptrs`（`out-ptr -> tuple`）、`@ffi_handle`（`void* <-> handle`）与 `@importc + var T`（borrow struct bridge）。

## 已移除/禁用写法（必须避免）
- 顶层声明：`proc`、`method`、`converter`。
- 类型转换/默认值：`TypeExpr expr`、`TypeExpr (expr)`、`TypeExpr()`、`cast[T](x)`、`(T)(x)`。
- 导入：字符串路径导入、相对/绝对路径导入、`from import`、`import A, B`、路径中带空格。
- 旧运算：`div`/`mod`、`concat`。
- no-pointer 门禁启用时：出现禁用指针类型或禁用指针操作会报 `no-pointer policy`。

## 模块与导入
```cheng
module demo

import std/os
import std/json as stdjson
import libp2p/swarm
import libp2p/[crypto,transport]
```

## 顶层声明（常用）
- 绑定：`let` `var` `const`。
- 例程：`fn` `iterator`。
- 类型：`type` `enum` `ref` `tuple` `set`。
- 抽象：`trait` `concept`。
- 元编程：`template` `macro`。

## 类型转换与默认初始化
- 显式转换：`T(x)`（`T` 为类型表达式）。
- 带类型标注且省略初始化时，使用该类型隐式默认值。
- `default[T]` 仅用于表达式/返回/实参位置。
- 空 `[]` 表示空序列字面量；必须有类型上下文（例如 `let xs: int32[] = []`，或返回类型已知时 `return []`），否则会歧义报错。
- `@[]` 已移除（会报错）。

隐式默认值（速记）：
- `bool=false`
- 整数/枚举=`0`
- 浮点=`0.0`
- `char='\0'`
- `str/cstring=""`
- 指针/`ref`/`var`/`void*`=`nil`
- 复合类型（`tuple/object/T[]/T[N]/Table/...`）为 zero-init

示例：
```cheng
let a: int32 = int32(123)
let b: int32
let c = default[int32]
```

## 调用与参数
- 小括号调用：`f()`、`f(x)`、`f(a, b)`、`f(a=1)`。
- 空格单参调用：`f x`。
- 具名参数支持 `=` 与 `:`。

## 最小可编译示例
```cheng
module hello

fn add(a: int32, b: int32): int32 =
    return a + b

fn main(): int32 =
    let result: int32 = add(1, 2)
    if result == 3:
        return 0
    return 1

main()
```
