# Cheng 工具链（Backend-only）

> 迁移说明（2026-02）：仓库已移除 `src/tooling/*.sh` 与 `scripts/*.sh` 入口壳；逻辑体统一收敛到全局可执行入口 `cheng_tooling` 的内嵌命令表（`src/tooling/cheng_tooling_embedded_inline.cheng`）。
> 统一执行口径：`cheng_tooling <id>`（例如 `cheng_tooling verify_backend_closedloop`）。
> 零脚本门禁：`verify_backend_zero_script_closure` 强制仓库不保留 `.sh` 入口文件。
> Stage1 fixed=0 门禁：`verify_backend_stage1_fixed0_envs` 强制 `STAGE1_SKIP_SEM/STAGE1_SKIP_OWNERSHIP` 已移除，且 `STAGE1_SEM_FIXED_0/STAGE1_OWNERSHIP_FIXED_0` 只能为 `0`。
> 运行时内嵌查询口径（2026-02-25）：`cheng_tooling embedded-ids/embedded-text` 直接调用 `tooling/cheng_tooling_embedded_inline`；内嵌 map 重写统一走 `cheng_tooling embedded-map-rewrite`（仓库已移除 Python query 脚本）。

## 进程通知（2026-02-25：单 Driver 收口）

> 本节为高优先级口径，优先于下文历史描述。用于通知并发开发/值班进程统一行为。
> 通知原文：`docs/process-notice-backend-driver-unification-2026-02-25.md`。

1. Canonical backend driver 固定为 `artifacts/backend_driver/cheng`。
2. `build-backend-driver` 输出仍收口到 canonical driver（默认 `artifacts/backend_driver/cheng`）；`driver-path` 固定返回 canonical driver。`build-backend-driver` 的 stage0 选择则允许在内部优先探测健康的 `artifacts/backend_selfhost_self_obj/cheng.stage2`。
3. `driver-path/compile` 不再支持 stage0 覆盖；固定 canonical driver。
4. `build-backend-driver` 的 `.attempt/.tmp.linkobj/.objs.lock` 临时产物迁移到 `chengcache/backend_driver_build_tmp`，不再写入 `artifacts/backend_driver` 顶层。
5. 新增清理命令：`cheng_tooling cleanup-backend-driver-history [--dry-run]`，仅保留 `cheng` 与 `cheng.objs`。
6. 构建后默认自动清理历史 `cheng*` 变体（`BACKEND_BUILD_DRIVER_AUTO_CLEAN_HISTORY=1`，可设 `0` 关闭）。

当前仓库提供 backend-only 主链路：

1. **`cheng` 子命令（canonical）**：后端主入口（`cheng_tooling cheng`，支持 `--emit:exe`）。
2. **单一 driver**：`cheng/release-compile` 统一使用 `artifacts/backend_driver/cheng`。
3. **并行与增量**：tooling 入口默认 `BACKEND_INCREMENTAL=1`；`chengc` 在 dev 轨默认并行（`CHENGC_DEV_MULTI_DEFAULT=1`，release 默认 `CHENGC_RELEASE_MULTI_DEFAULT=0`）；`BACKEND_MULTI_MODULE_CACHE` 默认 `0`，且 stable driver 当前固定禁用 module-cache load 路径（不作为生产配置面）。
4. **完全自举**：`emit=exe + self-link` 自举（`cheng_tooling bootstrap-pure`）。
5. **入口收敛**：所有脚本 ID 统一通过 `cheng_tooling <id>` 分发，不再保留按 ID 的 shell 包装文件。

## 零脚本写入规范

1. required 生产闭环的默认入口必须是 `cheng_tooling` / backend driver 的原生命令路由，不允许把 `sh src/tooling/<id>.sh` 当成发布主路径。
2. 新增验证/构建逻辑时，语义判定、contract 组装、报告字段和默认分发必须优先写入 Cheng 原生实现：`cheng_tooling` 命令、本仓库 Cheng 模块或内嵌命令表。
3. shell 只允许承担 `timeout`、`sample`、`trap`、临时目录、进程编排和兼容 launcher；不得成为唯一语义来源。
4. `verify_backend_zero_script_closure` / `verify_backend_zero_script_residual` 的目标是“required 语义逻辑零 shell 依赖”，不是要求仓库当下立即零 `.sh` 文件。
5. `cheng` 核心入口必须原生路由到 `cheng_tooling` 内部命令（`compile/chengc` 入口已移除）。

## cheng_tooling（统一脚本可执行入口）

```bash
# 列出所有可运行脚本 ID（内嵌于 cheng_tooling）
cheng_tooling list

# 按脚本 ID 运行（支持参数透传）
cheng_tooling run backend_prod_closure --help

# 简写：省略 run
cheng_tooling backend_prod_closure --help

# 构建全局多调用二进制（dev-only self-link + in-memory）
cheng_tooling build-global --out:artifacts/tooling_cmd/cheng_tooling --linker:self

# 安装多调用链接：一个二进制覆盖全部脚本入口
artifacts/tooling_cmd/cheng_tooling install \
  --dir:artifacts/tooling_cmd/bin \
  --bin:artifacts/tooling_cmd/cheng_tooling \
  --mode:copy \
  --manifest:artifacts/tooling_cmd/tooling_multicall_manifest.tsv \
  --force

# 通过多调用链接直接执行脚本 ID（无需再写 run）
artifacts/tooling_cmd/bin/backend_prod_closure --help

# 按需子集合并（可重复 --only / --exclude），用于拆分“一个或多个”全局二进制入口
artifacts/tooling_cmd/cheng_tooling install \
  --dir:artifacts/tooling_cmd/bin_core \
  --bin:artifacts/tooling_cmd/cheng_tooling \
  --mode:copy \
  --only:cheng_tooling \
  --only:backend_prod_closure \
  --only:verify_backend_closedloop \
  --force

# 按 profile 一次产出全局二进制包（full）
artifacts/tooling_cmd/cheng_tooling bundle \
  --out-dir:artifacts/tooling_bundle \
  --profile:full \
  --mode:copy \
  --linker:self \
  --force

# 查看/清理 stage0 相关 UE(orphan) 残留
artifacts/tooling_cmd/cheng_tooling stage0-ue-status --limit:8
artifacts/tooling_cmd/cheng_tooling stage0-ue-clean --dry-run
artifacts/tooling_cmd/cheng_tooling stage0-ue-clean --strict:1
```

说明：
- `cheng_tooling ...` 会自动构建并复用 `artifacts/tooling_cmd/cheng_tooling`（源码：`src/tooling/cheng_tooling.cheng`）；默认 `TOOLING_LINKER=self`，非发布链路固定走 dev in-memory 口径。
- `sync-global` 已下线；统一使用 `build-global --out:artifacts/tooling_cmd/cheng_tooling` 更新 canonical wrapper/launcher surface。若要显式重编 core Mach-O，使用 `build-global --out:artifacts/tooling_bundle/core/cheng_tooling_global --linker:self`。
- `build-global/bundle` 仅支持 `--linker:self`；发布链路统一使用 `release-compile` / `backend-prod-publish`。
- `build-global` 默认启用安全首编（`TOOLING_BUILD_GLOBAL_SAFE_FIRST=1`），优先规避 stage0 首轮 `rc=139`；可设 `TOOLING_BUILD_GLOBAL_SAFE_FIRST=0` 恢复先高优化再按崩溃重试。
- `build-global` 的 fast-path 仅在“现有产物健康且源码不比产物新”时复用；源码更新会强制重编。
- `build-global` 在参考支持平台 `darwin/arm64` 上默认要求自链接/dev 路径直接成功，并默认关闭 release-system fallback（`TOOLING_BUILD_GLOBAL_RELEASE_FALLBACK=0`）。对 canonical 默认目标 `artifacts/tooling_cmd/cheng_tooling`，当前会走 source-managed wrapper/launcher sync，不再强制把 monolithic `src/tooling/cheng_tooling.cheng` 自链接到 wrapper 路径；显式 core 目标仍保留 current-source 自举语义。其他目标继续保持保守 fallback 默认值，避免 `SIGILL/Segfault` 级别坏产物覆盖 canonical。
- `src/tooling/cheng_tooling_embedded_scripts/cheng_tooling_canonical.sh` 是 canonical wrapper 的 source 模板；repo wrapper 和 `build-global` 默认面都会把它同步到 `artifacts/tooling_cmd/cheng_tooling`。
- repo wrapper 里的 repo-local script inventory / install-list-bundle backfill / canonical wrapper sync helper 已拆到 [cheng_tooling_repo_gate_support.sh](/Users/lbcheng/cheng-lang/src/tooling/cheng_tooling_embedded_scripts/cheng_tooling_repo_gate_support.sh)，避免继续把这层 shell 兼容逻辑堆回 [cheng_tooling.sh](/Users/lbcheng/cheng-lang/src/tooling/cheng_tooling_embedded_scripts/cheng_tooling.sh)。
- canonical wrapper sync 的 Cheng 实现已从 monolith 拆到 [cheng_tooling_buildglobal_wrapper_sync.cheng](/Users/lbcheng/cheng-lang/src/tooling/cheng_tooling_buildglobal_wrapper_sync.cheng)，避免继续把 `build-global` 默认面 helper 堆回 [cheng_tooling.cheng](/Users/lbcheng/cheng-lang/src/tooling/cheng_tooling.cheng)。
- `install` 的链接/manifest 执行层已从 monolith 拆到 [cheng_tooling_install_links.cheng](/Users/lbcheng/cheng-lang/src/tooling/cheng_tooling_install_links.cheng)；`bundle` 目前保留 profile/path/build 分发在主文件，底层安装落到这个子模块。
- `bundle` 的参数解析已从 monolith 拆到 [cheng_tooling_bundle_args.cheng](/Users/lbcheng/cheng-lang/src/tooling/cheng_tooling_bundle_args.cheng)；主文件只保留 `buildGlobal + installLinks` 执行层，不再维护两份重复的 `bundle` 参数循环。
- repo 内直接执行 `src/tooling/cheng_tooling_embedded_scripts/cheng_tooling.sh` 时，任何“存在非空同名内嵌脚本且不是自 trampoline wrapper”的入口都会优先走 repo-local 源码脚本，不等待 canonical `artifacts/tooling_cmd/cheng_tooling` 重编完成；典型包括 `verify`、`verify_tooling_cmdline`、`verify_backend_stage1_fixed0_envs`、`verify_backend_string_literal_regression`、`verify_backend_default_output_safety`、`verify_new_expr_surface`、`verify_backend_string_abi_contract`、`verify_backend_dot_lowering_contract`、`verify_backend_selfhost_currentsrc_proof`、`chengc`。标准库基线入口已迁到 `cheng_tooling` 原生命令，不再依赖 repo-local std gate shell。
- `src/tooling/cheng_tooling_embedded_scripts/cheng_tooling_real.sh` 是 `.real` launcher 的 source 模板；repo wrapper 每次启动都会把它同步到 `artifacts/tooling_cmd/cheng_tooling.real`。
- `src/tooling/cheng_tooling_embedded_scripts/cheng_tooling_real_bin.sh` 是 `.real.bin` launcher 的 source 模板；repo wrapper 每次启动都会把它同步到 `artifacts/tooling_cmd/cheng_tooling.real.bin`。
- direct `.real` / direct `.real.bin` 默认都会落到 `artifacts/tooling_bundle/core/cheng_tooling_global` 这份稳定 core tooling 产物；`.real` 额外只保留 `126/127/139` 到 core/full 的回退。`223` 现统一视为 `deterministic_exit_223`，会直接报 `child exited 223 directly (non-POSIX signal)`，不再自动 fallback/skip。
- 当前 canonical `artifacts/tooling_cmd/cheng_tooling` 也会把这类脚本入口直通到 repo-local 最新实现；`.real` 和 `.real.bin` launcher 在仓库内也都会保守回退到 `src/tooling/cheng_tooling_embedded_scripts/<id>.sh`，所以 direct `.real.bin verify_backend_stage1_fixed0_envs`、`verify_backend_string_literal_regression`、`verify_backend_default_output_safety`、`verify_new_expr_surface`、`verify_backend_string_abi_contract`、`verify_backend_dot_lowering_contract`、`verify_backend_selfhost_currentsrc_proof` 这类 script-backed gate 现在也能工作。`verify_std_*` / `build_std_perf_baseline` 已改为 native route + native wrapper surface。
- wrapper 的 `list` / `embedded-ids` 现在也会把 repo-local 非空 script-backed gate 合并进输出，不需要等 fresh tooling binary 重编后才看见新增入口；当前包括 `verify_new_expr_surface`、`verify_backend_string_abi_contract`、`verify_backend_dot_lowering_contract`、`verify_backend_selfhost_currentsrc_proof`。
- 这些 script-backed gate 属于迁移中的兼容面，不代表规范主口径。后续重写 gate 时，应优先把语义核心搬到 Cheng/native 路径，再决定是否保留 shell 薄包装。
- 聚合器仅在 `main(argc, argv)` 的编译器内部 C ABI 入口桥接阶段使用指针参数；构建时显式放宽 `STAGE1_NO_POINTERS_NON_C_ABI=0` / `STAGE1_NO_POINTERS_NON_C_ABI_INTERNAL=0` 仅用于 tooling/runtime/internal 构建路径。用户公开编译入口不再暴露 `--abi`/`--std-noptr`/`--noptr-non-c-abi` 这组开关，用户源码 `@importc/@exportc` surface 默认按 no-pointer 生产口径执行。
- 可通过 `TOOLING_BIN` 覆盖可执行路径，通过 `TOOLING_BUILD_GLOBAL_EAGER_REBUILD=1` 强制重建。
- `cheng_tooling` 支持 multicall：当 `argv0` 是脚本 ID（例如 `backend_prod_closure`）时会自动分发到内嵌脚本负载。
- `install` 子命令可把全部 tooling 入口（内嵌脚本表）合并安装为“一个全局二进制 + 多个命令入口”的形态；支持重复 `--only:<id>` / `--exclude:<id>` 做按需子集安装，并支持 `--mode:symlink|hardlink|copy` 选择入口物化方式。默认 `--mode:hardlink`（可显式改为 `copy`）。
- 对 repo-local script-backed gate，wrapper 的 `install` 现在会在底层 core binary 安装完成后补写轻量 exec wrapper；当前已覆盖 `verify_new_expr_surface` 与 `verify_backend_selfhost_currentsrc_proof`，所以它们也会出现在安装目录里并能直接 `--help`/执行，不需要等 fresh tooling binary 重编。
- 同样地，wrapper 的 `bundle --profile:full` 现在也会在 `full/bin` 与 `full/manifest.tsv` 里补齐这类 repo-local script-backed gate；当前会复制并 root-修正 `verify_new_expr_surface` 与 `verify_backend_selfhost_currentsrc_proof` 的脚本体，所以 bundled gate 不再借道 canonical wrapper。
- `bundle` 子命令当前仅保留 `full` profile：tooling 全量入口（内嵌脚本表）。
- `cheng_tooling <id> [args...]`：统一执行入口解析器；支持 `TOOLING_EXEC_BUNDLE_PROFILE=full`（默认 `full`）与 `TOOLING_EXEC_BUNDLE_BIN_DIR=<dir>` 指定入口目录。
- `TOOLING_EXEC_REQUIRE_BUNDLE=1` 可禁用 global fallback（bundle 缺失直接失败），用于生产闭环“全局二进制分发”强约束。
- `cheng_tooling` 默认开启运行时兼容镜像（`TOOLING_EMBEDDED_INLINE_MIRROR=1`）：会把内嵌脚本临时回填到 `src/tooling/tooling_inline/*.inline` 与 `src/tooling/verify_inline/*.inline`，用于兼容仍引用历史路径的脚本实现；脚本结束后自动清理。
- `verify.sh`、`verify_backend_selfhost_nightly` 与 `backend_prod_closure` 已接入该解析器；其中 `backend_prod_closure` 默认 `profile=full` + `require_bundle=1` + `bundle_auto_build=0`（零脚本生产闭环）。
- 回归脚本：`cheng_tooling verify_tooling_cmdline`（同时覆盖 repo wrapper、canonical wrapper、direct `.real` launcher 与 multicall 的 `verify` / `chengc` / `backend_prod_closure`；报告：`artifacts/tooling_cmdline/tooling_cmdline.report.txt`）。

## cheng（canonical）

```bash
cheng_tooling cheng examples/stage1_codegen_fullspec.cheng --jobs:8
./stage1_codegen_fullspec
```

