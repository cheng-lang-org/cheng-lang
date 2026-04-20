# 当前任务

- 当前最新判断
  - call 主线下一步不再是补字段，而是继续把 parser 里剩下的 call `detail` builder 降到兼容层。
  - typed facts、CSG report、parser 去重键、`exprId` 现在都已经不再依赖存储里的 call `detail`。
  - 下一刀优先看 `v3ParserCallExprDetailExact(...)` 能不能进一步退出 parser 热路径，不再在行扫描里白造历史文本。

- 当前最新判断
  - 主 smoke / probe 里的 call `detail` 读取已经基本清掉。
  - 下一步如果继续收 call 真源，就该评估 parser 里的 `detail` 兼容文本是否还值得保留，还是可以逐步降成纯调试辅助。

- 当前最新判断
  - parser call 节点现在已经有完整结构字段，typed/report/counting 也已经切过去。
  - 下一步该收的是 `detail` 的历史尾巴：把 core smoke/probe 里剩余的 call `detail` 读取继续清掉，然后再考虑是否把 `detail` 对 call 降到纯调试用途。
  - call 主线这边下一刀已经不是“再补字段”，而是“删掉剩余第二真源”。

- 当前最新判断
  - call typed 真源这条线，`qualifier/callee/target/target_source/target_importc/prefix_style/arg_count/args_text` 现在都已经进了结构化 fact。
  - 下一步不该再往 `args_text` 这条线上补壳子，而是继续把 `detail` 里还剩的 `kind/resolved/reason/target_source/importc` 也抽成 parser 结构字段，彻底结束 typed/lowering 对 `expr.detail` 的依赖。
  - 这轮已经把 call 对表校验从“看起来在验”修成“真的在验”，下一刀优先补 parser 结构化 call 节点，而不是回头做 parser 微优化。

- 当前最新判断
  - call typed fact 这条线已经把 `qualifier/callee/target/target_source/target_importc` 都收进来了，qualified external 推断也已经改成直接吃这组结构化字段。
  - call typed fact 这条线又前进了一步：`callee/target` 已经进 fact/rule 和 CSG hash，typed return inference 也已经改成直接吃这层字段。
  - call HIR unresolved reason 的正式 report 验收已经补上，下一步不再在这条线上加壳子。
  - `joinPath/ByteBuffer` 那个 `Bytes` retain 崩点已经不能再只当单点事故看；下一步要把“`Bytes` 是否托管”继续前移成 typed/lowering 可见事实，不能长期只靠 runtime registry 兜住语义边界。
  - fresh `stage2` 重新验证后，`parser_path_smoke` 已过；`Bytes` parent-copy 那条旧红点也确认不是当前主线 blocker。
  - call HIR 的 report/验收面已经收口，下一步不再补“有没有这个字段”，而是继续收 `resolved target / unknown reason / lowering fact` 的强校验和剩余真源。
  - 顶层 `scalar function` 发射收口先暂停；必须先把 `let/assign/return/call/scalar stmt` 的 parity 矩阵补全，再做真正单路化。
  - 现阶段可以继续推进的是 typed lowering 真源、call HIR 工程化、CSG/lowering plan 强校验，不是再硬推 seed 顶层语句并线。
  - 最新已完成的是：call 的 `surface kind / resolved / reason / target_source / target_importc` 已经前移进 typed fact/rule，`compiler_csg/lowering_plan` report 也已经改成从 typed facts 出数。

- 当前补充闭环
  - 已完成：
    - Cheng 文档、repo skill、local skill 镜像已统一补上“没有 tracing GC、runtime perf 看 ORC、默认公开表面安全优先但不是全域自动安全”的固定口径。
    - `perf_memory_contract_smoke` 现口径已经明确：`orc_perf_contract` 看 ORC runtime，`*_compile_exec_phase_summary` 直接看正式 `system-link-exec` 报告里严格成立的编译 phase 下界。
  - 当前判断：
    - 这轮关于“理论下界、内存/GC perf、内存安全边界”的口径已经收口到正式文档和技能镜像。
    - 下一步回到编译主线时，只继续做真正的新热点，不再反复解释这三个概念边界。
    - 理论下界稳定样本已从单条 ORC 扩到四条：`object_native_link_plan_smoke`、`chain_node_smoke`、`content_stub_smoke`、`orc_perf_contract_smoke`。

