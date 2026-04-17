# codex (cheng v3)

`v3/codex` 是用 Cheng v3 重写 `codex-rs` 的第一条可日常使用主线。

现在这套实现只保留一份源码真值：

- `v3/codex` 自己的业务代码
- 仓库共享的 `src/std`
- 仓库共享的 `v3/src`

这里不再复制第二份 `codex/std/v3` 源码，也不再靠 symlink 假共享。

## 当前已落地

- 独立 `cheng-package.toml`
- `core + exec + cli + tui` 共用同一套核心语义
- 会话持久化
- shell 执行与 `--auto-approve` 硬门槛
- `exec` 支持 `--prompt` 和 `stdin` 合并输入
- `resume --list`、`resume --last`、`resume <session_id>`
- `login`、`login status`、`logout`、`status`
- `plugin list/get/add/remove`
- `mcp list/get/add/remove`
- `mcp add <name> -- <command>...` 的 `stdio` 形态
- `mcp add <name> --url:<url>` 的 `streamable_http` 形态
- `apply --patch:<file>`
- `sandbox info/run`
- Darwin 上真实调用 `/usr/bin/sandbox-exec`

## 当前不做

- 真模型接入
- MCP 完整 client/server 协议运行时
- 远程云端与插件市场
- Rust 版历史实验命令全量兼容

## 入口

- `src/cli/main.cheng`
- `src/exec/main.cheng`
- `src/tui/main.cheng`

## 存储真值

- `auth.env`
- `last_session`
- `sessions.index`
- `sessions/<session_id>.session`
- `plugins/plugins.index`
- `mcp_servers.env`

## 本地编译

```sh
artifacts/v3_backend_driver/cheng system-link-exec \
  --root:/Users/lbcheng/cheng-lang \
  --in:/Users/lbcheng/cheng-lang/v3/codex/src/cli/main.cheng \
  --emit:exe \
  --target:arm64-apple-darwin \
  --out:/tmp/codex_v3_cli
```

`exec` 和 `tui` 入口同理把 `--in:` 换成：

- `v3/codex/src/exec/main.cheng`
- `v3/codex/src/tui/main.cheng`

## 命令面

```text
codex exec --command:<shell> [--prompt:<text>] [stdin] [--cwd:<path>] [--title:<text>] [--session:<id>] --auto-approve
codex resume [--last] [--list] [session_id]
codex status
codex login --api-key:<text>
codex login status
codex logout
codex plugin list
codex plugin get <name>
codex plugin add <name> [--path:<dir>]
codex plugin remove <name> | --name:<name>
codex mcp list
codex mcp get <name>
codex mcp add <name> --url:<url>
codex mcp add <name> -- <command>...
codex mcp remove <name> | --name:<name>
codex apply --patch:<file> [--cwd:<path>]
codex sandbox info
codex sandbox run --command:<shell> [--cwd:<path>] [--write-root:<path>]
codex sandbox run -- [command args...]
```

TUI 内置命令：

- `/shell <cmd>`
- `/sessions`
- `/resume <id>`
- `/title <text>`
- `/quit`

## 已真实验收

这轮已经前台真跑通过：

- `printf 'stdin prompt line\n' | /tmp/codex_exec_main_probe4 exec --home:/tmp/codex-final-home.VRSenz --command:'printf exec-ok' --auto-approve`
  输出 `exec-ok` 和 `command ok rc=0`
- `/tmp/codex_cli_main_probe4 status --home:/tmp/codex-final-home.VRSenz`
  输出 `session_count=1`，`last_title=stdin prompt line`
- `/tmp/codex_cli_main_probe4 login --api-key:test-key --home:/tmp/codex-final-home.VRSenz`
  与 `login status/logout`
- `plugin add/list/get/remove`
- `mcp add/list/get/remove`
  两种 transport 都已真跑
- `printf '/sessions\n/quit\n' | /tmp/codex_cli_main_probe4 resume --last --home:/tmp/codex-final-home.VRSenz`
- `printf '/title tui-smoke\n/quit\n' | /tmp/codex_tui_main_probe4 --home:/tmp/codex-final-home.VRSenz`
- `/tmp/codex_cli_main_probe4 sandbox info`
  输出 `executor=/usr/bin/sandbox-exec`
- `/tmp/codex_cli_main_probe4 sandbox run --home:/tmp/codex-final-home.VRSenz --cwd:/tmp/codex-final-sandbox.KhFg7k --command:'echo sandbox-ok > sandbox.txt && cat sandbox.txt'`
  输出 `sandbox-ok`
- `/tmp/codex_cli_main_probe4 sandbox run --home:/tmp/codex-final-home.VRSenz --cwd:/tmp/codex-final-sandbox.KhFg7k --command:'curl -I --max-time 3 https://example.com'`
  在 sandbox 内失败并返回 `command failed rc=6`
- `/tmp/codex_exec_main_probe4 apply --cwd:/tmp/codex-final-repo.bbIspn --patch:/tmp/codex-final-repo.bbIspn/change.patch`
  成功把 `note.txt` 从 `hello` 应用成 `hello\nworld`

三份入口报告也都已重新做到：

- `/tmp/codex_cli_main_probe4.report.txt`
- `/tmp/codex_exec_main_probe4.report.txt`
- `/tmp/codex_tui_main_probe4.report.txt`

共同结果都是 `primary_object_unsupported_function_count=0`。
