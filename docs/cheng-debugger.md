# Cheng Debugger 最佳方案

> 2026-05-17 · 设计文档（非规范）
> 规范以 `docs/cheng-formal-spec.md` 为准；实现状态以 `docs/roadmap.md`、当前源码和当前可执行产物为准。

## 0. 结论

最佳方案不是单独写 DWARF，也不是只保留 line-map，而是统一生成 **DebugFacts**。

DebugFacts 是唯一调试事实源，再从它派生：

- Cheng 自有 line-map。
- crash / panic / bounds / signal trace 映射。
- DWARF `.debug_*` sections。
- 未来可选的 IDE 调试信息。

这样行号、函数边界、符号名、类型布局不会在 line-map、DWARF、LSP 之间漂移。

## 1. 当前事实

已有能力：

- `artifacts/backend_driver/cheng print-line-map`
- `artifacts/bootstrap/cheng.stage3 debug-report`
- `artifacts/bootstrap/cheng.stage3 print-asm`
- `artifacts/bootstrap/cheng.stage3 print-symbols`
- `artifacts/bootstrap/cheng.stage3 print-object`
- `system-link-exec` 报告字段：
  - `line_map=...`
  - `debug_line_map_path=...`

当前没有：

- DebugFacts。
- DWARF section writer。
- `.debug_line` 行表。
- `.debug_info` compile unit / subprogram DIE。
- 局部变量 location list。
- Cheng 类型 DIE。

## 2. 总体数据流

```
compiler canonical facts
  -> DebugFacts
      -> line-map
      -> crash trace mapping
      -> DWARF
      -> debugger reports
```

DebugFacts 必须来自编译器 canonical facts，不从源码二次扫描猜测。

输入：

- `WorldFacts`
- `SourceBundleFacts`
- `ImportGraphFacts`
- `SymbolFacts`
- `TypeFacts`
- `PrimaryObjectPlan`
- 最终 code layout

输出：

- 函数源码范围。
- 函数符号名和 link name。
- `low_pc/high_pc`。
- 基本块和 source span。
- 栈槽/局部变量映射。
- 类型布局。
- line-map 文本。
- DWARF sections。

## 3. DebugFacts 结构

建议结构：

```c
typedef struct {
    Span package_id;
    Span module_path;
    Span source_path;
    Span function_name;
    Span link_name;
    int32_t symbol_id;
    int32_t line_start;
    int32_t line_end;
    uint64_t low_pc;
    uint64_t high_pc;
} ChengDebugFunctionFact;

typedef struct {
    Span name;
    Span type_name;
    int32_t symbol_id;
    int32_t function_symbol_id;
    int32_t slot;
    int32_t offset;
    int32_t size;
    int32_t line_start;
    int32_t line_end;
} ChengDebugVariableFact;

typedef struct {
    Span type_name;
    int32_t size;
    int32_t align;
    int32_t kind;
} ChengDebugTypeFact;
```

约束：

- `low_pc/high_pc` 必须来自最终 code layout。
- 行号来自 parser/source span facts。
- 类型 size/align 来自真实布局。
- facts 缺失时 `--debug=dwarf` 必须失败。
- 普通无调试构建不写 DWARF。

## 4. DWARF 方案

DWARF 仍是外部调试器主线。

MVP 目标：

- `lldb` / `gdb` 能按函数断点。
- 能按源码行断点。
- `bt` 能显示源码位置。
- `source list` 能显示 Cheng 源码。

必需 section：

- `.debug_abbrev`
- `.debug_info`
- `.debug_line`
- `.debug_str`

Mach-O：

- `__DWARF,__debug_abbrev`
- `__DWARF,__debug_info`
- `__DWARF,__debug_line`
- `__DWARF,__debug_str`

ELF：

- `.debug_abbrev`
- `.debug_info`
- `.debug_line`
- `.debug_str`

COFF/Windows 不进 MVP。

## 5. 类型映射

| Cheng | DWARF 表示 |
|---|---|
| `bool` | base type, 1 byte |
| `int32` / `uint32` | base type, 4 bytes |
| `int64` / `uint64` | base type, 8 bytes |
| `str` | 24 字节 structure：`data@0`、`len@8`、`store_id@12`、`flags@16` |
| `T[N]` | array type |
| `T[]` | 16 字节 sequence header |
| `object` / `tuple` | structure type |
| payloadless enum | enumeration 或 int32 tag |
| ADT/tagged union | tag + payload structure |

no-pointer 生产表面禁裸指针。DWARF 中出现 pointer 只表示内部 ABI 或 FFI 边界，不改变用户语法。

## 6. Line-map 与 DWARF

line-map 必须保留，服务 Cheng 自有诊断：

- panic trace
- bounds trace
- signal trace
- debug-report
- `debug_line_map_path`

DWARF 服务外部调试器。

两者都由 DebugFacts 生成。DWARF 落地后也不能删除或弱化 `debug_line_map_path`。

编译报告至少包含：

- `debug_line_map_path=<path>`
- `debug_facts_cid=<cid>`
- `dwarf_enabled=0|1`
- `dwarf_section_count=<n>`

`--debug=dwarf` 失败时必须 hard-fail，不能只输出 line-map 后宣称源码级调试已启用。

## 7. 实现步骤

### Phase 1：DebugFacts + line-map 等价

- 从现有 line-map 输入提取 DebugFacts。
- 用 DebugFacts 重新生成 line-map。
- 新旧 line-map 对同一 fixture 必须一致。
- 报告输出 `debug_facts_cid`。

### Phase 2：函数级 DWARF

改动：

| 文件 | 改动 |
|---|---|
| `bootstrap/cheng_cold.c` | DebugFacts 到 DWARF section buffer |
| `bootstrap/macho_direct.h` | 写 `__DWARF` segment/sections |
| `bootstrap/elf64_direct.h` | 写 `.debug_*` sections |
| `tools/cold_regression_test.sh` | 增加 `dwarfdump` / `llvm-dwarfdump` 门禁 |

验收：

```bash
artifacts/backend_driver/cheng system-link-exec \
  --root:/abs/root \
  --in:/abs/root/src/tests/ordinary_zero_exit_fixture.cheng \
  --emit:exe \
  --target:arm64-apple-darwin \
  --out:/tmp/cheng_dbg \
  --report-out:/tmp/cheng_dbg.report.txt

dwarfdump /tmp/cheng_dbg
lldb /tmp/cheng_dbg
```

必须证明：

- `dwarfdump` 能看到 compile unit。
- 每个 emitted function 有 subprogram DIE。
- `main` 能按函数名断点。
- 至少一个源码行断点能命中正确行。
- 无 DWARF 构建产物字节不变。

### Phase 3：变量和类型

- 输出 `DW_TAG_formal_parameter`。
- 输出 `DW_TAG_variable`。
- 输出 `DW_AT_location`。
- 输出基础类型和复合类型 DIE。

只先支持稳定栈槽变量：

- `int32`
- `int64`
- `bool`
- payloadless enum tag

## 8. 不做的事

MVP 不做：

- 自定义调试 UI。
- 自定义 debug wire protocol。
- Cheng 表达式求值。
- ownership/ORC 可视化。
- COFF/Windows debug info。
- `lldb` Cheng 语法插件。

## 9. 验收

完成标准：

- DebugFacts 能重建现有 line-map。
- `debug_facts_cid` 稳定。
- `dwarfdump` 能解析。
- `lldb` 能按函数和源码行断点。
- release 默认路径不受影响。