说明：
- 可通过 `--name:<exeName>` 改输出二进制名。
- 可选 `--mm:<orc>`（或 `--orc`）显式设置内存模型；默认并固定 `orc`。
- no-pointer / `v2_noptr` 策略已内建为默认口径，不再暴露 `--abi` 或相关 no-pointer 开关。
- 包管理器接入：可选传入 `--manifest/--lock/--registry`，会生成构建元数据 `chengcache/<name>.buildmeta.toml`。
- 可选 `--pkg-cache:<dir>` 指定包缓存目录（默认 `chengcache/packages`，或 `PKG_CACHE` 环境变量）。
- 当 lock 存在时会拉取依赖包并设置 `PKG_ROOTS` 供 `cheng/<pkg>/...` 域名导入使用；生产包根以 `src/` 为模块根（`cheng/<pkg>/<path>` -> `<pkgroot>/src/<path>.cheng`）。`PKG_ROOTS` 可指向包根列表或容器根（如 `~/.cheng-packages`，解析会优先尝试 `cheng-<pkg>`）。
- 源码直发拉取可通过 `--source-peer`/`--source-listen` 或环境变量 `PKG_SOURCE_PEERS`/`PKG_SOURCE_LISTEN` 指定源地址。
- 依赖拉取模式/peer：`PKG_MODE=local|p2p`/`PKG_PEERS`。
- 供应链默认强制：`PKG_HTTP_FALLBACK=0`、`PKG_REQUIRE_SIGNATURE=1`、`PKG_REQUIRE_REGISTRY_MATCH=1`；`cheng_pkg_fetch` 在拉取前会执行 `lock-verify`，任一校验失败即阻断。
- 注册中心元数据：当提供 `--package/--channel` 时，会写入 `[snapshot]`（`cid/author_id/signature/pub_key`）。
- 构建期校验：可加 `--verify`（可选 `--ledger:<path>`）自动校验 buildmeta/lock/pkgmeta/snapshot。
- `cheng` 入口（canonical）支持 `--emit:exe`；`--emit-obj/--obj-out/--backend:obj` 已在生产链路移除并会直接报错。`compile/chengc` 已移除并返回 `rc=2`；`cheng` 固定 dev-only：固定注入 `BACKEND_BUILD_TRACK=dev`、`BACKEND_FAST_DEV_PROFILE=1`、`BACKEND_STAGE1_PARSE_MODE=outline`、`BACKEND_FN_SCHED=ws`、`BACKEND_DIRECT_EXE=1`、`BACKEND_LINKERLESS_INMEM=1`、`BACKEND_FAST_FALLBACK_ALLOW=0`，并显式透传 `BACKEND_JOBS/BACKEND_FN_JOBS`（未设置时走 host 并行默认），同时拒绝 `--release` 与 `BACKEND_BUILD_TRACK=release` 抬升。
- 发布编译入口改为 `release-compile`：固定 `BACKEND_BUILD_TRACK=release`、`BACKEND_LINKER=system`、`BACKEND_STAGE1_PARSE_MODE=full`、`BACKEND_FN_SCHED=ws`、`BACKEND_DIRECT_EXE=0`、`BACKEND_LINKERLESS_INMEM=0`、`BACKEND_FAST_FALLBACK_ALLOW=0`、`BACKEND_NO_RUNTIME_C=0`、`BACKEND_OPT_LEVEL=3`（默认 `BACKEND_RELEASE_CFLAGS/BACKEND_RELEASE_LDFLAGS=-O3 -flto`），并显式透传 `BACKEND_JOBS/BACKEND_FN_JOBS`。
- `release-compile` 支持 `--emit:exe|shared|static`。其中 `shared/static` 走 release object-first 打包链（后端先产 `obj`，再由系统工具链打包库；默认附带 runtime C object，可用 `BACKEND_RELEASE_LIBRARY_INCLUDE_RUNTIME=0` 关闭）。
- `cheng/release-compile` 都不接受 `--linker:*`，也不接受 `BACKEND_LINKER` 环境覆盖；`cheng` 固定 `self-link + direct-exe`，`release-compile` 固定 `system-link`。
- 并发公开契约：`BACKEND_JOBS` 是唯一公开 worker 数控制面；`BACKEND_FN_SCHED=serial` 仅保留给内部诊断、perf 对照与低内存 bring-up。
- `--emit:shared|static` 不允许 `--run/--run:*`；违规返回 `rc=2`。
- `cheng` 支持常驻编译 worker：`CHENGC_DAEMON=1` 会把请求转发到 `chengc_daemon`（`cheng_tooling chengc_daemon start|status|stop`）；用于减少频繁冷启动开销。
- `emit=obj` 不再用于非 release 可执行构建；dev/闭环主路径固定内存直出 `emit=exe`。`emit=obj` 仅保留内部 `allow-no-main` 工件生成通道（需显式 `BACKEND_INTERNAL_ALLOW_EMIT_OBJ=1`）。
- 运行入口双模：`--run` 默认 `host runner`（Dev），可显式用 `--run:host`；`--run:file` 保留“先产 exe 再执行”的兼容路径。host runner 关键变量：`BACKEND_HOTPATCH_MODE=trampoline`、`BACKEND_HOSTRUNNER_POOL_MB`（默认 `512`）、`BACKEND_HOSTRUNNER_PAGE_POLICY=rw_rx`、`BACKEND_HOTPATCH_LAYOUT_HASH_MODE=full_program`、`BACKEND_HOTPATCH_ON_LAYOUT_CHANGE=restart`、`BACKEND_HOTPATCH_TARGET_PLATFORMS=darwin,linux`。
- system linker 自动选择器：`cheng_tooling resolve_system_linker` 按 `BACKEND_SYSTEM_LINKER_PRIORITY`（默认 `mold,lld,default`）解析并追加 `-fuse-ld=...`；若显式设置 `BACKEND_LD` 或手写 `-fuse-ld=...`，选择器不覆盖。
- Linux AArch64 可选 no-libc profile：`BACKEND_ELF_PROFILE=nolibc` 且 `BACKEND_LINKER=self` 时，`chengc` 会切换到 `src/std/system_helpers_backend_nolibc_linux_aarch64.cheng`，并走无 `PT_INTERP`/`PT_DYNAMIC` 的静态链接口径；默认 profile 行为不变。
- 后端 driver 层（`src/backend/tooling/backend_driver.cheng`）：生产默认 `BACKEND_EMIT=exe`，`BACKEND_LINK_OBJS` 与 `emit=obj` 旧入口已移除；`emit=obj` 在非 release 可执行构建硬禁用，内部仅允许 `allow-no-main` 工件生成；`BACKEND_LINKER=system|self` 负责最终可执行物链接。
- 自举稳定性口径：`src/backend/tooling/backend_driver.cheng` 仅保留 native dispatch；`build-backend-driver` 的 seal/full 轨命中 stage0 崩溃或硬失败时直接按严格失败返回，不再走 delegate wrapper 回退链路。
- `build-backend-driver` seal/full rebuild 固定要求 `BACKEND_BUILD_DRIVER_REBUILD_SKIP_GLOBAL_INIT=0`（非 0 会直接拒绝），并默认 `BACKEND_BUILD_DRIVER_REBUILD_DISABLE_DUP_SCAN=0`。
- stage0 cstring 兼容前缀改为二进制 marker 探测，命中“removed config”新驱动时不再注入 `BACKEND_ENABLE_CSTRING_LOWERING/*` 旧环境变量。
- 后端 driver 默认参数：`BACKEND_EMIT=exe`、`BACKEND_TARGET=auto`；并行/增量由 `chengc` 入口统一控制。
- `backend_driver_path` 默认使用稳定 driver `artifacts/backend_driver/cheng`，并附带 stage0 编译探针；首选不健康时直接阻断。
- 后端 IR 入口：默认 `BACKEND_IR=uir`（仅支持 `uir`）；自举/构建脚本固定 `GENERIC_MODE=dict`、`GENERIC_SPEC_BUDGET=0`、`GENERIC_LOWERING=mir_dict`。
  - MIR 借用策略开关：`BORROW_IR=mir|stage1`（默认 `mir`）。泛型 lowering 固定 `mir_dict`。
- 后端优化默认：`BACKEND_OPT_LEVEL` 未设置时默认 `2`（若显式设置 `BACKEND_OPT=1` 则仍按 `1`）。
- SIMD 默认策略：`UIR_SIMD` 未设置时按优化级自动选择（`optLevel>=3` 开启，否则关闭）；可用 `UIR_SIMD=0|1` 强制覆盖。
- UIR 调优：`UIR_AGGRESSIVE=1` 打开激进闭环 pass（可结合 `UIR_FULL_ITERS` 控制轮次），`UIR_OPT2_ITERS`/`UIR_OPT3_ITERS`/`UIR_OPT3_CLEANUP_ITERS` 与 `UIR_CFG_CANON_ITERS` 可独立调高 `opt2/opt3` 与 CFG 收敛轮次（默认 `5/4/3/1`，范围分别为 `1..32/1..32/1..32/1..16`），`UIR_PROFILE=1` 打印每一轮 `uir_profile` 计时。
- `verify_backend_opt2`/`verify_backend_opt3` 支持额外优化环境与 fixture 透传：`UIR_SIMD=1`、`UIR_SIMD_MAX_WIDTH=<N>`、`UIR_SIMD_POLICY=<autovec|copy|loop|slp|none>`（默认 `autovec`）、`UIR_INLINE_ITERS`（默认 `4`）、`UIR_OPT2_FIXTURES`、`UIR_OPT3_FIXTURES`（均支持换行或分号分隔 fixture 列表）。
- `opt2` 默认新增 `SSU + NJVL-lite` 分析链路：`UIR_SSU=1`、`UIR_NOALIAS_NJVL_LITE=1`（均可显式设 `0` 关闭）；`noalias_report` 追加 `unknown_slot_clobbers`、`unknown_global_clobbers`、`kill_events` 字段，`ssu_report` 输出 `dup_candidates/move_candidates/use_version_max`。
- 新增 SIMD 阻断门禁：`cheng_tooling verify_backend_simd`（校验 `simd on/off` 语义一致 + `simd on` 双次可执行运行结果一致）。
  - 当 canonical driver 暂缺 `simd_loop_report/simd_slp_report` runtime 输出时，该 gate 会退回 source-surface 证明并在报告写入 `runtime_report_mode=source_fallback`；如需强制 runtime 日志必须包含 SIMD report，设置 `BACKEND_SIMD_STRICT_RUNTIME_REPORT=1`。
- 新增生产引用护栏：`cheng_tooling verify_backend_no_legacy_refs`（阻断生产入口直接依赖 legacy import / 旧 env）。
  - machine/UIR 收敛补充：该门禁额外阻断 internal 与 non-internal 的 `Mir/Lir`、`lr/lc/lo` 旧命名回退，以及 `obj/select_internal` 直接导入 `machine_internal`，要求走 `machine_types` + `machineReg/machineCond/machineOp` surface。
  - 生产闭环可改用 `cheng_tooling backend_prod_closure --uir-aggressive --uir-aggressive-iters:<n>` 覆盖 `BACKEND_PROD_UIR_FULL_ITERS`（默认 `2`，范围 `1..16`）。
  - 生产闭环可加 `--uir-stability`，触发 `backend.uir_stability` 闸口；对应脚本 `cheng_tooling verify_backend_uir_stability`。
  - `BACKEND_PROD_TARGET=<triple>` 可固定 `backend.opt/opt2/opt3/simd/uir_stability` 的目标三元组；未设置时会忽略外部非 darwin `BACKEND_TARGET` 并回退到 `arm64-apple-darwin`。
  - 也可直接改写 `opt2/opt3` 轮次：`--uir-opt2-iters:<n>`、`--uir-opt3-iters:<n>`、`--uir-opt3-cleanup-iters:<n>`、`--uir-cfg-canon-iters:<n>`，以及 SIMD/内联：`--uir-simd`（或 `--no-uir-simd`）、`--uir-simd-max-width:<n>`、`--uir-simd-policy:<policy>`、`--uir-inline-iters:<n>`。
- UIR 观测：`UIR_PROFILE=1` 输出 `uir_profile\t<label>\tstep_ms=...\ttotal_ms=...`，并在 `build_module` 后输出 `generics_report\tir=uir\tmode=...\tspec_budget=...\tborrow_ir=...\tgeneric_lowering=...`。
- PAR-01 观测基线：`cheng_tooling build_backend_profile_schema` 与 `cheng_tooling build_backend_profile_baseline` 生成冻结快照（默认写入 `src/tooling/backend_profile_schema.env` 与 `src/tooling/backend_profile_baseline.env`）；`cheng_tooling verify_backend_profile_schema` 与 `cheng_tooling verify_backend_profile_baseline` 校验 schema/baseline 漂移并输出 `artifacts/backend_profile/*.report.txt`。
- 契约/基线一键重建：`cheng_tooling build-backend-baselines`（单次调用顺序执行 rawptr/mem/dod/string ABI/dot-lowering/profile schema/profile baseline 七项）。
- 生产技术债总表：`docs/backend-techdebt-matrix.md` 统一记录 `SoA`、`DOD`、`NoAlias`、`Egraph`、`函数并行`、`str ABI/cstring`、`dot-lowering` 七个面；其中 `str ABI/cstring` 与 `dot-lowering` 现在都已进入 `backend_prod_closure` required。
- 结项口径：以上七个面的文档、账本与 closure 分层债已清；剩余实现差距或专用 perf 前提统一只记在各自“升级条件”，不再记作当前漂移。
- PAR-01 契约冻结：`cheng_tooling build_backend_mem_contract` 生成 `src/tooling/backend_mem_contract.env`；`cheng_tooling verify_backend_mem_contract` 校验 `docs/backend-mem-hotpatch-contract.md`（MemImage/PatchMeta）与闭环接入点漂移，输出 `artifacts/backend_mem_contract/*.report.txt`。
- DOPAR-01 DOD 契约冻结：`cheng_tooling build_backend_dod_contract` 生成 `src/tooling/backend_dod_contract.env`；`cheng_tooling verify_backend_dod_contract` 校验 `docs/cheng-plan-full.md`（DOD SoA/Arena/index 契约）与闭环接入点漂移，输出 `artifacts/backend_dod_contract/*.report.txt`。
- RPSPAR-01 Raw Pointer Safety 契约冻结：`cheng_tooling build_backend_rawptr_contract` 生成 `src/tooling/backend_rawptr_contract.env`；`cheng_tooling verify_backend_rawptr_contract` 校验 `docs/raw-pointer-safety.md` + `docs/cheng-formal-spec.md` + `src/tooling/README.md` 的冻结契约与闭环接入点漂移，输出 `artifacts/backend_rawptr_contract/*.report.txt`。
  - `rawptr_contract.tooling_readme.synced=1`
- SABI-01 String ABI / cstring 契约冻结：`cheng_tooling build_backend_string_abi_contract` 生成 `src/tooling/backend_string_abi_contract.env`；`cheng_tooling verify_backend_string_abi_contract` 校验 `docs/backend-string-abi-contract.md` + `docs/cheng-formal-spec.md` + `src/tooling/README.md` + `docs/backend-techdebt-matrix.md` 的漂移，输出 `artifacts/backend_string_abi_contract/*.report.txt`。
  - `string_abi_contract.tooling_readme.synced=1`
  - `string_abi_contract.selector_cstring_lowering.user_abi=0`
  - `string_abi_contract.closure=required`
  - 本项当前验证 selector-owned cstring lowering 与 hard-fail guard；default verify 与 `backend_prod_closure` 共同负责日常漂移检查。
- STRLIT-01 String literal regression gate：`cheng_tooling verify_backend_string_literal_regression` 复跑 canonical `artifacts/backend_driver/cheng` 的 4 个字符串字面量 runtime probe，并保留 `60s timeout + sample` 诊断报告到 `artifacts/backend_string_literal_regression/backend_string_literal_regression.report.txt`。
- STRFMT-01 Std strformat gate：`cheng_tooling verify_std_strformat` 分别编译并运行 `fmt(singleton str[])`、`fmt(str[])`、`lines(str[])` probe，报告到 `artifacts/std_strformat/std_strformat.report.txt`。
- OUTSAFE-01 Backend default output safety gate：`cheng_tooling verify_backend_default_output_safety` 校验 canonical driver 在未显式指定输出路径时不会改写输入 `.cheng` 源文件，且默认 exe / obj 输出路径都可用；报告到 `artifacts/backend_default_output_safety/backend_default_output_safety.report.txt`。
  - 本项用于阻断 sidecar/object cstring payload 回退，不进入 `backend_prod_closure` required。
- DOTLOW-01 dot-lowering 契约冻结：`cheng_tooling build_backend_dot_lowering_contract` 生成 `src/tooling/backend_dot_lowering_contract.env`；`cheng_tooling verify_backend_dot_lowering_contract` 校验 `docs/backend-dot-lowering-contract.md` + `docs/UIR.md` + `src/tooling/README.md` + `docs/backend-techdebt-matrix.md` 的漂移，输出 `artifacts/backend_dot_lowering_contract/*.report.txt`。
  - `dot_lowering_contract.tooling_readme.synced=1`
  - `dot_lowering_contract.closure=required`
  - 本项当前验证 semantic dot lowering 与 selector-owned cstring 路径的一致性；default verify 与 `backend_prod_closure` 共同负责日常漂移检查。
