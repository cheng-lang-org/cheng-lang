# Cheng 自研后端：任务清单、文件结构草案与 UIR 规格

> 版本：2026-02-21  
> 首发平台：macOS ARM64（Mach-O）  
> 目标产物：双轨制代码生成（Dev=linkerless self，Release=system linker）  
> 约束：有线程、无 GC、确定性内存安全（ORC/Ownership）

本文件描述 Cheng 自研后端的落地路线、目录结构草案与 UIR 主链实现规格。  
语言语义与所有权规则以 `docs/cheng-formal-spec.md` 为权威依据，本文件不改变语言语义，仅规定编译器内部实现与后端约束。
文中若出现历史 `MIR/LIR` 术语，均为历史描述；当前 `uir_internal/machine_internal` 命名已收敛到 `uirCore*` / `machineCore*`，生产入口不再走 legacy 后端链路。

> 2026-02-06 收敛说明：生产主链路已统一为 backend-only（`chengc`/`cheng`）。  
> ASM 与 legacy C bootstrap 不再作为生产入口与验收门禁。

## 进程通知（2026-02-25：单 Driver 收口）

> 本节为高优先级运行口径，优先于下文历史回退说明。用于通知并发进程统一 driver 策略。
> 通知原文：`docs/process-notice-backend-driver-unification-2026-02-25.md`。

1. backend driver canonical 路径固定：`artifacts/backend_driver/cheng`。
2. stage0 解析默认不启用历史候选回退；仅显式 `TOOLING_STAGE0_ALLOW_LEGACY_FALLBACK=1` 时允许。
3. `build-backend-driver` 临时 attempt/bak 产物写入 `chengcache/backend_driver_build_tmp`，不再污染 `artifacts/backend_driver` 顶层。
4. 新增命令 `cheng_tooling cleanup-backend-driver-history [--dry-run]`，用于将历史 `cheng*` 变体收敛为 canonical driver。
5. 默认开启构建后自动清理（`BACKEND_BUILD_DRIVER_AUTO_CLEAN_HISTORY=1`）。

---

## 1. 目标与范围

### 1.1 目标
- 自研后端输出跨平台可执行文件（先 macOS ARM64）。
- 双轨终态：Dev 模式优先 linkerless/self，Release 模式固定 system linker（优先 mold，回退 lld）。
- 最大化并行：模块/包级别并行生成 `.o` 或内存镜像，按轨道收敛到可执行文件。
- 确定性构建：固定排序、固定 hash 种子、禁止时间戳进入产物。
- 无 GC：依赖 ORC/Ownership 与 `Arc/Mutex` 等运行时原语。
- 线程安全：跨线程以 `Arc/Mutex/Atomic` 显式边界控制。

### 1.2 平台策略
- macOS：ARM64 + Mach‑O（已落地）；x86_64 + Mach‑O（已闭环）
- iOS：ARM64 + Mach‑O
- Windows：ARM64 + COFF（targets matrix 已覆盖 COFF `.obj` 产物校验）；x86_64 + COFF（已闭环 `.obj` 产物校验；`lld-link` 链接校验为可选增量 gate）
- Linux：ARM64 + ELF（已落地自研 ELF linker 初版；支持 `BACKEND_ELF_PROFILE=nolibc` 的真 no-libc 静态口径）；x86_64 + ELF（已闭环 `.o` 产物校验；link+run 需目标平台/交叉链接器）
- Linux：RISC-V64 + ELF（自研 `machine select/obj writer/self-linker` 代码路径已落地；闭环 gate 为 `sh src/tooling/tooling_exec.sh verify_backend_self_linker_riscv64`）
- Android：ARM64 + ELF
- 鸿蒙（OpenHarmony）：ARM64 + ELF
- Windows 链接器：lld-link（COFF）

### 1.3 不在范围（当前阶段）
- JIT
- 完整 Windows 生产闭环（COFF/PE 自研 self-link 初版可用，但 importc/运行时/可运行闭环仍待补齐）
- 全量调试信息（先支持最小符号）
- 高级后端优化（先保证正确性与确定性）

---

## 2. 编译流水线（建议）

```
Cheng 源码
  → 语义/所有权分析（沿用现有 stage1）
  → UIR API（统一入口，`BACKEND_IR=uir`）
  → High-UIR(MIR语义)：Ownership/Borrow/No-Alias 证明
  → Low-UIR(SSA/CodeGen)：DOD/SoA、E-Graph、Memory-Exe/Hotpatch
  → Dev 轨：self/linkerless writer 直出 EXE + host runner（trampoline + append-only）
  → Release 轨：UIR -> .o -> system linker（mold -> lld -> default）
```

Dev 轨热更终态（Darwin/Linux 首发）：
- `chengc --run` 默认进入 host runner（内存宿主）；`--run:file` 保留旧“产物后执行”兼容路径。
- 热补丁采用全局 trampoline 路由 + append-only 代码池；不再走“符号窗口原地覆写 + overflow 拒绝”。
- `layout hash` 漂移与池/增长压力统一走受控冷重启（`BACKEND_HOTPATCH_ON_LAYOUT_CHANGE=restart`）。
- JIT 页策略固定 `RW -> RX`（`BACKEND_HOSTRUNNER_PAGE_POLICY=rw_rx`）。

### 2.1 并行策略
- 编译单元：包/模块为单位（可并行产出 `.o`）。
- DAG：按 import 依赖拓扑排序并行。
- 确定性：编译队列排序固定、符号表固定、hash 种子固定。

### 2.2 当前实现（已落地）
- UIR 入口：`src/backend/uir/uir_types.cheng`、`src/backend/uir/uir_builder.cheng`、`src/backend/uir/uir_opt.cheng` 已落地，`backend_driver` 默认只接受 `BACKEND_IR=uir`。
- 主链策略：后端统一入口与生产门禁收敛到 UIR，内部实现位于 `src/backend/uir/uir_internal/*` 与 `src/backend/machine/*_internal/*`。
- UIR internal（命名已收敛）：采用表达式树（默认无 SSA）+ 基本块终结指令（`ret/br/cbr`）；`verify_backend_ssa` 用 `GENERIC_MODE=dict` 与 `GENERIC_MODE=hybrid` 两种闭环口径做输出一致性对比，不作为主路径开关。
- Machine internal：以 AArch64 为基线，涵盖整数算术/位运算（`add/sub/mul/sdiv/udiv/msub/and/or/xor/lsl/lsr/asr`）、比较跳转（`cmp/b.<cond>`）与最小栈操作，并通过对象写出层落地到目标格式。
- surface 收敛：`src/backend/obj/*` 与 `src/backend/machine/select_internal/*` 统一使用 `machineReg/machineCond/machineOp`，`machine_internal` 也已收敛为 `machineCoreReg/machineCoreCond/machineCoreOp`。
- 降级：`if/elif/else`、三目表达式 `?:`、比较与算术表达式已可直通至汇编与可执行文件。
- 控制流：`while`、`for in 0..<N` 与 `break/continue` 已可直通至汇编与可执行文件（`for` 仅支持整数区间）。
- 条件短路：`&&/||` 在条件与表达式语境统一短路，支持嵌套三目/短路表达式降级。
- 局部变量：`let/var/赋值` 以栈槽形式落地，支持基本读写。
- 对象类型（`type Foo =` 默认 `object`）：支持字段布局计算、`p.a` 读取与 `p.a = v` 写入；`var p: Foo` 省略初始化时执行隐式零初始化（逐字段 store 0）；`p2 = p1`/`var p2: Foo = p1` 为逐字段拷贝（保持 value 语义，不做指针槽别名）。
- 全局变量：支持模块级 `var/let/const`（当前仅 i32/i64/ptr 及零/整数字面量初始化），函数内可读写（通过 `adrp+add` 取址再 `ldr/str`）。
- 指针访问：支持 `&` 取局部地址、`*p` 解引用读取与 `*p = v` 写入（仅局部地址）。
- 字符串字面量：以 C‑string 常量落到 `.cstring` 段，可把其地址（i64）传给外部符号（如 `puts`）。
- 类型收敛：编译器主链路统一 `stage1`，显式拒绝 `string` 类型名；统一使用 `str`/`cstring`。
- 符号重载分发：运算符调用走编译期静态分发（无运行时动态分派）；`a[b] = v` 优先解析为 ``[]=``。普通赋值 `lhs = rhs` 对复合类型支持 ``=`` 静态分发（含泛型实例化），未命中时回退内建赋值。`[]/[]=` 已覆盖标准容器主路径（`str/seq/array/hashmap/json/Table/Table_fixed`），其余类型按静态重载解析与报错规则处理。
- 外部符号：支持仅声明无 body 的 `fn`（extern），并支持 `@importc("...")` 重写链接符号名（调用侧生成 `bl <symPrefix><link_name>`；darwin 的 `symPrefix` 为 `_`）。
- 多文件：入口文件递归解析 `import`（含 `import base/[x,y]`）。
  - 包解析：`<pkg>/<path>` 优先解析到仓库内 `src/<pkg>/<path>.cheng`（兼容接受 `cheng/<pkg>/<path>` 旧别名）；否则用 `PKG_ROOT/PKG_ROOTS`（逗号/分号/冒号分隔，默认 `~/.cheng-packages`）定位包根（含 `cheng-package.toml`，支持容器根 `cheng-<pkg>`），并加载 `<pkgroot>/src/<path>.cheng`（`<pkg>` 只用于选包，不参与包内路径）。
  - `std/<name>` 解析统一为仓库内 `src/std/<name>.cheng`（`stdlib/bootstrap` 已移除，不再作为导入回退路径）。
  - 工程约束（已落地）：`src/stage1`、`src/backend`、`src/tooling/*.cheng` 统一使用 `std/*` 导入标准库入口；`cheng/stdlib/bootstrap/*` 不再作为编译器主链路导入路径。
  - 模块构建（MVP）：三段式扫描（type decls → 函数签名 predeclare → 全局/函数体 lowering），保证跨文件 type alias 与 `var` 参数按引用传递可用。
  - 导入循环约束：导入图出现环时前端硬错误并输出环路链路（`Import cycle detected: A -> ... -> A`）；不再仅做静默 visited 去重。
  - 导入循环实现口径：active import trace 采用函数级 push/pop 守卫覆盖递归生命周期，避免块级提前 pop 导致 `A -> B -> A` 漏检。
  - 门禁口径：`verify_backend_import_cycle_predeclare` 已切为纯 runtime 断言（负例必须 compile fail 且包含 `Import cycle detected` 链路），不再接受 source-contract fallback。