- 当前最新闭环补充：
  - 已完成：
    - `V3ExprPrepScratch.args` 已改成 lazy slab，不再常驻内嵌 `args[32][4096]`。
    - `g_v3_expr_prep_args_free_list` 已接进线程本地复用主链。
    - `v3_prepare_expr_call_state(...)` 已直接 classify 到 `scratch->surface`，不再保留本地 `V3ExprSurface` 再整份拷贝。
    - fresh `stage0 -> bootstrap-bridge -> stage2 no-handoff` 主 smoke 继续全绿。
    - 两次 `perf_memory_contract_smoke` 已稳定在 `compile=4530ms/4430ms`、`run=6420ms/6610ms`、`chain=3380ms/3320ms`、`content=7300ms/7340ms`。
  - 当前判断：
    - `prepare scratch` 这条线先停。
    - 下一步只做 fresh 采样后的新第一热点，不再继续围着 scratch 做小修小补。

- 当前目标
  - 继续把 typed lowering 真源往前收，不让 lowering/report 再碰 parser 细节串；然后再回 parser 剩余热路径。

- 下一步顺序
  - 先继续把 call typed fact 的真源往前推
    - 现在 `qualifier/callee/target` 都已进 fact；下一步补 `grouped import / unknown member fallback / unresolved shadow` 的更细粒度 typed fact 验收，把剩余“只看总数”的地方继续拆开。
    - 把 `typed_expr` 里剩余只用于 call 校验的 `expr.detail` 读取继续压缩到最小，只保留 parser 对 typed fact 的一致性核对。
    - 再往前就是看要不要把 `args_text/arg_count/prefix_style` 也并进 typed fact，让 typed/lowering 对 call 表面完全不再碰 parser detail。
  - 再把 `Bytes` 这条 runtime registry 语义继续往 typed/lowering 收口
    - 现在已经有 `bytes_view_orc_registry_smoke` 钉住“borrowed view no-op / owned block real retain”。
    - 下一步不是回头修假红的 `bytes_parent_copy_orc_smoke`，而是补编译期 fact：哪些表达式一定是 borrow view，哪些只是 `type=Bytes` 但所有权未知，不能继续只靠类型名猜。
  - call HIR unresolved reason 的正式 report 验收已完成
    - 现在由 `call_hir_matrix_smoke` 负责把现有四个 reason fixture 分别钉到 `compiler_csg/lowering_plan` report。
  - 先单独定位 `perf_memory_contract_smoke`
    - 核对 `perfgate.v3PerfMemoryParseOutputInt64(output, key)` 这条 qualified external call 的 typed/lowering 真相，确认是不是 parser/import/call HIR 被打坏，还是当前头部原本就有 `stmt_let` lowering 缺口。
  - 再看 `v3ParserResolveUnqualifiedExternalCallTarget(...)`
    - 优先排查重复 lookup、重复字符串唯一化、重复 profile/sourcePath 归因。
  - 再看 `v3ParserCallExprDetailExact(...)`
    - 在不改 detail 协议的前提下，继续减少 pair 追加和字符串拼接。
  - 如果 parser 继续做
    - `MaxQualifiedCallDepthLines` 只碰确定有收益的局部边界判断，不再重走 `raw line` 那条已回弹的路径。
    - `ReadImportEdgesLines` 下一刀优先看 `SourceExists/FileSize` 与 path concat，不再重复碰整行 trim。

