# Cheng 去中心化计算与存储方案（基于 cheng-libp2p）

## 目标
- 提供 Cheng 语言原生的去中心化计算与存储能力。
- 支持 **可选模式**：本地运行/存储免费；去中心化运行/存储按需付费。
- 减少版本碎片：通道化依赖 + 严格语义化版本（SemVer） + 全局唯一主版本约束。
- 建立开发者收益闭环：输出可审计计量证据并对接 `RWAD-blockchain` 结算接口。

## 非目标
- 不追求取代以太坊或成为通用公链。
- 不在初期强制“链上执行”；但 RWAD 结算必须上链，优先落地链外执行 + 可验证回执。
- 不要求所有项目强制去中心化（保持本地模式可用）。

## 总体架构（两种模式）
### 1) 本地模式（默认）
- 本地编译、本地执行、本地存储。
- 不计费、不上报。

### 2) 去中心化模式（可选）
- **编译产物**：基于 Merkle DAG 的内容寻址（CAS），实现分块级去重。
- **计算**：由网络节点执行，输出结果 + 可验证回执；计算费用自动分润给包作者。
- **结算/激励**：`cheng-lang` 只输出可审计批次；积分与 RWAD 规则在 `RWAD-blockchain` 执行。

## 关键模块
### A0. 链上/链下边界（RWAD）
- **链上只存“指针与结算”**：通道 -> 最新快照 CID、作者签名、结算与审计事件上链即可。
- **包体与源码不上链**：包体走内容寻址存储与 P2P 分发，上链仅保存可验证摘要与引用。
- **可审计即可**：链上保存最小可验证数据，链下存储可复算与可追溯的来源。

### A. 编译与分发（去重与抗碎片）
- **DAG 分块去重**：包以 Merkle DAG 形式存储。新版本通过指向旧版未变动的 Chunk（分块）实现增量存储，物理层面最大化去重。
- **单一主版本规则**：依赖图中同一包名仅允许一个主版本（Major Version）存在。冲突时必须由顶层应用显式解决（Override 或重构），杜绝“依赖地狱”与隐式副作用。
- **通道锁定**：推荐依赖 `stable`/`lts` 通道。通道指针由 DAO 或可信方更新，结合 SemVer 兼容性检查，减少下游频繁改动 `lock` 文件。
- **SemVer 强制**：注册中心拒绝 API 破坏性改动（通过符号 diff）进入 Patch/Minor 版本发布，强制版本号语义准确。
- **可复现构建**：同一输入得到同一 Merkle Root；构建元数据包含完整的依赖 DAG 引用。
- **包身份**：包作者 namespace 的链侧认证规则由 `RWAD-blockchain` 定义；发布需签名。

### A1. 包与包管理器实现建议（落地）
- **统一清单**：包根目录固定 `cheng-package.toml`，lock 默认 `cheng.lock.toml`；manifest 只写依赖与通道，lock 只写可验证快照（`cid/author_id/signature/epoch`）。
- **解析规则**：单版本 + 通道；只允许顶层覆盖通道；支持 workspace/path override 但标记为 dev-only，禁止发布。
- **分发与缓存**：registry 只存快照映射；包体走 CID 内容寻址；客户端优先用 libp2p bitswap 拉取，失败再回退 HTTP；缓存到 `chengcache/packages/<cid>/`。
- **校验链路**：resolve -> verify(lock+registry) -> meta(build) -> buildmeta 写入产物；可选 ledger 校验，强制可审计。
- **工具链闭环**：`cheng_pkg resolve/verify/meta` + `chengc --manifest/--lock/--package` 一键完成依赖解析与构建元数据。
- **lock 校验落地**：`cheng_pkg_source lock-verify --lock:<file> [--registry:<path>]` 校验签名/冲突/注册表一致性。
- **闭环入口**：`sh src/tooling/tooling_exec.sh closedloop` 统一跑 bootstrap + verify（可选 `CLOSEDLOOP_LIBP2P=1` 触发 libp2p production closure；`CLOSEDLOOP_STRICT=1` 强制完整后端生产校验）。

### A1.1 源码直发（no-copy）
- **发布模式**：包不打包，直接发布源码清单（manifest），manifest 仅包含 `path/cid/size` 与 `source_addrs`。
- **只读接口**：`cheng_pkg_source serve` 的 p2p 服务能力已从 core binary 移出，需使用 p2p 专项工具链；core binary 当前仅保留 local-only manifest/publish/fetch。
- **锁文件标记**：`format = "source"` 表示依赖为源码直发；未标记默认 `tar`。
- **客户端拉取**：`cheng_pkg_fetch` 遇到 `format=source` 使用 `cheng_pkg_source fetch` 拉取源码到 `chengcache/packages/<cid>/`。
- **no-copy 的边界**：no-copy 代表“发布与分发策略”，而非物理上只有一份副本；可用访问控制/加密或远程执行保证不可分发。

### A1.2 版本图（Git-like）与“唯一入口”
- **版本图**：快照元数据包含 `cid/parent/author_id/signature/epoch/channel`，形成 DAG；支持 merge 与 tag。
- **唯一入口**：每个通道只有一个“最新快照指针”（head），作为全网统一入口；历史版本通过 CID 访问。
- **发布权唯一**：以作者签名与命名空间质押确立唯一发布权；禁止未授权更新通道。
- **不可复制策略**：建议强调“唯一发布权 + 唯一入口”，配合远程执行/加密传输，而非强行“全网唯一物理副本”。

### A1.3 import 同时支持本地与远程（libp2p）
- **语法保持不变**：`import cheng/<pkg>/...`；解析层决定本地或远程来源。
- **域名内容寻址**：`cheng/<pkg>` 视为包域名（IPNS 风格），等价 `pkg://cheng/<pkg>`；registry 解析出通道 head 的 `cid`，lock 固定快照版本。
- **解析顺序**：workspace/path override（dev-only）-> 本地缓存 -> libp2p 拉取 -> HTTP 回退（可禁用）。
- **可控开关**：`PKG_MODE=auto|local|p2p`、`PKG_PEERS` 控制远程来源；lock 记录 `source_mode/source_addrs`。
- **一致性保障**：lock 固定 CID，远程只提供内容传输，不影响依赖解析与可复现构建。

#### A1.3.1 域名映射与本地包根
- **域名映射**：`cheng/<pkg>/...` => `pkg://cheng/<pkg>`，并以 `cheng-package.toml` 的 `package_id` 校验一致性。
- **本地包根**：包源码根为 `src/`；编译器解析 `import cheng/<pkg>/<path>` 时映射为 `<pkgroot>/src/<path>.cheng`（包名仅用于定位包根，不参与包内路径）。
- **缓存路径**：默认落地到 `chengcache/packages/<cid>/`；也可镜像到 `~/.cheng-packages/<pkg>/<cid>/` 便于开发工具索引。

