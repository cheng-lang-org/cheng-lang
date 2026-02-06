# Cheng 工具链（Stage0c/Stage1）

当前仓库提供多条可用链路：

1. **Stage0 `bin/cheng`**：把 `.cheng` 源码直接编译为 C（stdout 或 `--out:` 文件）。
   - 直接汇编：`bin/cheng --mode:asm --file:<input.cheng> --out:<output.s>`（v0：仅常量返回）。
2. **Stage1 `./stage1_runner`**：Cheng 自举产物（源码位于 `src/stage1`），支持直接编译 `.cheng -> .c`（`--mode:c --file:... --out:...`），用于“纯 Cheng”工具链入口。
3. **`chengc.sh` 构建图**：基于模块依赖清单与缓存键实现**增量**与**并行调度**（`-j`）。
4. **完全自举**：Stage0→Stage1（中间生成 bootstrap runner），并校验 Stage1 输出确定性（见 `bootstrap.sh`）。
5. **纯 Cheng 自举**：不调用旧回退；若缺少 `./stage1_runner` 会从 `src/stage1/stage1_runner.seed.c` 构建种子（见 `bootstrap_pure.sh`）。

## chengc.sh

```bash
sh src/tooling/chengc.sh examples/stage1_codegen_fullspec.cheng --jobs:8
./stage1_codegen_fullspec
```

说明：
- 生成的依赖清单位于 `chengcache/<name>.deps.list`。
- 生成的中间产物位于 `chengcache/<name>.c`。
- 可通过 `--name:<exeName>` 改输出二进制名。
- 可选 `--mm:<orc|off>`（或 `--orc/--off`）设置内存模型；默认 `orc`（默认走 C 后端作为内存安全主路径；在支持平台可用 `--off` 回到自研 obj+self-link 路径做回归/排障）。
- 包管理器接入：可选传入 `--manifest/--lock/--registry`，会生成构建元数据 `chengcache/<name>.buildmeta.toml`。
- 可选 `--pkg-cache:<dir>` 指定包缓存目录（默认 `chengcache/packages`，或 `CHENG_PKG_CACHE` 环境变量）。
- 当 lock 存在时会拉取依赖包并设置 `CHENG_PKG_ROOTS` 供 `cheng/<pkg>/...` 域名导入使用；生产包根以 `src/` 为模块根（`cheng/<pkg>/<path>` -> `<pkgroot>/src/<path>.cheng`）。`CHENG_PKG_ROOTS` 可指向包根列表或容器根（如 `~/.cheng-packages`，解析会优先尝试 `cheng-<pkg>`）。
- 源码直发拉取可通过 `--source-peer`/`--source-listen` 或环境变量 `CHENG_PKG_SOURCE_PEERS`/`CHENG_PKG_SOURCE_LISTEN` 指定源地址。
- 依赖拉取模式/peer：`CHENG_PKG_MODE=local|p2p`/`CHENG_PKG_PEERS`（兼容旧变量 `CHENG_IO_MODE`/`CHENG_IO_PEERS`）。
- 注册中心元数据：当提供 `--package/--channel` 时，会写入 `[snapshot]`（`cid/author_id/signature/pub_key`）。
- 构建期校验：可加 `--verify`（可选 `--ledger:<path>`）自动校验 buildmeta/lock/pkgmeta/snapshot。
- 可选 `--backend:obj` 使用自研后端直出 `.o`（`--emit-obj` 仅产出 `.o` 并跳过链接；当前支持 AArch64 + x86_64 + riscv64 目标；非本机/非 Android 目标通常建议只做 obj 产物验证）。默认 `--linker:self`（不依赖 `cc`；已支持 Darwin aarch64/x86_64、Linux aarch64/riscv64、Windows aarch64），并默认使用纯 Cheng 运行时：编译 `src/std/system_helpers_backend.cheng` 为 `chengcache/system_helpers_backend.<target>.o` 并链接。可用 `--linker:system` 回退到系统链接（`cc`），或用 `CHENG_BACKEND_RUNTIME_OBJ=<path>` 指定自定义运行时 `.o`。
- Linux AArch64 可选 no-libc profile：`CHENG_BACKEND_ELF_PROFILE=nolibc` 且 `CHENG_BACKEND_LINKER=self` 时，`chengb.sh/chengc.sh` 会切换到 `src/std/system_helpers_backend_nolibc_linux_aarch64.cheng`，并走无 `PT_INTERP`/`PT_DYNAMIC` 的静态链接口径；默认 profile 行为不变。
- 后端 driver 层（`src/backend/tooling/backend_driver.cheng`）：`CHENG_BACKEND_EMIT=obj` 只生成 relocatable `.o`；`CHENG_BACKEND_EMIT=exe` 先产出 `.o` 再按 `CHENG_BACKEND_LINKER=self|system` 链接可执行文件。