- 每轮固定验收
  - `cc -std=c11 -O2 -Wall -Wextra -pedantic v3/bootstrap/cheng_v3_seed.c -o artifacts/v3_bootstrap/cheng.stage0`
  - `CHENG_V3_NO_BACKEND_DRIVER_HANDOFF=1 artifacts/v3_bootstrap/cheng.stage0 bootstrap-bridge`
  - `parser_path_smoke`
  - `parser_normalized_expr_smoke`
  - `call_hir_matrix_smoke`
  - `compiler_csg_smoke`
  - `lowering_plan_smoke`
  - `object_native_link_plan_smoke`
  - `CHENG_V3_SMOKE_COMPILER=/Users/lbcheng/cheng-lang/artifacts/v3_bootstrap/cheng.stage2 CHENG_V3_NO_BACKEND_DRIVER_HANDOFF=1 ... perf_memory_contract_smoke`
  - 必要时补 `/usr/bin/time -l` 和 `sample`

## cheng_node 第三阶段补充

- 已完成
  - `discovery + libp2p/quic/webrtc + VPN overlay/exit + MoQ/Fountain capability` 已并进 `cheng_node`。
  - `cheng_node ctl` 已打通 `announce / discover / stream / vpn-peer / vpn-route / vpn-status`。
  - `transport_discovery`、`vpn`、`content_moq_fountain` 都已接入统一 actor 面。
  - `identify` 不再被错误的 trusted-only 资源门禁挡住。

- 本轮硬修
  - `src/std/crypto/sha384.cheng`
    - `sha384Digest` 改成稳定 `while` 形状，ordinary 发码恢复。
  - `src/std/crypto/minasn1.cheng`
    - `trimLeadingZeros` 改成显式 `while`，去掉越作用域循环变量。
  - `v3/src/quic/platform/datapath_udp.cheng`
    - UDP bind/getsockname 报错串拆成逐段拼接，去掉 inline 大表达式。
  - `src/std/net/transports/udp_syscall.cheng`
    - blocking recv 报错串拆成逐段拼接。
  - `v3/src/project/cheng_node_transport.cheng`
    - discovery publish payload 改成 owning `Bytes` 拷贝，不再把 `str` 视图直接塞进 host store。
  - `v3/src/libp2p/host/host.cheng`
    - `identify` 资源导出改成公开可见资源，不再错误绑死 `trustedView`。

- 本轮验收
  - fresh `cheng_node_transport_vpn_smoke`
  - fresh `cheng_node_main`
  - `cheng_node_transport_vpn_smoke` 运行通过
  - `cheng_node_main ctl required-surface`
  - `cheng_node_main ctl bootstrap`
  - `cheng_node_main ctl announce` 三节点
  - `cheng_node_main ctl discover` 覆盖 `tcp/webrtc/quic`
  - `cheng_node_main ctl stream` 覆盖 `identify/content`
  - `cheng_node_main ctl head`
  - `cheng_node_main ctl vpn-peer`
  - `cheng_node_main ctl vpn-route overlay/exit`
  - `cheng_node_main ctl vpn-status`
  - `git diff --check`

- 编译时间
  - `cheng_node_transport_vpn_smoke real 26.69s`
  - `cheng_node_main real 28.42s`

## cheng_node 第四五阶段补充

- 已完成
  - `oracle + DiLoCo + EvoMap + compiler/world/publish 控制面` 已并进统一 `cheng_node`。
  - `MoQ + Fountain` 已继续保留在节点统一 capability 面内，没有再分裂成外部主程序。
  - `v3/src/DiLoCo` 已挂进统一 `cheng/v3` 包路径，`cheng_node` 主闭包可以直接编译、链接、验收。
  - `cheng_node ctl` 已补齐：
    - `oracle-submit / oracle`
    - `diloco-open / diloco-join / diloco-lease / diloco-barrier / diloco-delta / diloco-merge / diloco-status`
    - `evomap-agent / evomap-task / evomap-capsule / evomap-status`
  - `diloco-merge` 现已兼容 `--leader-seed` 和 `--peer-seed`。
  - `cheng_node` 身份根已扩成 10 域，新增 `evomap` 子密钥。
  - 统一索引面已补到 13 组，新增 `evomap(agent/task/capsule)`。

