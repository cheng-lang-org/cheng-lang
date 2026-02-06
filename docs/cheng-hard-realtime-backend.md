# Cheng 硬实时后端生产闭环（落地版）

本文结合 `docs/cheng-formal-spec.md` 与 `docs/hrt-profile-v1.md` 的硬实时约束，给出 Cheng **硬实时（Hard RT）汇编后端**的可执行闭环方案。核心思想是：**AOT 编译 + 可界定 WCET + 无 GC + 运行时可控**，并提供严格的“实时子集（HRT Profile）”与可验证的编译/链接闭环。

---

## 1. 设计目标

- **确定性**：可界定 Worst‑Case Execution Time（WCET），无不可控暂停。
- **无动态不确定性**：禁用 GC/JIT/动态反射/不可界定 I/O。
- **可验证**：静态检查 + 运行时最小化 + 任务级验收。
- **可移植**：支持 RTOS/裸机（FreeRTOS/Zephyr/RTEMS/裸机），并可在 macOS 上模拟验证。

非目标：
- 作为通用桌面后端或复杂动态语言运行时。
- “无需编译”的运行模式（硬实时本质仍需 AOT 编译）。

---

## 2. 总体方案概览

**新增编译后端：HRT Backend（直接 Cheng -> ASM / OBJ）**  
直接生成目标 ISA/ABI 的汇编或目标文件。

管线建议：

1. 前端：解析/语义/单态化/所有权  
2. HRT 约束验证（`hrt_verify` pass）  
3. HRT IR（低级可控 IR）  
4. 目标码生成（ASM/OBJ）  
5. 运行时链接（`hrt_runtime`，最小化）  

编译入口建议：

```
chengc --backend:hrt --target:<triple> --abi:<darwin|elf|coff> --hrt-verify
```

或新增模式：

```
bin/cheng --mode:hrt --file:<input.cheng> --out:<output.s>
```

---

## 2.0 生产闭环总览（可执行版）

闭环定义：**规范 → 编译 → 链接 → 验证 → 交付**。每一步必须有可复现命令与产物。

**闭环输入**：
- 源码（`*.cheng`）
- 目标三元组/ABI（`--target/--abi` 或 `CHENG_ASM_TARGET/CHENG_ASM_ABI`）
- 运行约束配置：`configs/hrt_runtime_constraints.env`
- 目标工具链与 SDK/NDK/RTOS 启动文件

**闭环门禁（必须全绿）**：
- HRT 语法/语义门禁：`sh src/tooling/verify_hrt_backend.sh`
- ASM 后端门禁：`sh src/tooling/verify_asm_backend.sh --fullspec --hrt`
- 运行约束门禁：`sh src/tooling/verify_hrt_runtime_constraints.sh --config:configs/hrt_runtime_constraints.env`
- 目标链接门禁：`sh src/tooling/asm_*_e2e.sh ...`（P0 目标）
- 一键总门禁：`./verify.sh`（CI 基线）

**闭环产物**：
- HRT 汇编：`chengcache/hrt/*.s`
- 运行时库：`chengcache/hrt_runtime/libhrt_runtime.a`
- 目标产物：`chengcache/<target>/*.{s,o,bin}`
- 归档：`artifacts/hrt/<target>/<build_id>/`（manifest + logs）

**闭环成功判据**：
- `./verify.sh` 输出 `verify ok`
- P0 目标至少 1 条“编译+链接”记录，产物与 `manifest.json` 齐全

---

## 2.0.1 非 HRT C 后端说明

直出 C、软实时（SRT）与 C+ASM 混合后端的统一规范已迁移到 `docs/cheng-direct-c-profile.md`，HRT 文档仅保留硬实时约束与闭环说明。

---

## 2.0.2 非 HRT 的 C + ASM 混合后端（建议方案）

目标：**主后端走 C，热点路径用 ASM**，获得接近手写汇编的性能，同时保持可移植与可维护性。  
这不是硬实时（HRT），也不要求 HRT 语法约束。

**核心思路**
- 90% 逻辑由 C 后端生成（可移植、易调试）。
- 关键热点（DSP/密集循环）提供 ASM 实现，按目标平台替换。
- 通过**稳定 ABI + 符号覆盖**实现“ASM 优先、C 回退”。

**建议落地路径**
1. **ABI/符号规范**：为热点函数定义稳定签名与命名规则（C/ASM 一致）。
2. **C 参考实现**：每个热点提供 C 版本，保证功能正确且可回退。
3. **ASM 覆盖实现**：按 target/ISA 提供 `*.S`，链接时优先选择。
4. **构建与选择**：构建系统按目标平台选择 ASM 目录；缺失则自动回退 C。
5. **验证与基准**：引入 checkasm 类基准与一致性验证，产出性能对比记录。

**闭环入口（最小可验证）**
- 功能与链接验证：`sh src/tooling/verify_c_backend_hybrid.sh`
- 一致性/指标记录：`sh src/tooling/verify_mixed_backend.sh --out:artifacts/hybrid/metrics.env`
备注：`verify_mixed_backend.sh` 默认使用 `./stage1_runner` 作为 C 前端（可用 `CHENG_C_FRONTEND` 覆盖）。

**优点**
- 不牺牲生态与平台适配；热点可局部极致优化。
- 易于渐进迁移：先 C 跑通，再逐个用 ASM 替换热点。

---

## 2.1 当前实现状态（2026-01-24）

已落地的能力（stage0c core）：

- **入口与管线**：
  - `bin/cheng --mode:hrt` / `chengc.sh --emit-hrt` 可用。
  - HRT/ASM 入口只依赖 stage0c（`bin/cheng`），不依赖 `stage1_runner`；`stage1_runner` 仅用于非 asm 的直出 C 管线。
  - `verify_hrt_backend.sh` 与 `verify_asm_backend.sh --hrt/--fullspec` 已接入生产闭环。
- **代码生成范围（子集）**：
  - 函数序言/栈帧、参数传递、局部变量、if/while/for-range、break/continue、直接函数调用。
  - `for` 仅支持数值范围（`a .. b` / `a ..< b`），不支持 seq/str/table 迭代。
- **目标组合（已验证）**：
  - `x86_64-apple-darwin`、`aarch64-unknown-elf`、`riscv64-unknown-elf`。
- **目标平台优先级（P0，需优先闭环）**：
  - macOS：`x86_64-apple-darwin` / `aarch64-apple-darwin`（ABI=darwin）
  - Windows：`x86_64-pc-windows-msvc`（ABI=coff）
  - Android：`aarch64-linux-android`（ABI=elf）
  - iOS：`aarch64-apple-ios`（ABI=darwin）
  - HarmonyOS：`aarch64-unknown-ohos`（ABI=elf）
- **目标平台扩展（P1，硬实时/嵌入式优先）**：
  - 裸机/RTOS：`aarch64-unknown-elf`、`riscv64-unknown-elf`
  - CI 基线：`x86_64-unknown-linux-gnu`（用于工具链验证，已纳入非 RTOS 目标矩阵默认项）
- **HRT 约束检查（已实现）**：
  - 禁止 runtime/importc 调用。
  - 禁止动态字面量（seq/table）。
  - 禁止动态容器类型（`seq/str/cstring/Table` 与非固定容量 `seq_*/Table_*`）；固定容量 `seq[T,N]`/`str[N]`/`Table[V,N]` 例外。
  - 禁止递归（基于可达调用图）。
  - 循环必须有界：`while` 需 `@bound(N)`；`for` 需常量范围或 `@bound`。
- **HRT Profile 规范（已补齐）**：
  - `docs/hrt-profile-v1.md` 已扩展为全语法 allow/reject 矩阵。
  - `docs/hrt-verify-diagnostics-map.md` 已补齐诊断映射与阶段说明。
- **运行约束与部署模板（已实现）**：
  - `configs/hrt_runtime_constraints.env` 作为最小配置模板。
  - `src/tooling/verify_hrt_runtime_constraints.sh` 作为配置校验入口。

仍未完成的部分：

- **运行时与内存模型**：固定容量容器已落地；ORC retain/release 插桩已落地；HRT v1 已禁用 ARC/ORC；RTOS 任务模型规范与调度绑定层已补齐，具体 RTOS 适配仍待完成。

### 2.1.1 文档进度（手动维护）
- 2026-01-24：新增 `std/hrt_rtos`，暴露 RTOS 同步原语与调度接口给 Cheng 层。
- 2026-01-24：补齐 RTOS 调度绑定层、同步原语清单与阻塞上界规则，提供最小任务样例说明。
- 2026-01-24：补齐 HRT ABI 基线（调用约定/寄存器/栈规则/返回与对齐）。
- 2026-01-24：补齐 HRT ABI 细节、数据段/初始化规则、类型布局规范、交叉工具链与 golden 差异策略说明。
- 2026-01-24：补齐 HRT Profile 全语法 allow/reject 矩阵、IR 降级/拒绝规则与诊断映射表。
- 2026-01-24：补齐数据段初始化顺序规则与 golden 产物/阈值说明（前置：BASE-STACK.S03）。
- 2026-01-24：补齐 `@bound/@wcet/@task` 注解语义、失败处理策略与 RTOS 任务入口说明。
- 2026-01-24：补齐 HRT 最小运行时符号集与 ABI 绑定说明（BASE-STACK.R01）。
- 2026-01-24：补齐 hrtnet/hrtfile 驱动闭环说明、WCET 适配器/模板/示例报告与 P0 目标矩阵命令/`./verify.sh` 说明。
- 2026-01-24：补齐版本策略与效果系统安全放行（P2-06/P2-07，含 `hrt_version_manifest.sh`/`verify_hrt_effects.sh`）。
- 2026-01-24：补齐 fullspec HRT v1 默认运行与 v2 用例独立验证提示（case/@task）。
- 2026-01-24：补齐非 AArch64 目标 CI 记录入口与 linker/boot 清单（P0/P1 记录索引）。
- 2026-01-24：补齐 Windows x86_64 CI 记录归档（x86_64-pc-windows-msvc）。
- 2026-01-24：补齐 RTOS AArch64/RV64 CI 编译/链接记录与归档入口说明。
- 2026-01-24：补齐 HarmonyOS AArch64 CI 编译/链接记录与归档入口。
- 2026-01-26：补齐 Linux x86_64 CI 记录归档（`hrt_e2e_matrix.sh` 支持 skip manifest；`--allow-skip` 仍保留可追溯记录）。
- 2026-01-24：非 HRT C 后端内容与进度迁移到 `docs/cheng-direct-c-profile.md`。
- 2026-01-24：补齐 SDK/NDK/RTOS 工具链模板与一键脚本/使用说明（P2-04）。
- 2026-01-24：本机 Darwin/arm64 通过模板走通 asm→obj→bin（`chengcache/hrt_template_a64_macos`）。
- 2026-01-24：补齐 iOS aarch64 CI 记录（`artifacts/hrt/aarch64-apple-ios/2026-01-24T10:32:41Z/manifest.json`）。
- 2026-01-24：补齐 HRT stdlib/HAL 允许清单与接口约定（含最小参考应用与诊断路径）。
- 2026-01-24：修复 asm fullspec 的 `_[]/sizeof` 符号路径并补充 macOS 链接 helper 说明（aarch64 本机闭环通过）。
- 2026-01-24：生成 P0 目标 golden 基线（asm，含全部 P0 目标）与门禁记录（见 `artifacts/hrt/golden/manifest.json`）。
- 2026-01-24：补齐 P2-03 调试/可观测工具链（符号映射、diff、trace 模板）与 P0 闭环记录。
- 2026-01-24：建立性能与回归基线（P1-06），新增基准脚本/指标记录/门禁策略。
- 2026-01-24：补齐最小后端优化管线开关/策略与回归用例（P2-01）。
- 2026-01-24：建立 WCET 友好优化策略与阈值策略（P2-02），补齐报告对比说明。

---

## 2.2 生产闭环缺口清单（闭环化定义）

以下缺口会阻碍“硬实时后端完整生产闭环”的覆盖面与可验证性。每一项都需要**明确工件**与**可验收标准**，否则无法形成从规范 → 编译 → 运行 → 验证 → 交付的闭环。

闭环标准：每个缺口必须落到 **规范/实现/验证/交付** 四类工件，否则只能算“愿景”而非“闭环”。

### A. 规范与子集定义缺口
- **语法/语义覆盖不完整**：pattern/case、结构体/tuple/enum、泛型/trait、宏/模板、复杂模块导入未被明确纳入/排除 HRT 子集。
  - 补齐：发布 HRT Profile 规范（允许/禁止项、替代写法、失败诊断）。
  - 现状：`docs/hrt-profile-v1.md` 已扩展到全语法 allow/reject 矩阵（见 §3.6）。
  - 验收：`hrt_verify` 对所有被列入子集的语法有明确报错或通过路径。
- **注解语义已闭合（规范）**：`@bound/@wcet/@task` 的编译期约束与运行期意义已统一定义（见 §3.7）。
  - 产出：注解语义文档 + 编译期检查规则 + 生成物映射（IR/元数据）。
  - 验收：同一段代码在不同 target 下行为一致、诊断一致。

### B. 编译与代码生成缺口
- **IR 降级/拒绝规则已补齐**：HRT 路径明确 “可降级/可放行/直接拒绝” 规则（见 §3.6.1）。
  - 现状：HRT v1/v2 的 IR 降级/拒绝策略与诊断映射已对齐。
  - 验收：失败必报错，不出现隐式回退到非 HRT 语义的路径。
- **数据段与初始化已闭合**：全局变量、常量表、字符串驻留、静态初始化时序已固化（见 §5.3）。
  - 已补齐：全局/常量初始化顺序与布局规则；链接脚本约定。
  - 验收：跨 target 可重复构建，镜像布局可比对。
- **调用约定与 ABI 细节待收敛**：已补齐 HRT v1 ABI 基线（调用约定/寄存器/栈规则/对齐/返回）；struct/tuple 返回、vararg、溢出规则仍待固化。
  - 已补齐：HRT v1 ABI 基线 + 目标对照用例；剩余规则留待 P1/P2 固化。
  - 验收：通过 ABI 回归测试（参数/返回/栈对齐）。

### C. 运行时与内存模型缺口
- **固定容量容器语义已落地**：`seq[T, N]` / `Table[V, N]` / `str[N]` 语义、边界行为与 HRT 规则已统一。
  - 产出：固定容量容器实现（内部名仍为 `seq_fixed/str_fixed/Table_fixed`）+ 单态化重写（`seq/str/Table` → 固定容量）+ HRT 允许列表。
  - 验收：动态扩容/rehash 被拒绝或不可达；固定容量链路可通过 HRT 验证。
- **ARC/ORC 约束策略已闭合**：HRT v1 禁用 ARC/ORC（`CHENG_MM=off` 或 `CHENG_ARC=0`）。
  - 产出：HRT 编译期强制检查 + 约束配置项 `HRT_CHENG_MM=off`。
  - 验收：HRT 编译时开启 ARC/ORC 即拒绝。
- **异常/故障处理规范已闭合**：`panic`/`trap`/错误码策略已统一（见 §4.3）。
  - 产出：HRT 失败处理规范（硬中断/看门狗/错误码），运行时保持最小实现。
  - 验收：panic 路径可预测，且在 WCET 预算内。

### D. WCET 工具链缺口
- **`@wcet` 与周期约束缺少强制校验**：没有统一的生成报告与合规输出。
  - 补齐：静态检查 + 报告输出（JSON/文本） + 可视化摘要。
  - 现状：已提供 v0 报告/门禁脚本，并支持从源码提取 `@wcet/@period`（仍需外部工具对接）。
  - 验收：在 CI 中强制通过（超限即失败）。
- **静态/动态工具对接未完成**：缺少与外部 WCET 工具的接口适配。
  - 补齐：IR/ASM 到工具输入格式的转换器与基准配置。
  - 验收：至少一个 target 形成“可运行 + 有报告”的完整闭环。

### E. RTOS/任务模型缺口
- **任务模型语义已闭合**：`@task/@period/@deadline/@priority` 的约束与入口规则已定义（见 §3.7 与 §4.4），调度接口仍待落地。
  - 产出：任务 ABI + 调度器绑定层 + RTOS 适配层（FreeRTOS/Zephyr/RTEMS 之一）。
  - 验收：最小任务样例在 RTOS 上可运行且满足周期约束。
- **中断与同步原语未限定**：可用的同步方式与阻塞策略不明确。
  - 补齐：允许的同步原语列表 + 阻塞上界规则。
  - 验收：阻塞调用必须带上界或被拒绝。

### F. 目标矩阵与工具链缺口
- **目标矩阵不完整**：AArch64 已有端到端链路，其余 ABI/target 仍缺编译+链接+运行验证。
  - 补齐：目标矩阵清单 + 每个 target 的验证命令与样例。
  - 现状：P0 目标 e2e 脚本已落地，尚缺 CI 运行记录。
  - 验收：每个 target 至少有编译 + 链接 + 基准运行的 CI 记录。
- **交叉工具链与链接脚本不统一**：脚本/启动文件未沉淀。
  - 补齐：统一的 linker script/启动代码清单，版本受控。
  - 验收：跨目标构建可复现，产物 hash 稳定。

### G. 验证用例与 CI 缺口
- **正/负用例不足**：容器、运行时调用、复杂控制流等仍缺少系统性覆盖。
  - 补齐：用例矩阵（语法/语义/注解/运行时/ABI），每类至少 1 正 1 负。
  - 验收：CI 中 `verify_hrt_backend.sh` 覆盖全部用例。
- **回归与差异检测基线**：golden 规范与基线产物已生成（见 §6.4 与 `artifacts/hrt/golden/manifest.json`）。
  - 已补齐：golden 产物规范 + 基线产物 + 差异阈值策略 + 失败提示。
  - 验收：回归变更可被精确定位到 pass/target。

### H. 可观测性与文档闭环缺口
- **诊断与可观测性不足**：错误定位不够具体，缺少解释性输出。
  - 补齐：`hrt_verify` 诊断规范（错误码、定位、修复建议）。
  - 现状：`docs/hrt-verify-diagnostics.md` 已发布。
  - 验收：常见失败路径可在 1 次修复内通过。