Stage1 全语法回归门禁：
```bash
sh src/tooling/verify_stage1_fullspec.sh
```

## 自研后端与全链自举

后端 driver 选择（脚本统一入口）：
```bash
# 默认优先：本地 `./backend_mvp_driver`（fresh + 可运行）
# 若缺失/过期，会自动重建本地 driver：
# - 默认关闭 selfhost（CHENG_BACKEND_BUILD_DRIVER_SELFHOST=0）
# - 优先使用 dist release seed（backend_release.tar.gz）回填本地 driver
# - 若失败，再回退到可运行的 stage2（含 legacy 路径）
sh src/tooling/backend_driver_path.sh

# 默认：tooling/verify 脚本会在退出时清理本地 `backend_mvp_driver*` 产物（移动到 `chengcache/_trash_backend_mvp_driver`），避免遗留占用空间。
# 如需保留（加速反复调试），可关闭：
# export CHENG_CLEAN_BACKEND_MVP_DRIVER_LOCAL=0
#
# `src/tooling/build_backend_driver.sh` 默认走 seed fallback（稳定优先）。
# 如需强制尝试 selfhost 重建（调试/研发用）：
# export CHENG_BACKEND_BUILD_DRIVER_SELFHOST=1
#
# 使用 selfhost stage2（用于全链自举/发布）
export CHENG_BACKEND_DRIVER=artifacts/backend_selfhost_self_obj/backend_mvp_driver.stage2
# legacy（兼容旧路径）：
# export CHENG_BACKEND_DRIVER=artifacts/backend_selfhost/backend_mvp_driver.stage2
```

后端链接环境助手（脚本统一注入 self-linker 运行时 `.o`）：
```bash
# 输出可直接给 `env` 使用的链接环境变量
sh src/tooling/backend_link_env.sh --driver:./backend_mvp_driver --target:arm64-apple-darwin --linker:self
```
说明：
- `--linker:self`：自动构建/复用 `chengcache/system_helpers.backend.cheng.<target>.o` 并输出  
  `CHENG_BACKEND_LINKER=self CHENG_BACKEND_NO_RUNTIME_C=1 CHENG_BACKEND_RUNTIME_OBJ=<...>`。
- `--linker:system`：输出 `CHENG_BACKEND_LINKER=system`。
- `--linker:auto`（默认）或未设置：不强制覆盖链接模式。

纯 Cheng（不走 C 后端）生成/更新固定 seed（stage2 driver）：
```bash
# 以“已存在的 stage2 driver”作为 seed（推荐：来自发布包/上一版本）
sh src/tooling/backend_seed_pure.sh \
  --seed:artifacts/backend_selfhost_self_obj/backend_mvp_driver.stage2 \
  --out:artifacts/backend_seed/backend_mvp_driver.stage2

# CI/生产可直接指定 seed 路径（缺失会失败，不再回退到 C seed）
export CHENG_SELF_OBJ_BOOTSTRAP_STAGE0=artifacts/backend_seed/backend_mvp_driver.stage2
sh src/tooling/backend_prod_closure.sh --only-self-obj-bootstrap
```

全链自举门禁（Darwin/arm64 必跑；其他平台 best-effort 可能 skip）：
```bash
sh src/tooling/verify_fullchain_bootstrap.sh
```
生产/CI 只跑后端直出 `.o` 的闭环（跳过 stage1_runner C 固定点；保留 obj-only fullspec + tool `--help` smoke）：
```bash
CHENG_FULLCHAIN_OBJ_ONLY=1 sh src/tooling/verify_fullchain_bootstrap.sh
```
说明：
- 若本地缺少 stage2 driver，但存在 `dist/releases/current_id.txt` 指向的发布 seed（`backend_release.tar.gz`，legacy fallback: `dist/backend/current_id.txt`），`verify_fullchain_bootstrap.sh` 会自动验签（best-effort）并解包 seed，自举生成 stage2 后继续 fullchain。
- `verify_backend_selfhost_bootstrap_self_obj.sh` 支持 `CHENG_SELF_OBJ_BOOTSTRAP_TIMEOUT=<seconds>`（默认 60）防止 stage1/stage2 自举编译长时间卡死。
  - 2026-02-06：已修复一类“无输出超时”根因（`src/std/strings.cheng` 的 `streq` 对 `nil` 比较递归调用自身）；该问题会让 selfhost 阶段卡住直到超时。
  - 修复后若 selfhost 失败，将优先以可诊断错误退出（而非长时间超时卡住）。