- 本轮硬修
  - `v3/src/project/cheng_node.cheng`
    - 补齐 Oracle、DiLoCo、EvoMap 三个 domain 的 runtime apply、O(1) keyed query 同步和状态摘要。
  - `v3/src/project/cheng_node_ctl_main.cheng`
    - 补齐 stage4/stage5 控制面子命令。
    - `diloco-merge` 的别名逻辑改成 ordinary 稳定写法，不再触发 lowering 红面。
  - `v3/src/runtime/program_support_backend_v3.cheng`
    - host runtime 补上 `cheng_mobile_host_emit_log` 导出 stub，修掉 `moq_fountain` 在普通主机链接面的缺符号问题。
  - `v3/src/tests/cheng_node_oracle_diloco_evomap_smoke.cheng`
    - DiLoCo 状态断言改成真实线协议字段名：`job_current_outer_step / lineage_count`。

- 本轮验收
  - fresh `cheng_node_oracle_diloco_evomap_smoke`
  - fresh `cheng_node_core_smoke`
  - fresh `cheng_node_main`
  - `cheng_node_main_final ctl oracle-submit/oracle`
  - `cheng_node_main_final ctl diloco-*` 全流程
  - `cheng_node_main_final ctl evomap-*` 全流程

- 编译时间
  - `cheng_node_oracle_diloco_evomap_smoke real 17.51s`
  - `cheng_node_core_smoke real 10.88s`
  - `cheng_node_main real 41.79s`

## cheng_node 第六阶段补充

- 已完成
  - `cheng_node ctl` 已补齐正式迁移入口：
    - `import-legacy-oracle`
    - `import-compiler-package`
    - `import-compiler-world`
    - `import-compiler-receipt`
  - 旧 `chain_node` 快照、旧 `oracle_bft` 状态、编译器 `package/world/receipt` 报告，已经都能导入同一个统一 `cheng_node` state。
  - compiler 的 `target` 索引不再只是注册表空壳；receipt 导入现在会同步锚定 `compiler/target`。
  - `oracle_bft_state_host` 已改成 ordinary 稳定写法，正式主闭包可直接编译通过。

- 当前剩余
  - DID 继续跟随 `import-legacy-chain` 一起迁，不额外造第二份独立导入格式。
  - 仓库里暂时没找到独立旧 `VPN/DiLoCo` 持久化 state 真源；这两项先不硬造迁移协议。
  - 下一步只剩三件正式收口活：
    - 回放验真
    - 切换脚本和旧命令迁移层
    - `perf_memory_contract_smoke` 那条 `stmt_let` lowering 红点

- 本轮验收
  - fresh `cheng_node_migration_smoke`
  - fresh `cheng_node_core_smoke`
  - fresh `cheng_node_oracle_diloco_evomap_smoke`
  - fresh `cheng_node_main`
  - `cheng_node_main_stage6 ctl import-legacy-chain`
  - `cheng_node_main_stage6 ctl import-legacy-oracle`
  - `cheng_node_main_stage6 ctl import-compiler-package`
  - `cheng_node_main_stage6 ctl import-compiler-world`
  - `cheng_node_main_stage6 ctl import-compiler-receipt`
  - `cheng_node_main_stage6 ctl query --group:compiler --kind:target`
  - `cheng_node_main_stage6 ctl oracle --pair:BTC/USD`

## v3 ORC / backend driver 续推

