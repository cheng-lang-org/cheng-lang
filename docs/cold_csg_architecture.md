# 冷编译器 CSG 架构演进方案

> 目标：冷编译器从"最小 Cheng 子集前端 + 后端"退化为"纯后端（lowering + codegen + emit）"，
> 前端（parser → typed_expr → primary_object_plan）完全由自举后的 Cheng 编译器承担，
> 通过 CSG 事实格式桥接两端，实现 100% 语言覆盖 + 毫秒级编译时间 + 最小 C 代码量。

## 1. 当前架构

```
┌─────────────────────────────────────────────────────┐
│ cheng_cold.c (~20000 行 C)                          │
│                                                     │
│  ┌──────────────┐  ┌──────────┐  ┌───────────────┐  │
│  │ parser/type  │→│ BodyIR   │→│ codegen/emit  │  │
│  │ (~8000 行)   │  │ (~4000)  │  │ (~8000 行)    │  │
│  │              │  │          │  │               │  │
│  │ Cheng 子集   │  │          │  │ ARM64/x86_64  │  │
│  │ 语法解析     │  │          │  │ /RISC-V/ELF   │  │
│  └──────────────┘  └──────────┘  └───────────────┘  │
│                                                     │
│  问题:                                               │
│  - 语言覆盖 ≤ 70%（冷子集，泛型/闭包/字符串插值等缺失）│
│  - 前端逻辑在 C 和 Cheng 中重复实现                    │
│  - 每次修改语法需要同时改两处                          │
└─────────────────────────────────────────────────────┘
```

## 2. 目标架构

```
┌──────────────────────────────────────────────────────────┐
│ Cheng 自举编译器（完整语言）                               │
│                                                          │
│  parser → typed_expr → lowering_plan                     │
│                           ↓                              │
│                    primary_object_plan                    │
│                           ↓                              │
│                    CSG facts 序列化                       │
│                    (cold_csg v2)                          │
└──────────────────────────┬───────────────────────────────┘
                           │
                           │ CSG 事实文件/内存块
                           │ (单次生成，可缓存)
                           ↓
┌──────────────────────────────────────────────────────────┐
│ cheng_cold.c (~10000 行 C)                                │
│                                                          │
│  ┌──────────────┐  ┌──────────┐  ┌──────────────────┐   │
│  │ CSG load     │→│ lowering │→│ codegen + emit   │   │
│  │ (cold_csg_   │  │ (cold_   │  │ (ARM64/x86_64/  │   │
│  │  load)       │  │  csg_    │  │  RISC-V ELF/    │   │
│  │ ~500 行      │  │  lower_*)│  │  COFF/Mach-O)   │   │
│  │              │  │ ~1500 行 │  │  ~8000 行        │   │
│  └──────────────┘  └──────────┘  └──────────────────┘   │
│                                                          │
│  收益:                                                    │
│  - 语言覆盖 100%（前端在 Cheng 侧）                       │
│  - C 代码量减半（20000 → 10000 行）                      │
│  - 编译时间降低（10-15ms → 5-8ms）                       │
│  - 单一前端实现（parser/type 只存在于 Cheng 侧）           │
└──────────────────────────────────────────────────────────┘
```

## 3. CSG 事实格式（cold_csg v2）

### 3.1 格式定义

Cold 编译器已有的 `ColdCsg` 数据结构作为事实容器。

```
cold_csg_version=2
cold_csg_entry=main

# 类型定义
cold_csg_type	<type_id>	<type_name>	<kind:enum|variant|object>	<variant_count>	<fields_json>
cold_csg_object	<object_id>	<object_name>	<field_count>	<fields_json>

# 函数体事实（primary_object_plan 产出）
cold_csg_function	<fn_id>	<fn_name>	<param_count>	<return_type>	<frame_size>
cold_csg_op	<fn_id>	<op_id>	<op_kind>	<target>	<a>	<b>	<c>	<line>
cold_csg_local	<fn_id>	<local_id>	<name>	<kind>	<slot_offset>	<size>	<align>
cold_csg_block	<fn_id>	<block_id>	<op_start>	<op_count>
cold_csg_term	<fn_id>	<term_id>	<term_kind>	<result>	<true_block>	<false_block>
cold_csg_call	<fn_id>	<call_id>	<target_fn>	<arg_start>	<arg_count>	<result_slot>
cold_csg_const	<const_id>	<const_name>	<value>
cold_csg_const_str	<const_id>	<const_name>	<value>

# 重定位/补丁
cold_csg_patch	<fn_id>	<patch_pos>	<target_fn>	<patch_kind:bl|adr|bcond>
cold_csg_reloc	<fn_id>	<reloc_offset>	<symbol_name>	<reloc_type>
```