- `cheng_tooling verify` 默认追加 `verify_backend_string_literal_regression`、`verify_std_strformat`、`verify_backend_default_output_safety`、`verify_new_expr_surface`、`verify_backend_selfhost_currentsrc_proof`、`verify_backend_string_abi_contract` 与 `verify_backend_dot_lowering_contract`；其中 `verify_backend_string_abi_contract` 与 `verify_backend_dot_lowering_contract` 既是 default-verify gate，也是 `backend_prod_closure` required gate；`verify_new_expr_surface` 负责阻断 real-source `new x` 语句面回退，并要求 bare current-source `cheng.stage2` 直接 compile+run `var x: T = new(T)` 最小 smoke；current-source proof gate 负责保证 bare `cheng.stage2` 与 `cheng.stage2.proof` 两条 fresh proof surface 都能 direct-compile + run `return_i64` smoke。`verify_backend_runtime_abi` 与 `verify_backend_sidecar_cheng_fresh` 已退出默认 `verify` 图，保留为专项 native-substrate gate。
- RPSPAR-02 ZRPC 收口 gate：`cheng_tooling verify_backend_rawptr_contract` 负责契约收口；`backend_prod_closure` / `verify_backend_closedloop` 同步执行 `rawptr_surface_forbid + rawptr_closedloop`，共同覆盖“语言表面绝对零裸指针”闭环。
- RPSPAR-02 Raw Pointer Surface 禁令：`cheng_tooling verify_backend_rawptr_surface_forbid` 校验“裸指针声明/指针运算/裸 `void*` 透出”三类负例必须失败，且诊断必须包含 `slice/tuple/handle/borrow` 替代建议；输出 `artifacts/backend_rawptr_surface_forbid/*.report.txt`。
- PURE-01 全栈纯化 surface gate：`cheng_tooling verify_backend_pure_cheng_surface` 现在冻结的是 active strict sidecar/proof manifest，本身检查三类漂移：active surface 是否重新引用 `backend_driver_c_sidecar_*` / `driver_c_build_module_stage1*` / `backend_driver_uir_sidecar_runtime_compat.c`、是否重新引入 `emergency_c` / `dist/releases/current/cheng` 这类 fallback 词面、以及三份 runtime host（`system_helpers.c` / `system_helpers_selflink_min_runtime.c` / `system_helpers_selflink_shim.c`）的 `driver_c_build_module_stage1_direct` 是否重新出现 direct->sidecar 回流。输出 `artifacts/backend_pure_cheng_surface/*.report.txt`。裸指针与 legacy `new` 语义漂移继续由独立 rawptr/new surface gate 负责，不在 PURE-01 里重复统计。
- RPSPAR-03 Slice 影子桥接：`cheng_tooling verify_backend_ffi_slice_shim` 校验 `importc` 形参 `T[]` 的桥接调用可编译（默认 compile-only；可设 `BACKEND_FFI_SLICE_SHIM_RUN=1` 开启运行）并覆盖 legacy `openArray[T]` 与用户层裸指针 surface 负例；输出 `artifacts/backend_ffi_slice_shim/backend_ffi_slice_shim.<target>.report.txt`。
- RPSPAR-04 Out-Ptr 影子桥接：`cheng_tooling verify_backend_ffi_outptr_tuple` 校验 `@ffi_out_ptrs + @importc` 的 tuple wrapper 降级（运行态正例 + status obj-only 正例 + arity 负例诊断）；输出 `artifacts/backend_ffi_outptr_tuple/*.report.txt`。
- RPSPAR-05 Handle 沙盒映射：`cheng_tooling verify_backend_ffi_handle_sandbox` 校验 runtime `ptr<->slot` 映射（C runtime + backend runtime 双实现）与过期 handle fail-safe（返回 `-1`，无 UAF），并输出 `artifacts/backend_ffi_handle_sandbox/*.report.txt`。
- RPSPAR-06 Borrow Struct* 桥接：`cheng_tooling verify_backend_ffi_borrow_bridge` 校验 `importc + var object` 正向桥接可运行，并校验产物符号包含 `_cheng_abi_borrow_mut_pair_i32`（默认固定 `system + runtime C` 口径以避免自链接 runtime 缺符号）；输出 `artifacts/backend_ffi_borrow_bridge/*.report.txt`。
- RPSPAR-07 Raw Pointer FFI 迁移：`cheng_tooling verify_backend_rawptr_migration` 校验迁移脚本 `cheng_tooling rawptr_migrate_ffi` 的 apply/check/rollback 闭环、风险报告与文档迁移入口；输出 `artifacts/backend_rawptr_migration/*.report.txt`。一键迁移：`cheng_tooling rawptr_migrate_ffi --root:<path> --apply --report:artifacts/backend_rawptr_migration/rawptr_migration.report.txt --backup-manifest:artifacts/backend_rawptr_migration/rawptr_migration.backups.tsv`；回滚：`cheng_tooling rawptr_migrate_ffi --rollback:artifacts/backend_rawptr_migration/rawptr_migration.backups.tsv`。
- RPSPAR-08 Raw Pointer 生产闭环：`cheng_tooling verify_backend_rawptr_closedloop` 校验 Raw Pointer required gate 集合与 `verify_backend_closedloop` / `backend_prod_closure` / `verify.sh` / CI 接入一致性；输出 `artifacts/backend_rawptr_closedloop/*.report.txt`。
- PAR-02 MemImage 核心：`cheng_tooling verify_backend_mem_image_core` 校验符号索引、段布局、重定位应用三类核心 marker，并执行 `self-link` 可执行产物探针（无 sidecar `.o/.objs` 残留）；输出 `artifacts/backend_mem_image_core/*.report.txt`。默认仅编译探针（`BACKEND_MEM_IMAGE_CORE_RUN=0`），可设 `BACKEND_MEM_IMAGE_CORE_RUN=1` 启用本机运行校验；可用 `BACKEND_MEM_IMAGE_CORE_DRIVER=<path>` 指定 gate driver。
- PAR-03 直出 EXE：`cheng_tooling verify_backend_mem_exe_emit` 校验格式写出（Mach-O/ELF/PE）、runtime 合并（`BACKEND_RUNTIME_OBJ` 负例/正例）与原子写盘（`tmp+rename`），并强制 sidecar 残留为 0；输出 `artifacts/backend_mem_exe_emit/backend_mem_exe_emit.<target>.report.txt`。
- PAR-04 Hotpatch 元数据：`cheng_tooling verify_backend_hotpatch_meta` 校验 `thunk map`、`patch_meta(schema=2)`（`thunk_id/target_slot_fileoff_or_memoff/code_pool_offset/layout_hash/commit_epoch`）与缺元数据/坏布局哈希/坏 ABI 负例；输出 `artifacts/backend_hotpatch_meta/backend_hotpatch_meta.<target>.report.txt`。
- PAR-05 热补丁事务：`cheng_tooling verify_backend_hotpatch_inplace` 校验 `trampoline + append-only + host runner` 事务语义（append 提交、layout-change 重启、编译失败保活、host pid 稳定）并输出 `artifacts/backend_hotpatch_inplace/backend_hotpatch_inplace.<target>.report.txt`。
- PAR-06 增量快路径：`cheng_tooling verify_backend_incr_patch_fastpath` 校验 dirty function 检测、最小编译计划与 `patch apply` 延迟收益（`inplace_apply_ms < full_build_ms`），并输出 `artifacts/backend_incr_patch_fastpath/backend_incr_patch_fastpath.<target>.report.txt`。
- PAR-07 回归门禁：`cheng_tooling verify_backend_mem_patch_regression` 校验 `determinism -> latency -> rollback -> crash-safe` 全链路，并输出 `artifacts/backend_mem_patch_regression/backend_mem_patch_regression.<target>.report.txt`。
- PAR-08 生产收口：`cheng_tooling backend_prod_closure --no-publish` 执行 dev required gates 收口；发布产物改由 `cheng_tooling backend-prod-publish` 统一生成并发布。
- PAR-05 兼容执行门禁：`cheng_tooling verify_backend_hotpatch` 持续覆盖 host-runner E2E（v1->v2 append 生效、编译失败保活、max-growth/layout-change 触发受控重启），并输出 `artifacts/backend_hotpatch/hotpatch.<target>.report.txt`。
- PAR-03 DOD/SoA：`cheng_tooling verify_backend_dod_soa` 校验 `stage1(nodeArena) + uir(int32 index)` 结构标记、`stage1/backend/uir` profile 事件与吞吐门禁（相对 `src/tooling/backend_dod_soa_baseline.env` 基线提升），并生成 `artifacts/backend_dod_soa/*.report.txt`。
- PAR-06 Release System-Link：`cheng_tooling verify_backend_release_c_o3_lto`（兼容脚本名）以 required 口径校验 release `system-link + O3/LTO`：强制 `BACKEND_LINKER=system`、`BACKEND_NO_RUNTIME_C=0`、`mold->lld->default` 解析，并输出 `artifacts/backend_release_c_o3_lto/backend_release_c_o3_lto.report.txt`（`gate=backend.release_system_link`）。
  - 并行实现：`emit=exe + BACKEND_MULTI=1` 时按单元编译并链接；优先使用 `fork` worker（`BACKEND_MULTI_FORK`，`stage1` 默认开启），不可用时回退到串行单元编译。
  - 单单元 fast-path：`emit=exe + multi` 仅在单元数 `>1` 时启用 `.objs` 多单元路径；单单元输入会走 `single.emit_obj + single.link`（避免无收益拆分与额外链接复杂度）。
  - module-cache（`.objs/.build.module.cache`；`BACKEND_MULTI_MODULE_CACHE`）在 `emit=exe + multi` 路径读写，复用模块建图结果；当前 stable 口径默认禁用，且对 load 路径做硬禁用防护（显式 unstable override 仅用于排障请求记录，不会在 stable driver 上启用 load）。
  - 默认会限制过细分桶（`BACKEND_MULTI_MIN_BATCH_UNITS`，默认 `8`），避免 worker 过多导致重复前端开销放大。
  - 同一输出目录采用 lock 互斥（`<out>.objs.lock`），避免并发产物互踩。
  - 增量 stamp 使用“单元文件状态 hash + 编译参数 hash（含编译器身份）”，减少无关改动重编并避免跨编译器复用旧产物。
  - 可执行单对象回退：`emit=exe` + Darwin + self-link 下可设 `BACKEND_MULTI_EXE_MAX_OBJS=<N>`（默认 `0` 禁用）在单元数超过阈值时回退“单对象编译+链接”；通常不建议开启（大模块多单元编译在当前实现下更快）。
  - `BACKEND_MULTI_LTO=1` 在 `emit=exe + multi` 路径下采用“全模块优化 + 单对象链接”口径（`<out>.objs` 中仅保留 1 个 `.o`），避免跨单元符号裁剪漂移。
  - `verify_backend_multi_lto` 现为“编译+运行”硬门禁：运行阶段默认 `10s` 超时（`BACKEND_MULTI_LTO_RUN_TIMEOUT`），超时/崩溃/符号缺失都会直接失败，不再回退 compile-only。

Stage1 全语法回归门禁：
```bash
cheng_tooling verify_stage1_fullspec
```
说明：
- `--allow-skip` 已不再支持（会直接失败）；不再允许把 unsupported host/missing codesign 视为跳过通过。
- 默认 `STAGE1_FULLSPEC_VALIDATE=0`（聚焦端到端行为闭环，避免大仓库在 validate=1 下超长编译）；如需严格后端校验口径，可显式设置 `STAGE1_FULLSPEC_VALIDATE=1`。
- fullspec 口径默认启用语义/所有权检查。
- 默认 `STAGE1_FULLSPEC_REUSE=1`：当输入/driver/关键编译参数未变化时，直接复用上次产物（`<name>.stage1_fullspec.stamp`），避免重复冷编译。
- 复用签名会纳入 `*` 环境变量哈希（排除 `STAGE1_FULLSPEC_REUSE/TIMEOUT`、timeout 诊断/健康探针变量、profile 开关与输入输出路径），配置变化会自动失效重编。
- 默认超时为 `60s`（`STAGE1_FULLSPEC_TIMEOUT`）；超时后会自动触发一次 profile 诊断复跑（`STAGE1_FULLSPEC_TIMEOUT_DIAG=1`，默认 `20s`，日志目录 `chengcache/stage1_fullspec_timeout_diag`），并追加轻量健康探针（`STAGE1_FULLSPEC_TIMEOUT_HEALTH_PROBE=1`，默认 `10s`）用于区分“特例慢编译”与“整体编译器异常”。
- 每次超时诊断会追加结构化摘要到 `chengcache/stage1_fullspec_timeout_diag/summary.tsv`，可配合 `cheng_tooling summarize_stage1_timeout_diag` 快速查看最近阻塞点。

## 自研后端与全链自举

后端 driver 选择（生产收口统一口径）：
```bash
# 查询 canonical driver（统一口径）
artifacts/tooling_cmd/cheng_tooling driver-path
artifacts/tooling_cmd/cheng_tooling driver-path --path-only

# 本地缺失时可显式重建（默认 system-link）
cheng_tooling build_backend_driver --name:artifacts/backend_driver/cheng

# 开发态可用解析器查看稳定 driver（缺失会自动重建）
cheng_tooling backend_driver_path

# 零脚本编译入口（cheng_tooling 原生子命令）
artifacts/tooling_cmd/cheng_tooling cheng --in:examples/hello.cheng --out:artifacts/chengc/hello

# multicall 模式（argv0=chengc）同样走原生 compile 子命令
cp -f "$(pwd)/artifacts/tooling_cmd/cheng_tooling" /tmp/chengc
/tmp/chengc examples/hello.cheng --out:artifacts/chengc/hello

# multicall 模式（argv0=cheng）可作为单入口别名
cp -f "$(pwd)/artifacts/tooling_cmd/cheng_tooling" /tmp/cheng
/tmp/cheng examples/hello.cheng --out:artifacts/chengc/hello

# 默认：tooling/verify 脚本会在退出时清理本地 `cheng*` 产物（移动到 `chengcache/_trash_cheng`），避免遗留占用空间。
# 如需保留（加速反复调试），可关闭：
# export CLEAN_CHENG_LOCAL=0
#
# `cheng_tooling build_backend_driver` 双轨：默认 fast，门禁/发布走 seal。
```
说明：
- `backend_driver_path` 默认校验 `stage1` 最小编译 smoke；如需额外 strict 口径，可设 `BACKEND_DRIVER_PATH_STAGE1_STRICT_SMOKE=1`；如需启用 dict fixture 探针可设 `BACKEND_DRIVER_PATH_STAGE1_DICT_SMOKE=1`。
- 生产闭环固定 canonical driver（`artifacts/backend_driver/cheng`）；若该 driver 体检失败会直接阻断，不再自动回退。
- `build_backend_driver` 原生命令会先尝试重编；若当前 stage0 在 `backend_driver.cheng` 上崩溃/失败，或重编产物体检失败，直接失败（无 stage0 复用回退）。
- stage0 环境变量已统一：native 重编/probe/compile/runtime-refresh 仅注入 `BACKEND_*`、`STAGE1_*`、`MM`、`CACHE`，不再注入 `CHENG_*` 兼容前缀。
- `build_backend_driver` 会输出 `build_backend_driver_mode=fast|seal` 与 `build_backend_driver_compile_mode=quick|full`，以及 `build_backend_driver_rebuilt=1|0`；`--require-rebuild`（或 `BACKEND_BUILD_DRIVER_REQUIRE_REBUILD=1`）用于强制“必须真实重编”。
- `build_backend_driver` 失败路径会额外输出 `build_backend_driver_last_stage0`、`build_backend_driver_last_rc`、`build_backend_driver_last_kind`，用于快速定位最后一次失败候选与退出类型。
- `--require-rebuild` 启用时，`build_backend_driver` 在所有 stage0 尝试失败后不再回写 stage0 到输出路径（避免覆盖失败现场），直接返回失败并保留诊断上下文。
- `build_backend_driver` 默认关闭 post-build compile probe（`BACKEND_BUILD_DRIVER_POST_PROBE=0`），避免坏产物 probe 触发长时间挂起；需要 strict 运行验证时可显式设 `BACKEND_BUILD_DRIVER_POST_PROBE=1`。
- `build_backend_driver` 内部固定 `whole_program=1`（不再提供 `BACKEND_BUILD_DRIVER_WHOLE_PROGRAM`/`BACKEND_BUILD_DRIVER_REBUILD_WHOLE_PROGRAM` 参数），并做两层产物体检：`nm -u` 内部未解析符号拦截（`BACKEND_BUILD_DRIVER_UNDEF_GUARD=1`）+ 必需导出符号检查（必须包含 `backendMain`）。拦截范围覆盖 `backend/driver/tooling/cheng_*` 与 `uir/macho/elf/coff/os_*` 等应由闭包吸收的符号。
- `build_backend_driver` 默认输出 canonical driver：`artifacts/backend_driver/cheng`；未显式传 `--stage0:<path>` 时会优先选择健康的 `artifacts/backend_selfhost_self_obj/cheng.stage2`，再回落到 canonical/seed/dist 候选。
- `build_backend_driver` 默认执行 stage0 capability preflight（`TOOLING_STAGE0_CAPABILITY_PREFLIGHT=1`）：比较 `stage0.cap` 签名与当前源码签名。当前 `source_capability_signature` 由 `tooling_stage0CapabilitySourceList()` 定义：固定覆盖 `src/std/cmdline.cheng`、`src/std/rawmem_support.cheng`、`src/stage1/{ast,token,lexer,parser,frontend_lib,diagnostics,semantics,ownership,monomorphize,type_syntax_lowering,c_profile_lowering}.cheng`、`src/backend/uir/uir_internal/uir_core_types.cheng`，并优先在运行时扫描 `src/backend/uir/uir_internal/uir_core_builder*.cheng` 全量 split-helper 家族；扫不到时才回落到源码内静态名单。其余仍覆盖 `src/backend/uir/uir_codegen.cheng`、`src/backend/machine/machine_internal/machine_core_types.cheng`、`src/backend/machine/select_internal/{aarch64_select,x86_64_select}.cheng`、`src/backend/obj/{macho_writer,linker_shared_core,macho_linker,macho_linker_x86_64,elf_linker,elf_linker_riscv64,coff_linker}.cheng` 与 `src/backend/tooling/backend_driver.cheng`。不匹配时直接 fail-fast 并提示先刷新 seed/driver。可用 `TOOLING_STAGE0_CAPABILITY_REQUIRE_FILE=1` 强制要求存在 `.cap.env` 侧车。
- `.cap.env` 侧车由正在运行的 `artifacts/tooling_cmd/cheng_tooling` 可执行文件生成，不是直接从源码现算；如果 binary 陈旧，侧车仍可能保留旧的 `source_capability_signature`/`source_count`。遇到这种情况应先重建 canonical tooling，再决定是否需要刷新 seed/driver。
- `src/tooling/cheng_tooling.cheng` 在 dev/host 同机口径下默认会产出 launcher 壳，而不是直接把目标路径写成本机 native binary；真实 companion 会落到 `<out>.repo_native_exec.bin`。只有显式设 `TOOLING_EMIT_SELFHOST_LAUNCHER=0`，才会尝试把目标路径本身编成 native 可执行文件。
- 当前 selfhost 排障基线：若 `build_backend_driver --require-rebuild --debug-rebuild` 的新 attempt 在 `tests/cheng/backend/fixtures/return_add.cheng` 上仅报 `Unexpected token`，先用 seed `artifacts/backend_selfhost_self_obj/cheng.stage2` 对同一 fixture 做 smoke，再检查 `chengcache/backend_driver_build_tmp` 中保留的 attempt。当前已知症状是 lexer 把 token lexeme 全部物化为空串（典型日志 `src_len=0 dst_len=0`），根因不在 import failure。建议配合 `BACKEND_DEBUG_TOKEN_COPY=1`、`STAGE1_TRACE_TOKENS=1`、`BACKEND_DEBUG_PARSE_DIAG=1` 复现。
- `build_backend_driver` 支持双轨：
  - `fast` 轨：默认轨，`compile_mode=quick`，默认 `BACKEND_BUILD_DRIVER_FAST_STRICT_NATIVE=1`，先走可用的 quick/system-link，并启用 quick stamp cache（`BACKEND_BUILD_DRIVER_QUICK_STAMP_CACHE=1`）与跨输出路径共享 quick cache（`BACKEND_BUILD_DRIVER_QUICK_SHARED_CACHE=1`）；如需实验 quick/self-link，可显式设 `BACKEND_BUILD_DRIVER_FAST_STRICT_NATIVE=0`。
  - `seal` 轨：`--seal`、`--full-rebuild` 或 `--require-rebuild` / `debug` 下默认进入，`compile_mode=full`，继续走 strict 参数集与稳定性优先口径。