- 已完成
  - `bytes_overwrite_orc_smoke` 的最后一个真红点已经收掉：
    - `slot = rhs` 的父对象复制现在会正确释放旧 payload、保留新 payload。
  - `bytes_layout_copy_smoke` 已补齐：
    - `layout.byteBufToBytes(...)`
    - `layout.byteSpanToBytes(...)`
    - 现在都由正式 host smoke 钉住“结果必须是 owning copy，源改写/release 不得污染副本”。
  - `bytes_buffer_handle_snapshot_smoke` 已补齐：
    - 同一源缓冲先后注册两个 handle 时，快照必须按注册时刻分离。
    - 两次 decode 出来的 `Bytes` 也必须各自独立，不能互相 alias。
  - `ffi_handle_generation_reuse_smoke` / `ffi_handle_generation_stale_trap_smoke` 已补齐：
    - `release_raw_bridge` 后同槽复用必须 bump generation。
    - 旧 handle 在复用后必须命中 `detail=stale`，不能退化成泛化 trap。
  - `build-ffi-handle` 已补成完整闭环：
    - 同时覆盖普通成功、generation reuse 成功、released trap、stale trap。
  - `sub sp` 临时栈期间的局部取址真源已经收回稳定 frame base：
    - `bytebuf_view_smoke`
    - `bytebuf_len_probe_smoke`
    - `bytes_param_helper_smoke`
    - `wrapper_result_forward_varparam_smoke`
    - `wrapper_rebuild_result_forward_varparam_smoke`
  - `build-backend-driver` 主线重新打通：
    - fresh `cheng_debug_candidate status` 恢复正常
    - fresh `artifacts/v3_bootstrap/cheng.stage3 build-backend-driver` 通过
    - fresh `artifacts/v3_backend_driver/cheng status` 恢复正常
  - 回归已补齐：
    - 默认 `run-host-smokes` 已纳入 `bytebuf_view_smoke`
    - 默认 `run-host-smokes` 已纳入 `bytebuf_len_probe_smoke`
    - 默认 `run-host-smokes` 已纳入 `bytes_layout_copy_smoke`
    - 默认 `run-host-smokes` 已纳入 `bytes_view_orc_registry_smoke`
    - 默认 `run-host-smokes` 已纳入 `bytes_param_helper_smoke`
    - 默认 `run-host-smokes` 已纳入 `bytes_buffer_handle_copy_smoke`
    - 默认 `run-host-smokes` 已纳入 `bytes_buffer_handle_snapshot_smoke`
    - 默认 `run-host-smokes` 已纳入 `bytes_parent_copy_orc_smoke`
    - `bytes_overwrite_orc_smoke`
    - 默认 `run-host-smokes` 已纳入 `ffi_handle_generation_reuse_smoke`
    - 默认 `run-host-smokes` 已纳入 `wrapper_result_forward_varparam_smoke`
    - 默认 `run-host-smokes` 已纳入 `wrapper_rebuild_result_forward_varparam_smoke`
    - `ffi_handle_stale_trap_smoke` / `ffi_handle_generation_stale_trap_smoke` 继续留在 `build-ffi-handle` 专用 trap gate
    - `perf_memory_contract_smoke`
    - `cheng_skill_consistency_smoke`

- 当前剩余
  - 继续把 alias overwrite、容器写入、FFI/raw-handle 边界补成系统性覆盖；现在已经把 `Bytes` registry / buffer-handle copy / parent copy / overwrite 收进了正式 gate。
  - fixed array composite `Bytes` lvalue materialize 这条当前还是 ordinary 红面，先不拿它做容器写 gate；容器边界下一步要换别的稳定形状。
  - 回到 `system-link-exec` phase 真源，把 `exec_phase_*` 的正式报告继续推到更多 smoke，不只停在当前已过样本。
  - 再回 parser 第一热点，按 fresh sample 继续做 `ResolveUnqualifiedExternalCallTarget / CallExprDetailExact`，不回头再磨 `prepare scratch`。

## mobile-shell / r2c 续推