- 默认：合并为一个 `UirModule` 参与 codegen（单 `.o`）；由 `UIR internal -> machine internal -> obj writer` 链路承载实现。
- 可选：开启 `BACKEND_MULTI=1` 或 `--multi`，按源文件拆分为多个编译单元生成多个 `.o`，最后统一链接。
- 单单元 fast-path：`emit=exe + multi` 仅在单元数 `>1` 时进入 `.objs` 多单元路径；单单元输入自动走 `single.emit_obj + single.link`。
- 并行执行模型（已升级）：父进程一次建图后按 `jobs` 分桶，子进程通过 `--unit-batch-file` 批量编译单元（不再“一单元一进程”）。
- 子进程并行策略：`stage1` 口径默认优先尝试 fork worker；若命中 `fork/waitpid` 异常或 worker 非零退出，会自动回退串行 multi 编译并输出诊断，避免并行异常直接阻断闭环。
- 分桶保护：默认启用最小批量限制（`BACKEND_MULTI_MIN_BATCH_UNITS=8`），避免 worker 过多引发重复前端解析开销放大。
- 并发互斥：同一 `<out>.objs` 目录通过 `<out>.objs.lock` 做互斥，避免并发产物互踩。
- 增量策略：每个单元按“依赖闭包文件状态”生成 stamp（非全模块单签名），降低无关修改触发的全量重编。
- 增量判定优化：依赖闭包 stamp 会预取单元文件状态并复用，减少大模块增量判定阶段重复 `stat` 的系统调用开销。
- 可执行单对象回退：`emit=exe` + Darwin + self-link 下可设 `BACKEND_MULTI_EXE_MAX_OBJS=<N>`（默认 `0` 禁用）在单元数超过阈值时回退“单对象编译+链接”；通常不建议开启（大模块多单元编译在当前实现下更快）。
- 验证脚本：`sh src/tooling/tooling_exec.sh verify_backend_float`（生成并运行可执行文件）。
- 并行/单对象 fast-path 验证：`sh src/tooling/tooling_exec.sh verify_backend_multi`（单元数>1 时验证 multi 路径；单单元输入验证 `single.emit_obj` fast-path）。
- 确定性：UIR internal 对 `funcs/globals/cstrs` 做稳定排序，cstring label 由稳定 hash 生成；`sh src/tooling/tooling_exec.sh verify_backend_determinism` 做回归检查；`sh src/tooling/tooling_exec.sh verify_backend_determinism_strict` 额外覆盖噪声环境变量/临时目录的双跑一致性；并提供 `.o/.exe` 口径的 strict 回归（`verify_backend_obj_determinism_strict` / `verify_backend_exe_determinism_strict`）。
- IR 校验：`BACKEND_VALIDATE=1` 启用 `uirCoreValidateModule`（UIR internal 结构一致性与 object layout 边界检查），用于在 codegen 前尽早发现内部不一致。
- Android（AArch64/ELF）：`sh src/tooling/tooling_exec.sh verify_backend_android`（优先使用 `ANDROID_NDK_HOME/ANDROID_NDK_ROOT`；未设置则尝试从 `ANDROID_SDK_ROOT/ANDROID_HOME` 或 macOS 默认 `~/Library/Android/sdk` 自动定位 NDK；可用 `ANDROID_API` 选择 API，默认 21；输出为 `artifacts/backend_android/*.android` 并用 NDK 的 `llvm-readelf/llvm-nm` 校验）。
  - 链接阶段默认自动追加 `-lm`（默认 C 运行时 `src/runtime/native/system_helpers.c` 依赖 `pow`；若改用 `BACKEND_RUNTIME=cheng` 的纯 Cheng 运行时则不需要 `pow`，但保留 `-lm` 也无碍）。
- Android 真机/模拟器运行：`sh src/tooling/tooling_exec.sh verify_backend_android_run`（需要 `adb` 与设备；可用 `ANDROID_SERIAL` 指定设备；默认复用 `artifacts/backend_android/*.android`，也可设 `ANDROID_REBUILD=1` 强制重建）。
- 一键闭环：`sh src/tooling/tooling_exec.sh verify_backend_closedloop`（聚合执行 targets/determinism/multi/float + stage1 smoke + mm；若存在 NDK 则额外跑 Android）。
  - 默认编译/链接口径：Dev gate 固定 `MM=orc + BACKEND_LINKER=self`；Release gate 固定 `BACKEND_LINKER=system + BACKEND_NO_RUNTIME_C=0`（`mold -> lld -> default`）。
  - `fullspec` 回归口径：使用后端 driver（默认 `artifacts/backend_driver/cheng`，兼容回退 `./cheng`；可用 `BACKEND_DRIVER=<path>` 指定）编译 `examples/backend_closedloop_fullspec.cheng` → 运行并要求返回码为 `0`；`backend_prod_closure` 默认注入 `BACKEND_RUN_FULLSPEC=1`，`BACKEND_RUN_FULLSPEC=0` 已不再支持；可用 `BACKEND_CLOSEDLOOP_FULLSPEC_INPUT` 覆盖输入文件。
  - 可选：`ANDROID_RUN=1` 时额外跑 `verify_backend_android_run`。
- 生产闭环（除平台）：`sh src/tooling/tooling_exec.sh backend_prod_closure`（默认启用 `BACKEND_VALIDATE=1`，聚合 determinism‑strict、opt/opt2、multi‑lto、multi_perf_regression、FFI/ABI matrix、obj/exe determinism、release manifest+bundle+sign/verify 与 mm 回归；`fullchain/stress` 为显式开关）。
  - ABI 口径：仅支持 `ABI=v2_noptr`；主闭环默认 no-pointer 兼容口径（`STAGE1_STD_NO_POINTERS=1`、`STAGE1_STD_NO_POINTERS_STRICT=0`、`STAGE1_NO_POINTERS_NON_C_ABI=1`、`STAGE1_NO_POINTERS_NON_C_ABI_INTERNAL=1`），并由 `verify_backend_abi_v2_noptr` 专项 gate 强制 strict 验证。
  - 用户默认入口（`chengc`）在 `v2_noptr` 下默认也强制 `STAGE1_NO_POINTERS_NON_C_ABI=1`（可显式覆盖用于排障）；`STAGE1_STD_NO_POINTERS_STRICT` 默认仍为 `0`。
  - 默认 strict：任一子步骤 `exit 2`（skip）即失败；`--allow-skip` 已不再支持。
  - 自举：默认包含后端 selfhost（产出 stage2）；全链自举（stage2→tools）改为显式 `--fullchain`（或 `BACKEND_RUN_FULLCHAIN=1`）。
  - 发布：默认启用 publish，且要求显式 seed（`--seed/--seed-id/--seed-tar`）；本地仅验收可用 `--no-publish`。
  - 打包：若存在 `artifacts/backend_selfhost_self_obj/cheng.stage2`，则 manifest/bundle 默认记录并打包 stage2 driver，并可附带全链产物。
  - 驱动选择（生产收口）：统一固定 `BACKEND_DRIVER=artifacts/backend_driver/cheng` 且 `BACKEND_DRIVER_ALLOW_FALLBACK=0`，主链不依赖 resolver 自动回退；seed/selfhost driver 仅用于显式排障覆盖。
    - `backend_driver_path` 的候选回退链仅用于开发排障，不作为生产收口默认路径。
  - stage0 探针默认走 `BACKEND_PROD_STAGE0_PROBE_MODE=path`（只复用 `backend_driver_path` 的可运行 smoke），避免为探针重编整个 `backend_driver.cheng`；必要时可切 `light`/`full`。
  - 主门禁链路在 `STAGE1_NO_POINTERS_NON_C_ABI=1` 时默认保持 stable driver（`BACKEND_MAIN_ALLOW_SELFHOST_DRIVER=auto`），selfhost driver 主要用于 `backend.abi_v2_noptr` 专项 gate；可用 `BACKEND_MAIN_ALLOW_SELFHOST_DRIVER=1` 强制主链切换 selfhost。
  - selfhost 自举步骤默认以 `STAGE1_NO_POINTERS_NON_C_ABI=0` / `STAGE1_NO_POINTERS_NON_C_ABI_INTERNAL=0` 运行（避免编译器源码阶段误拦截）；non-C-ABI no-pointer 由后续 `backend.closedloop` 与 `backend.abi_v2_noptr` 门禁收敛。
  - `backend.abi_v2_noptr` 专项 gate 默认优先使用本地 `artifacts/backend_driver/cheng`（要求具备 non-C-ABI no-pointer 诊断能力）；其次当前 `BACKEND_DRIVER`，再到 selfhost `cheng.stage2/stage1`；可用 `BACKEND_ABI_V2_DRIVER` 覆盖。
  - `backend.abi_v2_noptr` 在闭环中显式强制 `BACKEND_ABI_V2_NOPTR_NON_C_ABI_STRICT=1`，不允许 non-C-ABI probe 软跳过。
  - `backend_prod_closure` 默认新增阻断 gate：`backend.noptr_default_cli`（默认 CLI 入口 no-pointer 回归）、`backend.noptr_exemption_scope`（`...NON_C_ABI=0` 豁免范围 allowlist 守卫）与 `backend.linker_abi_core`（Darwin/Linux linker ABI manifest 差分白名单守护）。
  - `backend_driver_path` 会先做 driver 可运行性健康检查（超时保护）；默认优先复用可用 stage2/stage1 候选，必要时再重建本地 `artifacts/backend_driver/cheng`（兼容回退 `./cheng`；可用 `BACKEND_DRIVER_PATH_PREFER_REBUILD=1` 强制重建优先）。
  - `backend_driver_path` 的编译 smoke 默认为 `stage1`；如需额外启用 strict smoke，可设 `BACKEND_DRIVER_PATH_STAGE1_STRICT_SMOKE=1`；如需附加 dict fixture 探针可设 `BACKEND_DRIVER_PATH_STAGE1_DICT_SMOKE=1`。
