# Cheng 工具链（Backend-only）

当前仓库提供 backend-only 主链路：

1. **`chengc.sh`**：后端 `emit=exe` 主入口；`chengb.sh` 仅保留兼容转发壳。
2. **并行与增量**：tooling 入口默认 `BACKEND_MULTI=0`（稳定口径）与 `BACKEND_INCREMENTAL=1`。
3. **完全自举**：`emit=exe + self-link` 自举（见 `bootstrap_pure.sh`）。
4. **转发入口**：`bootstrap.sh` 转发到 `bootstrap_pure.sh`。

## cheng_tooling.sh（统一脚本可执行入口）

```bash
# 列出所有 src/tooling/*.sh 可运行脚本 ID
sh src/tooling/cheng_tooling.sh list

# 按脚本 ID 运行（支持参数透传）
sh src/tooling/cheng_tooling.sh run backend_prod_closure --help

# 简写：省略 run
sh src/tooling/cheng_tooling.sh backend_prod_closure --help
```

说明：
- `src/tooling/cheng_tooling.sh` 会自动构建并复用 `artifacts/tooling_cmd/cheng_tooling`（源码：`src/tooling/cheng_tooling.cheng`）。
- 可通过 `TOOLING_BIN` 覆盖可执行路径，通过 `TOOLING_FORCE_BUILD=1` 强制重建。
- 所有 `src/tooling/*.sh` 可通过该可执行入口统一调用，便于后续逐步收敛到纯 cmdline 工具链。
- 回归脚本：`sh src/tooling/verify_tooling_cmdline.sh`（报告：`artifacts/tooling_cmdline/tooling_cmdline.report.txt`）。

## chengc.sh

```bash
sh src/tooling/chengc.sh examples/stage1_codegen_fullspec.cheng --jobs:8
./stage1_codegen_fullspec
```

说明：
- 可通过 `--name:<exeName>` 改输出二进制名。
- 可选 `--mm:<orc>`（或 `--orc`）显式设置内存模型；默认并固定 `orc`。
- 可选 `--abi:<v2_noptr>`（等价于设置 `ABI`）；默认 `v2_noptr`。
- 包管理器接入：可选传入 `--manifest/--lock/--registry`，会生成构建元数据 `chengcache/<name>.buildmeta.toml`。
- 可选 `--pkg-cache:<dir>` 指定包缓存目录（默认 `chengcache/packages`，或 `PKG_CACHE` 环境变量）。
- 当 lock 存在时会拉取依赖包并设置 `PKG_ROOTS` 供 `cheng/<pkg>/...` 域名导入使用；生产包根以 `src/` 为模块根（`cheng/<pkg>/<path>` -> `<pkgroot>/src/<path>.cheng`）。`PKG_ROOTS` 可指向包根列表或容器根（如 `~/.cheng-packages`，解析会优先尝试 `cheng-<pkg>`）。
- 源码直发拉取可通过 `--source-peer`/`--source-listen` 或环境变量 `PKG_SOURCE_PEERS`/`PKG_SOURCE_LISTEN` 指定源地址。
- 依赖拉取模式/peer：`PKG_MODE=local|p2p`/`PKG_PEERS`。
- 注册中心元数据：当提供 `--package/--channel` 时，会写入 `[snapshot]`（`cid/author_id/signature/pub_key`）。
- 构建期校验：可加 `--verify`（可选 `--ledger:<path>`）自动校验 buildmeta/lock/pkgmeta/snapshot。
- `chengc.sh` 入口仅保留 `emit=exe`；`--emit-obj/--obj-out/--backend:obj` 已在生产链路移除并会直接报错。新增 `--release`（等价 `BACKEND_BUILD_TRACK=release`），默认走 system linker；默认（无 `--release`）为 `dev` 轨，支持 self-link 的目标优先 `--linker:self`，否则回退 `system`。linker 选择优先级固定为：`--linker` > `BACKEND_LINKER` > `BACKEND_BUILD_TRACK` 默认策略。可用 `BACKEND_RUNTIME_OBJ=<path>` 指定自定义运行时对象。
- 运行入口双模：`--run` 默认 `host runner`（Dev），可显式用 `--run:host`；`--run:file` 保留“先产 exe 再执行”的兼容路径。host runner 关键变量：`BACKEND_HOTPATCH_MODE=trampoline`、`BACKEND_HOSTRUNNER_POOL_MB`（默认 `512`）、`BACKEND_HOSTRUNNER_PAGE_POLICY=rw_rx`、`BACKEND_HOTPATCH_LAYOUT_HASH_MODE=full_program`、`BACKEND_HOTPATCH_ON_LAYOUT_CHANGE=restart`、`BACKEND_HOTPATCH_TARGET_PLATFORMS=darwin,linux`。
- system linker 自动选择器：`src/tooling/resolve_system_linker.sh` 按 `BACKEND_SYSTEM_LINKER_PRIORITY`（默认 `mold,lld,default`）解析并追加 `-fuse-ld=...`；若显式设置 `BACKEND_LD` 或手写 `-fuse-ld=...`，选择器不覆盖。
- Linux AArch64 可选 no-libc profile：`BACKEND_ELF_PROFILE=nolibc` 且 `BACKEND_LINKER=self` 时，`chengc.sh`（以及兼容壳 `chengb.sh`）会切换到 `src/std/system_helpers_backend_nolibc_linux_aarch64.cheng`，并走无 `PT_INTERP`/`PT_DYNAMIC` 的静态链接口径；默认 profile 行为不变。
- 后端 driver 层（`src/backend/tooling/backend_driver.cheng`）：生产默认 `BACKEND_EMIT=exe`，`BACKEND_LINK_OBJS` 与 `emit=obj` 旧入口已移除；`emit=obj` 仅允许 internal gate 显式 `BACKEND_INTERNAL_ALLOW_EMIT_OBJ=1` 时使用；`BACKEND_LINKER=system|self` 负责最终可执行物链接。
- 后端 driver 默认参数：`BACKEND_EMIT=exe`、`BACKEND_TARGET=auto`；tooling 在稳定性敏感 gate 默认固定 `BACKEND_MULTI=0`。
- `src/tooling/backend_driver_exec.sh` 仅作为 fallback wrapper（显式 `BACKEND_DRIVER_PATH_USE_WRAPPER=1` 时启用）；默认生产主路径直接使用 `artifacts/backend_driver/cheng`。
- 后端 IR 入口：默认 `BACKEND_IR=uir`（仅支持 `uir`）；自举/构建脚本会默认导出 `BACKEND_IR=uir`、`GENERIC_SPEC_BUDGET=0`。其中 fast 自举口径默认 `GENERIC_MODE=dict`（strict 可显式设 `hybrid`）。
  - MIR 借用/泛型策略开关：`BORROW_IR=mir|stage1`（默认 `mir`）与 `GENERIC_LOWERING=mir_hybrid|mir_dict`（默认 `mir_hybrid`）。
- 后端优化默认：`BACKEND_OPT_LEVEL` 未设置时默认 `2`（若显式设置 `BACKEND_OPT=1` 则仍按 `1`）。
- SIMD 默认策略：`UIR_SIMD` 未设置时按优化级自动选择（`optLevel>=3` 开启，否则关闭）；可用 `UIR_SIMD=0|1` 强制覆盖。
- UIR 调优：`UIR_AGGRESSIVE=1` 打开激进闭环 pass（可结合 `UIR_FULL_ITERS` 控制轮次），`UIR_OPT2_ITERS`/`UIR_OPT3_ITERS`/`UIR_OPT3_CLEANUP_ITERS` 与 `UIR_CFG_CANON_ITERS` 可独立调高 `opt2/opt3` 与 CFG 收敛轮次（默认 `5/4/3/1`，范围分别为 `1..32/1..32/1..32/1..16`），`UIR_PROFILE=1` 打印每一轮 `uir_profile` 计时。
- `verify_backend_opt2.sh`/`verify_backend_opt3.sh` 支持额外优化环境与 fixture 透传：`UIR_SIMD=1`、`UIR_SIMD_MAX_WIDTH=<N>`、`UIR_SIMD_POLICY=<autovec|copy|loop|slp|none>`（默认 `autovec`）、`UIR_INLINE_ITERS`（默认 `4`）、`UIR_OPT2_FIXTURES`、`UIR_OPT3_FIXTURES`（均支持换行或分号分隔 fixture 列表）。
- 新增 SIMD 阻断门禁：`sh src/tooling/verify_backend_simd.sh`（校验 `simd on/off` 语义一致 + `simd on` 双次可执行运行结果一致）。
- 新增生产引用护栏：`sh src/tooling/verify_backend_no_legacy_refs.sh`（阻断生产入口直接依赖 legacy import / 旧 env）。
  - machine/UIR 收敛补充：该门禁额外阻断 internal 与 non-internal 的 `Mir/Lir`、`lr/lc/lo` 旧命名回退，以及 `obj/select_internal` 直接导入 `machine_internal`，要求走 `machine_types` + `machineReg/machineCond/machineOp` surface。
  - 生产闭环可改用 `sh src/tooling/backend_prod_closure.sh --uir-aggressive --uir-aggressive-iters:<n>` 覆盖 `BACKEND_PROD_UIR_FULL_ITERS`（默认 `2`，范围 `1..16`）。
  - 生产闭环可加 `--uir-stability`，触发 `backend.uir_stability` 闸口；对应脚本 `sh src/tooling/verify_backend_uir_stability.sh`。
  - `BACKEND_PROD_TARGET=<triple>` 可固定 `backend.opt/opt2/opt3/simd/uir_stability` 的目标三元组；未设置时会忽略外部非 darwin `BACKEND_TARGET` 并回退到 `arm64-apple-darwin`。
  - 也可直接改写 `opt2/opt3` 轮次：`--uir-opt2-iters:<n>`、`--uir-opt3-iters:<n>`、`--uir-opt3-cleanup-iters:<n>`、`--uir-cfg-canon-iters:<n>`，以及 SIMD/内联：`--uir-simd`（或 `--no-uir-simd`）、`--uir-simd-max-width:<n>`、`--uir-simd-policy:<policy>`、`--uir-inline-iters:<n>`。
