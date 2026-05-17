# rust-csg progress

- 新包落点固定：`/Users/lbcheng/cheng-lang/rust-csg`。
- 源真值固定：`/Users/lbcheng/codex-lbcheng`。
- 当前实现状态：迁移基座已开始；产品入口必须显式失败，直到对应层真实移植完成。
- 当前不可宣称完成：CLI、exec、TUI、app-server、MCP、模型调用、插件市场、远程云端、WebRTC。
- 已编译通过：`rust_csg_parity_contract_smoke`、`cli/exec/tui/app_server/mcp_server` 五个 blocked 入口。
- 已用临时重编 cold 编译器验证：嵌套包自导入修复后，`rust_csg_parity_contract_smoke` 可真实导入 `cheng/rust-csg/core/parity`。
- 已补 `cold_nested_package_import_smoke`：同测 `cheng/codex/...` 兼容路径和 `rust-csg/...` 推荐路径。
- 已补 `cold_transitive_alias_scope_smoke`：入口同时导入 `cheng/codex/protocol/types` 和 `cheng/rust-csg/core/parity`，覆盖跨源同名 `types` alias。
- 已刷新正式 `artifacts/backend_driver/cheng`：候选先通过 nested import、transitive alias、rust-csg parity、rust-csg inventory static 四个 smoke，覆盖正式入口后同一组 smoke 再次通过。
- `artifacts/bootstrap/cheng.stage3 build-backend-driver --out:/tmp/rust_csg_cheng_candidate` 仍因缺失已退役的 `bootstrap/cheng_seed.c` 失败；当前可用刷新路径是 cold 编译器直接 `build-backend-driver`。
- 已接入真实 live scan：`rust_csg_inventory_smoke` 现在调用 `inventory.RustCsgCollectSourceStats()`，通过显式 Darwin provider object + 系统链接跑完，输出 `rust_csg_inventory_smoke ok`。
- 已补复现脚本：`rust-csg/support/run_inventory_smoke.sh` 固定执行 provider 编译、Cheng object 编译、系统链接和 smoke 运行。
- 暂未闭合：正式 `system-link-exec --emit:exe` 直接编译 live inventory 仍卡在 `first_unresolved_symbol=cheng_mem_release`；`--link-object + provider-archive` 可解析 rust-csg provider exports，但停在 provider 的 libSystem 依赖 `closedir`。下一步必须把 Darwin provider object 纳入真实系统链接主线，不能用假 stub。
