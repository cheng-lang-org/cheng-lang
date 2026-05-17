# Cheng 包管理器设计

> 2026-05-17 · 设计文档（非规范）
> 规范以 `docs/cheng-formal-spec.md` 为准；实现状态以 `docs/roadmap.md`、当前源码和当前可执行产物为准。

## 0. 评估结论

原文最大问题是把包管理器写成“Git clone + `cheng_packages/` + `@import "path"`”。这和当前正式规范冲突。

必须改成：

- 包身份用 `package_id = "pkg://cheng/<name>"`，libp2p CID 快照是主来源；Git/file 只能作为迁移期镜像或本地开发源，不是语言层包身份。
- 依赖至少声明 `package_id + channel`，可附带 SemVer 约束和校验和。
- 锁文件固定为 `cheng.lock.toml`，记录 `cid/author_id/signature/epoch`、解析来源和校验和。
- 用户源码只写归一化导入：`import <pkg>/<path>`、`import std/<path>`、`import pkg/[a,b]`；禁止字符串、绝对路径、相对路径导入。
- “编译期不联网”指 lowering/codegen/link 不访问网络。`cheng build` 可以编排前置的 `deps resolve/fetch`，但进入编译器后只消费本地 lock 和本地包根。

## 1. 包模型

Cheng 包 = 一个包根目录 + `cheng-package.toml` + `src/` 模块树。

```
<project>/
├── cheng-package.toml
├── cheng.lock.toml        # 解析后生成，必须签入
├── src/
│   ├── main.cheng
│   └── internal/
└── tests/
```

本地内容缓存不放进源码树。默认位置：

- `PKG_ROOT` / `PKG_ROOTS` 显式指定时优先使用。
- 未指定时使用 `~/.cheng-packages` 与 `chengcache/packages/<cid>/`。

## 2. `cheng-package.toml`

当前规范口径使用顶层字段，避免 `[package]` 包一层。

```toml
package_id = "pkg://cheng/fs"
module_prefix = "fs"
edition = "2026"
type = "lib" # lib / bin / test

[[dependencies]]
package_id = "pkg://cheng/core"
channel = "stable"
version = "^0.2"
checksum = "sha256:..."

[[dependencies]]
package_id = "pkg://cheng/net"
channel = "edge"
```

必填字段：

| 字段 | 说明 |
|---|---|
| `package_id` | 全局包身份，格式为 `pkg://cheng/<name>` |
| `module_prefix` | 本包导入前缀，通常等于 `<name>` |
| `[[dependencies]].package_id` | 依赖包身份 |
| `[[dependencies]].channel` | 解析通道，如 `stable`、`edge` |

可选字段：

| 字段 | 说明 |
|---|---|
| `version` | SemVer 约束，如 `^1.2`、`>=1.0.0` |
| `checksum` | 源码快照或包产物校验和 |
| `source` | libp2p、file 或迁移期 Git 镜像源，只影响获取，不影响包身份 |

## 3. `cheng.lock.toml`

lock 文件记录解析后的不可变快照。生产构建只认 lock，不跟随 channel head。

```toml
format_version = 1

[[packages]]
package_id = "pkg://cheng/core"
module_prefix = "core"
channel = "stable"
version = "0.2.4"
cid = "bafy..."
author_id = "did:key:z..."
signature = "ed25519:..."
epoch = 42
source = "libp2p:cheng"
checksum = "sha256:..."
format = "source"
```

约束：

- 同一 `package_id` 在同一个 lock 中只能出现一个解析结果。
- channel 解析必须生成确定的 `cid`。
- `format = "source"` 表示源码直发，编译器从本地包根读取 `src/`。
- lock 缺失、签名不合法、校验和不匹配、同包多版本冲突都必须 hard-fail。

## 4. 导入映射

用户源码写法：

```cheng
import fs/path
import core/result as result
import libp2p/[crypto,transport,swarm]
import std/os
```

解析规则：

| 导入 | 映射 |
|---|---|
| `import <pkg>/<path>` | `<pkgroot>/src/<path>.cheng` |
| `import std/<path>` | 当前仓库 `src/std/<path>.cheng` |
| `import cheng/<pkg>/<path>` | 兼容别名，归一化为 `<pkg>/<path>` |
| `import pkg/[a,b]` | 展开为 `import pkg/a` 和 `import pkg/b` |

禁止：

- `@import "../x.cheng"`
- `import "../x"`
- `import "/abs/x"`
- `import "pkg/path"`
- `from import`
- `import A, B`