- UIR 观测：`UIR_PROFILE=1` 输出 `uir_profile\t<label>\tstep_ms=...\ttotal_ms=...`，并在 `build_module` 后输出 `generics_report\tir=uir\tmode=...\tspec_budget=...\tborrow_ir=...\tgeneric_lowering=...`。
- PAR-01 观测基线：`sh src/tooling/build_backend_profile_schema.sh` 与 `sh src/tooling/build_backend_profile_baseline.sh` 生成冻结快照（默认写入 `src/tooling/backend_profile_schema.env` 与 `src/tooling/backend_profile_baseline.env`）；`sh src/tooling/verify_backend_profile_schema.sh` 与 `sh src/tooling/verify_backend_profile_baseline.sh` 校验 schema/baseline 漂移并输出 `artifacts/backend_profile/*.report.txt`。
- PAR-01 契约冻结：`sh src/tooling/build_backend_mem_contract.sh` 生成 `src/tooling/backend_mem_contract.env`；`sh src/tooling/verify_backend_mem_contract.sh` 校验 `docs/backend-mem-hotpatch-contract.md`（MemImage/PatchMeta）与闭环接入点漂移，输出 `artifacts/backend_mem_contract/*.report.txt`。
- DOPAR-01 DOD 契约冻结：`sh src/tooling/build_backend_dod_contract.sh` 生成 `src/tooling/backend_dod_contract.env`；`sh src/tooling/verify_backend_dod_contract.sh` 校验 `docs/cheng-plan-full.md`（DOD SoA/Arena/index 契约）与闭环接入点漂移，输出 `artifacts/backend_dod_contract/*.report.txt`。
- RPSPAR-01 Raw Pointer Safety 契约冻结：`sh src/tooling/build_backend_rawptr_contract.sh` 生成 `src/tooling/backend_rawptr_contract.env`；`sh src/tooling/verify_backend_rawptr_contract.sh` 校验 `docs/raw-pointer-safety.md` + `docs/cheng-formal-spec.md` + `src/tooling/README.md` 的冻结契约与闭环接入点漂移，输出 `artifacts/backend_rawptr_contract/*.report.txt`。
  - `rawptr_contract.tooling_readme.synced=1`
- RPSPAR-02 Raw Pointer Surface 禁令：`sh src/tooling/verify_backend_rawptr_surface_forbid.sh` 校验“裸指针声明/指针运算/裸 `void*` 透出”三类负例必须失败，且诊断必须包含 `slice/tuple/handle/borrow` 替代建议；输出 `artifacts/backend_rawptr_surface_forbid/*.report.txt`。
- RPSPAR-03 Slice 影子桥接：`sh src/tooling/verify_backend_ffi_slice_shim.sh` 校验 `importc` 形参 `T[]` 的桥接调用可编译（默认 compile-only；可设 `BACKEND_FFI_SLICE_SHIM_RUN=1` 开启运行）并覆盖 legacy `openArray[T]` 与用户层裸指针 surface 负例；输出 `artifacts/backend_ffi_slice_shim/backend_ffi_slice_shim.<target>.report.txt`。
- RPSPAR-04 Out-Ptr 影子桥接：`sh src/tooling/verify_backend_ffi_outptr_tuple.sh` 校验 `@ffi_out_ptrs + @importc` 的 tuple wrapper 降级（运行态正例 + status obj-only 正例 + arity 负例诊断）；输出 `artifacts/backend_ffi_outptr_tuple/*.report.txt`。
- RPSPAR-05 Handle 沙盒映射：`sh src/tooling/verify_backend_ffi_handle_sandbox.sh` 校验 runtime `ptr<->slot` 映射（C runtime + backend runtime 双实现）与过期 handle fail-safe（返回 `-1`，无 UAF），并输出 `artifacts/backend_ffi_handle_sandbox/*.report.txt`。
- RPSPAR-06 Borrow Struct* 桥接：`sh src/tooling/verify_backend_ffi_borrow_bridge.sh` 校验 `importc + var object` 正向桥接可运行，并校验产物符号包含 `_cheng_abi_borrow_mut_pair_i32`（默认固定 `system + runtime C` 口径以避免自链接 runtime 缺符号）；输出 `artifacts/backend_ffi_borrow_bridge/*.report.txt`。
- PAR-02 MemImage 核心：`sh src/tooling/verify_backend_mem_image_core.sh` 校验符号索引、段布局、重定位应用三类核心 marker，并执行 `self-link` 可执行产物探针（无 sidecar `.o/.objs` 残留）；输出 `artifacts/backend_mem_image_core/*.report.txt`。默认仅编译探针（`BACKEND_MEM_IMAGE_CORE_RUN=0`），可设 `BACKEND_MEM_IMAGE_CORE_RUN=1` 启用本机运行校验；可用 `BACKEND_MEM_IMAGE_CORE_DRIVER=<path>` 指定 gate driver。
- PAR-03 直出 EXE：`sh src/tooling/verify_backend_mem_exe_emit.sh` 校验格式写出（Mach-O/ELF/PE）、runtime 合并（`BACKEND_RUNTIME_OBJ` 负例/正例）与原子写盘（`tmp+rename`），并强制 sidecar 残留为 0；输出 `artifacts/backend_mem_exe_emit/backend_mem_exe_emit.<target>.report.txt`。
- PAR-04 Hotpatch 元数据：`sh src/tooling/verify_backend_hotpatch_meta.sh` 校验 `thunk map`、`patch_meta(schema=2)`（`thunk_id/target_slot_fileoff_or_memoff/code_pool_offset/layout_hash/commit_epoch`）与缺元数据/坏布局哈希/坏 ABI 负例；输出 `artifacts/backend_hotpatch_meta/backend_hotpatch_meta.<target>.report.txt`。
- PAR-05 热补丁事务：`sh src/tooling/verify_backend_hotpatch_inplace.sh` 校验 `trampoline + append-only + host runner` 事务语义（append 提交、layout-change 重启、编译失败保活、host pid 稳定）并输出 `artifacts/backend_hotpatch_inplace/backend_hotpatch_inplace.<target>.report.txt`。
- PAR-06 增量快路径：`sh src/tooling/verify_backend_incr_patch_fastpath.sh` 校验 dirty function 检测、最小编译计划与 `patch apply` 延迟收益（`inplace_apply_ms < full_build_ms`），并输出 `artifacts/backend_incr_patch_fastpath/backend_incr_patch_fastpath.<target>.report.txt`。
- PAR-07 回归门禁：`sh src/tooling/verify_backend_mem_patch_regression.sh` 校验 `determinism -> latency -> rollback -> crash-safe` 全链路，并输出 `artifacts/backend_mem_patch_regression/backend_mem_patch_regression.<target>.report.txt`。
- PAR-08 生产收口：`sh src/tooling/backend_prod_closure.sh --no-publish` 执行 required gates 收口并落盘 `artifacts/backend_prod/release_manifest.json` 与 `artifacts/backend_prod/backend_release.tar.gz`。
- PAR-05 兼容执行门禁：`sh src/tooling/verify_backend_hotpatch.sh` 持续覆盖 host-runner E2E（v1->v2 append 生效、编译失败保活、max-growth/layout-change 触发受控重启），并输出 `artifacts/backend_hotpatch/hotpatch.<target>.report.txt`。
- PAR-03 DOD/SoA：`sh src/tooling/verify_backend_dod_soa.sh` 校验 `stage1(nodeArena) + uir(int32 index)` 结构标记、`stage1/backend/uir` profile 事件与吞吐门禁（相对 `src/tooling/backend_dod_soa_baseline.env` 基线提升），并生成 `artifacts/backend_dod_soa/*.report.txt`。
- PAR-06 Release System-Link：`sh src/tooling/verify_backend_release_c_o3_lto.sh`（兼容脚本名）以 required 口径校验 release `system-link + O3/LTO`：强制 `BACKEND_LINKER=system`、`BACKEND_NO_RUNTIME_C=0`、`mold->lld->default` 解析，并输出 `artifacts/backend_release_c_o3_lto/backend_release_c_o3_lto.report.txt`（`gate=backend.release_system_link`）。
  - 并行实现：`emit=exe + BACKEND_MULTI=1` 时按单元编译并链接；优先使用 `fork` worker（`BACKEND_MULTI_FORK`，`stage1` 默认开启），不可用时回退到串行单元编译。
  - 单单元 fast-path：`emit=exe + multi` 仅在单元数 `>1` 时启用 `.objs` 多单元路径；单单元输入会走 `single.emit_obj + single.link`（避免无收益拆分与额外链接复杂度）。
  - module-cache（`.objs/.build.module.cache`；`BACKEND_MULTI_MODULE_CACHE`）在 `emit=exe + multi` 路径读写，复用模块建图结果。
  - 默认会限制过细分桶（`BACKEND_MULTI_MIN_BATCH_UNITS`，默认 `8`），避免 worker 过多导致重复前端开销放大。
  - 同一输出目录采用 lock 互斥（`<out>.objs.lock`），避免并发产物互踩。
  - 增量 stamp 使用“单元文件状态 hash + 编译参数 hash（含编译器身份）”，减少无关改动重编并避免跨编译器复用旧产物。
  - 可执行单对象回退：`emit=exe` + Darwin + self-link 下可设 `BACKEND_MULTI_EXE_MAX_OBJS=<N>`（默认 `0` 禁用）在单元数超过阈值时回退“单对象编译+链接”；通常不建议开启（大模块多单元编译在当前实现下更快）。
  - `BACKEND_MULTI_LTO=1` 在 `emit=exe + multi` 路径下采用“全模块优化 + 单对象链接”口径（`<out>.objs` 中仅保留 1 个 `.o`），避免跨单元符号裁剪漂移。

Stage1 全语法回归门禁：
```bash
sh src/tooling/verify_stage1_fullspec.sh
```
说明：
- 默认 `STAGE1_FULLSPEC_VALIDATE=0`（聚焦端到端行为闭环，避免大仓库在 validate=1 下超长编译）；如需严格后端校验口径，可显式设置 `STAGE1_FULLSPEC_VALIDATE=1`。
- 默认 `STAGE1_FULLSPEC_REUSE=1`：当输入/driver/关键编译参数未变化时，直接复用上次产物（`<name>.stage1_fullspec.stamp`），避免重复冷编译。
- 复用签名会纳入 `*` 环境变量哈希（排除 `STAGE1_FULLSPEC_REUSE/TIMEOUT`、timeout 诊断/健康探针变量、profile 开关与输入输出路径），配置变化会自动失效重编。
- 默认超时为 `60s`（`STAGE1_FULLSPEC_TIMEOUT`）；超时后会自动触发一次 profile 诊断复跑（`STAGE1_FULLSPEC_TIMEOUT_DIAG=1`，默认 `20s`，日志目录 `chengcache/stage1_fullspec_timeout_diag`），并追加轻量健康探针（`STAGE1_FULLSPEC_TIMEOUT_HEALTH_PROBE=1`，默认 `10s`）用于区分“特例慢编译”与“整体编译器异常”。
- 每次超时诊断会追加结构化摘要到 `chengcache/stage1_fullspec_timeout_diag/summary.tsv`，可配合 `src/tooling/summarize_stage1_timeout_diag.sh` 快速查看最近阻塞点。

