# Cheng World Resolver 与包管理设计

> 2026-05-17 · 设计文档（非规范）
> 规范以 `docs/cheng-formal-spec.md` 为准；实现状态以 `docs/roadmap.md`、当前源码和当前可执行产物为准。

## 0. 结论

最佳方案不是传统包管理器，而是 **World Resolver**。

Cheng 代码自托管在 Cheng libp2p 网络中，全球统一版本必须由 `world head -> CID 快照 -> 本地 lock/receipt -> 离线编译` 闭合。包管理器只是 World Resolver 的一部分，职责是把“可变通道”固化成“不可变世界快照”。

必须坚持：

- 包身份用 `package_id = "pkg://cheng/<name>"`。
- libp2p CID 快照是主来源；Git/file 只作为迁移期镜像或本地开发源。
- `cheng.lock.toml` 承载 world snapshot，不只是依赖列表。
- `compile/lowering/codegen/link` 不联网，只消费已锁定本地快照。
- LSP、Debugger、编译器都读同一份 world snapshot 和 compiler canonical facts。

## 1. 总体数据流

```
libp2p world head
  -> universe manifest
  -> package snapshots
  -> local CID cache
  -> cheng.lock.toml / world receipt
  -> compiler canonical facts
      -> build/codegen/link
      -> LSP facts
      -> DebugFacts
```

核心区别：

- `channel head` 是可变入口。
- `CID` 是不可变内容。
- `cheng.lock.toml` 是本次构建的世界快照。
- `compile receipt` 是“这次编译用了什么世界、源码、compiler、stdlib、runtime、目标参数”的可验证回执。

## 2. 包根与 Manifest

包根仍是 `cheng-package.toml` + `src/`。

```
<project>/
├── cheng-package.toml
├── cheng.lock.toml
├── src/
│   ├── main.cheng
│   └── internal/
└── tests/
```

`cheng-package.toml` 描述意图，不描述最终世界：

```toml
package_id = "pkg://cheng/fs"
module_prefix = "fs"
edition = "2026"
type = "lib"

[[dependencies]]
package_id = "pkg://cheng/core"
channel = "stable"
version = "^0.2"

[[dependencies]]
package_id = "pkg://cheng/net"
channel = "edge"
```

必填字段：

| 字段 | 说明 |
|---|---|
| `package_id` | 全局包身份 |
| `module_prefix` | 本包导入前缀 |
| `[[dependencies]].package_id` | 依赖包身份 |
| `[[dependencies]].channel` | 解析通道 |

可选字段：

| 字段 | 说明 |
|---|---|
| `version` | SemVer 约束 |
| `checksum` | 迁移源或本地源校验 |
| `source` | `libp2p`、`file:`、迁移期 Git 镜像源 |

## 3. World Lock

`cheng.lock.toml` 是 world snapshot。

```toml
format_version = 1
world_head_cid = "bafy-world..."
channel = "stable"
epoch = 42
author_id = "did:key:z..."
signature = "ed25519:..."

compiler_package_id = "pkg://cheng/compiler"
compiler_cid = "bafy-compiler..."
stdlib_package_id = "pkg://cheng/std"
stdlib_cid = "bafy-stdlib..."
runtime_package_id = "pkg://cheng/runtime"
runtime_cid = "bafy-runtime..."

[[packages]]
package_id = "pkg://cheng/core"
module_prefix = "core"
channel = "stable"
version = "0.2.4"
cid = "bafy-core..."
syntax_surface_cid = "bafy-syntax..."
export_surface_cid = "bafy-export..."
source = "libp2p:cheng"
checksum = "sha256:..."
format = "source"
```

硬约束：

- 同一 `package_id` 只能解析到一个 CID。
- manifest 与 lock 不一致必须失败。
- lock 签名、CID、checksum 不合法必须失败。
- 本地 CID cache 缺失必须失败。
- 编译阶段不能修改 lock。

## 4. Build 模式

