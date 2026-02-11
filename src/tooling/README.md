# Cheng 工具链（Backend-only）

当前仓库提供 backend-only 主链路：

1. **`chengc.sh` / `chengb.sh`**：后端 obj/exe 编译入口。
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
- 可选 `--mm:<orc|off>`（或 `--orc/--off`）设置内存模型；默认 `orc`。
- 包管理器接入：可选传入 `--manifest/--lock/--registry`，会生成构建元数据 `chengcache/<name>.buildmeta.toml`。
- 可选 `--pkg-cache:<dir>` 指定包缓存目录（默认 `chengcache/packages`，或 `CHENG_PKG_CACHE` 环境变量）。
- 当 lock 存在时会拉取依赖包并设置 `CHENG_PKG_ROOTS` 供 `cheng/<pkg>/...` 域名导入使用；生产包根以 `src/` 为模块根（`cheng/<pkg>/<path>` -> `<pkgroot>/src/<path>.cheng`）。`CHENG_PKG_ROOTS` 可指向包根列表或容器根（如 `~/.cheng-packages`，解析会优先尝试 `cheng-<pkg>`）。
- 源码直发拉取可通过 `--source-peer`/`--source-listen` 或环境变量 `CHENG_PKG_SOURCE_PEERS`/`CHENG_PKG_SOURCE_LISTEN` 指定源地址。
- 依赖拉取模式/peer：`CHENG_PKG_MODE=local|p2p`/`CHENG_PKG_PEERS`。
- 注册中心元数据：当提供 `--package/--channel` 时，会写入 `[snapshot]`（`cid/author_id/signature/pub_key`）。
- 构建期校验：可加 `--verify`（可选 `--ledger:<path>`）自动校验 buildmeta/lock/pkgmeta/snapshot。
- `chengc.sh` 入口仅保留 `--backend:obj`（可选 `--emit-obj` 仅产出 `.o` 并跳过链接；当前支持 AArch64 + x86_64 + riscv64 目标；非本机/非 Android 目标通常建议只做 obj 产物验证）。默认 `--linker:self`，并默认使用纯 Cheng 运行时：编译 `src/std/system_helpers_backend.cheng` 为 `chengcache/system_helpers_backend.<target>.o` 并链接。可用 `CHENG_BACKEND_RUNTIME_OBJ=<path>` 指定自定义运行时 `.o`。
- Linux AArch64 可选 no-libc profile：`CHENG_BACKEND_ELF_PROFILE=nolibc` 且 `CHENG_BACKEND_LINKER=self` 时，`chengb.sh/chengc.sh` 会切换到 `src/std/system_helpers_backend_nolibc_linux_aarch64.cheng`，并走无 `PT_INTERP`/`PT_DYNAMIC` 的静态链接口径；默认 profile 行为不变。
- 后端 driver 层（`src/backend/tooling/backend_driver.cheng`）：`CHENG_BACKEND_EMIT=obj` 只生成 relocatable `.o`；`CHENG_BACKEND_EMIT=exe` 会先产出中间 `.o` 再按 `CHENG_BACKEND_LINKER=self` 链接可执行文件（`chengb.sh/chengc.sh` 默认会清理同名 sidecar，需保留可设 `CHENG_BACKEND_KEEP_EXE_OBJ=1`）。
- 后端 driver 默认参数：`CHENG_BACKEND_EMIT=obj`、`CHENG_BACKEND_TARGET=auto`；tooling 默认显式传 `CHENG_BACKEND_WHOLE_PROGRAM=1`、`CHENG_BACKEND_MULTI=0`（需要并行时再显式开启 `CHENG_BACKEND_MULTI=1` / `--multi`）。`emit=exe` 路径默认启用对象级增量（`CHENG_BACKEND_INCREMENTAL=1`）。
  - 并行实现：父进程一次建图后按 `jobs` 分桶并行编译单元；优先使用 `fork` worker（`CHENG_BACKEND_MULTI_FORK`，`stage1` 默认开启），否则走子进程批处理（`--unit-batch-file`）。
  - 子进程并行默认启用 module-cache（`.objs/.build.module.cache`；`CHENG_BACKEND_MULTI_MODULE_CACHE`）复用父进程建图，避免每个 worker 重复构建模块导致变慢。
  - 默认会限制过细分桶（`CHENG_BACKEND_MULTI_MIN_BATCH_UNITS`，默认 `8`），避免 worker 过多导致重复前端开销放大。
  - 同一输出目录采用 lock 互斥（`<out>.objs.lock`），避免并发产物互踩。
  - 增量 stamp 使用“单元依赖闭包文件状态 + 编译器身份（默认取 driver 路径/mtime/size）”，减少无关改动重编并避免跨编译器复用旧产物。
  - 依赖闭包 stamp 会预取单元文件状态（mtime/size）并复用，降低大模块增量判定阶段的重复 `stat` 开销。
  - 可执行单对象回退：`emit=exe` + Darwin + self-link 下可设 `CHENG_BACKEND_MULTI_EXE_MAX_OBJS=<N>`（默认 `0` 禁用）在单元数超过阈值时回退“单对象编译+链接”；通常不建议开启（大模块多单元编译在当前实现下更快）。
  - `CHENG_BACKEND_MULTI_LTO=1` 在 `emit=exe + multi` 路径下采用“全模块优化 + 单对象链接”口径（`<out>.objs` 中仅保留 1 个 `.o`），避免跨单元符号裁剪漂移。