## 自研后端与全链自举

后端 driver 选择（脚本统一入口）：
```bash
# 默认优先：本地 `artifacts/backend_driver/cheng`（fresh + 可运行）
# 若缺失/过期，会自动重建本地 driver（默认 system-link）
sh src/tooling/backend_driver_path.sh

# 默认：tooling/verify 脚本会在退出时清理本地 `cheng*` 产物（移动到 `chengcache/_trash_cheng`），避免遗留占用空间。
# 如需保留（加速反复调试），可关闭：
# export CLEAN_CHENG_LOCAL=0
#
# `src/tooling/build_backend_driver.sh` 默认 system-link 重建；可用 `BACKEND_BUILD_DRIVER_LINKER=self` 做兼容验证。
#
# 使用 selfhost stage2（用于全链自举/发布）
export BACKEND_DRIVER=artifacts/backend_selfhost_self_obj/cheng.stage2
```
说明：
- `backend_driver_path.sh` 的可运行性 smoke 默认校验 `stage1` 最小编译路径；如需额外校验 strict 口径，可设 `BACKEND_DRIVER_PATH_STAGE1_STRICT_SMOKE=1`；如需启用 dict fixture 探针可设 `BACKEND_DRIVER_PATH_STAGE1_DICT_SMOKE=1`。
- `backend_driver_path.sh` 默认优先 `artifacts/backend_driver/cheng`；fallback 顺序为 `artifacts/backend_driver/cheng.fixed3` -> `artifacts/backend_seed/cheng.stage2` -> `dist/releases/current/cheng` -> `backend_driver_exec.sh`，仅在显式 `BACKEND_DRIVER_PATH_ALLOW_SELFHOST=1` 时才追加 `artifacts/backend_selfhost_self_obj/cheng.stage2|stage1`。
- `backend_prod_closure.sh` 主链 driver 选择默认与上述一致（默认不允许 selfhost fallback）；可用 `BACKEND_PROD_MAIN_DRIVER_PATH_ALLOW_SELFHOST=1` 放开。
- 若显式 `BACKEND_DRIVER` 不可运行，可设 `BACKEND_DRIVER_ALLOW_FALLBACK=1` 自动回退到可运行候选（默认保持严格失败）。

后端链接环境助手（脚本统一注入 self-linker 运行时 `.o`）：
```bash
# 输出可直接给 `env` 使用的链接环境变量
sh src/tooling/backend_link_env.sh --driver:artifacts/backend_driver/cheng --target:arm64-apple-darwin --linker:self
```
说明：
- `--linker:self`：自动构建/复用 `chengcache/system_helpers.backend.cheng.<target>.o` 并输出  
  `BACKEND_LINKER=self BACKEND_NO_RUNTIME_C=1 BACKEND_RUNTIME_OBJ=<...>`。
- `--linker:auto`（默认）或未设置：按目标自动选择（支持 self-link 的目标默认 `self`，否则回退 `system`）；生产主链建议固定 `self`。

纯 Cheng（不走 C 后端）生成/更新固定 seed（stage2 driver）：
```bash
# 以“已存在的 stage2 driver”作为 seed（推荐：来自发布包/上一版本）
sh src/tooling/backend_seed_pure.sh \
  --seed:artifacts/backend_selfhost_self_obj/cheng.stage2 \
  --out:artifacts/backend_seed/cheng.stage2

# CI/生产可直接指定 seed 路径（缺失会失败，不再使用 C seed）
export SELF_OBJ_BOOTSTRAP_STAGE0=artifacts/backend_seed/cheng.stage2
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
FULLCHAIN_OBJ_ONLY=1 sh src/tooling/verify_fullchain_bootstrap.sh
```
说明：
- `verify_fullchain_bootstrap.sh` 要求 stage2 driver 已存在（默认 `artifacts/backend_selfhost_self_obj/cheng.stage2`，可用 `FULLCHAIN_STAGE2` 覆盖）。
- 缺少 stage2 时，先运行 `sh src/tooling/verify_backend_selfhost_bootstrap_self_obj.sh` 或 `sh src/tooling/verify_backend_ci_obj_only.sh --seed:<path>`。
- `verify_fullchain_bootstrap.sh` 默认开启复用（`FULLCHAIN_REUSE=1`）：当 stage2/source/runtime 未变化时，复用 `stage1_fullspec_obj`，减少重复闭环耗时。
- fullchain 工具构建默认关闭（`FULLCHAIN_BUILD_TOOLS=0`）；仅在需要验证 `cheng_pkg_source/cheng_pkg/cheng_storage` 时再显式设置 `FULLCHAIN_BUILD_TOOLS=1`。开启后默认 best-effort（部分工具失败不阻断）；严格模式可设 `FULLCHAIN_BUILD_TOOLS_STRICT=1`，并通过 `FULLCHAIN_BUILD_TOOLS_STRICT_SCOPE=core|all` 控制范围（默认 `core` 仅约束编译器闭环，生态工具保留 best-effort；`all` 强制所有工具源码通过）。
  - 工具构建要求 `MM=orc`，不再自动回退 `MM=off`。
  - 启用后按主机核数并行构建（`FULLCHAIN_TOOL_JOBS`，`0`=auto）。
  - 工具单项默认超时 `20s`（`FULLCHAIN_TOOL_TIMEOUT`）；超时后优先复用既有可执行产物（`FULLCHAIN_TOOL_FALLBACK_REUSE=1`）。
  - 若既有产物不可运行，默认会生成仅用于 fullchain gate 的 `--help` stub（`FULLCHAIN_TOOL_STUB_ON_FAIL=1`）；如需“必须源码真实构建”，显式设 `FULLCHAIN_TOOL_STUB_ON_FAIL=0`。
  - `verify_backend_selfhost_bootstrap_self_obj.sh` 支持 `SELF_OBJ_BOOTSTRAP_TIMEOUT=<seconds>`（默认 60）防止 stage1/stage2 自举编译长时间卡死。
  - smoke 子阶段支持独立超时 `SELF_OBJ_BOOTSTRAP_SMOKE_TIMEOUT=<seconds>`（默认 15）；用于在 `fast` 模式下快速触发 smoke fallback，避免总时长被单次卡死拉满。
- `verify_backend_selfhost_bootstrap_self_obj.sh` 固定使用 `ABI=v2_noptr`（非 `v2_noptr` 将直接报错）。
- `ABI=v2_noptr` 默认会在 stage1 语义层启用 std no-pointer 门禁；可用 `STAGE1_STD_NO_POINTERS=0` 显式关闭（兼容口径），或用 `STAGE1_STD_NO_POINTERS=1`/`STAGE1_STD_NO_POINTERS_STRICT=1` 强制严格门禁。
- 新增 `STAGE1_NO_POINTERS_NON_C_ABI=1`：在非 C ABI 对接模块禁用 `*`/`&`/deref/`ptr_*` 等指针语法与操作（C ABI 桥接模块豁免）。
- `STAGE1_NO_POINTERS_NON_C_ABI_INTERNAL` 默认按开启处理（仅显式设为 `0` 时放宽到编译器内部路径），因此 `src/stage1`/`src/backend`/`src/tooling` 也默认纳入 no-pointer 门禁。
- `v2_noptr` 的策略由 `verify_backend_abi_v2_noptr.sh` 负向样例门禁保证：`src/std` 路径下的指针类型必须在编译期被拒绝。
- `verify_backend_abi_v2_noptr.sh` 固定只校验 `v2_noptr` 及 non-C-ABI 门禁，不再包含 `v1` 对照探针。
- `verify_backend_abi_v2_noptr.sh` 的 non-C-ABI 子门禁会显式设置 `STAGE1_STD_NO_POINTERS=0`，以隔离并验证 non-C-ABI 诊断本身。
- `chengc.sh`（以及兼容壳 `chengb.sh`）在 `ABI=v2_noptr` 时会自动注入 `STAGE1_STD_NO_POINTERS=1`、`STAGE1_STD_NO_POINTERS_STRICT=0`、`STAGE1_NO_POINTERS_NON_C_ABI=1`、`STAGE1_NO_POINTERS_NON_C_ABI_INTERNAL=1`；严格 `std` no-pointer 由 `verify_backend_abi_v2_noptr.sh` 专项 gate 覆盖。
- 新增 `verify_backend_noptr_default_cli.sh`：验证默认 `chengc` 入口（不手工注入 non-C-ABI env）会拒绝 non-C-ABI 指针样例，同时保留正样例与 C ABI bridge 正常通过。
  - stage0 选择顺序：`SELF_OBJ_BOOTSTRAP_STAGE0`（显式） -> 可执行 `BACKEND_DRIVER` -> `artifacts/backend_selfhost_self_obj/cheng_stage0_prod`/`cheng_stage0_default` -> `artifacts/backend_selfhost_self_obj/cheng.stage2` -> `artifacts/backend_selfhost_self_obj/cheng.stage1` -> `artifacts/backend_driver/cheng` -> `dist/releases/current/cheng` -> `artifacts/backend_seed/cheng.stage2` -> 可运行 `./cheng`（缺失或不可运行时才重建）。seed 仅用于“首次/无自举产物”的兜底。
  - 自举模式：`SELF_OBJ_BOOTSTRAP_MODE=strict|fast`（默认 `fast`）。`strict` 校验 `stage1->stage2` 固定点；若首次不收敛，会自动追加 `stage3 exe witness`（`F(stage2)` 的 `emit=exe`）并以 `stage2->stage3(exe)` 固定点作为收敛门禁，同时落盘 compile-stamp/hash witness。`fast` 只编译 stage1 并同步为 stage2（开发加速，跳过 fixed-point 校验）。
  - `fast` 模式默认沿用“优先复用已有 stage1/stage2”（`SELF_OBJ_BOOTSTRAP_FAST_REUSE_STALE=1`）；如需在 fast 下强制检查是否过期并触发重编，可设 `SELF_OBJ_BOOTSTRAP_FAST_REUSE_STALE=0`。
  - 自举编译默认口径：`SELF_OBJ_BOOTSTRAP_MULTI=1`、`SELF_OBJ_BOOTSTRAP_INCREMENTAL=1`；`strict` 模式默认并行并在 worker 探针失败时快速回退串行（默认 `SELF_OBJ_BOOTSTRAP_ALLOW_RETRY=0`，避免“失败后整轮重试”放大耗时），`fast` 模式保持重试开启并自动按逻辑核数设置 jobs（可用 `SELF_OBJ_BOOTSTRAP_FAST_JOBS_CAP` 设上限，默认 `8`）。
  - 超时策略：编译返回 `124`（timeout）时默认不再做同参重试，避免单阶段耗时翻倍掩盖性能回退；selfhost 主链已移除 stage0 兜底重编分支，超时/失败将直接报错。
  - `emit=exe` 若未产出 sidecar `.o`/`.objs`，selfhost 门禁仅在兼容诊断路径补充 `emit=obj` 对比对象；严格 fixed-point 以 `stage3 exe witness + compile-stamp/hash` 为准。
  - stage0 compat overlay 已从自举主链移除：`SELF_OBJ_BOOTSTRAP_STAGE0_COMPAT` 仅接受 `0`，命中语法不兼容时直接失败并输出 native 日志。
  - 自举临时产物默认使用稳定 session（`SELF_OBJ_BOOTSTRAP_SESSION=default`），可复用 `<out>_tmp_<session>.objs` 缓存；并行多任务可显式设置不同 session 避免互相覆盖。
  - 执行结束会输出 `backend.selfhost_self_obj.timing`（lock/stage1/stage2/smoke/total 秒数），并落盘 `selfhost_timing_<session>.tsv` 与 `selfhost_metrics_<session>.json`（可用 `SELF_OBJ_BOOTSTRAP_TIMING_OUT` / `SELF_OBJ_BOOTSTRAP_METRICS_OUT` 覆盖路径）；失败/超时路径也会补写 `total` 行（`fail`/`fail-timeout`）。
  - 可在问题排查时显式关闭：`SELF_OBJ_BOOTSTRAP_MULTI=0 SELF_OBJ_BOOTSTRAP_INCREMENTAL=0`。
  - 2026-02-08 同机冷态实测：`fast` 约 `17s`，`strict` 约 `24-27s`；`backend_prod_closure.sh --only-self-obj-bootstrap --selfhost-strict` 在 `60s` 约束内通过。
  - 2026-02-06：已修复一类“无输出超时”根因（`src/std/strings.cheng` 的字符串 `==` 对 `nil` 比较递归调用自身）；该问题会让 selfhost 阶段卡住直到超时。
  - 修复后若 selfhost 失败，将优先以可诊断错误退出（而非长时间超时卡住）。
