# 纯 Cheng 直接对象文件后端计划

## 结论

当前 `primary_object_emit` 生成汇编文本，再调用系统汇编器生成对象文件。这条路径可以作为自举验证基线，但不能继续扩成最终后端。

最终路线是：纯 Cheng 从 MIR/后端计划直接写平台对象文件，热路径只处理结构化字节 buffer、符号、重定位、section 和 line map，不再把机器指令转成汇编字符串。

## 目标

- 纯 Cheng 直接生成平台对象文件：
  - macOS / iOS: Mach-O `.o`
  - Linux / Android / OHOS: ELF `.o`
  - Windows: COFF `.obj`
- 编译时性能不低于当前 C seed 可达的对象生成路径。
- 运行时性能不低于同等语义下 C 后端生成的机器码。
- `cheng_seed.c` 只保留最小自举外根，不再承载新后端语义。
- `Fmt`、`Lines` 只用于诊断、报告和测试输出，不进入 codegen 热路径。

## 移动端集成

移动端属于主目标，不是后续附加项。

- Android：
  - Cheng 后端直接生成 ELF relocatable `.o`，优先 arm64。
  - Android 集成交付物必须是链接后的 `.so`；NDK/Gradle 只消费 `.so`、符号表和稳定 C ABI 边界。
  - UI/生命周期/输入必须走事件驱动入口，不能用轮询主循环模拟。
- OHOS：
  - Cheng 后端直接生成 ELF relocatable `.o`。
  - 鸿蒙集成交付物必须是链接后的 `.so`；Ark/Native bridge 只绑定 `.so` 导出符号。
  - 平台壳只负责装载 `.so`、生命周期和事件分发，不把后端语义塞回宿主脚本。
- iOS：
  - 目标对象格式走 Mach-O `.o`，arm64 与 simulator slice 分开验证。
  - Xcode 集成只接对象文件、module map/头文件和资源 manifest；Cheng 后端不生成临时 ObjC/C 壳作为生产路径。
- 移动端公共约束：
  - 统一使用 Cheng runtime 的 no-pointer 公共 ABI 和显式 handle 边界。
  - mobile shell smoke 必须覆盖对象生成、链接、启动、事件分发和内存泄漏统计。
  - direct object writer 默认启用前，Android/OHOS/iOS 至少各保留一个最小集成 smoke。

## 非目标

- 不继续扩大汇编文本后端的语义覆盖面。
- 不把新平台指令选择、对象布局、重定位语义补进 `cheng_seed.c`。
- 不用字符串后处理修补汇编。
- 不引入 C 脚本、外部脚本或 mock 数据作为生产链路。
- 不通过跳过验证、降级到旧路径、缓存假命中来声明完成。

## 文件拆解

第一阶段只新增纯 Cheng writer 模块，不替换现有路径。

- `src/core/backend/object_buffer.cheng`
  - 管理 `byte[]` 写入、对齐、patch 点和 section 局部 offset。
- `src/core/backend/object_symbols.cheng`
  - 管理符号表、局部符号、全局符号、未定义符号和导出名。
- `src/core/backend/object_relocs.cheng`
  - 管理重定位记录，按目标格式映射 relocation kind。
- `src/core/backend/macho_object_writer.cheng`
  - 先实现 arm64 Mach-O `.o`。
- `src/core/backend/elf_object_writer.cheng`
  - arm64 ELF 后续实现。
- `src/core/backend/coff_object_writer.cheng`
  - COFF 后续实现。
- `src/core/backend/direct_object_emit.cheng`
  - 统一入口：从 `PrimaryObjectPlan` 选择目标 writer。
- `src/tests/direct_object_writer_smoke.cheng`
  - 最小 return-zero、符号、重定位、line map 对拍。

## 数据结构

热路径只允许结构化数据，不允许汇编字符串作为中间表示。