- **生产使用手册缺失**：缺少从项目配置到交付的完整文档。
  - 补齐：最小生产流程文档（配置、构建、验证、交付）。
  - 现状：最小生产流程已补齐（见 §12）。
  - 验收：新项目可按文档完成一次可运行交付。

### I. 正式规范对齐缺口（Gemini 对齐项）
- **原子 RC 默认与 WCET 冲突**：正式规范默认原子 RC，但 HRT 需可预测 WCET。
  - 补齐：HRT 模式强制 `CHENG_MM_ATOMIC=0` 且 `CHENG_MM=off`/`CHENG_ARC=0`；禁止 `share_mt/Arc/Atomic/Mutex/RwLock`。
  - 验收：HRT Profile 与编译器均拒绝原子 RC/并发路径。
- **`async/await` 语义未定义**：正式规范包含 `async/await`，HRT 未定义其时序语义。
  - 补齐：HRT v1 禁用 `async/await`；后续版本定义静态状态机映射。
  - 验收：`hrt_verify` 对 `async/await` 明确拒绝。
- **并发共享安全边界不闭合**：`@task`/并发入口缺少统一的线程边界标识与 `Send/Sync` 校验。
  - 补齐：引入 `@thread_boundary` 并强制校验；将 `@task` 视为线程边界；跨任务共享必须 `Sync`，捕获值必须 `Send`；`T*`/FFI 句柄默认 `!Send/!Sync`。
  - 验收：全局可变共享路径被拒绝或被强制包裹在安全原语中。
- **`?` 与 panic 策略不明确**：`?` 隐式 return/panic 在 HRT 中不可接受。
  - 补齐：HRT v1 禁止 `?`；`panic` 仅允许显式路径（或直接拒绝）。
  - 验收：任意隐式 panic 路径被拒绝，诊断可定位。

### 2.2.1 缺口→任务→验收映射（闭环入口）

| 缺口 | 对应任务 | 验收/命令 |
| :--- | :--- | :--- |
| A 规范与子集（HRT） | P0-01 / 附录 A（LF-E13..LF-E19） | `sh src/tooling/verify_hrt_backend.sh` + `sh src/tooling/verify_asm_backend.sh --fullspec` |
| A' 全语法回归（非 HRT / Stage1） | P1-01 | `sh src/tooling/verify_stage1_fullspec.sh` |
| B 编译与生成 | P0-03 / P0-04 / 附录 A（BP-CG06/08/09） | `sh src/tooling/verify_asm_backend.sh --fullspec` |
| C 运行时与内存 | P0-04 / P1-02 / 附录 A（RT-01..RT-04） | `sh src/tooling/build_hrt_runtime.sh` + e2e 脚本（带运行时链接） |
| D WCET | P1-04 / P2-02 | WCET 报告门禁（`hrt_wcet_report.sh` + `verify_hrt_wcet.sh`） |
| E RTOS/任务 | P1-03 / P1-05 | `hrt_rtos_e2e.sh` + RTOS 启动/链接模板 |
| F 目标矩阵 | P0-02 / P1-05 | `asm_*_e2e.sh` 记录 + `./verify.sh` |
| G 验证与 CI | P0-06 / 附录 A（VT-01..VT-05） | `./verify.sh` + golden 差异策略 |
| H 可观测/文档 | P0-05 / P0-08 / P2-03 | `verify_hrt_backend.sh` + `verify_hrt_runtime_constraints.sh` |
| I 规范对齐 | P0-07 | `verify_hrt_backend.sh`（async/await/?/原子 RC 负例） |

### 2.2.2 缺口闭环卡片（规范/实现/验证/交付）

| 缺口 | 规范 | 实现 | 验证 | 交付 |
| :--- | :--- | :--- | :--- | :--- |
| A 规范与子集（HRT） | `docs/hrt-profile-v1.md` + HRT allow/reject 矩阵（已补齐） | `examples/hrt_*` + `verify_hrt_backend.sh` | `verify_hrt_backend.sh` | HRT Profile + 诊断规范 |
| A' 全语法回归（非 HRT / Stage1） | 全语法覆盖矩阵（非 HRT） | `examples/stage1_codegen_fullspec.cheng` | `verify_stage1_fullspec.sh` | `stage1_codegen_fullspec` 二进制/日志 |
| B 编译与生成 | HRT IR 降级/拒绝规则（已补齐）/ ABI 基线（已补齐）/ ABI 细节（已补齐：HRT v1 限制） | `src/stage0c/core/asm_emit*.c` | `verify_asm_backend.sh --fullspec` | 目标 `.s/.o` 产物 |
| C 运行时与内存 | HRT 内存模型/容器语义（已补齐，见 §4/§3.2） | `src/hrt/hrt_runtime.*` | `build_hrt_runtime.sh` + e2e | `chengcache/hrt_runtime/libhrt_runtime.a` |
| D WCET | `@wcet/@bound` 规范（已补齐，见 §3.7） | `src/tooling/hrt_wcet_report.sh`（v0） | `src/tooling/verify_hrt_wcet.sh`（v0） | `artifacts/hrt/wcet_report.json` |
| E RTOS/任务 | `@task`/调度规范（已补齐，见 §3.7/§4.4） | `src/hrt/hrt_runtime.[ch]` 绑定层 + `src/tooling/rtos/*` 启动/链接模板 | `hrt_rtos_e2e.sh` | RTOS 参考工程（建议） |
| F 目标矩阵 | 目标矩阵与工具链清单（已补齐，见 §7/§14/§16） | `asm_*_e2e.sh` | 目标编译+链接记录 | `chengcache/asm_*` 产物 |
| G 验证与 CI | 验证矩阵/用例规范（已补齐，见 §15/附录 A） + golden 规范（已补齐，见 §6.4） | `examples/hrt_*` + `verify_*.sh` | `./verify.sh` + golden | CI 产物与日志归档 |
| H 可观测/文档 | `docs/hrt-verify-diagnostics.md` | `docs/hrt-verify-diagnostics-map.md`（已补齐） | 负例覆盖与修复指引 | 生产流程文档（已补齐，见 §12） |
| I 规范对齐 | HRT overlay（`docs/cheng-formal-spec.md`） | HRT 禁用项校验 | `verify_hrt_backend.sh` | 规范对齐报告（已补齐，见 §12.9） |

### 2.2.3 正交原子任务矩阵（可并行开发）

原则：
- **正交**：每个任务只覆盖单一能力轴（规范/后端/运行时/工具链等）。
- **原子**：每个任务只产出一个可验收工件。
- **并行**：未声明依赖的任务可并行推进。

任务命名：`ATOM.<Axis>-NN`（Axis = `S` 规范、`B` 后端、`R` 运行时/IO、`W` WCET、`T` RTOS、`P` 目标矩阵、`V` 验证/CI、`D` 文档/交付）。

| 任务 ID | 轴 | 任务说明 | 产出 | 验收 | 依赖 |
| :--- | :--- | :--- | :--- | :--- | :--- |
| ATOMM.S01 | S | C+ASM 混合后端 ABI/符号覆盖规范 | `docs/cheng-direct-c-profile.md` | C/ASM 规则与回退策略明确 | — |
| ATOMM.B01 | B | 热点函数 C 参考实现 + 占位符号 | `src/stdlib/bootstrap/*` 或 `src/runtime/*` | 无 ASM 也可正确运行 | ATOMM.S01 |
| ATOMM.B02 | B | ASM 热点实现（按 target/ISA） | `src/asm/<target>/*.S` | 与 C 参考输出一致 | ATOMM.S01 |
| ATOMM.T01 | T | 构建系统：ASM 优先链接 + 缺失回退 | `src/tooling/chengc.sh` + 脚本 | 有/无 ASM 都可构建 | ATOMM.B01 |
| ATOMM.T02 | T | 模块级并行增量编译（C+ASM） | deps graph + cache | 单模块改动最小重编 | ATOMM.T01 |
| ATOMM.V01 | V | checkasm 类一致性/性能验证 | `src/tooling/verify_mixed_backend.sh` | 一致性通过 + 性能记录 | ATOMM.B02 |
| ATOMM.D01 | D | 混合后端使用说明与示例 | 本文档新增小节 | 可复现最小闭环 | ATOMM.V01 |

已完成归档（混合后端闭环基础设施）：`ATOMM.S01`、`ATOMM.T01`、`ATOMM.T02`、`ATOMM.V01`、`ATOMM.D01`。  
待业务侧补齐：`ATOMM.B01`、`ATOMM.B02`（具体热点/ISA）。

已完成归档：`ATOM.S01`、`ATOM.S02`、`ATOM.B01`、`ATOM.S03`、`ATOM.B02`、`ATOM.B03`、`ATOM.B04`、`ATOM.R01`、`ATOM.R02`、`ATOM.R03`、`ATOM.R04`、`ATOM.W01`、`ATOM.W02`、`ATOM.P01`、`ATOM.P02`、`ATOM.V01`、`ATOM.V02`、`ATOM.D01`、`ATOM.T01`。

> 注：附录 A 的 “语言×后端” 原子矩阵（LF×BP×TA）用于语法/后端覆盖；本节矩阵用于跨域闭环并行化。

#### 可直接复制的 Codex agent 提示词（ATOMM.*）

**PROMPT.ATOMM.S01**
```text
你是 Codex agent。目标：定义 C+ASM 混合后端的 ABI/符号覆盖规范。
产出：docs/cheng-direct-c-profile.md（新增混合后端小节）。
验收：明确 C/ASM 同名规则、弱符号/优先级、回退策略。
```

**PROMPT.ATOMM.B01**
```text
你是 Codex agent。目标：为热点路径补齐 C 参考实现与占位符号。
产出：src/stdlib/bootstrap/* 或 src/runtime/* 中的 C 实现与声明。
验收：无 ASM 时功能正确可运行。
依赖：ATOMM.S01。
```

**PROMPT.ATOMM.B02**
```text
你是 Codex agent。目标：为指定 target/ISA 提供 ASM 热点实现。
产出：src/asm/<target>/*.S + 绑定声明。
验收：与 C 参考实现一致，通过一致性测试。
依赖：ATOMM.S01。
```

**PROMPT.ATOMM.T01**
```text
你是 Codex agent。目标：构建系统支持 ASM 优先链接，缺失自动回退到 C。
产出：src/tooling/chengc.sh + 规则/脚本变更。
验收：同一接口在有 ASM/无 ASM 两种情况下均可构建。
依赖：ATOMM.B01。
```

**PROMPT.ATOMM.T02**
```text
你是 Codex agent。目标：为 C+ASM 混合后端加入模块级并行增量编译。
产出：deps graph + 缓存规则 + 并行构建入口。
验收：仅改动单模块时触发最小重编。
依赖：ATOMM.T01。
```

**PROMPT.ATOMM.V01**
```text
你是 Codex agent。目标：新增 checkasm 类的一致性与性能验证脚本。
产出：src/tooling/verify_mixed_backend.sh + 基准输出模板。
验收：一致性通过，性能对比有记录。
依赖：ATOMM.B02。
```

**PROMPT.ATOMM.D01**
```text
你是 Codex agent。目标：补齐混合后端使用说明与示例。
产出：docs/cheng-direct-c-profile.md 新增混合后端小节。
验收：可复现“构建→验证→性能记录”的最小闭环。
依赖：ATOMM.V01。
```

### 2.2.4 下一轮可直接派发的子任务清单

以下为剩余项拆成的可直接派发子任务，每项包含：目标、产出、验收、依赖。

任务命名：`NEXT-XX`（按优先级排序，可并行）。
（无，已全部归档。Linux x86_64 记录已由 `hrt_e2e_matrix.sh` 生成可追溯 manifest；工具链缺失时可用 `--allow-skip` 记录 `status=skipped`。）

已完成归档：`BASE-STACK.S03`、`BASE-STACK.R01`、`A-STACK.B02+V01`、`A-STACK.B03+B04+P02`、`A-STACK.R02`、`A-STACK.R03`、`A-STACK.P01+D01`、`NEXT-01`、`NEXT-02`、`NEXT-03.A`、`NEXT-03.B`、`NEXT-03.C`、`NEXT-03.D`、`NEXT-03.E`、`NEXT-04`、`NEXT-05`、`NEXT-06`、`NEXT-07`、`NEXT-08`、`NEXT-09.A`、`NEXT-09.B`、`NEXT-10`。

### 2.2.5 P0 最小闭环证据包（可交付）

必须具备以下证据，才算“闭环可交付”：
- 规范证据：`docs/hrt-profile-v1.md`、`docs/hrt-verify-diagnostics.md`
- 编译证据：`bin/cheng` + `chengc.sh --emit-hrt` 可用
- 验证证据：`./verify.sh` 输出 `verify ok`
- 产物证据：`chengcache/hrt/*.s`、`chengcache/hrt_runtime/libhrt_runtime.a`
- 运行约束证据：`configs/hrt_runtime_constraints.env` + `verify_hrt_runtime_constraints.sh`

---

## 2.3 HRT 是否会改变 Cheng 语法？有哪些影响？

**结论：不改变语言语法，但会收紧“可用子集”。**  
HRT 是一个**编译配置/Profile**，通过静态检查和注解要求（例如 `@bound`）限制可用构造。

### 典型影响（基于当前实现与规划）

- **`while true`**：
  - 语法不变，但在 HRT 模式下必须有 `@bound(N)` 紧邻标注，否则报错。
- **`for i in 0..x`**：
  - 若 `x` 不是编译期常量，需要 `@bound(N)`；否则 HRT 校验会拒绝。
  - 目前只支持数值范围 for（`..`/`..<`），seq/str/table 迭代不在子集内。
- **递归**：在 HRT 模式下被禁止（静态调用图检测）。
- **动态容器与字面量**：`seq/str/cstring/Table` 等动态结构、`@[...]`、`{...}` 动态字面量会被拒绝（固定容量 `seq[T,N]`/`str[N]`/`Table[V,N]` 例外）。
- **运行时/外部调用**：runtime 与 importc 调用在 HRT 模式下禁止。

换句话说：**语法本身不变，但 HRT 会让部分语义“不可用”或必须加注解才能通过**。

## 2.4 可执行任务清单（仅剩余项）

已完成归档：`P0-01..P0-09`、`P1-01..P1-06`、`P2-01..P2-07`。

### P1（全语法与 RTOS/WCET 扩展）
（无，已全部归档）

### P2（性能/优化/生态工具链完善）
（无，已全部归档）

## 2.5 生产闭环落地清单（仅剩余项）

闭环定义：**规范 → 实现 → 验证 → 交付**。每条任务至少有工件、命令、产物路径与验收口径。

已完成归档：`P0-01..P0-09`、`P1-01..P1-06`、`P2-01..P2-07`。

### P1 闭环落地（剩余）
（无，已全部归档）

### P2 闭环落地（剩余）
（无，已全部归档）

## 3. HRT Profile（实时子集）

为保证硬实时，必须对语言特性施加约束。建议引入 **HRT Profile**：

### 3.1 控制流约束
- 所有循环必须**可界定上界**：
  - `for i in 0 .. N`（N 为编译期常量或可验证上界）
  - `while` 需显式 `@bound(N)` 注解
- 递归默认禁止；若允许，必须 `@bound(depth=N)`。

### 3.2 内存约束
- 禁止隐式堆分配。
- `seq`/`str`/`Table` 等动态结构必须：
  - 使用固定容量容器（`seq[T, N]` / `Table[V, N]` / `str[N]`）
  - 禁止扩容与 rehash
- 允许的 importc 白名单（确定性内存操作）：`ptr_add`、`c_memcpy`、`c_memset`、`c_memcmp`、`c_strlen`、`c_strcmp`、`load_ptr`、`store_ptr`
- HRT v1 禁用 ARC/ORC（`CHENG_MM=off` 或 `CHENG_ARC=0`）：
  - HRT 路径不插入 `retain/release`
  - 需要引用计数的场景改用固定容量容器或显式 Arena/Region

### 3.3 I/O 与系统调用
- 禁止不可界定 I/O（网络、文件系统、阻塞锁）。

### 3.3.1 HRT I/O Ring 标准库落地（本仓库）
- 新增 `std/hrt_ring`：固定容量 Ring 元数据/视图（SPSC），要求 `cap` 为 2 的幂，保留 1 空槽。
- 新增 `std/hrtnet`：网络 RX/TX ring，提供 `peek/consume`、`reserve/commit` 零拷贝 API。
- 新增 `std/hrtnet_driver`：网卡驱动侧 init/bind API（非 HRT；importc 绑定），提供 `open/map/bind/kick`；本机闭环用 `CHENG_HRT_SYSIO=1` 将 `udp:<port>` 映射到系统 UDP loopback。
- 新增 `std/hrtfile`：文件提交/完成 ring，显式请求/完成结构体，HRT 侧零拷贝。
- 新增 `std/hrtfile_driver`：文件驱动侧 init/bind API（非 HRT；importc 绑定），本机闭环用 `CHENG_HRT_SYSIO=1` 以 `pread/pwrite` 处理 submit/complete。
- 新增 `std/hrt_rtos`：RTOS 调度/同步原语封装（yield/sleep/now + mutex/semaphore/event），用于 RTOS/v2 任务侧调用。
- sysio loopback 验证入口（driver 侧）：`sh src/tooling/verify_hrtnet_driver.sh` / `sh src/tooling/verify_hrtfile_driver.sh`
- 默认例子：`examples/hrt_netdev_loopback.cheng`、`examples/hrt_filedev_loopback.cheng`（脚本自动用 `CHENG_HRT_SYSIO=1` 构建 `chengcache/hrt_runtime_sys/libhrt_runtime.a` 并链接）
- 运行前提：外部驱动负责填充共享内存并推进 `tail`；本机 sysio 模式由 driver 轮询系统接口并推进 `tail`，HRT 任务仅轮询 `head/tail`。
- 约定：driver init 在非 HRT 阶段完成；HRT 任务只持有 ring 指针。

#### 3.3.1.1 hrtnet sysio loopback（driver 侧闭环）
目的：验证 `std/hrtnet_driver` 与运行时 sysio 映射的初始化/绑定路径，**不引入额外依赖库**。