- `CHENG_FULLCHAIN_OBJ_ONLY=1` 默认使用 `examples/backend_obj_fullspec.cheng` 作为 obj-only 门禁样例（要求输出 `fullspec ok`）；可用 `CHENG_FULLCHAIN_OBJ_FULLSPEC_FILE=<path>` 覆盖。

CI/生产（macOS arm64）使用 dist seed 跑“后端自举 + fullchain smoke”：
```bash
sh src/tooling/verify_backend_ci_obj_only.sh
```
说明：
- 默认从 `dist/releases/current_id.txt` 解析 seed（legacy fallback: `dist/backend/current_id.txt`）；也可传 `--seed:<path>` 指定。
- 若 dist seed 不存在，会回退到 `bootstrap.sh` + `build_backend_driver.sh` 生成本机 seed（可用 `--require-seed` 禁止回退）。

后端生产闭环（默认包含全链；可跳过）：
```bash
sh src/tooling/backend_prod_closure.sh
sh src/tooling/backend_prod_closure.sh --no-fullchain
```
说明：
- `backend_prod_closure.sh` 默认以 `CHENG_FULLCHAIN_OBJ_ONLY=1` 跑 fullchain；如需 legacy（stage1_runner C 固定点）用 `--fullchain-legacy`。
- 生产/CI 可用 `--require-seed` 禁止回退到本机 stage0（会从 `dist/releases/current_id.txt` 解析 seed，legacy fallback: `dist/backend/current_id.txt`；也可用 `--seed/--seed-id/--seed-tar` 指定）。
- 在 `CHENG_MM=orc CHENG_BACKEND_LINKER=self` 口径下，`opt/opt2/multi-lto/ssa/debug/stress/concurrency` gate 已统一接入 `backend_link_env.sh`，避免遗漏 `CHENG_BACKEND_RUNTIME_OBJ` 导致中途失败。
- `verify_backend_ffi_abi.sh` 在 `CHENG_BACKEND_LINKER=self` 与 `CHENG_BACKEND_LINKER=system` 口径均应直接通过，不再依赖脚本内回退逻辑。

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
- `verify_backend_self_linker_riscv64.sh` 默认口径为 `CHENG_MM=orc`，并优先复用 `artifacts/backend_selfhost_self_obj/backend_mvp_driver.stage2`（若存在）以避免 gate 内重建 driver 触发超时。

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
- 校验 `src/runtime/native/system_helpers.h` 与 C/runtime 后端实现的符号一致性（含 `_addr`/`__addr` fallback）。
- `verify_backend_closedloop.sh` 与 CI（Linux amd64 / macOS arm64）会执行此检查。

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
- 仅允许 `src/stage1/stage1_runner.seed.c` 作为 `src/stage1` 下唯一 `*.seed.c` 文件，禁止 `src/stage1/frontend_bootstrap.seed.c` 回归。
- 默认限制 `stage1_runner.seed.c` 文件大小不超过 `20000000` 字节（可用 `CHENG_STAGE1_SEED_MAX_BYTES` 调整）。
- `verify.sh` 与 `verify_backend_closedloop.sh` 会执行该门禁。

## C 后端生产闭环

一键闭环（推荐）：
```bash
sh src/tooling/c_backend_prod_closure.sh
```

说明：
- 默认使用 `./stage1_runner` 作为 C 前端（缺失则失败）。
- 需要确定性校验时加 `--determinism`；可用 `--frontend:<path>` 覆盖。
- 可在 `./verify.sh` 中启用：`CHENG_C_BACKEND_CLOSURE=1 ./verify.sh`（可选 `CHENG_C_BACKEND_CLOSURE_ARGS=--determinism`）。

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

`src/tooling/bootstrap.sh` 已不再作为生产工具链入口（脚本会直接报错并给出迁移提示）。

请统一使用 `bootstrap_pure.sh`：

```bash
src/tooling/bootstrap_pure.sh --fullspec
```

## bootstrap_pure.sh

```bash
src/tooling/bootstrap_pure.sh --fullspec
```

说明：
- 不调用旧回退（`bin/cheng`），以 `stage1_runner` 作为种子编译器完成闭环。
  - 若不存在 `./stage1_runner`，默认使用 `src/stage1/stage1_runner.seed.c` 构建一个种子 `stage1_runner`，也可用 `--seed:<path>` 覆盖。
  - 可选 `--skip-determinism` 跳过确定性校验（等价 `CHENG_BOOTSTRAP_SKIP_DETERMINISM=1`）。

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
- `chengb.sh`/`chengc.sh` 默认 `CHENG_MM=orc`；需要回退时再显式传 `--off` 或设置 `CHENG_MM=off`。
