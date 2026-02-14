# Cheng 工具链（Backend-only）

当前仓库提供 backend-only 主链路：

1. **`chengc.sh`**：后端 obj/exe 主入口；`chengb.sh` 仅保留兼容转发壳。
2. **并行与增量**：tooling 入口默认 `CHENG_BACKEND_MULTI=0`（稳定口径）与 `CHENG_BACKEND_INCREMENTAL=1`。
3. **完全自举**：纯 obj/exe + self-link 自举（见 `bootstrap_pure.sh`）。
4. **转发入口**：`bootstrap.sh` 转发到 `bootstrap_pure.sh`。

## chengc.sh

```bash
sh src/tooling/chengc.sh examples/stage1_codegen_fullspec.cheng --jobs:8
./stage1_codegen_fullspec
```

说明：
- 后端中间产物位于 `chengcache/<name>.o`（`--emit-obj` 时输出可通过 `--obj-out` 指定）。
- 可通过 `--name:<exeName>` 改输出二进制名。
- 可选 `--mm:<orc>`（或 `--orc`）显式设置内存模型；默认并固定 `orc`。
- 可选 `--abi:<v1|v2_noptr>`（等价于设置 `CHENG_ABI`）；默认沿用环境变量，未设置时为 `v1`。
- 包管理器接入：可选传入 `--manifest/--lock/--registry`，会生成构建元数据 `chengcache/<name>.buildmeta.toml`。
- 可选 `--pkg-cache:<dir>` 指定包缓存目录（默认 `chengcache/packages`，或 `CHENG_PKG_CACHE` 环境变量）。
- 当 lock 存在时会拉取依赖包并设置 `CHENG_PKG_ROOTS` 供 `cheng/<pkg>/...` 域名导入使用；生产包根以 `src/` 为模块根（`cheng/<pkg>/<path>` -> `<pkgroot>/src/<path>.cheng`）。`CHENG_PKG_ROOTS` 可指向包根列表或容器根（如 `~/.cheng-packages`，解析会优先尝试 `cheng-<pkg>`）。
- 源码直发拉取可通过 `--source-peer`/`--source-listen` 或环境变量 `CHENG_PKG_SOURCE_PEERS`/`CHENG_PKG_SOURCE_LISTEN` 指定源地址。
- 依赖拉取模式/peer：`CHENG_PKG_MODE=local|p2p`/`CHENG_PKG_PEERS`。
- 注册中心元数据：当提供 `--package/--channel` 时，会写入 `[snapshot]`（`cid/author_id/signature/pub_key`）。
- 构建期校验：可加 `--verify`（可选 `--ledger:<path>`）自动校验 buildmeta/lock/pkgmeta/snapshot。
- `chengc.sh` 入口仅保留 `--backend:obj`（可选 `--emit-obj` 仅产出 `.o` 并跳过链接；当前支持 AArch64 + x86_64 + riscv64 目标；非本机/非 Android 目标通常建议只做 obj 产物验证）。默认 `--linker:self`，并默认使用纯 Cheng 运行时：编译 `src/std/system_helpers_backend.cheng` 为 `chengcache/system_helpers_backend.<target>.o` 并链接。可用 `CHENG_BACKEND_RUNTIME_OBJ=<path>` 指定自定义运行时 `.o`。
- Linux AArch64 可选 no-libc profile：`CHENG_BACKEND_ELF_PROFILE=nolibc` 且 `CHENG_BACKEND_LINKER=self` 时，`chengc.sh`（以及兼容壳 `chengb.sh`）会切换到 `src/std/system_helpers_backend_nolibc_linux_aarch64.cheng`，并走无 `PT_INTERP`/`PT_DYNAMIC` 的静态链接口径；默认 profile 行为不变。
- 后端 driver 层（`src/backend/tooling/backend_driver.cheng`）：`CHENG_BACKEND_EMIT=obj` 只生成 relocatable `.o`；`CHENG_BACKEND_EMIT=exe` 会先产出中间 `.o` 再按 `CHENG_BACKEND_LINKER=self` 链接可执行文件（`chengc.sh`/`chengb.sh` 默认会清理同名 sidecar，需保留可设 `CHENG_BACKEND_KEEP_EXE_OBJ=1`）。
- 后端 driver 默认参数：`CHENG_BACKEND_EMIT=obj`、`CHENG_BACKEND_TARGET=auto`；`CHENG_BACKEND_MULTI=1`（默认并行）+ `CHENG_BACKEND_INCREMENTAL=1`（默认增量）。tooling 在少数稳定性敏感 gate 仍会显式固定串行口径。
- 后端 IR 入口：默认 `CHENG_BACKEND_IR=uir`（仅支持 `uir`）；自举/构建脚本会默认导出 `CHENG_BACKEND_IR=uir`、`CHENG_GENERIC_SPEC_BUDGET=0`。其中 fast 自举口径默认 `CHENG_GENERIC_MODE=dict`（strict 可显式设 `hybrid`）。
- UIR 观测：`CHENG_UIR_PROFILE=1` 输出 `uir_profile\t<label>\tstep_ms=...\ttotal_ms=...`，并在 `build_module` 后输出 `generics_report\tir=uir\tmode=...\tspec_budget=...`。
  - 并行实现：`emit=exe + CHENG_BACKEND_MULTI=1` 时按单元编译并链接；优先使用 `fork` worker（`CHENG_BACKEND_MULTI_FORK`，`stage1` 默认开启），不可用时回退到串行单元编译。
  - 单单元 fast-path：`emit=exe + multi` 仅在单元数 `>1` 时启用 `.objs` 多单元路径；单单元输入会走 `single.emit_obj + single.link`（避免无收益拆分与额外链接复杂度）。
  - module-cache（`.objs/.build.module.cache`；`CHENG_BACKEND_MULTI_MODULE_CACHE`）在 `emit=exe + multi` 路径读写，复用模块建图结果。
  - 默认会限制过细分桶（`CHENG_BACKEND_MULTI_MIN_BATCH_UNITS`，默认 `8`），避免 worker 过多导致重复前端开销放大。
  - 同一输出目录采用 lock 互斥（`<out>.objs.lock`），避免并发产物互踩。
  - 增量 stamp 使用“单元文件状态 hash + 编译参数 hash（含编译器身份）”，减少无关改动重编并避免跨编译器复用旧产物。
  - 可执行单对象回退：`emit=exe` + Darwin + self-link 下可设 `CHENG_BACKEND_MULTI_EXE_MAX_OBJS=<N>`（默认 `0` 禁用）在单元数超过阈值时回退“单对象编译+链接”；通常不建议开启（大模块多单元编译在当前实现下更快）。
  - `CHENG_BACKEND_MULTI_LTO=1` 在 `emit=exe + multi` 路径下采用“全模块优化 + 单对象链接”口径（`<out>.objs` 中仅保留 1 个 `.o`），避免跨单元符号裁剪漂移。

Stage1 全语法回归门禁：
```bash
sh src/tooling/verify_stage1_fullspec.sh
```

## 自研后端与全链自举

