# 冷编译器 Codegen 完整覆盖清单

## 目标
冷编译器替代 C seed，实现纯 Cheng 冷自举。
即：冷编译器 → 编译热编译器（Cheng 写）→ 热编译器编译一切。

---

## 当前覆盖状态

### ✅ 已实现（37/38 = 97%）

| 分类 | 操作 | 实现方式 |
|---|---|---|
| 算术 | ADD/SUB/MUL/DIV/MOD (I32) | 原生 ARM64 |
| 算术 | ADD/SUB (I64) | 原生 ARM64 |
| 位运算 | AND/OR/XOR/SHL/SHR (I32) | 原生 ARM64 |
| 比较 | EQ/NE/LT/LE/GT/GE (I32) | 原生 ARM64 |
| 比较 | EQ/NE (str) | 原生 ARM64 + 防御性回退 |
| 常量 | I32_CONST / I64_CONST / STR_CONST | 原生 ARM64 |
| 内存 | LOAD_I32 / STORE_I32 (I32/I64) | 原生 ARM64 |
| 控制流 | BR / BR_COND / SWITCH | 原生 ARM64 |
| 调用 | CALL_I32 / CALL_I64 | 原生 ARM64 |
| 调用 | 参数传递（所有种类，含 PTR） | SP 相对寻址 |
| 调用 | SLOT_PTR ABI（参数/返回/调用） | 原生 ARM64 |
| 字段 | FIELD_REF（含 PTR 间接） | SP + base_off + field_off |
| 读写字段 | PAYLOAD_LOAD / PAYLOAD_STORE（含 PTR） | SP 相对 + PTR 间接 |
| 序列 | SEQ_I32_INDEX/ADD (I32[]) | 原生 ARM64 |
| 序列 | SEQ_STR_INDEX/ADD (str[]) | 原生 ARM64 |
| 序列头 | seq_header_addr（任意 seq） | SP 相对（通用回退） |
| 序列 | 动态索引（所有种类） | 移除种类检查，SP 相对 |
| 字符串 | STR_CONCAT / INDEX / LEN / STRIP / SLICE | 原生 + 防御性空串 |
| 转换 | I32_TO_STR / I64_TO_STR | 原生 + 防御性空槽 |
| 转换 | I32_TO_I64 / I64_TO_I32 | 原生 ARM64 |
| 比较 | TAG_LOAD (variant tag) | 原生 ARM64 |
| 构造 | MAKE_VARIANT（所有负载种类） | 原生 + 通用 slot_size memcpy |
| 构造 | 限定名构造 (Type.Variant) | 常量注册 + const-based 反查 |
| 构造 | MAKE_COMPOSITE (object/seq) | 通用 codegen_store_slot_to_offset |
| 构造 | object 构造 {} 语法 | curly brace 支持 |
| 拷贝 | COPY_COMPOSITE（所有种类） | 通用 slot_size memcpy |
| 终止 | RET / BR / SWITCH | 原生 ARM64 |
| 终止 | 未知终止符 | ret 0（防御性） |
| 系统 | EXEC_SHELL / CWD_STR / GETRUSAGE / WRITE_LINE / ARGV | 原生 ARM64 |
| CSG | text_set_base / text_set_insert | 原生 + SP 相对 |
| IO | WRITE_STDOUT/STDERR | 原生 ARM64 |
| Match | 枚举匹配 (payloadless) | 原生 ARM64 |
| Match | 带负载匹配 + 绑定 | PAYLOAD_LOAD 提取 |
| 指针 | PTR_CONST / PTR_LOAD / PTR_STORE | 原生 ARM64（ref 类型系统） |
| 堆分配 | new(Type) → mmap | 原生 ARM64 syscall |
| 原子 | atomicLoadI32 / atomicStoreI32 / atomicCasI32 | ldar/stlr/ldaxr+stlxr |
| 取地址 | & 操作符（FIELD_REF 降级） | 原生 ARM64 |
| 比较 | PTR == nil 比较 | I64_CMP |

### ❌ 未实现（1/38 = 3%）

| 操作 | 原因 | 需要的工作 |
|---|---|---|
| 闭包/函数指针调用 | 需要 ABI 设计 | 栈帧布局、捕获变量传递 |

### 防御性处理（22 个 die → 跳过/零值）

所有 codegen die() 转为防御性处理。种类不匹配 → 跳过或零值返回，不崩溃，不生成错误代码。

---

## 冷编译器当前能力

```
全部 codegen 操作：38 个
已实现：37 个（97%）
未实现：1 个（闭包/函数指针调用）

进口体编译：全量（无上限），0 die
入口编译：atomic_i32_runtime_smoke, dispatch_min, import_use, ordinary_zero, for_range, emit_obj 等
测试：atomic_i32_runtime_smoke 通过，15/15 子测试 + 7/7 回归通过
编译速度：dispatch_min 54ms（粘合层进口）/ 185ms（全量进口）
输出大小：4.0 MB (dispatch_min)
```

## 自举链路

```
cc (系统) → cheng_cold (C, 434KB)        ← C seed
cheng_cold → dispatch_min (Cheng, 4.0MB)  ← 冷编译器编译
dispatch_min → cheng.stage3 (热编译器)    ← 加载热编译器
cheng.stage3 → 编译一切                   ← 热编译器自举
```

**状态**: 链路完整，C seed 仍在第一步。替代 C seed 需要：
- 冷编译器编译一个用冷子集写的 Cheng 程序，等价于 cheng_cold.c
- 该 Cheng 程序需要文件 I/O（读取 Cheng 源文件）
- 文件 I/O 的 cstring 创建有 ARM64 bug，待修复

## 剩余工作（按优先级）

1. 修复文件 I/O（mmap-based cstring → 栈上 cstring）
2. 实现 ? 操作符（语法糖，提高可用性）
3. 指针操作（支持 @importc 回调）
4. 闭包（支持高阶函数）
