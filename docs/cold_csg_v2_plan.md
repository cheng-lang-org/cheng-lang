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

当前已验证入口采用预编译 provider archive。这样 `--csg-in` 不依赖现场 Cheng provider 编译，也不需要 system linker 才能证明 archive 链路；`--link-providers` 已能从 primary undefined symbols 自动选择 Linux runtime roots 并生成真实 provider archive。Linux AArch64 `write/get_nprocs` 已由 cold 生成 syscall provider object 正向解析。链接报告只证明 archive/reloc 闭合；目标运行必须过 `tools/linux_aarch64_provider_runtime_smoke.sh`，缺 Linux AArch64 user-mode runner 直接失败。

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
| 阶段 5：provider archive 生成 | `bootstrap/cheng_cold.c` | `provider-archive-pack` 支持多个预编译 provider `.o` 和多个 export 写入 `.chenga` | archive hash、member count、导出表写 report；缺 export hard-fail | 多 ELF member/export 已入门禁；Linux runtime root-selective provider archive link-report smoke 已覆盖纯常量 roots 与 `write/get_nprocs` syscall-backed roots |
| 阶段 6：provider archive link | `bootstrap/cheng_cold.c` | `--link-object`、`--csg-in` 和 `--provider-archive` 组合在 cold 内部读取 object/archive 并生成 exe；`--link-providers` 从 primary undefined symbols 自动选择已支持 runtime roots | `system_link=0`、`linkerless_image=1`、`unresolved_symbol_count=0`、`provider_resolved_symbol_count>0`；坏 magic/缺导出/未知 runtime root hard-fail；Linux AArch64 runtime gate 运行 trace/cpu marker | AArch64/RISC-V ELF 最小链路闭合，不调用 `cc`；Linux AArch64 `write/get_nprocs` 已由 cold 生成真实 syscall provider object 并正向链接；目标运行需要 user-mode runner |
| 阶段 7：linkerless exe | `bootstrap/cheng_cold.c` | `--csg-in --emit:exe` 写 Mach-O/ELF/COFF 可执行布局 | ordinary、import_use、cold_subset、combined kernel 真实运行 marker | 不依赖系统 linker |
| 阶段 8：backend driver fixed-point | `artifacts/bootstrap/cheng.stage3`、`artifacts/backend_driver/cheng` | A 产 facts，cold 编 B；B 再产 facts，cold 编 C | B/C report 关键字段一致，facts hash 或等价 hash 稳定，smoke 全过 | 可替代对应 seed/link 路径 |
| 阶段 9：删除 cold 前端 | `bootstrap/cheng_cold.c` | 删除 parser/type/import source-direct 模块 | `--csg-in` fixed-point 和运行回归连续稳定 | 删除后无 source-direct 依赖 |

## 当前实测

