# Cheng 语言介绍

Cheng 当前主线已经收口到仓库根包 `pkg://cheng` 和根源码树 `src`。编译器内核位于 `src/core`，标准库位于 `src/std`，领域模块按目录拆分为 `libp2p`、`evomap`、`oracle`、`moq`、`fountain`、`hysteria2`、`diloco` 等。

## 入口

```sh
cc -std=c11 -O2 -Wall -Wextra -pedantic bootstrap/cheng_seed.c -o artifacts/bootstrap/cheng.stage0
artifacts/bootstrap/cheng.stage0 bootstrap-bridge
artifacts/bootstrap/cheng.stage3 build-backend-driver
artifacts/backend_driver/cheng status
```

日常编译优先使用：

```sh
artifacts/backend_driver/cheng system-link-exec \
  --root:/Users/lbcheng/cheng-lang \
  --in:/abs/path/file.cheng \
  --emit:exe \
  --target:arm64-apple-darwin \
  --out:/tmp/app
```

## 模块布局

- 编译器内核：`src/core/lang`、`src/core/ir`、`src/core/backend`、`src/core/tooling`、`src/core/runtime`
- 标准库：`src/std`
- 网络协议：`src/libp2p`、`src/quic`、`src/http3`、`src/qpack`
- 领域模块：`src/oracle`、`src/evomap`、`src/moq`、`src/fountain`、`src/hysteria2`、`src/diloco`
- 应用入口：`src/apps`
- r2c：`src/r2c` 与 `tools/r2c/experimental`
- mobile：`src/mobile` 和宿主运行时桥 `src/runtime/mobile`

## 语言原则

Cheng 以显式所有权、可验证运行时边界、原生产物和可自举编译链为核心。未覆盖语义应尽早失败，不做静默回退；系统功能优先收成 Cheng 模块和清晰 ABI，而不是外层脚本或隐式兼容壳。
