# v3/tooling

这里收自举、bench、冷热双轨验收命令。

当前先把基线钉死：

- `v3/bench/c_ref` 是同机 C 绝对值基线
- `v3/bench/c_ref/baseline_arm64_apple_darwin.txt` 是当前冻结样本
- `v3/src/tests/*` 是 Cheng 侧固定布局和二进制帧 smoke
- `v3/src/overlay/*` 是 `LSMR overlay` 的边界类型和策略接口
- `v3/src/chain/*` 现在已经有 `lsmr / anti_entropy / csg / pubsub / location_proof / consensus` 最小固定布局语义核
- `v3/src/project/bft_state_machine.cheng` 是当前 `L1(BFT-SMI)` 的唯一 Cheng 侧结算边界
- `v3/src/tooling/*` 把 `bootstrap contract / build plan / perf gate / hotpath scan` 收成真类型和真代码
- `v3/tooling/scan_forbidden_hotpath.sh` 会直接扫 `v3/src` 里被禁的字符串壳和 `BigInt`
- `v3/tooling/cheng_v3.sh` 是 `v3` 自己的主控入口
- `v3/tooling/verify_orphan_guard_v3.sh` 会扫描 `v3/tooling` 顶层所有入口文件（`.sh`、`.py`、无后缀），硬禁把 `guarded_exec_v3.py`、`orphan_guard_run.sh` 或 `python3` 再塞回脚本主线
- `v3/tooling/cheng_v3.sh debug-report/print-symbols/print-line-map/print-elf` 现在直接把 seed 内部调试面公开出来：可直接看 ordinary pipeline 报告、函数符号、源码行映射和 Linux AArch64 ELF relocatable object，不再先依赖 `lldb/nm/objdump`
- `v3/tooling/bootstrap_bridge_v3.sh` 负责把 `v3/bootstrap/cheng_v3_seed.c + stage1_bootstrap.cheng` 物化成 `artifacts/v3_bootstrap/cheng.stage0~3 + bootstrap.env`
- `v3/tooling/build_backend_driver_v3.sh` 直接用 `artifacts/v3_bootstrap/cheng.stage2` 产出 `artifacts/v3_backend_driver/cheng`
- `v3/tooling/build_program_selfhost_v3.sh` 会强制拿 `artifacts/v3_backend_driver/cheng` 去真编 `v3/src/tests/program_selfhost_smoke.cheng`
- `v3/tooling/build_chain_node_v3.sh` 会强制拿 `artifacts/v3_backend_driver/cheng` 去真编 `v3/src/project/chain_node_main.cheng`，并运行 `self-test`
- `v3/tooling/build_chain_node_linux_v3.sh` 现在是统一 Linux 入口：默认直接产 `aarch64-unknown-linux-gnu` 的 `ELF relocatable object`；如果显式给 `CHAIN_NODE_LINUX_ARTIFACT=exe`，会直接走 `v3 seed` 内建的 `internal_elf_linker` 真产 Linux AArch64 `nolibc exe`
- `v3/tooling/build_rwad_bft_state_machine_linux_v3.sh` 现在和 `chain_node` 同口径，默认直接产 `aarch64-unknown-linux-gnu` 的 `ELF relocatable object`；`RWAD_BFT_LINUX_ARTIFACT=exe` 会直接产 Linux AArch64 `nolibc exe`
- `v3/tooling/build_linux_nolibc_exe_v3.sh` 是 generic Linux `aarch64` `nolibc exe` 的统一构建入口，当前已经能真编真链，不再停在 preflight 骨架
- `v3/runtime/native/v3_linux_nolibc_aarch64_entry.S` 是当前 Linux `aarch64` 的最小 `_start`，负责把 `argc/argv` 转给 `cheng_v3_program_argv_entry`，随后走 Linux `exit` syscall
- `v3/tooling/run_v3_linux_object_smokes.sh` 会真编 `chain_node/rwad_bft_state_machine` 两个 generic Linux `aarch64` `.o`，并用 `file + llvm-objdump -f` 验证它们真是 `elf64-littleaarch64`
- `v3/tooling/run_v3_chain_node_process_smoke.sh` 是当前最小两进程 `chain_node` snapshot sync 验收
- `v3/tooling/run_v3_chain_node_three_node_smoke.sh` 把 `server -> relay -> client` 三节点 snapshot sync 收成真实进程 gate
- `v3/tooling/build_zero_exit_v3.sh` 会强制拿 `artifacts/v3_backend_driver/cheng` 去真编 `v3/src/tests/ordinary_zero_exit_fixture.cheng`，这是当前最小 ordinary compile 真链接验证，同时要求旁边真的生成 `.v3.map`
- `v3/tooling/build_panic_trace_v3.sh` 会强制拿 `artifacts/v3_backend_driver/cheng` 去真编 `v3/src/tests/ordinary_panic_fixture.cheng`，并要求运行 panic 后 stderr 真打印 `source_path + 行号`
- `v3/tooling/build_bounds_trace_v3.sh` 会强制拿 `artifacts/v3_backend_driver/cheng` 去真编 `v3/src/tests/ordinary_bounds_trace_fixture.cheng`，并要求运行越界后 stderr 默认同时打印 bounds 消息、源码栈和 native 栈
- `v3/tooling/build_call_chain_v3.sh` 会强制拿 `artifacts/v3_backend_driver/cheng` 去真编 `v3/src/tests/ordinary_call_chain_fixture.cheng`，这是当前 no-arg 尾调用真链接验证
- `v3/tooling/build_signal_trace_v3.sh` 会真编 `v3/src/tests/ordinary_signal_trace_fixture.cheng`，让程序停在 Cheng 循环里再注入 `SIGSEGV`，验证产物内嵌 source-map 能直接回到 `.cheng` 行号
- `v3/tooling/build_ffi_handle_v3.sh` 会真编 `v3/src/tests/ffi_handle_smoke.cheng` 和 `v3/src/tests/ffi_handle_stale_trap_smoke.cheng`，前者验证 generational handle 正向路径，后者强制验证 stale handle 默认直接带栈 crash
- `v3/tooling/run_v3_stage23_libp2p_smokes.sh` 会直接拿 `artifacts/v3_bootstrap/cheng.stage2` 和 `cheng.stage3` 真编真跑当前已经闭合的 `compiler/tooling + BFT-SMI + QUIC/TLS + libp2p + overlay/pubsub/dag/plumtree/erasure/content/Pin/chain_node` 主线；现在还额外挂一条 `fixed256_curve25519_smoke` 当 `TLS13 X25519` 的定点底座前哨。尾段还会真跑 `chain_node` 两进程与三进程同步 gate。这里保留的是 `WebRTC signal/session/sync` 和通用内容协议模型，不带宿主专属的原生 datachannel 内容桥 smoke。
- `v3/tooling/run_v3_host_smokes.sh` 会用当前 host compiler 真编真跑 `ref10_ashr/fixed256_curve25519/fixedbytes32_seq_index/program_selfhost/bft/overlay/csg/consensus/chain_node/pubsub/location_proof/chain_codec/anti_entropy/lsmr*`、`bft_three_replica_smoke`、`content/pin runtime`、宿主专属 `WebRTC` 原生 datachannel 内容桥 smoke 和 `udp_importc_smoke`；也支持 `sh run_v3_host_smokes.sh smoke_a smoke_b` 这种定向 smoke；可用 `CHENG_V3_SMOKE_COMPILER=<path>` 切编译器入口
- `v3/tooling/run_slice_gate.sh` 现在会顺序跑 `scan -> c_ref -> bootstrap-bridge -> build-backend-driver -> host fixed256_sha256 -> host default_init_literals -> stage2/stage3 libp2p smokes -> host smokes -> zero-exit -> panic-trace -> bounds-trace -> signal-trace -> call-chain -> ffi-handle -> program-selfhost -> chain_node -> bootstrap self-checks`
- `v3/tooling/run_slice_gate.sh` 现在还会先跑 `verify-orphan-guard`，确保 tooling 脚本主线不再回流到 Python 守护壳
- `v3/tooling/verify_debug_tools_v3.sh` 会固定用 `return_add` 夹具前台验 `debug-report / print-symbols / print-line-map / print-elf`
- `v3/tooling/compare_bench.sh` 用来把后续 `stage2/stage3` bench 和这份 C 基线同口径对拍
- `v3/tooling/cheng_v3.sh` 的外层日志固定写到 `artifacts/v3_tooling/cheng_v3_gate.seed.stderr.log`

