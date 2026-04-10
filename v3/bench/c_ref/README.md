# v3 C Baseline

这里是 `v3` 的同机 C 绝对值基线，不借 `stage0`，也不借旧文本协议。

覆盖项：

- `sha256`
- `x25519 shared key`
- `p256 public key`
- `p256 sign`
- `p256 verify`
- `chain binary hello frame encode/decode`

运行：

```sh
make -C v3/bench/c_ref run
```

输出口径：

- `iters`：循环次数
- `total_ns`：总纳秒
- `ns_per_op`：单次纳秒

这份基线后面要直接拿来和 `stage2/stage3` 的 Cheng bench 同口径对拍。
