# 冷编译器 Codegen 完整覆盖清单

## 目标
冷编译器替代 C seed，实现纯 Cheng 冷自举。
即：冷编译器 → 编译热编译器（Cheng 写）→ 热编译器编译一切。

## 自举前提
冷编译器的 codegen 必须覆盖热编译器用到的**全部** BodyIR 操作。
热编译器 ≈ 当前 `bootstrap/cheng_cold.c` 的 Cheng 等价物 + 更多特性。

---

## 当前覆盖状态

### ✅ 已实现（20/38）

| 分类 | 操作 | 状态 |
|---|---|---|
| 算术 | BODY_OP_ADD/SUB/MUL/DIV/MOD (I32) | ✅ |
| 算术 | BODY_OP_ADD/SUB (I64) | ✅ |
| 位运算 | BODY_OP_AND/OR/XOR/SHL/SHR (I32) | ✅ |
| 比较 | BODY_OP_EQ/NE/LT/LE/GT/GE (I32) | ✅ |
| 比较 | BODY_OP_EQ/NE (str) | ✅ |
| 常量 | BODY_OP_I32_CONST / I64_CONST / STR_CONST | ✅ |
| 内存 | BODY_OP_LOAD_I32 / STORE_I32 (I32/I64) | ✅ |
| 控制流 | BODY_OP_BR / BR_COND / SWITCH | ✅ |
| 调用 | BODY_OP_CALL_I32 / CALL_I64 | ✅ |
| 字段 | BODY_OP_FIELD_REF (任意 base) | ✅ |
| 写字段 | BODY_OP_PAYLOAD_STORE (任意 dst) | ✅ |
| 读字段 | BODY_OP_PAYLOAD_LOAD (任意 src) | ✅ |
| 序列 | BODY_OP_SEQ_I32_INDEX/ADD (I32[]) | ✅ |
| 序列 | BODY_OP_SEQ_STR_INDEX/ADD (str[]) | ✅ |
| 序列头 | codegen_seq_header_addr (任意 seq) | ✅ |
| 字符串 | BODY_OP_STR_CONCAT/INDEX/LEN/STRIP/SLICE | ✅ (防御性) |
| 转换 | BODY_OP_I32_TO_STR / I64_TO_STR | ✅ (防御性) |
| 比较 | BODY_OP_CMP (variant tag) | ✅ |
| 终止 | BODY_TERM_RET / BR / SWITCH | ✅ |

### ❌ 未实现（18/38）

| 分类 | 操作 | die() 位置 | 难度 |
|---|---|---|---|
| **Variant** | BODY_OP_MAKE_VARIANT (部分负载类型) | codegen_op L15568 | 中 |
| **Variant** | BODY_TERM_SWITCH (fallthrough) | codegen_switch | 中 |
| **Variant** | Match arm payload binding | parse_match 层 | 高 |
| **对象** | BODY_OP_MAKE_OBJECT | 未实现 | 高 |
| **对象** | 对象字段偏移（动态） | codegen_field_ref 变体 | 中 |
| **序列** | BODY_OP_SEQ_OPAQUE_* | 多处 die | 高 |
| **序列** | BODY_OP_SEQ_I32_INDEX_DYNAMIC | codegen_op | 中 |
| **序列** | BODY_OP_ARRAY_I32_INDEX_DYNAMIC | codegen_op | 中 |
| **返回** | 复合返回 sret（variant/str/object） | codegen_return | 高 |
| **拷贝** | BODY_OP_COPY_COMPOSITE | codegen_op | 中 |
| **IO** | BODY_OP_WRITE_STDOUT/STDERR | ✅ 已支持 | - |
| **IO** | BODY_OP_READ_FILE / WRITE_FILE | 未实现 | 中 |
| **系统** | BODY_OP_EXEC_SHELL / CWD_STR 等 | ✅ 已支持 | - |
| **转换** | BODY_OP_I32_TO_I64 / I64_TO_I32 | ✅ 已支持 | - |
| **指针** | BODY_OP_PTR_CONST / PTR_LOAD / PTR_STORE | 未实现 | 高 |
| **闭包** | 函数指针/闭包调用 | 未实现 | 高 |
| **泛型** | 泛型序列/对象的 codegen | 多处 die | 高 |
| **异常** | ? 操作符（Result 展开） | 未实现 | 高 |

---

## 移除 512 上限必须实现的（按优先级）

这些是进口体编译中最常触发的 die()：

### P0 — 大量模块用到的核心操作

| # | 操作 | 当前行为 | 实现方案 |
|---|---|---|---|
| 1 | 复合返回 sret | `die("missing sret slot")` | 体扫描跳过（已做），或实现 sret 传递 |
| 2 | MAKE_VARIANT 非 I32/I64/str 负载 | `die("unsupported variant payload slot kind")` | 体扫描跳过，或扩展 memcpy |
| 3 | match arm payload binding | 解析层 die | import_mode 守卫 |
| 4 | seq 动态索引（I32/str 之外） | `die("dynamic index target kind mismatch")` | SP 相对寻址 |

### P1 — 提升覆盖率

| # | 操作 | 实现方案 |
|---|---|---|
| 5 | Object 构造 (MAKE_OBJECT) | 零初始化 + 字段写入 |
| 6 | COPY_COMPOSITE | 按 slot_size memcpy |
| 7 | 泛型序列 codegen | 统一寻址模式 |

### P2 — 完整语言覆盖

| # | 操作 | 备注 |
|---|---|---|
| 8 | 指针操作 | 需要类型系统支持 |
| 9 | 闭包/函数指针 | 需要 ABI 设计 |
| 10 | ? 操作符展开 | Result 模式匹配语法糖 |

---

## 实现 P0 后的预期

```
512 上限移除 ✅ (已完成)
P0 全部实现 → dispatch_min 进口体 0 die, 编译后运行 edges=0
P1 全部实现 → src/core/runtime/ 下模块可作为入口编译
P2 全部实现 → src/core/backend/ 下模块可编译，冷编译器能编译热编译器
```

## 热编译器自举路径

```
1. 冷编译器 (C) ──cc──▶ 冷编译器二进制
2. 冷编译器 ──编译──▶ 热编译器子集 (Cheng)
3. 热编译器子集 ──编译──▶ 完整热编译器 (Cheng)
4. 完整热编译器 ──编译──▶ 一切
```

步骤 2 要求冷编译器的 codegen 覆盖热编译器子集用到的**所有**操作。
热编译器子集 ≈ 当前的 `cheng_cold.c` 逻辑用 Cheng 重写。