当前 `v3` 的默认 bootstrap 入口已经切到 `v3` 自己目录下：

- `bootstrap_bridge_v3.sh` 会先产出 `artifacts/v3_bootstrap/cheng.stage0`
- 然后固定重编成 `artifacts/v3_bootstrap/cheng.stage1 -> cheng.stage2 -> cheng.stage3`
- 最后把这条链写成 `artifacts/v3_bootstrap/bootstrap.env`
- `build_backend_driver_v3.sh`、`run_v3_stage23_libp2p_smokes.sh`、`run_v3_host_smokes.sh` 和 `run_slice_gate.sh` 都围着这条主线验收
- 现在已经把 `ordinary program selfhost smoke` 和 `chain_node smoke` 接进了 host smoke，把 `program-selfhost + chain_node` 接进了正式 gate
- `bootstrap_bridge_v3.sh` 现在还会把 `V3_COMPILER_ENTRY_SOURCE / V3_COMPILER_RUNTIME_SOURCE / V3_COMPILER_REQUEST_SOURCE` 写进 `artifacts/v3_bootstrap/bootstrap.env`
- `build_backend_driver_v3.report.txt` 现在会同时写 `planned_entry_source=*compiler_main.cheng` 和 `materialized_source=*stage1_bootstrap.cheng`，显式暴露“计划入口”和“当前实际输入”还没对齐
- 当前最小 ordinary compile 已经真接通：`build_zero_exit_v3.sh`、`build_panic_trace_v3.sh`、`build_bounds_trace_v3.sh`、`build_signal_trace_v3.sh` 和 `build_call_chain_v3.sh` 都会真发 `primary .o`、真编 provider `.o`、真链接；其中 `panic/bounds/signal` 三条链路都会校验源码栈回溯
- 当前 generic Linux `aarch64` 已经真收口：`run_v3_linux_object_smokes.sh` 会验 `ELF relocatable object`，`CHAIN_NODE_LINUX_ARTIFACT=exe v3/tooling/build_chain_node_linux_v3.sh` 和 `RWAD_BFT_LINUX_ARTIFACT=exe v3/tooling/build_rwad_bft_state_machine_linux_v3.sh` 都会直接产出 Linux AArch64 ELF 可执行文件；`x86_64-unknown-linux-gnu` 仍然保持硬失败
- ordinary entry bridge 的硬规则已经收死：尾调用必须直接发 `b callee`，不能再用 `bl callee; ret`，否则 `LR` 会被覆盖，入口桥会直接自旋
- 当前真实阻塞也已经收正：`build_program_selfhost_v3.sh` 和 `build_chain_node_v3.sh` 现在统一报 `v3 compiler: primary object body semantics missing`，说明 object/link 已接通，剩下的是函数体语义子集而不是 bootstrap/argv/contract/linker 假问题。这轮把 ordinary lowering 改成按 entry 可达函数裁剪后，`program_selfhost` 已从 `598` 个函数压到 `16` 个，`chain_node` 已从 `1120` 个压到 `112` 个。

后续硬规则：

- 所有性能和链路 gate 只认 `stage2/stage3`
- 不允许拿 `stage0` 结果冒充验收
- `L1(BFT-SMI)` 和 `L0(LSMR overlay)` 必须分别有独立 smoke，不能再把反熵窗口和结算 finality 混成一条 gate
- `chain_node` 三节点传播测试和 `BFT-SMI` 三副本一致性测试也必须分开，前者证明 overlay/store-sync，后者证明结算 deterministic
