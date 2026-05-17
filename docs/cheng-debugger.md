# Cheng Debugger 设计与实现计划

> 2026-05-17 · 设计文档（非规范）
> 规范以 `docs/cheng-formal-spec.md` 为准；实现状态以 `docs/roadmap.md`、当前源码和当前可执行产物为准。

## 0. 评估结论

调试器主线应分两层：

1. 当前已存在的 line-map / debug-report / crash trace，用于 Cheng 自己的错误定位。
2. 新增 DWARF，用 `lldb` / `gdb` 做源码级断点、单步、回溯和变量查看。

原文需要修正：

- 不能说 Cheng 完全没有调试信息。当前 `system-link-exec` 报告会写 `debug_line_map_path`，工具面已有 `print-line-map`、`debug-report`、`print-symbols`、`print-object`、`print-asm`。
- 不能假设 `FnPosition` 已经携带源码路径和行号。当前 `FnDef` 主要是签名信息；源码 span 来自扫描层 `ColdFunctionSymbol.source_span/line`，需要显式传到 codegen/DWARF 层。
- `str` 类型不是简单 pointer + length。当前 cold ABI 口径是 24 字节：`data@0`、`len@8`、`store_id@12`、`flags@16`。
- Mach-O 可执行文件应按 `__DWARF` segment/section 写入可被 `dwarfdump` 和 `lldb` 识别的元数据，不能只把裸字节附在文件末尾。
- COFF/Windows 不放进 MVP。先闭合 Mach-O 和 ELF。

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

相关源码：

- `src/core/backend/line_map.cheng`
- `src/core/runtime/debug_runtime_stub.cheng`
- `src/core/tooling/backend_driver_main.cheng`
- `src/core/tooling/backend_driver_dispatch_min.cheng`
- `bootstrap/cheng_cold.c`
- `bootstrap/cold_parser.c`
- `bootstrap/macho_direct.h`
- `bootstrap/elf64_direct.h`
- `bootstrap/coff_direct.h`

当前没有：

- DWARF section writer。
- `.debug_line` 行表。
- `.debug_info` compile unit / subprogram DIE。
- 局部变量 location list。
- Cheng 类型 DIE。

## 2. 方案选择

### 方案 A：DWARF + 现成调试器

Emit DWARF，使用 `lldb` / `gdb`。

优点：

- 直接获得断点、单步、回溯、watchpoint、变量查看。
- Mach-O 和 ELF 都有成熟支持。
- 不需要 Cheng runtime 参与断点协议。

结论：主线方案。

### 方案 B：自定义调试协议

实现 ptrace/debug stub/JSON-RPC 调试前端。

问题：

- 平台成本高。
- 变量、类型、表达式仍要一套调试元数据。
- 在 DWARF 证明不足前没有必要。

结论：不进入近期计划。

### 方案 C：GDB remote stub

适合裸机或嵌入式目标。

结论：未来可单独立项，不作为桌面开发体验主线。

## 3. DWARF MVP

目标：`lldb` 能做到：

- `break set -n main`
- `break set -f file.cheng -l 42`
- `run`
- `bt`
- `source list`

MVP 只做函数和行号，不做局部变量。

必需 section：

- `.debug_abbrev`
- `.debug_info`
- `.debug_line`
- `.debug_str`

Mach-O 映射：

- `__DWARF,__debug_abbrev`
- `__DWARF,__debug_info`
- `__DWARF,__debug_line`
- `__DWARF,__debug_str`

ELF 映射：

- `.debug_abbrev`
- `.debug_info`
- `.debug_line`
- `.debug_str`

## 4. 数据流

当前需要补一条显式数据链：

```
source file
  -> ColdFunctionSymbol { name, source_span, line, has_body }
  -> Fn debug metadata { symbol, low_pc, high_pc, file, line }
  -> DWARF line table + subprogram DIE
  -> object/executable writer
```

新增结构建议：

```c
typedef struct {
    Span name;
    Span source_path;
    int32_t line_start;
    int32_t line_end;
    uint64_t low_pc;
    uint64_t high_pc;
    int32_t symbol_index;
} ColdDebugFunction;

typedef struct {
    ColdDebugFunction *functions;
    int32_t function_count;
    Span compile_unit_path;
} ColdDebugUnit;
```

约束：

- `low_pc/high_pc` 必须来自最终 code layout。
- 行号必须来自源码扫描，不允许按函数顺序猜。
- object writer 和 executable writer 使用同一份 debug unit。
- 如果 debug metadata 缺失，`--debug=dwarf` 必须失败；普通无调试构建不写 DWARF。

## 5. 实现步骤

### Phase 1：函数级 DWARF

改动文件：

| 文件 | 改动 |
|---|---|
| `bootstrap/cold_parser.c` | 输出稳定 `ColdDebugFunction` 元数据 |
| `bootstrap/cheng_cold.c` | 收集 code offset/size，生成 DWARF section buffer |
| `bootstrap/macho_direct.h` | 写 `__DWARF` segment/sections |
| `bootstrap/elf64_direct.h` | 写 `.debug_*` section headers 和内容 |
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
- 无 DWARF 构建产物字节不变，避免影响 release 默认路径。

### Phase 2：局部变量

目标：`lldb frame variable` 能看到简单局部变量。

新增内容：

- `DW_TAG_formal_parameter`
- `DW_TAG_variable`
- `DW_AT_location`
- 栈槽到变量名的映射。

先只支持稳定栈槽变量：

- `int32`
- `int64`
- `bool`
- payloadless enum tag

寄存器临时值、移动后的变量、跨 block location list 放到后续。

### Phase 3：类型信息

目标：让调试器理解 Cheng 基础类型和复合类型。

类型映射：

| Cheng | DWARF 表示 |
|---|---|
| `bool` | base type, 1 byte |
| `int32` / `uint32` | base type, 4 bytes |
| `int64` / `uint64` | base type, 8 bytes |
| `str` | 24 字节 structure |
| `T[N]` | array type |
| `T[]` | 16 字节 sequence header，元素类型另记 |
| `object` / `tuple` | structure type |
| payloadless enum | enumeration 或 int32 tag |
| ADT/tagged union | tag + payload structure |

注意：

- no-pointer 生产表面禁裸指针；DWARF 里出现 pointer 只代表内部 ABI 或 FFI 边界，不代表用户公开语法可用。
- 泛型按单态化后的具体类型输出。

## 6. 和当前 line-map 的关系

line-map 继续保留，服务 Cheng 自有诊断：

- panic trace
- bounds trace
- signal trace
- debug-report
- 编译报告中的 `debug_line_map_path`

DWARF 服务外部调试器。两者可以共存，但验收分开：

- line-map 看 Cheng 工具命令和运行时诊断。
- DWARF 看 `dwarfdump`、`lldb`、`gdb`。

## 7. 不做的事

MVP 不做：

- 自定义调试 UI。
- 自定义 debug wire protocol。
- Cheng 表达式求值。
- ownership/ORC 可视化。
- COFF/Windows debug info。
- `lldb` Cheng 语法插件。

## 8. 总结

先做 Phase 1。它和当前 line-map 不冲突，能用最少实现换来真实断点和源码回溯。

完成标准不是“写入了几个 section”，而是：

- `dwarfdump` 能解析。
- `lldb` 能按函数和源码行断点。
- 报告能说明是否启用 DWARF。
- release 默认路径不受影响。
