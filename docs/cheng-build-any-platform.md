# Cheng 任意平台编译可执行文件指南（backend-only）

> 更新时间：2026-02-06  
> 适用范围：生产主链路（已移除 ASM/HRT/stage0c 入口）

## 1. 总体原则

- 统一入口：
  - `src/tooling/chengb.sh`（后端直连，最简）
  - `src/tooling/chengc.sh --backend:obj|c`（需要包管理/模块构建图时）
- 默认目标平台：`auto`（按当前运行平台自动选择 target triple）。
- 默认内存模型：`CHENG_MM=orc`（建议始终显式设置）。
- 推荐策略：
  1. 先用自研后端 `obj/exe` 路径（`--backend:obj` 或 `chengb.sh`）。
  2. 若目标 ISA/链接环境不满足，再切到 `--backend:c` + 目标平台 C 工具链。

---

## 2. 最小可执行命令（本机）

在仓库根目录执行：

```sh
CHENG_MM=orc sh src/tooling/chengb.sh examples/hello_puts.cheng \
  --emit:exe \
  --out:artifacts/chengb/hello_puts \
  --run
```

- 产物：`artifacts/chengb/hello_puts`
- `--run` 在 macOS 本机直接运行；Android 目标会走 `adb shell`。

### 2.1 单可执行入口（不依赖脚本）

直接调用后端驱动可执行文件（自动选择当前平台 target，并链接生成可执行文件）：

```sh
CHENG_MM=orc ./backend_mvp_driver \
  --frontend=stage1 \
  --emit=exe \
  --target=auto \
  examples/hello_puts.cheng \
  artifacts/backend_direct/hello_puts
```

说明：
- `--target=auto` 会按运行平台自动映射（Darwin/Linux/Windows 的 arm64/x86_64）。
- 不传 `--target` 时，默认同样走 `auto`。
- 默认使用系统链接器路径（`cc/ld`），无需外层 `chengb.sh` 包装。

---

## 3. 编译 Cheng 项目（入口 + import 图）

`chengb.sh/chengc.sh` 不是“只编译一个文件文本”，而是：
- 从入口文件（如 `src/main.cheng`）出发；
- 递归解析并编译整个 `import` 依赖图；
- 最终链接成一个可执行文件。

### 3.1 无外部包依赖（纯工程内模块）

```sh
CHENG_MM=orc sh src/tooling/chengb.sh src/main.cheng \
  --emit:exe \
  --out:artifacts/chengb/myapp
```

### 3.2 有包依赖（manifest/lock/registry）

先保证有 `stage1_runner`（仅首次需要）：

```sh
sh src/tooling/bootstrap.sh
```

再做项目构建（推荐）：

```sh
CHENG_MM=orc sh src/tooling/chengc.sh src/main.cheng \
  --backend:obj \
  --name:myapp \
  --manifest:doc/cheng-package-manifest.toml \
  --lock:build/cheng_pkg/cheng.lock.toml \
  --registry:build/cheng_registry/registry.jsonl \
  --package:pkg://cheng/myapp \
  --channel:stable \
  --verify
```

说明：
- `chengc.sh` 会按 lock 拉取依赖并自动设置 `CHENG_PKG_ROOTS`。
- 不传 `--lock` 时会按 `--manifest` 自动 resolve 一个 lock（默认在 `chengcache/`）。
- 项目产物默认是当前目录下 `./myapp`（对应 `--name:myapp`）。

### 3.3 只用本地包根（不走 resolve/publish）

```sh
CHENG_MM=orc \
CHENG_PKG_ROOTS="/abs/pkgs/cheng-libp2p,/abs/pkgs/cheng-ai" \
sh src/tooling/chengc.sh src/main.cheng --backend:obj --name:myapp
```

---

## 4. 路径 A：自研后端直出可执行文件（优先）

### 4.1 通用模板（exe）

```sh
CHENG_MM=orc sh src/tooling/chengb.sh <input>.cheng \
  --emit:exe \
  --linker:self \
  --out:artifacts/chengb/<name>
```

如需覆盖自动选择，可显式传 `--target:<target-triple>`。

示例（显式指定 Darwin ARM64）：

```sh
CHENG_MM=orc sh src/tooling/chengb.sh examples/hello_puts.cheng \
  --emit:exe \
  --target:arm64-apple-darwin \
  --linker:self \
  --out:artifacts/chengb/hello_darwin_arm64
```

### 4.2 通用模板（只产出 `.o/.obj`）

```sh
CHENG_MM=orc sh src/tooling/chengb.sh <input>.cheng \
  --emit:obj \
  --out:artifacts/chengb/<name>.o
```

### 4.3 什么时候用 `chengc.sh`

当你需要 lock/pkg/buildmeta/模块并行时，用：

```sh
CHENG_MM=orc sh src/tooling/chengc.sh <input>.cheng \
  --backend:obj \
  --name:<exe-name>
```

只产出对象文件：

