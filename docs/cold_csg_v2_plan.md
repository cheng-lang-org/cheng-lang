# cold_csg_v2 单一执行方案

目标：Cheng 完整编译器负责源码语义，产出 canonical `PrimaryObjectPlan/BodyIR` facts；`cheng_cold --csg-in` 只负责加载校验、object emit、linkerless 生成。毫秒级只统计 cold 后端，不把源码解析、typed、lowering、primary 生成算进去。

## 结论

这条路线可以实现毫秒级后端，但前提是 facts 已经存在，并且 load/verify/file I/O 被单独计量。不能把 “facts 在内存里” 当成默认前提。

```text
Cheng full compiler:
  source -> parser -> typed_expr -> lowering -> PrimaryObjectPlan -> cold_csg_v2 facts

cheng_cold --csg-in:
  mmap/load facts -> verify schema/ABI/hash -> emit object -> linkerless executable
```

当前先修稳定：

```text
PrimaryObjectPlan -> direct object -> native link -> run
```

只有 direct object 主线稳定，facts 往返才有可信语义参照。

## 不变量

- cold 不做语义 lowering、不做类型推导、不做 import 闭包扩展。
- facts 必须完整描述生成 `.o` 所需信息：type layout、slot、block、op、term、call、data、reloc、external symbols。
- provider/runtime 不在 facts reader 中猜测。只允许两种策略：显式预编译 archive，或 provider 自身也有独立 facts。
- 缺字段、越界、未知 record、ABI 不匹配、hash 不一致、未解析符号全部直接失败。
- 只维护本文件一个方案文档，避免多文档漂移。

## facts 格式

`cold_csg_v2` 使用长度前缀记录，不使用 tab 分隔、JSON、源码字符串重扫。

格式边界：

- canonical facts 使用文本 magic `CHENG_CSG_V2`，来源是 Cheng full compiler 的 `PrimaryObjectPlan`。
- 内部 BodyIR 快照使用二进制 magic `CHENGCSG`，只用于 cold 自身 fast-path 自检，不能算 canonical Phase 0 完成。
- `CHENGCSG` 函数记录必须显式保存参数 ABI 三元组 `slot_kind/slot_size/param_slot`，reader 禁止把参数顺序猜成 slot 编号。
- `CHENGCSG` block 记录必须显式保存 `block_term`，reader 禁止用 block 下标猜 term 下标。
- legacy v1 只能在文件显式以 `cold_csg_version=1` 开头时读取，禁止按“非 v2 就当 v1”猜格式。

头部：

| 字段 | 含义 |
|---|---|
| magic | 固定格式标识 |
| schema_version | 当前为 `2` |
| target_triple | 目标平台 |
| pointer_width | 指针宽度 |
| endian | 字节序 |
| abi_version | 调用约定和对象布局版本 |
| producer_version | 生成器版本 |
| schema_hash | schema hash |
| plan_hash | PrimaryObjectPlan 内容 hash |
| entry_symbol | 入口符号 |

record：

| record | 内容 |
|---|---|
| module | module path、source path、import edge |
| symbol | 函数、数据、外部符号、导出名、所属模块 |
| type_layout | kind、size、align、field offset、variant tag/payload |
| function | symbol id、frame size、param ABI、return ABI、body range |
| slot | slot id、kind、size、align、frame offset、param/local |
| block | block id、op range、term id |
| op | canonical BodyIR op、typed operands、result slot |
| term | return、branch、cbr、switch、unreachable |
| call | target symbol、arg ABI、result slot、reloc kind |
| data | string bytes、constant bytes、alignment、reloc |
| reloc | offset、target symbol、reloc type、addend |
| provider_input | archive path/hash 或 provider facts path/hash |
| debug_map | source path、line、function、instruction range |

编码规则：

- 所有字符串：`u32 byte_len + bytes`
- 所有数组：`u32 count + records`
- 所有 record：`u16 record_kind + u32 record_size + payload`
- reader 先校验长度，再读取字段
- record 顺序固定；跨 section 引用只用 id，不用文本查找