关键约定：
- 使用 `CHENG_HRT_SYSIO=1` 构建运行时，`udp:<port>` 映射到系统 UDP loopback。
- 校验入口使用 **stage0c asm 后端**（非 HRT），仅验证 driver 侧 API 绑定与链接。
- 默认示例：`examples/hrt_netdev_loopback.cheng`（`udp:0` 自动分配端口）。

验证入口（与脚本同步）：
```sh
sh src/tooling/verify_hrtnet_driver.sh
```

脚本行为要点：
- 运行时库：`chengcache/hrt_runtime_sys/libhrt_runtime.a`（自动携带 `-DCHENG_HRT_SYSIO=1`），并写入同目录 `.sysio` 标记。
- 输出目录：`chengcache/hrtnet_driver/`（生成 `.s/.o/.bin`）。
- 目标选择：默认按宿主自动选择 `aarch64-apple-darwin` / `x86_64-apple-darwin` / `x86_64-unknown-linux-gnu`。
- 可选运行：加 `--run` 仅在 host/target 匹配时执行产物，否则提示跳过。

常用覆盖参数（与环境变量等价）：
- `--example` / `HRT_NETDEV_EXAMPLE`：替换示例文件
- `--target` / `HRT_NETDEV_TARGET`，`--abi` / `HRT_NETDEV_ABI`：指定目标/ABI
- `--cc` / `HRT_NETDEV_CC`，`--cc-args` / `HRT_NETDEV_CC_ARGS`，`--sysroot` / `HRT_NETDEV_SYSROOT`
- `--cflags` / `HRT_NETDEV_CFLAGS`，`--ldflags` / `HRT_NETDEV_LDFLAGS`

#### 3.3.1.2 hrtfile sysio loopback（driver 侧闭环）
目的：验证 `std/hrtfile_driver` 与运行时 sysio 映射的 submit/complete 路径，**不引入额外依赖库**。

关键约定：
- 使用 `CHENG_HRT_SYSIO=1` 构建运行时，文件操作由 sysio 后端通过 `pread/pwrite` 模拟。
- 校验入口使用 **stage0c asm 后端**（非 HRT），仅验证 driver 侧 API 绑定与链接。
- 默认示例：`examples/hrt_filedev_loopback.cheng`（默认路径 `/tmp/cheng_hrt_filedev.bin`）。

验证入口（与脚本同步）：
```sh
sh src/tooling/verify_hrtfile_driver.sh
```

脚本行为要点：
- 运行时库：`chengcache/hrt_runtime_sys/libhrt_runtime.a`（自动携带 `-DCHENG_HRT_SYSIO=1`），并写入同目录 `.sysio` 标记。
- 输出目录：`chengcache/hrtfile_driver/`（生成 `.s/.o/.bin`）。
- 目标选择：默认按宿主自动选择 `aarch64-apple-darwin` / `x86_64-apple-darwin` / `x86_64-unknown-linux-gnu`。
- 可选运行：加 `--run` 仅在 host/target 匹配时执行产物，否则提示跳过。

常用覆盖参数（与环境变量等价）：
- `--example` / `HRT_FILEDEV_EXAMPLE`：替换示例文件
- `--target` / `HRT_FILEDEV_TARGET`，`--abi` / `HRT_FILEDEV_ABI`：指定目标/ABI
- `--cc` / `HRT_FILEDEV_CC`，`--cc-args` / `HRT_FILEDEV_CC_ARGS`，`--sysroot` / `HRT_FILEDEV_SYSROOT`
- `--cflags` / `HRT_FILEDEV_CFLAGS`，`--ldflags` / `HRT_FILEDEV_LDFLAGS`

### 3.3.2 内核级零拷贝 Ring 绑定（macOS/Android）
目标：内核/驱动写入共享内存 Ring，HRT 任务轮询处理（热路径零 syscall）。

**运行时后端（已落地）**
- `cheng/hrt/hrt_runtime.c` 支持 `CHENG_HRT_SHM`：`mmap` 共享内存作为 RX/TX Ring 与数据缓冲区。
- `ifname` 约定：
  - `ring:<name>` → 默认路径 `CHENG_HRT_RING_DIR/<name>_q<queue>.hrt`
  - `shm:/abs/path` → 直接使用绝对路径
- 默认目录：macOS `/tmp`，Android `/data/local/tmp`（可通过 `CHENG_HRT_RING_DIR` 覆盖）。

**共享内存布局（约定）**
- `rx_meta` + `tx_meta` + `rx_slots[]` + `tx_slots[]` + `rx_buffers[]` + `tx_buffers[]`
- `slot->data` 在用户态设置为映射内缓冲区指针；内核侧应忽略该字段，仅写 `len/flags` 并推进 `tail`。

**构建示例**
- macOS：`sh src/tooling/build_hrt_runtime.sh --cflags:"-DCHENG_HRT_SHM"`
- Android（NDK）：`sh src/tooling/build_hrt_runtime.sh --target:aarch64-linux-android --sysroot:$NDK_SYSROOT --cflags:"-DCHENG_HRT_SHM"`

**调用示例（HRT 侧）**
- `hrtnet_driver.hrtNetDriverOpenStr("ring:net0", 0, 1024, 1024)`
- HRT 任务：`poll → peek → consume`；发送路径：`reserve → commit → kick`

**驱动侧参考实现（用户态模拟）**
- 源码：`src/tooling/hrt_ring_shm_driver.c`（TX→RX loopback，可选 `--inject`）
- 构建（macOS）：`cc -O2 -std=c11 -I . -o chengcache/hrt_ring_shm_driver src/tooling/hrt_ring_shm_driver.c`
- 运行（loopback）：`./chengcache/hrt_ring_shm_driver --ifname:ring:net0 --rx-cap:1024 --tx-cap:1024`
- Android（NDK）：`$NDK_CLANG --target=aarch64-linux-android21 --sysroot=$NDK_SYSROOT -O2 -std=c11 -I . -o chengcache/hrt_ring_shm_driver_android src/tooling/hrt_ring_shm_driver.c`
- Android 运行：`adb push chengcache/hrt_ring_shm_driver_android /data/local/tmp/ && adb shell /data/local/tmp/hrt_ring_shm_driver_android --ifname:ring:net0 --rx-cap:1024 --tx-cap:1024`

**驱动侧 loopback 示例（Cheng）**
- 示例：`examples/hrt_netdev_ring_loopback.cheng`（使用 `ring:`，不覆写 `slot->data`）
- 验证脚本：`sh src/tooling/verify_hrtnet_ring_driver.sh`

### 3.4 并发与原子约束
- HRT v1 禁用 `async/await` 与并发 API（`spawn/channel`）。
- 强制 `CHENG_MM_ATOMIC=0` 且 `CHENG_MM=off`/`CHENG_ARC=0`；禁止 `Arc/Atomic/Mutex/RwLock`。
- `@task` 作为并发边界的 `Send/Sync` 规则在 P1 落地。
- 仅允许 RTOS 级固定耗时调用（建议标注 WCET）。
- 如启用 `@task`（P1），仅允许静态任务（`@task/@period/@deadline/@priority`）；禁止动态线程创建。

### 3.5 用户体验原则（HRT）
- 语法不变：同一语法集，HRT 仅作为编译模式与静态门禁。
- 先推断再要求：能静态推断上界/容量则通过；无法推断才要求显式标注或配置补齐。
- 配置优先：容量/预算/周期等尽量放到 build config，避免在业务代码中散落标注。
- 报错一次到位：诊断应指向“缺失的上界/容量/预算”与最小可行补法。
- 分级收紧：支持 “warn → error” 迁移策略，避免一次性破坏可用性。
- 库级封装：HRT 友好 API 承担复杂约束，应用层尽量保持普通写法。

### 3.5.1 语法友好与硬实时合一策略（实践）
- 同一语法 + Profile 门禁：HRT 只是编译配置，语法保持一致，约束由 `hrt_verify` 统一把关。
- 先推断再标注：能从类型/常量/固定容量推断上界则自动放行，无法推断才要求 `@bound` 或配置补齐。
- 库级封装约束：固定容量容器、Arena/Pool、有界 I/O 封装进库层，应用层尽量使用“普通写法”。
- 确定性替代而非禁用：用固定容量容器、显式错误码、轮询 + Ring Buffer 等替代动态特性。
- 分层架构：HRT 只覆盖热路径；非实时逻辑在普通模式，通过固定容量队列/只读快照交接。
- 渐进式门禁：支持 “warn → error” 演进，诊断提供最小可行修复建议。

### 3.6 HRT Profile v1 语法清单（与 `hrt_verify` 对齐）

为保证跨平台硬实时闭环，HRT Profile v1 仅允许**可静态界定、可确定生成**的语法子集；其余语法一律由 `hrt_verify` 拒绝。

**允许（当前全平台一致支持）**：
- 顶层：`import`/`include`、`fn` 声明、简单 `type`/`object`/`enum` 声明（仅用于静态布局）
- 语句：`let/var/const`（**仅允许单一标识符绑定**）、`if/while/for-range`、`break/continue`、`return`、表达式语句
- 表达式：`int/char/bool/nil` 字面量、标识符、括号、`cast`、前缀（`+ - ! ~ & *`）、中缀算术/比较/逻辑（`+ - * / % & | ^ << >>`、`== != < <= > >=`、`&& ||`）、函数调用（≤6 参数、无具名参数；**单参允许空格或小括号调用**）
- 约束：`@bound(N)`（紧邻 `while` 或 `for`）、禁止递归、禁止 runtime/importc 调用；`@wcet` 可选（仅用于 WCET 报告/门禁，不触发 `hrt_verify` 报错）

**禁止（`hrt_verify` 直接拒绝）**：
- 控制流扩展：`when`/`case`/`of`、`if/when/case` 表达式、`defer`/`yield`/`do`
- 并发语义：`async/await`、`spawn/channel`、`@task`（v1）
- 语法糖与高级构造：`lambda`、`comprehension`、`fast_asgn (+= 等)`、复杂 pattern 解构
- 成员访问与索引：允许结构体/固定容量容器字段访问（`.len/.cap/.buffer` 等）；`[]` 索引仍禁止（需显式 API）
- 编译期元编程：`macro`/`template`/`iterator`
- 动态与不可界定数据：`seq/str/cstring/Table` 类型与非固定容量 `seq_*/Table_*`、`@[...]`/`{...}` 动态字面量、字符串字面量（固定容量 `seq[T,N]`/`str[N]`/`Table[V,N]` 例外）
- 数值与类型：`float/float32/float64` 及其字面量
- Result 解包：`?`

> 注：后续扩展语法时，以“**新增允许项 + 对应验证规则 + 目标矩阵回归用例**”的方式逐步放开。

### 3.6.1 HRT IR 降级/拒绝规则（stage0c）

HRT 模式在 stage0c 的 pass 顺序：
`macro_expand → monomorphize → semantics → hrt_verify(hard_rt_check) → asm emit`

| 阶段 | 规则 | v1 | v2 | 诊断/备注 |
| --- | --- | --- | --- | --- |
| `macro_expand` | 拒绝 `macro/template` 声明，不展开 | 拒绝 | 拒绝 | 可能无 `HRT-` 前缀 |
| `monomorphize` | 固定容量重写：`seq/str/Table → seq[T,N]/str[N]/Table[V,N]`（内部实现名 `seq_fixed/str_fixed/Table_fixed`） | 允许 | 允许 | 需常量容量 |
| `monomorphize` | 关闭 `Result ?` 降级 | 拒绝 | 拒绝 | 命中 `postfix/if expr` 拒绝 |
| `hrt_verify(profile)` | 语法矩阵统一门禁 | 允许/拒绝 | 允许/拒绝 | D01/D02/D03/D04/D11 |
| `hrt_verify(types)` | 禁止动态容器/浮点/并发类型 | 拒绝 | 拒绝 | D07/D10 |
| `hrt_verify(calls)` | 禁止 runtime/importc/并发调用 | 拒绝 | 拒绝 | D06/D10 |
| `hrt_verify(loops)` | `while`/`for` 必须有界 | 强制 | 强制 | D05 |
| `hrt_verify(recursion)` | 禁止递归 | 拒绝 | 拒绝 | D08 |
| `hrt_verify(string.len)` | 禁止 `string.len` | 拒绝 | 拒绝 | D09 |
| `hrt_verify(config)` | 强制 `CHENG_MM_ATOMIC=0` + `CHENG_MM=off` | 强制 | 强制 | D10 |
| `hrt_verify(case/of)` | v2 仅允许简单模式 | 拒绝 | 允许 | D01/D02 |

> HRT v1 明确禁用 `async/await`、`?` 解包、原子 RC（`CHENG_MM_ATOMIC=0`）。

### 3.7 注解语义闭合（@bound/@wcet/@task）

> 目标：不新增运行时依赖，诊断与现有 `hrt_verify` 一致。

#### 3.7.1 `@bound(N)`（循环上界）
- 位置：必须作为独立注解行，紧邻 `while` 或 `for` 之前；中间不可插入其他语句。
- 参数：`N` 必须为编译期常量整数；否则 `hrt_verify` 报 `hard realtime asm backend expects @bound(<const int>)`（D05）。
- 语义：`N` 代表最大迭代次数承诺，用于 WCET 估计与门禁；HRT v1 不插入运行时计数或断言。
- 例外：`for i in 0 .. CONST` / `0 ..< CONST` 上界可推断时允许省略 `@bound`；无法推断则必须显式标注。
- 缺失诊断：缺少 `@bound` 时，`hrt_verify` 会报 `hard realtime asm backend requires bounded loops (@bound)` 或 `hard realtime asm backend requires bounded for-range or @bound`（D05）。

示例：
```
fn main(): int32 =
    var i: int32 = 0
    @bound(16)
    while i < 16:
        i = i + 1
    return 0
```

#### 3.7.2 `@wcet(N)`（函数/任务预算）
- 位置：标注在 `fn` 或 `@task` 入口上（v1 仅 `fn` 生效，`@task` 禁止）。
- 参数：`N` 为编译期常量整数，单位 **微秒（us）**，与 `hrt_wcet_report.sh` 的 `wcet_us` 字段对齐。
- 语义：用于 WCET 报告/外部工具对齐的预算声明；**当前 `hrt_verify` 不强制检查 `@wcet`**，门禁由 `hrt_wcet_report.sh`/外部工具负责。
- 缺省：未标注时，以配置文件或外部工具结果填充（`HRT_WCET_*` 或 `--from`）。

示例：
```
@wcet(800)
fn main(): int32 =
    return 0
```

#### 3.7.3 `@task/@period/@deadline/@priority`（RTOS 静态任务）
- **HRT v1 行为**：`@task` 及相关注解被 `hrt_verify` 拒绝（D11：`hard realtime asm backend does not allow @task annotations`）。
- **启用条件（P1/v2）**：当 `CHENG_HRT_PROFILE>=2` 或 RTOS 目标启用任务模型时，允许 `@task`。
- **约束（启用后）**：
  - `@task` 仅用于顶层 `fn`，**无参数**，返回 `int32`（错误码）。
  - 必须显式 `@period(<const int>)` 与 `@deadline(<const int>)`（单位 ns），`@priority(<const int>)` 为 RTOS 静态优先级。
  - 建议提供 `@wcet(<const int>)`；未提供时由 WCET 门禁脚本或外部工具补齐。
  - 禁止动态线程创建与不可界定阻塞；跨任务共享必须满足 `Send/Sync`（P1 约束）。
  - 任务入口天然视为线程边界；若启用额外并发/通道 API，必须显式 `@thread_boundary` 并执行 `Send/Sync` 校验（HRT v1 禁用并发 API）。

---

## 4. 运行时与内存模型

### 4.1 HRT Runtime（最小化）
提供独立运行时库：
- `hrt_runtime`：固定容量池、零 GC
- `hrt_panic`：硬中断/错误处理（可配 watchdog）
- `hrt_time`：确定性时钟 API

当前最小实现：
- `src/hrt/hrt_runtime.[ch]`：最小符号集（`cheng_mem_*`/`cheng_mm_*`、`c_strlen`、`__cheng_concat_str`、`__cheng_vec_contains_*`、`echo/panic`、`cheng_hrt_*` ring/slot/req/cpl + `cheng_hrt_{net,file}dev_*`；`CHENG_HRT_SYSIO=1` 启用 loopback）
- `src/tooling/build_hrt_runtime.sh`：生成 `chengcache/hrt_runtime/libhrt_runtime.a`

#### 4.1.1 运行时符号集与 ABI 绑定（R01 基线）
- **绑定入口**：stage0c 在 `src/stage0c/core/asm_emit_runtime.c` 将 IR/runtime 名称映射到 C 符号；链接时必须提供同名导出。
- **ABI 约束**：C ABI、无 name mangling；结构体按声明顺序布局；`int32`=4 字节；HRT v1 仅 64-bit 目标（指针 8 字节）。
- **最小符号集**（`libhrt_runtime.a` 必须导出，R02/R03 直接复用）：
  - 内存/诊断：`cheng_mem_retain`/`cheng_mem_release`/`cheng_mem_retain_atomic`/`cheng_mem_release_atomic`、`cheng_mem_refcount`/`cheng_mem_refcount_atomic`、`cheng_mm_retain_count`/`cheng_mm_release_count`/`cheng_mm_diag_reset`、`echo`/`panic`
  - 字符串/序列：`c_strlen`、`__cheng_concat_str`、`cheng_seq`、`__cheng_vec_contains_int32/int64/ptr_void/str`
  - IO Ring/Slot：`cheng_hrt_ring_meta`、`cheng_hrt_net_slot`、`cheng_hrt_file_req`、`cheng_hrt_file_cpl`、`cheng_hrt_{net,file}dev_map_t` + `cheng_hrt_{net,file}_*` 访问器与 open/map/poll/close
  - RTOS 绑定：`cheng_hrt_task_desc`/`cheng_hrt_rtos_ops`、`cheng_hrt_rtos_*`、`cheng_hrt_mutex/semaphore/event` 与对应操作函数

编译器绑定表（`asm_emit_runtime.c`）：

