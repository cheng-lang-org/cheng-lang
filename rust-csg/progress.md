# rust-csg progress

- 新包落点固定：`/Users/lbcheng/cheng-lang/rust-csg`。
- 源真值固定：`/Users/lbcheng/codex-lbcheng`。
- 当前实现状态：迁移基座已开始；产品入口必须显式失败，直到对应层真实移植完成。
- 当前不可宣称完成：CLI、exec、TUI、app-server、MCP、模型调用、插件市场、远程云端、WebRTC。
- 已编译通过：`rust_csg_inventory_smoke`、`rust_csg_parity_contract_smoke`、`cli/exec/tui/app_server/mcp_server` 五个 blocked 入口。
- 暂未接入：`src/core/inventory.cheng` 的 live scan 模块。原因是当前 cold backend 对嵌套包自导入未闭合，不能用绕路把失败藏掉。