- `build_backend_driver` 自举重建默认采用串行增量（`BACKEND_BUILD_DRIVER_MULTI=0`；并行失败自动串行重试；可由 `BACKEND_BUILD_DRIVER_MULTI` / `BACKEND_BUILD_DRIVER_INCREMENTAL` / `BACKEND_BUILD_DRIVER_JOBS` 覆盖）。
  - 通过 `tooling_exec` 调用 `build_backend_driver` 且未显式设置 `BACKEND_BUILD_DRIVER_STAGE0` 时，会先锁定单一 stage0（顺序：`artifacts/backend_driver/cheng` -> `artifacts/backend_seed/cheng.stage2` -> `artifacts/backend_selfhost_self_obj/cheng.stage2` -> `./cheng`，并跳过命中 `UE/UEs` 的候选），并导出 `BACKEND_BUILD_DRIVER_STAGE0`；若需覆盖，显式设置该变量即可。
  - 若显式启用多候选探测（`BACKEND_BUILD_DRIVER_MAX_STAGE0_ATTEMPTS`，`0` 表示尝试全部），会按候选链逐一尝试 stage0。
  - 自举编译环境会同时注入 `STAGE1_SKIP_SEM/OWNERSHIP/CPROFILE` 与 `STAGE1_SKIP_*` 同名变量，兼容 seed stage0 的历史前缀读取口径。
- 后端驱动：`src/backend/tooling/backend_driver.cheng` 生产口径固定 `emit=exe`；`emit=obj` 仅作为 internal gate 兼容入口（需显式 `BACKEND_INTERNAL_ALLOW_EMIT_OBJ=1`）；`--target=<triple|auto>`（`auto` 按当前主机平台自动选择；也可用 `BACKEND_TARGET/CC/LD/CFLAGS/LDFLAGS` 环境变量；`--cc=` 与 `--ld=` 可分别控制 link）；不再支持 `emit=asm`。

### 2.3 生产闭环现状（除平台）

> “生产闭环”在这里指：**从源码到可执行产物**的可复现构建 + 核心回归集的自动化验收 + 可观测性与发布流程可持续运行。

当前验证状态（2026-02-12）：
- 本仓库已执行 `sh src/tooling/tooling_exec.sh backend_prod_closure`，结果 `backend_prod_closure ok`（默认口径，含 selfhost/release/mm；fullchain/stress 按需开启）。
- `verify_backend_closedloop` 默认执行 `backend.spawn_api_gate`；该 gate 使用 v2 友好 fixture（不依赖 `std/async_rt` 内部 raw pointer 实现），在 `ABI=v2_noptr` 下直接回归。
- 在双轨口径下，`backend_prod_closure --no-publish` 已通过：Dev gate 以 `MM=orc BACKEND_LINKER=self` 收口，Release gate 以 `BACKEND_LINKER=system` 收口；闭环脚本对 `opt/opt2/multi-lto/ssa/ffi/debug/stress/concurrency` 相关 gate 已统一接入 `sh src/tooling/tooling_exec.sh backend_link_env`，避免 runtime 注入遗漏。
- Linux AArch64 no-libc 口径：`sh src/tooling/tooling_exec.sh verify_backend_nolibc_linux_aarch64` 已通过 Darwin/arm64 静态门禁；运行门禁需在 Linux aarch64 主机执行。

可选语义回归门禁（fullspec）：
- `fullspec`：`sh src/tooling/tooling_exec.sh verify_backend_closedloop` 支持 stage1 `fullspec`（默认 `examples/backend_closedloop_fullspec.cheng`）编译+运行验收，要求返回码为 `0`；该脚本独立运行时默认 `BACKEND_RUN_FULLSPEC=1`，`BACKEND_RUN_FULLSPEC=0` 已不再支持。
- fullspec 编译默认口径：`BACKEND_FULLSPEC_SKIP_SEM=1`、`BACKEND_FULLSPEC_GENERIC_MODE=dict`、`BACKEND_FULLSPEC_GENERIC_SPEC_BUDGET=0`、`BACKEND_FULLSPEC_SKIP_OWNERSHIP=1`、`BACKEND_FULLSPEC_VALIDATE=0`（可按需覆盖）；编译后会执行符号门禁，阻断未解析 `seqBytesOf_T` 回归。

已闭环（除平台生产闭环）：
- 一口气验收入口：`sh src/tooling/tooling_exec.sh backend_prod_closure`（聚合“语义回归 + IR 校验 + 后端自举（selfhost）+ determinism‑strict + opt + multi_perf_regression + FFI/ABI matrix + obj/exe determinism + release manifest+bundle+sign/verify + mm”；可用 `--no-selfhost` 跳过自举验收，`--fullchain/--stress` 显式加压测）。
- 后端自举（selfhost，host-only fast）：`sh src/tooling/tooling_exec.sh verify_backend_selfhost_bootstrap_self_obj` 默认走原生 `cheng_tooling selfhost-bootstrap-fast-host`。
  - stage0 解析顺序：`SELF_OBJ_BOOTSTRAP_STAGE0`（或 `--stage0`）→ `artifacts/backend_driver/cheng` → `artifacts/backend_seed/cheng.stage2` → `artifacts/backend_selfhost_self_obj/cheng.stage2` → `./cheng`；候选必须通过可运行性体检，并跳过命中 `UE/UEs` 的候选。
  - 原生 selfhost/build-driver 的 stage0 解析默认强制 compile probe；显式 stage0 不健康时会自动回退候选序列，避免选中“可执行但不可编译”的坏 driver。
  - `--compile-stage1` 开启真实重编；`SELFHOST_STAGE1_FULL_REBUILD=1|0` 切换 `full|quick`（原生命令默认 `0`，即 quick）；`SELFHOST_STAGE1_REQUIRE_REBUILD=1` 时禁止重编失败后复用 stage0。
  - 产物报告会输出 `selfhost_fast_stage1_status`（`rebuild|reused_stage0|alias`）、`selfhost_fast_stage1_compile_mode`（`full|quick|alias`）与 `selfhost_fast_stage1_rebuild_ok=1|0`。
  - fast 口径下 stage2 为 stage1 的 alias 拷贝（用于 host 自举链路提速观测）。
- 固定 seed（纯 Cheng 更新，不走 C 后端）：用“上一版本的 stage2 driver（二进制）”作为 seed（`SELF_OBJ_BOOTSTRAP_STAGE0=<seed>`），运行 `sh src/tooling/tooling_exec.sh backend_seed_pure --seed:<seed>` 可在同一链路下生成新的 stage2（自举固定点）。
- CI/生产（obj-only）：`sh src/tooling/tooling_exec.sh verify_backend_ci_obj_only`，默认从 `dist/releases/current_id.txt` 指向的发布 seed 提取 driver 走全链 `.o` 自举；也可传 `--seed:<path>`/`--seed-id:<id>`/`--seed-tar:<path>` 显式指定，不再自动候选本地 artifacts seed。该脚本默认 strict：子步骤返回 `status=2`（skip）会直接失败（`--strict` 仅保留兼容参数）。
- 全链复用口径：`verify_fullchain_bootstrap` 默认 `FULLCHAIN_REUSE=1`，在 stage2/source/runtime 未变化时复用 `stage1_fullspec_obj` 与工具产物，仅执行运行 smoke，降低重复闭环耗时。
- 全链 stage1 样例口径：默认样例为 `examples/backend_closedloop_fullspec.cheng`（可用 `FULLCHAIN_STAGE1_FILE` 覆盖），默认前端为 `FULLCHAIN_STAGE1_FRONTEND=stage1`，默认单次编译超时 `FULLCHAIN_STAGE1_TIMEOUT=60`，默认串行 `FULLCHAIN_STAGE1_MULTI=0`（显式并行失败会自动串行重试）。
- 全链 stage1 样例链接器口径：`FULLCHAIN_STAGE1_LINKER=self|system`（默认 `system`），`auto` 已不再支持。
- 全链工具并行：`verify_fullchain_bootstrap` 默认按主机核数并行构建 `cheng_pkg_source/cheng_pkg/cheng_storage`（`FULLCHAIN_TOOL_JOBS=0` auto）；工具构建默认 required（`FULLCHAIN_BUILD_TOOLS=1`，`FULLCHAIN_BUILD_TOOLS=0` 与 `FULLCHAIN_BUILD_TOOLS_STRICT=0` 均不再支持）。工具单项默认超时 `FULLCHAIN_TOOL_TIMEOUT=60`，超时/失败直接阻断，不再复用旧产物或生成 stub。
- 调试与可观测性：在移除 `emit=asm` 后已恢复最小 dSYM 口径回归（`sh src/tooling/tooling_exec.sh verify_backend_debug`）。
- UIR 校验：`BACKEND_VALIDATE=1` 在 codegen 前跑 `uirCoreValidateModule`，覆盖 block/terminator、branch target、表达式结构、object‑local 取址误用、object layout 边界等一致性检查。
- 确定性更强口径（初版）：`sh src/tooling/tooling_exec.sh verify_backend_determinism_strict` 覆盖噪声环境变量/临时目录下的双跑一致性（以 `.o` 为口径）。
- 优化（最小闭环）：`BACKEND_OPT=1` 启用 UIR 基础优化（常量折叠/指令合并/基础 DCE）；回归入口 `sh src/tooling/tooling_exec.sh verify_backend_opt`。
- SSA（最小闭环）：`verify_backend_ssa` 以 `GENERIC_MODE=dict` 与 `GENERIC_MODE=hybrid` 对比闭环输出；不再以 `BACKEND_SSA` 作为主路径开关。
- legacy 引用护栏：`verify_backend_no_legacy_refs` 校验生产入口（`backend_driver` + `src/backend/uir/*.cheng` + `src/backend/machine/*.cheng` + tooling）不再直连 legacy import 与旧 env。
- SIMD（生产 gate）：`verify_backend_simd` 已作为 `backend_prod_closure` 的阻断 gate，覆盖 `simd on/off` 语义一致、对象确定性与目标能力回退；默认行为由 `UIR_SIMD` + `BACKEND_OPT_LEVEL` 控制（`optLevel>=3` 自动开启）。
- FFI/ABI 边界（初版）：`sh src/tooling/tooling_exec.sh verify_backend_ffi_abi` 覆盖 extern+`@importc` 的多参数调用（含 9 参数栈传参）、i32/i64 混参与指针参数读写。
- FFI/ABI 自研链接器口径：`verify_backend_ffi_abi` 在 `BACKEND_LINKER=self` 与 `BACKEND_LINKER=system` 下均直接通过（含 `ffi_importc_varargs_direct_sum_i32`）。
- FFI/ABI out 参数（扩展）：`sh src/tooling/tooling_exec.sh verify_backend_ffi_abi` 额外覆盖 out 参数写回（`ffi_importc_out_pair_i32`，`var int32` 自动取址传参）。
- Raw Pointer Safety out-ptr tuple 桥接（RPSPAR-04）：`sh src/tooling/tooling_exec.sh verify_backend_ffi_outptr_tuple` 覆盖 `@ffi_out_ptrs + @importc` 降级（`tuple -> raw extern + wrapper`），包含运行态正例、status obj-only 编译正例与 arity 负例诊断。
- FFI/ABI 聚合返回（扩展）：`sh src/tooling/tooling_exec.sh verify_backend_ffi_abi` 覆盖聚合返回 wrapper/out（`ffi_importc_aggret_pair_i32`，间接调用 C 侧 struct-return）。
- FFI/ABI 栈参数（扩展）：`sh src/tooling/tooling_exec.sh verify_backend_ffi_abi` 额外覆盖 16 参数（含 i32 溢出栈参数）并修复 AArch64 i32 栈参布局。
- FFI/ABI varargs 栈槽（扩展）：`sh src/tooling/tooling_exec.sh verify_backend_ffi_abi` 覆盖 `varargs[int32]` 直连与 fixed-wrapper 两条路径；当前实现在 call-site 显式构建栈参数区并保持 16B 对齐。
- 目标文件/链接链路（可验收）：`sh src/tooling/tooling_exec.sh verify_backend_obj` 校验 `.o` 内符号与未定义外部符号。
- 跨平台矩阵闭环（可验收）：`sh src/tooling/tooling_exec.sh verify_backend_targets_matrix` 覆盖 darwin/ios(Mach‑O `.o`) + android/linux(ELF `.o`) + windows(COFF `.obj`)；已接入 `sh src/tooling/tooling_exec.sh verify_backend_closedloop`。
- 对象文件确定性（初版）：`sh src/tooling/tooling_exec.sh verify_backend_obj_determinism` 校验相同输入下两次产出 `.o` 的 sha256 一致性。
- 对象文件确定性（strict）：`sh src/tooling/tooling_exec.sh verify_backend_obj_determinism_strict` 在噪声环境变量（含 `TZ/TMPDIR/BACKEND_JOBS`）下双跑比对 `.o` 的 sha256。
- 可执行确定性（初版）：`sh src/tooling/tooling_exec.sh verify_backend_exe_determinism` 校验相同输出路径下的可执行文件二进制一致性（darwin 使用 `-Wl,-no_uuid`；linux 使用 `-Wl,--build-id=none`）。
- 可执行确定性（strict）：`sh src/tooling/tooling_exec.sh verify_backend_exe_determinism_strict` 在噪声环境变量下双跑比对可执行文件的 sha256（要求输出路径固定）。
- 优化增强（opt2，可验收）：`BACKEND_OPT2=1` 或 `BACKEND_OPT_LEVEL=2` 启用 inlining + dead func prune + CFG liveness DCE + const folding +（基础版）SROA/CSE/LICM；`BACKEND_OPT_LEVEL=3` 在 O2 基础上追加激进内联、向量化与后清理轮次；回归入口 `sh src/tooling/tooling_exec.sh verify_backend_opt2`（含 while 条件 LICM hoist 与 `return_opt2_merge_jumps`/`return_opt2_forward_subst` 回归）以及 `sh src/tooling/tooling_exec.sh verify_backend_opt3`。
- `UIR_AGGRESSIVE=1` 与 `UIR_FULL_ITERS`（默认 1）用于扩展级优化（`uir_func` 级分支等价重写、返回块折叠、等价块合并、布尔分支归一等）；
  在 `verify_backend_opt2/opt3/closedloop` 里可用于快速验证“激进闭环”是否回归语义。