- `FULLCHAIN_OBJ_ONLY=1` 默认使用 `examples/backend_fullchain_smoke.cheng` 作为 obj-only 样例（要求输出 `fullspec ok`）；可用 `FULLCHAIN_STAGE1_FILE=<path>` 覆盖。
- fullchain stage1 样例编译默认走 `FULLCHAIN_STAGE1_FRONTEND=stage1`（默认单次编译超时 `60s`）。
- fullchain stage1 样例编译默认超时 `60s`（`FULLCHAIN_STAGE1_TIMEOUT`），默认并行 `FULLCHAIN_STAGE1_MULTI=1`；并行失败会自动串行重试。可用 `FULLCHAIN_STAGE1_MULTI` / `FULLCHAIN_STAGE1_JOBS` 调整并行参数。
- fullchain stage1 样例链接器可用 `FULLCHAIN_STAGE1_LINKER=auto|self|system` 控制（默认 `auto`：先尝试 `self`，失败后回退 `system`）。

CI/生产（macOS arm64）使用 dist seed 跑“后端自举 + fullchain smoke”：
```bash
sh src/tooling/verify_backend_ci_obj_only.sh
```
说明：
- 默认从 `dist/releases/current_id.txt` 解析 seed；也可传 `--seed:<path>` 指定。
- 若 dist seed 不存在，会直接失败并提示补充 seed（可用 `--require-seed` 强制严格模式）。
- 不会自动使用 `artifacts/backend_seed/cheng.stage2` 或 `artifacts/backend_selfhost_self_obj/cheng.stage2`。
- 默认额外执行 `backend.ci.selfhost_perf_regression`（读取 `selfhost_timing_<session>.tsv` 做阈值检查）。
- selfhost perf 阈值默认来自 `src/tooling/selfhost_perf_baseline.env`；可用 `SELFHOST_PERF_BASELINE=<path>` 切换基线文件，或直接用 `SELFHOST_PERF_MAX_*` 覆盖单项阈值。
- 默认额外执行 `backend.ci.multi_perf_regression`（对 `verify_backend_multi.sh` + `verify_backend_multi_lto.sh` 做阈值检查）。
- multi perf 阈值默认来自 `src/tooling/multi_perf_baseline.env`；可用 `MULTI_PERF_BASELINE=<path>` 切换基线文件，或直接用 `MULTI_PERF_MAX_*` 覆盖阈值。

UIR 稳定性闸口（默认不在闭环中开启）：
```bash
UIR_STABILITY_ITERS=5 UIR_STABILITY_MODES=dict sh src/tooling/verify_backend_uir_stability.sh
```
- 该脚本会对关键 fixture 重复构建对象产物并比较哈希，确保 `UIR` 在同口径下可复现。
- 环境变量：`UIR_STABILITY_ITERS`（默认 3）、`UIR_STABILITY_MODES`（默认 `dict`）、`UIR_STABILITY_FIXTURES`（默认包含核心 cfg/分支优化类 fixture）、`UIR_STABILITY_OUT_DIR`（默认 `artifacts/backend_uir_stability`）、`UIR_STABILITY_OPT_LEVEL`、`UIR_STABILITY_FRONTEND`，以及与产线一致的 `UIR_OPT2_ITERS`、`UIR_OPT3_ITERS`、`UIR_OPT3_CLEANUP_ITERS`、`UIR_CFG_CANON_ITERS`、`UIR_SIMD*`。

