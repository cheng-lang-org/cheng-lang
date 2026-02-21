# Cheng 包管理器与存储收益激励（草案）

## 目标
- 为包作者提供稳定、可预测的收益。
- 在不强制 VM 运行时的前提下避免版本碎片。
- 保持用户读写免费；只有持久化需要付费。
- 通过按 epoch 结算降低运行时开销。
- 支持未来从中心化账本迁移到去中心化账本。

## 角色
- 包作者：发布包并获得版税。
- 应用开发者：依赖包并持久化数据。
- 存储提供者：存储内容寻址数据并获得存储费用。
- 注册中心：将通道解析为不可变包快照。
- 账本：记录每个 epoch 的租约、证明与结算。

## 核心模型
- 包以内容哈希寻址的不可变快照存在。
- 依赖引用通道（edge/stable/lts），而非精确版本。
- 单版本规则：构建图中同一包只允许一个版本。
- 持久化数据必须携带存储租约 token。
- 版税来自存储费用分成，并受策略上限约束。

## 版本图与 lock（补充）
- 每个通道只有一个最新快照指针（head），作为全网统一入口。
- 历史版本通过 CID 访问，快照元数据可包含 parent 形成版本 DAG。
- lock 固定 CID 与签名，确保可复现与可审计。

示例（lock 片段）：
```toml
package_id = "pkg://cheng/app-demo"
created_ts = "1730000000"

[[dependencies]]
package_id = "pkg://cheng/fs"
channel = "stable"
cid = "bafy..."
author_id = "node:alice"
pub_key = "ed25519:..."
signature = "ed25519:..."
epoch = 3
format = "tar"
parent = "bafy..."           # 建议扩展
head = "bafy..."             # 建议扩展
source_mode = "p2p"          # 建议扩展
source_addrs = ["/ip4/1.2.3.4/tcp/4001"]
```

## 包清单（概念）
```json
{
  "package_id": "pkg://cheng/fs",
  "author_id": "node:alice",
  "channel": "stable",
  "epoch": 3,
  "content_hash": "b3:...",
  "royalty_policy": {
    "royalty_rate": 0.12,
    "royalty_cap": 0.20
  },
  "storage_policy": {
    "min_duration_days": 30,
    "min_replicas": 2
  }
}
```

## 存储租约 Token
```json
{
  "lease_id": "lease-001",
  "package_id": "pkg://cheng/fs",
  "author_id": "node:alice",
  "storage_provider_id": "node:store-1",
  "bytes": 1073741824,
  "duration_days": 30,
  "replicas": 2,
  "price_per_gb_month": 0.25,
  "royalty_rate": 0.12,
  "treasury_rate": 0.03,
  "start_ts": "2025-03-01T00:00:00Z",
  "sig": "ed25519:..."
}
```

存储节点只接受包含有效签名租约 token 的写入。
过期租约会被垃圾回收。

## 定价与分成
定义：
- gb_months = (bytes / 1024^3) * (duration_days / 30) * replicas
- storage_cost = gb_months * price_per_gb_month
- royalty = storage_cost * royalty_rate
- treasury = storage_cost * treasury_rate
- provider = storage_cost - royalty - treasury

策略：
- royalty_rate 有上限（例如 20%）。
- treasury_rate 可选，可为 0。
- provider 份额必须为正，否则租约无效。

## 版本碎片控制
- 依赖必须引用通道，而非具体版本。
- 依赖图内同一包仅允许一个版本。
- 注册中心通道映射到不可变快照。
- 发布可生成 lock 文件以固定快照哈希。

## lock_version 与兼容策略（补充）
- 默认写出 v1（无扩展字段），确保旧工具链可读。
- 需要扩展字段时写出 `lock_version = 2` 并要求 v2 解析器。
- TOML/YAML 解析严格，扩展字段需同步升级；JSON 可作为过渡格式。

## 远程导入与安全策略（补充）
- 远程仅负责传输内容，依赖解析以 lock 中的 CID 为准。
- 拉取前校验作者签名与包身份，确保 CID 未被篡改。
- 允许配置 `PKG_MODE=local|p2p|auto` 与 `PKG_PEERS`，并支持白名单/黑名单策略。
- 建议在企业/生产环境禁用 HTTP 回退或强制签名校验失败即拒绝。

远程配置示例（补充）：
```toml
[remote]
mode = "p2p"
peers = ["/ip4/1.2.3.4/tcp/4001"]
allow = ["/dns4/node.example/tcp/4001"]
deny = ["/ip4/5.6.7.8/tcp/4001"]
http_fallback = false
require_signature = true
require_registry_match = true
```

环境变量优先级（补充）：
1) CLI 参数
2) `PKG_*`
3) `cheng-pkg-config.toml`
4) 内置默认值

模板文件：
- `configs/cheng-pkg-config.toml`

## 生产闭环（摘要）
- 生产流程建议执行 `resolve -> verify -> meta -> fetch -> build`，并保存 `cheng.meta.toml` 供审计。
- 生产环境建议启用 L2 策略：禁用 HTTP 回退、白名单 peer、强制签名与 registry 校验。
- 详见去中心化计算与存储文档的生产闭环章节。

## 滥用控制
- 最小租约大小与时长，降低垃圾写入。
- 每个应用/节点的租约创建速率限制。
- 存储证明检查；失败节点失去后续费用资格。
- 版税上限，防止恶意定价。

## 激励闭环
1. 作者向注册中心发布包快照。
2. 应用依赖包通道并运行解析后的快照。
3. 数据持久化时，运行时获取租约 token。
4. 存储提供者只接受带租约的写入。
5. epoch 结束后账本结算提供者费用与作者版税。

## 参考实现
参考分润计算器同时提供 Python 与 Cheng 版本。
- Python: `src/tooling/cheng_pkg_rewards.py`
- Cheng: `src/tooling/cheng_pkg_rewards.cheng`（用 `src/tooling/chengc.sh` 编译）
示例：
```bash
python src/tooling/cheng_pkg_rewards.py \
  --input docs/cheng-package-incentives-example.json \
  --out build/cheng-package-incentives-payouts.json \
  --pretty
```

Cheng 示例：
```bash
src/tooling/chengc.sh src/tooling/cheng_pkg_rewards.cheng --name:cheng_pkg_rewards
./cheng_pkg_rewards --input docs/cheng-package-incentives-example.json --out build/cheng-package-incentives-payouts.json --pretty
```