## facts 大小和 load 预算

backend driver 当前量级：1059 个函数、16796 个 op word。若每个 op 固定 20 字节，op 区就是：

```text
16796 * 20 = 335920 bytes
```

加上 slot、block、term、call、reloc、string、data、debug map，facts 文件进入 MB 级是正常风险，不能假设免费。

预算先按真实文件冷启动计：

| 输入 | facts_bytes | mmap/load/verify | emit obj | 说明 |
|---|---:|---:|---:|---|
| `ordinary_zero_exit_fixture` | <= 32KB | <= 1ms warm，<= 5ms cold file | <= 2ms | 阶段 0 卡口 |
| combined kernel | <= 512KB | <= 3ms warm，<= 10ms cold file | <= 10ms | 结构扩展卡口 |
| backend driver | <= 2MB | <= 8ms warm，<= 20ms cold file | <= 60ms | 20-80ms 总后端目标 |

报告必须拆字段：

```text
facts_bytes
facts_mmap_ms
facts_verify_ms
facts_decode_ms
facts_emit_obj_ms
facts_emit_exe_ms
facts_total_ms
```

若任一规模超预算，先改格式密度或 reader 结构，再继续扩大覆盖。可选优化是 symbol/string interning、op section dense array、varint/delta encoding、按 section hash 校验。

## provider/link 策略

provider 有自举循环风险：provider 是 Cheng 源码编出来的，若 `--csg-in` 执行时再现场编 provider，就回到完整 Cheng 编译器依赖。

本方案固定两条合法路径：

| 策略 | 用途 | 要求 |
|---|---|---|
| 预编译 provider archive | 第一条主线 | `provider_input` 写 archive 路径、hash、导出符号表；cold 只链接 archive 中已存在 `.o` |
| provider facts | 后续扩展 | provider 也由 Cheng full compiler 预先导出 facts；cold 分别 materialize provider `.o` 后再 link |

阶段 0 到 backend driver 打通前，默认采用预编译 provider archive。这样 `--csg-in` 不依赖现场 Cheng provider 编译，也不需要 system linker 才能闭合。

link 输入模型：

```text
primary facts -> primary.o
provider archive/facts -> provider.o list
external symbols -> must resolve in provider.o or platform import table
reloc -> cold linkerless/object linker applies reloc
```

未解析符号、archive hash 不匹配、provider 导出表缺项都直接失败。

## 执行阶段