后端 driver 选择（脚本统一入口）：
```bash
# 默认优先：本地 `artifacts/backend_driver/cheng`（fresh + 可运行）
# 若缺失/过期，会自动重建本地 driver（selfhost-only，不走 seed/stage2 自动候选）
sh src/tooling/backend_driver_path.sh

# 默认：tooling/verify 脚本会在退出时清理本地 `cheng*` 产物（移动到 `chengcache/_trash_cheng`），避免遗留占用空间。
# 如需保留（加速反复调试），可关闭：
# export CHENG_CLEAN_CHENG_LOCAL=0
#
# `src/tooling/build_backend_driver.sh` 仅支持 selfhost 重建（失败即报错退出）。
#
# 使用 selfhost stage2（用于全链自举/发布）
export CHENG_BACKEND_DRIVER=artifacts/backend_selfhost_self_obj/cheng.stage2
```
说明：
- `backend_driver_path.sh` 的可运行性 smoke 默认仅校验 `mvp` 最小编译路径；如需同时校验 `stage1`，可显式设置 `CHENG_BACKEND_DRIVER_PATH_STAGE1_SMOKE=1`。
- `backend_driver_path.sh` 默认会优先复用可用的 stage2/stage1 候选 driver；仅当无可用候选或显式 `CHENG_BACKEND_DRIVER_PATH_PREFER_REBUILD=1` 时，才强制重建本地 `artifacts/backend_driver/cheng`（仍兼容回退 `./cheng`）。
- 若显式 `CHENG_BACKEND_DRIVER` 不可运行，可设 `CHENG_BACKEND_DRIVER_ALLOW_FALLBACK=1` 自动回退到可运行候选（默认保持严格失败）。

后端链接环境助手（脚本统一注入 self-linker 运行时 `.o`）：
```bash
# 输出可直接给 `env` 使用的链接环境变量
sh src/tooling/backend_link_env.sh --driver:artifacts/backend_driver/cheng --target:arm64-apple-darwin --linker:self
```
说明：
- `--linker:self`：自动构建/复用 `chengcache/system_helpers.backend.cheng.<target>.o` 并输出  
  `CHENG_BACKEND_LINKER=self CHENG_BACKEND_NO_RUNTIME_C=1 CHENG_BACKEND_RUNTIME_OBJ=<...>`。
- `--linker:auto`（默认）或未设置：不强制覆盖链接模式；生产主链建议固定 `self`。

纯 Cheng（不走 C 后端）生成/更新固定 seed（stage2 driver）：
```bash
# 以“已存在的 stage2 driver”作为 seed（推荐：来自发布包/上一版本）
sh src/tooling/backend_seed_pure.sh \
  --seed:artifacts/backend_selfhost_self_obj/cheng.stage2 \
  --out:artifacts/backend_seed/cheng.stage2

# CI/生产可直接指定 seed 路径（缺失会失败，不再使用 C seed）
export CHENG_SELF_OBJ_BOOTSTRAP_STAGE0=artifacts/backend_seed/cheng.stage2
sh src/tooling/backend_prod_closure.sh --only-self-obj-bootstrap
```

全链自举门禁（Darwin/arm64 必跑；其他平台 best-effort 可能 skip）：
```bash
sh src/tooling/verify_fullchain_bootstrap.sh
```
开发加速自举（单阶段，非发布门禁口径）：
```bash
sh src/tooling/verify_backend_selfhost_bootstrap_fast.sh
```
生产/CI 只跑后端直出 `.o` 的闭环（obj-only fullspec + tool `--help` smoke）：
```bash
CHENG_FULLCHAIN_OBJ_ONLY=1 sh src/tooling/verify_fullchain_bootstrap.sh
```
说明：
- `verify_fullchain_bootstrap.sh` 要求 stage2 driver 已存在（默认 `artifacts/backend_selfhost_self_obj/cheng.stage2`，可用 `CHENG_FULLCHAIN_STAGE2` 覆盖）。
- 缺少 stage2 时，先运行 `sh src/tooling/verify_backend_selfhost_bootstrap_self_obj.sh` 或 `sh src/tooling/verify_backend_ci_obj_only.sh --seed:<path>`。
- `verify_fullchain_bootstrap.sh` 默认开启复用（`CHENG_FULLCHAIN_REUSE=1`）：当 stage2/source/runtime 未变化时，复用 `stage1_fullspec_obj`，减少重复闭环耗时。
- fullchain 工具构建默认关闭（`CHENG_FULLCHAIN_BUILD_TOOLS=0`）；仅在需要验证 `cheng_pkg_source/cheng_pkg/cheng_storage` 时再显式设置 `CHENG_FULLCHAIN_BUILD_TOOLS=1`。开启后默认 best-effort（部分工具失败不阻断）；严格模式可设 `CHENG_FULLCHAIN_BUILD_TOOLS_STRICT=1`，并通过 `CHENG_FULLCHAIN_BUILD_TOOLS_STRICT_SCOPE=core|all` 控制范围（默认 `core` 仅约束编译器闭环，生态工具保留 best-effort；`all` 强制所有工具源码通过）。
  - 工具构建要求 `CHENG_MM=orc`，不再自动回退 `CHENG_MM=off`。
  - 启用后按主机核数并行构建（`CHENG_FULLCHAIN_TOOL_JOBS`，`0`=auto）。
  - 工具单项默认超时 `20s`（`CHENG_FULLCHAIN_TOOL_TIMEOUT`）；超时后优先复用既有可执行产物（`CHENG_FULLCHAIN_TOOL_FALLBACK_REUSE=1`）。
  - 若既有产物不可运行，默认会生成仅用于 fullchain gate 的 `--help` stub（`CHENG_FULLCHAIN_TOOL_STUB_ON_FAIL=1`）；如需“必须源码真实构建”，显式设 `CHENG_FULLCHAIN_TOOL_STUB_ON_FAIL=0`。
  - `verify_backend_selfhost_bootstrap_self_obj.sh` 支持 `CHENG_SELF_OBJ_BOOTSTRAP_TIMEOUT=<seconds>`（默认 60）防止 stage1/stage2 自举编译长时间卡死。
  - smoke 子阶段支持独立超时 `CHENG_SELF_OBJ_BOOTSTRAP_SMOKE_TIMEOUT=<seconds>`（默认 15）；用于在 `fast` 模式下快速触发 smoke fallback，避免总时长被单次卡死拉满。
