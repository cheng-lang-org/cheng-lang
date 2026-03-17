# Cheng 自举链路

此目录承载 Cheng 语言的编译器主源码与后端自举实现。

- `backend/`：自研后端实现与驱动（`src/backend/tooling/backend_driver.cheng`）。
- `stage1/`：前端、语义与所有权分析实现（供 backend driver 解析/语义构建使用）。
- `std/`：标准库与后端运行时源码。
- `tooling/`：canonical tooling 命令、build/verify gate 与发布闭环。

当前主口径：

```sh
TOOLING=artifacts/tooling_cmd/cheng_tooling

$TOOLING build-backend-driver
$TOOLING driver-path --path-only
$TOOLING verify_backend_closedloop
$TOOLING backend_prod_closure
```

说明：
- 当前默认入口是 `cheng_tooling` 与 canonical backend driver。
- 历史 `docs/cheng-dev-plan.md` 与 `tooling_exec.sh` 不再是当前仓库的主文档入口。
- 更完整的工具链说明见 `src/tooling/README.md`；语言与闭环口径见 `docs/cheng-formal-spec.md`。

GUI IDE 见 `../cheng-ide/README.md`（或 `$IDE_ROOT/README.md`）。
