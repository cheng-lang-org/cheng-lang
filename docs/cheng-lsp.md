# Cheng LSP 设计与实现计划

> 2026-05-17 · 设计文档（非规范）
> 规范以 `docs/cheng-formal-spec.md` 为准；实现状态以 `docs/roadmap.md`、当前源码和当前可执行产物为准。

## 0. 评估结论

原文方向基本对：先做独立 `cheng-lsp`，不要把批处理编译器直接改成常驻 IDE 服务。

需要修正三点：

- 不能假设已有 `cold_parse_program()`、`AstNode`、`cold_parser_tokenize()`、`cold_parse_report_error()` 这类稳定 API。当前 `bootstrap/cold_parser.h` 暴露的是 `parser_token`、`cold_parse_import_line`、`cold_collect_*`、`parse_fn` 等低层入口。
- LSP 必须新增明确的 analysis API，不能靠扫描 stderr 或猜内部结构。
- 模块解析必须遵守 `cheng-package.toml`、`cheng.lock.toml` 和归一化 `import <pkg>/<path>` 规则；不支持字符串、相对、绝对导入。
- LSP 只读本地 lock 和本地 CID 缓存。补全、跳转、hover 不触发 libp2p resolve/fetch。

## 1. 架构

推荐做一个独立二进制：

```
cheng-lsp
├── JSON-RPC / LSP stdio 协议层
├── 文档表：打开文件、版本、源码文本
├── analysis API：词法、语法、导入、符号、引用
└── cold_parser.c / parser.cheng 共享能力
```

`cheng-lsp` 只走 stdio，不开 HTTP/TCP 服务。

## 2. 当前可复用事实

当前仓库已有：

- `bootstrap/cold_parser.h`
  - `parser_token` / `parser_peek` / `parser_take`
  - `cold_parse_import_line`
  - `cold_import_source_path`
  - `cold_collect_function_signatures`
  - `cold_collect_import_module_*`
  - `parse_fn`
  - `cold_collect_all_transitive_imports`
- `bootstrap/cold_types.h`
  - `Parser`
  - `Symbols`
  - `FnDef`
  - `ColdImportSource`
- `src/core/lang/parser.cheng`
  - 当前 Cheng 主线 parser 逻辑与规范更接近。
- `artifacts/backend_driver/cheng`
  - `print-build-plan`
  - `print-line-map`
  - `debug-report`
  - `system-link-exec`

当前缺口：

- 没有稳定 token stream API。
- 没有 LSP 诊断收集器。
- 没有 AST 节点稳定模型。
- `FnDef` 不携带稳定定义 span、引用列表、局部作用域链。
- cold parser 对完整 Cheng 语义仍是子集，不可把它说成全量类型系统。

## 3. 新增 analysis API

先加一个窄接口，不暴露 parser 内部结构。

建议新增：

| 文件 | 作用 |
|---|---|
| `bootstrap/cold_lsp_analysis.h` | 面向 LSP 的稳定 C API |
| `bootstrap/cold_lsp_analysis.c` | token、diagnostic、symbol、xref 适配层 |
| `src/tooling/cheng_lsp_main.c` | LSP 主循环 |
| `src/tooling/lsp_protocol.h` | JSON-RPC 与 LSP 类型序列化 |

核心类型：

```c
typedef struct {
    int32_t line;
    int32_t column;
    int32_t length;
    int32_t kind;
} ChengLspToken;

typedef struct {
    int32_t line;
    int32_t column;
    int32_t length;
    int32_t severity;
    const char *message;
} ChengLspDiagnostic;

typedef struct {
    const char *name;
    int32_t kind;
    int32_t def_line;
    int32_t def_column;
    int32_t def_length;
} ChengLspSymbol;
```

约束：

- API 必须返回结构化错误，不写 stderr 当协议数据。
- 所有位置统一 LSP 的 0-based line/character。
- 不支持的语义查询返回空结果或明确诊断，不生成猜测答案。
- 解析失败也要保留已完成 token 和顶层符号，便于编辑器继续工作。

## 4. Phase 1：诊断与语义高亮

目标：

- `initialize`
- `textDocument/didOpen`
- `textDocument/didChange`
- `textDocument/publishDiagnostics`
- `textDocument/semanticTokens/full`

