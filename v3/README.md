# v3

`v3` 不是给 `v2` 补壳，它是从第一天就按“自举后仍保住 C 级性能”来收口的新根。

文档入口：

- `v3/docs/README.md`
- `v3/docs/v2已踩性能和自举坑.md`
- `v3/docs/自举和性能开发计划.md`

当前这批已落地的最小闭环：

- `v3/src/std/bytes_layout.cheng`：固定布局 `ByteSpan / ByteBuf / FixedBytes[N]`
- `v3/src/lang/intern.cheng`：冷路径字符串只进 interner，热路径统一走 interned id
- `v3/src/ir/core_types.cheng`：`HIR/MIR/LIR` 最小真数据面，显式带 `move / borrow / noalias / escape / layout / ABI`
- `v3/src/std/crypto/fixed256.cheng`：`curve25519 / P-256 / sha256` 的固定 256 位接口
- `v3/src/chain/binary_types.cheng` + `v3/src/chain/codec_binary.cheng`：二进制链帧，不再走文本 header/payload
- `v3/src/chain/{lsmr,anti_entropy,csg,pubsub,location_proof,consensus}.cheng`：`LSMR / 反熵 / CSG / PubSub / 空间证明 / 非 VM 共识` 最小固定布局语义核
- `v3/src/tooling/*.cheng`：`bootstrap contracts / build plan / hotpath scan / perf gate / gate main`
- `v3/src/tooling/{compiler_main,compiler_runtime,compiler_request}.cheng`：ordinary compiler control-plane 骨架，已经和 bootstrap manifest 分离
- `v3/src/lang/parser.cheng` + `v3/src/backend/system_link_exec.cheng`：ordinary compile 新主线的最小 stub，`compiler_main` 已经先接到这条链上
- `v3/src/tests/*`：固定布局和二进制链帧 smoke
- `v3/src/tests/program_selfhost_smoke.cheng`：ordinary program selfhost host smoke
- `v3/src/project/chain_node_main.cheng`：`chain_node` 当前真 artifact 入口源，当前先收成可 host-run 的 `self-test` 入口
- `v3/bench/c_ref/*`：同机 OpenSSL/C 基线，覆盖 `sha256 / x25519 / p256 pubkey/sign/verify / chain codec`
- `v3/tooling/*.sh`：`cheng_v3 / bootstrap_bridge_v3 / build_backend_driver_v3 / build_zero_exit_v3 / build_program_selfhost_v3 / build_chain_node_v3 / run_v3_chain_node_process_smoke / run_v3_chain_node_three_node_smoke / run_v3_stage23_libp2p_smokes / run_v3_host_smokes / run_slice_gate`
- `v3/tooling/build_chain_node_linux_v3.sh` 和 `v3/tooling/build_rwad_bft_state_machine_linux_v3.sh`：当前默认会产 generic Linux `aarch64` 的 `ELF relocatable object`；显式切到 `*_LINUX_ARTIFACT=exe` 时会改走严格 `nolibc exe` preflight，不再直接掉进假链接错误
- `v3/tooling/build_linux_nolibc_exe_v3.sh` + `v3/runtime/native/v3_linux_nolibc_aarch64_entry.S`：当前 generic Linux `aarch64` `nolibc exe` 的统一骨架，已经有最小 `_start` 和 preflight 报告机制
- `v3/tooling/run_v3_linux_object_smokes.sh`：当前 generic Linux `aarch64` 目标的正式物件格式验收
- `v3/tooling/{verify_windows_builtin_linker_v3,verify_riscv64_builtin_linker_v3,run_v3_windows_riscv_builtin_smokes}.sh`：把仓库主 backend 已有的 Windows `COFF/PE` 和 `riscv64 ELF` 内建 self-link gate 收成 `v3` 可见的 cross-target 验收入口

当前主命令：