- SIMD 闸口：`verify_backend_simd` 作为 `opt3` 后阻断 gate，覆盖 `simd on/off` 语义一致与 `simd on` 对象确定性。
- 跨文件优化（multi‑lto，初版）：`BACKEND_MULTI_LTO=1` 在 `--multi` 的 exe 路径上先对全模块做 UIR 优化，再走单对象链接路径（避免分单元产物缺失与链接漂移）；回归入口 `sh src/tooling/tooling_exec.sh verify_backend_multi_lto`。
  - 运行态口径：`verify_backend_multi_lto` 默认执行可运行探针（`BACKEND_MULTI_LTO_RUN_TIMEOUT` 默认 `10s`），超时/符号缺失/崩溃均硬失败，不再回退 compile-only。
- 自研 ELF `.o` writer（可验收）：后端 `emit=obj` **默认直接产出** relocatable `.o`（不调用 `cc -c`）；可用 `BACKEND_OBJ_WRITER=elf` 强制选择 writer。回归入口 `sh src/tooling/tooling_exec.sh verify_backend_self_obj_writer`（需 NDK，用于 readelf/nm/link 校验）。
- 自研目标文件确定性（初版）：`sh src/tooling/tooling_exec.sh verify_backend_self_obj_writer_elf_determinism` / `sh src/tooling/tooling_exec.sh verify_backend_self_obj_writer_macho_determinism` / `sh src/tooling/tooling_exec.sh verify_backend_self_obj_writer_coff_determinism` 在相同输入下双跑比对自研 `.o/.obj` 的 sha256 一致性。
- Sanitizer（可选）：`sh src/tooling/tooling_exec.sh verify_backend_sanitizer` 以 `-fsanitize=address` 构建并运行一个最小 fixture；不支持则自动 skip（exit 2）。
- 运行时压力（初版）：`sh src/tooling/tooling_exec.sh verify_backend_stress` 编译 stage1 smoke（`hello_puts`）并重复运行（默认 10 次，可用 `BACKEND_STRESS_N` 调整）；编译与单次运行默认 `60s` 超时（`BACKEND_STRESS_TIMEOUT`）。
- 并发压力（初版）：`sh src/tooling/tooling_exec.sh verify_backend_concurrency_stress` 覆盖 `spawn/schedRun/chan` 的最小并发运行时调用并循环运行（默认 50 次，可用 `BACKEND_CONCURRENCY_N` 调整）；编译与单次运行默认 `60s` 超时（`BACKEND_CONCURRENCY_TIMEOUT`）。
- 运行时回归（初版）：`sh src/tooling/tooling_exec.sh verify_backend_mm` 覆盖 mem_scope 回收与手动 `memRelease` 的 live 计数不增长（执行带 60s 超时保护）；容器语义回归 `mm_container_balance`（`str[]` 的写入/覆盖/读取行为）默认开启，默认前端 `BACKEND_MM_CONTAINER_FRONTEND=stage1`。
- 运行时 ABI 一致性门禁（初版）：`sh src/tooling/tooling_exec.sh verify_backend_runtime_abi` 校验 `src/runtime/native/system_helpers.h` 声明与 C/runtime 后端实现的一致性（含 `_addr`/`__addr` 兜底符号），并在 `verify.sh`、`verify_backend_closedloop` 与 CI（Linux amd64、macOS arm64）执行。
- UIR 可观测性：`UIR_PROFILE=1` 输出 `uir_profile\t<label>\tstep_ms=...\ttotal_ms=...`，并输出 `generics_report\tir=uir\tmode=...\tspec_budget=...`。
- `UIR_AGGRESSIVE=1` 时，会额外输出激进 pass 的细分 `uir_profile` 段（如 `*.func_merge_identical_ret*`、`*.fold_cbr_to_ret_expr*`）用于定位CFG收敛瓶颈。
- 标准库目录同步门禁（初版）：`sh src/tooling/tooling_exec.sh verify_std_layout_sync` 校验 `src/std` 入口完整性、禁止遗留 `* copy.cheng`，并确保 `src/stdlib/bootstrap` 旧路径已移除。
- Stage1 seed 布局门禁（初版）：`sh src/tooling/tooling_exec.sh verify_stage1_seed_layout` 校验 seed driver 与布局约束，防止历史 seed 资产回归。
- 标准库入口门禁（初版）：`sh src/tooling/tooling_exec.sh verify_std_import_surface` 校验 `src/stage1`/`src/backend`/`src/tooling`/`src/decentralized`/`src/web` 不再直接导入 `cheng/stdlib/bootstrap/*`，防止 `std/*` 入口回退。
- 工具链交付（最小闭环）：`sh src/tooling/tooling_exec.sh backend_release_manifest` 输出 release manifest（git rev/cc 版本/产物 sha256；支持 `--driver:<path>` 与 `--stage1-backend:<path>` 记录 stage2 与全链产物 sha），`sh src/tooling/tooling_exec.sh backend_release_bundle` 生成最小发布包（tar.gz；支持 `--driver:<path>` 与重复 `--extra:<path>` 附带额外二进制），`sh src/tooling/tooling_exec.sh backend_release_sign`/`sh src/tooling/tooling_exec.sh backend_release_verify` 负责签名与验签（OpenSSL/Ed25519；若环境不支持 Ed25519 则自动降级 RSA‑SHA256）。
- 语言规范一致性门禁（skill）：`sh src/tooling/tooling_exec.sh verify_cheng_skill_consistency` 扫描 `docs/cheng-skill/` 内禁用语法（旧 `method/converter`、字符串/相对导入、`cast[T](x)`、`TypeExpr expr` 等），并校验 `references/ownership.md` 资源完整性；默认对 `tests/cheng/skill/hello_cheng_ci_sample.cheng` 做 **`MM=orc`** 的 obj+exe 抽样编译，并运行 `tests/cheng/backend/fixtures/return_add.cheng` smoke。`verify.sh` 默认执行该门禁（可用 `VERIFY_SKILL=0` 跳过）；CI（`.github/workflows/ci.yml`）在 Linux amd64 与 macOS arm64 执行完整检查，Windows amd64 执行 docs-only 检查（`SKILL_COMPILE=0`），用于防止 skill 文档与模板漂移。
- 仓库 `verify.sh` 默认统一并固定 `MM=orc` 口径；不再提供 `MM=off` 回归路径。
- 发布/回滚（本地闭环）：`sh src/tooling/tooling_exec.sh backend_release_publish` 默认发布到 `dist/releases/<release_id>` 并更新 `dist/releases/current`；`sh src/tooling/tooling_exec.sh backend_release_rollback` 可回滚到上一版本；`sh src/tooling/tooling_exec.sh backend_prod_publish` 一键“验收+发布”。