| IR/runtime 名称 | C 符号 | 说明 |
| --- | --- | --- |
| `memRetain` | `cheng_mem_retain` | HRT v1 默认空实现 |
| `memRelease` | `cheng_mem_release` | HRT v1 默认空实现 |
| `memRetainAtomic` | `cheng_mem_retain_atomic` | HRT v1 默认空实现 |
| `memReleaseAtomic` | `cheng_mem_release_atomic` | HRT v1 默认空实现 |
| `memRefCount` | `cheng_mem_refcount` | HRT v1 返回常量 |
| `memRefCountAtomic` | `cheng_mem_refcount_atomic` | HRT v1 返回常量 |
| `memRetainCount` | `cheng_mm_retain_count` | 诊断计数 |
| `memReleaseCount` | `cheng_mm_release_count` | 诊断计数 |
| `memDiagReset` | `cheng_mm_diag_reset` | 诊断复位 |
| `concat` / `__cheng_concat_str` | `__cheng_concat_str` | 字符串拼接 |
| `echo` | `echo` | 诊断输出 |
| `panic` | `panic` | 失败路径 |
| `__cheng_vec_*` / `__cheng_slice_vec*` | 同名 | 运行时直连 |

- **实现模式**：
  - 默认 stub：始终导出符号但不做系统 I/O（满足最小链接）。
  - `CHENG_HRT_SYSIO=1`：启用 UDP loopback（`hrtnet/hrtfile` 驱动侧自测）。
  - `CHENG_HRT_SHM=1`：启用共享内存 ring（与外部驱动协作）。

### 4.2 内存分配策略
可选方案：

1) **纯栈 + 静态区**  
   - 运行时零分配，最可预测
2) **固定容量对象池**  
   - `Pool[T; N]` 固定大小，O(1) 分配
3) **区域分配（Region/Arena）**  
   - 在任务边界统一释放

### 4.3 失败处理策略（panic/trap/错误码）
- **panic**：必须显式调用；无栈展开、无异常恢复。HRT v1 禁止 `?` 的隐式 panic 分支。
- **trap**：语义等同“立即停止”，可由平台映射为 `abort`/`__builtin_trap`/watchdog 触发；不允许恢复执行。
- **错误码**：可恢复错误一律用显式返回值/Result 分支处理；建议 `int32` 返回（`0` 表示成功，负值为错误）。
- **WCET 约束**：失败路径必须可界定并计入 WCET；禁止在失败路径中执行不可界定 I/O。
- **实现对齐**：当前 `hrt_runtime` 的 `panic` 直接 `abort()`（不引入新依赖）；RTOS 可替换为平台级 trap/watchdog 行为。

### 4.4 RTOS 任务模型入口（与 `hrt_rtos_e2e.sh` 对齐）
- **当前入口**：`src/tooling/rtos/*/crt0.S` 入口符号为 `main`，`hrt_rtos_e2e.sh` 直接链接并调用 `main`（随后进入 `wfi` idle）。
- **HRT v1**：仅允许单入口 `fn main(): int32`；`@task` 注解被 `hrt_verify` 拒绝（D11）。
- **启用任务模型（P1/v2）**：允许 `@task` 标注的顶层 `fn` 作为静态任务入口，RTOS 适配层负责创建/调度。
- **最小约束**（启用后）：任务函数无参数、返回 `int32`；必须显式 `@period/@deadline/@priority`（常量），并满足 `@bound` 与 `@wcet` 约束。

#### 4.4.1 RTOS 调度绑定层（runtime → RTOS）
- 运行时导出 `cheng_hrt_rtos_bind` 注册 `cheng_hrt_rtos_ops`，RTOS 适配层通过函数指针接管调度/时间/同步原语。
- 任务描述统一使用 `cheng_hrt_task_desc`（`name/period_ns/deadline_ns/priority/entry`），适配层负责将 `@task` 入口映射为静态任务表并调用 `cheng_hrt_rtos_register_task` + `cheng_hrt_rtos_start`。
- 未绑定时（默认 stub）：`*_lock/*_wait/sleep` 返回 `-1`，`yield` 为 no-op，确保**无隐式阻塞**。
- 最小任务样例：`examples/hrt_task_ok.cheng`（`@task/@period/@deadline/@priority`），默认由 `hrt_rtos_e2e.sh` 以 `HRT_RTOS_PROFILE=2` 编译验证。

#### 4.4.2 同步原语清单与阻塞上界
允许的同步原语（HRT v2 / RTOS 目标）：
- **互斥锁**：`cheng_hrt_mutex_*`（非递归）；阻塞必须显式 `timeout_ns`，`0` 表示非阻塞。
- **计数信号量**：`cheng_hrt_semaphore_*`；阻塞必须显式 `timeout_ns`。
- **事件/通知**：`cheng_hrt_event_*`；阻塞必须显式 `timeout_ns`。
- **调度让出/睡眠**：`cheng_hrt_rtos_yield`/`cheng_hrt_rtos_sleep_ns`。

阻塞上界规则（运行时/RTOS 适配层统一遵守）：
- 所有阻塞类调用必须携带**有界超时**（`timeout_ns > 0`）；`timeout_ns=0` 为非阻塞。
- `timeout_ns` 不得超过任务 `@deadline`（建议不超过 `@period`），超限应在适配层拒绝或裁剪。
- 若无 RTOS 绑定（stub 模式），阻塞类调用必须返回失败码，避免隐式等待。
- Cheng 层调用入口：`std/hrt_rtos`（`hrtMutex*`/`hrtSemaphore*`/`hrtEvent*` + `hrtYield/hrtSleepNs/hrtNowNs`）。

---

## 5. 后端代码生成设计

### 5.1 架构与 ABI
针对生产闭环，P0 平台优先支持：
- macOS：`x86_64-apple-darwin` / `aarch64-apple-darwin`（ABI=darwin）
- Windows：`x86_64-pc-windows-msvc`（ABI=coff）
- Android：`aarch64-linux-android`（ABI=elf）
- iOS：`aarch64-apple-ios`（ABI=darwin）
- HarmonyOS：`aarch64-unknown-ohos`（ABI=elf）

P1（硬实时/嵌入式）：
- `aarch64-unknown-elf`、`riscv64-unknown-elf`

#### 5.1.1 HRT ABI 基线（v1）
- 仅 64-bit 目标（`ptr_size=8`），默认小端；栈向下生长；**调用点/函数入口统一 16 字节对齐**。
- 入口函数 `main` 为默认入口；启用 `@task` 时任务入口遵循同一 ABI（v1 仍禁用 `@task`）。
- 仅允许**标量/指针类**参数与返回；聚合体（struct/tuple/array）**不按值传参/返回**。
- 返回值放在整数寄存器；`bool/char` 使用低位，高位不做 ABI 保证。
- 浮点类型在 HRT v1 禁止；**不使用 FP/SIMD 寄存器**。
- 命名参数与 `vararg` 不支持；参数/返回通过固定寄存器或 8 字节栈槽传递。
- 编译器仅使用**caller-saved**寄存器作为临时；仅保存帧指针/返回地址（见各 ISA 规则）。
- Darwin ABI 全局符号带 `_` 前缀（其余 ABI 无前缀）。

**ABI 基线规则表（v1）**：

| ISA/ABI | 参数寄存器 | 栈上传参 | 返回值 | 对齐/栈帧 | 备注 |
| --- | --- | --- | --- | --- | --- |
| x86_64 (ELF/Darwin/COFF) | `rdi rsi rdx rcx r8 r9` | v1 不支持（>6 直接拒绝） | `rax` | `rbp` 帧；`stack_size` 16 对齐 | COFF 仍用 SysV 寄存器序，不含 Windows shadow space |
| AArch64 (ELF/Darwin) | `x0..x7` | 8 字节槽（arg>=8） | `x0` | `stp x29,x30`；16 对齐 | 调用者预留 call area；后端先写 arg slots 再装载寄存器 |
| RISC-V64 (ELF) | `a0..a7` | 8 字节槽（arg>=8） | `a0` | 保存 `ra/s0`；16 对齐 | 评估参数时使用临时栈区，**不支持参数内嵌套调用** |

> 后续 B02/B03/B04/P02 仅引用本节基线，不重复定义 ABI。

#### 5.1.2 x86_64（ELF/Darwin/COFF）
- 参数 0..5 → `%rdi/%rsi/%rdx/%rcx/%r8/%r9`；**当前后端最多支持 6 个参数**。
- 返回值 → `%rax`（`bool/char` 仅低位有效，高位不保证）。
- 栈帧：`push %rbp; mov %rsp,%rbp; sub $stack_size,%rsp`，`stack_size` 16 字节对齐。
- 若未来放开 >6 参数：栈参数位于 `rbp+16+8*(i-6)`（当前实现直接拒绝）。
- COFF 仍沿用上述寄存器顺序（与 Windows x64 ABI 不一致）；对接系统 ABI 时需 shim/包装。

#### 5.1.3 AArch64（ELF/Darwin）
- 参数 0..7 → `x0..x7`；多余参数进入 8 字节栈槽（调用者栈区）。
- 返回值 → `x0`。
- 栈对齐 16 字节；函数序言保存 `x29/x30` 并设置 `x29` 为帧指针。

#### 5.1.4 RISC-V64（ELF）
- 参数 0..7 → `a0..a7`；多余参数进入 8 字节栈槽。
- 返回值 → `a0`。
- 栈对齐 16 字节；函数序言保存 `ra/s0` 并设置 `s0` 为帧指针。

#### 5.1.5 调用约定实施细则（HRT v1）
- 参数求值顺序：按源码 **左 → 右**；每个参数求值后立即落入临时槽，再统一装载寄存器/栈槽。
- x86_64：仅支持 0..5 参数（寄存器传递）；**不生成栈参数槽**，超限直接报错。
- AArch64/RV64：超出寄存器的参数按索引顺序写入 **8 字节栈槽**（caller 负责）。
  - 栈参数区起始地址为 `call` 时的 `sp`；AArch64 callee 读取 `x29 + 16` 起始，RV64 读取 `s0 + 0` 起始。
  - RV64 call 会为栈参数区做 16 字节对齐（不足补 8 字节）；AArch64 由 frame size 保证 16 字节对齐。
- 调用点对齐：`call`/`bl` 前 `sp` 必须 16 字节对齐；被调函数保持对齐不变。
- 返回值：标量返回寄存器；上位位宽不依赖，建议 **零扩展** 以保证可比对产物。
- 聚合类型（`object/tuple/array/seq[T,N]`）参数与返回在 HRT v1 禁止直接传递；应改为显式 `ptr/out` 参数。
  - 规划：HRT v2 采用 **sret 指针**（第一个参数寄存器），caller 预分配，callee 写入后返回寄存器忽略。
- 不支持 vararg/具名参数；禁用浮点与向量寄存器（HRT v1 本身禁止浮点）。

调用者/被调者保存寄存器：

| ISA/ABI | Caller-saved | Callee-saved | 备注 |
| :--- | :--- | :--- | :--- |
| x86_64 (ELF/Darwin/COFF) | `rax rcx rdx rsi rdi r8 r9 r10 r11` | `rbx rbp r12 r13 r14 r15` | COFF 仍沿用 SysV 保存规则 |
| AArch64 (ELF/Darwin) | `x0..x18` | `x19..x28 x29 x30` | `x29/x30` 为 frame/return |
| RV64 (ELF) | `a0..a7 t0..t6 ra` | `s0..s11` | `s0` 作为 frame ptr |

#### 5.1.6 ABI 用例计划（`verify_asm_backend.sh --fullspec`）
- `verify_asm_backend.sh --fullspec` 覆盖 ABI 基线：多 ABI（Darwin/ELF/COFF）与多 ISA（x86_64/AArch64/RV64）输出，验证符号前缀与指令形态。
- fullspec 输出必须包含 `.rodata/.data/.bss` 与 `g_const/g_data/g_zero`（验证数据段与初始化路径）。
- `--layout-out` 输出必须包含 `type Person`/`type Pair`（验证布局 hash/offset 输出格式）。
- 汇编中必须出现 `__cheng_concat_str`（验证运行时 ABI 绑定点）。

### 5.2 指令选择与寄存器分配
最小化版本：
- 线性扫描寄存器分配
- 无复杂指令融合
- 生成稳定、可预测的指令序列

#### 5.2.1 最小优化管线（P2-01）
- 开关：`CHENG_ASM_OPT=1` 启用；`CHENG_ASM_OPT=0/off/none` 回退。
- 报告：`CHENG_ASM_OPT_REPORT=<path>` 输出优化报告（removed_* 计数 + 规则列表）。
- 策略（v1，后端后置 peephole）：
  - `drop_dup_jump`：去重连续无条件跳转到同一标签。
  - `drop_fallthrough_jump`：移除紧邻目标标签的无条件跳转。
  - `drop_unreachable`：移除无条件跳转/ret 后、下一个 label 之前的无效指令。
- 回归用例：`examples/asm_opt_pipeline.cheng` + `sh src/tooling/verify_asm_opt.sh`（可选 `--skip-perf`）。

### 5.3 数据段与初始化规则（与 `asm_emit_layout` 对齐）
- 段映射（按 ABI 固定）：
  - Darwin：`.section __TEXT,__const` / `.section __DATA,__data` / `.section __DATA,__bss`
  - ELF：`.section .rodata` / `.data` / `.bss`
  - COFF：`.section .rdata` / `.data` / `.bss`
- 常量字符串：去重后落在 `.rodata`，标签形如 `.LstrN`，**NUL 结尾**，对齐 1 字节。
- 全局 `const`：
  - **必须初始化**；默认进入 `.rodata`。
  - 字符串常量 → 生成指向 `.LstrN` 的指针常量。
  - 整数常量 → `.byte/.short/.long/.quad`（按类型/推断大小）。
- 全局 `var/let`：
  - 无初始化 → `.bss`（` .zero <size>`）。
  - 有初始化 → `.data`；若值为 `0` 且非 `const`，折叠为 `.bss`。
  - 字符串初始化 → `.data` 中存放指向 `.LstrN` 的指针。
- 类型缺省：无显式类型且无初始值时，默认 `int32`（4 字节）。
- 初始化表达式：仅允许编译期常量表达式；**不生成**动态构造函数或运行时初始化序列。
- 初始化/收集顺序（稳定）：`const` 整数表 → 字符串池 → 类型布局 → 顶层全局（按 AST 顺序）。
- 输出顺序：`.rodata` → `.data` → `.bss`；条目按收集顺序稳定输出。
- 对齐指令：Darwin 优先 `.p2align`（2 的幂），否则 `.balign`；ELF/COFF 使用 `.balign`。

### 5.4 类型布局规范（size/align/offset/hash）
- 基础尺寸/对齐（默认配置）：`int32=4/align4`、`int64=8/align8`、`bool=1/align1`、`char=1/align1`、`ptr/usize/str/cstring=8/align8`。
- `enum`：HRT v1 统一按 `int32`（size=4/align=4）；禁止自定义宽度。
- `align_up(x, a)` 定义为 `(x + (a-1)) & ~(a-1)`；所有字段 offset 与最终 size 均使用该规则对齐。
- 禁止 `packed`/自定义对齐语义（HRT v1 仅允许自然对齐与自动 padding）。
- `array[T, N]`：`N` 必须为常量；`size = sizeof(T) * N`，`align = align(T)`。
- `tuple`：字段按声明顺序顺排；`offset = align_up(prev, field_align)`；最终 `size = align_up(total, max_align)`。
- `object`：若有基类，基类布局在前；随后字段按声明顺序顺排并按对齐插入 padding。
- `case` 分支（union）：分支布局取 **最大 size/align**，并按最大对齐插入；若需区分分支，需显式 `tag` 字段。
- `ref object`：对外类型为指针；布局以隐藏类型 `NameObj` 记录字段 offset。
- `seq/table`：布局为指针句柄（HRT v1 禁用动态容器，仅用于 ABI 占位）。
- 固定容量容器（`seq[T,N]`/`str[N]`/`Table[V,N]`）：布局以 `--layout-out` 输出为准；业务代码不得硬编码内部字段偏移。
- 布局 hash：FNV-1a 64-bit，初始值 `1469598103934665603`，乘子 `1099511628211`；输入为 type 名称 + size/align + 每个 field 的 name/offset/size/align。
- 布局输出：`chengc.sh --layout-out:<path>` 或设置 `CHENG_ASM_LAYOUT_OUT`。
  - 输出包含 `layout_version=1` 与各类型 `type <name> size=<n> align=<n> hash=0x...` 及 `field <name> offset=<n> size=<n> align=<n>`。

---

## 6. WCET 与验证闭环

### 6.1 静态检查
新增 `hrt_verify` pass：
- 检查动态分配、不可界定循环、动态线程等
- `@bound` 必须齐全；`@wcet` 不做强制校验（由 WCET 门禁脚本/外部工具负责）

### 6.2 动态验证
提供 `hrt_sim` 模式：
- 在 macOS 上运行“确定性模拟”
- 统计周期内耗时，验证 WCET 约束

### 6.3 工具链闭环
当前验收命令：
```
sh src/tooling/verify_hrt_runtime_constraints.sh --config:configs/hrt_runtime_constraints.env
sh src/tooling/build_hrt_runtime.sh
CHENG_MM_ATOMIC=0 CHENG_MM=off CHENG_ASM_RT=hard bin/cheng --mode:hrt --file:examples/hrt_fullspec.cheng --out:chengcache/hrt_fullspec.s
sh src/tooling/verify_hrt_backend.sh
sh src/tooling/verify_asm_backend.sh --fullspec --hrt
```
一键验收：
```
./verify.sh
```

> 说明：`verify_asm_backend.sh --fullspec --hrt` 默认按 **HRT v1** 运行；
> v2 用例（`case/@task`）由 `verify_hrt_backend.sh` 在内部以 `CHENG_HRT_PROFILE=2` 进行**单独验证**，
> 因此**不应**在全局导出 `CHENG_HRT_PROFILE=2` 运行 fullspec（否则 v1 负例会被放行而失败）。
> 如需单独验证 v2 用例，可直接运行：
> - `CHENG_HRT_PROFILE=2 bin/cheng --mode:hrt --file:examples/hrt_case_ok.cheng --out:chengcache/hrt/hrt_case_ok.s`
> - `CHENG_HRT_PROFILE=2 bin/cheng --mode:hrt --file:examples/hrt_task_ok.cheng --out:chengcache/hrt/hrt_task_ok.s`