仓内源码模块可按规范尝试 `<workspace>/src/<module>.cheng`。这只服务本仓开发，不改变包级导入规则。

## 5. 全球统一版本与编译边界

Cheng 代码自托管在 Cheng libp2p 网络中，全球统一版本靠 channel head 和 CID 快照实现，不靠编译器边编译边联网。

正确流水线：

```
libp2p registry/channel head
  -> deps resolve：channel/version -> CID
  -> deps fetch：CID -> 本地内容寻址缓存
  -> cheng.lock.toml：签名快照
  -> compile：只读本地快照
```

因此有三种模式：

| 模式 | 是否联网 | 行为 |
|---|---|---|
| `cheng deps resolve/fetch` | 是 | 从 libp2p 网络解析 channel head、拉取 CID 快照、验签、写 lock |
| `cheng build --online` | 可联网 | 先跑 resolve/fetch，再进入离线编译阶段 |
| `cheng build --locked` / release | 否 | 只读 `cheng.lock.toml` 和本地缓存，缺失即 hard-fail |

编译器内层仍保持硬边界：

- 不在 import 解析过程中访问网络。
- 不在 typecheck/lowering/codegen/link 过程中更新 channel。
- 不因缓存缺失改去远程拉取。
- 不把“最新 stable”当成隐式输入；必须先固化为 lock 中的 CID。

包管理器职责分两段：

1. `cheng deps resolve/fetch`
   - 读取 `cheng-package.toml`。
   - 按 `package_id + channel + version/checksum` 解析依赖图。
   - 校验作者签名、CID 和校验和。
   - 写入 `cheng.lock.toml`。
   - 把源码快照放到本地包缓存。

2. `cheng build/run/test`
   - 读取 `cheng.lock.toml`。
   - 校验本地包根存在且内容匹配 lock。
   - 构造 `PKG_ROOTS` 或等价 external package roots。
   - 调用 `artifacts/backend_driver/cheng system-link-exec`。
   - 编译内层不访问网络，不更新 lock。

## 6. CLI 设计

```
cheng new <project>
cheng deps add <package-id> --channel stable [--version ^1.2] [--source file:...]
cheng deps remove <package-id>
cheng deps resolve
cheng deps fetch
cheng deps status
cheng deps update [package-id]
cheng build --locked
cheng build --online
cheng run
cheng test
cheng publish
```

`cheng build` 的公开参数应贴近当前 driver：

```bash
artifacts/backend_driver/cheng system-link-exec \
  --root:/abs/project \
  --in:/abs/project/src/main.cheng \
  --emit:exe \
  --target:arm64-apple-darwin \
  --out:/tmp/app \
  --report-out:/tmp/app.report.txt
```

## 7. 发布格式

主发布物是可验证源码快照：

- `cid` 固定源码树内容。
- `author_id/signature/epoch` 固定作者与通道更新顺序。
- `sources.list.txt` 可列出源码闭包。

`.chenga` 是 provider archive 产物，适合预编译 runtime/provider 或离线分发。它不能替代源码 lock，也不能在没有源码快照证明时声明包闭合。

## 8. MVP 范围

第一阶段只做能闭合的最小集合：

- 解析顶层 `cheng-package.toml`。
- 解析 `[[dependencies]]` 的 `package_id/channel/version/checksum`。
- 生成并校验 `cheng.lock.toml`。
- 支持本地 `file:` 源和已存在的本地包缓存。
- 支持 `PKG_ROOTS` / external package roots 映射。
- 编译内层按 lock 校验导入，不联网。
- `cheng build --online` 只允许在进入编译器前联网刷新 lock 和本地 CID 缓存。
- 为 `import <pkg>/<path>`、`import std/<path>`、`import pkg/[a,b]` 加正反例。

第二阶段再接入 registry/channel 到 CID 的真实解析和 libp2p 源获取。Git 只保留为导入旧生态的镜像源，不进入默认主线。

## 9. 验收条件

```bash
cheng new my-app
cd my-app
cheng deps add pkg://cheng/fs --channel stable --source file:../cheng-fs
cheng deps resolve
cheng deps fetch
cheng build --locked
cheng run
cheng test
```

必须额外验证：

- 删除本地包缓存后，`cheng build --locked` 直接失败并指出缺失包根。
- `cheng build --online` 只能在编译前刷新 lock/cache，进入 driver 后不得联网。
- 修改缓存源码后，lock 校验失败。
- 同一 `package_id` 解析到两个 CID 时失败。
- 字符串/相对/绝对 import 全部失败。
- `cheng.lock.toml` 缺签名或校验和不匹配时失败。