### A1.4 lock 结构草案（支持版本图与远程来源）
```toml
# cheng.lock.toml（示意）
package_id = "pkg://cheng/app-demo"
created_ts = "1730000000"

[[dependencies]]
package_id = "pkg://cheng/libp2p"
channel = "edge"
cid = "bafy..."
author_id = "node:alice"
pub_key = "ed25519:..."
signature = "ed25519:..."
epoch = 12
format = "tar"               # 或 source
parent = "bafy..."           # 建议扩展：上一个快照
head = "bafy..."             # 建议扩展：通道 head
source_mode = "p2p"          # 建议扩展：local|p2p|http|auto
source_addrs = ["/ip4/1.2.3.4/tcp/4001", "/dns4/node.example/tcp/4001"]

[[dependencies]]
package_id = "pkg://cheng/gui"
channel = "stable"
cid = "bafy..."
author_id = "node:bob"
pub_key = "ed25519:..."
signature = "ed25519:..."
epoch = 8
format = "source"
source_mode = "auto"
```

### A1.5 版本操作与 CLI 设计
- **解析与锁定**：`cheng_pkg resolve --manifest --registry --out` 生成 lock。
- **追溯与对比**：`cheng_pkg log --package` 输出版本图；`cheng_pkg diff --from --to` 比较快照。
- **切换版本**：`cheng_pkg checkout --cid` 输出包根目录，供编译/测试使用。
- **远程拉取**：`cheng_pkg fetch --lock --mode:p2p --peers:<list>` 与 `--mode:local` 对齐。

示例输出（建议）：
```text
$ cheng_pkg log --package:pkg://cheng/libp2p
* head  bafy...  edge  epoch=12  author=node:alice
  prev  bafy...

$ cheng_pkg diff --from:bafyA --to:bafyB
changed: src/main.cheng
added:   src/utils/net.cheng

$ cheng_pkg checkout --cid:bafy...
root: chengcache/packages/bafy...
```

### A1.6 lock 字段规范表（建议）
顶层字段：
| 字段 | 必填 | 说明 | 示例 |
| --- | --- | --- | --- |
| package_id | 是 | 当前包 ID | `pkg://cheng/app-demo` |
| created_ts | 否 | 生成时间戳 | `1730000000` |

dependencies 字段（当前实现 + 建议扩展）：
| 字段 | 必填 | 说明 | 示例 |
| --- | --- | --- | --- |
| package_id | 是 | 包唯一标识 | `pkg://cheng/libp2p` |
| channel | 是 | 通道名 | `edge` |
| cid | 是 | 快照 CID（原始 CID 文本） | `bafy...` |
| format | 否 | 包格式 | `tar`/`source` |
| parent | 否 | 版本图父节点 | `bafy...` |
| head | 否 | 通道 head，用于一致性校验 | `bafy...` |
| author_id | 是 | 作者身份 | `node:alice` |
| pub_key | 否 | 作者公钥 | `ed25519:...` |
| signature | 是 | 作者签名 | `ed25519:...` |
| epoch | 是 | 发布 epoch | `12` |
| source_mode | 否 | 解析/拉取来源 | `local`/`p2p`/`http`/`auto` |
| source_addrs | 否 | 远程来源列表 | `["/ip4/1.2.3.4/tcp/4001"]` |

### A1.7 最小示例对（manifest + lock）
`cheng-package.toml`（本地/远程通用）：
```toml
package_id = "pkg://cheng/app-demo"

[[dependencies]]
package_id = "pkg://cheng/libp2p"
channel = "edge"
```

`cheng.lock.toml`（本地）：
```toml
package_id = "pkg://cheng/app-demo"
created_ts = "1730000000"

[[dependencies]]
package_id = "pkg://cheng/libp2p"
channel = "edge"
cid = "bafy..."
author_id = "node:alice"
pub_key = "ed25519:..."
signature = "ed25519:..."
epoch = 12
format = "tar"
source_mode = "local"
```

`cheng.lock.toml`（p2p）：
```toml
package_id = "pkg://cheng/app-demo"
created_ts = "1730000000"

[[dependencies]]
package_id = "pkg://cheng/libp2p"
channel = "edge"
cid = "bafy..."
author_id = "node:alice"
pub_key = "ed25519:..."
signature = "ed25519:..."
epoch = 12
format = "source"
source_mode = "p2p"
source_addrs = ["/ip4/1.2.3.4/tcp/4001"]
```

### A1.8 当前实现字段对照（cheng_pkg resolve/verify/meta）
当前实现（锁文件）：
- 顶层：`package_id`、`created_ts`、`dependencies`
- 依赖：`package_id`、`channel`、`epoch`、`cid`、`format`、`author_id`、`pub_key`、`signature`

当前实现（meta 文件）：
- 顶层：`package_id`、`channel`、`epoch`、`cid`、`format`、`author_id`、`pub_key`、`signature`、`created_ts`、`dependencies`

扩展建议（需更新解析/序列化与校验）：
- 依赖级：`parent`、`head`、`source_mode`、`source_addrs`

### A1.9 签名/审计失败处理（建议）
- **签名校验失败**：直接拒绝解析；除非显式 `--allow-unsigned`（开发模式）。
- **registry 不一致**：`verify` 失败并标记为不可信快照，默认阻断构建。
- **审计/抽样异常**：将包标记为 `quarantined`，需要人工确认或更高阈值复验。
- **远程拉取失败**：允许按策略回退本地缓存；生产环境建议禁用 HTTP 回退。

### A1.10 cheng_pkg 输出样例（当前实现）
`resolve --format:toml`：
```toml
package_id = "pkg://cheng/app-demo"
created_ts = "1730000000"

[[dependencies]]
package_id = "pkg://cheng/libp2p"
channel = "edge"
epoch = 12
cid = "bafy..."
format = "tar"
author_id = "node:alice"
pub_key = "ed25519:..."
signature = "ed25519:..."
```

`resolve --format:yaml`：
```yaml
package_id: "pkg://cheng/app-demo"
created_ts: "1730000000"
dependencies:
  - package_id: "pkg://cheng/libp2p"
    channel: "edge"
    epoch: 12
    cid: "bafy..."
    format: "tar"
    author_id: "node:alice"
    pub_key: "ed25519:..."
    signature: "ed25519:..."
```

`resolve --format:json`：
```json
{
  "package_id": "pkg://cheng/app-demo",
  "created_ts": "1730000000",
  "dependencies": [
    {
      "package_id": "pkg://cheng/libp2p",
      "channel": "edge",
      "epoch": 12,
      "cid": "bafy...",
      "format": "tar",
      "author_id": "node:alice",
      "pub_key": "ed25519:...",
      "signature": "ed25519:..."
    }
  ]
}
```