新增 AArch64 端到端实际链路（asm -> obj -> bin）：
```
clang --target=arm64-apple-darwin -Isrc/stdlib/bootstrap -c src/stdlib/bootstrap/system_helpers.c -o chengcache/system_helpers_a64.o
sh src/tooling/asm_aarch64_e2e.sh --file:examples/asm_fullspec.cheng \
  --target:aarch64-apple-darwin --abi:darwin --out:chengcache/asm_fullspec_a64_macos --ldflags:chengcache/system_helpers_a64.o

CHENG_ASM_SYSROOT="$ANDROID_SYSROOT" sh src/tooling/asm_aarch64_e2e.sh \
  --file:examples/asm_fullspec.cheng --target:aarch64-linux-android --abi:elf \
  --out:chengcache/asm_fullspec_android
```
注：其他 AArch64 目标同理，使用目标工具链编译 `system_helpers.c` 并通过 `--ldflags` 引入目标对象。
本机 macOS（aarch64/darwin）验证（2026-01-24）：
- 通过：`examples/asm_const_return.cheng`、`examples/asm_g1_control_flow.cheng`、`examples/asm_fullspec.cheng`（需 `system_helpers_a64.o` 参与链接）。

常用环境变量：
- `CHENG_ASM_CC` / `CHENG_ASM_CC_ARGS` / `CHENG_ASM_CC_TARGET`（交叉工具链）
- `CHENG_ASM_FLAGS` / `CHENG_ASM_LDFLAGS`
- `CHENG_ASM_SYSROOT` / `CHENG_ASM_SDKROOT`
- `CHENG_ASM_STARTUP` / `CHENG_ASM_LINKER_SCRIPT`

### 6.4 Golden 产物与差异策略（与 CI 对齐）
- 基线目录：`artifacts/hrt/golden/<target>/`（由 `hrt_e2e_matrix.sh` 读取）。
- 推荐基线：**优先 `asm`**（文本 diff，稳定性最高）；`obj/bin` 仅在 startup+linker 脚本固定且工具链可复现时启用。
- 更新基线（示例）：
  - `sh src/tooling/hrt_e2e_matrix.sh --golden-root:artifacts/hrt/golden --golden-kind:asm --golden-update`
- 差异阈值：**0（严格一致）**；不支持模糊阈值。
- 差异策略：`asm` 使用 `diff -u`，`obj/bin` 使用 `cmp`；失败时保存 `golden.diff`。
- 完整规范与 hash 稳定性规则：见 `artifacts/hrt/golden/README.md`。

#### 6.4.1 Hash 稳定执行清单（可操作）
- 固定工具链版本与目标 triple；CI/本地必须一致。
- 优先 `asm` golden；`obj/bin` 仅在 startup/linker 固定且工具链可复现时启用。
- 禁用构建指纹（示例）：
  - ELF：`CHENG_ASM_LDFLAGS="-Wl,--build-id=none"`
  - Darwin：`CHENG_ASM_LDFLAGS="-Wl,-no_uuid"`
  - COFF：`CHENG_ASM_LDFLAGS="-Wl,/Brepro"`（lld-link/link.exe）
- 固定时间戳：设置 `SOURCE_DATE_EPOCH`；禁止 `__DATE__/__TIME__` 等编译期时间戳。
- 固定输入顺序：startup → asm obj → 运行时库（如有）→ 其他对象；任何变更需更新 golden。

#### 6.4.2 调试/可观测工具链（P2-03）
- 行列/符号映射：`sh src/tooling/hrt_symbol_map.sh --src:examples/asm_fullspec.cheng --asm:chengcache/asm_fullspec_a64_macos.s --out:artifacts/hrt/trace/symbol_map.tsv`
- asm/obj 差异：`sh src/tooling/hrt_asm_obj_diff.sh --old-asm:artifacts/hrt/golden/<target>/<file>.s --new-asm:<current>.s --out-dir:artifacts/hrt/trace/diff`
- trace 模板：`sh src/tooling/hrt_trace.sh --name:<id> --cmd:"<repro>" --run --allow-fail`（产物：`artifacts/hrt/trace/<id>/trace.md`）
- 闭环示例：`artifacts/hrt/trace/2026-01-24_p0_macos_x64_concat/trace.md`

### 6.5 性能与回归基线（P1-06）
关键基准（当前 v1）：
- `asm_fullspec`：`examples/asm_fullspec.cheng`（`--mode:asm`）
- `hrt_fullspec`：`examples/hrt_fullspec.cheng`（`--mode:hrt`，`CHENG_MM=off`）

基准脚本与指标记录：
- 生成：`sh src/tooling/hrt_perf_bench.sh`  
  - 输出：`artifacts/hrt/perf/metrics.env`（KV 记录：`<bench>.<metric>=<value>`）
  - 默认重复：`HRT_PERF_REPEAT=3`（取 **最小值** 降噪；可改为 1）
- 更新基线：`sh src/tooling/hrt_perf_bench.sh --update-baseline`  
  - 产物：`artifacts/hrt/perf/baseline.env`

变更门禁策略（默认值，可覆盖）：
- 入口：`sh src/tooling/verify_hrt_perf.sh`
- 时间阈值：`compile_ms` **≤ baseline * (1 + 10%)**
- 体积阈值：`asm_bytes` **≤ baseline * (1 + 5%)**
- 覆盖：`--max-regression-pct:<n>` / `--max-size-growth-pct:<n>` 或环境变量 `HRT_PERF_MAX_REGRESSION_PCT` / `HRT_PERF_MAX_SIZE_GROWTH_PCT`
- 解释：脚本输出 `base/cur/delta%/limit`，用于定位波动原因

### 6.6 WCET 友好优化策略与阈值（P2-02）
核心原则：**不引入新的不确定优化；任何优化不得扩大 WCET 上界**。启用优化必须有可追溯的策略项与报告对比。

禁止扩大 WCET 的优化清单（v1）：
- **全局/跨函数激进内联**：若引入额外分支或指令数增长，需保持默认关闭。
- **大规模循环展开/软件流水**：除非有固定上界且 WCET 报告证明不劣化，否则禁用。
- **自动向量化/自动 SIMD**：HRT v1 默认禁用（时序/缓存/流水不可预测）。
- **分支预测/投机式重排**：不引入依赖预测的指令重排或 spec-load。
- **PGO/机器学习驱动优化**：依赖运行时统计，禁止进入 HRT 模式。
- **任何提升代码体积但无法解释收益的优化**：以“可解释”为前提，不满足则禁用。

允许的 WCET 友好优化（白名单，需保持语义等价）：
- 常量折叠、死代码消除、常量传播。
- 基本块内的局部 CSE/复制传播/无条件跳转折叠。
- 简单窥孔优化（指令数减少或不增）。
- 消除冗余 load/store（仅限同一基本块/无别名路径）。

阈值策略（报告对比）：
- 以 **baseline** 与 **current** 的 `wcet_us` 对比，**必须满足 `current <= baseline`**。
- 同时校验 `target/function/method/tool/period_ns/budget_us` 一致，确保可比性。
- 外部工具产出需先对齐到 `wcet_report.json`（见 §13），再做对比。

报告对比说明（可追溯）：
1) 生成 baseline：保存 `artifacts/hrt/wcet_baseline.json`（`build_id` 可不同）。
2) 生成 current：`artifacts/hrt/wcet_report.json`。
3) 比对字段：`wcet_us`（核心）、`target`、`function`、`method`、`tool`、`period_ns`、`budget_us`。
4) 若 `wcet_us` 下降或持平 → 允许；上升 → 禁止该优化并记录原因。

简易对比命令（示例）：
```sh
base="$(sed -n 's/.*\"wcet_us\"[[:space:]]*:[[:space:]]*\\([0-9][0-9]*\\).*/\\1/p' artifacts/hrt/wcet_baseline.json | head -n 1)"
cur="$(sed -n 's/.*\"wcet_us\"[[:space:]]*:[[:space:]]*\\([0-9][0-9]*\\).*/\\1/p' artifacts/hrt/wcet_report.json | head -n 1)"
test "$cur" -le "$base"
```

---

### 6.7 版本化与效果系统安全放行（P2-06/P2-07）
目标：让 ABI/IR/运行时变更可追溯可回滚，并通过效果注解自动放行安全库函数。

#### 6.7.1 版本策略（ABI / IR / Runtime）
- **ABI 版本**：影响可链接/可复用的二进制约束（结构体布局、调用约定、导出符号）。  
  - 变更规则：破坏兼容 → **Major+1**；新增但兼容 → **Minor+1**；仅修复 → **Patch+1**。
- **IR 版本**：影响后端/工具链的中间表示格式与语义；变更需同步版本并记录到产物清单。
- **Runtime 版本**：`hrt_runtime` 运行时符号集与行为；任何符号增删改均需更新版本。
- **追溯与回滚**：每次构建必须在 `artifacts/hrt/<target>/<build_id>/manifest.json` 记录 `{abi_version, ir_version, runtime_version, git_rev, toolchain}`；回滚时以 manifest + golden 为依据恢复版本。
- **推荐入口**：`sh src/tooling/hrt_version_manifest.sh --out:artifacts/hrt/version_manifest.json --target:$CHENG_ASM_TARGET`  
  - 可选环境变量：`HRT_ABI_VERSION` / `HRT_IR_VERSION` / `HRT_RUNTIME_VERSION` / `HRT_EFFECTS_VERSION`。

#### 6.7.2 效果注解规则（`@no_alloc` / `@no_block`）
- **用途**：标记“可安全放行”的库函数，用于 CI 自动 allowlist。
- **位置**：注解必须紧邻 `fn` 之前（允许注释/空行，禁止插入其他语句）。
- **规则**：  
  - `@no_alloc`：禁止堆分配/动态扩容/隐式分配；仅允许固定容量容器与显式内存操作。  
  - `@no_block`：禁止不可界定阻塞（文件/网络/锁等待/睡眠等）。  
  - 同时标注表示 **无分配且无阻塞**（最严格放行级别）。

#### 6.7.3 CI 自动放行（allowlist 生成）
- 生成 allowlist：`sh src/tooling/hrt_effects_allowlist.sh --root:src/stdlib/bootstrap --out:artifacts/hrt/effects_allowlist.json`
- 校验入口：`sh src/tooling/verify_hrt_effects.sh --root:src/stdlib/bootstrap --out:artifacts/hrt/effects_allowlist.json`
- CI 集成：`verify.sh` / `hrt_ci_matrix.sh` 已调用 `verify_hrt_effects.sh`，用于自动放行标注函数。

## 7. 多平台开发与交叉编译

本机（macOS）作为开发/验证平台：
- 运行 `--mode:hrt` 生成 Darwin 汇编
- 使用 `clang`/`as` 链接调试

Windows：
- 生成 COFF 汇编（`--abi:coff`），对接 MSVC/LLVM 链接器

移动端：
- Android：`--target:aarch64-linux-android`（NDK 工具链）
- iOS：`--target:aarch64-apple-ios`（Xcode 工具链）
- HarmonyOS：`--target:aarch64-unknown-ohos`（对应 SDK/工具链）

交叉编译（硬实时/嵌入式）：
- `--target:aarch64-unknown-elf` / `--target:riscv64-unknown-elf`
- 配套 RTOS linker script（`src/tooling/rtos/*/linker.ld`）

### 7.1 交叉工具链环境变量（与 `asm_*_e2e.sh` 对齐）
| 变量 | 作用 | 说明 |
| :--- | :--- | :--- |
| `CHENG_ASM_TARGET` | 目标 triple | 影响 ISA/ABI 选择 |
| `CHENG_ASM_ABI` | ABI (`darwin/elf/coff`) | 数据段/符号前缀规则 |
| `CHENG_ASM_MODE` | `asm/hrt` | 控制是否启用 HRT 约束 |
| `CHENG_ASM_CC` | 编译/链接器 | 默认 `clang` |
| `CHENG_ASM_CC_ARGS` | 额外 CC 参数 | 例如 `--sysroot` 或 SDK |
| `CHENG_ASM_CC_TARGET` | `--target` 值 | 为空时可省略（GCC 交叉工具链） |
| `CHENG_ASM_FLAGS` | 汇编/编译 flags | 影响 `.o` 生成 |
| `CHENG_ASM_LDFLAGS` | 链接 flags | 影响 `.bin` 可复现性 |
| `CHENG_ASM_SYSROOT` | sysroot | Android/ELF 目标 |
| `CHENG_ASM_SDKROOT` | SDK root | iOS/HarmonyOS（会回填 sysroot） |
| `CHENG_ASM_STARTUP` | 启动文件 | `crt0.o` / `crt0.S` 输出 |
| `CHENG_ASM_LINKER_SCRIPT` | linker script | RTOS/裸机 `linker.ld` |

### 7.2 Linker/启动代码收敛策略
- OS 目标默认依赖系统 CRT；若启用 `-nostdlib`，必须显式提供 `CHENG_ASM_STARTUP` 与入口符号。

#### 7.2.1 Linker/启动代码清单（规范）
| 目标 | Startup | Linker Script | 入口/关键符号 | 备注 |
| :--- | :--- | :--- | :--- | :--- |
| macOS/iOS/Android/HarmonyOS/Windows | 系统 CRT（默认 `clang`/`ld`） | 系统默认 | `main` | 若 `-nostdlib` 必须显式 `CHENG_ASM_STARTUP`/`CHENG_ASM_LINKER_SCRIPT` |
| aarch64-unknown-elf | `src/tooling/rtos/aarch64/crt0.S` | `src/tooling/rtos/aarch64/linker.ld` | `_start`, `__stack_top` | `.text.boot` 入口，基址 `0x80000000`，栈 `0x10000` |
| riscv64-unknown-elf | `src/tooling/rtos/riscv64/crt0.S` | `src/tooling/rtos/riscv64/linker.ld` | `_start`, `__stack_top` | `.text.boot` 入口，基址 `0x80000000`，栈 `0x10000` |

#### 7.2.2 Linker/启动代码约束
- 裸机/RTOS 目标统一使用 `src/tooling/rtos/*/crt0.S` + `src/tooling/rtos/*/linker.ld`：
  - 入口 `_start` 调用 `main`，linker 提供 `__stack_top`。
  - `.text/.rodata/.data/.bss` 段顺序固定，保证布局与 golden 对齐。
- 当前 `crt0.S` **不负责** `.bss` 清零或 `.data` 拷贝；裸机/RTOS 需由平台或后续启动层补齐。
- 参与链接的 startup/script 必须版本受控；任何变更都视为 ABI/产物变更并更新 golden。
- 若使用 `bin` golden，建议显式禁用 build-id/UUID（例如 ELF `-Wl,--build-id=none`、Darwin `-Wl,-no_uuid`），以保证 hash 稳定。

AArch64 实际链路（推荐用脚本）：
- macOS：`sh src/tooling/asm_aarch64_e2e.sh --file:examples/asm_fullspec.cheng --target:aarch64-apple-darwin --abi:darwin --out:chengcache/asm_fullspec_a64_macos --ldflags:chengcache/system_helpers_a64.o`
- iOS：`CHENG_ASM_CC=xcrun CHENG_ASM_CC_ARGS="--sdk iphoneos clang" CHENG_ASM_SYSROOT="$(xcrun --sdk iphoneos --show-sdk-path)" sh src/tooling/asm_aarch64_e2e.sh --file:examples/asm_fullspec.cheng --target:aarch64-apple-ios --abi:darwin --out:chengcache/asm_fullspec_ios`
- Android：`CHENG_ASM_SYSROOT="$ANDROID_SYSROOT" sh src/tooling/asm_aarch64_e2e.sh --file:examples/asm_fullspec.cheng --target:aarch64-linux-android --abi:elf --out:chengcache/asm_fullspec_android`
- HarmonyOS：`CHENG_ASM_SYSROOT="$OHOS_SYSROOT" sh src/tooling/asm_aarch64_e2e.sh --file:examples/asm_fullspec.cheng --target:aarch64-unknown-ohos --abi:elf --out:chengcache/asm_fullspec_ohos`
- 裸机/RTOS：`CHENG_ASM_CC=aarch64-none-elf-gcc CHENG_ASM_CC_TARGET= CHENG_ASM_STARTUP=crt0.o CHENG_ASM_LINKER_SCRIPT=linker.ld CHENG_ASM_LDFLAGS="-nostdlib" sh src/tooling/asm_aarch64_e2e.sh --file:examples/asm_fullspec.cheng --target:aarch64-unknown-elf --abi:elf --out:chengcache/asm_fullspec_a64_elf`

Windows COFF 可复现链接建议（对齐 `asm_windows_e2e.sh`）：
- 先编译 helper（Windows）：`clang-cl /c /I src\\stdlib\\bootstrap src\\stdlib\\bootstrap\\system_helpers.c /Fo:chengcache\\system_helpers_win64.obj`
- 在 Windows 主机：`sh src/tooling/asm_windows_e2e.sh --file:examples/asm_fullspec.cheng --out:chengcache/asm_fullspec_win64 --ldflags:"chengcache/system_helpers_win64.obj"`
- 在非 Windows 主机（需 Windows SDK + lld）：  
  `clang --target=x86_64-pc-windows-msvc -Isrc/stdlib/bootstrap -c src/stdlib/bootstrap/system_helpers.c -o chengcache/system_helpers_win64.obj`  
  `sh src/tooling/asm_windows_e2e.sh --file:examples/asm_fullspec.cheng --out:chengcache/asm_fullspec_win64 \
  --ldflags:"chengcache/system_helpers_win64.obj -fuse-ld=lld -nostdlib -Wl,/entry:main -Wl,/subsystem:console -Wl,/Brepro -Wl,/INCREMENTAL:NO -Wl,/DEBUG:NONE"`
