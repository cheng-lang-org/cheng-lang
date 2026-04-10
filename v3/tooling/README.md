# v3/tooling

这里收自举、bench、验收命令。

当前先把基线钉死：

- `v3/bench/c_ref` 是同机 C 绝对值基线
- `v3/bench/c_ref/baseline_arm64_apple_darwin.txt` 是当前冻结样本
- `v3/src/tests/*` 是 Cheng 侧固定布局和二进制帧 smoke
- `v3/src/tooling/*` 把 `bootstrap contract / build plan / perf gate / hotpath scan` 收成真类型和真代码
- `v3/tooling/scan_forbidden_hotpath.sh` 会直接扫 `v3/src` 里被禁的字符串壳和 `BigInt`
- `v3/tooling/cheng_v3.sh` 是 `v3` 自己的主控入口
- `v3/tooling/bootstrap_bridge_v3.sh` 负责把 `v3/bootstrap/cheng_v3_seed.c + stage1_bootstrap.cheng` 物化成 `artifacts/v3_bootstrap/cheng.stage0~3 + bootstrap.env`
- `v3/tooling/build_backend_driver_v3.sh` 直接用 `artifacts/v3_bootstrap/cheng.stage2` 产出 `artifacts/v3_backend_driver/cheng`
- `v3/tooling/run_slice_gate.sh` 会顺序跑 `scan -> c_ref -> bootstrap-bridge -> build-backend-driver -> bootstrap self-checks`
- `v3/tooling/compare_bench.sh` 用来把后续 `stage2/stage3` bench 和这份 C 基线同口径对拍
- `v3/tooling/cheng_v3.sh` 的外层日志固定写到 `artifacts/v3_tooling/cheng_v3_gate.seed.stderr.log`

当前 `v3` 的默认 bootstrap 入口已经切到 `v3` 自己目录下：

- `bootstrap_bridge_v3.sh` 会先产出 `artifacts/v3_bootstrap/cheng.stage0`
- 然后固定重编成 `artifacts/v3_bootstrap/cheng.stage1 -> cheng.stage2 -> cheng.stage3`
- 最后把这条链写成 `artifacts/v3_bootstrap/bootstrap.env`
- `build_backend_driver_v3.sh` 和 `run_slice_gate.sh` 都只从这个 `bootstrap.env` 取 `stage2/stage3`

后续硬规则：

- 所有性能和链路 gate 只认 `stage2/stage3`
- 不允许拿 `stage0` 结果冒充验收