`meta --format:toml`：
```toml
package_id = "pkg://cheng/app-demo"
channel = "stable"
epoch = 3
cid = "bafy..."
format = "tar"
author_id = "node:alice"
pub_key = "ed25519:..."
signature = "ed25519:..."
created_ts = "1730000000"

[[dependencies]]
package_id = "pkg://cheng/libp2p"
channel = "edge"
epoch = 12
cid = "bafy..."
format = "tar"
author_id = "node:alice"
pub_key = "ed25519:..."
signature = "ed25519:..."
```

`meta --format:yaml`：
```yaml
package_id: "pkg://cheng/app-demo"
channel: "stable"
epoch: 3
cid: "bafy..."
format: "tar"
author_id: "node:alice"
pub_key: "ed25519:..."
signature: "ed25519:..."
created_ts: "1730000000"
dependencies:
  - package_id: "pkg://cheng/libp2p"
    channel: "edge"
    epoch: 12
    cid: "bafy..."
    format: "tar"
    author_id: "node:alice"
    pub_key: "ed25519:..."
    signature: "ed25519:..."
```

`meta --format:json`：
```json
{
  "package_id": "pkg://cheng/app-demo",
  "channel": "stable",
  "epoch": 3,
  "cid": "bafy...",
  "format": "tar",
  "author_id": "node:alice",
  "pub_key": "ed25519:...",
  "signature": "ed25519:...",
  "created_ts": "1730000000",
  "dependencies": [
    {
      "package_id": "pkg://cheng/libp2p",
      "channel": "edge",
      "epoch": 12,
      "cid": "bafy...",
      "format": "tar",
      "author_id": "node:alice",
      "pub_key": "ed25519:...",
      "signature": "ed25519:..."
    }
  ]
}
```

`verify` 输出：
```
ok
```

### A1.11 错误输出与返回码（当前实现）
- **返回码**：成功 `0`，失败 `1`（当前实现）。
- **常见错误输出（示例）**：
```
resolve: missing --manifest
resolve: missing --registry
resolve: load manifest failed
resolve: failed
resolve: unknown format

verify: missing --lock
verify: load lock failed
verify: single-version conflict
verify: signature failed
verify: registry mismatch

meta: missing --lock
meta: missing --package
meta: missing --channel
meta: missing --registry
meta: load lock failed
meta: package mismatch
meta: snapshot not found
meta: unknown format
```

### A1.12 cheng_pkg 命令与参数规范（当前实现）
命令与参数：
- `resolve --manifest:<file> [--registry:<path>] [--out:<file>] [--format:toml|yaml|json]`
- `verify  --lock:<file> [--registry:<path>]`
- `meta    --lock:<file> --package:<id> --channel:<edge|stable|lts> [--registry:<path>] [--out:<file>] [--format:toml|yaml|json]`

默认值与行为：
- `resolve/meta` 默认 `--registry:build/cheng_registry/registry.jsonl`。
- `--format` 默认 `toml`；未知格式返回错误。
- `--out` 为空时输出到 stdout。

### A1.13 lock 扩展兼容/升级策略（建议）
- **现状**：TOML/YAML 解析对未知字段严格，遇到未知 key 会失败；JSON 解析会忽略未知字段。
- **策略**：新增字段（如 `parent/head/source_mode/source_addrs`）需同步升级解析/序列化与校验逻辑。
- **版本化**：可新增 `lock_version = 2`，并在 v2 中允许未知字段或显式白名单扩展字段。
- **迁移**：旧锁文件保持 v1；新字段仅在 v2 写出，避免旧工具链解析失败。

### A1.14 verify 冲突报告示例（当前实现）
`verify` 在单版本冲突时输出：
```
verify: single-version conflict
- pkg://cheng/fs: stable@1 bafy... [tar] vs edge@2 bafy... [source]
```

### A1.15 lock_version 迁移流程（建议）
目标：在不破坏旧工具链的前提下引入扩展字段。

示例（v1，无扩展字段）：
```toml
package_id = "pkg://cheng/app-demo"
created_ts = "1730000000"

[[dependencies]]
package_id = "pkg://cheng/libp2p"
channel = "edge"
epoch = 12
cid = "bafy..."
format = "tar"
author_id = "node:alice"
pub_key = "ed25519:..."
signature = "ed25519:..."
```

示例（v2，启用扩展字段）：
```toml
lock_version = 2
package_id = "pkg://cheng/app-demo"
created_ts = "1730000000"

[[dependencies]]
package_id = "pkg://cheng/libp2p"
channel = "edge"
epoch = 12
cid = "bafy..."
format = "tar"
author_id = "node:alice"
pub_key = "ed25519:..."
signature = "ed25519:..."
parent = "bafy..."
head = "bafy..."
source_mode = "p2p"
source_addrs = ["/ip4/1.2.3.4/tcp/4001"]
```

迁移建议：
- v1 仍作为默认写出格式，确保旧工具链可用。
- 当使用扩展字段时写出 `lock_version = 2`，并要求 v2 解析器。
- 解析器遇到未知字段时，应在 v2 宽容处理或显式白名单扩展字段。

### A1.16 冲突修复建议（建议）
- **顶层应用 override**：显式选择 `stable/lts` 或特定通道并重写依赖。
- **重构依赖**：分离冲突功能或抽出公共子包。
- **版本合流**：推动上游依赖收敛至同一主版本后再发布。

### A1.17 远程导入白名单/黑名单配置草案（建议）
环境变量与配置文件建议（二选一即可）：
```bash
# 环境变量（最小）
PKG_MODE=p2p
PKG_PEERS=/ip4/1.2.3.4/tcp/4001,/dns4/node.example/tcp/4001
PKG_ALLOW=/dns4/node.example/tcp/4001
PKG_DENY=/ip4/5.6.7.8/tcp/4001
```

```toml
# cheng-pkg-config.toml（示意）
[remote]
mode = "p2p"                 # local|p2p|auto
peers = ["/ip4/1.2.3.4/tcp/4001", "/dns4/node.example/tcp/4001"]
allow = ["/dns4/node.example/tcp/4001"]
deny = ["/ip4/5.6.7.8/tcp/4001"]
http_fallback = false
```

字段说明与默认值（建议）：
| 字段 | 默认值 | 说明 |
| --- | --- | --- |
| remote.mode | `auto` | 解析来源：`local|p2p|auto` |
| remote.peers | `[]` | 远程种子节点列表 |
| remote.allow | `[]` | 允许的 peer 地址白名单（空表示不过滤） |
| remote.deny | `[]` | 拒绝的 peer 地址黑名单 |
| remote.http_fallback | `false` | 是否允许 HTTP 回退 |
| remote.require_signature | `true` | 是否强制签名校验 |
| remote.require_registry_match | `true` | 是否强制 registry 校验 |

### A1.18 HTTP 回退策略与安全等级（建议）
- **等级 L0（开发）**：允许 HTTP 回退；签名失败可提示但不阻断。
- **等级 L1（默认）**：禁用 HTTP 回退；签名失败即拒绝；registry 不一致拒绝。
- **等级 L2（生产）**：只允许白名单 peer；强制签名 + registry 校验；记录审计日志。