- 可复现要点：固定 SDK/clang/lld 版本；开启 `/Brepro` + `/INCREMENTAL:NO`；避免生成 PDB（或固定 PDB 路径）；统一 `SOURCE_DATE_EPOCH`。
  - 若必须产出 PDB：`/Brepro /INCREMENTAL:NO /DEBUG:FULL /PDBALTPATH:%_PDB%`（避免绝对路径与时间戳）。

RTOS 目标矩阵（模板 + 一键脚本）：
- AArch64：`sh src/tooling/hrt_rtos_e2e.sh --targets:aarch64_elf --allow-skip`
- RISC-V64：`sh src/tooling/hrt_rtos_e2e.sh --targets:riscv64_elf --allow-skip`

---

## 8. 路线图（建议）

1) **HRT v0**：常量返回 + 基础函数 + x86_64 Darwin 验证（已完成）  
2) **HRT v1**：控制流（if/while/for-range）+ 栈变量 + 直接调用（已完成，子集）  
3) **HRT v2**：运行时绑定（mem/str/seq）+ P0 平台 ABI/目标矩阵收敛  
4) **HRT v3**：RTOS 任务模型 + WCET 验证 + P0 平台闭环 CI  
5) **HRT v4**：全语法覆盖 + P1 交叉平台扩展  
6) **HRT v5**：性能/优化/生态工具链完善（对应 P2）  

---

## 9. 与现有 ASM v0 的关系

当前 `stage0c` ASM 已从“常量返回 v0”扩展为**可用子集**（if/while/for-range/局部变量/直接调用）。  
仍保留**v0 回退路径**用于未支持的 target/ABI。硬实时后端继续：
- 保持 `--mode:asm` 与 `--mode:hrt` 并存
- 在子集基础上逐步扩展语法覆盖与运行时绑定

---

## 10. 待确认项

- `@task/@period/@deadline` 语法与语义
- 是否允许有限度 `arc`（retain/release）或仅栈/静态

---

## 11. 动态特性的硬实时解决方案（最佳实践）

硬实时（HRT）并不意味着“放弃”现代特性，而是用**确定性**的方式重新实现它们。针对 JSON、UI、异步和 GC 等动态特性，Cheng 推荐以下 HRT 适配模式。

适用范围（v1）：
- 禁用 `async/await` / `@task` / `?` / 原子 RC；并发相关能力延后至 P1。
- 仅允许固定容量容器与显式上界（`@bound` / 固定 Ring / Arena）。

运行前提（建议）：
- 绑定核心、关闭 DVFS、锁定工作集内存，避免运行期抖动。
- 明确中断/IO 上界与调度策略；所有阻塞调用必须可界定。

### 11.1 核心替代模式表

| 动态特性 | 主流动态方案 (禁止) | HRT 替代方案 (推荐) | 核心思想 | 容量/上界/拒绝策略 |
| :--- | :--- | :--- | :--- |
| **JSON/数据交换** | 动态 DOM 树 (`JObject`) | **Schema-First** / **Zero-Copy** | **不构建树**。直接绑定静态结构体，或使用游标线性扫描。 | 固定 schema + 固定 buffer；禁止递归解析；超限拒绝/降级 |
| **UI/DOM** | 保留模式对象树 (Widget Tree) | **IMGUI** (立即模式) / **ECS** | **不使用对象**。每一帧重建 UI 描述，或基于数组/ID 管理状态。 | 状态与命令缓冲固定容量；超限裁剪/丢弃 |
| **异步 I/O** | 堆分配 Future + 动态调度 | **轮询 + Ring Buffer**（v1）/ **静态状态机**（P1） | **不堆分配**。轮询固定缓冲；P1 编译器展开状态机。 | Ring 大小固定；超限丢弃/回压 |
| **垃圾回收 (GC)** | 堆扫描 + STW 暂停 | **Arena (区域内存)** / **Object Pool** | **不随机释放**。按帧/任务批量重置指针 (Arena)，O(1) 释放且无碎片。 | Arena 容量固定；跨周期引用禁止 |
| **并发/共享状态** | 动态线程 + 可变共享 | **固定拓扑队列** / **只读快照** | **边界清晰**。仅允许固定通道和只读共享。 | SPSC/MPMC 固定队列；超限拒绝 |
| **错误处理** | 异常/隐式 panic | **显式错误码/Result 分支** | **无隐式路径**。拒绝 `?` 与隐式 panic。 | 所有错误分支显式处理 |

### 11.2 典型实现案例

- **Web 后端 (Hybrid)**: 网络层使用标准模式处理 JSON/Auth；核心撮合/计算层使用 HRT 模式 + Arena 内存，通过 Ring Buffer 交换数据（固定容量 + 溢出拒绝）。
- **嵌入式 UI**: 使用 IMGUI 模式，UI 状态仅为一个 `Context` 结构体，渲染指令写入预分配 Vertex Buffer。
- **高频网络**: 网卡驱动直接写入用户态 Ring Buffer，HRT 任务轮询处理，零拷贝零系统调用（固定上界）。

### 11.3 目标硬件等级与保证差异

| 等级 | 典型硬件 | 保证范围 | 关键限制 |
| :--- | :--- | :--- | :--- |
| **Tier 1 (Hard RT)** | MCU/RTOS（AArch64/ARMv7/RISC-V） | 可证明 WCET | 必须固定频率/禁抢占干扰/固定内存上界 |
| **Tier 2 (Deterministic Latency)** | A-Core/x86（Linux/Android/macOS） | 无 GC/无分配抖动、低尾延迟 | 受缓存/乱序/OS 调度影响，WCET 仅作上界估计 |

建议：
- Tier 1 强制使用 RTOS/裸机，所有中断与阻塞必须有上界。
- Tier 2 仅承诺“确定性延迟”，避免宣称严格 WCET。

### 11.4 部署与配置清单（最小模板）

- **CPU**：绑核、隔离实时核心，关闭频率自动调节（DVFS）。
- **内存**：锁定工作集（mlock/预触页）；禁用透明大页与过度换页。
- **中断**：IRQ 优先级固定，限制可抢占源；关键路径禁用可变 IO。
- **调度**：固定周期与优先级；任务上界和超时策略显式化。
- **监控**：watchdog + 运行期计时器，超限强制上报/降级。
- **模板**：`configs/hrt_runtime_constraints.env`
- **检查**：`sh src/tooling/verify_hrt_runtime_constraints.sh --config:configs/hrt_runtime_constraints.env`

---

## 12. 最小生产流程（闭环手册）

目标：让新项目按“配置 → 构建 → 验证 → 交付”完成一次可运行闭环。

### 12.1 项目准备
- 选择硬件等级：Tier1（RTOS/裸机）或 Tier2（Linux/Android/macOS）。
- 引入运行约束模板：`configs/hrt_runtime_constraints.env`。
- 固定关键环境变量：`CHENG_MM_ATOMIC=0` 与 `CHENG_MM=off`（HRT v1 强制）。

### 12.2 编译（HRT）
- 入口 1：`chengc.sh --emit-hrt --file:<input.cheng> --out:<out.s>`
- 入口 2：`bin/cheng --mode:hrt --file:<input.cheng> --out:<out.s>`
- 默认前端：`--emit-hrt/--emit-asm` 固定使用 `bin/cheng`（stage0c），不会触发 `stage1_runner`；如需覆盖，使用 `CHENG_ASM_FRONTEND`
- 强制约束：`CHENG_MM_ATOMIC=0` 与 `CHENG_MM=off`（HRT v1 必选）；建议显式设置 `CHENG_ASM_RT=hard`
- HRT v2（case/of 等）需显式设置 `CHENG_HRT_PROFILE=2`（默认仍为 v1）
- 生产闭环已移除旧 IR 链路（`CHENG_DISABLE_NIFC` 不再使用）
- 产物：`chengcache/hrt/*.s`

#### 12.2.1 RWAD proofs（HRT 入口示例）
目的：将链上证明校验核心改为固定容量 + 无字符串/无动态分配，满足 HRT 子集。

- HRT 版模块：`/Users/lbcheng/.cheng-packages/RWAD-blockchain/src/proofs.cheng`
- 标准版模块：`/Users/lbcheng/.cheng-packages/RWAD-blockchain/src/proofs_std.cheng`
- HRT 入口：`/Users/lbcheng/.cheng-packages/RWAD-blockchain/src/proofs_hrt_main.cheng`

编译示例：
`CHENG_MM_ATOMIC=0 CHENG_MM=off bin/cheng --mode:hrt --file:/Users/lbcheng/.cheng-packages/RWAD-blockchain/src/proofs_hrt_main.cheng --out:/tmp/proofs_hrt_main.s`

说明：
- HRT 版只保留 Merkle/SPV 核心与固定容量路径，外部必须预解析并填充结构体。
- `ProofType/Side` 改为枚举；`str/seq/float` 全部移除。
- HRT 后端禁用 `[]` 中括号表达式；固定数组需通过 `ptr_add` 或 `seqFixedGet/Set` 的封装访问。

### 12.3 链接（目标平台）
- 使用 `src/tooling/asm_*_e2e.sh`（推荐）或直接调用交叉工具链。
- 若依赖 HRT 运行时：先 `sh src/tooling/build_hrt_runtime.sh`，链接时加 `-L chengcache/hrt_runtime -lhrt_runtime`
- 产物：`chengcache/<target>/*.o` 与目标二进制。

### 12.4 验证（闭环门禁）
- 语法/语义门禁：`sh src/tooling/verify_hrt_backend.sh`
- 汇编后端门禁：`sh src/tooling/verify_asm_backend.sh --fullspec --hrt`
- 驱动侧闭环门禁：`sh src/tooling/verify_hrtnet_driver.sh`
- 文件驱动闭环门禁：`sh src/tooling/verify_hrtfile_driver.sh`
- 运行约束门禁：`sh src/tooling/verify_hrt_runtime_constraints.sh --config:configs/hrt_runtime_constraints.env`
- 效果放行门禁：`sh src/tooling/verify_hrt_effects.sh --root:src/stdlib/bootstrap`
- 一键验收：`./verify.sh`

#### 12.4.1 `./verify.sh` 使用说明
- 入口：`./verify.sh`（仓库根目录执行）
- 前置：`configs/hrt_runtime_constraints.env` 可用；`bin/cheng` 缺失会自动 `src/tooling/build_stage0c.sh`
- 产物：`artifacts/hrt/wcet_report.json`、`artifacts/hrt/version_manifest.json`、`artifacts/hrt/effects_allowlist.json`、`chengcache/hrt_runtime/libhrt_runtime.a`、`verify_*` 日志
- 可选开关：`CHENG_STAGE1_FULLSPEC=1`（纳入 stage1 fullspec）、`CHENG_HRT_ALLOW_SKIP=1`（driver 允许 skip）、`CHENG_HRT_E2E=1`（运行 `hrt_e2e_matrix.sh`）、`CHENG_ASM_TARGET=<triple>`（WCET 报告目标）
- 成功标识：脚本末尾输出 `verify ok`

### 12.5 交付与归档（建议规范）
建议统一归档到：`artifacts/hrt/<target>/<build_id>/`
- `manifest.json`：`target/abi/toolchain/git_rev/sha256` 关键字段
- 产物：`*.s/*.o/*.bin`
- 日志：`verify_*.log` 与构建日志

### 12.6 GUI/Dapp 跨端闭环（Android + Desktop）
目标：Dapp 前后端统一 Cheng 原生实现，并在桌面与移动端复用同一套逻辑。

**源码与工程位置**
- GUI 库：`/Users/lbcheng/.cheng-packages/cheng-gui`
- Desktop：`/Users/lbcheng/unimaker_desktop`
- Android Host：`/Users/lbcheng/UniMaker/app/src/main`

**构建与同步**
1) Desktop 构建（在 `unimaker_desktop` 目录）：  
   `sh tooling/build_unimaker_desktop.sh --compiler:stage1`
2) Mobile 导出（在 `cheng-lang` 根目录）：  
   `CHENG_DEFINES=android,mobile_host CHENG_IDE_ROOT=/Users/lbcheng/.cheng-packages/cheng-ide CHENG_GUI_ROOT=/Users/lbcheng/.cheng-packages/cheng-gui CHENG_PKG_ROOTS=/Users/lbcheng/.cheng-packages/cheng-libp2p,/Users/lbcheng/.cheng-packages/cheng-ai,/Users/lbcheng/.cheng-packages/RWAD-blockchain,/Users/lbcheng/unimaker_desktop sh src/tooling/build_mobile_export.sh /Users/lbcheng/unimaker_desktop/main.cheng --name:unimaker_dapp --out:mobile_build/unimaker_dapp --with-bridge`  
   （`CHENG_PKG_ROOTS` 使用逗号/分号分隔；包含 RWAD-blockchain 与 `unimaker_desktop` 根目录，确保 stage1_runner 可解析工程内 import）
   （可选加速：`CHENG_DEPS_PREWARM=1 CHENG_DEPS_JOBS=8`，并行预热模块依赖与词法缓存）
3) Android 对接：  
   - `mobile_build/unimaker_dapp/unimaker_dapp.c` → `UniMaker/app/src/main/cpp/cheng_mobile/unimaker_dapp.c`  
   - `mobile_build/unimaker_dapp/bridge/*` → `UniMaker/app/src/main/cpp/cheng_mobile/`  
   - `assets_manifest.txt` → `UniMaker/app/src/main/assets/assets_manifest.txt`

**验证**
- Desktop：运行 `unimaker_desktop` 二进制。
- Android：`./gradlew :app:assembleDebug` 后启动 `com.cheng.mobile.ChengActivity`。

**闭环产物**
- `UniMaker/app/src/main/cpp/cheng_mobile/unimaker_dapp.c`
- `UniMaker/app/src/main/assets/assets_manifest.txt`
- `unimaker_desktop/unimaker_desktop`

---

### 12.7 闭环验收清单（P0）
- `./verify.sh` 输出 `verify ok`
- `verify_hrt_backend.sh` 与 `verify_asm_backend.sh --fullspec --hrt` 全绿
- `verify_hrt_runtime_constraints.sh` 输出 `hrt runtime constraints ok`
- `chengcache/hrt_runtime/libhrt_runtime.a` 与目标 `.o/.bin` 可追溯
- 归档目录 `artifacts/hrt/<target>/<build_id>/` 含 `manifest.json` 与日志

### 12.8 故障定位速查
- HRT 语法/语义失败：对照 `docs/hrt-verify-diagnostics.md`
- ASM 输出缺失/为空：检查 `chengcache/hrt` 与 `chengcache/asm` 产物
- 链接失败：核对 `CHENG_ASM_*` 与 SDK/NDK/sysroot 配置

### 12.9 规范对齐报告（HRT Overlay）

对齐基线：`docs/cheng-formal-spec.md` + `docs/hrt-profile-v1.md`。  
对齐原则：语法不变，HRT 以 Profile 方式收紧语义；所有收紧项必须有负例与验证入口。

对齐结论（v1）：
- 控制流：循环必须有界（`@bound`/常量上界）；递归禁止。
- 语义收紧：`case/of` 禁止；v2 需 `CHENG_HRT_PROFILE=2`。
- 类型/内存：禁止浮点、动态容器/字面量；仅允许固定容量容器。
- 并发/异步：`async/await/@task/?` 禁止；v2 需 `CHENG_HRT_PROFILE=2`。
- 运行时/外部：非白名单 `importc/runtime` 调用禁止；ARC/ORC 关闭（`CHENG_MM=off`/`CHENG_MM_ATOMIC=0`）。

验证证据：
- 用例矩阵：§15（`examples/hrt_bad_*` 负例）与附录 A。
- 门禁入口：`sh src/tooling/verify_hrt_backend.sh`。
- 诊断规范：`docs/hrt-verify-diagnostics.md`。

## 13. WCET 报告与门禁（v0 约定）

### 13.1 报告格式（JSON，稳定字段）
建议输出到：`artifacts/hrt/wcet_report.json`

稳定字段（最终报告必须包含，类型严格；`verify_hrt_wcet.sh` 读取顶层字段）：

| 字段 | 类型 | 必须 | 说明 |
| :--- | :--- | :--- | :--- |
| `target` | string | 是 | 目标三元组（默认 `CHENG_ASM_TARGET`） |
| `build_id` | string | 是 | UTC 时间戳（`YYYY-MM-DDTHH:MM:SSZ`） |
| `function` | string | 是 | 入口函数名（v0 仅支持单函数汇总） |
| `wcet_us` | int | 是 | WCET（微秒，整数） |
| `budget_us` | int | 是 | 预算（微秒，整数，`0` 表示未设置） |
| `period_ns` | int | 是 | 周期（纳秒，整数，`0` 表示未设置） |
| `method` | string | 是 | `static` / `dynamic` / `hybrid` / `manual` |
| `tool` | string | 是 | 工具名或归一化标识（例：`manual` / `aiT`） |
| `pass` | bool | 是 | `budget_us>0 && wcet_us>budget_us` → `false`，否则 `true` |

注意事项：
- 所有数值字段必须为**整数**（脚本不解析浮点）。
- 为避免脚本误匹配，**不要在报告中重复使用 `wcet_us`/`budget_us`/`pass` 键名**。
- v0 仅做**单函数**汇总；多函数需拆分为多个报告文件。

字段来源与优先级（`hrt_wcet_report.sh`）：
1) 显式 CLI 参数（`--wcet-us/--budget-us/--period-ns/--method/--tool/--target/--function`）
2) `--from` 外部输入文件（仅填补未显式设置的字段）
3) 配置/环境变量（`HRT_WCET_MEASURED_US` / `HRT_WCET_BUDGET_US` / `HRT_PERIOD_NS`）
4) 默认值（`wcet_us/budget_us/period_ns=0`，`method=static`，`tool=manual`，`function=main`）

示例（已验证 target：`aarch64-unknown-elf`）：
```json
{
  "target": "aarch64-unknown-elf",
  "build_id": "2026-01-24T12:00:00Z",
  "function": "main",
  "wcet_us": 800,
  "budget_us": 1000,
  "period_ns": 1000000,
  "method": "static",
  "tool": "manual",
  "pass": true
}
```

### 13.2 外部工具适配字段（`--from` 输入）
`src/tooling/hrt_wcet_report.sh --from:<path>` 会从外部结果中**补齐缺失字段**。支持三种格式：

