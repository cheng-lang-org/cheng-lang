# Cheng 规范覆盖矩阵（自动化门禁映射）

> 版本：2026-02-03

本文将 `docs/cheng-formal-spec.md` 的条目映射到仓库内的自动化回归/门禁脚本，作为“全语义覆盖”与“生产闭环”的可维护索引。

## 0. 总入口（推荐）

- **全链生产闭环（推荐）**：
  - `CHENG_CLOSEDLOOP_PROD=1 sh scripts/closedloop.sh`
  - 覆盖：Stage1 自举 + Stage1 fullspec + C-Profile 验收 + 自研后端验收 + 后端自举固定点 + 全链自举验收 + 产物签名/验签/发布（允许 skip 外部工具链项）。
- **后端生产闭环（单跑后端）**：
  - `sh src/tooling/backend_prod_closure.sh`
- **全链自举门禁（单跑全链）**：
  - `sh src/tooling/verify_fullchain_bootstrap.sh`

> 说明：部分外部工具链（Android NDK/adb、lld-link 等）缺失时脚本会 `exit 2` 触发 allow-skip；本机 Darwin/arm64 核心链路不得 skip。

---

## 1. 覆盖矩阵（按 `cheng-formal-spec.md` 章节）

| 规范章节 | 覆盖门禁（脚本/命令） | 关键样例/fixture | 验收口径 |
|---|---|---|---|
| 0.1 ORC/Ownership 闭环规则 | `./verify.sh` | `examples/test_orc_closedloop.cheng` | 运行通过；配合 `CHENG_MM_DIAG=1`/计数器可做排障 |
| 0.1 ORC runtime live-balance | `sh src/tooling/verify_backend_mm.sh` | `tests/cheng/backend/fixtures/mm_live_balance.cheng` | 运行通过（live 计数不增长） |
| 0.2 默认 move + var/共享 语义 | `./verify.sh` | `examples/test_orc_closedloop.cheng` | overwrite/return-move/alias/`defer` 等覆盖 |
| 0.3 多线程内存安全（Send/Sync） | `sh scripts/verify_send_sync_diag.sh` | `examples/diagnostics_send_sync_*` | fail 用例必须报错且包含关键诊断 |
| 0.3 并发压力（后端运行时） | `sh src/tooling/verify_backend_concurrency_stress.sh` | `tests/cheng/backend/fixtures/return_spawn_chan_i32.cheng` | 多次运行通过（默认 N=50） |
| 0.4 HRT Profile | `./verify.sh` / `sh src/tooling/hrt_prod_closure.sh --allow-skip` | `configs/hrt_runtime_constraints.env` + HRT fixtures | 约束与回归通过（缺目标工具链可 skip） |
| 0.5 直出 C Profile（FULLC） | `sh src/tooling/c_backend_prod_closure.sh`（可由 `CHENG_C_BACKEND_CLOSURE=1 ./verify.sh` 启用） | `examples/c_backend_fullspec.cheng` 等 | C-Profile 回归与（可选）确定性通过 |
| 1.1/1.2 文法与语义约束（Stage1） | `sh src/tooling/verify_stage1_fullspec.sh` | `examples/stage1_codegen_fullspec.cheng` | stdout 含 `fullspec ok` |
| 1.2 import 解析与循环 | `sh scripts/verify_stage1_import_cycle.sh` | 临时构造循环包 | 10s 内完成且不崩溃 |
| 1.1 字符串字面量边界 | `sh scripts/verify_stage1_stringlit.sh` | `tests/cheng/strings/stage1_stringlit_crash.cheng` | 生成 C 文件且不崩溃 |
| 1.2 空格单参调用约束 | `sh scripts/verify_space_call_diag.sh` | `examples/diagnostics_space_call_*` | fail 用例必须报错且包含关键诊断 |
| 1.2 借用/逃逸诊断 | `sh scripts/verify_var_borrow_diag.sh` | `examples/diagnostics_borrow_*` | fail 用例必须报错且包含关键诊断 |
| 附录 B 自研后端（全语义回归） | `sh src/tooling/verify_backend_closedloop.sh` | `examples/stage1_codegen_fullspec.cheng` | **后端编译并运行**，stdout 含 `fullspec ok`（默认 `CHENG_MM=orc`；可用 `CHENG_BACKEND_MM=off` 显式降级排障） |
| 自研后端：MIR/SSA/opt/FFI/obj/exe/determinism | `sh src/tooling/backend_prod_closure.sh` | `tests/cheng/backend/fixtures/*` | 聚合门禁全部通过（允许 skip 外部工具链项） |
| 全链自举（stage2→stage1_runner/tools） | `sh src/tooling/verify_fullchain_bootstrap.sh` | `src/stage1/frontend_stage1.cheng` + tooling 源码 | stage1_runner C 输出固定点一致；Stage1 fullspec 通过；工具 `--help` 通过 |

---

## 2. 外部工具链/允许 skip 项（非核心，但纳入 best-effort）

| 项 | 脚本 | skip 条件 |
|---|---|---|
| Android NDK 构建 | `sh src/tooling/verify_backend_android.sh` | 缺 NDK/clang/readelf/nm |
| Android 真机运行 | `sh src/tooling/verify_backend_android_run.sh` | 缺 adb 或无设备 |
| COFF + lld-link | `sh src/tooling/verify_backend_coff_lld_link.sh` | 缺 lld-link/llvm 工具 |
| 自研 ELF obj writer（Android） | `sh src/tooling/verify_backend_self_obj_writer.sh` | 缺 NDK |
