# Cheng v3 DiLoCo

`v3/DiLoCo` 是 `cheng/DiLoCo` 独立子包。

当前口径只做三件事：

- 公网联邦 DiLoCo 协议核，不是数值训练框架。
- 第一版只支持 fixed cohort，不支持动态扩缩容和中途热加入。
- merge 结果当前只是 artifact-level commit，不做纯 Cheng 数值平均。

## 模块

- `cheng/DiLoCo/core/diloco`
  负责类型、状态机、artifact 合同和 lineage。
- `cheng/DiLoCo/protocol/wire`
  负责 request/response wire 编解码。
- `cheng/DiLoCo/host/diloco_host`
  负责最小 host/QUIC adapter。

## 验收

- `diloco_state_smoke`
- `diloco_wire_smoke`
- `diloco_quic_twoproc_server_smoke`
- `diloco_quic_twoproc_client_smoke`
