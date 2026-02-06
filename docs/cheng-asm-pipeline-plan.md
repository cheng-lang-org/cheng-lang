# Cheng 直接编译汇编管线开发计划

## 背景与现状
- 当前编译管线：`cheng` 前端直接输出 C，最终由系统 C 编译器链接为可执行文件。
- 现状：已具备“直接产出汇编”的入口与管线，但 **stage0c 直出 ASM 仍是语法子集**，尚未覆盖全语言。

## 目标
- 新增 `chengc.sh` 汇编输出管线：`cheng → .c → .s`。
- 默认兼容主流平台（x86_64/arm64）本机编译器。
- 支持 RISC-V 汇编输出：允许指定交叉编译器与目标/架构参数。

## 非目标
- 不引入旧 IR 链路，不新增独立汇编后端。
- 不保证交叉链接与运行时可用，仅保证汇编生成。
- 不引入新的优化器或 IR 变更。

## 设计要点
- CLI 增加 `--emit-asm` / `--asm-out:<path>` / `--asm-flags:<flags>` / `--asm-target:<triple>`。
- 环境变量：`CHENG_ASM_CC` / `CHENG_ASM_FLAGS` / `CHENG_ASM_TARGET` / `CHENG_ASM_ABI`。
- 汇编生成使用 `cc -S`，基于直出 C 产物。
- `--emit-asm` 仅生成 `.s`，跳过链接；默认输出 `chengcache/<name>.s`。
- 汇编管线使用 `--mode:deps` 生成模块依赖（`CHENG_DEPS_RESOLVE=1` 输出解析后的路径），写入 `chengcache/<name>.deps.*`，配合 `chengcache/<name>.asm.key` 保证缓存键覆盖环境与工具链变更。

## 直接 ASM 后端（stage0c core）
- 新增 `bin/cheng --mode:asm`，直出 `.s`，不依赖旧中间文件。
- 当前支持子集：函数序言/栈帧、参数传递、局部变量、if/while/for-range、break/continue、直接函数调用。
- 限制：for 仅支持数值范围；named args 不支持；seq/str/Table 等动态容器未覆盖；aarch64 Darwin 仍是常量返回回退。
- 目标选择：`CHENG_ASM_TARGET`（或 `CHENG_TARGET`）；支持 `x86_64/aarch64/riscv64`。
- ABI 选择：`CHENG_ASM_ABI` 或目标三元组自动判定：Darwin（macOS/iOS）、ELF（Linux/Android/鸿蒙）、COFF（Windows）。
- Darwin 目标使用 `_main` 符号；ELF/COFF 使用 `main`。

## 里程碑
### M0：规划与入口
- 明确 CLI/环境变量与输出路径。
- 更新文档与验收命令。

### M1：脚本落地
- `chengc.sh` 支持汇编输出与可选目标参数。
- C 后端构建图新增汇编任务节点。

### M2：跨平台覆盖
- 本机平台稳定输出 `.s`。
- RISC-V 通过交叉编译器或 clang target 输出 `.s`。

## 执行清单（当前迭代）
- [x] 新增汇编管线开发计划文档。
- [x] `chengc.sh` 增加汇编输出参数与环境变量。
- [x] 构建图支持汇编产出并跳过链接。
- [x] 文档补充使用方式与 RISC-V 示例。
- [x] stage0c core 新增 `--mode:asm` 直出汇编（控制流/局部变量/直接调用子集）。
- [x] 新增 `src/tooling/verify_asm_backend.sh` 覆盖多 ABI/架构验证。
- [x] `bootstrap_stage0c_core.sh --asm/--fullspec` 触发汇编后端验收。
- [x] `chengc.sh --emit-asm` 注入模块依赖与缓存键（`*.deps.*` / `*.asm.key`）以支持增量/并行。

## 验证方式
- 本机汇编输出：
  - `sh src/tooling/chengc.sh examples/asm_const_return.cheng --emit-asm`
- 直接 ASM 输出：
  - `bin/cheng --mode:asm --file:examples/asm_const_return.cheng --out:chengcache/asm_const_return.s`
- 子集 fullspec 示例：
  - `bin/cheng --mode:asm --file:examples/asm_fullspec.cheng --out:chengcache/asm_fullspec.s`
- 汇编后端全量验证：
  - `sh src/tooling/verify_asm_backend.sh`（或 `--quick`）
  - 包含 `chengc.sh --emit-asm` 管线校验（可用 `--skip-pipeline` 关闭）。
- 全语法（fullspec）编译到汇编（当前为子集 fullspec 样例）：
  - `sh src/tooling/verify_asm_backend.sh --fullspec`
- Stage0c 全链路：
  - `src/tooling/bootstrap_stage0c_core.sh --asm`
- Windows/COFF：
  - `CHENG_ASM_ABI=coff bin/cheng --mode:asm --file:examples/asm_const_return.cheng --out:chengcache/asm_const_return_win64.s`
- RISC-V：
  - `CHENG_ASM_TARGET=riscv64-unknown-elf CHENG_ASM_ABI=elf sh src/tooling/chengc.sh examples/asm_const_return.cheng --emit-asm --asm-out:chengcache/asm_const_return_rv64.s`