- `verify_backend_selfhost_bootstrap_self_obj.sh` 默认启用 `CHENG_ABI=v2_noptr`（可显式覆盖）。
- `CHENG_ABI=v2_noptr` 默认会在 stage1 语义层启用 std no-pointer 门禁；可用 `CHENG_STAGE1_STD_NO_POINTERS=0` 显式关闭（兼容口径），或用 `CHENG_STAGE1_STD_NO_POINTERS=1`/`CHENG_STAGE1_STD_NO_POINTERS_STRICT=1` 强制严格门禁。
- 新增 `CHENG_STAGE1_NO_POINTERS_NON_C_ABI=1`：在非 C ABI 对接模块禁用 `*`/`&`/deref/`ptr_*` 等指针语法与操作（C ABI 桥接模块豁免）。
- `CHENG_STAGE1_NO_POINTERS_NON_C_ABI_INTERNAL` 默认按开启处理（仅显式设为 `0` 时放宽到编译器内部路径），因此 `src/stage1`/`src/backend`/`src/tooling` 也默认纳入 no-pointer 门禁。
- `v2_noptr` 的策略由 `verify_backend_abi_v2_noptr.sh` 负向样例门禁保证：`src/std` 路径下的指针类型必须在编译期被拒绝。
- `verify_backend_abi_v2_noptr.sh` 可通过 `CHENG_BACKEND_ABI_V2_NOPTR_ONLY=1` 切到 only-v2 口径（跳过 `v1` 探针，仅校验 `v2_noptr` 及 non-C-ABI 门禁）。
- `verify_backend_abi_v2_noptr.sh` 在 only-v2 的 non-C-ABI 子门禁下，会显式设置 `CHENG_STAGE1_STD_NO_POINTERS=0` 以隔离并验证 non-C-ABI 诊断本身。
- `chengc.sh`（以及兼容壳 `chengb.sh`）在 `CHENG_ABI=v2_noptr` 时会自动注入 `CHENG_STAGE1_STD_NO_POINTERS=1`、`CHENG_STAGE1_STD_NO_POINTERS_STRICT=0`、`CHENG_STAGE1_NO_POINTERS_NON_C_ABI=1`、`CHENG_STAGE1_NO_POINTERS_NON_C_ABI_INTERNAL=1`；严格 `std` no-pointer 由 `verify_backend_abi_v2_noptr.sh` 专项 gate 覆盖。
  - stage0 选择顺序：`CHENG_SELF_OBJ_BOOTSTRAP_STAGE0`（显式） -> 可执行 `CHENG_BACKEND_DRIVER` -> `artifacts/backend_driver/cheng` -> `dist/releases/current/cheng` -> `artifacts/backend_selfhost_self_obj/cheng.stage2` -> `artifacts/backend_selfhost_self_obj/cheng.stage1` -> `artifacts/backend_seed/cheng.stage2` -> 可运行 `./cheng`（缺失或不可运行时才重建）。seed 仅用于“首次/无自举产物”的兜底。
  - 自举模式：`CHENG_SELF_OBJ_BOOTSTRAP_MODE=strict|fast`（默认 `fast`）。`strict` 校验 `stage1->stage2` 固定点；若首次不收敛，会自动追加 `stage3 obj witness`（`F(stage2)` 的 `emit=obj`）并以 `stage2->stage3(obj)` 固定点作为收敛门禁。`fast` 只编译 stage1 并同步为 stage2（开发加速，跳过 fixed-point 校验）。
  - `fast` 模式默认沿用“优先复用已有 stage1/stage2”（`CHENG_SELF_OBJ_BOOTSTRAP_FAST_REUSE_STALE=1`）；如需在 fast 下强制检查是否过期并触发重编，可设 `CHENG_SELF_OBJ_BOOTSTRAP_FAST_REUSE_STALE=0`。
  - 自举编译默认口径：`CHENG_SELF_OBJ_BOOTSTRAP_MULTI=1`、`CHENG_SELF_OBJ_BOOTSTRAP_INCREMENTAL=1`；`strict` 模式并行失败会自动回退一次串行，`fast` 模式默认禁重试并自动按逻辑核数设置 jobs（可用 `CHENG_SELF_OBJ_BOOTSTRAP_FAST_JOBS_CAP` 设上限，默认 `8`）。
  - 超时策略：编译返回 `124`（timeout）时默认不再做同参重试，避免单阶段耗时翻倍掩盖性能回退；如需允许 stage0 兜底重编，可显式设置 `CHENG_SELF_OBJ_BOOTSTRAP_ALLOW_STAGE0_FALLBACK=1`（默认 `0`）。
  - `emit=exe` 若未产出 sidecar `.o`/`.objs`，selfhost 门禁会自动追加一次 `emit=obj` 生成对比对象，保证 fixed-point / smoke 对象比较口径稳定。
  - stage1 语法兼容：`fast` 模式仅在 `stage1.native` 日志命中解析类错误时才触发 `stage1.compat` overlay；timeout/非语法错误默认直接失败，保持延迟可控。
  - `stage1.compat` 触发关键字已收紧为解析错误语义（`Unexpected token` / `stage1 errors` / `parse error` / `parser error`），避免命中普通 profile 文本中的 `parse_*` 造成误触发重编。
  - 自举临时产物默认使用稳定 session（`CHENG_SELF_OBJ_BOOTSTRAP_SESSION=default`），可复用 `<out>_tmp_<session>.objs` 缓存；并行多任务可显式设置不同 session 避免互相覆盖。
  - 执行结束会输出 `backend.selfhost_self_obj.timing`（lock/stage1/stage2/smoke/total 秒数），并落盘 `selfhost_timing_<session>.tsv` 与 `selfhost_metrics_<session>.json`（可用 `CHENG_SELF_OBJ_BOOTSTRAP_TIMING_OUT` / `CHENG_SELF_OBJ_BOOTSTRAP_METRICS_OUT` 覆盖路径）；失败/超时路径也会补写 `total` 行（`fail`/`fail-timeout`）。
  - 可在问题排查时显式关闭：`CHENG_SELF_OBJ_BOOTSTRAP_MULTI=0 CHENG_SELF_OBJ_BOOTSTRAP_INCREMENTAL=0`。
  - 2026-02-08 同机冷态实测：`fast` 约 `17s`，`strict` 约 `24-27s`；`backend_prod_closure.sh --only-self-obj-bootstrap --selfhost-strict` 在 `60s` 约束内通过。
  - 2026-02-06：已修复一类“无输出超时”根因（`src/std/strings.cheng` 的字符串 `==` 对 `nil` 比较递归调用自身）；该问题会让 selfhost 阶段卡住直到超时。
  - 修复后若 selfhost 失败，将优先以可诊断错误退出（而非长时间超时卡住）。
- `CHENG_FULLCHAIN_OBJ_ONLY=1` 默认使用 `examples/backend_fullchain_smoke.cheng` 作为 obj-only 样例（要求输出 `fullspec ok`）；可用 `CHENG_FULLCHAIN_STAGE1_FILE=<path>` 覆盖。
- fullchain stage1 样例编译默认走 `CHENG_FULLCHAIN_STAGE1_FRONTEND=mvp`（60s 口径更稳定）；需要强制 stage1 前端时可设 `CHENG_FULLCHAIN_STAGE1_FRONTEND=stage1`。
- fullchain stage1 样例编译默认超时 `60s`（`CHENG_FULLCHAIN_STAGE1_TIMEOUT`），默认并行 `CHENG_FULLCHAIN_STAGE1_MULTI=1`；并行失败会自动串行重试。可用 `CHENG_FULLCHAIN_STAGE1_MULTI` / `CHENG_FULLCHAIN_STAGE1_JOBS` 调整并行参数。
- fullchain stage1 样例链接器可用 `CHENG_FULLCHAIN_STAGE1_LINKER=auto|self|system` 控制（默认 `auto`：先尝试 `self`，失败后回退 `system`）。

CI/生产（macOS arm64）使用 dist seed 跑“后端自举 + fullchain smoke”：
```bash
sh src/tooling/verify_backend_ci_obj_only.sh
```
说明：
- 默认从 `dist/releases/current_id.txt` 解析 seed；也可传 `--seed:<path>` 指定。
- 若 dist seed 不存在，会直接失败并提示补充 seed（可用 `--require-seed` 强制严格模式）。
- 不会自动使用 `artifacts/backend_seed/cheng.stage2` 或 `artifacts/backend_selfhost_self_obj/cheng.stage2`。
- 默认额外执行 `backend.ci.selfhost_perf_regression`（读取 `selfhost_timing_<session>.tsv` 做阈值检查）。
- selfhost perf 阈值默认来自 `src/tooling/selfhost_perf_baseline.env`；可用 `CHENG_SELFHOST_PERF_BASELINE=<path>` 切换基线文件，或直接用 `CHENG_SELFHOST_PERF_MAX_*` 覆盖单项阈值。