### 3.2 与现有 ColdCsg 的兼容性

现有 `ColdCsg` 结构已定义：

```c
typedef struct {
    int32_t fn_index;
    int32_t indent;
    Span kind;      // 行类型标记
    Span payload;   // 列数据（tab 分隔）
    int32_t line;
} ColdCsgStmt;

typedef struct {
    Span name;
    int32_t param_count;
    Span return_type;
    int32_t frame_size;
} ColdCsgFunction;

typedef struct {
    ColdCsgFunction *functions;
    int32_t function_count;
    ColdCsgStmt *stmts;
    int32_t stmt_count;
    Arena *arena;
    Symbols *symbols;
} ColdCsg;
```

扩展方案：
- `stmt` 的 `kind` 字段区分事实类型（`cold_csg_op`、`cold_csg_local`、`cold_csg_block` 等）
- `payload` 包含 tab 分隔的字段值
- `cold_csg_load` 已能解析这些事实并填充 `ColdCsg` 结构
- `cold_csg_lower_*` 将 `ColdCsg` → BodyIR → codegen

## 4. primary_object_plan 序列化

### 4.1 当前产出

`primary_object_plan.cheng` 当前产出 `PrimaryBodyIr` 结构：

```cheng
PrimaryBodyIr =
    functions: PrimaryObjectIrFunction[]
    ops: BodyOp[]          // BODY_OP_* 扁平数组
    locals: LocalSlot[]    // 局部变量
    blocks: BodyBlock[]    // 基本块
    terms: BodyTerm[]      // 终止符
    calls: CallOp[]        // 调用信息
    consts: ConstDef[]     // 常量
    patches: Patch[]       // 补丁/重定位
```

### 4.2 序列化函数

新增 `PrimaryBodyIrEmitCsgFacts(ir: PrimaryBodyIr, sink: FactSink)`：

```cheng
fn PrimaryBodyIrEmitCsgFacts(ir: PrimaryBodyIr, sink: var FactSink) =
    FactWriteVersion2(sink)
    
    // 1. 类型定义
    for t in ir.types:
        FactWriteTypeRecord(sink, t)
    
    // 2. 函数体事实
    for fn in ir.functions:
        FactWriteFunctionRecord(sink, fn)
        // ops
        for i in fn.opStart..<fn.opEnd:
            FactWriteOpRecord(sink, fn.id, ir.ops[i])
        // locals
        for i in fn.localStart..<fn.localEnd:
            FactWriteLocalRecord(sink, fn.id, ir.locals[i])
        // blocks
        for i in fn.blockStart..<fn.blockEnd:
            FactWriteBlockRecord(sink, fn.id, ir.blocks[i])
        // terms
        for i in fn.termStart..<fn.termEnd:
            FactWriteTermRecord(sink, fn.id, ir.terms[i])
        // calls
        for i in fn.callStart..<fn.callEnd:
            FactWriteCallRecord(sink, fn.id, ir.calls[i])
    
    // 3. 补丁/重定位
    for p in ir.patches:
        FactWritePatchRecord(sink, p)
```

## 5. 冷编译器改造

### 5.1 保留模块

| 模块 | 行数（估） | 说明 |
|------|-----------|------|
| `ColdCsg` 数据结构 | ~200 | CSG 事实容器 |
| `cold_csg_load` | ~300 | CSG 事实解析 |
| `cold_csg_lower_*` | ~1500 | CSG → BodyIR lowering |
| `BodyIR` | ~800 | 中间表示 |
| codegen (ARM64/x86_64/RISC-V) | ~3000 | 多目标代码生成 |
| ELF64/COFF/Mach-O emit | ~2000 | 格式写入 |
| 入口/命令分发 | ~500 | `cold_cmd_system_link_exec` 等 |
| arena/mmap/util | ~500 | 基础设施 |
| work-stealing/并行 | ~200 | 极限架构 |
| **合计** | **~9000** | |

### 5.2 移除模块

| 模块 | 行数（估） | 说明 |
|------|-----------|------|
| parser（token/span/indent） | ~3000 | 源码解析 |
| parse_fn/parse_expr/parse_stmt 等 | ~4000 | 语法解析 |
| type check（符号表构建） | ~2000 | 类型系统 |
| import 类型收集 | ~1000 | 导入处理 |
| CSG 类型行往返（已禁用） | ~800 | 冗余往返 |
| **合计** | **~11000** | |

