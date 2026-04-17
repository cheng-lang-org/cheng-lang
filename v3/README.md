# v3

`v3` 不是给 `v2` 补壳，它是从外根自举到 ordinary compiler 都尽量收成 Cheng 自身的一条新主线。

## 当前入口

`v3` 现在不再依赖 `v3/tooling/*.sh`。只认下面三个二进制：

- `artifacts/v3_bootstrap/cheng.stage0`
  用途：唯一外根，只负责 `bootstrap-bridge`。
- `artifacts/v3_bootstrap/cheng.stage3`
  用途：tooling、gate、debug、build、smoke 主入口。
- `artifacts/v3_backend_driver/cheng`
  用途：ordinary compile、world/CSG、r2c focused smoke、自托管 compiler control-plane 主入口。

## 冷启动

```sh
mkdir -p artifacts/v3_bootstrap
cc -std=c11 -O2 -Wall -Wextra -pedantic \
  v3/bootstrap/cheng_v3_seed.c \
  -o artifacts/v3_bootstrap/cheng.stage0

artifacts/v3_bootstrap/cheng.stage0 bootstrap-bridge
artifacts/v3_bootstrap/cheng.stage3 build-backend-driver
```

完成后，日常开发默认直接跑 `cheng.stage3` 或 `artifacts/v3_backend_driver/cheng`。

## 常用命令

```sh
artifacts/v3_bootstrap/cheng.stage3 scan-hotpath
artifacts/v3_bootstrap/cheng.stage3 print-bootstrap
artifacts/v3_bootstrap/cheng.stage3 verify-debug-tools
artifacts/v3_bootstrap/cheng.stage3 verify-debug-runtime
artifacts/v3_bootstrap/cheng.stage3 verify-debug-profile
artifacts/v3_bootstrap/cheng.stage3 verify-orphan-guard
artifacts/v3_bootstrap/cheng.stage3 slice-gate
artifacts/v3_bootstrap/cheng.stage3 run-host-smokes
artifacts/v3_bootstrap/cheng.stage3 run-stage23-libp2p-smokes
artifacts/v3_bootstrap/cheng.stage3 build-bft-state-machine
artifacts/v3_bootstrap/cheng.stage3 build-rwad-bft-state-machine
artifacts/v3_bootstrap/cheng.stage3 build-browser-host-wasm
artifacts/v3_bootstrap/cheng.stage3 build-chain-node
artifacts/v3_bootstrap/cheng.stage3 build-chain-node-linux
artifacts/v3_bootstrap/cheng.stage3 build-rwad-bft-linux
artifacts/v3_bootstrap/cheng.stage3 build-linux-nolibc-exe
artifacts/v3_bootstrap/cheng.stage3 verify-windows-builtin
artifacts/v3_bootstrap/cheng.stage3 verify-riscv64-builtin
artifacts/v3_bootstrap/cheng.stage3 run-cross-target-smokes

artifacts/v3_backend_driver/cheng status
artifacts/v3_backend_driver/cheng print-build-plan --target:arm64-apple-darwin
artifacts/v3_backend_driver/cheng system-link-exec --root:/abs/root/v3 --in:/abs/path/file.cheng --emit:exe --target:arm64-apple-darwin --out:/tmp/app
artifacts/v3_backend_driver/cheng emit-csg --root:/abs/root/v3 --in:/abs/path/file.cheng --out:/tmp/csg
artifacts/v3_backend_driver/cheng migrate-csg --root:/abs/root/v3 --in:/abs/path/file.cheng --legacy-in:/abs/path/legacy.cheng --out:/tmp/migrate
artifacts/v3_backend_driver/cheng verify-world --root:/abs/root/v3 --lock:/abs/path/cheng.lock.toml
artifacts/v3_backend_driver/cheng world-sync --root:/abs/root/v3 --world-head:<cid> --out:/tmp/world
artifacts/v3_backend_driver/cheng prove-equivalence --root:/abs/root/v3 --in:/abs/path/file.cheng --baseline-surface:/abs/path/surface.txt --out:/tmp/equiv
artifacts/v3_backend_driver/cheng prove-migration --root:/abs/root/v3 --in:/abs/path/file.cheng --legacy-in:/abs/path/legacy.cheng --out:/tmp/proof
artifacts/v3_backend_driver/cheng publish-world --root:/abs/root/v3 --in:/abs/path/file.cheng --target:arm64-apple-darwin --channel:stable --out:/tmp/publish
```

调试闭环也已经直接进 `cheng.stage3`：

```sh
artifacts/v3_bootstrap/cheng.stage3 debug-report --in:/abs/path/file.cheng --target:arm64-apple-darwin --emit:obj
artifacts/v3_bootstrap/cheng.stage3 print-symbols --in:/abs/path/file.cheng --target:arm64-apple-darwin --emit:obj
artifacts/v3_bootstrap/cheng.stage3 print-line-map --in:/abs/path/file.cheng --target:arm64-apple-darwin --emit:obj
artifacts/v3_bootstrap/cheng.stage3 print-object --object:/abs/path/file.o
artifacts/v3_bootstrap/cheng.stage3 print-asm --in:/abs/path/file.cheng --target:arm64-apple-darwin --emit:obj
artifacts/v3_bootstrap/cheng.stage3 profile-run --in:/abs/path/file.cheng --target:arm64-apple-darwin --out:/tmp/app
artifacts/v3_bootstrap/cheng.stage3 profile-report --in:/tmp/app.v3.profile.raw.txt --out:/tmp/app.profile.txt
artifacts/v3_bootstrap/cheng.stage3 crash-report --in:/tmp/app.crash.raw.txt --out:/tmp/app.crash.txt
```

## 已落地的主线

- `v3/src/std/bytes_layout.cheng`：固定布局 `ByteSpan / ByteBuf / FixedBytes[N]`
- `v3/src/lang/intern.cheng`：冷路径字符串 interner
- `v3/src/ir/core_types.cheng`：`HIR/MIR/LIR` 最小真数据面
- `v3/src/chain/*`：`lsmr / anti_entropy / csg / pubsub / location_proof / consensus` 最小固定布局语义核
- `v3/src/tooling/*`：`bootstrap contract / build plan / hotpath scan / gate / compiler control-plane`
- `v3/src/backend/system_link_exec.cheng`：ordinary compile 主入口
- `v3/src/tests/*`：固定布局、链路、debug、ordinary smoke
- `v3/src/project/chain_node_main.cheng`：`chain_node` 真 artifact 入口
- `v3/bench/c_ref/*`：同机 C 基线

## 当前规则

- `v3/tooling` 顶层已经禁掉所有 `.sh`。`verify-orphan-guard` 会直接失败，不允许 wrapper 回流。
- `bootstrap-bridge`、`build-backend-driver`、`slice-gate`、`verify-debug-*`、`run-host-smokes`、`run-stage23-libp2p-smokes` 都已经是 Cheng 本体命令。
- `debug-report / print-symbols / print-line-map / print-object / print-asm / profile-run / profile-report / crash-report` 都已经内建，不再依赖 `lldb / nm / otool / sample`。
- Linux `aarch64` 已经有真实 `ELF relocatable object` 和 `nolibc exe` 主线。
- Windows `COFF/PE` 和 `riscv64 ELF` 通过 `verify-windows-builtin / verify-riscv64-builtin / run-cross-target-smokes` 统一验收。
- 当前 ordinary 真阻塞仍是函数体语义子集，不是入口壳、对象格式、链接器或调试闭环。