后端生产闭环（默认快速口径；fullchain/stress 按需开启）：
```bash
sh src/tooling/backend_prod_closure.sh
sh src/tooling/backend_prod_closure.sh --fullchain
sh src/tooling/backend_prod_closure.sh --stress
```
说明：
- `backend_prod_closure.sh` 默认不跑 fullchain/stress；可用 `--fullchain` / `CHENG_BACKEND_RUN_FULLCHAIN=1` 和 `--stress` / `CHENG_BACKEND_RUN_STRESS=1` 显式开启。
- `backend_prod_closure.sh` 默认在 `backend.closedloop` 中关闭 fullspec（等价注入 `CHENG_BACKEND_RUN_FULLSPEC=0`）；需要时可显式设 `CHENG_BACKEND_RUN_FULLSPEC=1` 开启。
- `backend_prod_closure.sh` 默认 strict：任一步骤 `exit 2`（skip）会直接失败；仅本地排障时再显式加 `--allow-skip`。
- `backend_prod_closure.sh` 现在仅支持 `CHENG_ABI=v2_noptr`（若外部传入非 `v2_noptr` 会直接报错退出）；主闭环以 `v2_noptr` 兼容口径执行（`CHENG_STAGE1_STD_NO_POINTERS=0`），并通过 `backend.abi_v2_noptr` 步骤单独执行严格 no-pointer 门禁。
- `backend_prod_closure.sh` 默认包含 `backend.abi_v2_noptr` 专项门禁（`src/tooling/verify_backend_abi_v2_noptr.sh`）。
- `backend.abi_v2_noptr` 默认优先使用 selfhost `artifacts/backend_selfhost_self_obj/cheng.stage1` 执行（再回退 `cheng.stage2` 与当前 `CHENG_BACKEND_DRIVER`）；可用 `CHENG_BACKEND_ABI_V2_DRIVER` 显式覆盖。
- `backend_prod_closure.sh` 的主门禁链路在 `CHENG_STAGE1_NO_POINTERS_NON_C_ABI=1` 时默认不切换到 selfhost driver（避免 strict non-C-ABI 与历史 gate 口径冲突）；可用 `CHENG_BACKEND_MAIN_ALLOW_SELFHOST_DRIVER=1` 强制主链使用 selfhost driver。
- `backend_prod_closure.sh` 在 selfhost 成功后会优先切换后续 gate 到 `artifacts/backend_selfhost_self_obj/cheng.stage2`（即使最初显式传入了 `CHENG_BACKEND_DRIVER` 作为 stage0）；若未跑 selfhost，且 `CHENG_BACKEND_DRIVER` 未显式设置，才回落到 `backend_driver_path.sh`。
- 若主链选中的 `CHENG_BACKEND_DRIVER` 体检失败，`backend_prod_closure.sh` 会自动回退到可运行的 selfhost driver（优先 `cheng.stage1`，其次 `cheng.stage2`），再回退到 `backend_driver_path.sh` 的重选结果。
- `backend_prod_closure.sh` 默认导出 `CHENG_BACKEND_DRIVER_ALLOW_FALLBACK=1`，避免显式 driver 失效时整条链路被单点阻断。
- `backend_prod_closure.sh` 在主 driver 完成体检后会导出 `CHENG_BACKEND_DRIVER_PATH_STAGE1_SMOKE=0`，后续 gate 不再重复 stage1 smoke（保留主链稳定性并减少抖动）。
- `verify_backend_closedloop.sh` 默认执行 `backend.spawn_api_gate`（`src/tooling/verify_backend_spawn_api_gate.sh`）；该 gate 已切换到不依赖 `std/async_rt` 指针实现的 fixture，因此在 `CHENG_ABI=v2_noptr` 下也可直接回归。
- `verify_backend_closedloop.sh` 中 `backend.x86_64_darwin`/`backend.x86_64_linux` 作为目标矩阵扩展项，即使 `CHENG_BACKEND_MATRIX_STRICT=1` 也允许 `skip`（不阻断主闭环）。
- `verify_backend_obj_fullspec_gate.sh` 默认复用已有 `artifacts/backend_obj_fullspec_gate/backend_obj_fullspec`（避免重复冷编译超时）；如需强制重编可设 `CHENG_BACKEND_OBJ_FULLSPEC_REBUILD_ON_SOURCE=1` 或 `CHENG_BACKEND_OBJ_FULLSPEC_REBUILD_ON_DRIVER=1`。
- `backend_prod_closure.sh` 默认将自举阶段单次编译超时设为 `60s`（`CHENG_BACKEND_PROD_SELFHOST_TIMEOUT`），用于及早暴露性能回退。
- `backend_prod_closure.sh` 默认对每个 gate 施加 `60s` 超时（`CHENG_BACKEND_PROD_GATE_TIMEOUT`，设为 `0` 可关闭），超时直接失败并打印 gate 标签。
- `backend_prod_closure.sh` 默认开启超时诊断（`CHENG_BACKEND_PROD_TIMEOUT_DIAG=1`）：当 gate 超时会自动采样并输出诊断文件路径（默认目录 `chengcache/backend_timeout_diag`，采样时长 `CHENG_BACKEND_PROD_TIMEOUT_DIAG_SECONDS=5`，可通过 `CHENG_BACKEND_PROD_TIMEOUT_DIAG_TOOL` 覆盖采样器命令）。
- `backend_prod_closure.sh` 默认开启超时摘要（`CHENG_BACKEND_PROD_TIMEOUT_DIAG_SUMMARY=1`）：超时后会调用 `src/tooling/summarize_timeout_diag.sh` 输出热点 TopN（`CHENG_BACKEND_PROD_TIMEOUT_DIAG_SUMMARY_TOP`）。
- `backend_prod_closure.sh` 默认开启 `backend.selfhost_perf_regression`（`CHENG_BACKEND_RUN_SELFHOST_PERF=1`），读取 `selfhost_timing_<session>.tsv` 检查 stage1/stage2/total 阈值并在超阈值时失败。
- `backend.selfhost_perf_regression` 默认加载 `src/tooling/selfhost_perf_baseline.env`（可通过 `CHENG_SELFHOST_PERF_BASELINE` 覆盖）；发布 bundle 会附带该基线文件以保证阈值口径一致。
- 当 `CHENG_BACKEND_RUN_FULLSPEC=1` 时，`backend.closedloop` 使用独立超时 `CHENG_BACKEND_PROD_CLOSEDLOOP_TIMEOUT`（默认 `120s`），避免 fullspec 编译被通用 60s gate 误杀。
- `backend_prod_closure.sh` 的 selfhost 自举步骤会显式设置 `CHENG_STAGE1_NO_POINTERS_NON_C_ABI=0` 与 `CHENG_STAGE1_NO_POINTERS_NON_C_ABI_INTERNAL=0`（仅用于自举编译器源码），non-C-ABI no-pointer 策略仍由后续 `backend.closedloop`/`backend.abi_v2_noptr` 门禁强制。
- `backend_prod_closure.sh` 默认自举模式为 `fast`；可用 `--selfhost-strict`（或 `CHENG_BACKEND_PROD_SELFHOST_MODE=strict`）切到 fixed-point 口径。
- `backend_prod_closure.sh` 提供显式参数 `--selfhost-fast` / `--selfhost-strict`（优先级高于环境变量）；并支持 `--selfhost-strict-gate`（或 `CHENG_BACKEND_RUN_SELFHOST_STRICT=1`）在 `fast` 主链后追加一轮 `strict` 自举门禁。
- `--selfhost-strict-gate` 默认复用 fast 同一 session（避免重复冷编译）；可用 `CHENG_BACKEND_PROD_SELFHOST_STRICT_SESSION=<name>` 指定独立 strict session。
- `--selfhost-strict-gate` 默认启用 `CHENG_BACKEND_PROD_SELFHOST_STRICT_ALLOW_FAST_REUSE=1`：strict 口径优先复用 fast 生成的 `stage1/stage2` 产物，避免因源码时间戳触发重复冷编译超时；设为 `0` 可恢复 strict 重编口径。
- `backend_prod_closure.sh` 支持可选 `--selfhost-strict-noreuse-probe`（或 `CHENG_BACKEND_RUN_SELFHOST_STRICT_NOREUSE_PROBE=1`）追加 strict 冷路径探针（固定 `reuse=0`、`strict_allow_fast_reuse=0`，默认软探针）。
- strict no-reuse 探针默认不阻断主链：可通过 `CHENG_BACKEND_SELFHOST_STRICT_NOREUSE_PROBE_REQUIRE=1` 改为阻断；可用 `CHENG_BACKEND_PROD_SELFHOST_STRICT_NOREUSE_SESSION`、`CHENG_BACKEND_PROD_SELFHOST_STRICT_NOREUSE_ALLOW_STAGE0_FALLBACK` 覆盖会话与回退策略；探针 gate 超时独立于通用 gate（`CHENG_BACKEND_PROD_SELFHOST_STRICT_NOREUSE_GATE_TIMEOUT`，默认 `75s`），内部探针超时可单独设 `CHENG_BACKEND_PROD_SELFHOST_STRICT_NOREUSE_PROBE_TIMEOUT`，并会自动夹到 `< gate timeout` 以避免软探针被外层 gate 误判为硬失败。
- `verify_backend_selfhost_strict_noreuse_probe.sh` 默认使用 `CHENG_SELFHOST_STRICT_PROBE_GENERIC_MODE=hybrid`、`CHENG_SELFHOST_STRICT_PROBE_SKIP_CPROFILE=1`、`CHENG_SELFHOST_STRICT_PROBE_MULTI=0`、`CHENG_SELFHOST_STRICT_PROBE_MULTI_FORCE=0`、`CHENG_SELFHOST_STRICT_PROBE_ALLOW_RETRY=0`；`dict` 路径当前用于探索性诊断，可能生成不可运行的自举编译器产物。
- 若已显式设置 `CHENG_BACKEND_DRIVER` 且可执行，`backend_prod_closure.sh` 会优先将其作为 selfhost stage0，避免额外重建本地 driver。
- 若未显式提供 stage0，`backend_prod_closure.sh` 会按“可编译 smoke 探测”优先复用 `artifacts/backend_selfhost_self_obj/cheng.stage2`，其次 `cheng.stage1`，再其次 `dist/releases/current/cheng` / `artifacts/backend_seed/cheng.stage2`；探测失败（如可执行但编译崩溃）会自动跳过并回退到 `backend_driver_path.sh`。
- stage0 探针默认走 `CHENG_BACKEND_PROD_STAGE0_PROBE_MODE=path`（仅复用 `backend_driver_path.sh` 的可运行 smoke，不再额外重编 `backend_driver.cheng`）；可切到 `light`（mvp 小样例）或 `full`（历史重型探针）排查问题。
- `backend_prod_closure.sh` 默认固定 selfhost 复用口径：`CHENG_BACKEND_PROD_SELFHOST_REUSE=1`、`CHENG_BACKEND_PROD_SELFHOST_SESSION=prod`（可覆盖），减少重复冷编译与并发互踩。
- `verify_backend_selfhost_bootstrap_self_obj.sh` 会把 stage0 driver 固化拷贝到 `artifacts/backend_selfhost_self_obj/cheng_stage0_<session>` 后再执行，且仅在 stage0 **内容变化**时更新该副本，避免因时间戳漂移触发无效重编。
- `verify_backend_selfhost_bootstrap_self_obj.sh` 在 `CHENG_SELF_OBJ_BOOTSTRAP_REUSE=1` 下会复用 smoke 产物（`hello_puts`）并仅在依赖变化时重编，降低重复验收耗时。
- `verify_backend_selfhost_bootstrap_self_obj.sh` 在 `CHENG_GENERIC_MODE` 未显式设置时：`fast` 默认 `dict`、`strict` 默认 `hybrid`；并且 timeout 回收采用进程组+进程本体双重 kill，避免遗留孤儿编译进程。
- `backend_prod_closure.sh` 默认执行 `backend.coff_lld_link`；`verify_backend_coff_lld_link.sh` 在缺少 `lld-link/llvm-lld/ld.lld/lld` 时自动回退为 COFF `obj-only` 校验（不再 skip）。
- `backend_prod_closure.sh` 结束时会输出 `backend_prod_closure.timing_top`（按耗时降序的 gate top 列表），用于持续压缩闭环耗时。
- 生产/CI 可用 `--require-seed` 禁止使用本机 stage0；`--require-seed` 需要显式传 `--seed/--seed-id/--seed-tar`。
- 发布（默认开启）要求显式 seed：需传 `--seed/--seed-id/--seed-tar`；仅做本地闭环可加 `--no-publish`。
- `scripts/closedloop.sh` 在 `CHENG_CLOSEDLOOP_PROD=1` 下默认以 `--no-publish` 运行 `backend_prod_closure.sh`；若需要发布门禁，请显式设置 `CHENG_CLOSEDLOOP_BACKEND_PROD_ARGS` 传入 seed 参数。
- `scripts/closedloop.sh` 默认导出 `CHENG_BACKEND_PROD_GATE_TIMEOUT=60`、`CHENG_BACKEND_PROD_SELFHOST_TIMEOUT=60`、`CHENG_BACKEND_PROD_TIMEOUT_DIAG=1`、`CHENG_BACKEND_RUN_SELFHOST_PERF=1`。
- `scripts/closedloop.sh` 支持 `CHENG_CLOSEDLOOP_FRONTIER=auto|0|1`（默认 `auto`）：自动检测到 `cheng-libp2p` 仓库时追加 `verify_libp2p_frontier`；`auto` 默认非阻断（`CHENG_CLOSEDLOOP_FRONTIER_SOFT_AUTO=1`），显式 `1` 仍为阻断门禁。
- `scripts/closedloop.sh` 末尾默认尝试输出 timeout 采样摘要（`CHENG_CLOSEDLOOP_TIMEOUT_SUMMARY=1`）。
- `build_backend_driver.sh` 自举重建默认采用并行增量（`CHENG_BACKEND_BUILD_DRIVER_MULTI=1`）；并行失败会自动串行重试。可用 `CHENG_BACKEND_BUILD_DRIVER_MULTI` / `CHENG_BACKEND_BUILD_DRIVER_INCREMENTAL` / `CHENG_BACKEND_BUILD_DRIVER_JOBS` 覆盖。
- `build_backend_driver.sh` 未显式指定 stage0 时，会优先尝试稳定 seed（`artifacts/backend_driver/cheng`、`dist/releases/current/cheng`、`artifacts/backend_selfhost_self_obj/cheng_stage0_*`），再回退到 `artifacts/backend_seed/cheng.stage2` / 本地 `cheng` / `artifacts/backend_selfhost_self_obj/cheng.stage2|stage1`。
- `build_backend_driver.sh` 默认 `CHENG_GENERIC_MODE=dict`（可覆盖）；默认单次编译尝试超时 `60s`（`CHENG_BACKEND_BUILD_DRIVER_TIMEOUT`）。自举超时/失败时若 stage0 可运行且 smoke 通过，会自动回退 stage0 并返回成功（可用 `CHENG_BACKEND_BUILD_DRIVER_ALLOW_STAGE0_FALLBACK=0` 关闭）。
- 在 `CHENG_MM=orc CHENG_BACKEND_LINKER=self` 口径下，`opt/opt2/multi-lto/ssa/debug`（以及可选 `stress`）gate 已统一固定串行口径（`CHENG_BACKEND_MULTI=0`、`CHENG_BACKEND_MULTI_FORCE=0`、`CHENG_BACKEND_WHOLE_PROGRAM=1`）以保证稳定可复现。
- `verify_backend_concurrency_stress.sh` 默认跳过（`CHENG_BACKEND_CONCURRENCY_STRESS_ENABLED=0`）；需要并发压力回归时显式设置 `CHENG_BACKEND_CONCURRENCY_STRESS_ENABLED=1`。
- `verify_backend_stress.sh` 默认使用 stage1 smoke（`hello_puts`）循环运行（`CHENG_BACKEND_STRESS_N`，默认 10），并对编译与单次运行施加 `60s` 超时（`CHENG_BACKEND_STRESS_TIMEOUT`）。
- `verify_backend_concurrency_stress.sh` 在启用后（`CHENG_BACKEND_CONCURRENCY_STRESS_ENABLED=1`）对编译与单次运行施加 `60s` 超时（`CHENG_BACKEND_CONCURRENCY_TIMEOUT`）。
- `verify_backend_ffi_abi.sh` 在 `CHENG_BACKEND_LINKER=self` 口径应直接通过，不依赖脚本内额外兜底逻辑。