- `build_backend_driver` fast 轨默认开启 `BACKEND_BUILD_DRIVER_FAST_REUSE=1`，但仅当 final 输出自带的 build-driver stamp 与当前源码/能力签名精确匹配时才直接复用；旧 stamp 或缺失 stamp 会自动回退到正常重编。
- `build_backend_driver` fast 轨默认开启 `BACKEND_BUILD_DRIVER_FAST_CANONICAL_SMOKE=1`：当输出目标是 canonical driver（`artifacts/backend_driver/cheng`）时，quick 候选在安装前必须通过 `dev-inmem + release-system` 双 smoke，并写入 `cheng.quick_canonical_smoke.env` 证明侧车；缺失/不匹配时不会 direct-reuse，坏候选会按二进制 SHA 进入 quarantine，切断 quick/shared cache 复用。
- seal 轨的 strict native 下（`BUILD_DRIVER_STRICT_NATIVE=1`）即使 `timeout<=60s` 也默认不启用 `tight` profile；如需诊断才显式开启 `BACKEND_BUILD_DRIVER_REBUILD_TIGHT_TIMEOUT_PROFILE=1` 并关闭 strict。`tight` 仅调整 `parse/sched`（`outline/ws`），不会再降级语义/所有权 fixed-zero 合同（固定 `0/0`）。
- seal/full-rebuild 默认关闭“同参安全重试”（`BACKEND_BUILD_DRIVER_REBUILD_CRASH_RETRY_SAFE=0`）；需要排障时可显式开启，命中 `rc=139` 会降级到单 worker 串行重试（`jobs=1` + `multi=0`）。
- `build_backend_driver` 在 self-link / experimental fast 轨默认优先复用现有 runtime 组合对象（`BACKEND_BUILD_DRIVER_RUNTIME_OBJ_REFRESH=0`）；仅在显式开启时才刷新重建（`BACKEND_BUILD_DRIVER_RUNTIME_OBJ_REFRESH=1`）。底层刷新逻辑可用 `BACKEND_RUNTIME_OBJ_REFRESH_TIMEOUT` / `BACKEND_RUNTIME_OBJ_REFRESH_STRICT` 调整（超时默认 60s）。
- `build_backend_driver` 与 `selfhost-bootstrap-fast-host` 的 stage0 解析默认强制 compile probe，并默认跳过命中 `UE/UEs` 或 `PPID=1` 孤儿的候选路径（`TOOLING_STAGE0_ORPHAN_GUARD=1`，可设 `0` 关闭）。其中 `build_backend_driver` 在未显式传 `--stage0:<path>` 时，会优先探测健康的 `artifacts/backend_selfhost_self_obj/cheng.stage2`，再回落到常规候选列表。
- `build_backend_driver` 与 `selfhost-bootstrap-fast-host` 默认启用 stage0 quarantine 门禁（`TOOLING_STAGE0_QUARANTINE=1`）并在执行前做 preflight（`TOOLING_STAGE0_PREFLIGHT_TIMEOUT`，默认 8s）；执行路径不再复制隔离副本。发现既有 quarantine `UE/UEs` 或孤儿残留时会阻断新启动（`TOOLING_STAGE0_QUARANTINE_BLOCK_ON_UE=1`，可显式设 `0` 继续排障）。自动清理后会做短重检（`TOOLING_STAGE0_QUARANTINE_BLOCK_RECHECKS`，默认 `2`），降低“首轮已清理但仍误阻断”的抖动。
- Darwin 平台默认启用二进制执行策略静态预检（`TOOLING_DARWIN_CODESIGN_PREFLIGHT=1`）：在执行 stage0/`--help` 探针前先做 `codesign --verify`，失败立即阻断，避免反复触发 `_dyld_start` 类 `UE`。
- `build_backend_driver` 与 `selfhost-bootstrap-fast-host` 还会在命令入口前置一次 quarantine UE/orphan 快速阻断（默认开启：`TOOLING_BUILD_DRIVER_BLOCK_ON_UE=1`、`TOOLING_SELFHOST_BLOCK_ON_UE=1`），并输出 preview + cleanup hint，避免进入长时间候选重试。
- canonical stage0 自动“从 dist 回填修复”默认关闭（`TOOLING_STAGE0_CANONICAL_RECOVER=0`）；需要时显式开启，避免在 strict 闭环中引入隐式回退来源。
- stage0 调用默认串行化互斥（`TOOLING_STAGE0_LOCK=1`）：同机并发任务会先竞争 `chengcache/stage0_quarantine/stage0.lock`，避免多个 stage0 同时运行放大 `UE/UEs` 风险。锁 owner 现在同时记录 `owner.pid + owner.pgid`，`force takeover` 时优先按进程组清理，降低孤儿残留。可用 `TOOLING_STAGE0_LOCK_WAIT_SEC`（默认 60）控制等待上限，`TOOLING_STAGE0_LOCK_STALE_SEC`（默认 600）清理陈旧锁；默认关闭 `TOOLING_STAGE0_LOCK_FORCE_TAKEOVER`（`0`），等待超时返回 `rc=125`，只有显式设为 `1` 才会强制接管并清理 owner。
- `cheng` 原生命令统一接入 stage0 隔离副本 + preflight + 超时包装（`TOOLING_COMPILE_TIMEOUT`，默认 60s），并移除 compile 路径 stage0 全局锁以支持并发编译。
- `build-global` 命令入口默认执行 UE/orphan 阻断（`TOOLING_BUILD_GLOBAL_BLOCK_ON_UE=1`）。
- 可执行发布默认使用原子替换并在 Darwin 上重签名与清理 provenance（`TOOLING_DARWIN_CODESIGN_RESEAL=1`），减少半写入/策略拒绝导致的 `UE` 残留。
- `run <script>` 对重型脚本入口（含 `backend_prod_closure`）默认执行 UE/orphan 阻断（`TOOLING_RUN_SCRIPT_BLOCK_ON_UE=1`）。
- 新增原生排障命令：`stage0-ue-status`（查看残留）、`stage0-ue-clean`（清理残留）。
- `cheng_tooling backend_prod_closure` 默认增加前置硬门禁：先执行 `build_backend_driver --full-rebuild --require-rebuild` 且必须出现 `build_backend_driver_rebuilt=1`（可用 `BACKEND_PROD_REQUIRE_DRIVER_FULL_REBUILD=0` 临时关闭）。
- `cheng_tooling backend_prod_closure` 默认使用 native backend driver，不再包含 delegate boot mode 应急通道。
- `cheng_tooling build_backend_driver` 支持显式 `--stage0:<path>` 与 `--mode:auto|fast|seal`；未显式指定模式时默认 `auto`，即 `require-rebuild/debug` 走 `seal`，其余走 `fast`。
- `cheng_tooling backend_prod_closure` 前置 full-rebuild 继承 `build_backend_driver` 的 stage0 选择策略。
- stage0 解析默认会跳过“早于当前 driver 文件代际”的历史 `UE/UEs`（`TOOLING_STAGE0_GENERATION_GUARD=1`，默认开启；优先按 `TOOLING_STAGE0_INODE_GUARD=1` 比较进程 `txt` inode 与当前 driver inode，不可用时再回退到进程 `etime` 与 driver `mtime`，容差 `TOOLING_STAGE0_GENERATION_MARGIN_SEC`，默认 `3s`），避免旧残留造成永久阻断。
- stage0 quarantine stamp 现在按当前 binary `sha256` 生效；即使进程 `rc=0`，只要出现“编译成功但未产生产物”的假成功，也会写入 `stage0_block.<sha>.env` 并阻断后续复用。
- `TOOLING_STAGE0_SKIP_UE_GUARD=1` 与 `TOOLING_STAGE0_ORPHAN_ONLY_BYPASS=1` 均不再放行（会打印忽略提示并继续 hard-block）。
- stage0 增加 digest 硬阻断：可通过 `TOOLING_STAGE0_BLOCK_SHA256=<sha[,sha...]>` 配置阻断列表，防止坏 driver 再次进入执行链。
- stage0 quarantine 计数默认不再把 `dist/releases/current/cheng` 的历史 `UE` 计入全局阻断（`TOOLING_STAGE0_DIST_UE_GUARD=0`）；若需排障可显式设为 `1` 重新纳入统计。
- `build_backend_driver` fast/seal 两轨默认都走 `system`；如需实验 fast self-link，可显式设 `BACKEND_BUILD_DRIVER_FAST_STRICT_NATIVE=0` 并配合合法 linker 策略。
- `build_backend_driver` 在显式 seed stage0（`artifacts/backend_seed/...`）下，默认优先使用不含 bridge 的基础 runtime obj（`BACKEND_BUILD_DRIVER_SEED_BASE_RUNTIME_OBJ=1`），用于规避旧 stage0 对 `___stderrp` 未定义重定位类型的兼容问题。
- `build_backend_driver` 两轨都固定 `whole_program=1`；`fast` 轨默认 quick rebuild，`seal` 轨默认 full rebuild，seal 超时默认 `BACKEND_BUILD_DRIVER_REBUILD_TIMEOUT=60`。
- 原生超时包装会把子进程输出先落到临时日志再回放，避免 `UE/UEs` 子进程持有管道导致父进程长时间不返回；内部超时默认返回 `124`，若被外层 timeout/信号终止则可能表现为 `143`。
- 原生超时包装默认输出结构化诊断行（`TOOLING_TIMEOUT_DIAG=1`）：`cheng_tooling_timeout=1 timeout_sec=... elapsed_sec=... child_pid=... no_orphan=... drain_sec=... drain_loops=... label=...`；可用 `TOOLING_TIMEOUT_LABEL` 标记调用场景。
- 超时包装默认开启“强制回收孤儿子进程”语义（`TOOLING_TIMEOUT_NO_ORPHAN=1`）；如需临时恢复“超时即返回”，可设 `TOOLING_TIMEOUT_NO_ORPHAN=0`（可能增加孤儿进程残留风险）。
- no-orphan drain 现在带硬上限参数：`TOOLING_TIMEOUT_NO_ORPHAN_DRAIN_SEC`（默认 1）与 `TOOLING_TIMEOUT_NO_ORPHAN_DRAIN_MAX_LOOPS`（默认 8），用于避免不可中断子进程导致的超时回收长挂。
- 外部 `timeout/gtimeout` 调用默认关闭，统一走内建 no-orphan watchdog；如需临时启用外部实现可设 `TOOLING_TIMEOUT_USE_EXTERNAL=1`（不建议在 UE 排障期使用）。
- 若需在长时间锁占用时强制抢锁，可显式设置 `TOOLING_STAGE0_LOCK_FORCE_TAKEOVER=1`。
- `cheng_tooling` 默认 `TOOLING_NATIVE_ENABLE=1`，并且 `cheng/chengc/bootstrap/bootstrap_pure/backend_driver_path/build_backend_driver/verify_backend_selfhost_bootstrap_self_obj/verify_backend_selfhost_100ms_host` 为 native-required（失败即失败，不再回退脚本）。
- `BACKEND_ENABLE_CSTRING_LOWERING`/`BACKEND_CSTRING_LOWERING`/`BACKEND_ISEL_CSTRING_LOWERING` 已移除；selector-side eager `cstring lowering` 当前固定禁用，不再提供开关。用户 ABI 契约仍以 `str` 值语义 / `cstring` FFI 边界语义为准，必要 payload 由 `UIR metadata -> post-regalloc backfill` 与 object writer 兼容路径补齐。
- `run <script>` 的 embedded 执行改为临时脚本文件（`chengcache/embedded_scripts/*.run.sh`）并在命令结束后清理，不再复用 `.embedded.sh` 常驻缓存。

后端链接环境助手（脚本统一注入 self-linker 运行时 `.o`）：
```bash
# 输出可直接给 `env` 使用的链接环境变量
cheng_tooling backend_link_env --driver:artifacts/backend_driver/cheng --target:arm64-apple-darwin --linker:self
```
说明：
- `--linker:self`：自动构建/复用 `chengcache/system_helpers.backend.cheng.<target>.o` 并输出  
  `BACKEND_LINKER=self BACKEND_NO_RUNTIME_C=1 BACKEND_RUNTIME_OBJ=<...>`。
- `--linker:auto`（默认）或未设置：按目标自动选择（支持 self-link 的目标默认 `self`，否则回退 `system`）；生产主链建议固定 `self`。

纯 Cheng（不走 C 后端）生成/更新固定 seed（stage2 driver）：
```bash
# 以“已存在的 stage2 driver”作为 seed（推荐：来自发布包/上一版本）
cheng_tooling backend_seed_pure \
  --seed:artifacts/backend_selfhost_self_obj/cheng.stage2 \
  --out:artifacts/backend_seed/cheng.stage2

# CI/生产固定使用 canonical driver（缺失会失败，不再使用 C seed）
cheng_tooling backend_prod_closure --only-self-obj-bootstrap
```

全链自举门禁（Darwin/arm64 必跑；非支持主机直接失败）：
```bash
cheng_tooling verify_fullchain_bootstrap
```
开发加速自举（单阶段，非发布门禁口径）：
```bash
cheng_tooling verify_backend_selfhost_bootstrap_fast
```
生产/CI 的 fullchain 自举闭环（stage2->tool smoke；`stage1_fullspec` 仅作为 internal `obj-only` 校验门禁）：
```bash
FULLCHAIN_OBJ_ONLY=1 cheng_tooling verify_fullchain_bootstrap
```
说明：
- `verify_fullchain_bootstrap` 要求 stage2 driver 已存在（默认 `artifacts/backend_selfhost_self_obj/cheng.stage2`，可用 `FULLCHAIN_STAGE2` 覆盖）。
- 缺少 stage2 时，先运行 `cheng_tooling verify_backend_selfhost_bootstrap_self_obj` 或 `cheng_tooling verify_backend_ci_obj_only --seed:<path>`。
- `verify_fullchain_bootstrap` 默认开启复用（`FULLCHAIN_REUSE=1`）：当 stage2/source/runtime 未变化时，复用 `stage1_fullspec_obj`，减少重复闭环耗时。
- fullchain 工具构建默认开启且强阻断（`FULLCHAIN_BUILD_TOOLS=1`）；`FULLCHAIN_BUILD_TOOLS=0` 不再支持。`FULLCHAIN_BUILD_TOOLS_STRICT=0` 已不再支持。
  - 工具构建要求 `MM=orc`，不再自动回退 `MM=off`。
  - 工具构建链接器默认 `FULLCHAIN_TOOL_LINKER=system`（可显式设为 `self` 进行对照）。
  - 启用后按主机核数并行构建（`FULLCHAIN_TOOL_JOBS`，`0`=auto）。
  - 工具单项默认超时 `60s`（`FULLCHAIN_TOOL_TIMEOUT`）；超时/失败不再复用旧产物或生成 stub，而是直接阻断。
  - storage 工具默认使用 `src/tooling/cheng_storage_smoke.cheng` 参与 fullchain 构建（`FULLCHAIN_STORAGE_TOOL_SRC` 可覆盖为 `src/tooling/cheng_storage.cheng` 以恢复完整 CLI 口径）。
  - tool smoke 采用 `--help`/`-h`/无参三路探针；优先要求输出可诊断文本，若工具仅返回状态码（无 stdout/stderr）则记 `info` 并视为“可运行”。
  - `verify_backend_selfhost_bootstrap_self_obj` 支持 `SELF_OBJ_BOOTSTRAP_TIMEOUT=<seconds>`（默认 60）防止 stage1/stage2 自举编译长时间卡死。
  - smoke 子阶段支持独立超时 `SELF_OBJ_BOOTSTRAP_SMOKE_TIMEOUT=<seconds>`（默认 15）；用于在 `fast` 模式下快速触发 smoke fallback，避免总时长被单次卡死拉满。
  - 新增 `verify_backend_selfhost_bootstrap_inmem_witness`：固定 `full parse + ws + BACKEND_DIRECT_EXE=1 + BACKEND_LINKERLESS_INMEM=1 + BACKEND_LINKER=self`，用于在 strict fixed-point 前快速验证 compile-stage1 的真实 deepest front；默认超时 `SELF_OBJ_BOOTSTRAP_INMEM_WITNESS_TIMEOUT=60`。
  - 新增 `verify_backend_selfhost_inner_loop`：固定执行 `build-backend-driver --require-rebuild -> strict inmem witness -> strict host`，并将总控 timing/summary 落盘到 `artifacts/backend_selfhost_inner_loop/`；可用 `--no-strict` 收成“canonical rebuild + witness”快环，也可用 `--closure` 在 strict 通过后继续追加 `backend_prod_closure`。
- `verify_backend_selfhost_bootstrap_self_obj` 与默认公开编译入口都按内建 no-pointer 生产策略解释用户源码；不再要求用户显式传 `ABI=v2_noptr`。
- `STAGE1_STD_NO_POINTERS*` 与 `STAGE1_NO_POINTERS_NON_C_ABI*` 现在只用于内部实现、专项 gate 或编译器/运行时自举放宽，不再是公开编译配置面。
- 用户公开入口已移除 `--abi:v2_noptr`、`--std-noptr`、`--std-noptr-strict`、`--noptr-non-c-abi`、`--noptr-non-c-abi-internal`；默认 `cheng` / `release-compile` / `chengc` 会直接拒绝用户裸指针 surface。
- `v2_noptr` 的 no-pointer 策略由 `verify_backend_noptr_default_cli` 负向样例门禁保证：默认 `chengc` 入口必须拒绝用户源码裸指针样例，包括 `@importc` raw-pointer surface。
- 兼容命令 `verify_backend_abi_v2_noptr` 已收敛为 `verify_backend_noptr_default_cli` 的别名，不再维护独立 gate 逻辑。
- `verify_backend_noptr_default_cli` 默认验证三类路径：用户裸指针负例、safe `@importc` 正例、raw `@importc void*` 负例。
- `chengc` 默认按 no-pointer 生产口径编译用户源码；旧 `ABI/STAGE1_*NO_POINTERS*` 名称仅保留给内部兼容链，不应再当作用户入口文档化。
- 新增 `verify_backend_noptr_default_cli`：验证默认 `chengc` 入口（不手工放宽 no-pointer env）会拒绝用户指针样例与 raw `@importc void*` 样例，同时保留普通正样例与 safe `@importc` 正常通过。
  - stage0 选择固定为 canonical：`artifacts/backend_driver/cheng`；不可用则直接阻断。
  - 自举模式：`SELF_OBJ_BOOTSTRAP_MODE=strict|fast`（默认 `fast`）。`strict` 固定执行 `stage1 -> stage2 -> stage3(witness)` 三轮 full rebuild，并强制 `SHA256(stage2) == SHA256(stage3)`；仅诊断场景可用 `SELF_OBJ_BOOTSTRAP_STRICT_DIAG_ALLOW_MISMATCH=1` 放宽。`fast` 只编译 stage1 并同步为 stage2（开发加速，跳过 fixed-point 校验）。
  - strict fixed-point mismatch 会自动落盘诊断：`<out-dir>/strict_mismatch/summary.env`（含 `bytes/symbols/strings` diff 路径），并通过 `bootstrap_pure_strict_mismatch_report` / `selfhost_bootstrap_strict_mismatch_report` 字段输出。
  - strict 口径固定语义/所有权检查开启（`STAGE1_SEM_FIXED_0=0`、`STAGE1_OWNERSHIP_FIXED_0=0`），不再提供 strict skip 覆盖开关。
  - `verify_backend_selfhost_bootstrap_self_obj --strict-inmem-witness` 会在 strict host fixed-point 前显式跑一轮上述 in-memory witness；它只用于快速前沿确认，不替代最终 `stage2/stage3` SHA 一致性门禁。
  - 推荐内循环：`cheng_tooling verify_backend_selfhost_inner_loop --no-closure`。它会先强制刷新 canonical driver，再按 fail-fast 顺序执行 witness/strict，并输出 `selfhost_inner_loop.summary.env` 与 `selfhost_inner_loop_timing.tsv`，用于判断当前 deepest front 是否已经前移、是否值得再跑 `backend_prod_closure`。
  - `fast` 模式默认沿用“优先复用已有 stage1/stage2”（`SELF_OBJ_BOOTSTRAP_FAST_REUSE_STALE=1`）；如需在 fast 下强制检查是否过期并触发重编，可设 `SELF_OBJ_BOOTSTRAP_FAST_REUSE_STALE=0`。
  - `fast` 与 `strict` 都固定使用 dev in-memory 管线：`BACKEND_STAGE1_PARSE_MODE=outline`、`BACKEND_FN_SCHED=ws`、`BACKEND_DIRECT_EXE=1`；语义差异只体现在 `strict` 额外执行 fixed-point witness（`stage2/stage3` SHA 一致性）。
  - `BACKEND_DIRECT_EXE=1` 在 host darwin/arm64 + self-link 口径优先走 `macho_direct_exe_writer` 内存直写链路；默认不允许失败回退（`BACKEND_FAST_FALLBACK_ALLOW=0`）。
  - 自举编译默认口径：`SELF_OBJ_BOOTSTRAP_MULTI=1`、`SELF_OBJ_BOOTSTRAP_INCREMENTAL=1`；`strict` 模式默认并行并在 worker 探针失败时快速回退串行（默认 `SELF_OBJ_BOOTSTRAP_ALLOW_RETRY=0`，避免“失败后整轮重试”放大耗时），`fast` 模式保持重试开启并自动按逻辑核数设置 jobs（可用 `SELF_OBJ_BOOTSTRAP_FAST_JOBS_CAP` 设上限，默认 `8`）。
  - 超时策略：编译返回 `124`（timeout）时默认不再做同参重试，避免单阶段耗时翻倍掩盖性能回退；selfhost 主链已移除 stage0 兜底重编分支，超时/失败将直接报错。
  - `emit=exe` 若未产出 sidecar `.o`/`.objs`，selfhost 门禁仅在兼容诊断路径补充 `emit=obj` 对比对象；严格 fixed-point 以 `stage2/stage3` 可执行文件 SHA-256 一致为准。
  - stage0 compat overlay 已从自举主链移除：`SELF_OBJ_BOOTSTRAP_STAGE0_COMPAT` 仅接受 `0`，命中语法不兼容时直接失败并输出 native 日志。
  - 自举临时产物默认使用稳定 session（`SELF_OBJ_BOOTSTRAP_SESSION=default`），可复用 `<out>_tmp_<session>.objs` 缓存；并行多任务可显式设置不同 session 避免互相覆盖。
  - 执行结束会输出 `backend.selfhost_self_obj.timing`（lock/stage1/stage2/smoke/total 秒数），并落盘 `selfhost_timing_<session>.tsv` 与 `selfhost_metrics_<session>.json`（可用 `SELF_OBJ_BOOTSTRAP_TIMING_OUT` / `SELF_OBJ_BOOTSTRAP_METRICS_OUT` 覆盖路径）；失败/超时路径也会补写 `total` 行（`fail`/`fail-timeout`）。
  - 可在问题排查时显式关闭：`SELF_OBJ_BOOTSTRAP_MULTI=0 SELF_OBJ_BOOTSTRAP_INCREMENTAL=0`。
  - 2026-02-28 host-only 100ms gate 实测（`verify_backend_selfhost_100ms_host --compile-stage1 --iters:30 --enforce:1`）：`p95=83ms`、`p99=91ms`。
  - 2026-02-06：已修复一类“无输出超时”根因（`src/std/strings.cheng` 的字符串 `==` 对 `nil` 比较递归调用自身）；该问题会让 selfhost 阶段卡住直到超时。
  - 修复后若 selfhost 失败，将优先以可诊断错误退出（而非长时间超时卡住）。