后端生产闭环（默认快速口径；fullchain/stress 按需开启）：
```bash
sh src/tooling/backend_prod_closure.sh
sh src/tooling/backend_prod_closure.sh --fullchain
sh src/tooling/backend_prod_closure.sh --stress
```
说明：
- `backend_prod_closure.sh` 默认不跑 fullchain/stress；可用 `--fullchain` / `BACKEND_RUN_FULLCHAIN=1` 和 `--stress` / `BACKEND_RUN_STRESS=1` 显式开启。
- `backend_prod_closure.sh` 默认不跑 selfhost 自举（`BACKEND_RUN_SELFHOST=0`）；如需启用自举与相关性能/探针 gate，显式传 `--selfhost`（或设 `BACKEND_RUN_SELFHOST=1`）。
- `backend_prod_closure.sh` 默认在 `backend.closedloop` 中开启 fullspec（等价注入 `BACKEND_RUN_FULLSPEC=1`）；如需临时快速排障可显式设 `BACKEND_RUN_FULLSPEC=0` 关闭。
- `backend_prod_closure.sh --uir-stability` 会在 `opt3` 与 `ssa` 闸口之后执行 `backend.uir_stability`；默认已开启（`BACKEND_RUN_UIR_STABILITY=1`）。
- `backend_prod_closure.sh` 默认 strict：任一步骤 `exit 2`（skip）会直接失败；仅本地排障时再显式加 `--allow-skip`。
- `backend_prod_closure.sh --no-publish` 默认启用稳定收口参数集（`BACKEND_PROD_NO_PUBLISH_STABLE_PROFILE=1`）：自动关闭 `determinism_strict/opt/opt2/opt3/uir_stability/ssa/ffi/sanitizer/debug/exe_determinism/multi_perf` 可选 gate，仅保留 required 收口链路；设 `BACKEND_PROD_NO_PUBLISH_STABLE_PROFILE=0` 可恢复完整可选 gate。
- `backend_prod_closure.sh` 不再全局导出统一 linker；改为 gate 级显式口径：dev 链路 gate 固定 `BACKEND_LINKER=self`，release 链路 gate 固定 `BACKEND_LINKER=system`（并强制 `BACKEND_NO_RUNTIME_C=0`）。
- `backend_prod_closure.sh` 现在仅支持 `ABI=v2_noptr`（若外部传入非 `v2_noptr` 会直接报错退出）；主闭环以 `v2_noptr` 兼容口径执行（`STAGE1_STD_NO_POINTERS=1`），并通过 `backend.abi_v2_noptr` 步骤单独执行严格 no-pointer 门禁。
- `backend_prod_closure.sh` 默认包含 `backend.abi_v2_noptr` 专项门禁（`src/tooling/verify_backend_abi_v2_noptr.sh`）。
- `backend_prod_closure.sh` 默认新增并阻断 `backend.profile_schema`（`src/tooling/verify_backend_profile_schema.sh`）、`backend.import_cycle_predeclare`（`src/tooling/verify_backend_import_cycle_predeclare.sh`）、`backend.rawptr_contract`（`src/tooling/verify_backend_rawptr_contract.sh`）、`backend.rawptr_surface_forbid`（`src/tooling/verify_backend_rawptr_surface_forbid.sh`）、`backend.ffi_slice_shim`（`src/tooling/verify_backend_ffi_slice_shim.sh`）、`backend.ffi_outptr_tuple`（`src/tooling/verify_backend_ffi_outptr_tuple.sh`）、`backend.ffi_handle_sandbox`（`src/tooling/verify_backend_ffi_handle_sandbox.sh`）、`backend.ffi_borrow_bridge`（`src/tooling/verify_backend_ffi_borrow_bridge.sh`）、`backend.mem_contract`（`src/tooling/verify_backend_mem_contract.sh`）、`backend.dod_contract`（`src/tooling/verify_backend_dod_contract.sh`）、`backend.mem_image_core`（`src/tooling/verify_backend_mem_image_core.sh`）、`backend.mem_exe_emit`（`src/tooling/verify_backend_mem_exe_emit.sh`）、`backend.profile_baseline`（`src/tooling/verify_backend_profile_baseline.sh`）、`backend.dual_track`（`src/tooling/verify_backend_dual_track.sh`）、`backend.linkerless_dev`（`src/tooling/verify_backend_linkerless_dev.sh`）、`backend.hotpatch_meta`（`src/tooling/verify_backend_hotpatch_meta.sh`）、`backend.hotpatch_inplace`（`src/tooling/verify_backend_hotpatch_inplace.sh`）、`backend.incr_patch_fastpath`（`src/tooling/verify_backend_incr_patch_fastpath.sh`）、`backend.mem_patch_regression`（`src/tooling/verify_backend_mem_patch_regression.sh`）、`backend.hotpatch`（`src/tooling/verify_backend_hotpatch.sh`）、`backend.dod_soa`（`src/tooling/verify_backend_dod_soa.sh`）、`backend.metering_stream`（`src/tooling/verify_backend_metering_stream.sh`）、`backend.release_system_link`（`src/tooling/verify_backend_release_c_o3_lto.sh`）、`backend.noalias_opt`（`src/tooling/verify_backend_noalias_opt.sh`）、`backend.egraph_cost`（`src/tooling/verify_backend_egraph_cost.sh`）、`backend.dod_opt_regression`（`src/tooling/verify_backend_dod_opt_regression.sh`）、`backend.plugin_isolation`（`src/tooling/verify_backend_plugin_isolation.sh`）、`backend.mir_borrow`（`src/tooling/verify_backend_mir_borrow.sh`）与 `backend.plugin_system`（`src/tooling/verify_backend_plugin_system.sh`），确保观测基线、导入循环硬错误与无前置声明回归、Raw Pointer Safety 契约与语法/诊断禁令、Slice 影子桥接、Out-Ptr Tuple 桥接、Handle 沙盒映射、Borrow Struct* 桥接、MemImage/PatchMeta 契约、DOD SoA/Arena/index 契约、MemImage 核心数据流、MemImage 直出 EXE（runtime 合并 + 原子写盘）、热补丁元数据面、热补丁事务执行面、热补丁增量快路径、内存补丁回归门禁、DOD/SoA、noalias/egraph 正确性负例回归、metering streaming、Release system-link 与 Linkerless dev 路径持续收敛。
- `verify_backend_release_c_o3_lto.sh` 已升级为 required 的 release system-link gate：不再允许 known-blocker 放行，默认强制执行 `system-link + O3/LTO` 并运行产物。
- `verify_backend_dod_opt_regression.sh` 默认执行 noalias 负例回归（guard fixture off/on 对象一致）与 egraph/cost model 确定性双跑（对象 `cmp -s` 一致），并输出 `artifacts/backend_dod_opt_regression/*.report.txt`。
- `backend_prod_closure.sh` 默认新增并阻断 `backend.linker_abi_core`（`src/tooling/verify_backend_linker_abi_core.sh`）：生成 Darwin/Linux 自研 linker ABI manifest，差分仅允许 `src/tooling/linker_abi_core_diff_whitelist.allowlist` 白名单键。
- `backend.linker_abi_core` 默认只尝试非 selfhost driver；仅显式 `BACKEND_LINKER_ABI_CORE_ALLOW_SELFHOST=1` 时才回退 `backend_selfhost_self_obj/*`。
- `backend_prod_closure.sh` 支持 `BACKEND_OPT_DRIVER`：可仅覆盖 `backend.determinism_strict/exe_determinism_strict` 与 `backend.opt/opt2/multi_lto/multi_perf/opt3/simd/uir_stability/ssa/ffi/sanitizer/debug/exe_determinism` gate driver，不影响主 required-gates driver。
- `backend_prod_closure.sh` 默认新增并阻断 `backend.noptr_default_cli`（`src/tooling/verify_backend_noptr_default_cli.sh`）与 `backend.noptr_exemption_scope`（`src/tooling/verify_backend_noptr_exemption_scope.sh`），确保“默认入口零指针”与“豁免范围仅限自举/探针 allowlist”。
- `backend.abi_v2_noptr` 默认优先使用本地 `artifacts/backend_driver/cheng`（要求具备 non-C-ABI no-pointer 诊断 marker），并回退到当前 `BACKEND_DRIVER`；仅在启用 selfhost 时才探测 `backend_selfhost_self_obj/*`；可用 `BACKEND_ABI_V2_DRIVER` 显式覆盖。
- `backend_prod_closure.sh` 的主门禁链路在 `STAGE1_NO_POINTERS_NON_C_ABI=1` 时默认不切换到 selfhost driver（避免 strict non-C-ABI 与历史 gate 口径冲突）；可用 `BACKEND_MAIN_ALLOW_SELFHOST_DRIVER=1` 强制主链使用 selfhost driver。
- `backend_prod_closure.sh` 在 selfhost 成功后会优先切换后续 gate 到 `artifacts/backend_selfhost_self_obj/cheng.stage2`（即使最初显式传入了 `BACKEND_DRIVER` 作为 stage0）；若未跑 selfhost，且 `BACKEND_DRIVER` 未显式设置，才回落到 `backend_driver_path.sh`。
- 若主链选中的 `BACKEND_DRIVER` 体检失败，`backend_prod_closure.sh` 会直接失败；可通过 `BACKEND_DRIVER` 显式指定稳定 driver，或调整 `backend_driver_path.sh` 选择策略后再重试。
- `backend_prod_closure.sh` 默认导出 `BACKEND_DRIVER_ALLOW_FALLBACK=0`（显式 driver 失效时保持严格失败），需要兼容回退时可手动设为 `1`。
- `backend_prod_closure.sh` 在主 driver 完成体检后会导出 `BACKEND_DRIVER_PATH_STAGE1_STRICT_SMOKE=0`，后续 gate 不再重复 strict smoke（保留主链稳定性并减少抖动）。
- `verify_backend_closedloop.sh` 默认执行 `backend.spawn_api_gate`（`src/tooling/verify_backend_spawn_api_gate.sh`）；该 gate 已切换到不依赖 `std/async_rt` 指针实现的 fixture，因此在 `ABI=v2_noptr` 下也可直接回归。
- `verify_backend_closedloop.sh` 默认执行 `backend.import_cycle_predeclare`（`src/tooling/verify_backend_import_cycle_predeclare.sh`），持续验证“循环导入硬错误 + 无前置声明可编译”口径；该 gate 已切换为纯 runtime 断言（负例必须编译失败并输出 `Import cycle detected: ... -> ...` 链路），不再允许 source-contract fallback。
- `verify_backend_closedloop.sh` 默认执行 `backend.release_system_link`（`src/tooling/verify_backend_release_c_o3_lto.sh`）与 `backend.dual_track`（`src/tooling/verify_backend_dual_track.sh`），确保 Dev/Release 双轨策略持续可回归。
- `verify_backend_closedloop.sh` 默认执行 `backend.mir_borrow`（`src/tooling/verify_backend_mir_borrow.sh`），确保 `ownership/borrow -> noalias` 语义下沉与 `compile_stamp/generics_report` 字段持续可回归。
- `verify_backend_closedloop.sh` 中 `backend.x86_64_darwin`/`backend.x86_64_linux` 作为目标矩阵扩展项，即使 `BACKEND_MATRIX_STRICT=1` 也允许 `skip`（不阻断主闭环）。
- `verify_backend_obj_fullspec_gate.sh` 默认复用已有 `artifacts/backend_obj_fullspec_gate/backend_obj_fullspec`（避免重复冷编译超时）；如需强制重编可设 `BACKEND_OBJ_FULLSPEC_REBUILD_ON_SOURCE=1` 或 `BACKEND_OBJ_FULLSPEC_REBUILD_ON_DRIVER=1`。
- `backend_prod_closure.sh` 在启用 selfhost 时，默认将自举阶段单次编译超时设为 `120s`（`BACKEND_PROD_SELFHOST_TIMEOUT`）。
- `backend_prod_closure.sh` 默认对每个 gate 施加 `60s` 超时（`BACKEND_PROD_GATE_TIMEOUT`，设为 `0` 可关闭），超时直接失败并打印 gate 标签。
- `backend_prod_closure.sh` 默认开启超时诊断（`BACKEND_PROD_TIMEOUT_DIAG=1`）：当 gate 超时会自动采样并输出诊断文件路径（默认目录 `chengcache/backend_timeout_diag`，采样时长 `BACKEND_PROD_TIMEOUT_DIAG_SECONDS=5`，可通过 `BACKEND_PROD_TIMEOUT_DIAG_TOOL` 覆盖采样器命令）。
- `backend_prod_closure.sh` 默认开启超时摘要（`BACKEND_PROD_TIMEOUT_DIAG_SUMMARY=1`）：超时后会调用 `src/tooling/summarize_timeout_diag.sh` 输出热点 TopN（`BACKEND_PROD_TIMEOUT_DIAG_SUMMARY_TOP`）。
- `backend_prod_closure.sh` 在启用 selfhost 时默认开启 `backend.selfhost_perf_regression`（`BACKEND_RUN_SELFHOST_PERF=1`），读取 `selfhost_timing_<session>.tsv` 检查 stage1/stage2/total 阈值并在超阈值时失败。
- `backend.selfhost_perf_regression` 默认加载 `src/tooling/selfhost_perf_baseline.env`（可通过 `SELFHOST_PERF_BASELINE` 覆盖）；发布 bundle 会附带该基线文件以保证阈值口径一致。
- `backend_prod_closure.sh` 默认开启 `backend.multi_perf_regression`（`BACKEND_RUN_MULTI_PERF=1`），对 `backend.multi`/`backend.multi_lto` 做并行编译性能阈值检查（可用 `--no-multi-perf` 关闭）。
- `backend.multi_perf_regression` 默认加载 `src/tooling/multi_perf_baseline.env`（可通过 `MULTI_PERF_BASELINE` 覆盖）；可用 `BACKEND_PROD_MULTI_PERF_TIMEOUT` 或 `MULTI_PERF_TIMEOUT` 调整单 gate 超时。
- 当 `BACKEND_RUN_FULLSPEC=1` 时，`backend.closedloop` 使用独立超时 `BACKEND_PROD_CLOSEDLOOP_TIMEOUT`（默认 `120s`），避免 fullspec 编译被通用 60s gate 误杀。
- `backend_prod_closure.sh` 的 selfhost 自举步骤会显式设置 `STAGE1_NO_POINTERS_NON_C_ABI=0` 与 `STAGE1_NO_POINTERS_NON_C_ABI_INTERNAL=0`（仅用于自举编译器源码），non-C-ABI no-pointer 策略仍由后续 `backend.closedloop`/`backend.abi_v2_noptr` 门禁强制。
- `backend_prod_closure.sh` 在启用 selfhost 时默认自举模式为 `fast`；可用 `--selfhost-strict`（或 `BACKEND_PROD_SELFHOST_MODE=strict`）切到 fixed-point 口径。
- `backend_prod_closure.sh` 提供显式参数 `--selfhost-fast` / `--selfhost-strict`（优先级高于环境变量）；启用 selfhost 后默认启用 `--selfhost-strict-gate`（可用 `BACKEND_RUN_SELFHOST_STRICT=0` 关闭）在 `fast` 主链后追加一轮 `strict` 自举门禁。
- `--selfhost-strict-gate` 默认复用 fast 同一 session（避免重复冷编译）；可用 `BACKEND_PROD_SELFHOST_STRICT_SESSION=<name>` 指定独立 strict session。
- `--selfhost-strict-gate` 默认启用 `BACKEND_PROD_SELFHOST_STRICT_ALLOW_FAST_REUSE=1`：strict 口径优先复用 fast 生成的 `stage1/stage2` 产物，避免因源码时间戳触发重复冷编译超时；设为 `0` 可恢复 strict 重编口径。
- `backend_prod_closure.sh` 在启用 selfhost 时默认阻断 `backend.stage0_no_compat`（可用 `--no-stage0-no-compat-gate` 或 `BACKEND_RUN_STAGE0_NO_COMPAT_GATE=0` 关闭）：强制 `stage0 -> stage1` 原生编译，禁止 compat overlay（`SELF_OBJ_BOOTSTRAP_STAGE0_COMPAT=0`）。
- `backend.stage0_no_compat` 默认参数：`mode=fast`、`reuse=0`、`validate=0`、`skip_smoke=1`、`require_runnable=0`；可用 `BACKEND_PROD_STAGE0_NO_COMPAT_*` 覆盖（含 `..._GATE_TIMEOUT`、`..._STAGE0`、`..._SESSION`）。
- `backend_prod_closure.sh` 在启用 selfhost 时默认开启 strict no-reuse 冷路径探针（固定 `reuse=0`、`strict_allow_fast_reuse=0`，阻断模式）；默认启用 alias-off fast（`BACKEND_PROD_SELFHOST_STRICT_NOREUSE_ALIAS_OFF_FAST=1`），可用 `--no-selfhost-strict-noreuse-probe` 或 `BACKEND_RUN_SELFHOST_STRICT_NOREUSE_PROBE=0` 关闭。
- strict no-reuse 探针默认阻断主链：默认 `gate=110s`（`BACKEND_PROD_SELFHOST_STRICT_NOREUSE_GATE_TIMEOUT`）与内部 `probe=90s`（`BACKEND_PROD_SELFHOST_STRICT_NOREUSE_PROBE_TIMEOUT`），并自动夹到 `< gate timeout` 以避免外层 gate 误判；会话可用 `BACKEND_PROD_SELFHOST_STRICT_NOREUSE_SESSION` 覆盖。
- `verify_backend_selfhost_strict_noreuse_probe.sh` 默认使用 `SELFHOST_STRICT_PROBE_GENERIC_MODE=dict`、`SELFHOST_STRICT_PROBE_SKIP_CPROFILE=1`、`SELFHOST_STRICT_PROBE_REQUIRE_RUNNABLE=0`、`SELFHOST_STRICT_PROBE_MULTI=0`、`SELFHOST_STRICT_PROBE_MULTI_FORCE=0`、`SELFHOST_STRICT_PROBE_ALLOW_RETRY=0`、`SELFHOST_STRICT_PROBE_PREFLIGHT=1`（默认 `20s` 预检）；并行 worker 探针若命中 `fork worker failed / unit file not found / crash` 会在进入 stage1 前自动回退串行，避免“并行失败 + 重试”导致门禁耗时放大。当 stage0 seed 明确无法编译 `backend_driver.cheng` 时，软探针会快速 skip 并输出 preflight 日志路径，避免固定等待到 90s/110s 超时。
- 并行专项性能检查：`sh src/tooling/verify_backend_selfhost_parallel_perf.sh`（同一 stage0 下依次跑 serial/multi 两轮 strict no-reuse，默认要求 `parallel <= serial + 2s`）；`backend_prod_closure.sh` 在启用 selfhost 时默认阻断 `backend.selfhost_parallel_perf`（`BACKEND_RUN_SELFHOST_PARALLEL_PERF=0` 可关闭）。
- 新增新 driver 自举 smoke 阻断 gate：`sh src/tooling/verify_backend_driver_selfbuild_smoke.sh`。`backend_prod_closure.sh` 在启用 selfhost 时默认阻断 `backend.driver_selfbuild_smoke`（`BACKEND_RUN_DRIVER_SELFBUILD_SMOKE=0` 可关闭），默认 gate 超时 `60s`（`BACKEND_PROD_DRIVER_SELFBUILD_SMOKE_TIMEOUT`），内部自举超时 `55s`（`BACKEND_PROD_DRIVER_SELFBUILD_SMOKE_BUILD_TIMEOUT`）；失败会输出 `build_log/attempt_report/attempt_log/crash_report` 与根因分类（`cause`）。
- 若已显式设置 `BACKEND_DRIVER` 且可执行，`backend_prod_closure.sh` 会优先将其作为 selfhost stage0，避免额外重建本地 driver。
- 若未显式提供 stage0，`backend_prod_closure.sh` 会按“可编译 smoke 探测”优先复用 `artifacts/backend_selfhost_self_obj/cheng_stage0_prod`（其次 `cheng_stage0_default`、`cheng.stage2`、`cheng.stage1`、`artifacts/backend_driver/cheng`、`dist/releases/current/cheng`、`artifacts/backend_seed/cheng.stage2`）；探测失败（如可执行但编译崩溃）会自动跳过并回退到 `backend_driver_path.sh`。
- stage0 探针默认走 `BACKEND_PROD_STAGE0_PROBE_MODE=path`（仅复用 `backend_driver_path.sh` 的可运行 smoke，不再额外重编 `backend_driver.cheng`）；可切到 `light`（stage1 小样例）或 `full`（历史重型探针）排查问题。
- `backend_prod_closure.sh` 在启用 selfhost 时默认固定复用口径：`BACKEND_PROD_SELFHOST_REUSE=1`、`BACKEND_PROD_SELFHOST_SESSION=prod`（可覆盖），减少重复冷编译与并发互踩。
- `verify_backend_selfhost_bootstrap_self_obj.sh` 会把 stage0 driver 固化拷贝到 `artifacts/backend_selfhost_self_obj/cheng_stage0_<session>` 后再执行，且仅在 stage0 **内容变化**时更新该副本，避免因时间戳漂移触发无效重编。
- `verify_backend_selfhost_bootstrap_self_obj.sh` 在 `SELF_OBJ_BOOTSTRAP_REUSE=1` 下会复用 smoke 产物（`hello_puts`）并仅在依赖变化时重编，降低重复验收耗时。
- `verify_backend_selfhost_bootstrap_self_obj.sh` 在 `GENERIC_MODE` 未显式设置时：`fast` 默认 `dict`、`strict` 默认 `hybrid`；并且 timeout 回收采用进程组+进程本体双重 kill，避免遗留孤儿编译进程。
- `backend_prod_closure.sh` 默认执行 `backend.coff_lld_link`；`verify_backend_coff_lld_link.sh` 在缺少 `lld-link/llvm-lld/ld.lld/lld` 时自动回退为 COFF `obj-only` 校验（不再 skip）。
- `backend_prod_closure.sh` 结束时会输出 `backend_prod_closure.timing_top`（按耗时降序的 gate top 列表），用于持续压缩闭环耗时。
- 生产/CI 可用 `--require-seed` 禁止使用本机 stage0；`--require-seed` 需要显式传 `--seed/--seed-id/--seed-tar`。
- 发布（默认开启）要求显式 seed：需传 `--seed/--seed-id/--seed-tar`；仅做本地闭环可加 `--no-publish`。
- `scripts/closedloop.sh` 在 `CLOSEDLOOP_PROD=1` 下默认以 `--no-publish` 运行 `backend_prod_closure.sh`；若需要发布门禁，请显式设置 `CLOSEDLOOP_BACKEND_PROD_ARGS` 传入 seed 参数。
- 在上述 `backend_prod_closure.sh` 调用中，`backend.closedloop` 默认会跑 fullspec（`BACKEND_RUN_FULLSPEC=1`）；仅本地排障时可在 `CLOSEDLOOP_BACKEND_PROD_ARGS` 或环境中显式设置 `BACKEND_RUN_FULLSPEC=0`。
- `scripts/closedloop.sh` 也支持 `CLOSEDLOOP_UIR_STABILITY=1`，会自动把 `backend_prod_closure.sh --uir-stability` 加到生产闭环参数（与 `CLOSEDLOOP_BACKEND_PROD_ARGS` 合并）。
- `scripts/closedloop.sh` 默认导出 `BACKEND_PROD_GATE_TIMEOUT=60`、`BACKEND_PROD_SELFHOST_TIMEOUT=60`、`BACKEND_PROD_TIMEOUT_DIAG=1`、`BACKEND_RUN_SELFHOST_PERF=1`。
- `scripts/closedloop.sh` 在 `CLOSEDLOOP_PROD=1` 下默认 `STAGE1_FULLSPEC=0`（可通过 `STAGE1_FULLSPEC=1` 或 `CLOSEDLOOP_STAGE1_FULLSPEC_DEFAULT=1` 开启）；开启时默认导出 `STAGE1_FULLSPEC_TIMEOUT=60`（可显式覆盖）。
- `scripts/closedloop.sh` 支持 `CLOSEDLOOP_FRONTIER=auto|0|1`（默认 `auto`）：自动检测到 `cheng-libp2p` 仓库时追加 `verify_libp2p_frontier`；`auto` 默认非阻断（`CLOSEDLOOP_FRONTIER_SOFT_AUTO=1`），显式 `1` 仍为阻断门禁。
- `scripts/closedloop.sh` 在 `CLOSEDLOOP_LIBP2P=1` 时执行 `verify_libp2p_prod_closure.sh`（不再是 smoke-only）。
- `scripts/closedloop.sh` 末尾默认尝试输出 timeout 采样摘要（`CLOSEDLOOP_TIMEOUT_SUMMARY=1`）以及 stage1 fullspec 超时摘要（`CLOSEDLOOP_STAGE1_TIMEOUT_SUMMARY=1`）。
- `build_backend_driver.sh` 自举重建默认采用串行增量（`BACKEND_BUILD_DRIVER_MULTI=0`）；需要并行时可显式设为 `1`（并行失败会自动串行重试）。可用 `BACKEND_BUILD_DRIVER_MULTI` / `BACKEND_BUILD_DRIVER_INCREMENTAL` / `BACKEND_BUILD_DRIVER_JOBS` 覆盖。
- `build_backend_driver.sh` 未显式指定 stage0 时，会优先尝试 `artifacts/backend_selfhost_self_obj/cheng_stage0_*` 与 `artifacts/backend_selfhost_self_obj/cheng.stage2|stage1`，再尝试 `artifacts/backend_driver/cheng`、`dist/releases/current/cheng`、`artifacts/backend_seed/cheng.stage2` 与本地 `./cheng`。
- `build_backend_driver.sh` 默认 `BACKEND_BUILD_DRIVER_LINKER=system`（可显式设 `self` 做兼容验证）、`GENERIC_MODE=dict`（可覆盖）；默认单次编译尝试超时 `60s`（`BACKEND_BUILD_DRIVER_TIMEOUT`）；默认最多尝试 3 个 stage0 候选（`BACKEND_BUILD_DRIVER_MAX_STAGE0_ATTEMPTS`，`0` 表示尝试全部）；默认关闭 stage1 编译 smoke（`BACKEND_BUILD_DRIVER_SMOKE=0`，需要时可设 `1` 开启）；可设 `BACKEND_BUILD_DRIVER_FORCE=1` 跳过 reuse 强制重建。默认允许“重建失败后保留已有健康 driver”，如需硬失败可设 `BACKEND_BUILD_DRIVER_NO_RECOVER=1`（新 smoke 阻断 gate 默认开启此开关）。自举超时/失败会直接失败并返回错误，不再回退 stage0。
- `build_backend_driver.sh` 自举编译会同步注入 `STAGE1_SKIP_SEM/OWNERSHIP/CPROFILE` 与 `CHENG_STAGE1_SKIP_SEM/OWNERSHIP/CPROFILE` 双口径环境变量，兼容历史 seed stage0 的前缀读取差异，避免误开语义检查导致自举失败。
- 在 `MM=orc BACKEND_LINKER=self` 口径下，`opt/opt2/multi-lto/ssa/debug`（以及可选 `stress`）gate 已统一固定串行口径（`BACKEND_MULTI=0`、`BACKEND_MULTI_FORCE=0`、`BACKEND_WHOLE_PROGRAM=1`）以保证稳定可复现。
- `verify_backend_concurrency_stress.sh` 默认跳过（`BACKEND_CONCURRENCY_STRESS_ENABLED=0`）；需要并发压力回归时显式设置 `BACKEND_CONCURRENCY_STRESS_ENABLED=1`。
- `verify_backend_stress.sh` 默认使用 stage1 smoke（`hello_puts`）循环运行（`BACKEND_STRESS_N`，默认 10），并对编译与单次运行施加 `60s` 超时（`BACKEND_STRESS_TIMEOUT`）。
- `verify_backend_concurrency_stress.sh` 在启用后（`BACKEND_CONCURRENCY_STRESS_ENABLED=1`）对编译与单次运行施加 `60s` 超时（`BACKEND_CONCURRENCY_TIMEOUT`）。
- `verify_backend_ffi_abi.sh` 在 `BACKEND_LINKER=self` 口径应直接通过，不依赖脚本内额外兜底逻辑。

