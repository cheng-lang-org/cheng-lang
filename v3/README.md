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

v3/tooling/run_slice_gate.sh
```

其中 `v3/tooling/cheng_v3.sh` 是 `v3` 自己的 shell 主控 launcher；
`v3/tooling/bootstrap_bridge_v3.sh` 会把 `v3/bootstrap/cheng_v3_seed.c`
和 `v3/bootstrap/stage1_bootstrap.cheng` 收成
`artifacts/v3_bootstrap/cheng.stage0 -> cheng.stage1 -> cheng.stage2 -> cheng.stage3`
以及 `bootstrap.env`；
`v3/tooling/cheng_v3.sh debug-report/print-symbols/print-line-map/print-elf`
现在已经把 seed 里真实的 `world/lock/lowering/primary/object/native` 调试面、
符号表、源码行映射和 Linux AArch64 ELF relocatable object 检视面直接公开出来，
查 ordinary pipeline 不再先依赖 `lldb/nm/objdump`；
`v3/tooling/run_v3_host_smokes.sh` 会用当前 host compiler 真编真跑 `v3/src/tests` 里的 ordinary program smoke、`chain_node_smoke`、链语义 smoke、`bft_three_replica_smoke` 和 `udp_importc_smoke`；尾段还会真跑 `chain_node` 两进程与三进程同步 gate。这条 host smoke 当前已经全绿。`v3/tooling/build_zero_exit_v3.sh` 和 `v3/tooling/build_call_chain_v3.sh` 现在都能用 `artifacts/v3_backend_driver/cheng` 真编真链真跑，说明 ordinary 主链已经越过 `.o/link/argv bridge`，并且 no-arg 尾调用必须按 `b callee` 发射，不能再用 `bl ...; ret`。这轮再把 ordinary lowering 改成按 entry 可达函数裁剪后，`program_selfhost` 的可达集已经从 `598` 个函数压到 `16` 个，`chain_node` 从 `1120` 个压到 `112` 个；当前剩下的统一真阻塞还是 `primary object body semantics missing`，只是现在已经不再被大闭包噪音淹没。可用 `CHENG_V3_SMOKE_COMPILER=<path>` 切换编译器入口；
generic Linux 这一层也已经前移：`v3/tooling/build_chain_node_linux_v3.sh` 和 `v3/tooling/build_rwad_bft_state_machine_linux_v3.sh` 默认会真产 `aarch64-unknown-linux-gnu` 的 `ELF relocatable object`，`v3/tooling/run_v3_linux_object_smokes.sh` 会用 `file + llvm-objdump -f` 验证；同时 `v3/tooling/build_linux_nolibc_exe_v3.sh` 已经把 Linux `aarch64` `nolibc exe` 的 `_start + preflight` 骨架接上。但 Linux 可执行文件仍然没打通，因为这台 Darwin 机器没有 Linux ELF linker，而且 `v3 seed` 还编不过 [system_helpers_backend_nolibc_linux_aarch64.cheng](/Users/lbcheng/cheng-lang/src/std/system_helpers_backend_nolibc_linux_aarch64.cheng) 的 runtime object；`x86_64-unknown-linux-gnu` 也还没有 verified ordinary object path。
`v3/tooling/run_slice_gate.sh` 和 `v3/tooling/build_backend_driver_v3.sh`
都只吃这条主线。

当前实现补充：

- 上面这段 Linux 口径已经过期。generic Linux `aarch64` `nolibc exe` 现在已经由 `v3 seed` 自己内建的 `internal_elf_linker` 真打通，不再依赖宿主 `ld/lld/gcc`；`CHAIN_NODE_LINUX_ARTIFACT=exe v3/tooling/build_chain_node_linux_v3.sh` 和 `RWAD_BFT_LINUX_ARTIFACT=exe v3/tooling/build_rwad_bft_state_machine_linux_v3.sh` 都会直接产出 Linux AArch64 ELF 可执行文件。
- Windows 和 `riscv64` 这期不伪装成“`v3 seed` 已直接支持”。当前真实已落地的是仓库主 backend 的内建 `COFF/PE` 与 `riscv64 ELF` self-link 能力；我把它们收成了 `v3/tooling/verify_windows_builtin_linker_v3.sh`、`v3/tooling/verify_riscv64_builtin_linker_v3.sh` 和 `v3/tooling/run_v3_windows_riscv_builtin_smokes.sh` 三个统一入口。
- 这三个入口会优先复用现成主 backend driver；如果仓库里还没有健康的主 backend driver，会直接 fail-fast，并要求显式提供 `BACKEND_DRIVER=/abs/path/to/cheng`，不做假成功和 silent fallback。

硬规则：

- `v3` 热路径接口不再把字符串、文本帧、`BigInt/Bytes` 壳暴露到公开面
- `bagua/BPI` 只留 sidecar/诊断位置，不进入权威执行和二进制布局
- 所有后续 gate 都要拿 `stage2/stage3` 产物和这里的 C 基线同口径对拍
- 当前真实阻塞已经明文接进 gate：`artifacts/v3_backend_driver/cheng` 已经能真编最小 ordinary 程序，但 `program-selfhost/chain_node` 还缺普通函数体语义子集
- `run_slice_gate.sh` 现在不会再只看 `--help`，而是顺序调用 `build_zero_exit_v3.sh`、`build_call_chain_v3.sh`、`build_program_selfhost_v3.sh` 和 `build_chain_node_v3.sh` 让 `artifacts/v3_backend_driver/cheng` 真编 ordinary program
- `compiler_main` 现在已经能被 host compiler 真编；`help/status` 正常，`system-link-exec` 也会稳定报 `v3 compiler: ordinary pipeline not implemented`。当前缺的不是入口壳，而是这条入口下面的真实 backend pipeline