```sh
v3/tooling/scan_forbidden_hotpath.sh

v3/tooling/bootstrap_bridge_v3.sh

v3/tooling/cheng_v3.sh print-bootstrap

v3/tooling/cheng_v3.sh print-build-plan

v3/tooling/cheng_v3.sh debug-report --in:/abs/path/file.cheng --target:aarch64-unknown-linux-gnu --emit:obj

v3/tooling/cheng_v3.sh print-symbols --in:/abs/path/file.cheng --target:aarch64-unknown-linux-gnu --emit:obj

v3/tooling/cheng_v3.sh print-line-map --in:/abs/path/file.cheng --target:aarch64-unknown-linux-gnu --emit:obj

v3/tooling/cheng_v3.sh print-elf --object:/abs/path/file.o

v3/tooling/cheng_v3.sh verify-debug-tools

v3/tooling/cheng_v3.sh slice-gate

artifacts/v3_bootstrap/cheng.stage0 self-check --in:v3/bootstrap/stage1_bootstrap.cheng

artifacts/v3_bootstrap/cheng.stage1 print-contract --in:v3/bootstrap/stage1_bootstrap.cheng

artifacts/v3_bootstrap/cheng.stage2 print-contract --in:v3/bootstrap/stage1_bootstrap.cheng

artifacts/v3_bootstrap/cheng.stage3 self-check --in:v3/bootstrap/stage1_bootstrap.cheng

make -C v3/bench/c_ref run

v3/tooling/build_backend_driver_v3.sh

v3/tooling/build_bft_state_machine_v3.sh

v3/tooling/build_rwad_bft_state_machine_v3.sh

v3/tooling/build_browser_host_wasm_v3.sh

CHENG_V3_LINUX_EXE_SOURCE=/abs/path/file.cheng CHENG_V3_LINUX_EXE_OUT=/abs/path/out \
v3/tooling/build_linux_nolibc_exe_v3.sh

v3/tooling/build_chain_node_linux_v3.sh

v3/tooling/build_rwad_bft_state_machine_linux_v3.sh

v3/tooling/run_v3_linux_object_smokes.sh

v3/tooling/verify_windows_builtin_linker_v3.sh

v3/tooling/verify_riscv64_builtin_linker_v3.sh

v3/tooling/run_v3_windows_riscv_builtin_smokes.sh

v3/tooling/cheng_v3.sh verify-windows-builtin

v3/tooling/cheng_v3.sh verify-riscv64-builtin

v3/tooling/cheng_v3.sh run-cross-target-smokes

v3/tooling/run_v3_chain_node_three_node_smoke.sh

v3/tooling/run_v3_stage23_libp2p_smokes.sh

v3/tooling/run_v3_host_smokes.sh

v3/tooling/cheng_v3.sh run-v2-selfhost-gate

v3/tooling/run_slice_gate.sh
```