- `FULLCHAIN_OBJ_ONLY=1` 默认使用 `examples/backend_closedloop_fullspec.cheng` 作为 obj-only 样例（运行 `exit 0` 视为通过）；可用 `FULLCHAIN_STAGE1_FILE=<path>` 覆盖。
- fullchain stage1 样例编译默认走 `FULLCHAIN_STAGE1_FRONTEND=stage1`（默认单次编译超时 `60s`）。
- fullchain stage1 样例编译默认超时 `60s`（`FULLCHAIN_STAGE1_TIMEOUT`），默认串行 `FULLCHAIN_STAGE1_MULTI=0` 以避免 fullspec 并行抖动；需要并行对照时可显式设 `FULLCHAIN_STAGE1_MULTI=1`（并行失败会自动串行重试）。可用 `FULLCHAIN_STAGE1_MULTI` / `FULLCHAIN_STAGE1_JOBS` 调整并行参数。
- fullchain stage1 样例链接器仅支持 `FULLCHAIN_STAGE1_LINKER=self|system`（默认 `system`）；`auto` 回退策略已移除（不再自动降级）。

CI/生产（macOS arm64）使用 dist seed 跑“后端自举 + fullchain smoke”：
```bash
cheng_tooling verify_backend_ci_obj_only
```
说明：
- 默认从 `dist/releases/current_id.txt` 解析 seed；也可传 `--seed:<path>` 指定。
- 若 dist seed 不存在，会直接失败并提示补充 seed（可用 `--require-seed` 强制严格模式）。
- `verify_backend_ci_obj_only` 默认 strict：任一步骤返回 `status=2`（skip）会直接失败；`--strict` 仅保留兼容参数。
- 不会自动使用 `artifacts/backend_seed/cheng.stage2` 或 `artifacts/backend_selfhost_self_obj/cheng.stage2`。
- 默认额外执行 `backend.ci.selfhost_perf_regression`（读取 `selfhost_timing_<session>.tsv` 做阈值检查）。
- 专用机 100ms 硬门禁入口：`cheng_tooling verify_backend_selfhost_100ms_host`（基线文件：`src/tooling/selfhost_perf_100ms_host.env`）。
  - 零脚本（native）核心入口已并入 `cheng_tooling` 子命令（建议用于 Host-only 自举/编译链路）：
  - `cheng_tooling driver-path [--path-only]`
  - `cheng_tooling build-backend-driver [--out:<path>] [--target:<triple>] [--fast|--seal] [--require-rebuild]`
  - `cheng_tooling selfhost-bootstrap-fast-host [--compile-stage1|--no-compile-stage1] [--out-dir:<path>] [--timing-out:<path>]`
  - `cheng_tooling verify_backend_selfhost_bootstrap_self_obj [--mode:fast|strict] [--out-dir:<path>] [--timing-out:<path>]`
  - `cheng_tooling selfhost-100ms-host [--iters:<N>] [--p95-ms:<ms>] [--p99-ms:<ms>] [--host-only:<tag>] [--report:<path>] [--enforce:0|1]`
  - `cheng_tooling verify_backend_cdrop_emergency`（应急 C-Drop gate，默认不接入 release 阻断链）
  - `selfhost-bootstrap-fast-host` 额外输出 `selfhost_fast_stage1_status`、`selfhost_fast_stage1_compile_mode` 与 `selfhost_fast_stage1_rebuild_ok=1|0`；`compile-stage1` 下可用 `SELFHOST_STAGE1_REQUIRE_REBUILD=1` 强制“必须真实重编”，可用 `SELFHOST_STAGE1_FULL_REBUILD=1|0` 切换 `full|quick` 重编模式（默认 `0`，即 quick）。当 require-rebuild 开启且 quick 失败/不可运行时，默认会自动升级一次 full（`SELFHOST_STAGE1_AUTO_FULL_ON_REQUIRE=1`）。
  - `quick` 重编默认启用 light sanity（`SELFHOST_STAGE1_QUICK_LIGHT_SANITY=1`）：仅校验产物存在且非空，避免在 60s Dev 口径被严格 runnable probe 阻断；`full` 重编默认强制 compile-probe（`SELFHOST_STAGE1_FORCE_PROBE_ON_FULL=1`）。可设 `SELFHOST_STAGE1_QUICK_LIGHT_SANITY=0` 让 quick 也走 strict probe。
  - `selfhost-100ms-host` 报告新增 `compile_stage1`、`stage1_compile_mode`、`stage1_status`、`stage1_rebuild_ok`、`prewarm_stage1` 字段；`compile-stage1` 模式默认按性能阈值阻断（`SELFHOST_100MS_ENFORCE_WITH_COMPILE_STAGE1=1`），可显式设 `0` 仅报告不阻断。
  - `cheng_tooling cheng --in:<file.cheng> [--out:<path>]` 覆盖编译入口；`cheng_tooling bootstrap-pure` 覆盖 pure 自举入口。
- `cheng_tooling` 默认 `TOOLING_NATIVE_ENABLE=1`，且 `cheng/chengc/bootstrap/bootstrap_pure/backend_driver_path/build_backend_driver/verify_backend_selfhost_bootstrap_self_obj/verify_backend_selfhost_100ms_host` 为 native-required（失败即失败，不再脚本回落）。
- `cheng_tooling cheng` 的 compile crash 重试已移除（固定 `retry=0`）；若崩溃会直接失败并输出 `TOOLING_COMPILE_CRASH_REPORT_DIR`（默认 `artifacts/tooling_compile_crash`）。
- `cheng_tooling cheng` 的 stage0 preflight 自动重建已移除（固定 `no-auto-rebuild`）；preflight 失败直接硬失败。
- stage0 preflight 默认重试 3 次并接受 `--help` 的 `rc=1` 作为健康返回；可用 `TOOLING_STAGE0_PREFLIGHT_RETRIES`、`TOOLING_STAGE0_PREFLIGHT_WRAP` 调整。
- selfhost perf 阈值默认来自 `src/tooling/selfhost_perf_baseline.env`；可用 `SELFHOST_PERF_BASELINE=<path>` 切换基线文件，或直接用 `SELFHOST_PERF_MAX_*` 覆盖单项阈值。
- 默认额外执行 `backend.ci.multi_perf_regression`（对 `verify_backend_multi` + `verify_backend_multi_lto` 做阈值检查）。
- multi perf 阈值默认来自 `src/tooling/multi_perf_baseline.env`；可用 `MULTI_PERF_BASELINE=<path>` 切换基线文件，或直接用 `MULTI_PERF_MAX_*` 覆盖阈值。

UIR 稳定性闸口（默认不在闭环中开启）：
```bash
UIR_STABILITY_ITERS=5 UIR_STABILITY_MODES=dict cheng_tooling verify_backend_uir_stability
```
- 该脚本会对关键 fixture 重复构建对象产物并比较哈希，确保 `UIR` 在同口径下可复现。
- 环境变量：`UIR_STABILITY_ITERS`（默认 3）、`UIR_STABILITY_MODES`（默认 `dict`）、`UIR_STABILITY_FIXTURES`（默认包含核心 cfg/分支优化类 fixture）、`UIR_STABILITY_OUT_DIR`（默认 `artifacts/backend_uir_stability`）、`UIR_STABILITY_OPT_LEVEL`、`UIR_STABILITY_FRONTEND`，以及与产线一致的 `UIR_OPT2_ITERS`、`UIR_OPT3_ITERS`、`UIR_OPT3_CLEANUP_ITERS`、`UIR_CFG_CANON_ITERS`、`UIR_SIMD*`。

后端生产闭环（默认快速口径；fullchain/stress 按需开启）：
```bash
cheng_tooling backend_prod_closure
cheng_tooling backend_prod_closure --fullchain
cheng_tooling backend_prod_closure --stress

# 全量闭环（strict all-gates profile）
cheng_tooling backend_full_closure
# 等价快捷写法（必须紧跟在 backend_prod_closure 后）
cheng_tooling backend_prod_closure --full-closure
```
说明：
- `backend_full_closure` 会统一注入 `selfhost + fullchain + stress + dedicated 100ms gate`，并保持完整 gate 集（不做 profile 降级）。
- `backend_full_closure` 默认会将 `BACKEND_PROD_REQUIRE_DRIVER_FULL_REBUILD` 置为 `0`（仅对该别名生效），
  避免在已知 full-rebuild 阻断期被前置硬门禁提前终止；若你需要保留前置硬门禁，可显式导出 `BACKEND_PROD_REQUIRE_DRIVER_FULL_REBUILD=1` 再运行。
- `backend_full_closure` 默认将 selfhost 设为“可复用稳定口径”（`BACKEND_PROD_SELFHOST_NATIVE_COMPILE_STAGE1=0`、
  `BACKEND_PROD_SELFHOST_STAGE1_FULL_REBUILD=0`、`BACKEND_PROD_SELFHOST_STAGE1_REQUIRE_REBUILD=0`），
  并将 `SELFHOST_100MS_FULL_ENFORCE_ON_HOST=0`，确保 100ms full 轨在编译型回归期仍以报告方式收口。
- `backend_full_closure` 默认关闭易波动的自举附加探针（`stage0_no_compat / strict_noreuse / selfhost_parallel_perf / driver_selfbuild_smoke`），
  以保证全链主 gate 可连续执行；如需恢复这些探针，可在命令末尾显式追加对应启用参数。
- 若未显式传 `--seed/--seed-id/--seed-tar`，`backend_full_closure` 会自动补 `--no-publish`，避免发布链路因缺 seed 直接失败。
- `backend_prod_closure` 默认不跑 fullchain/stress；可用 `--fullchain` / `BACKEND_RUN_FULLCHAIN=1` 和 `--stress` / `BACKEND_RUN_STRESS=1` 显式开启。
- `backend_prod_closure` 现优先走 `cheng_tooling` 原生子命令执行路径（不再默认加载超大 embedded payload）；`run backend_prod_closure` 与 multicall 入口同样走原生路由。
- 一旦开启 `--fullchain`，`backend.fullchain_bootstrap.obj_only` 按 required gate 执行；不再使用 optional/best-effort 语义。
- `verify_fullchain_bootstrap` 的 stage1 fullspec 子阶段不再从 parallel 失败自动回退 serial；失败会直接阻断 fullchain。
- `backend_prod_closure` 默认不跑 selfhost 自举（`BACKEND_RUN_SELFHOST=0`）；如需启用自举与相关性能/探针 gate，显式传 `--selfhost`（或设 `BACKEND_RUN_SELFHOST=1`）。
- `backend_prod_closure` 默认在 `backend.closedloop` 中开启 fullspec（等价注入 `BACKEND_RUN_FULLSPEC=1`）；`BACKEND_RUN_FULLSPEC=0` 已不再支持。
- `backend_prod_closure --uir-stability` 会在 `opt3` 与 `ssa` 闸口之后执行 `backend.uir_stability`；默认已开启（`BACKEND_RUN_UIR_STABILITY=1`）。
- `backend_prod_closure` 默认 strict：任一步骤 `exit 2`（skip）会直接失败；`--allow-skip` 已不再支持。
- `backend_prod_closure --no-publish` 为兼容 no-op（native 闭环默认不执行发布尾链），且不再走 stable-profile 降级语义，默认保持完整 dev gate 集。
- 发布链路改由 `backend-prod-publish` 专门执行，并强制 `release_system_link -> release_manifest -> release_bundle -> release_sign -> release_verify -> release_publish` required 全链；缺失 OpenSSL/签名材料/验签失败会直接阻断（不再 best-effort）。
- `backend_prod_closure` gate runner 默认只认 canonical tooling 路径 `artifacts/tooling_cmd/cheng_tooling`；如需临时回退非 canonical，仅可显式设置 `BACKEND_PROD_ALLOW_NONCANONICAL_TOOL_BIN=1`。
- `backend_prod_closure` 默认新增并阻断 `backend.compile_name_canonical`（`cheng_tooling verify_backend_compile_name_canonical`）：强制指定生态仓的编译命令统一使用 `cheng`，禁止 `cheng_tooling compile/chengc` 旧调用残留。
- `backend_prod_closure` 默认新增并阻断 `backend.dev_track_only`（`cheng_tooling verify_backend_dev_track_only`）：强制 `cheng` 不接受 `--release`，并阻断 `BACKEND_BUILD_TRACK=release` 抬升、`BACKEND_LINKER` 环境覆盖；`compile/chengc` 入口必须返回 `rc=2`。
- `backend_prod_closure` 现在固定执行默认 no-pointer 生产口径；若外部显式抬升到旧 ABI/裸指针配置面会直接报错。no-pointer CLI 收口由 `verify_backend_noptr_default_cli` 单独执行。
- `backend_prod_closure` 已不再包含独立 `backend.abi_v2_noptr` required gate；兼容命令 `verify_backend_abi_v2_noptr` 保留为 `verify_backend_noptr_default_cli` 别名。
- `backend_prod_closure` required 链路包含 `backend.rawptr_contract`（`cheng_tooling verify_backend_rawptr_contract`），并单独执行 `backend.rawptr_surface_forbid` 与 `backend.rawptr_closedloop`。
- `backend.pure_cheng_surface`（`cheng_tooling verify_backend_pure_cheng_surface`）现在进入 `backend_prod_closure` required 图，作为 strict sidecar/proof active contract 的硬门禁；它禁止 active surface 回流到 `backend_driver_c_sidecar_*`、`driver_c_build_module_stage1*`、`backend_driver_uir_sidecar_runtime_compat.c`、`emergency_c` 和 `dist/releases/current/cheng`。runtime host 的 direct build path 也按同一规范固定为“direct-export only”，PURE-01 会静态阻断 `driver_c_build_module_stage1_direct -> cheng_sidecar_build_module_symbol` 的直接回流。
- `backend_prod_closure` required 现默认先执行 `backend.seed_uir_import`（`cheng_tooling verify_backend_seed_uir_import`）；`backend.sidecar_cheng_seed` 已退出默认 closure 图，保留为专项 native-substrate smoke。
- 相邻 fresh gate：`cheng_tooling verify_backend_sidecar_cheng_fresh`
  - 公开 sidecar 口径只接受 `--sidecar-mode:cheng`；resolver 只认 strict-fresh Cheng sidecar contract，不再接受 `emergency_c` 或 `dist/releases/current/cheng` 一类回退。
  - gate 负责生成并校验 repo-local 稳定 snapshot：bundle / compiler / real-driver 必须落在仓库路径（`chengcache/backend_driver_sidecar` + `artifacts/backend_sidecar_cheng_fresh`），缺失、stale 或 contract 不完整都会直接失败。
  - gate 同样 compile+run 三条 smoke：`return_add`、`return_new_expr_generic_box`、`return_new_ref_seq_growth`。
  - gate 会把结果落到 `artifacts/backend_sidecar_cheng_fresh/verify_backend_sidecar_cheng_fresh.{report.txt,snapshot.env}`，并为每条 fixture 保留 `*.compile.log` / `*.run.log`。
  - 当前不再默认接入 `cheng_tooling verify`，保留为专项 native-substrate smoke，也不进入 `backend_prod_closure` required。
- `verify_backend_rawptr_surface_forbid` 的 native 路径会额外校验 `src/std/cmdline.cheng`：必须走 runtime bridge `__cheng_rt_paramCount/__cheng_rt_paramStr`（并允许 `__cheng_rt_paramStrCopy` 稳定化字符串转值），且禁止 `void* / ptr_add / alloc / copyMem / setMem / *str*` 语法回流。
- `backend_prod_closure` 现默认要求 `backend.symbol_closure`（`cheng_tooling verify_backend_symbol_closure`）：固定 `return_add/return_new_ref_seq_growth` 需可编译且可运行，阻断 `_alloc/_c_strlen/_zeroMem` 运行时符号缺失。
- `backend_prod_closure` 现默认要求 `backend.release_compile_stability`（`cheng_tooling verify_backend_release_compile_stability`）：固定 `return_new_ref_seq_growth` 默认 3 次 `release-compile` 稳定性回归（可用 `BACKEND_PROD_RELEASE_COMPILE_STABILITY_ITERS` 覆盖）；出现 `rc=139` 直接失败。`verify_backend_release_compile_stability` 单独执行时默认 3 次（可用 `BACKEND_RELEASE_COMPILE_STABILITY_ITERS` 覆盖），发布入口 `backend-prod-publish` 默认也是 3 次（可用 `BACKEND_PROD_PUBLISH_RELEASE_COMPILE_STABILITY_ITERS` 覆盖）。
- `backend_prod_closure` required 默认执行 Native FFI 影子桥接两个纯 surface 硬门禁：
  - `verify_backend_ffi_slice_shim`（slice -> ptr/len）
  - `verify_backend_ffi_outptr_tuple`（`@ffi_out_ptrs` tuple 降级）
