# rust-csg findings

- Rust 源仓规模：`codex-rs` 当前约 `1738` 个 Rust 源文件、`528` 个 TypeScript 文件、`454` 个 `.snap` oracle、`360` 个 TUI frame 文本。
- Cargo workspace 当前有 `106` 个 member。全仓对位必须按 member 逐个关闭，不能只迁移 `core/cli/tui`。
- TUI 像素级在终端语义中定义为字符、ANSI 样式、布局、换行、截断、滚动位置一致；当前还没有 Cheng frame buffer，因此必须 blocked。
- `package_id = "pkg://cheng/rust-csg"` 会让模块路径包含 `cheng/rust-csg/...`。本包内源码按该包 id 导入。
- 当前 backend driver 的 cold 路径对嵌套包自导入仍会报 `cold import source open failed`，现有 `/Users/lbcheng/cheng-lang/codex` 也有同类失败；所以本轮产品入口和 smoke 先保持自包含可编译，`src/core/*` 作为真实实现源码保留，待嵌套包导入链闭合后接入。
- 源仓实时计数已用宿主命令核对：Cargo members=106、Rust files=1738、TypeScript files=528、snapshots=454、frames=360、product package.json=5。