- `bootstrap/cheng_cold.c` 已实现 CSG v2 双 reader/writer：public `emit-cold-csg-v2` 写 canonical `CHENG_CSG_V2` facts；显式 `system-link-exec --emit:csg-v2` 写 internal `CHENGCSG` self-test facts；`--csg-in` 同时消费 BodyIR 快照和 canonical 指令字 facts。
- 当前自动卡口是 `tools/cold_csg_v2_roundtrip_test.sh`：public canonical writer 覆盖 ordinary writer/read/link/run，并锁 writer report 的 `facts_bytes/facts_function_count/facts_word_count`；internal writer/reader 连续两次产 `.o` 并 `cmp` 确定性通过；同时验证 reader report、facts 预算、unknown/truncated hard-fail、provider archive 多 member/export、`--csg-in --provider-archive`、真实 backend driver canonical facts → AArch64 provider archive 直接入口。当前本地实跑结果为 859/859 PASS。
- `CHENG_CSG_V2` text object facts reader 已有最小 object smoke：`_main` 两条 ARM64 instruction word 连续两次读入生成 `.o` bit-identical，`cc` 链接后运行 exit 0。新增 public writer smoke 从 `ordinary_zero_exit_fixture` 写 canonical facts，再由 cold reader 生成 object 并链接运行 exit 0。
- **internal fixed-point 验证通过**：`CHENGCSG` section-binary facts 只保留为显式 cold self-test，锁 writer/reader 确定性；canonical `CHENG_CSG_V2` public writer 已有最小链路 smoke。完整 Cheng `PrimaryObjectPlan` 管线仍未闭合；cold_parser 的 Lowering/Primary materializer 现在必须写出明确 missing reason，不能把 859/859 结论外推为全量 PrimaryObjectPlan 管线闭合。
- `ordinary_zero_exit_fixture`: facts 280 bytes, exe 0.13-0.19ms, exit 0（codesign 后）。
- `cold_subset_coverage`: facts 19,666 bytes (23 函数/411+ ops), exe 0.15-0.22ms, MD5 一致。
- `--csg-in --emit:obj`: 两次产出 bit-identical, `_main` 全局符号, cc 链接后运行 exit 0。
- `--csg-in --emit:exe --provider-archive`: 两个 provider ELF member、两个 export、两个 resolved symbol，`system_link=0`、`linkerless_image=1`；该 smoke 只证明 archive 解析/链接边界。Linux AArch64 syscall roots 的目标运行由 `tools/linux_aarch64_provider_runtime_smoke.sh` 单独验证。
- report 字段：`facts_bytes`, `facts_mmap_ms`, `facts_verify_ms`, `facts_decode_ms`, `facts_total_ms`, `facts_function_count` 全部输出。
- 三架构 codegen 100% op 覆盖（ARM64/x86_64/RISC-V），全部通过自检。
- `build-backend-driver` 当前产物是 cold bootstrap patched-self candidate，contract hash `41cf6b574eae643f`；它不是纯 Cheng backend driver 闭合证明。纯 Cheng direct 编译 `backend_driver_dispatch_min.cheng` 已越过 `invalid string literal index`，当前由 `pure_backend_driver_direct_hard_fail` 锁住 `os.cheng_fopen/os.cheng_fflush/os.c_iometer_call` 未解析 patch 阻断点。
- `tools/cold_csg_v2_roundtrip_test.sh` 本地实跑 859/859 PASS，public `emit-cold-csg-v2` 已锁 canonical `CHENG_CSG_V2` writer/read/link/run smoke；internal `CHENGCSG` 仅作为显式 cold self-test 子门禁保留。

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
/tmp/cheng_cold system-link-exec --csg-in:/tmp/cheng_csg_v2_zero.facts --emit:obj --out:/tmp/cheng_csg_v2_zero.cold.1.o --target:arm64-apple-darwin --report-out:/tmp/cheng_csg_v2_zero.reader.1.report.txt
/tmp/cheng_cold system-link-exec --csg-in:/tmp/cheng_csg_v2_zero.facts --emit:obj --out:/tmp/cheng_csg_v2_zero.cold.2.o --target:arm64-apple-darwin --report-out:/tmp/cheng_csg_v2_zero.reader.2.report.txt
cmp /tmp/cheng_csg_v2_zero.cold.1.o /tmp/cheng_csg_v2_zero.cold.2.o
```

这组命令要求 `artifacts/backend_driver/cheng emit-cold-csg-v2` 输出 magic `CHENG_CSG_V2`。若退回 `CHENGCSG`，应视为 public writer 回归；`CHENGCSG` 只允许出现在显式 cold self-test。

当前最小 provider archive 链路已允许运行验证；runtime provider 纯常量 roots 已锁链接报告，Linux AArch64 `write/get_nprocs` 已由 cold syscall provider object 正向链接。Linux AArch64 syscall roots 的真实运行验证命令：

```sh
tools/linux_aarch64_provider_runtime_smoke.sh
CHENG_LINUX_AARCH64_RUNNER=/path/to/qemu-aarch64 tools/linux_aarch64_provider_runtime_smoke.sh
```

脚本会编译并运行 `runtime_provider_autolink_trace` 与 `runtime_provider_autolink_cpu_cores`，检查 provider archive report、trace stderr marker 和 cpu cores 非零退出码。native Linux AArch64、`qemu-aarch64`/`qemu-aarch64-static` 或显式 `CHENG_LINUX_AARCH64_RUNNER` 算合法 runner；`qemu-system-aarch64` 不能直接执行 Linux 用户态 ELF，不作为兜底。

Darwin provider archive 示例：

```sh
/tmp/cheng_cold system-link-exec --link-object:/tmp/cheng_csg_v2_zero.cold.1.o --provider-archive:/tmp/cheng_provider_runtime.chenga --emit:exe --out:/tmp/cheng_csg_v2_zero.1 --target:arm64-apple-darwin
/tmp/cheng_cold system-link-exec --link-object:/tmp/cheng_csg_v2_zero.cold.2.o --provider-archive:/tmp/cheng_provider_runtime.chenga --emit:exe --out:/tmp/cheng_csg_v2_zero.2 --target:arm64-apple-darwin
/tmp/cheng_csg_v2_zero.1
/tmp/cheng_csg_v2_zero.2
```

linkerless 接入后：

```sh
/tmp/cheng_cold system-link-exec --csg-in:/tmp/cheng_csg_v2_zero.facts --emit:exe --out:/tmp/cheng_csg_v2_zero --target:arm64-apple-darwin --provider-archive:/tmp/cheng_provider_runtime.chenga
/tmp/cheng_csg_v2_zero
```

## 完成定义

- 只有本文件一个 cold CSG v2 方案文档。
- 阶段 0 的 cold `.o` 重复生成确定性对拍通过。
- facts 文件大小和 load/verify 时间在 report 中可见。
- `--csg-in` 不触发源码 parser、typed_expr、semantic lowering。
- provider/runtime 必须通过显式 archive 或 provider facts 输入闭合；当前已闭合多 member/export ELF fixture，并锁住 Linux runtime roots 的链接报告，含 `write/get_nprocs` syscall-backed roots。
- provider archive/link 命令已存在；Darwin runtime marker hard-fail 已锁；Linux runtime provider 纯常量 root 集已自动归档链接；Linux AArch64 syscall root 运行 marker 必须通过 `tools/linux_aarch64_provider_runtime_smoke.sh`，缺 user-mode runner 直接失败。
- 未解析外部符号直接失败。
- 真实可执行文件运行通过，不接受 compile-only。
- fixed-point 通过后才允许删除 cold 前端。