- `verify_backend_ffi_handle_sandbox` 与 `verify_backend_ffi_borrow_bridge` 已退出默认 closure 图，保留为专项 native-substrate gate。
- `backend_prod_closure` 现默认要求 `backend.zero_script_residual`（`cheng_tooling verify_backend_zero_script_residual`）：阻断 required 路径 compile-only/skip 语义与 legacy `CHENG_*` 执行路径读取残留。
- `backend_prod_closure` 默认 `BACKEND_PROD_INMEM_ONLY=1`；若开启 `--fullchain`，需显式设置 `BACKEND_PROD_INMEM_ONLY=0`，否则直接失败（不再 skip `backend.fullchain_bootstrap`）。
- `backend_prod_closure` 现默认要求 `backend.opt2_impl_surface`（`cheng_tooling verify_backend_opt2_impl_surface`）：阻断 `uir_core_opt2` 占位实现回归，并检查 `UIR_SSU/UIR_NOALIAS_NJVL_LITE` 默认开关与 noalias 扩展字段可观测性。
- `backend_prod_closure` 现默认要求 `backend.uir_soa_surface`（`cheng_tooling verify_backend_uir_soa_surface`）：除校验 `uir_core_types` 的 SoA/index surface（`Uir*Id`、`UirCoreSoa`、`uirCoreSoaNew/Append*`）外，还要求 runtime probe 输出 `soa_report` 且 `balance_ok=1`；门禁会双次运行时探针并断言两次 `soa_report` 一致（determinism）。
- `verify_backend_uir_soa_surface` 现为 runtime-only：禁止 source-surface fallback。优先要求 runtime `soa_report`；若命中旧 driver 且缺失 `soa_report`，则要求 runtime 日志同时包含 `noalias_report + ssu_report`，并写入 `runtime_report_mode=runtime_legacy_surface`。
- `backend_prod_closure` 现默认要求 `backend.stage1_ast_soa_surface`（`cheng_tooling verify_stage1_ast_soa_surface`）：输出并比对 `stage1_ref_surface/uir_ref_surface/rawptr_surface` 快照；默认硬阻断（`BACKEND_PROD_STAGE1_AST_SOA_ENFORCE=1`），仅在显式设 `BACKEND_PROD_STAGE1_AST_SOA_ENFORCE=0` 时退回报告模式。
- `verify_backend_uir_soa_surface` 默认 runtime probe 使用 `emit=exe + system-link`；如需额外验证 self-link 子探针，可设置 `BACKEND_UIR_SOA_SELF_PROBE=1`（阻断失败再加 `BACKEND_UIR_SOA_SELF_PROBE_ENFORCE=1`）。
- `backend_prod_closure` required 默认额外执行 `backend.uir_soa_self_probe`（`cheng_tooling verify_backend_uir_soa_self_probe`），固定以 `BACKEND_UIR_SOA_SELF_PROBE=1 + BACKEND_UIR_SOA_SELF_PROBE_ENFORCE=1` 口径阻断。
- `verify_backend_noalias_opt` / `verify_backend_egraph_cost` / `verify_backend_dod_opt_regression` 现已收敛到 proof-phase surface：compile stamp 固定审计 `single_ir_dual_phase` / `p4_phase_v1` / `stage1_ownership_fixed_0_effective`，高层 phase 统计固定从 `generics_report` 读取 `high_uir_checked_funcs` / `high_uir_fallback_funcs` / `low_uir_lowered_funcs`。这些 gate 不再接受“只看到 pass 名称但 ownership proof 没进来”的假阳性。
- `verify_backend_noalias_opt` 与 `verify_backend_dod_opt_regression` 默认固定 `UIR_PROFILE=0` + `BACKEND_PROFILE=0`，使用 `compile_stamp + generics_report + noalias_report` 做 proof-backed 验收；`verify_backend_egraph_cost` 为了验 `rewrite/cost` profile，固定 `UIR_PROFILE=1` 且仍要求同一份 `compile_stamp + generics_report` phase surface。
- `verify_backend_mir_borrow` / `verify_backend_noalias_opt` / `verify_backend_egraph_cost` / `verify_backend_dod_opt_regression` 现在共享 proof-phase driver preflight：优先尝试 `BACKEND_PROOF_PHASE_DRIVER`，然后优先消费 current-source strict proof 产物 `artifacts/backend_selfhost_self_obj/probe_currentsrc_proof/cheng.stage2`（以及可用时的 `cheng.stage3.witness`）。这两份 bare current-source 产物现在会发布邻接的 `cheng.stage2.meta` / `cheng.stage2.compile_stamp.txt` 与 `cheng.stage3.witness.meta` / `cheng.stage3.witness.compile_stamp.txt`，作为 ownership-on published witness。若 current-source published surface 缺失，再消费 strict selfhost 发布的 `artifacts/backend_selfhost_self_obj/probe_prod.strict.noreuse/cheng.stage2.proof` 与 `cheng.stage3.witness.proof`。若这些正式 proof 产物仍缺失，才回退到组合面和较旧 candidate。若 ownership-on surface 统一卡在 `Cannot write while borrowed`，而 phase-off surface 仍报告 `stage1_ownership_fixed_0_effective=1`，这些 gate 会统一失败为 `ownership-on surfaces are blocked by compiler borrow regression`，不再回退到旧 surface 继续做假阳性验收。
- `verify_backend_mir_borrow` / `verify_backend_noalias_opt` / `verify_backend_egraph_cost` / `verify_backend_dod_opt_regression` 的 current-source 默认执行面现已完全收口：若 `artifacts/backend_selfhost_self_obj/probe_currentsrc_proof/cheng.stage2` 存在，则 preflight 会优先以这份 bare current-source `cheng.stage2` 同时作为 `phase_driver` 与 `phase_proof_driver`；bare published witness 元数据不再允许出现 `sidecar_compiler` 或 `exec_fallback_outer_driver`。`cheng.stage2.proof` 继续保留为兼容 launcher / fallback witness，但不再是默认 current-source witness。

- `verify_backend_selfhost_bootstrap_self_obj` 与 `verify_backend_selfhost_currentsrc_proof`：默认 bootstrap 输入已经固定为 `src/backend/tooling/backend_driver_proof.cheng`，不再默认拿 `src/backend/tooling/backend_driver.cheng` 当 strict stage0 publish 源。`verify_backend_selfhost_currentsrc_proof` 以这份 proof 输入稳定发布 current-source proof surface 到 `artifacts/backend_selfhost_self_obj/probe_currentsrc_proof/`。默认 `fast` 仍固定使用 canonical `artifacts/backend_driver/cheng` 作为 stage0；若 `probe_currentsrc_proof/cheng.stage2` 已存在，`fast` 默认允许复用这条 fresh lineage，但仍必须重跑 bare `cheng.stage2` 与 `cheng.stage2.proof` 的 direct compile+run smoke。切到 `SELFHOST_CURRENTSRC_PROOF_MODE=strict` 时，wrapper 会优先复用已经 fresh 的 `probe_currentsrc_proof/cheng.stage2` 作为 stage0，并默认打开 `SELF_OBJ_BOOTSTRAP_STRICT_ALLOW_FAST_REUSE=1`、`SELF_OBJ_BOOTSTRAP_STRICT_STAGE2_ALIAS=1`、`SELF_OBJ_BOOTSTRAP_SKIP_SMOKE=1`。这意味着 strict current-source proof 默认验证的是“沿当前 fresh current-source proof lineage 继续发布 strict witness”，而不是再重复跑一轮更老的 bootstrap `hello_puts` smoke。fast alias 模式下，`cheng.stage2.proof` 会直接复用 `stage1` 的 proof stamp/meta，不再重复跑一条没有增量信息的 publish probe；与此同时，gate 还必须给 bare `cheng.stage2` 写出邻接 `cheng.stage2.meta` / `cheng.stage2.compile_stamp.txt`，并同时实际用 `cheng.stage2.proof` 和 bare `cheng.stage2` 直接编译 `tests/cheng/backend/fixtures/return_i64.cheng`，再运行生成的可执行文件，结果分别记到 `cheng.stage2.proof.smoke.report.txt` 与 `cheng.stage2.smoke.report.txt`。bare current-source witness 的 published metadata 不再允许出现 `sidecar_compiler` 或 `exec_fallback_outer_driver`。
- Host-only strict 默认：`SELFHOST_STRICT_REBUILD=1`、`BUILD_DRIVER_STRICT_NATIVE=1`；build-driver fallback 路径已硬关闭。
  - `verify_backend_selfhost_100ms_host` 默认 `--compile-stage1`，并按 strict 口径强制 `FULL_REBUILD=1 + REQUIRE_REBUILD=1`。
  - 100ms 报告新增结构化字段：`stage0_driver_kind/fallback_used/quarantine_cleaned/lock_wait_ms/strict_rebuild_ok`。
  - `build-backend-driver` 已移除 `shim/reused_stage0/legacy relink` 回退；strict 口径对“native 产物 sanity 失败”直接失败。
- `verify_backend_release_c_o3_lto` 已升级为 required 的 release system-link gate：不再允许 known-blocker 放行，默认强制执行 `system-link + O3/LTO` 并运行产物。
- `verify_backend_dod_opt_regression` 默认执行 noalias 负例回归（guard fixture off/on 对象一致）与 egraph/cost model 确定性双跑（对象 `cmp -s` 一致），并输出 `artifacts/backend_dod_opt_regression/*.report.txt`。
- `backend_prod_closure` 默认新增并阻断 `backend.linker_abi_core`（`cheng_tooling verify_backend_linker_abi_core`）：当前收口为 host-only（darwin arm64）自研 self-link ABI 检查，固定 canonical driver，无回退硬失败。
- `verify_backend_linker_abi_core_cross` 提供跨目标可观测探针（darwin/linux/windows）：默认强制模式（阻断），可用 `--report-only` 或 `BACKEND_LINKER_ABI_CORE_CROSS_ENFORCE=0` 切到报告模式。
- `verify_backend_linker_abi_core_cross` 支持 `--targets:<csv|@file>`（或 `BACKEND_LINKER_ABI_CORE_CROSS_TARGETS`）按需收缩/扩展探针目标集合；`@file` 支持 csv/newline 列表。
- `verify_backend_linker_abi_core_cross` 支持 `--report-only`（等价 `--enforce:0`）；未显式指定 targets 时默认读取 `src/tooling/linker_abi_core_cross.targets.report`（兼容旧文件 `src/tooling/linker_abi_core_cross.targets`）。
- `backend_prod_closure` 可显式启用 cross 探针：`--linker-abi-core-cross`（默认关闭）；可叠加 `--linker-abi-core-cross-report-only`、`--linker-abi-core-cross-enforce:1` 与 `--linker-abi-core-cross-targets:<csv|@file>`。
- `backend_prod_closure` 在 `--linker-abi-core-cross-enforce:1` 且未指定 targets 时，默认收敛到 `src/tooling/linker_abi_core_cross.targets.host_required`（避免误把当前未收口的跨目标能力缺口当成主链阻断）。
- `backend.linker_abi_core` 与 `verify_backend_self_linker_{elf,coff}` 默认固定 stable driver 口径（`artifacts/backend_driver/cheng`），不再自动回退 seed/selfhost/release 候选。
- `verify_backend_hotpatch` / `verify_backend_hotpatch_meta` 现固定 required `self-link` 运行态口径：`BACKEND_HOTPATCH_GATE_LINKER` 只允许 `self`，不再接受 system-link 降级；命中 unsupported target 直接失败（不再 `status=skip`）。
- `verify_backend_hotpatch` / `verify_backend_hotpatch_meta` 在 required 口径下禁用 runnable 重试回退（`BACKEND_HOTPATCH_RUNNABLE_RETRIES>1` 会直接失败）。
- `verify_backend_emit_obj_contract` 已移除（命令固定 `rc=2`）；主口径改为“非 release 可执行构建禁止 `emit=obj`”，以 driver 硬策略阻断。
- `backend_prod_closure` 支持 `BACKEND_OPT_DRIVER`：可仅覆盖 `backend.determinism_strict/exe_determinism_strict` 与 `backend.opt/opt2/multi_lto/multi_perf/opt3/simd/uir_stability/ssa/ffi/sanitizer/debug/exe_determinism` gate driver，不影响主 required-gates driver。
- `verify_backend_determinism_strict` / `verify_backend_exe_determinism_strict` 默认固定到 stable release/system 口径：`BACKEND_BUILD_TRACK=release`、`BACKEND_LINKER=system`、`BACKEND_DIRECT_EXE=0`、`BACKEND_LINKERLESS_INMEM=0`。仅在显式传入 `BACKEND_LINKER=self` 时才走 self-link 变体。
- `backend_prod_closure` 默认新增并阻断 `backend.noptr_default_cli`（`cheng_tooling verify_backend_noptr_default_cli`）与 `backend.noptr_exemption_scope`（`cheng_tooling verify_backend_noptr_exemption_scope`），确保“默认入口零指针”与“豁免范围仅限自举/探针 allowlist”。
- `backend.abi_v2_noptr` 默认优先使用本地 `artifacts/backend_driver/cheng`（要求具备 user-surface no-pointer 诊断 marker）；可用 `BACKEND_ABI_V2_DRIVER` 显式覆盖。
- `backend_prod_closure` 主门禁链路固定稳定 driver（默认来自 `backend_driver_path`，即 `artifacts/backend_driver/cheng`），不再自动切换到 selfhost/stage2 候选。
- 若 canonical driver 体检失败，`backend_prod_closure` 会直接失败；请先修复或重建 `artifacts/backend_driver/cheng` 后重试。
- `backend_prod_closure` 在主 driver 完成体检后会导出 `BACKEND_DRIVER_PATH_STAGE1_STRICT_SMOKE=0`，后续 gate 不再重复 strict smoke（保留主链稳定性并减少抖动）。
- `verify_backend_closedloop` 默认执行 `backend.spawn_api_gate`（`cheng_tooling verify_backend_spawn_api_gate`）；该 gate 已切换到不依赖 `std/async_rt` 指针实现的 fixture，因此在 `ABI=v2_noptr` 下也可直接回归。
- `verify_backend_closedloop` 默认执行 `backend.import_cycle_predeclare`（`cheng_tooling verify_backend_import_cycle_predeclare`），持续验证“循环导入硬错误 + 无前置声明可编译”口径；该 gate 已切换为纯 runtime 断言（负例必须编译失败并输出 `Import cycle detected: ... -> ...` 链路），不再允许 source-contract fallback。
- `verify_backend_closedloop` 默认执行 `backend.release_system_link`（`cheng_tooling verify_backend_release_c_o3_lto`）与 `backend.dual_track`（`cheng_tooling verify_backend_dual_track`），确保 Dev/Release 双轨策略持续可回归。
- `verify_backend_closedloop` 默认执行 `backend.mir_borrow`（`cheng_tooling verify_backend_mir_borrow`），确保 `ownership/borrow -> noalias` 语义下沉与 `compile_stamp/generics_report` 字段持续可回归。
- `verify_backend_closedloop` 默认执行并阻断 `backend.linker_abi_core`（`cheng_tooling verify_backend_linker_abi_core`）与 `backend.self_linker.(elf|coff)`，跨目标 self-link 门禁不允许 skip 降级。
- `verify_backend_closedloop` 默认 `BACKEND_MATRIX_STRICT=1` 且 `BACKEND_RUN_FULLSPEC=1`：`status=2` 视为失败，fullspec 固定执行编译+运行（返回码为 0），不再使用 compile-only fallback。
- `verify_backend_obj_fullspec_gate` 默认复用已有 `artifacts/backend_obj_fullspec_gate/backend_obj_fullspec`（避免重复冷编译超时）；如需强制重编可设 `BACKEND_OBJ_FULLSPEC_REBUILD_ON_SOURCE=1` 或 `BACKEND_OBJ_FULLSPEC_REBUILD_ON_DRIVER=1`。
- `backend_prod_closure` 在启用 selfhost 时，默认将自举阶段单次编译超时设为 `60s`（`BACKEND_PROD_SELFHOST_TIMEOUT`），并默认启用 RSS 守卫 `8192MB`（`BACKEND_PROD_SELFHOST_MAX_RSS_MB`，设 `0` 关闭）。
- `backend_prod_closure` 低内存档位默认开启（`BACKEND_PROD_LOW_MEM_PROFILE=1`，可用 `--no-low-mem` 或 `BACKEND_PROD_LOW_MEM_PROFILE=0` 关闭）：会强制串行编译口径（`BACKEND_MULTI=0/BACKEND_MULTI_FORCE=0/BACKEND_INCREMENTAL=0/BACKEND_JOBS<=2`），默认关闭 `multi_perf/selfhost_parallel_perf/driver_selfbuild_smoke`，并把 selfhost/driver-selfbuild 默认 RSS 上限收敛到 `8192MB`（可用 `BACKEND_PROD_LOW_MEM_RSS_MB`、`BACKEND_PROD_LOW_MEM_JOBS` 覆盖）。
- `backend_prod_closure` 默认对每个 gate 施加 `60s` 超时（`BACKEND_PROD_GATE_TIMEOUT`，设为 `0` 可关闭），超时直接失败并打印 gate 标签。
- `backend_prod_closure` 默认开启超时诊断（`BACKEND_PROD_TIMEOUT_DIAG=1`）：当 gate 超时会自动采样并输出诊断文件路径（默认目录 `chengcache/backend_timeout_diag`，采样时长 `BACKEND_PROD_TIMEOUT_DIAG_SECONDS=5`，可通过 `BACKEND_PROD_TIMEOUT_DIAG_TOOL` 覆盖采样器命令）。
- `backend_prod_closure` 默认开启超时摘要（`BACKEND_PROD_TIMEOUT_DIAG_SUMMARY=1`）：超时后会调用 `cheng_tooling summarize_timeout_diag` 输出热点 TopN（`BACKEND_PROD_TIMEOUT_DIAG_SUMMARY_TOP`）。
- `backend_prod_closure` 在启用 selfhost 时默认开启 `backend.selfhost_perf_regression`（`BACKEND_RUN_SELFHOST_PERF=1`），读取 `selfhost_timing_<session>.tsv` 检查 stage1/stage2/total 阈值并在超阈值时失败。
- `backend.selfhost_perf_regression` 默认加载 `src/tooling/selfhost_perf_baseline.env`（可通过 `SELFHOST_PERF_BASELINE` 覆盖）；发布 bundle 会附带该基线文件以保证阈值口径一致。
- `backend_prod_closure` 在启用 dedicated 100ms gate（`BACKEND_RUN_SELFHOST_100MS=1`）时会拆成双轨：
  - `backend.selfhost_100ms_host.quick`（报告轨，默认 `BACKEND_RUN_SELFHOST_100MS_QUICK=1`，`SELFHOST_STAGE1_FULL_REBUILD=0`）。
  - `backend.selfhost_100ms_host.full`（阻断轨，默认 `BACKEND_RUN_SELFHOST_100MS_FULL=1`，`SELFHOST_STAGE1_FULL_REBUILD=1` + `SELFHOST_STAGE1_REQUIRE_REBUILD=1`）。
- 双轨参数默认从 `src/tooling/selfhost_perf_100ms_host.env` 读取（可设 `BACKEND_PROD_SELFHOST_100MS_BASELINE` 覆盖），并分别使用 gate 超时：
  - quick: `BACKEND_PROD_SELFHOST_100MS_QUICK_GATE_TIMEOUT`（默认 `60s`）
  - full: `BACKEND_PROD_SELFHOST_100MS_FULL_GATE_TIMEOUT`（默认 `240s`）