### A1.19 环境变量优先级与合并规则（建议）
优先级（高 → 低）：
1) CLI 参数（如 `--mode/--peers`）
2) 环境变量（`PKG_*`）
3) 配置文件 `cheng-pkg-config.toml`
4) 内置默认值

合并规则：
- `allow/deny/peers`：支持逗号分隔，CLI 覆盖环境变量，环境变量覆盖配置文件。
- `mode/http_fallback/require_signature/require_registry_match`：取优先级最高的单值。

### A1.20 生产/开发配置推荐（建议）
开发（L0）：
```toml
[remote]
mode = "auto"
http_fallback = true
require_signature = false
require_registry_match = false
```

默认（L1）：
```toml
[remote]
mode = "auto"
http_fallback = false
require_signature = true
require_registry_match = true
```

模板文件：
- `configs/cheng-pkg-config.toml`

### A1.21 `PKG_*` 环境变量清单（建议）
- `PKG_MODE`：`local|p2p|auto`
- `PKG_PEERS`：逗号分隔 peer 列表
- `PKG_ALLOW`：逗号分隔白名单
- `PKG_DENY`：逗号分隔黑名单
- `PKG_HTTP_FALLBACK`：`0|1`
- `PKG_REQUIRE_SIGNATURE`：`0|1`
- `PKG_REQUIRE_REGISTRY_MATCH`：`0|1`

生产（L2）：
```toml
[remote]
mode = "p2p"
peers = ["/dns4/node.example/tcp/4001"]
allow = ["/dns4/node.example/tcp/4001"]
http_fallback = false
require_signature = true
require_registry_match = true
```

### A2. 包化示例（cheng-libp2p / cheng-gui）
包清单（TOML）：
```toml
# /Users/lbcheng/.cheng-packages/cheng-libp2p/cheng-package.toml
package_id = "pkg://cheng/libp2p"

# /Users/lbcheng/cheng-gui/cheng-package.toml
package_id = "pkg://cheng/gui"

[[dependencies]]
package_id = "pkg://cheng/libp2p"
channel = "edge"
```

原生 libp2p 引入与调用（示例）：
```cheng
import cheng/libp2p/main as libp2p
import cheng/libp2p/utils/result as result

fn p2pProbe(listenAddr: str): bool =
    let hostRes: result.Result[libp2p.Switch] = libp2p.newHostWithAddressText(listenAddr)
    if result.IsErr(hostRes):
        return false
    var host: libp2p.Switch = result.Value(hostRes)
    let startRes: result.Result[bool] = libp2p.hostStart(host)
    if result.IsErr(startRes):
        return false
    libp2p.hostStop(host)
    return true
```

`cheng-gui` 已提供 `ide/gui/services/p2p_bridge.cheng`，通过 `IDE_P2P_LISTEN` 环境变量启用原生 libp2p 启动入口。

### A3. 生产闭环流程（打包 -> 发布 -> 解析 -> 拉取 -> 编译）
1) 打包并发布快照：
```bash
sh src/tooling/tooling_exec.sh cheng_pkg_publish --src:/Users/lbcheng/.cheng-packages/cheng-libp2p --package:pkg://cheng/libp2p \
  --author:node:alice --channel:edge --epoch:1 --priv:keys/alice.priv
```

源码直发（no-copy）发布：
```bash
sh src/tooling/tooling_exec.sh cheng_pkg_publish --src:/Users/lbcheng/.cheng-packages/cheng-libp2p --package:pkg://cheng/libp2p \
  --author:node:alice --channel:edge --epoch:1 --priv:keys/alice.priv --format:source \
  --source-addr:/ip4/127.0.0.1/tcp/4005
```

源码只读服务：
```bash
# 注：`cheng_pkg_source serve` 已移出 core binary（local-only），
# 该示例需使用 p2p 专项工具链对应的 source serve 命令。
```

2) 解析依赖并生成 lock：
```bash
./cheng_registry resolve --package:pkg://cheng/libp2p --channel:edge --registry:build/cheng_registry/registry.jsonl
./cheng_pkg resolve --manifest:docs/cheng-package-manifest.toml --registry:build/cheng_registry/registry.jsonl \
  --out:build/cheng_pkg/cheng.lock.toml
```

3) 拉取依赖快照并准备 `PKG_ROOTS`：
```bash
sh src/tooling/tooling_exec.sh cheng_pkg_fetch --lock:build/cheng_pkg/cheng.lock.toml --print-roots
```

源码直发拉取（可选指定源地址）：
```bash
sh src/tooling/tooling_exec.sh cheng_pkg_fetch --lock:build/cheng_pkg/cheng.lock.toml --print-roots \
  --source-peer:/ip4/127.0.0.1/tcp/4005
```

4) 编译（`chengc` 会自动拉取并设置 `PKG_ROOTS`）：
```bash
sh src/tooling/tooling_exec.sh chengc examples/your_app.cheng --manifest:docs/cheng-package-manifest.toml \
  --lock:build/cheng_pkg/cheng.lock.toml --registry:build/cheng_registry/registry.jsonl
```

说明：
- 包缓存默认 `chengcache/packages`，可通过 `--pkg-cache:<dir>` 或 `PKG_CACHE` 调整。
- `PKG_ROOT/PKG_ROOTS` 用于解析 `cheng/<pkg>/...` 域名导入；未设置时默认 `~/.cheng-packages`；可指向包容器根，解析器会优先尝试 `cheng-<pkg>` 子目录。

### A3.1 生产闭环（加固版，含结算）
目标：从发布到结算形成可审计闭环，并满足生产安全策略。

0) 生产配置（L2 示例）：
```toml
[remote]
mode = "p2p"
peers = ["/dns4/node.example/tcp/4001"]
allow = ["/dns4/node.example/tcp/4001"]
http_fallback = false
require_signature = true
require_registry_match = true
```

1) 发布快照（含签名）：
```bash
./cheng_registry keygen --out:build/cheng_registry/keypair.json
sh src/tooling/tooling_exec.sh cheng_pkg_publish --src:/Users/lbcheng/.cheng-packages/cheng-libp2p --package:pkg://cheng/libp2p \
  --author:node:alice --channel:stable --epoch:1 --priv:keys/alice.priv
```

2) 解析 + 校验：
```bash
./cheng_pkg resolve --manifest:docs/cheng-package-manifest.toml --registry:build/cheng_registry/registry.jsonl \
  --out:build/cheng_pkg/cheng.lock.toml
./cheng_pkg verify --lock:build/cheng_pkg/cheng.lock.toml --registry:build/cheng_registry/registry.jsonl
./cheng_pkg meta --lock:build/cheng_pkg/cheng.lock.toml --package:pkg://cheng/app-demo --channel:stable \
  --registry:build/cheng_registry/registry.jsonl --out:build/cheng_pkg/cheng.meta.toml
```