后端热点定位（sample，可执行版）：
```bash
# 自动构建并执行 C 版采样器（wrapper: profile_backend_sample.sh）
sh src/tooling/profile_backend_sample.sh --preset:fullchain-cold --duration:20 --top:12 --kill-after-sample

# 直接指定命令；支持对子进程名 attach，避免采到脚本壳 wait4
sh src/tooling/profile_backend_sample.sh --duration:12 --attach-substr:cheng.stage2 -- \
  env CHENG_CLEAN_CHENG_LOCAL=0 sh src/tooling/verify_fullchain_bootstrap.sh
```
说明：
- `src/tooling/profile_backend_sample.sh` 现在仅做包装：首次会调用 `src/tooling/build_profile_backend_sample.sh` 生成 `artifacts/bin/profile_backend_sample`（C 可执行文件）。
- 采样参数为命令行选项（`--preset/--duration/--interval-ms/--attach-substr/...`），不再依赖 shell 字符串拼接。
- preset 默认 `--attach-substr:cheng`，优先采样编译器子进程而非脚本父进程。
- `profile_backend_sample` 会从 sample 输出的 call graph 提取并按计数排序热点帧（过滤 `Thread_/main/start` 等噪声），便于快速定位热函数。
- `src/tooling/summarize_timeout_diag.sh` 可离线汇总 timeout 采样文件：
  - `sh src/tooling/summarize_timeout_diag.sh --dir:chengcache/backend_timeout_diag --latest:3 --top:12`
  - `sh src/tooling/summarize_timeout_diag.sh --file:chengcache/backend_timeout_diag/<file>.sample.txt --top:20`