后端热点定位（sample，可执行版）：
```bash
# 自动构建并执行 C 版采样器（wrapper: profile_backend_sample.sh）
sh src/tooling/profile_backend_sample.sh --preset:fullchain-cold --duration:20 --top:12 --kill-after-sample

# 直接指定命令；支持对子进程名 attach，避免采到脚本壳 wait4
sh src/tooling/profile_backend_sample.sh --duration:12 --attach-substr:cheng.stage2 -- \
  env CLEAN_CHENG_LOCAL=0 sh src/tooling/verify_fullchain_bootstrap.sh
```
说明：
- `src/tooling/profile_backend_sample.sh` 现在仅做包装：首次会调用 `src/tooling/build_profile_backend_sample.sh` 生成 `artifacts/bin/profile_backend_sample`（C 可执行文件）。
- 采样参数为命令行选项（`--preset/--duration/--interval-ms/--attach-substr/...`），不再依赖 shell 字符串拼接。
- preset 默认 `--attach-substr:cheng`，优先采样编译器子进程而非脚本父进程。
- `profile_backend_sample` 会从 sample 输出的 call graph 提取并按计数排序热点帧（过滤 `Thread_/main/start` 等噪声），便于快速定位热函数。
- `src/tooling/summarize_timeout_diag.sh` 可离线汇总 timeout 采样文件：
  - `sh src/tooling/summarize_timeout_diag.sh --dir:chengcache/backend_timeout_diag --latest:3 --top:12`
  - `sh src/tooling/summarize_timeout_diag.sh --file:chengcache/backend_timeout_diag/<file>.sample.txt --top:20`