实现：

1. 文档表保存 URI、路径、版本、全文。
2. 每次打开或变更后重新分析全文。
3. analysis API 输出 token 与诊断数组。
4. LSP 层只做 JSON 编解码和位置转换。

Token 分类：

| Cheng token | LSP token |
|---|---|
| `fn`、`let`、`var`、`type`、`import`、`if`、`elif`、`match` | `keyword` |
| 函数声明名、调用名 | `function` |
| 首字母大写类型名 | `type` |
| 局部绑定、参数 | `variable` |
| 字符串、`Fmt"..."` | `string` |
| 数字 | `number` |
| `//`、`#`、`/* */` | `comment` |
| `+ - * / % && || ! => ?` | `operator` |

验收：

- 语法错误显示在准确行列。
- 未闭合字符串、非法 import、`else if`、字符串路径 import 都有诊断。
- 保存无错误文件时诊断清空。

## 5. Phase 2：定义、引用、Hover、Outline

目标：

- `textDocument/definition`
- `textDocument/references`
- `textDocument/hover`
- `textDocument/documentSymbol`
- `workspace/symbol`

实现：

1. 顶层扫描收集 `fn/type/const/let/var/import` 定义。
2. 函数体扫描建立引用表。
3. 导入图按 `cheng-package.toml`、`cheng.lock.toml` 和 `PKG_ROOTS` 解析。
4. 只对已解析符号返回 definition/references。
5. Hover 只展示已知签名、类型标注和导出状态。

导入图解析只读本地状态：

- lock 缺失时只分析当前包和标准库，并给出依赖未锁定诊断。
- 本地 CID 缓存缺失时给出缺包诊断。
- 不在 LSP 请求处理中访问 libp2p 网络。

导出规则：

- 首字母 ASCII 大写才对外导出。
- 小写和 `_` 开头符号只在模块内可见。
- import 只导入对外导出符号。

## 6. Phase 3：补全、Rename、Code Action

目标：

- `textDocument/completion`
- `textDocument/rename`
- `textDocument/codeAction`

补全来源：

- 当前作用域局部绑定。
- 函数参数。
- 当前模块顶层导出符号。
- 已导入模块的导出符号。
- 关键字与标准库模块路径。

Rename 约束：

- 只对完全解析到同一符号的引用生成 edit。
- 新名字必须符合 Cheng 标识符规则。
- 跨模块 rename 必须尊重导出规则。
- 无法证明同一符号时直接拒绝。

Code action 只做确定性修复：

- `else if` -> `elif`
- 字符串 import -> 归一化 import，当且仅当能唯一映射。
- `import A, B` -> 多行 import。
- `seq[T]` / `array[T,N]` -> 当前容器语法，当且仅当能机械确定。

## 7. 深度编译器查询

完整类型推导、泛型实例化、ownership、`Send/Sync` 这类信息不放进 Phase 1/2。

需要时新增专门命令，例如：

```bash
artifacts/backend_driver/cheng analysis-query \
  --root:/abs/root \
  --in:/abs/root/src/main.cheng \
  --position:12:8 \
  --kind:hover-type \
  --json
```

在这个命令存在并通过门禁前，LSP 不声明完整类型 hover。

## 8. 客户端配置

VS Code：

- `vscode-cheng/package.json`
- 启动 `cheng-lsp`
- 语言 ID：`cheng`
- 文件后缀：`.cheng`

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

Helix：

```toml
[[language]]
name = "cheng"
scope = "source.cheng"
file-types = ["cheng"]
language-servers = ["cheng-lsp"]
```

## 9. 验收条件

Phase 1：

```bash
cheng-lsp --stdio
```

- `initialize` 返回 capabilities。
- `didOpen` 后收到 diagnostics。
- `semanticTokens/full` 返回稳定 token 序列。
- 非法 import、`else if`、未闭合字符串都有准确诊断。

Phase 2：

- `definition` 能跳到同文件函数、跨模块导出函数、类型定义。
- `references` 不跨符号混淆。
- `documentSymbol` 展示顶层 outline。
- import cycle 诊断必须输出链路。

Phase 3：

- rename 前后重新分析通过。
- completion 不展示不可见私有符号。
- code action 只输出可证明的机械改写。