- full 轨默认开启编译分段画像：`BACKEND_PROD_SELFHOST_100MS_FULL_PROFILE=1`，输出 `BACKEND_BUILD_DRIVER_PROFILE_OUT`（默认 `artifacts/backend_selfhost_100ms_host/full.build.profile.log`）；可通过 `build_backend_driver_profile_log=<path>` 回读。
- `backend_prod_closure` 默认关闭 `backend.multi_perf_regression`（`BACKEND_RUN_MULTI_PERF=0`）；可用 `--multi-perf` 或 `BACKEND_RUN_MULTI_PERF=1` 在专用 perf 机器上开启，对 `backend.multi`/`backend.multi_lto` 做并行编译性能阈值检查。
- `backend.multi_perf_regression` 默认加载 `src/tooling/multi_perf_baseline.env`（可通过 `MULTI_PERF_BASELINE` 覆盖）；可用 `BACKEND_PROD_MULTI_PERF_TIMEOUT` 或 `MULTI_PERF_TIMEOUT` 调整单 gate 超时。
- 当 `BACKEND_RUN_FULLSPEC=1` 时，`backend.closedloop` 使用独立超时 `BACKEND_PROD_CLOSEDLOOP_TIMEOUT`（默认 `60s`），避免 fullspec 编译被通用 60s gate 误杀。
- `backend_prod_closure` 默认以最小闭环模式执行 `backend.closedloop`（`BACKEND_PROD_CLOSEDLOOP_MINIMAL=1`）；如需回放完整 closedloop 集合可显式设 `BACKEND_PROD_CLOSEDLOOP_MINIMAL=0`。
- `backend_prod_closure` 的 selfhost 自举步骤会显式设置 `STAGE1_NO_POINTERS_NON_C_ABI=0` 与 `STAGE1_NO_POINTERS_NON_C_ABI_INTERNAL=0`（仅用于编译器/运行时内部源码），用户源码 no-pointer 策略仍由后续 `backend.closedloop`/`backend.abi_v2_noptr` 门禁强制。
- `backend_prod_closure` 在启用 selfhost 时默认自举模式为 `fast`；可用 `--selfhost-strict`（或 `BACKEND_PROD_SELFHOST_MODE=strict`）切到 fixed-point 口径。
- `backend_prod_closure` 提供显式参数 `--selfhost-fast` / `--selfhost-strict`（优先级高于环境变量）；启用 selfhost 后默认启用 `--selfhost-strict-gate`（可用 `BACKEND_RUN_SELFHOST_STRICT=0` 关闭）在 `fast` 主链后追加一轮 `strict` 自举门禁。
- `--selfhost-strict-gate` 默认复用 fast 同一 session（避免重复冷编译）；可用 `BACKEND_PROD_SELFHOST_STRICT_SESSION=<name>` 指定独立 strict session。
- `--selfhost-strict-gate` 默认启用 `BACKEND_PROD_SELFHOST_STRICT_ALLOW_FAST_REUSE=1`：strict 口径优先复用 fast 生成的 `stage1/stage2` 产物，避免因源码时间戳触发重复冷编译超时；设为 `0` 可恢复 strict 重编口径。
- C-Drop 应急 gate 默认关闭：`BACKEND_RUN_CDROP_EMERGENCY=0`；显式设为 `1` 时，`backend_prod_closure` 会追加执行 `backend.cdrop_emergency`（`cheng_tooling verify_backend_cdrop_emergency`）。
- `backend_prod_closure` 在启用 selfhost 时默认阻断 `backend.stage0_no_compat`（可用 `--no-stage0-no-compat-gate` 或 `BACKEND_RUN_STAGE0_NO_COMPAT_GATE=0` 关闭）：强制 `stage0 -> stage1` 原生编译，禁止 compat overlay（`SELF_OBJ_BOOTSTRAP_STAGE0_COMPAT=0`）。
- `backend.stage0_no_compat` 默认参数：`mode=fast`、`reuse=0`、`validate=0`、`skip_smoke=1`、`require_runnable=0`；fast 默认 `bootstrap_multi=1`，并使用真实编译器源码 `stage1_input=src/backend/tooling/backend_driver.cheng`（前移语法不兼容失败）；如需轻量排障可显式设 `STAGE0_NO_COMPAT_STAGE1_INPUT=tests/cheng/backend/fixtures/return_add.cheng`。可用 `BACKEND_PROD_STAGE0_NO_COMPAT_*` 覆盖（含 `..._GATE_TIMEOUT`、`..._STAGE0`、`..._SESSION`）。
- `backend_prod_closure` 在启用 selfhost 时默认开启 strict no-reuse 冷路径探针（固定 `reuse=0`、`strict_allow_fast_reuse=0`，阻断模式）；默认启用 alias-off fast（`BACKEND_PROD_SELFHOST_STRICT_NOREUSE_ALIAS_OFF_FAST=1`），可用 `--no-selfhost-strict-noreuse-probe` 或 `BACKEND_RUN_SELFHOST_STRICT_NOREUSE_PROBE=0` 关闭。
- strict no-reuse 探针默认阻断主链：默认 `gate=110s`（`BACKEND_PROD_SELFHOST_STRICT_NOREUSE_GATE_TIMEOUT`）与内部 `probe=90s`（`BACKEND_PROD_SELFHOST_STRICT_NOREUSE_PROBE_TIMEOUT`），并自动夹到 `< gate timeout` 以避免外层 gate 误判；会话可用 `BACKEND_PROD_SELFHOST_STRICT_NOREUSE_SESSION` 覆盖。
- `verify_backend_selfhost_strict_noreuse_probe` 默认使用 `SELFHOST_STRICT_PROBE_GENERIC_MODE=dict`、`SELFHOST_STRICT_PROBE_SKIP_CPROFILE=1`、`SELFHOST_STRICT_PROBE_REQUIRE_RUNNABLE=0`、`SELFHOST_STRICT_PROBE_MULTI=0`、`SELFHOST_STRICT_PROBE_MULTI_FORCE=0`、`SELFHOST_STRICT_PROBE_ALLOW_RETRY=0`、`SELFHOST_STRICT_PROBE_PREFLIGHT=1`（默认 `20s` 预检）；并行 worker 探针若命中 `fork worker failed / unit file not found / crash` 会在进入 stage1 前自动回退串行，避免“并行失败 + 重试”导致门禁耗时放大。当 stage0 seed 明确无法编译 `backend_driver.cheng` 时，软探针会快速 skip 并输出 preflight 日志路径，避免固定等待到 90s/110s 超时。
- 并行专项性能检查：`cheng_tooling verify_backend_selfhost_parallel_perf`（同一 stage0 下依次跑 serial/multi 两轮 strict no-reuse）。probe/build 失败始终阻断；`parallel <= serial + 2s` 的性能阈值默认仅作报告，专用 perf 主机上可设 `SELFHOST_PARALLEL_PERF_ENFORCE_ON_HOST=1`（可配合 `SELFHOST_PARALLEL_PERF_HOST_ONLY=<tag>`）升级为阻断。报告末尾会追加一行 `summary`，明确当前是 `dedicated_host_pending`、`dedicated_host_verified`、`report_only_slow` 还是 `probe_failed`。当前 darwin/arm64 enforced witness 已记录在 `artifacts/backend_selfhost_self_obj/selfhost_parallel_perf_dedicated_witness.tsv`。`backend_prod_closure` 在启用 selfhost 时默认不执行该 gate（`BACKEND_RUN_SELFHOST_PARALLEL_PERF=0`），可用 `--selfhost-parallel-perf` 或 `BACKEND_RUN_SELFHOST_PARALLEL_PERF=1` 开启。
- 新增新 driver 自举 smoke 阻断 gate：`cheng_tooling verify_backend_driver_selfbuild_smoke`。`backend_prod_closure` 在启用 selfhost 时默认不执行 `backend.driver_selfbuild_smoke`（`BACKEND_RUN_DRIVER_SELFBUILD_SMOKE=0`），可用 `--driver-selfbuild-smoke` 或 `BACKEND_RUN_DRIVER_SELFBUILD_SMOKE=1` 开启；默认 gate 超时 `60s`（`BACKEND_PROD_DRIVER_SELFBUILD_SMOKE_TIMEOUT`），内部自举超时 `55s`（`BACKEND_PROD_DRIVER_SELFBUILD_SMOKE_BUILD_TIMEOUT`），stage1 smoke 超时默认 `60s`（`DRIVER_SELFBUILD_SMOKE_STAGE1_TIMEOUT`），并默认启用 RSS 守卫 `8192MB`（`BACKEND_PROD_DRIVER_SELFBUILD_SMOKE_MAX_RSS_MB` / `DRIVER_SELFBUILD_SMOKE_MAX_RSS_MB`，设 `0` 关闭）；默认输出与主 driver 统一为 `artifacts/backend_driver/cheng`（可用 `BACKEND_PROD_DRIVER_SELFBUILD_SMOKE_OUTPUT` 覆盖）；生产闭环会强制 `DRIVER_SELFBUILD_SMOKE_SKIP_SEM=0`/`DRIVER_SELFBUILD_SMOKE_SKIP_OWNERSHIP=0`，避免 smoke 语义被环境变量降级；`--help` 探针日志默认落盘到 `<report-dir>/selfbuild_smoke.help.<pid>.log` 便于诊断。失败会输出 `build_log/smoke_log/attempt_report/attempt_log/crash_report` 与根因分类（`cause`）。
- `verify_backend_driver_selfbuild_smoke` 默认执行严格阻断：`build_rc!=0` 或 `smoke_rc!=0` 直接失败，不再接受“重编失败但 smoke 成功”的降级通过语义；`REQUIRE_REBUILD=1` 仍可用于显式声明必须重编口径。
- `backend_prod_closure` 的 selfhost stage0 固定使用 canonical driver（`artifacts/backend_driver/cheng`），不再接受环境变量覆盖。
- 若未显式提供 stage0，`backend_prod_closure` 默认仅使用稳定 driver（`artifacts/backend_driver/cheng`）；不再自动回退其它候选。
- stage0 探针默认走 `BACKEND_PROD_STAGE0_PROBE_MODE=path`（仅复用 `backend_driver_path` 的可运行 smoke，不再额外重编 `backend_driver.cheng`）；可切到 `light`（stage1 小样例）或 `full`（历史重型探针）排查问题。
- `backend_prod_closure` 在启用 selfhost 时默认固定复用口径：`BACKEND_PROD_SELFHOST_REUSE=1`、`BACKEND_PROD_SELFHOST_SESSION=prod`（可覆盖），减少重复冷编译与并发互踩。
- `verify_backend_selfhost_bootstrap_self_obj` 会把 stage0 driver 固化拷贝到 `artifacts/backend_selfhost_self_obj/cheng_stage0_<session>` 后再执行，且仅在 stage0 **内容变化**时更新该副本，避免因时间戳漂移触发无效重编。
- `verify_backend_selfhost_bootstrap_self_obj` 在 `SELF_OBJ_BOOTSTRAP_REUSE=1` 下会复用 smoke 产物（`hello_puts`）并仅在依赖变化时重编，降低重复验收耗时。
- `verify_backend_selfhost_bootstrap_self_obj` 固定 `GENERIC_MODE=dict`；并且 timeout 回收采用进程组+进程本体双重 kill，避免遗留孤儿编译进程。脚本默认启用 RSS 守卫 `SELF_OBJ_BOOTSTRAP_MAX_RSS_MB=24576`（设 `0` 关闭），并在启动前回收 `artifacts/backend_selfhost_self_obj` 下的孤儿 `cheng_stage*` 进程。
- stage0 UE 隔离默认强制跨工作区匹配并阻断，不再提供 orphan soft-bypass。
- stage0 路径守卫默认对“退出态 UE”（state 含 `E`）开启软旁路（`TOOLING_STAGE0_ALLOW_EXITING_UE_SOFT_BYPASS=1`），并在 quarantine 自动清理中默认尝试回收相关父进程链（`TOOLING_STAGE0_QUARANTINE_KILL_PARENT_CHAIN=1`），降低 `timeout wrapper -> sh lock -> UE child` 残留导致的持续阻断。
- `backend_prod_closure` 默认执行 `backend.coff_lld_link`；`verify_backend_coff_lld_link` 在缺少 `lld-link/llvm-lld/ld.lld/lld` 时自动回退为 COFF `obj-only` 校验（不再 skip）。
- `backend_prod_closure` 结束时会输出 `backend_prod_closure.timing_top`（按耗时降序的 gate top 列表），用于持续压缩闭环耗时。
- 生产/CI 可用 `--require-seed` 禁止使用本机 stage0；`--require-seed` 需要显式传 `--seed/--seed-id/--seed-tar`。
- 发布（默认开启）要求显式 seed：需传 `--seed/--seed-id/--seed-tar`；仅做本地闭环可加 `--no-publish`。
- `cheng_tooling closedloop` 在 `CLOSEDLOOP_PROD=1` 下默认以 `--no-publish` 运行 `backend_prod_closure`；若需要发布门禁，请显式设置 `CLOSEDLOOP_BACKEND_PROD_ARGS` 传入 seed 参数。
- 在上述 `backend_prod_closure` 调用中，`backend.closedloop` 默认会跑 fullspec（`BACKEND_RUN_FULLSPEC=1`）；`BACKEND_RUN_FULLSPEC=0` 已不再支持。
- `cheng_tooling closedloop` 也支持 `CLOSEDLOOP_UIR_STABILITY=1`，会自动把 `backend_prod_closure --uir-stability` 加到生产闭环参数（与 `CLOSEDLOOP_BACKEND_PROD_ARGS` 合并）。
- `cheng_tooling closedloop` 默认导出 `BACKEND_PROD_GATE_TIMEOUT=60`、`BACKEND_PROD_SELFHOST_TIMEOUT=60`、`BACKEND_PROD_TIMEOUT_DIAG=1`、`BACKEND_RUN_SELFHOST_PERF=1`。
- `cheng_tooling closedloop` 在 `CLOSEDLOOP_PROD=1` 下默认 `STAGE1_FULLSPEC=0`（可通过 `STAGE1_FULLSPEC=1` 或 `CLOSEDLOOP_STAGE1_FULLSPEC_DEFAULT=1` 开启）；开启时默认导出 `STAGE1_FULLSPEC_TIMEOUT=60`（可显式覆盖）。
- `cheng_tooling closedloop` 支持 `CLOSEDLOOP_FRONTIER=auto|0|1`（默认 `auto`）：自动检测到 `cheng-libp2p` 仓库时追加 `verify_libp2p_frontier`；`auto` 默认非阻断（`CLOSEDLOOP_FRONTIER_SOFT_AUTO=1`），显式 `1` 仍为阻断门禁。
- `cheng_tooling closedloop` 在 `CLOSEDLOOP_LIBP2P=1` 时执行 `verify_libp2p_prod_closure`（不再是 smoke-only）。
- `cheng_tooling closedloop` 末尾默认尝试输出 timeout 采样摘要（`CLOSEDLOOP_TIMEOUT_SUMMARY=1`）以及 stage1 fullspec 超时摘要（`CLOSEDLOOP_STAGE1_TIMEOUT_SUMMARY=1`）。
- `build_backend_driver` fast 轨默认 `BACKEND_BUILD_DRIVER_MULTI=0`、`BACKEND_BUILD_DRIVER_INCREMENTAL=1`，并默认开启 `BACKEND_BUILD_DRIVER_QUICK_SHARED_CACHE=1`，允许同一份源码/stage0/参数在不同 `--out` 之间复用 quick 产物；quick/shared stamp 现在额外绑定 `source_capability_signature`，覆盖与 stage0 capability preflight 相同的源码列表，其中包含完整 `uir_core_builder` split-helper 家族。seal 轨默认 `BACKEND_BUILD_DRIVER_REBUILD_MULTI=0`、`BACKEND_BUILD_DRIVER_REBUILD_INCREMENTAL=0`。需要并行时可显式覆盖 jobs/multi。
- fast 路径下“保留现有 canonical driver”不再顺手清掉 quick/cap/smoke 侧车；历史清理继续只在 seal/canonical 安装收口时执行，避免把 fast reuse 条件自己擦掉。
- `build_backend_driver` 未显式指定 stage0 时，会优先尝试健康的 `artifacts/backend_selfhost_self_obj/cheng.stage2`，再回落到 canonical/seed/dist 候选。
- `build_backend_driver` fast/seal 两轨默认都走 `system`，并固定 `GENERIC_MODE=dict` / `GENERIC_LOWERING=mir_dict`；如需实验 fast self-link，可显式设 `BACKEND_BUILD_DRIVER_FAST_STRICT_NATIVE=0`。seal 编译尝试超时默认 `60s`（`BACKEND_BUILD_DRIVER_REBUILD_TIMEOUT`）并启用 RSS 守卫 `24576MB`（`BACKEND_BUILD_DRIVER_MAX_RSS_MB`，设 `0` 关闭）；默认硬失败不回写 stage0（`BACKEND_BUILD_DRIVER_NO_RECOVER=1`）；仅显式设 `BACKEND_BUILD_DRIVER_NO_RECOVER=0` 才允许失败后复用健康 stage0。
- `build_backend_driver` 自举编译仅注入必需的 `BACKEND_*`/`STAGE1_*`（不含已移除的 skip env）环境变量，保持 seed stage0 口径稳定。
- 在 `MM=orc BACKEND_LINKER=self` 口径下，`opt/opt2/multi-lto/ssa/debug`（以及可选 `stress`）gate 已统一固定串行口径（`BACKEND_MULTI=0`、`BACKEND_MULTI_FORCE=0`；`whole_program` 内部固定为 `1`）以保证稳定可复现。
- `verify_backend_concurrency_stress` 默认跳过（`BACKEND_CONCURRENCY_STRESS_ENABLED=0`）；需要并发压力回归时显式设置 `BACKEND_CONCURRENCY_STRESS_ENABLED=1`。
- `verify_backend_stress` 默认使用 stage1 smoke（`hello_puts`）循环运行（`BACKEND_STRESS_N`，默认 10），并对编译与单次运行施加 `60s` 超时（`BACKEND_STRESS_TIMEOUT`）。
- `verify_backend_concurrency_stress` 在启用后（`BACKEND_CONCURRENCY_STRESS_ENABLED=1`）对编译与单次运行施加 `60s` 超时（`BACKEND_CONCURRENCY_TIMEOUT`）。
- `verify_backend_ffi_abi` 在 `BACKEND_LINKER=self` 口径应直接通过，不依赖脚本内额外兜底逻辑。

后端热点定位（sample，可执行版）：
```bash
# 自动构建并执行 C 版采样器（命令 ID: profile_backend_sample）
cheng_tooling profile_backend_sample --preset:fullchain-cold --duration:20 --top:12 --kill-after-sample

# 直接指定命令；支持对子进程名 attach，避免采到脚本壳 wait4
cheng_tooling profile_backend_sample --duration:12 --attach-substr:cheng.stage2 -- \
  env CLEAN_CHENG_LOCAL=0 cheng_tooling verify_fullchain_bootstrap
```
说明：
- `cheng_tooling profile_backend_sample` 现在仅做命令分发：首次会调用 `cheng_tooling build_profile_backend_sample` 生成 `artifacts/bin/profile_backend_sample`（C 可执行文件）。
- 采样参数为命令行选项（`--preset/--duration/--interval-ms/--attach-substr/...`），不再依赖 shell 字符串拼接。
- preset 默认 `--attach-substr:cheng`，优先采样编译器子进程而非脚本父进程。
- `profile_backend_sample` 会从 sample 输出的 call graph 提取并按计数排序热点帧（过滤 `Thread_/main/start` 等噪声），便于快速定位热函数。
- `cheng_tooling summarize_timeout_diag` 可离线汇总 timeout 采样文件：
  - `cheng_tooling summarize_timeout_diag --dir:chengcache/backend_timeout_diag --latest:3 --top:12`
  - `cheng_tooling summarize_timeout_diag --file:chengcache/backend_timeout_diag/<file>.sample.txt --top:20`