| 阶段 | files | action | verify | done |
|---|---|---|---|---|
| 阶段 0：最小 facts 往返 | `src/core/backend/primary_object_plan.cheng`、`bootstrap/cheng_cold.c` | 只用 `ordinary_zero_exit_fixture` 导出最小 facts，cold 读入后连续两次生成 `.o` | `facts -> cold reader -> cold object writer -> .o` 两次输出 bit-identical；坏 schema/截断输入直接失败；记录 facts 大小和 load 时间 | 确认格式和 object writer 可行，不再纸面设计 |
| 阶段 1：schema 固化 | `docs/cold_csg_v2_plan.md`、后续 schema 常量文件 | 固定 header、record kind、字段顺序、hash、错误码 | 截断、错版本、错 ABI、未知 record、hash drift 全部失败 | schema 不依赖分隔符或 JSON |
| 阶段 2：writer 扩展 | `src/core/backend/primary_object_plan.cheng` | 从 canonical PrimaryObjectPlan 导出 function/slot/block/op/term/call/data/reloc | 同一输入重复导出 hash 一致；record 数与 plan/report 一致 | facts 覆盖 `.o` 所需全部信息 |
| 阶段 3：reader 结构化 | `bootstrap/cheng_cold.c` | `--csg-in` reader 直接构造 dense sections，不做文本查找 | `facts_mmap_ms/facts_verify_ms/facts_decode_ms` 写 report；预算不过直接失败 | load 成本可见 |
| 阶段 4：object 合同验证 | `bootstrap/cheng_cold.c`、`src/core/backend/primary_object_plan.cheng` | `--csg-in --emit:obj` 覆盖更多 fixture，验证 cold 自身确定性和运行语义 | 同一 facts 多次生成 `.o` bit-identical；导出符号、未解析外部符号、reloc 目标集合稳定；链接同一 provider archive 后运行 marker 正确 | 不要求 Cheng direct `.o` 与 cold `.o` 字节一致 |
| 阶段 5：provider archive 生成 | `bootstrap/cheng_cold.c` | `provider-archive-pack` 支持多个预编译 provider `.o` 和多个 export 写入 `.chenga` | archive hash、member count、导出表写 report；缺 export hard-fail | 多 ELF member/export 已入门禁；runtime provider roots 未接入 |
| 阶段 6：provider archive link | `bootstrap/cheng_cold.c` | `--link-object`、`--csg-in` 和 `--provider-archive` 组合在 cold 内部读取 object/archive 并生成 exe | `system_link=0`、`linkerless_image=1`、`unresolved_symbol_count=0`、`provider_resolved_symbol_count=2`；坏 magic/缺导出 hard-fail | AArch64/RISC-V ELF 最小链路闭合，不调用 `cc` 或 `--link-providers` |
| 阶段 7：linkerless exe | `bootstrap/cheng_cold.c` | `--csg-in --emit:exe` 写 Mach-O/ELF/COFF 可执行布局 | ordinary、import_use、cold_subset、combined kernel 真实运行 marker | 不依赖系统 linker |
| 阶段 8：backend driver fixed-point | `artifacts/bootstrap/cheng.stage3`、`artifacts/backend_driver/cheng` | A 产 facts，cold 编 B；B 再产 facts，cold 编 C | B/C report 关键字段一致，facts hash 或等价 hash 稳定，smoke 全过 | 可替代对应 seed/link 路径 |
| 阶段 9：删除 cold 前端 | `bootstrap/cheng_cold.c` | 删除 parser/type/import source-direct 模块 | `--csg-in` fixed-point 和运行回归连续稳定 | 删除后无 source-direct 依赖 |

## 当前实测

- `bootstrap/cheng_cold.c` 已实现完整 CSG v2 reader/writer，支持 `--emit:csg-v2`（独立命令）和 `--csg-in`（BodyIR + 指令字两种格式）。
- 当前自动卡口是 `tools/cold_csg_v2_roundtrip_test.sh`：backend driver 产 `CHENGCSG` section-binary facts，cold reader 连续两次产 `.o`，`cmp` 确定性通过，并验证 writer/reader report、facts 预算、unknown/truncated hard-fail、provider archive 多 member/export、`--csg-in --provider-archive`。
- `CHENG_CSG_V2` text object facts reader 仍保留；它是 PrimaryObjectPlan 指令字 object facts 入口。当前 280B smoke 主线使用 `CHENGCSG` section-binary，不能把两者的验证结论混写。
- **fixed-point 验证通过**：C writer（`cheng_cold.c`）和 Cheng writer（`primary_object_plan.cheng`）对同一输入产出 **bit-identical** 的 facts 文件（MD5 一致），cold reader 分别消费后产出 **bit-identical** 的可执行文件。
- `ordinary_zero_exit_fixture`: facts 280 bytes, exe 0.13-0.19ms, exit 0（codesign 后）。
- `cold_subset_coverage`: facts 19,666 bytes (23 函数/411+ ops), exe 0.15-0.22ms, MD5 一致。
- `--csg-in --emit:obj`: 两次产出 bit-identical, `_main` 全局符号, cc 链接后运行 exit 0。
- `--csg-in --emit:exe --provider-archive`: 两个 provider ELF member、两个 export、两个 resolved symbol，`system_link=0`、`linkerless_image=1`。
- report 字段：`facts_bytes`, `facts_mmap_ms`, `facts_verify_ms`, `facts_decode_ms`, `facts_total_ms`, `facts_function_count` 全部输出。
- 三架构 codegen 100% op 覆盖（ARM64/x86_64/RISC-V），全部通过自检。
- `build-backend-driver` 已完成，产出 `artifacts/backend_driver/cheng`，contract hash `41cf6b574eae643f`。
- `tools/cold_regression_test.sh` 43/43 PASS；`tools/cold_csg_v2_roundtrip_test.sh` 53/53 PASS。