- `sample` 想看到完整调用栈与符号名：
  - 需要后端生成“可 unwind 的栈帧”（FP 链）+ 可执行文件包含符号表；
  - self-linker 口径建议用 `CHENG_BACKEND_LINKER_SYMTAB=all`（或 `--linker-symtab=all`）重建目标二进制，否则常见现象是 call graph 大量 `???`。
- `CHENG_BACKEND_PROFILE=1` 可启用后端内建分段计时：编译时在 stderr 输出 `backend_profile\t<label>\tstep_ms=...\ttotal_ms=...`，用于补足 sample 无符号时的慢阶段定位。
- `CHENG_UIR_PROFILE=1` 可启用 UIR 口径分段计时：编译时在 stderr 输出 `uir_profile\t<label>\tstep_ms=...\ttotal_ms=...`，并补充 `generics_report\t...`。
- `CHENG_STAGE1_PROFILE=1` 可启用 stage1 前端分段计时：在 stdout 输出 `[stage1] profile: lex=... load=... sem=... mono=... ownership=... total=...`，并附带 `[sem]/[mono]/[ownership]` 的细分统计，用于把 `backend_profile build_module` 拆到更具体阶段。
- `CHENG_MIR_PROFILE=1` 为兼容口径（已废弃，建议改用 `CHENG_UIR_PROFILE=1`）；当前仅用于 MIR 兼容层内部排障。
- `sample` 若显示热点集中在 `strlen` 与 `get/hashMap`，优先检查 `src/std/strings.cheng`、`src/std/seqs.cheng`、`src/std/hashmaps.cheng` 以及 `src/backend/tooling/backend_driver.cheng`、`src/backend/mir/mir_builder.cheng` 的字符串判空冷路径实现（当前已落地字符串 `==` 的 `c_strcmp` 快路径、`hashMapStrEqKnownLen` 的 `cmpMem+NUL` 比较，以及 `driver_strIsEmpty/mirStrIsEmpty` 判空快路径）。
- 2026-02-07 同口径 20s 采样：`_platform_strlen` top-stack 计数约 `6555 -> 5990`。
- 2026-02-08：修复 stage1 `loadTokenCache` 热路径反复 `strlen`（`sliceRangePlainKnownLen`），同口径下 `_platform_strlen` 不再主导 top-stack。
- 2026-02-08：Mach-O obj writer 将 `machoFindSymIndex` 由线性扫描改为 hash 索引，单模块 `emit=obj` 的 `single.emit_obj` 常见从 `~12s` 降到 `~3.6s`（以 `backend_driver.cheng` 编译为例）。
- 2026-02-08：`ptr_add` 改为 MIR intrinsic + 运行时 ptrmap 减少 `ptr_add` 调用后，selfhost strict 冷态 `total≈24-27s`（stage1≈11-13s，stage2≈12-13s；`CHENG_SELF_OBJ_BOOTSTRAP_REUSE=0`）。
- 2026-02-08：byte 指针 load/store（`bool/char/i8/u8`）由 runtime helper（`load_bool/store_bool + memcpy`）改为 MIR 直接 `load/store`（带 `nil` 分支），减少 `_platform_memmove` 热度并降低编译时开销。
- 2026-02-08：运行时 ORC ptrmap 优化：默认 init cap 提升到 `65536`，ptr hash 简化并在 grow rehash 中 hoist `mask`，降低 `cheng_ptrmap_grow/cheng_ptr_hash` 热路径开销。
- 2026-02-08：`backendStripSpaces` 增加“无空白直接返回”快路径（避免大量 `strip` 分配），`backend_driver.cheng` 单模块 `emit=obj` 口径下 `build_module` 常见 `~11.3s -> ~10.4s`，端到端 `~14.4s -> ~13.3s`（以 `CHENG_BACKEND_PROFILE/CHENG_STAGE1_PROFILE/CHENG_MIR_PROFILE` 输出为准）。
- 2026-02-09：修复 `profile_backend_sample` 的 attach 等待策略：当命中 root 自身或目标不 fork 子进程时不再长时间等待，避免 target 结束后采到 zombie 导致 `sample` 失败（exit=255）。
- 2026-02-09：`src/std/system_helpers_backend.cheng` 的 `cheng_ptr_hash` 忽略对齐位并减少一次乘法，降低 `cheng_ptr_hash/cheng_ptrmap_put` 热路径开销。
- 2026-02-09：`src/backend/mir/mir_types.cheng` 为 `typeAliases/objTypes/cstrs` 引入 `HashMapStrInt` 索引缓存（>=32 时启用），减少字符串等号比较的线性扫描；同机 `backend_driver.cheng` 单模块 `emit=obj` 常见 `~13.5s -> ~13.0s`（以 `/usr/bin/time` 为准）。