- `cheng_tooling summarize_stage1_timeout_diag` 可离线汇总 stage1 fullspec 超时诊断：
  - `cheng_tooling summarize_stage1_timeout_diag --latest:3`
  - `cheng_tooling summarize_stage1_timeout_diag --dir:chengcache/stage1_fullspec_timeout_diag --latest:5`
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
MM=orc BACKEND_LINKER=system cheng_tooling verify_backend_closedloop
MM=orc BACKEND_LINKER=system BACKEND_RUN_FULLSPEC=1 cheng_tooling verify_backend_closedloop
MM=orc cheng_tooling verify_backend_self_linker_riscv64
```
说明：
- `verify_backend_closedloop` 默认 `BACKEND_RUN_FULLSPEC=1` 且 `BACKEND_MATRIX_STRICT=1`（fullspec 编译+运行门禁为 required）；`BACKEND_RUN_FULLSPEC=0` / `BACKEND_MATRIX_STRICT=0` 已不再支持。
- `verify_backend_closedloop` 已纳入 `backend.spawn_api_gate`，对应脚本为 `cheng_tooling verify_backend_spawn_api_gate`（默认 API 禁 raw spawn、legacy 显式入口可用；当前 `fn()` 入口正向口径为 `spawn(entry)`）。
- `BACKEND_RUN_FULLSPEC=1` 使用 `examples/backend_closedloop_fullspec.cheng`；可用 `BACKEND_CLOSEDLOOP_FULLSPEC_INPUT` 覆盖输入文件。
- fullspec 编译默认口径为 `STAGE1_SEM_FIXED_0=0`、`STAGE1_OWNERSHIP_FIXED_0=0`、`BACKEND_FULLSPEC_GENERIC_MODE=dict`、`BACKEND_FULLSPEC_GENERIC_SPEC_BUDGET=0`、`BACKEND_FULLSPEC_VALIDATE=0`，其中 `whole_program` 在编译器内部固定为 `1`，并固定 `BACKEND_CLOSEDLOOP_FULLSPEC_MULTI=0`（默认串行编译；需要并行时显式覆盖）；`BACKEND_FULLSPEC_SKIP_SEM/BACKEND_FULLSPEC_SKIP_OWNERSHIP` 已移除为可配置项。
- `verify_backend_closedloop` 在 fullspec 编译后新增 `backend.closedloop_fullspec.symcheck`：若可用 `nm`，会阻断未解析 `seqBytesOf_T` 符号回归。
- `verify_backend_mm` 固定运行 `mm_live_balance + mm_container_balance`（required runtime gate）；`BACKEND_MM_CONTAINER=0` 已不再支持。容器用例前端可用 `BACKEND_MM_CONTAINER_FRONTEND` 覆盖（默认 `stage1`）。
- `verify_backend_multi` 保持并行优先；并行失败会自动串行重试，避免偶发并行崩溃阻断闭环。
- `verify_backend_self_linker_riscv64` 默认口径为 `MM=orc`，并优先复用 `artifacts/backend_selfhost_self_obj/cheng.stage2`（若存在）以避免 gate 内重建 driver 触发超时。

UIR float 回归：
```bash
cheng_tooling verify_backend_float
```
说明：
- `verify_backend_float` 当前覆盖 `return_float64_ops`、`return_float32_roundtrip`、`return_float_mixed_int_cast`、`return_float_compare_cast`、`return_float32_arith_chain`；当前口径已验证 `f64/f32` 算术、比较与 `f64/f32 <-> int` 基础 cast 语义（向零截断）。

Linux AArch64 no-libc 独立验收（不接入默认 verify）：
```bash
BACKEND_ELF_PROFILE=nolibc cheng_tooling verify_backend_nolibc_linux_aarch64
```
说明：
- 静态验收（macOS 可跑）：检查 `elf64-littleaarch64`、无 `PT_INTERP`/`PT_DYNAMIC`、无 undefined symbols、无 `libc.so/ld-linux-aarch64` 字符串。

libp2p frontier 编译探针：
```bash
cheng_tooling verify_libp2p_frontier
```
说明：
- 脚本支持 `--help`；未知参数会直接报错退出。
- import 探针（`cheng_probe_conn_import` / `cheng_probe_multihash_import_only`）默认走 `stage1` 前端（`FRONTIER_IMPORT_FRONTEND=stage1`），超时默认 `30s`（`FRONTIER_IMPORT_TIMEOUT`）。
- 重量探针（`mdns_smoke` / `msquic_transport_smoke`）保持 `stage1` 前端，超时默认 `60s`（`FRONTIER_TIMEOUT`）。
- frontier 默认使用 `FRONTIER_GENERIC_MODE=dict`、`FRONTIER_GENERIC_SPEC_BUDGET=0`；生产闭环口径固定 `dict`，用于抑制重量依赖图的 monomorphize 放大。
- frontier stage1 口径固定为 `FRONTIER_SKIP_SEM=0`、`FRONTIER_SKIP_OWNERSHIP=0`；`FRONTIER_VALIDATE` 仍可用于控制 backend UIR validate（默认 `0`）。
- frontier 默认开启超时诊断（`FRONTIER_TIMEOUT_DIAG=1`）：超时时自动采样并打印诊断路径（默认目录 `chengcache/libp2p_frontier/logs/timeout_diag`，采样时长 `FRONTIER_TIMEOUT_DIAG_SECONDS=5`）。
- frontier 默认开启超时摘要（`FRONTIER_TIMEOUT_DIAG_SUMMARY=1`，`FRONTIER_TIMEOUT_DIAG_SUMMARY_TOP=12`）。
- 日志与 summary 位于 `chengcache/libp2p_frontier/logs/`。
- 运行验收（仅 Linux aarch64 主机）：执行 `hello_puts/return_add/mm_live_balance` smoke。
- 跨仓口径说明：
  - `verify_libp2p_frontier` 固定 `dict`；`hybrid` 已从执行路径移除并在 tooling/driver 层硬拒绝。
  - 外部仓 `cheng-libp2p/scripts/verify.sh` 现已收敛为 `stable=dict + exe+run + 60s` 的默认闭环门禁；`full` 保留为诊断/对照口径（可用环境变量改 emit/timeout/compile-only）。
  - libp2p 生产闭环入口固定 dict-only。

libp2p 生产闭环一键门禁：
```bash
cheng_tooling verify_libp2p_prod_closure
```
说明：
- 顺序执行外部仓 `cheng-libp2p/scripts/verify.sh` 的 `stable/full`，并执行 `verify_libp2p_frontier`。
- 默认 `build/run` 超时阈值为 `60s`（可通过 `LIBP2P_BUILD_TIMEOUT` / `LIBP2P_RUN_TIMEOUT` 覆盖）。
- 默认保留 `hybrid` 拒绝回归校验（外部仓 `verify.sh` / `backend_build.sh` + 本仓 `verify_libp2p_frontier`）；可用 `LIBP2P_CHECK_HYBRID_REJECT=0` 关闭。

运行时 ABI 一致性门禁：
```bash
cheng_tooling verify_backend_runtime_abi
```
说明：
- 校验 `src/runtime/native/system_helpers.h` 与 C/runtime 后端实现的符号一致性（含 `_addr`/`__addr` 别名兼容）。
- `verify.sh`、`verify_backend_closedloop` 与 CI（Linux amd64 / macOS arm64）会执行此检查。

标准库导入入口门禁：
```bash
cheng_tooling verify_std_import_surface
```
说明：
- 校验 `src/stage1`、`src/backend`、`src/tooling`、`src/decentralized`、`src/web` 下的 Cheng 源码不再直接导入 `cheng/stdlib/bootstrap/*`。
- 约束统一入口为 `std/*`；`src/stdlib/bootstrap` 已移除，不作为可用 import 路径。
- 同时门禁禁止显式泛型 `reserve[T](...)` 调用；统一使用 `reserve(xs, n)` 或 `xs.cap = n`。

标准库目录同步门禁：
```bash
cheng_tooling verify_std_layout_sync
```
说明：
- 校验 `src/std` 主目录完整性，并确保 `src/stdlib/bootstrap` 旧路径已移除（迁移收敛门禁）。
- `verify_backend_closedloop` 会执行该门禁。

Stage1 seed 布局门禁：
```bash
cheng_tooling verify_stage1_seed_layout
```
说明：
- 主 seed 口径为 backend 可执行 seed（优先 `artifacts/backend_seed/cheng.stage2`）。
- `src/stage1/frontend_bootstrap.seed.c` 必须不存在。
- `verify.sh` 与 `verify_backend_closedloop` 会执行该门禁。
- `verify.sh` 默认导出 `BACKEND_PROD_GATE_TIMEOUT=60`、`BACKEND_PROD_SELFHOST_TIMEOUT=60`、`BACKEND_PROD_TIMEOUT_DIAG=1`、`BACKEND_RUN_SELFHOST=0`；可通过 `VERIFY_LIBP2P_FRONTIER=1` 追加 `libp2p frontier` 探针门禁。
- `verify.sh` 默认同时导出 `BACKEND_PROD_TIMEOUT_DIAG_SUMMARY=1` 与 `FRONTIER_TIMEOUT_DIAG_SUMMARY=1`（超时后自动打印热点摘要）。
- `verify.sh` 默认把 `verify.tooling_cmdline` 作为必跑 gate（`cheng_tooling verify_tooling_cmdline`）。
- `verify.sh` 默认把 `verify.backend_stage1_fixed0_envs` 作为必跑 gate，阻断 `STAGE1_SKIP_SEM/STAGE1_SKIP_OWNERSHIP` 回流，并要求 `STAGE1_SEM_FIXED_0/STAGE1_OWNERSHIP_FIXED_0` 保持 `fixed=0`。
- `verify.sh` 默认把 `verify.backend_string_literal_regression` 作为必跑 gate，复跑 canonical driver 的 4 个字符串字面量 probe，并保留超时采样报告。
- `verify.sh` 默认把 `verify.std_strformat` 作为必跑 gate，锁定 `std/strformat` 的 `fmt(str[]) / lines(str[])` 基本行为。
- `verify.sh` 默认把 `verify.backend_default_output_safety` 作为必跑 gate，阻断 canonical driver 在无显式输出路径时覆盖输入源码的回归。
- `verify.sh` 默认优先使用 `bundle full` 入口运行 `chengc/verify_backend_closedloop/backend_prod_closure`（缺失时自动构建）：
  - `VERIFY_USE_TOOLING_BUNDLE_FULL=1`（默认开启）
  - `VERIFY_TOOLING_BUNDLE_FULL_AUTO_BUILD=1`（默认开启）
  - `VERIFY_TOOLING_BUNDLE_OUT_DIR`（默认 `artifacts/tooling_bundle`）
  - `VERIFY_TOOLING_BUNDLE_FULL_BIN_DIR`（默认 `artifacts/tooling_bundle/full/bin`）
  - `VERIFY_TOOLING_BUNDLE_LINKER`（默认 `system`）
- `backend_prod_closure` 默认把 required gates 收口到 `full bundle` 入口（直接执行 `artifacts/tooling_bundle/full/bin/<gate>`），并强制：
  - `TOOLING_EXEC_BUNDLE_PROFILE=full`
  - `TOOLING_EXEC_REQUIRE_BUNDLE=1`
  - `TOOLING_EXEC_BUNDLE_CORE_AUTO_BUILD=0`
- 新增阻断门禁：`backend.zero_script_closure`（`cheng_tooling verify_backend_zero_script_closure`），禁止 `backend_prod_closure` 直调 `sh src/tooling/<id>.sh`。
- `verify_tooling_cmdline` 现在同时校验：
  - `full profile`（闭环核心入口）
  - `full profile + TOOLING_EXEC_REQUIRE_BUNDLE=1`（抽样入口 `verify_no_legacy_net_multiformats_imports`）
  - repo wrapper 直通 gate：`verify --help`、`verify_backend_stage1_fixed0_envs`、`verify_backend_string_literal_regression`、`verify_std_strformat`、`verify_backend_default_output_safety`、`verify_new_expr_surface`、`verify_backend_string_abi_contract`、`verify_backend_dot_lowering_contract`、`verify_backend_selfhost_currentsrc_proof`、`chengc --help`
  - canonical wrapper 直通 gate：`verify --help`、`verify_backend_stage1_fixed0_envs`、`verify_backend_string_literal_regression`、`verify_std_strformat`、`verify_backend_default_output_safety`、`verify_new_expr_surface`、`verify_backend_string_abi_contract`、`verify_backend_dot_lowering_contract`、`verify_backend_selfhost_currentsrc_proof`、`chengc --help`
  - `install` surface：安装目录里必须出现 `verify_new_expr_surface`、`verify_backend_string_abi_contract`、`verify_backend_dot_lowering_contract`、`verify_backend_selfhost_currentsrc_proof`，并且 `--help` 可直接执行
  - `bundle --profile:full` surface：`full/bin` 与 `full/manifest.tsv` 里必须同时包含 `verify_new_expr_surface`、`verify_backend_string_abi_contract`、`verify_backend_dot_lowering_contract`、`verify_backend_selfhost_currentsrc_proof`，并且 bundled wrapper 的 `--help` 可直接执行
- `verify.sh` 在运行 `verify.backend_prod_closure` 前会预构建 `full bundle`（`build.tooling_bundle_full` / `bundle.tooling_full`），以满足零脚本闭环的 bundle 预置要求。
- `verify.sh` 的 `chengc` smoke case 默认固定 canonical driver（`artifacts/backend_driver/cheng`）；不再使用环境变量覆盖 driver。
- `verify.sh` 的轻量 `chengc` smoke 默认使用 `VERIFY_SMOKE_LINKER=system`（避免开发态 self-link 抖动导致误报）；如需对照 self-link 可显式设 `VERIFY_SMOKE_LINKER=self`。
- `verify.sh` 轻量 case 默认使用 `VERIFY_FRONTEND=stage1`。
- `list_comp` 与 `pkg_import_srcroot` 默认走 `build` 模式（快速 obj 编译校验，不再默认跳过）：
  - `VERIFY_LIST_COMP_MODE=build|run|skip`（兼容 `VERIFY_LIST_COMP=1` -> `run`）。
  - `VERIFY_PKG_IMPORT_MODE=build|run|skip`（兼容 `VERIFY_PKG_IMPORT=1` -> `run`）。
- `pkg_import_srcroot` 的 `build` 模式默认使用 `stage1` + fixed-zero 语义/所有权 + `dict`（可通过 `VERIFY_PKG_IMPORT_BUILD_*` 覆盖）；`run` 模式默认 `VERIFY_PKG_IMPORT_FRONTEND=stage1`。

Cheng skill 一致性门禁：
```bash
cheng_tooling verify_cheng_skill_consistency
```
说明：
- 扫描 `docs/cheng-skill/` 与 `~/.codex/skills/cheng语言` 的关键文件漂移，并检查禁用语法。
- 默认执行最小编译+运行抽样；可用 `SKILL_COMPILE=0` 只做文档检查。
- 编译抽样默认优先复用 `artifacts/backend_driver/cheng`（其次 `artifacts/backend_seed/cheng.stage2`），并使用 `stage1` 前端口径（`SKILL_FRONTEND=stage1`）。
- 可用 `SKILL_DRIVER=<path>` 显式指定 gate driver；仅当 `SKILL_DRIVER_ALLOW_SELFHOST=1` 时才允许回退 `backend_selfhost_self_obj/*`。
- `verify.sh` 默认执行该门禁；可用 `VERIFY_SKILL=0` 临时跳过。

## 包管理闭环

常用脚本：
- `cheng_tooling cheng_pkg_pack`：把包目录打成 `.tar.gz` 快照。
- `cheng_tooling cheng_pkg_publish`：打包 -> 上传存储 -> 发布 registry 快照。
- `cheng_tooling cheng_pkg_fetch`：根据 lock 拉取快照并输出 `PKG_ROOTS`。
- `src/tooling/cheng_pkg_source.cheng`：源码清单（manifest）构建/发布/本地拉取（local-only core binary）。

示例：
```bash
cheng_tooling cheng_pkg_publish --src:/Users/lbcheng/.cheng-packages/cheng-libp2p --package:pkg://cheng/libp2p \
  --author:node:alice --channel:edge --epoch:1 --priv:keys/alice.priv

./cheng_registry resolve --package:pkg://cheng/libp2p --channel:edge --registry:build/cheng_registry/registry.jsonl
./cheng_pkg resolve --manifest:docs/cheng-package-manifest.toml --registry:build/cheng_registry/registry.jsonl \
  --out:build/cheng_pkg/cheng.lock.toml

cheng_tooling cheng_pkg_fetch --lock:build/cheng_pkg/cheng.lock.toml --print-roots
```

源码直发（no-copy）示例（local-only core binary）：
```bash
cheng_tooling cheng_pkg_publish --src:/Users/lbcheng/.cheng-packages/cheng-libp2p --package:pkg://cheng/libp2p \
  --author:node:alice --channel:edge --epoch:1 --priv:keys/alice.priv --format:source \
  --source-addr:/ip4/127.0.0.1/tcp/4005

# 当前 core binary 的 `fetch` 仅支持 local store（`--mode:local`）。
cheng_tooling cheng_pkg_fetch --lock:build/cheng_pkg/cheng.lock.toml --print-roots \
  --source-peer:/ip4/127.0.0.1/tcp/4005
```

## 内存模型开关

以下构建脚本支持 `--mm:<orc>`（兼容 `--orc`），并映射 `MM=orc`：

- `cheng_tooling build_unimaker_desktop`
- `cheng_tooling package_ide`
- `cheng_tooling package_unimaker_desktop`
- `cheng_tooling build_mobile_export`
- `cheng_tooling mobile_ci_android`
- `cheng_tooling mobile_ci_ios`
- `cheng_tooling mobile_ci_harmony`
- `cheng_tooling mobile_run_android`
- `cheng_tooling mobile_run_ios`
- `cheng_tooling mobile_run_harmony`
- `cheng_tooling verify_mobile_run_entrypoints`

说明：
- `cheng_tooling build_mobile_export` 在导出 Android 工程时，会优先使用 `src/tooling/mobile/android/cheng_mobile_host_android.c` 覆盖模板，避免部分设备 `ANativeWindow_lock` 返回非 4BPP buffer 导致的越界写崩溃。
- Android 导出与运行链路默认启用 Kotlin-only 校验：
  - `app/src/main/kotlin` 必须存在
  - `app/src/main` 下出现 `.java` 源文件会直接失败
- 统一入口支持：
  - `cheng_tooling cheng run android <file.cheng>`（支持 `--no-build --no-install --no-run --serial: --native`）
  - `cheng_tooling cheng run ios <file.cheng>`
  - `cheng_tooling cheng run harmony <file.cheng>`
- 快速门禁（不依赖真机安装）：
  - `cheng_tooling verify_mobile_run_entrypoints`
- `cheng_tooling verify_mobile_crash_free`（默认对 `build_mobile_export`、`cheng(light fixture)`、`cheng(mobile fixture)`三组都执行：串行 50 + 并发 4x20；要求 `rc=138/139` 为 0）

## bootstrap（已退役）

`cheng_tooling bootstrap` 已迁移为 `cheng_tooling bootstrap-pure` 原生入口。

请统一使用 `bootstrap_pure`：

```bash
cheng_tooling bootstrap_pure
```

## bootstrap_pure

```bash
cheng_tooling bootstrap_pure
```

说明：
- 不依赖 C 编译器；走 obj/exe + self-linker 完成 stage2 自举。
  - 固定使用 canonical driver：`artifacts/backend_driver/cheng`。
  - 若 canonical driver 不可用，先执行 `cheng_tooling build-backend-driver`。
  - `--mode:fast|strict`：`strict` 走 3-stage + SHA-256 fixed-point；`fast` 保持 stage2 alias 快路径。
  - 支持 `--fullspec`：自举成功后继续执行 `verify_fullchain_bootstrap`（obj-only fullchain gate）。
  - 不再支持 `--skip-determinism`。

## cheng_storage（去中心化存储/计算 CLI）

说明：
- 支持存储、租约、计量、执行请求、结算与审计等。
- 计算计量覆盖 CPU/内存/IO/GPU，支持 `--gpu_ms/--gpu_mem_bytes/--gpu_count/--gpu_type/--workload` 等参数。
- 完整示例见 `docs/cheng-decentralized-compute-storage.md`。

示例：
```bash
cheng_tooling cheng src/tooling/cheng_storage.cheng --frontend:stage1 --emit:exe --out:cheng_storage
# chengc 默认输出：./artifacts/chengc/cheng_storage
./cheng_storage exec --task:job-gpu-1 --package:pkg://cheng/fs --author:node:alice --requester:node:app-1 \
  --gpu_ms:180000 --gpu_mem_bytes:17179869184 --gpu_count:1 --gpu_type:A10G --workload:train \
  --price_gpu:0.00002 --price_gpu_mem:0.15 --epoch:1 --root:build/cheng_storage --mode:local
```

补充：
- `chengc` 固定 `MM=orc`；传入 `--mm:off`/`MM=off` 会直接报错。
