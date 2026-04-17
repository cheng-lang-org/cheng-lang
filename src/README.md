# Cheng 自举链路

此目录承载 Cheng 语言的编译器主源码与后端自举实现。

- `backend/`：自研后端实现与 `v3` 构建计划/代码生成实现。
- `stage1/`：前端、语义与所有权分析实现（供 backend driver 解析/语义构建使用）。
- `std/`：标准库与后端运行时源码。
- `tooling/`：canonical tooling 命令、build/verify gate 与发布闭环。

当前主口径已经收口到 `v3`：

```sh
sh v3/tooling/cheng_v3.sh bootstrap-bridge
sh v3/tooling/cheng_v3.sh build-backend-driver
artifacts/v3_backend_driver/cheng status
sh v3/tooling/cheng_v3.sh run-smokes
```

说明：
- `src/tooling/cheng_tooling.cheng` 已移除，`artifacts/tooling_cmd/cheng_tooling` 不再是现役入口。
- 旧 `artifacts/backend_driver/cheng` 已退役；当前编译入口是 `artifacts/v3_backend_driver/cheng` 与 `artifacts/v3_bootstrap/cheng.stage3`。
- 历史 `docs/cheng-dev-plan.md` 与 `tooling_exec.sh` 不再是当前仓库的主文档入口。
- `src/tooling/README.md` 现仅保留旧 backend-only 链路历史说明；当前工具链说明见 `v3/tooling/README.md`，语言与闭环口径见 `docs/cheng-formal-spec.md`。

GUI IDE 见 `../cheng-ide/README.md`（或 `$IDE_ROOT/README.md`）。