Stage1 全语法回归门禁：
```bash
sh src/tooling/verify_stage1_fullspec.sh
```

## 自研后端与全链自举

后端 driver 选择（脚本统一入口）：
```bash
# 默认优先：本地 `./cheng`（fresh + 可运行）
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

后端链接环境助手（脚本统一注入 self-linker 运行时 `.o`）：
```bash
# 输出可直接给 `env` 使用的链接环境变量
sh src/tooling/backend_link_env.sh --driver:./cheng --target:arm64-apple-darwin --linker:self
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
- fullchain 工具构建默认关闭（`CHENG_FULLCHAIN_BUILD_TOOLS=0`）；仅在需要验证 `cheng_pkg_source/cheng_pkg/cheng_storage` 时再显式设置 `CHENG_FULLCHAIN_BUILD_TOOLS=1`。
  - 工具构建要求 `CHENG_MM=orc`，不再自动回退 `CHENG_MM=off`。
  - 启用后按主机核数并行构建（`CHENG_FULLCHAIN_TOOL_JOBS`，`0`=auto）。
  - `verify_backend_selfhost_bootstrap_self_obj.sh` 支持 `CHENG_SELF_OBJ_BOOTSTRAP_TIMEOUT=<seconds>`（默认 60）防止 stage1/stage2 自举编译长时间卡死。
  - `verify_backend_selfhost_bootstrap_self_obj.sh` 默认启用 `CHENG_STAGE1_STD_NO_POINTERS=1` 与 `CHENG_STAGE1_STD_NO_POINTERS_STRICT=1`（显式环境变量可覆盖）。
  - `CHENG_STAGE1_STD_NO_POINTERS=1` 会启用 stage1 的 std no-pointer 门禁；默认采用“分层豁免”口径（低层 runtime/内存模型依赖模块豁免，确保生产链路可闭环）。
  - 可额外设置 `CHENG_STAGE1_STD_NO_POINTERS_STRICT=1` 开启审计口径（禁用新增迁移豁免；当前仍保留 runtime-core 模块豁免，避免破坏自举/发布链路）。
  - stage0 选择顺序：`CHENG_SELF_OBJ_BOOTSTRAP_STAGE0`（显式） -> 可执行 `CHENG_BACKEND_DRIVER` -> `artifacts/backend_selfhost_self_obj/cheng.stage2` -> `artifacts/backend_selfhost_self_obj/cheng.stage1` -> `artifacts/backend_seed/cheng.stage2` -> 可运行 `./cheng`（缺失或不可运行时才重建）。seed 仅用于“首次/无自举产物”的兜底。
  - 自举模式：`CHENG_SELF_OBJ_BOOTSTRAP_MODE=strict|fast`（默认 `strict`）。`strict` 默认校验 `stage1->stage2` 固定点；若首次不收敛，会自动追加 `stage3` 并以 `stage2->stage3` 固定点作为收敛门禁。`fast` 只编译 stage1 并同步为 stage2（开发加速，跳过 fixed-point 校验）。
  - 自举编译默认开启多单元增量：`CHENG_SELF_OBJ_BOOTSTRAP_MULTI=1`、`CHENG_SELF_OBJ_BOOTSTRAP_INCREMENTAL=1`、`CHENG_SELF_OBJ_BOOTSTRAP_MULTI_FORCE=1`；`CHENG_SELF_OBJ_BOOTSTRAP_JOBS` 默认 `0`（auto，按主机核数并行）。
  - 自举临时产物默认使用稳定 session（`CHENG_SELF_OBJ_BOOTSTRAP_SESSION=default`），可复用 `<out>_tmp_<session>.objs` 缓存；并行多任务可显式设置不同 session 避免互相覆盖。
  - 执行结束会输出 `backend.selfhost_self_obj.timing`（lock/stage1/stage2/smoke/total 秒数），用于快速定位慢阶段。
  - 可在问题排查时显式关闭：`CHENG_SELF_OBJ_BOOTSTRAP_MULTI=0 CHENG_SELF_OBJ_BOOTSTRAP_INCREMENTAL=0`。
  - 2026-02-08 同机冷态实测：`fast` 约 `17s`，`strict` 约 `24-27s`；`backend_prod_closure.sh --only-self-obj-bootstrap --selfhost-strict` 在 `60s` 约束内通过。
  - 2026-02-06：已修复一类“无输出超时”根因（`src/std/strings.cheng` 的 `streq` 对 `nil` 比较递归调用自身）；该问题会让 selfhost 阶段卡住直到超时。
  - 修复后若 selfhost 失败，将优先以可诊断错误退出（而非长时间超时卡住）。