其中 `v3/tooling/cheng_v3.sh` 是 `v3` 自己的 shell 主控 launcher；
`v3/tooling/bootstrap_bridge_v3.sh` 现在优先直接复用现成 `cheng.stage3` 或
`artifacts/v3_backend_driver/cheng` 做 `v3_selfhost` 自举，只在冷启动缺编译器时才回 C seed；
bootstrap 物化物仍会落成
`artifacts/v3_bootstrap/cheng.stage0 -> cheng.stage1 -> cheng.stage2 -> cheng.stage3`
以及 `bootstrap.env`；
`v3/tooling/build_backend_driver_v3.sh` 现在会先刷新 `bootstrap.env`，再直接把 fresh `cheng.stage3` 的 live `build-backend-driver` 命令转发出去，不再多绕一层 `cheng_v3.sh`；
`v3/tooling/cheng_v3.sh debug-report/print-symbols/print-line-map/print-elf`
现在已经把 seed 里真实的 `world/lock/lowering/primary/object/native` 调试面、
符号表、源码行映射和 Linux AArch64 ELF relocatable object 检视面直接公开出来，
查 ordinary pipeline 不再先依赖 `lldb/nm/objdump`；
`v3/tooling/cheng_v3.sh run-v2-selfhost-gate` 现在会固定强校验 `v2` 的 `tooling-selfhost-check + program-selfhost-check`，并要求 `quic_tls_transport_ecdsa_smoke` 与 `msquic_chain_smoke` 各自产出 `v2_network_smoke_report_v1` 版本化 report，不再只看原始 stdout；
`v3/tooling/cheng_v3.sh profile-run` 当前固定走“runtime 只产 `v3_profile_raw_v1`，再由 live `profile-report` 编译器命令汇总成 `v3_profile_v1`”这条路径；整条链不再编额外 helper，也不再依赖 `.v3.map` 文本解析，同时已经删掉 C 侧 `CHENG_V3_PROFILE_OUT` 最终报告分支；如果只开 profiler 而不显式给 raw 输出路径，runtime 默认会写 `<exe>.v3.profile.raw.txt`；
`v3/tooling/cheng_v3.sh crash-report` 现在和 `profile-report` 同口径：runtime 只吐 `v3_crash_raw_v1` 原始崩溃材料，最终 `[cheng-v3] v3_crash_report_v1` 统一由 live `crash-report --in:<raw-log> --out:<final-report>` 命令生成，不再让 C runtime 直接承担最终报告文本；
`v3/tooling/cheng_v3.sh verify-debug-runtime` 现在会直接卡死纯 Cheng debug runtime bridge 的 source/object 合同：Darwin ordinary 的 `provider_source_paths` 必须只落 `compiler_runtime_program_entry_provider_v3.cheng`、`core_runtime_provider_darwin_v3.cheng`、`debug_runtime_stub_v3.cheng`、`program_support_backend_v3.cheng` 这四个 `.cheng` provider；`runtime_debug_runtime_v3.o` 的对象合同也收成两层，公开 ABI 只能是 crash/profile/line-map 那 8 个导出，其余符号如果存在，必须全部落在 `cheng_v3_runtime_debug_runtime_stub_v3__*` 模块私有前缀下；
同一条 gate 现在还会继续卡 Darwin 宿主未定义符号白名单，以及 Linux AArch64 `nolibc` runtime object 的 syscall 依赖白名单，防止 bridge/runtime 依赖面继续静默长回去；
`v3/tooling/build_panic_trace_v3.sh`、`build_bounds_trace_v3.sh`、`build_signal_trace_v3.sh` 和 `verify-debug-runtime` 当前也已经统一成“raw crash log -> live crash-report -> final `v3_crash_report_v1`”这条路径；runtime 只保留信号、寄存器、machine/source frame 的原始材料输出；
`v3/tooling/run_v3_host_smokes.sh` 会用当前 host compiler 真编真跑 `v3/src/tests` 里的 ordinary program smoke、`chain_node_smoke`、链语义 smoke、`bft_three_replica_smoke`、`bio_reed_solomon_smoke`、`bio_same_person_smoke`、`bio_did_chain_node_smoke`、`bio_did_mobile_biometric_smoke` 和 `udp_importc_smoke`；尾段还会真跑 `chain_node` 两进程与三进程同步 gate。这条 host smoke 当前已经全绿。`v3/tooling/build_zero_exit_v3.sh` 和 `v3/tooling/build_call_chain_v3.sh` 现在都能用 `artifacts/v3_backend_driver/cheng` 真编真链真跑，说明 ordinary 主链已经越过 `.o/link/argv bridge`，并且 no-arg 尾调用必须按 `b callee` 发射，不能再用 `bl ...; ret`。这轮再把 ordinary lowering 改成按 entry 可达函数裁剪后，`program_selfhost` 的可达集已经从 `598` 个函数压到 `16` 个，`chain_node` 从 `1120` 个压到 `112` 个；当前剩下的统一真阻塞还是 `primary object body semantics missing`，只是现在已经不再被大闭包噪音淹没。可用 `CHENG_V3_SMOKE_COMPILER=<path>` 切换编译器入口；
generic Linux 这一层现在已经真打通：`v3/tooling/build_chain_node_linux_v3.sh` 和 `v3/tooling/build_rwad_bft_state_machine_linux_v3.sh` 默认会真产 `aarch64-unknown-linux-gnu` 的 `ELF relocatable object`，`v3/tooling/run_v3_linux_object_smokes.sh` 会做正式物件格式验收；同时 `v3/tooling/build_linux_nolibc_exe_v3.sh` 已经能通过 `v3 seed` 内建的 `internal_elf_linker` 真产 Linux AArch64 `nolibc exe`。另外 ordinary `x86_64-unknown-linux-gnu` 现在也已经有 verified object/exe 路径：`CHAIN_NODE_TARGET=x86_64-unknown-linux-gnu CHAIN_NODE_LINUX_ARTIFACT=exe v3/tooling/build_chain_node_linux_v3.sh`、`RWAD_BFT_TARGET=x86_64-unknown-linux-gnu RWAD_BFT_LINUX_ARTIFACT=exe v3/tooling/build_rwad_bft_state_machine_linux_v3.sh` 都能在 x64 Linux 主机上直接产出并跑 self-test。
`v3/tooling/run_slice_gate.sh` 和 `v3/tooling/build_backend_driver_v3.sh`
都只吃这条主线。

当前实现补充：