仍待完善（工程长期项）：
- 编译器全链自举（初版已落地）：`verify_backend_selfhost_bootstrap_self_obj` 默认 `fast` 口径产出 selfhost stage2（stage1 alias）；`strict` 口径执行 fixed-point（stage1/stage2，必要时追加 `stage3 exe witness` + compile-stamp/hash witness 收敛）。`verify_fullchain_bootstrap` 直接要求已有 stage2（默认 `artifacts/backend_selfhost_self_obj/cheng.stage2`，可用 `FULLCHAIN_STAGE2` 覆盖），并以 `MM=orc` 构建核心工具、执行 obj-only smoke（默认 `examples/backend_fullchain_smoke.cheng`）与 `--help` smoke。需要重样例时可显式设置 `FULLCHAIN_STAGE1_FILE=examples/backend_obj_fullspec.cheng`。生产/CI（macOS arm64）通常通过 `verify_backend_ci_obj_only` 以发布 seed 完成“seed→stage2→fullchain(obj-only)”闭环。
- 跨平台 `.o/.obj` 产物闭环：已补齐 x86_64（Linux ELF / Windows COFF）的 codegen+obj 回归（见 `verify_backend_targets_matrix`）；Darwin x86_64 已有 exe+run（Rosetta）闭环；Windows 侧 `lld-link` 校验作为可选增量 gate（`sh src/tooling/tooling_exec.sh verify_backend_coff_lld_link` 可在本机装有 LLVM lld 时做 COFF `.obj`→DLL 的 link 校验）。
- UIR/SSA：UIR 主链默认承载 SSA 语义，`verify_backend_ssa` 保留文件名兼容并用于 `dict/hybrid` 一致性回归；opt2 已覆盖 DCE/内联/死函数裁剪 +（基础版）CSE/SROA/LICM + CFG 无条件跳转压缩；跨模块优化已落地 multi‑lto 初版（`BACKEND_MULTI_LTO=1`），仍需更完整的链接器级 LTO。
- 目标文件/链接链路：自研 relocatable 目标文件 writer 已落地（AArch64）：ELF `.o` / Mach‑O `.o` / COFF `.obj`。
  - 入口：后端默认直接生成 relocatable `.o`；可用 `BACKEND_OBJ_WRITER=elf|macho|coff` 强制选择（默认按 target 自动选择）。
  - 回归：ELF `sh src/tooling/tooling_exec.sh verify_backend_self_obj_writer`（Android/NDK），Mach‑O `sh src/tooling/tooling_exec.sh verify_backend_self_obj_writer_macho`，COFF `sh src/tooling/tooling_exec.sh verify_backend_self_obj_writer_coff`；并提供 determinism 回归：`verify_backend_self_obj_writer_elf_determinism` / `verify_backend_self_obj_writer_macho_determinism` / `verify_backend_self_obj_writer_coff_determinism`；已接入 `sh src/tooling/tooling_exec.sh backend_prod_closure`。
  - 已落地：编译/链接命令可分离（`--cc`/`BACKEND_CC` vs `--ld`/`BACKEND_LD`）。
  - 已落地：Darwin/arm64 自研 Mach‑O linker（`BACKEND_LINKER=self`），链接后自动 `codesign -s -`（macOS 15+ 运行要求签名）。
  - 已落地：AArch64 ELF 自研 linker（Linux/Android，`BACKEND_LINKER=self`）；默认 profile 为 ET_DYN + dynamic segment，回归入口 `sh src/tooling/tooling_exec.sh verify_backend_self_linker_elf`（不运行，只校验产物头与 dynamic/phdr）。
  - 已落地：Linux AArch64 no-libc profile（`BACKEND_ELF_PROFILE=nolibc` + `BACKEND_LINKER=self`），产物要求无 `PT_INTERP/PT_DYNAMIC/DT_NEEDED`；回归入口 `sh src/tooling/tooling_exec.sh verify_backend_nolibc_linux_aarch64`（Darwin 仅静态验收，Linux aarch64 跑 smoke）。
  - 已落地：RISC-V64 ELF 自研 linker（Linux，`BACKEND_LINKER=self`）；回归入口 `sh src/tooling/tooling_exec.sh verify_backend_self_linker_riscv64`（默认 `MM=orc`，不运行，只校验产物头与 dynamic/phdr）。
  - 已补齐：RISC-V64 obj writer 对 runtime `f64 bits` intrinsic 的最小浮点指令集（`fmov/fadd/fsub/fmul/fdiv/fneg/fcvt.d.l`，含 `fcmp+bcond` 基础分支）编码，`src/std/system_helpers_backend.cheng` 可直出 riscv64 `.o`。
  - 已落地：AArch64 COFF/PE 自研 linker（Windows，`BACKEND_LINKER=self`）；回归入口 `sh src/tooling/tooling_exec.sh verify_backend_self_linker_coff`（不运行，只校验 PE/Import headers）。
    - 当前限制：仅支持 **未定义 BRANCH26（函数调用）** 通过 PE import+IAT stub 解析（内置 `KERNEL32.dll!ExitProcess`，其余默认映射到 `BACKEND_COFF_CRT_DLL`，默认 `UCRTBASE.dll`）；其它未定义 reloc 仍会报错；`.reloc`/ASLR 与完整可运行闭环仍待推进。
  - 架构策略更新：`*_linker.cheng` 进入兼容层（逐步废弃）；新能力优先落在 writer/linkerless 路径，Release 侧复用 system linker。
- 确定性更强口径：跨机器/跨工具链版本/更大噪声环境变量的复现与缓存键完备性仍需持续加强（当前已覆盖 `.o/.exe` strict 的初版口径）。
- 调试可观测性增强：已恢复最小 dSYM 回归（dsymutil + 符号校验）；已落地 FP 链栈帧布局以支持 `sample` 可读调用栈；仍需在自研 obj writer 内补齐 DWARF（行号/CFI/unwind）以支持真实单步调试。
- 运行时与并发压力：仍需加入更长跑/更高并发与 sanitizer/泄漏检测的体系化回归（当前已落地 `verify_backend_concurrency_stress` 与 `verify_backend_sanitizer` 的最小入口）。
- FFI/ABI 矩阵：已覆盖 importc 符号前缀规则（darwin `_` vs 其它目标）、varargs（i32 直连 + wrapper 固定参）、回调/函数指针 + ctx（i32/i64 混参）、聚合返回 wrapper/out 与 out 参数写回（`var` out）；varargs 浮点/更复杂回调（闭包捕获规则与析构顺序）等仍需扩展。
- 发布工程：CI 已接入（GitHub Actions：`.github/workflows/ci.yml`，macOS arm64 跑 self-obj stage2 自举 + `FULLCHAIN_OBJ_ONLY=1` 的 fullchain smoke）；远端发布/签名治理仍在推进中；当前已先落地本地 publish/rollback 的最小闭环脚本（见上）。

---

## 3. 文件结构草案

当前生产目录（UIR 主链）：

```
src/
  backend/
    uir/
      uir_types.cheng
      uir_builder.cheng
      uir_frontend.cheng
      uir_opt.cheng
      uir_codegen.cheng
      uir_validate.cheng
      uir_pass_manager.cheng
      uir_generics.cheng
      uir_instance_cache.cheng
      uir_internal/
        *
    machine/
      machine_types.cheng
      machine_select_aarch64.cheng
      machine_select_x86_64.cheng
      machine_select_riscv64.cheng
      machine_regalloc.cheng
      machine_internal/
        *
      select_internal/
        *
      regalloc_internal/
        *
    obj/
      macho_writer.cheng
      macho_writer_x86_64.cheng
      elf_writer.cheng
      elf_writer_x86_64.cheng
      elf_writer_riscv64.cheng
      coff_writer.cheng
      coff_writer_x86_64.cheng
      aarch64_enc.cheng
      riscv64_enc.cheng
    tooling/
      backend_driver.cheng
```

### 3.1 可复用/对接点
- 语义与 Ownership：复用 `docs/cheng-formal-spec.md` 的规则。
- 现有 `stage1`：作为语义后 AST 输入（由 `uir_frontend/uir_builder` 直接消费）。
- 运行时：复用现有 ORC 与线程原语接口。

---

## 4. UIR Core（内部历史 Mir 命名）规格草案

> 目标：为优化与后端生成提供稳定、可序列化的中间表示。  
> 本节描述 `src/backend/uir/uir_internal/*` 的内部协议；其中 `Mir*` 仅为历史命名，
> 不代表生产主链仍走 legacy MIR/LIR 后端。

### 4.1 模块与符号
- `Module`：
  - `target_triple`（如 `arm64-apple-darwin`）
  - `types[]`、`globals[]`、`functions[]`、`externs[]`
  - `metadata`（build hash / feature flags）
- `Global`：
  - `name`、`type`、`init`、`mutability`、`visibility`
- `Extern`：
  - `name`、`fn_type`、`call_conv`

### 4.2 类型系统（UIR Core）
- 标量：`i1/i8/i16/i32/i64`, `u8/u16/u32/u64`, `f32/f64`
- `int/uint`：由 `INTBITS` 映射到 `i32/u32` 或 `i64/u64`（后端 MVP 仅支持 32/64）。
- 指针：`ptr<T>`（可选 `addrspace`）
- 复合：`struct{...}`, `T[N]`（定长数组；布局/ABI 稳定）, `slice{ptr<T>, len}`（仅 IR 表示；源语言无独立 slice 类型，需零拷贝时显式传 `ptr + len`）
- 函数：`fn(param_types): ret_type`
- 所有权标记（元数据）：`Owned | Borrowed | Unmanaged`

### 4.3 值与 SSA
- `Value`：SSA 临时、参数、常量、全局引用
- 所有 SSA 临时在单基本块内唯一命名
- Phi 节点在块头部统一收敛

### 4.4 基本块
```
block <label>:
  [phi...]
  inst...
  terminator
```
终结指令：
- `ret v`
- `br label`
- `cbr cond, label_true, label_false`

### 4.5 指令集（最小子集）
数据与内存：
- `alloca T`
- `load ptr<T>`
- `store ptr<T>, value`
- `gep ptr<T>, idx...`（结构体/数组偏移）
- `ptradd ptr, offset`