- `CHENG_FULLCHAIN_OBJ_ONLY=1` 默认使用 `examples/backend_obj_fullspec.cheng` 作为 obj-only 门禁样例（要求输出 `fullspec ok`）；可用 `CHENG_FULLCHAIN_OBJ_FULLSPEC_FILE=<path>` 覆盖。

CI/生产（macOS arm64）使用 dist seed 跑“后端自举 + fullchain smoke”：
```bash
sh src/tooling/verify_backend_ci_obj_only.sh
```
说明：
- 默认从 `dist/releases/current_id.txt` 解析 seed；也可传 `--seed:<path>` 指定。
- 若 dist seed 不存在，会直接失败并提示补充 seed（可用 `--require-seed` 强制严格模式）。
- 不会自动使用 `artifacts/backend_seed/cheng.stage2` 或 `artifacts/backend_selfhost_self_obj/cheng.stage2`。

后端生产闭环（默认包含全链；可跳过）：
```bash
sh src/tooling/backend_prod_closure.sh
sh src/tooling/backend_prod_closure.sh --no-fullchain
```
说明：
- `backend_prod_closure.sh` 默认以 `CHENG_FULLCHAIN_OBJ_ONLY=1` 跑 fullchain。
- `backend_prod_closure.sh` 默认 strict：任一步骤 `exit 2`（skip）会直接失败；仅本地排障时再显式加 `--allow-skip`。
- `backend_prod_closure.sh` 默认启用 `CHENG_STAGE1_STD_NO_POINTERS=1` 与 `CHENG_STAGE1_STD_NO_POINTERS_STRICT=1`（显式设置环境变量可覆盖）。
- `backend_prod_closure.sh` 默认将自举阶段单次编译超时设为 `60s`（`CHENG_BACKEND_PROD_SELFHOST_TIMEOUT`），用于及早暴露性能回退。
- `backend_prod_closure.sh` 默认自举模式为 `strict`；可用 `CHENG_BACKEND_PROD_SELFHOST_MODE=fast` 临时切到单阶段自举加速本地迭代（发布/CI 建议保持 strict）。
- `backend_prod_closure.sh` 提供显式参数 `--selfhost-fast` / `--selfhost-strict`（优先级高于环境变量）。
- 若已显式设置 `CHENG_BACKEND_DRIVER` 且可执行，`backend_prod_closure.sh` 会优先将其作为 selfhost stage0，避免额外重建本地 `./cheng`。
- 若未显式提供 stage0，`backend_prod_closure.sh` 会优先复用 `artifacts/backend_selfhost_self_obj/cheng.stage2`，其次 `artifacts/backend_seed/cheng.stage2`，再其次 `cheng.stage1` 作为 selfhost stage0。
- `backend_prod_closure.sh` 默认固定 selfhost 复用口径：`CHENG_BACKEND_PROD_SELFHOST_REUSE=1`、`CHENG_BACKEND_PROD_SELFHOST_SESSION=prod`（可覆盖），减少重复冷编译与并发互踩。
- `verify_backend_selfhost_bootstrap_self_obj.sh` 会把 stage0 driver 固化拷贝到 `artifacts/backend_selfhost_self_obj/cheng_stage0_<session>` 后再执行，且仅在 stage0 **内容变化**时更新该副本，避免因时间戳漂移触发无效重编。
- `verify_backend_selfhost_bootstrap_self_obj.sh` 在 `CHENG_SELF_OBJ_BOOTSTRAP_REUSE=1` 下会复用 smoke 产物（`hello_puts`）并仅在依赖变化时重编，降低重复验收耗时。
- `backend_prod_closure.sh` 对 `backend.coff_lld_link` 做宿主能力探测：仅当存在 `lld-link/llvm-lld/ld.lld/lld` 时执行；缺失时打印 skip，不触发 strict 失败。
- `backend_prod_closure.sh` 结束时会输出 `backend_prod_closure.timing_top`（按耗时降序的 gate top 列表），用于持续压缩闭环耗时。
- 生产/CI 可用 `--require-seed` 禁止使用本机 stage0；`--require-seed` 需要显式传 `--seed/--seed-id/--seed-tar`。
- 发布（默认开启）要求显式 seed：需传 `--seed/--seed-id/--seed-tar`；仅做本地闭环可加 `--no-publish`。
- `scripts/closedloop.sh` 在 `CHENG_CLOSEDLOOP_PROD=1` 下默认以 `--no-publish` 运行 `backend_prod_closure.sh`；若需要发布门禁，请显式设置 `CHENG_CLOSEDLOOP_BACKEND_PROD_ARGS` 传入 seed 参数。
- `build_backend_driver.sh` 自举重建默认采用串行增量（`CHENG_BACKEND_BUILD_DRIVER_MULTI=0`）；可用 `CHENG_BACKEND_BUILD_DRIVER_MULTI` / `CHENG_BACKEND_BUILD_DRIVER_INCREMENTAL` / `CHENG_BACKEND_BUILD_DRIVER_JOBS` 覆盖。
- 在 `CHENG_MM=orc CHENG_BACKEND_LINKER=self` 口径下，`opt/opt2/multi-lto/ssa/debug/stress` gate 已统一固定串行口径（`CHENG_BACKEND_MULTI=0`、`CHENG_BACKEND_MULTI_FORCE=0`、`CHENG_BACKEND_WHOLE_PROGRAM=1`）以保证稳定可复现。
- `verify_backend_concurrency_stress.sh` 默认跳过（`CHENG_BACKEND_CONCURRENCY_STRESS_ENABLED=0`）；需要并发压力回归时显式设置 `CHENG_BACKEND_CONCURRENCY_STRESS_ENABLED=1`。
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
- `sample` 想看到完整调用栈与符号名：
  - 需要后端生成“可 unwind 的栈帧”（FP 链）+ 可执行文件包含符号表；
  - self-linker 口径建议用 `CHENG_BACKEND_LINKER_SYMTAB=all`（或 `--linker-symtab=all`）重建目标二进制，否则常见现象是 call graph 大量 `???`。
