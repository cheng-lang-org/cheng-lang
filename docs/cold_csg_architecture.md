# 冷编译器 CSG 架构演进方案（修正版 v2）

> 核心原则：冷编译器不接管 lowering/primary_object_plan，只消费 Cheng 编译器已产出的
> canonical PrimaryObjectPlan/BodyIR/facts，负责 load/verify → object emit → linkerless/link。
> 语义 lowering 保持在 Cheng 侧，不搬回 C。

## 1. 角色边界（不可逾越）

```
Cheng 编译器（完整语言，Cheng 实现）              冷编译器（纯后端，C 实现）
━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━
                                                  严禁：
parser → typed_expr → lowering_plan               - 语义 lowering
        → primary_object_plan                      - 类型推导/检查
        → canonical BodyIR/facts                   - variant/enum tag 分配
                                                   - 符号解析
        产出：CSG 事实文件（二进制/hex）           - import 闭包
                                                   - provider 选择
                                                   ━━━━━━━━━━━━━━━━━━━━━
                                                   只负责：
                                                   - load/verify facts
                                                   - object emit（格式写入）
                                                   - linkerless/link
                                                   - codegen（指令选择/编码）
```

**为什么 lowering 不能进冷编译器**：
- lowering 涉及语义决策（variant layout、enum tag、type layout、ABI lowering）
- 这些决策已经在 Cheng 侧完成（primary_object_plan），搬回 C 就是重复实现
- 路线会偏：cold 变成另一个 Cheng 编译器，而非最小种子后端

## 2. CSG 事实格式（修订）

### 2.1 禁止项

- ❌ tab 分隔文本字段
- ❌ JSON 嵌套字段
- ❌ 无 schema version
- ❌ 无 target/ABI 元数据

### 2.2 格式规范

```
┌─────────────────────────────────────────────────────────────────┐
│ Header (64 bytes, binary)                                        │
│   magic[8]      = "CHENGCSG"                                    │
│   version[4]    = uint32_le (当前: 2)                             │
│   target_triple[32]  = "arm64-apple-darwin\0"                   │
│   abi[4]        = uint32_le (0=system, 1=cold_no_runtime, ...)  │
│   pointer_width[1]   = 8                                         │
│   endianness[1]      = 0 (LE)                                    │
│   reserved[14]       = 0                                         │
├─────────────────────────────────────────────────────────────────┤
│ Section Index (n * 16 bytes)                                     │
│   section_kind[4]    = uint32_le                                 │
│     kind 0 = type_block                                           │
│     kind 1 = function                                             │
│     kind 2 = provider_root                                        │
│     kind 3 = external_symbol                                       │
│     kind 4 = data_constant                                         │
│     kind 5 = string_constant                                       │
│     kind 6 = relocation                                            │
│   section_offset[8]  = uint64_le (从文件头开始)                   │
│   section_size[4]    = uint32_le                                  │
├─────────────────────────────────────────────────────────────────┤
│ Payload Sections（各 section 独立的 hex/binary 编码）              │
│                                                                   │
│ type_block:                                                        │
│   type_count[4]                                                    │
│   [4]byte type_kind (0=enum 1=variant 2=object)                   │
│   [4]byte name_len + name_utf8                                    │
│   [4]byte field_count                                             │
│   for each field: [4]byte slot_kind + [4]byte slot_size           │
│                  + [4]byte field_offset + [4]byte name_len + name │
│                                                                   │
│ function:                                                         │
│   fn_count[4]                                                     │
│   for each fn:                                                    │
│     [4]byte name_len + name_utf8                                  │
│     [4]byte param_count + [4]byte return_kind + [4]byte frame_size│
│     [4]byte op_count                                              │
│     for each op: [4]byte op_kind + [4]byte dst + [4]byte a        │
│                  + [4]byte b + [4]byte c                          │
│     [4]byte local_count                                           │
│     for each local: [4]byte kind + [4]byte slot + [4]byte size    │
│     [4]byte block_count                                           │
│     for each block: [4]byte op_start + [4]byte op_count           │
│     [4]byte term_count                                            │
│     for each term: [4]byte kind + [4]byte result                  │
│                   + [4]byte true_block + [4]byte false_block      │
│     [4]byte call_count                                            │
│     for each call: [4]byte target_fn + [4]byte arg_start          │
│                   + [4]byte arg_count + [4]byte result_slot       │
│                                                                   │
│ provider_root:                                                    │
│   count[4]                                                        │
│   for each: [4]byte id + [4]byte path_len + path_utf8            │
│            + [4]byte name_len + name_utf8                         │
│            + [1]byte is_entry                                     │
│                                                                   │
│ external_symbol:                                                  │
│   count[4]                                                        │
│   for each: [4]byte name_len + name_utf8                          │
│            + [1]byte visibility (0=local 1=global 2=weak)        │
│            + [4]byte size                                          │
│                                                                   │
│ data_constant:                                                    │
│   count[4]                                                        │
│   for each: [4]byte name_len + name_utf8                          │
│            + [4]byte size + payload_bytes                         │
│                                                                   │
│ string_constant:                                                  │
│   count[4]                                                        │
│   for each: [4]byte id + [4]byte len + utf8_bytes                │
│                                                                   │
│ relocation:                                                       │
│   count[4]                                                        │
│   for each: [4]byte fn_id + [4]byte offset + [4]byte type        │
│            + [4]byte symbol_len + symbol_utf8                     │
│                                                                   │
├─────────────────────────────────────────────────────────────────┤
│ Trailer (32 bytes)                                                │
│   section_count[4]   = uint32_le                                  │
│   content_hash[32]   = BLAKE3 over all payload sections          │
└─────────────────────────────────────────────────────────────────┘
```

