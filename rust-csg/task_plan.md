# rust-csg task plan

目标：在 `/Users/lbcheng/cheng-lang/rust-csg` 用 Cheng 对位重写 `/Users/lbcheng/codex-lbcheng`，以 Rust/TS 源码、TUI snapshots 和 frame 文本为真值。

当前阶段只做迁移基座，不声明 Codex 已可用。

## files / action / verify / done

| files | action | verify | done |
|---|---|---|---|
| `cheng-package.toml` | 新建独立 Cheng 包 | parser 能识别 package root | 包落在仓库根目录 |
| `docs/source_inventory.md` | 固定源仓清单和计数合同 | 宿主实时计数 + `rust_csg_inventory_smoke` | 未分类文件 hard-fail |
| `docs/module_map.md` | 固定 Rust/TS 到 Cheng 的命名规则 | `rust_csg_inventory_smoke` | `-` 和 `/` 均归一到 `_` |
| `docs/parity_contract.md` | 固定未移植必须硬失败 | `rust_csg_parity_contract_smoke` | 不返回假成功 |
| `src/core/*` | 清单扫描、映射、合同核心 | 嵌套包导入链闭合后编译 smoke | 无 mock，无兜底 |
| `src/{cli,exec,tui,app_server,mcp_server}/main.cheng` | 入口落地为显式 blocked | 编译入口 | 不误报完成 |

## next

1. 先把 `protocol/config/state/rollout` 做成可 round-trip 的 Cheng 数据层。
2. 再做 `apply-patch/sandbox/shell-command/execpolicy` 真实执行层。
3. 最后做 TUI frame buffer 和 snapshot oracle。