- `CHENG_BACKEND_PROFILE=1` 可启用后端内建分段计时：编译时在 stderr 输出 `backend_profile\t<label>\tstep_ms=...\ttotal_ms=...`，用于补足 sample 无符号时的慢阶段定位。
- `CHENG_STAGE1_PROFILE=1` 可启用 stage1 前端分段计时：在 stdout 输出 `[stage1] profile: lex=... load=... sem=... mono=... ownership=... total=...`，并附带 `[sem]/[mono]/[ownership]` 的细分统计，用于把 `backend_profile build_module` 拆到更具体阶段。
- `CHENG_MIR_PROFILE=1` 可启用 MIR 构建（`mirBuildModuleFromRoot`）内部分段计时：在 stderr 输出 `mir_profile\t<label>\tstep_ms=...\ttotal_ms=...`，用于把 `backend_profile build_module` 进一步拆解到 `lower_top_level/fixups/...`。
- `sample` 若显示热点集中在 `strlen` 与 `get/hashMap`，优先检查 `src/std/strings.cheng`、`src/std/seqs.cheng`、`src/std/hashmaps.cheng` 以及 `src/backend/tooling/backend_driver.cheng`、`src/backend/mir/mir_builder.cheng` 的字符串判空冷路径实现（当前已落地 `streq` 的 `c_strcmp` 快路径、`hashMapStrEqKnownLen` 的 `cmpMem+NUL` 比较，以及 `driver_strIsEmpty/mirStrIsEmpty` 判空快路径）。
- 2026-02-07 同口径 20s 采样：`_platform_strlen` top-stack 计数约 `6555 -> 5990`。
- 2026-02-08：修复 stage1 `loadTokenCache` 热路径反复 `strlen`（`sliceRangePlainKnownLen`），同口径下 `_platform_strlen` 不再主导 top-stack。
- 2026-02-08：Mach-O obj writer 将 `machoFindSymIndex` 由线性扫描改为 hash 索引，单模块 `emit=obj` 的 `single.emit_obj` 常见从 `~12s` 降到 `~3.6s`（以 `backend_driver.cheng` 编译为例）。
- 2026-02-08：`ptr_add` 改为 MIR intrinsic + 运行时 ptrmap 减少 `ptr_add` 调用后，selfhost strict 冷态 `total≈24-27s`（stage1≈11-13s，stage2≈12-13s；`CHENG_SELF_OBJ_BOOTSTRAP_REUSE=0`）。
- 2026-02-08：byte 指针 load/store（`bool/char/i8/u8`）由 runtime helper（`load_bool/store_bool + memcpy`）改为 MIR 直接 `load/store`（带 `nil` 分支），减少 `_platform_memmove` 热度并降低编译时开销。
- 2026-02-08：运行时 ORC ptrmap 优化：默认 init cap 提升到 `65536`，ptr hash 简化并在 grow rehash 中 hoist `mask`，降低 `cheng_ptrmap_grow/cheng_ptr_hash` 热路径开销。
- 2026-02-08：`backendStripSpaces` 增加“无空白直接返回”快路径（避免大量 `strip` 分配），`backend_driver.cheng` 单模块 `emit=obj` 口径下 `build_module` 常见 `~11.3s -> ~10.4s`，端到端 `~14.4s -> ~13.3s`（以 `CHENG_BACKEND_PROFILE/CHENG_STAGE1_PROFILE/CHENG_MIR_PROFILE` 输出为准）。
- 2026-02-09：修复 `profile_backend_sample` 的 attach 等待策略：当命中 root 自身或目标不 fork 子进程时不再长时间等待，避免 target 结束后采到 zombie 导致 `sample` 失败（exit=255）。
- 2026-02-09：`src/std/system_helpers_backend.cheng` 的 `cheng_ptr_hash` 忽略对齐位并减少一次乘法，降低 `cheng_ptr_hash/cheng_ptrmap_put` 热路径开销。
- 2026-02-09：`src/backend/mir/mir_types.cheng` 为 `typeAliases/objTypes/cstrs` 引入 `HashMapStrInt` 索引缓存（>=32 时启用），减少 `streq` 线性扫描；同机 `backend_driver.cheng` 单模块 `emit=obj` 常见 `~13.5s -> ~13.0s`（以 `/usr/bin/time` 为准）。

