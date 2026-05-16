# module map

## naming

- Rust crate / TS package path segment `-` maps to Cheng `_`.
- Rust workspace path `/` maps to Cheng `_` for leaf module bucket names.
- Source modules live under `src/crates/<normalized_name>/...`.
- Product entries live under `src/cli`, `src/exec`, `src/tui`, `src/app_server`, `src/mcp_server`.

## examples

| source | Cheng bucket |
|---|---|
| `app-server-protocol` | `src/crates/app_server_protocol` |
| `codex-mcp` | `src/crates/codex_mcp` |
| `utils/path-utils` | `src/crates/utils_path_utils` |
| `memories/read` | `src/crates/memories_read` |

## rule

No implicit fallback. If a source member has no mapped Cheng module, inventory smoke must report it as `explicitly_blocked`.
