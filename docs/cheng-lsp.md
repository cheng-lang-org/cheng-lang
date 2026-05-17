# Cheng LSP 最佳方案

> 2026-05-17 · 设计文档（非规范）
> 规范以 `docs/cheng-formal-spec.md` 为准；实现状态以 `docs/roadmap.md`、当前源码和当前可执行产物为准。

## 0. 结论

最佳方案不是让 LSP 直接绑定 `cold_parser.c`，而是让 LSP 消费 **compiler semantic facts**。

`cold_parser.c` 可以作为 MVP 的词法/浅语法入口，但不能成为长期权威。长期权威必须来自同一套编译器前端 facts，否则 LSP、编译器、Debugger 会对同一源码给出不同答案。

## 1. 总体架构

```
cheng.lock.toml / world receipt
  -> compiler frontend
  -> canonical semantic facts
      -> LSP server
      -> DebugFacts
      -> build/lowering/codegen
```

LSP 不自行解析世界，不联网更新依赖，不猜类型。它只做：

- JSON-RPC / LSP 协议。
- 管理打开文件 overlay。
- 请求 compiler facts。
- 把 facts 转成 LSP 响应。

## 2. Facts 分层

| Facts | LSP 用途 |
|---|---|
| `TokenFacts` | semantic tokens |
| `ParseFacts` | syntax diagnostics、outline |
| `ImportGraphFacts` | 跨模块跳转、导入错误、循环链 |
| `SymbolFacts` | definition、references、workspace symbol |
| `TypeFacts` | hover、completion detail |
| `ExportSurfaceFacts` | 只展示可见符号 |
| `OwnershipFacts` | 借用/逃逸诊断 |
| `ConcurrencyFacts` | `Send/Sync`、线程边界诊断 |
| `WorldFacts` | package/std/compiler/runtime CID |

Facts 必须满足：

- 结构化。
- 稳定版本号。
- 0-based line/column 位置。
- 明确 world snapshot / source version。
- 不通过 stderr 传协议数据。

## 3. LSP Server

`cheng-lsp` 是常驻进程：

```
cheng-lsp
├── LSP stdio 协议层
├── 文档 overlay
├── facts cache
├── world snapshot reader
└── compiler facts query client
```

约束：

- `initialize` 时读取 `cheng-package.toml` 和 `cheng.lock.toml`。
- lock 缺失时只分析当前包和标准库，并给依赖未锁定诊断。
- 本地 CID cache 缺失时给缺包诊断。
- 补全、跳转、hover 不触发 libp2p resolve/fetch。
- 打开文件 overlay 不写入 world cache，除非用户保存。

## 4. Compiler Facts API

新增稳定命令：

```bash
artifacts/backend_driver/cheng analysis-facts \
  --root:/abs/root \
  --in:/abs/root/src/main.cheng \
  --lock:/abs/root/cheng.lock.toml \
  --overlay:/tmp/cheng-lsp-overlay.json \
  --out:/tmp/cheng.facts.json
```

最小输出：

```json
{
  "format": "CHENG_ANALYSIS_FACTS_V1",
  "world_head_cid": "bafy...",
  "source_version": 12,
  "tokens": [],
  "diagnostics": [],
  "symbols": [],
  "references": [],
  "imports": []
}
```

规则：

- facts 生成失败必须返回结构化错误。
- 不支持的查询返回明确 unsupported diagnostic。
- 未解析调用、导入缺失、循环导入必须 hard-fail 或输出错误 facts，不能静默跳过。

## 5. MVP 路径

MVP 可以分两步，避免等完整 facts 一次完成。

### Phase 1：LSP 协议 + 浅 facts

先用 `bootstrap/cold_parser.h` 暴露一个窄 API：

- token scan
- import line parse
- 顶层 `fn/type/const` scan
- 确定性语法诊断

仅支持：

- `initialize`
- `didOpen`
- `didChange`
- `publishDiagnostics`
- `semanticTokens/full`
- `documentSymbol`

这阶段必须明确标注为 shallow facts，不声明完整类型和完整语义。

### Phase 2：Compiler Facts

接入 `analysis-facts`：

- `definition`
- `references`
- `hover`
- `workspace/symbol`
- 跨模块导入图

### Phase 3：完整 IDE 功能

依赖完整 semantic facts：

- `completion`
- `rename`
- `codeAction`
- ownership diagnostics
- `Send/Sync` diagnostics

## 6. Code Action 边界

只做可证明机械改写：

- `else if` -> `elif`
- 字符串 import -> 归一化 import，当且仅当能唯一映射。
- `import A, B` -> 多行 import。
- `seq[T]` / `array[T,N]` -> 当前容器语法，当且仅当能机械确定。

Rename 必须只改同一 symbol id 的引用。无法证明同一符号时拒绝。

## 7. 客户端配置

VS Code：

- `vscode-cheng/package.json`
- 启动 `cheng-lsp`
- 文件后缀 `.cheng`

Neovim：

```lua
vim.api.nvim_create_autocmd('FileType', {
  pattern = 'cheng',
  callback = function()
    vim.lsp.start({
      name = 'cheng-lsp',
      cmd = { 'cheng-lsp' },
      root_dir = vim.fs.dirname(vim.fs.find('cheng-package.toml', { upward = true })[1]),
    })
  end,
})
```

## 8. 验收

Phase 1：

- 非法 import、`else if`、未闭合字符串都有准确诊断。
- semantic tokens 稳定。
- outline 不依赖 stderr。

Phase 2：

- definition/references 使用同一 symbol id。
- 跨模块跳转只看 lock 中的 CID 快照。
- import cycle 输出完整链路。

Phase 3：

- completion 不展示不可见私有符号。
- rename 后重新生成 facts 通过。
- hover 类型来自 `TypeFacts`，不是 LSP 自己猜。