- `--from-format:json|kv`：识别键 `wcet_us` / `budget_us` / `period_ns` / `method` / `tool` / `target` / `function`
- `--from-format:plain`：取文件内**第一个整数**作为 `wcet_us`

推荐的工具适配扩展字段（可选；不与稳定字段重名）：
| 字段 | 类型 | 说明 |
| :--- | :--- | :--- |
| `adapter_report_path` | string | 外部工具原始报告路径 |
| `adapter_tool_version` | string | 工具版本 |
| `adapter_tool_exit_code` | int | 工具退出码 |
| `adapter_units` | string | 原始单位（建议 `cycles` / `us`） |
| `adapter_wcet_cycles` | int | 原始 WCET（周期） |
| `adapter_cpu_hz` | int | CPU 频率（Hz），用于换算到微秒 |
| `adapter_notes` | string | 其他说明 |

外部工具输入示例（JSON，供 `--from` 使用；多余字段会被忽略）：
```json
{
  "wcet_us": 920,
  "budget_us": 1000,
  "period_ns": 1000000,
  "method": "static",
  "tool": "aiT",
  "target": "aarch64-unknown-elf",
  "function": "main",
  "adapter_report_path": "artifacts/hrt/tools/ait/report.json",
  "adapter_tool_version": "11.2",
  "adapter_units": "cycles",
  "adapter_wcet_cycles": 184000,
  "adapter_cpu_hz": 200000000
}
```

### 13.2.1 适配配置模板（建议路径）
目的：将外部工具产物映射为 `wcet_report.json` 的稳定字段；**脚本不做单位换算**，需由适配层先换算为 `wcet_us`。

建议路径（可按工具分目录管理；已提供示例）：
- `src/tooling/wcet_adapter_template.env`（示例模板，可按项目复制）
- 外部产物：`artifacts/hrt/tools/<tool>/report.json`
- 适配产物：`artifacts/hrt/tools/<tool>/adapter.json`（供 `--from` 使用）

模板示例（KV；由调用脚本/CI 读取并拼装参数）：
```text
# src/tooling/wcet_adapter_template.env (示例)
WCET_ADAPTER_TOOL=aiT
WCET_ADAPTER_INPUT=artifacts/hrt/tools/ait/report.json
WCET_ADAPTER_INPUT_FORMAT=json
WCET_ADAPTER_CPU_HZ=200000000
WCET_ADAPTER_TARGET=aarch64-unknown-elf
WCET_ADAPTER_FUNCTION=main
WCET_ADAPTER_METHOD=static
WCET_ADAPTER_OUT=artifacts/hrt/tools/ait/adapter.json
WCET_ADAPTER_PERIOD_NS=1000000
WCET_ADAPTER_BUDGET_US=1000
```

示例调用（适配层拼参）：
```sh
. src/tooling/wcet_adapter_template.env
sh src/tooling/hrt_wcet_adapter.sh \
  --in:"$WCET_ADAPTER_INPUT" \
  --in-format:"$WCET_ADAPTER_INPUT_FORMAT" \
  --cpu-hz:"$WCET_ADAPTER_CPU_HZ" \
  --tool:"$WCET_ADAPTER_TOOL" \
  --target:"$WCET_ADAPTER_TARGET" \
  --function:"$WCET_ADAPTER_FUNCTION" \
  --method:"$WCET_ADAPTER_METHOD" \
  --period-ns:"$WCET_ADAPTER_PERIOD_NS" \
  --budget-us:"$WCET_ADAPTER_BUDGET_US" \
  --out:"$WCET_ADAPTER_OUT"
sh src/tooling/hrt_wcet_report.sh \
  --config:configs/hrt_runtime_constraints.env \
  --tool:"$WCET_ADAPTER_TOOL" \
  --from:"$WCET_ADAPTER_OUT" \
  --from-format:json
```

### 13.2.2 IR/ASM 转换与适配器脚本
- 转换器：`src/tooling/hrt_wcet_adapter.sh` 支持 `--asm/--ir` + `--emit-input` 输出工具输入文件（ASM 默认剔除 `.file/.loc`）。
- 适配输出：生成 `adapter.json`（含 `wcet_us`/`period_ns` 等稳定字段），供 `hrt_wcet_report.sh --from` 读取。
- 示例（ASM → 工具输入）：`sh src/tooling/hrt_wcet_adapter.sh --asm:chengcache/hrt/example.s --emit-input:artifacts/hrt/tools/ait/input.s --wcet-us:800 --tool:aiT --target:aarch64-unknown-elf --out:artifacts/hrt/tools/ait/adapter.json`

### 13.2.3 示例报告（单 target）
- 示例报告：`src/tooling/wcet_samples/aarch64-unknown-elf/wcet_report.json`
- 门禁验证：`sh src/tooling/verify_hrt_wcet.sh --report:src/tooling/wcet_samples/aarch64-unknown-elf/wcet_report.json --config:configs/hrt_runtime_constraints.env`

### 13.3 最小门禁流程（建议）
1) 编译产出 `.s/.o`（HRT 模式）
2) 生成 WCET 报告：`sh src/tooling/hrt_wcet_report.sh --config:configs/hrt_runtime_constraints.env`
   （可选从源码提取：`--file:examples/hrt_wcet_ok.cheng --function:main`；外部工具结果导入：`--from:<path>`，可选 `--from-format:json|kv|plain`）
3) WCET 门禁：`sh src/tooling/verify_hrt_wcet.sh --report:artifacts/hrt/wcet_report.json`

### 13.4 目标示例（生成报告）
目标（示例）：`aarch64-unknown-elf`

```sh
sh src/tooling/hrt_wcet_report.sh \
  --out:artifacts/hrt/wcet_report.json \
  --target:aarch64-unknown-elf \
  --wcet-us:800 \
  --budget-us:1000 \
  --period-ns:1000000 \
  --method:static \
  --tool:manual
```

---

## 14. 目标矩阵验收表（P0 优先）

> 注：`asm_fullspec` 需链接 `system_helpers_<target>.o`（提供 `cheng_memcpy/cheng_memset/cheng_strlen/ptr_add`）；示例见 §2.5。

| 目标 | ABI | 脚本 | 必要环境 | 验证命令 | 状态 |
| :--- | :--- | :--- | :--- | :--- | :--- |
| macOS x86_64 | darwin | `asm_x86_64_e2e.sh` | Xcode clang（或 `CHENG_ASM_CC`/`CHENG_ASM_SDKROOT`）+ `system_helpers_x64.o` | `sh src/tooling/asm_x86_64_e2e.sh --file:examples/asm_fullspec.cheng --target:x86_64-apple-darwin --abi:darwin --out:chengcache/asm_fullspec_x64_macos --ldflags:chengcache/system_helpers_x64.o` | CI 记录：`artifacts/hrt/x86_64-apple-darwin/2026-01-24T05:05:59Z/manifest.json` |
| macOS aarch64 | darwin | `asm_aarch64_e2e.sh` | Xcode clang（或 `CHENG_ASM_CC`/`CHENG_ASM_SDKROOT`）+ `system_helpers_a64.o` | `sh src/tooling/asm_aarch64_e2e.sh --file:examples/asm_fullspec.cheng --target:aarch64-apple-darwin --abi:darwin --out:chengcache/asm_fullspec_a64_macos --ldflags:chengcache/system_helpers_a64.o` | CI 记录：`artifacts/hrt/aarch64-apple-darwin/2026-01-24T05:05:59Z/manifest.json` |
| Linux x86_64 | elf | `asm_x86_64_e2e.sh` | clang/gcc（或 `CHENG_ASM_CC`）+ `system_helpers_x64_linux.o` | `sh src/tooling/asm_x86_64_e2e.sh --file:examples/asm_fullspec.cheng --target:x86_64-unknown-linux-gnu --abi:elf --out:chengcache/asm_fullspec_x64_linux --ldflags:chengcache/system_helpers_x64_linux.o` | CI 记录：`artifacts/hrt/x86_64-unknown-linux-gnu/<build_id>/manifest.json`（缺工具链时 `--allow-skip` 记录 `status=skipped`） |
| Windows x86_64 | coff | `asm_windows_e2e.sh` | `lld/link` + `system_helpers_win64.obj` | `sh src/tooling/asm_windows_e2e.sh --file:examples/asm_fullspec.cheng --out:chengcache/asm_fullspec_win64 --ldflags:"chengcache/system_helpers_win64.obj -fuse-ld=lld -nostdlib -Wl,/entry:main -Wl,/subsystem:console"` | CI 记录：`artifacts/hrt/x86_64-pc-windows-msvc/2026-01-24T10:38:58Z/manifest.json` |
| Android aarch64 | elf | `asm_android_e2e.sh` | `NDK_SYSROOT/NDK_CLANG` | `CHENG_ASM_SYSROOT="$NDK_SYSROOT" CHENG_ASM_CC="$NDK_CLANG" sh src/tooling/asm_android_e2e.sh --file:examples/asm_fullspec.cheng --out:chengcache/asm_fullspec_android` | CI 记录：`artifacts/hrt/aarch64-linux-android/2026-01-24T05:05:59Z/manifest.json` |
| iOS aarch64 | darwin | `asm_ios_e2e.sh` | `IOS_SDKROOT` + xcrun clang | `CHENG_ASM_CC=xcrun CHENG_ASM_CC_ARGS="--sdk iphoneos clang" CHENG_ASM_SYSROOT="$IOS_SDKROOT" sh src/tooling/asm_ios_e2e.sh --file:examples/asm_fullspec.cheng --out:chengcache/asm_fullspec_ios` | CI 记录：`artifacts/hrt/aarch64-apple-ios/2026-01-24T10:32:41Z/manifest.json` |
| HarmonyOS aarch64 | elf | `asm_harmony_e2e.sh` | `OHOS_SDKROOT/OHOS_CLANG` | `CHENG_ASM_SYSROOT="$OHOS_SDKROOT" CHENG_ASM_CC="$OHOS_CLANG" sh src/tooling/asm_harmony_e2e.sh --file:examples/asm_fullspec.cheng --out:chengcache/asm_fullspec_ohos` | CI 记录：`artifacts/hrt/aarch64-unknown-ohos/2026-01-24T10:42:39Z/manifest.json`（`asm_const_return` + `aarch64-elf-gcc`，`-nostdlib`） |

---

## 14.1 RTOS 目标矩阵（P1）

| 目标 | ABI | 脚本 | 必要环境 | 验证命令 | 状态 |
| :--- | :--- | :--- | :--- | :--- | :--- |
| aarch64-unknown-elf | elf | `asm_aarch64_e2e.sh` | `aarch64-none-elf-*` | `sh src/tooling/hrt_rtos_e2e.sh --targets:aarch64_elf ...` | CI 记录：`artifacts/hrt/rtos/aarch64-unknown-elf/2026-01-24T10:37:43Z/manifest.json` |
| riscv64-unknown-elf | elf | `asm_riscv64_e2e.sh` | `riscv64-unknown-elf-*` | `sh src/tooling/hrt_rtos_e2e.sh --targets:riscv64_elf ...` | CI 记录：`artifacts/hrt/rtos/riscv64-unknown-elf/2026-01-24T10:37:58Z/manifest.json` |

---

## 14.2 目标矩阵 CI 记录索引（P0/P1）

记录入口说明：
- `hrt_e2e_matrix.sh` 归档：`artifacts/hrt/<matrix_target>/<build_id>/manifest.json`
- 单目标脚本归档（或手工归档）：`artifacts/hrt/<target_triple>/<build_id>/manifest.json`
- RTOS 归档：`artifacts/hrt/rtos/<rtos_target>/<build_id>/manifest.json`

### P0 记录
| 目标 | 记录入口 | 说明 |
| :--- | :--- | :--- |
| macOS x86_64 | `artifacts/hrt/x86_64-apple-darwin/2026-01-24T05:05:59Z/manifest.json` | 非 AArch64 记录（本机归档） |
| macOS aarch64 | `artifacts/hrt/aarch64-apple-darwin/2026-01-24T05:05:59Z/manifest.json` | 本机归档 |
| Linux x86_64 | `artifacts/hrt/x86_64-unknown-linux-gnu/<build_id>/manifest.json` | 记录已归档（缺工具链时 `--allow-skip` 生成 `status=skipped`） |
| Windows x86_64 | `artifacts/hrt/x86_64-pc-windows-msvc/2026-01-24T10:38:58Z/manifest.json` | 记录已归档（link 需 Windows CI 补齐） |
| Android aarch64 | `artifacts/hrt/aarch64-linux-android/2026-01-24T05:05:59Z/manifest.json` | 本机归档 |
| iOS aarch64 | `artifacts/hrt/aarch64-apple-ios/2026-01-24T10:32:41Z/manifest.json` | 本机归档 |
| HarmonyOS aarch64 | `artifacts/hrt/aarch64-unknown-ohos/2026-01-24T10:42:39Z/manifest.json` | 本机归档（`asm_const_return` + `aarch64-elf-gcc`，`-nostdlib`） |

### P1 RTOS 记录
| 目标 | 记录入口 | 说明 |
| :--- | :--- | :--- |
| aarch64-unknown-elf | `artifacts/hrt/rtos/aarch64-unknown-elf/2026-01-24T10:37:43Z/manifest.json` | 本机归档 |
| riscv64-unknown-elf | `artifacts/hrt/rtos/riscv64-unknown-elf/2026-01-24T10:37:58Z/manifest.json` | 本机归档 |

---

## 14.3 Linker/Boot 清单（P0/P1）

| 目标 | 启动/入口 | Linker Script | 备注 |
| :--- | :--- | :--- | :--- |
| macOS x86_64 | 系统 CRT（`main` 入口） | 系统默认 | 依赖 `system_helpers_x64.o`（由 `src/stdlib/bootstrap/system_helpers.c` 编译） |
| macOS aarch64 | 系统 CRT（`main` 入口） | 系统默认 | 依赖 `system_helpers_a64.o` |
| Linux x86_64 | 系统 CRT（`main` 入口） | 系统默认 | 依赖 `system_helpers_x64_linux.o` |
| Windows x86_64 | 系统 CRT / `lld-link` | 系统默认 | 依赖 `system_helpers_win64.obj`，入口 `main` |
| Android aarch64 | NDK CRT | 系统默认 | 依赖 NDK sysroot/toolchain |
| iOS aarch64 | Xcode CRT | 系统默认 | 依赖 iOS SDK |
| HarmonyOS aarch64 | OHOS CRT | 系统默认 | 依赖 OHOS SDK |
| RTOS aarch64 | `src/tooling/rtos/aarch64/crt0.S`（`_start`） | `src/tooling/rtos/aarch64/linker.ld` | `__stack_top` 由 linker 提供 |
| RTOS riscv64 | `src/tooling/rtos/riscv64/crt0.S`（`_start`） | `src/tooling/rtos/riscv64/linker.ld` | `__stack_top` 由 linker 提供 |

---

## 15. HRT 语法覆盖与用例矩阵（v1）

| 规则 | 正例 | 负例 | 验证入口 |
| :--- | :--- | :--- | :--- |
| 禁止 importc/runtime | `examples/hrt_fullspec.cheng` | `examples/hrt_bad_importc.cheng` / `examples/hrt_bad_runtime.cheng` | `verify_hrt_backend.sh` |
| 禁止动态容器/字面量 | `examples/hrt_fullspec.cheng` | `examples/hrt_bad_seq.cheng` / `examples/hrt_bad_table.cheng` / `examples/hrt_bad_seq_lit.cheng` | `verify_hrt_backend.sh` |
| 固定容量容器（P1-02） | `examples/hrt_fixed_cap_ok.cheng` | `examples/hrt_bad_seq.cheng` | `verify_hrt_backend.sh` |
| 禁止 float | `examples/hrt_fullspec.cheng` | `examples/hrt_bad_float.cheng` | `verify_hrt_backend.sh` |
| 禁止递归 | `examples/hrt_fullspec.cheng` | `examples/hrt_bad_recursion.cheng` | `verify_hrt_backend.sh` |
| 循环必须有界 | `examples/hrt_fullspec.cheng` | `examples/hrt_bad_while.cheng` / `examples/hrt_bad_for.cheng` | `verify_hrt_backend.sh` |
| 禁止 case/of | `examples/hrt_fullspec.cheng` | `examples/hrt_bad_case.cheng` / `examples/hrt_bad_case_expr.cheng` | `verify_hrt_backend.sh` |
| 禁止 async/await/@task/? | `examples/hrt_fullspec.cheng` | `examples/hrt_bad_async.cheng` / `examples/hrt_bad_await.cheng` / `examples/hrt_bad_task.cheng` / `examples/hrt_bad_question.cheng` | `verify_hrt_backend.sh` |
| 禁止宏/模板/trait/概念/迭代器 | `examples/hrt_fullspec.cheng` | `examples/hrt_bad_macro.cheng` / `examples/hrt_bad_template.cheng` / `examples/hrt_bad_trait.cheng` / `examples/hrt_bad_concept.cheng` / `examples/hrt_bad_iterator.cheng` | `verify_hrt_backend.sh` |
| 禁止隐式 runtime strlen | `examples/hrt_fullspec.cheng` | `examples/hrt_bad_strlen.cheng` | `verify_hrt_backend.sh` |

---

## 16. 生态工具链模板与 stdlib/HAL 子集（P2-04/P2-05）

### 16.1 SDK/NDK/RTOS 工具链模板

模板路径：`src/tooling/toolchains/`（纯 `.env` 配置文件）

| 模板 | 目标 | 说明 |
| :--- | :--- | :--- |
| `macos_x64.env` | `x86_64-apple-darwin` | macOS x86_64（clang/Xcode SDK） |
| `macos_a64.env` | `aarch64-apple-darwin` | macOS arm64（clang/Xcode SDK） |
| `linux_x64.env` | `x86_64-unknown-linux-gnu` | Linux x86_64（clang/gcc） |
| `windows_x64.env` | `x86_64-pc-windows-msvc` | Windows x86_64（clang/lld + MSVC ABI） |
| `android_a64.env` | `aarch64-linux-android` | Android aarch64（NDK） |
| `ios_a64.env` | `aarch64-apple-ios` | iOS aarch64（Xcode SDK） |
| `harmony_a64.env` | `aarch64-unknown-ohos` | HarmonyOS aarch64（OHOS SDK） |
| `rtos_aarch64_elf.env` | `aarch64-unknown-elf` | RTOS aarch64（`crt0.S` + `linker.ld`） |
| `rtos_riscv64_elf.env` | `riscv64-unknown-elf` | RTOS riscv64（`crt0.S` + `linker.ld`） |