### 2.3 关键约束

- **全部字段**：长度前缀 + 值，无分隔符，无文本解析歧义
- **schema version**：Header.version，`cold_csg_load` 版本检测，不匹配直接 fail
- **target/ABI**：Header 中声明，用于 object emit 阶段的格式选择和 ABI 决策
- **content_hash**：BLAKE3 哈希覆盖全部 payload sections，用于完整性验证和 fixed-point 比对
- **provider/external/data**：完整描述可执行文件所需的外部依赖，没有这些信息不能闭合到可执行文件

## 3. Provider/Link 模型

### 3.1 事实必须包含的外部依赖

```
可执行文件 = 编译单元(.o) + provider roots + runtime support + external symbols + data/string 常量 + reloc

CSG 事实块:
  provider_root:  运行时入口（core_runtime, debug_runtime, program_support）
  external_symbol: 外部符号（visible, size, linkage）
  data_constant:   嵌入数据（TLS, 全局变量, const 段）
  string_constant: 字符串常量池
  relocation:      重定位条目（符号名 + 偏移 + 类型）
```

### 3.2 冷编译器的消费

```
cold_csg_load → 解析全部 section
    → type_block    → 填充 ColdCsg.symbols（类型表）
    → function      → 填充 ColdCsg.functions + stmts（函数体事实）
    → provider_root → 填充 provider 列表（link provider 选择）
    → external_symbol → 填充符号表（elf64_write_symtab）
    → data_constant → 填充 .rodata section
    → string_constant → 填充 .cstring section
    → relocation    → 填充 rela section
    → cold_csg_lower → BodyIR → codegen → emit
```

## 4. 冷编译器改造（修正）

### 4.1 保留模块（纯后端）

| 模块 | 行数 | 说明 |
|------|------|------|
| `cold_csg_load` + verify | ~500 | CSG 事实解析 + schema/version/hash 验证 |
| `cold_csg_lower_*` | ~1500 | CSG → BodyIR lowering（事实→中间表示） |
| `BodyIR` | ~800 | 中间表示 |
| codegen (ARM64/x86_64/RISC-V) | ~3000 | 多目标指令选择/编码 |
| ELF64/COFF/Mach-O emit | ~2000 | 格式写入 |
| 入口/命令分发 | ~500 | `cold_cmd_system_link_exec` 等 |
| arena/mmap/util | ~500 | 基础设施 |
| work-stealing/并行 | ~200 | 极限架构 |
| **合计** | **~9000** | |

### 4.2 移除条件（不可提前）

```
删除 cold parser/frontend 的前提条件（全部满足才能执行）：

□ --csg-in 产物与传统路径产物 object hash 一致
□ fixed-point: cold_cold(cheng(primary_object_plan)) = cold_cheng(source)
□ 30/30 回归全部通过（--csg-in 模式）
□ 3/3 runtime smoke exit 0（--csg-in 模式）
□ build-backend-driver --csg-in 自举 closed loop
□ 跨端产物格式验证通过（ELF/COFF/Mach-O）

任一不满足 → 保留 parser/frontend，暴露 gap，不删除
```

