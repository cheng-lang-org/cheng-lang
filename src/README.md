# Cheng 自举链路

此目录是当前 Cheng 主包的唯一源码树。

- `core/`：编译器内核、语言前端、IR、后端、运行时 provider 与 tooling/gate。
- `std/`：标准库。
- `apps/`：应用入口与应用 GUI。
- `libp2p/`、`quic/`、`http3/`、`qpack/`：网络与协议模块。
- `oracle/`、`evomap/`、`moq/`、`hysteria2/`、`diloco/`：领域模块。
- `r2c/`、`mobile/`：r2c 与移动端 Cheng 模块。
- `runtime/mobile/`：宿主移动运行时桥接文件。
- `tests/`：Cheng smoke 与回归测试。

当前主口径已经收口到仓库根主线：

```sh
artifacts/bootstrap/cheng.stage3 bootstrap-bridge
artifacts/backend_driver/cheng build-backend-driver
artifacts/backend_driver/cheng status
artifacts/backend_driver/cheng run-smokes
```

说明：
- 旧 `cheng_tooling` 源文件已移除，`artifacts/tooling_cmd/cheng_tooling` 不再是现役入口。
- 当前编译入口是 `artifacts/backend_driver/cheng` 与 `artifacts/bootstrap/cheng.stage3`。
- 历史 `docs/cheng-dev-plan.md` 与 `tooling_exec.sh` 不再是当前仓库的主文档入口。
- 当前工具链说明见 `src/core/tooling/README.md`，语言与闭环口径见 `docs/cheng-formal-spec.md`。

GUI IDE 见 `../cheng-ide/README.md`（或 `$IDE_ROOT/README.md`）。