### 5.3 编译入口

```c
// 新入口：从 CSG 事实编译
static bool cold_compile_csg_to_object(const char *csg_path,
                                        const char *out_path,
                                        const char *target,
                                        ColdCompileStats *stats) {
    Arena *arena = arena_new();
    ColdCsg csg;
    if (!cold_csg_load(&csg, arena, csg_path)) return false;
    
    // lowering: CSG → BodyIR
    for (int32_t i = 0; i < csg.function_count; i++) {
        cold_csg_lower_function(&csg, i);
    }
    
    // codegen + emit（并行）
    codegen_program(/* ... */);
    
    // 格式写入（根据 target 选择）
    if (is_macho)  cold_emit_macho(out_path, code);
    if (is_elf)    cold_emit_elf64_obj(out_path, code, machine);
    if (is_coff)   cold_emit_coff_obj(out_path, code);
}
```

## 6. 自举管线

```
阶段 0（初始）:
  cheng_cold.c (旧版，含前端) → 编译后端驱动 → cheng.stage1

阶段 1（过渡）:
  cheng.stage1 → 编译 Cheng 源码 → 产出两份产物:
    a) backend_driver/cheng (可执行)
    b) cold 消费用 CSG facts (backend_driver.cheng.csg)

阶段 2（csg-in 自举）:
  cheng_cold.c (新版，纯后端) --csg-in:backend_driver.cheng.csg → 新 backend_driver
  验证: 新旧 backend_driver 行为一致 (fixed_point)

阶段 3（纯 csg 自举闭环）:
  砍掉冷编译器的 parser/frontend 模块
  只保留 CSG load + lowering + codegen + emit
  冷编译器 < 10000 行 C
```

## 7. 性能预估

| 阶段 | 冷编译器行数 | 编译时间 | 语言覆盖 |
|------|-------------|---------|---------|
| 当前 | ~20000 | 12-15ms | ~70%（冷子集） |
| 过渡期 | ~20000 | 15-20ms（CSG 生成 + cold 编译） | 100% |
| 目标 | ~10000 | 5-8ms（纯后端） | 100% |

CSG facts 可缓存：首次编译生成 CSG 文件，后续编译直接 `--csg-in` 跳过前端，仅走 lowering + codegen + emit。

## 8. 实现计划

### Phase 1：CSG 事实序列化（1-2 天）

- `primary_object_plan.cheng` 中实现 `PrimaryBodyIrEmitCsgFacts`
- 产出标准 CSG v2 格式
- 验证：生成的 CSG 能被 `cold_csg_load` 正确解析

### Phase 2：冷编译器纯后端模式（1-2 天）

- 添加 `--csg-in` 作为冷编译器主入口
- `cold_compile_csg_path_to_macho` → 完整实现（当前仅框架）
- 验证：`--csg-in` 产物与传统路径产物 SHA 一致

### Phase 3：parser/frontend 移除（1 天）

- 确认 Phase 2 稳定后，移除 parser/type/import 模块
- 冷编译器缩减到 ~10000 行
- 全量回归 + 自举 fixed_point 验证

## 9. 风险与缓解

| 风险 | 缓解 |
|------|------|
| CSG 事实格式不完整，丢失语义信息 | 从 `primary_object_plan` 的完整 BodyIR 导出，确保信息无损 |
| lowering 阶段性能瓶颈 | `cold_csg_lower_*` 已在当前冷编译器中验证（~2-3ms） |
| 自举链断裂 | 保留旧版冷编译器作为 fallback，分阶段迁移 |
| 格式兼容性 | CSG v2 版本号管理，`cold_csg_load` 支持版本检测 |

## 10. 总结

这次架构演进将冷编译器从"最小 Cheng 子集编译器"转变为"通用 Cheng 后端（lowering + codegen + emit）"，核心变化：

- **语言覆盖**：70% → 100%（前端完全由 Cheng 自举编译器承担）
- **C 代码量**：20000 → 10000 行（砍掉 parser/frontend）
- **编译时间**：12-15ms → 5-8ms（跳过解析，直接从事实 lowering）
- **维护成本**：单一前端实现（Cheng 侧），冷编译器只做后端
- **自举闭环**：Cheng → CSG facts → cold → 新 Cheng，链路清晰可验证