### 4.3 迁移策略（严格双轨）

```
禁止：保留旧版作为 "fallback"

要求：
  每次编译同时走两条路径：
    路径 A: 传统 parser → type → BodyIR → codegen → emit
    路径 B: Cheng 前端 → facts → cold_csg_load → lowering → codegen → emit

  验证：
    - 两条路径的 object hash 必须一致（确定性）
    - 两条路径的 report 字段必须一致
    - 两条路径的 exit code 必须一致

  不一致 → fail hard，暴露差异，不回退到路径 A
```

## 5. 性能数据（实测，非预估）

### 5.1 当前实测

```
cheng_skill_consistency_smoke 报告:
  primary_object_plan_ms = 83172  (83ms)
  
  编译端到端（含前端）: ~83ms
  冷编译器后端（codegen + emit）: ~5-8ms

  结论：前端（parser + typed_expr + primary_object_plan）是瓶颈，
        冷编译器后端已经够快。
```

### 5.2 CSG 架构的收益

```
CSG facts 可缓存：
  首次编译: Cheng 前端 (~83ms) → facts 文件 → 冷编译器 (~5-8ms) → 可执行文件
  后续编译: facts 文件（缓存命中）→ 冷编译器 (~5-8ms) → 可执行文件

  缓存场景的冷编译器编译时间: 5-8ms（与当前一致，无性能退步）
  首次编译需等待前端产出 facts（83ms），但这是 Cheng 编译器的时间，不计入冷编译器
```

### 5.3 不做预估

- "100% 语言覆盖" → 需实测证明，不能声称
- "5-8ms 全流程" → 需实测证明，当前 facts 生成（Cheng 侧）83ms
- 所有性能数据必须在报告中有字段支撑

## 6. 实现计划（修正）

### Phase 1：CSG 事实格式 + 序列化

- 定义二进制格式（Header + Section Index + Payload + Trailer）
- `primary_object_plan.cheng` 实现事实序列化
- 冷编译器 `cold_csg_load` 适配新格式（schema version 检测）
- 验证：产物能被正确解析（hash 匹配）

### Phase 2：冷编译器纯消费模式

- `--csg-in` 作为冷编译器入口，完整实现 load → lowering → codegen → emit
- 双轨验证：传统路径 vs --csg-in 路径，object hash 比对
- provider/link 模型接入（provider_root → link provider 选择）

### Phase 3：回归 + fixed-point

- 30/30 回归全部双轨通过
- 3/3 runtime smoke 双轨 exit 0
- build-backend-driver --csg-in 自举闭环
- fixed-point 验证通过

### Phase 4：移除 parser/frontend（条件触发）

- 仅在 Phase 3 全部绿灯后执行
- 删除 parser/type/import 模块
- 冷编译器缩减到 ~10000 行
- 全量双轨验证再次通过

### 各阶段不可跳过，不可合并

## 7. 当前已知 Gap

| Gap | 影响 | 优先级 |
|-----|------|--------|
| CSG facts 格式需要从 tab/文本升级到二进制/hex | 当前格式不可用于生产 | Phase 1 |
| primary_object_plan 不支持事实序列化 | 无 facts 产出 | Phase 1 |
| cheng_skill_consistency_smoke native 执行挂起 | 83ms 产物不可运行 | 调查 |
| provider/link 模型在 CSG 路径中缺失 | 不能闭合到可执行文件 | Phase 2 |
| --csg-in 路径未经双轨验证 | 数据不足 | Phase 2-3 |
| 性能数据只有单点（83ms），无全量统计 | 不能声称覆盖/时间 | 持续 |

## 8. 总结

这次架构演进将冷编译器从"最小 Cheng 子集编译器"转变为"canonical facts 消费者（纯后端）"：

- **角色**：不接管 lowering，只消费已产出的 canonical 事实
- **格式**：二进制/hex 编码，带 schema version + target/ABI + hash
- **迁移**：严格双轨验证，失败暴露，不回退，不保留 fallback
- **删除**：仅在所有验证通过后，按条件触发
- **性能**：只报告实测数据，不做预估