后端闭环验收（统一 ORC + self linker）：
```bash
CHENG_MM=orc CHENG_BACKEND_LINKER=self sh src/tooling/verify_backend_closedloop.sh
CHENG_MM=orc CHENG_BACKEND_LINKER=self CHENG_BACKEND_RUN_FULLSPEC=1 sh src/tooling/verify_backend_closedloop.sh
CHENG_MM=orc sh src/tooling/verify_backend_self_linker_riscv64.sh
```
说明：
- `verify_backend_closedloop.sh` 默认 `CHENG_BACKEND_RUN_FULLSPEC=0`（不跑 fullspec）；显式设为 `1` 才开启 fullspec 编译+运行门禁。
- 当前 `CHENG_BACKEND_RUN_FULLSPEC=1` 仍属于追踪口径（已知可能出现 `mir_builder: main not found`）；默认生产闭环口径不依赖该步骤。
- `verify_backend_mm.sh` 默认只跑 `mm_live_balance`；容器回归 `mm_container_balance` 需显式 `CHENG_BACKEND_MM_CONTAINER=1`。
- `verify_backend_self_linker_riscv64.sh` 默认口径为 `CHENG_MM=orc`，并优先复用 `artifacts/backend_selfhost_self_obj/cheng.stage2`（若存在）以避免 gate 内重建 driver 触发超时。

Linux AArch64 no-libc 独立验收（不接入默认 verify）：
```bash
CHENG_BACKEND_ELF_PROFILE=nolibc sh src/tooling/verify_backend_nolibc_linux_aarch64.sh
```
说明：
- 静态验收（macOS 可跑）：检查 `elf64-littleaarch64`、无 `PT_INTERP`/`PT_DYNAMIC`、无 undefined symbols、无 `libc.so/ld-linux-aarch64` 字符串。
- 运行验收（仅 Linux aarch64 主机）：执行 `hello_puts/return_add/mm_live_balance` smoke。

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