3) 拉取 + 编译（仅 p2p，禁 HTTP 回退）：
```bash
PKG_MODE=p2p PKG_PEERS=/dns4/node.example/tcp/4001 \
  sh src/tooling/tooling_exec.sh cheng_pkg_fetch --lock:build/cheng_pkg/cheng.lock.toml --print-roots

PKG_MODE=p2p PKG_PEERS=/dns4/node.example/tcp/4001 \
  sh src/tooling/tooling_exec.sh chengc examples/your_app.cheng --manifest:docs/cheng-package-manifest.toml \
  --lock:build/cheng_pkg/cheng.lock.toml --registry:build/cheng_registry/registry.jsonl
```

4) 运行 + 计量 + 结算（示意）：
```bash
# 记录租约或计算计量，再生成回执与结算
./cheng_storage lease --package:pkg://cheng/libp2p --author:node:alice --provider:node:store-1 \
  --file:README.md --days:30 --replicas:2 --price:0.25 --royalty:0.12 --treasury:0.03 \
  --root:build/cheng_storage --mode:local

./cheng_storage settle --epoch:1 --root:build/cheng_storage --format:json --top:10
```

生产检查清单：
- `verify` 必须通过（签名 + registry 一致）。
- 禁用 HTTP 回退，白名单 peer 生效。
- 产物旁生成 `cheng.meta.toml` 以便审计。
- 结算输出保存并上链摘要（epoch、hash、payouts）。

### A4. 语义去重与反作弊 (Semantic Deduplication)
针对功能/代码高度相似的“洗稿”或恶意 Fork 包，实施语义级去重策略：

1.  **AST 指纹 (SimHash)**：
    - 发布时，注册中心对包源码进行 AST 解析，提取结构化特征（忽略命名与注释），计算局部敏感哈希（SimHash）。
    - 相似度 >= 90% 的新包将被标记为“高相似副本”。

2.  **原创值分 (Originality Score)**：
    - 每个包拥有原创值（0.0-1.0）。首发包默认为 1.0。
    - 高相似副本的原创值默认为 0.0，不具备 RWAD 挖矿（版税/引用奖励）资格。
    - **申诉机制**：若 Fork 版本有重大性能优化或 Bug 修复，需提交差异报告并通过 DAO/审计委员会投票，才能恢复原创值与收益资格。

3.  **同质化折叠**：
    - 包管理器 `search` 结果默认折叠高相似副本，优先展示高原创值、高引用数的源包。
    - 旨在消除生态噪音，让流量与收益回归真正的创作者。

### B. 存储层（类似 IPFS + 租约）
- **内容寻址**：chunk + Merkle DAG + CID。
- **写入收费**：持久化必须携带存储租约 token。
- **读免费**：读取走链外缓存，防止每次读都付费。
- **租约与回收**：到期自动 GC，防止状态膨胀。
- **存储证明**：节点定期提交存储证明，否则扣罚。

### C. 计算层（改进 EVM 的不足）
- **链外执行 + 可验证回执**：先做结果可验，再决定是否上链结算。
- **计量模型**：CPU/内存/IO/GPU 权重化，GPU 覆盖训练/推理。
- **GPU 资源约束**：支持 `gpu_ms`/`gpu_mem_bytes`/`gpu_count`、`gpu_type` 与 `workload_kind`（train/infer/other）。
- **批量结算**：按 epoch 聚合结算，降低链交互成本。
- **仲裁机制**：对争议任务进行复算或抽查。

### D. 计费与分润（RWAD 生态飞轮）
- **cheng-lang 职责**：只生产可审计事件（租约、计量、回执、审计、欺诈、存储证明）与 epoch 结算批次。
- **RWAD-blockchain 职责**：预算、积分核算、兑换率、队列积压、惩罚与状态滚动全部在链侧执行。
- **对接方式**：`cheng_rwad_bridge export/apply` + `cheng-rwad-settlement/v2` 契约，不在本仓实现积分↔RWAD算法。
- **策略文档归属**：经济模型、参数治理与风控规则统一维护在 `RWAD-blockchain/docs`。

### E. 链上结算与 RWAD
- **结算单位**：RWAD（统一结算单位，不承诺固定法币面值）。
- **接口边界**：`cheng-lang` 仅提交批次证据；链侧返回最终结算结果与下一状态。
- **批量结算**：按 epoch 批量提交，降低链上交互成本。
- **跨链一致性**：由 `RWAD-blockchain` 侧桥接策略保障。

## 优点
- **收益闭环清晰**：包作者长期获得租约/计算分润。
- **版本碎片受控**：通道 + 单版本规则收敛生态。
- **用户可选**：本地/去中心化模式切换，降低阻力。
- **读体验好**：读免费/缓存减少使用门槛。

## 缺点 / 风险
- **强制性不足**：开源工具链可被绕过计费。
- **系统复杂度高**：编译、存储、计费、证明、仲裁均需设计。
- **性能波动**：远程执行与证明增加延迟。
- **冷启动困难**：需要足够节点与数据才能有体验优势。
- **合规压力**：跨境支付、税务、KYC 可能成为阻碍。

## 关键改进点（优先级）
1) **可复现构建/签名产物**：抗碎片与抗作弊的基础。
2) **存储租约 + 回收机制**：保证长期可持续性。
3) **读免费但带宽激励**：避免节点被刷。
4) **链外执行回执 + 仲裁**：先解决可信计算再考虑上链。
5) **经济模型稳定**：避免“写少收入不足”的激励失衡。

## 路线图
### Phase 0：规格与协议
- 定义 CID、租约、执行回执、分润规则。
- 输出最小协议与数据结构文档。

### Phase 1：存储网络 MVP
- libp2p DHT + 内容寻址 + 存储证明。
- 租约发行与回收机制。

### Phase 2：去中心化编译与执行 MVP
- 可复现构建/官方签名产物。
- 远程执行节点 + 计量 + 回执。
- RWAD 结算模块接入与批量提交接口。

### Phase 3：可信执行与结算优化
- 抽查/仲裁 + RWAD 链上批量结算。
- 扩展生态激励与反作弊策略。

## 统筹推进（与开发计划同步）
- 统筹执行计划见 `docs/cheng-dev-plan.md` 的 **去中心化计算与存储（统筹执行计划・TOML）**。
- 当前推进重点（P2 compute-pilot）：执行请求/回执协议、计量 SDK/宏、epoch 结算管线。
- 下一阶段（P3 compiler-integration）：编译产物元数据、P2P IO 后端、构建元数据写入。
- RWAD 链上结算为硬性要求：计量/回执聚合后必须上链结算并生成可审计凭证。

