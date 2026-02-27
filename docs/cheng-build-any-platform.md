# Cheng 任意平台编译可执行文件指南（backend-only）

> 更新时间：2026-02-06  
> 适用范围：生产主链路（已移除 ASM 与 legacy C bootstrap 入口）

## 1. 总体原则

- 统一入口：`./cheng`（直连编译器，不依赖脚本包装）。
- 默认目标平台：`auto`（按当前运行平台自动选择 target triple）。
- 默认内存模型：`MM=orc`（建议始终显式设置）。
- 仅支持 `obj/exe` 产物，`emit-c` 与 `--backend:c` 不再作为生产路径。
- 自举闭环入口：`cheng_tooling bootstrap_pure --mode:strict --fullspec`（strict fixed-point + fullchain）。
- C-Drop 仅应急：`cheng_tooling verify_backend_cdrop_emergency`（默认不纳入发布阻断链，`BACKEND_RUN_CDROP_EMERGENCY=0`）。
- 直接命令行参数见：`docs/backend-driver-cli.md`。

---

## 2. 最小可执行命令（本机）

在仓库根目录执行：

```sh
MM=orc ./cheng \
  --frontend=stage1 \
  --emit=exe \
  --target=auto \
  --input=examples/hello_puts.cheng \
  --output=artifacts/backend_direct/hello_puts
```

- 产物：`artifacts/backend_direct/hello_puts`

### 2.1 单可执行入口（不依赖脚本）

直接调用后端驱动可执行文件（自动选择当前平台 target，并链接生成可执行文件）：

```sh
MM=orc ./cheng \
  --frontend=stage1 \
  --emit=exe \
  --target=auto \
  --input=examples/hello_puts.cheng \
  --output=artifacts/backend_direct/hello_puts
```

说明：
- `--target=auto` 会按运行平台自动映射（Darwin/Linux/Windows 的 arm64/x86_64）。
- 不传 `--target` 时，默认同样走 `auto`。
- 默认使用系统链接器路径（`cc/ld`），无需外层 `chengb` 包装。

---

## 3. 编译 Cheng 项目（入口 + import 图）

直接传入口文件给 `cheng`，编译器会解析 import 图并完成全量编译。

### 3.1 产出可执行文件（exe）

```sh
MM=orc ./cheng \
  --frontend=stage1 \
  --emit=exe \
  --target=auto \
  --linker=self \
  --input=src/main.cheng \
  --output=artifacts/backend_direct/myapp
```

### 3.2 仅产出对象文件（obj）

```sh
MM=orc \
PKG_ROOTS="/abs/pkgs/cheng-libp2p,/abs/pkgs/cheng-ai" \
./cheng \
  --frontend=stage1 \
  --emit=obj \
  --target=auto \
  --input=src/main.cheng \
  --output=artifacts/backend_direct/myapp.o
```

---

## 4. 构建运行时对象（无 main）

当需要先编译 runtime（例如 `system_helpers_backend.cheng`）时，使用：

```sh
./cheng \
  --emit=obj \
  --target=arm64-apple-darwin \
  --frontend=stage1 \
  --allow-no-main \
  --input=src/std/system_helpers_backend.cheng \
  --output=chengcache/system_helpers_backend.arm64-apple-darwin.o
```

说明：`whole_program` 已在编译器内部固定为 `1`，无需也不能再通过参数切换。

---

## 5. 平台选择建议

- `arm64-apple-darwin`：`--target=arm64-apple-darwin --emit=exe --linker=self`
- `x86_64-apple-darwin`：`--target=x86_64-apple-darwin`
- `aarch64-linux-android`：`--target=aarch64-linux-android`（需要 Android NDK/adb 配套）
- `aarch64-unknown-linux-gnu`：优先 `--linker=self`；真 no-libc 需额外设 `BACKEND_ELF_PROFILE=nolibc`
- `x86_64-unknown-linux-gnu`、`*-windows-msvc`：先 `--emit=obj` 验证产物，再按目标平台链接

---

## 6. 常见问题

### 6.1 `missing backend driver`

- 直接使用仓库根目录的 `./cheng`。
- 若需要固定其它 driver 路径，直接调用该绝对路径即可。

### 6.2 自研 linker 下运行时符号未定义

- 先显式编译 runtime `.o`（见第 4 节），再通过 `--runtime-obj=<path> --no-runtime-c` 传入。

### 6.3 Linux AArch64 真 no-libc（不走 libc）

- 要求：`--target=aarch64-unknown-linux-gnu` 且 `--linker=self`。
- 需要同时设置：`BACKEND_ELF_PROFILE=nolibc`。

---

## 7. 最小烟雾测试（ORC）

```sh
MM=orc ./cheng \
  --frontend=stage1 \
  --emit=exe \
  --target=auto \
  --input=examples/hello_puts.cheng \
  --output=chengcache/hello_puts.smoke

./chengcache/hello_puts.smoke
```

期望输出：`hello from cheng`
