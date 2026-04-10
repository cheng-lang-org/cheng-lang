# v3/tooling

这里收自举、bench、验收命令。

当前先把基线钉死：

- `v3/bench/c_ref` 是同机 C 绝对值基线
- `v3/bench/c_ref/baseline_arm64_apple_darwin.txt` 是当前冻结样本
- `v3/src/tests/*` 是 Cheng 侧固定布局和二进制帧 smoke
- `v3/src/chain/*` 现在已经有 `lsmr / anti_entropy / csg / pubsub / location_proof / consensus` 最小固定布局语义核
- `v3/src/tooling/*` 把 `bootstrap contract / build plan / perf gate / hotpath scan` 收成真类型和真代码
- `v3/tooling/scan_forbidden_hotpath.sh` 会直接扫 `v3/src` 里被禁的字符串壳和 `BigInt`
- `v3/tooling/cheng_v3.sh` 是 `v3` 自己的主控入口
- `v3/tooling/bootstrap_bridge_v3.sh` 负责把 `v3/bootstrap/cheng_v3_seed.c + stage1_bootstrap.cheng` 物化成 `artifacts/v3_bootstrap/cheng.stage0~3 + bootstrap.env`
- `v3/tooling/build_backend_driver_v3.sh` 直接用 `artifacts/v3_bootstrap/cheng.stage2` 产出 `artifacts/v3_backend_driver/cheng`
- `v3/tooling/build_program_selfhost_v3.sh` 会强制拿 `artifacts/v3_backend_driver/cheng` 去真编 `v3/src/tests/program_selfhost_smoke.cheng`
- `v3/tooling/build_chain_node_v3.sh` 会强制拿 `artifacts/v3_backend_driver/cheng` 去真编 `v3/src/project/chain_node_main.cheng`
- `v3/tooling/build_zero_exit_v3.sh` 会强制拿 `artifacts/v3_backend_driver/cheng` 去真编 `v3/src/tests/ordinary_zero_exit_fixture.cheng`，这是当前最小 ordinary compile 真链接验证，同时要求旁边真的生成 `.v3.map`
- `v3/tooling/build_panic_trace_v3.sh` 会强制拿 `artifacts/v3_backend_driver/cheng` 去真编 `v3/src/tests/ordinary_panic_fixture.cheng`，并要求运行 panic 后 stderr 真打印 `source_path + 行号`
- `v3/tooling/build_call_chain_v3.sh` 会强制拿 `artifacts/v3_backend_driver/cheng` 去真编 `v3/src/tests/ordinary_call_chain_fixture.cheng`，这是当前 no-arg 尾调用真链接验证
- `v3/tooling/run_v3_host_smokes.sh` 会用当前 host compiler 真编真跑 `fixed_surface/program_selfhost/csg/consensus/chain_node/pubsub/location_proof/chain_codec/anti_entropy/lsmr*` smoke；可用 `CHENG_V3_SMOKE_COMPILER=<path>` 切编译器入口
- `v3/tooling/run_slice_gate.sh` 现在会顺序跑 `scan -> c_ref -> bootstrap-bridge -> build-backend-driver -> host smokes -> zero-exit -> panic-trace -> call-chain -> program-selfhost -> chain_node -> bootstrap self-checks`
- `v3/tooling/compare_bench.sh` 用来把后续 `stage2/stage3` bench 和这份 C 基线同口径对拍
- `v3/tooling/cheng_v3.sh` 的外层日志固定写到 `artifacts/v3_tooling/cheng_v3_gate.seed.stderr.log`

当前 `v3` 的默认 bootstrap 入口已经切到 `v3` 自己目录下：

- `bootstrap_bridge_v3.sh` 会先产出 `artifacts/v3_bootstrap/cheng.stage0`
- 然后固定重编成 `artifacts/v3_bootstrap/cheng.stage1 -> cheng.stage2 -> cheng.stage3`
- 最后把这条链写成 `artifacts/v3_bootstrap/bootstrap.env`
- `build_backend_driver_v3.sh`、`run_v3_host_smokes.sh` 和 `run_slice_gate.sh` 都围着这条主线验收
- 现在已经把 `ordinary program selfhost smoke` 和 `chain_node smoke` 接进了 host smoke，把 `program-selfhost + chain_node` 接进了正式 gate
- `bootstrap_bridge_v3.sh` 现在还会把 `V3_COMPILER_ENTRY_SOURCE / V3_COMPILER_RUNTIME_SOURCE / V3_COMPILER_REQUEST_SOURCE` 写进 `artifacts/v3_bootstrap/bootstrap.env`
- `build_backend_driver_v3.report.txt` 现在会同时写 `planned_entry_source=*compiler_main.cheng` 和 `materialized_source=*stage1_bootstrap.cheng`，显式暴露“计划入口”和“当前实际输入”还没对齐
- 当前最小 ordinary compile 已经真接通：`build_zero_exit_v3.sh` 和 `build_call_chain_v3.sh` 都能真发 `primary .o`、真编 provider `.o`、真链接并真跑返回码为 `0`
- ordinary entry bridge 的硬规则已经收死：尾调用必须直接发 `b callee`，不能再用 `bl callee; ret`，否则 `LR` 会被覆盖，入口桥会直接自旋
- 当前真实阻塞也已经收正：`build_program_selfhost_v3.sh` 和 `build_chain_node_v3.sh` 现在统一报 `v3 compiler: primary object body semantics missing`，说明 object/link 已接通，剩下的是函数体语义子集而不是 bootstrap/argv/contract/linker 假问题。这轮把 ordinary lowering 改成按 entry 可达函数裁剪后，`program_selfhost` 已从 `598` 个函数压到 `16` 个，`chain_node` 已从 `1120` 个压到 `112` 个。

后续硬规则：

- 所有性能和链路 gate 只认 `stage2/stage3`
- 不允许拿 `stage0` 结果冒充验收
