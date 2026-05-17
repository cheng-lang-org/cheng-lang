# rust-csg findings

- Rust 源仓规模：`codex-rs` 当前约 `1738` 个 Rust 源文件、`528` 个 TypeScript 文件、`454` 个 `.snap` oracle、`360` 个 TUI frame 文本。
- Cargo workspace 当前有 `106` 个 member。全仓对位必须按 member 逐个关闭，不能只迁移 `core/cli/tui`。
- TUI 像素级在终端语义中定义为字符、ANSI 样式、布局、换行、截断、滚动位置一致；当前还没有 Cheng frame buffer，因此必须 blocked。
- `package_id = "pkg://cheng/rust-csg"` 会让模块路径包含 `cheng/rust-csg/...`。本包内源码按该包 id 导入。
- 已修 `bootstrap/cold_parser.c` 的 nested package import path：`cheng/rust-csg/core/parity` 现在映射为 `rust-csg/src/core/parity.cheng`，不再错误映射到 `src/rust-csg/core/parity.cheng`。
- `rust_csg_parity_contract_smoke` 已恢复为真实 `import cheng/rust-csg/core/parity`，正式 `artifacts/backend_driver/cheng` 已刷新并验证通过。
- `cold_nested_package_import_smoke` 覆盖了兼容路径 `cheng/codex/protocol/types` 和推荐路径 `rust-csg/protocol/types as csg_types`。
- 已修 cold import closure 的 alias 去重：不同源文件闭包内允许同名 alias 指向不同 path，去重粒度改为 `alias + path`。新增 `cold_transitive_alias_scope_smoke` 覆盖入口同时导入 `cheng/codex/protocol/types` 与 `cheng/rust-csg/core/parity` 的场景。
- `artifacts/bootstrap/cheng.stage3 build-backend-driver --out:/tmp/rust_csg_cheng_candidate` 仍因缺失已退役的 `bootstrap/cheng_seed.c` 失败；本轮改用当前 cold 编译器直接生成候选，再按候选门禁后覆盖正式 `artifacts/backend_driver/cheng`。
- `rust-csg/src/core/inventory.cheng` 的 live scan 已越过嵌套包导入和 alias 冲突；`rust_csg_inventory_smoke` 不再用常量假绿，已经调用真实 `inventory.RustCsgCollectSourceStats()`。
- 已补 `rust-csg/support/darwin_inventory_provider.c`，按 cold 实际 ABI 提供 `JoinPath/ReadFile/WalkDirRec/RustCsgContains/RustCsgEndsWith/StartsWith`：`str` 入参为 `(data, len/store_id)`，`str`/`str[]` 返回使用 `x8` 返回槽。
- 真实 inventory smoke 通过路径：`rust-csg/support/run_inventory_smoke.sh` 先 `emit:obj` 产出 primary object，再用 Darwin provider object 和 `cc -arch arm64 ... -lc` 系统链接运行，输出 `rust_csg_inventory_smoke ok`。
- 直接 `emit:exe` 下正式 driver 仍报告首个 unresolved symbol 为 `cheng_mem_release`。`provider-archive-pack + --link-object` 能解析 rust-csg provider exports，但 linkerless Mach-O provider archive 停在 provider 的 libSystem 依赖 `closedir`。不能用假 stub；需要 Darwin provider object 系统链接主线闭合。
- 源仓实时计数已用宿主命令核对：Cargo members=106、Rust files=1738、TypeScript files=528、snapshots=454、frames=360、product package.json=5。