| 模式 | 是否联网 | 行为 |
|---|---|---|
| `cheng deps resolve/fetch` | 是 | 解析 world head，拉取 CID 快照，验签，写候选 lock |
| `cheng build --online` | 可联网 | 先 resolve/fetch，再以 `--locked` 同一路径编译 |
| `cheng build --locked` | 否 | 只读 lock 和本地 CID cache |
| release | 否 | 等同 `--locked`，额外要求签名和 receipt 完整 |

`cheng build --online` 必须是组合命令：

1. 解析 world head。
2. 生成候选 `cheng.lock.toml`。
3. 校验候选 lock 与本地 CID cache。
4. 原子替换 lock。
5. 进入 `--locked` 离线编译路径。

## 5. 导入映射

用户源码只写归一化导入：

```cheng
import fs/path
import core/result as result
import libp2p/[crypto,transport,swarm]
import std/os
```

映射：

| 导入 | 本地解析 |
|---|---|
| `import <pkg>/<path>` | lock 中 `<pkg>` 的 CID cache：`src/<path>.cheng` |
| `import std/<path>` | lock 中 stdlib CID 或仓库 `src/std/<path>.cheng` |
| `import cheng/<pkg>/<path>` | 兼容别名，归一化为 `<pkg>/<path>` |
| `import pkg/[a,b]` | 展开为 `import pkg/a`、`import pkg/b` |

禁止：

- `@import "../x.cheng"`
- `import "../x"`
- `import "/abs/x"`
- `import "pkg/path"`
- `from import`
- `import A, B`

## 6. Compiler Facts

World Resolver 的产物不止 lock，还要为编译器构造 canonical facts 输入：

| Facts | 作用 |
|---|---|
| `WorldFacts` | world head、package CID、compiler/std/runtime CID |
| `SourceBundleFacts` | 每个模块的路径、源码 hash、源码文本 CID |
| `ImportGraphFacts` | 归一化导入边、循环检测链 |
| `ExportSurfaceFacts` | Go 风格大写导出面 |
| `SyntaxSurfaceFacts` | parser/grammar 版本和可接受语法面 |
| `CompileReceiptFacts` | 本次编译输入、目标、产物和 receipt CID |

LSP 和 Debugger 不重新猜这些事实，只消费同一份 facts。

## 7. CLI

```
cheng new <project>
cheng deps add <package-id> --channel stable [--version ^1.2] [--source file:...]
cheng deps remove <package-id>
cheng deps resolve
cheng deps fetch
cheng deps status
cheng deps update [package-id]
cheng world receipt
cheng build --locked
cheng build --online
cheng run
cheng test
cheng publish
```

底层编译仍调用当前 driver：

```bash
artifacts/backend_driver/cheng system-link-exec \
  --root:/abs/project \
  --in:/abs/project/src/main.cheng \
  --emit:exe \
  --target:arm64-apple-darwin \
  --out:/tmp/app \
  --report-out:/tmp/app.report.txt
```

## 8. MVP

第一阶段：

- 把 `cheng.lock.toml` 升级为 world snapshot。
- `cheng build --locked` 校验 world/lock/cache 后再进入 driver。
- `cheng build --online` 只做前置 resolve/fetch 和原子 lock 更新。
- 输出 `compile_receipt_cid`。
- 为 import 正反例、lock 缺失、CID 缺失、签名错误加门禁。

第二阶段：

- 接入 libp2p world head 解析。
- 接入 package snapshot / syntax surface / export surface CID。
- LSP 和 Debugger 改为消费 compiler canonical facts。

## 9. 验收

```bash
cheng deps resolve
cheng deps fetch
cheng world receipt
cheng build --locked
```

必须验证：

- 无网络时 `--locked` 可复现构建。
- lock 指向的本地 CID 缺失时失败。
- 修改本地 CID cache 内容时失败。
- 同一 `package_id` 多 CID 冲突失败。
- `--online` 只在编译前联网，进入 driver 后不得联网。
- 编译报告包含 `world_head_cid`、`compile_receipt_cid`、`compiler_cid`、`stdlib_cid`、`runtime_cid`。