- `src/tooling/summarize_stage1_timeout_diag.sh` 可离线汇总 stage1 fullspec 超时诊断：
  - `sh src/tooling/summarize_stage1_timeout_diag.sh --latest:3`
  - `sh src/tooling/summarize_stage1_timeout_diag.sh --dir:chengcache/stage1_fullspec_timeout_diag --latest:5`
- `sample` 想看到完整调用栈与符号名：
  - 需要后端生成“可 unwind 的栈帧”（FP 链）+ 可执行文件包含符号表；
  - self-linker 口径建议用 `BACKEND_LINKER_SYMTAB=all`（或 `--linker-symtab=all`）重建目标二进制，否则常见现象是 call graph 大量 `???`。
- `BACKEND_PROFILE=1` 可启用后端内建分段计时：编译时在 stderr 输出 `backend_profile\t<label>\tstep_ms=...\ttotal_ms=...`，用于补足 sample 无符号时的慢阶段定位。
- `UIR_PROFILE=1` 可启用 UIR 口径分段计时：编译时在 stderr 输出 `uir_profile\t<label>\tstep_ms=...\ttotal_ms=...`，并补充 `generics_report\t...`。
- `UIR_AGGRESSIVE=1` 会打开 `UIR_AGGRESSIVE` 路径中的增强 CFG/pass，便于产物一致性与编译路径回归对比（配合 `UIR_FULL_ITERS` 调节）；此外可通过 `UIR_OPT2_ITERS`、`UIR_OPT3_ITERS`、`UIR_OPT3_CLEANUP_ITERS`、`UIR_CFG_CANON_ITERS` 分别控制 `opt2/opt3` 的收敛与收尾清理轮次。
- `BACKEND_SSA` 已移除；设置后会在 `backend_driver` 直接报错退出（改用 `BACKEND_OPT_LEVEL`）。
- `STAGE1_PROFILE=1` 可启用 stage1 前端分段计时：在 stdout 输出 `[stage1] profile: lex=... load=... sem=... mono=... ownership=... total=...`，并附带 `[sem]/[mono]/[ownership]` 的细分统计，用于把 `backend_profile build_module` 拆到更具体阶段。
- `MIR_PROFILE` 已移除；设置后会在 `backend_driver` 直接报错退出（改用 `UIR_PROFILE=1`）。
- `sample` 若显示热点集中在 `strlen` 与 `get/hashMap`，优先检查 `src/std/strings.cheng`、`src/std/seqs.cheng`、`src/std/hashmaps.cheng` 以及 `src/backend/tooling/backend_driver.cheng`、`src/backend/uir/uir_internal/uir_core_builder.cheng` 的字符串判空冷路径实现（当前已落地字符串 `==` 的 `c_strcmp` 快路径、`hashMapStrEqKnownLen` 的 `cmpMem+NUL` 比较，以及 `driver_strIsEmpty` 与 UIR builder 判空快路径）。
- 2026-02-07 同口径 20s 采样：`_platform_strlen` top-stack 计数约 `6555 -> 5990`。
- 2026-02-08：修复 stage1 `loadTokenCache` 热路径反复 `strlen`（`sliceRangePlainKnownLen`），同口径下 `_platform_strlen` 不再主导 top-stack。
- 2026-02-08：Mach-O obj writer 将 `machoFindSymIndex` 由线性扫描改为 hash 索引，单模块 `emit=obj` 的 `single.emit_obj` 常见从 `~12s` 降到 `~3.6s`（以 `backend_driver.cheng` 编译为例）。
- 2026-02-08：`ptr_add` 改为 UIR intrinsic + 运行时 ptrmap 减少 `ptr_add` 调用后，selfhost strict 冷态 `total≈24-27s`（stage1≈11-13s，stage2≈12-13s；`SELF_OBJ_BOOTSTRAP_REUSE=0`）。
- 2026-02-08：byte 指针 load/store（`bool/char/i8/u8`）由 runtime helper（`load_bool/store_bool + memcpy`）改为 UIR 直接 `load/store`（带 `nil` 分支），减少 `_platform_memmove` 热度并降低编译时开销。
- 2026-02-08：运行时 ORC ptrmap 优化：默认 init cap 提升到 `65536`，ptr hash 简化并在 grow rehash 中 hoist `mask`，降低 `cheng_ptrmap_grow/cheng_ptr_hash` 热路径开销。
- 2026-02-08：`backendStripSpaces` 增加“无空白直接返回”快路径（避免大量 `strip` 分配），`backend_driver.cheng` 单模块 `emit=obj` 口径下 `build_module` 常见 `~11.3s -> ~10.4s`，端到端 `~14.4s -> ~13.3s`（以 `BACKEND_PROFILE/STAGE1_PROFILE/UIR_PROFILE` 输出为准）。
- 2026-02-09：修复 `profile_backend_sample` 的 attach 等待策略：当命中 root 自身或目标不 fork 子进程时不再长时间等待，避免 target 结束后采到 zombie 导致 `sample` 失败（exit=255）。
- 2026-02-09：`src/std/system_helpers_backend.cheng` 的 `cheng_ptr_hash` 忽略对齐位并减少一次乘法，降低 `cheng_ptr_hash/cheng_ptrmap_put` 热路径开销。
- 2026-02-09：`src/backend/uir/uir_internal/uir_core_types.cheng` 为 `typeAliases/objTypes/cstrs` 引入 `HashMapStrInt` 索引缓存（>=32 时启用），减少字符串等号比较的线性扫描；同机 `backend_driver.cheng` 单模块 `emit=obj` 常见 `~13.5s -> ~13.0s`（以 `/usr/bin/time` 为准）。

后端闭环验收（统一 ORC + system linker）：
```bash
MM=orc BACKEND_LINKER=system sh src/tooling/verify_backend_closedloop.sh
MM=orc BACKEND_LINKER=system BACKEND_RUN_FULLSPEC=1 sh src/tooling/verify_backend_closedloop.sh
MM=orc sh src/tooling/verify_backend_self_linker_riscv64.sh
```
说明：
- `verify_backend_closedloop.sh` 默认 `BACKEND_RUN_FULLSPEC=0`（不跑 fullspec）；显式设为 `1` 才开启 fullspec 编译+运行门禁。
- `verify_backend_closedloop.sh` 已纳入 `backend.spawn_api_gate`，对应脚本为 `src/tooling/verify_backend_spawn_api_gate.sh`（默认 API 禁 raw spawn、legacy 显式入口可用；当前 `fn()` 入口正向口径为 `spawn(entry)`）。
- `BACKEND_RUN_FULLSPEC=1` 默认使用 `examples/backend_closedloop_fullspec.cheng`；可用 `BACKEND_CLOSEDLOOP_FULLSPEC_INPUT` 覆盖输入文件。
- fullspec 编译默认口径为 `BACKEND_FULLSPEC_SKIP_SEM=1`、`BACKEND_FULLSPEC_GENERIC_MODE=dict`、`BACKEND_FULLSPEC_GENERIC_SPEC_BUDGET=0`、`BACKEND_FULLSPEC_SKIP_OWNERSHIP=1`、`BACKEND_FULLSPEC_VALIDATE=0`，并固定 `BACKEND_WHOLE_PROGRAM=1` 与 `BACKEND_CLOSEDLOOP_FULLSPEC_MULTI=0`（默认串行编译；需要并行时显式覆盖）。
- `verify_backend_closedloop.sh` 在 fullspec 编译后新增 `backend.closedloop_fullspec.symcheck`：若可用 `nm`，会阻断未解析 `seqBytesOf_T` 符号回归。
- `verify_backend_mm.sh` 默认运行 `mm_live_balance + mm_container_balance`；容器用例默认使用 `BACKEND_MM_CONTAINER_FRONTEND=stage1`。如需关闭容器用例可设 `BACKEND_MM_CONTAINER=0`。
- `verify_backend_multi.sh` 保持并行优先；并行失败会自动串行重试，避免偶发并行崩溃阻断闭环。
- `verify_backend_self_linker_riscv64.sh` 默认口径为 `MM=orc`，并优先复用 `artifacts/backend_selfhost_self_obj/cheng.stage2`（若存在）以避免 gate 内重建 driver 触发超时。

