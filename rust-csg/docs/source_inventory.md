# source inventory

源真值：`/Users/lbcheng/codex-lbcheng`

## fixed counts

| item | expected |
|---|---:|
| Cargo workspace members | 106 |
| Rust source files in `codex-rs` | 1738 |
| TypeScript source files in `codex-rs` | 528 |
| Rust TUI `.snap` oracle files | 454 |
| TUI frame `.txt` oracle files | 360 |
| product `package.json` files excluding vendored cargo/node caches | 5 |

这些数字是迁移基座的硬合同。源仓变化时必须先更新本文件和 smoke 常量。

## Cargo workspace members

状态统一为 `explicitly_blocked`，直到对应 Cheng 模块、测试和 parity oracle 完成。

```text
aws-auth
analytics
agent-graph-store
agent-identity
backend-client
ansi-escape
async-utils
app-server
app-server-transport
app-server-client
app-server-protocol
app-server-test-client
debug-client
apply-patch
arg0
feedback
features
install-context
codex-backend-openapi-models
code-mode
cloud-requirements
cloud-tasks
cloud-tasks-client
cloud-tasks-mock-client
cli
collaboration-mode-templates
connectors
config
device-key
shell-command
shell-escalation
skills
core
core-api
core-plugins
core-skills
hooks
secrets
exec
file-system
exec-server
execpolicy
execpolicy-legacy
external-agent-migration
external-agent-sessions
keyring-store
file-search
linux-sandbox
lmstudio
login
codex-mcp
mcp-server
memories/read
memories/write
model-provider-info
models-manager
network-proxy
ollama
process-hardening
protocol
realtime-webrtc
rollout
rollout-trace
rmcp-client
responses-api-proxy
response-debug-context
sandboxing
stdio-to-uds
otel
tui
tools
v8-poc
utils/absolute-path
utils/cargo-bin
git-utils
utils/cache
utils/image
utils/json-to-toml
utils/home-dir
utils/pty
utils/readiness
utils/rustls-provider
utils/string
utils/cli
utils/elapsed
utils/sandbox-summary
utils/sleep-inhibitor
utils/approval-presets
utils/oss
utils/output-truncation
utils/path-utils
utils/plugins
utils/fuzzy-match
utils/stream-parser
utils/template
codex-client
codex-api
state
terminal-detection
test-binary-support
thread-manager-sample
thread-store
uds
codex-experimental-api-macros
plugin
model-provider
```

## TypeScript/product packages

```text
package.json
.devcontainer/codex-install/package.json
codex-cli/package.json
codex-rs/responses-api-proxy/npm/package.json
sdk/typescript/package.json
```