后端闭环验收（统一 ORC + self linker）：
```bash
CHENG_MM=orc CHENG_BACKEND_LINKER=self sh src/tooling/verify_backend_closedloop.sh
CHENG_MM=orc CHENG_BACKEND_LINKER=self CHENG_BACKEND_RUN_FULLSPEC=1 sh src/tooling/verify_backend_closedloop.sh
CHENG_MM=orc sh src/tooling/verify_backend_self_linker_riscv64.sh
```
说明：
- `verify_backend_closedloop.sh` 默认 `CHENG_BACKEND_RUN_FULLSPEC=0`（不跑 fullspec）；显式设为 `1` 才开启 fullspec 编译+运行门禁。
- `verify_backend_closedloop.sh` 已纳入 `backend.spawn_api_gate`，对应脚本为 `src/tooling/verify_backend_spawn_api_gate.sh`（默认 API 禁 raw spawn、legacy 显式入口可用；当前 `fn()` 入口正向口径为 `spawn(entry)`）。
- `CHENG_BACKEND_RUN_FULLSPEC=1` 默认使用 `examples/backend_closedloop_fullspec.cheng`；可用 `CHENG_BACKEND_CLOSEDLOOP_FULLSPEC_INPUT` 覆盖输入文件。
- fullspec 编译默认口径为 `CHENG_BACKEND_FULLSPEC_SKIP_SEM=1`、`CHENG_BACKEND_FULLSPEC_GENERIC_MODE=hybrid`、`CHENG_BACKEND_FULLSPEC_GENERIC_SPEC_BUDGET=0`、`CHENG_BACKEND_FULLSPEC_SKIP_OWNERSHIP=1`、`CHENG_BACKEND_FULLSPEC_VALIDATE=0`，并固定 `CHENG_BACKEND_WHOLE_PROGRAM=1`（默认并行优先，失败后串行回退）。
- `verify_backend_mm.sh` 默认运行 `mm_live_balance + mm_container_balance`；容器用例默认使用 `CHENG_BACKEND_MM_CONTAINER_FRONTEND=mvp`（可显式设为 `stage1`）。如需关闭容器用例可设 `CHENG_BACKEND_MM_CONTAINER=0`。
- `verify_backend_multi.sh` 保持并行优先；并行失败会自动串行重试，避免偶发并行崩溃阻断闭环。
- `verify_backend_self_linker_riscv64.sh` 默认口径为 `CHENG_MM=orc`，并优先复用 `artifacts/backend_selfhost_self_obj/cheng.stage2`（若存在）以避免 gate 内重建 driver 触发超时。

MIR float 回归（MVP）：
```bash
sh src/tooling/verify_backend_mvp.sh
```
说明：
- `verify_backend_mvp.sh` 当前覆盖 `return_float64_ops`、`return_float32_roundtrip`、`return_float_mixed_int_cast`、`return_float_compare_cast`、`return_float32_arith_chain`；当前口径已验证 `f64/f32` 算术、比较与 `f64/f32 <-> int` 基础 cast 语义（向零截断）。

Linux AArch64 no-libc 独立验收（不接入默认 verify）：
```bash
CHENG_BACKEND_ELF_PROFILE=nolibc sh src/tooling/verify_backend_nolibc_linux_aarch64.sh
```
说明：
- 静态验收（macOS 可跑）：检查 `elf64-littleaarch64`、无 `PT_INTERP`/`PT_DYNAMIC`、无 undefined symbols、无 `libc.so/ld-linux-aarch64` 字符串。

libp2p frontier 编译探针：
```bash
sh src/tooling/verify_libp2p_frontier.sh
```
说明：
- 脚本支持 `--help`；未知参数会直接报错退出。
- import 探针（`cheng_probe_conn_import` / `cheng_probe_multihash_import_only`）默认走 `mvp` 前端（`CHENG_FRONTIER_IMPORT_FRONTEND=mvp`），超时默认 `30s`（`CHENG_FRONTIER_IMPORT_TIMEOUT`）。
- 重量探针（`mdns_smoke` / `msquic_transport_smoke`）保持 `stage1` 前端，超时默认 `60s`（`CHENG_FRONTIER_TIMEOUT`）。
- frontier 默认使用 `CHENG_FRONTIER_GENERIC_MODE=dict`、`CHENG_FRONTIER_GENERIC_SPEC_BUDGET=0`（可覆盖），用于抑制重量依赖图的 monomorphize 放大。
- 可通过 `CHENG_FRONTIER_SKIP_SEM` / `CHENG_FRONTIER_SKIP_OWNERSHIP` / `CHENG_FRONTIER_VALIDATE` 控制 stage1 口径（默认 `1/1/0`，用于加速 smoke）。
- frontier 默认开启超时诊断（`CHENG_FRONTIER_TIMEOUT_DIAG=1`）：超时时自动采样并打印诊断路径（默认目录 `chengcache/libp2p_frontier/logs/timeout_diag`，采样时长 `CHENG_FRONTIER_TIMEOUT_DIAG_SECONDS=5`）。
- frontier 默认开启超时摘要（`CHENG_FRONTIER_TIMEOUT_DIAG_SUMMARY=1`，`CHENG_FRONTIER_TIMEOUT_DIAG_SUMMARY_TOP=12`）。
- 日志与 summary 位于 `chengcache/libp2p_frontier/logs/`。
- 运行验收（仅 Linux aarch64 主机）：执行 `hello_puts/return_add/mm_live_balance` smoke。
- 跨仓口径说明：
  - `verify_libp2p_frontier.sh` 默认 `dict`，因为 `hybrid` 在 `mdns_smoke/msquic_transport_smoke` 上易触发 60s 超时，`dict` 更稳定。
  - `cheng-libp2p/scripts/verify.sh` 目前分层为 `stable/full`：`stable` 固定 `dict + emit=obj + 60s` 的 compile-only gate，`full` 仅作为 exe 闭环探针（可能因 dict helper 链接闭合缺口失败）。
  - 长期目标仍是统一到 `dict + exe+run`；在 helper 闭合修复完成前，脚本应真实反映能力边界，不做伪闭环。

运行时 ABI 一致性门禁：
```bash
sh src/tooling/verify_backend_runtime_abi.sh
```
说明：
- 校验 `src/runtime/native/system_helpers.h` 与 C/runtime 后端实现的符号一致性（含 `_addr`/`__addr` 别名兼容）。
- `verify.sh`、`verify_backend_closedloop.sh` 与 CI（Linux amd64 / macOS arm64）会执行此检查。

标准库导入入口门禁：
```bash
sh src/tooling/verify_std_import_surface.sh
```
说明：
- 校验 `src/stage1`、`src/backend`、`src/tooling`、`src/decentralized`、`src/web` 下的 Cheng 源码不再直接导入 `cheng/stdlib/bootstrap/*`。
- 约束统一入口为 `std/*`；`src/stdlib/bootstrap` 已移除，不作为可用 import 路径。
- 同时门禁禁止显式泛型 `reserve[T](...)` 调用；统一使用 `reserve(xs, n)` 或 `xs.cap = n`。

标准库目录同步门禁：
```bash
sh src/tooling/verify_std_layout_sync.sh
```
说明：
- 校验 `src/std` 主目录完整性，并确保 `src/stdlib/bootstrap` 旧路径已移除（迁移收敛门禁）。
- `verify_backend_closedloop.sh` 会执行该门禁。