- 已完成
  - `mobile_shell_codegen_smoke` 已恢复全绿，runtime contract 导出面保持：
    - `mobile_shell_runtime_contract_v1.json`
    - `runtime/runtime_contract_payload.json`
    - `runtime/runtime_bundle_payload.json`
    - `runtime/mobile_shell_launch_args.kv`
    - `runtime/mobile_shell_launch_args.json`
  - runtime contract / payload / bundle payload 已镜像进三端真实资源目录：
    - Android: `android/app/src/main/assets/...`
    - iOS: `ios/runtime/...`
    - Harmony: `harmony/entry/src/main/resources/rawfile/...`
  - launch args sidecar 已同步镜像进三端真实资源目录：
    - Android: `android/app/src/main/assets/runtime/mobile_shell_launch_args.{kv,json}`
    - iOS: `ios/runtime/mobile_shell_launch_args.{kv,json}`
    - Harmony: `harmony/entry/src/main/resources/rawfile/mobile_shell_launch_args.{kv,json}`
  - iOS `project.pbxproj` 已把 runtime contract / payload / bundle payload 加入 `Resources` build phase。
  - iOS `project.pbxproj` 已把 `mobile_shell_launch_args.kv/json` 一起加入 `Resources` build phase。
  - 三端宿主源码现在都带有 launch args sidecar 相对路径常量：
    - Android: `kChengLaunchArgsKvRel / kChengLaunchArgsJsonRel`
    - iOS: `kChengLaunchArgsKvRel / kChengLaunchArgsJsonRel`
    - Harmony: `kChengLaunchArgsKvRel / kChengLaunchArgsJsonRel`
  - Android 宿主现在会在 `surfaceCreated(...)` 时从 `assets/runtime/mobile_shell_launch_args.{kv,json}` 读取 sidecar 文本，并把两份文本一起传给 native host。
  - Android native host 现在会在 `nativeCreate(...)` 里调用 `cheng_mobile_host_runtime_set_launch_args(...)`，不再只停在路径常量阶段。
  - iOS 宿主现在会在 `initWithFrame:` 时从 bundle 里的 `runtime/mobile_shell_launch_args.{kv,json}` 读取 sidecar 文本，并调用 `cheng_mobile_host_runtime_set_launch_args(...)`。
  - iOS 导出不再把文件写进仓库根下怪目录：
    - `</string>/ios/...`
    - `\";/ios/...`
  - `ios/ChengGeneratedApp.xcodeproj/project.pbxproj` 已改成稳定落盘路径，不再缺失。
  - 仓库根下误生成的 `<` 和 `\";` 目录已清理。

- 当前剩余
  - 继续把 `mobile-shell` 从“能导出 buildable 壳”推进到“壳里能真实启动 Cheng GUI runtime”。
  - 继续让三端宿主真正消费 runtime manifest / runtime contract / runtime bundle；这轮只把 Android 和 iOS 的 launch args sidecar 读取打通了，runtime manifest 本身还没进宿主消费闭环。
  - Harmony 宿主还没开始读 launch args sidecar，下一步单独补这条，不回头做 launch args 内联。
  - 宿主 launch args 注入已经确认走 sidecar 读取这条正路；不要再回到“直接内联进原宿主生成函数”那条会把 `mobile_shell_tool` 打红的路径。

- 新进展
  - `mobile-shell export --platform ios` 已再次核验，不再生成 `</string>/ios` 这类仓库根脏目录。
  - `mobile-shell build-probe`、`verify-mobile-shell-build-probe`、`mobile_shell_build_probe_smoke` 已重新全绿。
  - `mobile_shell_codegen_smoke` 已补齐三端 runtime 资源镜像断言，并重新全绿。
  - Android / iOS 宿主现在都已经从各自打包资源里读取 `mobile_shell_launch_args.kv/json`，并把文本交给 runtime 的 `cheng_mobile_host_runtime_set_launch_args(...)`。
  - `/tmp/v3_mobile_shell_probe_launch_read/mobile_shell_build_probe.summary.env` 已实锤三端构建全绿：`android_exit_code=0`、`ios_exit_code=0`、`harmony_exit_code=0`。

- 本轮验收
  - `artifacts/v3_backend_driver/cheng run-host-smokes mobile_shell_codegen_smoke`
  - `artifacts/v3_backend_driver/cheng mobile-shell build-probe --platform all --out-dir /tmp/v3_mobile_shell_probe_launch_read --app-id org.cheng.launchread --app-name ChengLaunchRead --lib-name cheng_launch_read_app`
  - `artifacts/v3_backend_driver/cheng verify-mobile-shell-build-probe`
  - `artifacts/v3_backend_driver/cheng run-host-smokes mobile_shell_build_probe_smoke`