UIR float 回归：
```bash
sh src/tooling/verify_backend_float.sh
```
说明：
- `verify_backend_float.sh` 当前覆盖 `return_float64_ops`、`return_float32_roundtrip`、`return_float_mixed_int_cast`、`return_float_compare_cast`、`return_float32_arith_chain`；当前口径已验证 `f64/f32` 算术、比较与 `f64/f32 <-> int` 基础 cast 语义（向零截断）。

Linux AArch64 no-libc 独立验收（不接入默认 verify）：
```bash
BACKEND_ELF_PROFILE=nolibc sh src/tooling/verify_backend_nolibc_linux_aarch64.sh
```
说明：
- 静态验收（macOS 可跑）：检查 `elf64-littleaarch64`、无 `PT_INTERP`/`PT_DYNAMIC`、无 undefined symbols、无 `libc.so/ld-linux-aarch64` 字符串。

libp2p frontier 编译探针：
```bash
sh src/tooling/verify_libp2p_frontier.sh
```
说明：
- 脚本支持 `--help`；未知参数会直接报错退出。
- import 探针（`cheng_probe_conn_import` / `cheng_probe_multihash_import_only`）默认走 `stage1` 前端（`FRONTIER_IMPORT_FRONTEND=stage1`），超时默认 `30s`（`FRONTIER_IMPORT_TIMEOUT`）。
- 重量探针（`mdns_smoke` / `msquic_transport_smoke`）保持 `stage1` 前端，超时默认 `60s`（`FRONTIER_TIMEOUT`）。
- frontier 默认使用 `FRONTIER_GENERIC_MODE=dict`、`FRONTIER_GENERIC_SPEC_BUDGET=0`；生产闭环口径固定 `dict`，用于抑制重量依赖图的 monomorphize 放大。
- 可通过 `FRONTIER_SKIP_SEM` / `FRONTIER_SKIP_OWNERSHIP` / `FRONTIER_VALIDATE` 控制 stage1 口径（默认 `1/1/0`，用于加速 smoke）。
- frontier 默认开启超时诊断（`FRONTIER_TIMEOUT_DIAG=1`）：超时时自动采样并打印诊断路径（默认目录 `chengcache/libp2p_frontier/logs/timeout_diag`，采样时长 `FRONTIER_TIMEOUT_DIAG_SECONDS=5`）。
- frontier 默认开启超时摘要（`FRONTIER_TIMEOUT_DIAG_SUMMARY=1`，`FRONTIER_TIMEOUT_DIAG_SUMMARY_TOP=12`）。
- 日志与 summary 位于 `chengcache/libp2p_frontier/logs/`。
- 运行验收（仅 Linux aarch64 主机）：执行 `hello_puts/return_add/mm_live_balance` smoke。
- 跨仓口径说明：
  - `verify_libp2p_frontier.sh` 固定 `dict`，因为 `hybrid` 在 `mdns_smoke/msquic_transport_smoke` 上易触发 60s 超时，无法满足 60s 生产门禁。
  - `cheng-libp2p/scripts/verify.sh` 现已收敛为 `stable=dict + exe+run + 60s` 的默认闭环门禁；`full` 保留为诊断/对照口径（可用环境变量改 emit/timeout/compile-only）。
  - libp2p 生产闭环入口不再接受 `hybrid`。

libp2p 生产闭环一键门禁：
```bash
sh src/tooling/verify_libp2p_prod_closure.sh
```
说明：
- 顺序执行 `cheng-libp2p/scripts/verify.sh` 的 `stable/full`，并执行 `verify_libp2p_frontier.sh`。
- 默认 `build/run` 超时阈值为 `60s`（可通过 `LIBP2P_BUILD_TIMEOUT` / `LIBP2P_RUN_TIMEOUT` 覆盖）。
- 默认校验 `hybrid` 拒绝路径（`verify.sh` / `backend_build.sh` / `verify_libp2p_frontier.sh`）；可用 `LIBP2P_CHECK_HYBRID_REJECT=0` 关闭。

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
- 主 seed 口径为 backend 可执行 seed（优先 `artifacts/backend_seed/cheng.stage2`，也可通过 `SELF_OBJ_BOOTSTRAP_STAGE0` 指定）。
- `src/stage1/frontend_bootstrap.seed.c` 必须不存在。
- `verify.sh` 与 `verify_backend_closedloop.sh` 会执行该门禁。
- `verify.sh` 默认导出 `BACKEND_PROD_GATE_TIMEOUT=60`、`BACKEND_PROD_SELFHOST_TIMEOUT=120`、`BACKEND_PROD_TIMEOUT_DIAG=1`、`BACKEND_RUN_SELFHOST=0`；可通过 `VERIFY_LIBP2P_FRONTIER=1` 追加 `libp2p frontier` 探针门禁。
- `verify.sh` 默认同时导出 `BACKEND_PROD_TIMEOUT_DIAG_SUMMARY=1` 与 `FRONTIER_TIMEOUT_DIAG_SUMMARY=1`（超时后自动打印热点摘要）。
- `verify.sh` 的 `chengc` smoke case 默认固定 `BACKEND_DRIVER=artifacts/backend_seed/cheng.stage2`；可用 `VERIFY_CHENGC_DRIVER=<path>` 覆盖，或 `VERIFY_USE_STABLE_DRIVERS=0` 关闭稳定 driver 注入。
- `verify.sh` 轻量 case 默认使用 `VERIFY_FRONTEND=stage1`。
- `list_comp` 与 `pkg_import_srcroot` 默认走 `build` 模式（快速 obj 编译校验，不再默认跳过）：
  - `VERIFY_LIST_COMP_MODE=build|run|skip`（兼容 `VERIFY_LIST_COMP=1` -> `run`）。
  - `VERIFY_PKG_IMPORT_MODE=build|run|skip`（兼容 `VERIFY_PKG_IMPORT=1` -> `run`）。
- `pkg_import_srcroot` 的 `build` 模式默认使用 `stage1` + `skip_sem/skip_ownership` + `dict`（可通过 `VERIFY_PKG_IMPORT_BUILD_*` 覆盖）；`run` 模式默认 `VERIFY_PKG_IMPORT_FRONTEND=stage1`。

Cheng skill 一致性门禁：
```bash
sh src/tooling/verify_cheng_skill_consistency.sh
```
说明：
- 扫描 `docs/cheng-skill/` 与 `~/.codex/skills/cheng语言` 的关键文件漂移，并检查禁用语法。
- 默认执行最小编译+运行抽样；可用 `SKILL_COMPILE=0` 只做文档检查。
- 编译抽样默认优先复用 `artifacts/backend_seed/cheng.stage2`（其次 `dist/releases/current/cheng`、`artifacts/backend_driver/cheng`），并使用 `stage1` 前端口径（`SKILL_FRONTEND=stage1`）。
- 可用 `SKILL_DRIVER=<path>` 显式指定 gate driver；仅当 `SKILL_DRIVER_ALLOW_SELFHOST=1` 时才允许回退 `backend_selfhost_self_obj/*`。
- `verify.sh` 默认执行该门禁；可用 `VERIFY_SKILL=0` 临时跳过。

## 包管理闭环

常用脚本：
- `src/tooling/cheng_pkg_pack.sh`：把包目录打成 `.tar.gz` 快照。
- `src/tooling/cheng_pkg_publish.sh`：打包 -> 上传存储 -> 发布 registry 快照。
- `src/tooling/cheng_pkg_fetch.sh`：根据 lock 拉取快照并输出 `PKG_ROOTS`。
- `src/tooling/cheng_pkg_source.cheng`：源码清单（manifest）构建/发布/本地拉取（local-only core binary）。

示例：
```bash
sh src/tooling/cheng_pkg_publish.sh --src:/Users/lbcheng/.cheng-packages/cheng-libp2p --package:pkg://cheng/libp2p \
  --author:node:alice --channel:edge --epoch:1 --priv:keys/alice.priv

./cheng_registry resolve --package:pkg://cheng/libp2p --channel:edge --registry:build/cheng_registry/registry.jsonl
./cheng_pkg resolve --manifest:docs/cheng-package-manifest.toml --registry:build/cheng_registry/registry.jsonl \
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

以下构建脚本支持 `--mm:<orc>`（兼容 `--orc`），并映射 `MM=orc`：

- `src/tooling/build_unimaker_desktop.sh`
- `src/tooling/package_ide.sh`
- `src/tooling/package_unimaker_desktop.sh`
- `src/tooling/build_mobile_export.sh`
- `src/tooling/mobile_ci_android.sh`
- `src/tooling/mobile_ci_ios.sh`
- `src/tooling/mobile_ci_harmony.sh`

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
  - `--seed:<path>`/`SELF_OBJ_BOOTSTRAP_STAGE0` 可显式指定 stage0 driver。
  - 未指定时会按顺序自动探测：`BACKEND_DRIVER` -> `artifacts/backend_driver/cheng` -> `dist/releases/current/cheng` -> `artifacts/backend_seed/cheng.stage2` -> `artifacts/backend_selfhost_self_obj/cheng.stage2` -> `artifacts/backend_selfhost_self_obj/cheng.stage1` -> 可运行 `./cheng`；若均不可用，再尝试 `src/tooling/build_backend_driver.sh --name:artifacts/backend_driver/cheng`。
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
- `chengc.sh`（以及兼容壳 `chengb.sh`）固定 `MM=orc`；传入 `--mm:off`/`MM=off` 会直接报错。