## 当前实现（MVP 起步）
- 位置：`cheng/decentralized/*`（CID/租约/账本/本地存储 + libp2p P2P 入口 + 注册中心 + 计量/结算/审计 + 抽样/欺诈/信誉 + 执行请求/回执 + 计量 SDK）
- 结算现状：账本事件仍写入 `ledger.jsonl`；`cheng-lang` 通过 `cheng_rwad_bridge export/apply` 与 `RWAD-blockchain` 接口对接，不在本仓内执行积分↔RWAD兑换规则。
- 接口契约：`export` 批次固定输出 `settlement_sha256`；`apply` 默认强校验 `result_schema/request_schema` 与批次状态。
- 计算计量与执行请求新增 GPU 字段：`gpu_ms/gpu_mem_bytes/gpu_count/gpu_type/workload_kind` 与 GPU 价格字段，账本事件与 CLI 参数同步。
- ledger 事件新增 `audit_sample`/`fraud_report`/`storage_proof`，用于抽样审计、作恶上报与存储证明记录。
- CLI：
  - `src/tooling/cheng_storage.cheng`（存储 + 计量 + 结算 + 审计）
  - `sh src/tooling/tooling_exec.sh cheng_rwad_bridge`（RWAD 接口导出/回执应用）
  - `sh src/tooling/tooling_exec.sh verify_rwad_interface_contract`（跨仓接口契约验收）
  - `src/tooling/cheng_registry.cheng`（通道注册中心）
  - `src/tooling/cheng_pkg.cheng`（manifest 解析与 resolve/lock/verify）
- `--mode:p2p` 已接入 cheng-libp2p bitswap 的最小读写与 `serve`，用于点对点获取区块；可选从本地块目录预热 bitswap。
- 可选 IO backend：新增 `cheng/decentralized/io_backend.cheng`，支持 `cid://` 读写与 `IO_MODE/ROOT/LEDGER/LISTEN/PEERS` 模式切换。
- IO backend 增强：提供 `readText/writeText` 及 `readTextAuto/writeTextAuto`（按环境变量初始化默认 backend）。
- IO backend 租约写入：提供 `storeBytesWithLease/putFileWithLease` 与 `*Auto` 版本（写入前验证租约 token，并在 ledger 记录租约成本）。
- IO backend 租约强制开关：`IO_REQUIRE_LEASE=1` 且 `IO_MODE=p2p` 时，`writeBytes/putFile/storeBytes` 会拒绝无租约写入。
- IO shim（应用侧便捷 API）：新增 `cheng/decentralized/io_shim.cheng`，提供 `readFileAuto/writeFileAuto` 与 `ioLastError`，以及 `write*WithLease`（无 Result 样板）。
- CLI put 对齐：`cheng_storage put` 使用 io_backend，支持租约写入时落账本，并遵循租约强制策略。
- 新增 **租约 token 签名/校验**：`leasegen` 生成租约 token（含签名），`put --lease:<token>` 在写入前校验。
- 新增跨仓闭环脚本：`sh src/tooling/tooling_exec.sh demo_compute_settle` 与 `sh src/tooling/tooling_exec.sh verify_demo_compute_settle` 覆盖 exec/meter/receipt/sample/audit/fraud/settle/bridge-export/bridge-apply。

