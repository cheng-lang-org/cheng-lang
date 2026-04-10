# v3/chain

这里先把链路的数据面改成二进制。

当前已落地：

- 固定布局 `LsmrAddress`
- 二进制 `FrameHeader`
- `HELLO / ADVERTISE / WANT_STATE` 三类 payload 的 encode/decode

硬规则：

- header 和 payload 都不再是文本
- `CID / peer id / state root` 全部走定长字节
