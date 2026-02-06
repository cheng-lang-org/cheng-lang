# Cheng 自举链路

此目录承载 Cheng 语言的自举实现。

- `stage0/`：宿主层引导前端（词法/语法/诊断），用于生成自托管前端 runner 的 C 代码。
- `stage1_runner`：Cheng 自举产物/主编译器；源码位于 `stage1/`（自托管前端实现），用于自举产出自身。
- `stdlib/bootstrap/`：最小标准库（bootstrap），逐步以 Cheng 实现为准。

构建与运行示例见 `doc/cheng-dev-plan.md`：
- 含 Stage0 的一键完全自举（Stage0→Stage1）：`src/tooling/bootstrap.sh --fullspec`
- 纯 Cheng 自举闭环（不调用 Stage0；可从 seed 启动）：`src/tooling/bootstrap_pure.sh --fullspec`

GUI IDE 见 `../cheng-ide/README.md`（或 `$CHENG_IDE_ROOT/README.md`）。