CLI 示例：
```bash
# 编译 CLI
sh src/tooling/tooling_exec.sh chengc src/tooling/cheng_storage.cheng --name:cheng_storage
sh src/tooling/tooling_exec.sh chengc src/tooling/cheng_registry.cheng --name:cheng_registry
sh src/tooling/tooling_exec.sh chengc src/tooling/cheng_pkg.cheng --name:cheng_pkg

# 初始化存储目录
./cheng_storage init --root:build/cheng_storage --mode:local

# 写入文件并返回 CID
./cheng_storage put --file:README.md --root:build/cheng_storage --mode:local

# 写入文本并返回 CID（可配租约）
./cheng_storage put-text --text:"hello" --root:build/cheng_storage --mode:local
# echo "hello" | ./cheng_storage put-text --stdin --root:build/cheng_storage --mode:local

# 读取 CID 到文件
./cheng_storage get --cid:<cid> --out:/tmp/out.txt --root:build/cheng_storage --mode:local

# 读取 CID 并输出文本
./cheng_storage cat --cid:<cid> --root:build/cheng_storage --mode:local
# ./cheng_storage cat --cid:<cid> --raw --root:build/cheng_storage --mode:local

# 最小端到端示例（租约 -> 写入 -> 读取 -> 结算）
# sh src/tooling/tooling_exec.sh demo_io_lease --mode:local --root:build/cheng_demo
# sh src/tooling/tooling_exec.sh demo_io_lease --mode:p2p --root:build/cheng_demo --listen:/memory/1 --peer:/memory/1 --require-lease
# # 可选：--clean 清理 root，--reset-ledger 仅清理 ledger，--fail-without-lease 演示无租约写入失败
# #      --regen-lease 强制重新生成租约 token（默认复用以便重复验收）
# # p2p 模式要求至少提供 --listen 或 --peer
# # demo 会同时展示 cat 与 cat --raw 的输出差异
# # 验收脚本：sh src/tooling/tooling_exec.sh verify_demo_io_lease --mode:local --root:build/cheng_demo_verify

# 最小计算闭环示例（exec -> meter -> receipt -> sample/audit -> settle -> bridge-export -> bridge-apply）
# sh src/tooling/tooling_exec.sh demo_compute_settle --mode:local --root:build/cheng_demo_compute
# # 验收脚本：sh src/tooling/tooling_exec.sh verify_demo_compute_settle --mode:local --root:build/cheng_demo_compute_verify

# P2P serve：监听并响应 bitswap 请求（默认处理 1 次，可用 --max 调整）
./cheng_storage serve --root:build/cheng_storage --mode:p2p --listen:/memory/1 --max:10

# P2P 写入（落盘，供 serve 预热后分发）
./cheng_storage put --file:README.md --root:build/cheng_storage --mode:p2p

# P2P 读取（向 peer 请求）
./cheng_storage get --cid:<cid> --out:/tmp/out.txt --root:build/cheng_storage --mode:p2p --peer:/memory/1

# IO backend（可选）：使用 cid:// 走去中心化读取
# IO_MODE=p2p IO_ROOT=build/cheng_storage IO_PEERS=/memory/1
# IO_REQUIRE_LEASE=1
#
# # IO backend（租约写入）
# # import cheng/decentralized/io_backend as iob
# # import cheng/decentralized/lease_token as dtoken
# # import cheng/libp2p/utils/result as result
# # let tokRes = dtoken.loadLeaseTokenFile("build/cheng_storage/lease-token.json")
# # if result.IsOk(tokRes):
# #     iob.putFileWithLeaseAuto("README.md", result.Value(tokRes))
#
# # IO shim（应用侧）
# # import cheng/decentralized/io_shim as ioshim
# # let text = ioshim.readFileAuto("cid://<cid>")
# # if len(ioshim.ioLastError()) > 0:
# #     # 处理错误
# #     0
#
# # write with lease token
# # let cid = ioshim.writeTextAutoWithLease("hello", "build/cheng_storage/lease-token.json")
# # if len(ioshim.ioLastError()) > 0:
# #     0

# 记录租约并写入账本
./cheng_storage lease --package:pkg://cheng/fs --author:node:alice --provider:node:store-1 \
  --file:README.md --days:30 --replicas:2 --price:0.25 --royalty:0.12 --treasury:0.03 \
  --root:build/cheng_storage --mode:local

# 生成租约 token（签名）
./cheng_storage leasegen --package:pkg://cheng/fs --author:node:alice --provider:node:store-1 \
  --file:README.md --days:30 --replicas:2 --price:0.25 --royalty:0.12 --treasury:0.03 \
  --priv:<priv-key> --out:build/cheng_storage/lease-token.json

# 写入时校验租约 token
./cheng_storage put --file:README.md --lease:build/cheng_storage/lease-token.json \
  --root:build/cheng_storage --mode:local

# 记录计算计量并写入账本
./cheng_storage meter --task:job-1 --package:pkg://cheng/fs --author:node:alice --executor:node:exec-1 \
  --cpu_ms:1200 --mem_bytes:104857600 --io_bytes:10485760 --price_cpu:0.000001 \
  --price_mem:0.10 --price_io:0.05 --royalty:0.12 --treasury:0.03 --epoch:1 \
  --root:build/cheng_storage --mode:local

# 记录计算请求（执行前）
./cheng_storage exec --task:job-1 --package:pkg://cheng/fs --author:node:alice --requester:node:app-1 \
  --input:cid://input --code:cid://artifact --args:cid://args \
  --cpu_ms:2000 --mem_bytes:268435456 --io_bytes:33554432 \
  --price_cpu:0.000001 --price_mem:0.10 --price_io:0.05 --epoch:1 \
  --root:build/cheng_storage --mode:local

# GPU 训练/推理任务（示例：执行请求 + 计量）
./cheng_storage exec --task:job-gpu-1 --package:pkg://cheng/fs --author:node:alice --requester:node:app-1 \
  --input:cid://input --code:cid://artifact --args:cid://args \
  --gpu_ms:180000 --gpu_mem_bytes:17179869184 --gpu_count:1 --gpu_type:A10G --workload:train \
  --price_gpu:0.00002 --price_gpu_mem:0.15 --epoch:1 \
  --root:build/cheng_storage --mode:local

./cheng_storage meter --task:job-gpu-1 --package:pkg://cheng/fs --author:node:alice --executor:node:exec-1 \
  --gpu_ms:175000 --gpu_mem_bytes:16106127360 --gpu_count:1 --gpu_type:A10G --workload:train \
  --price_gpu:0.00002 --price_gpu_mem:0.15 --royalty:0.12 --treasury:0.03 --epoch:1 \
  --root:build/cheng_storage --mode:local

# GPU 最小 E2E 流程（exec -> meter -> receipt -> settle）
# 1) 执行请求，得到 req-xxx
./cheng_storage exec --task:job-gpu-2 --package:pkg://cheng/fs --author:node:alice --requester:node:app-1 \
  --gpu_ms:120000 --gpu_mem_bytes:12884901888 --gpu_count:1 --gpu_type:A10G --workload:infer \
  --price_gpu:0.00002 --price_gpu_mem:0.15 --epoch:1 \
  --root:build/cheng_storage --mode:local

# 2) 上报计量，得到 usage-xxx
./cheng_storage meter --task:job-gpu-2 --package:pkg://cheng/fs --author:node:alice --executor:node:exec-1 \
  --gpu_ms:110000 --gpu_mem_bytes:11811160064 --gpu_count:1 --gpu_type:A10G --workload:infer \
  --price_gpu:0.00002 --price_gpu_mem:0.15 --royalty:0.12 --treasury:0.03 --epoch:1 \
  --root:build/cheng_storage --mode:local

# 3) 回执绑定 request 与 usage
./cheng_storage receipt --request:req-xxx --task:job-gpu-2 --executor:node:exec-1 --status:ok \
  --result:cid://result --usage:usage-xxx --epoch:1 \
  --root:build/cheng_storage --mode:local

# 4) 按 epoch 结算
./cheng_storage settle --epoch:1 --root:build/cheng_storage --format:json --top:10

# 记录执行回执（执行后）
./cheng_storage receipt --request:req-xxx --task:job-1 --executor:node:exec-1 --status:ok \
  --result:cid://result --usage:usage-xxx --epoch:1 \
  --root:build/cheng_storage --mode:local

# 计量 SDK（应用侧手工打点 / 模板封装）
# import cheng/decentralized/metering_sdk as msdk
# var cfg = msdk.MeteringConfig(ledgerPath: "build/cheng_storage/ledger.jsonl", mode: druntime.smLocal,
#     packageId: "pkg://cheng/fs", authorId: "node:alice", executorId: "node:exec-1",
#     priceCpuMs: 0.000001, priceMemGb: 0.10, priceIoGb: 0.05,
#     priceGpuMs: 0.00002, priceGpuMemGb: 0.15, gpuType: "A10G", workloadKind: "train",
#     royaltyRate: 0.12, treasuryRate: 0.03, epoch: 1)
# var st = msdk.initMetering(cfg, "job-1")
# msdk.meterAddCpu(&st, 1200)
# msdk.meterAddMem(&st, 104857600)
# msdk.meterAddIo(&st, 10485760)
# msdk.meterAddGpu(&st, 175000)
# msdk.meterAddGpuMem(&st, 16106127360)
# msdk.meterSetGpuCount(&st, 1)
# msdk.installIoMeteringHook(&st, msdk.iomWriteOnly)
# writeFile("data.txt", "payload")
# msdk.removeIoMeteringHook()
# msdk.flushMetering(cfg, st)
#
# msdk.withMetering(cfg, "job-1"):
#     msdk.meterAddCpu(&__cheng_meter_state, 1200)
#     msdk.meterAddMem(&__cheng_meter_state, 104857600)
#     msdk.meterAddIo(&__cheng_meter_state, 10485760)
#     msdk.withIoMetering(&__cheng_meter_state, msdk.iomWriteOnly):
#         # 这里执行 IO，写入字节将自动计量
#         writeFile("data.txt", "payload")
#
# # 一行封装（编译期展开）
# msdk.withWriteMetering(cfg, "job-1"):
#     writeFile("data.txt", "payload")

# IO 计量钩子当前覆盖 std/os 与 decentralized/file_bytes（本地块存储/registry），
# 网络流量与 libp2p 流量暂不计入，后续在 P3/P4 扩展。

# 记录审计/抽查结果
./cheng_storage audit --task:job-1 --executor:node:exec-1 --auditor:node:auditor-1 --status:ok \
  --result:cid://... --epoch:1 --note:sample-checked --root:build/cheng_storage

# 抽样生成审计清单（可选写入 ledger）
./cheng_storage sample --epoch:1 --base-rate:0.10 --high-risk-rate:1.0 --risk-window-epochs:3 --seed:round-1 --auditor:node:auditor-1 \
  --ledger:build/cheng_storage/ledger.jsonl --format:json --out:build/cheng_storage/audit-sample.json --record

# 上报欺诈/作恶线索
./cheng_storage fraud --task:job-1 --executor:node:exec-1 --reporter:node:auditor-1 \
  --receipt:receipt-xxx --reason:bad-output --severity:high --epoch:1 --root:build/cheng_storage

# 速率限制检查（基于 ledger 统计）
./cheng_storage ratelimit --epoch:1 --requester:node:app-1 --max-requests:100 \
  --ledger:build/cheng_storage/ledger.jsonl --format:yaml

# 执行节点信誉汇总
./cheng_storage repute --executor:node:exec-1 --ledger:build/cheng_storage/ledger.jsonl --format:json

# 存储提供者信誉汇总
./cheng_storage store-repute --provider:node:store-1 --ledger:build/cheng_storage/ledger.jsonl --format:json

# 存储证明（读取块并记录 proof_hash）
./cheng_storage proof --cid:cid://<cid> --provider:node:store-1 --epoch:1 --root:build/cheng_storage --mode:local

# 按 epoch 结算输出（JSON/TOML/YAML），可选 top 预览
./cheng_storage settle --epoch:1 --root:build/cheng_storage --out:build/cheng_storage/settlement-epoch-1.json --format:json --top:10
./cheng_storage settle --epoch:1 --root:build/cheng_storage --out:build/cheng_storage/settlement-epoch-1.toml --format:toml --top:10
./cheng_storage settle --epoch:1 --root:build/cheng_storage --out:build/cheng_storage/settlement-epoch-1.yaml --format:yaml --top:10
./cheng_storage settle --epoch:1 --root:build/cheng_storage --format:json --top:10 \
  --reconcile-csv:build/cheng_storage/reconcile-epoch-1.csv

# 导出 RWAD 接口批次（由 RWAD-blockchain 执行积分/RWAD结算）
sh src/tooling/tooling_exec.sh cheng_rwad_bridge export --epoch:1 --root:build/cheng_storage \
  --batch-id:cheng-epoch-1 --out:build/cheng_storage/rwad-batch-epoch-1.json

# 应用 RWAD-blockchain 返回结果（状态与批次一致性校验）
sh src/tooling/tooling_exec.sh cheng_rwad_bridge apply --result:build/cheng_storage/rwad-result-epoch-1.json \
  --batch:build/cheng_storage/rwad-batch-epoch-1.json \
  --batch-id:cheng-epoch-1 --require-status:finalized --out:build/cheng_storage/rwad-ack-epoch-1.json

# 接口契约验收（不依赖 cheng_storage 写路径）
sh src/tooling/tooling_exec.sh verify_rwad_interface_contract --rwad-root:/Users/lbcheng/.cheng-packages/RWAD-blockchain

# 结算输出包含 counts/audit_total/penalties：处罚金额会计入 treasury_total，并从 executors 扣减。
# 结算输出包含 preview（top 列表，limit 可用 --top 控制；设为 0 可关闭）。
# 结算输出包含 reconcile（exec_request/exec_receipt/usage 对账清单、非 ok 回执、epoch 不一致），同样受 --top 限制。
# reconcile 可导出 CSV（列：kind, receipt_id, request_id, usage_id, ref_id, receipt_epoch, ref_epoch, status, error）。
# 结算输出包含 trust 汇总（audit_sample/fraud_report/storage_proof 计数与分布）。

结算输出示例（TOML，截断）：
```toml
epoch = 1
storage_total = 0.25
treasury_total = 0.03