算术与比较：
- `add/sub/mul`（固定宽度整数；unsigned 回绕，signed 语义在 lowering 处固定为回绕或 trap）
- `sdiv/udiv`、`smod/umod`
- `and/or/xor/not`
- `shl`、`lshr`（无符号右移）、`ashr`（有符号右移）
- `icmp`（eq/ne/lt/le/gt/ge）
- 当前实现（MVP）：`>>` 根据整数有符号性选择 `ashr`（AArch64 `asr`）或 `lshr`（AArch64 `lsr`）；位移量在指令层按位宽取模（寄存器移位天然 mask），与 `docs/cheng-formal-spec.md` 的语义约定一致。
- 当前实现（MVP）：`/` 与 `%` 根据整数有符号性选择 `sdiv/udiv`；取模使用 `msub` 计算余数。
- 当前实现（MVP）：比较 `< <= > >=` 按有符号性选择 cond code（signed: `lt/le/gt/ge`；unsigned: `lo/ls/hi/hs`）。

调用：
- `call fn, args...`
- `tailcall`（可选）

类型转换：
- `bitcast`
- `zext/sext/trunc`
- `ptrcast`
- 当前实现（MVP）：`i32 -> i64` 使用 `sxtw`，`i64 -> i32` 截断低 32 位

所有权/运行时（显式）：
- `orc_retain ptr`
- `orc_release ptr`
- `orc_share ptr`

### 4.6 调用约定（macOS ARM64）
- 参数：整数/指针入 `x0..x7`，浮点入 `v0..v7`，多余参数走栈
- 返回：整数/指针 `x0`，浮点 `v0`
- 栈对齐：16 字节
- callee-saved：`x19..x28`, `fp(x29)`, `lr(x30)` 等
- 当前实现（MVP）：覆盖整数参数 `x0..x7`；溢出栈参数按类型大小/对齐顺序布局（例如 i32=4B 对齐，i64/ptr=8B 对齐），并保证出参区 `sp` 16B 对齐
- 当前实现（MVP）：prologue 保存 `x29/x30`，`x29` 作为 frame base

#### 4.6.1 `importc` varargs 栈传参与 ABI 细节（已实现）
- 触发条件：仅 extern/importc 且最后一个形参是 `varargs`/`varargs[T]`。
- 参数约束：至少 1 个固定参数；`T` 仅支持标量/指针子集（`int{8,16,32,64}`、`uint{8,16,32,64}`、`bool/char`、`ptr/cstring/str`）。
- AArch64 Darwin：
  - 命名参数溢出区与变参区均使用 **8 字节槽**。
  - `varargs` 的 `i32` 元素会先做符号扩展后按 8 字节写栈，避免 `va_arg` 读取不一致。
  - 调用点先落临时槽，再回装 `x0..x7`，最后 `bl`；全过程保持 `sp` 16B 对齐。
- x86_64（SysV 子集）：
  - 栈上传参统一 8 字节槽。
  - varargs 调用前显式写 `AL=0`（当前子集不使用向量寄存器传参）。
- 回归入口：`sh src/tooling/tooling_exec.sh verify_backend_ffi_abi`（`ffi_importc_varargs_direct_sum_i32` / `ffi_importc_varargs_sum10_i32`）与 `sh src/tooling/tooling_exec.sh verify_backend_ffi_outptr_tuple`。

#### 4.6.2 易遗漏的特殊实现（建议长期保留在文档）
- `importc` 聚合返回不是直接按值 ABI：统一走 wrapper/out 参数（`ffi_importc_aggret_pair_i32`）。
- `var` out 参数在调用点自动取址：源码写 `foo(x)`，ABI 实际传 `&x`（`ffi_importc_out_pair_i32`）。
- Darwin 符号名前缀处理是“仅剥一次 `_`”：内部 `__cheng_*` 不会被误剥，避免链接名错配。
- x86_64 COFF 当前仍沿用 SysV 寄存器序，不含 Windows x64 shadow space；对接系统 ABI 需 shim。
- 自研 linker 路径依赖 runtime 对象注入（`BACKEND_RUNTIME_OBJ`/`BACKEND_NO_RUNTIME_C`），漏配会出现运行时未定义符号。

### 4.7 确定性要求
- 模块内符号排序稳定（按 name 排序）
- 常量池稳定排序（按字面量与类型 key）
- IR 序列化固定字段顺序

---

## 5. 详细任务清单（可执行）

### 5.0 并行开发任务矩阵（正交/可并行）

> 目的：把本文的“流水线/目录/IR 规格/验收脚本”拆成可并行推进的正交工作包。  
> 说明：同一工作包内部存在依赖的子任务已合并为**一个**提示词（便于一次性闭环）。  
> 口径：进度以“脚本验收通过”为准；不代表跨平台/更多优化的长期项已全部完成。

| Work Package | 范围（尽量只改） | 关键依赖 | 验收入口（最小闭环） | 进度（截至 2026-02-02） |
|---|---|---|---|---|
| BK-W01：UIR 入口 + UIR internal（BK-T01~BK-T04） | `src/backend/uir/*`（主），`src/backend/uir/uir_internal/*`（internal） | `docs/cheng-formal-spec.md`（语义权威） | `BACKEND_VALIDATE=1 sh src/tooling/tooling_exec.sh verify_backend_float` + `sh src/tooling/tooling_exec.sh verify_backend_ssa` + `sh src/tooling/tooling_exec.sh verify_backend_opt2` + `sh src/tooling/tooling_exec.sh verify_backend_closedloop` + `sh src/tooling/tooling_exec.sh verify_backend_determinism_strict` | 已完成（回归通过） |
| BK-W02：指令选择/栈帧/目标文件输出 | `src/backend/machine/select_internal/*` `src/backend/machine/machine_internal/*` `src/backend/tooling/*` | UIR/ABI 约束稳定 | `sh src/tooling/tooling_exec.sh verify_backend_targets` + `sh src/tooling/tooling_exec.sh verify_backend_targets_matrix` | 已完成（AArch64 MVP；x86_64：Darwin Mach‑O obj+exe+run 闭环；Linux ELF / Windows COFF 已闭环 obj 产物校验） |
| BK-W03：自研目标文件 writer（macho/elf/coff） | `src/backend/obj/*` | reloc 模型稳定 | `sh src/tooling/tooling_exec.sh verify_backend_self_obj_writer` + `sh src/tooling/tooling_exec.sh verify_backend_self_obj_writer_macho` + `sh src/tooling/tooling_exec.sh verify_backend_self_obj_writer_coff` | 已完成（回归通过） |
| BK-W04：Tooling/并行编译/确定性 | `src/backend/tooling/*` `src/tooling/*` | 无（但影响全局） | `sh src/tooling/tooling_exec.sh verify_backend_multi` + `sh src/tooling/tooling_exec.sh verify_backend_determinism_strict` + `sh src/tooling/tooling_exec.sh verify_backend_closedloop` | 已完成（回归通过） |
| BK-W05：FFI/ABI 覆盖（importc/varargs/callback/aggret） | `src/backend/uir/uir_internal/*`（call lowering）`src/backend/machine/select_internal/*`（call conv）`tests/cheng/backend/fixtures/ffi_*` | BK-W02（调用约定） | `sh src/tooling/tooling_exec.sh verify_backend_ffi_abi` | 已完成（回归通过） |
| BK-W06：调试可观测性（DWARF 最小闭环） | `src/backend/obj/*`（必要时 `src/backend/tooling/*`） | BK-W02（prologue/栈帧） | `sh src/tooling/tooling_exec.sh verify_backend_debug` | 已恢复（最小 dSYM 口径） |

### Phase A：基础后端框架
1. 定义 UIR 数据结构与序列化
2. 语义后 AST → UIR/SSA 生成（含 CFG）
3. 中端优化：常量折叠、DCE、CFG 简化
4. Machine IR 结构定义与 lowering（保持平台无关）

### Phase B：macOS ARM64 MVP
1. AArch64 指令选择（最小指令集）
2. 线性扫描寄存器分配
3. 栈布局（prologue/epilogue）
4. 生成 Mach‑O relocatable `.o`（自研 writer）
5. 链接：优先走自研 Mach‑O linker（`BACKEND_LINKER=self`），并 `codesign -s -`（macOS 15+）

### Phase C：运行时与线程
1. ORC retain/release/runtime 接口对接
2. 线程原语封装（spawn/join/mutex/atomic）
3. 调用边界与 `Send/Sync` 约束对接

### Phase D：跨平台扩展
1. x86_64 后端（ELF/COFF）
2. Android ARM64（ELF）
3. Windows COFF + lld-link

### Phase E：优化增强（中长期）
1. 内联、SROA、CSE、LICM（已落地基础版，仍可增强）
2. 逃逸分析与引用计数削减
3. 跨模块优化（LTO）

---

## 6. 与 `cheng-formal-spec.md` 的差异

### 6.1 定位不同
- `cheng-formal-spec.md`：**语言规范**（语法与语义的权威定义）。
- 本文档：**编译器后端实现规范**（IR、ABI、流水线、并行与确定性规则）。

### 6.2 约束层级不同
- 语言规范不规定对象文件格式、调用约定、寄存器分配。
- 本文档明确：平台 ABI、对象格式、链接流程与后端约束。

### 6.3 可能的实现偏离
为了性能或确定性，后端实现可能引入：
- 额外的显式 `orc_retain/release` 指令（语言层不可见）
- 平台特定 ABI 细节（返回值、对齐规则）
- 编译期并行与缓存策略（非语言语义）

---

## 7. 下一步建议
- 现状确认：macOS ARM64 MVP、最小回归集、`BACKEND_OPT/SSA/OPT2` 的“优化闭环”与 `backend_prod_closure` 已可一口气验收通过。
- 跨平台矩阵闭环（已落地）：`sh src/tooling/tooling_exec.sh verify_backend_targets_matrix` 已纳入 `sh src/tooling/tooling_exec.sh verify_backend_closedloop`；当前已覆盖 Windows/COFF、iOS/Mach‑O、Android/Linux ELF 的 `.o/.obj` 产物校验，后续可继续扩展更多 triple 的系统化探测。
- 自研 `.o/.obj` writer（已落地）：AArch64 relocatable 目标文件已支持（后端 `emit=obj` 默认直接产出；可用 `BACKEND_OBJ_WRITER=elf|macho|coff` 强制选择）。回归入口分别为：
  - ELF：`sh src/tooling/tooling_exec.sh verify_backend_self_obj_writer`（Android/NDK）
  - Mach‑O：`sh src/tooling/tooling_exec.sh verify_backend_self_obj_writer_macho`
  - COFF：`sh src/tooling/tooling_exec.sh verify_backend_self_obj_writer_coff`
