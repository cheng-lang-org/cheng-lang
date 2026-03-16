# Cheng 自举链路

此目录承载 Cheng 语言的后端自举实现。

- `backend/`：自研后端实现与驱动（`src/backend/tooling/backend_driver.cheng`）。
- `stage1/`：前端库实现（供 backend driver 解析/语义构建使用）。
- `std/`：标准库与后端运行时源码。

构建与运行示例见 `docs/cheng-dev-plan.md`：
- 纯 Cheng 一键自举（obj/exe + self-link，seed 启动）：`sh src/tooling/tooling_exec.sh bootstrap_pure --fullspec`
- 兼容入口（转发到 pure 自举）：`sh src/tooling/tooling_exec.sh bootstrap --fullspec`

GUI IDE 见 `../cheng-ide/README.md`（或 `$IDE_ROOT/README.md`）。