Cheng skill 一致性门禁：
```bash
sh src/tooling/verify_cheng_skill_consistency.sh
```
说明：
- 扫描 `docs/cheng-skill/` 与 `~/.codex/skills/cheng语言` 的关键文件漂移，并检查禁用语法。
- 默认执行最小编译+运行抽样；可用 `CHENG_SKILL_COMPILE=0` 只做文档检查。
- `verify.sh` 默认执行该门禁；可用 `CHENG_VERIFY_SKILL=0` 临时跳过。

## 包管理闭环

常用脚本：
- `src/tooling/cheng_pkg_pack.sh`：把包目录打成 `.tar.gz` 快照。
- `src/tooling/cheng_pkg_publish.sh`：打包 -> 上传存储 -> 发布 registry 快照。
- `src/tooling/cheng_pkg_fetch.sh`：根据 lock 拉取快照并输出 `CHENG_PKG_ROOTS`。
- `src/tooling/cheng_pkg_source.cheng`：源码清单（manifest）构建/发布/只读服务/拉取。

示例：
```bash
sh src/tooling/cheng_pkg_publish.sh --src:/Users/lbcheng/.cheng-packages/cheng-libp2p --package:pkg://cheng/libp2p \
  --author:node:alice --channel:edge --epoch:1 --priv:keys/alice.priv

./cheng_registry resolve --package:pkg://cheng/libp2p --channel:edge --registry:build/cheng_registry/registry.jsonl
./cheng_pkg resolve --manifest:doc/cheng-package-manifest.toml --registry:build/cheng_registry/registry.jsonl \
  --out:build/cheng_pkg/cheng.lock.toml

sh src/tooling/cheng_pkg_fetch.sh --lock:build/cheng_pkg/cheng.lock.toml --print-roots
```

源码直发（no-copy）示例：
```bash
sh src/tooling/cheng_pkg_publish.sh --src:/Users/lbcheng/.cheng-packages/cheng-libp2p --package:pkg://cheng/libp2p \
  --author:node:alice --channel:edge --epoch:1 --priv:keys/alice.priv --format:source \
  --source-addr:/ip4/127.0.0.1/tcp/4005

# 源码只读服务
./cheng_pkg_source serve --src:/Users/lbcheng/.cheng-packages/cheng-libp2p --manifest:<manifest-cid> \
  --listen:/ip4/127.0.0.1/tcp/4005

sh src/tooling/cheng_pkg_fetch.sh --lock:build/cheng_pkg/cheng.lock.toml --print-roots \
  --source-peer:/ip4/127.0.0.1/tcp/4005
```

## 内存模型开关

以下构建脚本支持 `--mm:<orc|off>`（兼容 `--orc/--off`），并映射 `CHENG_MM`：

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
  - 未指定时会按顺序自动探测：`CHENG_BACKEND_DRIVER` -> `artifacts/backend_seed/cheng.stage2` -> `artifacts/backend_selfhost_self_obj/cheng.stage2` -> `artifacts/backend_selfhost_self_obj/cheng.stage1` -> 可运行 `./cheng`；若均不可用，再尝试 `src/tooling/build_backend_driver.sh --name:cheng`。
  - 不再支持 `--skip-determinism`。

## cheng_storage（去中心化存储/计算 CLI）

说明：
- 支持存储、租约、计量、执行请求、结算与审计等。
- 计算计量覆盖 CPU/内存/IO/GPU，支持 `--gpu_ms/--gpu_mem_bytes/--gpu_count/--gpu_type/--workload` 等参数。
- 完整示例见 `docs/cheng-decentralized-compute-storage.md`。

示例：
```bash
sh src/tooling/chengb.sh src/tooling/cheng_storage.cheng --frontend:stage1 --emit:exe --out:cheng_storage
# 或：src/tooling/chengc.sh src/tooling/cheng_storage.cheng --backend:obj --name:cheng_storage
./cheng_storage exec --task:job-gpu-1 --package:pkg://cheng/fs --author:node:alice --requester:node:app-1 \
  --gpu_ms:180000 --gpu_mem_bytes:17179869184 --gpu_count:1 --gpu_type:A10G --workload:train \
  --price_gpu:0.00002 --price_gpu_mem:0.15 --epoch:1 --root:build/cheng_storage --mode:local
```

补充：
- `chengb.sh`/`chengc.sh` 默认 `CHENG_MM=orc`；仅排障时再显式传 `--off` 或设置 `CHENG_MM=off`。
