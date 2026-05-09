# 冷编译器 Codegen 完整覆盖清单

## 目标
冷编译器替代 C seed，实现纯 Cheng 冷自举。
即：冷编译器 → 编译热编译器（Cheng 写）→ 热编译器编译一切。

---

## 当前覆盖状态

### ✅ 已实现（35/38 = 92%）

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
| 调用 | 参数传递（所有种类） | SP 相对寻址（通用回退） |
| 字段 | FIELD_REF（任意 base） | SP + base_off + field_off |
| 读写字段 | PAYLOAD_LOAD / PAYLOAD_STORE | SP 相对 + OBJECT_REF 间接 |
| 序列 | SEQ_I32_INDEX/ADD (I32[]) | 原生 ARM64 |
| 序列 | SEQ_STR_INDEX/ADD (str[]) | 原生 ARM64 |
| 序列头 | seq_header_addr（任意 seq） | SP 相对（通用回退） |
| 序列 | 动态索引（所有种类） | 移除种类检查，SP 相对 |
| 字符串 | STR_CONCAT / INDEX / LEN / STRIP / SLICE | 原生 + 防御性空串 |
| 转换 | I32_TO_STR / I64_TO_STR | 原生 + 防御性空槽 |
| 转换 | I32_TO_I64 / I64_TO_I32 | 原生 ARM64 |
| 比较 | TAG_LOAD (variant tag) | 原生 ARM64 |
| 构造 | MAKE_VARIANT（所有负载种类） | 原生 + 通用 slot_size memcpy |
| 构造 | MAKE_COMPOSITE (object/seq) | 通用 codegen_store_slot_to_offset |
| 拷贝 | COPY_COMPOSITE（所有种类） | 通用 slot_size memcpy |
| 终止 | RET / BR / SWITCH | 原生 ARM64 |
| 终止 | 未知终止符 | ret 0（防御性） |
| 系统 | EXEC_SHELL / CWD_STR / GETRUSAGE / WRITE_LINE / ARGV | 原生 ARM64 |
| CSG | text_set_base / text_set_insert | 原生 + SP 相对 |
| IO | WRITE_STDOUT/STDERR | 原生 ARM64 |

### ❌ 未实现（3/38 = 8%）

| 操作 | 原因 | 需要的工作 |
|---|---|---|
| 指针（PTR_CONST/LOAD/STORE） | 需要类型系统支持 | 指针类型的一等公民支持 |
| 闭包/函数指针调用 | 需要 ABI 设计 | 栈帧布局、捕获变量传递 |
| `?` 操作符（Result 展开） | 语法糖，需编译器层 | Result 模式匹配自动展开 |

---

## 冷编译器当前能力

```
全部 codegen 操作：38 个
已实现：35 个（92%）
未实现：3 个（指针/闭包/?操作符 — 需要架构设计而非简单扩展）

进口体编译：全量（无上限），0 die
入口编译：dispatch_min, import_use, ordinary_zero 等
测试：7/7 回归测试通过
编译速度：dispatch_min 195ms（全进口）/ 54ms（粘合层）
```

## 剩余工作（按优先级）

### 1. 完成自举最小闭环
- 冷编译器能编译 dispatch_min ✅
- dispatch_min 能加载热编译器 → 热编译器在 C 中，需确认加载路径
- 热编译器编译 Cheng 程序 → 已工作（通过系统链接器）

### 2. 让冷编译器编译热编译器的 Cheng 等价物
- 需要指针/闭包/?操作符 → 3 个未实现操作
- 或找到不用这些特性的热编译器子集

### 3. 性能优化
- 进口体编译 195ms → 目标 100ms 以内
- 入口编译 54ms → 已在目标范围内