- 优化增强（已落地基础版）：opt2 已覆盖内联 + 死函数裁剪 + CFG liveness DCE + const folding +（基础版）SROA/CSE/LICM + 结构化跳转压缩（回归 `sh src/tooling/tooling_exec.sh verify_backend_opt2`）；跨模块优化已落地 multi‑lto 初版（回归 `sh src/tooling/tooling_exec.sh verify_backend_multi_lto`），仍按收益逐步增强。
- 闭环入口统一：优先用 `sh src/tooling/tooling_exec.sh backend_prod_closure` 做后端产物/发布链路验收，避免漏测。
- 生产闭环收口：`backend_prod_closure` 默认 strict（skip 即失败）；`backend.coff_lld_link` 在缺少 `lld-link/llvm-lld/ld.lld/lld` 时自动回退为 COFF `obj-only` 验证并保持 gate 通过，不再以 skip 语义降级。
- 自举性能门槛：`backend_prod_closure` 默认将 selfhost 单次编译超时设为 `240s`（`BACKEND_PROD_SELFHOST_TIMEOUT`），并默认启用 `BACKEND_PROD_SELFHOST_MAX_RSS_MB=24576` 的 RSS 守卫。
- 自举默认口径：闭环默认开启原生 compile-stage1（`BACKEND_PROD_SELFHOST_NATIVE_COMPILE_STAGE1=1`），并默认要求 `full` 真实重编（`BACKEND_PROD_SELFHOST_STAGE1_FULL_REBUILD=1` + `BACKEND_PROD_SELFHOST_STAGE1_REQUIRE_REBUILD=1`）。
- 自举 stage0 复用：当显式设置 `BACKEND_DRIVER` 且可执行时，`backend_prod_closure` 优先透传为 `SELF_OBJ_BOOTSTRAP_STAGE0`，避免重复重建本地 driver。
- 自举 stage0 默认策略：未显式指定 stage0 时优先固定 `artifacts/backend_driver/cheng`；若缺失，再通过 `backend_driver_path` 解析稳定 driver（`BACKEND_DRIVER_PATH_ALLOW_SELFHOST=0`）。当首选 stage0 健康探针失败时，selfhost 会按显式候选顺序尝试 `artifacts/backend_driver/cheng -> artifacts/backend_seed/cheng.stage2 -> artifacts/backend_selfhost_self_obj/cheng.stage2 -> ./cheng`，并跳过命中 `UE/UEs` 的候选；全部失败才阻断。
- 自举 stage0 compat overlay 已移除：生产链路固定 `SELF_OBJ_BOOTSTRAP_STAGE0_COMPAT=0`，`backend.stage0_no_compat` 默认阻断；seed stage0 若不支持当前语法，需刷新 stage0/seed，而不是对源码做 overlay rewrite。
- `backend.stage0_no_compat` 在 `mode=fast` 默认使用轻量 `stage1_input`（`tests/cheng/backend/fixtures/return_add.cheng`）以满足 `60s` gate；需要完整编译器源码重编时可设 `STAGE0_NO_COMPAT_STAGE1_INPUT=src/backend/tooling/backend_driver.cheng`。
- 破坏性语法升级流程（Checklist，工程约束）：当需要从源码层面移除/替换语法（例如 `seq[T]` -> `T[]`、`@[...]` -> `[...]`、引入列表生成式等）时，推荐按以下最小流程落地，避免自举/文档/门禁漂移：
  1. 规范与文档先行：先更新 `docs/cheng-formal-spec.md` 与相关设计文档（如 `docs/container-refactor.md`、`docs/list-comprehension.md`），并同步 `docs/cheng-skill/` 与 `$HOME/.codex/skills/cheng语言/`；跑 `sh src/tooling/tooling_exec.sh verify_cheng_skill_consistency`。
  2. 实现新语法：parser 支持 + lowering 到既有内部 canonical（尽量保持后端稳定），必要时加 parse recovery。
  3. 禁用旧语法：旧写法一律硬错误并给出明确迁移提示；旧语法只允许作为内部 lowering 目标存在。
  4. 自举兼容：生产链路不再支持 stage0 overlay；若 seed stage0 不支持新语法，直接升级 stage0/seed，并确保 `backend.stage0_no_compat` 门禁通过。
  5. 回归与 seed：补最小正/反例 tests，并跑 `sh verify.sh`（或最小相关 gate）；需要刷新 seed 时用 `sh src/tooling/tooling_exec.sh bootstrap_pure` 并加 `BOOTSTRAP_UPDATE_SEED=1`。
- 自举缓存口径固定：`backend_prod_closure` 默认传递 `SELF_OBJ_BOOTSTRAP_REUSE=1` 与 `SELF_OBJ_BOOTSTRAP_SESSION=prod`（可覆盖），降低重复冷编译概率并避免并行任务互踩。
- 自举执行稳定性：`verify_backend_selfhost_bootstrap_self_obj` 会在执行前将 stage0 driver 拷贝到 `artifacts/backend_selfhost_self_obj/cheng_stage0_<session>`；该副本仅在 stage0 内容变化时刷新，避免时间戳漂移导致的无效重编。
- 自举 smoke 复用：`verify_backend_selfhost_bootstrap_self_obj` 在 `SELF_OBJ_BOOTSTRAP_REUSE=1` 下复用 `hello_puts` smoke 产物，仅在依赖变化时重编，减少重复闭环耗时。
- 闭环耗时观测：`backend_prod_closure` 结束时输出 `backend_prod_closure.timing_top`（按耗时降序的 gate top 列表），用于持续压缩串/并行链路耗时。
- 自举并行性能阻断：`backend_prod_closure` 默认启用 `backend.selfhost_parallel_perf`（`verify_backend_selfhost_parallel_perf`），同一 stage0 下串行/并行各跑一轮 strict no-reuse，要求 `parallel <= serial + 2s`（可用 `SELFHOST_PARALLEL_PERF_MAX_SLOWDOWN_SEC` 调整）。
- 新 driver 自举 smoke 阻断：`backend_prod_closure` 默认启用 `backend.driver_selfbuild_smoke`（`verify_backend_driver_selfbuild_smoke`）；可用 `BACKEND_RUN_DRIVER_SELFBUILD_SMOKE=0` 关闭。默认 gate 超时 `60s`（`BACKEND_PROD_DRIVER_SELFBUILD_SMOKE_TIMEOUT`），内部自举超时 `55s`（`BACKEND_PROD_DRIVER_SELFBUILD_SMOKE_BUILD_TIMEOUT`），且默认输出与主 driver 统一为 `artifacts/backend_driver/cheng`（可用 `BACKEND_PROD_DRIVER_SELFBUILD_SMOKE_OUTPUT` 覆盖）。
- 热点采样可执行化：`sh src/tooling/tooling_exec.sh profile_backend_sample` 已转为 tooling 命令分发入口，实际执行 `artifacts/bin/profile_backend_sample`（由 `src/tooling/profile_backend_sample.c` 构建）；支持 `--attach-substr` 按子进程名 attach，避免采到脚本壳 `wait4` 假热点。
- 内建分段计时：`BACKEND_PROFILE=1` 时编译器会在 stderr 输出 `backend_profile\t<label>\tstep_ms=...\ttotal_ms=...`，用于补足 sample 无符号时的慢阶段定位。
- 前端分段计时：`STAGE1_PROFILE=1` 时编译器会在 stdout 输出 `[stage1] profile: ...`，并附带 `[sem]/[mono]/[ownership]` 细分统计，用于把 `backend_profile build_module` 拆到 lex/load/sem/mono/ownership。
- Stage1 outline/函数任务观测：`STAGE1_PROFILE=1` 还会输出 `outline_parse/materialize_bodies/merge_module`，并追加 `[stage1] profile_ext: fn_tasks=...ms`，用于跟踪两遍扫描与函数任务阶段耗时。
- UIR 分段计时：`UIR_PROFILE=1` 时编译器会在 stderr 输出 `uir_profile\t<label>\tstep_ms=...\ttotal_ms=...`，用于把 `backend_profile build_module` 继续拆到 `lower_top_level/fixups/...`。`MIR_PROFILE` 已移除，设置后会在 driver 直接报错。
- backend driver 快速管线开关：`BACKEND_FAST_DEV_PROFILE`、`BACKEND_STAGE1_PARSE_MODE`（`outline|full`）、`BACKEND_FN_SCHED`（`ws|serial`）、`BACKEND_FN_JOBS`、`BACKEND_DIRECT_EXE`、`BACKEND_FAST_FALLBACK_ALLOW`；dev 轨默认 `outline/ws/direct_exe`，release 轨默认 `full/serial/no-direct`。
- host darwin/arm64 的 direct-exe 路径已收敛到 `macho_direct_exe_writer`（内存直写 + self-link）；默认失败即阻断，不再静默回退到 system-link（除非显式 `BACKEND_FAST_FALLBACK_ALLOW=1`）。
- `backend_driver` 在 direct-exe 路径新增 profile 标签：`direct_exe_layout`、`direct_exe_patch`、`direct_exe_write`（随 `BACKEND_PROFILE/UIR_PROFILE` 输出）。
- 专用机 100ms 门禁：`sh src/tooling/tooling_exec.sh verify_backend_selfhost_100ms_host`（基线：`src/tooling/selfhost_perf_100ms_host.env`）；`backend_prod_closure` 在 `BACKEND_RUN_SELFHOST_100MS=1` 时拆成 quick/report 与 full/blocking 双轨：quick 轨默认 `SELFHOST_STAGE1_FULL_REBUILD=0`，full 轨默认 `SELFHOST_STAGE1_FULL_REBUILD=1` + `SELFHOST_STAGE1_REQUIRE_REBUILD=1`，并可导出 `BACKEND_BUILD_DRIVER_PROFILE_OUT` 画像日志；非目标主机仍为报告模式。
- Host-only 严格默认（2026-02-25）：`SELFHOST_STRICT_REBUILD=1`、`BUILD_DRIVER_STRICT_NATIVE=1`、`BUILD_DRIVER_ALLOW_FALLBACK=0`；`verify_backend_selfhost_100ms_host` 默认 `compile-stage1 + full rebuild + require rebuild`，并输出 `stage0_driver_kind/fallback_used/quarantine_cleaned/lock_wait_ms/strict_rebuild_ok`。
- `backend_prod_closure` required 新增 `backend.opt2_impl_surface`（`verify_backend_opt2_impl_surface`），用于阻断 `uir_core_opt2` 占位实现回归。
- 2026-02-07 冷路径优化（基于 `sample`）：
  - 热点指向 `strlen` + `get/hashMap` 路径，已落地 `src/std/strings.cheng` 的字符串 `==` 快路径（指针/nil 快判 + `c_strcmp`）与 `hasPrefix` 长度缓存。
  - 已落地 `src/std/seqs.cheng` / `src/std/system.cheng` 的 seq 访问快路径（`get/getPtr/setPtr` 与 `[]/[]=` 改为直接索引指针运算，移除 `cheng_seq_get/set` 调用开销）。
  - 已落地 `src/std/hashmaps.cheng` 的 `HashMapPtrInt` 直接指针槽位访问；字符串槽位比较进一步改为 `hashMapStrEqKnownLen`（复用 `keyLen` + `cmpMem+NUL`，移除 `len(cur)`），减少探测路径重复 `strlen`。
  - 已落地 `src/std/hashmaps.cheng` 的字符串 key 单遍元信息计算：`hashMapHash64StrMeta` 同时产出 `hash+len+first-char`，`HashMapStrInt/HashMapStrSeqInt` 查槽不再对 key 做 `len+hash` 双遍扫描。
  - 已落地 `src/backend/tooling/backend_driver.cheng` 与 `src/backend/uir/uir_internal/uir_core_builder.cheng` 的字符串判空冷路径（`driver_strIsEmpty/driver_strNonEmpty`、`mirStrIsEmpty/mirStrNonEmpty`），批量替换 `x==nil||len(x)==0` 与 `x!=nil&&len(x)>0` 的热路径判断。
  - 已落地 `backend_driver` 并行增量算法优化：`driver_collectUnitFiles` 改为 `HashMapStrInt` 去重（移除大规模线性查重），`driver_buildUnitDependencyGraph` 改为 bitset 去重依赖并一次性回填 `int32[]`，`driver_buildUnitDepClosureBits` 预计算全量传递闭包，`driver_buildUnitDepClosureStamp` 改为闭包哈希 stamp（移除 unit 级大字符串拼接）。
  - 已落地 `driver_buildUnitModule` 冷路径优化：从“每个 unit 双次全量 funcs 扫描”改为“单次扫描构建 `name->func` 索引 + 按需 extern 回填”，减少 `U×F` 级重复扫描。
  - 已落地 `backend_driver_path` 重建互斥锁：并行场景下本地 driver 重建改为串行化，避免互踩丢失。
  - 采样对比（同口径 20s）：`_platform_strlen` top-stack 计数约 `6555 -> 5990 -> 747`（最近一轮：`artifacts/profile/sample_selfhost20_after_hashopt.txt`）。
  - 实测（同机冷态，`SELF_OBJ_BOOTSTRAP_REUSE=0`）：`verify_backend_selfhost_bootstrap_self_obj`（strict, timeout=60）`total≈24-27s`（stage1≈11-13s，stage2≈12-13s）。
  - 2026-02-08：UIR call 解析引入候选缓存（避免每次 `call` 扫描全量 `mod.funcs`），并将 `ptr_add` lowering 为 UIR intrinsic；运行时 ptrmap 热路径减少 `ptr_add` 调用；同机冷态 strict 实测 `total≈24-27s`（`SELF_OBJ_BOOTSTRAP_REUSE=0`）。
  - 2026-02-08：byte 指针 load/store（`bool/char/i8/u8`）由 runtime helper（`load_bool/store_bool + memcpy`）改为 UIR 直接 `load/store`（带 `nil` 分支），减少 `_platform_memmove` 热度并降低编译时开销。
  - 2026-02-08：运行时 ORC ptrmap 优化：默认 init cap 提升到 `65536`，ptr hash 简化并在 grow rehash 中 hoist `mask`，降低 `cheng_ptrmap_grow/cheng_ptr_hash` 热路径开销。
  - 2026-02-08：`backendStripSpaces` 增加“无空白直接返回”快路径，减少大量 strip 分配与 `ptrmap_put` 压力；同机 `backend_driver.cheng` 单模块 `emit=obj` 口径 `build_module` 常见 `~11.3s -> ~10.4s`（以 `BACKEND_PROFILE/STAGE1_PROFILE/UIR_PROFILE` 输出为准）。
  - 2026-02-09：修复 `profile_backend_sample` 的 attach 等待策略：当命中 root 自身或目标不 fork 子进程时不再长时间等待，避免 target 结束后采到 zombie 导致 `sample` 失败（exit=255）。
  - 2026-02-09：`src/std/system_helpers_backend.cheng` 的 `cheng_ptr_hash` 忽略对齐位并减少一次乘法，降低 `cheng_ptr_hash/cheng_ptrmap_put` 热路径开销。
  - 2026-02-09：`src/backend/uir/uir_internal/uir_core_types.cheng` 为 `typeAliases/objTypes/cstrs` 引入 `HashMapStrInt` 索引缓存（>=32 时启用），减少字符串等号比较的线性扫描；同机 `backend_driver.cheng` 单模块 `emit=obj` 口径常见 `~13.5s -> ~13.0s`（以 `/usr/bin/time` 为准）。