## cheng_node_main 统一入口收口

- 已完成
  - `cheng_node_main` 现在同时支持：
    - 直接子命令入口：`cheng_node_main <subcmd>`
    - 兼容前缀入口：`cheng_node_main ctl <subcmd>`
  - 旧链迁移闭环已经打通：
    - 旧 `chain_node` 快照可以写出非空 payload
    - 旧快照可以重新解码并导入 `cheng_node`
    - 导入后 state reload 会重建账本、proof、索引和 runtime 状态
  - `ByteBuf/ByteSpan -> Bytes` 的公共 owning copy 已收口到 `bytes_layout`，不再让快照编码、payload 哈希、fixed32 读取落到空视图或错误切片。
  - state reload 的 stored-event replay 已改成“重建账本/索引/运行时，不重放 live actor dispatch”，避免把恢复路径和实时派发路径混成一层。
  - `v3/src/project/cheng_node.cheng`、`v3/src/project/cheng_node_ledger.cheng`、`v3/src/tooling/compiler_world.cheng`、`v3/src/tooling/compiler_csg.cheng` 的 CID helper 已统一回 owning bytes 路径。

- 当前剩余
  - 继续把旧 `cheng_node` 独立入口往下裁掉，最终只保留 `cheng_node_main` 作为正式宿主。
  - 继续做第六阶段剩余两项：
    - 切换脚本和旧命令迁移层
    - `perf_memory_contract_smoke` 那条 `stmt_let` lowering 红点

- 本轮验收
  - `artifacts/v3_backend_driver/cheng run-host-smokes cheng_node_identity_smoke`
  - `artifacts/v3_backend_driver/cheng run-host-smokes cheng_node_core_smoke`
  - `artifacts/v3_backend_driver/cheng run-host-smokes cheng_node_migration_smoke`
  - `artifacts/v3_backend_driver/cheng run-host-smokes cheng_node_oracle_diloco_evomap_smoke`
  - `/tmp/cheng_node_main_unified6 required-surface`
  - `/tmp/cheng_node_main_unified6 ctl required-surface`
  - `/tmp/cheng_node_main_unified6 bootstrap --root:cheng-node-main-unified-root`
  - `/usr/bin/time -p artifacts/v3_backend_driver/cheng system-link-exec --in:v3/src/project/cheng_node_main.cheng --emit:exe --target:arm64-apple-darwin --out:/tmp/cheng_node_main_unified6 --report-out:/tmp/cheng_node_main_unified6.report.txt`

## cheng_node_main 入口去旧壳

- 已完成
  - `cheng_node_main` 的帮助口径已经改成：
    - 默认：`cheng_node_main <command> [flags]`
    - 兼容：`cheng_node_main ctl <command> [flags]`
  - `v3/src/project/cheng_node_ctl_main.cheng` 里未使用的旧 `V3ChengNodeCtlCommand*` 包装函数已经删除，只保留统一的 `V3ChengNodeCommandAt(...)` 分发面。
  - `cheng_node_core_smoke` 已改成校验 `cheng_node_main` 默认入口口径，不再把 `ctl` 当唯一默认面。

- 本轮验收
  - `artifacts/v3_backend_driver/cheng run-host-smokes cheng_node_core_smoke`
  - `/tmp/cheng_node_main_unified7 required-surface`
  - `/tmp/cheng_node_main_unified7 ctl required-surface`
  - `/tmp/cheng_node_main_unified7 bootstrap --root:cheng-node-main-unified-root-7`
  - `/usr/bin/time -p artifacts/v3_backend_driver/cheng system-link-exec --in:v3/src/project/cheng_node_main.cheng --emit:exe --target:arm64-apple-darwin --out:/tmp/cheng_node_main_unified7 --report-out:/tmp/cheng_node_main_unified7.report.txt`
