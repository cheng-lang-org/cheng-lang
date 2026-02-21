# cheng 直连编译（无脚本）

## 目标

- 仅使用 `cheng`。
- 仅使用 `obj/exe` 产物。
- 不走 `emit-c` 或 `--backend:c` 兼容链路。

## 命令格式

```sh
./cheng [options] [<input.cheng> [<output>]]
```

常用选项：

- `--emit=obj|exe`
- `--target=<triple|auto>`
- `--frontend=stage1`（唯一支持值）
- `--input=<path>`（可替代位置参数）
- `--output=<path>`（可替代位置参数）
- `--linker=self`
- `--allow-no-main` / `--no-allow-no-main`
- `--whole-program` / `--no-whole-program`
- `--skip-global-init` / `--no-skip-global-init`
- `--runtime-obj=<path>`
- `--runtime-c=<path>` / `--no-runtime-c`
- `--link-objs=<obj-list-file>`
- `--jobs=<N>`

## 最小闭环（Darwin arm64 示例）

1) 先编译运行时对象（无 `main`）：

```sh
./cheng \
  --emit=obj \
  --target=arm64-apple-darwin \
  --frontend=stage1 \
  --allow-no-main \
  --whole-program \
  --input=src/std/system_helpers_backend.cheng \
  --output=chengcache/system_helpers_backend.arm64-apple-darwin.o
```

2) 再编译业务程序可执行文件：

```sh
./cheng \
  --emit=exe \
  --target=arm64-apple-darwin \
  --frontend=stage1 \
  --linker=self \
  --no-runtime-c \
  --runtime-obj=chengcache/system_helpers_backend.arm64-apple-darwin.o \
  --input=examples/hello_puts.cheng \
  --output=chengcache/hello_puts
```

## 约束

- `emit=asm` 已移除。
- `emit-c` 路径已移除，不再建议使用 `stage1` C 输出链。