[payouts.authors]
"node:alice" = 0.03

[payouts.providers]
"node:store-1" = 0.19
```

# 注册中心：生成密钥
./cheng_registry keygen --out:build/cheng_registry/keypair.json

# 注册中心：发布包快照到通道
./cheng_registry publish --package:pkg://cheng/fs --author:node:alice --channel:stable --epoch:1 \
  --file:README.md --priv:<priv-key> --registry:build/cheng_registry/registry.jsonl

# 注册中心：解析通道到快照
./cheng_registry resolve --package:pkg://cheng/fs --channel:stable --registry:build/cheng_registry/registry.jsonl

# 注册中心：校验签名
./cheng_registry verify --package:pkg://cheng/fs --channel:stable --registry:build/cheng_registry/registry.jsonl

# 包管理器：manifest -> lock（默认输出 TOML）
./cheng_pkg resolve --manifest:docs/cheng-package-manifest.toml --registry:build/cheng_registry/registry.jsonl \
  --out:build/cheng_pkg/cheng.lock.toml

# 包管理器：校验 lock（可选对照 registry）
./cheng_pkg verify --lock:build/cheng_pkg/cheng.lock.toml --registry:build/cheng_registry/registry.jsonl
# 校验时会报告单版本冲突（同包出现多个快照）。

# 如需 JSON 输出（兼容旧流程）
./cheng_pkg resolve --manifest:docs/cheng-package-manifest.toml --registry:build/cheng_registry/registry.jsonl \
  --out:build/cheng_pkg/cheng.lock.json --format:json

# 如需 YAML 输出
./cheng_pkg resolve --manifest:docs/cheng-package-manifest.yaml --registry:build/cheng_registry/registry.jsonl \
  --out:build/cheng_pkg/cheng.lock.yaml --format:yaml

# 产物元数据：结合 lock + registry 快照（用于编译产物旁的元数据）
./cheng_pkg meta --lock:build/cheng_pkg/cheng.lock.toml --package:pkg://cheng/app-demo --channel:stable \
  --registry:build/cheng_registry/registry.jsonl --out:build/cheng_pkg/cheng.meta.toml
```

manifest 示例（`docs/cheng-package-manifest.toml`）：
```toml
package_id = "pkg://cheng/app-demo"

[[dependencies]]
package_id = "pkg://cheng/fs"
channel = "stable"

[[dependencies]]
package_id = "pkg://cheng/net"
channel = "edge"
```

manifest 示例（`docs/cheng-package-manifest.yaml`）：
```yaml
package_id: "pkg://cheng/app-demo"
dependencies:
  - package_id: "pkg://cheng/fs"
    channel: "stable"
  - package_id: "pkg://cheng/net"
    channel: "edge"
```

## 关键指标（用于评估成败）
- 生态：活跃包数量、依赖图收敛度、LTS 覆盖率。
- 体验：远程执行延迟、失败率、存储可用性。
- 经济：开发者收益稳定性、节点收益/成本比。
- 安全：回执伪造率、异常租约比例、存储证明通过率。

## 未决问题
- 计算回执的最小可信证明形式是什么？
- 计费模型如何兼顾公平与性能（CPU/内存/IO/GPU 权重）？
- 是否允许完全离线运行的租约？
- 结算与支付如何合规化？

## 结论
该方案更适合作为 **Cheng 生态专用的去中心化计算与存储网络**，而非通用公链。
先存储、后计算、再去中心化结算，是风险最小且落地最快的路线。
