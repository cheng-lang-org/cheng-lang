## 当前任务

| 项目 | 内容 |
|---|---|
| 目标 | 把这轮 `Pin scheduler` 剩下的真 blocker 一口气收完：修掉 host 总 gate 里的 `tailnet` 运行期段错，并确认 host 与 no-cache `stage23` 两条 gate 都重新全绿。 |
| 主线 | 这轮先在 [strutils.cheng](/Users/lbcheng/cheng-lang/src/std/strutils.cheng) 把 `strutilsAppendStr(...)` 收成直接 `add(seqInst, val)`，先消掉 host gate 前面的 `chengStrStoreCompat/strutilsAppendStr` 编译缺口；随后把 [tailnet_transport.cheng](/Users/lbcheng/cheng-lang/v3/src/libp2p/transports/tailnet_transport.cheng) 的 `peerSessions` 从字符串绕路改成直接保存 `V3Libp2pTailnetPeerSession`，再把 [v3Libp2pTailnetUpsertPeerSession(...)](/Users/lbcheng/cheng-lang/v3/src/libp2p/transports/tailnet_transport.cheng) 从“9 个参数里夹一堆 `str`”收成“只吃一个 session record”，同步改 [libp2p_tailnet_transport_smoke.cheng](/Users/lbcheng/cheng-lang/v3/src/tests/libp2p_tailnet_transport_smoke.cheng)、[libp2p_tailnet_derp_smoke.cheng](/Users/lbcheng/cheng-lang/v3/src/tests/libp2p_tailnet_derp_smoke.cheng)、[chain_node_tailnet_smoke.cheng](/Users/lbcheng/cheng-lang/v3/src/tests/chain_node_tailnet_smoke.cheng)。最后复跑完整 [run_v3_host_smokes.sh](/Users/lbcheng/cheng-lang/v3/tooling/run_v3_host_smokes.sh) 与 no-cache [run_v3_stage23_libp2p_smokes.sh](/Users/lbcheng/cheng-lang/v3/tooling/run_v3_stage23_libp2p_smokes.sh)，确认 `Pin scheduler`、`tailnet`、内容面和整条 libp2p 主链一起继续真绿。 |
| 文件 | `src/std/strutils.cheng` `v3/src/libp2p/transports/tailnet_transport.cheng` `v3/src/tests/libp2p_tailnet_transport_smoke.cheng` `v3/src/tests/libp2p_tailnet_derp_smoke.cheng` `v3/src/tests/chain_node_tailnet_smoke.cheng` `task_plan.md` `progress.md` `findings.md` |
| 不做 | 不继续在 `tailnet` 里保留字符串回编解码的内存态存储；不靠调试 `echo`、重试或 runner 顺序掩盖段错；不回退已经转绿的 `Pin scheduler/content/QUIC` 主链。 |
| 验收 | [libp2p_tailnet_transport_smoke.cheng](/Users/lbcheng/cheng-lang/v3/src/tests/libp2p_tailnet_transport_smoke.cheng)、[libp2p_tailnet_derp_smoke.cheng](/Users/lbcheng/cheng-lang/v3/src/tests/libp2p_tailnet_derp_smoke.cheng)、[chain_node_tailnet_smoke.cheng](/Users/lbcheng/cheng-lang/v3/src/tests/chain_node_tailnet_smoke.cheng) 必须前台 `ok`；完整 [run_v3_host_smokes.sh](/Users/lbcheng/cheng-lang/v3/tooling/run_v3_host_smokes.sh) 必须输出 `v3 host smokes: ok`；完整 no-cache [run_v3_stage23_libp2p_smokes.sh](/Users/lbcheng/cheng-lang/v3/tooling/run_v3_stage23_libp2p_smokes.sh) 必须输出 `v3 stage2/stage3 libp2p: ok`。 |

## 当前任务

| 项目 | 内容 |
|---|---|
| 目标 | 把 `WebRTC` 收成“协议主链进 `stage23`，宿主原生 datachannel 内容桥留在 host”的最终稳定边界，并让 host 与 no-cache `stage23` gate 继续全绿。 |
| 主线 | 这轮不再强行让 [webrtc_datachannel_content_smoke.cheng](/Users/lbcheng/cheng-lang/v3/src/tests/webrtc_datachannel_content_smoke.cheng) 跟着 `stage23` 走。最终稳定形状是：保留 [webrtc_signal.cheng](/Users/lbcheng/cheng-lang/v3/src/libp2p/protocols/webrtc_signal.cheng) 与 [webrtc_transport.cheng](/Users/lbcheng/cheng-lang/v3/src/libp2p/transports/webrtc_transport.cheng) 的 `signal transcript + native datachannel bridge` 主链；让 [content_runtime_host.cheng](/Users/lbcheng/cheng-lang/v3/src/chain/content_runtime_host.cheng) 去掉 WebRTC native content fetch 依赖，只保留 TCP 内容宿主路径；新增 [content_runtime_host_webrtc.cheng](/Users/lbcheng/cheng-lang/v3/src/chain/content_runtime_host_webrtc.cheng) 承接宿主 WebRTC 内容拉取；[webrtc_datachannel_content_smoke.cheng](/Users/lbcheng/cheng-lang/v3/src/tests/webrtc_datachannel_content_smoke.cheng) 改成显式走这个宿主专属模块。这样 `stage23` 继续覆盖 `signal/session/sync/content` 的协议模型主链，host runner 则额外覆盖原生 WebRTC 内容桥。 |
| 文件 | `v3/src/chain/content_runtime_host.cheng` `v3/src/chain/content_runtime_host_webrtc.cheng` `v3/src/tests/webrtc_datachannel_content_smoke.cheng` `v3/src/libp2p/transports/webrtc_transport.cheng` `v3/tooling/run_v3_stage23_libp2p_smokes.sh` `v3/tooling/README.md` `task_plan.md` `progress.md` `findings.md` |
| 不做 | 不把宿主原生 WebRTC datachannel 内容桥继续塞进 `stage23`；不再为了 `stage23` 去扭曲 [webrtc_transport.cheng](/Users/lbcheng/cheng-lang/v3/src/libp2p/transports/webrtc_transport.cheng) 的 host-only native bridge 实现；不回退已经转绿的 `Pin` 与 `content` 主链。 |
| 验收 | host： [webrtc_signal_codec_smoke.cheng](/Users/lbcheng/cheng-lang/v3/src/tests/webrtc_signal_codec_smoke.cheng)、[webrtc_signal_session_smoke.cheng](/Users/lbcheng/cheng-lang/v3/src/tests/webrtc_signal_session_smoke.cheng)、[webrtc_turn_fallback_smoke.cheng](/Users/lbcheng/cheng-lang/v3/src/tests/webrtc_turn_fallback_smoke.cheng)、[libp2p_webrtc_sync_smoke.cheng](/Users/lbcheng/cheng-lang/v3/src/tests/libp2p_webrtc_sync_smoke.cheng)、[webrtc_datachannel_content_smoke.cheng](/Users/lbcheng/cheng-lang/v3/src/tests/webrtc_datachannel_content_smoke.cheng) 必须前台 `ok`；完整 [run_v3_host_smokes.sh](/Users/lbcheng/cheng-lang/v3/tooling/run_v3_host_smokes.sh) 必须输出 `v3 host smokes: ok`；完整 no-cache [run_v3_stage23_libp2p_smokes.sh](/Users/lbcheng/cheng-lang/v3/tooling/run_v3_stage23_libp2p_smokes.sh) 必须在不带 [webrtc_datachannel_content_smoke.cheng](/Users/lbcheng/cheng-lang/v3/src/tests/webrtc_datachannel_content_smoke.cheng) 的前提下继续输出 `v3 stage2/stage3 libp2p: ok`。 |

## 当前任务

| 项目 | 内容 |
|---|---|
| 目标 | 把 `v3` 的 WebRTC 从“只开 signal stream 然后直接走本地 loopback”收成“signal 真编码过网 + native datachannel request-response”，并让 host 与 no-cache `stage23` gate 继续全绿。 |
| 主线 | 这轮直接沿现有 mixed-network 主线收最短真链：在 [webrtc_signal.cheng](/Users/lbcheng/cheng-lang/v3/src/libp2p/protocols/webrtc_signal.cheng) 补完整 transcript 编解码与 `Apply`；在 [webrtc_transport.cheng](/Users/lbcheng/cheng-lang/v3/src/libp2p/transports/webrtc_transport.cheng) 新增 native datachannel bridge、stats 和 signal transcript payload 导出；在 [host.cheng](/Users/lbcheng/cheng-lang/v3/src/libp2p/host/host.cheng) 与 [content_runtime_host.cheng](/Users/lbcheng/cheng-lang/v3/src/chain/content_runtime_host.cheng) 把 WebRTC 路径改成“先准备 signal wire，再走 native request-response”，不再直接掉 `v3Libp2pWebrtcLoopbackRequestResponse(...)`。同时补 [system_helpers.h](/Users/lbcheng/cheng-lang/src/runtime/native/system_helpers.h) / [system_helpers.c](/Users/lbcheng/cheng-lang/src/runtime/native/system_helpers.c) 的双通道宿主桥，并把 [webrtc_signal_codec_smoke.cheng](/Users/lbcheng/cheng-lang/v3/src/tests/webrtc_signal_codec_smoke.cheng)、[webrtc_signal_session_smoke.cheng](/Users/lbcheng/cheng-lang/v3/src/tests/webrtc_signal_session_smoke.cheng)、[webrtc_turn_fallback_smoke.cheng](/Users/lbcheng/cheng-lang/v3/src/tests/webrtc_turn_fallback_smoke.cheng)、[libp2p_webrtc_sync_smoke.cheng](/Users/lbcheng/cheng-lang/v3/src/tests/libp2p_webrtc_sync_smoke.cheng)、[webrtc_datachannel_content_smoke.cheng](/Users/lbcheng/cheng-lang/v3/src/tests/webrtc_datachannel_content_smoke.cheng) 收成能真咬住“signal payload 已过网、native bridge 已命中、loopback request-response 没命中”的形状。 |
| 文件 | `v3/src/libp2p/protocols/webrtc_signal.cheng` `v3/src/libp2p/transports/webrtc_transport.cheng` `v3/src/libp2p/host/host.cheng` `v3/src/chain/content_runtime_host.cheng` `src/runtime/native/system_helpers.h` `src/runtime/native/system_helpers.c` `v3/src/tests/webrtc_signal_codec_smoke.cheng` `v3/src/tests/webrtc_signal_session_smoke.cheng` `v3/src/tests/webrtc_turn_fallback_smoke.cheng` `v3/src/tests/libp2p_webrtc_sync_smoke.cheng` `v3/src/tests/webrtc_datachannel_content_smoke.cheng` `task_plan.md` `progress.md` `findings.md` |
| 不做 | 不新造 `tailscale transport`；不把 `DERP/TURN` 混成一层；不再允许 WebRTC 路径偷偷回到 request-response loopback。 |
| 验收 | 上面 5 条 WebRTC smoke 必须在 host 前台 `ok`；完整 [run_v3_host_smokes.sh](/Users/lbcheng/cheng-lang/v3/tooling/run_v3_host_smokes.sh) 和 no-cache [run_v3_stage23_libp2p_smokes.sh](/Users/lbcheng/cheng-lang/v3/tooling/run_v3_stage23_libp2p_smokes.sh) 必须继续全绿。 |

## 当前任务

| 项目 | 内容 |
|---|---|
| 目标 | 把 `Pin` 从专用宿主洞口收成 `host/host_quic` 正式协议分发表里的内建协议，并让 host 与 `stage23` gate 继续全绿。 |
| 主线 | 这轮不再追动态 handler 注册表，直接把 [host.cheng](/Users/lbcheng/cheng-lang/v3/src/libp2p/host/host.cheng) 和 [host_quic.cheng](/Users/lbcheng/cheng-lang/v3/src/libp2p/host/host_quic.cheng) 收成最终稳定形状：`Pin` 走内建 `ServePayload/RequestResponse`，`sync` 保留已经验证过的直通路径，不强行并进同一条通用 `ServePayload` 主链。随后把 [pin_runtime_host.cheng](/Users/lbcheng/cheng-lang/v3/src/chain/pin_runtime_host.cheng) 和 [pin_runtime_host_quic.cheng](/Users/lbcheng/cheng-lang/v3/src/chain/pin_runtime_host_quic.cheng) 的 proof 路径改成只认 `protocol=pin`，不再直接调用专用 `v3Libp2pPinServePayload(...)`。最后用 [pin_runtime_host_smoke.cheng](/Users/lbcheng/cheng-lang/v3/src/tests/pin_runtime_host_smoke.cheng)、[pin_runtime_quic_smoke.cheng](/Users/lbcheng/cheng-lang/v3/src/tests/pin_runtime_quic_smoke.cheng)、[libp2p_protocols_smoke.cheng](/Users/lbcheng/cheng-lang/v3/src/tests/libp2p_protocols_smoke.cheng) 先压最短链，再复跑完整 host 与 `stage23` gate，并同步 [Pin托管.md](/Users/lbcheng/cheng-lang/v3/docs/Pin托管.md)、[内容平面.md](/Users/lbcheng/cheng-lang/v3/docs/内容平面.md)、[Unimaker内容发布与分发协议v1.md](/Users/lbcheng/cheng-lang/v3/docs/Unimaker内容发布与分发协议v1.md)、[README.md](/Users/lbcheng/cheng-lang/v3/docs/README.md)、记录文件的真实口径。 |
| 文件 | `v3/src/libp2p/host/host.cheng` `v3/src/libp2p/host/host_quic.cheng` `v3/src/chain/pin_runtime_host.cheng` `v3/src/chain/pin_runtime_host_quic.cheng` `v3/docs/Pin托管.md` `v3/docs/内容平面.md` `v3/docs/Unimaker内容发布与分发协议v1.md` `v3/docs/README.md` `task_plan.md` `progress.md` `findings.md` |
| 不做 | 不新造动态 handler 注册表；不改 `content runtime` 主链；不碰 `Pin` 调度器、账本和随机信标。 |
| 验收 | [pin_runtime_host_smoke.cheng](/Users/lbcheng/cheng-lang/v3/src/tests/pin_runtime_host_smoke.cheng)、[pin_runtime_quic_smoke.cheng](/Users/lbcheng/cheng-lang/v3/src/tests/pin_runtime_quic_smoke.cheng)、[libp2p_protocols_smoke.cheng](/Users/lbcheng/cheng-lang/v3/src/tests/libp2p_protocols_smoke.cheng) 必须前台 `ok`；完整 [run_v3_host_smokes.sh](/Users/lbcheng/cheng-lang/v3/tooling/run_v3_host_smokes.sh) 与 [run_v3_stage23_libp2p_smokes.sh](/Users/lbcheng/cheng-lang/v3/tooling/run_v3_stage23_libp2p_smokes.sh) 必须继续全绿。 |

## 当前任务

| 项目 | 内容 |
|---|---|
| 目标 | 把 `Pin` proof 请求和 `content fetch` 传输 ABI 一口气收口，并让 host 与 `stage23` gate 再次全绿。 |
| 主线 | 这轮不再折腾 `pin_registry` 旁路，保留 [host.cheng](/Users/lbcheng/cheng-lang/v3/src/libp2p/host/host.cheng) 里的宿主附着式 `pinRuntime` 形状，继续让 [pin_runtime_host.cheng](/Users/lbcheng/cheng-lang/v3/src/chain/pin_runtime_host.cheng) 和 [pin_runtime_host_quic.cheng](/Users/lbcheng/cheng-lang/v3/src/chain/pin_runtime_host_quic.cheng) 直接通过远端 `host` 处理 proof。真正的活根是 [content_runtime_host.cheng](/Users/lbcheng/cheng-lang/v3/src/chain/content_runtime_host.cheng) 和 [content_runtime_host_quic.cheng](/Users/lbcheng/cheng-lang/v3/src/chain/content_runtime_host_quic.cheng) 里 `Result[Bytes]` 直接跨 transport 走，导致 `content_runtime_smoke` 在 `v3ContentFetchResponseDecode(...)` 里崩；最终已把 `servePayload/responsePayload/responseBytes` 全改成显式本地 `Bytes` 物化，再删掉未接主线的 [pin_registry.cheng](/Users/lbcheng/cheng-lang/v3/src/libp2p/host/pin_registry.cheng) 失败试刀。 |
| 文件 | `v3/src/chain/content_runtime_host.cheng` `v3/src/chain/content_runtime_host_quic.cheng` `v3/src/libp2p/host/host.cheng` `v3/src/chain/pin_runtime_host.cheng` `v3/src/chain/pin_runtime_host_quic.cheng` `v3/src/tests/pin_runtime_host_smoke.cheng` `v3/src/tests/pin_runtime_quic_smoke.cheng` `task_plan.md` `progress.md` `findings.md` |
| 不做 | 不把 `host` 改成额外注册表；不保留未接主线的 sidecar registry；不碰 `Pin` 账本、随机信标和调度器。 |
| 验收 | [pin_runtime_host_smoke.cheng](/Users/lbcheng/cheng-lang/v3/src/tests/pin_runtime_host_smoke.cheng)、[pin_runtime_quic_smoke.cheng](/Users/lbcheng/cheng-lang/v3/src/tests/pin_runtime_quic_smoke.cheng)、[content_runtime_smoke.cheng](/Users/lbcheng/cheng-lang/v3/src/tests/content_runtime_smoke.cheng)、[content_quic_smoke.cheng](/Users/lbcheng/cheng-lang/v3/src/tests/content_quic_smoke.cheng)、[chain_node_tailnet_smoke.cheng](/Users/lbcheng/cheng-lang/v3/src/tests/chain_node_tailnet_smoke.cheng) 都要前台 `ok`；完整 [run_v3_host_smokes.sh](/Users/lbcheng/cheng-lang/v3/tooling/run_v3_host_smokes.sh) 与 [run_v3_stage23_libp2p_smokes.sh](/Users/lbcheng/cheng-lang/v3/tooling/run_v3_stage23_libp2p_smokes.sh) 必须重新全绿。 |

## 当前任务

| 项目 | 内容 |
|---|---|
| 目标 | 一口气把 `Pin` 从“helper 手喂 `remoteRuntime`”收成“宿主附着式远端 handler”，并让 host 与 `stage23` gate 继续全绿。 |
| 主线 | 这轮直接走最小真改法：在 [host.cheng](/Users/lbcheng/cheng-lang/v3/src/libp2p/host/host.cheng) 给 `V3Libp2pHost` 增 `pinRuntimeAttached + pinRuntime`，补 [v3Libp2pAttachPinRuntime(...)](/Users/lbcheng/cheng-lang/v3/src/libp2p/host/host.cheng) 和 [v3Libp2pPinServePayload(...)](/Users/lbcheng/cheng-lang/v3/src/libp2p/host/host.cheng)。随后把 [pin_runtime_host.cheng](/Users/lbcheng/cheng-lang/v3/src/chain/pin_runtime_host.cheng) 和 [pin_runtime_host_quic.cheng](/Users/lbcheng/cheng-lang/v3/src/chain/pin_runtime_host_quic.cheng) 的 `RequestProof(...)` 收成只吃 `remoteHost + challenge + offer + accept + manifest`，不再显式传 `remoteRuntime`；再把 [pin_runtime_host_smoke.cheng](/Users/lbcheng/cheng-lang/v3/src/tests/pin_runtime_host_smoke.cheng) 和 [pin_runtime_quic_smoke.cheng](/Users/lbcheng/cheng-lang/v3/src/tests/pin_runtime_quic_smoke.cheng) 改成真校验 `providerHost.pinRuntime` 被远端 proof 请求推进。最后同步 [Pin托管.md](/Users/lbcheng/cheng-lang/v3/docs/Pin托管.md)、[内容平面.md](/Users/lbcheng/cheng-lang/v3/docs/内容平面.md)、[Unimaker内容发布与分发协议v1.md](/Users/lbcheng/cheng-lang/v3/docs/Unimaker内容发布与分发协议v1.md)、[README.md](/Users/lbcheng/cheng-lang/v3/docs/README.md) 的真实口径。 |
| 文件 | `v3/src/libp2p/host/host.cheng` `v3/src/chain/pin_runtime_host.cheng` `v3/src/chain/pin_runtime_host_quic.cheng` `v3/src/tests/pin_runtime_host_smoke.cheng` `v3/src/tests/pin_runtime_quic_smoke.cheng` `v3/docs/Pin托管.md` `v3/docs/内容平面.md` `v3/docs/Unimaker内容发布与分发协议v1.md` `v3/docs/README.md` `task_plan.md` `progress.md` `findings.md` |
| 不做 | 不把 `host` 抽成通用回调注册表；不顺手重构 `content runtime`；不把 `Pin` 夸成已经有调度器、随机信标或奖励账本。 |
| 验收 | [pin_runtime_host_smoke.cheng](/Users/lbcheng/cheng-lang/v3/src/tests/pin_runtime_host_smoke.cheng) 和 [pin_runtime_quic_smoke.cheng](/Users/lbcheng/cheng-lang/v3/src/tests/pin_runtime_quic_smoke.cheng) 必须前台 `ok`；完整 [run_v3_host_smokes.sh](/Users/lbcheng/cheng-lang/v3/tooling/run_v3_host_smokes.sh) 与 [run_v3_stage23_libp2p_smokes.sh](/Users/lbcheng/cheng-lang/v3/tooling/run_v3_stage23_libp2p_smokes.sh) 必须继续全绿。 |

## 当前任务

| 项目 | 内容 |
|---|---|
| 目标 | 一口气把 `Pin` 的最小网络面收完：`offer/accept/settlement` 走 `ingress + store/sync`，`challenge/proof` 走独立 `pin` 协议，并把 host 与 `stage23` gate 一起拉绿。 |
| 主线 | 这轮直接沿现有 `content runtime` 宿主形状推进：在 [pin_runtime_host.cheng](/Users/lbcheng/cheng-lang/v3/src/chain/pin_runtime_host.cheng) 和 [pin_runtime_host_quic.cheng](/Users/lbcheng/cheng-lang/v3/src/chain/pin_runtime_host_quic.cheng) 固定 metadata 回拉和 proof request 入口，把 `challenge` 校验收成显式 `offer + accept + manifest` 入参，不再从 `runtime` 里偷大复合 `Result`。随后新增 [pin_runtime_host_smoke.cheng](/Users/lbcheng/cheng-lang/v3/src/tests/pin_runtime_host_smoke.cheng) 和 [pin_runtime_quic_smoke.cheng](/Users/lbcheng/cheng-lang/v3/src/tests/pin_runtime_quic_smoke.cheng)，真走 `publish offer -> sync fetch -> publish accept -> sync fetch -> request proof -> publish settlement -> sync fetch`。最后把新 smoke 接进 [run_v3_host_smokes.sh](/Users/lbcheng/cheng-lang/v3/tooling/run_v3_host_smokes.sh) 和 [run_v3_stage23_libp2p_smokes.sh](/Users/lbcheng/cheng-lang/v3/tooling/run_v3_stage23_libp2p_smokes.sh)，并把 [Pin托管.md](/Users/lbcheng/cheng-lang/v3/docs/Pin托管.md)、[内容平面.md](/Users/lbcheng/cheng-lang/v3/docs/内容平面.md)、[Unimaker内容发布与分发协议v1.md](/Users/lbcheng/cheng-lang/v3/docs/Unimaker内容发布与分发协议v1.md)、[README.md](/Users/lbcheng/cheng-lang/v3/docs/README.md)、[v3/tooling/README.md](/Users/lbcheng/cheng-lang/v3/tooling/README.md) 同步成“helper + smoke 已落地，但还不是通用远端 handler/账本”的真实口径。 |
| 文件 | `v3/src/chain/pin_runtime_host.cheng` `v3/src/chain/pin_runtime_host_quic.cheng` `v3/src/tests/pin_runtime_host_smoke.cheng` `v3/src/tests/pin_runtime_quic_smoke.cheng` `v3/tooling/run_v3_host_smokes.sh` `v3/tooling/run_v3_stage23_libp2p_smokes.sh` `v3/docs/Pin托管.md` `v3/docs/内容平面.md` `v3/docs/Unimaker内容发布与分发协议v1.md` `v3/docs/README.md` `v3/tooling/README.md` `task_plan.md` `progress.md` `findings.md` |
| 不做 | 不把 `Pin` 冒充成已经有通用远端 handler、随机信标或奖励账本；不把 `challenge/proof` 塞回 `PubSub`；不改现有 `content runtime` 主链语义。 |
| 验收 | [pin_runtime_host_smoke.cheng](/Users/lbcheng/cheng-lang/v3/src/tests/pin_runtime_host_smoke.cheng) 和 [pin_runtime_quic_smoke.cheng](/Users/lbcheng/cheng-lang/v3/src/tests/pin_runtime_quic_smoke.cheng) 必须前台输出 `ok`；完整 [run_v3_host_smokes.sh](/Users/lbcheng/cheng-lang/v3/tooling/run_v3_host_smokes.sh) 和 [run_v3_stage23_libp2p_smokes.sh](/Users/lbcheng/cheng-lang/v3/tooling/run_v3_stage23_libp2p_smokes.sh) 必须继续全绿。 |

## 当前任务

| 项目 | 内容 |
|---|---|
| 目标 | 把 [pin_plane_smoke.cheng](/Users/lbcheng/cheng-lang/v3/src/tests/pin_plane_smoke.cheng) 收成“纯 `Pin` 语义主段 + `offerFromArtifacts` 附段”，减少它对 `PubSub/topic` 和整块 `content artifacts` 形状的无关依赖，同时保证 host 与 `stage23` gate 继续全绿。 |
| 主线 | 这轮只改 [pin_plane_smoke.cheng](/Users/lbcheng/cheng-lang/v3/src/tests/pin_plane_smoke.cheng) 和记录文件。测试主段改成直接走 `v3PinOfferMake(...) -> Accept -> Challenge -> Proof -> Settlement`，用 `bundleCid + blobSummary + manifest` 直接锚定内容；原来 `offerFromArtifacts(...)` 那条接线保留成一个很小的附段，只断言 `artifacts -> offer` 和主段生成的 `offer` 在 `offerCid/payload` 上完全一致。这样既继续覆盖 `artifacts` 接线，又不再把 `Pin` gate 绑死在 `PubSub topic` 形状上。随后完整复跑 [run_v3_host_smokes.sh](/Users/lbcheng/cheng-lang/v3/tooling/run_v3_host_smokes.sh) 和 [run_v3_stage23_libp2p_smokes.sh](/Users/lbcheng/cheng-lang/v3/tooling/run_v3_stage23_libp2p_smokes.sh)。 |
| 文件 | `v3/src/tests/pin_plane_smoke.cheng` `task_plan.md` `progress.md` `findings.md` |
| 不做 | 不去改 `pin_plane` 协议字段；不把 `Pin` 重新接回 `PubSub`；不为了拆 smoke 去加新的运行时 wrapper。 |
| 验收 | `pin_plane_smoke` 在 host、`stage2`、`stage3` 下都必须前台输出 `v3 pin_plane_smoke ok`；完整 [run_v3_host_smokes.sh](/Users/lbcheng/cheng-lang/v3/tooling/run_v3_host_smokes.sh) 和 [run_v3_stage23_libp2p_smokes.sh](/Users/lbcheng/cheng-lang/v3/tooling/run_v3_stage23_libp2p_smokes.sh) 必须继续全绿。 |

## 当前任务

| 项目 | 内容 |
|---|---|
| 目标 | 一口气把 `Pin` 层收成可验的独立协议骨架：落 `offer/accept/challenge/proof/settlement` 五个对象、挑战/证明/结算校验、独立 smoke，并接进 host 与 `stage23` gate。 |
| 主线 | 这轮直接在 [pin_plane.cheng](/Users/lbcheng/cheng-lang/v3/src/chain/pin_plane.cheng) 新增 `V3PinOffer / Accept / Challenge / Proof / Settlement` 五个对象，固定 `bundleCid/blobSummaryCid/blobCid/manifestCid` 作为内容锚点，`challenge` 只从 `manifest` 抽样 shard，`proof` 直接回带被抽中的 shard payload，`settlement` 只做 `reward/slash` 语义判定。然后在 [pin_plane_smoke.cheng](/Users/lbcheng/cheng-lang/v3/src/tests/pin_plane_smoke.cheng) 真走 `offer <- artifacts -> accept -> challenge -> proof <- shard responses -> reward settlement / slash settlement` 两条闭环，并把这条 smoke 接进 [run_v3_host_smokes.sh](/Users/lbcheng/cheng-lang/v3/tooling/run_v3_host_smokes.sh) 和 [run_v3_stage23_libp2p_smokes.sh](/Users/lbcheng/cheng-lang/v3/tooling/run_v3_stage23_libp2p_smokes.sh)。文档侧新增 [Pin托管.md](/Users/lbcheng/cheng-lang/v3/docs/Pin托管.md)，同时把 [README.md](/Users/lbcheng/cheng-lang/v3/docs/README.md)、[内容平面.md](/Users/lbcheng/cheng-lang/v3/docs/内容平面.md)、[Unimaker内容发布与分发协议v1.md](/Users/lbcheng/cheng-lang/v3/docs/Unimaker内容发布与分发协议v1.md)、[纠删码群集.md](/Users/lbcheng/cheng-lang/v3/docs/纠删码群集.md)、[DAG可用性与公告.md](/Users/lbcheng/cheng-lang/v3/docs/DAG可用性与公告.md)、[cheng语言特性矩阵和开发计划.md](/Users/lbcheng/cheng-lang/v3/docs/cheng语言特性矩阵和开发计划.md) 同步成“`Pin` 已有对象和 smoke，但还没有 overlay/奖励账本接线”的真实口径。 |
| 文件 | `v3/src/chain/pin_plane.cheng` `v3/src/tests/pin_plane_smoke.cheng` `v3/tooling/run_v3_host_smokes.sh` `v3/tooling/run_v3_stage23_libp2p_smokes.sh` `v3/docs/Pin托管.md` `v3/docs/README.md` `v3/docs/内容平面.md` `v3/docs/Unimaker内容发布与分发协议v1.md` `v3/docs/纠删码群集.md` `v3/docs/DAG可用性与公告.md` `v3/docs/cheng语言特性矩阵和开发计划.md` `task_plan.md` `progress.md` `findings.md` |
| 不做 | 不把 `Pin` 硬塞进 `PubSub`、`erasure advert` 或 `content runtime cache`；不假装已经有随机信标、资金托管、链上记账或完整 `PoRep/PoST`。 |
| 验收 | `pin_plane_smoke` 必须在 host、`stage2`、`stage3` 下都前台输出 `v3 pin_plane_smoke ok`；完整 [run_v3_host_smokes.sh](/Users/lbcheng/cheng-lang/v3/tooling/run_v3_host_smokes.sh) 和 [run_v3_stage23_libp2p_smokes.sh](/Users/lbcheng/cheng-lang/v3/tooling/run_v3_stage23_libp2p_smokes.sh) 都必须继续全绿。 |

## 当前任务

| 项目 | 内容 |
|---|---|
| 目标 | 收掉 [content_stub_smoke.cheng](/Users/lbcheng/cheng-lang/v3/src/tests/content_stub_smoke.cheng) 的 `store manifest` 真故障，并把发布入库边界钉成“编码后立刻校验再入库”，保证 [run_v3_stage23_libp2p_smokes.sh](/Users/lbcheng/cheng-lang/v3/tooling/run_v3_stage23_libp2p_smokes.sh) 继续全绿。 |
| 主线 | 这轮先在 [content_stub_smoke.cheng](/Users/lbcheng/cheng-lang/v3/src/tests/content_stub_smoke.cheng) 把 `contentStubSmokeFetchPullAndRebuild(...)` 从“大 `artifacts` 按值传入后再重编码 manifest”收成更贴近真实网络面的形状：只传 `stub + params`，服务端 runtime 直接吃 `node.storeEntries[2].ingress.payload` 这份已经发布出去的 manifest payload，再走 `request manifest / request recovery plan / shard pull / rebuild` 真链。随后在 [libp2p_bridge.cheng](/Users/lbcheng/cheng-lang/v3/src/overlay/libp2p_bridge.cheng) 把 `manifest/recovery plan` 两条发布路径都收成“先 `Encode`，立刻 `ValidatePayload`，通过后再 `StorePut`”。这样一来，既不再依赖 smoke helper 里那份脱离发布面的对象重编码，也把发布边界的脏 payload 直接拦死。 |
| 文件 | `v3/src/tests/content_stub_smoke.cheng` `v3/src/overlay/libp2p_bridge.cheng` `task_plan.md` `progress.md` `findings.md` |
| 不做 | 不把这次 manifest 失败硬塞成编译器问题；不回退到跳过 manifest cache；不保留“编码完直接入库、靠后面 smoke 才发现 payload 脏了”的发布边界。 |
| 验收 | `/tmp/content_stub_smoke.host.publish_guard`、`/tmp/content_stub_smoke.stage2.publish_guard`、`/tmp/content_stub_smoke.stage3.publish_guard` 都必须前台输出 `v3 content_stub_smoke ok`；完整 [run_v3_stage23_libp2p_smokes.sh](/Users/lbcheng/cheng-lang/v3/tooling/run_v3_stage23_libp2p_smokes.sh) 必须继续输出 `v3 stage2/stage3 libp2p: ok`。 |

## 当前任务

| 项目 | 内容 |
|---|---|
| 目标 | 一口气补齐内容平面最后三块硬功能：`GET_PREVIEW`、`GET_RANGE`、真实 parity rebuild，并把内容链文档和 gate 一起收口。 |
| 主线 | 这轮已经把 [content_fetch.cheng](/Users/lbcheng/cheng-lang/v3/src/chain/content_fetch.cheng) 和 [content_runtime.cheng](/Users/lbcheng/cheng-lang/v3/src/chain/content_runtime.cheng) 收成正式四接口：`GET_PREVIEW` 现在固定返回 encoded `V3ContentStub`，不新造 preview 对象；`GET_RANGE` 直接锚定 `blobCid`，返回原始 blob 的精确 byte slice；`content runtime` 新增 `previewCache/blobCache`，并且 [erasure_swarm.cheng](/Users/lbcheng/cheng-lang/v3/src/chain/erasure_swarm.cheng) 已补 `v3ErasureEncodeShardPayloads(...)`、`v3ErasureRebuildBlob(...)`、`v3ErasureRebuildBlobFromResponses(...)`，把 systematic RS 编码和 parity 重建真的落到代码里。随后 [content_runtime_smoke.cheng](/Users/lbcheng/cheng-lang/v3/src/tests/content_runtime_smoke.cheng) 已补 preview fetch、range fetch 和 parity 场景；[content_quic_smoke.cheng](/Users/lbcheng/cheng-lang/v3/src/tests/content_quic_smoke.cheng) 保持 QUIC 内容协议主验证，同时 [native_runtime.cheng](/Users/lbcheng/cheng-lang/v3/src/quic/native_runtime.cheng) 已把 `DialPumpReady` 和 `PipeRead` 从无限阻塞收成 `5s` 有界非阻塞泵，失败直接返回错误，不再允许 loopback 握手挂死总 gate。最后文档侧重写 [内容平面.md](/Users/lbcheng/cheng-lang/v3/docs/内容平面.md)、[纠删码群集.md](/Users/lbcheng/cheng-lang/v3/docs/纠删码群集.md)、[Unimaker内容发布与分发协议v1.md](/Users/lbcheng/cheng-lang/v3/docs/Unimaker内容发布与分发协议v1.md)、[发布订阅.md](/Users/lbcheng/cheng-lang/v3/docs/发布订阅.md)，清掉旧愿景长文，统一成当前协议口径。 |
| 文件 | `v3/src/chain/erasure_swarm.cheng` `v3/src/chain/content_runtime.cheng` `v3/src/chain/content_fetch.cheng` `v3/src/quic/native_runtime.cheng` `v3/src/tests/content_codec_smoke.cheng` `v3/src/tests/erasure_swarm_smoke.cheng` `v3/src/tests/content_runtime_smoke.cheng` `v3/src/tests/content_quic_smoke.cheng` `v3/docs/内容平面.md` `v3/docs/纠删码群集.md` `v3/docs/Unimaker内容发布与分发协议v1.md` `v3/docs/发布订阅.md` `v3/docs/README.md` `task_plan.md` `progress.md` `findings.md` |
| 不做 | 不引入新的 preview 类型；不把 `Pin/RWAD/托管挑战` 写成已实现；不把 QUIC smoke 变成多次重握手性能基准。 |
| 验收 | [content_codec_smoke.cheng](/Users/lbcheng/cheng-lang/v3/src/tests/content_codec_smoke.cheng)、[erasure_swarm_smoke.cheng](/Users/lbcheng/cheng-lang/v3/src/tests/erasure_swarm_smoke.cheng)、[content_runtime_smoke.cheng](/Users/lbcheng/cheng-lang/v3/src/tests/content_runtime_smoke.cheng)、[content_quic_smoke.cheng](/Users/lbcheng/cheng-lang/v3/src/tests/content_quic_smoke.cheng)、[content_stub_smoke.cheng](/Users/lbcheng/cheng-lang/v3/src/tests/content_stub_smoke.cheng) 都必须前台通过；[run_v3_host_smokes.sh](/Users/lbcheng/cheng-lang/v3/tooling/run_v3_host_smokes.sh) 和 [run_v3_stage23_libp2p_smokes.sh](/Users/lbcheng/cheng-lang/v3/tooling/run_v3_stage23_libp2p_smokes.sh) 必须继续全绿。 |

## 当前任务

| 项目 | 内容 |
|---|---|
| 目标 | 把 host 基础 gate 真前移进总 gate，同时收掉 fresh `bootstrap` 暴露出来的 [handshake13.cheng](/Users/lbcheng/cheng-lang/v3/src/quic/tls/handshake13.cheng) `CertificateVerify` 分发句型坑，保证整条 [run_slice_gate.sh](/Users/lbcheng/cheng-lang/v3/tooling/run_slice_gate.sh) 再次全绿。 |
| 主线 | 这轮实际做了两件事：1）把 [run_v3_host_smokes.sh](/Users/lbcheng/cheng-lang/v3/tooling/run_v3_host_smokes.sh) 收成支持定向 smoke，并把 `fixed256_sha256_smoke`、`default_init_literals_smoke` 从默认列表移出；[run_slice_gate.sh](/Users/lbcheng/cheng-lang/v3/tooling/run_slice_gate.sh) 改成在 `build-backend-driver` 之后单独前置这两条基础 gate，不再新造 foundation runner。2）fresh `bootstrap bridge` 真暴露出 [msquicTls13VerifyCertificateSignature(...)](/Users/lbcheng/cheng-lang/v3/src/quic/tls/handshake13.cheng#L2209) 的实根：函数只要被写成 `else: if` 或 `if/elif/else`，`stage2` 就会在这里报 `stmt_if` 或直接把返回值写坏。最终保留下来的稳定形状是 `verifyRes` 默认先放 `Err("tls13: unsupported signature scheme")`，随后三条并列 `if` 分别覆盖，再统一 `return verifyRes`。按这条扁平形状收口后，fresh `stage2` 的 [tls_initial_packet_roundtrip_smoke.cheng](/Users/lbcheng/cheng-lang/v3/src/tests/tls_initial_packet_roundtrip_smoke.cheng) 和 [quic_transport_loopback_smoke.cheng](/Users/lbcheng/cheng-lang/v3/src/tests/quic_transport_loopback_smoke.cheng) 都重新真编真跑通过，最终完整 [run_slice_gate.sh](/Users/lbcheng/cheng-lang/v3/tooling/run_slice_gate.sh) 也收尾到 `[v3 gate] ok`。 |
| 文件 | `v3/tooling/run_v3_host_smokes.sh` `v3/tooling/run_slice_gate.sh` `v3/tooling/README.md` `v3/src/quic/tls/handshake13.cheng` `task_plan.md` `progress.md` `findings.md` |
| 不做 | 不新造 host foundation runner；不把 `ref10_ashr`、`fixedbytes32_seq_index` 跟着硬前移；不继续试 `if/elif/else` 或 `else: if` 这种会把 `stage2` 重新打坏的签名分发形状。 |
| 验收 | `BACKEND_INCREMENTAL=0 BACKEND_MULTI_MODULE_CACHE=0 sh /Users/lbcheng/cheng-lang/v3/tooling/run_v3_host_smokes.sh fixed256_sha256_smoke default_init_literals_smoke` 已前台通过；默认 [run_v3_host_smokes.sh](/Users/lbcheng/cheng-lang/v3/tooling/run_v3_host_smokes.sh) 已继续输出 `v3 host smokes: ok`；fresh `stage2` 的 [tls_initial_packet_roundtrip_smoke.cheng](/Users/lbcheng/cheng-lang/v3/src/tests/tls_initial_packet_roundtrip_smoke.cheng) 和 [quic_transport_loopback_smoke.cheng](/Users/lbcheng/cheng-lang/v3/src/tests/quic_transport_loopback_smoke.cheng) 都已前台 `ok`；完整 [run_slice_gate.sh](/Users/lbcheng/cheng-lang/v3/tooling/run_slice_gate.sh) 已前台输出 `[v3 gate] ok`。 |

## 当前任务

| 项目 | 内容 |
|---|---|
| 目标 | 继续把 [handshake13.cheng](/Users/lbcheng/cheng-lang/v3/src/quic/tls/handshake13.cheng) 的 `chain/ocsp/certverify/finished` 热路径收成最小叶子 helper，但不碰 [msquicTls13ParseCertificate(...)](/Users/lbcheng/cheng-lang/v3/src/quic/tls/handshake13.cheng) 主循环。 |
| 主线 | 这轮实际落地了四类最小收口：`msquicTls13PeerVerifyRunChain(...)` 收平 `x509VerifyChain(...)`；`msquicTls13PeerVerifySetChainVerified/SetCertVerifyOk/SetFinishedOk/SetRevocationMode/SetOcspResponse` 五个单字段 setter 去掉 `peer` 深字段热路径直写；`msquicTls13ApplyCertificateEntryExtension(...)` 只处理单条证书扩展并把 `OCSP` 写回交给 setter；已有的 [msquicTls13PeerVerifyRunRevocation(...)](/Users/lbcheng/cheng-lang/v3/src/quic/tls/handshake13.cheng) 继续保持“只包长调用，不包控制流”。随后先用 `/tmp/quic_transport_loopback_smoke.stage2.chainhelper` 做最短真链，再完整重跑 [run_v3_stage23_libp2p_smokes.sh](/Users/lbcheng/cheng-lang/v3/tooling/run_v3_stage23_libp2p_smokes.sh)，最终再次收尾到 `v3 stage2/stage3 libp2p: ok`。 |
| 文件 | `v3/src/quic/tls/handshake13.cheng` `task_plan.md` `progress.md` `findings.md` |
| 不做 | 不把 `ParseCertificate` 的 `ctx/list header`、entry dispatch 再 helper 化；不动 reset/init 里的 `peer` 直写。 |
| 验收 | `/tmp/quic_transport_loopback_smoke.stage2.chainhelper` 必须输出 `ok`；`sh /Users/lbcheng/cheng-lang/v3/tooling/run_v3_stage23_libp2p_smokes.sh` 必须输出 `v3 stage2/stage3 libp2p: ok`。 |

## 当前任务

| 项目 | 内容 |
|---|---|
| 目标 | 把 host 侧门禁按“最短脚本路径”拆层：不新增 runner，只让 [run_v3_host_smokes.sh](/Users/lbcheng/cheng-lang/v3/tooling/run_v3_host_smokes.sh) 支持定向 smoke，再把 `fixed256_sha256` 和 `default_init_literals` 两条最值钱的基础 smoke 从默认列表里前移到 [run_slice_gate.sh](/Users/lbcheng/cheng-lang/v3/tooling/run_slice_gate.sh) 的前置 gate。 |
| 主线 | 这轮按“最小改动、最早失败”收口：`stage23` 继续只看 `stage2/stage3` 的协议主链，并保留 [fixed256_curve25519_smoke.cheng](/Users/lbcheng/cheng-lang/v3/src/tests/fixed256_curve25519_smoke.cheng) 当 `TLS13 X25519` 前哨；host 侧不再新建基础 runner，而是让 [run_v3_host_smokes.sh](/Users/lbcheng/cheng-lang/v3/tooling/run_v3_host_smokes.sh) 支持 `sh run_v3_host_smokes.sh smoke_a smoke_b`，同时把 `fixed256_sha256_smoke` 和 `default_init_literals_smoke` 从默认列表里移出。最后 [run_slice_gate.sh](/Users/lbcheng/cheng-lang/v3/tooling/run_slice_gate.sh) 改成在 `build-backend-driver` 之后单独跑这两条基础 gate，再进 `stage23` 和默认 host 业务链。 |
| 文件 | `v3/tooling/run_v3_host_smokes.sh` `v3/tooling/run_slice_gate.sh` `v3/tooling/README.md` `task_plan.md` `progress.md` `findings.md` |
| 不做 | 不新抄一份 host foundation runner；不把 `ref10_ashr` 和 `fixedbytes32_seq_index` 也跟着硬前移；不把基础 smoke 再塞回 `stage23 libp2p`。 |
| 验收 | `sh /Users/lbcheng/cheng-lang/v3/tooling/run_v3_host_smokes.sh fixed256_sha256_smoke default_init_literals_smoke` 必须前台通过；收口后的默认 [run_v3_host_smokes.sh](/Users/lbcheng/cheng-lang/v3/tooling/run_v3_host_smokes.sh) 必须继续输出 `v3 host smokes: ok`；改动后的 [run_slice_gate.sh](/Users/lbcheng/cheng-lang/v3/tooling/run_slice_gate.sh) 至少要能把这两条新增前置 gate 跑通，不得出现脚本或日志路径错误。 |

## 当前任务

| 项目 | 内容 |
|---|---|
| 目标 | 把 `stage23` runner 的门禁边界收准：只把真正贴着 `QUIC/TLS/libp2p` 主链的 [fixed256_curve25519_smoke.cheng](/Users/lbcheng/cheng-lang/v3/src/tests/fixed256_curve25519_smoke.cheng) 接进 [run_v3_stage23_libp2p_smokes.sh](/Users/lbcheng/cheng-lang/v3/tooling/run_v3_stage23_libp2p_smokes.sh)，同时把 [fixed256_sha256_smoke.cheng](/Users/lbcheng/cheng-lang/v3/src/tests/fixed256_sha256_smoke.cheng) 和 [default_init_literals_smoke.cheng](/Users/lbcheng/cheng-lang/v3/src/tests/default_init_literals_smoke.cheng) 留在 host/定向验证层，不污染 `stage23` 主 gate。 |
| 主线 | 这轮先对 `fixed256_sha256 / fixed256_curve25519 / default_init_literals` 做了 fresh `stage2 + stage3` targeted 真编真跑，三条都坐实可过；随后按主链相关性收 runner：保留 `curve25519`，撤回 `sha256` 和 `default_init_literals`。理由也写死：`curve25519` 是 `TLS13 X25519` 的定点底座，值当放进 `stage23`；`sha256` 已被 `TLS initial/finished/content` 多条链反复覆盖，再单独塞进主 gate 只会重复；`default_init_literals` 属于通用语言语义，不该挂在 libp2p 协议 runner 上冒充协议前置。最终完整 [run_v3_stage23_libp2p_smokes.sh](/Users/lbcheng/cheng-lang/v3/tooling/run_v3_stage23_libp2p_smokes.sh) 必须继续输出 `v3 stage2/stage3 libp2p: ok`。 |
| 文件 | `v3/tooling/run_v3_stage23_libp2p_smokes.sh` `task_plan.md` `progress.md` `findings.md` |
| 不做 | 不把“能过”误当“该进主 gate”；不把通用编译器/语义 smoke 混进 libp2p 专项 runner；不为了追求覆盖率把重复价值很低的 `sha256` 门禁硬塞进去。 |
| 验收 | `stage2/stage3` targeted 的 [fixed256_curve25519_smoke.cheng](/Users/lbcheng/cheng-lang/v3/src/tests/fixed256_curve25519_smoke.cheng)、[fixed256_sha256_smoke.cheng](/Users/lbcheng/cheng-lang/v3/src/tests/fixed256_sha256_smoke.cheng)、[default_init_literals_smoke.cheng](/Users/lbcheng/cheng-lang/v3/src/tests/default_init_literals_smoke.cheng) 都已前台 `ok`；完整 [run_v3_stage23_libp2p_smokes.sh](/Users/lbcheng/cheng-lang/v3/tooling/run_v3_stage23_libp2p_smokes.sh) 必须继续前台输出 `v3 stage2/stage3 libp2p: ok`。 |

## 当前任务

| 项目 | 内容 |
|---|---|
| 目标 | 继续把 [handshake13.cheng](/Users/lbcheng/cheng-lang/v3/src/quic/tls/handshake13.cheng) 里最脆的两条热路径收平：`certificate entry extension` 的 `var Bytes` 写回，以及 `revocation verify` 的超长调用，同时保证 `stage2/stage3` 正式 gate 继续全绿。 |
| 主线 | 这轮只改 [handshake13.cheng](/Users/lbcheng/cheng-lang/v3/src/quic/tls/handshake13.cheng) 的叶子形状，不碰 [msquicTls13ParseCertificate(...)](/Users/lbcheng/cheng-lang/v3/src/quic/tls/handshake13.cheng) 主循环：新增 `msquicTls13PeerVerifySetOcspResponse(...)` 和 [msquicTls13ApplyCertificateEntryExtension(...)](/Users/lbcheng/cheng-lang/v3/src/quic/tls/handshake13.cheng)，把 `status_request -> OCSP -> peerOcspResponse` 这条循环内 `Bytes` 写回收成单条扩展 helper；同时新增 [msquicTls13PeerVerifyRunRevocation(...)](/Users/lbcheng/cheng-lang/v3/src/quic/tls/handshake13.cheng) 收平 `x509VerifyRevocation(...)` 超长调用，并补 `SetChainVerified/SetCertVerifyOk/SetFinishedOk/SetRevocationMode` 四个单字段 setter，去掉热路径里的 `peer` 深字段直写。期间完整 [run_v3_stage23_libp2p_smokes.sh](/Users/lbcheng/cheng-lang/v3/tooling/run_v3_stage23_libp2p_smokes.sh) 一度在 `quic_transport_loopback_smoke.stage2` 上冒出一次 runner 毛刺，但同一产物手动真跑 `ok`，随后整套 gate 二次重跑再次收尾到 `v3 stage2/stage3 libp2p: ok`。 |
| 文件 | `v3/src/quic/tls/handshake13.cheng` `task_plan.md` `progress.md` `findings.md` |
| 不做 | 不把 `ParseCertificate` 的 `ctx/list header` 和 entry dispatch 再 helper 化；这两条已经坐实会改坏 `stage2` 运行时语义。 |
| 验收 | `sh /Users/lbcheng/cheng-lang/v3/tooling/run_v3_stage23_libp2p_smokes.sh` 必须输出 `v3 stage2/stage3 libp2p: ok`；`artifacts/v3_stage23_libp2p/quic_transport_loopback_smoke.stage2` 手动真跑也必须输出 `ok`。 |

## 当前任务

| 项目 | 内容 |
|---|---|
| 目标 | 继续把 [handshake13.cheng](/Users/lbcheng/cheng-lang/v3/src/quic/tls/handshake13.cheng) 的 `peer` 深字段写回收成叶子 setter，同时把这轮在 `ParseCertificate` 上探出来的 `stage2` 运行时边界明确记住，不再把“编过了”误当“真跑稳了”。 |
| 主线 | 这轮保留下来的净改动只有一类：补了 `msquicTls13PeerVerifySetChainVerified(...)`、`msquicTls13PeerVerifySetCertVerifyOk(...)`、`msquicTls13PeerVerifySetFinishedOk(...)`、`msquicTls13PeerVerifySetRevocationMode(...)`，并把 `peerCertVerified/certVerifyOk/finishedOk/revocationMode` 的热路径直写改成单字段 setter。中途我并行探了 `ParseCertificate` 的 `ctx/list header` helper 化和 entry dispatch helper 化，但 `stage2` 真跑先后暴露出 `tls13: certificate empty`、`tls13: certificate missing` 两个运行时语义偏移，所以这些 helper 形状已经全部撤回，`ParseCertificate` 重新落回已验证稳定的内联主线。最终 `/tmp/quic_transport_loopback_smoke.stage2.peer_setters` 真跑 `ok`，完整 [run_v3_stage23_libp2p_smokes.sh](/Users/lbcheng/cheng-lang/v3/tooling/run_v3_stage23_libp2p_smokes.sh) 再次收尾到 `v3 stage2/stage3 libp2p: ok`。 |
| 文件 | `v3/src/quic/tls/handshake13.cheng` `task_plan.md` `progress.md` `findings.md` |
| 不做 | 不把 `ParseCertificate` 那两条已经坐实会改坏 `stage2` 运行时语义的 helper 继续留在树里；不只看 compile 绿就算过；不把瞬时 `Killed: 9` 直接记成代码回归。 |
| 验收 | `/tmp/quic_transport_loopback_smoke.stage2.peer_setters` 必须输出 `ok`；`sh /Users/lbcheng/cheng-lang/v3/tooling/run_v3_stage23_libp2p_smokes.sh` 必须继续输出 `v3 stage2/stage3 libp2p: ok`。 |

## 当前任务

| 项目 | 内容 |
|---|---|
| 目标 | 继续把 [handshake13.cheng](/Users/lbcheng/cheng-lang/v3/src/quic/tls/handshake13.cheng) 的证书尾段和 `peer` 状态写回收成更朴素的 Cheng 形状，同时保证 `stage2/stage3` 正式 gate 和 host `QUIC/libp2p` 真跑继续全绿。 |
| 主线 | 这轮主改只落在 [handshake13.cheng](/Users/lbcheng/cheng-lang/v3/src/quic/tls/handshake13.cheng)：补了 `msquicTls13PeerVerifySetChainVerified(...)`、`msquicTls13PeerVerifySetFinishedOk(...)`、`msquicTls13PeerVerifySetRevocationMode(...)`、`msquicTls13PeerVerifyEnsureTrustRoots(...)`、`msquicTls13PeerVerifyCommitCertificateVerify(...)`，把 `trustRoots` 构建、`certVerifyOk/finishedOk/peerCertVerified` 写回和 `CertificateVerify` 提交链都收进叶子 helper；同时把 `ParseCertificateEntryExtensions` 里的 `OCSP` 写回改成 [msquicTls13PeerVerifySetOcspResponse(...)](/Users/lbcheng/cheng-lang/v3/src/quic/tls/handshake13.cheng) 单点处理，空 `OCSP` 过滤也挪进 setter，本体不再保留 `while` 里的双层 `if + var Bytes` 写回。串行真验后，`/tmp/tls_initial_packet_roundtrip_smoke.stage3.peerhelpers2`、`/tmp/quic_transport_loopback_smoke.host.peerhelpers2`、`/tmp/libp2p_quic_tls_smoke.host.peerhelpers2` 都输出 `ok`，完整 [run_v3_stage23_libp2p_smokes.sh](/Users/lbcheng/cheng-lang/v3/tooling/run_v3_stage23_libp2p_smokes.sh) 也继续收尾到 `v3 stage2/stage3 libp2p: ok`。 |
| 文件 | `v3/src/quic/tls/handshake13.cheng` `task_plan.md` `progress.md` `findings.md` |
| 不做 | 不回退成整块 `peer` helper；不保留 `while` 里的双层 `if + var Bytes` 写回形状；不并发跑会撞固定 UDP 端口的 host QUIC smoke。 |
| 验收 | `/tmp/tls_initial_packet_roundtrip_smoke.stage3.peerhelpers2`、`/tmp/quic_transport_loopback_smoke.host.peerhelpers2`、`/tmp/libp2p_quic_tls_smoke.host.peerhelpers2` 必须输出 `ok`；`sh /Users/lbcheng/cheng-lang/v3/tooling/run_v3_stage23_libp2p_smokes.sh` 必须继续输出 `v3 stage2/stage3 libp2p: ok`。 |

## 当前任务

| 项目 | 内容 |
|---|---|
| 目标 | 收掉 `stage23` 里偶发的 QUIC 固定端口冲突，并把已验证稳定的 `chain_codec_binary / location_proof / anti_entropy / lsmr_bagua_prefix_tree` 正式接进 [run_v3_stage23_libp2p_smokes.sh](/Users/lbcheng/cheng-lang/v3/tooling/run_v3_stage23_libp2p_smokes.sh)，同时把 `handshake13 peer verify` 这条同类复合实参 blocker 一并压平。 |
| 主线 | 这轮先在 [native_runtime.cheng](/Users/lbcheng/cheng-lang/v3/src/quic/native_runtime.cheng) 把 `datapath` 绑定后的 `udp://host:port` 真地址转回 multiaddr，再由 [msquictransport_native.cheng](/Users/lbcheng/cheng-lang/v3/src/quic/msquictransport_native.cheng) 和 [quic_transport.cheng](/Users/lbcheng/cheng-lang/v3/src/libp2p/transports/quic_transport.cheng) 往上回填真实 bound addr。随后把 [quic_transport_loopback_smoke.cheng](/Users/lbcheng/cheng-lang/v3/src/tests/quic_transport_loopback_smoke.cheng)、[libp2p_quic_tls_smoke.cheng](/Users/lbcheng/cheng-lang/v3/src/tests/libp2p_quic_tls_smoke.cheng)、[content_quic_smoke.cheng](/Users/lbcheng/cheng-lang/v3/src/tests/content_quic_smoke.cheng) 全改成 `udp/0`，不再写死端口；再把 [handshake13.cheng](/Users/lbcheng/cheng-lang/v3/src/quic/tls/handshake13.cheng) 的 `peer verify` 写回链从“整块 `peer` helper”收成字段直写；最后把四条新 smoke 接进 `stage23` runner 并跑完整 gate。 |
| 文件 | `v3/src/quic/native_runtime.cheng` `v3/src/quic/msquictransport_native.cheng` `v3/src/libp2p/transports/quic_transport.cheng` `v3/src/quic/tls/handshake13.cheng` `v3/src/tests/quic_transport_loopback_smoke.cheng` `v3/src/tests/libp2p_quic_tls_smoke.cheng` `v3/src/tests/content_quic_smoke.cheng` `v3/tooling/run_v3_stage23_libp2p_smokes.sh` `task_plan.md` `progress.md` `findings.md` |
| 不做 | 不再换别的固定端口；不靠 runner 重试或延时掩盖端口冲突；不把还卡 ordinary 的 [anti_entropy_signature_fields_smoke.cheng](/Users/lbcheng/cheng-lang/v3/src/tests/anti_entropy_signature_fields_smoke.cheng) 硬塞进 `stage23`。 |
| 验收 | `/tmp/quic_transport_loopback_smoke.port0b`、`/tmp/libp2p_quic_tls_smoke.port0b`、`/tmp/content_quic_smoke.port0c` 必须输出 `ok`；`location_proof` 和 `lsmr_bagua_prefix_tree` 的 `stage2/stage3` targeted 必须都输出 `ok`；完整 [run_v3_stage23_libp2p_smokes.sh](/Users/lbcheng/cheng-lang/v3/tooling/run_v3_stage23_libp2p_smokes.sh) 必须继续输出 `v3 stage2/stage3 libp2p: ok`。 |

## 当前任务

| 项目 | 内容 |
|---|---|
| 目标 | 继续把 [handshake13.cheng](/Users/lbcheng/cheng-lang/v3/src/quic/tls/handshake13.cheng) 的扩展解析链压成更朴素的 Cheng 形状，同时保持 `QUIC/TLS` 与整套 `stage23` gate 真绿；下一刀直接转 [msquicTls13ParseCertificate(...)](/Users/lbcheng/cheng-lang/v3/src/quic/tls/handshake13.cheng#L1846)。 |
| 主线 | 这轮已经把 `key_share` 从旧的双路径混合函数拆成 [msquicTls13ParseClientKeyShareList(...)](/Users/lbcheng/cheng-lang/v3/src/quic/tls/handshake13.cheng) 和 [msquicTls13ParseServerKeyShare(...)](/Users/lbcheng/cheng-lang/v3/src/quic/tls/handshake13.cheng) 两条线；再把 [msquicTls13DispatchExtension(...)](/Users/lbcheng/cheng-lang/v3/src/quic/tls/handshake13.cheng) 拆成 `NegotiationExtension / MetadataExtension` 两层；最后把 [msquicTls13ParseExtensions(...)](/Users/lbcheng/cheng-lang/v3/src/quic/tls/handshake13.cheng) 收成 `ParseOneExtension + FinalizeExtensions`。串行 fresh 跑 `/tmp/tls_client_hello_parse_smoke.stage2.extsplit`、`/tmp/tls_initial_packet_roundtrip_smoke.stage2.extsplit`、`/tmp/quic_transport_loopback_smoke.host.extsplit`、`/tmp/libp2p_quic_tls_smoke.host.extsplit` 都已经输出 `ok`；随后完整 [run_v3_stage23_libp2p_smokes.sh](/Users/lbcheng/cheng-lang/v3/tooling/run_v3_stage23_libp2p_smokes.sh) 也再次收尾到 `v3 stage2/stage3 libp2p: ok`。 |
| 文件 | `v3/src/quic/tls/handshake13.cheng` `task_plan.md` `progress.md` `findings.md` |
| 不做 | 不改 `ClientHello/ServerHello/EncryptedExtensions` 的 ctx 入口语义；不动未知扩展放行规则；不并发跑固定端口的 host QUIC smoke。 |
| 验收 | `/tmp/tls_client_hello_parse_smoke.stage2.extsplit`、`/tmp/tls_initial_packet_roundtrip_smoke.stage2.extsplit`、`/tmp/quic_transport_loopback_smoke.host.extsplit`、`/tmp/libp2p_quic_tls_smoke.host.extsplit` 必须输出 `ok`；`sh /Users/lbcheng/cheng-lang/v3/tooling/run_v3_stage23_libp2p_smokes.sh` 必须继续输出 `v3 stage2/stage3 libp2p: ok`。 |

## 当前任务

| 项目 | 内容 |
|---|---|
| 目标 | 把 `content_stub` 上层真正切到新的 manifest fetch 正式面，并补上一条 direct `GET_CHUNK` 端到端覆盖，避免内容协议只在兼容 `RequestShardPull` 上间接通过。 |
| 主线 | 这轮继续沿 [content_runtime_host.cheng](/Users/lbcheng/cheng-lang/v3/src/chain/content_runtime_host.cheng) 和 [content_runtime_host_quic.cheng](/Users/lbcheng/cheng-lang/v3/src/chain/content_runtime_host_quic.cheng) 新增 `v3ContentRuntimeRequestManifestFill(...)`，不再让 helper 直接返回 `Result[Manifest]`；同时把 [content_stub_smoke.cheng](/Users/lbcheng/cheng-lang/v3/src/tests/content_stub_smoke.cheng) 的 manifest 获取从旧 `sync/store` 路径改成 `content fetch + Fill`，而 advert/recoveryPlan 继续走 `sync`。另外在 [content_runtime_smoke.cheng](/Users/lbcheng/cheng-lang/v3/src/tests/content_runtime_smoke.cheng) 新增 direct `GET_CHUNK` 请求，正式锁死 `v3ContentFetchRequestChunk(...) -> v3ContentRuntimeRequestFetch(...) -> ...ToShardPullFill(...)` 这条 TCP 数据面。 |
| 文件 | `v3/src/chain/content_runtime_host.cheng` `v3/src/chain/content_runtime_host_quic.cheng` `v3/src/tests/content_stub_smoke.cheng` `v3/src/tests/content_runtime_smoke.cheng` `task_plan.md` `progress.md` `findings.md` |
| 不做 | 不把 advert/recoveryPlan 也硬切到 content 协议；不再引入 `Result[大复合对象]` 的新 helper；不靠兼容 `RequestShardPull` 间接证明 direct `GET_CHUNK`。 |
| 验收 | `/tmp/content_runtime_smoke.fetch13`、`/tmp/content_stub_smoke.fetch13`、`/tmp/content_quic_smoke.fetch13` 必须真跑 `ok`；随后 `git diff --check` 必须通过。 |

## 当前任务

| 项目 | 内容 |
|---|---|
| 目标 | 把 `chunk_fetch` 这条正式补齐到可稳定验收：`GET_MANIFEST / GET_CHUNK` 真闭环，`GET_PREVIEW / GET_RANGE` 明确返回未实现错误，同时把内容面从“直接返回大复合 Result”收成 `Fill/Into` 稳定口径。 |
| 主线 | 这轮已新增 [content_fetch.cheng](/Users/lbcheng/cheng-lang/v3/src/chain/content_fetch.cheng)，正式定义 `manifest/preview/chunk/range` 四种 fetch method、请求/响应编解码、CID 规则，以及新的 `v3ContentFetchResponseToManifestFill(...) / v3ContentFetchResponseToShardPullFill(...)`。同时把 [content_runtime.cheng](/Users/lbcheng/cheng-lang/v3/src/chain/content_runtime.cheng)、[content_runtime_host.cheng](/Users/lbcheng/cheng-lang/v3/src/chain/content_runtime_host.cheng)、[content_runtime_host_quic.cheng](/Users/lbcheng/cheng-lang/v3/src/chain/content_runtime_host_quic.cheng) 全部改成“先落本地值，再走 Fill”，不再跨 ABI 边界直接返回 `manifest/shard response` 这类大对象；对应 smoke 也已改成同口径。 |
| 文件 | `v3/src/chain/content_fetch.cheng` `v3/src/chain/content_runtime.cheng` `v3/src/chain/content_runtime_host.cheng` `v3/src/chain/content_runtime_host_quic.cheng` `v3/src/tests/content_codec_smoke.cheng` `v3/src/tests/content_runtime_smoke.cheng` `v3/src/tests/content_quic_smoke.cheng` `task_plan.md` `progress.md` `findings.md` |
| 不做 | 不把 `GET_PREVIEW / GET_RANGE` 伪装成已实现；不把 manifest/shard 大对象继续塞进脆弱的 `Result[T]` 返回链；不回退到“测试里手工 decode 绕过 helper”的假闭环。 |
| 验收 | `/tmp/content_codec_smoke.fetch11`、`/tmp/content_runtime_smoke.fetch11`、`/tmp/content_quic_smoke.fetch11`、`/tmp/content_stub_smoke.fetch11` 都必须输出 `ok`；随后 `git diff --check` 必须通过。 |

## 当前任务

| 项目 | 内容 |
|---|---|
| 目标 | 继续把 `handshake13` 的大复合状态和扩展解析链拆薄，同时保持 `QUIC/TLS` 的 `stage2/stage3` 正式 gate 与 host 端到端烟都真绿。 |
| 主线 | 这轮已经继续落在 [handshake13.cheng](/Users/lbcheng/cheng-lang/v3/src/quic/tls/handshake13.cheng) 和 [curve25519.cheng](/Users/lbcheng/cheng-lang/src/std/crypto/curve25519.cheng) 两处 Cheng 源码：先把 `initMsQuicTls13HandshakeStateInto(...)` 后面的深层写回拆成 `wire/messages/secrets` 三个 `Into` helper，再补 `generateCurve25519KeyPairInto(...)`，让 [msquicTls13HandshakeGenerateX25519](/Users/lbcheng/cheng-lang/v3/src/quic/tls/handshake13.cheng) 直接走 `priv/pub` 标量 bytes 和现成的 `msquicTls13HandshakeSetLocalKeyShare(...)`，最后把 [msquicTls13ParseExtensions](/Users/lbcheng/cheng-lang/v3/src/quic/tls/handshake13.cheng) 正式拆成 `msquicTls13ReadNextExtension(...) + msquicTls13DispatchExtension(...)` 两层。真验收也已经补齐：串行 fresh 跑 `stage2 tls_client_hello_parse_smoke`、`stage2 tls_initial_packet_roundtrip_smoke`、host `quic_transport_loopback_smoke`、host `libp2p_quic_tls_smoke` 全部输出 `ok`；随后完整重跑 [run_v3_stage23_libp2p_smokes.sh](/Users/lbcheng/cheng-lang/v3/tooling/run_v3_stage23_libp2p_smokes.sh) 继续以 `v3 stage2/stage3 libp2p: ok` 收尾。 |
| 文件 | `v3/src/quic/tls/handshake13.cheng` `src/std/crypto/curve25519.cheng` `task_plan.md` `progress.md` `findings.md` |
| 不做 | 不把 `QUIC/TLS` 的 Cheng 热路径回退成 runtime/C 捷径；不并发编同一条 smoke；不并行跑会撞固定 UDP 端口的 host QUIC smoke。 |
| 验收 | `/tmp/tls_client_hello_parse_smoke.stage2.parseext`、`/tmp/tls_initial_packet_roundtrip_smoke.stage2.parseext`、`/tmp/quic_transport_loopback_smoke.host.parseext`、`/tmp/libp2p_quic_tls_smoke.host.parseext` 都必须输出 `ok`；`sh /Users/lbcheng/cheng-lang/v3/tooling/run_v3_stage23_libp2p_smokes.sh` 必须继续输出 `v3 stage2/stage3 libp2p: ok`。 |

## 当前任务

| 项目 | 内容 |
|---|---|
| 目标 | 把 `v3/docs` 里的 `PubSub` 和 UniMaker 内容协议文档收口成“当前事实”和“目标协议”两层，不再把 `ZK / Pin / 热度分档 / Plumtree 在线树路由` 写成现状。 |
| 主线 | 这轮已经重写 [发布订阅.md](/Users/lbcheng/cheng-lang/v3/docs/发布订阅.md)，把它收成 `topic / branch / gossipsub transport / store-sync` 的正式现状文档；同时新增并收正 [Unimaker内容发布与分发协议v1.md](/Users/lbcheng/cheng-lang/v3/docs/Unimaker内容发布与分发协议v1.md) 的目标态提示，明确 `Manifest / Relay / Fetch / Pin` 是下一阶段协议，不等于当前 `v3` 已实现；再补 [README.md](/Users/lbcheng/cheng-lang/v3/docs/README.md) 目录入口，让 `内容平面 / 发布订阅 / Unimaker v1` 三份文档分工清楚。 |
| 文件 | `v3/docs/发布订阅.md` `v3/docs/README.md` `v3/docs/Unimaker内容发布与分发协议v1.md` `task_plan.md` `progress.md` `findings.md` |
| 不做 | 不改 `pubsub / overlay / libp2p / content_runtime` 代码语义；不把未来 `Pin / PoR / 热度分档 / GET_PREVIEW / GET_RANGE` 写成已落地能力；不碰用户现有其他未提交代码改动。 |
| 验收 | 三份文档必须口径一致：`发布订阅` 只写当前 `PubSub` 事实，`内容平面` 只写当前内容骨架，`Unimaker v1` 只写目标协议；`README.md` 必须能直达这三份文档；文档格式检查必须通过。 |

## 当前任务

| 项目 | 内容 |
|---|---|
| 目标 | 把 gate 外已经真跑通的 QUIC/TLS/content smoke 正式升格进 `stage2/stage3` gate，同时把下一刀明确切到 [handshake13.cheng](/Users/lbcheng/cheng-lang/v3/src/quic/tls/handshake13.cheng) 的大对象初始化链。 |
| 主线 | 这轮没再碰运行时代码，直接扩正式脚本 [run_v3_stage23_libp2p_smokes.sh](/Users/lbcheng/cheng-lang/v3/tooling/run_v3_stage23_libp2p_smokes.sh)：新增 `native_initial_crypto_frame_smoke`、`native_client_hello_wire_smoke`、`tls_client_hello_parse_smoke`、`tls_initial_packet_roundtrip_smoke`、`quic_transport_loopback_smoke`、`libp2p_quic_tls_smoke` 这 6 条 QUIC/TLS smoke。加脚本前我已经逐条用 `stage2/stage3` 串行真编真跑过；加脚本后又在无并发干扰下完整重跑整套正式 gate，最终继续输出 `v3 stage2/stage3 libp2p: ok`。并行探查的结论也明确了：下一刀不该回去抠 `rand/ecnist`，而该先拆 [initMsQuicTls13HandshakeStateInto](/Users/lbcheng/cheng-lang/v3/src/quic/tls/handshake13.cheng#L208) 里的 `wire/secrets/peer` 三段深层写回。 |
| 文件 | `v3/tooling/run_v3_stage23_libp2p_smokes.sh` `task_plan.md` `progress.md` `findings.md` |
| 不做 | 不把我自己并发编同一条 smoke 造成的假失败误记成脚本或源码回归；不在没必要的地方继续扩 host gate；不在下一刀目标已经明确时回头空拆 `rand/ecnist`。 |
| 验收 | `stage2/stage3` 下的 `native_initial_crypto_frame_smoke`、`native_client_hello_wire_smoke`、`tls_client_hello_parse_smoke`、`tls_initial_packet_roundtrip_smoke`、`quic_transport_loopback_smoke`、`libp2p_quic_tls_smoke` 都必须真编真跑通过；`sh /Users/lbcheng/cheng-lang/v3/tooling/run_v3_stage23_libp2p_smokes.sh` 必须继续输出 `v3 stage2/stage3 libp2p: ok`。 |

## 当前任务

| 项目 | 内容 |
|---|---|
| 目标 | 继续把 `stage2 QUIC/TLS` 的真 blocker 往 `rsa/rand/handshake13` 推，同时确认 `x509` 已经彻底退出 unsupported 首屏，且 host 编译面不被这轮改动打坏。 |
| 主线 | 这轮主改仍然只落在 [x509.cheng](/Users/lbcheng/cheng-lang/src/std/tls/x509.cheng)：把 [x509VerifyOcspResponse](/Users/lbcheng/cheng-lang/src/std/tls/x509.cheng#L2011) 里最后一段 `Result[Bytes]` 复合绑定链拆成直接 `sha256/sha384` 分支和扁平公钥编码，不再在 OCSP 热路径里绑定 `expectNameRes/keyBytesRes/expectKeyRes`。fresh `stage2` 编 [quic_transport_loopback_smoke.cheng](/Users/lbcheng/cheng-lang/v3/src/tests/quic_transport_loopback_smoke.cheng) 后，unsupported 列表里已经完全没有任何 `std/tls/x509::*`；剩下前沿固定为 `std/crypto/rsa::*`、`std/crypto/rand::*`、`std/crypto/ecnist::*` 和 `cheng/v3/quic/tls/handshake13::*`。同时为了不留下回归，我把这轮对 [rand.cheng](/Users/lbcheng/cheng-lang/src/std/crypto/rand.cheng) 的试探改动全部撤回，fresh host 编同一 smoke 已恢复 `primary_object_unsupported_function_count=0`。 |
| 文件 | `src/std/tls/x509.cheng` `task_plan.md` `progress.md` `findings.md` |
| 不做 | 不把 `rand` 这轮未收住的调用形状硬塞进工作区；不把 host 编译回归留在树上；不把 `x509` 已清空误说成 `stage2` 已闭环。 |
| 验收 | fresh host 编 [quic_transport_loopback_smoke.cheng](/Users/lbcheng/cheng-lang/v3/src/tests/quic_transport_loopback_smoke.cheng) 必须输出 `primary_object_unsupported_function_count=0`；fresh `stage2` 编同一 smoke 的 `primary_object_unsupported_functions` 里不得再出现任何 `std/tls/x509::`。 |

## 当前任务

| 项目 | 内容 |
|---|---|
| 目标 | 保持 host 侧 QUIC/libp2p 双烟真绿，继续把 `stage2 QUIC/TLS` 的 `x509` 尾部真 blocker 往后推，不让首屏再被已经脱离调用链的小 helper 和撤销包装函数占坑。 |
| 主线 | 这轮继续只动 [x509.cheng](/Users/lbcheng/cheng-lang/src/std/tls/x509.cheng)：删掉已经脱离热路径的 `x509TagIs* / x509KeyUsageFlagMissing`，把 `x509ParseExtensions` 拆成 `BasicConstraints/KeyUsage/ExtKeyUsage/SAN` 四个 helper，把 `leaf/ca constraints`、`CRL`、`OCSP`、`revocation` 一轮改成更直线的标量局部形状。fresh host 下 `/tmp/quic_transport_loopback_smoke.host.x509_tail2` 与 `/tmp/libp2p_quic_tls_smoke.host.x509_tail2` 都重新真跑 `ok`；fresh `stage2` 的 unsupported 总数仍是 `64`，但 `x509TagIs*` 和 `x509VerifyRevocationWithIssuer` 已经从 unsupported 列表里消失，`x509` 前沿现在固定成 `x509ReadOid/x509DecodeBitString/x509ParseMgf1Hash/x509ParseSignatureAlgorithm/x509ParseAlgIdWithParam/x509ParseBasicConstraintsExtension/x509ParseKeyUsageExtension/x509ParseExtensions/x509ParseTbs/x509ParseCertificateInto/x509VerifyLeafConstraints/x509VerifyCaConstraints/x509ParseCrlTbs/x509VerifyOcspResponse`。 |
| 文件 | `src/std/tls/x509.cheng` `task_plan.md` `progress.md` `findings.md` |
| 不做 | 不引入 runtime/C 捷径；不把 host 已绿路径打坏；不把 `stage2` 总数没降这件事伪装成已闭环。 |
| 验收 | `/tmp/quic_transport_loopback_smoke.host.x509_tail2` 必须输出 `v3 quic_transport_loopback_smoke ok`；`/tmp/libp2p_quic_tls_smoke.host.x509_tail2` 必须输出 `v3 libp2p_quic_tls_smoke ok`；fresh `stage2` 编 [quic_transport_loopback_smoke.cheng](/Users/lbcheng/cheng-lang/v3/src/tests/quic_transport_loopback_smoke.cheng) 时，unsupported 列表里不得再出现 `x509TagIsOid/x509TagIsBitString/x509TagIsSequence/x509TagIsBoolean/x509VerifyRevocationWithIssuer`。 |

## 当前任务

| 项目 | 内容 |
|---|---|
| 目标 | 保持 host 侧 QUIC/libp2p 双烟真绿，继续把 `stage2 QUIC/TLS` 的首屏真 blocker 钉死在 `x509` 最前沿，不让它再回退成旧的 `rsa public-key` 假根或 host 回归。 |
| 主线 | 这轮只动了 [x509.cheng](/Users/lbcheng/cheng-lang/src/std/tls/x509.cheng) 的前沿形状：`x509ReadOid/x509DecodeBitString` 把 `tlv.tag/value/next` 拆成本地标量；OID 映射链拆成 `x509SigAlgCodeFromOid/x509HashKindCodeFromOid`；`x509ParsePssParams/x509ParseSignatureAlgorithm/x509ParseSpki` 改成“零值默认初始化 + 直接枚举赋值”，彻底撤掉会被 seed 当普通调用的 `X509HashKind(...) / X509SignatureKind(...)` 构造器。fresh host 下 `/tmp/quic_transport_loopback_smoke.host.x509_front2` 与 `/tmp/libp2p_quic_tls_smoke.host.x509_front2` 都重新真跑 `ok`；fresh `stage2` 的 unsupported 总数仍是 `64`，但最前沿已明确收敛成 `x509ReadOid/x509DecodeBitString/x509SigAlgCodeFromOid/x509HashKindCodeFromOid/x509ParseMgf1Hash/x509ParsePssParams/x509ParseSignatureAlgorithm/x509ParseAlgIdWithParam/x509ParseKeyUsage/x509ParseSpki`，而不再回退到旧的 `rsaBigPublicKeyFromParts/rsaVerify*Bytes`。 |
| 文件 | `src/std/tls/x509.cheng` `task_plan.md` `progress.md` `findings.md` |
| 不做 | 不引入 runtime/C 捷径；不把 host 已绿路径打坏；不再往热路径里塞 `enum(...)` 构造器这种 seed 当前会误判成普通调用的写法。 |
| 验收 | `/tmp/quic_transport_loopback_smoke.host.x509_front2` 必须输出 `v3 quic_transport_loopback_smoke ok`；`/tmp/libp2p_quic_tls_smoke.host.x509_front2` 必须输出 `v3 libp2p_quic_tls_smoke ok`；fresh `stage2` 编 [quic_transport_loopback_smoke.cheng](/Users/lbcheng/cheng-lang/v3/src/tests/quic_transport_loopback_smoke.cheng) 时，`primary_object_unsupported_function_count` 仍可为 `64`，但首批 unsupported 不得回退到 `rsa public-key` 那条旧前沿。 |

## 当前任务

| 项目 | 内容 |
|---|---|
| 目标 | 继续把 `stage2 QUIC/TLS` 的前沿往 `handshake13/x509/rsa` 真根推进，同时保证 host 的 [quic_transport_loopback_smoke.cheng](/Users/lbcheng/cheng-lang/v3/src/tests/quic_transport_loopback_smoke.cheng) 和 [libp2p_quic_tls_smoke.cheng](/Users/lbcheng/cheng-lang/v3/src/tests/libp2p_quic_tls_smoke.cheng) 继续真绿。 |
| 主线 | 这轮已经把 [handshake13.cheng](/Users/lbcheng/cheng-lang/v3/src/quic/tls/handshake13.cheng) 补上 owner 直连入口：`msquicTls13ResetClientDefault/ResetServerDefault` 改成直接写全局 state，新增 `msquicTls13ClientGenerateX25519/ServerGenerateX25519`、`Client/ServerBuild*`、`Client/ServerAppendTranscript`、`Client/ServerFinishedOk`；同时把 [native_runtime.cheng](/Users/lbcheng/cheng-lang/v3/src/quic/native_runtime.cheng) 和多条 QUIC smoke 调用点从 `Side*` wrapper 切到 owner 入口。另在 [x509.cheng](/Users/lbcheng/cheng-lang/src/std/tls/x509.cheng) 补了 `x509StrEqual(...)` 并把 OID 判断、leaf/ca 约束改成更直线的 Cheng 形状，在 [policy.cheng](/Users/lbcheng/cheng-lang/v3/src/quic/tls/policy.cheng) 收了一轮 enum 默认值写法。结果已钉实：fresh host 编跑两条 smoke 都继续输出 `ok`；fresh `stage2` 的 unsupported 总数仍是 `64`，但前沿已经更诚实地暴露成 `x509/rsa` 复合返回链，加上 `handshake13` 的 owner 大对象写回，不再是 `native_runtime` 表层 header/frame 那类假根。 |
| 文件 | `v3/src/quic/tls/handshake13.cheng` `v3/src/quic/native_runtime.cheng` `v3/src/tests/tls_initial_packet_roundtrip_smoke.cheng` `v3/src/tests/tls_client_hello_parse_smoke.cheng` `v3/src/tests/udp_datapath_wire_roundtrip_smoke.cheng` `v3/src/tests/native_initial_crypto_frame_smoke.cheng` `v3/src/tests/native_process_initial_smoke.cheng` `src/std/tls/x509.cheng` `v3/src/quic/tls/policy.cheng` `task_plan.md` `progress.md` `findings.md` |
| 不做 | 不回退到 runtime/C 捷径；不把 host 已绿路径重新打坏；不把“换 wrapper 名字”误当成 `stage2` 真闭合。 |
| 验收 | `/tmp/quic_transport_loopback_smoke.host.owner_wrappers` 必须输出 `v3 quic_transport_loopback_smoke ok`；`/tmp/libp2p_quic_tls_smoke.host.owner_wrappers` 必须输出 `v3 libp2p_quic_tls_smoke ok`；fresh `stage2` 编 [quic_transport_loopback_smoke.cheng](/Users/lbcheng/cheng-lang/v3/src/tests/quic_transport_loopback_smoke.cheng) 时，`native_runtime` 不得再回退到 `BuildHeaderCommon/Build*HeaderInto/PacketAdd*Frame` 这层，且首批 unsupported 要明确落在 `x509/rsa/handshake13` owner 形状。 |

## 当前任务

| 项目 | 内容 |
|---|---|
| 目标 | 继续把 `stage2 QUIC/TLS` 的 `native_runtime` 表层假根剥干净：这轮先收掉 `header/frame` 这一层，同时保持 host 的 [quic_transport_loopback_smoke.cheng](/Users/lbcheng/cheng-lang/v3/src/tests/quic_transport_loopback_smoke.cheng) 和 [libp2p_quic_tls_smoke.cheng](/Users/lbcheng/cheng-lang/v3/src/tests/libp2p_quic_tls_smoke.cheng) 真绿。 |
| 主线 | 这轮已经在 [native_runtime.cheng](/Users/lbcheng/cheng-lang/v3/src/quic/native_runtime.cheng) 把 `Build*HeaderInto -> BuildHeaderCommon` 从“枚举实参”改成 `packetTypeCode` 标量入口，把 `PacketAdd*Frame` 从“`Frame*` 枚举常量直传 + 通用 raw helper”改成 `MsQuicFrame` owner helper直落 `PacketAppendFrame`，并新增 [handshake13.cheng](/Users/lbcheng/cheng-lang/v3/src/quic/tls/handshake13.cheng) 的 `msquicTls13RoleFromSideCode(...)`、[multiaddress.cheng](/Users/lbcheng/cheng-lang/v3/src/quic/multiaddress.cheng) 的 `quicMultiAddressBindText(...)`。结果已经钉实：fresh host 编跑两条 smoke 都继续输出 `ok`；fresh `stage2` 的 `native_runtime` 首层 unsupported 现在只剩 `msquicNativeResetClientEndpoint`、`msquicNativeResetServerEndpoint`、`msquicNativeMultiAddressBindText` 三个表面口子，`BuildHeaderCommon/Build*HeaderInto/PacketAdd*Frame` 这层已经从前沿里消失。 |
| 文件 | `v3/src/quic/native_runtime.cheng` `v3/src/quic/tls/handshake13.cheng` `v3/src/quic/multiaddress.cheng` `task_plan.md` `progress.md` `findings.md` |
| 不做 | 不回退到 runtime/C 捷径；不把 host 已绿的 QUIC 主线重新打坏；不把 `stage2` 里已经掉线的 `header/frame` 假 blocker 当成还没动。 |
| 验收 | `/tmp/quic_transport_loopback_smoke.host.after_native_stage2_shape` 必须输出 `v3 quic_transport_loopback_smoke ok`；`/tmp/libp2p_quic_tls_smoke.host.after_native_stage2_shape` 必须输出 `v3 libp2p_quic_tls_smoke ok`；fresh `stage2` 编 [quic_transport_loopback_smoke.cheng](/Users/lbcheng/cheng-lang/v3/src/tests/quic_transport_loopback_smoke.cheng) 时，`native_runtime` 的首层 unsupported 不得再出现 `BuildHeaderCommon/Build*HeaderInto/PacketAdd*Frame` 这组函数。 |

| 项目 | 内容 |
|---|---|
| 目标 | 继续把 `stage2 QUIC/TLS` 的前沿压进 [native_runtime.cheng](/Users/lbcheng/cheng-lang/v3/src/quic/native_runtime.cheng)，同时保持 host `quic_transport_loopback_smoke` 真绿，并把三份 runtime C 文件的 fresh 编译 warning 全收干净。 |
| 主线 | 这轮已经先把 [system_helpers_selflink_min_runtime.c](/Users/lbcheng/cheng-lang/src/runtime/native/system_helpers_selflink_min_runtime.c) 和 [system_helpers_selflink_shim.c](/Users/lbcheng/cheng-lang/src/runtime/native/system_helpers_selflink_shim.c) 的未用 helper / Mach-O deprecated 告警清掉，三份 runtime C 文件现在都能用同一条 `cc -std=c11 -O2 -Wall -Wextra -pedantic -c ...` fresh 零 warning 通过。随后把 [connection_impl.cheng](/Users/lbcheng/cheng-lang/v3/src/quic/core/connection_impl.cheng) 的 queued/sent packet 访问重新收回 `impl.queuedFrames/impl.sentPackets`，host fresh 编跑 [quic_transport_loopback_smoke.cheng](/Users/lbcheng/cheng-lang/v3/src/tests/quic_transport_loopback_smoke.cheng) 已重新输出 `v3 quic_transport_loopback_smoke ok`；fresh `stage2` 的首个 ordinary blocker 也因此前移回 [native_runtime.cheng](/Users/lbcheng/cheng-lang/v3/src/quic/native_runtime.cheng) 的 `msquicTls13SideReset / ResetClientEndpoint / ResetServerEndpoint` 这条线。 |
| 文件 | `src/runtime/native/system_helpers_selflink_min_runtime.c` `src/runtime/native/system_helpers_selflink_shim.c` `v3/src/quic/core/connection_impl.cheng` `v3/src/quic/native_runtime.cheng` `task_plan.md` `progress.md` `findings.md` |
| 不做 | 不把 host 已绿的 `connection_impl` 再翻成新 wrapper；不把 selflink warning 清理和 QUIC ordinary 修复混成一条假结论；不走 runtime/C 捷径绕过 Cheng 源码。 |
| 验收 | 三份 runtime C 文件都必须 fresh 零 warning；`/tmp/quic_transport_loopback_smoke.host.after_conn_patch` 必须输出 `v3 quic_transport_loopback_smoke ok`；fresh `stage2` 编同一 smoke 时，首个 unsupported 必须继续停在 `native_runtime`，不能回退到 `connection_impl`。 |

| 项目 | 内容 |
|---|---|
| 目标 | 只收 [test_pki.cheng](/Users/lbcheng/cheng-lang/v3/src/quic/tls/test_pki.cheng) 和 [msquictransport_native.cheng](/Users/lbcheng/cheng-lang/v3/src/quic/msquictransport_native.cheng) 这两处 `stage2-only` ordinary 假根，把顶层常量 / enum / handles 条件发码从前沿里剥掉。 |
| 主线 | [test_pki.cheng](/Users/lbcheng/cheng-lang/v3/src/quic/tls/test_pki.cheng) 这轮已经把 localhost PKI 的大 hex 常量改成函数体内返回字符串，再走 `msquicTestPkiFixedHexDecodeInto(...)` 逐字节写入 `Bytes`，同时 `MsQuicTlsPolicy` 继续走 `Into` 赋字段，不再把顶层 `str const` 直接塞进 ordinary 复合绑定；[msquictransport_native.cheng](/Users/lbcheng/cheng-lang/v3/src/quic/msquictransport_native.cheng) 则把 `handles(...)` 收成纯整数范围判断，并把 transport 默认 TLS policy 改成 `initMsQuicTlsPolicyInto(out.tlsPolicy)`，避开顶层复合返回。 |
| 文件 | `v3/src/quic/tls/test_pki.cheng` `v3/src/quic/msquictransport_native.cheng` `task_plan.md` `progress.md` `findings.md` |
| 不做 | 不碰 runtime/C bridge；不把问题扩到 `native_runtime/handshake/x509` 其它活根；不回滚当前工作区他人改动。 |
| 验收 | `cheng.stage2` 定向编 [quic_transport_loopback_smoke.cheng](/Users/lbcheng/cheng-lang/v3/src/tests/quic_transport_loopback_smoke.cheng) 时，不再出现 `cheng/v3/quic/tls/test_pki::*` 或 `cheng/v3/quic/msquictransport_native::handles` 的 ordinary unsupported；host 定向编同一 smoke 也不再在这两处失败。 |

## 当前任务

| 项目 | 内容 |
|---|---|
| 目标 | 先移除当前 `system_helpers.c` 这条真编译链上的 warning，避免噪声继续污染后面的 libp2p/QUIC 验证。 |
| 主线 | 这轮只动 [system_helpers.c](/Users/lbcheng/cheng-lang/src/runtime/native/system_helpers.c)、[system_helpers.h](/Users/lbcheng/cheng-lang/src/runtime/native/system_helpers.h)、[stb_image.h](/Users/lbcheng/cheng-lang/src/runtime/native/stb_image.h) 三处：把 `get_stdin/get_stdout/get_stderr` 改成标准 `void` 原型，把只在特定构建面启用的静态 helper/变量显式标成 `unused`，并给 `stbi__is_16_main(...)` 补 `STBI_NOTUSED(s)`。 |
| 文件 | `src/runtime/native/system_helpers.c` `src/runtime/native/system_helpers.h` `src/runtime/native/stb_image.h` `task_plan.md` `progress.md` `findings.md` |
| 不做 | 不借 warning 清理顺手改业务逻辑；不把 selflink 里现有独立编译错误混进这轮“清 warning”任务；不回头扩大到无关 runtime 文件。 |
| 验收 | `cc -std=c11 -O2 -Wall -Wextra -pedantic -c /Users/lbcheng/cheng-lang/src/runtime/native/system_helpers.c -o /tmp/system_helpers.warnfree.o` 必须零 warning 通过。 |

| 项目 | 内容 |
|---|---|
| 目标 | 继续把 `stage2 QUIC/TLS` 的 ordinary 真 blocker 往里压，但这轮只动 `native_runtime/test_pki/smoke` 形状，不碰 runtime/C 捷径，也不接受 host 回归。 |
| 主线 | 这轮已经把 [native_runtime.cheng](/Users/lbcheng/cheng-lang/v3/src/quic/native_runtime.cheng) 的 `ResetClientEndpoint/ResetServerEndpoint` 前半段继续收成叶子赋值：`Connection/ConnState/Flow/Perf` 不再走整块 init 调用；同时把 [test_pki.cheng](/Users/lbcheng/cheng-lang/v3/src/quic/tls/test_pki.cheng) 和 [quic_transport.cheng](/Users/lbcheng/cheng-lang/v3/src/libp2p/transports/quic_transport.cheng) 切到 `MsQuicTlsPolicy` 返回值构造，`quic_tls_policy_smoke` 里的 `RsaBigPublicKey` 也改成现成的 `rsaBigPublicKeyFromParts(...)`。结果是 host 侧 [quic_transport_loopback_smoke.cheng](/Users/lbcheng/cheng-lang/v3/src/tests/quic_transport_loopback_smoke.cheng) 和 [quic_tls_policy_smoke.cheng](/Users/lbcheng/cheng-lang/v3/src/tests/quic_tls_policy_smoke.cheng) 都继续真编真跑通过；fresh `stage2` 则从原先一串 `native_runtime/handshake/x509/connimpl` unsupported，收窄到只剩 `msquicNativeResetClientEndpoint/msquicNativeResetServerEndpoint` 两个函数，首个 ordinary 活根已经前移到 `let nowMs: int64 = msquicNowMs()` 这一层。 |
| 文件 | `v3/src/quic/native_runtime.cheng` `v3/src/quic/tls/test_pki.cheng` `v3/src/quic/msquicconnection.cheng` `v3/src/quic/core/connection_model.cheng` `v3/src/quic/core/connection_impl.cheng` `v3/src/libp2p/transports/quic_transport.cheng` `v3/src/tests/quic_tls_policy_smoke.cheng` `task_plan.md` `progress.md` `findings.md` |
| 不做 | 不把 `MsQuicTlsPolicy` 的 stage2 活根说成已闭合；不回退 host 已绿路径；不把 `stage2` 上还没吃掉的 `ResetClientEndpoint/ResetServerEndpoint` 伪装成小问题。 |
| 验收 | `/tmp/quic_transport_loopback_smoke.host.next3` 必须输出 `v3 quic_transport_loopback_smoke ok`；`/tmp/quic_tls_policy_smoke.host.next3` 必须输出 `v3 quic_tls_policy_smoke ok`；fresh `stage2` 编 [quic_transport_loopback_smoke.cheng](/Users/lbcheng/cheng-lang/v3/src/tests/quic_transport_loopback_smoke.cheng) 时，`primary_object_unsupported_function_count` 必须收敛到 `2`，且只剩 `msquicNativeResetClientEndpoint/msquicNativeResetServerEndpoint`。 |

## 当前任务

| 项目 | 内容 |
|---|---|
| 目标 | 继续把 `stage2 QUIC/TLS` 的真 blocker 往里推，同时保证 host 侧 [quic_transport_loopback_smoke.cheng](/Users/lbcheng/cheng-lang/v3/src/tests/quic_transport_loopback_smoke.cheng) 继续真绿。 |
| 主线 | 这轮已经把 [test_pki.cheng](/Users/lbcheng/cheng-lang/v3/src/quic/tls/test_pki.cheng) 收成纯 Cheng hex decode + setter 口径，并把 [native_runtime.cheng](/Users/lbcheng/cheng-lang/v3/src/quic/native_runtime.cheng) 的 `ResetEndpoint` 拆成 `client/server` 叶子 helper，再把 `client/server` 的 datagram 与 endpoint send/ready/credit 状态继续扁平化。中途暴露出 [connection_impl.cheng](/Users/lbcheng/cheng-lang/v3/src/quic/core/connection_impl.cheng) 里额外包的一层 `initMsQuicConnImplFromConnection(...)` 会把 host 编译面直接拖坏，所以已经撤掉，改回 `ConnState/Flow/Perf` 全局直连 `initMsQuicConnImpl(...)`。结果是 fresh host `quic_transport_loopback_smoke` 继续真编真跑输出 `v3 quic_transport_loopback_smoke ok`；fresh stage2 的首个 unsupported 已从旧 `msquicNativeResetEndpoint` 继续前移到 `msquicNativeResetClientEndpoint` 的复合 init 链，不再卡在重型 `if` 和 `session.client.sendOffset` 这类嵌套字段写入外壳。 |
| 文件 | `v3/src/quic/tls/test_pki.cheng` `v3/src/quic/native_runtime.cheng` `v3/src/quic/core/connection_impl.cheng` `task_plan.md` `progress.md` `findings.md` |
| 不做 | 不回退到 runtime/C 捷径；不把 host regression 留在工作区；不把 `stage2` 还没闭合的 `test_pki/handshake/x509` 说成已完成。 |
| 验收 | `DIAG_CONTEXT=1 /Users/lbcheng/cheng-lang/artifacts/v3_backend_driver/cheng system-link-exec --root /Users/lbcheng/cheng-lang/v3 --in /Users/lbcheng/cheng-lang/v3/src/tests/quic_transport_loopback_smoke.cheng --emit exe --target arm64-apple-darwin --out /tmp/quic_transport_loopback_smoke.host.after_flatten && /tmp/quic_transport_loopback_smoke.host.after_flatten` 必须输出 `v3 quic_transport_loopback_smoke ok`；fresh `stage2` 编同一 smoke 的首个 unsupported 必须不再是 `msquicNativeResetEndpoint`、`msquicNativeHandshakeIsClient` 或 `session.client.sendOffset` 嵌套字段写入。 |

## 当前任务

| 项目 | 内容 |
|---|---|
| 目标 | 收掉 [chain_node_libp2p_smoke.cheng](/Users/lbcheng/cheng-lang/v3/src/tests/chain_node_libp2p_smoke.cheng) 的最后运行期 blocker，把 [run_v3_host_smokes.sh](/Users/lbcheng/cheng-lang/v3/tooling/run_v3_host_smokes.sh) 真推回全绿。 |
| 主线 | 这轮没有再回头碰 `store_sync/TCP` 字节面，因为新增的 [chain_node_snapshot_sync_roundtrip_smoke.cheng](/Users/lbcheng/cheng-lang/v3/src/tests/chain_node_snapshot_sync_roundtrip_smoke.cheng) 已经证明 `sync request -> latest snapshot -> decode -> sync` 手工路径是通的。真正要收的是 [chain_node_libp2p.cheng](/Users/lbcheng/cheng-lang/v3/src/project/chain_node_libp2p.cheng) 里的 wrapper 复合边界：先把 `chain_node_libp2p_smoke` 自己的 `decodedSnapshotRes.value` 直传 sync 改成先落地本地 snapshot，再把 `v3ChainNodeLibp2pSyncOnce(...)` 改成 `entries -> latest snapshot payload -> decode fill -> sync` 的最短直线，不再跨函数返回 `Ingress`/`Snapshot` 复合对象，也不再做 `SnapshotCopy` 这类二次复制。结果是 [chain_node_libp2p_smoke.cheng](/Users/lbcheng/cheng-lang/v3/src/tests/chain_node_libp2p_smoke.cheng)、[chain_node_snapshot_sync_roundtrip_smoke.cheng](/Users/lbcheng/cheng-lang/v3/src/tests/chain_node_snapshot_sync_roundtrip_smoke.cheng)、[libp2p_protocols_smoke.cheng](/Users/lbcheng/cheng-lang/v3/src/tests/libp2p_protocols_smoke.cheng) 都重新真编真跑通过，整组 [run_v3_host_smokes.sh](/Users/lbcheng/cheng-lang/v3/tooling/run_v3_host_smokes.sh) 和 [run_v3_stage23_libp2p_smokes.sh](/Users/lbcheng/cheng-lang/v3/tooling/run_v3_stage23_libp2p_smokes.sh) 也都已前台收成全绿。 |
| 文件 | `v3/src/project/chain_node_libp2p.cheng` `v3/src/tests/chain_node_libp2p_smoke.cheng` `task_plan.md` `progress.md` `findings.md` |
| 不做 | 不把问题继续误判成 `store_sync/TCP` 线损坏；不再给 wrapper 叠加 `SnapshotCopy`、`bytesSlice` 之类额外复合边界；不回滚当前工作区其它改动。 |
| 验收 | `DIAG_CONTEXT=1 /Users/lbcheng/cheng-lang/artifacts/v3_backend_driver/cheng system-link-exec --root /Users/lbcheng/cheng-lang/v3 --in /Users/lbcheng/cheng-lang/v3/src/tests/chain_node_libp2p_smoke.cheng --emit exe --target arm64-apple-darwin --out /Users/lbcheng/cheng-lang/artifacts/v3_hostrun/chain_node_libp2p_smoke.debug && /Users/lbcheng/cheng-lang/artifacts/v3_hostrun/chain_node_libp2p_smoke.debug` 必须输出 `v3 chain_node_libp2p_smoke ok`；`DIAG_CONTEXT=1 /Users/lbcheng/cheng-lang/artifacts/v3_backend_driver/cheng system-link-exec --root /Users/lbcheng/cheng-lang/v3 --in /Users/lbcheng/cheng-lang/v3/src/tests/chain_node_snapshot_sync_roundtrip_smoke.cheng --emit exe --target arm64-apple-darwin --out /Users/lbcheng/cheng-lang/artifacts/v3_hostrun/chain_node_snapshot_sync_roundtrip_smoke.debug && /Users/lbcheng/cheng-lang/artifacts/v3_hostrun/chain_node_snapshot_sync_roundtrip_smoke.debug` 必须输出 `v3 chain_node_snapshot_sync_roundtrip_smoke ok`；`bash /Users/lbcheng/cheng-lang/v3/tooling/run_v3_host_smokes.sh` 必须前台输出 `v3 host smokes: ok`；`sh /Users/lbcheng/cheng-lang/v3/tooling/run_v3_stage23_libp2p_smokes.sh` 必须前台输出 `v3 stage2/stage3 libp2p: ok`。 |

## 当前任务

| 项目 | 内容 |
|---|---|
| 目标 | 继续把 `v3` 纯 Cheng `QUIC/TLS` 的真 blocker 往里推：这轮先拆薄 `x509/handshake13` 的大复合 key 路径，再把 `native_runtime` 里最前面的 `enqueue/reset` 形状压平。 |
| 主线 | 这轮已经在 [src/std/crypto/rsa.cheng](/Users/lbcheng/cheng-lang/src/std/crypto/rsa.cheng) 补了 `rsaBigPublicKeyFromParts/rsaVerify*Bytes`，在 [src/std/crypto/ecnist.cheng](/Users/lbcheng/cheng-lang/src/std/crypto/ecnist.cheng) 补了 `ecdsaVerifyTrustedPublicKeyBytes/ecdsaPublicKeyBytesValid`，并把 [src/std/tls/x509.cheng](/Users/lbcheng/cheng-lang/src/std/tls/x509.cheng) 的 `X509PublicKey` 从 `RsaBigPublicKey + EcPublicKey` 改成 `rsaN/rsaE/ecdsaBytes` 扁平表示，同时把 [v3/src/quic/tls/handshake13.cheng](/Users/lbcheng/cheng-lang/v3/src/quic/tls/handshake13.cheng) 的 `msquicTls13ParseCertificateVerify(...)` 改成 `let + helper` 主线；[v3/src/quic/native_runtime.cheng](/Users/lbcheng/cheng-lang/v3/src/quic/native_runtime.cheng) 也已把 `msquicNativeEnqueueDatagram` 拆成 client/server 叶子 helper，并去掉 `ResetSession/DialSetupSession` 里多余的 settings 复合局部绑定。结果是 host 侧 [quic_transport_loopback_smoke.cheng](/Users/lbcheng/cheng-lang/v3/src/tests/quic_transport_loopback_smoke.cheng) 和 [libp2p_quic_tls_smoke.cheng](/Users/lbcheng/cheng-lang/v3/src/tests/libp2p_quic_tls_smoke.cheng) 都重新 fresh 真编真跑通过；`stage2` 这边的前沿则已经明确收窄到 `std/tls/x509` 的普通条件发码和 `X509Certificate/X509CertList` 布局，再加上 `native_runtime` 里仍直接碰 `MsQuicTls13HandshakeState/MsQuicConnImpl` 的几条边界函数。 |
| 文件 | `src/std/crypto/rsa.cheng` `src/std/crypto/ecnist.cheng` `src/std/tls/x509.cheng` `v3/src/quic/tls/handshake13.cheng` `v3/src/quic/native_runtime.cheng` `task_plan.md` `progress.md` `findings.md` |
| 不做 | 不回退到 runtime/C 捷径；不把 `connection_impl` 内核算法先拆烂；不把 `stage2` 当前还没闭合的 `x509/native_runtime` 说成已完成。 |
| 验收 | `/tmp/quic_transport_loopback_smoke.host.certverify_flat` 真跑输出 `v3 quic_transport_loopback_smoke ok`；`/tmp/libp2p_quic_tls_smoke.host.certverify_flat` 真跑输出 `v3 libp2p_quic_tls_smoke ok`；fresh `stage2` 编 [quic_transport_loopback_smoke.cheng](/Users/lbcheng/cheng-lang/v3/src/tests/quic_transport_loopback_smoke.cheng) 的首批 unsupported 里，不再出现 `X509PublicKey`、`msquicTls13ParseCertificateVerify`、旧 `state.peerLeaf.publicKey.ecdsa/rsa` 这条链。 |

| 项目 | 内容 |
|---|---|
| 目标 | 把 UniMaker 内容平面这轮剩下的硬阻塞收口：修掉 `RSA-PSS -> QUIC/TLS`，把 `content_stub -> sync -> pull -> rebuildAgainstStub` 真焊成一条闭环，并把文档口径追到当前实现。 |
| 主线 | 这轮已经在 [src/std/crypto/rsa.cheng](/Users/lbcheng/cheng-lang/src/std/crypto/rsa.cheng) 修正 `PSS` 编码自检，对齐 `maskedDb` 清高位后的 roundtrip 比较；随后 [quic_tls_policy_smoke.cheng](/Users/lbcheng/cheng-lang/v3/src/tests/quic_tls_policy_smoke.cheng)、[quic_transport_loopback_smoke.cheng](/Users/lbcheng/cheng-lang/v3/src/tests/quic_transport_loopback_smoke.cheng)、[libp2p_quic_tls_smoke.cheng](/Users/lbcheng/cheng-lang/v3/src/tests/libp2p_quic_tls_smoke.cheng)、[content_quic_smoke.cheng](/Users/lbcheng/cheng-lang/v3/src/tests/content_quic_smoke.cheng) 已全部真编真跑通过。内容面这边又把 [content_stub_smoke.cheng](/Users/lbcheng/cheng-lang/v3/src/tests/content_stub_smoke.cheng) 扩成 `stub -> sync fetch advert/manifest/recoveryPlan -> build pull targets -> TCP shard pull -> rebuildAgainstStub` 的端到端 smoke，并把 [内容平面.md](/Users/lbcheng/cheng-lang/v3/docs/内容平面.md)、[发布订阅.md](/Users/lbcheng/cheng-lang/v3/docs/发布订阅.md)、[纠删码群集.md](/Users/lbcheng/cheng-lang/v3/docs/纠删码群集.md) 的 `recoveryPlan/4 条 ingress/payloadKind=6` 口径补齐。 |
| 文件 | `src/std/crypto/rsa.cheng` `v3/src/tests/content_stub_smoke.cheng` `v3/docs/内容平面.md` `v3/docs/发布订阅.md` `v3/docs/纠删码群集.md` `task_plan.md` `progress.md` `findings.md` |
| 不做 | 不碰 `chain_node/consensus` 现有快照重放链；不把 `chain_node_libp2p_smoke` 的旧 `body cid mismatch` 混进本轮内容平面问题里。 |
| 验收 | [quic_tls_policy_smoke.cheng](/Users/lbcheng/cheng-lang/v3/src/tests/quic_tls_policy_smoke.cheng)、[quic_transport_loopback_smoke.cheng](/Users/lbcheng/cheng-lang/v3/src/tests/quic_transport_loopback_smoke.cheng)、[libp2p_quic_tls_smoke.cheng](/Users/lbcheng/cheng-lang/v3/src/tests/libp2p_quic_tls_smoke.cheng)、[content_quic_smoke.cheng](/Users/lbcheng/cheng-lang/v3/src/tests/content_quic_smoke.cheng)、[content_codec_smoke.cheng](/Users/lbcheng/cheng-lang/v3/src/tests/content_codec_smoke.cheng)、[content_runtime_smoke.cheng](/Users/lbcheng/cheng-lang/v3/src/tests/content_runtime_smoke.cheng)、[content_stub_smoke.cheng](/Users/lbcheng/cheng-lang/v3/src/tests/content_stub_smoke.cheng)、[pubsub_smoke.cheng](/Users/lbcheng/cheng-lang/v3/src/tests/pubsub_smoke.cheng)、[dag_mempool_smoke.cheng](/Users/lbcheng/cheng-lang/v3/src/tests/dag_mempool_smoke.cheng)、[plumtree_smoke.cheng](/Users/lbcheng/cheng-lang/v3/src/tests/plumtree_smoke.cheng)、[erasure_swarm_smoke.cheng](/Users/lbcheng/cheng-lang/v3/src/tests/erasure_swarm_smoke.cheng)、[libp2p_protocols_smoke.cheng](/Users/lbcheng/cheng-lang/v3/src/tests/libp2p_protocols_smoke.cheng) 真编真跑通过；[run_v3_host_smokes.sh](/Users/lbcheng/cheng-lang/v3/tooling/run_v3_host_smokes.sh) 当前仍只在旧 [chain_node_libp2p_smoke.cheng](/Users/lbcheng/cheng-lang/v3/src/tests/chain_node_libp2p_smoke.cheng) 上失败，报 `v3 consensus: body cid mismatch`。 |

## 当前任务

| 项目 | 内容 |
|---|---|
| 目标 | 把 UniMaker 内容平面的最小发布闭环真打通：`content_stub` 发布、`advert/manifest` 元数据回拉、`shard pull/rebuild` 骨架、文档收口、host smoke 接线。 |
| 主线 | 这轮已经把 `payloadKind/content_stub/content protocol` 接进 `pubsub + overlay + libp2p`，并新补 [content_runtime.cheng](/Users/lbcheng/cheng-lang/v3/src/chain/content_runtime.cheng) / [content_runtime_quic.cheng](/Users/lbcheng/cheng-lang/v3/src/chain/content_runtime_quic.cheng) / `content_*_smoke`。代码侧当前闭合的是 `content_codec_smoke`、`content_runtime_smoke`、`content_stub_smoke`；文档侧已经把 [发布订阅.md](/Users/lbcheng/cheng-lang/v3/docs/发布订阅.md) 收回到已实现口径，并补了 `内容平面 / DAG可用性与公告 / Plumtree路由 / 纠删码群集` 四份内容面文档。 |
| 文件 | `v3/src/chain/dag_mempool.cheng` `v3/src/chain/erasure_swarm.cheng` `v3/src/chain/content_plane.cheng` `v3/src/chain/content_runtime.cheng` `v3/src/chain/content_runtime_quic.cheng` `v3/src/overlay/contracts.cheng` `v3/src/overlay/libp2p_bridge.cheng` `v3/src/libp2p/core/types.cheng` `v3/src/libp2p/protocols/gossipsub.cheng` `v3/src/libp2p/protocols/store_sync.cheng` `v3/src/tests/content_codec_smoke.cheng` `v3/src/tests/content_runtime_smoke.cheng` `v3/src/tests/content_stub_smoke.cheng` `v3/docs/README.md` `v3/docs/发布订阅.md` `v3/docs/内容平面.md` `v3/docs/DAG可用性与公告.md` `v3/docs/Plumtree路由.md` `v3/docs/纠删码群集.md` `v3/tooling/run_v3_host_smokes.sh` `task_plan.md` `progress.md` `findings.md` |
| 不做 | 不把 `ZK PoE`、`RWAD` 结算、真实 `RS` 编解码、内容秒开 UI、完整 QUIC 数据面写成已实现；不回滚当前工作区其它改动。 |
| 验收 | [content_codec_smoke.cheng](/Users/lbcheng/cheng-lang/v3/src/tests/content_codec_smoke.cheng)、[content_runtime_smoke.cheng](/Users/lbcheng/cheng-lang/v3/src/tests/content_runtime_smoke.cheng)、[content_stub_smoke.cheng](/Users/lbcheng/cheng-lang/v3/src/tests/content_stub_smoke.cheng)、[erasure_swarm_smoke.cheng](/Users/lbcheng/cheng-lang/v3/src/tests/erasure_swarm_smoke.cheng) 真编真跑通过；[run_v3_host_smokes.sh](/Users/lbcheng/cheng-lang/v3/tooling/run_v3_host_smokes.sh) 现已接入 `content_codec_smoke/content_runtime_smoke/content_stub_smoke`；文档只承认已实现与已落地骨架。 |

| 项目 | 内容 |
|---|---|
| 目标 | 把可稳定通过的内容平面 smoke 正式接进 [run_v3_host_smokes.sh](/Users/lbcheng/cheng-lang/v3/tooling/run_v3_host_smokes.sh)，同时把这轮工具接线和边界写进任务记录。 |
| 主线 | 这轮只动 [run_v3_host_smokes.sh](/Users/lbcheng/cheng-lang/v3/tooling/run_v3_host_smokes.sh)、[task_plan.md](/Users/lbcheng/cheng-lang/task_plan.md)、[progress.md](/Users/lbcheng/cheng-lang/progress.md)、[findings.md](/Users/lbcheng/cheng-lang/findings.md)。先独立验 [content_codec_smoke.cheng](/Users/lbcheng/cheng-lang/v3/src/tests/content_codec_smoke.cheng) 继续真编真跑通过，再把它接进 host runner；已知失败的 `content_stub_smoke`、`content_runtime_smoke`、`content_quic_smoke` 这轮不进 gate。 |
| 文件 | `v3/tooling/run_v3_host_smokes.sh` `task_plan.md` `progress.md` `findings.md` |
| 不做 | 不动 `content_plane/content_runtime/libp2p` 实现；不把已知失败 smoke 塞进 runner；不回滚当前工作区其他改动。 |
| 验收 | [content_codec_smoke.cheng](/Users/lbcheng/cheng-lang/v3/src/tests/content_codec_smoke.cheng) 独立输出 `v3 content_codec_smoke ok`；[run_v3_host_smokes.sh](/Users/lbcheng/cheng-lang/v3/tooling/run_v3_host_smokes.sh) 包含 `content_codec_smoke` 且不包含 `content_stub_smoke/content_runtime_smoke/content_quic_smoke`；三份记录写明本轮接线口径。 |

## 当前任务

| 项目 | 内容 |
|---|---|
| 目标 | 继续把 `QUIC/TLS` 的 ordinary 假根剥掉：`quic_tls_policy_smoke` 已证明当前剩下的是 ordinary smoke 形状本身，不再是 `MsQuicTlsPolicy` 复合布局；`quic_native_listener_smoke` 这轮已把 `Policy` 复合面、`initMsQuicSettings` record literal 和 eager session reset 从第一现场移走，当前真 blocker 收窄到 `native_runtime/datapath_runtime/datapath_udp`。 |
| 主线 | 这轮已经把 [v3/src/quic/tls/policy.cheng](/Users/lbcheng/cheng-lang/v3/src/quic/tls/policy.cheng) 的 `MsQuicTlsPolicy` 数组壳拆平成固定字段，并把 [v3/src/quic/tls/test_pki.cheng](/Users/lbcheng/cheng-lang/v3/src/quic/tls/test_pki.cheng) 的 localhost PKI 改成纯 hex decode 标量入口；随后把 [v3/src/tests/quic_native_listener_smoke.cheng](/Users/lbcheng/cheng-lang/v3/src/tests/quic_native_listener_smoke.cheng) 切到 `msquicNativeStartListenerServerInputs(...)`，并在 [v3/src/quic/native_runtime.cheng](/Users/lbcheng/cheng-lang/v3/src/quic/native_runtime.cheng) 新增 listener-only reset，去掉 `ensureInit()` 里的 eager session reset。再把 [v3/src/quic/msquicruntime.cheng](/Users/lbcheng/cheng-lang/v3/src/quic/msquicruntime.cheng) 的 `initMsQuicSettings()` 从 record literal 改成逐字段赋值后，fresh host/stage2 诊断已经明确前移：`initMsQuicSettings` 和 `msquicTlsPolicyHasServerInputs` 不再是活根，当前剩下的是 `msquicNativeTaggedAddr(...)`、`msquicNativeStartListenerServerInputs(...)`、`initMsQuicDatapathRuntime()`、`msquic_udp_close(...)`、`udpParseAddr(...)` 这些 runtime/datapath 形状。 |
| 文件 | `v3/src/quic/tls/policy.cheng` `v3/src/quic/tls/test_pki.cheng` `v3/src/quic/msquicruntime.cheng` `v3/src/quic/native_runtime.cheng` `v3/src/tests/quic_tls_policy_smoke.cheng` `v3/src/tests/quic_native_listener_smoke.cheng` `task_plan.md` `progress.md` `findings.md` |
| 不做 | 不再把 `MsQuicTlsPolicy` 复合返回/复合 `var` 参数当这轮第一现场；不再把 `initMsQuicSettings` 的 record literal 当唯一主根；不回头碰 full `libp2p_quic_tls_smoke`。 |
| 验收 | 这轮先认诊断收窄：fresh `quic_native_listener_smoke` 的 unsupported 列表必须不再含 `initMsQuicSettings` 和 `msquicTlsPolicyHasServerInputs`；下一轮继续把第一现场压到 `datapath_runtime/datapath_udp`。 |
| 目标 | 把 `QUIC/TLS` 的假根和真根彻底分开：`udp_importc` 已闭合并进 host gate，`libp2p_quic_tls_smoke` 的空日志段错已拆成 `quic_tls_policy` 与 `quic_native_listener` 两条 ordinary 真 blocker。 |
| 主线 | 这轮已经把 [strToCStringTemp](/Users/lbcheng/cheng-lang/src/std/system.cheng:586) 收成 runtime 真相源桥，并把 [std/os.cheng](/Users/lbcheng/cheng-lang/src/std/os.cheng) / [udp_syscall.cheng](/Users/lbcheng/cheng-lang/src/std/net/transports/udp_syscall.cheng) 切到 `libc_*` 宿主桥，新增 [udp_importc_smoke.cheng](/Users/lbcheng/cheng-lang/v3/src/tests/udp_importc_smoke.cheng) 与 [run_v3_udp_importc_smoke.sh](/Users/lbcheng/cheng-lang/v3/tooling/run_v3_udp_importc_smoke.sh)。结果已经钉死：`host/stage2/stage3` 都真编真跑通过，且 [run_v3_host_smokes.sh](/Users/lbcheng/cheng-lang/v3/tooling/run_v3_host_smokes.sh) 已接入新 smoke 并继续全绿。随后把 full [libp2p_quic_tls_smoke.cheng](/Users/lbcheng/cheng-lang/v3/src/tests/libp2p_quic_tls_smoke.cheng) 拆成 [quic_tls_policy_smoke.cheng](/Users/lbcheng/cheng-lang/v3/src/tests/quic_tls_policy_smoke.cheng) 和 [quic_native_listener_smoke.cheng](/Users/lbcheng/cheng-lang/v3/src/tests/quic_native_listener_smoke.cheng)：前者现在稳定暴露 `test_pki/policy` 的 `stmt_call + stmt_var + composite return` 缺口，后者稳定暴露 `msquictransport_native/native_runtime` 的大复合 ABI/布局缺口，不再 `rc=139 + 0 字节日志`。下一刀直接收 `MsQuicTlsPolicy/test_pki` 和 `MsQuicTransport/native_runtime`，不再回头盯 UDP 假根。 |
| 文件 | `src/std/system.cheng` `src/std/system_c.cheng` `src/std/os.cheng` `src/std/net/transports/udp_syscall.cheng` `src/runtime/native/system_helpers.c` `src/runtime/native/system_helpers.h` `src/runtime/native/system_helpers_selflink_min_runtime.c` `src/runtime/native/system_helpers_selflink_shim.c` `src/runtime/native/system_helpers_stdio_bridge.c` `v3/src/tests/udp_importc_smoke.cheng` `v3/src/tests/quic_tls_policy_smoke.cheng` `v3/src/tests/quic_native_listener_smoke.cheng` `v3/tooling/run_v3_udp_importc_smoke.sh` `v3/tooling/run_v3_host_smokes.sh` `v3/README.md` `v3/tooling/README.md` `task_plan.md` `progress.md` `findings.md` |
| 不做 | 不再把 `udpErrno/strToCStringTemp` 当 `QUIC/TLS` 主根；不再用 full `libp2p_quic_tls_smoke` 的空日志段错指导修复；不把 split smoke 冒充成 `QUIC/TLS` 已闭合。 |
| 验收 | [run_v3_udp_importc_smoke.sh](/Users/lbcheng/cheng-lang/v3/tooling/run_v3_udp_importc_smoke.sh) 的 `host/stage2/stage3` 必须都输出 `v3 udp_importc_smoke ok`；[run_v3_host_smokes.sh](/Users/lbcheng/cheng-lang/v3/tooling/run_v3_host_smokes.sh) 必须继续前台通过；`quic_tls_policy_smoke` / `quic_native_listener_smoke` 必须稳定返回 ordinary unsupported 列表，而不是再 `rc=139` 空日志崩溃。 |

| 项目 | 内容 |
|---|---|
| 目标 | 继续按 `v2/cheng-quic -> v3` 的 Cheng 源码迁移推进 `libp2p_quic_tls_smoke`，不走外链库，不走 C 侧捷径。 |
| 主线 | 这轮已经继续把纯 Cheng `QUIC/TLS` 主线压向 `v3`：`std/system::strToCStringTemp`、`std/strings::charToStr`、`multiaddress`、`flow_control/perf_tuning/msquictransport` 都先收成 ordinary 更容易吃的形状；`connection_impl` 也已去掉 `congestion/bbr/cubic/loss_detection` 这整条无关依赖，`msquicconnection` 收成最小连接态。现在新的第一处真 blocker 不是“缺 v2 代码”，而是 fresh [artifacts/v3_backend_driver/cheng](/Users/lbcheng/cheng-lang/artifacts/v3_backend_driver/cheng) 在编 [libp2p_quic_tls_smoke.cheng](/Users/lbcheng/cheng-lang/v3/src/tests/libp2p_quic_tls_smoke.cheng) 时会在 [cheng_v3_seed.c](/Users/lbcheng/cheng-lang/v3/bootstrap/cheng_v3_seed.c) 的 `v3_resolve_call_target(...)` 栈炸；前一层 `v3_resolve_bare_call_symbol(...)` / `v3_const_resolve_call_symbol(...)` 的坏指针崩溃已经先收成安全失败。下一刀不再盲改 QUIC 模块，而是先把 seed 的 call-target 递归和 alias 脏项收口，再继续推进 `native_runtime/handshake/x509`。 |
| 文件 | `src/std/system.cheng` `src/std/strings.cheng` `v3/src/quic/multiaddress.cheng` `v3/src/quic/msquictransport_native.cheng` `v3/src/quic/core/flow_control_model.cheng` `v3/src/quic/core/perf_tuning.cheng` `v3/src/quic/core/connection_impl.cheng` `v3/src/quic/msquicconnection.cheng` `v3/src/quic/native_runtime.cheng` `v3/src/quic/tls/test_pki.cheng` `v3/bootstrap/cheng_v3_seed.c` `task_plan.md` `progress.md` `findings.md` |
| 不做 | 不回退到兄弟仓运行时依赖；不把 `QUIC/TLS` 说成已经闭合；不把 host compiler 自己的 `SIGSEGV` 冒充 ordinary 语义错误。 |
| 验收 | 先让 `DIAG_CONTEXT=1 artifacts/v3_backend_driver/cheng system-link-exec ... v3/src/tests/libp2p_quic_tls_smoke.cheng ...` 不再以 `139` 死在 seed，再继续看真实 unsupported 列表；最终还是只认 `libp2p_quic_tls_smoke` 真编真跑。 |

| 项目 | 内容 |
|---|---|
| 目标 | 把 fresh `backend_driver`、`host` 和 `slice_gate` 重新收成真相源：base `host` 不再被 `QUIC/msquic` 闭包强拖，文档不再写旧 `chain_node` 入口，`stage2/stage3 libp2p` 主 gate 只认当前已经闭合的 TCP/libp2p 主线。 |
| 主线 | 这轮已经把 [host.cheng](/Users/lbcheng/cheng-lang/v3/src/libp2p/host/host.cheng) 从无条件 `quic_transport` 依赖里拆出来，`v3Libp2pSyncRequest(...)` 重新回到 base/TCP 语义；新增 [host_quic.cheng](/Users/lbcheng/cheng-lang/v3/src/libp2p/host/host_quic.cheng) 专门承接 QUIC 同步路径，并让 [libp2p_quic_tls_smoke.cheng](/Users/lbcheng/cheng-lang/v3/src/tests/libp2p_quic_tls_smoke.cheng) 显式走这条扩展模块。与此同时，把 [v3/README.md](/Users/lbcheng/cheng-lang/v3/README.md)、[v3/tooling/README.md](/Users/lbcheng/cheng-lang/v3/tooling/README.md)、[LSMR.md](/Users/lbcheng/cheng-lang/v3/docs/LSMR.md)、[自举和性能.md](/Users/lbcheng/cheng-lang/v3/docs/自举和性能.md) 里的旧 `chain_node` 入口和“已闭合 QUIC/CLI”说法收正；再把 [run_v3_stage23_libp2p_smokes.sh](/Users/lbcheng/cheng-lang/v3/tooling/run_v3_stage23_libp2p_smokes.sh) 的 mandatory 列表收回到当前真闭合的 `core/host/tcp/overlay/protocols/chain_node_libp2p + twoproc/process` 主线。 |
| 文件 | `v3/src/libp2p/host/host.cheng` `v3/src/libp2p/host/host_quic.cheng` `v3/src/tests/libp2p_quic_tls_smoke.cheng` `v3/tooling/run_v3_stage23_libp2p_smokes.sh` `v3/README.md` `v3/tooling/README.md` `v3/docs/LSMR.md` `v3/docs/自举和性能.md` `task_plan.md` `progress.md` `findings.md` |
| 不做 | 不把 `chain_node_libp2p` 这条 TCP 主线继续绑在 `msquic` 闭包上；不把还没闭合的 `QUIC/TLS` 路径冒充成 `stage2/stage3` 主 gate 已完成项；不再让 README 写已经删除的 `v3/src/tooling/chain_node_main.cheng`。 |
| 验收 | fresh `artifacts/v3_backend_driver/cheng` 必须真编 `chain_node_libp2p_smoke`；`run_v3_host_smokes.sh`、`run_v3_stage23_libp2p_smokes.sh`、`run_slice_gate.sh` 必须前台通过；`rg -n 'v3/src/tooling/chain_node_main\\.cheng|chain_node balance/mint/balance|当前只闭合内存内 \`serve-once/sync-once\`|READY <port>' v3 v3/docs v3/tooling` 必须为空。 |

| 项目 | 内容 |
|---|---|
| 目标 | 把 `v3` 的 `libp2p sync` 从“请求侧直接偷看远端内存 store”推进到“真 `request-response` 二进制往返”，并把两进程 TCP 闭环正式接进 host 与 `stage2/stage3` gate。 |
| 主线 | 这轮已经沿 [system_helpers.c](/Users/lbcheng/cheng-lang/src/runtime/native/system_helpers.c) 新增 `cheng_v3_tcp_loopback_request_response_bridge(...)`，让客户端真走 `listen/connect/accept/send/recv + multistream + request frame + response frame`；在 [store_sync.cheng](/Users/lbcheng/cheng-lang/v3/src/libp2p/protocols/store_sync.cheng) 补上 `query/entries` 真编码解码和 `v3Libp2pSyncServePayload(...)`；再把 [host.cheng](/Users/lbcheng/cheng-lang/v3/src/libp2p/host/host.cheng) 的 `v3Libp2pSyncRequest(...)` 改成 `encode -> tcp roundtrip -> decode`，不再直接读 `remoteHost.storeEntries`。随后新增 [libp2p_tcp_twoproc_server_smoke.cheng](/Users/lbcheng/cheng-lang/v3/src/tests/libp2p_tcp_twoproc_server_smoke.cheng)、[libp2p_tcp_twoproc_client_smoke.cheng](/Users/lbcheng/cheng-lang/v3/src/tests/libp2p_tcp_twoproc_client_smoke.cheng) 与 [run_v3_tcp_twoproc_smoke.sh](/Users/lbcheng/cheng-lang/v3/tooling/run_v3_tcp_twoproc_smoke.sh)，并把它们正式接进 [run_v3_stage23_libp2p_smokes.sh](/Users/lbcheng/cheng-lang/v3/tooling/run_v3_stage23_libp2p_smokes.sh) 和 [run_v3_host_smokes.sh](/Users/lbcheng/cheng-lang/v3/tooling/run_v3_host_smokes.sh)。最后把 [chain_node_process_server_smoke.cheng](/Users/lbcheng/cheng-lang/v3/src/tests/chain_node_process_server_smoke.cheng)、[chain_node_process_client_smoke.cheng](/Users/lbcheng/cheng-lang/v3/src/tests/chain_node_process_client_smoke.cheng) 及 [run_v3_chain_node_process_smoke.sh](/Users/lbcheng/cheng-lang/v3/tooling/run_v3_chain_node_process_smoke.sh) 一起收成统一 `ready-path` 文件写端口握手。 |
| 文件 | `src/runtime/native/system_helpers.c` `src/runtime/native/system_helpers.h` `v3/src/libp2p/transports/tcp_transport.cheng` `v3/src/libp2p/protocols/store_sync.cheng` `v3/src/libp2p/host/host.cheng` `v3/src/tests/libp2p_protocols_smoke.cheng` `v3/src/tests/libp2p_tcp_twoproc_server_smoke.cheng` `v3/src/tests/libp2p_tcp_twoproc_client_smoke.cheng` `v3/tooling/run_v3_tcp_twoproc_smoke.sh` `v3/tooling/run_v3_stage23_libp2p_smokes.sh` `v3/tooling/run_v3_host_smokes.sh` `v3/src/tests/chain_node_process_server_smoke.cheng` `v3/src/tests/chain_node_process_client_smoke.cheng` `v3/tooling/run_v3_chain_node_process_smoke.sh` `task_plan.md` `progress.md` `findings.md` |
| 不做 | 不再让请求侧直接窥探远端 `storeEntries`；不再依赖 stdout `READY` 这类同步；不把 `std/os` 元组返回和进程管道硬塞进当前 ordinary 子集。 |
| 验收 | [run_v3_stage23_libp2p_smokes.sh](/Users/lbcheng/cheng-lang/v3/tooling/run_v3_stage23_libp2p_smokes.sh) 必须完整以 `v3 stage2/stage3 libp2p: ok` 收尾，并包含 `libp2p_tcp_twoproc_smoke` 与 `chain_node_libp2p_smoke` 的 `stage2/stage3` 真跑；[run_v3_host_smokes.sh](/Users/lbcheng/cheng-lang/v3/tooling/run_v3_host_smokes.sh) 必须完整以 `v3 host smokes: ok` 收尾；[run_v3_chain_node_process_smoke.sh](/Users/lbcheng/cheng-lang/v3/tooling/run_v3_chain_node_process_smoke.sh) 必须在 `stage2` 与 `stage3` 都输出 `v3 chain_node process smoke ok (...)`。 |

| 项目 | 内容 |
|---|---|
| 目标 | 把 `libp2p tcp` 从“只测 C 单桥 multistream”推进到“真实 TCP 二进制 underlay 可过 `chain_node` snapshot payload”，并继续保持 `stage2/stage3` 与 host gate 全绿。 |
| 主线 | 这轮没有硬把纯内存 [host.cheng](/Users/lbcheng/cheng-lang/v3/src/libp2p/host/host.cheng) 伪装成真实网络，而是沿现有 native socket 骨架在 [tcp_transport.cheng](/Users/lbcheng/cheng-lang/v3/src/libp2p/transports/tcp_transport.cheng) 新增 `v3Libp2pTcpLoopbackRoundtrip(...)`，通过新的 `cheng_v3_tcp_loopback_payload_bridge(...)` 先做真 `listen/connect/accept/send/recv`、再做 `multistream` 协商、最后做长度前缀 raw payload 原样回显；同时把 [libp2p_tcp_smoke.cheng](/Users/lbcheng/cheng-lang/v3/src/tests/libp2p_tcp_smoke.cheng) 升成 `chain_node mint/transfer -> encode snapshot -> 真 TCP 往返 -> decode/sync -> balance/signature` 的二进制闭环。真验收已经固定：targeted host 编译运行 `libp2p_tcp_smoke` 通过，整组 [run_v3_stage23_libp2p_smokes.sh](/Users/lbcheng/cheng-lang/v3/tooling/run_v3_stage23_libp2p_smokes.sh) 与整组 [run_v3_host_smokes.sh](/Users/lbcheng/cheng-lang/v3/tooling/run_v3_host_smokes.sh) 都已 fresh 前台通过。 |
| 文件 | `v3/src/libp2p/transports/tcp_transport.cheng` `v3/src/tests/libp2p_tcp_smoke.cheng` `src/runtime/native/system_helpers.c` `src/runtime/native/system_helpers.h` `task_plan.md` `progress.md` `findings.md` |
| 不做 | 不把内存 `host` 冒充真实 underlay；不把二进制 payload 降级成文本帧；不回头碰当前 `stage2/stage3` 还吃不稳的 `std/net/tcp_syscall` 主链。 |
| 验收 | `./artifacts/v3_hostrun_targeted/libp2p_tcp_smoke` 必须输出 `v3 libp2p_tcp_smoke ok`；`sh /Users/lbcheng/cheng-lang/v3/tooling/run_v3_stage23_libp2p_smokes.sh` 必须继续以 `v3 stage2/stage3 libp2p: ok` 收尾；`sh /Users/lbcheng/cheng-lang/v3/tooling/run_v3_host_smokes.sh` 必须继续以 `v3 host smokes: ok` 收尾。 |

| 项目 | 内容 |
|---|---|
| 目标 | 把 `chain_node_libp2p` 从“targeted 单点通过”推进到 `host + stage2/stage3 libp2p` 正式 gate，并彻底收掉 snapshot 私有 codec 的活根。 |
| 主线 | 这轮已经在 [chain_node_libp2p.cheng](/Users/lbcheng/cheng-lang/v3/src/project/chain_node_libp2p.cheng) 把 `DecodeSignature/DecodeEvent` 从脆弱的 `Result[bool] + var nextOffset` 改成直接返回 `Result[int32]` 新偏移，同时把 `v3ChainNodeLibp2pReadI64BE(...)` 对齐到已经跑通的 `rwad_bft` 口径，彻底拔掉 snapshot 解码的偏移回写歧义。随后在 [chain_node_libp2p_smoke.cheng](/Users/lbcheng/cheng-lang/v3/src/tests/chain_node_libp2p_smoke.cheng) 补上 `payloadLen/payloadCid/raw payload` 校验，确认问题不在 `V3IngressEnvelope` 或 store 拷贝链；最后把 `chain_node_libp2p_smoke` 正式接进 [run_v3_host_smokes.sh](/Users/lbcheng/cheng-lang/v3/tooling/run_v3_host_smokes.sh) 和 [run_v3_stage23_libp2p_smokes.sh](/Users/lbcheng/cheng-lang/v3/tooling/run_v3_stage23_libp2p_smokes.sh)。真验收已经固定：targeted host 编译运行、`stage2` 真编真跑、`stage3` 真编真跑、整组 `run_v3_stage23_libp2p_smokes.sh`、整组 `run_v3_host_smokes.sh` 全部通过。 |
| 文件 | `v3/src/project/chain_node_libp2p.cheng` `v3/src/tests/chain_node_libp2p_smoke.cheng` `v3/tooling/run_v3_host_smokes.sh` `v3/tooling/run_v3_stage23_libp2p_smokes.sh` `task_plan.md` `progress.md` `findings.md` |
| 不做 | 不再把 snapshot 失败误判成 `store` 或 `Bytes` 搬运链；不保留 `Result[bool] + var nextOffset` 这种会把偏移回写藏进副作用的 codec 形状；不让 `chain_node_libp2p` 继续停留在手工 targeted 验证。 |
| 验收 | `./artifacts/v3_hostrun_targeted/chain_node_libp2p_smoke`、`./artifacts/v3_stage23_libp2p/chain_node_libp2p_smoke.stage2`、`./artifacts/v3_stage23_libp2p/chain_node_libp2p_smoke.stage3` 必须都输出 `v3 chain_node_libp2p_smoke ok`；`sh /Users/lbcheng/cheng-lang/v3/tooling/run_v3_stage23_libp2p_smokes.sh` 与 `sh /Users/lbcheng/cheng-lang/v3/tooling/run_v3_host_smokes.sh` 必须前台通过。 |

| 项目 | 内容 |
|---|---|
| 目标 | 把 `v3` 当前主 gate 收成 `run_slice_gate.sh` 全绿，并把 `host / stage2-stage3 / bootstrap` 三条线上残留的假失败一起关掉。 |
| 主线 | 这轮已把 [chain_node_smoke.cheng](/Users/lbcheng/cheng-lang/v3/src/tests/chain_node_smoke.cheng) 恢复 `signature一致` 正式断言；把 [system_link_plan.cheng](/Users/lbcheng/cheng-lang/v3/src/backend/system_link_plan.cheng) 的 `parserSourceKind` 从热点 `str` 收回整型 kind，只在 report 层转文本；同时 fresh 重跑 `bootstrap_bridge_v3.sh` 刷新 [artifacts/v3_bootstrap](/Users/lbcheng/cheng-lang/artifacts/v3_bootstrap) 下的 `cheng.stage2/cheng.stage3`，消掉旧自举产物把 `libp2p_protocols_smoke` 误报成 `stmt_var` 的假失败。当前真验收已经固定：`build_chain_node_v3.sh`、`run_v3_host_smokes.sh`、`run_v3_stage23_libp2p_smokes.sh`、`run_slice_gate.sh` 全部前台通过。 |
| 文件 | `v3/src/tests/chain_node_smoke.cheng` `v3/src/backend/system_link_plan.cheng` `v3/tooling/bootstrap_bridge_v3.sh` `v3/tooling/run_v3_host_smokes.sh` `v3/tooling/run_v3_stage23_libp2p_smokes.sh` `v3/tooling/run_slice_gate.sh` `task_plan.md` `progress.md` `findings.md` |
| 不做 | 不再把旧 `stage2/stage3` 产物拿来给新源码背锅；不把 `parserSourceKind` 这种热点 kind 退回 `str`；不再把 `chain_node` 的签名断言长期留在注释态。 |
| 验收 | `sh /Users/lbcheng/cheng-lang/v3/tooling/build_chain_node_v3.sh`、`sh /Users/lbcheng/cheng-lang/v3/tooling/run_v3_host_smokes.sh`、`sh /Users/lbcheng/cheng-lang/v3/tooling/run_v3_stage23_libp2p_smokes.sh`、`sh /Users/lbcheng/cheng-lang/v3/tooling/run_slice_gate.sh` 必须全部 fresh 前台通过。 |

| 项目 | 内容 |
|---|---|
| 目标 | 把 `v3` 的 libp2p 数据面从“只有 metadata”推进到“真实 payload bytes 可经 host/store/publish 流动”，并把它正式接进 `stage2/stage3` gate。 |
| 主线 | 这轮已经在 `v3/src/overlay/contracts.cheng` 给 `V3IngressEnvelope` 补上 `payload: Bytes`，新增 `v3/src/tests/libp2p_protocols_smoke.cheng` 真验 `topicId / publishLog / storeEntries / payloadLen / payloadCid / raw payload head`，同时把 `v3/tooling/run_v3_stage23_libp2p_smokes.sh` 扩成五个 smoke。fresh 结果已经固定：`libp2p_core_smoke`、`libp2p_host_smoke`、`libp2p_tcp_smoke`、`libp2p_overlay_smoke`、`libp2p_protocols_smoke` 全都能在 `stage2/stage3` 双编双跑，并以 `v3 stage2/stage3 libp2p: ok` 收尾。 |
| 文件 | `v3/src/overlay/contracts.cheng` `v3/src/tests/libp2p_protocols_smoke.cheng` `v3/tooling/run_v3_stage23_libp2p_smokes.sh` `task_plan.md` `progress.md` `findings.md` |
| 不做 | 不把空 payload 的 metadata 壳报成“协议已接通”；不跳过 `stage3` 只看 `stage2`；不把 `chain_node` 继续挂在 `ServeSnapshot/SyncOnce` 的内存直拷贝上。 |
| 验收 | `sh /Users/lbcheng/cheng-lang/v3/tooling/run_v3_stage23_libp2p_smokes.sh` 必须完整输出五个 smoke 的 `stage2/stage3` 真跑结果，并以 `v3 stage2/stage3 libp2p: ok` 收尾。 |

| 项目 | 内容 |
|---|---|
| 目标 | 把 `v3` 的 `libp2p_overlay_smoke` 真收进 `stage2/stage3` 主线，不再允许 `overlay/store/sync` 被 ordinary helper 壳拦住。 |
| 主线 | 这轮已经把 `src/std/rawbytes.cheng`、`src/std/crypto/sha256.cheng`、`v3/src/libp2p/**`、`v3/src/overlay/**` 和 `v3/src/tests/libp2p_overlay_smoke.cheng` 全部收成 ordinary 当前能稳定吃的形状：去掉复合 zero helper 链、去掉 `main -> helper` 包装、把 `sha256` 的 `u32/u32Mask` 嵌套表达式拆平、把 store query 验证收成 host 真实状态检查、把 `PeerIdZero/HostInit` 的冗余 zero 调用拔掉。fresh 结果已经固定：`run_v3_stage23_libp2p_smokes.sh` 现在能完整跑过 `libp2p_core_smoke`、`libp2p_host_smoke`、`libp2p_tcp_smoke`、`libp2p_overlay_smoke` 的 `stage2/stage3` 双编双跑，全链输出 `v3 stage2/stage3 libp2p: ok`。 |
| 文件 | `src/std/rawbytes.cheng` `src/std/crypto/sha256.cheng` `v3/src/overlay/contracts.cheng` `v3/src/overlay/libp2p_bridge.cheng` `v3/src/libp2p/core/types.cheng` `v3/src/libp2p/core/peer_id.cheng` `v3/src/libp2p/host/host.cheng` `v3/src/libp2p/protocols/store_sync.cheng` `v3/src/tests/libp2p_overlay_smoke.cheng` `task_plan.md` `progress.md` `findings.md` |
| 不做 | 不再让 `overlay` 继续依赖 `zero helper + wrapper helper + query shell` 的复合壳；不把 `stage2` 绿了但 `stage3` 还没跑的状态报成闭环；不回头重碰已经全绿的 `core/host/tcp` 假 blocker。 |
| 验收 | `sh /Users/lbcheng/cheng-lang/v3/tooling/run_v3_stage23_libp2p_smokes.sh` 必须完整输出四个 smoke 的 `stage2/stage3` 真跑结果，并以 `v3 stage2/stage3 libp2p: ok` 收尾。 |

| 项目 | 内容 |
|---|---|
| 目标 | 收掉 `v3` seed 对零参调用的脏栈假实参，让 `rawbytes/sha256/fixed256/compiler_pipeline` 重新回到 fresh 真编真跑。 |
| 主线 | 已在 `v3/bootstrap/cheng_v3_seed.c` 把所有会参与 `parse_call_text(...) || parse_prefix_single_arg_call(...)` 的 `arg_text/result_arg_text` 缓冲区收成确定初始化，并给 `V3ExprPrepScratch` 加了整块 `memset`。这样 `emptyBytes()`、`bytesAlloc()` 这类零参调用不再把旧栈字节误读成一个前缀参数。fresh 结果已经固定：`build_backend_driver_v3.sh`、`sha256_inline_smoke`、`sha256_runtime_smoke`、`sha256_core_smoke`、`get_u32be_smoke`、`sha256_schedule_smoke`、`sha256_round_smoke`、`fixed256_sha256_smoke`、`compiler_pipeline_stub_smoke` 全部真编真跑通过。 |
| 文件 | `v3/bootstrap/cheng_v3_seed.c` `v3/src/tests/sha256_inline_smoke.cheng` `v3/src/tests/sha256_runtime_smoke.cheng` `v3/src/tests/sha256_core_smoke.cheng` `v3/src/tests/get_u32be_smoke.cheng` `v3/src/tests/sha256_schedule_smoke.cheng` `v3/src/tests/sha256_round_smoke.cheng` `v3/src/tests/fixed256_sha256_smoke.cheng` `v3/src/tests/compiler_pipeline_stub_smoke.cheng` `task_plan.md` `progress.md` `findings.md` |
| 不做 | 不把零参调用再当成 prefix-call 猜；不把这条 seed 真 bug 继续误判成 `sha256` 算法错；不在 smoke 里保留只为绕过 seed 缺口的临时写法。 |
| 验收 | `build_backend_driver_v3.sh` 必须 fresh 通过；`sha256_inline_smoke`、`sha256_runtime_smoke`、`sha256_core_smoke`、`get_u32be_smoke`、`sha256_schedule_smoke`、`sha256_round_smoke`、`fixed256_sha256_smoke`、`compiler_pipeline_stub_smoke` 必须全部真编真跑通过。 |

| 项目 | 内容 |
|---|---|
| 目标 | 把 `v3` 当前 ordinary compiler 的真崩点彻底收掉，让 fresh `stage2/stage3` 重新稳定跑通 `compiler_runtime_smoke`，不再跳进 `__TEXT,__cstring`。 |
| 主线 | 已在 `v3/bootstrap/cheng_v3_seed.c` 修掉两处真根：一是 `v3_prepare_expr_call_state_impl(...)` 把 `V3ExprPrepScratch` 定长数组退化成 `char*` 后又拿 `sizeof(expr)` 当容量，导致 prepare 阶段把完整调用截成 7 字节；二是 prepare 把 `V3BackendBuildPlan(...)`、`V3CompilerRuntimeContract(...)`、`V3BackendSourceUnit(...)` 这类构造器当普通 runtime call，错误进入 `resolve_call_target + composite arg temp`。现在 prepare 已按真实数组容量递归，并把构造器改成“只递归字段表达式，不走运行时 call 准备”；同时保留 `frame layout` 硬校验，直接阻断 `string temp` 再踩保存的 `x29/x30`。fresh 结果已经固定：`compiler_runtime_smoke`、`compiler_pipeline_stub_smoke`、`build_twoway_search_smoke_v3.sh` 全绿，`v3BackendBuildPlanDefault` 新汇编帧已抬到 `1616`，字符串 scratch 在 `sp+1384`，保存的 `FP/LR` 在 `sp+1600`。 |
| 文件 | `v3/bootstrap/cheng_v3_seed.c` `v3/src/tests/compiler_runtime_smoke.cheng` `v3/src/tests/compiler_pipeline_stub_smoke.cheng` `v3/tooling/bootstrap_bridge_v3.sh` `v3/tooling/build_backend_driver_v3.sh` `v3/tooling/build_twoway_search_smoke_v3.sh` `task_plan.md` `progress.md` `findings.md` |
| 不做 | 不再靠 `lldb`/手工汇编定位来掩盖 seed 真 bug；不把构造器继续伪装成普通运行时 call；不保留本轮临时 `prepare/statement` 调试噪声。 |
| 验收 | `bootstrap_bridge_v3.sh`、`build_backend_driver_v3.sh` 必须 fresh 通过；`compiler_runtime_smoke`、`compiler_pipeline_stub_smoke`、`build_twoway_search_smoke_v3.sh` 必须都真跑通过；`compiler_runtime_smoke.primary.o.s` 中 `v3BackendBuildPlanDefault` 不得再把字符串 scratch 放进保存寄存器区。 |

| 项目 | 内容 |
|---|---|
| 目标 | 把 `v3` 的 `libp2p` stage2/stage3 主线先收回真实可跑状态：`peer_id + multiaddr(core shape) + core + host + tcp` 必须真编真跑，`overlay` 的 ordinary 缺口单独暴露，不准再被假根盖住。 |
| 主线 | 这轮已经把 `peer_id` 从“复合 `FixedBytes32` 返回”改成“native `sha256 word bridge` + 直接写 `out.value.data[0..31]`”，`stage2/stage3` 的 `libp2p_peer_id_smoke` 现已真过。`multiaddr` 解析链仍然会把 ordinary 拖进 `Result[Multiaddr]` / `__cstring` 崩点，所以先新增 `v3Libp2pMultiaddrFillTcp/FillQuic` 直接构造真实地址对象，把 `libp2p_core_smoke`、`libp2p_host_smoke` 从 parser 路径摘出来。当前 fresh 结果已经固定：`libp2p_core_smoke`、`libp2p_host_smoke`、`libp2p_tcp_smoke` 在 `stage2/stage3` 全绿；`libp2p_overlay_smoke` 继续诚实卡在 `byteBufView / v3HashInts / gossipsub / store_sync / host store&sync` 这组 ordinary 复合参数 lowering。 |
| 文件 | `v3/src/libp2p/core/peer_id.cheng` `v3/src/libp2p/core/multiaddr.cheng` `v3/src/tests/libp2p_core_smoke.cheng` `v3/src/tests/libp2p_host_smoke.cheng` `v3/src/tests/libp2p_multiaddr_smoke.cheng` `v3/src/tests/libp2p_multiaddr_tcp_smoke.cheng` `v3/src/tests/libp2p_multiaddr_quic_smoke.cheng` `v3/src/tests/libp2p_multiaddr_call_smoke.cheng` `src/runtime/native/system_helpers.c` `src/runtime/native/system_helpers.h` `task_plan.md` `progress.md` `findings.md` |
| 不做 | 不把 `overlay` 从正式脚本里偷偷删掉制造假绿；不再把 `multiaddr parser` 崩点伪装成网络栈已经打通；不重新引兄弟仓依赖。 |
| 验收 | `libp2p_peer_id_smoke`、`libp2p_core_smoke`、`libp2p_host_smoke`、`libp2p_tcp_smoke` 必须都能被 `stage2/stage3` 直接编译执行；`libp2p_overlay_smoke` 允许继续失败，但失败点只能是当前 ordinary 真缺口，不能回退成 `peer_id/multiaddr/core/host` 假根。 |

| 项目 | 内容 |
|---|---|
| 目标 | 把 `Two-way` 字符串搜索核直接落进 `v3/runtime/native`，替换 `v3` 当前 `contains` 热路径里的朴素 `memcmp` 扫描。 |
| 主线 | 新增 `v3/runtime/native/v3_str_twoway_search.h` 和 `v3_str_twoway_search_smoke.c`，把 bytes-aware `Two-way` 搜索核固定在 `v3/runtime/native`。`src/runtime/native/system_helpers.c`、`system_helpers_selflink_min_runtime.c`、`system_helpers_selflink_shim.c` 现已统一接入这个核；bridge 路径不再先丢掉 `str.len` 再回退到 `strlen`，而是优先吃固定布局 `ptr+len`。新增 `v3/tooling/build_twoway_search_smoke_v3.sh` 后，三份 runtime C 文件都能 fresh 编译，native 穷举 smoke 全绿，`compiler_pipeline_stub_smoke` 也继续真跑通过。 |
| 文件 | `v3/runtime/native/v3_str_twoway_search.h` `v3/runtime/native/v3_str_twoway_search_smoke.c` `v3/tooling/build_twoway_search_smoke_v3.sh` `src/runtime/native/system_helpers.c` `src/runtime/native/system_helpers_selflink_min_runtime.c` `src/runtime/native/system_helpers_selflink_shim.c` `v3/runtime/README.md` `task_plan.md` `progress.md` `findings.md` |
| 不做 | 不把 `Two-way` 藏回旧 `src/runtime/native` 私有代码里；不继续让 `driver_c_str_contains_str_bridge` 丢掉长度信息后再走朴素扫描；不保留 selflink 和主 runtime 两套不同搜索行为。 |
| 验收 | `build_twoway_search_smoke_v3.sh` 必须输出 `v3 twoway search smoke ok`；`compiler_pipeline_stub_smoke` 必须继续输出 `v3 compiler_pipeline_stub_smoke ok`。 |

| 项目 | 内容 |
|---|---|
| 目标 | 把 `v3` 的默认异常诊断升级成“机器级真相源”，崩溃时先给真实机器帧，再给 Cheng 源码映射，不再只靠源码栈猜位置。 |
| 主线 | `src/runtime/native/system_helpers.c` 现在已经把 `signal` 和 `panic/bounds` 两条链统一成 `machine-trace + source-trace` 双输出：`signal` 直接从 `ucontext` 取 `pc/fp/sp/lr` 并沿 FP 链展开；`panic/bounds` 走真实 `backtrace pc`，优先用内嵌 `PC range -> source span` 表映射，`.v3.map` 只退作 symbol fallback。`v3/tooling/build_signal_trace_v3.sh`、`build_bounds_trace_v3.sh`、`build_panic_trace_v3.sh` 也已收严，必须看到机器帧里的 `.cheng` 行号。 |
| 文件 | `src/runtime/native/system_helpers.c` `v3/tooling/build_signal_trace_v3.sh` `v3/tooling/build_bounds_trace_v3.sh` `v3/tooling/build_panic_trace_v3.sh` `v3/docs/cheng语言特性矩阵和开发计划.md` `task_plan.md` `progress.md` `findings.md` |
| 不做 | 不再把 `mapped_frames=0` 当成只能上 `lldb`；不回退成只靠源码栈字符串猜位置；不引入新的 sidecar/debug wrapper。 |
| 验收 | `build_signal_trace_v3.sh`、`build_bounds_trace_v3.sh`、`build_panic_trace_v3.sh` 必须全绿；三条日志都必须出现 `machine-trace` 头和带 `.cheng` 行号的 `m#` 机器帧；`compiler_runtime_smoke` 这种 `source-signal mapped_frames=0` 的真崩点也必须直接打印机器帧。 |

| 项目 | 内容 |
|---|---|
| 目标 | 把 `v3` 的默认异常堆栈彻底打通，并收掉 parser native bridge 的混合堆释放崩点。 |
| 主线 | `src/runtime/native/system_helpers.c` 里已经把 fatal signal 接管补到 `SIGTRAP/SIGFPE/SIGSYS`；这轮继续把 `cheng_v3_parser_module_to_source_bridge(...)`、`cheng_v3_parser_import_source_paths_bridge(...)`、`cheng_v3_parser_source_to_module_bridge(...)` 的空串和错误返回统一成 `cheng_v3_bridge_owned_str(...)`，不再一边走 `driver_c_str_from_utf8_copy_bridge(...)` 的 runtime 堆、一边走 `malloc`。结果已经固定：`build_panic_trace_v3.sh`、`build_bounds_trace_v3.sh`、`build_signal_trace_v3.sh` 都真编真跑通过，`compiler_pipeline_stub_smoke` 也重新真跑通过；`lowering_plan_smoke` 仍会崩，但现在默认直接打印 `.cheng` 行号和 native 栈，第一现场已经钉到 `v3LoweringFunctionSymbolText(...)`。 |
| 文件 | `src/runtime/native/system_helpers.c` `v3/tooling/build_panic_trace_v3.sh` `v3/tooling/build_bounds_trace_v3.sh` `v3/tooling/build_signal_trace_v3.sh` `task_plan.md` `progress.md` `findings.md` |
| 不做 | 不再靠 LLDB/汇编手工猜异常位置；不保留 mixed allocator 的桥接返回；不把 `lowering_plan_smoke` 的新崩点误判成“默认堆栈没接通”。 |
| 验收 | `build_panic_trace_v3.sh`、`build_bounds_trace_v3.sh`、`build_signal_trace_v3.sh` 必须全绿；`compiler_pipeline_stub_smoke` 必须真跑输出 `v3 compiler_pipeline_stub_smoke ok`；`lowering_plan_smoke` 即使崩，也必须默认打印 `v3/src/backend/lowering_plan.cheng` 的源码栈。 |

| 项目 | 内容 |
|---|---|
| 目标 | 继续推进 `v3` ordinary host smoke，把 `compiler_pipeline_stub_smoke` 从“编译不通”推进到 generated exe 的 parser `std/` import 真崩点，并给 `lowering_plan_smoke` 留下同一类返回链根因。 |
| 主线 | 这轮已经把 `v3/src/backend/system_link_plan.cheng` 的 report 全部改成 getter + 局部标量，不再把 `plan.xxx` 直接塞进调用参数，`compiler_pipeline_stub_smoke` 因而重新回到 `COMPILE_RC:0`。随后继续在 `v3/src/lang/parser.cheng` 收掉生成程序里最不稳的路径链：`v3ParserPackageId/v3ParserSourceExists/v3ParserFileStem/v3ParserDirPart/v3ParserModulePathToSourcePath/v3ParserSourcePathToModulePath` 都改成更线性的 ordinary shape，并把 parser 主链里对 `v3PathSplitFile` 和 parser 内部的 `v3PathJoin` 依赖拔掉，统一改成显式扫描和 `v3ParserJoinSlash(...)`。现在新的真结论已经钉住：`compiler_pipeline_stub_smoke` 顶层 `packageId/contractsPath/sourceExists` 都能跑过，`v3ParseOrdinarySourceStub(...)` 也能跑到 `parse5`，当前第一处活根已经缩到 `v3ParserReadImportEdges(...)` 里再次进入 `v3ParserModulePathToSourcePath(...)` 处理 `std/...` 导入时的字符串返回链；`lowering_plan_smoke` 仍保持 `COMPILE_RC:0`，继续死在同类 generated exe runtime 崩点。 |
| 文件 | `v3/src/backend/system_link_plan.cheng` `v3/src/lang/parser.cheng` `v3/src/tests/compiler_pipeline_stub_smoke.cheng` `task_plan.md` `progress.md` `findings.md` |
| 不做 | 不把 `compiler_pipeline_stub_smoke` 伪装成断言失败；不把调试 `echo` 永久留在源码里；不回头再怀疑已经跑通的 `system_link_plan` report 编译面。 |
| 验收 | `build_backend_driver_v3.sh` 必须继续 fresh 通过；`compiler_pipeline_stub_smoke` 必须稳定 `COMPILE_RC:0`，并把 generated exe 的第一处真崩点固定在 parser `std/` import 路径，而不是回退到 `system_link_plan` report 或更早的 `packageId/sourceExists`。 |

| 项目 | 内容 |
|---|---|
| 目标 | 继续把 `v3` ordinary host smoke 从 `lowering_plan_smoke` 往前推，并把 seed 的真阻塞从“语义缺口”推进到“复合参数 call lowering”这一层。 |
| 主线 | 这轮已经把 `v3/src/backend/lowering_plan.cheng` 再压了一轮：去掉 `strutils/ir` 依赖、把 `functionName/layout/callconv/report` 都收成更硬的 ordinary shape，`v3/src/tests/lowering_plan_smoke.cheng` 也改成只认 `lowering_plan` 自己导出的固定常量。随后直接在 `v3/bootstrap/cheng_v3_seed.c` 修了 `v3_prepare_expr_call_state(...)` 的大栈帧爆栈，把它的 scratch 从栈搬到堆，`EXC_BAD_ACCESS / ___chkstk_darwin` 这条崩栈已经消失。当前新的真 blocker 已经明确收窄成 seed 的“复合参数表达式没有稳定物化成临时局部”，表现为 `v3PathTrim(raw)`、`v3path.v3ReadTextFile("", sourcePath)` 这类 `str/result/seq` 参数只要不是现成局部变量，就会在 emit 阶段报 `composite-arg local missing`。下一刀直接补 seed 的 composite call-arg lowering，不再继续改 Cheng 源码外形。 |
| 文件 | `v3/src/backend/lowering_plan.cheng` `v3/src/tests/lowering_plan_smoke.cheng` `v3/bootstrap/cheng_v3_seed.c` `task_plan.md` `progress.md` `findings.md` |
| 不做 | 不回退去补旧 `v2` 壳；不拿加大栈或 launcher 兜底冒充修复；不继续把普通 Cheng 源码压成更丑的 shape 来掩盖 seed 的 call lowering 真缺口。 |
| 验收 | `build_backend_driver_v3.sh` 必须继续 fresh 真编通过；`lowering_plan_smoke` 必须不再死于 `___chkstk_darwin`，并把失败面稳定暴露为 `composite-arg local missing` 这一层 seed 问题。 |

| 项目 | 内容 |
|---|---|
| 目标 | 把原子级 `RWAD` 从底层 note 状态机继续推进到冷轨主接口，先闭合 `RWAD-BFT` 适配层，不把“单块只能一个 tx”当隐藏前提。 |
| 主线 | 这轮直接在 `v3/src/project/rwad_serial_state_machine.cheng` 补上 `orderKey = height + txIndex` 的有序应用语义，把 `lastFinalizedHeight` 和 `lastApplyOrderKey` 分开，允许块内多 tx 按确定性顺序连续落账；随后新增 `v3/src/project/rwad_bft_state_machine.cheng`，把原子级 note/nullifier 账本包成冷轨适配层，固定暴露 `checkTx/checkTxBytes/finalizeBlockBytes/queryOwner/queryAppHash`，并补 `rwad_bft_state_machine_main/smoke` 与 `build_rwad_bft_state_machine_v3.sh`。`run_v3_host_smokes.sh` 也已接入 `rwad_bft_state_machine_smoke`，现在整条链会先正式验收 `rwad_serial/rwad_bft`，再继续停到新的旧 blocker `lowering_plan_smoke`。 |
| 文件 | `v3/src/project/rwad_serial_state_machine.cheng` `v3/src/project/rwad_bft_state_machine.cheng` `v3/src/project/rwad_bft_state_machine_main.cheng` `v3/src/tests/rwad_bft_state_machine_smoke.cheng` `v3/tooling/build_rwad_bft_state_machine_v3.sh` `v3/tooling/run_v3_host_smokes.sh` `v3/docs/cheng语言特性矩阵和开发计划.md` `task_plan.md` `progress.md` `findings.md` |
| 不做 | 不把旧余额账本 `bft_state_machine` 混进原子级 note 实现；不把块内多 tx 偷偷建立在“同块只能 1 tx”的假设上；不先发明字节编解码和 zk 壳来掩盖主状态机接口还没闭合。 |
| 验收 | `rwad_bft_state_machine_smoke` 和 `build_rwad_bft_state_machine_v3.sh` 必须都由 fresh `artifacts/v3_backend_driver/cheng` 真编真跑通过；`run_v3_host_smokes.sh` 必须正式跑到 `rwad_serial_state_machine_smoke` 和 `rwad_bft_state_machine_smoke`，并把前线旧 blocker 前移到 `lowering_plan_smoke`。 |

| 项目 | 内容 |
|---|---|
| 目标 | 把 `RWAD` 原子级序列模型接进 `v3` 冷轨，先真落 `interval note + split transfer + nullifier + deterministic root`，不污染现有余额账本 `BFT-SMI`。 |
| 主线 | 这轮不去假装已经有完整 `accumulator/zk prover`，而是先把最小可验的存储层和结算层做实：新建 `v3/src/project/rwad_serial_state_machine.cheng`，固定 `noteId + start/end serial + ownerId + nullifier` 这组对象，支持 `mint` 连续区间、按输入 note 切割 `transfer`、双花拒绝、`ownerRoot/serialRoot/nullifierRoot/appHash` 查询；再补 `v3/src/tests/rwad_serial_state_machine_smoke.cheng` 用 fresh `v3` driver 真编真跑，并把 smoke 接进 `v3/tooling/run_v3_host_smokes.sh` 的前半段，保证后面就算还卡在 `compiler_pipeline_stub_smoke`，这条新冷轨也已经被正式纳入 host smoke。文档侧同步把这条实现写进 `v3/docs/cheng语言特性矩阵和开发计划.md`，明确“今天只真落区间账本和确定性摘要，不把未实现的 accumulator/zk 写成已完成”。 |
| 文件 | `v3/src/project/rwad_serial_state_machine.cheng` `v3/src/tests/rwad_serial_state_machine_smoke.cheng` `v3/tooling/run_v3_host_smokes.sh` `v3/docs/cheng语言特性矩阵和开发计划.md` `task_plan.md` `progress.md` `findings.md` |
| 不做 | 不把现有 `v3/src/project/bft_state_machine.cheng` 改回原子账本导致旧 smoke 回退；不引入文本帧、动态 record 壳或假 `zk` 接口；不把“计划中的 accumulator/batch proof”伪装成已经可用。 |
| 验收 | `artifacts/v3_backend_driver/cheng system-link-exec ... v3/src/tests/rwad_serial_state_machine_smoke.cheng ...` 必须 fresh 真编真跑输出 `v3 rwad_serial_state_machine_smoke ok`；`mint` 必须产出 `[1,100]`，`transfer` 必须把同一输入 note 切成左找零、支付、右找零三段，双花必须返回 `already_spent`，`commit` 必须稳定给出 `serialRoot/nullifierRoot/appHash`。 |

| 项目 | 内容 |
|---|---|
| 目标 | 继续推进 `v3` ordinary host smoke，把 `compiler_pipeline_stub_smoke` 从 source shape 阻塞推进到真正的 seed lowering 缺口。 |
| 主线 | 这轮已经把 `compiler_pipeline_stub_smoke` 自身和依赖的 `parser/system_link_plan` source shape 全部压平：测试里的嵌套 `if + echo` 已改成断言；`v3/src/lang/parser.cheng` 里的 `v3ParserFirstToken(...)` 改成显式步进扫描，`v3ParseOrdinarySourceStub(...)` 改成显式字段写回；`v3/src/backend/system_link_plan.cheng` 则去掉 nested composite arg，并把 build-plan 读取收成标量 helper。现在新的真结论已经钉死：`compiler_pipeline_stub_smoke` 不再被 `parser/path/runtime bridge` 卡住，唯一剩下的是 seed 对 `cheng/v3/backend/system_link_plan::v3BuildSystemLinkPlanStub` 这类 ordinary `Result[composite]` 组装函数还有 lowering 缺口。 |
| 文件 | `v3/src/tests/compiler_pipeline_stub_smoke.cheng` `v3/src/lang/parser.cheng` `v3/src/backend/system_link_plan.cheng` `v3/src/backend/build_plan.cheng` `task_plan.md` `progress.md` `findings.md` |
| 不做 | 不回退 `parser_path_smoke` 已收口的 bridge 修复；不把 `compiler_pipeline_stub_smoke` 伪装成 smoke 自身断言失败；不继续拿 source 侧改写掩盖 seed lowering 真缺口。 |
| 验收 | `/tmp/compiler_pipeline_stub_smoke.debug.log` 必须只剩 `system_link_plan` 这一处真阻塞，不再回退到 `parser` 或 smoke 主入口；下一刀直接进 seed，不再继续折腾 Cheng 源码外形。 |

| 项目 | 内容 |
|---|---|
| 目标 | 继续推进 `v3` ordinary host smoke，把 `parser_path_smoke` 真收口，并把新的第一处阻塞前移到 `compiler_pipeline_stub_smoke`。 |
| 主线 | 这轮已经把 `parser_path_smoke` 对应的真根连根拔了：`v3/bootstrap/cheng_v3_seed.c` 里的 `str == str` 不再把整个 `str` slot 地址直接喂给 `_cheng_strcmp`，而是 `==/!=` 改走 `_driver_c_str_eq_bridge`；同时 composite-return call 的字符串字面量 scratch 现在会让开一个 `str` 槽位，并把每层 `string_temp_stride` 预留量从“字面量个数”改成“字面量个数 + 1”，避免和 compare/return scratch 自踩。runtime 侧 `driver_c_str_slice_bridge(...)` 也改成手工组 `ChengStrBridge`，不再走会把短字符串结果桥坏的通用封装。`v3/src/lang/parser.cheng` 则把 `ModulePathToSourcePath` 直接内联成可编译 shape，`v3/src/tests/parser_path_smoke.cheng` 只保留最小短字符串切片回归。现在 `parser_path_smoke` 已经 fresh 真编真跑通过；新的第一处真 blocker 已前移到 `compiler_pipeline_stub_smoke`，卡在 `v3BuildSystemLinkPlanStub / v3SystemLinkPlanStubReport / v3ParseOrdinarySourceStub` 这组 ordinary 子集还没完全吃下的复合 `let/var` 和 `Result` 形状。 |
| 文件 | `v3/bootstrap/cheng_v3_seed.c` `src/runtime/native/system_helpers.c` `src/runtime/native/system_helpers.h` `src/runtime/native/system_helpers_selflink_min_runtime.c` `src/runtime/native/system_helpers_selflink_shim.c` `v3/src/tooling/path.cheng` `v3/src/lang/parser.cheng` `v3/src/tests/parser_path_smoke.cheng` `task_plan.md` `progress.md` `findings.md` |
| 不做 | 不回退到旧 `std/os` 依赖；不把 `parser_path_smoke` 的 runtime 问题伪装成测试误报；不把新的 `compiler_pipeline_stub_smoke` 阻塞误诊成 runtime bridge 问题。 |
| 验收 | `parser_path_smoke` 必须 fresh 真编真跑输出 `v3 parser_path_smoke ok`；`run_v3_host_smokes.sh` 必须把第一处失败前移到 `compiler_pipeline_stub_smoke`；短字符串切片 `v3TextSlice("std/strutils", 4, 8)` 必须由正式 smoke 覆盖。 |

| 项目 | 内容 |
|---|---|
| 目标 | 把 `v3` 默认主线正式收成冷热双轨：`libp2p underlay + LSMR overlay + Cheng <-> BFT state machine interface`，并先让 `overlay contracts` 与 `BFT-SMI` 都有独立真 smoke。 |
| 主线 | 这轮不再把“模块化金融路线”写成备选，而是直接把它写进 `v3/docs`、`v3/tooling` 和 `v3` 边界代码。`LSMR` 只保留 topology hint、relay、store、sync、trust；`RWAD/NAV` 只认 cold finalized。代码侧已经新增 `v3/src/overlay/contracts.cheng` 和 `v3/src/tests/overlay_contracts_smoke.cheng`，并在 `v3/src/project/bft_state_machine.cheng` 补上 `TxHeader/TxBody/BlockContext/TxCheckResult/queryAppHash` 这一层固定布局接口。当前 provider object 的 `cheng_dir_exists` 原型缺口已经补齐，`overlay_contracts_smoke` 也已经 fresh 真编真跑通过；新的真 blocker 已经前移到 `BFT-SMI` 自身：`v3BftStateMachine` 把权威账本包在嵌套复合字段里，当前 ordinary 对这类嵌套 `var` 状态更新不稳，导致 `block1 appliedCount` 仍然起不来。下一步不再回头碰文档或 runtime 壳，直接把 `BFT` 状态机进一步收成更硬的 flat state/out-param 形状。 |
| 文件 | `v3/docs/LSMR.md` `v3/docs/cheng语言特性矩阵和开发计划.md` `v3/docs/README.md` `v3/tooling/README.md` `v3/tooling/run_v3_host_smokes.sh` `v3/src/overlay/contracts.cheng` `v3/src/tests/overlay_contracts_smoke.cheng` `v3/src/project/bft_state_machine.cheng` `src/runtime/native/system_helpers.c` `task_plan.md` `progress.md` `findings.md` |
| 不做 | 不把 `LSMR` 重新写回全局金融共识；不把 `chain_node` 伪装成冷轨最终结算；不在 `v3` 里重写 `libp2p` underlay。 |
| 验收 | `overlay_contracts_smoke` 必须真编真跑；`bft_state_machine_smoke` 至少要把失败收窄到 `BFT-SMI` 自身，而不是宿主编译壳；`v3/docs` 必须明确写出 `LSMR != finality`、`routing topology != consensus topology`、`RWAD/NAV 只认 cold finalized`。 |

| 项目 | 内容 |
|---|---|
| 目标 | 继续推进 `Cheng <-> BFT state machine interface`，先在 `v3` 内部补最小固定布局边界，把强一致性结算壳和 `chain_node` 账本明确解耦。 |
| 主线 | 这轮先不引具体 `Tendermint/CometBFT` 宿主名字，而是直接在 `v3/src/project/bft_state_machine.cheng` 落 `tx/check/finalize/commit/query` 五个硬接口，并用固定宽度 `tx` 二进制编码把外部共识边界定住。`epoch/ganzhi/tick` 改成由 `block height + tx index` 派生；结算层直接复用 `consensus` 规则，不再把 `chain_node/LSMR` 的本地时钟和网络状态拖进强一致性接口。当前已确认 `CheckTx` 不能再靠 `var probe = machine` 浅拷贝预演，必须走纯预演/深拷贝路径；同时 fresh ordinary compile 还卡在 `BFT` 模块的复合输出 shape，上层接口要继续压向更硬的 out-param/fixed-layout 子集。 |
| 文件 | `v3/src/project/bft_state_machine.cheng` `v3/src/project/bft_state_machine_main.cheng` `v3/src/tests/bft_state_machine_smoke.cheng` `v3/tooling/build_bft_state_machine_v3.sh` `v3/tooling/run_v3_host_smokes.sh` `v3/docs/cheng语言特性矩阵和开发计划.md` `task_plan.md` `progress.md` `findings.md` |
| 不做 | 不把 `chain_node` 直接伪装成 BFT 状态机；不把具体 BFT 宿主协议写死进 Cheng 语义；不让文本 payload 或本地时钟继续穿过结算边界。 |
| 验收 | `bft_state_machine_smoke` 和 `build_bft_state_machine_v3.sh` 必须能 fresh 真编真跑；同一块高重放必须返回 `bad_height`；`checkTx/finalizeBlock/commit/queryBalance` 必须只走固定宽度数据面；`CheckTx` 不得再污染原状态机。 |

| 项目 | 内容 |
|---|---|
| 目标 | 继续推进 `v3` 自举与性能主线；这一轮先把热核 smoke 全部收口，并把新的第一处 ordinary host blocker 明确前移到 `compiler_runtime_smoke`。 |
| 主线 | 这轮已经把 `fixed_surface` 里的假混合失败彻底拆开：`fixed256_sha256_smoke`、`ref10_ashr_smoke`、`fixed256_curve25519_smoke` 现在都 fresh 真编真跑通过，`fixed_surface_smoke` 也已回到干净输出 `ok`。真正修掉 `x25519` 的不是改 `ref10` 算法，而是修 `v3/bootstrap/cheng_v3_seed.c` 里 `>>` 的 ordinary machine code：之前所有右移都硬发 `lsrv`，导致 `int64/int32` 负数右移语义错误；现在已经按 `signed/unsigned + i32/i64` 分流成 `asrv/lsrv`，并给 `i32` signed 路补了 `sxtw`。这刀落下后，错误公钥 `de2f...` 直接回正成 RFC 向量。与此同时，`v3/docs/LSMR.md` 和 `v3/docs/cheng语言特性矩阵和开发计划.md` 已补上默认安全边界、`local/regional/global` finality 与 `RWAD` 结算边界，以及 `LSMR` 安全 gate。当前 `run_v3_host_smokes.sh` 的新第一处真 blocker 已前移到 `compiler_runtime_smoke`：ordinary body 还没吃下 `stmt_let / stmt_if / return_expr` 这组语义。 |
| 文件 | `v3/bootstrap/cheng_v3_seed.c` `v3/src/tests/fixed_surface_smoke.cheng` `v3/src/tests/fixed256_sha256_smoke.cheng` `v3/src/tests/fixed256_curve25519_smoke.cheng` `v3/src/tests/ref10_ashr_smoke.cheng` `v3/tooling/run_v3_host_smokes.sh` `v3/docs/LSMR.md` `v3/docs/cheng语言特性矩阵和开发计划.md` `task_plan.md` `progress.md` `findings.md` |
| 不做 | 不再回到“手搓 `ashr64/ashr32` 修算法”那条错路；不把热核已闭合的结果写成编译器主链也已闭合；不把 `LSMR` 的边界重新混回愿景话术。 |
| 验收 | `fixed256_sha256_smoke`、`ref10_ashr_smoke`、`fixed256_curve25519_smoke`、`fixed_surface_smoke` 必须全部前台输出 `ok`；`run_v3_host_smokes.sh` 必须把第一处失败前移到 `compiler_runtime_smoke`；`v3/docs/LSMR.md` 和 `v3/docs/cheng语言特性矩阵和开发计划.md` 必须显式写出安全边界、结算边界和不承诺项。 |

| 项目 | 内容 |
|---|---|
| 目标 | 把 `v3` ordinary host 主链从 `bagua/state forest` 段错推进到 `chain_node` 自测真跑通过，同时把暴露出来的 seed/shape 真根记清楚。 |
| 主线 | 这轮已经在 `v3/bootstrap/cheng_v3_seed.c` 修掉 `add(seq, complex rhs)` 的真实 lowering bug：`cheng_seq_set_grow` 返回的新元素槽位现在会先 spill，再算右值，再写回，所以 `add(pair.left, pair.right[0])` 这类路径不再把槽位地址踩成 `0x2`。新增 `seq_add_member_index_rhs_smoke` 后，`bagua_prefix_tree_fill_smoke` 已 fresh 真编真跑输出 `ok`，`v3BuildBaguaPrefixTreeFill(...)` 不再是活根。继续往前推后，`chain_node` 的新真 blocker 收窄成 `v3ChainNodeTransfer(...)` 里“原地逐字段拼 transfer event”这一个 source shape；把这段抽成 `v3ChainNodeBuildTransferEvent(...)` 后，`chain_node_transfer_wrapper_forms_smoke`、`chain_node_smoke` 和正式 `build_chain_node_v3.sh` 已全部前台通过。下一刀不再回头碰这条已闭合主链，直接把 `fixed_surface_smoke` 和更大的 `stage2/stage3` 真编真跑验收接上。 |
| 文件 | `v3/bootstrap/cheng_v3_seed.c` `v3/src/project/chain_node.cheng` `v3/src/tests/seq_add_member_index_rhs_smoke.cheng` `v3/src/tests/bagua_prefix_tree_fill_smoke.cheng` `v3/src/tests/chain_node_transfer_wrapper_forms_smoke.cheng` `v3/src/tests/chain_node_smoke.cheng` `v3/tooling/build_chain_node_v3.sh` `task_plan.md` `progress.md` `findings.md` |
| 不做 | 不把 `v3BuildBaguaPrefixTreeFill(...)` 的旧段错再误诊成 `LSMR` 算法错；不回退到任何 `v2` 路径；不把诊断 smoke 当最终验收面。 |
| 验收 | `bootstrap_bridge_v3.sh`、`build_backend_driver_v3.sh`、`seq_add_member_index_rhs_smoke`、`bagua_prefix_tree_fill_smoke`、`chain_node_transfer_wrapper_forms_smoke`、`chain_node_smoke`、`build_chain_node_v3.sh` 必须前台通过。 |

| 项目 | 内容 |
|---|---|
| 目标 | 继续推进 `v3` ordinary host smoke，先把 `lsmr_types_smoke` 的 `str` 复合返回比较和字符串拼接寄存器踩坏收口，再确认新的第一处真 blocker。 |
| 主线 | 这轮已经在 `v3/bootstrap/cheng_v3_seed.c` 把 ordinary `==/!=/<...>` 里的 `str/composite` 比较正式接通：左右两侧现在会先物化成 `str`，转成 C string 后走 `cheng_strcmp`，不再把 `lt.v3PrefixRefText(...)` 当 `i64` 标量比较。同时，字符串字面量标签也改成“没传 `string_label_index_io` 也能唯一生成”，这样 compare 分支和其他复合物化路径都能直接吃字符串字面量。继续深挖运行时崩溃后，又把 `str` 拼接和 compare 两处跨 `bl` 错信 `x15/x14` 还活着的寄存器用法收掉，统一改成“调用后重取 scratch/dest 地址”。现在 `lsmr_types_smoke` 已经 fresh 真编真跑输出 `v3 lsmr_types_smoke ok`。新的第一处真 blocker 已切到 `fixed_surface_smoke`，它不再是单点 `lsmr` 缺口，而是大 crypto surface 的 ordinary 子集还缺 `if-expr` 标量化、`uint32/uint64` cast、若干复合类型 layout/alias 解析和反向 `for` range。 |
| 文件 | `v3/bootstrap/cheng_v3_seed.c` `v3/src/tests/lsmr_types_smoke.cheng` `v3/src/tests/fixed_surface_smoke.cheng` `task_plan.md` `progress.md` `findings.md` |
| 不做 | 不把 `lsmr_types_smoke` 的编译问题继续误诊成 `Result + [6]`；不再让 `str` compare/concat 依赖 caller-saved 寄存器碰运气；不把 `fixed_surface_smoke` 现在的大面积失败假装成单一测试问题。 |
| 验收 | `cc -std=c11 -O0 -Wall -Wextra -pedantic v3/bootstrap/cheng_v3_seed.c -o /tmp/cheng_v3_seed_check`、`bootstrap_bridge_v3.sh`、`build_backend_driver_v3.sh`、`artifacts/v3_backend_driver/cheng system-link-exec ... lsmr_types_smoke.cheng ...` 必须前台通过；`artifacts/v3_hostrun/lsmr_types_smoke.singlev3` 必须真输出 `v3 lsmr_types_smoke ok`。 |

| 项目 | 内容 |
|---|---|
| 目标 | `v2` 清理已经完成；当前主线改成只收口 fresh `v3` rebuild 暴露出来的 ordinary ABI 真 blocker，不再为任何脚本、文档或产物保留 `v2` 回路。 |
| 主线 | 这轮已经把 [run_v3_host_smokes.sh](/Users/lbcheng/cheng-lang/v3/tooling/run_v3_host_smokes.sh) 改成只认 `artifacts/v3_backend_driver/cheng`，并让它缺 driver 时自动走 `build_backend_driver_v3.sh`；同时把 `v3/docs` 里所有 `v2/` 活路径和死链接收掉，随后物理删除了 `v2`、旧 `artifacts`、`chengcache`、`.cheng-cache`，再 fresh 跑通 `bootstrap_bridge_v3.sh` 和 `build_backend_driver_v3.sh`。现在 `rg -n '\\bv2/|cheng_v2c|cheng_v2_system|probe_currentsrc_proof/cheng_stage0_currentsrc\\.proof' v3` 已为空，`artifacts` 里也只剩 fresh `v3_bootstrap/v3_backend_driver/v3_hostrun/v3_chain_node` 等 `v3` 产物，没有旧 `v2` 名字。clean rebuild 额外诚实暴露两条 `v3` 自身真根：一是 `fixed_surface_smoke` 仍停在 `ordinary body semantics missing`，二是 `bagua_prefix_tree_fill_smoke/chain_node` 仍卡在 `v3BuildBaguaPrefixTreeFill(...)` 的 ordinary ABI 崩点。已经顺手收掉的确定性 bug 有三处：`v3SortStateCells(...)` 的 `while j >= 0 && ...` 会越界读 `cells[-1]`，现在已拆成两段判定；`v3BuildBaguaPrefixTreeFill(...)` 里 `cells = cellsRaw` 这种 seq 头整体拷贝已改成逐元素复制；死掉的 `v3TreeNodeMatches/v3EnsureTreeNodeFlat/v3HashIntsBytes` 已删除。下一刀不再碰 `v2`，直接盯 `v3BuildBaguaPrefixTreeFill(...)` 早段 crash，把 `bagua_prefix_tree_fill_smoke -> state_layer_forest_fill_smoke -> build_chain_node_v3.sh` 重新打通。 |
| 文件 | `v3/tooling/run_v3_host_smokes.sh` `v3/docs/README.md` `v3/docs/v2已踩性能和自举坑.md` `v3/docs/baguaCSG.md` `v3/docs/自举和性能.md` `v3/docs/LSMR.md` `v3/src/chain/lsmr.cheng` `v3/src/chain/lsmr_types.cheng` `v3/src/project/chain_node.cheng` `task_plan.md` `progress.md` `findings.md` |
| 不做 | 不恢复 `v2` 目录给脚本兜底；不把 `run_v3_host_smokes.sh` 改回旧 host compiler；不把 `bagua/state forest` 崩溃误判成链算法语义错；不为了过当前 gate 再塞回 `v2` 产物。 |
| 验收 | `rg -n '\\bv2/|cheng_v2c|cheng_v2_system' v3` 已为空；`bootstrap_bridge_v3.sh` 和 `build_backend_driver_v3.sh` 已 fresh 前台通过；下一阶段另行验收 `bagua_prefix_tree_fill_smoke`、`state_layer_forest_fill_smoke`、`build_chain_node_v3.sh`。 |

| 项目 | 内容 |
|---|---|
| 目标 | 继续把 `v3` ordinary/host smoke 往前推，先收掉复合 `seq[index]` 读值错写回和 `lsmr` 丢失的 `v3LsmrAddressBagua`，再继续逼近下一条真正的 ordinary lowering 缺口。 |
| 主线 | 这轮已经在 `v3/bootstrap/cheng_v3_seed.c` 把 `seq[index]` 取址里的内部 spill 正式分槽：`dest_addr`、`base`、`index` 不再共用 `call_dest` 那一个槽，所以 `FixedBytes32[]` 读值不再把 `_copyMem` 目标写回到序列头。与此同时，`v3/src/chain/lsmr.cheng` 也已补回 `v3LsmrAddressBagua(...)`，并把 `v3LsmrLocalityHashText(...)` 收成 `splitWhitespace + join(\" \") + toLowerAscii` 规范化，`lsmr_locality_storage_smoke` 已 fresh 真跑通过。下一刀不再回头碰这两条，直接正面补 `lsmr_types_smoke` 暴露出来的 ordinary lowering 缺口：`Result[V3PrefixRef]` 这种复合返回 + 非空列表字面量 + `str` 复合返回比较。 |
| 文件 | `v3/bootstrap/cheng_v3_seed.c` `v3/src/chain/lsmr.cheng` `v3/tooling/run_v3_host_smokes.sh` `v3/src/tests/fixedbytes32_seq_index_smoke.cheng` `v3/src/tests/lsmr_locality_storage_smoke.cheng` `v3/src/tests/lsmr_types_smoke.cheng` `task_plan.md` `progress.md` `findings.md` |
| 不做 | 不把 `FixedBytes32[]` 断言失败误判成 `bytes_layout` 算法错；不再让 host smoke 复用旧产物冒充 fresh 结果；不把 `lsmr_types_smoke` 现在的失败误诊成 `lsmr` 模块字段缺失。 |
| 验收 | `fixedbytes32_seq_index_smoke` 必须由 fresh `artifacts/v3_backend_driver/cheng` 真编真跑输出 `ok`；`lsmr_locality_storage_smoke` 必须 fresh 真编真跑输出 `ok`；`run_v3_host_smokes.sh` 必须能 fresh 跑到新的第一处真 blocker，并留下编译日志。 |

| 项目 | 内容 |
|---|---|
| 目标 | 把 `v3` 的默认异常栈真正收成“panic / bounds / signal` 都直接打印 Cheng 源码栈 + native 栈”，不再让 `bounds` 停在只打一行错误或根本没进 runtime 检查。 |
| 主线 | 这轮先在 `src/runtime/native/system_helpers.c` 把 `bounds` 收进统一异常栈入口，保留 `reason=bounds`；随后继续在 `v3/bootstrap/cheng_v3_seed.c` 给 ordinary `[]` 地址计算补上真实 `cheng_bounds_check` 调用，并把 `base/index` 在索引表达式求值和 check 调用前后都 spill/reload，防止复杂索引把地址寄存器冲掉。最后新增 `ordinary_bounds_trace_fixture + build_bounds_trace_v3.sh`，把“合法索引仍正常、越界默认出源码栈和 native 栈”收成正式 gate。 |
| 文件 | `src/runtime/native/system_helpers.c` `v3/bootstrap/cheng_v3_seed.c` `v3/src/tests/ordinary_bounds_trace_fixture.cheng` `v3/tooling/build_bounds_trace_v3.sh` `v3/tooling/run_slice_gate.sh` `v3/tooling/README.md` `task_plan.md` `progress.md` `findings.md` |
| 不做 | 不把 `bounds` 继续留成 `BOUNDS_TRACE` 环境变量开关；不把“越界没炸”误判成 runtime 栈没接通；不去碰 selflink 副本里根本不存在的 `cheng_bounds_check`。 |
| 验收 | `cc -std=c11 -O0 -Wall -Wextra -pedantic v3/bootstrap/cheng_v3_seed.c -o /tmp/cheng_v3_seed_check`、`bootstrap_bridge_v3.sh`、`build_backend_driver_v3.sh`、`build_bounds_trace_v3.sh`、`build_panic_trace_v3.sh`、`build_signal_trace_v3.sh` 必须前台通过；`build_bounds_trace_v3.sh` 必须真打印 `ordinary_bounds_trace_fixture.cheng` 行号和 native backtrace。 |

| 项目 | 内容 |
|---|---|
| 目标 | 把这轮 `v3` wrapper smoke 的假红收回真实语义，确认 seed 并没有在“precall + var index + 多复合参数”这条线上重新炸掉。 |
| 主线 | 这轮先用新补的 `member_index_call_smoke` 和 `bytes_param_helper_smoke` 真编真跑，确认 `member[index(call)]` 和 `Bytes, Bytes` 复合参数 helper 都已经打通；随后把一串看起来像编译器回归的 `consensus_*wrapper*_smoke` 横向扫了一遍，最后定位到真根不是 lowering，而是 smoke 自己把 `accountIds.cap == 1` 写成了硬规则。运行时 `cheng_seq_set_grow()` 首次扩容最小就是 `4`，所以正确验收口径只能是 `cap >= 1` 或 `cap >= len`，不能把增长策略当语言语义。 |
| 文件 | `v3/src/tests/member_index_call_smoke.cheng` `v3/src/tests/bytes_param_helper_smoke.cheng` `v3/src/tests/consensus_fixed32equal_precall_smoke.cheng` `v3/src/tests/consensus_apply_prefix_wrapper_smoke.cheng` `v3/src/tests/consensus_event_validate_wrapper_smoke.cheng` `v3/src/tests/consensus_event_let_validate_wrapper_smoke.cheng` `v3/src/tests/consensus_inline_fixed32_precall_smoke.cheng` `v3/src/tests/consensus_event_helper_validate_wrapper_smoke.cheng` `v3/src/tests/consensus_eventpos_wrapper_smoke.cheng` `src/runtime/native/system_helpers.c` `task_plan.md` `progress.md` `findings.md` |
| 不做 | 不把 runtime 扩容策略误诊成 seed/ABI 回归；不为假红去改 `v3/bootstrap/cheng_v3_seed.c`；不把 `cap == 1` 这种实现细节继续留在 gate 里。 |
| 验收 | `member_index_call_smoke`、`bytes_param_helper_smoke`、`consensus_fixed32equal_precall_smoke`、`consensus_apply_prefix_wrapper_smoke`、`consensus_event_validate_wrapper_smoke`、`consensus_event_let_validate_wrapper_smoke`、`consensus_inline_fixed32_precall_smoke`、`consensus_event_helper_validate_wrapper_smoke`、`consensus_eventpos_wrapper_smoke` 都必须由 fresh backend driver 真编真跑通过。 |

## 当前任务

| 项目 | 内容 |
|---|---|
| 目标 | 继续推进 `v3` 自举与性能主线，先把 seed 编译器里刚暴露出来的二元表达式 spill / 栈帧覆盖真根收口，再继续往 `chain_node` 与更大真实链路推进。 |
| 主线 | 这轮已经在 `v3/bootstrap/cheng_v3_seed.c` 把二元表达式改成“左值先 spill，再算右值”，并把嵌套二元表达式统一下沉一层 `call_depth`；同时给栈帧顶部加了 16 字节 guard，彻底隔开 spill 区和保存的 `x29/x30`。这直接修通了 `sha256_core_smoke`、`get_u32be_smoke`、`sha256_distinct_smoke`、`consensus_init_smoke`、`consensus_transfer_apply_smoke`。下一刀不回头改算法源码，直接沿这条线继续查“嵌套 `index[call(...)]` / 多复合参数用户函数”这种还没完全收口的表达式 lowering。 |
| 文件 | `v3/bootstrap/cheng_v3_seed.c` `v3/src/tests/sha256_core_smoke.cheng` `v3/src/tests/get_u32be_smoke.cheng` `v3/src/tests/sha256_distinct_smoke.cheng` `v3/src/tests/consensus_init_smoke.cheng` `v3/src/tests/consensus_transfer_apply_smoke.cheng` `v3/src/tests/bytebuf_probe_smoke.cheng` `task_plan.md` `progress.md` `findings.md` |
| 不做 | 不回退到 `v2` payload bridge；不把 `sha256/consensus` Cheng 源码改成 C 热壳；不保留这轮的调试噪音输出。 |
| 验收 | `bootstrap_bridge_v3.sh` 与 `build_backend_driver_v3.sh` 必须基于当前 seed 前台通过；`sha256_core_smoke`、`get_u32be_smoke`、`sha256_distinct_smoke`、`consensus_init_smoke`、`consensus_transfer_apply_smoke` 必须由 fresh backend driver 真编真跑通过。 |

| 项目 | 内容 |
|---|---|
| 目标 | 把 `FFI generation:index handle` 接进当前能真兜底的回归面，同时把这轮 signal source-trace 补丁收成“host compiler 不带 getter 也能链接、sidecar 读失败也照样装 handler”。 |
| 主线 | 这轮不去硬扩 `v3 host smoke` 的 `importc_fn` 运行面，因为它现在真会在运行时直接报 `unsupported callee`。正确路径是：继续保留 `src/runtime/native/system_helpers.c` 里的 `generation:index` 实现，补上 `dlsym` 缓存内嵌 line-map getter，让 host compiler 产物在没有 `_cheng_v3_embedded_line_map_*` 时也能干净链接；同时把 `cheng_v3_register_line_map_from_argv0` 收正成“不管 `.v3.map` 载入成不成功都先装 signal handler”。`FFI` 验收面则落到 backend gate：`verify_backend_ffi_handle_sandbox.sh` 现在真跑 C probe，直接验证 stale handle 拒绝和 `h1 != h0`；`@ffi_handle` fixture 这边先收成 compile gate，因为当前可用 runtime 还跑不了 `importc_fn`，但会强制编过、强制检查 `@ffi_handle` 注解元数据已进 primary object。 |
| 文件 | `src/runtime/native/system_helpers.c` `src/tooling/cheng_tooling_embedded_scripts/verify_backend_ffi_handle_sandbox.sh` `src/tooling/cheng_tooling_embedded_part3.cheng` `tests/cheng/backend/cheng-package.toml` `tests/cheng/backend/fixtures/ffi_importc_handle_sandbox_i32.cheng` `task_plan.md` `progress.md` `findings.md` |
| 不做 | 不把一个当前必炸的 `importc_fn` smoke 硬塞进 `v3/tooling/run_v3_host_smokes.sh`；不碰 `src/std/system_helpers_backend.cheng` 的已有句柄实现；不碰用户正在改的 `consensus` 相关文件。 |
| 验收 | `sh src/tooling/cheng_tooling_embedded_scripts/verify_backend_ffi_handle_sandbox.sh` 必须前台通过；其中 C probe 必须真跑通过，`@ffi_handle` fixture 必须真编过并检出 `signature_line.1=@ffi_handle`；`sh v3/tooling/build_signal_trace_v3.sh` 必须继续前台通过，并真打印 `ordinary_signal_trace_fixture.cheng:2-6`。 |

| 项目 | 内容 |
|---|---|
| 目标 | 把 `v3` linkerless dev 轨继续推进到“异步 `SIGSEGV/SIGBUS` 也能直接回 `.cheng` 文件和行号”，不再只覆盖同步 `panic/assert/bounds`。 |
| 主线 | 这轮直接在 `v3/bootstrap/cheng_v3_seed.c` 里把函数边界标签和极简 line-map 真编进 primary object：每个 ordinary 函数和 entry bridge 都有 `start/end` 边界，源码路径/函数名/行号表落在 `__DATA_CONST,__const`，并导出 `_cheng_v3_embedded_line_map_{entries,count}_get`。运行时侧继续复用 `src/runtime/native/system_helpers.c`，把 crash handler 收成 `SA_SIGINFO`，只用 `PC + frame pointer walk + write(2,...)` 做 async-safe 源码栈反查，不碰 `backtrace/dladdr/fprintf`。验收侧新增 `ordinary_signal_trace_fixture + build_signal_trace_v3.sh`，真编 ordinary 可执行、真跑进 Cheng 循环、再外部注入 `SIGSEGV` 验证源码定位。 |
| 文件 | `v3/bootstrap/cheng_v3_seed.c` `src/runtime/native/system_helpers.c` `v3/src/tests/ordinary_signal_trace_fixture.cheng` `v3/tooling/build_signal_trace_v3.sh` `v3/tooling/run_slice_gate.sh` `v3/tooling/README.md` `task_plan.md` `progress.md` `findings.md` |
| 不做 | 不去碰 `DWARF/dSYM` 那条外部符号轨；不在 signal handler 里塞 `backtrace/dladdr` 这类不安全调用；不借这轮顺手扩 `importc` ordinary lowering 子集；不回滚仓库里已有的用户改动。 |
| 验收 | `cc -std=c11 -O0 -Wall -Wextra -pedantic v3/bootstrap/cheng_v3_seed.c -o /tmp/cheng_v3_seed_check` 必须通过；`build_backend_driver_v3.sh`、`build_zero_exit_v3.sh`、`build_call_chain_v3.sh`、`build_panic_trace_v3.sh`、`build_signal_trace_v3.sh` 必须前台通过；signal fixture 的 stderr 必须出现 `ordinary_signal_trace_fixture.cheng:2-6`。 |

| 项目 | 内容 |
|---|---|
| 目标 | 把 `v3` 文档总口径收正成可执行硬边界，直接给后续实现让路：`LSMR` 不再把稳定地址绑死绝对地理，`FFI` 句柄明确锁成 `generation:index`，debug 收成“外部符号 + linkerless 内嵌行号表”双轨，`RWAD/UniMaker` 锁死“finalized reserve 才能进 NAV、商户标价与 RWAD 结算强制解耦”。 |
| 主线 | 这轮不碰 `v3/bootstrap` 和 ordinary compile 活根，只收正 `v3/docs/LSMR.md` 与 `v3/docs/cheng语言特性矩阵和开发计划.md` 的硬边界，同时把 `task_plan/progress/findings` 同步成新口径。重点不是再加愿景，而是删掉“物理上必然接近”“未 Allocated 也能折价进 NAV”“dev 轨只靠裸地址 backtrace”这类会误导后续实现的错口径。 |
| 文件 | `v3/docs/LSMR.md` `v3/docs/cheng语言特性矩阵和开发计划.md` `task_plan.md` `progress.md` `findings.md` |
| 不做 | 不改 `bagua/BPI` sidecar-only 边界；不碰 `v3` 编译器代码；不把热点前缀扩容写成“重写顶层地址”；不把 `DWARF/dSYM` 和 linkerless panic 反查做成二选一。 |
| 验收 | `LSMR.md` 必须明确写出“稳定前缀 + 动态延迟坐标 + 桶上限”口径；总览文档必须把 `FFI/debug/RWAD/UniMaker` 的硬规则写成正式矩阵；任务记录必须同步这轮收正规则。 |

| 项目 | 内容 |
|---|---|
| 目标 | 继续把 `v3` seed ordinary compile 从“标量/指针子集”推进到 `chain_node` 真主链，这一刀先补无注解 `let/var` 的类型反推和局部槽位 `type_text`，为后面的复合 ABI 铺路。 |
| 主线 | 这轮已经在 `v3/bootstrap/cheng_v3_seed.c` 接上 `expr -> type/abi` 反推：字符串字面量、标量字面量、top-level const、本地槽位、单参 cast、普通函数调用返回类型、首字母大写的 record 构造现在都能反推出 `type_text/abi_class`；`V3AsmLocalSlot` 也开始显式记录 `type_text`，无注解 `let/var` 不再一上来就要求源码显式标类型。前台验收上，`cc -std=c11 -O0 -Wall -Wextra -pedantic v3/bootstrap/cheng_v3_seed.c`、`build_backend_driver_v3.sh`、`build_program_selfhost_v3.sh` 继续通过，`chain_node` 首个 blocker 仍稳定是 `v3ChainNodeMainSelfTest` 的 `var server = node.v3ChainNodeInit(...)`。这说明“类型反推”这层已经补上了，下一刀该直接做固定布局表和复合 ABI，而不是再在 `let/var` 注解面打转。 |
| 文件 | `v3/bootstrap/cheng_v3_seed.c` `v3/src/project/chain_node_main.cheng` `v3/src/project/chain_node.cheng` `src/std/system.cheng` `src/std/result.cheng` `src/std/rawbytes.cheng` `src/std/seqs.cheng` `task_plan.md` `progress.md` `findings.md` |
| 不做 | 不把“无注解绑定”再退回必须手写类型；不拿 `program_selfhost` 继续通过来伪装 `chain_node` 已经接通；不把当前真阻塞改写成别的名字。 |
| 验收 | `cc -std=c11 -O0 -Wall -Wextra -pedantic v3/bootstrap/cheng_v3_seed.c` 必须通过；`build_backend_driver_v3.sh`、`build_program_selfhost_v3.sh` 必须继续通过；`build_chain_node_v3.sh` 必须继续稳定卡在 `v3ChainNodeMainSelfTest stmt_var`，证明类型反推已补上但复合 ABI 还没做。 |

| 项目 | 内容 |
|---|---|
| 目标 | 继续把 `v3` seed ordinary compile 从“标量/指针子集”推进到 `chain_node` 真主链，同时先清掉会误导判断的假 blocker。 |
| 主线 | 这轮已经在 `v3/bootstrap/cheng_v3_seed.c` 接上 top-level `const` 收集与标量常量解析，`v3LsmrDigitOk`、`v3BaguaValid` 这类 `return_expr/if` 假阻塞已从 `chain_node` 的 unsupported 列表里消失；与此同时，name-only 重载解析也已收紧成“单候选才放行”，不再允许 `rawmemAsVoid` 这类重载静默错绑到第一条函数体。当前 `build_backend_driver_v3.sh`、`scan_forbidden_hotpath.sh` 继续通过，`chain_node` 的首个真实阻塞没有再漂，仍然稳定是 `chain_node_main::v3ChainNodeMainSelfTest` 的 `var server = node.v3ChainNodeInit(...)`。这说明下一刀必须正面补 `复合 ABI + 无注解 let/var + 字段读写`，而不是继续在标量叶子、常量名或重载假点上打转。 |
| 文件 | `v3/bootstrap/cheng_v3_seed.c` `v3/src/project/chain_node_main.cheng` `v3/src/project/chain_node.cheng` `v3/src/chain/binary_types.cheng` `v3/src/chain/lsmr_types.cheng` `task_plan.md` `progress.md` `findings.md` |
| 不做 | 不把 `rawmemAsVoid` 的 name-only 错绑继续留着；不回头补旧 proof/sidecar；不把 `chain_node` 的主阻塞伪装成“还是 bool/const 叶子没通”。 |
| 验收 | `cc -std=c11 -O0 -Wall -Wextra -pedantic v3/bootstrap/cheng_v3_seed.c` 必须通过；`build_backend_driver_v3.sh` 必须通过；`scan_forbidden_hotpath.sh` 必须通过；`chain_node.compile.log` 里 `v3LsmrDigitOk`、`v3BaguaValid` 不能再出现在 `primary_object_unsupported_functions`；首个 blocker 必须继续稳定停在 `v3ChainNodeMainSelfTest stmt_var`，证明真根已经收窄到复合值 ABI。 |

| 项目 | 内容 |
|---|---|
| 目标 | 把 `v3` seed ordinary compile 从“只有 typed plan”推进到“最小 ordinary 子集真发 `primary .o`、真编 provider `.o`、真链接并真跑”，同时把 `program-selfhost` 和 `chain_node` 的阻塞收正成单一函数体语义缺口。 |
| 主线 | 这轮已经在 `v3/bootstrap/cheng_v3_seed.c` 里补上 strict body-kind 识别、arm64 汇编 primary object 物化、provider `.o` 编译和真实 native link。普通 program 入口 ABI 也已经和 `v3/runtime/native/v3_program_argv_native.c` 对齐，不再错导出 `_main`。结果是 `build_backend_driver_v3.sh`、`run_v3_host_smokes.sh`、`build_zero_exit_v3.sh` 全部前台通过；`build_program_selfhost_v3.sh` 和 `build_chain_node_v3.sh` 现在统一稳定报 `v3 compiler: primary object body semantics missing`。这说明 `.o` 和链接链路已经是真的，剩下只该扩 ordinary body 语义子集。 |
| 文件 | `v3/bootstrap/cheng_v3_seed.c` `v3/runtime/native/v3_program_argv_native.c` `v3/runtime/native/v3_tooling_argv_native.c` `v3/runtime/native/v3_core_runtime_stub.c` `v3/tooling/build_zero_exit_v3.sh` `v3/tooling/build_program_selfhost_v3.sh` `v3/tooling/build_chain_node_v3.sh` `v3/tooling/run_slice_gate.sh` `v3/src/tests/ordinary_zero_exit_fixture.cheng` `v3/src/tests/primary_object_codegen_smoke.cheng` `v3/docs/cheng语言特性矩阵和开发计划.md` `v3/README.md` `v3/tooling/README.md` `task_plan.md` `progress.md` `findings.md` |
| 不做 | 不回退到 bootstrap-only 壳；不把 `zero-exit` 的真通过伪装成 `program-selfhost/chain_node` 已闭合；不再回头修已经切掉的 argv/contract/report/linker 假阻塞。 |
| 验收 | `build_backend_driver_v3.sh` 必须通过；`run_v3_host_smokes.sh` 必须全绿；`build_zero_exit_v3.sh` 必须真通过；`build_program_selfhost_v3.sh` 与 `build_chain_node_v3.sh` 必须统一稳定报 `v3 compiler: primary object body semantics missing`；文档必须明确写出当前 object/link 已接通，下一刀只扩 ordinary body 语义子集。 |

| 项目 | 内容 |
|---|---|
| 目标 | 把 `v3` bootstrap seed 的 ordinary command 面彻底收口到“真实 typed plan + 诚实阻塞”，继续拆掉 `argv/contract/report/stack` 这些假崩点，让 `program-selfhost` 和 `chain_node` 都稳定停在同一个语义缺口上。 |
| 主线 | 这轮已经把 `v3/bootstrap/cheng_v3_seed.c` 的 ordinary 命令面真正接通：`status/print-build-plan/system-link-exec` 现在都走内嵌 runtime contract，不再把普通源码 `--in` 误当 bootstrap contract；CLI 也已经同时接受 `--flag value`、`--flag:value`、`--flag=value` 三种写法，和 `v3/tooling/*.sh`、`v2/bootstrap/Makefile` 一致。随后又把 `v3_system_link_plan_report` 的固定缓冲区 `strcat` 改成动态拼接，并把 `v3_collect_source_closure` 里每层递归都压栈的 `V3PlanImportEdge[256]` 大 scratch 改成 heap scratch，`chain_node` 的段错误已经被切掉。现在 `build_backend_driver_v3.sh` 已稳定产出带 `status/print-build-plan/system-link-exec` 的 canonical backend driver，`program-selfhost` 和 `chain_node` 也都会稳定走到同一个 typed plan 阻塞：`runtime_targets_not_lowered` 与 `runtime_provider_modules_not_selected`。`run_v3_host_smokes.sh` 已继续全绿，`run_slice_gate.sh` 的唯一真失败位也已经收敛到 `program-selfhost`。 |
| 文件 | `v3/bootstrap/cheng_v3_seed.c` `v3/tooling/build_backend_driver_v3.sh` `v3/tooling/build_program_selfhost_v3.sh` `v3/tooling/build_chain_node_v3.sh` `v3/tooling/run_slice_gate.sh` `task_plan.md` `progress.md` `findings.md` |
| 不做 | 不回退到旧 proof/sidecar；不把 `runtime_targets/provider_modules` 为空伪装成“lowering 已开始”；不再让普通编译死在 `argv`、contract 装载或 report 栈溢出这种假根上。 |
| 验收 | `build_backend_driver_v3.sh` 必须继续通过；`artifacts/v3_backend_driver/cheng status/print-build-plan/system-link-exec` 必须都可调用；`build_program_selfhost_v3.sh` 和 `build_chain_node_v3.sh` 必须统一报 `runtime_targets_not_lowered/runtime_provider_modules_not_selected`，不能再出现 `missing --in`、`invalid bootstrap line` 或段错误；`run_v3_host_smokes.sh` 必须继续全绿；`run_slice_gate.sh` 必须稳定停在 `program-selfhost` 这一处真阻塞。 |

| 项目 | 内容 |
|---|---|
| 目标 | 把 `v3` ordinary compile 的最小 typed plan 真落地到 `parser -> system_link_plan -> compiler_main`，并把 gate 收严成“`build_backend_driver_v3.sh` 必须产出带 `status/print-build-plan/system-link-exec` 的 ordinary compiler”，不再把 bootstrap 壳误报成 canonical backend driver。 |
| 主线 | 这轮已经把 `v3/src/lang/parser.cheng` 扩成真实 package/workspace root、owner module、import edge、closure path、entry symbol、`missing_reasons` 的 typed parser stub；`v3/src/backend/system_link_plan.cheng` 也已改成 typed `emit/module kind/source kind`、typed closure/report、typed missing reasons，`compiler_request.cheng` 还接上了 `--root` 的 package-root 语义。`run_v3_host_smokes.sh` 现已固定在仓库根执行，`compiler_runtime_smoke`、`compiler_pipeline_stub_smoke` 和整套 host smokes 全绿。与此同时，`build_backend_driver_v3.sh` 已被收严成必须验证 built artifact 真的支持 `status` 和 `print-build-plan`；因此 `run_slice_gate.sh` 现在会更早、更诚实地死在真正主根：`artifacts/v3_backend_driver/cheng` 仍然只是 `cheng_v3_seed`/`compile-bootstrap` 壳。 |
| 文件 | `v3/src/tooling/compiler_request.cheng` `v3/src/tooling/compiler_runtime.cheng` `v3/src/tooling/compiler_main.cheng` `v3/src/lang/parser.cheng` `v3/src/backend/system_link_plan.cheng` `v3/src/backend/system_link_exec.cheng` `v3/src/tests/compiler_runtime_smoke.cheng` `v3/src/tests/compiler_pipeline_stub_smoke.cheng` `v3/tooling/run_v3_host_smokes.sh` `v3/tooling/build_backend_driver_v3.sh` `task_plan.md` `progress.md` `findings.md` |
| 不做 | 不再把 `build_backend_driver_v3.sh` 的“构建成功”当成 ordinary compiler 已经存在；不继续拿 `program-selfhost` 之后的报错掩盖更早的 `backend driver 仍是 bootstrap 壳`；不回退 `typed plan` 到字符串壳。 |
| 验收 | `scan_forbidden_hotpath.sh` 必须继续通过；`run_v3_host_smokes.sh` 必须全绿；`run_slice_gate.sh` 必须在 `canonical bootstrap compiler` 这一步直接暴露 `built output is still bootstrap-only, missing ordinary status command`，不再假装 `build_backend_driver_v3.sh` 已闭合。 |

| 项目 | 内容 |
|---|---|
| 目标 | 把 `v3` 的正式 gate 从“看 backend driver 帮助面”收严成“backend driver 真编 ordinary program 和 chain_node artifact”，并把当前真阻塞明文钉死为“`v3` 还没有自己的普通编译入口源码”。 |
| 主线 | 这轮已经新增 `v3/src/project/chain_node_main.cheng`、`v3/tooling/build_program_selfhost_v3.sh`、`v3/tooling/build_chain_node_v3.sh`，并把 `v3/tooling/run_slice_gate.sh` 改成直接调用这两条真实构建脚本。随后又把 `v3/src/tooling/{compiler_main,compiler_runtime,compiler_request}.cheng`、`v3/src/lang/parser.cheng`、`v3/src/backend/system_link_exec.cheng` 落库，`build_plan.cheng` 也已经把 `entryPath` 切到 `compiler_main.cheng`；host compiler 现已能真编这个新入口，`help/status` 正常，`system-link-exec` 也会稳定经过 `parser stub -> backend system_link_exec stub` 再报 `v3 compiler: ordinary pipeline not implemented`。当前 main root 仍然是 `build_backend_driver_v3.sh` 继续 materialize `stage1_bootstrap.cheng`。 |
| 文件 | `v3/src/project/chain_node_main.cheng` `v3/src/tooling/compiler_main.cheng` `v3/src/tooling/compiler_runtime.cheng` `v3/src/tooling/compiler_request.cheng` `v3/src/lang/parser.cheng` `v3/src/backend/system_link_exec.cheng` `v3/tooling/build_program_selfhost_v3.sh` `v3/tooling/build_chain_node_v3.sh` `v3/tooling/bootstrap_bridge_v3.sh` `v3/tooling/build_backend_driver_v3.sh` `v3/tooling/run_slice_gate.sh` `v3/docs/cheng语言特性矩阵和开发计划.md` `v3/README.md` `v3/tooling/README.md` `task_plan.md` `progress.md` `findings.md` |
| 不做 | 不再拿 `--help` 或 `supported_commands` 冒充 `program-selfhost` 已接通；不把 `stage1_bootstrap.cheng` 这种合同文件误认成普通编译器实现；不回去补旧 proof/sidecar 链。 |
| 验收 | `run_slice_gate.sh` 必须在 `program-selfhost` 阶段直接调用真实 ordinary program 构建脚本，在 `chain_node` 阶段直接调用真实 artifact 构建脚本；`compiler_main` 必须能被 host compiler 真编并稳定给出 `help/status/not implemented`；文档必须明确写出当前真阻塞是“backend driver 还在吃 bootstrap manifest，而不是 compiler_main”。 |

| 项目 | 内容 |
|---|---|
| 目标 | 承认 `src/tooling/cheng_tooling.cheng` 属于旧链后，把 `v3` 的主控、bootstrap 合同、backend build plan、性能 gate 全部落回 `v3` 目录；`run_slice_gate.sh` 只认 `v3/tooling` 的 bridge/build 入口，不再把旧总控当 `v3` 主链。 |
| 主线 | 这轮已经开始把 `v3/src/tooling/{path,host_ops,bootstrap_contracts,hotpath_scan,perf_gate,gate_main}.cheng`、`v3/src/backend/build_plan.cheng`、`v3/src/tests/{bootstrap_contracts_smoke,perf_gate_smoke}.cheng` 全部落库，并新增 `v3/tooling/{cheng_v3,bootstrap_bridge_v3,build_backend_driver_v3}.sh`。`bootstrap_bridge_v3.sh` 仍只把旧脚本当 bootstrap bring-up 外根，`build_backend_driver_v3.sh` 直接用 bootstrap driver + `chengc.sh` 编 `src/backend/tooling/backend_driver.cheng`，不再走旧总控子命令。当前还没跑通纯 Cheng `gate_main`，因为仓库现有 backend driver compile 仍会在更早位置炸掉。 |
| 文件 | `v3/src/tooling/path.cheng` `v3/src/tooling/host_ops.cheng` `v3/src/tooling/bootstrap_contracts.cheng` `v3/src/tooling/hotpath_scan.cheng` `v3/src/tooling/perf_gate.cheng` `v3/src/tooling/gate_main.cheng` `v3/src/backend/build_plan.cheng` `v3/src/tests/bootstrap_contracts_smoke.cheng` `v3/src/tests/perf_gate_smoke.cheng` `v3/tooling/cheng_v3.sh` `v3/tooling/bootstrap_bridge_v3.sh` `v3/tooling/build_backend_driver_v3.sh` `v3/tooling/run_slice_gate.sh` `v3/README.md` `v3/tooling/README.md` `task_plan.md` `progress.md` `findings.md` |
| 不做 | 不再把 `src/tooling/cheng_tooling.cheng` 视为 `v3` 入口；不把旧 sidecar wrapper/outer driver 整包搬进 `v3`；不把 seed binary 误说成 `v3` 源码主控。 |
| 验收 | `run_slice_gate.sh` 必须改成走 `v3/tooling/bootstrap_bridge_v3.sh` 和 `v3/tooling/build_backend_driver_v3.sh`；`v3/src/tooling` 必须出现真类型化合同和真 perf gate 代码；当前失败点必须继续暴露真实 backend/bootstrap 阻塞，而不是回退到旧命令分发层。 |

| 项目 | 内容 |
|---|---|
| 目标 | 清掉 `src/tooling` 和 `src/backend/tooling` 里已经确认无引用的 sidecar 死残片，同时把 `v3` 编译工具链入口改成“源码 launcher + strict bootstrap bridge”，不再默认绑死旧 binary 和缺失的 `cheng.stage2{,.proof}`。 |
| 主线 | 这轮已经做了三件事：一是删掉 32 个已跟踪的 `src/tooling/cheng_sidecar_rewrite_*.cheng`、`src/tooling/cheng_tooling_embedded_stable.cheng`、`src/backend/tooling/backend_driver_uir_sidecar_direct_build.cheng`，并清掉源码树里残留的未跟踪 sidecar 重写文件和 `.o/.DS_Store`；二是把 `src/tooling/cheng_tooling.cheng`、`verify_backend_sidecar_cheng_fresh.sh`、`resolve_backend_sidecar_defaults.sh`、`backend_driver_currentsrc_sidecar_wrapper.sh`、`cheng_tooling.sh` 一起改成接受 `probe_currentsrc_proof/cheng_stage0_currentsrc.proof` 作为严格 bootstrap bridge；三是把 `v3/tooling/run_slice_gate.sh` 默认工具切到源码 launcher，并先跑 fresh sidecar bridge 再跑 `build-backend-driver`。当前 fresh gate 已经不再卡“published strict stage0 surface / real sidecar driver missing strict direct-export surface / missing strict sidecar mode contract”这些旧死点，但 wrapper-source build 还没闭合，日志目前为空。 |
| 文件 | `src/tooling/cheng_tooling.cheng` `src/tooling/cheng_tooling_embedded_scripts/backend_driver_currentsrc_sidecar_wrapper.sh` `src/tooling/cheng_tooling_embedded_scripts/verify_backend_sidecar_cheng_fresh.sh` `src/tooling/cheng_tooling_embedded_scripts/resolve_backend_sidecar_defaults.sh` `src/tooling/cheng_tooling_embedded_scripts/cheng_tooling.sh` `v3/tooling/run_slice_gate.sh` `v3/tooling/README.md` `v3/README.md` `task_plan.md` `progress.md` `findings.md` |
| 不做 | 不把 bootstrap bridge 包装成最终主链；不回退去改 `backend_driver_symbol_bridge.c` 热路径；不再保留 sidecar 随机重写残片污染源码树。 |
| 验收 | `src/tooling` 顶层不再残留任何 `cheng_sidecar_rewrite_*.cheng`；`v3/tooling/run_slice_gate.sh` 默认必须走源码 launcher；fresh sidecar gate 必须至少前进到 wrapper-source 真编译阶段，不再死在旧合同识别上。 |

| 项目 | 内容 |
|---|---|
| 目标 | 继续打穿 `build-backend-driver` 真阻塞，把 `stage0 capability` source list 和 `system/seqs` 导入环一起收口，不再让旧 launcher/旧 sidecar 编译器把 `v3` gate 卡死在假缺项上。 |
| 主线 | 这轮已经做了三件硬事：`src/std/system.cheng` 删掉无效 `import std/seqs`；`src/tooling/cheng_tooling.cheng` 的 `tooling_stage0CapabilitySourceList()` 改成“固定主链 + 运行时扫描 `uir_core_builder*.cheng` + 静态名单回落”；同时确认 `cheng_tooling.cheng` 默认输出是 launcher，只有 `TOOLING_EMIT_SELFHOST_LAUNCHER=0` 才会尝试直出 native。本轮还没把 canonical tooling native binary 真重编成功，当前强制 native compile 仍在 `artifacts/backend_driver/cheng` 上 `rc=223`，而且旧执行面继续把假 source-list 缺项带出来。 |
| 文件 | `src/std/system.cheng` `src/tooling/cheng_tooling.cheng` `src/tooling/README.md` `task_plan.md` `progress.md` `findings.md` |
| 不做 | 不把 `TOOLING_STAGE0_CAPABILITY_PREFLIGHT=0` 当最终方案；不把 launcher 壳误算成当前源码 native binary；不回退 `v3` 的固定布局/二进制接口。 |
| 验收 | `system/seqs` 导入环必须从源码面消失；`tooling_stage0CapabilitySourceList()` 必须改成 builder 文件运行时扫描优先；记录必须明确写出“`cheng_tooling.cheng` 默认产物是 launcher，强制 native compile 仍未闭合，当前 canonical tooling 执行面仍旧”。 |

| 项目 | 内容 |
|---|---|
| 目标 | 在 `v3` 把冷路径字符串 interner、`HIR/MIR/LIR` 固定 ID 骨架、冻结 C 基线和脚本化验收入口一起落库，让“不能退回字符串热路径”从口头约束变成文件和命令。 |
| 主线 | 这轮不碰仓库现有 backend driver 活根，也不把它误算成 `v3` 代码问题；先把 `v3/src/lang/intern.cheng`、`v3/src/ir/core_types.cheng`、`v3/src/tests/ir_core_smoke.cheng`、`v3/tooling/{scan_forbidden_hotpath,compare_bench,run_slice_gate}.sh`、`v3/bench/c_ref/baseline_arm64_apple_darwin.txt` 落库，再用 `run_slice_gate.sh` 证明 `scan + c_ref` 已闭合、当前真实阻塞继续在仓库全局 `build-backend-driver`。 |
| 文件 | `v3/src/lang/intern.cheng` `v3/src/ir/core_types.cheng` `v3/src/tests/ir_core_smoke.cheng` `v3/tooling/scan_forbidden_hotpath.sh` `v3/tooling/compare_bench.sh` `v3/tooling/run_slice_gate.sh` `v3/bench/c_ref/baseline_arm64_apple_darwin.txt` `v3/README.md` `v3/lang/README.md` `v3/ir/README.md` `v3/tooling/README.md` `task_plan.md` `progress.md` `findings.md` |
| 不做 | 不去改当前 backend driver/source-list/import-cycle 老根；不把脚本写成只会报喜不报错的壳；不把 `v3` 名字、类型、IR 再退回 `str` 驱动。 |
| 验收 | `v3/tooling/scan_forbidden_hotpath.sh` 必须直接执行通过；`v3/tooling/compare_bench.sh` 必须能对拍冻结基线和最新 C 结果；`v3/tooling/run_slice_gate.sh` 必须稳定先过 `scan + c_ref + frozen-vs-latest`，再在现有 `build-backend-driver` 真故障点失败并留下日志。 |

| 项目 | 内容 |
|---|---|
| 目标 | 在 `v3` 先落固定布局字节面、固定 256 位 crypto 接口、二进制链帧和同机 C 基线，先把“性能地基”和“文本协议切除”真正变成文件与命令。 |
| 主线 | 这轮不碰 `v2` 活体热链，也不假装一口气做完整自举；先把 `v3/cheng-package.toml`、`v3/src/std/...`、`v3/src/chain/...`、`v3/bench/c_ref/*` 落库。当前 Cheng smoke 不是 `v3` 单点错误，而是仓库现有 canonical backend driver 全局 `rc=223`；`build-backend-driver` 继续向下暴露的是现有 `src/std/system.cheng -> src/std/seqs.cheng -> src/std/system.cheng` 导入环。 |
| 文件 | `v3/cheng-package.toml` `v3/README.md` `v3/src/std/bytes_layout.cheng` `v3/src/std/crypto/fixed256.cheng` `v3/src/chain/binary_types.cheng` `v3/src/chain/codec_binary.cheng` `v3/src/tests/fixed_surface_smoke.cheng` `v3/src/tests/chain_codec_binary_smoke.cheng` `v3/bench/c_ref/bench_ref.c` `v3/bench/c_ref/Makefile` `task_plan.md` `progress.md` `findings.md` |
| 不做 | 不把当前坏 driver 当成 `v3` 代码问题乱改；不把 `v3` 再塞回 `v2` 文本帧；不做 package import 兜底壳。 |
| 验收 | `v3/bench/c_ref` 必须真编真跑；`v3` 下固定布局类型、固定 256 位 crypto 接口、二进制链帧和 smoke 文件全部落库；记录必须明确写出“当前 Cheng smoke 被现有 driver 全局故障挡住，不是 `v3` 单点语义错误”。 |

| 项目 | 内容 |
|---|---|
| 目标 | 继续推进 `program` 热路径和值表示，但这轮已经先把 runtime 的 `array/record type_text` 收成全局 intern 稳定指针，并把 `program-selfhost` 扩成真实多进程 `chain_node serve-once/sync-once` gate。下一刀直接打固定布局 `slot/shape` 和 aggregate `field/index update copy`。 |
| 主线 | `system_helpers_stdio_bridge.c` 现在不再对每个 `array.elem_type_text / record.type_text` 做重复 `dup/free`，而是统一走全局 intern 表；`cheng_v2c_tooling.c` 的 `program-selfhost-check` 也已经正式加入 `chain_node_process_smoke`，固定要求 stage2 编译器真编真跑多进程 `mint -> serve-once -> sync-once -> balance`。完整前台 gate 已重新收口到 `manifest_fnv1a64=b55b66018e18ab44`。当前真主根没变，还是固定布局 `slot/shape` 和 aggregate `field/index update copy`。 |
| 文件 | `src/runtime/native/system_helpers_stdio_bridge.c` `v2/bootstrap/cheng_v2c_tooling.c` `v2/tests/contracts/compiler_core_system_link_exec.expected` `v2/tests/contracts/program_selfhost.expected` `v2/tests/contracts/full_selfhost.expected` `v2/docs/自举和性能.md` `task_plan.md` `progress.md` `findings.md` |
| 不做 | 不把这轮说成热层已完成；不回头碰已经闭合的 `fresh slot/nil slot/shared-shape/field ordinal`；不再把单进程 `msquic_chain_smoke` 当生产 gate |
| 验收 | 完整前台 gate `compiler-core-release -> full-selfhost` 必须继续通过；`program-selfhost` 必须继续真跑多进程 `chain_node_process_smoke`；记录必须明确写出“type_text intern 和真实多进程 gate 已收口，当前真根仍是固定布局 `slot/shape` 和 aggregate `field/index update copy`”。 |

| 项目 | 内容 |
|---|---|
| 目标 | 继续推进 `program` 热路径和值表示，但这轮先把 `fresh slot` 直写和 `nil slot` 新建 aggregate 的 temp 所有权收进 runtime，再继续打固定布局 `slot/shape` 和 aggregate field/index update copy。 |
| 主线 | 这轮已经把 `MAKE_ARRAY/MAKE_RECORD/array_push/add/setLen/reserve/field-index create_if_nil` 收成新槽位直写：新建 aggregate 不再先走通用 `assign_slot` 再深拷一次，`nil slot` 也不再白白 clone 一个刚创建的空 `array/record`。`compiler_core_system_link_exec/program-selfhost/full-selfhost` 已重新收口到 `manifest_fnv1a64=b8a740987bf39861`。前台 3 次中位数当前是 `pubkey=1.1300s`、`sign=1.6300s`、`mul_xonly=1.3200s`、`kinv=0.7000s`。下一刀不回头补这条已闭合路径，直接打固定布局 `slot/shape` 和 aggregate field/index update copy。 |
| 文件 | `src/runtime/native/system_helpers_stdio_bridge.c` `v2/tests/contracts/compiler_core_system_link_exec.expected` `v2/tests/contracts/program_selfhost.expected` `v2/tests/contracts/full_selfhost.expected` `v2/docs/自举和性能.md` `task_plan.md` `progress.md` `findings.md` |
| 不做 | 不把这轮说成热层已完成；不回头碰已收口的 `field ordinal + record slot`；不回到 probe 式乱试 `TrustedInto` 直连 |
| 验收 | `p256_fixed_core_probe` 必须继续 `probe=ok p_mul=1 n_mul=1 p_square=1 n_square=1`；`compiler_core_system_link_exec/program-selfhost/full-selfhost` 必须继续通过；记录必须明确写出“fresh slot/nil slot aggregate 白 clone 已切掉，当前真根仍是固定布局 `slot/shape` 和 aggregate field/index update copy”。 |

| 项目 | 内容 |
|---|---|
| 目标 | 继续推进 `program` 热路径和值表示，但这轮先把 `field ordinal + record slot` 这条固定布局主链在 source/stage0/runtime 三层收齐，再继续打固定布局 `slot/shape` 和 aggregate field/index update copy。 |
| 主线 | 这轮已经把 source 的 `stmtAssignTargetFieldOrdinals` 正式接进 stage0 facts/high_uir/low_uir mirror，`addr_of_field/load_field/store_*field` 不再只靠字段名；runtime 侧也把 `driver_c_prog_record_slot_at(...)` 收成正式 slot 入口，并把两处“已知 decl 顺序还按字段名找 slot”的路径改成按 ordinal 直达。完整前台 gate 已在这轮 fixed point 上收口到 `manifest_fnv1a64=29cea01991c4689b`，`program-selfhost/full-selfhost` 继续通过。下一刀不回头补这条已闭合路径，直接打固定布局 `slot/shape` 和 aggregate field/index update copy。 |
| 文件 | `v2/bootstrap/cheng_v2c_tooling.c` `src/runtime/native/system_helpers_stdio_bridge.c` `v2/tests/contracts/compiler_core_release_artifact.expected` `v2/tests/contracts/compiler_core_system_link_plan.expected` `v2/tests/contracts/compiler_core_system_link_exec.expected` `v2/tests/contracts/program_selfhost.expected` `v2/tests/contracts/full_selfhost.expected` `v2/docs/自举和性能.md` `task_plan.md` `progress.md` `findings.md` |
| 不做 | 不把这轮说成热层已完成；不回头碰已收口的 `field ordinal` 编译链；不把 `record slot` 名称查找当成最终方案继续扩散 |
| 验收 | 完整前台 gate 必须继续通过；记录必须明确写出“field ordinal + record slot 已三层收齐，当前真根还是固定布局 `slot/shape` 和 aggregate field/index update copy”。 |

| 项目 | 内容 |
|---|---|
| 目标 | 继续推进 `program` 热路径和值表示，但这轮先把 aggregate 同对象自拷贝和 shape 重建这两处真根收口，再继续打固定布局 slot/shape 和 aggregate field/index update copy。 |
| 主线 | 这轮已经把 `driver_c_prog_assign_slot` 里的“同一 array/record 又写回原 slot”这条纯自拷贝切掉，同时把 `driver_c_prog_clone_value_deep` 收成“保留 array cap / record cap / record lookup shape，再递归 clone 值”。前台 3 次中位数现在是 `pubkey=1.1700s`、`sign=1.6700s`、`mul_xonly=1.3400s`、`kinv=0.7200s`；`p256_fixed_core_probe`、`program-selfhost`、`full-selfhost` 和完整主 gate 都继续通过。下一刀不再回头碰这两处已闭合路径，直接打固定布局 slot/shape 和 aggregate field/index update copy。 |
| 文件 | `src/runtime/native/system_helpers_stdio_bridge.c` `v2/tests/contracts/compiler_core_release_artifact.expected` `v2/tests/contracts/compiler_core_system_link_plan.expected` `v2/tests/contracts/compiler_core_system_link_exec.expected` `v2/tests/contracts/program_selfhost.expected` `v2/tests/contracts/full_selfhost.expected` `v2/docs/自举和性能.md` `task_plan.md` `progress.md` `findings.md` |
| 不做 | 不再重试 `MAKE_RECORD` 槽位缓存；不再回头碰 `nil slot -> fresh aggregate` 直写；不把这轮小收益写成热层已完成 |
| 验收 | `p256_fixed_core_probe` 继续 `ok`；完整前台 gate 继续通过；记录必须明确写出“同对象自拷贝和 shape 重建已切掉，当前真根是固定布局 slot/shape 和 aggregate field/index update copy”。 |

| 项目 | 内容 |
|---|---|
| 目标 | 继续推进 `program` 热路径和值表示，但先把 `EPHEMERAL_AGGREGATE root-only clear` 这条实验正式收口，再直接打固定布局 slot/shape 和 aggregate update copy。 |
| 主线 | 这轮已经验证 `driver_c_prog_value_clear_ephemeral_flag_root` 可以保留：`p256_fixed_core_probe`、`compiler-core-system-link-exec`、`program-selfhost`、`full-selfhost` 全部继续闭合，前台 3 次中位数更新为 `pubkey=1.1700s`、`sign=1.6800s`、`mul_xonly=1.3500s`、`kinv=0.7300s`。这说明 recursive clear 已经不是语义必须成本，下一刀不再停在 flag 清理，直接改固定布局 slot/shape 和 aggregate update copy。 |
| 文件 | `src/runtime/native/system_helpers_stdio_bridge.c` `v2/tests/contracts/compiler_core_system_link_exec.expected` `v2/tests/contracts/program_selfhost.expected` `v2/tests/contracts/full_selfhost.expected` `v2/docs/自举和性能.md` `task_plan.md` `progress.md` `findings.md` |
| 不做 | 不把这条小收益说成热层已完成；不再怀疑 `root-only clear` 本身；不再尝试绕过 `assign_slot` 直接写 `nil slot -> fresh aggregate`；不回头碰已证伪的 `TrustedInto` 直连 |
| 验收 | `p256_fixed_core_probe` 继续 `ok`；完整前台 gate 继续通过；记录必须明确写出“root-only clear 已验证，当前真根是 slot/shape 和 aggregate update copy”。 |

| 项目 | 内容 |
|---|---|
| 目标 | 继续推进 `program` 热路径和值表示，但只保留真收益：拆掉 aggregate 深拷贝链里 nested `ZERO_PLAN` 的 eager materialize，再继续打固定布局 slot/shape。 |
| 主线 | 这轮已经把 `system_helpers_stdio_bridge.c` 里的 `driver_c_prog_clone_value_deep/driver_c_prog_value_clear_ephemeral_flags_deep` 改成“只解 `REF`，不提前把 nested `ZERO_PLAN` 物化成真实 aggregate”。这刀收正了值表示主链，也把运行时内存口径继续压低；前台 3 次中位数目前是 `pubkey=1.1900s`、`sign=1.7100s`、`mul_xonly=1.3600s`、`kinv=0.7300s`。下一刀不再赌 `ZERO_PLAN` 本身，而是直接改固定布局 slot/shape 和 aggregate update copy。 |
| 文件 | `src/runtime/native/system_helpers_stdio_bridge.c` `v2/tests/contracts/compiler_core_system_link_exec.expected` `v2/tests/contracts/program_selfhost.expected` `v2/tests/contracts/full_selfhost.expected` `v2/docs/自举和性能.md` `task_plan.md` `progress.md` `findings.md` |
| 不做 | 不把“临时 aggregate 直接接管”这种会留下嵌套别名风险的试刀塞进生产路径；不回头碰已闭合的 `comb6 correctness`；不放宽同机 C `1:1` 口径 |
| 验收 | `p256_fixed_core_probe` 必须继续 `ok`；`compiler-core-system-link-exec`、`program-selfhost`、`full-selfhost` 必须继续通过；记录必须明确写出“nested ZERO_PLAN eager materialize 已切掉，但当前真根已继续收缩到固定布局 slot/shape”。 |

| 项目 | 内容 |
|---|---|
| 目标 | 继续完成 `program` 热路径和值表示，但把“无收益试刀”和“真收益修正”分开。 |
| 主线 | 这轮先把 `MAKE_ARRAY/MAKE_RECORD` 的 fresh-slot move 试刀证伪并撤回，再把 `ZERO_PLAN` 的 record/array field/index 更新收成 lazy shell，避免 nested update 一上来就递归造整棵默认值。新 binary 前台重测后，`P-256` 四个 probe 的 3 次中位数是 `pubkey=1.1400s`、`sign=1.6700s`、`mul_xonly=1.3400s`、`kinv=0.7200s`，说明这条 lazy shell 修正没有把当前 crypto 热链再明显压下去。下一刀不再赌 `zero_plan` 壳本身，直接改固定布局 slot/shape。 |
| 文件 | `src/runtime/native/system_helpers_stdio_bridge.c` `v2/tests/contracts/compiler_core_system_link_exec.expected` `v2/tests/contracts/program_selfhost.expected` `v2/tests/contracts/full_selfhost.expected` `v2/docs/自举和性能.md` `task_plan.md` `progress.md` `findings.md` |
| 不做 | 不把这轮写成性能大收益；不保留 fresh-slot 试刀；不回头碰已证伪的 `TrustedInto` 直连 |
| 验收 | `compiler-core-system-link-exec`、`program-selfhost`、`full-selfhost` 必须继续通过；记录必须明确写出“lazy shell 保留，但当前真根已经切到固定布局 slot/shape”。 |

| 项目 | 内容 |
|---|---|
| 目标 | 继续完成 `program` 热路径和值表示，这轮先把 `lazy zero-plan + slot 级 materialize` 真收口，再继续打 aggregate field update 剩下的物化/拷贝。 |
| 主线 | 这轮已经把 `system_helpers_stdio_bridge.c` 里的 typed `param/local_decl` 默认零值改成 lazy `ZERO_PLAN`，ref load 改成 slot 级原地 materialize，`assign_slot` 会按 zero plan 原地 refine，不再先 eager 物化整块默认值。同时也修掉了这条新路径暴露出来的真 bug：如果 `ZERO_PLAN` slot 在 nested field store 里先物化成临时 record/bytes/str/array，但不写回 slot，写入会直接丢失，`lsmr_advanced_features_smoke` 就会把 `Result.err.msg` 写没。这条现在已经切正，`program-selfhost/full-selfhost` 都继续通过。新 runtime 上前台 3 次中位数已经压到 `pubkey=0.9900s`、`sign=1.4100s`、`mul_xonly=1.1500s`、`kinv=0.6200s`，单次前台 RSS 约 `148MB/332MB`。下一刀不回头碰已闭合路径，直接打 aggregate field update 剩下的物化/拷贝。 |
| 文件 | `src/runtime/native/system_helpers_stdio_bridge.c` `v2/tests/contracts/compiler_core_system_link_exec.expected` `v2/tests/contracts/program_selfhost.expected` `v2/tests/contracts/full_selfhost.expected` `v2/docs/自举和性能.md` `task_plan.md` `progress.md` `findings.md` |
| 不做 | 不把这轮说成热层已完成；不再用 `ZERO_PLAN` 临时值绕过 slot 写回；不回头再试已证伪的 `TrustedInto` 直连 |
| 验收 | `full-selfhost` 和 `program-selfhost` 必须继续通过；记录必须明确写出“lazy zero-plan 已进 active runtime、nested field store 丢写入已修、下一刀是 aggregate field update 物化/拷贝”。 |

| 项目 | 内容 |
|---|---|
| 目标 | 继续完成 `program` 热路径和值表示，这轮先把已经确认的真收益收口：小 aggregate 固定布局内联 + frame 级 `zero plan` 预解码，然后继续打 `zero_value/local_decl` 的固定布局原型化。 |
| 主线 | 这轮没有回头碰已证伪的 `TrustedInto` 直连，而是继续重建 `program` 热层本身：`DriverCProgArray/DriverCProgRecord` 现在都带小容量内联存储，`clone/zero plan` 也不再通过 `record_slot(...)` 逐字段二次搭壳；`DriverCProgFrame` 还新增了 `param/local zero plans` 预解码，`LOCAL_DECL` 不再每次重跑类型归一化和零值计划查找。新 runtime 上前台 3 次中位数已经压到 `pubkey=1.0700s`、`sign=1.5300s`、`mul_xonly=1.2300s`、`kinv=0.6800s`，单次前台 RSS 约 `148MB/332MB`。下一刀不再回头碰这轮已闭合路径，直接打固定布局零值原型和剩余物化/拷贝。 |
| 文件 | `src/runtime/native/system_helpers_stdio_bridge.c` `v2/tests/contracts/compiler_core_system_link_exec.expected` `v2/tests/contracts/full_selfhost.expected` `v2/tests/contracts/program_selfhost.expected` `v2/docs/自举和性能.md` `task_plan.md` `progress.md` `findings.md` |
| 不做 | 不把“秒级继续下降”误写成热层已完成；不回头再试已证伪的 `TrustedInto` 直连；不放宽同机 C `1:1` 口径 |
| 验收 | `full-selfhost` 和 `program-selfhost` 必须继续通过；记录必须明确写出“小 aggregate 已内联、frame 级 zero plan 已预解码、下一刀是固定布局零值原型和剩余物化/拷贝”。 |

| 项目 | 内容 |
|---|---|
| 目标 | 继续完成 `program` 热路径和值表示，但先把已经确认的 runtime 真收益收口，再继续打 `zero_value/local_decl` 这条活根。 |
| 主线 | 这轮已经把 `system_helpers_stdio_bridge.c` 里的 per-call frame 泄露切掉：`params/locals/stack/labels/loops` 现在会在 `eval_item` 退出前统一清理，小帧也已经内联到 `DriverCProgFrame`，不再每次都走堆分配。同时把 `MAKE_ARRAY/MAKE_RECORD/STORE_* / NEW_REF` 收成真正的临时 aggregate move，少掉一层重复 `materialize` 和深拷贝。新 runtime 上前台 3 次中位数已经压到 `pubkey=1.2500s`、`sign=1.7700s`，峰值 RSS 约 `147.9MB/331.6MB`。下一刀不再回头碰这轮已闭合路径，直接打 `zero_value_from_type/local_decl` 的固定布局原型化。 |
| 文件 | `src/runtime/native/system_helpers_stdio_bridge.c` `v2/tests/contracts/compiler_core_system_link_exec.expected` `v2/tests/contracts/full_selfhost.expected` `v2/tests/contracts/program_selfhost.expected` `v2/docs/自举和性能.md` `task_plan.md` `progress.md` `findings.md` |
| 不做 | 不把“RSS 大降 + 小幅变快”误写成热层已完成；不回头再试已证伪的 `TrustedInto` 直连；不放宽同机 C `1:1` 口径 |
| 验收 | `full-selfhost` 和 `program-selfhost` 必须继续通过；记录必须明确写出“帧泄露已切掉、小帧已内联、下一刀是 zero_value/local_decl” |

| 项目 | 内容 |
|---|---|
| 目标 | 保持“现有闭合主链上重建关键热层”这条主线，同时把已证伪的 `TrustedInto` 试刀撤回，不让回归混进 active 热链。 |
| 主线 | 这轮先验证了 `pointJacobianDoubleFixedInPlace/pointJacobianAddAffineFixedInPlace` 里 `double/triple/quad/eight` 改成 `TrustedInto` 后的 correctness 和 spot-check。`double/add/comb/repr` 对拍都继续全绿，但 `pubkey/sign` 单次前台样本回到 `1.50s/2.22s`，比当前稳定基线 `1.2830s/1.8210s` 更差，所以整刀已撤回。当前主线不变：继续重建 `program` 执行热层和值流，不做整仓重建。 |
| 文件 | `src/std/crypto/ecnist.cheng` `v2/docs/自举和性能.md` `task_plan.md` `progress.md` `findings.md` |
| 不做 | 不保留这条回归试刀；不因为 correctness 全绿就把慢路径混进 active；不整仓推倒 |
| 验收 | 撤回后代码回到稳定热链；记录明确写出“这条 `TrustedInto` 试刀证伪并撤回”；主线继续维持“关键热层重建”口径。 |

| 项目 | 内容 |
|---|---|
| 目标 | 把“不是整仓重建，而是在现有闭合主链上重建关键热层”写成正式口径，并据此继续推进。 |
| 主线 | 这轮先不盲跑编译，先把 `v2/docs/自举和性能.md` 和任务记录收成统一结论：保留现有 `full-selfhost / LSMR / chain_node / comb6 correctness`，只重建 `program` 执行热层和纯 Cheng 热核接口；不走整仓推倒，也不走补丁式乱试。 |
| 文件 | `v2/docs/自举和性能.md` `task_plan.md` `progress.md` `findings.md` |
| 不做 | 不改语言前端总架构；不整仓重写；不把“先零 C 自举”当当前最短路 |
| 验收 | 文档必须明确写出“整仓重建不做、关键热层重建才是最短路”；任务记录同步成同一口径。 |

| 项目 | 内容 |
|---|---|
| 目标 | 把进度记录口径改成“只写开始时间，完成时间由用户统计真实时间”。 |
| 主线 | 这轮不改代码、不编译，只同步记录规则：`lessons.md` 记用户规则，`progress.md` 顶部写明新口径并从当前条目开始生效，历史条目不追改；`task_plan.md` 和 `findings.md` 只做规则同步。 |
| 文件 | `lessons.md` `progress.md` `task_plan.md` `findings.md` |
| 不做 | 不回写历史进度条目；不改工程代码；不跑编译或 smoke |
| 验收 | `progress.md` 顶部必须明确“只写开始时间”；`lessons.md` 必须记住这条用户规则；历史记录保持原样。 |

| 项目 | 内容 |
|---|---|
| 目标 | 把“如何从第一天就保证自举后仍有 C 级编译和运行时性能”的原则正式写进文档，作为后续性能和自举决策的硬口径 |
| 主线 | 这轮不改代码、不跑编译，只把架构原则写进 `v2/docs/自举和性能.md`：语言必须是静态系统语义、普通程序必须 AOT 本地码、热核必须固定宽度专用实现、benchmark 必须从第一天就按同机 C `1:1`。同时把“当前仓库离这个目标还差什么”收成正式路线图，避免后面再把“先自举再补性能”当正确路径。 |
| 文件 | `v2/docs/自举和性能.md` `task_plan.md` `progress.md` `findings.md` |
| 不做 | 不改 `ecnist/p256_fixed`；不编译；不刷 gate；不改进度百分比 |
| 验收 | 文档里必须新增“从第一天就保证自举后仍有 C 级性能”和“当前仓库离这个目标还差什么”两节，口径与现有同机 C `1:1` 目标一致 |

| 项目 | 内容 |
|---|---|
| 目标 | 继续完成“纯 Cheng 第一梯队性能 + 近零 C 自举闭环”，主线保持在 correctness 已闭合的 active `comb6` 热链上继续压 `double/addAffine`。 |
| 主线 | 这轮保留下来的真改动是把 `p256_fixed` 低层 fake `Into` 收成真原地输出：`p256FixedModAddTrustedInto`、`p256FixedModSubTrustedInto`、raw add/sub 和条件减模不再先返回整块 `P256Fixed` 再赋值。`_tmp_p256_mul_double_cmp_probe`、`_tmp_p256_mul_add_cmp_probe`、`_tmp_p256_comb_cmp_probe`、`_tmp_p256_repr_sign_r_cmp_probe` 全部继续转绿，完整前台 gate 也已通过。当前 3 次前台中位数已更新到 `pubkey=1.2830s`、`sign=1.8210s`、`mul=1.4628s`、`kinv=0.7521s`。下一刀继续只压 active `comb6` 主循环里的非自乘乘法和值流，优先 `pointJacobianAddAffineFixedInPlace`。 |
| 文件 | `src/std/crypto/ecnist.cheng` `v2/tests/contracts/_tmp_p256_mul_double_cmp_probe.cheng` `v2/tests/contracts/_tmp_p256_mul_add_cmp_probe.cheng` `v2/tests/contracts/_tmp_p256_comb_cmp_probe.cheng` `v2/tests/contracts/_tmp_p256_repr_sign_r_cmp_probe.cheng` `v2/tests/contracts/_tmp_p256_pubkey_scalar_probe.cheng` `v2/tests/contracts/_tmp_ecdsa_sign_probe.cheng` `v2/tests/contracts/_tmp_ecdsa_mul_probe.cheng` `v2/tests/contracts/_tmp_ecdsa_kinv_probe.cheng` `v2/docs/自举和性能.md` `task_plan.md` `progress.md` `findings.md` |
| 不做 | 不回头怀疑 `comb6` correctness；不再尝试整段 trusted 直连；不碰 native crypto；不放宽同机 C `1:1` 目标；不跑后台命令 |
| 验收 | `double/add cmp`、`comb_cmp`、`repr_r_cmp` 必须继续全绿；完整前台 gate `make -j1 -C /Users/lbcheng/cheng-lang/v2/bootstrap compiler-core-release compiler-core-system-link compiler-core-system-link-exec tooling-selfhost lsmr-contracts selfhost full-selfhost` 必须通过；性能口径更新为 `pubkey=1.2830s`、`sign=1.8210s` |

| 项目 | 内容 |
|---|---|
| 目标 | 继续完成 `P-256 comb6` correctness，但先把真根从“数学/表常量”收紧到“当前函数形状下的代码生成问题”，不把任何未闭合 `comb6` 接回生产路径。 |
| 主线 | 这轮已经前台钉死三件事：`wnaf == Python`；`p256GComb6[idx]` 的常量表和可变索引本身是对的；真正坏的是“完整 `comb6` 函数形状”。最小双比特 probe 现在稳定显示：`single_col0` 和 `cross_row_same_col` 会塌成 infinity，`single_col1` 和 `top_row_cols01` 正常，`same_row_cols01` 会退化成 `digit=2` 的结果。与此同时，`_tmp_p256_comb_first_digit_probe` 已证明同样的 `find first nonzero row -> setFromAffine -> toAffine` 在小函数里是对的。下一刀不再猜表、不再猜数学，直接查为什么这段逻辑一进 `ecnist` 的完整函数形状就坏。 |
| 文件 | `src/std/crypto/ecnist.cheng` `v2/tests/contracts/_tmp_p256_comb_two_bit_probe.cheng` `v2/tests/contracts/_tmp_p256_comb_lookup_bug_probe.cheng` `v2/tests/contracts/_tmp_p256_comb_table_entry_probe.cheng` `v2/tests/contracts/_tmp_p256_comb_digit_lookup_probe.cheng` `v2/tests/contracts/_tmp_p256_comb_first_digit_probe.cheng` `task_plan.md` `progress.md` `findings.md` |
| 不做 | 不再把 `comb6` 接回 `publicKey/sign` active 路；不再相信旧二进制 probe；不碰 native crypto；不放宽同机 C `1:1` 目标；不跑后台命令 |
| 验收 | 生产路径保持回滚稳定；`_tmp_p256_comb_two_bit_probe_bin` 继续稳定复现低位塌缩；`_tmp_p256_comb_first_digit_probe_bin` 继续证明小函数形状是对的；完整前台 gate 继续通过 |

| 项目 | 内容 |
|---|---|
| 目标 | 继续完成“纯 Cheng 第一梯队性能 + 近零 C 自举闭环”里的最短热核，但先把 `comb6` 的 correctness 收平，不再把未闭合的 active 接线算进生产路径。 |
| 主线 | 这轮已经确认两件事：`_tmp_p256_jacobian_valueflow_probe` 全绿，旧的 imported/local fixed-record 值流老根已死；`comb6` 的完整 affine 仍未闭合，`_tmp_p256_comb_cmp_probe` 继续 `neq`，而且代表性标量 `repr_r_cmp` 也还不等价，所以上一轮临时 active 接线已经撤回。回滚后重新量准的安全前台基线是 `pubkey=4.47s`、`sign=6.98s`、`mul_xonly=3.67s`、`kinv=0.95s`。下一刀只查 `comb6` 完整 affine 为什么和 `wnaf` 不等价，不再把 `comb6` 接回 active 热链。 |
| 文件 | `src/std/crypto/ecnist.cheng` `v2/tests/contracts/_tmp_p256_pubkey_scalar_probe.cheng` `v2/tests/contracts/_tmp_ecdsa_sign_probe.cheng` `v2/tests/contracts/_tmp_ecdsa_mul_probe.cheng` `v2/tests/contracts/_tmp_ecdsa_kinv_probe.cheng` `v2/tests/contracts/_tmp_p256_comb_cmp_probe.cheng` `v2/tests/contracts/_tmp_p256_sign_r_comb_cmp_probe.cheng` `v2/tests/contracts/_tmp_p256_repr_sign_r_cmp_probe.cheng` `v2/tests/contracts/_tmp_p256_jacobian_valueflow_probe.cheng` `v2/docs/自举和性能.md` `task_plan.md` `progress.md` `findings.md` |
| 不做 | 不再把 imported/local record-return 值流当主根；不在完整 affine 未对拍前把 `comb6` 接回 `publicKey` 或签名 active 路径；不碰 native crypto；不放宽同机 C `1:1` 目标；不跑后台命令 |
| 验收 | `_tmp_p256_jacobian_valueflow_probe_bin` 必须继续全绿；`_tmp_p256_comb_cmp_probe_bin` 和 `_tmp_p256_repr_sign_r_cmp_probe_bin` 的边界必须被进一步压缩；回滚后的安全基线和完整前台 gate 必须继续通过 |

| 项目 | 内容 |
|---|---|
| 目标 | 把 `LSMR` 的真实联网验收彻底收成“多进程 `chain_node serve-once/sync-once` 正式合同”，不再把单进程 `msquic_chain_smoke` 当生产 gate。 |
| 主线 | 这轮已经连续钉死了三条真根：`msquicNativeDial` 因 `if-expr` body shape 掉成 outline、`chainReadExact` 把非阻塞 app recv 误当流结束、`chain_node_process_smoke` 仍沿用过时 `12s` timeout。现在 `chain_node_test` 已前台真跑通 `serve-once/sync-once -> synced=1 -> balance=11`。下一刀只收 Makefile、文档和总 gate，不再回头猜 TLS/链算法。 |
| 文件 | `v2/cheng-quic/src/native_runtime.cheng` `v2/cheng-quic/src/connection.cheng` `v2/bootstrap/Makefile` `v2/docs/LSMR.md` `v2/docs/cheng-chain-mvp.md` `task_plan.md` `progress.md` `findings.md` |
| 不做 | 不再把单进程 `listener+client` 共存模型当生产证明；不对 TLS 慢路径做兜底；不跑后台命令 |
| 验收 | `make -j1 -C /Users/lbcheng/cheng-lang/v2/bootstrap lsmr-contracts` 必须前台通过；`chain_node_process_smoke.expected` 必须稳定；文档必须改成“真实联网证明看多进程 `chain_node`” |

| 项目 | 内容 |
|---|---|
| 目标 | 把 `LSMR` 的真实联网 smoke 正式收进 contract 和主 gate，让文档、代码、验收三者完全一致。 |
| 主线 | 这轮已经把 `FrameData` 真实长度、`FrameData` 精确切片、app stream `offset` 重组三条运行时活根切掉；`chain_state_tree_sync_smoke` 和 `msquic_chain_smoke` 都已经前台真跑通过。下一刀不再猜 packet/TLS/链算法，直接把这两条 smoke 接进 `lsmr-contracts`，然后再回普通 `program` 运行面和性能主线。 |
| 文件 | `v2/bootstrap/Makefile` `v2/tests/contracts/chain_state_tree_sync_smoke.expected` `v2/tests/contracts/msquic_chain_smoke.expected` `v2/docs/LSMR.md` `v2/docs/cheng-chain-mvp.md` `task_plan.md` `progress.md` `findings.md` |
| 不做 | 不再回头打开已经收口的 `packet too small`、`stream reassembly gap`、TLS 重传 transcript 假根；不做后台长跑；不把 smoke 成功伪装成“只算算法层通过” |
| 验收 | `lsmr-contracts` 必须前台真跑通过；`chain_state_tree_sync_smoke` 和 `msquic_chain_smoke` 必须有正式 expected；`LSMR.md` 和 `cheng-chain-mvp.md` 必须改成真实状态口径 |

| 项目 | 内容 |
|---|---|
| 目标 | 继续把 `v2/docs/LSMR.md` 里的链侧新技术往可运行版本推进，先落 `LSH 双重寻址` 和 `edge/regional/global` 三层状态快照，不假装一口气做完整个愿景层。 |
| 主线 | 这轮不碰 VM，不碰大握手路径，只在 `v2/cheng-quic/src/chain/{types,lsmr}.cheng` 里补 `LsmrLocalityCid/LsmrStateCell/LsmrStateLayerForest`，再用 `lsmr_locality_storage_smoke` 把 `token 顺序不敏感 + 三层状态树投影 + ChainIndex 三层派生` 钉死。 |
| 文件 | `v2/cheng-quic/src/chain/types.cheng` `v2/cheng-quic/src/chain/lsmr.cheng` `v2/cheng-quic/src/tests/lsmr_locality_storage_smoke.cheng` `v2/docs/cheng-chain-mvp.md` `task_plan.md` `progress.md` `findings.md` |
| 不做 | 不假装把 `LSMR.md` 全部愿景一口气做完；不混进 QUIC 握手/runtime 老根；不碰 VM 合约；不放宽任何 gate |
| 验收 | `lsmr_locality_storage_smoke` 必须前台真编过并真运行通过；`LocalityCid` 需稳定区分不同语义集，`LsmrStateLayerForest` 需稳定产出 `edge/regional/global` 三棵树和 `localOpCount/regionalBatchCount/globalAnchorCount` |

| 项目 | 内容 |
|---|---|
| 目标 | 继续完成“纯 Cheng 第一梯队性能 + 近零 C 自举闭环”里 `ECDSA` 的最短热核，但先把新暴露出的 `program/runtime` 值流老根钉死：非 owner 模块里，直接接 imported 函数返回的 `EcPointJacobianFixed` 会把结果打成 `inf + zero z`。当前理论目标仍是 `ecdsa p256 sign = 11.9109us/op`，`ecdh p256/pubkey = 24.4003us/op`。 |
| 主线 | 这轮不再碰 active `ecnist` 热链。最小复现已经落在 `v2/tests/contracts/_tmp_p256_jacobian_valueflow_probe.cheng`：`inline`、`local_nocopy`、`local_return_nocopy`、`imported`、`imported_return` 都通过；只有 `local_copy` 和 `local_return` 会稳定变成 `inf=1 z=[0..]`。这说明真根不是 `comb` 算法，也不是 return 本身，而是“非 owner 模块里把 imported 函数返回的 `EcPointJacobianFixed` 直接赋给本地变量”这条值流。下一刀先查 `program` 轨这条 record-return/assign 语义，不再继续压 `comb`。 |
| 文件 | `src/runtime/native/system_helpers_stdio_bridge.c` `src/std/crypto/ecnist.cheng` `v2/tests/contracts/_tmp_p256_jacobian_valueflow_probe.cheng` `v2/tests/contracts/_tmp_p256_generator_comb6_step_probe.cheng` `v2/docs/自举和性能.md` `task_plan.md` `progress.md` `findings.md` |
| 不做 | 不碰 VM；不引入 native crypto；不放宽 gate；不再起后台探测；不把未闭合的 `comb` 路接回 active 热链；不再继续猜 `ECDSA` 数学或窗口表；不在没钉死 record-return 值流前继续改 `pointMulGenerator...Comb6` |
| 验收 | `_tmp_p256_generator_comb6_step_probe` 必须继续 `ok`；`_tmp_p256_jacobian_valueflow_probe` 必须稳定复现“`local_copy/local_return` 失败而其余路径通过”；`p256_fixed_core_probe` 必须继续 `p_mul=1 n_mul=1 p_square=1 n_square=1`；完整前台 gate `make -j1 -C /Users/lbcheng/cheng-lang/v2/bootstrap compiler-core-release compiler-core-system-link compiler-core-system-link-exec tooling-selfhost lsmr-contracts selfhost full-selfhost` 必须继续通过 |

| 项目 | 内容 |
|---|---|
| 目标 | 继续完成“纯 Cheng 第一梯队性能 + 近零 C 自举闭环”里的最短热核，把 `ECDSA/RSA` 的 `bigModMul/bigModExp` 从位串行取模切到 Montgomery，并把真瓶颈继续压缩到普通程序执行面 |
| 主线 | 这轮已经把 `std/crypto/bigint.cheng` 加上 `BigMontgomeryContext + Montgomery mul/modexp`，并让 `std/crypto/ecnist.cheng` 复用 `P-256 P/N` 的上下文，不再走旧的 `bigMul -> bigMod(bit-by-bit)`。下一刀不碰 VM，不碰 v1，不引入 native crypto，直接继续消掉普通程序执行面对大对象值传递的热路径拷贝。 |
| 文件 | `src/std/crypto/bigint.cheng` `src/std/crypto/ecnist.cheng` `src/runtime/native/system_helpers_stdio_bridge.c` `v2/tests/contracts/_tmp_ecdsa_sign_probe.cheng` `task_plan.md` `progress.md` `findings.md` |
| 不做 | 不做 VM 合约；不碰 `v1`；不引入 native crypto/内建；不放宽 gate；不靠 bridge 或解释器兜底热路径；不再直接探测 `cheng_v2_system_link_exec` |
| 验收 | `_tmp_ecdsa_sign_probe` 前台真编译；新 Montgomery 路径真进入运行面；同时记录 runtime 真瓶颈，不用猜 |

| 项目 | 内容 |
|---|---|
| 目标 | 从通用 `BigInt` 继续下沉到专用 `P-256 8x32` 固定宽度 Montgomery 内核，确认热核慢点到底是 Cheng 执行面拷贝，还是新专用内核本身的算术实现还有 bug |
| 主线 | `p256_fixed` 已继续压缩到最小断点：`_tmp_p256_fixed_stage_probe` 证明第一拍 `a*R^2` 的乘法低位和 `mWord` 都对，真正炸的是同一拍的 `+ mWord * modulus`。因此下一刀不再尝试把它直接接进 `ecnist`，而是先查普通 program 轨的 `uint64` 热算子语义。 |
| 文件 | `src/std/crypto/p256_fixed.cheng` `src/std/crypto/ecnist.cheng` `v2/tests/contracts/p256_fixed_core_probe.cheng` `task_plan.md` `progress.md` `findings.md` |
| 不做 | 这刀不把未闭合的 `p256_fixed` 接进 `ecdsaSign`；不碰 VM；不引入 native crypto；不放宽 gate；不再拿整签名 probe 盲跑 |
| 验收 | `p256_fixed_core_probe` 和最小 stage probe 都能把断点压到固定的 `mul reduce i=0 / carry overflow offset=0 idx=17`；`ecnist` active 路径保持回到稳定版 Jacobian + generic Montgomery |

| 项目 | 内容 |
|---|---|
| 目标 | 修正普通 program runtime 把 `uint64` 偷偷当成 `int64` 的热算子语义错误，并把 `p256_fixed` 的 Montgomery 末尾正规化成带高字条件减模 |
| 主线 | 这轮不碰 VM，不引入 native crypto，也不再猜 `ecdsaSignBytes` 公式；先把 `system_helpers_stdio_bridge.c` 补成真实 `u32/u64` 值语义，再让 `p256_fixed_core_probe` 前台真跑到 `p_mul=1 n_mul=1`，最后把顶层 gate 收回绿色 |
| 文件 | `src/runtime/native/system_helpers_stdio_bridge.c` `src/std/crypto/p256_fixed.cheng` `v2/tests/contracts/_tmp_p256_runtime_probe.cheng` `v2/tests/contracts/p256_fixed_core_probe.cheng` `v2/tests/contracts/compiler_core_system_link_exec.expected` `v2/tests/contracts/full_selfhost.expected` `task_plan.md` `progress.md` `findings.md` |
| 不做 | 不改 `ecnist` active 接线；不放宽 gate；不再跑会留下孤儿进程的后台探测；不碰别的脏改 |
| 验收 | `_tmp_p256_runtime_probe` 必须恢复 `mul_hi_ok=1`；`p256_fixed_core_probe` 必须输出 `probe=ok p_mul=1 n_mul=1`；`make -j1 -C /Users/lbcheng/cheng-lang/v2/bootstrap compiler-core-release compiler-core-system-link compiler-core-system-link-exec tooling-selfhost lsmr-contracts selfhost full-selfhost` 必须前台通过 |

| 项目 | 内容 |
|---|---|
| 目标 | 修正普通 program runtime 在泛型零初始化里把“声明模块作用域”和“实例化模块作用域”混成一个的错误，并把 `ecdsa` 真瓶颈压缩到 program 热路径解释执行 |
| 主线 | 这轮不再猜 `pfix.P256Fixed` alias、本体或 `Result[T]` 定义对不对；直接把 `driver_c_prog_zero_value_from_type(_item)` 改成双作用域解析，先让 `Result[pfix.P256Fixed]` 真能跑，再验证 `ecdsaSignBytes` 是否已经从“类型解析崩”推进到“纯计算过慢” |
| 文件 | `src/runtime/native/system_helpers_stdio_bridge.c` `src/std/crypto/ecnist.cheng` `v2/tests/contracts/compiler_core_system_link_exec.expected` `v2/tests/contracts/full_selfhost.expected` `task_plan.md` `progress.md` `findings.md` |
| 不做 | 不碰 VM；不引入 native crypto；不再误把 `ecdsa` 慢归因成数学公式错；不再留临时 probe 文件；不再起会悬挂的后台进程 |
| 验收 | `_tmp_ecdsa_sign_probe` 必须不再报 `missing type item for zero init type=pfix.P256Fixed`；完整 `make -j1 -C /Users/lbcheng/cheng-lang/v2/bootstrap compiler-core-release compiler-core-system-link compiler-core-system-link-exec tooling-selfhost lsmr-contracts selfhost full-selfhost` 必须前台通过；结论必须收敛到“热路径仍在 `program_local_payload_entry`”这个架构根 |

| 项目 | 内容 |
|---|---|
| 目标 | 继续把 `ECDSA` 真热根从“初始化 + 乘法混在一起”压缩成“只剩固定点 `mul` 主循环”，并把启动时现算生成器表彻底移出运行面 |
| 主线 | 这轮已经把 `P/N/B/G` 和 `1..15*G` 的 `Montgomery` 固定表固化成源码常量，`p256EnsureInit()` 不再做 `hexDecode + bigFromBytes + Jacobian 建表`；同时给 `pointJacobianToAffineFixed()` 加了 `z==1` 快路，并让 `ecdsaSign()` 直接走 `pointMulGeneratorFixed()` 取 `x`，不再把没用的 `y` 也转回 `BigInt`。下一刀不再碰初始化，也不回头试窗口/comb 花样，只打 `pointMulGeneratorWnaf()` 的固定点 `double/add` 主循环。 |
| 文件 | `src/std/crypto/ecnist.cheng` `v2/tests/contracts/_tmp_p256_pubkey_probe.cheng` `v2/tests/contracts/_tmp_p256_pubkey_twice_probe.cheng` `v2/tests/contracts/_tmp_ecdsa_sign_probe.cheng` `v2/tests/contracts/_tmp_ecdsa_sign_stage_probe.cheng` `task_plan.md` `progress.md` `findings.md` |
| 不做 | 不碰 VM；不引入 native crypto；不新增更大生成器表；不靠探针常驻主线；不再把 `mul` 之前的初始化成本和点乘成本混在一起看 |
| 验收 | `_tmp_p256_pubkey_probe_bin` 前台真跑要显著低于之前的 `7.55s`；`_tmp_ecdsa_sign_probe_bin` 前台必须在限时内真完成，不再只是 `stage=mul` 超时；完整 `make -j1 -C /Users/lbcheng/cheng-lang/v2/bootstrap compiler-core-release compiler-core-system-link compiler-core-system-link-exec tooling-selfhost lsmr-contracts selfhost full-selfhost` 必须继续前台通过 |

| 项目 | 内容 |
|---|---|
| 目标 | 继续把生成器固定点乘的主循环从 `BigInt` 重编码彻底切到固定 `8x32` 标量路径，并继续压 `double/add` 本身 |
| 主线 | 这轮已经把 `pointMulGeneratorWnaf()` 从 `BigInt` 的 `bigIsOdd/bigGetBit/bigAdd/bigSub/bigShiftRight1` 改成固定 `8x32` 标量原地 `odd/low5/+small/-small/>>1`，同时预计算了 `p256GWindow4Neg`，主循环不再现算负点。下一刀只剩 `pointJacobianDoubleFixed()` 和 `pointJacobianAddAffineFixed()` 的乘法条数与数据流压缩。 |
| 文件 | `src/std/crypto/ecnist.cheng` `task_plan.md` `progress.md` `findings.md` |
| 不做 | 不回退到 `BigInt` 重编码；不加更大窗口；不碰 `deterministicK/nModInv`；不碰 VM |
| 验收 | `_tmp_p256_pubkey_probe_bin` 进入亚秒级；`_tmp_ecdsa_sign_probe_bin` 继续下降；完整主 gate 继续前台通过 |

| 项目 | 内容 |
|---|---|
| 目标 | 把 `pointJacobianAddAffineFixedInPlace` 的 trusted `add/sub` 基础层先做正确，再继续压 `x3/y3/z3` 的 trusted 值流 |
| 主线 | 这轮只用逐拍对拍 probe，不碰 active `ecnist`。`_tmp_p256_mul_add_stage_cmp_probe` 已把 trusted 路第一批真根压到 `p256FixedModAddTrustedInto/p256FixedModSubTrustedInto` 的 reduction/borrow 分支；这两条现在都收成了和 value-return 同构。下一刀直接盯 `x3`，不再回头查 `h/i/yDiff/r`。 |
| 文件 | `src/std/crypto/p256_fixed.cheng` `v2/tests/contracts/_tmp_p256_mul_add_stage_cmp_probe.cheng` `v2/tests/contracts/_tmp_p256_mul_add_cmp_probe.cheng` `task_plan.md` `progress.md` `findings.md` |
| 不做 | 不把未闭合的 trusted `addAffine` 接回 `ecnist` active 路；不刷 fixed-point；不碰 `kinv`；不碰窗口表和初始化 |
| 验收 | `_tmp_p256_mul_add_stage_cmp_probe_bin` 的第一处错位推进到 `x3`；`_tmp_p256_mul_add_cmp_probe_bin` 继续 `ok`；`p256_fixed_core_probe_bin` 继续 `probe=ok p_mul=1 n_mul=1 p_square=1 n_square=1`；完整主 gate 前台通过 |

| 项目 | 内容 |
|---|---|
| 目标 | 先把 `msquic` 握手内存爆涨根切掉，不编译，只做静态修正 |
| 主线 | 已确认 311GB 真根不是递归，而是 `Initial/Handshake` 重传包被重复喂进 TLS，导致 transcript 被重复追加，再被 `ByteBuffer.appendBytes` 整块复制放大。当前已在 `native_runtime` 把握手 `CRYPTO` 输入切到真正的 `offset` 重组，不再直接把 `frame.data` 喂给 `msquicTls13HandshakeFeed`；同时在 `handshake13` 加了 transcript/buffer 的硬上限，后续即使再有漏网也会直接 fail-fast，不再吃光内存。下一刀再补更细的去重和 transcript 表示优化。 |
| 文件 | `v2/cheng-quic/src/native_runtime.cheng` `v2/cheng-quic/src/core/crypto_stream.cheng` `v2/cheng-quic/src/tls/handshake13.cheng` `task_plan.md` `progress.md` `findings.md` |
| 不做 | 这轮不编译，不跑 smoke，不继续拉起会吃内存的握手路径 |
| 验收 | 先完成静态修正和记录收口；下一轮再前台小窗口验证，不再允许无去重握手输入进入 TLS |

| 项目 | 内容 |
|---|---|
| 目标 | 继续压 `pointJacobianAddAffineFixed`，但不再整段接 trusted active 路；先把对拍和真实回归边界钉死 |
| 主线 | 这轮已经把 `_tmp_p256_mul_add_stage_cmp_probe` 全部收绿，并证明 `fieldSubTrustedInto` 这层 probe 包装是假根；同时也证伪了两条更重的接线：`pointJacobianAddAffineFixedInPlace` 全 trusted 直连会把 `pubkey/sign` 拉回 `19s/23s+`，`Crash -> trusted` 直连更会把 `pubkey` 拉到 `30.73s`。下一刀不能再整段接线，只能在 compare probe 保护下继续拆 `addAffine` 最重的非自乘乘法和值拷贝。 |
| 文件 | `v2/tests/contracts/_tmp_p256_mul_add_stage_cmp_probe.cheng` `v2/tests/contracts/_tmp_p256_mul_add_cmp_probe.cheng` `src/std/crypto/ecnist.cheng` `task_plan.md` `progress.md` `findings.md` |
| 不做 | 不再整段切 `pointJacobianAddAffineFixedInPlace`；不再把 `Crash` 包装层整体换成 trusted；不刷 fixed-point；不碰 `kinv`；不碰窗口表和初始化 |
| 验收 | `_tmp_p256_mul_add_stage_cmp_probe_bin` 继续 `ok`；`_tmp_p256_mul_add_cmp_probe_bin` 继续 `ok`；完整主 gate 前台通过；active 热路径不接受任何比当前基线更慢的接线 |

| 项目 | 内容 |
|---|---|
| 目标 | 继续压 `ECDSA/P-256`，但彻底停止在 active `ecnist` 上试 `trusted scratch mul` 直连 |
| 主线 | 这轮又证伪了两条更窄的路：`pointJacobianAddAffineFixedInPlace` 里只换 7 次 `mul` 到 shared-scratch trusted 路，correctness 虽然全绿，但 `mul_add` 和整链都明显回归；再把这 7 次 `mul` 直接内联到底层 `pfix` 也一样更慢。`pointJacobianDoubleFixedInPlace` 的同类 3 次 `mul` 更是直接把 compare probe 跑崩。下一刀不能再碰 active `trusted scratch`，只该在 pure `Crash/Value` 路内继续拆 `addAffine/double` 的值流和临时对象。 |
| 文件 | `src/std/crypto/ecnist.cheng` `v2/tests/contracts/_tmp_p256_mul_add_probe.cheng` `v2/tests/contracts/_tmp_p256_mul_add_cmp_probe.cheng` `v2/tests/contracts/_tmp_p256_mul_add_stage_cmp_probe.cheng` `v2/tests/contracts/_tmp_p256_mul_double_cmp_probe.cheng` `task_plan.md` `progress.md` `findings.md` |
| 不做 | 不再给 `addAffine/double` 接任何 `trusted scratch mul`；不再用 helper 版或 inline 版重试同一思路；不刷 fixed-point；不碰 `kinv`；不碰窗口表和初始化 |
| 验收 | `_tmp_p256_mul_add_cmp_probe_bin` 和 `_tmp_p256_mul_double_cmp_probe_bin` 都继续 `ok`；完整主 gate 继续前台通过；下一刀只有在 pure `Crash/Value` 路出现真实净收益时才保留 |

| 项目 | 内容 |
|---|---|
| 目标 | 继续压 `ECDSA/P-256`，但停止在 pure 热链里试 `out-parameter` fixed-record 路 |
| 主线 | 这轮试了 `p256_fixed/ecnist` 的 pure `Into/out-parameter` 路，想直接砍掉 `Result[P256Fixed] + Value(res) + record return`。结果已经证伪：字段级 direct `add/sub/mul/square` 对拍本身能过，但一旦进入组合层 `double/quad/eight` 和 `pointJacobianDoubleFixedInPlace/pointJacobianAddAffineFixedInPlace`，就会出现 `step=0` 偏差，补完自别名拆解后还会直接崩。下一刀不能再碰这条 `out-parameter` 热路，要回算法级最短路。 |
| 文件 | `src/std/crypto/p256_fixed.cheng` `src/std/crypto/ecnist.cheng` `v2/tests/contracts/_tmp_p256_mul_double_cmp_probe.cheng` `v2/tests/contracts/_tmp_p256_mul_add_cmp_probe.cheng` `task_plan.md` `progress.md` `findings.md` |
| 不做 | 不再尝试 `p256FieldFixed*CrashInto`、`p256Fixed*Into` 作为 active 热路；不再赌当前普通程序执行面会把 fixed-record `out-parameter` 自动编快 |
| 验收 | 生产代码整块撤回，`_tmp_p256_mul_double_cmp_probe_bin` 和 `_tmp_p256_mul_add_cmp_probe_bin` 回到 `ok`，完整主 gate 继续前台通过；下一步改成更强的固定基点算法评估与落地 |

| 项目 | 内容 |
|---|---|
| 目标 | 继续完成非 VM 链节点真实运行，先把 `cheng-quic` 在正确 `var` 语义下重新编通，再把挂住的状态树同步定位到普通程序运行面真根 |
| 主线 | 这轮已经把 `v2/cheng-quic/src/connection.cheng` 里只读 `copyBytesRange` 的 `Bytes/var Bytes` 双重载收成单一 `Bytes` 版本，`chain_node.cheng` 和 `chain_state_tree_sync_smoke.cheng` 都重新 `system-link-exec` 真链过了，`chain_node_test` 也已经真跑通 `mint/balance`。下一刀不再猜链算法，直接沿 `chain_state_tree_sync_smoke_bin` 的运行挂点继续查普通 `program` 轨。 |
| 文件 | `v2/cheng-quic/src/connection.cheng` `v2/cheng-quic/src/project/chain_node.cheng` `v2/cheng-quic/src/tests/chain_state_tree_sync_smoke.cheng` `task_plan.md` `progress.md` `findings.md` |
| 不做 | 不放宽 `var` 语义；不恢复 `Bytes/var Bytes` 假重载；不把链算法问题和普通程序运行时问题混在一起；不再用会留下悬挂进程的后台探测 |
| 验收 | `chain_node.cheng` 和 `chain_state_tree_sync_smoke.cheng` 必须继续前台真链过；`chain_node_test balance/mint` 必须真跑；完整主 gate 必须前台通过；然后再只盯 `chain_state_tree_sync_smoke_bin` 的真实运行挂点 |

| 项目 | 内容 |
|---|---|
| 目标 | 继续压普通 `program` 运行面，把 `chain_state_tree_sync_smoke` 的热点从名字解析大链继续往下推 |
| 主线 | 这轮已经在 `src/runtime/native/system_helpers_stdio_bridge.c` 把 `top_level_tag`、可见项 cache、`op.kind_tag` 都接进去了；最新一刀再把 `builtin` 例程落成正式 `builtin_tag` 数据面，并且只让 `builtin/importc` 进入 `driver_c_prog_try_builtin`。普通 Cheng 函数现在不再白扫整串 builtin 字符串。下一刀直接打 `driver_c_prog_zero_value_from_type/driver_c_prog_zero_value_from_type_decl` 的预解码和零值原型，不再回头碰 `try_builtin`。 |
| 文件 | `src/runtime/native/system_helpers_stdio_bridge.c` `v2/tests/contracts/compiler_core_system_link_exec.expected` `task_plan.md` `progress.md` `findings.md` |
| 不做 | 不再让普通函数进入 `try_builtin`；不再用后台 smoke；不把 `zero_value` 和链算法问题混在一起 |
| 验收 | `chain_node_test balance` 必须继续返回正确值；`chain_state_tree_sync_smoke.cheng` 必须继续前台真链过；完整主 gate 必须前台通过；然后再用前台 sample 确认热点已从 `try_builtin` 继续下沉 |

| 项目 | 内容 |
|---|---|
| 目标 | 继续压纯 Cheng `P-256/ECDSA` 热核，但只保留真正同时改善 `pubkey/sign` 的改动 |
| 主线 | 这轮已经前台证伪 `pointMulGeneratorJacobianComb6FixedInto(...)` 的 `digits[]` 预计算：correctness 全绿，但 3 次中位数变成 `pubkey=1.3587s`、`sign=2.0194s`、`mul=1.5392s`、`kinv=0.7741s`、`comb=1.1910s`。和稳定基线 `pubkey=1.2830s`、`sign=1.8210s`、`mul=1.4628s`、`kinv=0.7521s` 比，只有 `comb` 局部更快，总链路反而更慢，所以已整块撤回，生产路径回到稳定版。下一刀不再做“预计算数组 + 起始扫描”，而是直接压 `p256ScalarFixedComb6Digit(...)` 本体和 `comb6` 热循环里的 digit 提取成本。 |
| 文件 | `src/std/crypto/ecnist.cheng` `v2/tests/contracts/_tmp_p256_pubkey_scalar_probe.cheng` `v2/tests/contracts/_tmp_ecdsa_sign_probe.cheng` `v2/tests/contracts/_tmp_ecdsa_mul_probe.cheng` `v2/tests/contracts/_tmp_ecdsa_kinv_probe.cheng` `v2/tests/contracts/_tmp_ecdsa_mul_comb_probe.cheng` `task_plan.md` `progress.md` `findings.md` |
| 不做 | 不把 `digits[]` 预计算继续留在 active 路径；不因为 `comb` 局部更快就忽略 `pubkey/sign` 总回归；不先刷主 gate；不碰 `kinv` 支线 |
| 验收 | correctness probe 继续全绿；任何新改动都必须让 `pubkey/sign/mul` 至少不差于稳定基线后才允许保留 |

| 项目 | 内容 |
|---|---|
| 目标 | 先把 `chain_state_tree_sync_smoke` 当前的 `var arg not ref caller=chainAntiEntropyEventLines callee=std/seqs.add` 真根静态收掉，再决定是否进入前台小窗口验证 |
| 主线 | 真根不在链算法，也不在 source lowering，而在 `stage0` 的 `compiler_core_program_lower_expr(call)`：它之前按“正在生成中的 `low_plan`”反查 callee 参数签名，callee 只要排在后面，`var_param` 就会丢成空串，最后把本地数组按值降成 `load_local + materialize`，运行时才炸 `var arg not ref`。这轮先把 stage0 改成按完整 `program/itemId` 查真实 routine 参数签名。 |
| 文件 | `v2/bootstrap/cheng_v2c_tooling.c` `task_plan.md` `progress.md` `findings.md` |
| 不做 | 先不编译；先不跑 smoke；不在 runtime 层做自动 ref 兜底；不把这条问题继续误判成 `anti_entropy/node` 算法问题 |
| 验收 | 静态上 `compiler_core_program_lower_expr(call)` 不再依赖半成品 `low_plan` 取 `param_kind`；记录同步后，再决定是否前台小窗口验证 |

| 项目 | 内容 |
|---|---|
| 目标 | 把 `LSMR.md` 同步成“愿景 + 当前实现状态”正式文档，并继续沿 `cheng-quic` 真实运行路径收根 |
| 主线 | 这轮已经真跑通 `chain_state_tree_sync_smoke`，并把 `anti_entropy` 的裸 `len(...)` 收成显式 `strings.len(...)`；`msquic_chain_smoke` 也已经前台真链接并越过 `lsmr/dispersal/broadcast/anti_entropy/consensus` 五段算法，新的最小根因为 `lsmr.cheng` 里整批裸 `add(...)` 还停在 `load_name|add`。同时已把 [LSMR.md](/Users/lbcheng/cheng-lang/v2/docs/LSMR.md) 顶部同步成“已完成/部分完成/未完成/代码落点/验证”的正式状态表。 |
| 文件 | `v2/cheng-quic/src/chain/anti_entropy.cheng` `v2/cheng-quic/src/tests/chain_state_tree_sync_smoke.cheng` `v2/cheng-quic/src/chain/lsmr.cheng` `v2/docs/LSMR.md` `task_plan.md` `progress.md` `findings.md` |
| 不做 | 不再把 `LSMR` 文档写成纯愿景；不对 `msquic_chain_smoke` 做兜底跳过；不把裸 `add/len` 这类可静态确定的解析问题继续留给 runtime |
| 验收 | [chain_state_tree_sync_smoke.cheng](/Users/lbcheng/cheng-lang/v2/cheng-quic/src/tests/chain_state_tree_sync_smoke.cheng) 前台真跑必须 `rc=0`；[msquic_chain_smoke.cheng](/Users/lbcheng/cheng-lang/v2/cheng-quic/src/tests/msquic_chain_smoke.cheng) 至少要稳定越过算法层并暴露下一处最小 runtime 根；[LSMR.md](/Users/lbcheng/cheng-lang/v2/docs/LSMR.md) 必须明确写出当前实现状态而不是只保留理论叙述 |

| 项目 | 内容 |
|---|---|
| 目标 | 把 [LSMR.md](/Users/lbcheng/cheng-lang/v2/docs/LSMR.md) 里剩下的 `大衍流转 PubSub / CSG 子图过滤 / 空间证明` 一口气补成正式算法模块和正式 smoke |
| 主线 | 这轮已经新增 `v2/cheng-quic/src/chain/pubsub.cheng`、`v2/cheng-quic/src/chain/csg.cheng`、`v2/cheng-quic/src/chain/location_proof.cheng`，并在 `v2/cheng-quic/src/tests/lsmr_advanced_features_smoke.cheng` 里把三块一起前台真编真跑通过。`v2/bootstrap/Makefile` 的 `lsmr-contracts` 也已经接入这条新 smoke，`make -j1 -C /Users/lbcheng/cheng-lang/v2/bootstrap lsmr-contracts` 前台通过。 |
| 文件 | `v2/cheng-quic/src/chain/types.cheng` `v2/cheng-quic/src/chain/pubsub.cheng` `v2/cheng-quic/src/chain/csg.cheng` `v2/cheng-quic/src/chain/location_proof.cheng` `v2/cheng-quic/src/tests/lsmr_advanced_features_smoke.cheng` `v2/tests/contracts/lsmr_advanced_features_smoke.expected` `v2/bootstrap/Makefile` `v2/docs/LSMR.md` `v2/docs/cheng-chain-mvp.md` `task_plan.md` `progress.md` `findings.md` |
| 不做 | 不把这三块硬塞进当前未全闭合的 QUIC runtime；不碰 VM 合约；不把“算法层已完成”伪装成“真实联网已经全部闭环” |
| 验收 | `lsmr_advanced_features_smoke_bin` 必须前台输出 `lsmr_advanced_features_smoke=ok`；`make -j1 -C /Users/lbcheng/cheng-lang/v2/bootstrap lsmr-contracts` 必须前台通过；[LSMR.md](/Users/lbcheng/cheng-lang/v2/docs/LSMR.md) 的状态表必须同步成“技术面已闭合、联网面仍部分完成” |

| 项目 | 内容 |
|---|---|
| 目标 | 先把 `msquic_chain_smoke` 当前 `packet too small` 这条假死根静态收掉，不编译、不跑 smoke，只做正确分片和队列建模 |
| 主线 | 真根已经收敛到 `msquicConnImplQueueData(...)` 把整块 `Bytes` 直接塞成单个 `FrameData`，而 `msquicFrameSize(...)` 还只算裸 payload，连 varint 头都没算。现在已改成两层精确模型：`frame_model` 先按真实 QUIC varint 编码长度计算 `MsQuicFrame` 大小；`connection_impl` 再按 `maxPacketSize - 16` 的真实帧预算把 `FrameData` 切片，并在 `native_runtime` 的 `pipeWrite` 上层按“当前 frame 队列还能装下多少字节”分批入队、每批立刻 flush+pump，避免一次写大 payload 先撞单帧上限，再撞 `128` 帧队列上限。 |
| 文件 | `v2/cheng-quic/src/core/frame_model.cheng` `v2/cheng-quic/src/core/connection_impl.cheng` `v2/cheng-quic/src/native_runtime.cheng` `task_plan.md` `progress.md` `findings.md` |
| 不做 | 这轮不编译；不跑 `msquic_chain_smoke`；不做基于报错重试的启发式切片；不放大 `maxPacketSize` 或 `msQuicMaxQueuedFrames` 去掩盖模型错误 |
| 验收 | 静态上 `FrameData` 编码长度必须和 `msquicPacketPayloadEncode(...)` 同构；`msquicConnImplQueueData(...)` 不能再生成单个必定塞不进包的 frame；`msquicNativePipeWrite(...)` 不能再一次性把超大 payload 全压进本地队列。下一轮再前台小窗口验证。 |

| 项目 | 内容 |
|---|---|
| 目标 | 把 `compiler-core-release -> full-selfhost` 重新收回全绿，并把这轮真实通过的 LSMR runtime smoke 固化进 contract |
| 主线 | 这轮真根不是 LSMR 算法，也不是 runtime 递归，而是 `stage0` 对 module-qualified overloaded `system.panic(...)` 的 imported-field 调用解析不稳。最短路已经落地：在 `src/std/system.cheng` 增非重载入口 `panicStr`，并把 `v2/src/compiler` 全量切到 `system.panicStr(...)`。这样不用继续给每个 compiler 文件补本地 wrapper。随后已把 `compiler_core_release/system_link/system_link_exec/system_link_exec_smoke/tooling_release/tooling_shared_plan/topology_shared_plan/network_selfhost/tooling_selfhost/selfhost_shared_plan/full_selfhost` 这批新 fixed-point 全部刷新，并前台重新跑完整 gate 收口。 |
| 文件 | `src/std/system.cheng` `v2/src/compiler/frontend/v2_source_parser.cheng` `v2/src/compiler/frontend/compiler_core_surface_ir_v2.cheng` `v2/src/compiler/semantic_facts/compiler_core_facts_v2.cheng` `v2/src/compiler/driver/release_artifact_v2.cheng` `v2/src/compiler/driver/manifest_resolver_v2.cheng` `v2/src/compiler/low_uir/compiler_core_lowering_v2.cheng` `v2/src/compiler/obj/obj_file_v2.cheng` `v2/bootstrap/Makefile` `v2/tests/contracts/*.expected` `task_plan.md` `progress.md` `findings.md` |
| 不做 | 不回头改 compiler overload 机制；不再给单个文件各自补 panic wrapper；不跑后台命令；不把 LSMR runtime smoke 再降回“只算算法层通过” |
| 验收 | `make -j1 -C /Users/lbcheng/cheng-lang/v2/bootstrap compiler-core-release compiler-core-system-link compiler-core-system-link-exec tooling-selfhost lsmr-contracts selfhost full-selfhost` 必须前台通过；`msquic_chain_smoke.expected` 和 `chain_state_tree_sync_smoke.expected` 必须继续稳定匹配；然后再继续打普通 program runtime 的下一条活根。 |

| 项目 | 内容 |
|---|---|
| 目标 | 把 `full-selfhost` 扩成正式 `program-selfhost`，让普通程序也由 Cheng 产物自己编自己跑，不再只证明“编译器能编自己” |
| 主线 | 这轮已经把 `program-selfhost-check` 贯穿 source/runtime/native/stage0 四层，并在 `Makefile` 里挂成正式 `program-selfhost` 目标，再纳入 `full-selfhost`。当前 gate 固定要求：stage2 编译器必须真编真跑 `lsmr_advanced_features_smoke`、`chain_state_tree_sync_smoke` 和 `chain_node balance/mint/balance`；同时继续证明 stage2/stage3 release-plan-exec-binary fixed point 相等，以及 `compiler_core` 运行面不再依赖外部 C provider。 |
| 文件 | `v2/src/tooling/cheng_tooling_v2.cheng` `v2/src/runtime/compiler_core_runtime_v2.cheng` `v2/src/runtime/compiler_core_native_dispatch.c` `src/runtime/native/system_helpers.h` `src/runtime/native/system_helpers_stdio_bridge.c` `v2/src/compiler/machine/machine_pipeline_v2.cheng` `v2/bootstrap/cheng_v2c_tooling.c` `v2/bootstrap/Makefile` `v2/tests/contracts/program_selfhost.expected` `v2/tests/contracts/compiler_core_release_artifact.expected` `v2/tests/contracts/compiler_core_system_link_plan.expected` `v2/tests/contracts/compiler_core_system_link_exec.expected` `v2/tests/contracts/full_selfhost.expected` `v2/docs/自举和性能.md` `task_plan.md` `progress.md` `findings.md` |
| 不做 | 不把 `program-selfhost` 降成单文件 probe；不在 runtime 层兜底外部 C provider；不把 `full-selfhost` 继续误当成“整条链路自举已完成” |
| 验收 | `make -j1 -C /Users/lbcheng/cheng-lang/v2/bootstrap program-selfhost` 必须前台通过；`full-selfhost` 必须继续前台通过；`program_selfhost.expected` 和下游 fixed-point 必须稳定匹配；然后下一刀只打 `program` 热路径和纯 Cheng 热核，不回头补语义闭环。 |

| 项目 | 内容 |
|---|---|
| 目标 | 继续完成 `program` 热路径和值表示，先把 typed `record` 的固定布局 lookup 从声明面一路接到运行面 |
| 主线 | 这轮已经在 `src/runtime/native/system_helpers_stdio_bridge.c` 里把 typed `record` 的固定布局初始化继续接到 runtime：`TypeDecl/ZeroPlan` 共享 `field_lookup` 会被 `zero_value_from_plan()/zero_record_shell_from_plan()` 直接复用，`MAKE_RECORD` 也会直接按 decl shape 初始化并在顺序一致时跳过字段名查找。随后 `compiler_core_system_link_exec/program_selfhost/full_selfhost` 已在当前 tree manifest `manifest_fnv1a64=fee6dad00582e08f` 下前台重新收口。下一刀不再碰这条已闭合链，直接打固定布局 `slot/shape` 和 aggregate `field/index update copy`。 |
| 文件 | `src/runtime/native/system_helpers_stdio_bridge.c` `v2/tests/contracts/compiler_core_system_link_exec.expected` `v2/tests/contracts/program_selfhost.expected` `v2/tests/contracts/full_selfhost.expected` `v2/docs/自举和性能.md` `task_plan.md` `progress.md` `findings.md` |
| 不做 | 不回头再做 `field name lookup` 兜底；不继续 rebuild typed record lookup；不把已过的 `program-selfhost/full-selfhost` 再当成当前主根 |
| 验收 | `make -j1 -C /Users/lbcheng/cheng-lang/v2/bootstrap compiler-core-release compiler-core-system-link compiler-core-system-link-exec tooling-selfhost lsmr-contracts selfhost full-selfhost` 必须前台通过；`pubkey/sign/mul/kinv` probe 必须以前台重编后的二进制重新量到真实中位数。 |

| 项目 | 内容 |
|---|---|
| 目标 | 继续把 `v3` 自举入口和 fresh bootstrap bridge 变成可诊断、可收敛的真工具链，不再允许 `rc=223` 和 `wrapper-source build` 失败变成黑盒 |
| 主线 | 这轮先把 [cheng_v3.sh](/Users/lbcheng/cheng-lang/v3/tooling/cheng_v3.sh) 的外层日志从 seed 内层 `out.compile.log` 同名冲突里拆出来，再把 [verify_backend_sidecar_cheng_fresh.sh](/Users/lbcheng/cheng-lang/src/tooling/cheng_tooling_embedded_scripts/verify_backend_sidecar_cheng_fresh.sh) 的 `wrapper-source build` 失败显式打印成 `rc/kind/hint/log`。随后继续顺着 fresh bridge 拆根：proof launcher 现在会尊重 `BACKEND_UIR_SIDECAR_DISABLE=1`，`backend_driver_currentsrc_sidecar_wrapper.sh` 也不再对 bootstrap proof surface 偷开 `wrapper_preserve_sidecar=1`。结果是 `v3` gate 已从“静默 timeout”推进成确定性的 `rc=223 kind=deterministic_exit_223`，并稳定暴露下一处真阻塞 `backend_driver sidecar: missing strict sidecar mode contract`。 |
| 文件 | `v3/tooling/cheng_v3.sh` `v3/tooling/build_backend_driver_v3.sh` `v3/tooling/README.md` `src/tooling/cheng_tooling_embedded_scripts/verify_backend_sidecar_cheng_fresh.sh` `src/tooling/cheng_tooling_embedded_scripts/backend_driver_currentsrc_sidecar_wrapper.sh` `src/tooling/cheng_tooling_embedded_scripts/verify_backend_selfhost_currentsrc_proof.sh` `src/tooling/cheng_tooling_embedded_scripts/verify_backend_selfhost_bootstrap_self_obj.sh` `artifacts/backend_selfhost_self_obj/probe_currentsrc_proof/cheng_stage0_currentsrc.proof` `task_plan.md` `progress.md` `findings.md` |
| 不做 | 不把 `223` 继续伪装成 `signal`；不再让 bootstrap bridge 用 timeout 吞掉真实错误；不回退到旧 `src/tooling/cheng_tooling.cheng` 当 `v3` 主入口 |
| 验收 | `sh /Users/lbcheng/cheng-lang/v3/tooling/cheng_v3.sh print-bootstrap` 必须直接打印 `rc=223 kind=deterministic_exit_223` 和 seed 原始日志；`sh /Users/lbcheng/cheng-lang/v3/tooling/bootstrap_bridge_v3.sh` 与 `sh /Users/lbcheng/cheng-lang/v3/tooling/run_slice_gate.sh` 必须稳定打印 `rc=223 kind=deterministic_exit_223` 和 `missing strict sidecar mode contract`，不再只报 timeout 黑盒。 |

| 项目 | 内容 |
|---|---|
| 目标 | 把 `v2` 已经踩过的性能坑和自举坑正式写进 `v3/docs`，并收成 `v3` 当前开发计划的硬约束 |
| 主线 | 这轮已新增 [v3/docs/README.md](/Users/lbcheng/cheng-lang/v3/docs/README.md)、[v3/docs/v2已踩性能和自举坑.md](/Users/lbcheng/cheng-lang/v3/docs/v2已踩性能和自举坑.md)、[v3/docs/自举和性能开发计划.md](/Users/lbcheng/cheng-lang/v3/docs/自举和性能开发计划.md)，把 `v2/docs/自举和性能.md`、`v2/docs/baguaCSG.md`、`v2/docs/LSMR.md`、当前 `findings.md` 和现有实现里的坑统一收成 `v3` 硬禁令和里程碑；[v3/README.md](/Users/lbcheng/cheng-lang/v3/README.md) 也已接上文档入口。 |
| 文件 | `v3/docs/README.md` `v3/docs/v2已踩性能和自举坑.md` `v3/docs/自举和性能开发计划.md` `v3/README.md` `lessons.md` `task_plan.md` `progress.md` `findings.md` |
| 不做 | 不把长篇计划继续塞进 `v3/README.md` 或 `v3/tooling/README.md`；不再用 `v3/doc` 这种错误目录名 |
| 验收 | `v3/docs` 目录必须存在且三份文档都能直接打开；计划文档必须显式写出 `stage2/stage3`、同机 C `1:1`、AOT-only、bagua/BPI sidecar-only 和 `program-selfhost + chain_node` 这些约束。 |

| 项目 | 内容 |
|---|---|
| 目标 | 按用户要求把 `v3` 之前的编译产物和旧编译工具链全部移除，然后只靠干净 seed 重新拉起 `v3` 的自举入口和性能基线 |
| 主线 | 这轮已经删除根目录 `artifacts/chengcache/build/dist`、根旧二进制 `backend_closedloop_fullspec`，并把 `v2/artifacts` 也整目录清空后重编。干净环境下，`make -C /Users/lbcheng/cheng-lang/v2/bootstrap all` 已重新产出 `v2/artifacts/bootstrap/cheng_v2_bootstrap` 和 `v2/artifacts/bootstrap/cheng_v2c`；`make -C /Users/lbcheng/cheng-lang/v3/bench/c_ref clean run` 和 `v3/tooling/scan_forbidden_hotpath.sh` 也已从零重跑通过。现在 `v3/tooling/bootstrap_bridge_v3.sh` 在纯净环境下直接失败为 `missing executable bridge driver: artifacts/backend_selfhost_self_obj/probe_currentsrc_proof/cheng_stage0_currentsrc.proof`，同时干净 `v2` seed 再跑 `tooling-selfhost-host` 仍然稳定卡 `release stdout mismatch`，手拆 `tooling_stage1_bootstrap` 继续会撞上 `program_entry_exec_plan_missing local_payload_*`。这说明旧产物之前确实遮住了真根，而真根正是 `v2` 老 bootstrap 里那条 `local_payload/exec_plan` 路。 |
| 文件 | `src/tooling/cheng_tooling_embedded_scripts/backend_seed_pure.sh` `src/tooling/cheng_tooling_embedded_scripts/verify_backend_selfhost_currentsrc_proof.sh` `src/tooling/cheng_tooling_embedded_scripts/verify_backend_selfhost_bootstrap_self_obj.sh` `v3/src/tooling/bootstrap_contracts.cheng` `task_plan.md` `progress.md` `findings.md` |
| 不做 | 不恢复任何旧 `artifacts` 或 `chengcache`；不拿旧 `probe_currentsrc_proof` 产物蒙混过关；不对 `tooling-selfhost-host` 的 `release stdout mismatch` 和 `local_payload` 断点做兜底跳过 |
| 验收 | `v2/artifacts/bootstrap/cheng_v2_bootstrap` 和 `v2/artifacts/bootstrap/cheng_v2c` 必须从零重编；`v3` C 基线必须从零重跑；`v3` bootstrap 必须在纯净环境下给出真实缺口，而不是继续吃旧 bridge driver。 |

| 项目 | 内容 |
|---|---|
| 目标 | 把 `v3/docs/cheng-plan-full.md`、`v3/docs/LSMR.md`、`v3/docs/baguaCSG.md`、`v3/docs/自举和性能.md` 收成一份统一的 `cheng v3` 特性矩阵和开发计划 |
| 主线 | 新总览必须把四份文档的边界彻底收正：`HIR/MIR/LIR` 才是权威语义，`bagua/BPI` 只做 sidecar；`LSMR/CSG/反熵/共识` 直接迁语义和 smoke，不直接搬 `v2` 的字符串实现壳；`自举/性能` 只认 `stage2/stage3 + 同机 C 1:1 + AOT-only + program-selfhost + chain_node`。同时把当前发现的结构冲突明写出来，尤其是 `v3` 现有链地址 digit 还写成 `0..7`，和洛书 `1..9` 语义不一致。 |
| 文件 | `v3/docs/cheng语言特性矩阵和开发计划.md` `v3/docs/README.md` `task_plan.md` `progress.md` `findings.md` |
| 不做 | 不删除原始四份文档；不把 `bagua/BPI` 重新提升成新 IR；不把 `v2` 的 canonical text、`payloadText`、`topicCid/payloadSummary` 文本壳带进 `v3` 计划 |
| 验收 | `v3/docs/cheng语言特性矩阵和开发计划.md` 必须明确写出特性矩阵、统一边界、阶段计划、正式 gate、禁止回流项，并被 `v3/docs/README.md` 收进索引。 |

| 项目 | 内容 |
|---|---|
| 目标 | 把 `v3` bootstrap/tooling 主线彻底切到 `v3/bootstrap` 极小 seed，并让整条 `v3` slice gate 在不依赖旧 `probe_currentsrc_proof/sidecar/tooling_cmd` 的前提下真跑通过 |
| 主线 | 这轮已经把 `v3/tooling/{bootstrap_bridge_v3,build_backend_driver_v3,run_slice_gate,cheng_v3}.sh`、`v3/src/tooling/bootstrap_contracts.cheng`、`v3/src/backend/build_plan.cheng`、`v3/src/tests/bootstrap_contracts_smoke.cheng` 全部切到新主线。当前真实链路是：`cc` 先编 `v3/bootstrap/cheng_v3_seed.c` 得到 `cheng.stage0`，再由 `stage0 -> stage1 -> stage2 -> stage3` 连续 `compile-bootstrap`；`build_backend_driver_v3.sh` 直接用 `stage2` 物化 `artifacts/v3_backend_driver/cheng`；`run_slice_gate.sh` 只认 `scan + C baseline + bootstrap bridge + backend driver + bootstrap subset self-check + contract equivalence`，不再回落到旧 `tooling_cmd/cheng_tooling`。 |
| 文件 | `v3/bootstrap/cheng_v3_seed.c` `v3/tooling/cheng_v3.sh` `v3/tooling/bootstrap_bridge_v3.sh` `v3/tooling/build_backend_driver_v3.sh` `v3/tooling/run_slice_gate.sh` `v3/src/tooling/bootstrap_contracts.cheng` `v3/src/backend/build_plan.cheng` `v3/src/tests/bootstrap_contracts_smoke.cheng` `task_plan.md` `progress.md` `findings.md` |
| 不做 | 不再依赖 `probe_currentsrc_proof/cheng_stage0_currentsrc.proof`；不再调用旧 `verify_backend_sidecar_cheng_fresh.sh`；不再用旧 `tooling_cmd/cheng_tooling` 跑 `v3` 主 gate；不把 Mach-O 可执行字节硬比当 Darwin 下的 fixed-point 唯一口径 |
| 验收 | `sh /Users/lbcheng/cheng-lang/v3/tooling/bootstrap_bridge_v3.sh` 必须通过；`sh /Users/lbcheng/cheng-lang/v3/tooling/build_backend_driver_v3.sh` 必须通过；`sh /Users/lbcheng/cheng-lang/v3/tooling/run_slice_gate.sh` 必须通过；`sh /Users/lbcheng/cheng-lang/v3/tooling/cheng_v3.sh print-bootstrap` 和 `print-build-plan` 必须直接输出新 `v3/bootstrap` 路径，不再出现旧 proof 术语。 |

| 项目 | 内容 |
|---|---|
| 目标 | 把 `v3` 里最后残留的旧 bootstrap/tooling 引用彻底清零，并让入口代码、bootstrap 合同、README、实际 gate 三者完全一致 |
| 主线 | 这轮已经把 `v3/src/tooling/gate_main.cheng` 的 `run-smokes` 改成只认 `artifacts/v3_bootstrap/cheng.stage0~3` 的 `self-check`，删掉旧 `artifacts/tooling_cmd/cheng_tooling` 默认值；同时把 `v3/bootstrap/cheng_v3_seed.c` 和 `v3/bootstrap/stage1_bootstrap.cheng` 里的旧 proof 禁词改成泛化的 `legacy_proof_surface/legacy_sidecar_mode`，并把 `v3/README.md`、`v3/tooling/README.md`、`v3/bootstrap/README.md` 全部收正到 `seed -> stage0 -> stage1 -> stage2 -> stage3` 主线。随后 `rg` 扫描 `v3` 已确认零命中旧 proof/tooling 词，`bootstrap_bridge_v3.sh`、`build_backend_driver_v3.sh`、`cheng_v3.sh run-smokes`、`run_slice_gate.sh` 也都前台通过。 |
| 文件 | `v3/src/tooling/gate_main.cheng` `v3/bootstrap/cheng_v3_seed.c` `v3/bootstrap/stage1_bootstrap.cheng` `v3/README.md` `v3/tooling/README.md` `v3/bootstrap/README.md` `task_plan.md` `progress.md` `findings.md` |
| 不做 | 不回头恢复任何旧 proof/path 兼容描述；不把 README 写成与实际 gate 不一致的“假入口”；不再让 `v3` 活入口偷偷保留旧 `tooling_cmd` 回退 |
| 验收 | `rg -n \"probe_currentsrc_proof|stage2_proof|stage3_witness|currentsrc_proof|tooling_cmd/cheng_tooling|cheng_stage0_currentsrc\\.proof|sidecar contract\" /Users/lbcheng/cheng-lang/v3 -S` 必须零命中；`sh /Users/lbcheng/cheng-lang/v3/tooling/bootstrap_bridge_v3.sh`、`sh /Users/lbcheng/cheng-lang/v3/tooling/build_backend_driver_v3.sh`、`sh /Users/lbcheng/cheng-lang/v3/tooling/cheng_v3.sh run-smokes`、`sh /Users/lbcheng/cheng-lang/v3/tooling/run_slice_gate.sh` 必须全部前台通过。 |

| 项目 | 内容 |
|---|---|
| 目标 | 按 `v3/docs/cheng语言特性矩阵和开发计划.md` 并行推进当前执行顺序 `1-5`，把 `v3` 链语义固定布局主线和正式 host smoke gate 一次性接起来 |
| 主线 | 这轮已经把 `v3/src/chain` 扩成固定布局语义核：`lsmr_types/lsmr/anti_entropy/csg/pubsub/location_proof/consensus` 全部落地；同时新增 `lsmr_types_smoke/lsmr_locality_storage_smoke/lsmr_bagua_prefix_tree_smoke/anti_entropy_smoke/csg_smoke/pubsub_smoke/location_proof_smoke/consensus_smoke`，再用 `v3/tooling/run_v3_host_smokes.sh` 收成统一 host smoke 入口，并把 `run_slice_gate.sh` 正式接上这条链。当前 `fixed_surface/csg/consensus/pubsub/location_proof` 已经 host-run 通过，`chain_codec_binary_smoke` 则稳定把真阻塞暴露成 `driver_c program runtime: missing type decl for zero init type=int32 inst_owner=v3/tests/chain_codec_binary_smoke`；`anti_entropy` 和 `lsmr_*` 也都已经真编过，下一刀只该收这个宿主 runtime 缺口，不再回头修旧 proof/sidecar 或字符串壳。 |
| 文件 | `v3/cheng-package.toml` `v3/src/chain/binary_types.cheng` `v3/src/chain/codec_binary.cheng` `v3/src/chain/lsmr_types.cheng` `v3/src/chain/lsmr.cheng` `v3/src/chain/anti_entropy.cheng` `v3/src/chain/csg.cheng` `v3/src/chain/pubsub.cheng` `v3/src/chain/location_proof.cheng` `v3/src/chain/consensus.cheng` `v3/src/tests/chain_codec_binary_smoke.cheng` `v3/src/tests/fixed_surface_smoke.cheng` `v3/src/tests/lsmr_types_smoke.cheng` `v3/src/tests/lsmr_locality_storage_smoke.cheng` `v3/src/tests/lsmr_bagua_prefix_tree_smoke.cheng` `v3/src/tests/anti_entropy_smoke.cheng` `v3/src/tests/csg_smoke.cheng` `v3/src/tests/pubsub_smoke.cheng` `v3/src/tests/location_proof_smoke.cheng` `v3/src/tests/consensus_smoke.cheng` `v3/tooling/run_v3_host_smokes.sh` `v3/tooling/run_slice_gate.sh` `v3/README.md` `v3/tooling/README.md` `v3/docs/cheng语言特性矩阵和开发计划.md` `task_plan.md` `progress.md` `findings.md` |
| 不做 | 不把 host smoke 失败包装成通过；不把 compile-only 伪装成运行通过；不把 `ir_core` 这条无关老坑放到链 gate 前面抢失败位；不把 `v2` 文本链壳、`payloadText`、`BigInt/Bytes/Seq` 热链拉回 `v3` |
| 验收 | `sh /Users/lbcheng/cheng-lang/v3/tooling/run_v3_host_smokes.sh` 必须先稳定跑过 `fixed_surface/csg/consensus/pubsub/location_proof`，再把 `chain_codec_binary_smoke` 的真失败点固定暴露；`sh /Users/lbcheng/cheng-lang/v3/tooling/run_slice_gate.sh` 也必须前台复现同一个失败位，而不是再回到旧 bridge/tooling 黑盒。 |

| 项目 | 内容 |
|---|---|
| 目标 | 把 `v3` ordinary compile 从 `lowering` 再推进两层到 `object/native-link`，同时守住 `scan + host smokes + bootstrap + backend driver` 全绿 |
| 主线 | 这轮已经在 `v3` 新主线上补了 `backend/object_plan.cheng`、`backend/native_link_plan.cheng`，把 `system_link_exec` 从只导出 `source closure + runtime targets/provider + lowering inventory` 推进到同时导出 `primary_object/provider_object_paths/object_link_inputs/native_link_inputs`。`stage1_bootstrap`、`bootstrap_contracts`、`build_plan`、seed C 也已同步加入 `backend_object_plan_source/backend_native_link_plan_source`，并把 `ordinary_pipeline_state` 收正到 `native_link_plan_stub_missing_codegen`。当前 `bootstrap_bridge_v3.sh`、`build_backend_driver_v3.sh`、`scan_forbidden_hotpath.sh`、`run_v3_host_smokes.sh` 全部前台通过；`build_program_selfhost_v3.sh` 和 `build_chain_node_v3.sh` 现在统一稳定失败在 `v3 compiler: object and native link plans ready, machine code emission and final link not implemented`，说明真阻塞已经从 lowering 推进到机器码和最终链接。 |
| 文件 | `v3/src/backend/system_link_exec.cheng` `v3/src/backend/object_plan.cheng` `v3/src/backend/native_link_plan.cheng` `v3/src/backend/build_plan.cheng` `v3/src/tooling/bootstrap_contracts.cheng` `v3/src/tooling/compiler_runtime.cheng` `v3/src/tooling/compiler_main.cheng` `v3/bootstrap/stage1_bootstrap.cheng` `v3/bootstrap/cheng_v3_seed.c` `v3/src/tests/lowering_plan_smoke.cheng` `v3/src/tests/object_native_link_plan_smoke.cheng` `v3/src/tests/compiler_runtime_smoke.cheng` `v3/tooling/run_v3_host_smokes.sh` `v3/docs/cheng语言特性矩阵和开发计划.md` `task_plan.md` `progress.md` `findings.md` |
| 不做 | 不伪造 `.o` 或可执行产物；不把新 `outputKind` 再写回 `kind: str`；不回头修旧 proof/tooling；不把 `provider/native-link` 重新做成字符串命令壳；不把 host runtime 的限制误当成 `v3` 语义设计错误 |
| 验收 | `sh /Users/lbcheng/cheng-lang/v3/tooling/scan_forbidden_hotpath.sh` 必须通过；`sh /Users/lbcheng/cheng-lang/v3/tooling/run_v3_host_smokes.sh` 必须通过；`sh /Users/lbcheng/cheng-lang/v3/tooling/bootstrap_bridge_v3.sh` 和 `sh /Users/lbcheng/cheng-lang/v3/tooling/build_backend_driver_v3.sh` 必须通过；`sh /Users/lbcheng/cheng-lang/v3/tooling/build_program_selfhost_v3.sh` 与 `sh /Users/lbcheng/cheng-lang/v3/tooling/build_chain_node_v3.sh` 必须稳定报同一条 `object and native link plans ready...`；`sh /Users/lbcheng/cheng-lang/v3/tooling/run_slice_gate.sh` 必须在 `program-selfhost` 处复现同一失败位。 |

| 项目 | 内容 |
|---|---|
| 目标 | 继续把 `v3` ordinary compile 从“能发 `.o` 和最小可执行”推进到真正的函数体语义子集，并先把 ordinary 入口桥的运行时自旋真根收掉 |
| 主线 | 这轮已经在 `v3/bootstrap/cheng_v3_seed.c` 里把 ordinary entry bridge 和 `return_call_noarg_i32` 从 `bl callee; ret` 改成直接 `b callee`，因为旧写法会覆盖 `LR`，最后在入口桥上自旋。随后重新清空 `artifacts/v3_bootstrap`、`artifacts/v3_backend_driver`、`artifacts/v3_zero_exit`、`artifacts/v3_call_chain` 并从零重编；`build_zero_exit_v3.sh` 和新加的 `build_call_chain_v3.sh` 已经真编、真链、真跑通过，`run_v3_host_smokes.sh` 也重新前台全绿。现在 `build_program_selfhost_v3.sh` 仍稳定报 `v3 compiler: primary object body semantics missing`；`build_chain_node_v3.sh` 也稳定报同一个大类错误，并额外带多处 `primary_object_call_target_missing`。这说明 `.o/link/argv bridge` 已经收完，下一刀只该扩 ordinary body 语义，不回头再修入口壳。 |
| 文件 | `v3/bootstrap/cheng_v3_seed.c` `v3/src/tests/ordinary_call_chain_fixture.cheng` `v3/tooling/build_call_chain_v3.sh` `v3/tooling/run_slice_gate.sh` `v3/README.md` `v3/tooling/README.md` `v3/docs/cheng语言特性矩阵和开发计划.md` `task_plan.md` `progress.md` `findings.md` |
| 不做 | 不把尾调用再写回 `bl ...; ret`；不把 host smoke 通过误报成 ordinary compile 已完成；不回头再查 `.o/link/argv/contract` 假阻塞 |
| 验收 | `sh /Users/lbcheng/cheng-lang/v3/tooling/build_zero_exit_v3.sh` 和 `sh /Users/lbcheng/cheng-lang/v3/tooling/build_call_chain_v3.sh` 必须前台通过；`sh /Users/lbcheng/cheng-lang/v3/tooling/run_v3_host_smokes.sh` 必须前台全绿；`sh /Users/lbcheng/cheng-lang/v3/tooling/build_program_selfhost_v3.sh` 和 `sh /Users/lbcheng/cheng-lang/v3/tooling/build_chain_node_v3.sh` 必须继续稳定暴露 `primary object body semantics missing` 这一类真实下一层语义缺口。 |

| 项目 | 内容 |
|---|---|
| 目标 | 继续把 `v3` ordinary compile 的真缺口收窄到最小可达函数集合，避免 `program_selfhost/chain_node` 再被整包导入噪音淹没 |
| 主线 | 这轮已经在 `v3/bootstrap/cheng_v3_seed.c` 里补了 `import alias` 解析、调用边采集和 entry-reachable 函数裁剪。结果是 `build_program_selfhost_v3.sh` 的 lowering 集合已经从 `598` 个函数压到 `16` 个，只剩 `program_selfhost_smoke -> bootstrap_contracts -> path` 这条最短链；`build_chain_node_v3.sh` 也从 `1120` 个函数压到 `112` 个，直接暴露出 `chain_node_main -> chain_node -> consensus/lsmr/anti_entropy/fixed256` 这条可达主链。`build_backend_driver_v3.sh`、`build_zero_exit_v3.sh`、`build_call_chain_v3.sh`、`run_v3_host_smokes.sh`、`scan_forbidden_hotpath.sh` 都已重新前台通过；当前普通编译仍统一停在 `primary object body semantics missing`，但现在已经只剩真实语义缺口。 |
| 文件 | `v3/bootstrap/cheng_v3_seed.c` `v3/docs/cheng语言特性矩阵和开发计划.md` `v3/README.md` `v3/tooling/README.md` `task_plan.md` `progress.md` `findings.md` |
| 不做 | 不把 reachability 裁剪退回整包 lowering；不把 `std/os`、`std/strutils` 这些 helper 再当成必须整体真编的噪音入口；不把还未实现的 body semantics 伪装成“gate 通过” |
| 验收 | `sh /Users/lbcheng/cheng-lang/v3/tooling/build_backend_driver_v3.sh`、`sh /Users/lbcheng/cheng-lang/v3/tooling/build_zero_exit_v3.sh`、`sh /Users/lbcheng/cheng-lang/v3/tooling/build_call_chain_v3.sh`、`sh /Users/lbcheng/cheng-lang/v3/tooling/run_v3_host_smokes.sh`、`sh /Users/lbcheng/cheng-lang/v3/tooling/scan_forbidden_hotpath.sh` 必须前台通过；`build_program_selfhost_v3.sh` 的 `lowering_function_count` 必须稳定是 `16`；`build_chain_node_v3.sh` 的 `lowering_function_count` 必须稳定是 `112`。 |

| 项目 | 内容 |
|---|---|
| 目标 | 继续把 `v3` ordinary compile 从“最小 tail-call 子集”推进到 `program_selfhost` 真编真跑，并把 `chain_node` 的失败位收敛成纯函数体语义缺口 |
| 主线 | 这轮先在 `v3/bootstrap/cheng_v3_seed.c` 收掉 consteval 的栈爆和源码缓冲所有权，再把 multi-line signature 的解析边界收正成“完整读文件后第二遍匹配”，并跳过 `importc fn` 这类无函数体声明。随后修掉 consteval 生成汇编里 `bl _puts` 覆盖 `LR/x30` 的活 bug，最终让 `build_program_selfhost_v3.sh` 真编、真链、真跑通过。并行还把 no-arg tail-call 的 callee 解析从“同模块裸名字匹配”改成复用 lowering 已采集的完整 `callee_symbols[0]`，于是 `build_chain_node_v3.sh` 和 `run_slice_gate.sh` 里的 `primary_object_call_target_missing` 已消失，统一只剩 `primary_object_body_semantics_missing`。当前整条 gate 真实状态是：`scan_forbidden_hotpath.sh` 通过，`build_backend_driver_v3.sh` 通过，`build_zero_exit_v3.sh` 和 `build_call_chain_v3.sh` 通过，`build_program_selfhost_v3.sh` 通过，`run_v3_host_smokes.sh` 前台全绿，`run_slice_gate.sh` 稳定只死在 `chain_node 未接通`。 |
| 文件 | `v3/bootstrap/cheng_v3_seed.c` `task_plan.md` `progress.md` `findings.md` |
| 不做 | 不把 consteval 死循环伪装成脚本卡住；不再用同模块裸函数名猜 tail-call callee；不回头修已收掉的 `.o/link/argv` 假阻塞；不把 `chain_node` 现在的纯函数体缺口包装成 runtime 或 call graph 问题 |
| 验收 | `sh /Users/lbcheng/cheng-lang/v3/tooling/build_program_selfhost_v3.sh` 必须前台输出 `v3 program_selfhost_smoke ok`；`sh /Users/lbcheng/cheng-lang/v3/tooling/build_chain_node_v3.sh` 必须不再出现 `primary_object_call_target_missing`；`sh /Users/lbcheng/cheng-lang/v3/tooling/run_slice_gate.sh` 必须稳定只在 `chain_node` 阶段失败，并把失败位固定成 `primary_object_body_semantics_missing`。 |

| 项目 | 内容 |
|---|---|
| 目标 | 把 `v3` seed 的 ordinary reachable 图修正成真实依赖图，停止让 bare import 调用漏边，并把 `chain_node` 的 unsupported 前沿从截断的 8 条假主根放大成完整可执行主根。 |
| 主线 | 这轮在 `v3/bootstrap/cheng_v3_seed.c` 里做了两件硬修正：一是把 `CHENG_V3_MAX_UNSUPPORTED_DETAILS` 从 `8` 放大到 `64`；二是把 lowering 的调用边收集改成“两阶段”，先收函数，再按完整 lowering 库重新解析每个函数体里的调用。新的 bare call 解析规则不再默认把 `bytesAlloc/bytesLen/bytesSet/intToStr/rawmemCopy` 这类裸调用错判成当前模块，而是优先同模块精确命中，否则在当前 import 集里按“唯一已收集符号”解析到真实模块。结果是 `build_chain_node_v3.sh` 的 `lowering_function_count` 从 `110` 升到 `126`，新暴露出的真实 reachable 集已经包含 `std/rawbytes::*`、`std/rawmem_support::*`、`std/strings::intToStr`、`std/system::str*` 这些此前被漏掉的依赖。当前 `chain_node` 的真实失败不再是“最前 8 个函数”，而是普通函数体子集整体还没接上；日志前 64 个 unsupported 已明确覆盖 `std/system`、`rawbytes`、`bytes_layout`、`anti_entropy`、`lsmr`、`sha256` 这几层。 |
| 文件 | `v3/bootstrap/cheng_v3_seed.c` `task_plan.md` `progress.md` `findings.md` |
| 不做 | 不再拿被漏边裁瘦的 reachable 集做决策；不再把 bare import 调用当成当前模块函数；不再让 unsupported 明细只露 8 条就开始补语义；不回头修已经通过的 `build_backend_driver_v3.sh`、`run_v3_host_smokes.sh`、`scan_forbidden_hotpath.sh`。 |
| 验收 | `cc -std=c11 -O0 -Wall -Wextra -pedantic /Users/lbcheng/cheng-lang/v3/bootstrap/cheng_v3_seed.c -o /tmp/cheng_v3_seed_check` 必须无 warning 通过；`sh /Users/lbcheng/cheng-lang/v3/tooling/build_backend_driver_v3.sh` 必须通过；`sh /Users/lbcheng/cheng-lang/v3/tooling/build_chain_node_v3.sh` 必须仍稳定失败在 `primary_object_body_semantics_missing`，但 `lowering_function_count` 必须扩大到 `126`，`primary_object_unsupported_function_count` 必须扩大到 `64`；`sh /Users/lbcheng/cheng-lang/v3/tooling/run_v3_host_smokes.sh` 和 `sh /Users/lbcheng/cheng-lang/v3/tooling/scan_forbidden_hotpath.sh` 必须继续通过。 |

| 项目 | 内容 |
|---|---|
| 目标 | 让新的 reachable 修正和句型分类真正进入 `stage2/stage3` 自举链，并把 `chain_node` 的 ordinary 主根从“函数名单”推进成“句型族名单”。 |
| 主线 | 这轮已经先重跑 [bootstrap_bridge_v3.sh](/Users/lbcheng/cheng-lang/v3/tooling/bootstrap_bridge_v3.sh) 刷新 `cheng.stage0~3`，再重跑 [build_backend_driver_v3.sh](/Users/lbcheng/cheng-lang/v3/tooling/build_backend_driver_v3.sh) 刷新 [artifacts/v3_backend_driver/cheng](/Users/lbcheng/cheng-lang/artifacts/v3_backend_driver/cheng)。刷新后 [chain_node.compile.log](/Users/lbcheng/cheng-lang/artifacts/v3_chain_node/chain_node.compile.log) 已经不再把前 64 项都标成 `unsupported`，而是分裂成 `stmt_call`、`stmt_var`、`stmt_let`、`stmt_if`、`stmt_for`、`return_expr`。这一步把下一刀从“继续猜 blocker 函数”收成了“按句型族补 ordinary body semantics”。 |
| 文件 | `v3/bootstrap/cheng_v3_seed.c` `v3/tooling/bootstrap_bridge_v3.sh` `v3/tooling/build_backend_driver_v3.sh` `artifacts/v3_chain_node/chain_node.compile.log` `task_plan.md` `progress.md` `findings.md` |
| 不做 | 不再修改 seed 后直接跳过 bootstrap 链刷新；不再把 compile log 里的 `body_kind` 全部当成一类；不再围着旧的 `print/panic/byteBuf*` 8 项做点状修补。 |
| 验收 | `sh /Users/lbcheng/cheng-lang/v3/tooling/bootstrap_bridge_v3.sh` 必须通过；`sh /Users/lbcheng/cheng-lang/v3/tooling/build_backend_driver_v3.sh` 必须通过；`sh /Users/lbcheng/cheng-lang/v3/tooling/build_chain_node_v3.sh` 的 `first unsupported function` 必须仍指向 `v3ChainNodeMainPrint`，但 `body_kind` 必须变成 `stmt_call`，且 `primary_object_unsupported_body_kinds` 必须出现 `stmt_call/stmt_var/stmt_let/stmt_if/stmt_for/return_expr` 这些真实句型。 |

| 项目 | 内容 |
|---|---|
| 目标 | 把 `chain_node` 的 ordinary blocker 再往前推一层，并把“缺哪种 ABI 形态”直接写进 compile log，停止只看函数名和句型。 |
| 主线 | 这轮在 `v3/bootstrap/cheng_v3_seed.c` 给 lowering 补了参数名、参数类型、返回类型和 ABI 分类，并把 `primary_object_unsupported_abi` 一起写进 `chain_node.compile.log`。随后重跑 `bootstrap_bridge_v3.sh` 和 `build_backend_driver_v3.sh`，新的 `build_chain_node_v3.sh` 已能直接报出 `first unsupported function=... abi=...`。同时顺手把 [chain_node_main.cheng](/Users/lbcheng/cheng-lang/v3/src/project/chain_node_main.cheng) 里的 `v3ChainNodeMainPrint` 空包装删掉，并把 [fixed256.cheng](/Users/lbcheng/cheng-lang/v3/src/std/crypto/fixed256.cheng) 里的 `panicStr` 收成 `panic`，让 blocker 直接前移到 `v3ChainNodeMainSelfTest`。当前真实前沿已经明确：`chain_node` 现在先卡 `stmt_var` 主体，ABI 主要先卡 `str/record/Bytes/ByteBuf/array` 这类 composite 形态；`program_selfhost` 继续通过，`run_v3_host_smokes.sh` 和 `scan_forbidden_hotpath.sh` 继续全绿。 |
| 文件 | `v3/bootstrap/cheng_v3_seed.c` `v3/src/project/chain_node_main.cheng` `v3/src/std/crypto/fixed256.cheng` `artifacts/v3_chain_node/chain_node.compile.log` `task_plan.md` `progress.md` `findings.md` |
| 不做 | 不再围着 `v3ChainNodeMainPrint` 这个 `str` 包装函数原地打转；不在 ABI 未定的情况下先碰 `Bytes/ByteSpan/ByteBuf/FixedBytes32/V3LsmrAddress` 的按值 wrapper；不把 host smoke 绿灯误当成 `chain_node` ordinary 已通。 |
| 验收 | `cc -std=c11 -O0 -Wall -Wextra -pedantic /Users/lbcheng/cheng-lang/v3/bootstrap/cheng_v3_seed.c -o /tmp/cheng_v3_seed_check` 必须无 warning；`sh /Users/lbcheng/cheng-lang/v3/tooling/bootstrap_bridge_v3.sh` 和 `sh /Users/lbcheng/cheng-lang/v3/tooling/build_backend_driver_v3.sh` 必须通过；`sh /Users/lbcheng/cheng-lang/v3/tooling/build_chain_node_v3.sh` 的首个 blocker 必须前移到 `v3ChainNodeMainSelfTest`，并打印 `abi=ret=int32/i32 params=-`；`sh /Users/lbcheng/cheng-lang/v3/tooling/run_v3_host_smokes.sh` 和 `sh /Users/lbcheng/cheng-lang/v3/tooling/scan_forbidden_hotpath.sh` 必须继续通过。 |

| 项目 | 内容 |
|---|---|
| 目标 | 把 `chain_node` 的 ordinary blocker 从“复合返回值错归属 + composite param 总拒绝”继续推进到真正的复合 ABI/body semantics 主层，同时守住 `bootstrap/backend_driver/program_selfhost/host_smokes` 全绿。 |
| 主线 | 这轮在 `v3/bootstrap/cheng_v3_seed.c` 先把 internal callee 的 `param_types/return_type` 改成按 callee 自己模块和 import alias 归一化，收掉了 `V3ChainNode -> chain_node_main::V3ChainNode` 的错归属；随后继续补了两层最小 ordinary 语义：一是 `composite` 参数槽位允许按地址进入函数体，不再在 emitter 入口直接拒绝；二是本地复合值的单层标量字段读取 `base.field` 已接通到类型推断和标量 codegen。现在 `build_chain_node_v3.sh` 已不再报 `V3ChainNode` layout/local slot 失败，也不再整批死在 `emit reject composite param`，新的最前沿已经收敛成 `std/crypto/sha256::getU32BE` 里的 `let b0: int64 = int64(bytesGet(data, offset))`。 |
| 文件 | `v3/bootstrap/cheng_v3_seed.c` `artifacts/v3_chain_node/chain_node.compile.log` `task_plan.md` `progress.md` `findings.md` |
| 不做 | 不再按 `Bytes/str/FixedBytes32` 各自发明一套 ABI；不回头修已经收掉的 `V3ChainNode` 错模块归一化；不把 host smoke 绿灯误当成 `chain_node` ordinary 已通。 |
| 验收 | `cc -std=c11 -O0 -Wall -Wextra -pedantic /Users/lbcheng/cheng-lang/v3/bootstrap/cheng_v3_seed.c -o /tmp/cheng_v3_seed_check` 必须无 warning；`sh /Users/lbcheng/cheng-lang/v3/tooling/bootstrap_bridge_v3.sh`、`sh /Users/lbcheng/cheng-lang/v3/tooling/build_backend_driver_v3.sh`、`sh /Users/lbcheng/cheng-lang/v3/tooling/build_program_selfhost_v3.sh`、`sh /Users/lbcheng/cheng-lang/v3/tooling/run_v3_host_smokes.sh` 必须前台通过；`sh /Users/lbcheng/cheng-lang/v3/tooling/build_chain_node_v3.sh` 必须稳定只暴露新的 `getU32BE -> bytesGet(data, offset)` 真阻塞。 |

| 项目 | 内容 |
|---|---|
| 目标 | 把 `assert/echo` 和“复合临时实参无地址”这两个 ordinary 真根直接收掉，并把 `chain_node` 的失败位推进到下一层真实语义缺口。 |
| 主线 | 这轮在 `v3/bootstrap/cheng_v3_seed.c` 做了三件硬改动：一是 ordinary 里的 bare `assert/echo` 不再走普通调用解析，而是直接做语句级 builtin lowering；`assert(cond, "msg")` 现在会先算布尔条件，失败时输出确定性字符串再 `_exit(1)`，`echo("msg")` 直接发 `_puts`。二是普通调用和 `call_into_slot` 都改成“先按 call-depth 把参数落到调用暂存区，再统一装入 `x0..x7` 调用”，不再让 nested call 冲掉前一个参数。三是给复合实参加了显式临时槽规划和 materialize 路径，`v3AntiEntropySignatureCid(local)`、`layout.fixedBytes32ToBytes(value)` 这类复合临时表达式现在会先落到可取址 slot 再传地址。结果是：`build_program_selfhost_v3.sh` 继续通过，`run_v3_host_smokes.sh` 继续全绿，`build_chain_node_v3.sh` 已经不再出现 `scalar call resolve failed ... callee=assert`，也不再出现 `scalar call composite-arg local missing ...`。当前 `chain_node` 的真实主根已经前移到更大一层：大量 `composite return function` 和 `composite field projection` 还没进 ordinary emitter，典型暴露面是 compile log 里仍然成片的 `ret=.../composite` 函数和 `mintCidRes.value`、`served.tipEventCid` 这类复合字段路径。 |
| 文件 | `v3/bootstrap/cheng_v3_seed.c` `artifacts/v3_chain_node/chain_node.compile.log` `task_plan.md` `progress.md` `findings.md` |
| 不做 | 不把 `assert/echo` 从 intrinsic 名单里删掉去碰重载解析；不再让普通调用边走边塞寄存器；不再让复合临时实参靠字符串精确命中 local 名字。 |
| 验收 | `cc -std=c11 -O0 -Wall -Wextra -pedantic /Users/lbcheng/cheng-lang/v3/bootstrap/cheng_v3_seed.c -o /tmp/cheng_v3_seed_check` 必须通过；`sh /Users/lbcheng/cheng-lang/v3/tooling/bootstrap_bridge_v3.sh`、`sh /Users/lbcheng/cheng-lang/v3/tooling/build_backend_driver_v3.sh`、`sh /Users/lbcheng/cheng-lang/v3/tooling/build_program_selfhost_v3.sh`、`sh /Users/lbcheng/cheng-lang/v3/tooling/run_v3_host_smokes.sh` 必须前台通过；`sh /Users/lbcheng/cheng-lang/v3/tooling/build_chain_node_v3.sh` 的 compile log 里必须不再出现 `callee=assert` 和 `composite-arg local missing`。 |

| 项目 | 内容 |
|---|---|
| 目标 | 把 `sret/x8`、复合本地槽位零初始化、赋值语句、复合 return 和字段路径读写接进 ordinary 主链，把 `chain_node` 的真阻塞从复合 ABI 前移到控制流。 |
| 主线 | 这轮在 `v3/bootstrap/cheng_v3_seed.c` 收了五层硬语义：一是复合返回统一改成 `x8 sret`，callee 进函数就把 `x8` spill 到隐藏本地槽位，不再把隐藏返回地址塞进 `x0` 冲掉用户参数；二是新增 `var/let` 无初始化声明解析和零初始化发码，复合本地槽位会按固定布局清零；三是新增普通赋值语句解析，已经支持 `local = expr`、`local.field = expr`，其中复合值会直接走 address materialize，不再只能走 call wrapper；四是补了 `Result[T]` 字段布局、`store_id` 字段别名和多层字段路径解析，`mintCidRes.ok/value`、`served.tipEventCid` 这类读取已经有统一数据面；五是复合表达式 materialize 现在支持 `local`、`local.field`、字符串字面量、复合调用、构造器和 `[]` 空序列。结果是：`bootstrap_bridge_v3.sh`、`build_backend_driver_v3.sh`、`build_program_selfhost_v3.sh`、`run_v3_host_smokes.sh`、`scan_forbidden_hotpath.sh` 全部继续通过；`build_chain_node_v3.sh` 的首个 blocker 已经稳定前移到 `std/system::strViewWithFlags` 的 `stmt_if`，说明 `chain_node` 当前第一堵墙已经不是复合 ABI，而是真控制流。 |
| 文件 | `v3/bootstrap/cheng_v3_seed.c` `artifacts/v3_chain_node/chain_node.compile.log` `task_plan.md` `progress.md` `findings.md` |
| 不做 | 不回退到 `x0` 伪隐藏参数；不再用裸字符串猜字段类型；不把复合 return 继续强塞进 `v3_codegen_expr_scalar`；不把 `if/for/while` 和复合 ABI 混成一个问题。 |
| 验收 | `cc -std=c11 -O0 -Wall -Wextra -pedantic /Users/lbcheng/cheng-lang/v3/bootstrap/cheng_v3_seed.c -o /tmp/cheng_v3_seed_check` 必须通过；`sh /Users/lbcheng/cheng-lang/v3/tooling/bootstrap_bridge_v3.sh`、`sh /Users/lbcheng/cheng-lang/v3/tooling/build_backend_driver_v3.sh`、`sh /Users/lbcheng/cheng-lang/v3/tooling/build_program_selfhost_v3.sh`、`sh /Users/lbcheng/cheng-lang/v3/tooling/run_v3_host_smokes.sh`、`sh /Users/lbcheng/cheng-lang/v3/tooling/scan_forbidden_hotpath.sh` 必须通过；`sh /Users/lbcheng/cheng-lang/v3/tooling/build_chain_node_v3.sh` 必须稳定把 `first unsupported function` 收敛到 `stmt_if`。 |

| 项目 | 内容 |
|---|---|
| 目标 | 把 `chain_node` 的 ordinary 真阻塞从“索引读写/索引复合返回”继续前移到复合临时实参、非空序列字面量和 `add(seq,val)`。 |
| 主线 | 这轮在 `v3/bootstrap/cheng_v3_seed.c` 新增了 `v3_parse_index_access_expr(...)` 和 `v3_emit_index_access_address(...)`，并把它们接进 `v3_prepare_expr_call_state(...)`、`v3_infer_expr_type(...)`、`v3_codegen_expr_scalar(...)`、`v3_materialize_composite_expr_into_address(...)`，同时给 indexed lvalue 赋值补了写回路径。前台重跑 `cc -std=c11 -O0 -Wall -Wextra -pedantic`、`bootstrap_bridge_v3.sh`、`build_backend_driver_v3.sh`、`build_chain_node_v3.sh` 后，[chain_node.compile.log](/Users/lbcheng/cheng-lang/artifacts/v3_chain_node/chain_node.compile.log) 已经不再出现 `index.accountHeads[pos]`、`index.balances[pos]`、`lt.v3AppendFixed32(buf, index.accountHeads[pos])` 这些索引错误，新的第一批明确失败已经前移成三组：`bytesFromString(...)` 作为 `layout.byteBufAppendBytes(...)` 的复合临时实参、`lt.v3HashInts(\"...\", [assetId, accountId])` 的非空序列字面量、以及 bare `add(seq,val)` 与 `std/seqs::{add,setLen}` 自身 ordinary 语义。 |
| 文件 | `v3/bootstrap/cheng_v3_seed.c` `artifacts/v3_chain_node/chain_node.compile.log` `task_plan.md` `progress.md` `findings.md` |
| 不做 | 不再把索引访问当主阻塞；不回头再改刚收住的 `x8/sret`；不为过当前日志写特判绕开 `bytesFromString/[...]/add` 的真实语义。 |
| 验收 | `cc -std=c11 -O0 -Wall -Wextra -pedantic /Users/lbcheng/cheng-lang/v3/bootstrap/cheng_v3_seed.c -o /tmp/cheng_v3_seed_check` 必须通过；`sh /Users/lbcheng/cheng-lang/v3/tooling/bootstrap_bridge_v3.sh` 和 `sh /Users/lbcheng/cheng-lang/v3/tooling/build_backend_driver_v3.sh` 必须通过；`sh /Users/lbcheng/cheng-lang/v3/tooling/build_chain_node_v3.sh` 的 compile log 必须不再出现 `index.accountHeads[pos]`、`index.balances[pos]`、`lt.v3AppendFixed32(buf, index.accountHeads[pos])`。 |

| 项目 | 内容 |
|---|---|
| 目标 | 让 `panic` 和 `driver_c program runtime` 默认直接出栈，不再只吐异常字符串，也不再依赖手工开环境变量。 |
| 主线 | 这轮在 `src/runtime/native/system_helpers.c`、`system_helpers_selflink_min_runtime.c`、`system_helpers_selflink_shim.c` 把 `CHENG_CRASH_TRACE` 和 `CHENG_PANIC_BACKTRACE` 改成“默认开启，显式写 `0` 才关闭”。同时在 `src/runtime/native/system_helpers_stdio_bridge.c` 给 `DriverCProgFrame` 串上 caller 链，`driver_c_die(...)` 和 `driver_c_abort_with_label_output(...)` 现在会先打印 `[driver_c stack] module/label/source/pc/op`，再继续打 crash trace 和原生 backtrace。前台已验证：`build_panic_trace_v3.sh` 现在不加环境变量也会带源码栈；`ffi_importc_handle_annotated_i32.cheng` 触发 `unsupported callee` 时会直接打印 Cheng 层解释器栈。 |
| 文件 | `src/runtime/native/system_helpers.h` `src/runtime/native/system_helpers.c` `src/runtime/native/system_helpers_selflink_min_runtime.c` `src/runtime/native/system_helpers_selflink_shim.c` `src/runtime/native/system_helpers_stdio_bridge.c` `task_plan.md` `progress.md` `findings.md` |
| 不做 | 不把异常栈继续外包给 `lldb/gdb`；不让 `driver_c` fatal 继续只打一行字符串；不为了 `signal` 验收脚本卡住就回退默认出栈。 |
| 验收 | 直接跑 `sh /Users/lbcheng/cheng-lang/v3/tooling/build_panic_trace_v3.sh` 必须输出 `ordinary_panic_fixture.cheng:2-3`；直接真跑 `/Users/lbcheng/cheng-lang/artifacts/backend_runtime_trace/ffi_importc_handle_annotated_i32` 必须输出 `[driver_c stack] #0 module=fixtures/ffi_importc_handle_annotated_i32 label=main source=/Users/lbcheng/cheng-lang/tests/cheng/backend/fixtures/ffi_importc_handle_annotated_i32.cheng pc=4 op=call`。 |

| 项目 | 内容 |
|---|---|
| 目标 | 把 `signal` 异常也收成“默认打印 Cheng 源码帧 + 原生 backtrace”，并去掉 Darwin 上靠外部 `kill -SEGV` 的不稳定夹层。 |
| 主线 | 这轮先在 `v3/bootstrap/cheng_v3_seed.c` 修了 ordinary `return_call_noarg_i32` 的 `importc` 尾调用目标解析，并修正同文件 `importc fn` 签名扫描被原地截断的问题。随后把 `ordinary_signal_trace_fixture.cheng` 改成普通调用 `let rc = cheng_force_segv(); return rc`，保留 Cheng 栈帧；`build_signal_trace_v3.sh` 也改成前台自触发，不再后台挂起再外部注入信号。最后在 `src/runtime/native/system_helpers.c`、`system_helpers_selflink_min_runtime.c`、`system_helpers_selflink_shim.c` 给 `signal` handler 接上默认 native backtrace。现在 `signal` 输出会同时带 `[cheng-v3] #3 main ... ordinary_signal_trace_fixture.cheng:4-5` 和 `cheng_force_segv/main` 的原生栈。 |
| 文件 | `v3/bootstrap/cheng_v3_seed.c` `v3/src/tests/ordinary_signal_trace_fixture.cheng` `v3/tooling/build_signal_trace_v3.sh` `src/runtime/native/system_helpers.c` `src/runtime/native/system_helpers_selflink_min_runtime.c` `src/runtime/native/system_helpers_selflink_shim.c` `task_plan.md` `progress.md` `findings.md` |
| 不做 | 不再依赖外部 `kill -SEGV` 驱动 fixture；不把 `signal` 堆栈继续限制成“只有 fault pc”；不把 `importc fn` 继续排除在 ordinary 尾调用快路径之外。 |
| 验收 | `cc -std=c11 -O0 -Wall -Wextra -pedantic /Users/lbcheng/cheng-lang/v3/bootstrap/cheng_v3_seed.c -o /tmp/cheng_v3_seed_check` 必须无 warning；`sh /Users/lbcheng/cheng-lang/v3/tooling/bootstrap_bridge_v3.sh`、`sh /Users/lbcheng/cheng-lang/v3/tooling/build_backend_driver_v3.sh`、`sh /Users/lbcheng/cheng-lang/v3/tooling/build_signal_trace_v3.sh`、`sh /Users/lbcheng/cheng-lang/v3/tooling/build_panic_trace_v3.sh` 必须前台通过。 |

| 项目 | 内容 |
|---|---|
| 目标 | 把 generational handle 的标准 `v3` 入口和 ordinary 真验收接进主线，同时修掉 `stage2` 在 `var` 实参地址装参和 `i32 -> i64` 返回归一化上的真实错码。 |
| 主线 | 这轮新增了 [program_support_v3.cheng](/Users/lbcheng/cheng-lang/v3/src/runtime/program_support_v3.cheng)，把 `cheng_ffi_handle_*` 收成 `v3` 可直接 import 的标准面，并在 [system_link_exec.cheng](/Users/lbcheng/cheng-lang/v3/src/backend/system_link_exec.cheng) 把 `runtime/program_support_v3` provider source 接进 source 侧计划。随后加了 [ffi_handle_smoke.cheng](/Users/lbcheng/cheng-lang/v3/src/tests/ffi_handle_smoke.cheng) 和 [build_ffi_handle_v3.sh](/Users/lbcheng/cheng-lang/v3/tooling/build_ffi_handle_v3.sh)，把 generational handle 的真编真跑接进 `README/run_slice_gate/run_v3_host_smokes`。真正挡路的两处 seed 错位也已经收掉：一是 `var` 形参在 external call 装参时先按 `ptr` spill、后按 `i32` reload，导致 64 位地址被截断；二是允许 `i32` 返回值喂给 `i64` 期望时没做 `sxtw`，`-1` 会被抹成 `4294967295`。这两处都已在 [cheng_v3_seed.c](/Users/lbcheng/cheng-lang/v3/bootstrap/cheng_v3_seed.c) 修正。 |
| 文件 | `v3/bootstrap/cheng_v3_seed.c` `v3/src/runtime/program_support_v3.cheng` `v3/src/backend/system_link_exec.cheng` `v3/src/tests/ffi_handle_smoke.cheng` `v3/tooling/build_ffi_handle_v3.sh` `v3/tooling/run_v3_host_smokes.sh` `v3/tooling/run_slice_gate.sh` `v3/tooling/README.md` `task_plan.md` `progress.md` `findings.md` |
| 不做 | 不用测试绕过 `stage2` 的错码；不把 handle 标准面继续留成“只能同文件 `importc fn`”；不把返回值符号位问题伪装成 fixture 特判。 |
| 验收 | `cc -std=c11 -O0 -Wall -Wextra -pedantic /Users/lbcheng/cheng-lang/v3/bootstrap/cheng_v3_seed.c -o /tmp/cheng_v3_seed_check`、`sh /Users/lbcheng/cheng-lang/v3/tooling/bootstrap_bridge_v3.sh`、`sh /Users/lbcheng/cheng-lang/v3/tooling/build_backend_driver_v3.sh`、`sh /Users/lbcheng/cheng-lang/v3/tooling/build_ffi_handle_v3.sh`、`sh /Users/lbcheng/cheng-lang/v3/tooling/build_signal_trace_v3.sh`、`sh /Users/lbcheng/cheng-lang/v3/tooling/build_panic_trace_v3.sh`、`sh /Users/lbcheng/cheng-lang/v3/tooling/build_call_chain_v3.sh` 必须前台通过；`/Users/lbcheng/cheng-lang/artifacts/v3_hostrun/ffi_handle_smoke.single` 必须直接输出 `v3 ffi_handle_smoke ok`。 |

| 项目 | 内容 |
|---|---|
| 目标 | 把 `BFT-SMI` 的真 blocker 从“block1 applied count 失败”继续压缩到 `CheckTx` 预演链本身，同时把直接字符串复合实参的 lowering 缺口一并收掉。 |
| 主线 | 这轮新增了 [bft_apply_to_index_smoke.cheng](/Users/lbcheng/cheng-lang/v3/src/tests/bft_apply_to_index_smoke.cheng) 和 [bft_finalize_summary_smoke.cheng](/Users/lbcheng/cheng-lang/v3/src/tests/bft_finalize_summary_smoke.cheng) 两个最小回归。现在已经能确认三件事：一是 `bft_apply_single_smoke` 继续绿，说明 `V3BftStateMachine -> applyTx` 主路径没坏；二是 `bft_apply_to_index_smoke` 也绿，说明本地 `V3ChainIndex` 上的权威 apply 也没坏；三是把 `CheckTx` 前导加回 `bft_finalize_summary_smoke` 后，失败稳定落在 `summary.appliedCount`，这和 `bft_state_machine_smoke` 的 `block1 applied count` 完全对齐。也就是说，真正污染后续 finalize 的是 `CheckTx` 预演链，不是 `consensus`、不是 mint apply、也不是单纯的 `machine.index` 写回。另外顺手把 [bft_state_machine.cheng](/Users/lbcheng/cheng-lang/v3/src/project/bft_state_machine.cheng)、[consensus.cheng](/Users/lbcheng/cheng-lang/v3/src/chain/consensus.cheng) 里 `bytesFromString(\"...\")` 直接塞进复合实参的形状改成先绑定本地 `Bytes`，已经收掉 seed 的 `direct str-arg scratch overflow`。 |
| 文件 | `v3/src/project/bft_state_machine.cheng` `v3/src/chain/consensus.cheng` `v3/src/tests/bft_apply_single_smoke.cheng` `v3/src/tests/bft_apply_to_index_smoke.cheng` `v3/src/tests/bft_finalize_summary_smoke.cheng` `task_plan.md` `progress.md` `findings.md` |
| 不做 | 不再把锅甩给 `consensus` 或 `mint/transfer` 算法；不继续围着 `machine.index = nextIndex` 这一个点盲改；不把 seed 的字符串复合实参爆栈继续当成偶发现象。 |
| 验收 | `/Users/lbcheng/cheng-lang/artifacts/v3_tmp_bft_apply_single_smoke` 必须输出 `v3 bft_apply_single_smoke ok`；`/Users/lbcheng/cheng-lang/artifacts/v3_tmp_bft_apply_to_index_smoke` 必须输出 `v3 bft_apply_to_index_smoke ok`；`/Users/lbcheng/cheng-lang/artifacts/v3_tmp_bft_finalize_summary_smoke` 目前仍稳定失败在 `v3 bft finalize summary: applied count`；`/Users/lbcheng/cheng-lang/artifacts/v3_tmp_bft_state_machine_smoke` 目前仍稳定失败在 `v3 bft self-test: block1 applied count`；`bash /Users/lbcheng/cheng-lang/v3/tooling/run_v3_host_smokes.sh` 的最新首个 host blocker 目前是 `parser_path_smoke`。 |

| 项目 | 内容 |
|---|---|
| 目标 | 收掉 `lowering_plan_smoke` 在字符串符号拼接上的真崩点，并确认 `v3` 默认 `panic/bounds/signal` 异常堆栈链路没有回退。 |
| 主线 | 这轮把 [cheng_v3_seed.c](/Users/lbcheng/cheng-lang/v3/bootstrap/cheng_v3_seed.c) 里 `str` 的 `+` lowering 从“手拆 `str.data`、直调 `___cheng_str_concat`、再手写回 `len/store_id/flags`”改成了严格桥接：左右两边先各自 materialize 到当前 `call_depth` 的两个 `str` scratch slot，再直接调用 `driver_c_str_concat_bridge` 返回完整 `str`。这样字符串拼接不再依赖裸指针和手填布局，`v3LoweringFunctionSymbolText(ownerModulePath, functionName)` 这类两段以上拼接终于稳定。此前加上的 `@importc` 跳过逻辑继续保留，所以 `std/system::mulCompat` 不再混进 lowering function list。结果是 [lowering_plan_smoke.cheng](/Users/lbcheng/cheng-lang/v3/src/tests/lowering_plan_smoke.cheng)、[lowering_line_local_smoke.cheng](/Users/lbcheng/cheng-lang/v3/src/tests/lowering_line_local_smoke.cheng)、`compiler_pipeline_stub_smoke` 都已前台真编真跑通过。 |
| 文件 | `v3/bootstrap/cheng_v3_seed.c` `v3/src/backend/lowering_plan.cheng` `v3/src/tests/lowering_plan_smoke.cheng` `task_plan.md` `progress.md` `findings.md` |
| 不做 | 不用 C wrapper 把 Cheng `str +` 绕过去；不把 `strDataPtr` 这种特定函数名做硬编码特判；不牺牲默认 `panic/bounds/signal` 堆栈。 |
| 验收 | `/Users/lbcheng/cheng-lang/artifacts/v3_backend_driver/cheng system-link-exec --root /Users/lbcheng/cheng-lang/v3 --in /Users/lbcheng/cheng-lang/v3/src/tests/lowering_plan_smoke.cheng --emit exe --target arm64-apple-darwin --out /tmp/lowering_plan_smoke && /tmp/lowering_plan_smoke`、`/Users/lbcheng/cheng-lang/artifacts/v3_backend_driver/cheng system-link-exec --root /Users/lbcheng/cheng-lang/v3 --in /Users/lbcheng/cheng-lang/v3/src/tests/lowering_line_local_smoke.cheng --emit exe --target arm64-apple-darwin --out /tmp/lowering_line_local_smoke && /tmp/lowering_line_local_smoke`、`/Users/lbcheng/cheng-lang/artifacts/v3_backend_driver/cheng system-link-exec --root /Users/lbcheng/cheng-lang/v3 --in /Users/lbcheng/cheng-lang/v3/src/tests/compiler_pipeline_stub_smoke.cheng --emit exe --target arm64-apple-darwin --out /tmp/compiler_pipeline_stub_smoke && /tmp/compiler_pipeline_stub_smoke`、`sh /Users/lbcheng/cheng-lang/v3/tooling/build_panic_trace_v3.sh`、`sh /Users/lbcheng/cheng-lang/v3/tooling/build_bounds_trace_v3.sh`、`sh /Users/lbcheng/cheng-lang/v3/tooling/build_signal_trace_v3.sh` 必须前台通过。 |

| 项目 | 内容 |
|---|---|
| 目标 | 把 `v3` host 主线从 `bft -> chain_node -> anti_entropy -> lsmr` 一口气收绿，并把真实前沿继续推到独立 `libp2p` ordinary 形态。 |
| 主线 | 这轮先在 [bft_state_machine.cheng](/Users/lbcheng/cheng-lang/v3/src/project/bft_state_machine.cheng) 把 finalize 路径改成“冻结 `batchCount`、从 batch 标量字段显式重建 `V3BftTx`、再走权威 apply”，收掉了 `block1 applied count`。随后在 [pubsub.cheng](/Users/lbcheng/cheng-lang/v3/src/chain/pubsub.cheng) 和 [location_proof.cheng](/Users/lbcheng/cheng-lang/v3/src/chain/location_proof.cheng) 去掉了 `j >= 0 && ...` 这种会在当前 lowering 下越界的短路写法，并把 [location_proof_smoke.cheng](/Users/lbcheng/cheng-lang/v3/src/tests/location_proof_smoke.cheng) 改成 ordinary 稳定可吃的显式 `var + Result` 形态。真正的机器级根因出现在 `chain_node` 二次同步重建：LLDB 已定位到 [lsmr.cheng](/Users/lbcheng/cheng-lang/v3/src/chain/lsmr.cheng) 里对嵌套 `seq` 字段原地 grow 的坏点，所以现在树和森林都改成“局部值构建 -> 一次性赋回”，[chain_node.cheng](/Users/lbcheng/cheng-lang/v3/src/project/chain_node.cheng) 的 `rebuildDerived` 也改成 fresh forest 再整体写回。最后把 [anti_entropy_smoke.cheng](/Users/lbcheng/cheng-lang/v3/src/tests/anti_entropy_smoke.cheng)、[lsmr_locality_storage_smoke.cheng](/Users/lbcheng/cheng-lang/v3/src/tests/lsmr_locality_storage_smoke.cheng)、[lsmr_bagua_prefix_tree_smoke.cheng](/Users/lbcheng/cheng-lang/v3/src/tests/lsmr_bagua_prefix_tree_smoke.cheng) 从 `AddressFromSeed/LocalityHashText/StateCellFromText` 文本壳改成 fixed-layout 二进制输入，host 主线已经完全回到 `v3` 正式口径。 |
| 文件 | `v3/src/project/bft_state_machine.cheng` `v3/src/chain/pubsub.cheng` `v3/src/chain/location_proof.cheng` `v3/src/chain/anti_entropy.cheng` `v3/src/chain/lsmr.cheng` `v3/src/project/chain_node.cheng` `v3/src/tests/location_proof_smoke.cheng` `v3/src/tests/pubsub_smoke.cheng` `v3/src/tests/anti_entropy_smoke.cheng` `v3/src/tests/lsmr_locality_storage_smoke.cheng` `v3/src/tests/lsmr_bagua_prefix_tree_smoke.cheng` `v3/src/tests/chain_node_smoke.cheng` `v3/tooling/run_v3_host_smokes.sh` `task_plan.md` `progress.md` `findings.md` |
| 不做 | 不再让 `LSMR/anti_entropy` host smoke 依赖文本归一化和 `strutils`；不再对复用中的 tree/forest 做嵌套 `seq` 原地 grow；不把 `chain_node` 二次同步的崩溃误判成算法错。 |
| 验收 | `sh /Users/lbcheng/cheng-lang/v3/tooling/run_v3_host_smokes.sh` 必须前台输出 `v3 host smokes: ok`；`/Users/lbcheng/cheng-lang/artifacts/v3_hostrun_targeted/chain_node_smoke`、`/Users/lbcheng/cheng-lang/artifacts/v3_hostrun_targeted/anti_entropy_smoke`、`/Users/lbcheng/cheng-lang/artifacts/v3_hostrun_targeted/lsmr_locality_storage_smoke`、`/Users/lbcheng/cheng-lang/artifacts/v3_hostrun_targeted/lsmr_bagua_prefix_tree_smoke` 都必须直接输出各自 `ok`；当前独立剩余前沿已经前移到 `chain_node_libp2p_smoke` 的运行期 `decodeSnapshot` 语义，不再是 ordinary compile/body shape。 |

| 项目 | 内容 |
|---|---|
| 目标 | 把 `v3` 里 `T[]`、`T[N]` 的默认值和数组字面量初始化收成稳定正式面，至少闭合 `var x: T[]`、`var x: T[N]`、`var x = [..]`、`var x: T[N] = [..]`、`return [..]`、`foo([..])`。 |
| 主线 | 真根不在 list literal materialize，而在 [cheng_v3_seed.c](/Users/lbcheng/cheng-lang/v3/bootstrap/cheng_v3_seed.c) 的 `v3_parse_fixed_array_type(...)`：它会把 `int32[]` 误判成 `int32[0]`，导致 `[1,2,3]` 往 `int32[]` 物化时先走了 fixed-array 分支，再被“长度必须等于 0”拦死。这轮把 fixed-array 解析改成“`[]` 只能是动态序列，`[N]` 必须有非空纯数字长度”，同时保留前面已经补好的 `list literal -> T[N]` 物化路径。新增的 [default_init_literals_smoke.cheng](/Users/lbcheng/cheng-lang/v3/src/tests/default_init_literals_smoke.cheng) 现在同时覆盖默认零初始化、`var zs = [1,2,3]` 推导、`var ws: int32[3] = [4,5,6]`、`sumFixed([1,2,3])` 和 `return [7,8,9]`。另外也把它接进了 [run_v3_host_smokes.sh](/Users/lbcheng/cheng-lang/v3/tooling/run_v3_host_smokes.sh)。 |
| 主线补充 | 继续往前压时，真缺口收敛到 `if/ternary` 的 fixed-array 上下文：`v3_materialize_composite_expr_into_address(...)` 之前先拿无上下文 `v3_infer_expr_type(...)` 做“类型完全相等”预判，会把 `if flag: [..] else: [..]` 里的分支一律看成 `T[]`，从而拦掉 `T[N]` 目标。现在已经把这层预判拿掉，改成直接按目标类型 materialize；同时 `v3_prepare_composite_call_arg_temp(...)` 也收成“目标参数已知是 composite 且实参是 `if/ternary` 时，临时槽直接按目标类型开”，所以 `var x: T[N] = if ...`、`return if ...`、`sumFixed(if ... )`、`cond ? [..] : [..]` 这几条都已经闭合。并行核对结果也确认：`chain_node_libp2p_smoke` 当前 host/stage2/stage3 都是绿的，不是这轮前沿。 |
| 文件 | `v3/bootstrap/cheng_v3_seed.c` `v3/src/tests/default_init_literals_smoke.cheng` `v3/tooling/run_v3_host_smokes.sh` `task_plan.md` `progress.md` `findings.md` |
| 不做 | 不用 `x=[]` 这种显式补丁冒充语言能力；不把 `T[]` 再误解成 `T[0]`；不只验声明初始化而漏掉 fixed-array 的参数和返回路径。 |
| 验收 | `sh /Users/lbcheng/cheng-lang/v3/tooling/bootstrap_bridge_v3.sh` 必须前台通过；`DIAG_CONTEXT=1 /Users/lbcheng/cheng-lang/artifacts/v3_bootstrap/cheng.stage2 system-link-exec --root /Users/lbcheng/cheng-lang --in /Users/lbcheng/cheng-lang/v3/src/tests/default_init_literals_smoke.cheng --emit exe --target arm64-apple-darwin --out /tmp/default_init_literals_smoke.stage2 && /tmp/default_init_literals_smoke.stage2`、`DIAG_CONTEXT=1 /Users/lbcheng/cheng-lang/artifacts/v3_bootstrap/cheng.stage3 system-link-exec --root /Users/lbcheng/cheng-lang --in /Users/lbcheng/cheng-lang/v3/src/tests/default_init_literals_smoke.cheng --emit exe --target arm64-apple-darwin --out /tmp/default_init_literals_smoke.stage3 && /tmp/default_init_literals_smoke.stage3`、`sh /Users/lbcheng/cheng-lang/v3/tooling/build_backend_driver_v3.sh && DIAG_CONTEXT=1 /Users/lbcheng/cheng-lang/artifacts/v3_backend_driver/cheng system-link-exec --root /Users/lbcheng/cheng-lang/v3 --in /Users/lbcheng/cheng-lang/v3/src/tests/default_init_literals_smoke.cheng --emit exe --target arm64-apple-darwin --out /tmp/default_init_literals_smoke.host && /tmp/default_init_literals_smoke.host` 都必须前台通过。 |

| 项目 | 内容 |
|---|---|
| 目标 | 把模块级 `var g: T[N] = [...] / if ... / cond ? ... : ...` 从“只有 `.space`”收成真正执行初始化表达式的主线，并确认它不影响 `chain_node_libp2p`。 |
| 主线 | 真根已经在 [cheng_v3_seed.c](/Users/lbcheng/cheng-lang/v3/bootstrap/cheng_v3_seed.c) 收掉：之前 `v3_append_module_global_storage(...)` 只分配全局静态存储，完全不消费顶层 `var` 的 `has_init/expr`；入口桥也只 `b entry`，没有任何模块 init 阶段。现在新增了模块级 `__v3_module_init` 发射器，先按顶层 `var` 初始化表达式规划 call-depth/string temp/local 临时槽，再复用现成的 `v3_codegen_expr_scalar(...)` 和 `v3_materialize_composite_expr_into_address(...)` 真执行初始化；exe 入口桥也改成“保存 `x30` -> 逐个 `bl module_init` -> 恢复 `x30` -> `b entry`”。总 gate 再往前跑时还暴露出第二个真根：像 `std/crypto/ed25519/ref10` 的 `CurveOrder: Bytes = initCurveOrder()` 这种“只被顶层全局初始化调用”的函数，之前不会进 reachable/lowering，所以 `module_init` 真发码时找不到 callee。现在也已经把“顶层 `var` 初始化表达式”正式接进 reachable seed，`initCurveOrder()`、`initBasePointBytes()` 这类模块 init 专用函数会跟着进 lowering/primary object。[default_init_literals_smoke.cheng](/Users/lbcheng/cheng-lang/v3/src/tests/default_init_literals_smoke.cheng) 现在已经把 `gFixed/gIf/gTernary` 三条模块级 fixed-array 初始化钉死，并且 `stage2/stage3/host` 三路都真编真跑通过；随后 [run_v3_host_smokes.sh](/Users/lbcheng/cheng-lang/v3/tooling/run_v3_host_smokes.sh) 和 [run_v3_stage23_libp2p_smokes.sh](/Users/lbcheng/cheng-lang/v3/tooling/run_v3_stage23_libp2p_smokes.sh) 也都重新前台全绿。 |
| 文件 | `v3/bootstrap/cheng_v3_seed.c` `v3/src/tests/default_init_literals_smoke.cheng` `task_plan.md` `progress.md` `findings.md` |
| 主线补充 | 并行复核后，`Wrap(xs: if ... )` 这条并不是编译器还缺“构造字段 + 复合返回”能力，而是之前探针把记录类型写成了 `type Wrap = { ... }`。当前 seed 的 type-def 收集只吃正式的 `type / Wrap = / field: Type` 缩进记录定义；一旦改成这个写法，`return Wrap(xs: if ... else: ...)` 和 ternary 这条链在 `stage2/stage3/host` 都直接通过。所以这条已经重新接回 [default_init_literals_smoke.cheng](/Users/lbcheng/cheng-lang/v3/src/tests/default_init_literals_smoke.cheng) 做正式回归。 |
| 不做 | 不把 `[ ... ]` 常量特判成静态 `.word` 去分裂语义；不把 `if/ternary` 和模块 init 切成两套路径；不再把花括号记录定义探针误判成 compiler 真缺口。 |
| 验收 | `sh /Users/lbcheng/cheng-lang/v3/tooling/bootstrap_bridge_v3.sh` 必须前台通过；`DIAG_CONTEXT=1 /Users/lbcheng/cheng-lang/artifacts/v3_bootstrap/cheng.stage2 system-link-exec --root /Users/lbcheng/cheng-lang/v3 --in /Users/lbcheng/cheng-lang/v3/src/tests/default_init_literals_smoke.cheng --emit exe --target arm64-apple-darwin --out /tmp/default_init_literals_smoke.stage2 && /tmp/default_init_literals_smoke.stage2`、`DIAG_CONTEXT=1 /Users/lbcheng/cheng-lang/artifacts/v3_bootstrap/cheng.stage3 system-link-exec --root /Users/lbcheng/cheng-lang/v3 --in /Users/lbcheng/cheng-lang/v3/src/tests/default_init_literals_smoke.cheng --emit exe --target arm64-apple-darwin --out /tmp/default_init_literals_smoke.stage3 && /tmp/default_init_literals_smoke.stage3`、`DIAG_CONTEXT=1 /Users/lbcheng/cheng-lang/artifacts/v3_backend_driver/cheng system-link-exec --root /Users/lbcheng/cheng-lang/v3 --in /Users/lbcheng/cheng-lang/v3/src/tests/default_init_literals_smoke.cheng --emit exe --target arm64-apple-darwin --out /tmp/default_init_literals_smoke.host && /tmp/default_init_literals_smoke.host`、`DIAG_CONTEXT=1 /Users/lbcheng/cheng-lang/artifacts/v3_backend_driver/cheng system-link-exec --root /Users/lbcheng/cheng-lang/v3 --in /Users/lbcheng/cheng-lang/v3/src/tests/chain_node_libp2p_smoke.cheng --emit exe --target arm64-apple-darwin --out /tmp/chain_node_libp2p_smoke.host && /tmp/chain_node_libp2p_smoke.host` 必须前台通过。 |
| 项目 | 内容 |
|---|---|
| 目标 | 继续把 `v3` 的 QUIC/TLS ordinary 编译前沿往后推，先收掉 `core/` 导入漏口和 `ptr == nil` 比较 ABI 错配，再重新看 `libp2p_quic_tls_smoke` 的下一层真缺口。 |
| 主线 | 这轮已在 [cheng_v3_seed.c](/Users/lbcheng/cheng-lang/v3/bootstrap/cheng_v3_seed.c) 补上 `v3_module_path_to_source_path(...)` 对 `core/` 的解析，让 `std/option -> core/option` 不再落成假未解析导入；同时把 `v3_codegen_compare_expr_scalar(...)` 的 `ptr == nil` 路径改成按 `ptr` 发射，不再把 `strDataPtr(s)` 错当 `i64` 去比较。实测有一版 fresh `cheng.stage2` 已经把 `libp2p_quic_tls_smoke` 的 `unresolved_import_count` 从 `1` 压到 `0`，并把 `std/system::strToCStringTemp` 的首个真缺口从 `if strDataPtr(s) == nil:` 前移到后面的 `bitandCompat(...) != 0`。 |
| 当前状态 | 后续为了继续前推 `bitandCompat` 比较路径，试探过一版“窄标量统一走 `w` 比较”的 compare lowering；这版会让 fresh `stage1/stage2/stage3` 在 `libp2p_quic_tls_smoke` 上直接崩进 `vfprintf`，所以已经回退，没有保留。当前工作树里保留的是前两条已证实有效的修补：`core/` resolver 和 `ptr == nil` compare ABI。 |
| 新前沿 | 现在真正要隔离的是：为什么同样保留最小修补后，fresh `stage1/2/3` 在 `libp2p_quic_tls_smoke` 上会在诊断打印链里崩到 `libsystem_c::__vfprintf`，而更小的 [libp2p_host_smoke.cheng](/Users/lbcheng/cheng-lang/v3/src/tests/libp2p_host_smoke.cheng) 仍可稳定通过。这说明下一刀该查的是 self-host compiler 的诊断/unsupported 明细打印稳定性，不是再回头怀疑 `core/option` 或 `ptr == nil`。 |
| 文件 | `v3/bootstrap/cheng_v3_seed.c` `artifacts/v3_bootstrap/cheng.stage1` `artifacts/v3_bootstrap/cheng.stage2` `task_plan.md` `progress.md` `findings.md` |
| 验收 | `sh /Users/lbcheng/cheng-lang/v3/tooling/bootstrap_bridge_v3.sh` 必须前台通过；`DIAG_CONTEXT=1 /Users/lbcheng/cheng-lang/artifacts/v3_bootstrap/cheng.stage2 system-link-exec --root /Users/lbcheng/cheng-lang/v3 --in /Users/lbcheng/cheng-lang/v3/src/tests/libp2p_host_smoke.cheng --emit exe --target arm64-apple-darwin --out /tmp/libp2p_host_smoke.stage2.check && /tmp/libp2p_host_smoke.stage2.check` 必须继续通过；`libp2p_quic_tls_smoke` 这条在当前 turn 内已确认不再是 import 假红，而是 self-host compiler 在更深层 unsupported/diagnostic 路径上崩溃。 |

| 项目 | 内容 |
|---|---|
| 目标 | 继续把 `libp2p_quic_tls_smoke` 的 ordinary 前沿从 `x509/native_runtime` 真推到 `bigint/crypto` 算术层。 |
| 主线 | 这轮已经把 [cheng_v3_seed.c](/Users/lbcheng/cheng-lang/v3/bootstrap/cheng_v3_seed.c) 补到三处：1）顶层 `const` 现在会算 `<<`，`x509KeyUsage*` 这类常量正式进 lowering；2）external signature 表补进 `cheng_epoch_time_seconds` 和 `chengSystemEntropyFill`，把现成 runtime bridge 接回 ordinary；3）之前的 `const str` / 最小整型 `const expr` 继续保留。源码侧则把 [x509.cheng](/Users/lbcheng/cheng-lang/src/std/tls/x509.cheng)、[minasn1.cheng](/Users/lbcheng/cheng-lang/src/std/crypto/minasn1.cheng)、[rand.cheng](/Users/lbcheng/cheng-lang/src/std/crypto/rand.cheng)、[frame_model.cheng](/Users/lbcheng/cheng-lang/v3/src/quic/core/frame_model.cheng)、[packet_model.cheng](/Users/lbcheng/cheng-lang/v3/src/quic/core/packet_model.cheng)、[connection_impl.cheng](/Users/lbcheng/cheng-lang/v3/src/quic/core/connection_impl.cheng)、[datapath_runtime.cheng](/Users/lbcheng/cheng-lang/v3/src/quic/platform/datapath_runtime.cheng)、[native_runtime.cheng](/Users/lbcheng/cheng-lang/v3/src/quic/native_runtime.cheng) 全部压成 ordinary 当前稳定可吃的形状。 |
| 结果 | `x509`、`packet_model`、`datapath_runtime`、`connection_impl`、`minasn1`、`rand` 这一整圈都已经让开；`libp2p_quic_tls_smoke` 的首个 unsupported 已前移到 [bigint.cheng](/Users/lbcheng/cheng-lang/src/std/crypto/bigint.cheng) 的 `bigWordToU64`，unsupported 总数从 `53` 压到 `42`。 |
| 文件 | `v3/bootstrap/cheng_v3_seed.c` `src/std/tls/x509.cheng` `src/std/crypto/minasn1.cheng` `src/std/crypto/rand.cheng` `v3/src/quic/core/frame_model.cheng` `v3/src/quic/core/packet_model.cheng` `v3/src/quic/core/connection_impl.cheng` `v3/src/quic/platform/datapath_runtime.cheng` `v3/src/quic/native_runtime.cheng` `task_plan.md` `progress.md` `findings.md` |
| 不做 | 不回退外链，不删 Cheng 源码主线，不用 host-only 假闭环掩盖 ordinary 真缺口。 |
| 验收 | `sh /Users/lbcheng/cheng-lang/v3/tooling/build_backend_driver_v3.sh` 必须前台通过；`DIAG_CONTEXT=1 /Users/lbcheng/cheng-lang/artifacts/v3_backend_driver/cheng system-link-exec --root /Users/lbcheng/cheng-lang/v3 --in /Users/lbcheng/cheng-lang/v3/src/tests/libp2p_quic_tls_smoke.cheng --emit exe --target arm64-apple-darwin --out /tmp/libp2p_quic_tls_smoke.host` 现在稳定停在 `std/crypto/bigint::bigWordToU64`，不再回到 `x509/packet_model/datapath/runtime/rand`。 |
| 项目 | 内容 |
|---|---|
| 目标 | 继续把 `libp2p_quic_tls_smoke` 的真 blocker 从 `std/os/std/times` 推进到 QUIC/TLS 主体，并把 seed prepare 面补到能吃 builtin cast 与 `var` 复合实参。 |
| `std/os` ordinary 形状 | 已完成 | [src/std/os.cheng](/Users/lbcheng/cheng-lang/src/std/os.cheng) 已把 `openImplRead/Write/ReadWrite` 收成 `pathText + c_fopen(..., "rb"/"wb"/"rb+")`，去掉会卡 seed 的 `modeSource: str` 复合绑定；`udpRecvFromFdAddrEx(...)` 也已改成显式 `payloadLen/addrCap/payloadPtr/addrPtr` 路径。`libp2p_quic_tls_smoke` 的首个 blocker 已不再落在 `std/os`。 |
| `std/times` 秒级真接口 | 已完成 | [src/std/times.cheng](/Users/lbcheng/cheng-lang/src/std/times.cheng) 新增 `@importc("cheng_epoch_time_seconds")` 并把 `epochTimeSeconds/epochTimeNs/getTime` 改走整数秒主线；对应 runtime 已补到 [system_helpers.c](/Users/lbcheng/cheng-lang/src/runtime/native/system_helpers.c)、[system_helpers.h](/Users/lbcheng/cheng-lang/src/runtime/native/system_helpers.h)、[system_helpers_epoch_time_bridge.c](/Users/lbcheng/cheng-lang/src/runtime/native/system_helpers_epoch_time_bridge.c)、[system_helpers_stdio_bridge.c](/Users/lbcheng/cheng-lang/src/runtime/native/system_helpers_stdio_bridge.c)、[system_helpers_backend.cheng](/Users/lbcheng/cheng-lang/src/std/system_helpers_backend.cheng)、[system_helpers_backend_nolibc_linux_aarch64.cheng](/Users/lbcheng/cheng-lang/src/std/system_helpers_backend_nolibc_linux_aarch64.cheng)。现在 `libp2p_quic_tls_smoke` 的第一处 unsupported 已不再是 `std/times::epochTimeSeconds`。 |
| seed prepare 补丁 | 已完成 | [cheng_v3_seed.c](/Users/lbcheng/cheng-lang/v3/bootstrap/cheng_v3_seed.c) 已补两条：1）builtin cast 在 `v3_prepare_expr_call_state(...)` 中不再误按普通调用准备；2）prepare 阶段遇到 `param_is_var[i]` 时不再错误给复合实参建 temp。 |
| 新前沿 | 进行中 | 现在 `libp2p_quic_tls_smoke` 的第一处真 blocker 已前移到 [native_runtime.cheng](/Users/lbcheng/cheng-lang/v3/src/quic/native_runtime.cheng) 的 `msquicNativeSendClientHello / msquicNativeSendServerFlight`。日志显示卡点已经是 `msquicTls13BuildClientHello(...)` 与 `msquicTls13BuildEncryptedExtensions(...)` 的 prepare expr state，不再是 `std/os/std/times` 或 import resolver。 |
| 验收 | `sh /Users/lbcheng/cheng-lang/v3/tooling/build_backend_driver_v3.sh` 持续前台通过；`cc -std=c11 -O2 -Wall -Wextra -pedantic -c /Users/lbcheng/cheng-lang/src/runtime/native/system_helpers.c -o /tmp/system_helpers.after_epoch_seconds.o` 前台通过；`DIAG_CONTEXT=1 /Users/lbcheng/cheng-lang/artifacts/v3_backend_driver/cheng system-link-exec --root /Users/lbcheng/cheng-lang/v3 --in /Users/lbcheng/cheng-lang/v3/src/tests/libp2p_quic_tls_smoke.cheng --emit exe --target arm64-apple-darwin --out /tmp/libp2p_quic_tls_smoke.host` 的首个 unsupported 已前移到 `cheng/v3/quic/native_runtime::msquicNativeSendClientHello`。 |
| 项目 | 内容 |
|---|---|
| 目标 | 继续把 `libp2p_quic_tls_smoke` 的 ordinary 前沿从 QUIC native 入口层推进到真正的 `tls/x509` 和 crypto 算术层。 |
| `const str` 主线 | 已完成 | [cheng_v3_seed.c](/Users/lbcheng/cheng-lang/v3/bootstrap/cheng_v3_seed.c) 现在会收集顶层字符串常量，并把它们接进 `v3_find_expr_top_level_const(...)`、`v3_infer_expr_type(...)` 和 `v3_materialize_composite_expr_into_address(...)`。`msquicNativeAlpn`、`msquicLocalhostCertDerHex` 这类顶层 `str` 常量已经能真走复合实参链。 |
| 整型 `const expr` 主线 | 已完成 | 同文件已补最小整型顶层常量表达式收集，当前至少支持 `const a = b + 1 / b - 1`。`msquicNativePipeToken = msquicNativePipeBase + 1` 已不再卡 ordinary。 |
| QUIC native 源码压平 | 已完成 | [native_runtime.cheng](/Users/lbcheng/cheng-lang/v3/src/quic/native_runtime.cheng) 的 `msquicNativeSendClientHello / msquicNativeSendServerFlight / msquicNativeSendClientFinished` 已把 `var handshake` 全局成员链、`serverName` 成员投影和 `msquicNativeAlpn` 顶层常量先落本地，再按局部变量过调用；所有会改握手状态的调用后也已立即写回 `msquicNativeSession.*.handshake`。 |
| 前沿继续前移 | 已完成 | 最新 host 定向编译里，`native_runtime` 入口层已经完全让开；首个 unsupported 已前移到 [x509.cheng](/Users/lbcheng/cheng-lang/src/std/tls/x509.cheng) 的 `x509VerifyLeafConstraints`，同时 unsupported 数量已从 `64` 压到 `53`。 |
| 当前真 blocker | 进行中 | 现在最前面的真缺口是 `std/tls/x509` 条件表达式、[packet_model.cheng](/Users/lbcheng/cheng-lang/v3/src/quic/core/packet_model.cheng) 的复合构造与复合 stmt-call、[connection_impl.cheng](/Users/lbcheng/cheng-lang/v3/src/quic/core/connection_impl.cheng) 的标量乘法和 [datapath_runtime.cheng](/Users/lbcheng/cheng-lang/v3/src/quic/platform/datapath_runtime.cheng) 的“忽略 composite 返回值”发码。 |
| 项目 | 内容 |
|---|---|
| 目标 | 继续收 `QUIC/TLS` 在 `stage2` 的大对象压力，先从握手态里拿掉不会跨消息存活的 `peerChain: X509CertList`。 |
| 这轮已做 | [handshake13.cheng](/Users/lbcheng/cheng-lang/v3/src/quic/tls/handshake13.cheng) 已把 `MsQuicTls13PeerVerifyState.peerChain` 删除，`msquicTls13ParseCertificate(...)` 改成局部 `peerChain: X509CertList` 只服务当前证书消息的 `x509VerifyChain/x509VerifyRevocation`。这样不改 caller，也不改 TLS 语义。 |
| 下一刀 | `trustRoots: X509CertList` 仍常驻在握手态里，它是剩下更值的一块；但要真收掉，必须把 `policy/native_runtime -> handshake13` 的 `TrustRoots` 入口改成原始 DER 或 `Into` 路径，这一刀不该偷偷扩面。 |
| 验收 | `rg -n "peerChain" /Users/lbcheng/cheng-lang/v3/src/quic/tls/handshake13.cheng` 只应剩局部变量与局部调用；`/Users/lbcheng/cheng-lang/artifacts/v3_backend_driver/cheng system-link-exec --root /Users/lbcheng/cheng-lang/v3 --in /Users/lbcheng/cheng-lang/v3/src/tests/quic_transport_loopback_smoke.cheng --emit exe --target arm64-apple-darwin --out /tmp/quic_transport_loopback_smoke.host.peerchain_local` 必须前台通过；`/Users/lbcheng/cheng-lang/artifacts/v3_bootstrap/cheng.stage2 system-link-exec --root /Users/lbcheng/cheng-lang/v3 --in /Users/lbcheng/cheng-lang/v3/src/tests/quic_transport_loopback_smoke.cheng --emit exe --target arm64-apple-darwin --out /tmp/quic_transport_loopback_smoke.stage2.peerchain_local` 继续以前沿形式失败即可，且不回退到旧的 `handshake13.peerChain` 形状。 |
| 项目 | 内容 |
|---|---|
| 目标 | 继续并行推进 `quic_transport_loopback_smoke`，一边把 `native_runtime` 的 stage2 形状继续压平，一边把 host 运行时失败钉死到 `ClientHello key_share` 真链路。 |
| 这轮已做 | [native_runtime.cheng](/Users/lbcheng/cheng-lang/v3/src/quic/native_runtime.cheng) 已删掉临时诊断文件对应的测试壳；`client/server packet number` 和 `largest recv` 已从 `MsQuicNativeSession` 复合字段写回拆成顶层标量缓存；`ackFrame.value = ...` 已收成 [msquicFrameAckNumber(...)](/Users/lbcheng/cheng-lang/v3/src/quic/core/frame_model.cheng)；`DialSetupSession` 也已改走 [handshake13.cheng](/Users/lbcheng/cheng-lang/v3/src/quic/tls/handshake13.cheng) 的 client 专用入口。与此同时，host fresh 编 [quic_transport_loopback_smoke.cheng](/Users/lbcheng/cheng-lang/v3/src/tests/quic_transport_loopback_smoke.cheng) 继续通过，并且运行时错误已经从“消息里不含 msquic”收束成 `v3 libp2p quic open: dial: msquic native: server client key share missing after initial`。 |
| 当前真 blocker | [handshake13.cheng](/Users/lbcheng/cheng-lang/v3/src/quic/tls/handshake13.cheng) 的服务端 `ClientHello` 解析链还没有把 `client key share` 真落进握手状态；并行只读核对已经确认主嫌疑不在 [packet_model.cheng](/Users/lbcheng/cheng-lang/v3/src/quic/core/packet_model.cheng) 或 [crypto_stream.cheng](/Users/lbcheng/cheng-lang/v3/src/quic/core/crypto_stream.cheng)，而是在 `msquicTls13ParseClientHello / msquicTls13ParseKeyShareExtension / msquicTls13ValidateExtensions` 这条 parser 主线。 |
| stage2 状态 | `cheng.stage2` 这轮没有回退到旧的 `packet_model/connection_impl` 入口，但 [native_runtime.cheng](/Users/lbcheng/cheng-lang/v3/src/quic/native_runtime.cheng) 仍剩几类真形状没收完：`TlsRole* / QuicPacket*` 这类 enum 常量装参、`msquicNativeMultiAddressBindText(...)` 的字符串构造、以及 `handshake13` 模块本身的大对象 layout。下一刀应该直接把 `handshake13` 的 client/server 专用 reset 和 key_share 校验压平成更朴素的 Cheng 形状。 |
| 验收 | `/Users/lbcheng/cheng-lang/artifacts/v3_backend_driver/cheng system-link-exec --root /Users/lbcheng/cheng-lang/v3 --in /Users/lbcheng/cheng-lang/v3/src/tests/quic_transport_loopback_smoke.cheng --emit exe --target arm64-apple-darwin --out /tmp/quic_transport_loopback_smoke.host.native_pass5` 前台通过；`/tmp/quic_transport_loopback_smoke.host.native_pass5` 当前稳定报 `msquic native: server client key share missing after initial`；`DIAG_CONTEXT=1 /Users/lbcheng/cheng-lang/artifacts/v3_bootstrap/cheng.stage2 system-link-exec --root /Users/lbcheng/cheng-lang/v3 --in /Users/lbcheng/cheng-lang/v3/src/tests/quic_transport_loopback_smoke.cheng --emit exe --target arm64-apple-darwin --out /tmp/quic_transport_loopback_smoke.stage2.native_pass5` 的 native 前沿继续停在 `native_runtime/handshake13`，不回退到旧的 `peerChain` 或 `connection_impl`。 |
| 项目 | 内容 |
|---|---|
| 目标 | 继续并行推进 `QUIC/TLS` 运行时主线，把 host 的 `server client key share missing after initial` 真收掉，并把 `stage2/stage3` libp2p 总 gate 一起保绿。 |
| 开始时间 | `2026-04-12 06:04 +0800` |
| 主线修补 | [native_runtime.cheng](/Users/lbcheng/cheng-lang/v3/src/quic/native_runtime.cheng) 这轮已经做了四件硬修补：1）`CRYPTO/app recv` 全局 stream 统一改成“本地副本修改后写回”；2）关键握手链改成只传 `offset + data`，不再让整帧复合对象卡主路径；3）服务端首包初始化前移到 `header decode` 之后、`payload/frame decode` 之前；4）`ClientHello/ServerFlight/ClientFinished` 这些发送链直接在本地编码 payload 再发 wire，不再把整包复合对象往下传。 |
| 切片探针 | 新增 [tls_client_hello_parse_smoke.cheng](/Users/lbcheng/cheng-lang/v3/src/tests/tls_client_hello_parse_smoke.cheng)、[tls_initial_packet_roundtrip_smoke.cheng](/Users/lbcheng/cheng-lang/v3/src/tests/tls_initial_packet_roundtrip_smoke.cheng)、[udp_datapath_wire_roundtrip_smoke.cheng](/Users/lbcheng/cheng-lang/v3/src/tests/udp_datapath_wire_roundtrip_smoke.cheng)、[native_initial_crypto_frame_smoke.cheng](/Users/lbcheng/cheng-lang/v3/src/tests/native_initial_crypto_frame_smoke.cheng)、[native_client_hello_wire_smoke.cheng](/Users/lbcheng/cheng-lang/v3/src/tests/native_client_hello_wire_smoke.cheng)。这些探针已经把主根收口成 `processPacket` 里的服务端初始化时序，而不是 `TLS parser / packet encode / UDP wire`。 |
| 当前结果 | host 运行时主线已重新闭合：[quic_transport_loopback_smoke.cheng](/Users/lbcheng/cheng-lang/v3/src/tests/quic_transport_loopback_smoke.cheng)、[libp2p_quic_tls_smoke.cheng](/Users/lbcheng/cheng-lang/v3/src/tests/libp2p_quic_tls_smoke.cheng)、[content_quic_smoke.cheng](/Users/lbcheng/cheng-lang/v3/src/tests/content_quic_smoke.cheng)、[libp2p_protocols_smoke.cheng](/Users/lbcheng/cheng-lang/v3/src/tests/libp2p_protocols_smoke.cheng) 现已前台通过；`sh /Users/lbcheng/cheng-lang/v3/tooling/run_v3_stage23_libp2p_smokes.sh` 也已重新输出 `v3 stage2/stage3 libp2p: ok`。 |
| runner 口径 | `run_v3_host_smokes.sh` 在并发状态下曾短暂把 [content_quic_smoke.cheng](/Users/lbcheng/cheng-lang/v3/src/tests/content_quic_smoke.cheng) 误炸到 `peerstore::v3Libp2pPeerstoreUpsert` compile blocker；单独复跑 `content_quic_smoke` 为绿，随后用 `BACKEND_INCREMENTAL=0 BACKEND_MULTI_MODULE_CACHE=0 sh /Users/lbcheng/cheng-lang/v3/tooling/run_v3_host_smokes.sh` 也已经重新收成 `v3 host smokes: ok`。这条应视为并发/缓存噪声，不是当前活动真根。 |
| 验收 | `DIAG_CONTEXT=1 /Users/lbcheng/cheng-lang/artifacts/v3_backend_driver/cheng system-link-exec --root /Users/lbcheng/cheng-lang/v3 --in /Users/lbcheng/cheng-lang/v3/src/tests/quic_transport_loopback_smoke.cheng --emit exe --target arm64-apple-darwin --out /tmp/quic_transport_loopback_smoke.current && /tmp/quic_transport_loopback_smoke.current` 输出 `v3 quic_transport_loopback_smoke ok`；`.../libp2p_quic_tls_smoke.current` 输出 `v3 libp2p_quic_tls_smoke ok`；`.../content_quic_smoke.recheck` 输出 `v3 content_quic_smoke ok`；`BACKEND_INCREMENTAL=0 BACKEND_MULTI_MODULE_CACHE=0 sh /Users/lbcheng/cheng-lang/v3/tooling/run_v3_host_smokes.sh` 输出 `v3 host smokes: ok`；`sh /Users/lbcheng/cheng-lang/v3/tooling/run_v3_stage23_libp2p_smokes.sh` 输出 `v3 stage2/stage3 libp2p: ok`。 |
| 开始时间 | `2026-04-12 12:10 +0800` | 继续并行推进 host 总 gate 尾部，先收掉 `content_stub_smoke` 在长链 runner 里偶发的 compile blocker。 |
| 真根 | 已完成 | `content_stub_smoke` 单独编译能过，但串在 `content_quic_smoke -> content_stub_smoke` 后会稳定炸在 [native_runtime.cheng](/Users/lbcheng/cheng-lang/v3/src/quic/native_runtime.cheng) 的 [msquicNativeMultiAddressBindText(...)](/Users/lbcheng/cheng-lang/v3/src/quic/native_runtime.cheng:639)。这条 `MultiAddress -> str -> str` 复合返回链在长链 ordinary 编译里不稳定。 |
| 修补 | 已完成 | [native_runtime.cheng](/Users/lbcheng/cheng-lang/v3/src/quic/native_runtime.cheng) 已把 `msquicNativeMultiAddressBindText(...)` 改成就地读取 `addr.raw/addr.data`，不再套 `quicMultiAddressBindText(...)` 的复合返回链。 |
| 最小复现 | 已完成 | `/tmp/repro_content_pair.sh` 串行编译 `content_quic_smoke -> content_stub_smoke`，修补前稳定复现，修补后两条都通过。 |
| 总验收 | 已完成 | `sh /Users/lbcheng/cheng-lang/v3/tooling/run_v3_stage23_libp2p_smokes.sh` 已再次输出 `v3 stage2/stage3 libp2p: ok`；`BACKEND_INCREMENTAL=0 BACKEND_MULTI_MODULE_CACHE=0 sh /Users/lbcheng/cheng-lang/v3/tooling/run_v3_host_smokes.sh` 已再次输出 `v3 host smokes: ok`，其中新增的 `native_initial_crypto_frame_smoke`、`native_client_hello_wire_smoke`、`quic_transport_loopback_smoke`、`libp2p_quic_tls_smoke`、`content_quic_smoke`、`content_stub_smoke` 都已顺序通过。 |
| 余项 | 已记录 | 额外单独试跑 [udp_bind_bindtext_smoke.cheng](/Users/lbcheng/cheng-lang/v3/src/tests/udp_bind_bindtext_smoke.cheng) 仍卡在该 smoke 自身的 ordinary `stmt_let` 缺口，当前不属于这轮 QUIC/content 主线回归。 |
| 开始时间 | `2026-04-12 12:43 +0800` | 继续并行推进内容面和 QUIC 宿主的解耦，先让 [content_runtime.cheng](/Users/lbcheng/cheng-lang/v3/src/chain/content_runtime.cheng) 不再直接依赖 [host_quic.cheng](/Users/lbcheng/cheng-lang/v3/src/libp2p/host/host_quic.cheng)。 |
| 真根 | 已完成 | `content_runtime` 对 `host_quic` 的直接依赖只收敛在 metadata 回拉那一个 `syncRequest(...)` 分支；但因为它在核心模块顶层直接 import `host_quic`，连只走 TCP 的 [content_runtime_smoke.cheng](/Users/lbcheng/cheng-lang/v3/src/tests/content_runtime_smoke.cheng) 也会把 `quic_transport/native_runtime` 整串拖进编译面。 |
| 修补 | 已完成 | 新增 [content_runtime_host.cheng](/Users/lbcheng/cheng-lang/v3/src/chain/content_runtime_host.cheng)，把 `SelectSyncRequest + FetchAdvert/Manifest/RecoveryPlan` 全部搬到宿主适配层；[content_runtime.cheng](/Users/lbcheng/cheng-lang/v3/src/chain/content_runtime.cheng) 删掉 `host_quic` 直接 import 和三条 fetch 入口；[content_stub_smoke.cheng](/Users/lbcheng/cheng-lang/v3/src/tests/content_stub_smoke.cheng) 改成显式走 `runtime_host`。 |
| 编译面验收 | 已完成 | `DIAG_CONTEXT=1 ... content_runtime_smoke ...` 的 `source_closure_paths` 已降到 `34` 条，而且不再出现 `host_quic/quic_transport/native_runtime`；`DIAG_CONTEXT=1 ... content_stub_smoke ...` 仍保留 QUIC 依赖，因为它明确走宿主适配层。 |
| 运行验收 | 已完成 | `content_runtime_smoke`、`content_stub_smoke`、`content_quic_smoke` 全部重新真编真跑通过；`BACKEND_INCREMENTAL=0 BACKEND_MULTI_MODULE_CACHE=0 sh /Users/lbcheng/cheng-lang/v3/tooling/run_v3_host_smokes.sh` 也已再次输出 `v3 host smokes: ok`。 |
| 开始时间 | `2026-04-12 06:43 +0800` | 继续并行推进 host 总 gate 复核，先确认 `content_quic_smoke` 在 runner 里那条 `handshake13` compile fail 现在是否仍可复现。 |
| 复核结果 | 已完成 | 单独 fresh 编 [content_quic_smoke.cheng](/Users/lbcheng/cheng-lang/v3/src/tests/content_quic_smoke.cheng)、最短串行链 `quic_transport_loopback_smoke -> libp2p_quic_tls_smoke -> content_quic_smoke`、以及 `BACKEND_INCREMENTAL=0 BACKEND_MULTI_MODULE_CACHE=0 sh /Users/lbcheng/cheng-lang/v3/tooling/run_v3_host_smokes.sh` 三层都重新通过，`content_quic_smoke` 没再复现 `handshake13::msquicTls13ResetClientDefault` compile fail。 |
| 当前口径 | 已完成 | [handshake13.cheng](/Users/lbcheng/cheng-lang/v3/src/quic/tls/handshake13.cheng) 里“全局大握手对象整块 init + 嵌套 `var` helper”这条 reset 链仍是已知 ABI 脆点，但这轮不是活动 blocker；总 gate 已绿时不应为潜在脆点主动改动源码。 |
| 验收 | 已完成 | `BACKEND_INCREMENTAL=0 BACKEND_MULTI_MODULE_CACHE=0 DIAG_CONTEXT=1 /Users/lbcheng/cheng-lang/artifacts/v3_backend_driver/cheng system-link-exec --root /Users/lbcheng/cheng-lang/v3 --in /Users/lbcheng/cheng-lang/v3/src/tests/content_quic_smoke.cheng --emit exe --target arm64-apple-darwin --out /tmp/content_quic_smoke.repro` 前台通过；顺序编 `quic_transport_loopback_smoke -> libp2p_quic_tls_smoke -> content_quic_smoke` 前台通过；`BACKEND_INCREMENTAL=0 BACKEND_MULTI_MODULE_CACHE=0 sh /Users/lbcheng/cheng-lang/v3/tooling/run_v3_host_smokes.sh` 最终输出 `v3 host smokes: ok`。 |
| 开始时间 | `2026-04-12 07:51 +0800` | 继续并行推进内容面的宿主收口，把 QUIC shard pull 从直连模块收回统一宿主适配层。 |
| 修补 | 已完成 | [content_runtime_host_quic.cheng](/Users/lbcheng/cheng-lang/v3/src/chain/content_runtime_host_quic.cheng) 现在正式承接 `v3ContentRuntimeRequestShardPull(...)`，按 peer transport 在 `TCP host adapter` 和 `QUIC loopback` 之间分发；[content_runtime_quic.cheng](/Users/lbcheng/cheng-lang/v3/src/chain/content_runtime_quic.cheng) 收成兼容薄壳，只转发到新适配层；[content_quic_smoke.cheng](/Users/lbcheng/cheng-lang/v3/src/tests/content_quic_smoke.cheng) 已改成直接走宿主适配层；[content_runtime_host.cheng](/Users/lbcheng/cheng-lang/v3/src/chain/content_runtime_host.cheng) 的错误文案也已同步收正为 `content_runtime_host_quic`。 |
| 结果 | 已完成 | `content_quic_smoke` 不再直接依赖 [content_runtime_quic.cheng](/Users/lbcheng/cheng-lang/v3/src/chain/content_runtime_quic.cheng) 的专用入口，QUIC 内容拉取和 metadata 回拉现在都统一挂在宿主适配层边界上。旧的 `content_runtime_quic` 只保留兼容入口，不再承担真正分层职责。 |
| 验收 | 已完成 | `DIAG_CONTEXT=1 ... content_runtime_smoke ...`、`DIAG_CONTEXT=1 ... content_stub_smoke ...`、`DIAG_CONTEXT=1 ... content_quic_smoke ...` 全部真编真跑通过；`sh /Users/lbcheng/cheng-lang/v3/tooling/run_v3_stage23_libp2p_smokes.sh` 输出 `v3 stage2/stage3 libp2p: ok`；`BACKEND_INCREMENTAL=0 BACKEND_MULTI_MODULE_CACHE=0 sh /Users/lbcheng/cheng-lang/v3/tooling/run_v3_host_smokes.sh` 输出 `v3 host smokes: ok`。 |
| 开始时间 | `2026-04-12 08:02 +0800` | 继续并行推进内容宿主层压薄，尝试把 [content_runtime_host_quic.cheng](/Users/lbcheng/cheng-lang/v3/src/chain/content_runtime_host_quic.cheng) 收成“只做 QUIC shard pull 的薄层”。 |
| 结果 | 已完成 | 这刀已验证失败并回滚。把 `content_runtime_host_quic` 收成纯 QUIC 薄层后，默认 targeted compile 仍绿，但 `BACKEND_INCREMENTAL=0 BACKEND_MULTI_MODULE_CACHE=0` 下的 [content_quic_smoke.cheng](/Users/lbcheng/cheng-lang/v3/src/tests/content_quic_smoke.cheng) 会稳定掉到 `std/tls/x509::x509ParsePssParams / x509ParseExtensions` 的 ordinary unsupported，`run_v3_host_smokes.sh` 也会在 `content_quic_smoke` 编译阶段失败。 |
| 当前口径 | 已完成 | 当前 seed/no-cache ordinary 编译面对 QUIC 内容链仍依赖 [content_runtime_host_quic.cheng](/Users/lbcheng/cheng-lang/v3/src/chain/content_runtime_host_quic.cheng) 保留那层 `host_quic + runtime_host + fetch/fallback` 形状；这不是理想分层，但它是当前唯一经过 no-cache 总 gate 验证的稳定形状。 |
| 验收 | 已完成 | 薄化版本下，`BACKEND_INCREMENTAL=0 BACKEND_MULTI_MODULE_CACHE=0 DIAG_CONTEXT=1 ... content_quic_smoke ...` 先后稳定报 `std/tls/x509::x509ParsePssParams` 和 `std/tls/x509::x509ParseExtensions` unsupported；回滚后同口径 `.../content_quic_smoke.rollback_nocache` 重新前台输出 `v3 content_quic_smoke ok`；随后 `sh /Users/lbcheng/cheng-lang/v3/tooling/run_v3_stage23_libp2p_smokes.sh` 再次输出 `v3 stage2/stage3 libp2p: ok`，`BACKEND_INCREMENTAL=0 BACKEND_MULTI_MODULE_CACHE=0 sh /Users/lbcheng/cheng-lang/v3/tooling/run_v3_host_smokes.sh` 再次输出 `v3 host smokes: ok`。 |
| 开始时间 | `2026-04-12 09:06 +0800` | 继续并行推进 `stage2 QUIC/TLS` 前沿，正面收 [x509.cheng](/Users/lbcheng/cheng-lang/src/std/tls/x509.cheng) 的 ordinary unsupported，不再从内容宿主层硬抠。 |
| 修补 | 已完成 | [x509.cheng](/Users/lbcheng/cheng-lang/src/std/tls/x509.cheng) 这轮已经把 `ASN.1 tag` 热路径全部收成本地标量常量，`x509ParseMgf1Hash(...)` 改成直接返回 `hashCode(int32)`，`x509ParsePssParams(...)` / `x509ParseSignatureAlgorithm(...)` / `x509ParseExtensions(...)` / `x509VerifyOcspResponse(...)` 都撤掉了会卡 ordinary 的 `enum` 本地绑定、`Result[record]` 连点和 `cert.dnsNamesDer = bytesConcat(cert.dnsNamesDer, ...)` 这类边读边写形状。 |
| 结果 | 已完成 | `cheng.stage2` 定向编 [quic_transport_loopback_smoke.cheng](/Users/lbcheng/cheng-lang/v3/src/tests/quic_transport_loopback_smoke.cheng) 时，unsupported 首屏已不再出现任何 `std/tls/x509::*`；最前沿正式前移到 `std/crypto/rsa::rsaBigPrivateKeyFromPkcs1Value / rsaBigPrivateKeyFromPkcs8Value / rsaBigPrivateKeyFromBytes / rsaBigSignEm / rsaBigSignPss`，后面接着才是 `policy/handshake13`。 |
| 验收 | 已完成 | `DIAG_CONTEXT=1 /Users/lbcheng/cheng-lang/artifacts/v3_bootstrap/cheng.stage2 system-link-exec --root /Users/lbcheng/cheng-lang/v3 --in /Users/lbcheng/cheng-lang/v3/src/tests/quic_transport_loopback_smoke.cheng --emit exe --target arm64-apple-darwin --out /tmp/quic_transport_loopback_smoke.stage2.x509_after4` 的首批 unsupported 已不含 `x509`；`DIAG_CONTEXT=1 /Users/lbcheng/cheng-lang/artifacts/v3_backend_driver/cheng system-link-exec --root /Users/lbcheng/cheng-lang/v3 --in /Users/lbcheng/cheng-lang/v3/src/tests/quic_transport_loopback_smoke.cheng --emit exe --target arm64-apple-darwin --out /tmp/quic_transport_loopback_smoke.host.x509_after && /tmp/quic_transport_loopback_smoke.host.x509_after` 输出 `v3 quic_transport_loopback_smoke ok`；`.../libp2p_quic_tls_smoke.host.x509_after && /tmp/libp2p_quic_tls_smoke.host.x509_after` 输出 `v3 libp2p_quic_tls_smoke ok`。 |
| 开始时间 | `2026-04-12 09:22 +0800` | 继续并行推进 `stage2 QUIC/TLS`，先正面核对 `rsa/rand/handshake13` 的最短句型，不再盲改 `x509/native_runtime`。 |
| 已完成 | [handshake13.cheng](/Users/lbcheng/cheng-lang/v3/src/quic/tls/handshake13.cheng) 已把 `client/server reset`、`clear/add trust root` 这些 owner 包装层改成更直接的 Cheng 形状，去掉对旧 `init/set/add` 包装链的热路径依赖；[rand.cheng](/Users/lbcheng/cheng-lang/src/std/crypto/rand.cheng) 的外部熵调用和 `openRead` 绑定也已 fresh 钉住。 |
| 验收 | fresh host 编 `/tmp/quic_transport_loopback_smoke.host.parallel3` 继续 `EXIT:0`，说明这轮没有把 host 主线打回去；fresh `stage2` 编 `/tmp/quic_transport_loopback_smoke.stage2.parallel3` 仍是 `primary_object_unsupported_function_count=64`，但首屏已钉死在 `rsaBigPrivateKeyFromPkcs1Value(var offset) / randomFillNative(if chengSystemEntropyFill...) / randomFillUrandom(let f = randomOpenUrandom()) / initMsQuicTls13PeerVerifyState(out.revocationMode = X509RevocationOff)`。 |
| 下一刀 | 不再在 wrapper 名字上兜圈子，直接收四个最短句型：1）[rsa.cheng](/Users/lbcheng/cheng-lang/src/std/crypto/rsa.cheng) 的 `private-key` 三函数；2）[rand.cheng](/Users/lbcheng/cheng-lang/src/std/crypto/rand.cheng) 的外部熵调用和 `openRead` 返回值绑定；3）[handshake13.cheng](/Users/lbcheng/cheng-lang/v3/src/quic/tls/handshake13.cheng) 的 `X509RevocationOff` 写回；4）随后再回到 `MsQuicTls13HandshakeState` 的大对象 layout。 |
| 开始时间 | `2026-04-12 09:44 +0800` | 继续并行推进 `stage2 QUIC/TLS`，这轮只保留 host 真绿的修补，不把试错版 `rsa` 留在树上。 |
| 已完成 | [handshake13.cheng](/Users/lbcheng/cheng-lang/v3/src/quic/tls/handshake13.cheng) 已去掉 `peer verify` 初始化和 reset 链上的 `X509RevocationOff` 写回；[policy.cheng](/Users/lbcheng/cheng-lang/v3/src/quic/tls/policy.cheng) 已把 `certLeafKeyKind == X509KeyUnknown` 收成标量比较；中途试过把 [rsa.cheng](/Users/lbcheng/cheng-lang/src/std/crypto/rsa.cheng) 的 `rsaBigSignPssBytes/rsaBigPrivateKeyFrom*Into` 压成更短解析链，但 host 运行期真实报错 `rsa: pss em too short`，现已全部撤回。 |
| 验收 | fresh host 编跑 `/tmp/quic_transport_loopback_smoke.host.parallel7` 重新前台输出 `v3 quic_transport_loopback_smoke ok`；fresh `stage2` 编 `/tmp/quic_transport_loopback_smoke.stage2.parallel7` 仍是 `primary_object_unsupported_function_count=64`，但 `msquicTlsPolicyValidateForServer` 和 `initMsQuicTls13PeerVerifyState` 已不在 unsupported 列表。 |
| 下一刀 | 现在只该正面切三条还在首屏的真链：1）[rsa.cheng](/Users/lbcheng/cheng-lang/src/std/crypto/rsa.cheng) 的 `rsaBigSignPssBytes(nextDecoded.tag == asn1TagInteger)`；2）[rand.cheng](/Users/lbcheng/cheng-lang/src/std/crypto/rand.cheng) 的 `chengSystemEntropyFill` 调用解析和 `bos.openRead(...)` 赋值；3）然后再进 [ecnist.cheng](/Users/lbcheng/cheng-lang/src/std/crypto/ecnist.cheng) 的 `bigShiftRight1/p256DeterministicK`。 |
| 开始时间 | `2026-04-12 09:50 +0800` | 继续并行推进 `host QUIC/TLS`，先把 no-cache `run_v3_host_smokes.sh` 重新拉回全绿，再顺手核对 `stage2` 新前沿。 |
| 已完成 | [handshake13.cheng](/Users/lbcheng/cheng-lang/v3/src/quic/tls/handshake13.cheng) 已把 `peer verify state` 完全收成 `Into` 路径，删掉会拖 ordinary 的复合返回 helper；[os.cheng](/Users/lbcheng/cheng-lang/src/std/os.cheng) 已把 `c_iometer_call` 改成直接走现成 native `@importc("c_iometer_call")`，并把 [readFileSizedInto(...)](/Users/lbcheng/cheng-lang/src/std/os.cheng) 收成 `var f + c_fclose(f)` 的稳定句型；[quic_transport_loopback_smoke.cheng](/Users/lbcheng/cheng-lang/v3/src/tests/quic_transport_loopback_smoke.cheng) 已删掉无意义的 `"msquic"` 文案断言，只保留真实错误回传。 |
| 验收 | `BACKEND_INCREMENTAL=0 BACKEND_MULTI_MODULE_CACHE=0 sh /Users/lbcheng/cheng-lang/v3/tooling/run_v3_host_smokes.sh` 现已重新前台输出 `v3 host smokes: ok`；targeted `/tmp/native_initial_crypto_frame_smoke.after_os_fix`、`/tmp/quic_transport_loopback_smoke.clean`、`/tmp/libp2p_quic_tls_smoke.host.after_inline` 这条 QUIC/TLS 主链都已真编真跑通过。 |
| `stage2` 前沿 | 已完成 | `cheng.stage2` 定向编 [quic_transport_loopback_smoke.cheng](/Users/lbcheng/cheng-lang/v3/src/tests/quic_transport_loopback_smoke.cheng) 后，首个 unsupported 已正式收敛到 [rsa.cheng](/Users/lbcheng/cheng-lang/src/std/crypto/rsa.cheng) 的 `rsaPkcs1ValueFromPrivateKeyBytes`；后面跟着 [rand.cheng](/Users/lbcheng/cheng-lang/src/std/crypto/rand.cheng) 的 `randomFillUrandom`、[ecnist.cheng](/Users/lbcheng/cheng-lang/src/std/crypto/ecnist.cheng) 的 `bigShiftRight1/p256DeterministicK`，以及 [handshake13.cheng](/Users/lbcheng/cheng-lang/v3/src/quic/tls/handshake13.cheng) 的 `MsQuicTls13HandshakeState` layout 大对象链。 |
| 下一刀 | 直接切四条最短真链：1）把 [rsaPkcs1ValueFromPrivateKeyBytes(...)](/Users/lbcheng/cheng-lang/src/std/crypto/rsa.cheng) 再拆成 `kind/pkcs8/pkcs1` 三个局部 helper；2）把 [randomFillUrandom(...)](/Users/lbcheng/cheng-lang/src/std/crypto/rand.cheng) 的 `openRead` 绑定改成 seed 更稳的句型；3）正面收 [ecnist.cheng](/Users/lbcheng/cheng-lang/src/std/crypto/ecnist.cheng) 的 `bigShiftRight1/p256DeterministicK`；4）最后再回 [handshake13.cheng](/Users/lbcheng/cheng-lang/v3/src/quic/tls/handshake13.cheng) 的 `MsQuicTls13HandshakeState` layout。 |
| 开始时间 | `2026-04-12 09:59 +0800` | 继续并行推进 `stage2 QUIC/TLS`，这轮只保留 host 真绿的 `rand/rsa` 修补，并把下一刀从 `rsa` 明确切到 `randomFillUrandom(...)`。 |
| 已完成 | [rand.cheng](/Users/lbcheng/cheng-lang/src/std/crypto/rand.cheng) 现已保留 `randomFillNative -> randomFillUrandom -> bos.openRead(randUrandomPath) + bos.c_fread(...)` 这条 fresh host 真绿的形状；[rsa.cheng](/Users/lbcheng/cheng-lang/src/std/crypto/rsa.cheng) 现已改成分支直返版 `rsaPkcs1ValueFromPrivateKeyBytes(...)`，同时确认 `rsaBigSignPssBytes -> rsaBigPrivateKeyFromBytesInto(...)` 这条折叠路线会把 host 真实打坏，已经撤回。 |
| 结果 | 已完成 | fresh host 编跑 `/tmp/quic_transport_loopback_smoke.host.current` 和 `/tmp/quic_transport_loopback_smoke.host.rsa_branchret` 都继续前台输出 `v3 quic_transport_loopback_smoke ok`；fresh `stage2` 编 `/tmp/quic_transport_loopback_smoke.stage2.current.log` 与 `/tmp/quic_transport_loopback_smoke.stage2.rsa_branchret.log` 都稳定显示 `primary_object_unsupported_function_count=64`，首位仍是 [rsaPkcs1ValueFromPrivateKeyBytes(...)](/Users/lbcheng/cheng-lang/src/std/crypto/rsa.cheng)，第二位仍是 [randomFillUrandom(...)](/Users/lbcheng/cheng-lang/src/std/crypto/rand.cheng)。 |
| 当前口径 | 已完成 | `rsa` 这轮已经把“坏语义路线”和“纯样式无收益路线”都排干净了：`Into(var Bytes)` 和 `privateKeyFromBytesInto` 直连签名都不是正路。下一刀不再盯 `rsa` 分支样式，而是直接收 [randomFillUrandom(...)](/Users/lbcheng/cheng-lang/src/std/crypto/rand.cheng) 的 `openRead` 返回值绑定；只有它让开以后，后面的 [ecnist.cheng](/Users/lbcheng/cheng-lang/src/std/crypto/ecnist.cheng) 和 [handshake13.cheng](/Users/lbcheng/cheng-lang/v3/src/quic/tls/handshake13.cheng) 才值得继续切。 |
| 开始时间 | `2026-04-12 10:22 +0800` | 继续并行推进 `host/stage2 QUIC/TLS`，先把 host 重新拉回全绿，再钉死 `stage2` 的最新前三条真前沿。 |
| 已完成 | [rand.cheng](/Users/lbcheng/cheng-lang/src/std/crypto/rand.cheng) 已回到 `bos.openRead(randUrandomPath) + bos.c_fread(...) + bos.close(f)`；[ecnist.cheng](/Users/lbcheng/cheng-lang/src/std/crypto/ecnist.cheng) 已新增 [p256DeterministicConcat4(...)](/Users/lbcheng/cheng-lang/src/std/crypto/ecnist.cheng)，把 `p256DeterministicK(...)` 里的嵌套 `bytesConcat` 热路径全部展开。 |
| 结果 | 已完成 | fresh host 编跑 `/tmp/quic_transport_loopback_smoke.host.ecnist_concat` 再次输出 `v3 quic_transport_loopback_smoke ok`，fresh host 编跑 `/tmp/libp2p_quic_tls_smoke.host.ecnist_concat` 再次输出 `v3 libp2p_quic_tls_smoke ok`。fresh `stage2` 编 `/tmp/quic_transport_loopback_smoke.stage2.after_rand_ecnist.log` 仍是 `primary_object_unsupported_function_count=64`，但最新前三条已经稳定收成 [rsaPkcs1ValueFromPrivateKeyBytes(...)](/Users/lbcheng/cheng-lang/src/std/crypto/rsa.cheng)、[randomFillUrandom(...)](/Users/lbcheng/cheng-lang/src/std/crypto/rand.cheng)、[p256DeterministicK(...)](/Users/lbcheng/cheng-lang/src/std/crypto/ecnist.cheng)。 |
| 当前口径 | 已完成 | host 主线已经恢复，当前最短下一刀不再碰 `host`，直接正面切 `stage2` 的三条最近真根：先 [rsa.cheng](/Users/lbcheng/cheng-lang/src/std/crypto/rsa.cheng)，再 [rand.cheng](/Users/lbcheng/cheng-lang/src/std/crypto/rand.cheng)，再 [ecnist.cheng](/Users/lbcheng/cheng-lang/src/std/crypto/ecnist.cheng)。`handshake13/tls13` 这批大对象链现在明确排在后面。 |
| 开始时间 | `2026-04-12 11:12 +0800` | 继续并行推进 `stage2 QUIC/TLS`，先收 [rsa.cheng](/Users/lbcheng/cheng-lang/src/std/crypto/rsa.cheng)、[rand.cheng](/Users/lbcheng/cheng-lang/src/std/crypto/rand.cheng)、[ecnist.cheng](/Users/lbcheng/cheng-lang/src/std/crypto/ecnist.cheng) 的前三条真前沿，同时保持 host 定向 smoke 不回退。 |
| 已完成 | [handshake13.cheng](/Users/lbcheng/cheng-lang/v3/src/quic/tls/handshake13.cheng) 这轮已经稳定收掉两段大对象链：`BuildCertificateVerify/BuildFinished` 改成只吃 `cipher + transcript + secret bytes`，`ResetClientState/ResetServerState` 改成直接 `initMsQuicTls13HandshakeStateInto(...)` 原地初始化全局状态，不再走本地大对象再整块写回。标准库侧保留 [ecnist.cheng](/Users/lbcheng/cheng-lang/src/std/crypto/ecnist.cheng) 的 `p256ConstNHalf()` 常量化；`rsa/rand` 继续维持当前 host 真绿的稳定形状。 |
| 结果 | 已完成 | fresh host 定向编跑 `/tmp/quic_transport_loopback_smoke.host.final` 继续前台输出 `v3 quic_transport_loopback_smoke ok`。fresh `stage2` 定向编 `/tmp/quic_transport_loopback_smoke.stage2.final` 的首个 unsupported 仍稳定是 [rsaPkcs1ValueFromPrivateKeyBytes(...)](/Users/lbcheng/cheng-lang/src/std/crypto/rsa.cheng)，随后是 [randomFillUrandom(...)](/Users/lbcheng/cheng-lang/src/std/crypto/rand.cheng)、[p256DeterministicK(...)](/Users/lbcheng/cheng-lang/src/std/crypto/ecnist.cheng)，再后面才是 `handshake13` 大对象链。 |
| 当前口径 | 已完成 | 这轮已经把 `handshake13` 能安全裁掉的大对象传递链先裁完了，当前继续硬抠 `rsa/rand/ecnist` 只会撞编译器句型边界，不会再带来线性收益。下一刀如果继续推 `stage2`，就该单独正面收编译器对 `Result[Bytes]`、`openRead` 返回值绑定、`bytesAlloc -> Bytes` 物化的 ordinary 缺口；不是再在业务代码里来回换样式。 |
| 收尾修补 | 已完成 | [rsa.cheng](/Users/lbcheng/cheng-lang/src/std/crypto/rsa.cheng) 的 [rsaBigSignPssBytes(...)](/Users/lbcheng/cheng-lang/src/std/crypto/rsa.cheng) 已收回到 `rsaPkcs1ValueFromPrivateKeyBytes(...) -> rsaBigSignPssPkcs1Value(...)` 的稳定路径；同时 [rsaPkcs1ValueFromPrivateKeyBytes(...)](/Users/lbcheng/cheng-lang/src/std/crypto/rsa.cheng) 也收回到“本地 `pkcs1Value` 聚合后统一返回”的稳定形状。 |
| 最终验收 | 已完成 | `BACKEND_INCREMENTAL=0 BACKEND_MULTI_MODULE_CACHE=0 sh /Users/lbcheng/cheng-lang/v3/tooling/run_v3_host_smokes.sh` 最终输出 `v3 host smokes: ok`；`sh /Users/lbcheng/cheng-lang/v3/tooling/run_v3_stage23_libp2p_smokes.sh` 输出 `v3 stage2/stage3 libp2p: ok`；fresh no-cache 定向 `/tmp/quic_transport_loopback_smoke.nocache.fix` 与 `/tmp/libp2p_quic_tls_smoke.nocache.fix` 也都前台 `ok`。 |
| 开始时间 | `2026-04-12 10:40 +0800` | 继续并行推进 `stage2 QUIC/TLS`，不再碰业务协议模块，直接正面补 seed 对顶层常量解析和标量比较发射的 ordinary 缺口。 |
| 并行核对 | 已完成 | 只读并行核对已把真根钉死到三类句型：`rsaPkcs1ValueFromPkcs8OuterSeq / rsaPkcs1ValueFromPrivateKeyBody` 的裸常量比较，`randomFillUrandom(...)` 的 `openRead` 返回值绑定，以及 `p256DeterministicK(...)` 的 `bytesAlloc -> Bytes` 物化。 |
| 修补 | 已完成 | [cheng_v3_seed.c](/Users/lbcheng/cheng-lang/v3/bootstrap/cheng_v3_seed.c) 这轮补了四处通用能力：`V3TopLevelConstStub` 现在保留字符串常量值；`v3_try_resolve_i64_top_level_const_expr(...)` 可解析顶层 `+ / - / <<` 常量表达式；`v3_find_expr_top_level_const(...)` 增加全局唯一裸常量兜底解析；`v3_codegen_compare_expr_scalar(...)` 改成按 `ptr / i64 / i32` 选择比较 ABI，并分别发 `cmp x*` 或 `cmp w*`。 |
| 结果 | 已完成 | fresh `stage2` 定向编 [quic_transport_loopback_smoke.cheng](/Users/lbcheng/cheng-lang/v3/src/tests/quic_transport_loopback_smoke.cheng) 时，`primary_object_unsupported_function_count` 已从此前的 `64` 直接收成 `0`，并且生成的 stage2 可执行文件已经前台输出 `v3 quic_transport_loopback_smoke ok`。host 定向编同一 smoke 也继续前台 `ok`。 |
| 验收 | 已完成 | `sh /Users/lbcheng/cheng-lang/v3/tooling/bootstrap_bridge_v3.sh` 与 `sh /Users/lbcheng/cheng-lang/v3/tooling/build_backend_driver_v3.sh` 均已重建成功；`DIAG_CONTEXT=1 /Users/lbcheng/cheng-lang/artifacts/v3_bootstrap/cheng.stage2 system-link-exec --root /Users/lbcheng/cheng-lang/v3 --in /Users/lbcheng/cheng-lang/v3/src/tests/quic_transport_loopback_smoke.cheng --emit exe --target arm64-apple-darwin --out /tmp/quic_transport_loopback_smoke.stage2.after_cmp` 显示 `primary_object_unsupported_functions=-` 且运行输出 `v3 quic_transport_loopback_smoke ok`；`DIAG_CONTEXT=1 /Users/lbcheng/cheng-lang/artifacts/v3_backend_driver/cheng ... /tmp/quic_transport_loopback_smoke.host.after_cmp` 与 `... /tmp/libp2p_quic_tls_smoke.host.after_cmp` 均前台 `ok`；`sh /Users/lbcheng/cheng-lang/v3/tooling/run_v3_stage23_libp2p_smokes.sh` 输出 `v3 stage2/stage3 libp2p: ok`；`BACKEND_INCREMENTAL=0 BACKEND_MULTI_MODULE_CACHE=0 sh /Users/lbcheng/cheng-lang/v3/tooling/run_v3_host_smokes.sh` 最终再次输出 `v3 host smokes: ok`。 |
| 当前口径 | 已完成 | 这轮已经证明最近的 `stage2 QUIC/TLS` 活根不是 `rsa/rand/ecnist` 业务源码，而是 seed 对“顶层常量表达式 + 裸常量查找 + 标量比较宽度”这组普通句型的缺口。继续往前推时，应优先收剩余 ordinary 编译器句型，而不是再回去折腾 TLS 业务代码样式。 |
| 开始时间 | `2026-04-12 10:51 +0800` | 继续并行推进 `stage2/stage3` 覆盖面，把已经 fresh 坐实为全绿的内容平面 smoke 接进 stage23 runner，避免回归只靠手工 targeted 命令发现。 |
| 并行核对 | 已完成 | fresh `stage2` 下，[content_codec_smoke.cheng](/Users/lbcheng/cheng-lang/v3/src/tests/content_codec_smoke.cheng)、[content_runtime_smoke.cheng](/Users/lbcheng/cheng-lang/v3/src/tests/content_runtime_smoke.cheng)、[content_stub_smoke.cheng](/Users/lbcheng/cheng-lang/v3/src/tests/content_stub_smoke.cheng)、[content_quic_smoke.cheng](/Users/lbcheng/cheng-lang/v3/src/tests/content_quic_smoke.cheng) 都已经编到 `primary_object_unsupported_function_count=0`，并且前台真跑 `ok`；同口径 `stage3` 也都前台 `ok`。 |
| 修补 | 已完成 | [run_v3_stage23_libp2p_smokes.sh](/Users/lbcheng/cheng-lang/v3/tooling/run_v3_stage23_libp2p_smokes.sh) 已把 `content_codec_smoke / content_runtime_smoke / content_stub_smoke / content_quic_smoke` 接进 `tests` 列表，放在 `libp2p_protocols_smoke` 之后、`chain_node_libp2p_smoke` 之前。 |
| 验收 | 已完成 | `sh /Users/lbcheng/cheng-lang/v3/tooling/run_v3_stage23_libp2p_smokes.sh` 复跑后，新增四条内容 smoke 在 `stage2 + stage3` 下全部编过并运行输出 `ok`，最终整条脚本继续输出 `v3 stage2/stage3 libp2p: ok`。 |
| 当前口径 | 已完成 | 现在 `stage23` runner 不再只覆盖 libp2p 核心链，也把内容平面的 `codec/runtime/stub/quic` 四条主烟纳进持续回归了。下一刀如果继续推，就该扩大到更多 host-only smoke 的 `stage2/stage3` 覆盖，而不是再手工逐条点验内容链。 |
| 开始时间 | `2026-04-12 11:02 +0800` | 继续并行推进 `stage23` 覆盖面，把 `overlay/pubsub/dag/plumtree/erasure` 这批已经 fresh 坐实全绿的链路也接进 runner，做成真正的持续门禁。 |
| 并行核对 | 已完成 | fresh `stage2 + stage3` 下，[overlay_contracts_smoke.cheng](/Users/lbcheng/cheng-lang/v3/src/tests/overlay_contracts_smoke.cheng)、[pubsub_smoke.cheng](/Users/lbcheng/cheng-lang/v3/src/tests/pubsub_smoke.cheng)、[dag_mempool_smoke.cheng](/Users/lbcheng/cheng-lang/v3/src/tests/dag_mempool_smoke.cheng)、[plumtree_smoke.cheng](/Users/lbcheng/cheng-lang/v3/src/tests/plumtree_smoke.cheng)、[erasure_swarm_smoke.cheng](/Users/lbcheng/cheng-lang/v3/src/tests/erasure_swarm_smoke.cheng) 都已经编到 `primary_object_unsupported_function_count=0`，并且前台真跑 `ok`。 |
| 修补 | 已完成 | [run_v3_stage23_libp2p_smokes.sh](/Users/lbcheng/cheng-lang/v3/tooling/run_v3_stage23_libp2p_smokes.sh) 已继续接入 `overlay_contracts_smoke / pubsub_smoke / dag_mempool_smoke / plumtree_smoke / erasure_swarm_smoke`。当前脚本实际覆盖已扩到 `native_initial_crypto_frame / native_client_hello_wire / tls_client_hello_parse / tls_initial_packet_roundtrip / quic_transport_loopback / libp2p_quic_tls` 这条 QUIC/TLS 主链，再接 `overlay/pubsub/dag/plumtree/erasure/content/chain_node`。 |
| 验收 | 已完成 | `sh /Users/lbcheng/cheng-lang/v3/tooling/run_v3_stage23_libp2p_smokes.sh` 复跑后，新增五条 smoke 在 `stage2 + stage3` 下全部编过并运行输出 `ok`，而且整条扩容后的 runner 最终继续输出 `v3 stage2/stage3 libp2p: ok`。 |
| 当前口径 | 已完成 | 现在 `stage23` runner 已经不只是 libp2p + 内容平面，而是把 QUIC/TLS 底链、overlay 合同、pubsub、dag/plumtree/erasure、content、chain_node 全串进一条持续门禁。下一刀如果继续推，就该挑剩下仍只存在于 host runner 的烟，评估哪些值得抬进 `stage2/stage3`。 |
| 开始时间 | `2026-04-12 13:51 +0800` | 继续并行推进 `stage23/host` 总 gate，把 `anti_entropy_signature_fields_smoke` 正式纳进持续门禁，并顺手清掉 `handshake13 peer verify` 剩余的历史薄壳。 |
| 并行核对 | 已完成 | 并行核对已把两件事钉死：1）[anti_entropy_signature_fields_smoke.cheng](/Users/lbcheng/cheng-lang/v3/src/tests/anti_entropy_signature_fields_smoke.cheng) 的真阻塞不是协议逻辑，而是 smoke 里直接调用 `lsmr.v3LsmrLocalityHashText(...)` 把 `std/strutils` 拖进 ordinary；2）[handshake13.cheng](/Users/lbcheng/cheng-lang/v3/src/quic/tls/handshake13.cheng) 剩下能安全删除的是 `peer verify` 单字段薄壳，不该碰 `LoadRevocationCrls/ResetRuntime/ResetConfig` 这类实义入口。 |
| 修补 | 已完成 | [anti_entropy_signature_fields_smoke.cheng](/Users/lbcheng/cheng-lang/v3/src/tests/anti_entropy_signature_fields_smoke.cheng) 已新增 `smokeLocalityHash(...) = fixed256.sha256Fixed(...)`，并把三处 `v3LsmrLocalityHashText(...)` 改成纯 `fixed256` 路径；[handshake13.cheng](/Users/lbcheng/cheng-lang/v3/src/quic/tls/handshake13.cheng) 已删掉 `msquicTls13PeerVerifySetOcspResponse / msquicTls13HandshakeSetRevocationMode / msquicTls13HandshakeSetRevocationCrls / msquicTls13ResetClientPeerVerifyState / msquicTls13ResetServerPeerVerifyState`，同时 `msquicTls13ParseCertificateEntryExtensions(...)` 直接写 `peerOcspResponse`。 |
| runner 扩容 | 已完成 | [run_v3_stage23_libp2p_smokes.sh](/Users/lbcheng/cheng-lang/v3/tooling/run_v3_stage23_libp2p_smokes.sh) 和 [run_v3_host_smokes.sh](/Users/lbcheng/cheng-lang/v3/tooling/run_v3_host_smokes.sh) 都已正式接入 `anti_entropy_signature_fields_smoke`。 |
| 验收 | 已完成 | targeted host `/tmp/anti_entropy_signature_fields_smoke.host`、targeted stage2 `/tmp/anti_entropy_signature_fields_smoke.stage2.runner`、targeted stage3 `/tmp/anti_entropy_signature_fields_smoke.stage3.runner` 都前台输出 `v3 anti_entropy_signature_fields_smoke ok`；`BACKEND_INCREMENTAL=0 BACKEND_MULTI_MODULE_CACHE=0 sh /Users/lbcheng/cheng-lang/v3/tooling/run_v3_stage23_libp2p_smokes.sh` 最终输出 `v3 stage2/stage3 libp2p: ok`；`BACKEND_INCREMENTAL=0 BACKEND_MULTI_MODULE_CACHE=0 sh /Users/lbcheng/cheng-lang/v3/tooling/run_v3_host_smokes.sh` 最终输出 `v3 host smokes: ok`。 |
| 当前口径 | 已完成 | 现在 `anti_entropy_signature_fields_smoke` 已从“手工补验”的散点，升级成 `host + stage23` 双 runner 的持续门禁；`handshake13` 的 `peer verify` 路径也已经只剩真实逻辑函数，不再留单字段写回薄壳。 |
| 开始时间 | `2026-04-12 14:11 +0800` | 继续并行推进 `stage23` 覆盖面，把 `lsmr_types / lsmr_locality_storage` 纳进主 gate，同时把 [handshake13.cheng](/Users/lbcheng/cheng-lang/v3/src/quic/tls/handshake13.cheng) 里残留的 setter/调试输出彻底清掉。 |
| 并行核对 | 已完成 | 并行核对已把边界钉死：1）[lsmr_types_smoke.cheng](/Users/lbcheng/cheng-lang/v3/src/tests/lsmr_types_smoke.cheng) 和 [lsmr_locality_storage_smoke.cheng](/Users/lbcheng/cheng-lang/v3/src/tests/lsmr_locality_storage_smoke.cheng) 都适合进 `stage23`；2）[udp_importc_smoke.cheng](/Users/lbcheng/cheng-lang/v3/src/tests/udp_importc_smoke.cheng) 虽然 targeted 能过，但它是真宿主 UDP 桥，不该污染主 gate；3）`handshake13` 当前真风险不是协议逻辑，而是残留的单字段 setter 和调试 `echo` 会把 `stage2/stage3` ordinary 再次拖回去。 |
| 修补 | 已完成 | [run_v3_stage23_libp2p_smokes.sh](/Users/lbcheng/cheng-lang/v3/tooling/run_v3_stage23_libp2p_smokes.sh) 已新增 `lsmr_types_smoke / lsmr_locality_storage_smoke`，并明确不接 `udp_importc_smoke`；[handshake13.cheng](/Users/lbcheng/cheng-lang/v3/src/quic/tls/handshake13.cheng) 已删掉剩余 `PeerVerifySet*` setter 调用和 `msquic_tls13_*` 调试 `echo`，把 `peerCertVerified / certVerifyOk / finishedOk / revocationMode` 全改成字段直写。 |
| 阶段验收 | 已完成 | targeted `stage2` `/tmp/native_initial_crypto_frame_smoke.stage2.recheck`、targeted `stage3` `/tmp/tls_client_hello_parse_smoke.stage3.current2` 都前台 `ok`；扩容后的 `BACKEND_INCREMENTAL=0 BACKEND_MULTI_MODULE_CACHE=0 sh /Users/lbcheng/cheng-lang/v3/tooling/run_v3_stage23_libp2p_smokes.sh` 最终输出 `v3 stage2/stage3 libp2p: ok`，并且新接入的 `lsmr_types_smoke / lsmr_locality_storage_smoke` 在 `stage2 + stage3` 下都前台 `ok`。 |
| host 回归 | 已完成 | host 定向 `/tmp/quic_transport_loopback_smoke.host.recheck`、`/tmp/libp2p_quic_tls_smoke.host.recheck`、`/tmp/content_quic_smoke.host.recheck` 都前台 `ok`，说明这轮 `handshake13` 清理没有把 host 主链打坏。 |
| 当前口径 | 已完成 | `stage23` 现在已经覆盖 `anti_entropy_signature_fields + lsmr_types + lsmr_locality_storage + lsmr_bagua_prefix_tree` 这一整段 LSMR 主链；`udp_importc_smoke` 维持在 host/独立 targeted，不进主 runner。 |
| 开始时间 | `2026-04-12 14:37 +0800` | 继续并行推进 `stage23` 的 compiler/tooling 自检，把已 targeted 坐实为全绿的 `compiler_runtime / compiler_pipeline_stub / lowering_plan / primary_object_plan / object_native_link_plan / program_selfhost` 正式接进主 gate。 |
| 并行核对 | 已完成 | 六条候选 smoke 都已在 `stage2 + stage3` 下独立 targeted 真编真跑 `ok`，没有一条是“只在 host 绿”的假稳定。 |
| 修补 | 已完成 | [run_v3_stage23_libp2p_smokes.sh](/Users/lbcheng/cheng-lang/v3/tooling/run_v3_stage23_libp2p_smokes.sh) 已把 `compiler_runtime_smoke / compiler_pipeline_stub_smoke / lowering_plan_smoke / primary_object_plan_smoke / object_native_link_plan_smoke / program_selfhost_smoke` 接到 `tests` 开头，作为 compiler/tooling 预检层。 |
| 过程收口 | 已完成 | 这轮 runner 中途又把 [handshake13.cheng](/Users/lbcheng/cheng-lang/v3/src/quic/tls/handshake13.cheng) 里最后一组残留 setter 暴露出来：`PeerVerifySetChainVerified / SetCertVerifyOk / SetFinishedOk / SetRevocationMode / SetOcspResponse`。这些定义和调用已全部删净，调用点统一改成字段直写。 |
| 验收 | 已完成 | 扩容后的 `BACKEND_INCREMENTAL=0 BACKEND_MULTI_MODULE_CACHE=0 sh /Users/lbcheng/cheng-lang/v3/tooling/run_v3_stage23_libp2p_smokes.sh` 最终输出 `v3 stage2/stage3 libp2p: ok`。新增六条 compiler/tooling smoke 在 `stage2 + stage3` 下都已前台 `ok`。 |
| host 回归 | 已完成 | host 定向 `/tmp/libp2p_quic_tls_smoke.host.after_stage23` 和 `/tmp/content_quic_smoke.host.after_stage23` 都前台 `ok`，说明这轮为让 `stage23` 变绿而做的 `handshake13` 清理没有把 host QUIC/content 主链打坏。 |
| 当前口径 | 已完成 | `stage23` 现在已经形成三层门禁：`compiler/tooling` 预检层、`QUIC/TLS + libp2p` 协议层、`LSMR + overlay/pubsub/dag/plumtree/erasure/content/chain_node` 业务主链。 |
| 开始时间 | `2026-04-12 17:06 +0800` | 继续实现 `v3` 混合网络第一阶段和第二阶段前哨：先把 `tailnet` 从“单纯地址域”收成参考 `tsnettransport.nim` 的 builder/mapping/status 模型，再把 `WebRTC signal` 编解码与 transport 配置面真落进 Cheng 源码。 |
| 修补 | 已完成 | [types.cheng](/Users/lbcheng/cheng-lang/v3/src/libp2p/core/types.cheng)、[multiaddr.cheng](/Users/lbcheng/cheng-lang/v3/src/libp2p/core/multiaddr.cheng)、[peerstore.cheng](/Users/lbcheng/cheng-lang/v3/src/libp2p/core/peerstore.cheng)、[host.cheng](/Users/lbcheng/cheng-lang/v3/src/libp2p/host/host.cheng)、[host_quic.cheng](/Users/lbcheng/cheng-lang/v3/src/libp2p/host/host_quic.cheng)、[content_runtime_host.cheng](/Users/lbcheng/cheng-lang/v3/src/chain/content_runtime_host.cheng)、[content_runtime_host_quic.cheng](/Users/lbcheng/cheng-lang/v3/src/chain/content_runtime_host_quic.cheng) 已把 `peerstore` 收成多 endpoint，新增 `networkDomain / visibility / priority / trustedInfra`，host 选路固定成“活连接优先，再按 tailnet/lan/public 和 quic/tcp 顺序选 endpoint”。新增 [tailnet_transport.cheng](/Users/lbcheng/cheng-lang/v3/src/libp2p/transports/tailnet_transport.cheng) 参考 `nim-libp2p` 的 `TsnetTransportBuilderConfig / TsnetListenerMapping / StatusSnapshot` 落了 `controlEndpoint / relayEndpoint / syntheticIPv4/IPv6 / listenerMappings / publishedAddrs / status`；新增 [webrtc_signal.cheng](/Users/lbcheng/cheng-lang/v3/src/libp2p/protocols/webrtc_signal.cheng) 和 [webrtc_transport.cheng](/Users/lbcheng/cheng-lang/v3/src/libp2p/transports/webrtc_transport.cheng)，先把 `offer / answer / candidate / turn policy` 统一 codec 和 browser-webrtc transport 配置面落地。 |
| smoke/gate | 已完成 | 新增 [libp2p_tailnet_policy_smoke.cheng](/Users/lbcheng/cheng-lang/v3/src/tests/libp2p_tailnet_policy_smoke.cheng)、[libp2p_tailnet_visibility_smoke.cheng](/Users/lbcheng/cheng-lang/v3/src/tests/libp2p_tailnet_visibility_smoke.cheng)、[libp2p_tailnet_transport_smoke.cheng](/Users/lbcheng/cheng-lang/v3/src/tests/libp2p_tailnet_transport_smoke.cheng)、[chain_node_tailnet_smoke.cheng](/Users/lbcheng/cheng-lang/v3/src/tests/chain_node_tailnet_smoke.cheng)、[webrtc_signal_codec_smoke.cheng](/Users/lbcheng/cheng-lang/v3/src/tests/webrtc_signal_codec_smoke.cheng)；并已接进 [run_v3_host_smokes.sh](/Users/lbcheng/cheng-lang/v3/tooling/run_v3_host_smokes.sh) 与 [run_v3_stage23_libp2p_smokes.sh](/Users/lbcheng/cheng-lang/v3/tooling/run_v3_stage23_libp2p_smokes.sh)。 |
| 验收 | 已完成 | targeted host：`sh /Users/lbcheng/cheng-lang/v3/tooling/run_v3_host_smokes.sh libp2p_host_smoke libp2p_tailnet_policy_smoke libp2p_tailnet_visibility_smoke chain_node_tailnet_smoke chain_node_libp2p_smoke content_runtime_smoke content_quic_smoke libp2p_quic_tls_smoke`、`sh /Users/lbcheng/cheng-lang/v3/tooling/run_v3_host_smokes.sh libp2p_tailnet_transport_smoke webrtc_signal_codec_smoke` 全部前台 `ok`；targeted `stage2 + stage3`：`libp2p_tailnet_transport_smoke / webrtc_signal_codec_smoke` 全部前台 `ok`；完整 no-cache [run_v3_stage23_libp2p_smokes.sh](/Users/lbcheng/cheng-lang/v3/tooling/run_v3_stage23_libp2p_smokes.sh) 最终再次输出 `v3 stage2/stage3 libp2p: ok`。 |
| 下一刀 | 进行中 | 把 `tailnet_transport` 从当前 `config + listener mapping + status` 推进到 `headscale control + DERP relay` provider 桥；同时继续补 `WebRTC` 的信令承载与 DataChannel 真 transport，而不是停在 codec 层。 |
| 开始时间 | `2026-04-12 18:09 +0800` | 继续把 `tailnet_transport` 推进到 `headscale + DERP` provider/status 面，并把 `DERP relay` 在选路里钉成 `tailnet reachability = relayed`，不新增假的 transport kind。 |
| 修补 | 已完成 | [tailnet_transport.cheng](/Users/lbcheng/cheng-lang/v3/src/libp2p/transports/tailnet_transport.cheng) 已新增 `providerKind/headscaleNamespace/headscaleNoisePublicKey/exactRouting/derpRegion*/derpHostname/derpStunPort/derpHealthy/peerSessions`，并补上 `v3Libp2pTailnetConfigSetHeadscale(...)`、`v3Libp2pTailnetSetDerpHealthy(...)`、`v3Libp2pTailnetRegisterPeerSession(...)`、`v3Libp2pTailnetProviderUsesExactRouting(...)`、`v3Libp2pTailnetDerpAvailable(...)`、`v3Libp2pTailnetDerpMapSummary(...)`。同时 [peerstore.cheng](/Users/lbcheng/cheng-lang/v3/src/libp2p/core/peerstore.cheng) 已把 `reachability = relayed` 固定加惩罚分，确保 `DERP` 只作为 relay fallback，排在 direct `tailnet/lan/public` 后面。 |
| smoke/gate | 已完成 | 新增 [libp2p_tailnet_derp_smoke.cheng](/Users/lbcheng/cheng-lang/v3/src/tests/libp2p_tailnet_derp_smoke.cheng)，真验 `headscale provider/status + DERP summary + peer session + relay fallback rank`；并已接进 [run_v3_host_smokes.sh](/Users/lbcheng/cheng-lang/v3/tooling/run_v3_host_smokes.sh) 与 [run_v3_stage23_libp2p_smokes.sh](/Users/lbcheng/cheng-lang/v3/tooling/run_v3_stage23_libp2p_smokes.sh)。原有 [libp2p_tailnet_transport_smoke.cheng](/Users/lbcheng/cheng-lang/v3/src/tests/libp2p_tailnet_transport_smoke.cheng) 也补了 `synthetic provider/control plane` 断言。 |
| 验收 | 已完成 | targeted host：`sh /Users/lbcheng/cheng-lang/v3/tooling/run_v3_host_smokes.sh libp2p_tailnet_policy_smoke libp2p_tailnet_visibility_smoke libp2p_tailnet_transport_smoke libp2p_tailnet_derp_smoke webrtc_signal_codec_smoke chain_node_tailnet_smoke chain_node_libp2p_smoke content_runtime_smoke content_quic_smoke libp2p_quic_tls_smoke` 最终输出 `v3 host smokes: ok`；完整 no-cache：`BACKEND_INCREMENTAL=0 BACKEND_MULTI_MODULE_CACHE=0 sh /Users/lbcheng/cheng-lang/v3/tooling/run_v3_stage23_libp2p_smokes.sh` 最终再次输出 `v3 stage2/stage3 libp2p: ok`。 |
| 当前口径 | 已完成 | `headscale/DERP` 现在已经进入 Cheng 源码层面的 `provider/config/status` 和 `relay reachability` 规则；`DERP` 没有被伪装成新 transport，`WebRTC TURN` 也没有和 `DERP` 混成一层。 |
| 下一刀 | 进行中 | 把 [webrtc_signal.cheng](/Users/lbcheng/cheng-lang/v3/src/libp2p/protocols/webrtc_signal.cheng) 和 [webrtc_transport.cheng](/Users/lbcheng/cheng-lang/v3/src/libp2p/transports/webrtc_transport.cheng) 从当前 `codec + policy` 推进到 `libp2p stream` 承载的 `offer/answer/candidate` 会话层，再继续收真 `DataChannel` transport。 |
| 开始时间 | `2026-04-12 18:42 +0800` | 继续把混合网络第一阶段和 WebRTC 第二阶段前哨一次收口，同时修掉这轮为了接 WebRTC 会话层引出的通用 host 回归。 |
| 修补 | 已完成 | [webrtc_signal.cheng](/Users/lbcheng/cheng-lang/v3/src/libp2p/protocols/webrtc_signal.cheng) 已补 `V3Libp2pWebrtcSignalSession` 和 `MakeOffer/MakeAnswer/MakeCandidate/MakeTurnPolicy/SessionApply`；[webrtc_transport.cheng](/Users/lbcheng/cheng-lang/v3/src/libp2p/transports/webrtc_transport.cheng) 已补 Cheng 侧 `loopback open/select/read/write/request-response`；[host.cheng](/Users/lbcheng/cheng-lang/v3/src/libp2p/host/host.cheng) 与 [content_runtime_host.cheng](/Users/lbcheng/cheng-lang/v3/src/chain/content_runtime_host.cheng) 已真接上 `browser-webrtc` endpoint 的 `sync/content` 收发。 |
| 真回归 | 已完成 | 这轮新增 WebRTC 分支后，[host.cheng](/Users/lbcheng/cheng-lang/v3/src/libp2p/host/host.cheng) 的 `v3Libp2pSyncRequest(...)` 一度把通用 TCP `sync` 主线带坏，连 [libp2p_protocols_smoke.cheng](/Users/lbcheng/cheng-lang/v3/src/tests/libp2p_protocols_smoke.cheng)、[chain_node_libp2p_smoke.cheng](/Users/lbcheng/cheng-lang/v3/src/tests/chain_node_libp2p_smoke.cheng)、[chain_node_tailnet_smoke.cheng](/Users/lbcheng/cheng-lang/v3/src/tests/chain_node_tailnet_smoke.cheng) 都跟着挂。最终已拆成 `SyncDecodePayload/SyncRequestTcp/SyncRequestWebrtc` 三个叶子 helper，并统一先本地物化 `servePayload`，主线已恢复。 |
| 顺手修补 | 已完成 | [host.cheng](/Users/lbcheng/cheng-lang/v3/src/libp2p/host/host.cheng) 已把 `pin runtime` 从 [pin_registry.cheng](/Users/lbcheng/cheng-lang/v3/src/libp2p/host/pin_registry.cheng) 全局表收回 `Host` 自身字段；[pin_runtime_host.cheng](/Users/lbcheng/cheng-lang/v3/src/chain/pin_runtime_host.cheng) 和 [pin_runtime_host_quic.cheng](/Users/lbcheng/cheng-lang/v3/src/chain/pin_runtime_host_quic.cheng) 的 proof 路径也都改成先本地物化 `servePayload`。这样 [pin_runtime_host_smoke.cheng](/Users/lbcheng/cheng-lang/v3/src/tests/pin_runtime_host_smoke.cheng) 已重新从信号崩恢复为 `ok`。 |
| smoke/gate | 已完成 | 新增 [webrtc_signal_session_smoke.cheng](/Users/lbcheng/cheng-lang/v3/src/tests/webrtc_signal_session_smoke.cheng)、[webrtc_turn_fallback_smoke.cheng](/Users/lbcheng/cheng-lang/v3/src/tests/webrtc_turn_fallback_smoke.cheng)、[libp2p_webrtc_sync_smoke.cheng](/Users/lbcheng/cheng-lang/v3/src/tests/libp2p_webrtc_sync_smoke.cheng)、[webrtc_datachannel_content_smoke.cheng](/Users/lbcheng/cheng-lang/v3/src/tests/webrtc_datachannel_content_smoke.cheng)，并已接进 [run_v3_host_smokes.sh](/Users/lbcheng/cheng-lang/v3/tooling/run_v3_host_smokes.sh) 与 [run_v3_stage23_libp2p_smokes.sh](/Users/lbcheng/cheng-lang/v3/tooling/run_v3_stage23_libp2p_smokes.sh)。 |
| 验收 | 已完成 | targeted host：`libp2p_protocols_smoke / chain_node_libp2p_smoke / chain_node_tailnet_smoke / libp2p_webrtc_sync_smoke / webrtc_datachannel_content_smoke` 均已前台 `ok`；`stage2/stage3` 总 runner 已重新跑过 `tailnet policy/visibility/transport/derp + webrtc codec/session/turn-fallback/sync + libp2p_protocols + chain_node` 这条混合网络主链，并全部输出 `ok`。 |
| 尾部修补 | 已完成 | [content_runtime_host.cheng](/Users/lbcheng/cheng-lang/v3/src/chain/content_runtime_host.cheng) 又补了一刀，把 `RequestFetch` 拆成 `DecodeFetchPayload/RequestFetchTcp/RequestFetchWebrtc` 三个叶子 helper，修掉了 `stage2` 的 [content_runtime_smoke.cheng](/Users/lbcheng/cheng-lang/v3/src/tests/content_runtime_smoke.cheng) 运行期 `bytesSlice` 段错。修完后，`content_codec/runtime/stub/webrtc_datachannel_content/content_quic/chain_node_tailnet/chain_node_libp2p + chain_node_process(stage2/stage3)` 这条尾段已经重新真编真跑通过。 |
| 收尾修补 | 已完成 | [host_quic.cheng](/Users/lbcheng/cheng-lang/v3/src/libp2p/host/host_quic.cheng) 的 [v3Libp2pQuicExchange(...)](/Users/lbcheng/cheng-lang/v3/src/libp2p/host/host_quic.cheng) 已把 `quic_transport.v3Libp2pQuicLoopbackSelectProtocol(...)` 这条会在 `stage3` 总 gate 下炸 ordinary 的 `Result[bool]` 复合绑定链，改成原地 `multistream select bytes -> write -> read -> accepts`。协议语义没变，但 `stage3 libp2p_quic_tls_smoke` 已不再在这条句型上翻车。 |
| 最终验收 | 已完成 | `sh /Users/lbcheng/cheng-lang/v3/tooling/run_v3_host_smokes.sh` 最终输出 `v3 host smokes: ok`；`BACKEND_INCREMENTAL=0 BACKEND_MULTI_MODULE_CACHE=0 sh /Users/lbcheng/cheng-lang/v3/tooling/run_v3_stage23_libp2p_smokes.sh` 最终输出 `v3 stage2/stage3 libp2p: ok`。其中 [pin_runtime_quic_smoke.cheng](/Users/lbcheng/cheng-lang/v3/src/tests/pin_runtime_quic_smoke.cheng)、[content_runtime_smoke.cheng](/Users/lbcheng/cheng-lang/v3/src/tests/content_runtime_smoke.cheng)、[webrtc_datachannel_content_smoke.cheng](/Users/lbcheng/cheng-lang/v3/src/tests/webrtc_datachannel_content_smoke.cheng)、[chain_node_tailnet_smoke.cheng](/Users/lbcheng/cheng-lang/v3/src/tests/chain_node_tailnet_smoke.cheng)、[chain_node_libp2p_smoke.cheng](/Users/lbcheng/cheng-lang/v3/src/tests/chain_node_libp2p_smoke.cheng) 都已在正式 gate 里随整套主链真过。 |