## 验收命令

当前先验收 direct object 主线：

```sh
artifacts/bootstrap/cheng.stage3 build-backend-driver
artifacts/backend_driver/cheng run-host-smokes cheng_skill_consistency_smoke
artifacts/backend_driver/cheng run-host-smokes perf_memory_contract_smoke
```

阶段 0 新增命令：

```sh
tools/cold_csg_v2_roundtrip_test.sh
```

手工展开：

```sh
artifacts/backend_driver/cheng emit-cold-csg-v2 --root:/Users/lbcheng/cheng-lang --in:tests/cheng/backend/fixtures/return_let.cheng --out:/tmp/cheng_csg_v2_zero.facts --report-out:/tmp/cheng_csg_v2_zero.writer.report.txt
bootstrap/cheng_cold --system-link-exec --csg-in:/tmp/cheng_csg_v2_zero.facts --emit:obj --out:/tmp/cheng_csg_v2_zero.cold.1.o --target:arm64-apple-darwin --report-out:/tmp/cheng_csg_v2_zero.reader.1.report.txt
bootstrap/cheng_cold --system-link-exec --csg-in:/tmp/cheng_csg_v2_zero.facts --emit:obj --out:/tmp/cheng_csg_v2_zero.cold.2.o --target:arm64-apple-darwin --report-out:/tmp/cheng_csg_v2_zero.reader.2.report.txt
cmp /tmp/cheng_csg_v2_zero.cold.1.o /tmp/cheng_csg_v2_zero.cold.2.o
```

这组命令要求 `artifacts/backend_driver/cheng` 已由包含 `emit-cold-csg-v2` 的源码刷新。刷新前只运行 cold reader/object writer 的最小 facts smoke。

当前最小 provider archive 链路已允许运行验证；runtime provider archive 接入后再跑真实 runtime roots：

```sh
bootstrap/cheng_cold --link-object:/tmp/cheng_csg_v2_zero.cold.1.o --provider-archive:/tmp/cheng_provider_runtime.chenga --emit:exe --out:/tmp/cheng_csg_v2_zero.1 --target:arm64-apple-darwin
bootstrap/cheng_cold --link-object:/tmp/cheng_csg_v2_zero.cold.2.o --provider-archive:/tmp/cheng_provider_runtime.chenga --emit:exe --out:/tmp/cheng_csg_v2_zero.2 --target:arm64-apple-darwin
/tmp/cheng_csg_v2_zero.1
/tmp/cheng_csg_v2_zero.2
```

linkerless 接入后：

```sh
bootstrap/cheng_cold --csg-in:/tmp/cheng_csg_v2_zero.facts --emit:exe --out:/tmp/cheng_csg_v2_zero --target:arm64-apple-darwin --provider-archive:/tmp/cheng_provider_runtime.chenga
/tmp/cheng_csg_v2_zero
```

## 完成定义

- 只有本文件一个 cold CSG v2 方案文档。
- 阶段 0 的 cold `.o` 重复生成确定性对拍通过。
- facts 文件大小和 load/verify 时间在 report 中可见。
- `--csg-in` 不触发源码 parser、typed_expr、semantic lowering。
- provider/runtime 必须通过显式 archive 或 provider facts 输入闭合；当前已闭合多 member/export ELF fixture。
- provider archive/link 命令已存在；Darwin runtime marker hard-fail 已锁；runtime provider roots 还未完成。
- 未解析外部符号直接失败。
- 真实可执行文件运行通过，不接受 compile-only。
- fixed-point 通过后才允许删除 cold 前端。