Stage1 seed 布局门禁：
```bash
sh src/tooling/verify_stage1_seed_layout.sh
```
说明：
- 主 seed 口径为 backend 可执行 seed（优先 `artifacts/backend_seed/cheng.stage2`，也可通过 `CHENG_SELF_OBJ_BOOTSTRAP_STAGE0` 指定）。
- `src/stage1/frontend_bootstrap.seed.c` 必须不存在。
- `verify.sh` 与 `verify_backend_closedloop.sh` 会执行该门禁。
- `verify.sh` 默认导出 `CHENG_BACKEND_PROD_GATE_TIMEOUT=60`、`CHENG_BACKEND_PROD_SELFHOST_TIMEOUT=60`、`CHENG_BACKEND_PROD_TIMEOUT_DIAG=1`；可通过 `CHENG_VERIFY_LIBP2P_FRONTIER=1` 追加 `libp2p frontier` 探针门禁。
- `verify.sh` 默认同时导出 `CHENG_BACKEND_PROD_TIMEOUT_DIAG_SUMMARY=1` 与 `CHENG_FRONTIER_TIMEOUT_DIAG_SUMMARY=1`（超时后自动打印热点摘要）。
- `verify.sh` 轻量 case 默认使用 `CHENG_VERIFY_FRONTEND=mvp`。
- `list_comp` 与 `pkg_import_srcroot` 默认走 `build` 模式（快速 obj 编译校验，不再默认跳过）：
  - `CHENG_VERIFY_LIST_COMP_MODE=build|run|skip`（兼容 `CHENG_VERIFY_LIST_COMP=1` -> `run`）。
  - `CHENG_VERIFY_PKG_IMPORT_MODE=build|run|skip`（兼容 `CHENG_VERIFY_PKG_IMPORT=1` -> `run`）。
- `pkg_import_srcroot` 的 `build` 模式默认使用 `stage1` + `skip_sem/skip_ownership` + `dict`（可通过 `CHENG_VERIFY_PKG_IMPORT_BUILD_*` 覆盖）；`run` 模式默认 `CHENG_VERIFY_PKG_IMPORT_FRONTEND=stage1`。

Cheng skill 一致性门禁：
```bash
sh src/tooling/verify_cheng_skill_consistency.sh
```
说明：
- 扫描 `docs/cheng-skill/` 与 `~/.codex/skills/cheng语言` 的关键文件漂移，并检查禁用语法。
- 默认执行最小编译+运行抽样；可用 `CHENG_SKILL_COMPILE=0` 只做文档检查。
- 编译抽样默认优先复用 `artifacts/backend_selfhost_self_obj/cheng.stage2`（其次 `cheng.stage1`、`artifacts/backend_seed/cheng.stage2`），并使用稳定 `mvp` 前端口径（可用 `CHENG_SKILL_FRONTEND=stage1` 切回 stage1）。
- `verify.sh` 默认执行该门禁；可用 `CHENG_VERIFY_SKILL=0` 临时跳过。

## 包管理闭环

常用脚本：
- `src/tooling/cheng_pkg_pack.sh`：把包目录打成 `.tar.gz` 快照。
- `src/tooling/cheng_pkg_publish.sh`：打包 -> 上传存储 -> 发布 registry 快照。
- `src/tooling/cheng_pkg_fetch.sh`：根据 lock 拉取快照并输出 `CHENG_PKG_ROOTS`。
- `src/tooling/cheng_pkg_source.cheng`：源码清单（manifest）构建/发布/本地拉取（local-only core binary）。

示例：
```bash
sh src/tooling/cheng_pkg_publish.sh --src:/Users/lbcheng/.cheng-packages/cheng-libp2p --package:pkg://cheng/libp2p \
  --author:node:alice --channel:edge --epoch:1 --priv:keys/alice.priv

./cheng_registry resolve --package:pkg://cheng/libp2p --channel:edge --registry:build/cheng_registry/registry.jsonl
./cheng_pkg resolve --manifest:doc/cheng-package-manifest.toml --registry:build/cheng_registry/registry.jsonl \
  --out:build/cheng_pkg/cheng.lock.toml

sh src/tooling/cheng_pkg_fetch.sh --lock:build/cheng_pkg/cheng.lock.toml --print-roots
```

源码直发（no-copy）示例（local-only core binary）：
```bash
sh src/tooling/cheng_pkg_publish.sh --src:/Users/lbcheng/.cheng-packages/cheng-libp2p --package:pkg://cheng/libp2p \
  --author:node:alice --channel:edge --epoch:1 --priv:keys/alice.priv --format:source \
  --source-addr:/ip4/127.0.0.1/tcp/4005

# 当前 core binary 的 `fetch` 仅支持 local store（`--mode:local`）。
sh src/tooling/cheng_pkg_fetch.sh --lock:build/cheng_pkg/cheng.lock.toml --print-roots \
  --source-peer:/ip4/127.0.0.1/tcp/4005
```

## 内存模型开关

以下构建脚本支持 `--mm:<orc>`（兼容 `--orc`），并映射 `CHENG_MM=orc`：

- `src/tooling/build_unimaker_desktop.sh`
- `src/tooling/package_ide.sh`
- `src/tooling/package_unimaker_desktop.sh`
- `src/tooling/build_mobile_export.sh`
- `src/tooling/mobile_ci_android.sh`
- `src/tooling/mobile_ci_ios.sh`

说明：
- `src/tooling/build_mobile_export.sh` 在导出 Android 工程时，会优先使用 `src/tooling/mobile/android/cheng_mobile_host_android.c` 覆盖模板，避免部分设备 `ANativeWindow_lock` 返回非 4BPP buffer 导致的越界写崩溃。

## bootstrap.sh（已退役）

`src/tooling/bootstrap.sh` 已迁移为转发入口：会提示并直接转发到 `bootstrap_pure.sh`。

请统一使用 `bootstrap_pure.sh`：

```bash
src/tooling/bootstrap_pure.sh --fullspec
```

## bootstrap_pure.sh

```bash
src/tooling/bootstrap_pure.sh --fullspec
```

说明：
- 不依赖 C 编译器；走 obj/exe + self-linker 完成 stage2 自举。
  - `--seed:<path>`/`CHENG_SELF_OBJ_BOOTSTRAP_STAGE0` 可显式指定 stage0 driver。
  - 未指定时会按顺序自动探测：`CHENG_BACKEND_DRIVER` -> `artifacts/backend_driver/cheng` -> `dist/releases/current/cheng` -> `artifacts/backend_seed/cheng.stage2` -> `artifacts/backend_selfhost_self_obj/cheng.stage2` -> `artifacts/backend_selfhost_self_obj/cheng.stage1` -> 可运行 `./cheng`；若均不可用，再尝试 `src/tooling/build_backend_driver.sh --name:artifacts/backend_driver/cheng`。
  - 不再支持 `--skip-determinism`。

## cheng_storage（去中心化存储/计算 CLI）

说明：
- 支持存储、租约、计量、执行请求、结算与审计等。
- 计算计量覆盖 CPU/内存/IO/GPU，支持 `--gpu_ms/--gpu_mem_bytes/--gpu_count/--gpu_type/--workload` 等参数。
- 完整示例见 `docs/cheng-decentralized-compute-storage.md`。

示例：
```bash
sh src/tooling/chengc.sh src/tooling/cheng_storage.cheng --frontend:stage1 --emit:exe --out:cheng_storage
# 或：sh src/tooling/chengb.sh src/tooling/cheng_storage.cheng --frontend:stage1 --emit:exe --out:cheng_storage
# chengc 默认输出：./artifacts/chengc/cheng_storage
./cheng_storage exec --task:job-gpu-1 --package:pkg://cheng/fs --author:node:alice --requester:node:app-1 \
  --gpu_ms:180000 --gpu_mem_bytes:17179869184 --gpu_count:1 --gpu_type:A10G --workload:train \
  --price_gpu:0.00002 --price_gpu_mem:0.15 --epoch:1 --root:build/cheng_storage --mode:local
```

补充：
- `chengc.sh`（以及兼容壳 `chengb.sh`）固定 `CHENG_MM=orc`；传入 `--mm:off`/`CHENG_MM=off` 会直接报错。
