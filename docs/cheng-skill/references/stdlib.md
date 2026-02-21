# Cheng 标准库速查（稳定对齐版）

权威来源：`docs/cheng-formal-spec.md`、仓库 `src/std/`（Cheng 标准库入口）与 `src/runtime/native/`（C 运行时实现）。
本文件只做模块定位与导入示例；语义细节以正式规范与标准库源码为准。

## 导入规则（生产约束）
- 仅使用归一化模块路径：`std/<module>`、`<pkg>/<path>`。
- `cheng/<pkg>/<path>` 为兼容别名，推荐迁移为 `<pkg>/<path>`。
- 禁止字符串路径导入、相对路径导入、绝对路径导入。
- 禁止路径中空格。
- 允许前缀合并导入：`import libp2p/[crypto,transport,swarm]`；分组不支持 `as`。
- no-pointer 生产口径（`ABI=v2_noptr` + `STAGE1_NO_POINTERS_NON_C_ABI=1`）下，非 C ABI 模块默认禁指针；C ABI bridge 模块按策略豁免。
- 禁用指针类型：`T*`、`void*`、`ref T`、`ptr[T]`。
- 禁用指针操作：`&`、`*`、`->`、`dataPtr/getPointer`、`ptr_add/load_ptr/store_ptr`、`copyMem/setMem/zeroMem`、`alloc/dealloc`。

```cheng
import std/os
import std/times
import std/json as stdjson
import libp2p/swarm
import libp2p/[crypto,transport,swarm]
```

## 核心模块（`src/std`）
- `system.cheng`：运行时类型与内存管理基元。
- `strings.cheng`：字符串工具。
- `seqs.cheng`：序列工具与迭代支持。
- `tables.cheng`：表/映射工具。
- `bytes.cheng`：字节缓冲工具。
- `streams.cheng`：流接口。
- `hashes.cheng`：哈希工具。
- `os.cheng`：系统工具。
- `cmdline.cheng`：命令行解析。
- `c.cheng`：C 互操作工具。

## `std/cmdline`（更新）
- 入口函数：`programName()`、`argCount()`/`paramCount()`、`argStr(i)`/`paramStr(i)`。
- flag 探测：`findFlag`、`hasFlag`、`isFlag/isFlagAt`。
- flag 取值：`readFlagValue`、`readFirstFlagValue`、`readLastFlagValue`、`readFlagValueAt`、`readFlagValueAt2`。
- 类型解析：`parseBool`、`parseInt32`、`readBoolFlag`、`readIntFlag`。
- 取值格式兼容：`--key:value`、`--key=value`、`--key value`。

## FFI 影子桥接（RPS 口径）
- `@ffi_map`：把 `T[]`/借用切片桥接到 C ABI 的 `(ptr,len)`。
- `@ffi_out_ptrs`：把 C 的 out-ptr 参数回收为 tuple 返回。
- `@ffi_handle`：把 C `void*` 句柄收敛为 `u32/u64` handle（runtime 映射与失效保护）。
- `@importc + var T`：结构体借用桥接；当前生产 gate 使用 `system + runtime C` 口径验证运行态与符号收敛。

## 字符串类型与默认值（语言对齐）
- 内建字符串类型仅 `str` 与 `cstring`；`string` 不是内建类型名。
- 编译器 `stage1` 主链路对 `string` 类型名会直接报错（提示改用 `str`/`cstring`）。
- 隐式默认初始化时，`str/cstring` 默认值为 `""`（空串）。
- 运行时与 FFI 边界可能出现 `nil` 字符串值；边界代码建议同时考虑 `nil` 与空串。
- `std/strings` 提供 `len/==/[]/[]=/strIsEmpty/strNonEmpty` 等基础能力。
- `charAt` 不再作为标准库接口；历史代码中的 `charAt(s, i)` 在编译期静态改写为 `s[i]`。
- 容器语法：动态序列 `T[]`、定长数组 `T[N]`；序列字面量仅 `[]` / `[a, b]`；旧 `seq[T]`/`array[T,N]`/`@[]` 已移除。

## `std/` 常用模块（`src/std`）
- 系统与时间：`std/os`、`std/times`、`std/monotimes`。
- 字符串与解析：`std/strutils`、`std/strformat`、`std/parseutils`。
- 集合与容器：`std/sequtils`、`std/sets`、`std/tables`、`std/streams`。
- 数学与算法：`std/math`、`std/algorithm`。
- 并发：`std/sync`、`std/syncio`。
- 其它：`std/json`、`std/unicode`、`std/hashes`。

## Native 运行时（`src/runtime/native`）
- `system_helpers.c/.h`：后端链接到系统工具链时所需的运行时符号实现与 ABI 声明。
- `system_helpers_float_bits.c`：x86_64 Darwin 等路径的浮点 bit 运算补充符号。
- `stb_image.h`：图像解码第三方头，仅供 native 运行时/工具链打包使用。

## 并发约束（摘要）
- 同线程共享使用 `share`。
- 跨线程共享必须使用 `share_mt` 或 `Arc[T]`。
- 可变共享优先使用 `Mutex[T]`/`RwLock[T]`。