- 上面这段 Linux 口径已经过期。generic Linux `aarch64` `nolibc exe` 现在已经由 `v3 seed` 自己内建的 `internal_elf_linker` 真打通，不再依赖宿主 `ld/lld/gcc`；`CHAIN_NODE_LINUX_ARTIFACT=exe v3/tooling/build_chain_node_linux_v3.sh` 和 `RWAD_BFT_LINUX_ARTIFACT=exe v3/tooling/build_rwad_bft_state_machine_linux_v3.sh` 都会直接产出 Linux AArch64 ELF 可执行文件。
- Windows 和 `riscv64` 这期不伪装成“`v3 seed` 已直接支持”。当前真实已落地的是仓库主 backend 的内建 `COFF/PE` 与 `riscv64 ELF` self-link 能力；我把它们收成了 `v3/tooling/verify_windows_builtin_linker_v3.sh`、`v3/tooling/verify_riscv64_builtin_linker_v3.sh` 和 `v3/tooling/run_v3_windows_riscv_builtin_smokes.sh` 三个统一入口。
- 这三个入口会优先复用现成主 backend driver；如果仓库里还没有健康的主 backend driver，会直接 fail-fast，并要求显式提供 `BACKEND_DRIVER=/abs/path/to/cheng`，不做假成功和 silent fallback。
- `v3/tooling/bootstrap_bridge_v3.sh` 现在只保留最小冷启动选择：有 `cheng.stage3` 就直接交给 `cheng.stage3 bootstrap-bridge`，没有则优先复用 `artifacts/v3_backend_driver/cheng`，再没有才回 `cheng.stage0` 或 C seed 临时 runner；真正的 freshness 判断已经回到编译器本体。
- `v3` 现在已经补上生物同一人证明到 DID/链上存证这条主链：[bio_reed_solomon.cheng](/Users/lbcheng/cheng-lang/v3/src/project/bio_reed_solomon.cheng) 用 `GF(256)` 上的 `RS(32,16)` 和全 32 行 Berlekamp-Welch 解码收口 helper data 纠错；[bio_same_person.cheng](/Users/lbcheng/cheng-lang/v3/src/project/bio_same_person.cheng) 在此基础上把 `helper mask -> root secret -> P-256 Schnorr` 接成正式证明链；[bio_did_chain_node.cheng](/Users/lbcheng/cheng-lang/v3/src/project/bio_did_chain_node.cheng) 再把“指纹匹配 -> DID 创建/导入 -> 设备固定 PeerId -> chain_node 事件日志存证”串成一条正式编排链；新增的 [biometric.cheng](/Users/lbcheng/cheng-lang/v3/src/mobile/hardware/biometric.cheng) 和 [bio_did_mobile_biometric.cheng](/Users/lbcheng/cheng-lang/v3/src/project/bio_did_mobile_biometric.cheng) 又把“移动端指纹授权边界 -> DID 主线”收成正式 typed Cheng API。当前默认宿主桥还没接进仓库，所以未注入宿主结果时会直接 fail-fast。[bio_reed_solomon_smoke.cheng](/Users/lbcheng/cheng-lang/v3/src/tests/bio_reed_solomon_smoke.cheng)、[bio_same_person_smoke.cheng](/Users/lbcheng/cheng-lang/v3/src/tests/bio_same_person_smoke.cheng)、[bio_did_chain_node_smoke.cheng](/Users/lbcheng/cheng-lang/v3/src/tests/bio_did_chain_node_smoke.cheng) 和 [bio_did_mobile_biometric_smoke.cheng](/Users/lbcheng/cheng-lang/v3/src/tests/bio_did_mobile_biometric_smoke.cheng) 已接进默认 host smoke。

硬规则：

- `v3` 热路径接口不再把字符串、文本帧、`BigInt/Bytes` 壳暴露到公开面
- `bagua/BPI` 只留 sidecar/诊断位置，不进入权威执行和二进制布局
- 所有后续 gate 都要拿 `stage2/stage3` 产物和这里的 C 基线同口径对拍
- 当前真实阻塞已经明文接进 gate：`artifacts/v3_backend_driver/cheng` 已经能真编最小 ordinary 程序，但 `program-selfhost/chain_node` 还缺普通函数体语义子集
- `run_slice_gate.sh` 现在不会再只看 `--help`，而是顺序调用 `build_zero_exit_v3.sh`、`build_call_chain_v3.sh`、`build_program_selfhost_v3.sh` 和 `build_chain_node_v3.sh` 让 `artifacts/v3_backend_driver/cheng` 真编 ordinary program
- `compiler_main` 现在已经能被 host compiler 真编；`help/status` 正常，`system-link-exec` 也会稳定报 `v3 compiler: ordinary pipeline not implemented`。当前缺的不是入口壳，而是这条入口下面的真实 backend pipeline