一键命令（示例，macOS arm64）：
```sh
sh src/tooling/hrt_toolchain_init.sh --template:macos_a64 --out:chengcache/hrt_template_macos_a64
```

RTOS 示例（aarch64）：
```sh
sh src/tooling/hrt_toolchain_init.sh --template:rtos_aarch64_elf --out:chengcache/hrt_template_rtos_a64
```

### 16.2 最小项目模板 + 验证链路

一键脚本会生成最小项目（`main.cheng`）并完成 asm→obj→bin：
```sh
sh src/tooling/hrt_toolchain_init.sh --template:<name> --out:<dir>
```

验证建议：
- **生成**：`hrt_toolchain_init.sh` 自动完成 asm→obj→bin。
- **验证**：`verify_asm_backend.sh --fullspec`（后端规范校验）+ `verify_hrt_backend.sh`（HRT 语法/语义门禁）。
- **参考应用**：`examples/hrt_netdev_loopback.cheng` / `examples/hrt_filedev_loopback.cheng`；诊断入口 `verify_hrtnet_driver.sh` / `verify_hrtfile_driver.sh`。

### 16.3 HRT 允许 stdlib 子集（v1 规范）

HRT v1 采用**默认拒绝**策略：仅允许确定性、固定容量、无动态分配的标准库能力。凡涉及动态分配、系统调用、阻塞 I/O、非白名单 `importc/runtime` 的模块均禁止进入 `--mode:hrt` 编译路径。

#### 16.3.1 HRT 运行期允许（可在 `--mode:hrt` 编译）
| 模块/能力 | 允许接口 | 约束 |
| :--- | :--- | :--- |
| `std/hrt_ring` | `HrtRingMeta/HrtRingView` + `hrtRing*` | SPSC ring；`cap` 必须 2 的幂；O(1)；无分配 |
| `std/hrtnet` | `HrtNetQueue` + `hrtNetRxPeek/Consume/TxReserve/Commit/Count` | 仅允许 `cheng_hrt_net_slot_ptr` importc；无阻塞 |
| `std/hrtfile` | `HrtFileQueue` + `hrtFileSubmitReserve/Commit/CompletePeek/Consume` | 仅允许 `cheng_hrt_file_req_ptr` / `cheng_hrt_file_cpl_ptr` |
| `std/seqs` | `seq[T,N]` + `seqFixed*` | 禁止 `seq[T]`；禁止 `[]` 语法糖（用 `seqFixedGet/Set`） |
| `std/strings` | `str[N]` + `strFixed*` | 禁止 `str` 字面量与 `strFixedView`（返回 `str`） |
| `std/math` | 整数运算 | 任何浮点相关 API 禁止 |

#### 16.3.2 仅初始化阶段允许（非 HRT 编译）
以下模块只允许出现在**非 HRT 初始化阶段**（`--mode:asm` 或独立进程），不得与 HRT 运行路径混编：

| 模块/能力 | 用途 | 约束 |
| :--- | :--- | :--- |
| `std/hrtnet_driver` | `open/map/bind/poll/kick` 驱动侧初始化 | 使用 `importc` + `str/cstring`；必须在 HRT 任务启动前完成 |
| `std/hrtfile_driver` | `open/map/bind/poll` 驱动侧初始化 | 使用 `importc` + `str/cstring`；必须在 HRT 任务启动前完成 |
| `std/os` / `std/posix` / `std/windows` 等 | 设备发现/配置 | 仅用于控制面或初始化，不得进入 HRT 任务 |

显式禁止（HRT 运行阶段）：
- 动态容器与字面量：`seq/str/Table`、`@[...]`、`{...}`
- 非白名单 `importc/runtime` 调用（见 `docs/hrt-profile-v1.md`）
- 任何会触发隐式堆分配或不可界定阻塞的 API

允许的 `importc` 白名单（HRT 运行期）：
- `ptr_add`、`c_memcpy`、`c_memset`、`c_memcmp`、`c_strlen`、`c_strcmp`、`load_ptr`、`store_ptr`
- `cheng_hrt_net_slot_ptr`、`cheng_hrt_file_req_ptr`、`cheng_hrt_file_cpl_ptr`

### 16.4 HRT 允许 HAL 子集（规范）

HRT HAL 以 **“初始化阶段”** 与 **“硬实时阶段”** 分层；只有硬实时阶段 API 可在 `--mode:hrt` 运行路径中调用。

**初始化阶段（非 HRT）**：
- 允许 `open/map/bind/config` 类 API（如 `cheng_hrt_netdev_open/map/bind`）。
- 允许 OS/驱动交互与资源发现，但必须完成**静态配置**后移交 HRT 侧使用。

**硬实时阶段（HRT）**：
- 仅允许 **O(1)/有上界** 的 poll/kick/ack/now_ns 类接口。
- 禁止动态分配、系统调用、阻塞等待与不可界定 I/O。
- 仅允许指针/偏移访问共享内存（ring/descriptor），不得访问驱动栈或内核态接口。

HAL 约定（最小一致性要求）：
- API 必须返回错误码（`int32`），不得隐式 panic。
- 所有操作必须声明 WCET（可在文档或 `@wcet` 注解中给出）。
- RTOS 目标的同步原语仅在 `@task` 模式（v2）启用。

#### 16.4.1 接口命名与返回约定
- `importc` 符号统一使用 `cheng_hrt_*` 前缀（驱动侧与 HRT 侧一致）。
- `*_open/_close/_map/_poll/_kick` 返回 `bool` 或 `int32` 错误码（失败必须可诊断）。
- 句柄类型为 `void*`（不暴露内部结构），生命周期覆盖 HRT 任务运行期。

#### 16.4.2 Ring/共享内存约束
- `HrtRingMeta` 共享字段：`head/tail/cap/mask`（`cap` 必须 2 的幂，`mask = cap - 1`）。
- SPSC 语义：生产者仅写 `tail`，消费者仅写 `head`；必要的内存栅栏由驱动侧负责。
- slot 结构（`HrtNetSlot` / `HrtFileReq` / `HrtFileCpl`）布局固定，禁止动态扩展。

#### 16.4.3 最小参考应用与诊断
- 参考应用：`examples/hrt_netdev_loopback.cheng` / `examples/hrt_filedev_loopback.cheng`。
- 诊断入口：`sh src/tooling/verify_hrtnet_driver.sh` / `sh src/tooling/verify_hrtfile_driver.sh`（必要时加 `--allow-skip`）。

---

## 附录 A：Cheng 直出 ASM 覆盖：正交原子任务矩阵

目标：在“直接 Cheng->S 路径下，按正交原子任务拆解全语法 ASM 覆盖，并可独立推进与验收。

### A. 轴定义

#### 1) 语言切片（LF）
每个切片尽量只覆盖一种语义能力或语法类目。
- LF-E01：字面量（int/bool/char/nil）
- LF-E02：前缀/中缀表达式（算术/位运算/逻辑）
- LF-E03：比较/条件（==, !=, <, <=, >, >=）
- LF-E04：if/elif/else
- LF-E05：while
- LF-E06：for-in（range/seq/str）
- LF-E07：函数定义/调用/返回
- LF-E08：局部变量/赋值/作用域
- LF-E09：全局变量/常量
- LF-E10：struct/tuple/object 构造与字段访问
- LF-E11：seq/array 基础（len/索引/赋值）
- LF-E12：字符串与拼接（str/cstring）
- LF-E13：pattern（let/var/const 解构与 case/of）
- LF-E14：Result/? 与 error 分支
- LF-E15：ORC/retain/release（内存管理语义）
- LF-E16：模块导入/别名/符号解析
- LF-E17：泛型实例化（monomorphize）
- LF-E18：trait/concept + where 约束
- LF-E19：模板/宏展开（语句/表达式位置）

#### 2) 后端切片（BP）
每个切片只解决一类后端能力。
- BP-CG01：函数序言/栈帧/返回值写回
- BP-CG02：寄存器分配（最小可用策略）
- BP-CG03：参数传递（寄存器/栈）
- BP-CG04：加载/存储（局部/全局/字段/索引）
- BP-CG05：控制流（条件分支/循环）
- BP-CG06：常量与只读数据段（.rodata/.data/.bss）
- BP-CG07：调用约定与调用保存寄存器
- BP-CG08：运行时符号调用（mem/arc/str/seq）
- BP-CG09：类型布局（size/align/offset）
- BP-CG10：调试与诊断映射（最小行列映射）

#### 3) 目标切片（TA）
每个切片只代表一种 ISA/ABI。
- TA-X64-DARWIN：x86_64 + Darwin（macOS）
- TA-X64-ELF：x86_64 + ELF（Linux/Android 等）
- TA-X64-COFF：x86_64 + COFF（Windows）
- TA-A64-DARWIN：aarch64 + Darwin（macOS/iOS）
- TA-A64-ELF：aarch64 + ELF（嵌入式/RTOS）
- TA-RV64-ELF：riscv64 + ELF（嵌入式/RTOS）

### B. 原子任务命名规范
`TASK.<LF>.<BP>.<TA>`  
例：`TASK.LF-E07.BP-CG03.TA-X64-ELF` 表示“函数调用参数传递在 x86_64 ELF 的实现”。

### C. 核心矩阵（语言 × 后端）
说明：以下为**目标无关**的最小实现切片，先把语义/布局/IR 形态固化，供各 TA 复用。

| LF | BP | 任务描述 | 验收 |
|---|---|---|---|
| LF-E01 | BP-CG06 | 常量池/立即数布局策略 | 常量表达式可落到数据段/立即数 | [DONE] |
| LF-E02 | BP-CG04 | 一元/二元算术与位运算的 load/store 形态 | 指令序列可验证 | [DONE] |
| LF-E03 | BP-CG05 | 比较与条件分支 lowering | if/while 基本块可连通 | [DONE] |
| LF-E04 | BP-CG05 | if/elif/else 基本块结构 | 结构化 CFG 正确 | [DONE] |
| LF-E05 | BP-CG05 | while 的回边与跳转 | 循环入口/退出正确 | [DONE] |
| LF-E06 | BP-CG05 | for-in range/seq/str 的 lowering | 迭代变量更新正确 | [DONE] |
| LF-E07 | BP-CG01 | 函数序言/返回寄存器约定抽象 | main/自定义函数能返回 | [DONE] |
| LF-E07 | BP-CG03 | 调用参数分配策略（抽象） | call site 参数分配正确 | [DONE] |
| LF-E08 | BP-CG04 | 局部变量栈布局 + load/store | 变量读写正确 | [DONE] |
| LF-E09 | BP-CG06 | 全局变量区与初始化 | 全局读写正确 | [DONE] |
| LF-E10 | BP-CG09 | 结构体/tuple 布局与字段偏移 | field offset 正确 | [DONE] |
| LF-E11 | BP-CG04 | seq/array 索引与边界语义 | 索引读写正确 | [DONE] |
| LF-E12 | BP-CG08 | 字符串运行时接口调用 | str ops 可调用 | [DONE] |
| LF-E13 | BP-CG05 | pattern/case/of 控制流与绑定 | 匹配路径正确 | [DONE] |
| LF-E14 | BP-CG05 | Result/? 错误传播控制流 | early-return 正确 | [DONE] |
| LF-E15 | BP-CG08 | ORC retain/release 插入点 | 计数一致 | [DONE] |
| LF-E16 | BP-CG06 | 模块级符号解析与导入表 | 符号解析可复用 | [DONE] |
| LF-E17 | BP-CG09 | 单态化后的类型布局一致性 | 实例化布局稳定 | [DONE] |
| LF-E18 | BP-CG10 | 约束失败诊断映射 | 错误位置正确 | [DONE] |
| LF-E19 | BP-CG05 | 模板/宏展开后的控制流 | 展开结果可编译 | [DONE] |

建议的并行策略（先锁定接口，再并行落地）：
- **共享接口**：CFG/IR 形态、布局 hash/offset 规范、错误码与定位协议。
- **G1（控制流组）**：LF-E13 / LF-E14 / LF-E19（共用 CFG lowering 规则，需强耦合协同）。
- **G4（诊断映射组）**：LF-E18（相对独立，并行推进）。
- **G5（ORC 插桩组）**：LF-E15（依赖 G3 的布局/ABI 约定，已落地）。

### D. 目标矩阵（后端 × 目标）
说明：对每个 TA 逐一落地“调用约定/寄存器/指令序列/数据段格式”。

| TA | BP | 任务描述 | 验收 |
|---|---|---|---|
| TA-X64-DARWIN | BP-CG01 | prologue/epilogue + 符号前缀 | main 可链接 | [DONE] |
| TA-X64-DARWIN | BP-CG03 | Darwin 调用约定 | 参数/返回一致 | [DONE] |
| TA-X64-DARWIN | BP-CG05 | 条件/循环跳转指令 | if/while 通过 | [DONE] |
| TA-X64-DARWIN | BP-CG06 | .rodata/.data/.bss 布局 | 常量/全局可见 | [DONE] |
| TA-X64-ELF | BP-CG01 | ELF prologue/epilogue | main 可链接 | [DONE] |
| TA-X64-ELF | BP-CG03 | ELF 调用约定 | 参数/返回一致 | [DONE] |
| TA-X64-COFF | BP-CG01 | COFF prologue/epilogue | main 可链接 | [DONE] |
| TA-A64-ELF | BP-CG01 | AArch64 ELF 入口与栈帧 | main 可链接 | [DONE] |
| TA-A64-ELF | BP-CG03 | AArch64 ELF 调用约定 | 参数/返回一致 | [DONE] |
| TA-RV64-ELF | BP-CG01 | RV64 ELF 入口与栈帧 | main 可链接 | [DONE] |
| TA-RV64-ELF | BP-CG03 | RV64 ELF 调用约定 | 参数/返回一致 | [DONE] |
| TA-A64-DARWIN | BP-CG01 | Darwin AArch64 入口符号 | main 可链接 | [DONE] |
| TA-A64-DARWIN | BP-CG03 | Darwin AArch64 约定 | 参数/返回一致 | [DONE] |

### E. 运行时与库链接（RT）
每条任务只关注一个运行时接口的 ABI 绑定与调用。
- RT-01：`cheng_mem_retain/release/refcount` 调用约定 [DONE]
- RT-02：`__cheng_concat_str` 与 `cstring/str` 桥接 [DONE]
- RT-03：`__cheng_vec_*` 与 seq 基础操作 [DONE]
- RT-04：`panic/echo/assert` 等诊断输出 [DONE]
- RT-05：hrtnet/hrtfile sysio loopback 绑定与验证入口 [DONE]

### F. 验证矩阵（VT）
每条任务只覆盖一个独立可测路径。
- VT-01：`examples/asm_const_return.cheng` → `.s`（所有 TA） [DONE]
- VT-02：`examples/test_orc_closedloop.cheng` → `.s`（x86_64 ELF） [DONE]
- VT-03：`examples/asm_fullspec.cheng` → `.s`（子集 fullspec） [DONE]
- VT-04：`src/tooling/verify_asm_backend.sh --quick` [DONE]
- VT-05：`src/tooling/verify_asm_backend.sh`（全量矩阵） [DONE]

### F.1 TODO 任务闭环卡片（与 [TODO] 行对应）

| TASK | 产出 | 验证/命令 | 阶段 |
| :--- | :--- | :--- | :--- |
| TASK.LF-E13.BP-CG05 | pattern/case/of lowering + 绑定规则 + 负例诊断 | `verify_asm_backend.sh --fullspec` + `verify_hrt_backend.sh`（`CHENG_HRT_PROFILE=2` case 用例） | P1 / HRT v2 [DONE] |
| TASK.LF-E14.BP-CG05 | Result/? lowering + HRT v1 拒绝路径 | `verify_asm_backend.sh --fullspec` + `verify_hrt_backend.sh` | P1 [DONE] |
| TASK.LF-E15.BP-CG08 | ORC retain/release 插桩与调用约定 | `verify_asm_backend.sh --fullspec` + VT-02 | P1（依赖 LF-E17/ABI） [DONE] |
| TASK.LF-E16.BP-CG06 | 模块导入表/符号解析缓存（`modules.map` + `*.modsym` 产物） | `verify_asm_backend.sh --fullspec`（新增 multi-module 用例） | P1 [DONE] |
| TASK.LF-E17.BP-CG09 | 单态化布局 hash/offset 生成与一致性校验（`--layout-out` 产物） | `verify_asm_backend.sh --fullspec`（泛型布局用例） | P1/P2 [DONE] |
| TASK.LF-E18.BP-CG10 | 约束失败定位 + 诊断映射表 | `verify_hrt_backend.sh`（负例覆盖） | P1 [DONE] |
| TASK.LF-E19.BP-CG05 | template/macro 展开后 CFG 生成 | `verify_asm_backend.sh --fullspec`（宏用例） | P1/P2 [DONE] |
| TASK.VT-02 | `test_orc_closedloop` → `.s`（x86_64 ELF）回归项 | `verify_asm_backend.sh --fullspec`（纳入 VT） | P1 [DONE] |

### G. 交付节拍（建议）
1) 已完成 “LF-E07/08/04/05 + BP-CG01/03/04/05 + TA-X64-DARWIN”。  
2) 已完成 ELF/COFF 与 A64/RV64 ELF 基线。  
3) 继续补齐数据段与运行时接口（RT-01..RT-04）。  
4) 扩展 fullspec 覆盖到更完整语法与容器。  

### H. 并行执行指南（推荐 5 组）
以下为并行启动的 5 个 Codex 实例提示词，按优先级排序：


### I. 状态栏（手动维护）
在每个 TASK 行尾追加：`[TODO] / [DOING] / [DONE]`。