```sh
CHENG_MM=orc sh src/tooling/chengc.sh <input>.cheng \
  --backend:obj \
  --emit-obj \
  --obj-out:artifacts/chengc/<name>.o
```

---

## 5. 路径 B：任意平台兜底（C backend）

当目标平台不在当前自研后端直连能力内，使用 `--backend:c`，交给目标 C 工具链完成最终链接。

```sh
CHENG_MM=orc \
CC=<target-cc> \
CFLAGS="<target-cflags>" \
LDFLAGS="<target-ldflags>" \
sh src/tooling/chengc.sh <input>.cheng \
  --backend:c \
  --name:<exe-name>
```

说明：

- `CC/CFLAGS/LDFLAGS` 由目标平台工具链决定（可交叉编译）。
- 如果只要中间 C 文件：

```sh
CHENG_MM=orc sh src/tooling/chengc.sh <input>.cheng --backend:c --emit-c
```

---

## 6. 平台选择建议

- `arm64-apple-darwin`：优先 `chengb.sh --emit:exe --linker:self`
- `x86_64-apple-darwin`：优先 `chengb.sh --emit:exe`（可能需要 Rosetta 运行环境）
- `aarch64-linux-android`：可 `--emit:exe --run`（需 `adb` 设备/模拟器）
- `aarch64-unknown-linux-gnu`：优先 `--linker:self`；需要真 no-libc 时额外设 `CHENG_BACKEND_ELF_PROFILE=nolibc`
- `x86_64-unknown-linux-gnu`、`*-windows-msvc`：
  - 先做 `--emit:obj` 验证产物；
  - 再按目标工具链链接，或切 `--backend:c` 兜底。

---

## 7. 常见问题

### 7.1 `missing backend driver`

- 工具会自动解析/构建 driver；若你要固定 driver，可设：

```sh
export CHENG_BACKEND_DRIVER=/abs/path/to/backend_mvp_driver
```

### 7.2 反复重编导致慢/超时

- 长链路建议固定：

```sh
export CHENG_MM=orc
export CHENG_CLEAN_BACKEND_MVP_DRIVER_LOCAL=0
```

### 7.3 自研 linker 下运行时符号未定义

- 优先使用默认 runtime 注入（`CHENG_BACKEND_RUNTIME_OBJ` 自动生成）。
- 如需自定义 runtime `.o`：

```sh
export CHENG_BACKEND_RUNTIME_OBJ=/abs/path/to/runtime.o
```

### 7.4 Linux AArch64 真 no-libc（不走 libc）怎么用

- 仅支持 Linux AArch64 且必须 `CHENG_BACKEND_LINKER=self`。
- 开启方式：

```sh
CHENG_MM=orc \
CHENG_BACKEND_LINKER=self \
CHENG_BACKEND_ELF_PROFILE=nolibc \
sh src/tooling/chengb.sh tests/cheng/backend/fixtures/hello_puts.cheng \
  --frontend:stage1 \
  --target:aarch64-unknown-linux-gnu \
  --emit:exe \
  --out:artifacts/chengb/hello_puts.nolibc
```

- 验收脚本：

```sh
CHENG_BACKEND_ELF_PROFILE=nolibc sh src/tooling/verify_backend_nolibc_linux_aarch64.sh
```

- Darwin/arm64 主机只做静态结构验收（无 `PT_INTERP/PT_DYNAMIC` 等）；Linux aarch64 主机会额外执行运行 smoke。

---

## 8. 1 分钟烟雾测试（ORC 口径）

```sh
timeout 60 env CHENG_MM=orc CHENG_CLEAN_BACKEND_MVP_DRIVER_LOCAL=0 \
  sh src/tooling/verify_backend_obj.sh

timeout 60 env CHENG_MM=orc CHENG_CLEAN_BACKEND_MVP_DRIVER_LOCAL=0 \
  sh src/tooling/verify_backend_exe_determinism.sh

timeout 60 env CHENG_MM=orc CHENG_SELF_OBJ_BOOTSTRAP_TIMEOUT=60 \
  sh src/tooling/verify_backend_selfhost_bootstrap_self_obj.sh
```

以上三条通过，可认为“编译（emitter）+ 链接（linker）+ 自举（selfhost）”主链路在当前环境可用。

---

## 9. 最小分发闭环示例（stage2 driver）

使用最小分发物（selfhost stage2 driver）编译并运行综合语法样例：

```sh
cd /Users/lbcheng/cheng-lang
CHENG_MM=orc \
CHENG_CLEAN_BACKEND_MVP_DRIVER_LOCAL=0 \
CHENG_BACKEND_DRIVER=artifacts/backend_selfhost_self_obj/backend_mvp_driver.stage2 \
sh src/tooling/chengb.sh examples/backend_obj_fullspec.cheng \
  --frontend:stage1 \
  --emit:exe \
  --out:artifacts/chengb/backend_obj_fullspec_min

./artifacts/chengb/backend_obj_fullspec_min
```

期望输出：`fullspec ok`