- 后续优先级：先补齐 x86_64（ELF/COFF）的可执行闭环与 `lld-link` 端到端回归，其次增强调试信息与 determinism 口径；其余见上文“仍待完善（工程长期项）”清单。


---

## 9. 生产闭环推进计划（2026-02-05 更新）

> 目标：将“除平台闭环已通”推进到“跨平台可运行闭环 + CI 非 skip 门禁 + 发布治理闭环”。

### 9.1 门禁口径（新增）
- **发布门禁（必跑）**：`sh src/tooling/tooling_exec.sh backend_prod_closure` 在固定工具链环境下通过，且关键平台项不允许以 `skip(exit 2)` 代替通过。
- **平台门禁（必达）**：`darwin arm64/x86_64`、`linux x86_64`、`windows arm64/x86_64` 至少达到“obj + link + smoke”口径；其中 darwin 维持 `exe+run` 口径。
- **产物门禁（必达）**：`backend_release_manifest` + `backend_release_bundle` + `backend_release_sign` + `backend_release_verify` 全链通过，并可执行一次 rollback 演练。

### 9.2 P0（当前迭代，1-2 周）
- 固化 CI 工具链基线：准备包含 Android NDK 与 LLVM（含 `lld-link/llvm-lld`）的 runner 环境，消除关键脚本静默 skip。
- 把 `backend_prod_closure` 接入发布前强门禁：CI 失败条件包含关键项 skip（不是仅看 exit code 0）。
- 对齐跨平台最小可运行闭环目标：优先补齐 `x86_64 ELF/COFF` 的可执行链路与 `lld-link` 端到端回归。
- 验收命令（P0）：`sh src/tooling/tooling_exec.sh backend_prod_closure`、`sh src/tooling/tooling_exec.sh verify_backend_targets_matrix`、`sh src/tooling/tooling_exec.sh verify_backend_coff_lld_link`。
- 本轮已落地：
  - `backend_prod_closure` 默认 strict（skip 策略）且 selfhost 默认 `fast`；`backend.coff_lld_link` 在工具缺失时改走 COFF `obj-only` 回退（非 skip），并保留单脚本 `verify_backend_coff_lld_link` 供固定工具链环境强制门禁。
  - `verify_backend_coff_lld_link` 扩展为跨平台工具解析（`lld-link/llvm-lld/ld.lld/lld` 与 `llvm-objdump`），可在 Linux CI 端到端校验 COFF `.obj -> .dll`（含 x86_64）。
  - Linux amd64 CI 增加 `verify_backend_x86_64_linux` 与 `verify_backend_coff_lld_link` 门禁步骤。
  - `verify_backend_targets_matrix` 完成跨主机验证兼容改造（`nm/otool` 缺失时回退 `llvm-nm/llvm-objdump`），并在 Linux amd64 CI 上启用 strict matrix 门禁。
  - `verify_backend_closedloop` 改为按主机启用 x86_64 专项（Darwin 跑 `verify_backend_x86_64_darwin`，Linux x86_64 跑 `verify_backend_x86_64_linux`），避免 strict 门禁被跨主机 `skip` 干扰。

### 9.3 P1（下一迭代，2-4 周）
- Windows COFF/PE 自研 linker 能力补齐：完善未覆盖 reloc、`.reloc/ASLR`，并把“可运行闭环”从可选增量 gate 提升为可验收项。
- Determinism 口径增强：从“同机双跑”扩展到“跨机器/跨工具链版本”的复现校验与缓存键完备性。
- 调试能力增强：在自研 obj writer 内补齐 DWARF（行号/CFI/unwind），把“最小可用”提升到“可单步调试”。
- FFI/ABI 长尾覆盖：扩展 varargs 浮点、更复杂 callback、闭包捕获与析构顺序的稳定回归。
- 验收命令（P1）：`sh src/tooling/tooling_exec.sh verify_backend_self_linker_coff`、`sh src/tooling/tooling_exec.sh verify_backend_determinism_strict`、`sh src/tooling/tooling_exec.sh verify_backend_debug`、`sh src/tooling/tooling_exec.sh verify_backend_ffi_abi`、`sh src/tooling/tooling_exec.sh verify_backend_ffi_outptr_tuple`。

### 9.4 P2（发布治理，持续项）
- 远端发布/签名治理：固化 release 产物来源、签名链与审计记录，统一 stage2 seed 的来源与更新节奏。
- 回滚演练常态化：每次候选发布执行 publish + rollback 烟测，保证回退路径与脚本有效。
- CI/发布看板化：记录“通过/失败/skip”趋势，避免回归结果被 skip 掩盖。

### 9.5 风险与缓解
- 工具链缺失导致 skip：以固定 runner + 明确 fail-fast 条件规避。
- 跨平台链路碎片化：统一用 `backend_prod_closure` 聚合验收，单脚本仅作定位。
- 质量项被交付压力挤压：将 determinism/debug/FFI 长尾纳入发布门禁，而非“有空再补”。