```cheng
type
    ObjectSection =
        name: str
        align: int32
        bytes: byte[]
        relocStart: int32
        relocCount: int32

    ObjectSymbol =
        name: str
        sectionIndex: int32
        offset: int64
        size: int64
        global: bool
        undefined: bool

    ObjectReloc =
        sectionIndex: int32
        offset: int64
        symbolIndex: int32
        kind: int32
        addend: int64

    ObjectFilePlan =
        targetTriple: str
        sections: ObjectSection[]
        symbols: ObjectSymbol[]
        relocs: ObjectReloc[]
```

后续真实代码可以调整字段名，但必须保持这四类核心表：section、symbol、reloc、byte buffer。

## 迁移顺序

1. 保留现有 `.s` 路径作为基线。
2. 新增 Mach-O arm64 object writer，只覆盖最小函数：
   - `.text`
   - `_main` / Cheng 函数符号
   - return-zero
   - return-call BR26 relocation
   - line map 结束标记
3. 用同一 `PrimaryObjectPlan` 同时生成：
   - 汇编路径对象文件
   - direct object writer 对象文件
4. 对拍：
   - 可执行运行结果一致。
   - 必要符号存在。
   - relocation 数量和目标一致。
   - line map 可读取。
5. 迁移 float/int 基础指令编码：
   - integer add/sub/mul/div/compare
   - float add/sub/mul/div/compare
   - load/store
   - branch/call/ret
6. 迁移 entry bridge、runtime hook、profile hook。
7. 迁移 ELF arm64。
8. 迁移 COFF。
9. 默认路径切到 direct object writer。
10. 字符串汇编路径只保留为诊断对拍入口，不能作为生产 fallback。

## 性能约束

- codegen 热路径禁止使用字符串拼接、`Fmt` 拼接和汇编文本。
- 指令编码必须直接写入 `byte[]`。
- section layout 必须一次规划，避免反复搬移大 buffer。
- relocation patch 必须通过 offset 表回填，不能扫文本。
- 大数组写入必须预估容量并 `reserve`。
- 编译内存使用必须纳入 `perf_memory_contract_smoke`。
- direct object writer 的对象生成耗时必须进入 report：
  - `exec_phase_direct_object_layout_ms`
  - `exec_phase_direct_object_emit_ms`
  - `direct_object_bytes`
  - `direct_object_reloc_count`

## 正确性门禁

- `build-backend-driver` 必须通过。
- `float64_mul_backend_smoke` 必须通过，并确认生成机器码包含真实 float 指令。
- 新增 direct writer smoke：
  - `direct_object_return_zero_smoke`
  - `direct_object_symbol_reloc_smoke`
  - `direct_object_line_map_smoke`
  - `direct_object_float64_smoke`
- 对拍门禁不能走降级：
  - direct writer 失败就失败。
  - 不允许自动回到 `.s` 路径后返回成功。
  - 不允许复用陈旧对象缓存。

## 完成标准

- macOS arm64 direct object writer 默认启用。
- `primary_object_asm_write_ms` 和 `primary_object_asm_compile_ms` 不再出现在默认生产路径。
- `cheng_seed.c` 不新增后端语义。
- 同一 smoke 在 direct object writer 和汇编基线下运行结果一致。
- 编译内存和耗时报告稳定，且没有泄漏增长。
- 字符串汇编路径只作为显式诊断命令存在。

## 下一步

当前已完成 `object_buffer.cheng`、`object_symbols.cheng`、`object_relocs.cheng`、最小 Mach-O arm64 writer、standalone zero-exit、return-zero `emit:obj`、return-call BR26 relocation `emit:obj`、二参 `float64 * float64 -> float64` 对象函数，以及最小 `float64_mul_backend_smoke` 的 `fmul/fcmp` 退出码 exe。

下一步只推进真实语义 codegen：entry bridge 的普通函数返回路径、整数算术、float64 更多表达式形状，以及 `assert/array/echo` 版 smoke 的完整 lowering。未完成的语义必须继续 hard-fail，不能把表达式忽略后返回 0。
