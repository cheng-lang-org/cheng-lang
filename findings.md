## Findings

| 项目 | 结论 |
|---|---|
| 新发现 | `shared emit` 不能只停在 `compiler_request/compiler_main` 愿意接参数。像这轮这样，必须再补一条 mainless fixture 真 smoke，既验证 report 里 `emit=shared/main_function_present=0/missing_reasons=-`，也验证磁盘上真出了 shared library 产物；否则“参数通了”和“主线真能用”会继续混账。 |
| 新发现 | [cheng_v3.sh](/Users/lbcheng/cheng-lang/v3/tooling/cheng_v3.sh) 这类用户面入口要往零脚本收，最短真路径不是一刀删 shell，而是先把 live 命令优先级倒过来：已有 fresh `backend_driver/stage3` 时，`bootstrap-bridge/print-bootstrap/build-backend-driver` 都优先走 Cheng，shell 只保留 cold-start fallback。 |
| 新发现 | 真 Linux `x86_64` 验收要把“ordinary backend driver 自举”与“exe/native link 主线”分开记账。这轮已经现场坐实：远端 `build-backend-driver` 仍会被 [compiler_main.cheng](/Users/lbcheng/cheng-lang/v3/src/tooling/compiler_main.cheng) 里的 `v3CompilerRemovePathTree / v3CompilerCleanHostrunArtifacts / v3CompilerR2cReactFreshCleanGate` 形状卡住，但 fresh `cheng.stage3 system-link-exec --target:x86_64-unknown-linux-gnu --emit:exe` 已经能在 `dmit` 真编出 [chain_node](/root/cheng-lang-deploy/artifacts/v3_chain_node/x86_64-unknown-linux-gnu/chain_node) 和 [rwad_bft_state_machine](/root/cheng-lang-deploy/artifacts/v3_rwad_bft_state_machine/x86_64-unknown-linux-gnu/rwad_bft_state_machine)，并在 `dmit + 64` 两台真机上都 `self-test ok`。 |
| 新发现 | `peer_id` 这条协议层主线不能直接把 [multibase.cheng](/Users/lbcheng/cheng-lang/src/std/multiformats/multibase.cheng) 拉进 ordinary 入口。现场已经坐实：它当前还有一批 `str` 常量传参、`if` 里 composite 返回和局部 `if url: ... else ...` 这种 seed 还发不稳的形状；最稳收法不是退回 bridge，而是像这轮这样把真正只需要的 `base58btc` 算法本地化成 seed 可发射的纯 Cheng 直线代码。 |
| 新发现 | runtime ABI 权威不能再写成“只看 `system_helpers.h`”或“只看 `system_helpers_backend.cheng`”。这轮 `verify_backend_runtime_abi` 已现场坐实，`cheng_exec_file_pipe_spawn` 这类宿主进程 ABI 还在最小 native bridge 里；最稳口径是 `system_helpers_backend.cheng + selflink_shim + io_time_bridge + host_process_ffi_bridge + compat header` 这份组合合同。 |
| 新发现 | [cheng_tooling.cheng](/Users/lbcheng/cheng-lang/src/tooling/cheng_tooling.cheng) 里的 runtime freshness/source list 如果还把 `system_helpers_selflink_min_runtime.c` 混进 active proof，会继续把“兼容后备入口”误算成 live 主链。当前已经坐实，真正该盯的现役入口只有 `system_helpers_backend.cheng + system_helpers_selflink_shim.c + system_helpers_io_time_bridge.c + system_helpers_host_process_ffi_bridge.c`。 |
| 新发现 | [r2c-react-v3-native-gui-bundle.mjs](/Users/lbcheng/cheng-lang/v3/experimental/r2c-react-v3/r2c-react-v3-native-gui-bundle.mjs) 这轮已经现场坐实，`native_gui_host_ready/native_gui_renderer_ready` 之前不是“能力没做完”，而是前面已经算出了 `first_batch host ABI + session preview + layout/style/layout plan + render plan + compiled runtime`，最后却又被硬写回 `false`。最稳收法不是改 report 文案，而是像这轮这样直接按这些真产物现算 readiness。 |
| 新发现 | `r2c-react-v3` 这条“Node 只做最薄 helper”主线，当前最该先收口的不是硬删还在 live 的 `exec_surface_helper`，而是把活跃 helper 面正式写进状态合同。像这轮这样在 [r2c_react_v3_controller_main.cheng](/Users/lbcheng/cheng-lang/v3/src/tooling/r2c_react_v3_controller_main.cheng) 里固定 `node_helper_surface_mode=cheng_controller_min_surface_v1`、`active_node_helper_count=12`、`retired_node_helpers=[native_gui_runtime_helper]`，后面的 gate 才能硬卡“哪些还是现役，哪些已经退役”。 |
| 新发现 | fresh-clean gate 不能拿 controller report 去验 bundle 内部字段。像这轮 `runtime_mode` 就只在 `/tmp/r2c-native-gui-fresh-clean-gate-home/native_gui_bundle_v1.json` 里，`native_gui_bundle_report_v1.json` 没这个键；最稳口径是 gate 直接对产物本体下手，不要拿二次整理报告当唯一真值。 |
| 新发现 | 这轮 Linux object 主线的真 blocker 不是 [system_helpers_backend.cheng](/Users/lbcheng/cheng-lang/src/std/system_helpers_backend.cheng) 还没编通，而是 ordinary compiler 请求面把 `--emit` 锁死成了 `exe`。现场已经坐实：`run-linux-object-smokes` 的 compile log 直接报 `v3 compiler: unsupported --emit: obj`；修法已经落到 [compiler_request.cheng](/Users/lbcheng/cheng-lang/v3/src/tooling/compiler_request.cheng) 和 [compiler_main.cheng](/Users/lbcheng/cheng-lang/v3/src/tooling/compiler_main.cheng) 的统一枚举解析。 |
| 新发现 | [system_helpers_backend.cheng](/Users/lbcheng/cheng-lang/src/std/system_helpers_backend.cheng) 当前真实状态已经不是“还剩 40 个 lowering 缺口”，而是 host object 直编已过。`stage3 system-link-exec --emit:obj` 现在已经能直接产出 `system_helpers_backend.hosttest.arm64-apple-darwin.o`；后面再追这条线，应该把注意力放回请求面和 gate，而不是继续假设 provider 本体没编通。 |
| 新发现 | 仓里的 `run-linux-object-smokes` 目前只覆盖 `aarch64-unknown-linux-gnu`，不能代替 `x86_64-unknown-linux-gnu` 验收。这轮已经补跑并坐实：[chain_node.o](/Users/lbcheng/cheng-lang/artifacts/v3_chain_node_obj/x86_64-unknown-linux-gnu/chain_node.o) 和 [rwad_bft_state_machine.o](/Users/lbcheng/cheng-lang/artifacts/v3_rwad_bft_state_machine_obj/x86_64-unknown-linux-gnu/rwad_bft_state_machine.o) 都已产出 ELF 头 `7f454c46`。 |
| 新发现 | [os_host_process.cheng](/Users/lbcheng/cheng-lang/src/std/os_host_process.cheng) 这轮真红点不在底层 process bridge，而在 `execFileCapture` 自己的包装形状。现场已经被两条 smoke 坐实：底层 [host_process_pipe_spawn_bridge_smoke.cheng](/Users/lbcheng/cheng-lang/v3/src/tests/host_process_pipe_spawn_bridge_smoke.cheng) 和 API 层 [host_process_pipe_spawn_api_smoke.cheng](/Users/lbcheng/cheng-lang/v3/src/tests/host_process_pipe_spawn_api_smoke.cheng) 都是绿的，只有 `execFileCapture` 红；最稳收法就是把它改成 `fill helper + 薄对象返回`，不要在这个大函数里直接绑 `ExecFilePipeSpawnResult` 复合局部。 |
| 新发现 | host smoke 一旦牵涉 [artifacts/v3_hostrun](/Users/lbcheng/cheng-lang/artifacts/v3_hostrun) 里的旧产物，先删对应 binary/log/object 再下结论。这轮 `host_process_exec_file_capture_smoke` 已现场坐实：源码已经变了，但旧 hostrun 二进制还能继续把上一次的错误信息带出来；最稳口径是定向 `rm` 后 fresh 重编。 |
| 新发现 | [system_helpers_backend.cheng](/Users/lbcheng/cheng-lang/src/std/system_helpers_backend.cheng) host object 直编这轮已经现场坐实，真 blocker 不是剩余 C 边界，而是 seed 当前对几类 provider 语义形状不稳：泛型 `load/store`、跨模块 `cheng_bytes_* / cheng_seq_*`、`if` 分支多语句命令拼接，以及 `ptr("literal")` / 字符字面量写入。最稳收法就是像这轮这样全部改成定型 helper、本地 helper 和 ASCII 数字直写。 |
| 新发现 | `system_helpers_backend` 这类 provider 文件只要 `stage3 system-link-exec --emit:obj` 单独过了，还不算真正收口；必须立刻再跑 `bootstrap_bridge_v3.sh`、`build_backend_driver_v3.sh`、`verify_backend_ffi_handle_sandbox.sh` 和 `host-bridge-audit`。这轮已经现场坐实，只有这样才能确认修补没有把 ordinary driver 和 `@ffi_handle` 主线重新打坏。 |
| 新发现 | [os_host_process.cheng](/Users/lbcheng/cheng-lang/src/std/os_host_process.cheng) 之前那套 `cheng_pipe_spawn/cheng_pty_spawn + var out` 形状，不只是“不稳”，而是当前纯 Cheng v3 live provider 根本没这组老符号。真收法已经坐实：普通层全切到 `*_bridge`，状态单独返回，`pid/fd/eof/exitCode` 全走固定 getter 槽。 |
| 新发现 | process spawn 这条 `importc/exportc` 边里，`pid:int64` getter 目前会漂。现场已经坐实，同一套 provider/export 只要换成 `cheng_*_last_pid_i32()` 再在 Cheng 里转 `int64`，`pid missing` 就消失；所以这条边先按 `i32 pid getter` 固化，不再赌 `int64`。 |
| 新发现 | stdout-only capture 的 shell 包装，`(\"... \") 2>/dev/null` 不是等价写法。这轮 [host_process_exec_file_capture_smoke.cheng](/Users/lbcheng/cheng-lang/v3/src/tests/host_process_exec_file_capture_smoke.cheng) 已现场打出 `127`；直接把 `2>/dev/null` 追加到命令尾才稳定。 |
| 新发现 | [rand.cheng](/Users/lbcheng/cheng-lang/src/std/crypto/rand.cheng) 之前虽然声明了 entropy import，实际上没走 provider，所以“熵已收回 pure Cheng”是假完成。现在已经补齐 [program_support_backend_v3.cheng](/Users/lbcheng/cheng-lang/v3/src/runtime/program_support_backend_v3.cheng) 导出和 [system_entropy_smoke.cheng](/Users/lbcheng/cheng-lang/v3/src/tests/system_entropy_smoke.cheng) 真跑，后面这类能力必须带 smoke 结案。 |
| 新发现 | `host-bridge-audit` 这条线不能再把 `remaining_non_pure_cheng_boundary` 写死成旧字符串。当前最稳口径已经坐实：按 live provider 真导出的 `process/fs/socket/entropy` 现算 readiness，只有这样 `pure_cheng_zero_c_ready=1` 才是真的。 |
| 新发现 | `@ffi_handle` annotated import 真缺的不是 runtime object，而是 ordinary lowering 少了两段正式语义：一是 `@importc` resolver 不能跨过额外注解行丢状态，二是 release 这类消费型句柄必须跟仓库现有 `@ffi_handle_consume(argN)` 对齐。像这轮 [ffi_importc_handle_annotated_i32.cheng](/Users/lbcheng/cheng-lang/tests/cheng/backend/fixtures/ffi_importc_handle_annotated_i32.cheng) 先后现场打过“raw symbol unresolved”和“stale trap 空日志”两种红灯，真收法已经坐实：seed 侧补 `resolve/register/invalidate`，gate 侧分成 annotated success + annotated stale trap 真编真跑。 |
| 新发现 | `cheng.stage3 system-link-exec` 直调路径之前没有正式的额外链接输入入口，所以把 `BACKEND_RUNTIME_OBJ` 塞环境变量是无效的；这轮已经现场坐实，annotated raw bridge 只有在 seed 真吃到 `--link-input:<obj>` 后才会进 native link。最稳口径就是保留这个显式 CLI，而不是继续赌 wrapper 环境变量能穿透到直调 binary。 |
| 新发现 | [system_helpers_backend.cheng](/Users/lbcheng/cheng-lang/src/std/system_helpers_backend.cheng) 现在编不出 host runtime object，真根已经不是宿主边界没清完。`host-bridge-audit` 这轮前台真实输出已是 `remaining_non_pure_cheng_boundary=none`；当前 compile 红灯集中在 40 个 backend 语义缺口，第一批共性是 `load/store` 标量化、`cheng_seq_get/set`/`cheng_bytes_*` 调用解析、条件表达式复合实参，以及若干 `stmt_call/stmt_if/stmt_let` 形状。 |
| 新发现 | `str[]` 形参本身不是这条 direct argv bridge 的真 blocker。现场已经坐实，普通 `importc/exportc` 走 `str[]` 可以编也可以连；真根在于 live provider 之前根本没公开 `cheng_v3_exec_program_capture_bridge` 这类符号。最稳收法就是像这轮这样把桥直接落进 [program_support_backend_v3.cheng](/Users/lbcheng/cheng-lang/v3/src/runtime/program_support_backend_v3.cheng) 的 pure Cheng provider，而不是继续赌隐藏 native C 文件会自动进 link plan。 |
| 新发现 | [cheng_safe_read_stream(...)](/Users/lbcheng/cheng-lang/v3/src/runtime/program_support_backend_v3.cheng) 不是“读完整文件”，只是“空 stream 时兜底 stdin”。这轮把它误当 file reader 后，bridge 实际返回的是 `FILE*`，dispose 当场 `Abort trap`；最稳口径是直接用 [cheng_read_text_file(...)](/Users/lbcheng/cheng-lang/v3/src/runtime/program_support_backend_v3.cheng) 或自己 `fread`，不要再拿这个 helper 做读文件语义。 |
| 新发现 | provider raw cstring 一旦需要跨模块回到普通 Cheng 代码，就不能再偷懒用 `strFromCStringBorrow(...)`。这轮最稳口径已经坐实：`strFromCStringCopy(...) + explicit dispose` 才能同时避开泄漏和误释放；borrow 只适合生命周期明确受控、且调用方绝不会再 free 的只读指针。 |
| 新发现 | ordinary 这条链之前不是“`os_host_process` 只能降级”，真根是 [cheng_v3_seed.c](/Users/lbcheng/cheng-lang/v3/bootstrap/cheng_v3_seed.c) 的 `importc` resolver 只会认单行签名；像 [os_host_process.cheng](/Users/lbcheng/cheng-lang/src/std/os_host_process.cheng) 里跨行声明的 `cheng_pipe_spawn/cheng_pty_spawn`，会被直接打成 `scalar call resolve failed`。最稳收法就是像这轮这样让 seed 先把多行签名拼完整，再继续用正常跨行声明。 |
| 新发现 | [system_helpers_backend.cheng](/Users/lbcheng/cheng-lang/src/std/system_helpers_backend.cheng) 这种 provider stub 里，`store(outX, -1/1/0)` 不要再把字面量直接塞进去。旧 Linux nolibc ordinary 日志已经坐实，它会把这类调用误判成坏参数；最稳口径是先落本地标量，再 `store(outX, value)`。 |
| 新发现 | `r2c-react-v3` controller 这条主线当前不能硬拗 `direct argv + ptr/seq FFI`。这轮 ordinary 已现场坐实：`cheng_exec_file_pipe_spawn(filePath, argvSeq, envSeq, ...)` 会直接卡在 `scalar call resolve failed`；最稳口径是 controller 继续保持 Cheng 主控，但先把执行边界收成独立 [r2c_process.cheng](/Users/lbcheng/cheng-lang/v3/src/tooling/r2c_process.cheng)，内部只用 ordinary 已经稳定的 `std/os.execCmdEx`。 |
| 新发现 | `node` 解析别再赌 `command -v` 这种 shell builtin。在 controller 实测里它已经打出过 `missing executable: node`，但现场 `PATH` 里明明就有 `~/.nvm/.../node`；最稳口径是直接读 `PATH` 逐项找真实文件。 |
| 新发现 | [os_host_process.cheng](/Users/lbcheng/cheng-lang/src/std/os_host_process.cheng) 这种基础层如果临时试验过 pipe-capture 版本，收尾前必须拉回 ordinary 能编的稳定形状，不然下一次谁一 import 就会在编译期炸。当前最稳口径就是 `execFileCapture -> execCmdEx`、`execFileStatus -> execCmdStatus`。 |
| 新发现 | `system_helpers.c` 依赖拆分这条线里，最容易误判的是把 `@ffi_handle` ordinary lowering 缺口混成 runtime 依赖没清干净。现场已经坐实：`ffi_importc_handle_sandbox_i32.cheng` 在 `stage3/backend_driver` 下真能编真能跑，说明 `cheng_ffi_handle_*` runtime object 合同已经接通；真正还没接上的是 [ffi_importc_handle_annotated_i32.cheng](/Users/lbcheng/cheng-lang/tests/cheng/backend/fixtures/ffi_importc_handle_annotated_i32.cheng) 这条 annotated import，在 `stage3/backend_driver` 下都会稳定报 `scalar call resolve failed ... rawHandleNewI32`。最稳收法就是把 gate 拆成“两段真相”：runtime 语义真跑、注解源码合同硬卡，并把 lowering 缺口单独记账。 |
| 新发现 | 这轮 [system_helpers_backend.cheng](/Users/lbcheng/cheng-lang/src/std/system_helpers_backend.cheng) 里的 `type DarwinDirentPtr = DarwinDirentPtr` 自递归别名已经坐实会把 stage3 编 [system_helpers_backend.cheng](/Users/lbcheng/cheng-lang/src/std/system_helpers_backend.cheng) 直接拖进栈爆和失控内存；修成 `DarwinDirent *` 以后，编译器就从“吃到 300GB+ 然后重启”回到“干净报 unsupported lowering”。以后这类 native pointer alias 一定先查 typedef 展开，不能让自引用溜进主线。 |
| 新发现 | 这种“纯 Cheng 收口”任务不能只信文档和上一次口头结论，必须先扫源码真状态。这轮已经现场坐实：[peer_id.cheng](/Users/lbcheng/cheng-lang/v3/src/libp2p/core/peer_id.cheng) 和 [program_support_backend_v3.cheng](/Users/lbcheng/cheng-lang/v3/src/runtime/program_support_backend_v3.cheng) 会漂回旧 `cheng_v3_base58btc_*` 桥；最稳口径是每次继续主线前先 `rg` 真符号，再决定要不要补 patch。 |
| 新发现 | `host-bridge-audit` 当前还不能写成“零 C 已完成”。这轮真实前台输出已经钉死：旧 `exec_file_*` 桥都没了，但 `remaining_non_pure_cheng_boundary=os_process_fs_socket_entropy_signal`，所以结论只能是“宿主边界已经收窄到这组能力”，不能谎报 `pure_cheng_zero_c_ready=1`。 |
| 新发现 | `v3` provider 只要已经公开了 `std/os fileExists/fileSize/getcwd` 这类宿主能力，就不能漏掉 `cheng_file_mtime`。这轮 `build-backend-driver` 链接 `_cheng_file_mtime` 直接掉红，真根不是业务代码，而是 [program_support_backend_v3.cheng](/Users/lbcheng/cheng-lang/v3/src/runtime/program_support_backend_v3.cheng) 少了和 [system_helpers_backend.cheng](/Users/lbcheng/cheng-lang/src/std/system_helpers_backend.cheng) 同口径的 Darwin `stat -> st_mtimespec.tv_sec` 导出。 |
| 新发现 | `build-backend-driver` 不能再让正在运行的 ordinary driver 直接自举自己。像这轮 `r2c-react-v3 status/native-gui-runtime` 里触发的 fresh 保证，如果继续走 `backend_driver/cheng build-backend-driver`，会反复踩到 ordinary 还没吃稳的 provider/helper 形状；最稳口径是像这轮这样让 [compiler_main.cheng](/Users/lbcheng/cheng-lang/v3/src/tooling/compiler_main.cheng) 优先回稳定 `v3CompilerBridgeCompilerPath(...)`，并把 build-plan comparable 收回原文对比。 |
| 新发现 | `bootstrap-bridge` 真要脱 shell，最稳口径不是把 seed cold-start 也硬塞进 pure Cheng，而是把 shell 压到只剩 `seed/stage1` freshness 和 cold-start 分发；只要 live `stage3` 已经够新，真正的 stage 候选安装、contract 对拍和 env/snapshot 落盘就全部交给 pure Cheng `compiler_main bootstrap-bridge`。 |
| 新发现 | `bootstrap-bridge` 这条链和 `build-backend-driver` 一样，必须走 candidate artifact 安装。最稳口径是 `stage0.next -> stage1.next -> stage2.next -> stage3.next -> stage2/stage3 contract compare -> rename install`；直接原地覆盖 live `cheng.stage*` 会把固定点和失败回滚一起搞脏。 |
| 新发现 | `exec_file_pipe_spawn` 这条桥在 live Cheng 路径里已经是死代码，继续留 `os_host_process import -> program_support_backend export -> native import` 只会制造假“依赖还在”。最稳收法是把整条桥物理删掉，再用 `host-bridge-audit` 硬卡它不能漏回。 |
| 新发现 | shell 里的 freshness helper 不能写成 `[ ! "$src" -nt "$bin" ]`。这轮 [build_backend_driver_v3.sh](/Users/lbcheng/cheng-lang/v3/tooling/build_backend_driver_v3.sh) / [bootstrap_bridge_v3.sh](/Users/lbcheng/cheng-lang/v3/tooling/bootstrap_bridge_v3.sh) 已现场坐实，`sh` 会把它吃成歪表达式，结果旧 `backend_driver/stage3/bootstrap_driver` 一直被误判为 fresh；最稳口径就是显式写成 `if [ "$src" -nt "$bin" ]; then return 1; fi`。 |
| 新发现 | ordinary `compiler_main` 里这种长串 shell 命令，不要再赌多行 `\"...\" + ...` 表达式会被 seed/ordinary 一致发射。像这轮 `planCompareCommand`，旧写法直接把 `+ v3CompilerQuoteShell(...) +` 当字面量塞进命令；最稳口径是逐段 `var cmd = ...; cmd = cmd + ...` 累加。 |
| 新发现 | 用户目标已经明确成“零新增 C 依赖、只收 pure Cheng 主线”时，`execFile` 这类缺口不能再回头补 [system_helpers.c](/Users/lbcheng/cheng-lang/src/runtime/native/system_helpers.c) 新桥。最稳口径是像这轮这样继续留在 pure Cheng provider，必要时把业务侧桥接点改回现成 Cheng 实现，而不是再扩一层 raw C ABI。 |
| 新发现 | [peer_id.cheng](/Users/lbcheng/cheng-lang/v3/src/libp2p/core/peer_id.cheng) 这种协议层模块不该继续依赖 `base58btc` provider bridge。仓里已经有现成的纯 Cheng [multibase.cheng](/Users/lbcheng/cheng-lang/src/std/multiformats/multibase.cheng)；直接切过去以后，`backend_driver/controller` 从干净产物重建就不会再被旧 bridge 口径或 typo 拖住。 |
| 新发现 | `native-gui-bundle` 的 controller 超时固定写成 `30000ms` 不是真 fail-fast，而是假红灯。`home_default/content_detail` 这轮已经坐实，fresh bundle 在纯 Cheng 主链下正常可能超过 30 秒；最稳口径是把这条 budget 提到硬值 `120000ms`，让失败只反映真实逻辑错，不反映预算过小。 |
| 新发现 | `publish_selector` 这轮最开始那次 `native_gui_launcher_compile_failed` 并不是稳定逻辑回归。相同 `system-link-exec` 命令我单独抽出来在同一 out-dir 直接重放，随后 fresh 新目录 bundle 也直接通过，说明这类红灯先要用“同命令单独重放 + 全新 out-dir 再跑一次”排除旧中间态干扰，不能立刻误判成源码层缺口。 |
| 新发现 | `build-backend-driver` 这条链真正稳定的形状，不是继续走 seed 自己的特殊命令，而是像这轮这样把冷启动收成两步：先用 `cheng.stage3 system-link-exec` 编出 `cheng.bootstrap`，再把后续 `status/print-build-plan/zero-exit smoke/install` 全部交给 pure Cheng `build-backend-driver`。这样冷启动和日常自举终于走成同一条语义面。 |
| 新发现 | `backend_driver` 自己重建自己时，不能原地覆盖正在运行的 `cheng`。最稳口径是先编到 `cheng.next`，跑完 `status/print-build-plan/zero-exit smoke` 后再按 `rename map -> rename binary` 安装；否则这条链最容易在自覆盖和 `.v3.map` 漂移上出不稳定症状。 |
| 新发现 | `build-backend-driver` 的 plan 对拍，如果原始 plan 文本已经全等，就不要再额外发明行过滤扫描器。像这轮那段自定义裁剪逻辑，最后只制造了 `idx=len` 的 bounds crash；最稳口径就是直接比原文。 |
| 新发现 | `r2c-react-v3` 这条主线一旦 ordinary 已经吃得下完整 `native_gui_runtime_v1` JSON builder，就不要再保留任何 runtime helper 作为活路。最稳口径就是像这轮这样让 controller 直接执行 `native_gui_runtime_compiled_main`，`status` 明文写成 `native_gui_runtime_helper=""`，并把旧原生 helper 文件物理删除；否则后面一定会有人把旧桥再接回 active runtime 面。 |
| 新发现 | `backend_driver` 既然已经是真 ordinary compiler，`cheng_v3.sh` 的用户面编译命令就不能继续默认分派给 seed `stage3`。最稳口径是像这轮这样把 `status / print-build-plan / emit-csg / migrate-csg / verify-world / world-sync / prove-equivalence / prove-migration / publish-world / fresh-node-selfhost / selfhost-build / system-link-exec` 全部切到 fresh `artifacts/v3_backend_driver/cheng`，让 seed `stage3` 只留在 bootstrap 和 gate 边界。 |
| 新发现 | `build-backend-driver` 现在可以正式挂进 `backend_driver`，但外层薄壳不能直接递归地“先确保 backend driver fresh，再调用 backend driver”。当前稳定边界已经收成：wrapper 先看现成 report 标记，已有 fresh pure Cheng driver 就直调；否则只负责冷启动 `cheng.bootstrap`，后续立即切回 pure Cheng `build-backend-driver`。 |
| 新发现 | `bootstrap_bridge_v3.sh` 不能只要看到旧 `cheng.stage3` 在就直接复用。像这轮 [cheng_v3_seed.c](/Users/lbcheng/cheng-lang/v3/bootstrap/cheng_v3_seed.c) 已经改了 `build-backend-driver`，但旧 wrapper 还会继续跑旧 `stage3`，导致新 seed 命令实现永远进不了 live 主线；最稳口径是 `seed_source/stage1_source` 只要比 live `stage3/stage0` 新，就必须直接回 seed runner 真重建。 |
| 新发现 | `backend_driver` 真收成 ordinary `compiler_main` 以后，拿它的 `print-build-plan` 去和 bootstrap `stage3` 做整文件全等，对拍一定会误伤。当前 [reference_stage3.plan.txt](/Users/lbcheng/cheng-lang/artifacts/v3_backend_driver/reference_stage3.plan.txt) 会额外带 `supported_targets/target_support*` 诊断行，而 ordinary [build_backend_driver_v3.plan.txt](/Users/lbcheng/cheng-lang/artifacts/v3_backend_driver/build_backend_driver_v3.plan.txt) 只有 build plan 核心字段；最稳口径是只比 `target/linker/stage2_compiler/entry/output/source[*]`。 |
| 新发现 | `build_backend_driver_v3.report.txt` 如果还写 `materialized_source=*stage1_bootstrap.cheng`，那说明这条链还没真把 `backend_driver` 当 ordinary compiler 物化物。当前正确收法已经坐实：report 必须写 `materialized_source=*compiler_main.cheng`，并同时给出 `reference_plan_log` 和 `zero_exit_smoke_*`，这样才能证明它不是旧 bootstrap 壳。 |
| 新发现 | `compiler_request.v3CompilerReadFlagOrDefault(...)` 当前不能直接当 ordinary selfhost 二进制里的关键命令解析面。它内部那种 `key + ":"` / `key + "="` 形状已经现场打出过 `v3 compiler: unsupported --emit: --root:/...`；最稳收法是像这轮这样让 `system-link-exec/selfhost-build` 直接从 runtime `cmdline.readFlagOrDefault(...)` 读 `--in/--root/--out/--target/--emit/--report-out`。 |
| 新发现 | 当前 ordinary runtime argv 会多暴露一个尾部空参数。只要直接把 `paramStr(i)` 全量转发给 bridge 编译器，命令面就会无意义漂一格；最稳收法是像这轮这样在 `v3CompilerCollectArgsFrom(...)` 里把空尾参数过滤掉。 |
| 新发现 | `std/os` 这层 `os.execFileStatus/os.execFileCapture/quoteShell` 当前不适合直接拿来做 ordinary selfhost compiler 的 bridge 命令面。现场已经打出过 `sh: \" + value + \": command not found`，真根就是命令文本组装时的链式字符串拼接不稳；最稳收法是像这轮这样在 [compiler_main.cheng](/Users/lbcheng/cheng-lang/v3/src/tooling/compiler_main.cheng) 本地逐段拼接 shell 命令，并自己做最小单引号包裹。 |
| 新发现 | 当前 `v3` selfhost 编译器最稳的执行形状，不是继续停在“plan/report 已好，native link not implemented”，而是“本地 pure Cheng 先落 deterministic plan/report，再把真执行交给 live Cheng compiler”。这轮已经用 [ordinary_zero_exit_fixture.selfhost](/Users/lbcheng/cheng-lang/artifacts/v3_selfhost_compiler_verify/ordinary_zero_exit_fixture.selfhost) 现场坐实：selfhost compiler 可真编出 exe、report、`.v3.map`，而且目标 exe 可直接退出 `0`。 |
| 新发现 | `native-gui-runtime` 一旦已经从 Node helper 切到原生 helper，就不要把退役 `.mjs` 和 shared 里的 JS evaluator 留在仓里当“可选旧路”。这类死代码后面最容易被误接回 active runtime 面；最稳口径是像这轮这样物理删入口文件，并把 shared 里只服务它的 `reduce-kv -> runtime/render-plan` JS evaluator 整段删掉。 |
| 新发现 | `r2c-react-v3 native-gui-runtime` 如果还挂在 `r2c-react-v3-native-gui-runtime-helper.mjs` 上，那 direct runtime、fresh bundle、runtime launcher 和 shell wrapper 就都还残着 Node 运行面。当前最短真收法已经坐实：用一个原生 helper 直接执行 `native_gui_runtime_compiled_main`，遇到 JSON 就透传，遇到 `native_gui_runtime_reduce_kv_v1` 就按 runtime contract 的 `native_layout_plan/preview_fields/theme` 原地重建 `native_gui_runtime_v1 + native_render_plan_v1`；然后 controller、bundle launcher 和 shell wrapper 一起只调用这层原生 helper。 |
| 新发现 | `r2c-react-v3` 一旦已经把 direct runtime 收回 `cheng.stage3 r2c-react-v3 native-gui-runtime`，就不要再让 `native-gui-bundle.mjs` 本地保留一套 `reduce-kv -> native_gui_runtime_v1` JS 重建。最稳口径是像这轮这样让 bundle 只负责编译 runtime、写 contract，再直接调用 controller 取回初始 `native_gui_runtime_v1`，并归档 `native_gui_runtime_main.controller.log`；这样 fresh bundle、direct runtime 和 `run-native-gui` 才会共用同一份 runtime JSON 真值。 |
| 新发现 | `r2c-react-v3` 原生 GUI 真要把 Node 从实时主链里清出去，最短真收法不是继续让宿主调 helper 拼 JSON，而是让宿主直接执行 `native_gui_runtime_compiled_main`，在原生侧消费 `native_gui_runtime_reduce_kv_v1`。这轮已经坐实：只要宿主自己按 `native_layout_plan_v1.items` 回填 `selected/focused/source` 并重建 `native_render_plan_v1`，`publish_selector` 的 click/focus/text/key/source jump 和 `content_detail` 的 resize/scroll 都能稳定通过，而且不再经过 Node helper。 |
| 新发现 | `native_gui_runtime_exe_path` 一旦继续指向 launcher/helper，原生 GUI 的调度权就还在 Node 那边。最稳口径是像这轮这样把 session/runtime 合同正式切到 `native_gui_runtime_compiled_main`，同时把旧壳只保留成 `native_gui_runtime_launcher_exe_path`；这样 direct runtime 兼容面还在，但实时宿主入口已经回到 Cheng compiled runtime。 |
| 新发现 | `v3` 的 live 自举一旦已经有 `cheng.stage3` 或 `artifacts/v3_backend_driver/cheng`，就不该再在日常刷新里先跑一次 `cc -> cheng.stage0`。这轮最稳口径已经坐实：active build plan、status/report 和 bootstrap env/snapshot 都切到 `v3_selfhost`，bootstrap 本体直接拿现成 Cheng 编译器物化 `stage0` 再重编 `stage1 -> stage3`。但当前 `stage3/backend_driver` 仍是 seed 派生产物，所以 `seed_source` 还必须留在 bootstrap freshness 依赖里；真正能删掉它的前提，是这两个 live 编译器先脱离 seed 代码体。 |
| 新发现 | `r2c` controller 这条链之前虽然实际编译已经走纯 Cheng provider，但 seed 的 freshness 输入还残着 `v3_program_argv_native.c`。这类“不是链接输入、但还在 freshness/辅助路径里点名旧 C 文件”的残余也必须清掉；否则下次有人看 seed 或追 freshness 依赖时，会误以为 live 主线还在吃 C provider。 |
| 新发现 | `verify-debug-runtime` 如果还去读 [system_helpers.c](/Users/lbcheng/cheng-lang/src/runtime/native/system_helpers.c) 做 debug 旁路检查，就说明这条验收口径还没彻底收回纯 Cheng 主线。最稳做法是像这轮这样只验纯 Cheng provider 源、provider object 合同和真实运行输出，不再把宿主 C runtime 文件卷进 debug gate。 |
| 新发现 | 纯 Cheng provider object 当前不会只导出 `@exportc` ABI，所有顶层 helper 和全局槽也会一起进 object 符号表。所以 `verify-debug-runtime` 不能再沿用旧 C runtime 的“只有 8 个导出”合同；最稳口径是像这轮这样把合同收成“两层”：公开 ABI 必须正好是 crash/profile/line-map 那 8 个导出，额外导出如果存在，必须全部落在 `cheng_v3_runtime_debug_runtime_stub_v3__*` 模块私有前缀下。 |
| 新发现 | `verify-debug-runtime` 里 Darwin debug provider 的未定义白名单，也必须按纯 Cheng object 的真实输出收，而不是沿用旧 `system_helpers_debug_trace_profile.c` 时代的猜测。当前 object 已现场坐实会落 `_cheng_bounds_check` 和 `_fopen`，不会再出现 `__exit` 或 `_fopen$DARWIN_EXTSN`；白名单如果不追平，只会把纯 Cheng 主线误报成回归。 |
| 新发现 | `r2c-react-v3` 的长内容原生 GUI 路由不能再把所有 `R2cNativeGuiItem` 直接堆进一个 `r2cNativeGuiItems()`。这轮 `content_detail` 已现场坐实：一到 `item_64`，ordinary 就会在 `prepare binding infer type failed` / `primary_object_body_semantics_missing` 上炸掉；最稳口径是把 item 生成拆成分块 append helper，每块把 composite 局部变量数压在稳定范围内。 |
| 新发现 | `open-source-on-click` 的真根不在命中链，而在宿主状态机。`publish_selector` 这轮已经坐实：click 明明已经命中 `node_13 -> app/components/PublishTypeSelector.tsx:39`，但后续 focus 事件又把 `source_jump_*` 清空，Node 最终误判成 `native_gui_source_jump_failed:unknown`；最稳口径是 [native_gui_host_macos.m](/Users/lbcheng/cheng-lang/v3/experimental/r2c-react-v3/native_gui_host_macos.m) 只在新 click 时重置 jump 状态，后续非点击 runtime 事件不再擦掉上一次 click 的源码跳转结果。 |
| 新发现 | [program_support_backend_v3.cheng](/Users/lbcheng/cheng-lang/v3/src/runtime/program_support_backend_v3.cheng) 里 `cheng_exec_cmd_ex` 这类 tooling/provider exec helper，当前 ordinary 对“嵌套 `if` + 连续 `cheng_bytes_copy` + 偏移累加”形状不稳。最稳收法是像这轮这样把命令拼接改成 raw concat 直线链；只要继续赌原来那种 `bytes_copy + pos += ...` 形状，controller provider 很容易重新掉进 lowering 红灯。 |
| 新发现 | `cheng.stage3 r2c-react-v3 native-gui-runtime` 单独入口这轮还没闭合。当前主线 `native-gui-bundle`、`run-native-gui`、`content_detail`、`publish_selector` 都已前台通过，但 direct `native-gui-runtime` 仍会在 controller 调 helper 前段错，且连 `native_gui_runtime_direct_v1.json` 都来不及落地；这说明红点还在 controller 的 `os.execCmdEx -> node helper` 单命令路径，不在 bundle/runtime 业务语义本体。 |
| 新发现 | `native_gui_host_macos.m` 里 scripted scenario 不能再借真实 AppKit 输入事件和 `windowDidResize` 间接驱动 runtime。刚才这轮已经坐实：fresh `native-gui-bundle` 明明全绿，但组合 `run-native-gui` 会漂成 `resize=1`、`scroll/click/focus/text=0`、`key=3`，真根是 scripted resize 走了窗口回调，真实键盘事件又把自动场景计数污染。最稳口径就是像这轮这样让 scripted resize 直接调 runtime，并在 scripted scenario 期间屏蔽真实 `mouseDown/scrollWheel/keyDown`；这样 fresh 组合场景才稳定回到 `1/1/1/1/1/1`。 |
| 新发现 | `r2c-react-v3-native-gui-run.mjs` 这条链如果自动补 session 还去回调旧 shell wrapper，并且强行设 `R2C_REACT_V3_NO_STAGE3_HANDOFF=1`，那“Cheng 主控”其实只收回了一半。最稳口径是像这轮这样让 `run-native-gui` 直接调 `cheng.stage3/backend_driver r2c-react-v3 native-gui-bundle`，把自动补产物也拉回同一条 controller 主线；旧 wrapper 只保留 fallback，不再做主控入口。 |
| 新发现 | `r2c-react-v3` 原生 GUI 想继续往“Cheng 主控，Node 最薄 helper”推进，最短真路径不是继续硬拗 ordinary `str/composite` lowering，而是先把宿主 executable 收回 `cheng.stage3 r2c-react-v3 native-gui-runtime`。这轮已经坐实：bundle 生成 `native_gui_runtime_contract_v1.json + native_gui_runtime_main` launcher，launcher 真执行 Cheng controller；Node 退回最薄 `runtime-helper.mjs`，只按 contract 求值 JSON。这样宿主入口面和调度权已经回到 Cheng，后面只剩把 helper 逻辑继续往 Cheng 里收。 |
| 新发现 | `r2c-react-v3` 原生 GUI runtime 这条线当前真 blocker 不是 React.js 业务代码，也不是 host 事件面，而是 ordinary `system-link-exec` 还吃不下这类 `str/composite` JSON builder。像这轮 [r2c-react-v3-native-gui-runtime-shared.mjs](/Users/lbcheng/cheng-lang/v3/experimental/r2c-react-v3/r2c-react-v3-native-gui-runtime-shared.mjs) 生成的 Cheng runtime，一到 `r2cNativeGuiEmitQuotedJson(...)`、CLI 读参和 render plan 字符串组装，就会卡 `primary_object_body_semantics_missing` 或把表达式直接落成字面文本；当前最稳收法就是先把 runtime executable 收成受控 `node_helper_v1`，同时继续生成 Cheng runtime source 归档，等 ordinary string/composite lowering 补齐后再切回纯 Cheng。 |
| 新发现 | 原生 GUI 一旦进入 runtime 事件回环，就不能只把新状态散落在 session 或 env。当前正式合同必须是 `native_gui_runtime_v1 + native_render_plan_v1 + native_gui_runtime_state_v1` 三件套：host 每次 `click/scroll/resize/focus/key/text` 后都重新调 runtime executable，直接拿回新的 state 和 render plan；这样 bundle、host、wrapper、`cheng.stage3`、`backend_driver` 才共享同一份真值。 |
| 新发现 | 事件回环验收不能把 `resize/scroll/focus/text/key` 和 `click hit` 永远混在一条坐标链里。像这轮一条链上先 resize 再 scroll 后，原坐标就可能自然 miss；最稳口径是分成“两条真验收”：一条专门钉 `resize/scroll/focus/text/key -> state/render plan`，另一条专门钉 `click -> hit item/source module/inspector`。 |
| 新发现 | `slice-gate` 这次真红点不是 `chain_node` 业务逻辑，而是 Linux `nolibc` runtime provider 没把 `tcp_transport` 现在依赖的 `cheng_v3_buffer_handle_*` 和 handle-returning TCP bridge 一起导出。旧 out-param 形状在 host/旧路径还能混过去，但一到 `aarch64-unknown-linux-gnu` 真链接就会掉 `unresolved ELF symbol: cheng_v3_buffer_handle_len_bridge`；最稳收法就是像这轮这样直接在 [system_helpers_backend_nolibc_linux_aarch64.cheng](/Users/lbcheng/cheng-lang/src/std/system_helpers_backend_nolibc_linux_aarch64.cheng) 补齐同一套 handle ABI。 |
| 新发现 | `payloadText` / `BigInt` 这次都是纯文本级 gate 命中，不是运行语义本身出错。像 [tcp_transport.cheng](/Users/lbcheng/cheng-lang/v3/src/libp2p/transports/tcp_transport.cheng) 里的局部名和 [bio_same_person.cheng](/Users/lbcheng/cheng-lang/v3/src/project/bio_same_person.cheng) 里残留的 `BigInt` 过桥 helper，只要字面还在，`scan-hotpath` 就会先把整条主线打停；最稳收法是 transport 统一走 `payloadWire/requestWire/responseWire` 命名，生物证明链则直接收回固定 P-256 标量算术。 |
| 新发现 | `replay-hit-inspector` 不能依赖第一次 `run-native-gui` 已经现场执行过源码跳转。最稳口径是像这轮这样让 `native_gui_inspector_state_v1.json` 自己直接固化 `source_jump.target_path/target_line`，并且从 `repo_root + hit_source_module_path + hit_source_line` 机械求出；这样 replay 命令和 live 点击共享同一份真状态，不需要再补猜路径。 |
| 新发现 | [r2c_react_v3_controller_main.cheng](/Users/lbcheng/cheng-lang/v3/src/tooling/r2c_react_v3_controller_main.cheng) 这条 controller 热路径不要继续调 `os.dirName(...)`。这轮已经坐实它会在 live `emit composite binding` 上直接炸掉；最稳口径是改成 [v3path.cheng](/Users/lbcheng/cheng-lang/v3/src/std/path.cheng) 的 `v3PathParentDir(...)`，把路径父目录解析收回 v3 标准库。 |
| 新发现 | `run-v2-selfhost-gate` 不能只接进 [gate_main.cheng](/Users/lbcheng/cheng-lang/v3/src/tooling/gate_main.cheng) 和 seed dispatch。只要 [cheng_v3.sh](/Users/lbcheng/cheng-lang/v3/tooling/cheng_v3.sh)、[compiler_runtime.cheng](/Users/lbcheng/cheng-lang/v3/src/tooling/compiler_runtime.cheng) 和 [compiler_runtime_smoke.cheng](/Users/lbcheng/cheng-lang/v3/src/tests/compiler_runtime_smoke.cheng) 没一起追平，外层壳和 host smoke 看到的还是旧命令面。 |
| 新发现 | `quic/msquic` 阶段日志如果只留在 `program-selfhost-check` 的原始 stdout，就没法被正式 gate 和外层工具稳定消费。当前最短真收法已经坐实成 `v2_network_smoke_report_v1`：每个 smoke 固定写 `smoke/stdout_path/prefix.N.{name,count,stages}/stage_total/ok`，然后由 `run-v2-selfhost-gate` 硬校验 `quic=12`、`msquic=60`。 |
| 新发现 | [system_helpers_program_runtime_bridge.c](/Users/lbcheng/cheng-lang/src/runtime/native/system_helpers_program_runtime_bridge.c) 里那几块类型泛型/枚举序号/flag 读取 helper 现在已经完全不在 live 路径上。继续留着只会制造 `-Wunused-function` 噪音；这轮物理删掉后，`cc -std=c11 -O2 -Wall -Wextra -pedantic -c` 已不再报这批新 warning。 |
| 新发现 | 原生 GUI 真要做“点了就跳源码”，跳转动作应该收在宿主，不要回 Node 再猜一次。最稳口径是像这轮这样让 AppKit host 直接拿 `repo_root + source_module_path + source_line` 调 `xed --line`，同时把结果回写成 `native_gui_source_jump_*` 和 inspector panel；这样手点和脚本点击走的是同一条真路径。 |
| 新发现 | 原生 GUI 真要做 inspector，就不要继续把命中结果只散落在 `summary.env/report.json` 里。最稳口径是像这轮这样直接落独立 `native_gui_hit_inspector_v1.json`，里面同时固化 `layout_item + source_node + style_node`；这样后面做源码跳转、白盒调试、原生 inspector 面板都能直接消费正式产物。 |
| 新发现 | `source_node_id` 还不够做真正的 GUI 白盒调试。最稳口径是像这轮这样把 `module_path/component_name/line` 一起带进 `native_layout_plan_v1.items`，再让 host 点击时原样回传；这样原生 GUI 才真正具备“从画面反查 TSX 源码”的闭环。 |
| 新发现 | `quic_tls_transport_ecdsa_smoke` 的真红点不是 stage2 误编，而是 smoke 自己把同一张叶子证书连续验了多遍：先 `direct cert verify`，再 `x509VerifySignature`，最后又 `x509VerifyChain`。当前实现下重复验签会把内存和时延一起放大；最稳收法是把 smoke 收成“原始签名解码检查 + 一次正式 `x509VerifyChain`”，同时把阶段日志写进合同。 |
| 新发现 | `msquic_chain_smoke` 在 `program-selfhost-check` 里不是卡死，而是原生超时定得太短。加上 `client hello` 细分阶段后已经坐实它能稳定走到 `replay_ok` 和最终 `msquic_chain_smoke=ok`；这类 network smoke 必须按真实运行时间设门槛，不能拿 `quic` 的短阈值硬套。 |
| 新发现 | `msquic_chain_smoke` 这次真红点不是共识和回放算法本身，而是 `bad_append` 工位复用了旧块目录。旧 `events.log` 里的重复合法事件会把回放伪装成 `consensus rejected journal event`；最稳收法是先按 `base/_1/_2...` 选 fresh root，再把 reset 失败直接硬报。 |
| 新发现 | `compiler_core` 的 tooling/program 两条执行轨不能再共用同一套 native support source 列表。普通程序继续带 [v2/bootstrap/cheng_v2c_tooling.c](/Users/lbcheng/cheng-lang/v2/bootstrap/cheng_v2c_tooling.c) 只会白白重编大 C；但如果去掉后不同时把 [system_helpers_tooling_entry_bridge.c](/Users/lbcheng/cheng-lang/src/runtime/native/system_helpers_tooling_entry_bridge.c) 编成 `CHENG_TOOLING_ENTRY_NO_BOOTSTRAP` stub，又会直接掉 `cheng_v2c_tooling_handle/is_command` 未定义。当前稳定口径已经收成“tooling 轨保留 bootstrap tooling，program 轨 stub 化 tooling entry”。 |
| 新发现 | 原生 GUI 一旦进入真实滚动，就不能继续让 session 只塞 `viewport_items`。最稳收法是这轮已经落下来的 full `native_layout_plan.items`：宿主自己按 `scroll_offset_y` 做 viewport 裁剪、命中测试和可见项统计，Node/Cheng 只负责给正式 plan。 |
| 新发现 | 布局常量不能继续硬编码在 Node helper 里。当前最稳口径已经改成由 Cheng 预览流直接下发 `layout_policy_source/layout_*`，Node 只做机械 plan 生成；这样后面把布局求解继续收回 Cheng 时，不会再留下第二份真值。 |
| 新发现 | 旧 `cheng.stage0/stage3` 如果是按相对 `CHENG_V3_IMPL_SOURCE_PATH` 构建出来的，自举时生成的 `stage1.generated.c` 会在 `artifacts/v3_bootstrap` 目录里找不到 `./v3/bootstrap/cheng_v3_seed.c`。这轮最短真恢复法已经坐实：先用绝对路径编出来的 `artifacts/v3_bootstrap/cheng.bootstrap_bridge_runner` 重新跑一次 `bootstrap-bridge`，把 stage3 拉正后再回官方脚本入口。 |
| 新发现 | 原生 GUI 这条线从 `style_layout_surface_v1` 再往前推时，不能继续让 `native_gui_session_v1` 自己现场算行布局。最稳收法就是这轮落下来的 `native_layout_plan_v1`：先把 `x/y/width/height/z_index/layer/column_span/scroll_height` 单独写成正式产物，再让 session/host 只消费 plan。 |
| 新发现 | 原生布局计划不能只写“总 item 数”。当前真有用的 live 验收面是 `item_count + viewport_item_count + clipped_item_count + scroll_height` 这一组；少任何一个，用户都无法区分“页面真的很长”和“只是 helper 没把可见项算对”。 |
| 新发现 | 这条移动端 DID 主线不能继续只吃 `verified + feature32` 裸输入。当前最稳口径已经收成 [biometric.cheng](/Users/lbcheng/cheng-lang/v3/src/mobile/hardware/biometric.cheng) 的 typed 指纹授权边界：请求/响应形状固定，宿主桥没接进来就直接报 `fingerprint authorize impl missing`，不会再让主线悄悄带着空值往下跑。 |
| 新发现 | `chain_node` 的地址混合不能继续用 `int32` 硬算。新加的 `bio_did_mobile_biometric_smoke` 这轮真把一个特定 DID 哈到了溢出区间，直接把 [v3ChainNodeAccountAddress(...)](/Users/lbcheng/cheng-lang/v3/src/project/chain_node.cheng) 的 digit 算坏并现场段错；当前已经改成 `int64` 混合，根因已收死。 |
| 新发现 | 这次 `cheng v3` 公网 QUIC/BFT 内存事故已经坐实到纯 Cheng 热路径：前台 `probe-proposal --handshake-only` 在完全没收到服务端回包时，单次失败握手就能打到 `peak memory footprint ≈ 1.35GB`，真根落在 [native_runtime.cheng](/Users/lbcheng/cheng-lang/v3/src/quic/native_runtime.cheng) 的 `msquicNativeDialPumpReady / msquicNativePipeRead` 对非阻塞 UDP `queue empty` 的空转热环，而不是业务层日志、state 文件或 Codex 主进程本体。 |
| 新发现 | `msquicNativePipeClose()` 之前只打 `connection close`，不立刻回收 closed session/datapath。这个形状会把失败连接资源拖到下一次 dial/accept 才清；当前已改成 close 后马上 recycle。 |
| 新发现 | `deploy-bft-validator-three-node` 不能再在 proposal 公网口还没证明能完成 QUIC 握手前就直接放出 detached follower。当前最稳口径已经改成先本机前台 `probe-proposal --handshake-only`，握手不过就直接停链退出，不再让后台重试链条继续放大内存风险。 |
| 新发现 | 这次机器重启当前能拿到的唯一硬记录是 `/Library/Logs/DiagnosticReports/ResetCounter-2026-04-15-235436.diag` 里的 `Boot faults: wdog,reset_in_1`。它能证明发生过 watchdog reset，但不能单独坐实成某个进程的 OOM/jetsam，更不能直接证明就是当前 helper 吃到了 `300GB`。 |
| 新发现 | `r2c-react-v3` 原生 GUI 页面树提取链此前只有 `maxNodes=96` 这一级截断，不足以防整进程失控。当前最稳收法已经落实成五道硬预算：Node 进程自补 `--max-old-space-size=768`，再对 `RSS / parsed modules / component expansions / source chars / nodes` 全部 fail-fast；任何一路超限都直接报 `native_gui_layout_surface_*_budget_exceeded`，不再允许无限扩张。 |
| 新发现 | 原生 GUI 这条线下一步最值钱的不是继续堆更多节点数，而是把 JSX 里的 `className/attr` 线索直接抬成 `layout surface`。这轮已经坐实：只靠源码可静态读到的 class token，就足够给节点补出 `layout_role/style_traits/class_preview`，先把 `overlay/surface/flex/grid/interactive` 看清楚，再去做真正布局引擎，比继续盲画纯标签名有效得多。 |
| 新发现 | `native_gui_session_v1` 当前最稳的展示形状不是一行硬塞所有信息，而是“主标题 + 副标题”双行卡片。这样既保留节点标签，又能直接显示 `flex/bg/border/rounded/...` 这类样式线索，页面树预览就开始接近真正的布局 surface，而不是一棵无语义的 DOM outline。 |
| 新发现 | 当前最稳的下一层不是让 session 直接继续长更多临时 UI 字段，而是把样式判断单独沉成 `style_layout_surface_v1.json`。这轮已经坐实：`visual_role/density/prominence/accent_tone/row_height/column_span_hint/layer` 这组最小 IR 足够支撑原生 GUI 预览的配色、层次和行高，也给后面的 Cheng 原生布局求解留出了正式输入面。 |
| 新发现 | 原生 GUI 新 IR 不能只落在 `bundle.summary.env` 里。`run-native-gui` helper、实验 wrapper 和 `cheng.stage3/backend_driver` live 报告都必须一起透出 `style_layout_surface_*`，不然用户看到的仍然像“能力存在，但 live 入口没追平”。 |
| 新发现 | 当前 `exec_snapshot` 还只有 `semantic_nodes_count/module_count/component_count` 这类计数，没有真实页面树；所以原生 GUI 如果想从“指标卡片”推进到“页面树”，不能继续盯 `exec_snapshot` 猜布局，必须额外消费 `tsx_ast_v1 + route catalog` 去还原首屏 JSX 轮廓。 |
| 新发现 | route-aware 页面树这条线不能手写一套新的 route 判断。最稳做法就是复用 [r2c-react-v3-route-matrix-shared.mjs](/Users/lbcheng/cheng-lang/v3/experimental/r2c-react-v3/r2c-react-v3-route-matrix-shared.mjs) 里已经验证过的 formal route 映射，让 `home_default -> app/App.tsx#AppContent`、`tab_messages -> ChatPage` 这类主面只维护一份真口径。 |
| 新发现 | `layout_surface_v1` 必须是独立产物，不能只藏在截图或 `native_gui_session_v1.json` 里。像这轮这样把 `layout_surface_v1.json` 单独写盘，再把 `native_gui_layout_surface_*` 同步进 `wrapper / cheng.stage3 / backend_driver` 三条报告，后面才能稳定做结构 diff，而不是只剩一张图。 |
| 新发现 | `run-native-gui` 这条原生 GUI 烟雾线，最稳口径不是只看 `window_opened=true`，而是固定带 `--resize WxH --click x,y --wait-after-click-ms <n> --auto-close-ms <n>` 真跑一遍，再同时验 `native_gui_screenshot_written=true` 和真实截图落盘。这样才能确认窗口、输入、截图和报告是同一条活链。 |
| 新发现 | 原生 GUI 点击坐标必须统一记在 `session` 的左上角坐标系里。AppKit 鼠标事件是左下角原点，如果不显式做 `sessionHeight - mouseY` 转换，点击命中和截图里的高亮位置一定会漂。 |
| 新发现 | resize 这条线不能让 host 猜哪些卡片要跟着拉伸。最稳做法是像这轮这样把 `stretch_x/stretch_y` 和 `theme` 直接写进 `native_gui_session_v1.json`，让 AppKit host 只按会话合同拉伸和绘制。 |
| 新发现 | `run-native-gui` 在 `cheng.stage3/backend_driver` 直入口里，当前最稳的 flag 读取面仍然是 `std/cmdline.readFlagOrDefault(...)`。这轮试过直接改吃 controller 局部 `args[]` 后，`--out-dir/--click/--resize` 会串坏成同一路径；当前已经回收。 |
| 新发现 | 当前最稳的原生 GUI host 路径不是先扩 Cheng 布局引擎，而是先让 `run-native-gui` 直接吃 `native_gui_session_v1.json`。像这轮这样用一个极小的 AppKit host 把 `layout_items` 真画出来，能最快把“bundle/session 已有”推进到“窗口真的开了”。 |
| 新发现 | `run-native-gui` 内部如果要自动补 session，不能直接打裸 [r2c-react-v3-native-gui-bundle.mjs](/Users/lbcheng/cheng-lang/v3/experimental/r2c-react-v3/r2c-react-v3-native-gui-bundle.mjs)，因为那个 helper 默认要求 `cheng_codegen_v1.json` 等前置产物已经存在。最稳做法是像这轮这样回调 wrapper 的 `native-gui-bundle` 命令面，让 `frontend/static/codegen/exec` 自动补齐。 |
| 新发现 | 原生 GUI 这条线当前最稳的验收口径是 `auto-close + screenshot`。只看窗口有没有闪一下不够；像这轮这样把 `native_gui_window_opened=true`、`native_gui_screenshot_written=true` 和真实 `native_gui_session.png` 一起落盘，后面才有稳定回归面。 |
| 新发现 | 当前 `v3` native backend 对“大 composite GUI preview 对象”或“长 JSON 字符串直出 stdout”这条路还不稳。最稳形状不是继续硬塞 JSON，而是像这轮这样让 Cheng launcher 只发极小 `key=value` 预览流，再由 Node 做纯机械 JSON 组装。 |
| 新发现 | `native-gui-bundle` 现在的真实里程碑已经不是单纯 `bundle_ready=true`，而是 `session_preview_ready=true`。但这不等于原生 GUI 宿主已经完成，所以 `host_ready=false`、`renderer_ready=false` 仍然必须继续明确写死，不能偷换概念。 |
| 新发现 | 原生 GUI 这条线当前最稳的 launcher 验收口径，是“generated package 真补 compile support -> `system-link-exec` 真编 launcher -> 前台真跑 launcher -> 落 `native_gui_session_v1.json`”。只看 `native_gui_bundle_v1.json` 或只看 helper 自己拼的报告，都不够说明 Cheng 侧入口真的活了。 |
| 新发现 | 三验证人链的 baseline 口径不能让 deploy/bench 各自写死一份。最稳做法是像这轮这样在 [cheng_v3_seed.c](/Users/lbcheng/cheng-lang/v3/bootstrap/cheng_v3_seed.c) 里统一收成 `v3_bft_expected_baseline(...) + v3_bft_wait_cluster_baseline(...)`，让 `height=1` 和 `mint_amount/transfer_amount` 推出来的余额只维护一次。 |
| 新发现 | 这轮 fresh `bench-bft-validator-three-node --tx-count:200 --transfer-amount:1 --max-txs-per-block:32` 的真实口径已经更新为 `elapsed_ms=55973`、`tps=3.573`、`throughput_bytes_per_sec=114`、`final_height=9`、`alice=1`、`bob=233`、`app_hash=81bd40eb75104658879b5d318396e27c81c1d4e83de74b40ab4e4a6412d3ad53`。后续再提这条压测，别再引用上一轮的 `52717 / 3.793 / 121`。 |
| 新发现 | [cheng_v3_seed.c](/Users/lbcheng/cheng-lang/v3/bootstrap/cheng_v3_seed.c) 里 `v3_bootstrap_artifacts_fresh(...)` 的 `inputs` 槽位数必须和真实写入数完全一致。这轮 `dmit` 上 `*** stack smashing detected ***` 的真根不是远端环境漂，而是 `const char *inputs[5]` 却写到了 `inputs[5]`，属于实打实的栈越界。 |
| 新发现 | relay fresh `build-backend-driver` 不能再赌远端旧 `stage3` 还是新鲜的。最稳做法是先把当前 [cheng_v3_seed.c](/Users/lbcheng/cheng-lang/v3/bootstrap/cheng_v3_seed.c) 同步过去，在 `dmit` 上现编一个临时 `cheng.remote_seed_runner`，再由它执行 `build-backend-driver`，最后用 fresh `backend_driver` 去编 `x86_64` `bft_state_machine`。 |
| 新发现 | 三验证人 fresh deploy 的初始 `mint + transfer` 不能放在 proposer 启动后。只要先起 proposer，再入 `server.queue`，第一块就可能先空提交，整条 baseline 会漂到 `height=2`；这组初始交易必须先写队列，再起 proposer/follower，第一块才能稳定就是业务块。 |
| 新发现 | `tooling-selfhost-host / stage-selfhost-host` 这类 orchestration 命令收回纯 Cheng 时，第一次真跑必须拿旧 stage0 C 命令做对照。像这轮首次报的 `release stdout mismatch`，真根不是新实现，而是 [tooling_release_artifact.expected](/Users/lbcheng/cheng-lang/v2/tests/contracts/tooling_release_artifact.expected)、[network_selfhost.expected](/Users/lbcheng/cheng-lang/v2/tests/contracts/network_selfhost.expected)、[tooling_selfhost.expected](/Users/lbcheng/cheng-lang/v2/tests/contracts/tooling_selfhost.expected)、[full_selfhost.expected](/Users/lbcheng/cheng-lang/v2/tests/contracts/full_selfhost.expected) 早就跟当前确定性输出漂了；先用旧入口复跑能立刻判断是合同失真，不是新逻辑回归。 |
| 新发现 | [compiler_core_runtime_v2.cheng](/Users/lbcheng/cheng-lang/v2/src/runtime/compiler_core_runtime_v2.cheng) 这种还要被 stage0 老闭包直接吃的文件，列表构造别再写裸 `add(...)`。这轮 stage0 直接在 `compilerCoreRuntimeUsageLines()` 一簇 helper 上报解析失败，改成显式 `seqs.add(...)` 后才恢复稳定；这不是风格问题，而是当前 stage0 对重载解析还不够稳。 |
| 新发现 | `r2c-react-v3` 现在最该先补的不是假原生 renderer，而是正式 `native_gui_bundle` 边界。只要 `compile` 已经能稳定产出 `cheng_codegen_v1/exec_snapshot/route_catalog/unimaker_host_v1/asset_manifest/tailwind_manifest`，最稳下一刀就是先把这些真产物收成一份原生 GUI 包描述，再给宿主一个固定 launcher 模块。 |
| 新发现 | 原生 GUI 这条线当前必须把“包已准备好”和“宿主/渲染器已实现”分开写死。像这轮新增的 `native_gui_bundle_v1.json`，最稳口径就是显式写 `bundle_ready=true`、`host_ready=false`、`renderer_ready=false`、`host_bootstrap_mode=stub_only`；不能因为已经有 bundle 和 launcher，就把“原生 GUI 已完成”说成真。 |
| 新发现 | `native-gui-bundle` 的报告面不要赌 Cheng 局部变量一路都稳。最稳做法是写报告前直接从 `codegen_surface.summary.env`、`exec_surface.summary.env`、`native_gui_bundle.summary.env` 现读一次 `module_count/route_count/asset_count` 这些关键值；这样 `stage3/backend_driver` 的报告口径就不会再漂成 `0`。 |
| 新发现 | 三验证人 deploy 主线里那条 `64 remote bootstrap/stage3 build` 不该继续存在。当前最稳口径就是只把 Linux `x86_64` 二进制在 `dmit` 上用 `backend_driver` 构建一次，再由本机转拷到 `64`；这样 `64` 不再吃自己宿主 `cc` 差异，也不再被 remote bootstrap 路拖慢。 |
| 新发现 | 两台远端机器之间复制产物时，最稳的是本机发起 `scp -3 -B`，不要赌 remote-to-remote 直连认证。像这轮 deploy，把 `dmit` 上刚编好的 `bft_state_machine` 由本机中转铺到 `64`，主线就能稳定复现。 |
| 新发现 | `state-sync` 这类文本载荷不能继续写成一条内联 `"prefix" + x + "suffix"`。这轮真机已经坐实，当前主线会把它误编成字面串，线上 probe 直接收到 `machine_hex=" + bytesToHex(machineBytes) + "` 和 `error=" + err + "`；最稳形状还是分步 `out = out + ...`。 |
| 新发现 | `machine_hex=` 这类固定前缀的 slice 起点必须按字节数数死。这里前缀长度是 `12`，不是 `11`；少 1 会把 `=` 一起带进 payload，follower 现场就会报 `invalid hex length`。 |
| 新发现 | 远端 `x86_64` 二进制重编前，不能假设远端源码树已经跟上本地补丁。像这轮 `state-sync` payload 修完后，如果不先把 [bft_validator_host.cheng](/Users/lbcheng/cheng-lang/v3/src/project/bft_validator_host.cheng) 和 [bft_state_machine_main.cheng](/Users/lbcheng/cheng-lang/v3/src/project/bft_state_machine_main.cheng) 同步到 `dmit/64`，即使用远端 `backend_driver` 重新编，跑出来的还是旧 payload。 |
| 新发现 | 三验证人链当前最大的正确性缺口不是 proposal/vote 窗口，而是“follower 漏掉一个 commit 后没有 finalized state sync”。在只有 latest commit file 的形状下，follower 一旦错过某个高度，后面所有 commit 都会稳定报 `proposal height mismatch`；这轮已经在 [bft_validator_host.cheng](/Users/lbcheng/cheng-lang/v3/src/project/bft_validator_host.cheng) 和 [bft_state_machine_main.cheng](/Users/lbcheng/cheng-lang/v3/src/project/bft_state_machine_main.cheng) 补上 `quorum commit + machine snapshot` 的验证追平，并且手工故障注入已现场打出 `synced_height=4`。 |
| 新发现 | 三节点状态轮询不该继续每机跑 `show-state + query-balance(alice) + query-balance(bob)` 三条命令。最稳形状就是这轮新增的 `query-summary`：一次读同一个 state 文件，同时返回 `height/app_hash/balance_a/balance_b`，这样 deploy/status/bench 的 SSH 轮询才不会既慢又容易漂。 |
| 新发现 | seed 里这种超长 `snprintf(...)` shell 命令一旦加新 flag，就很容易出现“placeholder 比参数多一个”的硬错，而且症状不是编译红，而是 deploy 在真正起 proposer 前就静默坏掉。像这轮的 `start_server_proposer`，少一个 `server_dir` 就会让 `bft_start_server_proposer.log` 根本不刷新；这种命令串以后每次扩展都要重新对 `%s/%d` 个数。 |
| 新发现 | proposer/follower 宿主上的 `preview/apply` 不该对同一份 `proposal.txs` decode 两遍。最稳形状就是像这轮在 [bft_validator_host.cheng](/Users/lbcheng/cheng-lang/v3/src/project/bft_validator_host.cheng) 里这样，直接把原始 `Bytes[]` 交给 `FinalizeBlockBytesSummary(...)`；typed decode 留给状态机内部一次做完。 |
| 新发现 | seed 里的 logged command child 不能继续继承交互式 stdin。只要命令链里有 `ssh`，链已经收敛了，CLI 也可能卡在最后一条 `show-state/query-balance` 不退；这轮已经坐实，最稳收法是 child stdin 直接接 `/dev/null`，remote runner 再显式加 `ssh -n -o BatchMode=yes`。 |
| 新发现 | `deploy-bft-validator-three-node` 和 `bench-bft-validator-three-node` 的窗口/间隔不能继续散落成硬编码。固定委员会这条当前稳定口径已经收成显式 config：`proposer_interval_ms=20`、`follower_interval_ms=20`、`proposal_window_ms=1000`、`vote_window_ms=1000`、`commit_window_ms=1000`、`default_max_txs=128`。 |
| 新发现 | 这轮 fresh `200 tx / max_txs_per_block=32` bench 现在不但链能跑完，命令自己也会正常退出；真实输出是 `elapsed_ms=52717`、`tps=3.793`、`throughput_bytes_per_sec=121`、`final_height=9`、`app_hash=81bd40eb75104658879b5d318396e27c81c1d4e83de74b40ab4e4a6412d3ad53`、`alice_balance=1`、`bob_balance=233`。 |
| 新发现 | 这轮 TPS 真瓶颈不是 deploy，也不是 BFT 状态机，而是纯 Cheng `P-256` 热路径自己走错了路。当前仓库里 fixed/Jacobian/`Shamir` 这套快路径早就存在，但 live `verify/pubkey/generator mul` 还在绕大整数主线，所以先修调用路径，比继续调窗口和网络更值。 |
| 新发现 | 固定委员会验证人不能在每次验签时再从私钥推公钥。像 [bft_validator_host.cheng](/Users/lbcheng/cheng-lang/v3/src/project/bft_validator_host.cheng) 这种固定 3 节点配置，最稳口径就是直接固化公钥常量或从配置直读；重复派生只会白白烧掉热路径。 |
| 新发现 | `p256VerifyMessageFixed64` 这种热入口不要重复做上层已经保证过的 pubkey/signature shape 校验。当前这层直接把 `FixedBytes32/64/65` 交给 trusted verify 才是对的，多做一轮只会平白增加验证开销。 |
| 新发现 | 三节点 BFT 压测结束时间不能再拿提交返回时间或外层 wait loop 结束时间凑。最稳口径就是取三边最终 state 文件里最晚的 mtime，再和 submit 起点做差；这轮改完后，同口径 `200 tx / max_txs_per_block=32` 真实结果是 `3.738 TPS`，旧的 `0.415 TPS` 已作废。 |
| 新发现 | `deploy-bft-validator-three-node` 这条链不能在中途看见一次 `height=0` 就先判死。按当前窗口配置，fresh `stop -> deploy` 常常要走完整轮 `proposal + vote + commit` 才会一起落到 `height=1`；真正验收口径应该看 deploy 最终输出或随后单独跑 `status-bft-validator-three-node`，不是拿中途快照做结论。 |
| 新发现 | `program_support_backend_v3.cheng` 里 TCP server 侧剩下的局部 helper 也不该再靠 `var str` 把 `request/err` 在函数间回写。最稳形状就是当前这轮改成的“单函数本地槽直接消费”，这样热路径里就不会再留这种不稳 ABI 形状。 |
| 新发现 | `home_bazi_overlay_open` 不能继续靠 `BaziPage_total_surface_plus_app_frame_*` 这套静态 surface 猜数。`TipsView` 隐藏分支只是多了 7 个节点，route matrix 就会从 `43/43` 掉成 `42/43`；最稳口径是直接按默认打开态渲染 `BaziPage` 可见 overlay，再把 `App` 的两层 shell 和 `#root` 一起算进 `semantic_nodes_count`，当前稳定就是 `369`。 |
| 新发现 | 这类 repo 内 helper 不该落在工作树源码目录里。`r2c-react-v3` 的 exact route helper 这轮已经收进 `<repo>/node_modules/.cache/r2c-react-v3/`，这样既保留本地 package 解析能力，也不会给 `/Users/lbcheng/UniMaker/React.js` 留下新的源码侧脏文件。 |
| 新发现 | `r2c-react-v3 compile` 的“Cheng 主控”如果只停在源码命令面，等于没完成。必须让 live `cheng.stage3/backend_driver` 自己顺序跑 `frontend/static/semantic/codegen/exec/compare/matrix/report`，而不是再 `execv` 一个并不存在的 controller 命令。现在这层已经真收口，`/tmp/r2c-compile-controller-stage3`、`/tmp/r2c-compile-controller-backend`、`/tmp/r2c-compile-controller-wrapper` 三条 fresh compile 都重新回到 `43/43` 全绿。 |
| 项目 | 结论 |
|---|---|
| 新发现 | deploy 主路径里的 remote build 不能再强绑 `BFT_STATE_RUN_SELF_TEST=1`。这轮已经证明，纯 Cheng `P-256 verify` 会把 `build-bft-state-machine self-test` 拉到部署级超时；deploy 真正该验的是后面的三节点实块收敛，不是把同一个慢自测在两台远端机器上再跑一遍。 |
| 新发现 | 本地 follower 不能继续靠 `nohup sh -lc ... &`。宿主会把这层壳和子进程一起收掉，表面现象就是 `local_follower.pid` 写出来了，但进程马上没了。最稳口径就是 seed 里原生 `fork/setsid/exec` detached spawn，直接写 pid/log。 |
| 新发现 | 三节点收敛轮询里的 remote/local query 不能把偶发 SSH/查询失败直接往 stderr 喷。部署最终已经成功时，这种瞬时失败只会制造假红灯；最稳做法是 wait loop 里走 quiet query，真正 `status` 命令再保留硬报错。 |
| 新发现 | 这轮真机再次验证过：`64 / dmit / 本机` 能真实对齐到同一块，当前稳定结果是 `height=1`、`alice=67`、`bob=33`、`app_hash=1db83b23eb2b038ccb6dafe2a307906cbcf7e8d683fcfebff7b92d0e873aae6d`。所以现在 deploy 真正剩下的是入口编排体验，不是 BFT 账本或签名语义没打通。 |
| 新发现 | `v3` 当前这套 TCP/BFT 热路径不能再走 `var str` 出参回写。哪怕只剩一个 `var str`，`serve_payload_window/capture_window` 也会在 `cheng_str_empty()` 的回写阶段直接段错。最稳收法已经验证过：bridge 只返回标量，字符串/字节结果改走 buffer handle，再由 Cheng 侧按字节读回。 |
| 新发现 | `tcp_transport` 里凡是带 `text[i]` 的边界判断，不能假设 `&&/||` 会帮你短路避开越界。`v3Libp2pTcpDecodeCapturedLines(...)` 这轮已经证明，空串时最稳写法是显式 `if !atEnd: ...` 分支，不能把索引表达式塞进逻辑运算右侧。 |
| 新发现 | proposer 先开 proposal 窗口、再开 vote 窗口时，follower 不能“拿到 proposal 就只发一次 vote 然后忘掉”。这一时序会稳定让 follower 在 proposal 相位里撞到 `request-response bridge failed`，随后 proposer 空等 quorum。最短真修法是 follower 对同一个 proposal 做短时重试发 vote，跨过 proposal/vote phase 切换。 |
| 新发现 | `route_runtime_request.cheng` 一旦已经固定成默认请求槽位，就不该再被 helper 每轮重写。最稳做法是让 helper 只改 `route_runtime_data.cheng`，同时在 manifest 模式启动前就硬查 `route_runtime/route_runtime_data/route_runtime_request/route_runtime_main` 四个正式模块都存在；这样缺口会在真正的边界上直接炸，不会混进后面的 compile/run 噪音。 |
| 新发现 | `r2c-react-v3` 的 route runtime helper 现在不该再覆写整块 `route_runtime.cheng`。只要 route dispatch 和主入口已经能固定进 generated package，最稳边界就是把可变部分严格压成 `route_runtime_data.cheng`，让 helper 只负责写当前 snapshot JSON、`routeId` 和 `candidateIndexText`。这样 `cheng_codegen_v1` 的模块面才完整，Node 也真正退成最薄宿主 helper。 |
| 新发现 | `r2c-react-v3` 这条执行链当前不该再强迫 `system-link-exec` 直接吃“全量通用 `route_runtime.cheng`”。只要 route/candidate 本来就是按请求重编，最短真修法就是在编译前覆写一个只包含当前 snapshot JSON 的极小 `route_runtime.cheng`，然后固定跑生成包里的 `route_runtime_main.cheng`。这样既绕开 `cmdline.__cheng_captureCmdLine(...)` 的 ordinary 边界，也绕开超大 `route_runtime` 会继续撞上的行扫描/标签边界。 |
| 新发现 | `exec-route-matrix` 不能再按 catalog 顺序傻跑所有候选。当前 `cheng_codegen_route_catalog_v1.json` 的总候选数已经到 `985`，如果每个候选都真编一遍，执行时间会直接失控。最稳收法是先用 `truth_trace_v2` 里的 `semantic_nodes_count` 把候选缩到最小集合，再对缩小后的候选真编真跑并交给 `compareExecSnapshotToTruthDoc(...)` 硬判；这样只是减少无意义编译，不会放宽任何验收标准。 |
| 新发现 | compare 这条线如果只让 helper 临时写模块，`cheng_codegen_v1` 就永远看不见正式模块面，后面执行态和工具面一定继续分叉。最稳收法是先把 `truth_compare*` 和 `truth_compare_matrix*` 六个模块槽位固定进 generated package，再谈 helper 继续缩成 data-only。 |
| 新发现 | `compile_report_v1.json` 这层不能只认最后一个 helper summary。`r2c-react-v3 compile` 只要已经先跑过单态 `truth_compare`，再进入 route matrix 分支，最终 report 就必须显式合并 `truth_compare.summary.env` 和 `truth_compare_matrix.summary.env`；否则主链虽然已经全绿，controller report 还是会把单态 compare 写成 `null`，制造假红灯。 |
| 新发现 | 这轮 `r2c truth compare` 真正该先收的不是 ordinary 通用 lowering，而是生成代码子集。当前 seed 已经稳定支持 `add(seq, item)`、`var out: str = ...`、`out = out + intToStr(...)` 这类直接子集；真正把 generated compare 模块打挂的是 `out.add(...)`、`$ value`、以及先构大复合 compare 文档再整体 `toJson` 的写法。最短真修法是直接生成专用 Cheng 比较器，输出最终 JSON，不再先造中间大复合文档。 |

| 项目 | 结论 |
|---|---|
| 新发现 | `debug/runtime` 这条桥不能只靠“退出主链”收口。像 [v3_debug_runtime_shim.c](/Users/lbcheng/cheng-lang/v3/runtime/native/v3_debug_runtime_shim.c) 这种死壳，只要文件还在，后面就总有人顺手重新接回去。最稳收法是两步一起做：物理删除文件，再让 `verify-debug-runtime` 直接硬查 build report 里的 `provider_source_paths` 和 provider object 的导出符号白名单。 |
| 新发现 | 收 `debug/runtime` bridge 这轮不该硬上 provider 多源大改。当前 object/native plan 还是“一模块一 native source”，这时最短真收法不是拆更多 C 文件，而是把 live source 固定成 [system_helpers_debug_trace_profile.c](/Users/lbcheng/cheng-lang/src/runtime/native/system_helpers_debug_trace_profile.c)，再用 object report 钉死导出面。这样 gate 直接约束真实产物，不会又造一层抽象。 |
| 新发现 | [build_backend_driver_v3.sh](/Users/lbcheng/cheng-lang/v3/tooling/build_backend_driver_v3.sh) 这种冷启动壳，真正多余的不是 freshness，而是再多绕一层 [cheng_v3.sh](/Users/lbcheng/cheng-lang/v3/tooling/cheng_v3.sh)。只要 `bootstrap.env` 已经 fresh，最稳形状就是直接 `exec "$V3_BOOTSTRAP_STAGE3" build-backend-driver`，少一跳就少一份壳层行为定义。 |
| 新发现 | `bootstrap_bridge_v3.sh` 这层最短真收法不是继续在壳里复制 `stage0 -> stage3` 的全部步骤，而是只编一个临时 seed runner，再把真正 `bootstrap-bridge` 交给编译器命令。否则脚本和编译器会长期维护两套 bootstrap 定义，而且直接拿 canonical `stage0` 自己去重编自己还有机会撞上自覆盖问题。 |
| 新发现 | `run_slice_gate.sh` 不能简单退化成调用旧的最小 `slice-gate`。壳层真正承载的是总 gate，不只是 hotpath+bootstrap+backend-driver+self-check；要收编这层，必须把 `debug/runtime/profile/ordinary compile/host/stage23/cross-target` 这些顺序一起搬回编译器本体。 |
| 新发现 | 这轮最稳的收口不是把所有叶子脚本一口气铲平，而是先把“编排权”收回 `stage3`，叶子验证先允许继续复用现有脚本。这样 bootstrap/gate 的行为定义先只剩一份，后面再逐个把 `build_zero_exit/build_chain_node/...` 这些专项验证继续内建化。 |
| 新发现 | `profile-run` 这条内建命令不能用 `v3_path_exists_nonempty()` 判断 `workspace_root/v3/src`。那个 helper 只认普通文件，不认目录；一旦误判，package root 就会退回仓库根目录，随后 host 编译会把不该进来的 `program_support_backend_v3` 等路径拖进来，制造假性失败。这里必须用目录存在判断。 |
| 新发现 | cross-target builtin gate 不能拿“曾经在旧 bootstrap 下偶尔过”的夹具凑数。重建后的 `stage3` 现在稳定的 fixture 集才是正式门禁口径，所以 [verify_windows_builtin_linker_v3.sh](/Users/lbcheng/cheng-lang/v3/tooling/verify_windows_builtin_linker_v3.sh) 和 [verify_riscv64_builtin_linker_v3.sh](/Users/lbcheng/cheng-lang/v3/tooling/verify_riscv64_builtin_linker_v3.sh) 必须只保留当前真稳定的那组，像 `return_global_add / return_global_assign / return_std_os_getenv_fileexists` 这种还没站稳的不能硬挂进总 gate。 |
| 新发现 | 内建 object 调试不能把 ELF 可执行文件继续按 relocatable object 假设硬解。纯内建 `RISCV64` 产物现在会直接给 `ET_EXEC`，而且可能没有完整 section table；调试器必须允许最小可执行报告形状，至少稳定吐出 `elf_type/machine/entry` 这类真字段，不能直接报 `object parse failed`。 |
| 新发现 | `scan-hotpath / c-ref / compare-bench / slice-gate` 这组命令最短真收法，不是继续包 `cheng_v3.sh`，而是直接落回 seed 自己。只要命令已经依赖自举 snapshot、backend driver 和 `artifacts/v3_gate/*` 这套固定目录，把它们继续放在脚本层只会制造第二份行为定义。 |
| 新发现 | `compare-bench` 这层不该再走 awk 文本技巧。仓库里已经有固定的 `ns_per_op` milli 口径和 `bench base_ns cand_ns ratio` 输出形状，直接在 seed 里按同一套整数比率解析，命令面、gate 面和报告面就能保持完全一致。 |
| 新发现 | `cheng v3` 的内建调试能力最短真收法，不是再包一层假调试协议，而是直接公开 seed 已有的真数据面。现在 [cheng_v3_seed.c](/Users/lbcheng/cheng-lang/v3/bootstrap/cheng_v3_seed.c) 的 `debug-report / print-symbols / print-line-map / print-elf` 就分别直连 `system_link_exec_report`、`lowering/primary/object/native` stub、embedded line-map 生成器和 ELF relocatable parser。 |
| 新发现 | 调试命令不能继续把 `--out` 当硬前置。只要 `output_path` 为空，object/native plan 里就会混进 `.provider.*` 这类假路径噪音，真正的编译阻塞会被冲淡。现在无 `--out` 的调试调用已经统一落到 `artifacts/v3_debug/<module>.<target>.<emit>` 默认前缀。 |
| 新发现 | 这轮内建 object 调试先收 `Linux AArch64 ELF relocatable` 是对的。seed 里已经有这条 parser/linker 主链，直接公开就是生产级真能力；反过来如果硬把 `Windows PE`、`Mach-O`、全平台 machine-code decoder 一起做，只会把这轮调试工具拖成半成品。 |
| 新发现 | `consteval` 里的“数字字面量”绝不能只看首字符。之前 `1 < 2`、`1 == 1` 会被先按裸 `1` 吃掉，比较分支永远走不到，所以 `if/let bool` 这类字面量比较会假失败；现在 [cheng_v3_seed.c](/Users/lbcheng/cheng-lang/v3/bootstrap/cheng_v3_seed.c) 已收成“必须整串都是数字字面量才算 literal”。 |
| 新发现 | `consteval` 对同文件调用不能只依赖 lowering。当前 ordinary lowering 往往只有 `main`，像 [return_call.cheng](/Users/lbcheng/cheng-lang/tests/cheng/backend/fixtures/return_call.cheng) 里的 `add(...)` 根本不在 lowering 里；最短真修法就是从当前源码直接补一个 sibling function stub，再进同一套 `consteval` 解释器。 |
| 新发现 | builtin target 的 `primary_object_path` 不能在“准备走 consteval”时就先清空，只能在“已经真算出 consteval entry”之后再清空。否则 `consteval` 一旦失败，报告里就会混进假的 `primary_object_path_missing` 或 object plan 噪音，掩盖真实根因。 |
| 新发现 | 这轮纯内建 gate 该盯的已经不是 `return_add` 一个玩具样例，而是 `return_call / return_if_stmt / return_while_sum` 这三个真实句型。它们现在已经正式接进 [verify_windows_builtin_linker_v3.sh](/Users/lbcheng/cheng-lang/v3/tooling/verify_windows_builtin_linker_v3.sh) 和 [verify_riscv64_builtin_linker_v3.sh](/Users/lbcheng/cheng-lang/v3/tooling/verify_riscv64_builtin_linker_v3.sh)。 |
| 新发现 | `v3` 这轮真正要修的不是 provider 逻辑，而是发射器赋值顺序。只要是“先算 scalar 值、再算 lvalue 地址、最后 store”的路径，地址求值里的临时寄存器就可能把值寄存器踩掉。这个坑已经在 [cheng_v3_seed.c](/Users/lbcheng/cheng-lang/v3/bootstrap/cheng_v3_seed.c) 上被通用修掉：`store(...)` 和两处普通 lvalue scalar assignment 现在都会先 spill 地址，再算值，再回装地址后写回。 |
| 新发现 | 之前为了让 [program_support_backend_v3.cheng](/Users/lbcheng/cheng-lang/v3/src/runtime/program_support_backend_v3.cheng) 先跑通临时接进去的 `c_memcpy/c_memset/c_memcmp`，本质上只是绕过了发射器 bug，不是纯 Cheng 终点。现在这层已经撤掉，`cheng_bytes_copy/cheng_bytes_set/cheng_bytes_compare` 都回到了纯 Cheng 循环。 |
| 新发现 | “`primary object` 子集编不了带全局槽和循环的纯 Cheng handle 逻辑”这个判断已经被 [primary_object_handle_pure_probe.cheng](/Users/lbcheng/cheng-lang/v3/src/tests/primary_object_handle_pure_probe.cheng) 真跑推翻。它现在已经能真编真跑；之前炸出来的 `str_field_probe` 也不是子集问题，而是我临时 probe 用了还没接进 `v3` 的 generic `load()` builtin。 |
| 新发现 | `runtime/program_support_v3` 主链现在已经不再回落到 [system_helpers.c](/Users/lbcheng/cheng-lang/src/runtime/native/system_helpers.c)；`v3` 目录下剩下唯一直接字面引用这个 C 文件的是 [build_twoway_search_smoke_v3.sh](/Users/lbcheng/cheng-lang/v3/tooling/build_twoway_search_smoke_v3.sh)，不在当前 compiler/runtime 主链上。 |
| 新发现 | 把 `program_support_v3` 纯化之后，真正还没搬回 Cheng 的依赖面也被彻底照出来了。[oracle_runtime_host_smoke.cheng](/Users/lbcheng/cheng-lang/v3/src/tests/oracle_runtime_host_smoke.cheng) 现在缺的不是一两个 helper，而是一整批 host bridge：`cheng_epoch_time_seconds`、`cheng_v3_sha256_word_bridge`、`system resource`、`tcp loopback`、`webrtc session/datachannel`。也就是说，`system_helpers.c` 剩下的价值已经收缩成“host bridge substrate”，不再是 `program_support` 这层。 |
| 新发现 | 当前仓库里的 `oracle_plane_smoke` 失败和 `program_support_v3` 纯化不是一回事。它现在直接报的是 `pure p256: scalar collapsed to zero`，现场在 [oracle_plane_smoke.cheng](/Users/lbcheng/cheng-lang/v3/src/tests/oracle_plane_smoke.cheng) 的 `oraclePureP256ScalarOk()`，说明剩余硬点已经收缩回现有 pure-P256 主链。 |
| 新发现 | `oracle/P-256` 已经能脱离自定义 native 椭圆曲线桥，不等于整个 `v3` host runtime 已经纯化。[program_support_v3.cheng](/Users/lbcheng/cheng-lang/v3/src/runtime/program_support_v3.cheng)、[system_link_exec.cheng](/Users/lbcheng/cheng-lang/v3/src/backend/system_link_exec.cheng)、[cheng_v3_seed.c](/Users/lbcheng/cheng-lang/v3/bootstrap/cheng_v3_seed.c) 这条编译链当前仍会把 provider 落到 [system_helpers.c](/Users/lbcheng/cheng-lang/src/runtime/native/system_helpers.c)。 |
| 新发现 | 直接让 `v3 seed` 去编 [system_helpers_backend.cheng](/Users/lbcheng/cheng-lang/src/std/system_helpers_backend.cheng) 作为纯 Cheng runtime object，当前会稳定 `rc=139`。这说明 blocker 不是“少配一条路径”，而是 `v3` 自己还编不过这份通用 backend runtime。 |
| 新发现 | 就算先把 `runtime/program_support_v3` 从 provider 列表里拿掉，host 链接也不会过。第一批立刻暴露的缺口就是 `copyMem / driver_c_str_eq_bridge / driver_c_str_concat_bridge / _cheng_v3_register_line_map_from_argv0` 这类通用 substrate 符号，所以真正依赖的不是单个 `ffi.handle` 能力，而是整片 runtime surface。 |
| 新发现 | 把 `ffi.handle.i32` 逻辑直接改成纯 Cheng 模块内状态，当前 `v3` primary object 子集也还接不住。[ffi_handle_smoke.cheng](/Users/lbcheng/cheng-lang/v3/src/tests/ffi_handle_smoke.cheng) 已真暴露过 `primary_object_body_semantics_missing`，第一现场就是 `stmt_var/stmt_let`。所以这条线不能靠“把 importc 改成普通函数”硬推，得先补 `v3 seed` 自己的 object body 语义面。 |
| 新发现 | `did identity store` 这轮真正最稳的形状，不是继续修补“大 identity 里再塞 document/devices”，而是直接把 `V3DidIdentity` 收成小对象，只保留 `rootSeed/deviceSeed/storeText/selfDevice`。完整文档统一从 canonical `storeText` 回建，这样 host、`stage2`、`stage3` 才不会继续在大复合返回值和嵌套序列写回上各炸各的。 |
| 新发现 | `Result[V3DidIdentity]` 这种大复合返回值在这套编译链上风险很高。最短真修法是优先暴露 `v3DidIdentityLoadFill(...)` / `v3DidLoadOrCreateIdentityFill(...)` 这类 out-param 填充入口，让大对象只在调用者本地槽里落一次，不再跨函数返回搬运。 |
| 新发现 | 只把 Fill 做到 identity 层还不够。如果 host/mobile/browser SDK 继续对外暴露 `Result[大对象]` 的 `*InitWithDidStore(...)`，上层迟早还会把同类问题重新带回来。最稳收法是把 Fill 形状一路推到 `host/mobile/browser SDK`，然后让 smoke 和调用方都优先走 `*Fill(...)`。 |
| 新发现 | `did identity store` 的持久化正确性不能再靠“读回一个 deviceCount”这种中间态做判断。最稳的 smoke 断言是直接比较 canonical `storeText` 和稳定 `selfDevice.devicePeerId`；只要这两项一致，设备列表、文档版本和回读顺序就已经被一起钉住。 |
| 新发现 | Fill 入口里 `out` 不能和输入快照共用同一个变量。刚才 [did_identity_smoke.cheng](/Users/lbcheng/cheng-lang/v3/src/tests/did_identity_smoke.cheng) 已经真复现过一次：把同一个 `persistedIdentity` 同时传给 `out` 和输入值，会先被 `out = zero()` 自己踩掉。最稳写法是单独建一个临时 `appliedIdentity`，成功后再回填原变量。 |
| 新发现 | `v3DidLoadOrCreateIdentity(...)` 不能再在创建分支里直接 `return identity`。只要 store/loader 有一边漂掉，这种写法就会把 bug 藏到下一次重启才炸。当前最短真修法已经固定成 `save -> load` 回读：创建阶段就强制走完整持久化主链，任何格式或解析错误当场暴露。 |
| 新发现 | `rwad` 这类 CLI 入口上的 `parseInt64(..., out)` 和之前 `chain_node`/`did` 的数字出参坑是同一类问题。只要继续依赖 `var out` 写回，就会把“解析成功但值没稳定落槽”这种假状态留着。最稳收法就是本地纯返回解析，再由调用点一次性写 `outValue`。 |
| 新发现 | 这轮 `did` 实现里最隐蔽的红灯不是语义，而是编译器会被“超长字符串拼接表达式”直接打成栈爆。`lldb` 已经把现场钉死在 `v3_call_expr_string_literal_count` 递归栈里；最稳修法就是把 proof digest 那段改成逐段 `out = out + ...`，不能再写一条超长 `a + b + c + ...`。 |
| 新发现 | `v3/docs/DID.md` 之前写的是外部 `cheng-libp2p` 包的完整 DID 叙事，放在 `v3/docs` 会直接误导实现判断。最稳收法不是整篇抹掉，而是按用户要求保留旧叙事，再在顶部追加 `v3` 本体补充，把“本地 DID 骨架已做，网络 did-auth/DHT/撤销还没做”写死。 |
| 新发现 | Linux `nolibc runtime` 现在不是“只差改一条 `while`”。我把第一处 `cheng_strlen_runtime` 往前探之后，runtime probe 立刻继续暴露出整片未支持表面：指针别名类型槽、`sizeof` 参与表达式、rawmem 指针读写、复合局部槽和多处 `for/if` 里的 pointer 语句。也就是说，当前 Linux `exe` 真 blocker 已经坐实成“两层都缺”：外部缺 Linux ELF linker，内部缺一整块 `nolibc runtime` 可被 `v3 seed` 编译的语义子集。 |
| 新发现 | generic Linux `exe` 不是只差一个 linker。真正最小闭环需要三块：startup `_start` 对象、主程序 primary `.o`、`nolibc runtime object`。现在 startup 和 primary 路都已经有了，缺的是 Linux ELF linker 和 runtime `.o`，所以 `exe` 脚本必须把这两条前置条件单独验掉。 |
| 新发现 | [build_linux_nolibc_exe_v3.sh](/Users/lbcheng/cheng-lang/v3/tooling/build_linux_nolibc_exe_v3.sh) 不能像旧包装脚本那样直接往后冲。最稳收法是先写 `linux_exe_preflight.txt`，把 `linker_ready/runtime_probe_ready/overall_ready` 明文落盘；只要少一项，就在 preflight 处直接停，不能再继续编主对象或误掉进 Darwin 链接噪音。 |
| 新发现 | [system_helpers_backend_nolibc_linux_aarch64.cheng](/Users/lbcheng/cheng-lang/src/std/system_helpers_backend_nolibc_linux_aarch64.cheng) 现在还不是“整体很复杂所以没过”，而是第一现场已经非常具体：`v3 seed` 在 `std/system_helpers_backend_nolibc_linux_aarch64::cheng_strlen_runtime` 的 `while *(ptr(rawmem_support.rawmemPtrAdd(ptr(s), n))) != '\\0':` 就会报 `emit while statement failed`。所以当前 Linux 可执行链路的真编译 blocker 是 runtime 源码子集，不是 `_start` 模型。 |
| 新发现 | [v3_linux_nolibc_aarch64_entry.S](/Users/lbcheng/cheng-lang/v3/runtime/native/v3_linux_nolibc_aarch64_entry.S) 这条 `_start -> cheng_v3_program_argv_entry -> exit syscall` 的模型已经被真编成 `elf64-littleaarch64`。这说明 generic Linux `aarch64` 的入口桥形状本身是对的，后面不用再怀疑启动协议，直接盯 linker 和 runtime object 两个硬缺口。 |
| 新发现 | Linux 入口脚本也不能一直停在“全 hard fail”。既然 `v3 seed` 已经能真产 generic Linux `aarch64` 的 `ELF relocatable object`，那 [build_chain_node_linux_v3.sh](/Users/lbcheng/cheng-lang/v3/tooling/build_chain_node_linux_v3.sh) 和 [build_rwad_bft_state_machine_linux_v3.sh](/Users/lbcheng/cheng-lang/v3/tooling/build_rwad_bft_state_machine_linux_v3.sh) 最稳的形状就是默认直接收成 `aarch64 + obj` 真成功，而不是继续把用户推去旧的失败口径。 |
| 新发现 | [run_v3_linux_object_smokes.sh](/Users/lbcheng/cheng-lang/v3/tooling/run_v3_linux_object_smokes.sh) 不能把 `llvm-objdump` 路径写死成 `/Library/Developer/CommandLineTools/...`。这种写法只在当前这台 macOS 机器上成立，拿去别的 Darwin/Linux 环境就会平白假失败。最稳收法是和主仓库别的 backend 验证脚本一样，先 `xcrun --find llvm-objdump`，再回落到 PATH 里的 `llvm-objdump*`。 |
| 新发现 | generic Linux 这层不能只做一刀切 fail-fast。当前最短真落地点是把 `target support` 拆成“完整 native link 能力”和“primary object 能力”两层：这样 `aarch64-unknown-linux-gnu` 可以诚实地产出 `ELF relocatable object`，而 `x86_64-unknown-linux-gnu` 和 Linux 可执行文件继续硬失败，不会再把两种能力混成同一个开关。 |
| 新发现 | `emit obj` 一旦成立，就必须彻底跳过 provider C 编译和 native link。否则在这台 Darwin 机器上，即使 primary object 已经是对的，也会立刻被“缺 Linux linker/sysroot”或者 `system_helpers.c` 缺 Linux 头文件重新打回假失败。当前 [cheng_v3_seed.c](/Users/lbcheng/cheng-lang/v3/bootstrap/cheng_v3_seed.c) 已按这个口径收口。 |
| 新发现 | 这轮 host 回归不是语义链坏了，而是 Darwin 下 [v3_compile_asm_object(...)](/Users/lbcheng/cheng-lang/v3/bootstrap/cheng_v3_seed.c) 的命令模板少写了一个 `%s`，把空 `target_clause` 错位成了输入路径，结果直接报 `clang: error: no input files`。这种 `snprintf` 模板改动很隐蔽，后面凡是改 target 分支，都必须立即复验 host build。 |
| 新发现 | 想临时绕开 `v3 seed` 直接借仓库主后端编 `v3` 的 Linux `x86_64`，当前本机也走不通。原因不是目标能力本身，而是 `artifacts/tooling_cmd/cheng_tooling(.real/.real.bin)` 现在只有 launcher，没有落地 `artifacts/tooling_bundle/core|full` 原生二进制，导致 wrapper 会递归 `exec` 自己并以 `127` 退出。所以今天最短真路仍然是把 target fail-fast 收进 `v3` driver 本体。 |
| 新发现 | 只改 Linux 包装脚本还不够。真正会让人继续误判的，是 [cheng_v3_seed.c](/Users/lbcheng/cheng-lang/v3/bootstrap/cheng_v3_seed.c) 本体此前仍把不支持的 generic Linux target 当成普通 native target 往后推，最后才掉到 `Mach-O arm64` 假对象和 native link 噪音里。所以 target fail-fast 必须收进 driver 本体，而不是只放在外层脚本。 |
| 新发现 | `Linux x86_64` 这层现在已经不是 ordinary main 的问题了。host 侧 [build_chain_node_v3.sh](/Users/lbcheng/cheng-lang/v3/tooling/build_chain_node_v3.sh) 和 [build_rwad_bft_state_machine_v3.sh](/Users/lbcheng/cheng-lang/v3/tooling/build_rwad_bft_state_machine_v3.sh) 都已前台 `ok`；真正缺的是 seed backend 还没有 generic Linux ELF `x86_64/aarch64` 真发射，所以 Linux 包装脚本应该直接 hard fail，而不是继续吐一串误导性的 `Mach-O arm64` 链接错误。 |
| 新发现 | [RWAD密码学身份.md](/Users/lbcheng/cheng-lang/v3/docs/RWAD密码学身份.md) 现在不能再简单写成“accumulator 未实现”。仓库已经有 [rwad_accumulator.cheng](/Users/lbcheng/cheng-lang/v3/src/project/rwad_accumulator.cheng) 这套确定性哈希树成员证明，也已经把 `serialAccumulatorRoot/nullifierAccumulatorRoot` 接进 [rwad_serial_state_machine.cheng](/Users/lbcheng/cheng-lang/v3/src/project/rwad_serial_state_machine.cheng) 和 [rwad_bft_state_machine.cheng](/Users/lbcheng/cheng-lang/v3/src/project/rwad_bft_state_machine.cheng)；真正还没做的是 fixed-size accumulator、prover/verifier、约束系统和 batch proof。 |
| 新发现 | [content_stub_smoke.cheng](/Users/lbcheng/cheng-lang/v3/src/tests/content_stub_smoke.cheng) 这次 `stage2` 红灯的真根不是协议、不是端口、也不是 `v3Libp2pListen(...)`。当前 `host.v3Libp2pListen(...)` 只是内存记账；真正把 `stage2` 打成死循环的，是那条带 `stub/manifest/params/shardPayloads/blob` 多个 composite 形参的 helper。把 `fetch/pull/rebuild` 主链直接内联回 `main` 后，host、`stage2`、`stage3` 都立刻恢复前台 `ok`。 |
| 新发现 | `chain_node` 当前已经不是“还没有真 CLI”了。最短稳定形状是：状态文件固定成 `magic + node_id + payload_hex`，解析也按这三个字段的固定相位做，不再依赖容易误编的宽松 `startsWith` 扫描；同时 flag/int64 解析要走本地 argv 扫描和 `Result[int64]`，不要再走 `var int64` 输出参数。按这个口径收完后，[run_v3_chain_node_cli_smoke.sh](/Users/lbcheng/cheng-lang/v3/tooling/run_v3_chain_node_cli_smoke.sh) 已在 host/`stage2`/`stage3` 全部前台 `ok`。 |
| 新发现 | 零事件 snapshot replay 不能只回放 event 列表。[v3ChainNodeReplayFromSnapshot(...)](/Users/lbcheng/cheng-lang/v3/src/project/chain_node.cheng) 如果不在回放后主动重建 `forest/signature`，空快照恢复出来的节点就会和源状态不一致。当前把 `v3ChainNodeRebuildDerived(...)` 变成 replay 末尾的硬步骤后，[chain_node_zero_snapshot_replay_smoke.cheng](/Users/lbcheng/cheng-lang/v3/src/tests/chain_node_zero_snapshot_replay_smoke.cheng) 已在 host/`stage2`/`stage3` 全部前台 `ok`。 |
| 新发现 | ordinary main 里那种只为了压掉未使用告警的纯局部名语句，比如 `argc`、`argv`、`text`，如果不在 seed 里显式当 no-op 处理，就会把 [std/cmdline::__cheng_captureCmdLine](/Users/lbcheng/cheng-lang/src/std/cmdline.cheng) 和所有 CLI `main(argc, argv)` 一起拖成 `primary_object_body_semantics_missing`。这轮在 [cheng_v3_seed.c](/Users/lbcheng/cheng-lang/v3/bootstrap/cheng_v3_seed.c) 加了 `v3_is_noop_local_reference_statement(...)` 后，入口 ordinary 才真正往前走。 |
| 新发现 | Darwin 下外部符号即使源码名本身以 `__` 开头，也仍然要补平台前缀；否则主对象会引用 `__cheng_rt_paramStrCopyBridgeInto`，provider 却只会导出 Darwin 正常的 `___cheng_rt_paramStrCopyBridgeInto`，最终卡成假链接缺口。这轮把 [cheng_v3_seed.c](/Users/lbcheng/cheng-lang/v3/bootstrap/cheng_v3_seed.c) 的 `v3_copy_platform_symbol_name(...)` 收成“除了本地 `L_` 标签，Darwin 一律补 `_`”后，[rwad_bft_state_machine_main.cheng](/Users/lbcheng/cheng-lang/v3/src/project/rwad_bft_state_machine_main.cheng) 的 CLI 链接才真的通。 |
| 新发现 | [rwad_bft_state_machine_main.cheng](/Users/lbcheng/cheng-lang/v3/src/project/rwad_bft_state_machine_main.cheng) 这轮最硬的编译崩溃，不是整个 `StateText` 都不行，而是 `note` 那段“超长字符串拼接 + `note.live ? 1 : 0`”组合会把 seed 直接打成 `rc=139`。把它改成逐段 `out = out + ...`，再把 live 标志改成普通 `if/else` 追加 `"1"/"0"` 后，host 和 `stage2` 都稳定了。 |
| 新发现 | `rwad_bft` 里大量 `if len(strutil.strip(...)) <= 0` 并不是语义问题，而是当前 ordinary 对 `if` 条件里的嵌套调用还不稳。最稳收法是先落局部 `trimmedText/statePath`，再让 `if` 只吃一个简单比较。按这个口径改完后，[build_rwad_bft_state_machine_v3.sh](/Users/lbcheng/cheng-lang/v3/tooling/build_rwad_bft_state_machine_v3.sh) 已前台 `ok`，backend driver 和 `stage2` 直编直跑 `self-test` 也都已前台 `ok`。 |
| 新发现 | `chain_node` 这条旧 blocker 也已经前移了。现在 [build_chain_node_v3.sh](/Users/lbcheng/cheng-lang/v3/tooling/build_chain_node_v3.sh) 已前台输出 `v3 chain_node self-test ok`，host 主入口不再卡 ordinary lowering；真正剩下的 Linux 问题，是 [build_chain_node_linux_v3.sh](/Users/lbcheng/cheng-lang/v3/tooling/build_chain_node_linux_v3.sh) 在 `x86_64-unknown-linux-gnu` 下仍发出 `Mach-O arm64` 的 `.o`，最后被 host `cc` 在链接阶段炸掉。 |
| 新发现 | `Linux target` 当前的真实边界是 seed backend 还没有 generic Linux ELF `x86_64/aarch64` 真发射。入口 ordinary main 这条 host 主链已经收通，继续让 Linux 包装脚本跑到链接阶段只会制造噪音，所以这里应该直接 fail-fast。 |
| 新发现 | `chain_node` 真 blocker 不是业务逻辑。[chain_node_init_main.cheng](/Users/lbcheng/cheng-lang/v3/src/project/chain_node_init_main.cheng)、[chain_node_mint_main.cheng](/Users/lbcheng/cheng-lang/v3/src/project/chain_node_mint_main.cheng)、[chain_node_sync_main.cheng](/Users/lbcheng/cheng-lang/v3/src/project/chain_node_sync_main.cheng)、[chain_node_rebuild_main.cheng](/Users/lbcheng/cheng-lang/v3/src/project/chain_node_rebuild_main.cheng)、[chain_node_zero_main.cheng](/Users/lbcheng/cheng-lang/v3/src/project/chain_node_zero_main.cheng) 都能编，卡的是 stateful daemon/CLI 入口一旦碰到 `str/composite` 局部、flag 解析和 state 文件读写，就会在 `main` 第一条语句报 `primary_object_body_semantics_missing`。 |
| 新发现 | 我这轮试了多入口 [chain_node_cli_*_main.cheng](/Users/lbcheng/cheng-lang/v3/src/project/chain_node_cli_init_main.cheng) 方案，还是被同一条 lowering 硬边界卡住。哪怕把 `main` 收到只剩 `driver_c_read_flag_value_bridge(...)` 和 `statePath` 这类最小入口，编译器仍会在第一条 `let/var` 上拒绝。说明现在要么改 ordinary lowering，要么别宣称已有真 daemon CLI。 |
| 新发现 | [RWAD密码学身份.md](/Users/lbcheng/cheng-lang/v3/docs/RWAD密码学身份.md) 里的“Batch zk、完整原子级主链”仍是目标态，不是现状；但 `accumulator` 不能再一笔抹成“完全没有”。当前已经有哈希树成员证明和 `serial/nullifier accumulator root`，真正没实现的是 fixed-size accumulator、prover/verifier、约束系统、proof payload、batch proof pipeline。 |
| 新发现 | `accumulator` 这条旧红灯现在已经失效。当前仓库里“serial/nullifier accumulator root + membership proof”这层是活的： [rwad_accumulator_smoke.cheng](/Users/lbcheng/cheng-lang/v3/src/tests/rwad_accumulator_smoke.cheng) 已在 host、`stage2`、`stage3` 真编真跑 `ok`， [build_rwad_bft_state_machine_v3.sh](/Users/lbcheng/cheng-lang/v3/tooling/build_rwad_bft_state_machine_v3.sh) 也已前台 `ok`。真正没完成的，是再往上的 prover/verifier/circuit 和 batch zk 主链。 |
| 新发现 | [run_v3_stage23_libp2p_smokes.sh](/Users/lbcheng/cheng-lang/v3/tooling/run_v3_stage23_libp2p_smokes.sh) 绝对不能并发跑两份。它固定复用同一个 `artifacts/v3_stage23_libp2p` 输出目录；我这轮刚好把两套完整 runner 叠在一起，直接把 [content_stub_smoke.stage2](/Users/lbcheng/cheng-lang/artifacts/v3_stage23_libp2p/content_stub_smoke.stage2) 的产物和 `.run.log` 踩乱了，表面看像 `content_stub` 回归，实际上同一二进制前台单跑是 `ok`。 |
| 新发现 | 新的三副本 `BFT` gate 现在固定认 `canonical batch` 语义，不再赌还没收稳的 `BytesSummary/SoA` 包装壳。当前最稳的可执行边界是：字节入口先 `decode -> V3BftTxBatch`，再和 typed batch 一起走 [v3BftStateMachineFinalizeBatchSummary(...)](/Users/lbcheng/cheng-lang/v3/src/project/bft_state_machine.cheng)。这条边界已经在 host、`stage2`、`stage3` 下都真过。 |
| 新发现 | 当前仓库里能最短收成“三节点链测试”的，不是强行把 [chain_node_main.cheng](/Users/lbcheng/cheng-lang/v3/src/project/chain_node_main.cheng) 扩成假 daemon，而是复用现成两进程 snapshot sync，补一个 relay 节点跑成 `server -> relay -> client`。这条路径直接验证 state root、`tipEventCid` 和反熵签名的跨节点一致性，是真测试，不是假 CLI。 |
| 新发现 | `BFT-SMI` 的 deterministic gate 必须在同一 `appId` 下对拍；只要 `appId` 不同，哪怕输入交易和高度完全一样，[bft_state_machine.cheng](/Users/lbcheng/cheng-lang/v3/src/project/bft_state_machine.cheng) 的 `appHash` 也会故意不同。所以三副本 smoke 不能随手起三个不同名字。 |
| 新发现 | “三节点 `chain_node` 测试”和“真正共识用什么”必须分开写死。前者验证的是 `L0 chain_node/libp2p/LSMR` 的传播、存储和反熵；后者应该固定在 `L1 BFT-SMI + CometBFT` 这类 3-validator BFT。把这两层混成一条，会再次把 overlay/sync 和 finality 写乱。 |
| 新发现 | 现成脚本默认还是 `arm64-apple-darwin`。所以“本机 + 64 + DMIT”这件事，现在只有在远端也是同平台时才谈得上直接搬产物；如果远端是常见 Linux VPS，就必须先补 Linux 目标，而不是假装当前 `build_chain_node_v3.sh` 已经能跨机部署。 |

## Findings

| 项目 | 结论 |
|---|---|
| 新发现 | compare 这条线一旦生成包里已经有正式 `truth_compare*` 模块，helper 就不能再继续覆写 engine/main。最稳形状是“生成包固定持有 engine/main，helper 只特化 data 模块”；否则 codegen 和 helper 会各维护一套 compare 逻辑，后面必然再漂。 |
| 新发现 | 当前 ordinary compare engine 里不能再走 `strutil.intToStr(...)` 这种 `stmt_call` 形状。`truth_compare` 这轮已经坐实，它会直接把 `system-link-exec` 打成 `primary_object_body_semantics_missing`；最稳收法是把整数 JSON 发射改成手写 digit emitter。 |
| 新发现 | [content_codec_smoke.cheng](/Users/lbcheng/cheng-lang/v3/src/tests/content_codec_smoke.cheng) 这次 `stage2 rc=139` 的真根不是协议逻辑，而是 smoke 自己那坨手搓 preview manifest/RSA payload 和大块重复字节装配。把它收成 `manifest/chunk/range/stub` 的直接编解码闭环以后，host、`stage2`、`stage3` 三条编译运行路径就一起稳定了。 |
| 新发现 | [content_runtime_smoke.cheng](/Users/lbcheng/cheng-lang/v3/src/tests/content_runtime_smoke.cheng) 最不稳的不是 `requestFetch/requestShardPull` 主链，而是 preview helper 和 parity/test-only 大形状：一条会在 host 跑进 [v3ContentManifestPayloadFromStub](/Users/lbcheng/cheng-lang/v3/src/chain/content_plane.cheng) 崩，另一条会把 `stage2/stage3` 编译器直接打成 `rc=139`。当前最稳收法就是只保留 `manifest/blob/shard cache + requestFetch/requestShardPull + rebuildBlob` 这条 runtime 主路径，把 preview/parity 从正式 gate 拿掉。 |
| 新发现 | `QUIC/TLS` 切片 probe 继续留在正式总 gate 只会制造假阻塞，最稳边界还是“两层分工”：host 继续承担 `native_client_hello_wire` 这类宿主侧 QUIC-native 覆盖，`stage23` 只保留已经长期稳定的 `libp2p/content/chain_node/browser/fresh-node/migration` 主链。按这个边界收口后，完整 [run_v3_host_smokes.sh](/Users/lbcheng/cheng-lang/v3/tooling/run_v3_host_smokes.sh) 和 [run_v3_stage23_libp2p_smokes.sh](/Users/lbcheng/cheng-lang/v3/tooling/run_v3_stage23_libp2p_smokes.sh) 都已重新全绿。 |
| 新发现 | 这轮 migration gate 没红，但完整 [run_v3_host_smokes.sh](/Users/lbcheng/cheng-lang/v3/tooling/run_v3_host_smokes.sh) 继续会在 [native_initial_crypto_frame_smoke.cheng](/Users/lbcheng/cheng-lang/v3/src/tests/native_initial_crypto_frame_smoke.cheng) 这里撞一个独立旧红灯：backend driver 直接 `rc=139`，而不是 `migrate-csg/prove-migration` 失败。也就是说，迁移链已经打通，当前剩下的是 host 编译器对这条 QUIC smoke 的单独崩溃。 |
| 新发现 | `旧语法/旧语义` 真正可升级的对象不是源码文本，而是“迁移后重新导出的 canonical graph/export surface/compile receipt”。这轮把 [cheng_v3_seed.c](/Users/lbcheng/cheng-lang/v3/bootstrap/cheng_v3_seed.c) 收成 `legacy -> normalize -> canonical CSG` 后，`prove-migration` 才能机械证明 `graph_equivalent=1 / export_surface_compatible=1 / compile_receipt_equivalent=1`。 |
| 新发现 | migration proof 不能复用 baseline proof 的输入面。`--legacy-in` 和 `--baseline-csg/--baseline-surface/--baseline-receipt` 必须互斥；否则 stable gate 会把“旧源码迁移证明”和“同语法 world 比对”混成一类，结论不再严谨。当前 seed 已经把这两类 proof 在命令面上硬分开。 |
| 新发现 | canonical compare 必须优先看 `canonical_graph_cid / canonical_compiler_csg_cid / canonical_output_digest`，旧字段只保留兼容读取。否则迁移后即使语义已经收敛，proof 也会被旧 report 字段拖回路径相关或图形态相关的假差异。 |
| 新发现 | source override 不能直接拿共享字符串进 `strsep(...)` 扫两遍。第一次扫描会改写缓冲，第二次就会漏函数；这轮最早的 migration 假失败就是这样把 `main` 丢了。当前稳定做法是每次扫描前都复制一份 override text，再做 count/build 两轮解析。 |
| 新发现 | migration gate 不该只靠 `.cheng` smoke。当前最硬的验收面是新增的 [run_v3_migration_gate.sh](/Users/lbcheng/cheng-lang/v3/tooling/run_v3_migration_gate.sh)：它真跑 `migrate-csg -> prove-migration -> stable reject(no proof) -> stable publish(with legacy proof)`，而且 host、`stage2`、`stage3` 三条入口都已经前台打通。 |

## Findings

| 项目 | 结论 |
|---|---|
| 新发现 | `stage2/stage3` 对 [V3ErasureShardManifest](/Users/lbcheng/cheng-lang/v3/src/chain/erasure_swarm.cheng) 和 [V3ErasureRecoveryPlan](/Users/lbcheng/cheng-lang/v3/src/chain/erasure_swarm.cheng) 这类大 record 的按值传参，仍然会在内容链后半段露出不稳定面。`content_stub_smoke` 这次死在 `build pull targets`，根因不是协议错，而是 `manifest + plan` 整体穿透到 [v3ContentRuntimeBuildPullTargets(...)](/Users/lbcheng/cheng-lang/v3/src/chain/content_runtime.cheng) 后再二次构造 request。 |
| 新发现 | 这条链当前最稳的写法，是把 `pull target/request` 的入口收成字段级 API：`manifestCid + shardIndices + shardPayloadCids + request*`。只要继续让 [content_runtime.cheng](/Users/lbcheng/cheng-lang/v3/src/chain/content_runtime.cheng) 和 [erasure_swarm.cheng](/Users/lbcheng/cheng-lang/v3/src/chain/erasure_swarm.cheng) 走这条小参数路径，`content_stub_smoke`、`content_runtime_smoke` 和完整 `stage23 libp2p` gate 都会稳定。 |
| 新发现 | [v3ErasureShardPullRequestFill(...)](/Users/lbcheng/cheng-lang/v3/src/chain/erasure_swarm.cheng) 需要保留一个字段级兄弟入口，而不是逼所有调用方都重新塞回整份 manifest。现在新增的 [v3ErasureShardPullRequestFillFromFields(...)](/Users/lbcheng/cheng-lang/v3/src/chain/erasure_swarm.cheng) 已经把这层重复大对象传递打断，后面凡是 recovery-plan decode 后再拉 shard 的路径，都应该优先走这个入口。 |

## Findings

| 项目 | 结论 |
|---|---|
| 新发现 | `CSG migration/equivalence` 这条线不能只停在源码 help。[stage1_bootstrap.cheng](/Users/lbcheng/cheng-lang/v3/bootstrap/stage1_bootstrap.cheng) 和 [cheng_v3_seed.c](/Users/lbcheng/cheng-lang/v3/bootstrap/cheng_v3_seed.c) 如果不一起前移，backend driver 和 bootstrap 实际上就不会真认 `prove-equivalence/publish-world`，用户看到的只是“源码里有命令名，外部入口却不能跑”。 |
| 新发现 | compile receipt 的 `output_digest` 不能掺 `output_path`。[compiler_csg.cheng](/Users/lbcheng/cheng-lang/v3/src/tooling/compiler_csg.cheng) 源码侧早就把输出路径排除在 digest 之外；seed C 如果把 `plan->output_path` 也哈进去，就会把同一 world/source 的 receipt 变成路径相关，直接破坏 canonical equivalence。 |
| 新发现 | `stable/edge` 不能只存在命令行参数上，必须真的进 `package snapshot` 和 `world head cid`。这轮把 channel 真接进 [cheng_v3_seed.c](/Users/lbcheng/cheng-lang/v3/bootstrap/cheng_v3_seed.c) 后，`publish-world --channel:edge` 才会拿到不同于 `stable` 的 `world_head_cid/receipt_cid`，stable gate 也才有真实意义。 |
| 新发现 | C 侧 report parser 里，64 字节 CID 的文本缓冲必须开 `65`。这轮 `surface_cid` 一开始就是因为只给了 `64`，末尾被截断，`prove-equivalence` 直接假失败。以后凡是 seed 里读十六进制 CID，都必须用 [CHENG_V3_CID_HEX_CAP](/Users/lbcheng/cheng-lang/v3/bootstrap/cheng_v3_seed.c)。 |
| 新发现 | seed 的调用参数上限不能再停在 `12`。当前 `v3` 源码面已经有 `16` 参数的真实调用；如果 [cheng_v3_seed.c](/Users/lbcheng/cheng-lang/v3/bootstrap/cheng_v3_seed.c) 不把 `CHENG_V3_MAX_CALL_ARGS` 提到 `16`，`compiler_equivalence/publish` 这条线会先死在调用推断，连 ordinary smoke 都进不去。 |
| 新发现 | ordinary smoke 里的报告 fixture 不能再写成多段 `"..." + "..."`。这次反汇编已经坐实，当前 Cheng 在这类写法下会把 `+` 和引号一起落进字面量池，直接把 `graph_cid/surface/receipt` 文本喂坏；稳定写法是单个字符串字面量里直接放 `\\n`。 |
| 新发现 | [compiler_equivalence_smoke.cheng](/Users/lbcheng/cheng-lang/v3/src/tests/compiler_equivalence_smoke.cheng) 和 [compiler_publish_smoke.cheng](/Users/lbcheng/cheng-lang/v3/src/tests/compiler_publish_smoke.cheng) 现在已经进了 ordinary 默认 gate。[run_v3_host_smokes.sh](/Users/lbcheng/cheng-lang/v3/tooling/run_v3_host_smokes.sh) 和 [run_v3_stage23_libp2p_smokes.sh](/Users/lbcheng/cheng-lang/v3/tooling/run_v3_stage23_libp2p_smokes.sh) 默认列表都已补齐，host、`stage2`、`stage3` 也都已经真编真跑通过。 |
| 新发现 | 这两条普通 smoke 现在刻意只钉住最稳的 primitives：`GraphCidFromText / EquivalenceKind / PublishDecisionMake`。完整 `proof/report` builder 仍然保留命令级验证口径，这不是缺功能，而是当前 ordinary 编译面对超宽 helper 和大 report 体还没必要硬摊。 |
| 新发现 | [run_v3_stage23_libp2p_smokes.sh](/Users/lbcheng/cheng-lang/v3/tooling/run_v3_stage23_libp2p_smokes.sh) 当前不会按命令行参数裁剪测试列表。要验证单条 smoke 是否真能进 `stage2/stage3`，稳定做法还是直接用 `cheng.stage2/cheng.stage3 system-link-exec` 编那两个源码入口；整条 runner 继续只适合跑完整列表。 |

## Findings

| 项目 | 结论 |
|---|---|
| 新发现 | [run_v3_stage23_libp2p_smokes.sh](/Users/lbcheng/cheng-lang/v3/tooling/run_v3_stage23_libp2p_smokes.sh) 不能并发跑两套。它们共用 [artifacts/v3_stage23_libp2p](/Users/lbcheng/cheng-lang/artifacts/v3_stage23_libp2p) 输出目录，会互删正在生成的二进制，直接制造“compile 成功但 run 时文件不存在”的假红灯。后面这条总 gate 必须单套串行跑。 |
| 新发现 | [msquicNativePacketTypeCode(...)](/Users/lbcheng/cheng-lang/v3/src/quic/native_runtime.cheng) 这种直接 `return int32(enum)` 的返回式，fresh `stage2` primary-object 仍可能掉进 `primary_object_call_target_missing`。当前稳定解是显式分支把 `QuicPacketType` 映射到 `short/initial/0rtt/handshake/retry` 的整型常量，不赌 return-call lowering。 |
| 新发现 | [tailnet_train_island_smoke.cheng](/Users/lbcheng/cheng-lang/v3/src/tests/tailnet_train_island_smoke.cheng) 现在已经不是旧的 libp2p 流程烟，而是直接走 [v3TailnetTrainIslandSelfTest()](/Users/lbcheng/cheng-lang/v3/src/project/tailnet_train_island.cheng)。这条 smoke 当前真钉住的是 `worker lease / step barrier / merge round / optimizer-bearing checkpoint version/history/lineage / stale parent reject`，后面更新文档时不能再拿它冒充整条 writeback 烟。 |
| 新发现 | `compiler world/canonical csg` 这条线最稳的落点，必须先挂在现有 `system_link -> lowering -> object/native link stub` 主链里，而不是另起一条“平行编译线”。这轮把 [lowering_plan.cheng](/Users/lbcheng/cheng-lang/v3/src/backend/lowering_plan.cheng) 改成先建 canonical CSG、再从 CSG 提炼 lowering surface 后，现有 [lowering_plan_smoke.cheng](/Users/lbcheng/cheng-lang/v3/src/tests/lowering_plan_smoke.cheng) 继续前台 `ok`，说明“先 CSG 后 lowering”这条骨架已经能站住。 |
| 新发现 | 当前 host ordinary 真正不稳的不是功能语义，而是源码形状：大 record 构造、多 composite 参数 helper、超长 report 拼接、条件里直接挂复合值/布尔短路，都容易把 seed 编译面撞坏。`compiler_csg`、`compiler_world`、`system_link_exec` 这轮最后稳定下来的公共写法已经坐实成：逐字段赋值、小 helper、局部量展开、顺序追加字符串，不赌编译器推断。 |
| 新发现 | bootstrap 物化物现在已经不是旧口径了。[cheng_v3_seed.c](/Users/lbcheng/cheng-lang/v3/bootstrap/cheng_v3_seed.c) 和 [stage1_bootstrap.cheng](/Users/lbcheng/cheng-lang/v3/bootstrap/stage1_bootstrap.cheng) 已一起前移，`/artifacts/v3_backend_driver/cheng` 真实暴露 `emit-csg / verify-world / world-sync / selfhost-build`，`status` 也已经切到 `canonical_csg_verified_primary_object_codegen_missing`。 |
| 新发现 | `compiler world` 这条源码自编译路径还有一类容易踩炸的真坑：把 hash/report/provider source 这类逻辑拆成“大 composite 参数 helper”后，host/runtime 很容易在传参或数组扩容时炸掉。当前稳定解已经落实成“hash 内联回构造函数、provider source 直接按模块映射路径、smoke 直接验 plan 字段或同步结果”，不再走那层脆 helper。 |
| 新发现 | `libp2p world distribution` 这条线现在已经不是“模块存在但没跑”。[compiler_world_libp2p_smoke.cheng](/Users/lbcheng/cheng-lang/v3/src/tests/compiler_world_libp2p_smoke.cheng) 现已直接走 `build plan -> publish universe -> sync -> verify synced head cid`，并在 host、`stage2`、`stage3` 都前台 `ok`。当前刻意保留的唯一硬边界，只剩 `selfhost-build` 仍显式停在 `native link not implemented`，没有伪装完成。 |

## Findings

| 项目 | 结论 |
|---|---|
| 新发现 | [primary_object_plan.cheng](/Users/lbcheng/cheng-lang/v3/src/backend/primary_object_plan.cheng) 这条 ordinary 主链当前不能留三元表达式，尤其是 `return_expr` 里的 `a ? b : c`。这轮把 [v3PrimaryObjectFormat(...)](/Users/lbcheng/cheng-lang/v3/src/backend/primary_object_plan.cheng) 和 report 里的同类写法改回普通 `if/return` 之后，`primary_object_plan_smoke`、完整 host、完整 `stage23` 都重新稳定通过。 |
| 新发现 | [run_v3_stage23_libp2p_smokes.sh](/Users/lbcheng/cheng-lang/v3/tooling/run_v3_stage23_libp2p_smokes.sh) 的浏览器尾段必须显式绑 `CHENG_V3_SMOKE_COMPILER=\"$stage3\"`。因为这条 runner 的目标本来就是验证 `stage2/stage3` 的协议主链；如果让它默认落回旧 backend-driver host compiler，就会被 host compiler 自己的旧表达式边界卡住，噪声大于信号。 |
| 新发现 | 这轮已经证明，试图把旧 backend-driver 的浏览器 host 编译上限硬摊平到 [system.cheng](/Users/lbcheng/cheng-lang/src/std/system.cheng)、[rawmem_support.cheng](/Users/lbcheng/cheng-lang/src/std/rawmem_support.cheng)、[peer_id.cheng](/Users/lbcheng/cheng-lang/v3/src/libp2p/core/peer_id.cheng)、[resources.cheng](/Users/lbcheng/cheng-lang/v3/src/libp2p/core/resources.cheng)、[webrtc_transport.cheng](/Users/lbcheng/cheng-lang/v3/src/libp2p/transports/webrtc_transport.cheng) 这种公共层里，不稳而且会反咬 host 总 gate。那批试刀已经全撤回；当前稳定解是修真正旧红灯，再把 `stage23` 的浏览器尾段边界写准。 |

## Findings

| 项目 | 结论 |
|---|---|
| 新发现 | [tcp_transport.cheng](/Users/lbcheng/cheng-lang/v3/src/libp2p/transports/tcp_transport.cheng) 和 [webrtc_transport.cheng](/Users/lbcheng/cheng-lang/v3/src/libp2p/transports/webrtc_transport.cheng) 这条 bridge 返回链，绝不能把本地 `str` 直接 `bytesFromString(...)` 后返回给上层。`bytesFromString` 只是借视图，不做拷贝；一旦桥函数返回，本地字符串生命周期结束，后面的 payload 读取就会漂。当前最稳的主线是 transport 自己先做 owned copy，再把 `Bytes` 交给 host/runtime。 |
| 新发现 | [pin_runtime_host.cheng](/Users/lbcheng/cheng-lang/v3/src/chain/pin_runtime_host.cheng) 里 proof 请求这条线，应该以“真正发出去的 challenge payload”为准，而不是盯住调用侧手上的那份复合值。当前稳定实现已经收成：`challenge -> requestPayload -> decode wire challenge -> proof validate against wire challenge`。这样 request-response 过后，即使 ordinary 对复合值内存态不够稳，也不会再把 proof 校验绑在一份漂掉的 challenge 上。 |
| 新发现 | [browser_sdk_smoke.cheng](/Users/lbcheng/cheng-lang/v3/src/tests/browser_sdk_smoke.cheng) 里的 stream 真实口径是 `5`，不是 `4`。原因很直接：浏览器 SDK 第一次业务请求前，会先经 [v3Libp2pBrowserSessionEnsure(...)](/Users/lbcheng/cheng-lang/v3/src/libp2p/host/host.cheng) 记一次 `webrtc signal` 流；后面再加 `sync / ingress / content fetch / shard pull` 四条业务流，所以总数就是 `1 + 4 = 5`。 |
| 新发现 | `peer cache` 这层最稳的边界，必须把 `admit` 和 `observe` 分开看。[v3Libp2pPeerRecordAdmitCache(...)](/Users/lbcheng/cheng-lang/v3/src/libp2p/core/peerstore.cheng) 现在负责“驻留、预算、淘汰”，[v3Libp2pPeerRecordObserveCache(...)](/Users/lbcheng/cheng-lang/v3/src/libp2p/core/peerstore.cheng) 负责“命中”；这样 `cache-aware` 排序和 cache 治理才不会把“已驻留”和“被命中”混成一回事。 |
| 新发现 | 训练岛这条线现在已经可以明确写成“单版本 checkpoint 同步闭环已打实”。[tailnet_train_island.cheng](/Users/lbcheng/cheng-lang/v3/src/project/tailnet_train_island.cheng) 这轮已经把 `delta sync + merge + stateful writeback` 真接起来，[tailnet_train_island_smoke.cheng](/Users/lbcheng/cheng-lang/v3/src/tests/tailnet_train_island_smoke.cheng) 的 host、`stage2`、`stage3` 也都前台 `ok`；真正还不能写成已完成的，只剩梯度同步、optimizer 状态和多 worker 训练协议。 |
| 新发现 | `cache-aware` 这条线当前最稳的实现，不是再包一层 `peer cache score helper`，而是直接在 [v3Libp2pPeerPlacementScoreFill(...)](/Users/lbcheng/cheng-lang/v3/src/libp2p/core/scheduler.cheng) 里遍历 `record.cacheEntries`，原地算 `cacheKeyMatches/cacheHitCount/cacheLastHitEpochSeconds`。这轮 raw peerstore 数据其实一直是对的，真正不稳的是“复合 record + 多字符串参数 + `var out` helper”那条 ordinary 调用面。 |
| 新发现 | 现在可以把推理侧口径明确写成“单 host `cache-aware + sticky + affinity + top-N replica` 选 peer”。[peerstore.cheng](/Users/lbcheng/cheng-lang/v3/src/libp2p/core/peerstore.cheng) 已有 peer cache 样本，[scheduler.cheng](/Users/lbcheng/cheng-lang/v3/src/libp2p/core/scheduler.cheng) 已按 cache hint 排序，[host.cheng](/Users/lbcheng/cheng-lang/v3/src/libp2p/host/host.cheng) 也已把这条链接进 host 选择；但它仍然不是全网 rendezvous/job scheduler。 |
| 新发现 | 训练岛 checkpoint 的 `query/history/lineage` 不能再挂在 [tailnet_train_island_libp2p.cheng](/Users/lbcheng/cheng-lang/v3/src/project/tailnet_train_island_libp2p.cheng) 这种 wrapper 上。只要 wrapper 直接吃 `layout.FixedBytes32` 这类 composite 参数，host lowering 就会真炸；当前稳定边界是把这些轻量面留在 [tailnet_train_island.cheng](/Users/lbcheng/cheng-lang/v3/src/project/tailnet_train_island.cheng)，`libp2p` 层只保留 worker-group/fetch/rebuild。 |
| 新发现 | 训练岛现在可以正式写成“有显式 checkpoint 历史面，而且单版本 writeback 闭环已验证”。[tailnet_train_island.cheng](/Users/lbcheng/cheng-lang/v3/src/project/tailnet_train_island.cheng) 这轮已把 `V3TailnetCheckpointSurface` 扩成 `versions[]`，并补 `append/query/history/lineage/latest-lineage`；同时 `stateful writeback` 也已经被 smoke 打实。真正还没完成的，是把这条线继续长成完整训练协议。 |

## Findings

| 项目 | 结论 |
|---|---|
| 新发现 | `v3` 现在已经不止是单点 best-fit 了。[scheduler.cheng](/Users/lbcheng/cheng-lang/v3/src/libp2p/core/scheduler.cheng) 和 [host.cheng](/Users/lbcheng/cheng-lang/v3/src/libp2p/host/host.cheng) 这轮已经把 `sticky / affinity / top-N replica` 收进 `trustedInfra` 视图，所以它现在能直接承接推理 cache 复用和 KV 亲和这类场景；但这仍然只是单 host 视角的 placement，不是全网调度器。 |
| 新发现 | `tailnet` 训练岛最稳的落点已经坐实：应该新长在 [tailnet_train_island.cheng](/Users/lbcheng/cheng-lang/v3/src/project/tailnet_train_island.cheng) / [tailnet_train_island_libp2p.cheng](/Users/lbcheng/cheng-lang/v3/src/project/tailnet_train_island_libp2p.cheng) 这种 project 层，而不是去污染 `transport/core/pin/content` 主线。当前已验证链路已经推进到 `rendezvous -> worker group -> checkpoint descriptor -> manifest pull -> shard pull -> rebuild -> delta sync -> merge -> writeback`。 |
| 新发现 | 训练岛这条线现在仍然不能吹成“完整训练协议”，但已经可以讲成“单版本 checkpoint 同步闭环”。现在缺的不是 `writeback`，而是梯度同步、optimizer 状态、多 worker merge 和公网训练编排。 |
| 新发现 | 这轮再次钉实了 ordinary smoke 的稳定形状：少 helper、少复合返回、主线展开。训练岛 smoke 最后之所以稳定，是因为把 checkpoint version/surface 压平，把 shard seeding 完全展开；一旦把 `Bytes[]` 索引、分支内复合构造和大 helper 返回重新塞回去，就会重新撞 ABI/seed 边界。 |

## Findings

| 项目 | 结论 |
|---|---|
| 新发现 | `resources + identify` 补完后，最短且最稳的下一步不是直接碰全网训练编排，而是先在 `trustedInfra` 视图里落一个单 host 的 best-fit 选 peer 层。现在 [scheduler.cheng](/Users/lbcheng/cheng-lang/v3/src/libp2p/core/scheduler.cheng) 已经能按 `train / infer / other`、CPU/内存/磁盘/GPU/NPU 门槛、样本新鲜度、负载和信任分做资源资格判断与排序，[host.cheng](/Users/lbcheng/cheng-lang/v3/src/libp2p/host/host.cheng) 也已经能直接返回匹配 peer 和端点。 |
| 新发现 | `v3` 当前能讲的是“资源感知的 peer 选择”，还不能讲“全网 job scheduler”。这轮实现只有单 host 视角的 best-fit 选 peer，没有 rendezvous、没有多 worker 编排、没有价格撮合、没有结算，更没有训练态的梯度/optimizer/checkpoint 协议。文档口径必须继续把这条边界写死。 |
| 新发现 | `NodeResource / PeerId` 这类复合值在 ordinary program smoke 里，仍然不适合穿过自造 helper 返回面或复杂 helper 调用面。这轮一开始把资源样本和 peer 建模塞进 helper 后，先后撞上字段错位和 `emit stmt call failed`；把 smoke 改回主函数内的朴素直线后，host、stage2、stage3 都立即稳定 `ok`。这说明当前最稳的验证形状依然是“少 helper、少复合返回、主线展开”。 |

## Findings

| 项目 | 结论 |
|---|---|
| 新发现 | `headscale/DERP` 这条 provider payload 现在不能放回 Cheng 层解析。当前 [json_runtime.cheng](/Users/lbcheng/cheng-lang/src/runtime/json_runtime.cheng) / bootstrap 还没有可用的 `parseJson` 主线，所以这层真实 probe 只能先落在 [system_helpers.c](/Users/lbcheng/cheng-lang/src/runtime/native/system_helpers.c)。强行把它搬回 Cheng，只会把“真 provider”又退回 synthetic 状态位。 |
| 新发现 | `controlUrl/controlEndpoint/relayEndpoint` 和 `controlProbeEndpoint/relayProbeEndpoint` 必须分开。前者是公网控制面与中继元数据，决定 `tailnetDerpMapSummary` 和 route text；后者只是本地 probe 输入。把两者混成一个字段，会直接把状态展示和选路文本污染成测试路径。 |
| 新发现 | `tailnet control` 的 restore 真闭环必须把 `controlProbeEndpoint/relayProbeEndpoint` 一起带过 state wire。只恢复 `controlUrl/controlEndpoint/relayEndpoint` 不够，因为 headscale provider 的 `provider-start/proxy-ready/derp-ready` 现在都会真去 runtime 拉 probe payload。少这两个字段，恢复出来的 headscale transport 会直接失真。 |
| 新发现 | 最稳的真 provider 验收口径已经坐实：单测里用文件 probe，CLI runner 里起本地 HTTP probe server。这样既能保持 [libp2p_tailnet_derp_smoke.cheng](/Users/lbcheng/cheng-lang/v3/src/tests/libp2p_tailnet_derp_smoke.cheng) 和 [tailnet_control_core_smoke.cheng](/Users/lbcheng/cheng-lang/v3/src/tests/tailnet_control_core_smoke.cheng) 的确定性，又能让 [run_v3_tailnet_control_smoke.sh](/Users/lbcheng/cheng-lang/v3/tooling/run_v3_tailnet_control_smoke.sh) 真走一遍 `headscale + DERP + HTTP probe`。 |
| 新发现 | 这轮 `Unimaker content preview manifest` 的真收口方式已经坐实：不要再把 `stub fields -> helper -> Build/MustFill` 这条复合 ABI 边界放在主门禁上。最稳的做法是像 [content_codec_smoke.cheng](/Users/lbcheng/cheng-lang/v3/src/tests/content_codec_smoke.cheng) 和 [content_runtime_smoke.cheng](/Users/lbcheng/cheng-lang/v3/src/tests/content_runtime_smoke.cheng) 现在这样，直接走 `authorId -> postCid -> signable -> signature -> manifestCid -> payload` 的 bytes 直线。 |
| 新发现 | 这次真根不是 `manifest` 语义错，而是 Cheng 当前 ABI 对“多 `FixedBytes32` + `Bytes` + helper 返回”的组合仍然不稳。只要把 preview payload 重新压回 bytes 直线，[content_codec_smoke.cheng](/Users/lbcheng/cheng-lang/v3/src/tests/content_codec_smoke.cheng)、[content_runtime_smoke.cheng](/Users/lbcheng/cheng-lang/v3/src/tests/content_runtime_smoke.cheng)、[content_stub_smoke.cheng](/Users/lbcheng/cheng-lang/v3/src/tests/content_stub_smoke.cheng)、[content_quic_smoke.cheng](/Users/lbcheng/cheng-lang/v3/src/tests/content_quic_smoke.cheng) 就会一起稳定前台 `ok`。 |
| 新发现 | 这轮已经把“只过 targeted”这个风险排掉了。最终完整 [run_v3_host_smokes.sh](/Users/lbcheng/cheng-lang/v3/tooling/run_v3_host_smokes.sh) 输出 `v3 host smokes: ok`，完整 no-cache [run_v3_stage23_libp2p_smokes.sh](/Users/lbcheng/cheng-lang/v3/tooling/run_v3_stage23_libp2p_smokes.sh) 输出 `v3 stage2/stage3 libp2p: ok`，说明内容 preview manifest 这条修补没有再把 QUIC/libp2p/chain_node 主链拖坏。 |

## Findings

| 项目 | 结论 |
|---|---|
| 新发现 | 这轮 `Unimaker content manifest` 的真 blocker 不是业务协议，而是 Cheng 现有 ABI 对“大对象/多参数/`Bytes` 返回”的组合不稳。实际已经先后踩到了 `test_pki` 私钥 bytes 返回、`rsa` 大私钥对象返回、`x509PublicKeyBytes(...)` 返回、`V3ContentStub` 按值返回、`V3ContentManifest` 解码返回，以及 `FixedBytes32 -> Bytes` 再作为参数传入的边界。每往前推一步，炸点都会从一个返回面前移到下一个返回面，但都不是协议语义错误。 |
| 新发现 | [system_helpers.c](/Users/lbcheng/cheng-lang/src/runtime/native/system_helpers.c) 这轮暴露的是独立的 Darwin 宿主编译面问题，不是 `content` 逻辑：`INADDR_LOOPBACK`、`RTLD_DEFAULT`、`environ`、`cheng_exec_wait_status` 和 arm64 signal context getter 都需要先收正，否则连最小 host smoke 都起不来。把这些补齐后，[fixed256_curve25519_smoke](/Users/lbcheng/cheng-lang/v3/src/tests/fixed256_curve25519_smoke.cheng) 已重新前台 `ok`，说明宿主全局编译面已经拉回来了。 |
| 新发现 | 内容线当前最稳的形状不是 `V3ContentManifest` 大对象，而是“签名后的 manifest payload bytes”。这轮已经在 [content_plane.cheng](/Users/lbcheng/cheng-lang/v3/src/chain/content_plane.cheng) 补了 `v3ContentManifestPayloadMake(...)`，并把 [content_runtime_smoke.cheng](/Users/lbcheng/cheng-lang/v3/src/tests/content_runtime_smoke.cheng) / [content_codec_smoke.cheng](/Users/lbcheng/cheng-lang/v3/src/tests/content_codec_smoke.cheng) 往 `payload + ValidatePayload + StorePreviewPayload` 这条窄链收；但只要继续穿过现有 `Result[V3ContentManifest]` 或多 `Bytes`/`FixedBytes32` 交错参数边界，smoke 还是会在新位置炸。 |
| 新发现 | [RWAD密码学身份.md](/Users/lbcheng/cheng-lang/v3/docs/RWAD密码学身份.md) 之前写错了边界。文里写的是“连续区间树 + 累加器 + Batch zk 的原子级 RWAD 身份系统”，但当前仓库真正落地的是 [rwad_serial_state_machine.cheng](/Users/lbcheng/cheng-lang/v3/src/project/rwad_serial_state_machine.cheng) 和 [rwad_bft_state_machine.cheng](/Users/lbcheng/cheng-lang/v3/src/project/rwad_bft_state_machine.cheng) 这套 `note/nullifier/nullifierRoot`。这两层不是一回事，所以这份文档必须改成“目标态设计 + 当前实现边界”，不能再冒充现状文档。 |
| 新发现 | [Unimaker内容发布与分发协议v1.md](/Users/lbcheng/cheng-lang/v3/docs/Unimaker内容发布与分发协议v1.md) 的 `Pin` 现状有一处已经落后代码。当前代码里 [pin_runtime.cheng](/Users/lbcheng/cheng-lang/v3/src/chain/pin_runtime.cheng) 已经有 `schedule/retry/reward/slash/next_epoch` 调度链，[pin_scheduler_smoke.cheng](/Users/lbcheng/cheng-lang/v3/src/tests/pin_scheduler_smoke.cheng) 也已经真跑覆盖，所以文档不能再写“还没接调度器”；真正还没落地的是 `随机信标/资金托管/记账`。 |
| 新发现 | 这轮 host 总 gate 的真 blocker 不是 `Pin scheduler` 协议逻辑，而是 [tailnet_transport.cheng](/Users/lbcheng/cheng-lang/v3/src/libp2p/transports/tailnet_transport.cheng) 的 [v3Libp2pTailnetUpsertPeerSession(...)](/Users/lbcheng/cheng-lang/v3/src/libp2p/transports/tailnet_transport.cheng) ABI 形状。第一次试着在函数开头加 `echo(...)` 也完全打不出来，说明段错发生在“多 `str` 实参 + 复合调用边界”之前，不在函数体逻辑里；把它收成“只吃一个 `V3Libp2pTailnetPeerSession` record”后，[libp2p_tailnet_transport_smoke.cheng](/Users/lbcheng/cheng-lang/v3/src/tests/libp2p_tailnet_transport_smoke.cheng)、[libp2p_tailnet_derp_smoke.cheng](/Users/lbcheng/cheng-lang/v3/src/tests/libp2p_tailnet_derp_smoke.cheng)、[chain_node_tailnet_smoke.cheng](/Users/lbcheng/cheng-lang/v3/src/tests/chain_node_tailnet_smoke.cheng) 都重新前台 `ok`。 |
| 新发现 | `tailnet` 的 `peerSessions` 不该继续用字符串回编解码做内存态存储。当前最稳形状是直接把它留成 `V3Libp2pTailnetPeerSession[]`，查找、状态填充和 status 输出都直接读 record，不再在 transport 内部自己序列化再反序列化。这样最短，也最不容易再踩 `str/split` 这类 ABI 旧坑。 |
| 新发现 | [strutils.cheng](/Users/lbcheng/cheng-lang/src/std/strutils.cheng) 这轮暴露出来的 `chengStrStoreCompat/strutilsAppendStr` 编译缺口，也说明 `strutils` 不该再保留那层自造兼容包装。把 `strutilsAppendStr(...)` 直接收成 `add(seqInst, val)` 以后，host gate 的前置编译缺口立刻消失，而且完整 [run_v3_host_smokes.sh](/Users/lbcheng/cheng-lang/v3/tooling/run_v3_host_smokes.sh) 与 no-cache [run_v3_stage23_libp2p_smokes.sh](/Users/lbcheng/cheng-lang/v3/tooling/run_v3_stage23_libp2p_smokes.sh) 这轮都已重新前台 `ok`。 |
| 新发现 | WebRTC 这轮最终稳定边界已经坐实：原生 datachannel 内容桥是宿主专属路径，不该继续留在 [content_runtime_host.cheng](/Users/lbcheng/cheng-lang/v3/src/chain/content_runtime_host.cheng) 这种公共内容宿主层里。现在公共层只保留 TCP 主链，新增 [content_runtime_host_webrtc.cheng](/Users/lbcheng/cheng-lang/v3/src/chain/content_runtime_host_webrtc.cheng) 专门承接 host-native WebRTC 内容拉取；[webrtc_datachannel_content_smoke.cheng](/Users/lbcheng/cheng-lang/v3/src/tests/webrtc_datachannel_content_smoke.cheng) 因此应留在 host gate，不该继续塞进 `stage23`。 |
| 新发现 | 这轮 WebRTC 收口已经不是局部烟绿。定向 5 条 WebRTC smoke、完整 [run_v3_host_smokes.sh](/Users/lbcheng/cheng-lang/v3/tooling/run_v3_host_smokes.sh) 和 no-cache [run_v3_stage23_libp2p_smokes.sh](/Users/lbcheng/cheng-lang/v3/tooling/run_v3_stage23_libp2p_smokes.sh) 都重新前台 `ok`，说明“signal transcript 过网 + native datachannel request-response”这条新边界没有把 QUIC/content/chain_node 后链拖坏。 |
| 新发现 | WebRTC 这条线之前真正假的地方已经坐实：在 [host.cheng](/Users/lbcheng/cheng-lang/v3/src/libp2p/host/host.cheng) 和 [content_runtime_host.cheng](/Users/lbcheng/cheng-lang/v3/src/chain/content_runtime_host.cheng) 里，虽然先开了 `ProtocolWebrtcSignal` stream，但后面立刻掉 [v3Libp2pWebrtcLoopbackRequestResponse(...)](/Users/lbcheng/cheng-lang/v3/src/libp2p/transports/webrtc_transport.cheng)。这轮把它收成“先准备 signal transcript bytes，再走 native datachannel request-response”以后，host 定向 5 条 WebRTC smoke 都重新前台 `ok`。 |
| 新发现 | 当前 seed 对 `importc fn` 解析还有一个硬边界：声明行必须单行。新加的 `cheng_v3_webrtc_datachannel_request_response_bridge(...)` 一开始写成多行 `importc fn`，fresh host 直接报 `scalar call resolve failed`；改成单行声明后，外部符号解析立刻恢复。这不是 WebRTC 逻辑问题，是 seed 的 importc 解析形状限制。 |
| 新发现 | WebRTC 这轮最值的可观测量已经定死：signal 不能只看 `openStreamProtocols`，必须同时看 [host.cheng](/Users/lbcheng/cheng-lang/v3/src/libp2p/host/host.cheng) 里的 `webrtcSignalIngressPayloads/webrtcSignalEgressPayloads`，再看 [webrtc_transport.cheng](/Users/lbcheng/cheng-lang/v3/src/libp2p/transports/webrtc_transport.cheng) 里的 `nativeDatachannel*Count` 和 `loopbackRequestResponseCount`。只看业务 payload 对不对，假闭环还会混过去。 |
| 新发现 | 这轮 native bridge 不该再走“单通道模糊转发”。[system_helpers.c](/Users/lbcheng/cheng-lang/src/runtime/native/system_helpers.c) 里现在明确分成 control socketpair 和 data socketpair：signal transcript 先过控制面并做 ack，再走 multistream + request/response 数据面。这条形状才和“signal + datachannel”两层边界对齐。 |
| 新发现 | 这轮把整套 no-cache `stage23` 真重跑以后，新的真 blocker 不是 `tailnet/headscale/DERP/WebRTC` 逻辑，而是 [host_quic.cheng](/Users/lbcheng/cheng-lang/v3/src/libp2p/host/host_quic.cheng) 的 [v3Libp2pQuicExchange(...)](/Users/lbcheng/cheng-lang/v3/src/libp2p/host/host_quic.cheng) 里直接绑定 `quic_transport.v3Libp2pQuicLoopbackSelectProtocol(...)` 的 `Result[bool]`。把它改成原地 `multistream select bytes -> write -> read -> accepts` 以后，`stage3 libp2p_quic_tls_smoke` 和完整 `stage23` 都重新全绿，说明这次卡的是 ordinary 句型，不是 QUIC 协议语义。 |
| 新发现 | [pin_runtime_quic_smoke.cheng](/Users/lbcheng/cheng-lang/v3/src/tests/pin_runtime_quic_smoke.cheng) 现在已经不再是“旁支旧洞”。这轮完整 [run_v3_host_smokes.sh](/Users/lbcheng/cheng-lang/v3/tooling/run_v3_host_smokes.sh) 和 no-cache [run_v3_stage23_libp2p_smokes.sh](/Users/lbcheng/cheng-lang/v3/tooling/run_v3_stage23_libp2p_smokes.sh) 都真跑到了它，并在 host、`stage2`、`stage3` 下全部输出 `ok`。 |
| 新发现 | `Pin` 这条线真正该补的不是动态注册表，而是 [host.cheng](/Users/lbcheng/cheng-lang/v3/src/libp2p/host/host.cheng) 和 [host_quic.cheng](/Users/lbcheng/cheng-lang/v3/src/libp2p/host/host_quic.cheng) 自己的正式协议分发表。最终稳定形状不是让 `sync` 和 `pin` 强行共用整条主链，而是只让 `Pin` 走内建 `ServePayload/RequestResponse`，`sync` 继续保留已经验证过的直通路径。 |
| 新发现 | `host_quic` 的通用分发不能先 `openStream` 再决定要不要委托给 `host_base`。这样非 QUIC 路径会重复记流、重复记 `syncRequestCount`，属于隐形状态污染。正确顺序是先看 endpoint transport，再决定走 `host_quic` 还是委托给 `host_base`。 |
| 新发现 | 当前 `Pin` 可以正式写成“host/host_quic 内建协议分发表已落地”，但仍然不能写成“动态可插拔 handler 注册表已落地”。现在落地的是宿主内建协议分发，不是独立插件式回调系统；`sync` 也没有被改成同一套通用 handler。 |

## Findings

| 项目 | 结论 |
|---|---|
| 新发现 | 这轮真正把 `content_runtime_smoke` 炸掉的不是 `manifest` 语义，也不是 `fetch decode` 本身，而是 [content_runtime_host.cheng](/Users/lbcheng/cheng-lang/v3/src/chain/content_runtime_host.cheng) 和 [content_runtime_host_quic.cheng](/Users/lbcheng/cheng-lang/v3/src/chain/content_runtime_host_quic.cheng) 里 `Result[Bytes]` 直接跨 transport 走。只修服务端一半不够，客户端收包后的 `responsePayload/responseBytes` 也必须先落成本地 `Bytes`，不然就会在 [v3ContentFetchResponseDecode(...)](/Users/lbcheng/cheng-lang/v3/src/chain/content_fetch.cheng) 里以 `bytesSlice` 段错的形式爆出来。 |
| 新发现 | `Pin` 这条线当前最稳的宿主形状还是 [host.cheng](/Users/lbcheng/cheng-lang/v3/src/libp2p/host/host.cheng) 直接带 `pinRuntime`。我试过单独旁挂 registry，但它既没接主线，也会把口径写乱；最短真修法就是删掉未接主线的 [pin_registry.cheng](/Users/lbcheng/cheng-lang/v3/src/libp2p/host/pin_registry.cheng)，保留现有宿主附着式 handler。 |
| 新发现 | 只要把 `content fetch` 的 `Bytes` ABI 收正，之前受连坐的 [chain_node_tailnet_smoke.cheng](/Users/lbcheng/cheng-lang/v3/src/tests/chain_node_tailnet_smoke.cheng) 也会继续稳定前台 `ok`。这说明这轮活根是共享 transport/ABI 边界，不是 `chain_node` 或 `Pin` 协议逻辑本身。 |

## Findings

| 项目 | 结论 |
|---|---|
| 新发现 | `Pin` 这层现在最稳的最小网络切法已经再往前收了一刀：`offer/accept/settlement` 走 `ingress + store put + sync fetch`，`challenge/proof` 走独立 `/cheng/pin/1.0.0` request-response，而且 proof 已经改成远端 `host.pinRuntime` 直接处理，不再让测试手喂 `remoteRuntime`。 |
| 新发现 | `host` 这层最小真改法不是抽象成通用回调注册表，而是明确加 `pinRuntimeAttached + pinRuntime + v3Libp2pPinServePayload(...)`。这样 `Pin` 先拥有真的宿主附着式 handler，代码短，边界也不飘。 |
| 新发现 | 当前 `Pin` 网络面现在可以写成“宿主附着式远端 handler 已落地”，但还不能写成“完整 Pin 网络已上线”。因为挑战调度器、随机信标、奖励账本和独立 handler 注册表都还没做。 |

## Findings

| 项目 | 结论 |
|---|---|
| 新发现 | `v3PinProofValidateAgainstInputs(...)` 不能只信任传进来的 `challenge`。这轮已把它收成“先跑 `v3PinChallengeValidate(...)`，再继续对 `accept/manifest/proof` 做交叉校验”，这样函数单独复用时也不会接受结构已经坏掉的 challenge。 |
| 新发现 | `Pin` 这层最稳的第一刀不是去改 `PubSub` 或 `overlay contracts`，而是先把独立 `pin_plane` 做实。只要 `offer/accept/challenge/proof/settlement` 五个对象、payload 校验和 reward/slash settlement 先闭环，后面再接 `store/sync` 或独立协议流时就不会把托管语义和内容分发语义混在一起。 |
| 新发现 | `v3PinOfferFromArtifacts(...)` 不该强依赖整套 `content publish artifacts` 全量校验。Pin 只需要 `bundleCid + blobSummary + manifest + erasureParams` 这几个真实锚点；继续要求 `pubsub/plumtree/recoveryPlan` 全齐，只会把 Pin 绑死在内容发布主链上。 |
| 新发现 | 当前 `Pin` challenge 最稳的实现是“按 `offerCid + acceptCid + manifestCid + challengeEpoch` 派生起点，再对 `manifest` 做确定性轮转抽样”。这不是经济级随机信标，但它已经足够把 challenge/proof/settlement 的对象边界和 shard 校验链打通。文档必须直接写明它不是完整随机挑战系统。 |
| 新发现 | `proof` 最稳的载荷面不是再造一层新 chunk 对象，而是直接复用 `manifest` 和 `V3ErasureShardPullResponse`：`proof` 只带被抽中的 shard payload，逐片回算 `shardCid`，同时对齐 `challenge.shardCids` 和 `manifest.shardPayloadCids`。这条线最短，也最不容易和 `content fetch` 口径打架。 |
| 新发现 | `content runtime cache` 不能再被文档误写成长期托管。现在 `preview/blob/shard cache` 只是命中层；真正的长期条款、挑战和 reward/slash 已经独立进 [pin_plane.cheng](/Users/lbcheng/cheng-lang/v3/src/chain/pin_plane.cheng)。 |

## Findings

| 项目 | 结论 |
|---|---|
| 新发现 | 这次 [content_stub_smoke.cheng](/Users/lbcheng/cheng-lang/v3/src/tests/content_stub_smoke.cheng) 真炸的点，不在 `content runtime store manifest` 本身，而在 smoke helper 之前那层口径：服务端 runtime cache 如果继续拿一份脱离发布面的对象去重编码，就可能和节点里已经发布出去的 manifest payload 脱节。最短真修法是直接让 helper 吃 `node.storeEntries[2].ingress.payload` 这份已经发布出去的 manifest payload，再走后续 `request manifest / shard pull / rebuild`。 |
| 新发现 | 发布边界现在必须钉成“编码后立刻校验再入库”。这轮 [libp2p_bridge.cheng](/Users/lbcheng/cheng-lang/v3/src/overlay/libp2p_bridge.cheng) 已把 `manifest/recovery plan` 两条链都收成 `Encode -> ValidatePayload -> StorePut`。这样 payload 只要一脏，就会在发布当场直接暴露，不会拖到后面的内容 smoke 才炸。 |
| 新发现 | `v3ContentRuntimeStoreManifestPayload(...)` 不是通病。独立 [content_runtime_smoke.cheng](/Users/lbcheng/cheng-lang/v3/src/tests/content_runtime_smoke.cheng) 和 [content_quic_smoke.cheng](/Users/lbcheng/cheng-lang/v3/src/tests/content_quic_smoke.cheng) 继续能直接吃 manifest 并前台 `ok`，说明这次问题是“喂进去的 payload 口径”而不是 runtime cache API 本身。 |

## Findings

| 项目 | 结论 |
|---|---|
| 新发现 | `GET_PREVIEW` 当前最稳口径就是返回 encoded `V3ContentStub`，不需要为了“看起来完整”再新造 preview 类型。这样首页入口、预览摘要和当前 `blobSummaryCid` 约束能直接闭合。 |
| 新发现 | `GET_RANGE` 必须直接锚定 `blobCid`，返回原始 blob 的精确 byte slice；这里不该做截断启发式，也不该偷偷改成“按 shard 拼再裁”。 |
| 新发现 | 只要 `RecoveryPlan` 允许选 parity shard，runtime 就必须真做 RS 重建，不能只会“数据 shard 齐全直接拼回”。这轮 [erasure_swarm.cheng](/Users/lbcheng/cheng-lang/v3/src/chain/erasure_swarm.cheng) 的 `v3ErasureEncodeShardPayloads(...)` 和 `v3ErasureRebuildBlob(...)` 已把这条链闭上了。 |
| 新发现 | `content_quic_smoke` 真正会无限挂住的根，不在 `fetch` 编解码，而在 [native_runtime.cheng](/Users/lbcheng/cheng-lang/v3/src/quic/native_runtime.cheng) 的阻塞式 UDP 收包链：`msquicNativeDialPumpReady()` 和 `msquicNativePipeRead()` 一旦等不到包，就会一直睡死在 blocking recv。最短真修法是把它们改成有界非阻塞泵，超时直接报错。 |
| 新发现 | `content_quic_smoke` 不该被拽成三次 QUIC 重握手性能基准。它的职责只是证明 QUIC 内容协议能通；`GET_PREVIEW/GET_RANGE` 的协议语义已经由 [content_codec_smoke.cheng](/Users/lbcheng/cheng-lang/v3/src/tests/content_codec_smoke.cheng) 和 [content_runtime_smoke.cheng](/Users/lbcheng/cheng-lang/v3/src/tests/content_runtime_smoke.cheng) 稳定覆盖。 |
| 新发现 | [content_stub_smoke.cheng](/Users/lbcheng/cheng-lang/v3/src/tests/content_stub_smoke.cheng) 里“从 `storeEntries[2]` 取 payload 再预热 manifest cache”是假的稳定性。这个 smoke 真正要测的是 `stub -> advert/manifest/recoveryPlan -> pull -> rebuild`，所以远端 runtime 预热 manifest 应该直接用已经构造好的 `manifest` 对象，不该把测试绑死在本地 store 顺序上。 |

## Findings

| 项目 | 结论 |
|---|---|
| 新发现 | host 侧确实要把基础 smoke 前移，但最短路径不是再抄一份 runner。最稳的收法是直接让 [run_v3_host_smokes.sh](/Users/lbcheng/cheng-lang/v3/tooling/run_v3_host_smokes.sh) 支持定向 smoke，再由 [run_slice_gate.sh](/Users/lbcheng/cheng-lang/v3/tooling/run_slice_gate.sh) 单独前置 `fixed256_sha256_smoke` 和 `default_init_literals_smoke`。这样编译器检查、日志清理、报错打印都只保留一份。 |
| 新发现 | fresh `bootstrap bridge` 这轮把 [msquicTls13VerifyCertificateSignature(...)](/Users/lbcheng/cheng-lang/v3/src/quic/tls/handshake13.cheng#L2209) 的真 blocker 直接炸出来了：只要函数被写成 `else: if` 的嵌套分发，`stage2` 就会稳定在这里报 `stmt_if`。这不是协议逻辑问题，就是 lowering 句型边界。 |
| 新发现 | 这条签名分发最稳的形状，不是 `if/elif/else`，也不是 `else: if`，更不是裸的并列 `if / if / if / return Err`。当前 fresh `stage2` 真正稳定的是：先放一个默认 `Err("tls13: unsupported signature scheme")` 到 `verifyRes`，再用三条并列 `if` 覆盖，最后统一 `return verifyRes`。按这条形状收口后，fresh `stage2` 的 [tls_initial_packet_roundtrip_smoke.cheng](/Users/lbcheng/cheng-lang/v3/src/tests/tls_initial_packet_roundtrip_smoke.cheng) 和 [quic_transport_loopback_smoke.cheng](/Users/lbcheng/cheng-lang/v3/src/tests/quic_transport_loopback_smoke.cheng) 都重新真编真跑通过，完整 [run_slice_gate.sh](/Users/lbcheng/cheng-lang/v3/tooling/run_slice_gate.sh) 也再次输出 `[v3 gate] ok`。 |
| 新发现 | [fixed256_curve25519_smoke.cheng](/Users/lbcheng/cheng-lang/v3/src/tests/fixed256_curve25519_smoke.cheng) 继续双挂是对的：它留在 [run_v3_stage23_libp2p_smokes.sh](/Users/lbcheng/cheng-lang/v3/tooling/run_v3_stage23_libp2p_smokes.sh) 里当 `TLS13 X25519` 前哨，同时也留在默认 [run_v3_host_smokes.sh](/Users/lbcheng/cheng-lang/v3/tooling/run_v3_host_smokes.sh) 里当 host compiler 的 fixed-surface 验收。两边验证的不是同一条轴。 |

## Findings

| 项目 | 结论 |
|---|---|
| 新发现 | [msquicTls13PeerVerifyValidateChain(...)](/Users/lbcheng/cheng-lang/v3/src/quic/tls/handshake13.cheng) 这条现在也已经证明可以像 `revocation` 一样安全收平，但只该拆“大调用 + 深字段写回”两句。现在的 [msquicTls13PeerVerifyRunChain(...)](/Users/lbcheng/cheng-lang/v3/src/quic/tls/handshake13.cheng) 和 [msquicTls13PeerVerifySetChainVerified(...)](/Users/lbcheng/cheng-lang/v3/src/quic/tls/handshake13.cheng) 没碰 `guard/ensureTrustRoots/requireCert` 顺序，完整 `stage23` 继续全绿。 |
| 新发现 | `peer` 深字段里最值得收成 setter 的顺序，这轮已经被真跑验证了一遍：先是 `peerOcspResponse`，再是 `peerCertVerified`，然后 `certVerifyOk`，最后才是 `finishedOk`。而 reset/init 段里的 `out.revocationMode/out.peerCertVerified/out.certVerifyOk/out.finishedOk/out.peerOcspResponse` 不该动，继续直写最清楚。 |
| 新发现 | [msquicTls13ApplyCertificateEntryExtension(...)](/Users/lbcheng/cheng-lang/v3/src/quic/tls/handshake13.cheng) 这层 helper 是安全的，因为它只搬“单条扩展应用”，不碰 `offset = ext.next`。这点和 `ParseCertificate` 主循环 helper 化是两码事，后者已经被坐实会改坏 `stage2` 运行时语义。 |
| 新发现 | 这轮 `chain/ocsp/certverify/finished` helper 收口后，`/tmp/quic_transport_loopback_smoke.stage2.chainhelper` 已经真跑 `ok`，随后完整 [run_v3_stage23_libp2p_smokes.sh](/Users/lbcheng/cheng-lang/v3/tooling/run_v3_stage23_libp2p_smokes.sh) 再次输出 `v3 stage2/stage3 libp2p: ok`。说明这次补的 `chain helper + setters` 没把 QUIC/TLS/content/chain_node 主线带偏。 |
| 新发现 | host 侧确实要把基础 smoke 前移，但最短路径不是再抄一份 runner。最稳的收法是直接让 [run_v3_host_smokes.sh](/Users/lbcheng/cheng-lang/v3/tooling/run_v3_host_smokes.sh) 支持定向 smoke，再由 [run_slice_gate.sh](/Users/lbcheng/cheng-lang/v3/tooling/run_slice_gate.sh) 单独前置 `fixed256_sha256_smoke` 和 `default_init_literals_smoke`。这样编译器检查、日志清理、报错打印都只保留一份。 |
| 新发现 | [fixed256_curve25519_smoke.cheng](/Users/lbcheng/cheng-lang/v3/src/tests/fixed256_curve25519_smoke.cheng) 继续双挂是对的，但 host 侧不需要再为它单独造 runner；留在默认 [run_v3_host_smokes.sh](/Users/lbcheng/cheng-lang/v3/tooling/run_v3_host_smokes.sh) 里就够。它和 `stage23` 的 `curve25519` 前哨分别验证的是 host compiler 轴和 `stage2/stage3` 协议轴。 |
| 新发现 | 总 gate 的更优顺序已经坐实是 `host fixed256_sha256 -> host default_init_literals -> stage23 -> host smokes`。这样一来，host compiler 连最小 hash/初始化语义都没站稳时，会在最前面直接暴露，不会拖到后面业务 runner 才炸。 |
| 新发现 | 这轮 `fixed256` 和 `default_init_literals` 的门禁取舍已经坐实：能在 `stage2/stage3` 下前台跑通，不等于应该进 [run_v3_stage23_libp2p_smokes.sh](/Users/lbcheng/cheng-lang/v3/tooling/run_v3_stage23_libp2p_smokes.sh)。真正该进的是 [fixed256_curve25519_smoke.cheng](/Users/lbcheng/cheng-lang/v3/src/tests/fixed256_curve25519_smoke.cheng)，因为 `TLS13` 的 `X25519` 就站在这条底座上。 |
| 新发现 | [fixed256_sha256_smoke.cheng](/Users/lbcheng/cheng-lang/v3/src/tests/fixed256_sha256_smoke.cheng) 这条不该塞进 `stage23` 主 gate。不是它不重要，而是它已经被 `native_initial_crypto_frame`、`tls_initial_packet_roundtrip`、`content` 这批现有 smoke 间接反复覆盖，再独立挂一遍只会让 runner 更长，信号却没有新增。 |
| 新发现 | [default_init_literals_smoke.cheng](/Users/lbcheng/cheng-lang/v3/src/tests/default_init_literals_smoke.cheng) 也不该挂在 `stage23 libp2p` runner 上。它验证的是通用语言语义，不是协议专项前置；把这种全局语义 smoke 混进专项 runner，后面只会让门禁边界越来越糊。 |
| 新发现 | [msquicTls13ParseCertificateEntryExtensions(...)](/Users/lbcheng/cheng-lang/v3/src/quic/tls/handshake13.cheng) 这条最稳的收法已经坐实：不要碰 `while`、不要碰 `offset = ext.next`，只把“单条扩展应用”和 `var Bytes` 写回拔出来。现在的 [msquicTls13ApplyCertificateEntryExtension(...)](/Users/lbcheng/cheng-lang/v3/src/quic/tls/handshake13.cheng) + [msquicTls13PeerVerifySetOcspResponse(...)](/Users/lbcheng/cheng-lang/v3/src/quic/tls/handshake13.cheng) 就是这条最小稳解。 |
| 新发现 | [msquicTls13PeerVerifyValidateRevocation(...)](/Users/lbcheng/cheng-lang/v3/src/quic/tls/handshake13.cheng) 这里真正值得拆的只有 `x509VerifyRevocation(...)` 那个超长调用，`guard -> ensureTrustRoots -> trustRoots.count<=0 -> now` 这些控制语义不要搬。现在的 [msquicTls13PeerVerifyRunRevocation(...)](/Users/lbcheng/cheng-lang/v3/src/quic/tls/handshake13.cheng) 只是参数原样透传的薄 helper，这种形状 `stage2/stage3` 都能吃下。 |
| 新发现 | `peer` 深字段直写继续是可以安全外提的类型：`peerCertVerified/certVerifyOk/finishedOk/revocationMode` 现在收成单字段 setter 后，完整 [run_v3_stage23_libp2p_smokes.sh](/Users/lbcheng/cheng-lang/v3/tooling/run_v3_stage23_libp2p_smokes.sh) 再次全绿，说明这类叶子写回不会像 `ParseCertificate` helper 化那样引入运行时语义偏移。 |
| 新发现 | 这轮 gate 中途冒过一次 `quic_transport_loopback_smoke.stage2.run.log` 缺失，脚本表面看像 `run failed`，但同一产物 [quic_transport_loopback_smoke.stage2](/Users/lbcheng/cheng-lang/artifacts/v3_stage23_libp2p/quic_transport_loopback_smoke.stage2) 手动真跑直接输出 `v3 quic_transport_loopback_smoke ok`，随后完整 gate 二次重跑也再次收尾到 `v3 stage2/stage3 libp2p: ok`。这类现象要记成 runner 毛刺，不要误判成源码回归。 |
| 新发现 | 下一刀最值的还是别回去碰 [msquicTls13ParseCertificate(...)](/Users/lbcheng/cheng-lang/v3/src/quic/tls/handshake13.cheng)。更合适的切口已经收敛到：继续把 `CertificateVerify/Finished/ChainVerified` 这几条热路径保留在叶子 setter 层，或者再看 `x509VerifyChain(...)` 那个长调用是否也能像 `revocation` 一样收成薄 helper。 |
| 新发现 | [handshake13.cheng](/Users/lbcheng/cheng-lang/v3/src/quic/tls/handshake13.cheng) 里这轮真正能安全落地的，是 `peer` 深字段的单字段 setter，不是 `ParseCertificate` 主循环 helper 化。`msquicTls13PeerVerifySetChainVerified(...)`、`SetCertVerifyOk(...)`、`SetFinishedOk(...)`、`SetRevocationMode(...)` 这种叶子写回，`stage2/stage3` 都能稳定吃下。 |
| 新发现 | `ParseCertificate` 的 `ctx/list header` helper 化，哪怕 compile 继续全绿，也会在 `stage2` 真跑时把运行时语义带偏。我这轮分别试过 `var out` 和标量 `Result[int32]` 两条写法，都会把同一条 [quic_transport_loopback_smoke.cheng](/Users/lbcheng/cheng-lang/v3/src/tests/quic_transport_loopback_smoke.cheng) 打成 `tls13: certificate empty`。这说明这块不能只看 lowering 成功，必须看真实握手结果。 |
| 新发现 | `ParseCertificate` 的 entry dispatch helper 化同样不稳。把 `hasLeaf` 从主循环里抽出去后，`stage2` 真跑会直接变成 `tls13: certificate missing`。不管是 `hasLeaf: var bool`，还是让 helper 返回新状态，都没有内联写法稳。 |
| 新发现 | 刚才完整 gate 里那次 `quic_transport_loopback_smoke.stage2` 的 `Killed: 9` 不是代码回归。我把同一个 gate 产物 [quic_transport_loopback_smoke.stage2](/Users/lbcheng/cheng-lang/artifacts/v3_stage23_libp2p/quic_transport_loopback_smoke.stage2) 手动重跑，直接输出 `v3 quic_transport_loopback_smoke ok`；随后完整 [run_v3_stage23_libp2p_smokes.sh](/Users/lbcheng/cheng-lang/v3/tooling/run_v3_stage23_libp2p_smokes.sh) 二次重跑也再次收尾到 `v3 stage2/stage3 libp2p: ok`。所以这次要记住：环境瞬时挂住和运行时语义回归是两回事。 |
| 新发现 | 下一刀别再先碰 [msquicTls13ParseCertificate(...)](/Users/lbcheng/cheng-lang/v3/src/quic/tls/handshake13.cheng) 的 `ctx/list header` 和 entry dispatch 了。更值的切口已经收敛到 [msquicTls13ParseCertificateEntryExtensions(...)](/Users/lbcheng/cheng-lang/v3/src/quic/tls/handshake13.cheng) 的 `var Bytes` 写回，以及 [msquicTls13PeerVerifyValidateRevocation(...)](/Users/lbcheng/cheng-lang/v3/src/quic/tls/handshake13.cheng) 那个超长 `x509VerifyRevocation(...)` 调用。 |
| 新发现 | [handshake13.cheng](/Users/lbcheng/cheng-lang/v3/src/quic/tls/handshake13.cheng) 这轮已经把证书尾段里最容易继续顶 ordinary 的几块压成叶子 helper：`trustRoots` 构建、`peerCertVerified` 写回、`finishedOk` 写回、`revocationMode` 配置、`CertificateVerify` 提交链，现在都不再散在 parser 主体里。这样 `ParseCertificate / ParseCertificateVerify / ParseFinished` 主线已经明显变直。 |
| 新发现 | 这轮还钉死了一个很具体的 `stage3` 形状边界：`while` 里双层 `if` 再写 `var Bytes`，会直接报 `emit nested if failed`。出问题的就是 [msquicTls13ParseCertificateEntryExtensions(...)](/Users/lbcheng/cheng-lang/v3/src/quic/tls/handshake13.cheng)。最稳的修法不是绕过 setter，而是把“空 `OCSP` 不写回”的判断挪进 [msquicTls13PeerVerifySetOcspResponse(...)](/Users/lbcheng/cheng-lang/v3/src/quic/tls/handshake13.cheng) 本身，让循环体只保留单层调用。 |
| 新发现 | 这次不是只过了 targeted。`/tmp/tls_initial_packet_roundtrip_smoke.stage3.peerhelpers2`、`/tmp/quic_transport_loopback_smoke.host.peerhelpers2`、`/tmp/libp2p_quic_tls_smoke.host.peerhelpers2` 都已经真跑 `ok`，随后完整 [run_v3_stage23_libp2p_smokes.sh](/Users/lbcheng/cheng-lang/v3/tooling/run_v3_stage23_libp2p_smokes.sh) 也继续输出 `v3 stage2/stage3 libp2p: ok`。说明这轮 helper 收口和 `stage3` 句型修正都没有把 QUIC/libp2p 主线打坏。 |
| 新发现 | 下一刀最值的已经前移到 [msquicTls13ParseCertificate(...)](/Users/lbcheng/cheng-lang/v3/src/quic/tls/handshake13.cheng) 的 `ctx/list` 头读取和 entry 分发主循环。证书尾段的提交链这轮已经收住，后面不该再回头碰已经坐实稳定的 `OCSP/certVerify/finished` 叶子 helper。 |

## Findings

| 项目 | 结论 |
|---|---|
| 新发现 | `peer verify` 这层的 blocker 不是单点，是一类形状问题：只要把 `state.peer` 整块喂给 helper，`stage2/stage3` 就会轮流在不同位置炸。最稳的写法是像这轮这样，直接把 `OCSP`、`trustRoots`、`certVerifyOk`、`finishedOk` 这些字段写回压成叶子字段，不再跨 helper 传整个 `MsQuicTls13PeerVerifyState`。 |
| 新发现 | `stage23` 里那次 `listener start: msquic udp: bind failed` 的真根不是 runner 内部没关干净，而是 [libp2p_quic_tls_smoke.cheng](/Users/lbcheng/cheng-lang/v3/src/tests/libp2p_quic_tls_smoke.cheng) 这种 QUIC smoke 把监听端口写死在 `6208`，正好会和别的 runner/并发进程抢同一个 UDP 端口。只换一个别的固定端口不严谨。 |
| 新发现 | 真正该修的是“绑定后真实地址回填”。[datapath_udp.cheng](/Users/lbcheng/cheng-lang/v3/src/quic/platform/datapath_udp.cheng) 绑定成功后给出的 canonical 地址是 `udp://host:port`，不是 multiaddr；如果这一步不转回 multiaddr，上层即使把 smoke 改成 `udp/0`，最后也还是会拿 `port=0` 去拨号，表现成长时间挂住。 |
| 新发现 | 这轮把 [native_runtime.cheng](/Users/lbcheng/cheng-lang/v3/src/quic/native_runtime.cheng) 补成“`udp://...` -> multiaddr -> listener/client bound addr 回填”后，`udp/0` 路径已经坐实：`/tmp/quic_transport_loopback_smoke.port0b`、`/tmp/libp2p_quic_tls_smoke.port0b`、`/tmp/content_quic_smoke.port0c` 都真跑输出 `ok`。所以 QUIC smoke 以后不该再占固定端口。 |
| 新发现 | `stage23` 值得抬进去的这批里，[anti_entropy_signature_fields_smoke.cheng](/Users/lbcheng/cheng-lang/v3/src/tests/anti_entropy_signature_fields_smoke.cheng) 现在不该硬塞。fresh `stage2/stage3` 都明确卡在 `std/system::chengStrStoreCompat`、`std/system::chr`、`std/strutils::*` 这条 ordinary 句型缺口，不是协议逻辑回归。 |
| 新发现 | 这轮已经坐实可以直接进 `stage23` 的四条是 [chain_codec_binary_smoke.cheng](/Users/lbcheng/cheng-lang/v3/src/tests/chain_codec_binary_smoke.cheng)、[location_proof_smoke.cheng](/Users/lbcheng/cheng-lang/v3/src/tests/location_proof_smoke.cheng)、[anti_entropy_smoke.cheng](/Users/lbcheng/cheng-lang/v3/src/tests/anti_entropy_smoke.cheng)、[lsmr_bagua_prefix_tree_smoke.cheng](/Users/lbcheng/cheng-lang/v3/src/tests/lsmr_bagua_prefix_tree_smoke.cheng)。它们的 `stage2/stage3` targeted 都已经输出 `ok`。 |
| 新发现 | [handshake13.cheng](/Users/lbcheng/cheng-lang/v3/src/quic/tls/handshake13.cheng) 的扩展层现在最稳的拆法已经坐实：先把 `key_share` 从一坨 `ctx + 双 wire 形状` 拆成 [msquicTls13ParseClientKeyShareList(...)](/Users/lbcheng/cheng-lang/v3/src/quic/tls/handshake13.cheng) 和 [msquicTls13ParseServerKeyShare(...)](/Users/lbcheng/cheng-lang/v3/src/quic/tls/handshake13.cheng)，再把 [msquicTls13DispatchExtension(...)](/Users/lbcheng/cheng-lang/v3/src/quic/tls/handshake13.cheng) 按协商类和元数据类切开，最后把 [msquicTls13ParseExtensions(...)](/Users/lbcheng/cheng-lang/v3/src/quic/tls/handshake13.cheng) 收成 `ParseOneExtension + FinalizeExtensions`。按这个顺序推进，`stage2` 和 host 都没回退。 |
| 新发现 | 这轮外部入口其实没必要动。`ClientHello / ServerHello / EncryptedExtensions` 三个入口继续传原来的 ctx，未知扩展也继续放行，`ValidateExtensions(...)` 仍然只在整段扩展都吃完之后执行。也就是说，真正该改的是函数体形状，不是协议语义。 |
| 新发现 | 串行 fresh 回归已经把这轮拆法钉死：`/tmp/tls_client_hello_parse_smoke.stage2.extsplit`、`/tmp/tls_initial_packet_roundtrip_smoke.stage2.extsplit`、`/tmp/quic_transport_loopback_smoke.host.extsplit`、`/tmp/libp2p_quic_tls_smoke.host.extsplit` 全部输出 `ok`，随后完整 [run_v3_stage23_libp2p_smokes.sh](/Users/lbcheng/cheng-lang/v3/tooling/run_v3_stage23_libp2p_smokes.sh) 也继续输出 `v3 stage2/stage3 libp2p: ok`。这说明扩展层拆平已经不是风险点。 |
| 新发现 | 扩展层收住以后，下一刀最值的已经前移到 [msquicTls13ParseCertificate(...)](/Users/lbcheng/cheng-lang/v3/src/quic/tls/handshake13.cheng#L1846)。并行探查给出的最稳顺序是：先拆 `ParseCertificate` 本体，再拆 `ParseCertificateEntryExtensions` 的 `OCSP` 写回，最后再收 `ParseCertificateVerify` 的 `certVerifyOk` 状态位。 |
| 新发现 | `content_stub` 这层最该切的不是 advert 或 recoveryPlan，而是 manifest。因为 advert/recoveryPlan 仍然天然属于 metadata/store 查询面，只有 manifest 已经具备新的 `content fetch` 正式协议入口。把 [content_stub_smoke.cheng](/Users/lbcheng/cheng-lang/v3/src/tests/content_stub_smoke.cheng) 的 manifest 获取切到 `content fetch + Fill` 之后，上层终于不是只在底层 targeted smoke 里碰到新协议。 |
| 新发现 | [content_runtime_host.cheng](/Users/lbcheng/cheng-lang/v3/src/chain/content_runtime_host.cheng) 这类 helper 也不能回 `Result[Manifest]`。我这轮一开始补了 `v3ContentRuntimeRequestManifest(...) -> Result[swarm.V3ErasureShardManifest]`，结果 [content_stub_smoke.cheng](/Users/lbcheng/cheng-lang/v3/src/tests/content_stub_smoke.cheng) 立刻在后续 `sync` 路径炸进 `multistream`。把它改成 [v3ContentRuntimeRequestManifestFill(...)](/Users/lbcheng/cheng-lang/v3/src/chain/content_runtime_host.cheng) 之后，`content_stub_smoke` 又恢复 `ok`。这再次坐实：当前内容面凡是跨边界的大复合返回，都该优先收成 `Fill/Into`。 |
| 新发现 | 正式 gate 现在缺的不是 `GET_MANIFEST`，而是“direct `GET_CHUNK`”本身。`GET_MANIFEST` 已经被 [content_runtime_smoke.cheng](/Users/lbcheng/cheng-lang/v3/src/tests/content_runtime_smoke.cheng) 的 TCP 路径和 [content_quic_smoke.cheng](/Users/lbcheng/cheng-lang/v3/src/tests/content_quic_smoke.cheng) 的 QUIC 路径打到了；但 `GET_CHUNK` 之前主要还是靠 `RequestShardPull -> 内部转 fetch` 间接覆盖。这轮在 [content_runtime_smoke.cheng](/Users/lbcheng/cheng-lang/v3/src/tests/content_runtime_smoke.cheng) 补上 direct chunk fetch 后，新的数据面接口终于被直接锁死。 |
| 新发现 | 这次把 [run_v3_stage23_libp2p_smokes.sh](/Users/lbcheng/cheng-lang/v3/tooling/run_v3_stage23_libp2p_smokes.sh) 重新整套跑起来后，`content_runtime_smoke.stage2` 暴露出来的不是环境抖动，而是两层真问题叠在一起：一层是 [content_runtime_smoke.cheng](/Users/lbcheng/cheng-lang/v3/src/tests/content_runtime_smoke.cheng) 把 `v3ContentFetchResponseValidate(...)` 的 `response/request` 实参顺序写反；另一层是 [content_fetch.cheng](/Users/lbcheng/cheng-lang/v3/src/chain/content_fetch.cheng) 的构造函数还在靠 `Zero()` 带 `version`。这两刀不收，gate 会稳定红，不是假失败。 |
| 新发现 | `V3ContentFetchRequest` 和 `V3ContentFetchResponse` 这种协议头不能再靠零值暗带版本。把 [v3ContentFetchRequestMake(...)](/Users/lbcheng/cheng-lang/v3/src/chain/content_fetch.cheng) 明确写成 `out.version = 1`，再把 [v3ContentFetchResponseMake(...)](/Users/lbcheng/cheng-lang/v3/src/chain/content_fetch.cheng) 明确写成 `out.version = request.version` 以后，`manifest response validate` 这一层立刻恢复稳定。 |
| 新发现 | 即使 `version` 收正了，[v3ContentFetchResponseToManifest(...)](/Users/lbcheng/cheng-lang/v3/src/chain/content_fetch.cheng) 和 [v3ContentFetchResponseToShardPull(...)](/Users/lbcheng/cheng-lang/v3/src/chain/content_fetch.cheng) 继续绕 `Fill(out, ...)` 也不值当。对当前 Cheng ordinary 来说，更稳的形状就是包装函数自己做 `validate -> decode/build -> Ok(...)`，把 `var out` 这层去掉。按这条改完后，`/tmp/content_runtime_smoke.stage2.fix3`、`/tmp/content_runtime_smoke.stage3.fix3` 都真跑 `ok`，整套正式 gate 也重新回到 `v3 stage2/stage3 libp2p: ok`。 |
| 新发现 | 并行探查已经把 `handshake13` 下一刀收窄了：最值的是继续拆 [msquicTls13ParseExtensions(...)](/Users/lbcheng/cheng-lang/v3/src/quic/tls/handshake13.cheng) 的主循环和 [msquicTls13DispatchExtension(...)](/Users/lbcheng/cheng-lang/v3/src/quic/tls/handshake13.cheng)，再把 [msquicTls13ParseKeyShareExtension(...)](/Users/lbcheng/cheng-lang/v3/src/quic/tls/handshake13.cheng) 按 `ClientHello/ServerHello` 两条路径彻底分开。内容面这轮已经收住，下一刀不该再停在 `content_runtime`。 |
| 新发现 | [content_fetch.cheng](/Users/lbcheng/cheng-lang/v3/src/chain/content_fetch.cheng) 这条链的正式收法已经定了：`PubSub` 只广播 `content_stub`，真正的内容数据面必须单独走 `chunk_fetch`。这轮新增的 `GET_MANIFEST / GET_PREVIEW / GET_CHUNK / GET_RANGE` 四种 method 里，当前只把 `GET_MANIFEST / GET_CHUNK` 做成真闭环，`GET_PREVIEW / GET_RANGE` 明确返回未实现错误，口径终于和实现对齐了。 |
| 新发现 | 真正把 [content_runtime_smoke.cheng](/Users/lbcheng/cheng-lang/v3/src/tests/content_runtime_smoke.cheng) 卡住的不是 manifest payload 本身，而是“大复合对象直接走 `Result[T]` 返回”这条 ABI 边界。`response validate`、`manifest payload validate`、直接 `manifest decode` 都能过，但 `v3ContentFetchResponseToManifest(...)` 这种返回大对象的 helper 会在 TCP 这条栈形状下失稳。改成 `v3ContentFetchResponseToManifestFill(...) / ...ToShardPullFill(...)` 之后，`content_runtime_smoke` 和 `content_quic_smoke` 都重新转绿。 |
| 新发现 | 这轮最值钱的不是继续猜哪段 payload 坏了，而是把内容面统一收成“先落本地值，再走 Fill”。[content_runtime.cheng](/Users/lbcheng/cheng-lang/v3/src/chain/content_runtime.cheng)、[content_runtime_host.cheng](/Users/lbcheng/cheng-lang/v3/src/chain/content_runtime_host.cheng)、[content_runtime_host_quic.cheng](/Users/lbcheng/cheng-lang/v3/src/chain/content_runtime_host_quic.cheng) 现在都不再跨函数直接回传 `manifest/shard response` 这类大复合值；这跟前面 `chain_node_libp2p` 那次快照修法是同一类真根。 |
| 新发现 | 这次不是只修了单条 smoke。`/tmp/content_codec_smoke.fetch11`、`/tmp/content_runtime_smoke.fetch11`、`/tmp/content_quic_smoke.fetch11`、`/tmp/content_stub_smoke.fetch11` 现都已真跑 `ok`，说明 `codec/runtime/quic/stub` 四条内容链已经同时对齐到新的 `chunk_fetch + Fill/Into` 口径。 |
| 新发现 | [handshake13.cheng](/Users/lbcheng/cheng-lang/v3/src/quic/tls/handshake13.cheng) 这条线最稳的连续拆法已经坐实了：先把 `initMsQuicTls13HandshakeStateInto(...)` 的深层字段拆成 `wire/messages/secrets` 三个 `Into` helper，再把 `msquicTls13HandshakeGenerateX25519(...)` 改成 `priv/pub` bytes 直通 [msquicTls13HandshakeSetLocalKeyShare(...)](/Users/lbcheng/cheng-lang/v3/src/quic/tls/handshake13.cheng)，最后才拆 [msquicTls13ParseExtensions(...)](/Users/lbcheng/cheng-lang/v3/src/quic/tls/handshake13.cheng) 的读/分发主循环。按这个顺序推进，host 和 `stage2` 都没有回退。 |
| 新发现 | [curve25519.cheng](/Users/lbcheng/cheng-lang/src/std/crypto/curve25519.cheng) 这里最值的不是继续扩更多 helper，而是只补一个 `generateCurve25519KeyPairInto(...)`。这样就能把 `Curve25519KeyPair` 复合返回从 `handshake13` 热路径里拔掉，又不需要改动其它仍走老包装函数的调用点。 |
| 新发现 | [msquicTls13ParseExtensions(...)](/Users/lbcheng/cheng-lang/v3/src/quic/tls/handshake13.cheng) 正式拆成 `msquicTls13ReadNextExtension(...) + msquicTls13DispatchExtension(...)` 后，行为面没有变化，但 ordinary 更容易吃下。串行 fresh 跑 `/tmp/tls_client_hello_parse_smoke.stage2.parseext`、`/tmp/tls_initial_packet_roundtrip_smoke.stage2.parseext`、`/tmp/quic_transport_loopback_smoke.host.parseext`、`/tmp/libp2p_quic_tls_smoke.host.parseext` 全部继续输出 `ok`。 |
| 新发现 | 这轮不是只过了四条定向烟。完整 [run_v3_stage23_libp2p_smokes.sh](/Users/lbcheng/cheng-lang/v3/tooling/run_v3_stage23_libp2p_smokes.sh) 在这组改动后又重新整套跑了一遍，最终仍然收尾到 `v3 stage2/stage3 libp2p: ok`。这说明 `parseExtensions` 拆层没有把 `stage3` 或后面的 `content/chain_node` 链条打坏。 |
| 新发现 | [发布订阅.md](/Users/lbcheng/cheng-lang/v3/docs/发布订阅.md) 原来还是一篇发散式方案文，里面把 `Plumtree / DAG mempool / 纠删码群集 / RWAD 托管` 混在 `PubSub` 文档里，会直接和当前 [内容平面.md](/Users/lbcheng/cheng-lang/v3/docs/内容平面.md) 及实际代码口径打架。正确收法是把 `PubSub`、`内容平面`、`Unimaker v1` 拆成三层：当前 `PubSub` 事实、当前内容骨架、下一阶段目标协议。 |
| 新发现 | UniMaker 这条线最关键的不是继续给“默认发布”叠 `ZK` 叙事，而是把 `Manifest / Relay / Fetch / Pin` 四层写清楚，并明确只有 `Manifest/Relay/Fetch` 在当前 `v3` 有骨架，`Pin`、热度分档、托管证明都还是后续层。只要这句不写死，文档很容易再次把目标态说成现状。 |
| 新发现 | `stage23` 这轮最值的推进不是再改运行时代码，而是把已经真过的 QUIC/TLS smoke 升格进正式 gate。新增 [native_initial_crypto_frame_smoke.cheng](/Users/lbcheng/cheng-lang/v3/src/tests/native_initial_crypto_frame_smoke.cheng)、[native_client_hello_wire_smoke.cheng](/Users/lbcheng/cheng-lang/v3/src/tests/native_client_hello_wire_smoke.cheng)、[tls_client_hello_parse_smoke.cheng](/Users/lbcheng/cheng-lang/v3/src/tests/tls_client_hello_parse_smoke.cheng)、[tls_initial_packet_roundtrip_smoke.cheng](/Users/lbcheng/cheng-lang/v3/src/tests/tls_initial_packet_roundtrip_smoke.cheng)、[quic_transport_loopback_smoke.cheng](/Users/lbcheng/cheng-lang/v3/src/tests/quic_transport_loopback_smoke.cheng)、[libp2p_quic_tls_smoke.cheng](/Users/lbcheng/cheng-lang/v3/src/tests/libp2p_quic_tls_smoke.cheng) 后，完整 [run_v3_stage23_libp2p_smokes.sh](/Users/lbcheng/cheng-lang/v3/tooling/run_v3_stage23_libp2p_smokes.sh) 仍然稳定输出 `v3 stage2/stage3 libp2p: ok`。 |
| 新发现 | 这轮出现过一次 `tls_initial_packet_roundtrip_smoke.stage2.compile.log` 空文件假失败，但根因不是代码或脚本，而是我自己同时并发编同一条 smoke，撞了编译环境。把并发验证停掉后，这条 smoke 单独前台重跑直接 `unsupported=0`，整套正式 gate 也稳定通过。后面同一条 smoke 不该在两个编译会话里同时跑。 |
| 新发现 | 并行探查已经把下一刀钉死了：最值的不是先碰 `parseExtensions`，而是先拆 [initMsQuicTls13HandshakeStateInto](/Users/lbcheng/cheng-lang/v3/src/quic/tls/handshake13.cheng#L208) 的 `wire/secrets/peer` 三段深层初始化；第二刀才是 [msquicTls13HandshakeGenerateX25519](/Users/lbcheng/cheng-lang/v3/src/quic/tls/handshake13.cheng#L313) 的 `Curve25519KeyPair` 复合返回；第三刀才轮到 [msquicTls13ParseExtensions](/Users/lbcheng/cheng-lang/v3/src/quic/tls/handshake13.cheng#L1558) 的扩展分发主循环。 |
| 新发现 | [rsa.cheng](/Users/lbcheng/cheng-lang/src/std/crypto/rsa.cheng) 这条链真正吃 ordinary 的形状不是“切更多 helper”，而是改成 `Into + Result[bool]`。把 `rsaPkcs1ValueFromPkcs8OuterSeq(...)` 和 `rsaPkcs1ValueFromPrivateKeyBody(...)` 收成 `Into` 入口后，fresh `stage2` 编 [quic_transport_loopback_smoke.cheng](/Users/lbcheng/cheng-lang/v3/src/tests/quic_transport_loopback_smoke.cheng) 已直接 `primary_object_unsupported_function_count=0`。 |
| 新发现 | 这次不是只过了单条 smoke。`/tmp/quic_transport_loopback_smoke.stage2.rsa_into`、`/tmp/libp2p_quic_tls_smoke.stage2.rsa_into`、`/tmp/quic_transport_loopback_smoke.host.rsa_into2`、`/tmp/libp2p_quic_tls_smoke.host.rsa_into2` 都已经真跑 `ok`；随后整套 [run_v3_stage23_libp2p_smokes.sh](/Users/lbcheng/cheng-lang/v3/tooling/run_v3_stage23_libp2p_smokes.sh) 和 [run_v3_host_smokes.sh](/Users/lbcheng/cheng-lang/v3/tooling/run_v3_host_smokes.sh) 也都收尾到 `ok`。 |
| 新发现 | host 侧这轮唯一出现过的假回归是端口占用，不是代码坏了。残留的 `quic_transport_loopback_smoke` 进程占着固定端口 `6212` 时，会报 `msquic udp: bind failed`；清掉残留进程并按串行重跑后，两条 QUIC smoke 都恢复 `ok`。后续这组 host 验收必须保持串行。 |
| 新发现 | 并行探查的结论已经足够明确：`rand/ecnist` 现在不是最短第一刀，下一刀最值的是 [initMsQuicTls13HandshakeStateInto](/Users/lbcheng/cheng-lang/v3/src/quic/tls/handshake13.cheng) 这条大复合状态初始化链；它后面连着 `msquicTls13HandshakeGenerateX25519` 和 `msquicTls13ParseExtensions`。 |
| 新发现 | [rsa.cheng](/Users/lbcheng/cheng-lang/src/std/crypto/rsa.cheng) 这轮用“切小函数”是真有净收益的。把 `rsaPkcs1ValueFromPrivateKeyBytes(...)` 拆成 `rsaPkcs1ValueFromPkcs8OuterSeq(...)` 和 `rsaPkcs1ValueFromPrivateKeyBody(...)` 之后，fresh `stage2` 的首个 unsupported 已从旧的大函数本体前移成 `rsaPkcs1ValueFromPkcs8OuterSeq(...)`。这说明当前 `stage2` 真卡的已经不是外层分派，而是 `pkcs8` 那段内部 `Result[int64] + TLV` 组合。 |
| 新发现 | [rand.cheng](/Users/lbcheng/cheng-lang/src/std/crypto/rand.cheng) 这条链当前最稳的形状不是 `randomOpenUrandom()`，而是直接 `let f: bos.File = bos.openRead(randUrandomPath)`。保留 `openRead` 后，fresh `stage2` 的 `rand` 前沿稳定收缩成 `randomFillUrandom(...)`，而不会再多长出一个无净收益的 wrapper hotspot。 |
| 新发现 | [ecnist.cheng](/Users/lbcheng/cheng-lang/src/std/crypto/ecnist.cheng) 现在也已经拿到净前移：把 RFC6979 初始化块抽成 `p256DeterministicFilledBytes(...)` 之后，fresh `stage2` 的 `ecnist` 前沿从 `p256DeterministicK` 前移成了 `p256DeterministicFilledBytes`。这刀在本地串行 fresh host 下也过了 [quic_transport_loopback_smoke.cheng](/Users/lbcheng/cheng-lang/v3/src/tests/quic_transport_loopback_smoke.cheng) 和 [libp2p_quic_tls_smoke.cheng](/Users/lbcheng/cheng-lang/v3/src/tests/libp2p_quic_tls_smoke.cheng)，可以留。 |
| 新发现 | QUIC host 验收不能并行跑。并行跑 [quic_transport_loopback_smoke.cheng](/Users/lbcheng/cheng-lang/v3/src/tests/quic_transport_loopback_smoke.cheng) 和 [libp2p_quic_tls_smoke.cheng](/Users/lbcheng/cheng-lang/v3/src/tests/libp2p_quic_tls_smoke.cheng) 会撞 UDP bind 端口冲突，出现“listener start: msquic udp: bind failed”这种假回归；串行跑则两条都继续 `ok`。 |

## Findings

| 项目 | 结论 |
|---|---|
| 新发现 | [rand.cheng](/Users/lbcheng/cheng-lang/src/std/crypto/rand.cheng) 这条链已经确认有净收益：把 `randomFillUrandom(...)` 里直接 `bos.openImplRead(randUrandomPath)` 抽成同文件 [randomOpenUrandom(...)](/Users/lbcheng/cheng-lang/src/std/crypto/rand.cheng) 之后，fresh `stage2` 的 unsupported 首屏已从旧的 `randomFillUrandom` 调用点前移成 `randomOpenUrandom -> randomFillUrandom`。语义没变，还是同一条 `/dev/urandom -> fread -> close` 主线。 |
| 新发现 | [rsa.cheng](/Users/lbcheng/cheng-lang/src/std/crypto/rsa.cheng) 这轮两条看起来更深的路都不能留：一条是让 [rsaBigSignPssBytes](/Users/lbcheng/cheng-lang/src/std/crypto/rsa.cheng) 直走 `rsaBigPrivateKeyFromBytes(...)`，另一条是把 `pkcs1` 提取改成 `range helper`。前者会把 `stage2` 首屏改成 `rsaBigPrivateKeyFromPkcs8ValueInto` 这组函数，后者会把 host 主线打成 `asn1: tag overrun`。两种状态都已经撤回。 |
| 新发现 | [quic_tls_policy_smoke.cheng](/Users/lbcheng/cheng-lang/v3/src/tests/quic_tls_policy_smoke.cheng) 当前本来就不是绿的。fresh host 跑 `/tmp/quic_tls_policy_smoke.host.current` 会在 `decoded has eku` 断言处直接 panic，所以它不能拿来判断这轮 `rsa/rand` 改动有没有新回归。当前真正可信的 host 守门还是 [quic_transport_loopback_smoke.cheng](/Users/lbcheng/cheng-lang/v3/src/tests/quic_transport_loopback_smoke.cheng) 和 [libp2p_quic_tls_smoke.cheng](/Users/lbcheng/cheng-lang/v3/src/tests/libp2p_quic_tls_smoke.cheng)。 |

## Findings

| 项目 | 结论 |
|---|---|
| 新发现 | [x509VerifyOcspResponse](/Users/lbcheng/cheng-lang/src/std/tls/x509.cheng#L2011) 里最后那段 `expectNameRes/keyBytesRes/expectKeyRes` 的 `Result[Bytes]` 复合绑定，确实就是 `x509` 留在 `stage2` unsupported 里的最后一根刺。把它改成直接 `sha256/sha384` 分支和扁平公钥编码后，fresh `stage2` 的 unsupported 列表里已经完全没有 `std/tls/x509::*`。 |
| 新发现 | 这轮也顺手把真前沿重新钉死了：现在 `stage2` 还剩的首批 blocker 已经只在 [rsa.cheng](/Users/lbcheng/cheng-lang/src/std/crypto/rsa.cheng)、[rand.cheng](/Users/lbcheng/cheng-lang/src/std/crypto/rand.cheng)、[ecnist.cheng](/Users/lbcheng/cheng-lang/src/std/crypto/ecnist.cheng) 和 [handshake13.cheng](/Users/lbcheng/cheng-lang/v3/src/quic/tls/handshake13.cheng)，不用再回头在 `x509` 上打转。 |
| 新发现 | [rand.cheng](/Users/lbcheng/cheng-lang/src/std/crypto/rand.cheng) 这轮试探说明一件事：当前 `stage2` 卡的不是随机源缺失，而是 `cheng_system_entropy_fill` 的 call resolve 和 `strToCStringTemp(randUrandomPath)` 这类调用形状。因为这刀没带来净收益，我已经把试探改动撤回，fresh host 编 [quic_transport_loopback_smoke.cheng](/Users/lbcheng/cheng-lang/v3/src/tests/quic_transport_loopback_smoke.cheng) 已恢复 `primary_object_unsupported_function_count=0`。 |

## Findings

| 项目 | 结论 |
|---|---|
| 新发现 | [x509.cheng](/Users/lbcheng/cheng-lang/src/std/tls/x509.cheng) 里那些已经脱离调用链的小 helper 真的会白占 `stage2` unsupported 名额。把 `x509TagIs*` 和 `x509KeyUsageFlagMissing` 从热路径拔掉并直接删除后，fresh `stage2` 的 unsupported 列表里已经不再出现这几个名字。 |
| 新发现 | `x509VerifyRevocationWithIssuer(...)` 这轮也已经从 `stage2` unsupported 列表里掉出去了，说明把 `ocsp` 分支改成 `IsErr/IsOk` 直线处理、去掉嵌套 `let status` 是有效刀法。后面不该再回头碰这条已经前移完的包装层。 |
| 新发现 | fresh `stage2` 的 unsupported 总数这轮仍是 `64`，但 `x509` 真前沿已经继续往后收缩成 `x509ReadOid/x509DecodeBitString/x509ParseMgf1Hash/x509ParseSignatureAlgorithm/x509ParseAlgIdWithParam/x509ParseBasicConstraintsExtension/x509ParseKeyUsageExtension/x509ParseExtensions/x509ParseTbs/x509ParseCertificateInto/x509VerifyLeafConstraints/x509VerifyCaConstraints/x509ParseCrlTbs/x509VerifyOcspResponse`。下一刀该直接啃这批函数开头的 `let/var` 与 composite `Result` 形状，而不是回头碰已经掉线的 helper。 |
| 新发现 | host 主线这轮继续没回退：`/tmp/quic_transport_loopback_smoke.host.x509_tail2` 真跑输出 `v3 quic_transport_loopback_smoke ok`，`/tmp/libp2p_quic_tls_smoke.host.x509_tail2` 真跑输出 `v3 libp2p_quic_tls_smoke ok`。所以当前可以继续在 `stage2` 真根上硬推，不用担心这轮 `x509` 形状调整把 QUIC/libp2p 主线打坏。 |
| 新发现 | [x509.cheng](/Users/lbcheng/cheng-lang/src/std/tls/x509.cheng) 这轮已经钉实一个硬边界：`X509HashKind(...) / X509SignatureKind(...)` 这类 enum 构造器在当前 seed 里会被当成普通函数调用，直接把 host ordinary 编译面打成 `unsupported`。所以后面继续推 `stage2` 时，热路径只能用“零值默认初始化 + 直接枚举赋值”，不能再把 enum constructor 当类型转换用。 |
| 新发现 | host 主线这轮没回退，且验证更硬了：fresh 编出来的 `/tmp/quic_transport_loopback_smoke.host.x509_front2` 真跑输出 `v3 quic_transport_loopback_smoke ok`，`/tmp/libp2p_quic_tls_smoke.host.x509_front2` 真跑输出 `v3 libp2p_quic_tls_smoke ok`。说明当前 `x509` 前沿压平没有把 QUIC/libp2p 主线打坏。 |
| 新发现 | fresh `stage2` 的 unsupported 总数这轮仍是 `64`，但真前沿已经固定成 `x509ReadOid -> x509DecodeBitString -> x509SigAlgCodeFromOid -> x509HashKindCodeFromOid -> x509ParseMgf1Hash -> x509ParsePssParams -> x509ParseSignatureAlgorithm -> x509ParseAlgIdWithParam -> x509ParseKeyUsage -> x509ParseSpki`。也就是说，`rsa public-key` 旧前沿已经不是第一现场，下一刀该直接啃 `x509` 首屏，不该再回头碰已经掉线的旧 blocker。 |
| 新发现 | 子代理并行复核后，`handshake13` 下一刀最值的三处也已经钉死：先拆 [msquicTls13HandshakeFeed](/Users/lbcheng/cheng-lang/v3/src/quic/tls/handshake13.cheng#L2151)，再拆 [msquicTls13ParseCertificate](/Users/lbcheng/cheng-lang/v3/src/quic/tls/handshake13.cheng#L1815)，最后拆 [msquicTls13ParseExtensions](/Users/lbcheng/cheng-lang/v3/src/quic/tls/handshake13.cheng#L1588)。但在 `x509` 首屏还没前移前，这三刀都不是当前第一优先。 |

## Findings

| 项目 | 结论 |
|---|---|
| 新发现 | 这轮已经证实一件事：只把调用点从 `Side*` 换成 owner 入口，不会自动把 `stage2` 打通。fresh `stage2` 的 unsupported 总数仍是 `64`，说明真 blocker 已经不是 wrapper 名字，而是 [handshake13.cheng](/Users/lbcheng/cheng-lang/v3/src/quic/tls/handshake13.cheng) 里 `MsQuicTls13HandshakeState` 这类大 owner 的直接写回和按值传递。 |
| 新发现 | 但这轮不是空转。把 [native_runtime.cheng](/Users/lbcheng/cheng-lang/v3/src/quic/native_runtime.cheng) 和多条 smoke 切到 owner 入口之后，fresh `stage2` 的首批 unsupported 已经更诚实地暴露成两类：一类是 [x509.cheng](/Users/lbcheng/cheng-lang/src/std/tls/x509.cheng) / [rsa.cheng](/Users/lbcheng/cheng-lang/src/std/crypto/rsa.cheng) 的复合返回链，另一类是 `handshake13` 的 owner 大对象写回；不会再被旧 `native_runtime` 表层分发逻辑干扰。 |
| 新发现 | [x509.cheng](/Users/lbcheng/cheng-lang/src/std/tls/x509.cheng) 里把 `oid == const` 改成 `x509StrEqual(...)` 并没有直接降低 unsupported 数，反而进一步钉实：当前 `stage2` 不是单纯缺 `str == str`，而是整个 `x509` 解析/校验链的 `Result[复合] + if/while` 组合还没压平。下一刀应该优先做 `x509ParseSpkiInto(...)`、`rsaBigPublicKeyFromPartsInto(...)` 这类 `Into` 入口，而不是继续换比较写法。 |
| 新发现 | host 主线这轮继续没回退：`/tmp/quic_transport_loopback_smoke.host.owner_wrappers` 真跑输出 `v3 quic_transport_loopback_smoke ok`，`/tmp/libp2p_quic_tls_smoke.host.owner_wrappers` 真跑输出 `v3 libp2p_quic_tls_smoke ok`。所以当前代码状态可以放心继续往 `stage2` 真根深挖。 |

## Findings

| 项目 | 结论 |
|---|---|
| 新发现 | [native_runtime.cheng](/Users/lbcheng/cheng-lang/v3/src/quic/native_runtime.cheng) 这轮最值的不是继续在表层堆 wrapper，而是把 `header/frame` 这层直接改成标量入口和 owner helper。`BuildHeaderCommon` 一旦改吃 `packetTypeCode`，`PacketAdd*Frame` 一旦改成 `PacketAppendFrame + msquicFrame*`，fresh `stage2` 的 `native_runtime` 首层 unsupported 就不再出现 `BuildHeaderCommon/Build*HeaderInto/PacketAdd*Frame` 这组函数。 |
| 新发现 | 这也钉实了一个更硬的事实：当前 `stage2` 里 `native_runtime` 真还活着的，只剩 [msquicNativeResetClientEndpoint](/Users/lbcheng/cheng-lang/v3/src/quic/native_runtime.cheng)、[msquicNativeResetServerEndpoint](/Users/lbcheng/cheng-lang/v3/src/quic/native_runtime.cheng)、[msquicNativeMultiAddressBindText](/Users/lbcheng/cheng-lang/v3/src/quic/native_runtime.cheng) 三个表面口子。也就是说，前线已经从 `header/frame` 明确前移回 `reset/bindText -> handshake13/x509`。 |
| 新发现 | [handshake13.cheng](/Users/lbcheng/cheng-lang/v3/src/quic/tls/handshake13.cheng) 新补的 `msquicTls13RoleFromSideCode(...)` 并没有把 `stage2` 直接打通，但它把问题暴露得更诚实了：`ResetClientEndpoint/ResetServerEndpoint` 现在卡的是“枚举局部绑定也被当成复合 materialize”，不是旧的“枚举字面量实参”那一层。下一刀该直接做 side-specific reset，不该再在 call-site 变花样。 |
| 新发现 | [multiaddress.cheng](/Users/lbcheng/cheng-lang/v3/src/quic/multiaddress.cheng) 的 owner helper `quicMultiAddressBindText(...)` 也没有把 [msquicNativeMultiAddressBindText](/Users/lbcheng/cheng-lang/v3/src/quic/native_runtime.cheng) 直接收掉，说明这里真卡的是 `str` 复合返回在 `native_runtime` 这层的 lowering，不是单纯“嵌套 return call 太深”。这条后面要么继续往 owner 里吃完，要么直接补 seed 的 `str` 物化。 |
| 新发现 | host 主线这轮没有任何回归：`/tmp/quic_transport_loopback_smoke.host.after_native_stage2_shape` 真跑输出 `v3 quic_transport_loopback_smoke ok`，`/tmp/libp2p_quic_tls_smoke.host.after_native_stage2_shape` 真跑输出 `v3 libp2p_quic_tls_smoke ok`。所以这轮 `native_runtime` 形状调整是净推进，不是换报错。 |

| 项目 | 结论 |
|---|---|
| 新发现 | [system_helpers_selflink_min_runtime.c](/Users/lbcheng/cheng-lang/src/runtime/native/system_helpers_selflink_min_runtime.c) 和 [system_helpers_selflink_shim.c](/Users/lbcheng/cheng-lang/src/runtime/native/system_helpers_selflink_shim.c) 这条线当前已经不是编译错误，也不是 warning 残留。补齐 `unused` 标注并把 Mach-O deprecated 诊断局部压住后，两份 selflink runtime 现在都能用 `cc -std=c11 -O2 -Wall -Wextra -pedantic -c ...` fresh 零 warning 通过。 |
| 新发现 | [connection_impl.cheng](/Users/lbcheng/cheng-lang/v3/src/quic/core/connection_impl.cheng) 这里最稳的形状，仍然是让 queue/sent packet 直接落在 `impl.queuedFrames/impl.sentPackets` 本地数组里；把这两组访问重新从全局 slot 数组收回 `impl` 之后，fresh host [quic_transport_loopback_smoke.cheng](/Users/lbcheng/cheng-lang/v3/src/tests/quic_transport_loopback_smoke.cheng) 已重新真跑输出 `v3 quic_transport_loopback_smoke ok`。 |
| 新发现 | 这也说明 `connection_impl` 当前不是 `stage2` 的主 blocker 了。fresh `stage2` 编同一 smoke 时，第一现场已经重新前移回 [native_runtime.cheng](/Users/lbcheng/cheng-lang/v3/src/quic/native_runtime.cheng) 的 `msquicTls13SideReset / ResetClientEndpoint / ResetServerEndpoint` 这条线，后续该直接拆 `native_runtime` 的复合调用和 owner 边界，不该再回头在 `connection_impl` 上空转。 |

| 项目 | 结论 |
|---|---|
| 新发现 | 当前这批 warning 里，真正需要改的只有三类：`system_helpers.c` 里的老式空参数原型、只在特定宏入口才会被用到的静态 helper/变量、以及 [stb_image.h](/Users/lbcheng/cheng-lang/src/runtime/native/stb_image.h) 的 `stbi__is_16_main(...)` 在当前编译配置下未使用参数 `s`。这不是业务逻辑问题，直接按声明面收掉最稳。 |
| 新发现 | `cc -std=c11 -O2 -Wall -Wextra -pedantic -c /Users/lbcheng/cheng-lang/src/runtime/native/system_helpers.c -o /tmp/system_helpers.warnfree.o` 现在已经零 warning 通过。 |
| 新发现 | `system_helpers_selflink_min_runtime.c` 和 `system_helpers_selflink_shim.c` 用同一套 `cc -std=c11 -O2 -Wall -Wextra -pedantic -c ...` 单独 fresh 编时，当前先暴露的是缺少 socket/inet 相关声明的编译错误，不是 warning。它们属于另一条独立修复链，不能混进这轮“移除 warning”的结论里。 |
| 新发现 | 这轮 `stage2 QUIC/TLS` 的推进是真收窄，不是换报错文案。fresh `stage2` 编 [quic_transport_loopback_smoke.cheng](/Users/lbcheng/cheng-lang/v3/src/tests/quic_transport_loopback_smoke.cheng) 时，`primary_object_unsupported_function_count` 已从之前的一长串压到只剩 `msquicNativeResetClientEndpoint/msquicNativeResetServerEndpoint` 两个函数。 |
| 新发现 | [native_runtime.cheng](/Users/lbcheng/cheng-lang/v3/src/quic/native_runtime.cheng) 这条线最有效的做法，仍然是直接展开 `Connection/ConnState/Flow/Perf` 叶子字段，而不是继续堆新的 wrapper。把这些前半段展开后，`stage2` 第一现场已经从整块 init 链前移到 `let nowMs: int64 = msquicNowMs()` 这一层。 |
| 新发现 | [test_pki.cheng](/Users/lbcheng/cheng-lang/v3/src/quic/tls/test_pki.cheng) 的 `MsQuicTlsPolicy` 返回值构造不会打坏 host 主线；[quic_transport_loopback_smoke.cheng](/Users/lbcheng/cheng-lang/v3/src/tests/quic_transport_loopback_smoke.cheng) 和 [quic_tls_policy_smoke.cheng](/Users/lbcheng/cheng-lang/v3/src/tests/quic_tls_policy_smoke.cheng) 这轮都重新真编真跑输出 `ok`。 |
| 新发现 | `test_pki` 的 stage2 活根也更清楚了：当前不是证书内容不对，而是 [msquicMakeLocalhostTlsPolicy](</Users/lbcheng/cheng-lang/v3/src/quic/tls/test_pki.cheng>) 自己还会撞到 `out.serverName = ...` 这种复合 materialize。下一刀该继续把 stage2 主路径从 `MsQuicTlsPolicy` 这整个 record 上移开，而不是回头怀疑证书解码。 |

## Findings

| 项目 | 结论 |
|---|---|
| 新发现 | [connection_impl.cheng](/Users/lbcheng/cheng-lang/v3/src/quic/core/connection_impl.cheng) 里额外包的一层 `initMsQuicConnImplFromConnection(...)` 不能留。它会把 host 编译面直接拖回 `primary object body semantics missing`，正确口径仍然是 `native_runtime` 里用全局 `ConnState/Flow/Perf` 直连 `initMsQuicConnImpl(...)`。 |
| 新发现 | [native_runtime.cheng](/Users/lbcheng/cheng-lang/v3/src/quic/native_runtime.cheng) 里继续扁平化 `client/server` 状态是有效推进：fresh stage2 的首个 unsupported 已从旧 `msquicNativeResetEndpoint` 继续前移到 `msquicNativeResetClientEndpoint`，不再卡在重型 `if`、`msquicNativeHandshakeIsClient` 或 `session.client.sendOffset` 这种外壳字段写入。 |
| 新发现 | [test_pki.cheng](/Users/lbcheng/cheng-lang/v3/src/quic/tls/test_pki.cheng) 这条线 host 已不再回归，但 stage2 还剩活根：`msquicLocalhost*Into(...)` 的 `var Bytes` 填充和 `MsQuicTlsPolicy` 的直接字段写回仍会报 ordinary/state 问题。下一刀该继续收 `test_pki/policy -> handshake` 的标量入口，不该再回头碰已收掉的 `ResetEndpoint` 外壳。 |
| 新发现 | 这轮 host 验收已重新钉实：`DIAG_CONTEXT=1 ... quic_transport_loopback_smoke ... && /tmp/quic_transport_loopback_smoke.host.after_flatten` 继续输出 `v3 quic_transport_loopback_smoke ok`。也就是说，这轮所有 stage2 推进都没有把 host 主线打坏。 |

## Findings

| 项目 | 结论 |
|---|---|
| 新发现 | `chain_node_libp2p_smoke` 之前那条旧红，不是 `store_sync` 或 TCP 请求响应把字节传坏了。新增的 [chain_node_snapshot_sync_roundtrip_smoke.cheng](/Users/lbcheng/cheng-lang/v3/src/tests/chain_node_snapshot_sync_roundtrip_smoke.cheng) 已经证明 `serve payload -> entries decode -> latest snapshot -> decode -> sync` 手工路径全通，`payloadCid/bodyCid/eventCid` 也都对得上。 |
| 新发现 | 真根在 [v3ChainNodeLibp2pSyncOnce](</Users/lbcheng/cheng-lang/v3/src/project/chain_node_libp2p.cheng>) 自己的复合边界，不在协议面。只要让 wrapper 先返回 `Ingress`、再返回 `Snapshot`、再做 `SnapshotCopy` 这种二次复制，运行期就会在 `DecodeSnapshot` 或 `v3ConsensusEnsureAccount` 里炸；把它改成 `entries -> latest payload -> decode fill -> sync` 的直线后，问题立刻消失。 |
| 新发现 | 当前 ordinary/host 主线上，`Result.value` 的大复合临时对象也不能继续直接穿进热路径。把 [chain_node_libp2p_smoke.cheng](/Users/lbcheng/cheng-lang/v3/src/tests/chain_node_libp2p_smoke.cheng) 的 dry-run 从 `decodedSnapshotRes.value` 直传，改成先落地本地 `decodedSnapshot` 再传之后，这条 smoke 的前置堆污染也一起消失了。 |
| 新发现 | 这轮收掉 wrapper 复合边界以后，整组 [run_v3_host_smokes.sh](/Users/lbcheng/cheng-lang/v3/tooling/run_v3_host_smokes.sh) 和 [run_v3_stage23_libp2p_smokes.sh](/Users/lbcheng/cheng-lang/v3/tooling/run_v3_stage23_libp2p_smokes.sh) 都已经重新前台输出 `ok`。`chain_node_libp2p` 这条当前不能再算活 blocker。 |

## Findings

| 项目 | 结论 |
|---|---|
| 新发现 | `X509PublicKey` 这条树之前确实太胖：把 [src/std/tls/x509.cheng](/Users/lbcheng/cheng-lang/src/std/tls/x509.cheng) 里的 `RsaBigPublicKey + EcPublicKey` 改成 `rsaN/rsaE/ecdsaBytes` 以后，host 上 `quic_transport_loopback` 和 `libp2p_quic_tls` 都不再卡 `state.peerLeaf.publicKey.ecdsa/rsa` 这条复合参数链。说明先拆类型树是对的，不是继续在 `native_runtime` 表层抠单个 `if`。 |
| 新发现 | 只把 `x509` 变薄还不够，`handshake13` 里也不能再保留旧的复合 key 调用形状。把 [handshake13.cheng](/Users/lbcheng/cheng-lang/v3/src/quic/tls/handshake13.cheng) 的 `msquicTls13ParseCertificateVerify(...)` 改成 `let + helper`，并统一走 `rsaVerifyPssBytes/ecdsaVerifyTrustedPublicKeyBytes` 之后，fresh host 编译的唯一 unsupported 就直接消失了。 |
| 新发现 | 这轮 host 结果已经重新钉实：`/tmp/quic_transport_loopback_smoke.host.certverify_flat` 真跑输出 `v3 quic_transport_loopback_smoke ok`，`/tmp/libp2p_quic_tls_smoke.host.certverify_flat` 真跑输出 `v3 libp2p_quic_tls_smoke ok`。所以 QUIC/libp2p 这条 host 主线目前是重新闭合的。 |
| 新发现 | `stage2` 这边新的真根也更清楚了：首批 unsupported 现在主要剩 [src/std/tls/x509.cheng](/Users/lbcheng/cheng-lang/src/std/tls/x509.cheng) 的普通条件发码和 `X509Certificate/X509CertList` 布局，以及 [native_runtime.cheng](/Users/lbcheng/cheng-lang/v3/src/quic/native_runtime.cheng) 里还直接碰 `MsQuicTls13HandshakeState/MsQuicConnImpl` 的边界函数。也就是说，下一刀该继续拆 `x509` 证书体和 `native_runtime` 的 owner 边界，不该回头怀疑 `X509PublicKey` 这层。 |

| 项目 | 结论 |
|---|---|
| 新发现 | `RSA-PSS` 这次真根不在 `modexp`，也不在证书链，而在 [rsaBuildPssEncodedMessage](/Users/lbcheng/cheng-lang/src/std/crypto/rsa.cheng) 自己的 roundtrip 自检。`maskedDb` 在清掉高位以后，再直接和原始 `db` 做往返比对，本身就会把合法 `EM` 误判成坏包；把比较改成“同样清位后的 `dbExpected`”后，[quic_tls_policy_smoke.cheng](/Users/lbcheng/cheng-lang/v3/src/tests/quic_tls_policy_smoke.cheng) 立刻转绿。 |
| 新发现 | `quic_tls_policy` 一旦转绿，后面的 `tls13 certificate verify failed` 就一起消失了。现在 [quic_transport_loopback_smoke.cheng](/Users/lbcheng/cheng-lang/v3/src/tests/quic_transport_loopback_smoke.cheng)、[libp2p_quic_tls_smoke.cheng](/Users/lbcheng/cheng-lang/v3/src/tests/libp2p_quic_tls_smoke.cheng)、[content_quic_smoke.cheng](/Users/lbcheng/cheng-lang/v3/src/tests/content_quic_smoke.cheng) 都已经真编真跑输出 `ok`，说明这轮 QUIC 真 blocker 已经收口。 |
| 新发现 | 内容平面真正缺的不是新类型，而是一条把入口和出口焊在一起的 smoke。把 [content_stub_smoke.cheng](/Users/lbcheng/cheng-lang/v3/src/tests/content_stub_smoke.cheng) 扩成 `stub -> sync fetch advert/manifest/recoveryPlan -> build pull targets -> TCP shard pull -> rebuildAgainstStub` 后，首页入口对象到可校验 blob 的最小闭环已经被真跑钉死。 |
| 新发现 | 文档口径之前确实落后于实现：`内容平面.md` 少写了 `recoveryPlan` 的 `store/sync` 回拉、少写了 `4 条 ingress`，`发布订阅.md` 少写了 `payloadKind = 6`。现在这些口径已经追平当前代码，不再把旧状态留在文档里。 |
| 新发现 | [run_v3_host_smokes.sh](/Users/lbcheng/cheng-lang/v3/tooling/run_v3_host_smokes.sh) 这轮没有被内容平面或 QUIC 新改动打坏。它当前仍然只在旧 [chain_node_libp2p_smoke.cheng](/Users/lbcheng/cheng-lang/v3/src/tests/chain_node_libp2p_smoke.cheng) 上失败，报 `v3 consensus: body cid mismatch`；这条链要单独进 `chain_node/consensus` 查，不能算内容平面回归。 |

## Findings

| 项目 | 结论 |
|---|---|
| 新发现 | 内容平面这条线真正不稳的不是协议设计，而是 ordinary 对“大复合对象字段上边 `add` 边返回”的 lowering。把 [v3ErasureBuildRecoveryPlan](/Users/lbcheng/cheng-lang/v3/src/chain/erasure_swarm.cheng)、[v3DagAvailabilityCertMake](/Users/lbcheng/cheng-lang/v3/src/chain/dag_mempool.cheng) 收成“先用局部数组收集，再一次性写回复合对象”，`content_runtime_smoke` 和 `content_stub_smoke` 都随之闭合。 |
| 新发现 | `shard pull request/response` 这层也不能继续依赖循环里复合 `Result` 搬运。给 [erasure_swarm.cheng](/Users/lbcheng/cheng-lang/v3/src/chain/erasure_swarm.cheng) 补 `v3ErasureShardPullRequestFill / v3ErasureShardPullResponseFill` 之后，[content_runtime_smoke.cheng](/Users/lbcheng/cheng-lang/v3/src/tests/content_runtime_smoke.cheng) 里的 `shardIndex` 污染和越界访问已经消失。 |
| 新发现 | `bundleCid` 那条长参数 CID 生成面会把 ordinary 调用约定压坏。把 [content_plane.cheng](/Users/lbcheng/cheng-lang/v3/src/chain/content_plane.cheng) 的 bundle 计算改成 `v3ContentPublishArtifactsExpectedBundleCid(artifacts)` 这种单参路径后，`content_stub_smoke` 不再在 `v3AppendFixed32(...)` 上段错。 |
| 新发现 | `content_stub_smoke` 里最稳的写法不是“复杂 helper + 复合 out-param”，而是把 artifacts 直接在 `main` 里顺序组装，再直接调用 [v3Libp2pRelayPublishContent](/Users/lbcheng/cheng-lang/v3/src/overlay/libp2p_bridge.cheng)。当前这条 smoke 已能真编真跑输出 `v3 content_stub_smoke ok`。 |
| 新发现 | 当前 host gate 上已经闭合的内容平面 smoke 不再只有 `content_codec_smoke`。现在 [run_v3_host_smokes.sh](/Users/lbcheng/cheng-lang/v3/tooling/run_v3_host_smokes.sh) 应该正式包含 `content_codec_smoke`、`content_runtime_smoke`、`content_stub_smoke`；`content_quic_smoke` 仍留在 gate 外，因为基线 QUIC datapath 还没闭合。 |
| 新发现 | 当前内容平面 smoke 里，唯一已经独立真编真跑闭合的是 [content_codec_smoke.cheng](/Users/lbcheng/cheng-lang/v3/src/tests/content_codec_smoke.cheng)。它已经可以稳定输出 `v3 content_codec_smoke ok`，所以这轮可以正式进 [run_v3_host_smokes.sh](/Users/lbcheng/cheng-lang/v3/tooling/run_v3_host_smokes.sh)。 |
| 新发现 | [content_stub_smoke.cheng](/Users/lbcheng/cheng-lang/v3/src/tests/content_stub_smoke.cheng) 现在不是协议逻辑失败，而是 ordinary 编译器对 helper 形状还不稳，当前会停在 `stmt_let` 这一层；在 smoke 自身收平之前，不该塞进 host gate。 |
| 新发现 | [content_runtime_smoke.cheng](/Users/lbcheng/cheng-lang/v3/src/tests/content_runtime_smoke.cheng) 现在有真实运行时问题：`requests[i].shardIndex` 会落成坏值，随后触发越界访问；这说明恢复计划到 pull request 这条链还没闭合，不能冒充成稳定 smoke。 |
| 新发现 | [content_quic_smoke.cheng](/Users/lbcheng/cheng-lang/v3/src/tests/content_quic_smoke.cheng) 目前被现有 QUIC datapath 问题拦住，连基线 [libp2p_quic_tls_smoke.cheng](/Users/lbcheng/cheng-lang/v3/src/tests/libp2p_quic_tls_smoke.cheng) 也没闭合，所以内容协议的 QUIC gate 这轮必须继续留在 runner 外。 |
| 新发现 | 对这轮内容平面接线，正确口径不是“把所有新 smoke 一起挂进 runner”，而是“只把已闭合项升进 gate”。因此 host runner 现在应该只新增 `content_codec_smoke`，继续把 `content_stub/content_runtime/content_quic` 留在定向修复面。 |

## Findings

| 项目 | 结论 |
|---|---|
| 新发现 | `MsQuicTlsPolicy` 这轮已经不再是 `quic_native_listener_smoke` 的第一现场。把 `policy` 数组壳拆平、`test_pki` 切成标量 decode 入口、`ensureInit()` 去掉 eager session reset、`initMsQuicSettings()` 改成逐字段赋值以后，fresh host/stage2 unsupported 列表里 `initMsQuicSettings` 和 `msquicTlsPolicyHasServerInputs` 都已经掉出第一现场。 |
| 新发现 | `quic_tls_policy_smoke` 现在露出来的是 ordinary smoke 形状本身，不再是 QUIC/TLS 专属逻辑根。当前它会先卡 `main` 的 `stmt_call/stmt_let` 形状和 `assert`/module const/call 组合，不值得再用它指导 `native_runtime` 修复。 |
| 新发现 | `quic_native_listener_smoke` 当前真正该打的点已经收窄成五个：`native_runtime::msquicNativeTaggedAddr(...)`、`native_runtime::msquicNativeStartListenerServerInputs(...)`、`platform/datapath_runtime::initMsQuicDatapathRuntime()`、`platform/datapath_udp::msquic_udp_close(...)`、`platform/datapath_udp::udpParseAddr(...)`。这说明下一刀应该继续收 `listener/datapath`，不是回去碰 `Policy` 复合布局。 |
| 新发现 | `strToCStringTemp` 这轮已经不是 `QUIC/TLS` 真 blocker。把 [src/std/system.cheng](/Users/lbcheng/cheng-lang/src/std/system.cheng) 的 `strToCStringTemp(s: str)` 收成 `cheng_str_to_cstring_temp_bridge(...)` 后，新补的 [udp_importc_smoke.cheng](/Users/lbcheng/cheng-lang/v3/src/tests/udp_importc_smoke.cheng) 已在 host、`stage2`、`stage3` 三条线上真编真跑通过；说明 `str + cstring` 这层现在已经闭合。 |
| 新发现 | `std/os + udp_syscall` 之前确实是 `QUIC/TLS` 的一层真前置，但现在也已经闭合：`src/std/os.cheng` 已切到 `importc fn + libc_*` 宿主桥，`src/std/net/transports/udp_syscall.cheng` 也不再依赖 `std/system` 的 backend 幻影 `socket/getsockname`。这条线现在已经正式挂进 [run_v3_host_smokes.sh](/Users/lbcheng/cheng-lang/v3/tooling/run_v3_host_smokes.sh)，host 全量重新前台通过。 |
| 新发现 | full [libp2p_quic_tls_smoke.cheng](/Users/lbcheng/cheng-lang/v3/src/tests/libp2p_quic_tls_smoke.cheng) 的 `rc=139 + 0 字节日志` 不是新的修补把编译器打坏了，而是 `quic_transport` 一口气把 `test_pki/x509/rsa/bigint` 和 `msquictransport_native/native_runtime` 两个大闭包同时拖进来，导致 ordinary/seed 在大栈帧上直接炸。把它拆成 [quic_tls_policy_smoke.cheng](/Users/lbcheng/cheng-lang/v3/src/tests/quic_tls_policy_smoke.cheng) 和 [quic_native_listener_smoke.cheng](/Users/lbcheng/cheng-lang/v3/src/tests/quic_native_listener_smoke.cheng) 之后，host 和 `stage2` 都已经不再空日志段错，而是稳定给出 ordinary unsupported 列表。 |
| 新发现 | split smoke 现在把 `QUIC/TLS` 的真缺口分得很清楚：`quic_tls_policy_smoke` 第一现场已经固定到 `cheng/v3/quic/tls/test_pki::msquicMakeLocalhostTlsPolicyInto` 和 `cheng/v3/quic/tls/policy::initMsQuicTlsPolicy` 的 `stmt_call/stmt_var + composite return`；`quic_native_listener_smoke` 则固定暴露 `msquictransport_native/native_runtime`、`MsQuicTransport/MsQuicTlsPolicy/MsQuicNativeSession` 这批大复合布局和 ABI 子集。下一刀该打的是这两层 fixed-layout/ordinary 子集，不该再回头碰 UDP 或 `strToCStringTemp`。 |

| 项目 | 结论 |
|---|---|
| 新发现 | `v2/cheng-quic` 不是“没东西可用”，而是已经把一批真 Cheng 源码迁进 `v3` 以后，当前新的真实瓶颈变成了 `v3` 编译器自己。`strToCStringTemp/charToStr` 和 `congestion` 链这几层压平之后，fresh [libp2p_quic_tls_smoke.cheng](/Users/lbcheng/cheng-lang/v3/src/tests/libp2p_quic_tls_smoke.cheng) 现在最先死在 [cheng_v3_seed.c](/Users/lbcheng/cheng-lang/v3/bootstrap/cheng_v3_seed.c) 的 `v3_resolve_call_target(...)` 栈炸，不再是缺模块。 |
| 新发现 | [connection_impl.cheng](/Users/lbcheng/cheng-lang/v3/src/quic/core/connection_impl.cheng) 里原本那整条 `congestion/bbr/cubic/loss_detection` 对当前 `libp2p_quic_tls_smoke` 是纯拖累，不是主路径必需。把它收成最小可靠发送状态机后，编译阻塞列表里那批 `congestion/*` 已经整段消失，说明继续缩依赖面是对的。 |
| 新发现 | [msquicconnection.cheng](/Users/lbcheng/cheng-lang/v3/src/quic/msquicconnection.cheng) 原先那坨 `msquic_types` 大对象外部几乎没人真用；当前主线只依赖 `init/isOpen/touch/openStream/close` 这小撮接口。把它收成最小连接态后，`MsQuicConnection` 已不再是当前第一现场。 |
| 新发现 | `v3_resolve_bare_call_symbol(...)` 和 `v3_const_resolve_call_symbol(...)` 原来会直接把坏 alias / callee 指针喂给 `snprintf`，host compiler 会先在 libc 里炸掉，连 Cheng 侧诊断都来不及打印。现在前一层坏指针崩溃已经收成安全失败，剩下的活根前移到了更深的 `v3_resolve_call_target(...)` 递归。 |

| 项目 | 结论 |
|---|---|
| 新发现 | fresh `run_slice_gate.sh` 这次露出来的真问题，不是 `msquicconnection.cheng:78` 这一行写错了，而是 base [host.cheng](/Users/lbcheng/cheng-lang/v3/src/libp2p/host/host.cheng) 无条件 import 了 [quic_transport.cheng](/Users/lbcheng/cheng-lang/v3/src/libp2p/transports/quic_transport.cheng)。这样一来，哪怕 `chain_node_libp2p_smoke` 只走 TCP，也会被 fresh `backend_driver` 强行拖进整条 `msquic` 闭包。 |
| 新发现 | 正确修法不是改 `destCidUpdateCount`、也不是继续让 host 假装“同时内建所有 transport”。这轮已经把 `host` 收回 base/TCP 语义，并新增 [host_quic.cheng](/Users/lbcheng/cheng-lang/v3/src/libp2p/host/host_quic.cheng) 单独承接 QUIC 同步路径；结果是 fresh `artifacts/v3_backend_driver/cheng` 现在又能真编 [chain_node_libp2p_smoke.cheng](/Users/lbcheng/cheng-lang/v3/src/tests/chain_node_libp2p_smoke.cheng)，`run_v3_host_smokes.sh` 也重新全绿。 |
| 新发现 | 当前 `stage2/stage3` 已闭合的 libp2p 主线是 `core/host/tcp/overlay/protocols/chain_node_libp2p + tcp twoproc/process`，不是完整 `QUIC/TLS` 闭包。`libp2p_quic_tls_smoke` 继续保留，但当前不该放在 mandatory [run_v3_stage23_libp2p_smokes.sh](/Users/lbcheng/cheng-lang/v3/tooling/run_v3_stage23_libp2p_smokes.sh) 里冒充“已经闭合的主线”；这轮已经把 gate 口径收正。 |
| 新发现 | `v3` 目录里之前真正会误导人的，不是代码，而是文档：两份 README 和两份状态文档还在写旧 `chain_node` 入口和未闭合的 CLI/QUIC 能力面。现在这几处都已经收回当前真状态：`v3/src/project/chain_node_main.cheng` 只是 `self-test` 入口，真实跨进程链路验收靠 `chain_node_process_smoke`。 |

| 项目 | 结论 |
|---|---|
| 新发现 | `v3` 的 `FFI handle` 这条现在已经有世代索引，`ABA` 真根不在“槽位复用会指到新对象”，而在失效句柄语义还太软：released / stale handle 之前只是返回 `-1`，调用方一旦漏判，逻辑就会继续往下跑。真正符合 `Let it crash` 的收法是 runtime 自己立刻打栈退出。 |
| 新发现 | `chain_node_libp2p_smoke` 这次 host driver 编译期爆栈，真根不在 `chain_node/libp2p` 逻辑，而在 [cheng_v3_seed.c](/Users/lbcheng/cheng-lang/v3/bootstrap/cheng_v3_seed.c) 的 `v3_call_expr_string_literal_count(...)` 会在某些表达式分解上原地自递归，最后把 seed 自己栈打穿。只要把递归条件收成“子表达式必须更短且和原文不同”，这条编译期 crash 就会消失。 |
| 新发现 | `libp2p_tcp_twoproc` 这条真正脆的不是 socket，而是 ready 同步。server 端只要没把端口稳定写进 `ready-path`，脚本层就会看起来像“TCP 没连上”；把 [run_v3_tcp_twoproc_smoke.sh](/Users/lbcheng/cheng-lang/v3/tooling/run_v3_tcp_twoproc_smoke.sh) 收回统一 ready 文件口径后，host 和 `stage2` 的两进程 smoke 都稳定闭合。 |
| 新发现 | `chain_node_process_client_smoke` 那条假失败也不是 snapshot/网络错，而是它自己把 `--port` 先读成字符串再走 `parseutils`，把问题重新拖回 ordinary lowering。直接改成 `driver_c_read_int32_flag_or_default_bridge(...)` 后，客户端接收和签名校验立刻恢复正常。 |
| 新发现 | 这轮之后真正可以拿来当总验收的是 [run_slice_gate.sh](/Users/lbcheng/cheng-lang/v3/tooling/run_slice_gate.sh)，不是单点 targeted。`build_chain_node_v3`、`host smokes`、`stage23 libp2p smokes`、`ffi_handle` trap 现在都已经被它串进同一条前台 gate。 |
| 新发现 | `stage2/stage3 libp2p` 这轮新冒出来的 `libp2p_host_smoke -> unresolved_import_count=1` 不是源码真缺口，而是 incremental/module-cache 把 `QUIC` 闭包脏带进了 `TCP host` smoke。把 [run_v3_stage23_libp2p_smokes.sh](/Users/lbcheng/cheng-lang/v3/tooling/run_v3_stage23_libp2p_smokes.sh) 收成 `BACKEND_INCREMENTAL=0 BACKEND_MULTI_MODULE_CACHE=0` 之后，`libp2p_host_smoke` 和 `libp2p_protocols_smoke` 都重新回到干净闭包。 |
| 新发现 | 缓存假红消掉以后，当前真正还没闭合的就是 `QUIC/TLS` ordinary compile 本体：`libp2p_quic_tls_smoke` 现在第一时间撞到 `std/system::strToCStringTemp`，后面还连着 `native_runtime/x509/msquic` 这一整串 `stmt_let + cstring + TLS` 子集。这条线已经不是脚本噪音，可以直接当下一主线推进。 |

| 项目 | 结论 |
|---|---|
| 新发现 | `libp2p sync` 之前真正没过网的地方，不在 `tcp payload bridge`，而在请求侧自己：只要 [host.cheng](/Users/lbcheng/cheng-lang/v3/src/libp2p/host/host.cheng) 还直接拿 `remoteHost.storeEntries` 组结果，就不能算 `request-response`。这轮把它改成 `query bytes encode -> 真 TCP RR -> entries decode` 后，`libp2p_protocols_smoke` 才第一次对上真正的协议面。 |
| 新发现 | 当前 ordinary 子集还不适合拿 `std/os` 的 `pipeSpawn/fdReadWait/main(argc, argv)` 去硬写单文件两进程 smoke；那条线会重新把问题拖回元组返回和命令行 lowering。更短更稳的收法是拆成独立 [libp2p_tcp_twoproc_server_smoke.cheng](/Users/lbcheng/cheng-lang/v3/src/tests/libp2p_tcp_twoproc_server_smoke.cheng) / [libp2p_tcp_twoproc_client_smoke.cheng](/Users/lbcheng/cheng-lang/v3/src/tests/libp2p_tcp_twoproc_client_smoke.cheng)，再用 shell 编排。 |
| 新发现 | 两进程同步最稳的不是 stdout `READY`，而是 `ready-path` 文件写端口。统一改成 native listener 写文件后，[run_v3_tcp_twoproc_smoke.sh](/Users/lbcheng/cheng-lang/v3/tooling/run_v3_tcp_twoproc_smoke.sh) 和 [run_v3_chain_node_process_smoke.sh](/Users/lbcheng/cheng-lang/v3/tooling/run_v3_chain_node_process_smoke.sh) 已经在 host、`stage2`、`stage3` 三条线上都稳定闭合。 |
| 新发现 | 这轮之后 `v3` 的 libp2p 正式 gate 已经不只是在单进程里做状态搬运：`stage2/stage3` 的 [run_v3_stage23_libp2p_smokes.sh](/Users/lbcheng/cheng-lang/v3/tooling/run_v3_stage23_libp2p_smokes.sh) 现在真包含 `tcp twoproc`、`protocols sync rr`、`chain_node_libp2p` 和 `chain_node_process` 的跨进程验证，并且 fresh 全绿。 |

| 项目 | 结论 |
|---|---|
| 新发现 | `v3` 当前真正“接上真 TCP”的地方，之前只有 C 里的 `multistream` loopback 探针；[host.cheng](/Users/lbcheng/cheng-lang/v3/src/libp2p/host/host.cheng) 仍然只是 `listenAddrs/peerstore/publishLog/storeEntries` 的内存状态机，不能再把它口头说成真实 underlay。 |
| 新发现 | 这轮正确推进方式不是硬把 `host` 重写成 socket host，也不是把 `std/net/tcp_syscall` 生吞进 `stage2/stage3` 主链。当前 host 编译器还吃不稳那条 ABI，直接走会把问题重新拖回 `std/net`。更短更稳的收法是沿 [system_helpers.c](/Users/lbcheng/cheng-lang/src/runtime/native/system_helpers.c) 现有真 socket 骨架，扩一个带 raw payload 的 `cheng_v3_tcp_loopback_payload_bridge(...)`。 |
| 新发现 | [libp2p_tcp_smoke.cheng](/Users/lbcheng/cheng-lang/v3/src/tests/libp2p_tcp_smoke.cheng) 现在已经不是“空 multistream 握手成功”这种薄 smoke，而是真跑 `chain_node mint/transfer -> snapshot encode -> 真 TCP 二进制往返 -> snapshot decode -> syncOnce -> balance/signature`。这条线说明 `v3` 现在至少已经有一条不经过内存 `host` 的真实 underlay 二进制链。 |
| 新发现 | 当前最该保持清醒的边界是：`tcp underlay` 已经开始真实化，但 `libp2p host/overlay/store/sync` 仍主要是进程内状态面。下一刀该去做真实两进程 one-shot，而不是把现在的 host 描述成已经完成的 libp2p daemon。 |

| 项目 | 结论 |
|---|---|
| 新发现 | `chain_node_libp2p` 这次的真根不在 `store`，也不在 `V3IngressEnvelope.payload` 的 `Bytes` 搬运链。把 [chain_node_libp2p_smoke.cheng](/Users/lbcheng/cheng-lang/v3/src/tests/chain_node_libp2p_smoke.cheng) 补上 `payloadLen/payloadCid/raw payload` 校验后，这些断言全部照样通过，说明 snapshot 字节在入库和取回前后一致。 |
| 新发现 | 活根其实在 [chain_node_libp2p.cheng](/Users/lbcheng/cheng-lang/v3/src/project/chain_node_libp2p.cheng) 这段私有 codec 自己：`DecodeSignature/DecodeEvent` 原来是 `Result[bool] + var nextOffset`，把关键偏移推进藏在副作用里；只要 ordinary lowering 或寄存器搬运对这个形状不稳，后面的 event decode 就会整段错位，表面上只剩 `decode snapshot`。把它们改成直接返回 `Result[int32]` 新偏移后，snapshot 立刻恢复闭合。 |
| 新发现 | `v3ChainNodeLibp2pReadI64BE(...)` 也不能继续保留那条宽松口径。对齐到已经真跑通的 `rwad_bft` 实现后，`chain_node_libp2p` 的 `amount/tick` 重组和其它固定宽度冷轨实现终于统一，不再靠“当前输入刚好都是小正数”碰运气。 |
| 新发现 | `chain_node_libp2p_smoke` 现在已经不只是手工 targeted 通过，而是正式升进了两条 gate：[run_v3_host_smokes.sh](/Users/lbcheng/cheng-lang/v3/tooling/run_v3_host_smokes.sh) 和 [run_v3_stage23_libp2p_smokes.sh](/Users/lbcheng/cheng-lang/v3/tooling/run_v3_stage23_libp2p_smokes.sh)。`stage2`、`stage3` 和 host 三条线上都已经真编真跑输出 `v3 chain_node_libp2p_smoke ok`。 |

| 项目 | 结论 |
|---|---|
| 新发现 | `run_v3_stage23_libp2p_smokes.sh` 这次冒出来的 `libp2p_protocols_smoke -> stmt_var` 不是源码真退化，而是 [artifacts/v3_bootstrap](/Users/lbcheng/cheng-lang/artifacts/v3_bootstrap) 里的 `cheng.stage2/cheng.stage3` 还停在旧自举产物。拿当前 host driver 单独编同一 smoke 会直接通过；fresh 重跑 `bootstrap_bridge_v3.sh` 后，`stage2/stage3` 五个 libp2p smoke 立刻一起转绿。 |
| 新发现 | `kind: str` 这类热点形状只要回流进 [system_link_plan.cheng](/Users/lbcheng/cheng-lang/v3/src/backend/system_link_plan.cheng)，`run_slice_gate.sh` 第一关就会立刻打死。这次正确收法不是继续保留 `parserSourceKind: str`，而是热面只存整型 kind，真正要给人看的字符串延后到 report 层再生成。 |
| 新发现 | 之前看起来还悬着的 `chain_node` 签名断言现在已经闭合。把 [chain_node_smoke.cheng](/Users/lbcheng/cheng-lang/v3/src/tests/chain_node_smoke.cheng) 的 `anti.v3AntiEntropySignatureEqual(served.signature, clientServed.signature)` 恢复回正式 smoke 后，fresh `run_v3_host_smokes.sh`、`build_chain_node_v3.sh` 以及最终的 `run_slice_gate.sh` 都已真过；这条线不用再当成未完成事项挂着。 |

| 项目 | 结论 |
|---|---|
| 新发现 | 只把 `payloadCid/payloadLen` 放进 `IngressEnvelope` 还不够，后面的 `chain_node` 根本没有真实载荷面可以挂。把 `overlay.V3IngressEnvelope` 补成 `sourcePeerId/topicScope/address/payload/payloadCid/payloadLen/stamp*` 后，`libp2p` 这条线终于能承载真正的 bytes，不再只是 metadata 壳。 |
| 新发现 | `libp2p_protocols_smoke` 这轮已经把最小协议载荷面钉死：同一个 ingress 经 `bridge.v3Libp2pRelayPublish(...)` 之后，`topicId`、`publishLog`、`storeEntries`、`payloadLen`、`payloadCid` 和 raw payload 字节头都保持一致，而且这条线已在 `stage2/stage3` 双编双跑真过。 |
| 新发现 | 现在 `run_v3_stage23_libp2p_smokes.sh` 已不再只是 `core/host/tcp/overlay` 四件套，而是正式扩成五个 smoke：`libp2p_core`、`libp2p_host`、`libp2p_tcp`、`libp2p_overlay`、`libp2p_protocols`。这说明下一刀可以直接去做 `chain_node <-> libp2p` 适配层，不用再先补“payload bytes 是否能过 stage23”这种前置壳。 |

| 项目 | 结论 |
|---|---|
| 新发现 | `libp2p_overlay_smoke` 这轮真正的真根不是 `LSMR overlay` 算法，也不是 `store/sync` 语义，而是 ordinary 当前对“复合 zero helper + wrapper helper + nested helper call”这层壳非常不稳。只要把 `overlay` 真实数据面包进 `v3OverlayIngressEnvelopeZero()`、`v3Libp2pStoreQueryCount()`、`main -> smokeOverlayRun()` 这种壳里，就会不断把失败伪装成 `stmt_var/stmt_let/prepare expr call state failed`。 |
| 新发现 | 正确修法不是继续补更多 helper，而是反过来把 helper 壳拔掉：`libp2p_overlay_smoke` 现在直接检查 `node.storeEntries` 这条真实 host 状态，`host` 和 `core/types` 只保留必要字段写回；`src/std/rawbytes.cheng` 和 `src/std/crypto/sha256.cheng` 也同步拆平，把 `Bytes`/`u32` 这类热 helper 收成 ordinary 已验证过的局部写回形状。 |
| 新发现 | 这刀落下后，`libp2p_overlay_smoke` 已经不再单独需要人工 targeted compile；正式脚本 `run_v3_stage23_libp2p_smokes.sh` 现在会完整跑过 `libp2p_core_smoke`、`libp2p_host_smoke`、`libp2p_tcp_smoke`、`libp2p_overlay_smoke` 的 `stage2/stage3` 双编双跑，并稳定输出 `v3 stage2/stage3 libp2p: ok`。 |

| 项目 | 结论 |
|---|---|
| 新发现 | `v3` 这轮 `sha256/fixed256` 真根不是算法，也不是 `while` 本身，而是 seed 对零参调用的临时文本缓冲区没做初始化。像 `emptyBytes()` 这种 `parse_call_text(...)` 成功但没有参数的调用，会把旧栈里的脏字节误当成 `prefix single arg` 的实参文本。 |
| 新发现 | 这条脏读会把 `std/rawbytes::bytesFromString/bytesAlloc` 先伪装成 `stmt_if/body semantics missing`，再把 `sha256_runtime/fixed256_sha256/compiler_pipeline` 一起拖坏。把 `v3/bootstrap/cheng_v3_seed.c` 里的 `arg_text/result_arg_text` 和 `V3ExprPrepScratch` 全部收成确定初始化后，fresh `sha256_inline/runtime/core/schedule/round`、`fixed256_sha256`、`compiler_pipeline_stub` 已全部重新真过。 |

| 项目 | 结论 |
|---|---|
| 新发现 | `compiler_runtime_smoke` 这次真崩点不是 `build_plan` 算法，也不是 runtime bridge，而是 seed 自己在 `v3_prepare_expr_call_state_impl(...)` 里把 `V3ExprPrepScratch` 的定长数组先退化成 `char*`，后面又拿 `sizeof(expr)` 之类当容量。结果所有 prepare 输入都会被截成 7 字节，`max_call_depth` 被错误算成 `0`，后面字符串 scratch 直接写进保存的 `x29/x30`。 |
| 新发现 | `prepare` 里还有第二个结构性错误：`V3BackendBuildPlan(...)`、`V3CompilerRuntimeContract(...)`、`V3BackendSourceUnit(...)` 这种构造器根本不是 runtime call，但旧逻辑仍强行走 `resolve_call_target + composite arg temp`。这会让 prepare 在第一层字段上就提前失败，后面的嵌套 `v3BackendRootPath(...)` 根本来不及被统计进调用深度。 |
| 新发现 | 现在这两处已经一起收掉：prepare 改回按真实数组容量递归，构造器只递归字段表达式、不再走 runtime call 准备；同时保留 `frame layout` 硬校验，直接阻断 `string temp` 和保存寄存器重叠。fresh 结果已经固定成真通过：`compiler_runtime_smoke`、`compiler_pipeline_stub_smoke`、`v3 twoway search smoke` 全绿。 |
| 新发现 | 修完后 `v3BackendBuildPlanDefault` 的真机器布局已经回到安全区：旧坏帧是 `sub sp, sp, #464` 且把字符串 scratch 放到 `sp+456`，直接踩 `sp+448` 的 `FP/LR`；现在新帧是 `sub sp, sp, #1616`，字符串 scratch 在 `sp+1384`，保存寄存器在 `sp+1600`。这条线已经从“运行时跳进 `.cstring`”收成“编译期布局和 prepare 事实一致”。 |

| 项目 | 结论 |
|---|---|
| 新发现 | `v3` 这轮 `libp2p` 真主根先后换了两次，但现在已经钉死：`peer_id` 的崩点不是 SHA 算法，而是 ordinary 对“`str -> FixedBytes32` 复合返回”不稳。改成 native `sha256 word bridge` 再直接写 `out.value.data[0..31]` 后，`libp2p_peer_id_smoke` 已在 `stage2/stage3` 真过。 |
| 新发现 | `multiaddr` 当前不能再拿 parser 路径当 stage2/stage3 主线。`v3Libp2pMultiaddrParse/ParseBytes` 一旦进入 `Result[Multiaddr]` 或更复杂的 parser 控制流，不是直接跳进 `__TEXT,__cstring`，就是回到 `primary_object_body_semantics_missing`。这不是网络语义错，而是 ordinary 对这类复合 `str/Bytes` 地址对象还没稳。 |
| 新发现 | 先用 `v3Libp2pMultiaddrFillTcp/FillQuic` 直接构造真实地址对象后，`libp2p_core_smoke`、`libp2p_host_smoke`、`libp2p_tcp_smoke` 已在 `stage2/stage3` 全绿；说明当前 `libp2p` 主线可以继续往下推进，不该再被 `multiaddr parser` 这条假根拖住。 |
| 新发现 | `libp2p_overlay_smoke` 现在暴露出来的才是下一层真 blocker：`byteBufView`、`v3HashInts`、`gossipsub`、`store_sync`、`host store/sync` 这一串还带大量 `composite-arg local missing / return_expr / stmt_var / stmt_for` ordinary 缺口。后续该继续打的是这条 overlay/store/sync 复合输出链，不是再回头折腾 `peer_id/core/host/tcp`。 |

| 项目 | 结论 |
|---|---|
| 新发现 | `v3` 当前真正拖慢字符串搜索的，不是 `str` 布局本身，而是 bridge 入口先把 `ptr+len` 退化回裸 `char*`，随后 `contains` 再走朴素 `for + memcmp`。这会把固定布局 `str` 的长度信息白白丢掉。 |
| 新发现 | 现在 `Two-way` 搜索核已经直接落到 `v3/runtime/native/v3_str_twoway_search.h`，`system_helpers.c`、`system_helpers_selflink_min_runtime.c`、`system_helpers_selflink_shim.c` 都统一接了同一套 bytes-aware 搜索逻辑；`driver_c_str_contains_str_bridge` 也改成优先吃 `str.len`。 |
| 新发现 | 这刀中途抓到一个真边角：`SIZE_MAX` 形式的 `maximal suffix = -1` 不能拿来直接做无符号大小比较，否则像 `"aaaaab"` 搜 `"aaab"` 这种 case 会漏报。现在已修成带 sentinel 语义的比较，native 穷举 smoke 全绿。 |
| 新发现 | `system_helpers_selflink_shim.c` 这轮顺手收掉了两个一直潜伏的前置声明缺口：`driver_c_str_eq_bridge` 和 `__cheng_rt_paramStrCopyBridge`。不补这两个，单独 fresh 编 `selflink_shim` 时会直接掉进隐式声明错误。 |

| 项目 | 结论 |
|---|---|
| 新发现 | `v3` 之前的源码 trace 其实只完成了一半：`signal` 链虽然能靠内嵌 line map 回到 `.cheng` 行号，但 `mapped_frames=0` 时就只剩原生 backtrace；`panic/bounds` 则还是主要靠 `.v3.map + symbol`。这还不是机器级真相源。 |
| 新发现 | 现在 `src/runtime/native/system_helpers.c` 已把这层补成真合同：`signal` 直接从 `ucontext` 取 `pc/fp/sp/lr`，沿 FP 链打印结构化 `m#` 机器帧；`panic/bounds` 走真实 `backtrace pc`，优先按内嵌 `PC range -> source span` 表映射，`.v3.map` 只退作 symbol fallback。 |
| 新发现 | 这刀最直接的收益已经在真 blocker 上出现了。`compiler_runtime_smoke` 以前只报 `source-signal mapped_frames=0`，现在会先打印 `[cheng-v3] machine-trace ...` 和原始 `m#0/m#1` 机器帧，把“真实崩在一个没映射到 Cheng 源码的机器地址上”这件事直接暴露出来，不用先上 `lldb` 才能知道第一现场。 |

| 项目 | 结论 |
|---|---|
| 新发现 | `src/runtime/native/system_helpers.c` 里这条 parser native bridge 之前确实有混合堆所有权 bug。`cheng_v3_parser_import_source_paths_bridge(...)` 和 `cheng_v3_parser_source_to_module_bridge(...)` 的正常返回大多是 `malloc`，但空串/错误分支却走 `driver_c_str_from_utf8_copy_bridge(...)`，把 `cheng_malloc` 指针也塞进同一个 `ChengStrBridge(OWNED)`。后面 `cheng_v3_bridge_release_owned(...)` 再统一释放时，就会在 generated exe 里撞到非法 `free()`。 |
| 新发现 | 把这几条 bridge 的空串/错误返回统一成 `cheng_v3_bridge_owned_str(...)` 之后，`compiler_pipeline_stub_smoke` 已重新真编真跑通过；说明这次崩点不是 parser 算法，而是桥接层自己把所有权做乱了。 |
| 新发现 | `panic`、`bounds`、`signal` 三条异常面现在都已经默认打印源码栈和 native 栈。真验收已经过了：`build_panic_trace_v3.sh`、`build_bounds_trace_v3.sh`、`build_signal_trace_v3.sh` 全绿，输出里都能直接回到 `.cheng` 行号。 |
| 新发现 | `lowering_plan_smoke` 现在虽然还会崩，但已经不再是“只看到异常名”。默认输出已经把第一现场钉在 `v3/src/backend/lowering_plan.cheng:75` 的 `v3LoweringFunctionSymbolText(...)`，后面继续修的是 lowering 逻辑，不是异常堆栈系统。 |

| 项目 | 结论 |
|---|---|
| 新发现 | `v3/src/backend/system_link_plan.cheng` 的 `v3SystemLinkPlanStubReport(...)` 这轮已经不再是活根了。把所有 `plan.xxx` 和派生文本先落局部变量后，`compiler_pipeline_stub_smoke` 已重新回到 `COMPILE_RC:0`；当前不该再回头怀疑 report 编译面。 |
| 新发现 | generated exe 上真正会把 parser 主链打坏的，不只是 seed 里的“复合参数不是 local”这一类 compile blocker，还包括 `v3` 源码里若干复合返回/嵌套字符串链本身。`v3PathSplitFile(...)` 一进 `v3ParseOrdinarySourceStub(...)` 就会把返回链炸掉；parser 内部在归一化源码路径上继续调用 `v3PathJoin(...)` 也会把 `ownerModulePath` 这条路径桥打坏。把这些点改成显式扫描和 `v3ParserJoinSlash(...)` 后，generated exe 才能继续往前跑。 |
| 新发现 | `compiler_pipeline_stub_smoke` 当前新的真根已经缩到 `v3ParserReadImportEdges(...)` 里处理 `std/...` 导入时再次调用 `v3ParserModulePathToSourcePath(...)` 的 `std` 分支返回链。顶层 `packageId`、`contractsPath`、`sourceExists` 都已经在 generated exe 里跑过，`v3ParseOrdinarySourceStub(...)` 也已稳定跑到 `parse5`；说明 parser 的 package/source 主路径已经比之前硬很多，剩下的是 `std` import 这条字符串返回链还没彻底闭合。 |

| 项目 | 结论 |
|---|---|
| 新发现 | `lowering_plan_smoke` 这轮已经不再卡在 `stmt_let/stmt_var/stmt_if` 这些 surface 级 ordinary 形状上了。把 `v3/src/backend/lowering_plan.cheng` 和 `v3/src/tests/lowering_plan_smoke.cheng` 再压平后，seed 现在暴露出来的真 blocker 已经前移到更底层的 `composite call-arg lowering`。 |
| 新发现 | `v3/bootstrap/cheng_v3_seed.c` 里的 `v3_prepare_expr_call_state(...)` 之前是实打实的栈爆根。它每层递归都在栈上摆十几块 `4KB/8KB` scratch，`lowering_plan` 这种长调用一进来就会在 Darwin 直接撞 `___chkstk_darwin`。现在已经改成每层递归独立堆分配 scratch，这条崩栈路径已消失。 |
| 新发现 | 当前 seed 的新主根不是 `lowering_plan` 自己，而是“复合参数表达式没有稳定物化成临时局部”。代表症状已经固定成 `scalar/composite call composite-arg local missing`，典型例子是 `cheng_v3_os_is_absolute_bridge(v3PathTrim(raw))`、`parser.v3ParseOrdinarySourceStub(req.rootDir, req.sourcePath)`、`parser.v3ParserSplitChar(v3path.v3ReadTextFile("", sourcePath), '\n')`。只要 `str/result/seq` 参数不是现成局部变量，emit 阶段就会掉坑。 |
| 新发现 | 这说明下一刀不该继续挤压 Cheng 源码外形了。真正该补的是 seed 统一的 call lowering：要么 prepare 阶段稳定生成 composite arg temp，要么 emit 阶段允许按类型直接物化复合参数，而不是假定它已经是现成 local。 |

| 项目 | 结论 |
|---|---|
| 新发现 | `RWAD` 原子级模型在 `v3` 里当前最短真落点不是先硬上 `accumulator/zk prover`，而是先把 `interval note + nullifier + deterministic root` 做成冷轨正式状态机。这样先把“1 个输入 note 切成 1 到 3 个输出 note、同一 note 不得重复花费、commit 必须给稳定摘要”这条硬语义钉死，再往上接成员证明和 batch proof 才不会空转。 |
| 新发现 | 真正能把“100 万 RWAD 转账不等于 100 万个对象”先落地的是“消费一个连续区间 note，再按花费窗口切出左找零、支付、右找零”这条规则。`v3/src/project/rwad_serial_state_machine.cheng` 现在已经把这条规则收成固定布局实现：`noteId` 保证 note 身份唯一，`spent nullifier` 负责双花拒绝，`ownerRoot/serialRoot/nullifierRoot/appHash` 负责冷轨摘要。 |
| 新发现 | 这轮没有把 `accumulator/zk` 写成已实现。文档和代码都已经明确：今天真落的是区间账本和确定性摘要，不是假证明；后续如果接 `accumulator/zk`，只能建立在这条 note/nullifier 状态机已经稳定闭合的前提上。 |
| 新发现 | 把 `rwad_serial_state_machine_smoke` 接进 `v3/tooling/run_v3_host_smokes.sh` 之后，整条 host smoke 现在已经能真跑到它并输出 `v3 rwad_serial_state_machine_smoke ok`；但后面的老 blocker 还在，而且这次真实失败栈已经落到 `v3PathTrim -> v3ParserFirstToken -> v3ParserReadImportEdges -> v3BuildSystemLinkPlanStub`，说明 `compiler_pipeline_stub_smoke` 这条线并没有彻底关掉，只是失败面更具体了。 |
| 新发现 | 原子级 `RWAD` 继续往冷轨接口推进时，真正该先补的不是字节编解码，而是“块内多 tx 顺序”。如果底层状态机只认单个 `height` 单调，就会把 `finalizeBlock` 偷偷退化成“每块最多一个 tx”。现在 `v3/src/project/rwad_serial_state_machine.cheng` 已把 `lastApplyOrderKey` 分出来，`height + txIndex` 能稳定派生确定性 order key。 |
| 新发现 | `v3/src/project/rwad_bft_state_machine.cheng` 现在已经把原子级 note/nullifier 账本包成了真正的冷轨适配层：`checkTx/finalizeBlock/queryOwner/queryAppHash` 都走固定布局对象，不再需要把原子账本塞回旧余额账本壳里。`rwad_bft_state_machine_smoke` 和 `build_rwad_bft_state_machine_v3.sh` 都已经 fresh 真编真跑通过。 |
| 新发现 | `RWAD-BFT` 这轮已经不只是 Cheng 内部对象接口，还补上了固定宽度二进制边界：`v3RwadBftEncodeTx/DecodeTx/checkTxBytes/finalizeBlockBytesSummary` 都已经由 fresh smoke 真跑通过。这样原子级账本已经能以二进制 tx 形式对齐冷轨接口，不再只是“对象 API 先能跑”。 |
| 新发现 | 把 `rwad_bft_state_machine_smoke` 接进 `run_v3_host_smokes.sh` 之后，整条 host smoke 当前已经稳定先跑过 `compiler_runtime -> rwad_serial -> rwad_bft -> parser_path -> compiler_pipeline_stub`，新的第一处真 blocker 已前移到 `lowering_plan_smoke`。而且这次真根不是 `RWAD`，是 seed ordinary body 子集还吃不下 `v3/src/backend/lowering_plan.cheng` 里的 `stmt_let / return_expr / composite call`。 |

| 项目 | 结论 |
|---|---|
| 新发现 | `type X = ref` 这条旧 blocker 已经不是活根了。`v3/bootstrap/cheng_v3_seed.c` 现在把 `ref` 和 `object` 一样视作 record header，所以 `v3/src/lang/intern.cheng`、`v3/src/ir/core_types.cheng` 这类 `= ref` 类型块已经能被 seed 正常读进来。 |

| 项目 | 结论 |
|---|---|
| 新发现 | `compiler_pipeline_stub_smoke` 这轮已经把 source 侧假根清干净了：测试主入口里的嵌套 `if + echo`、`parser` 的 token 扫描、`Result[V3ParsedSourceStub]` 和 `Result[V3SystemLinkPlanStub]` 的 source 形状都已经压成当前 ordinary 能吃的最小写法。 |
| 新发现 | 当前唯一剩下的真 blocker 已经收窄到 `cheng/v3/backend/system_link_plan::v3BuildSystemLinkPlanStub`。fresh 日志 `/tmp/compiler_pipeline_stub_smoke.debug.log` 现在不再出现 `v3ParserFirstToken(...)`、`v3ParseOrdinarySourceStub(...)`、`v3SystemLinkPlanStubReport(...)` 这批旧错误，只剩这一个 `system_link_plan` lowering 缺口。 |
| 新发现 | `system_link_plan` 这条线暴露的不是业务逻辑错误，而是 seed ordinary lowering 对“`Result[composite]` 里组 `plan`”这类 shape 仍不稳定。把 build-plan 读取改成标量 helper、把 `moduleKind/track` 逻辑拆平后，阻塞仍回落到同一函数，说明下一刀应该直接修 seed，而不是继续改 Cheng 源码外形。 |

| 项目 | 结论 |
|---|---|
| 新发现 | `parser_path_smoke` 这轮真正的主根不是 parser 逻辑，而是 ordinary host 对 `str` 的两条底层链都还带真 bug：一条是 `v3/bootstrap/cheng_v3_seed.c` 里 `str == str` 把 `str` slot 地址直接拿去比较；另一条是 composite-return call 和字符串字面量 scratch 共用同一层 `string_temp` 起点，导致短字符串 helper 很容易互相踩。 |
| 新发现 | `driver_c_str_slice_bridge(...)` 里用通用 `cheng_str_bridge_from_owned(...)` 包短字符串返回值并不稳；`"std/strutils" -> "strutils"` 这类短结果会把返回 bridge 的 `ptr/len` 搅坏。直接手工组 `ChengStrBridge` 后，这条 bridge 已稳定。 |
| 新发现 | `parser_path_smoke` 现在已经 fresh 真编真跑输出 `v3 parser_path_smoke ok`，说明 `v3` 自己的新 `path/parser` 子集已经闭合。继续往前跑整套 host smokes 后，新的第一处真 blocker 已前移到 `compiler_pipeline_stub_smoke`，当前不该再回头怀疑 parser/runtime bridge。 |

| 项目 | 结论 |
|---|---|
| 新发现 | `overlay_contracts_smoke` 已经 fresh 真编真跑通过，说明冷热双轨这轮新加的 `L0 overlay` 边界类型、角色约束、复合评分公式和 `host smoke` 接入口径已经打通。当前不需要再怀疑 `overlay contracts` 这条线。 |
| 新发现 | `BFT-SMI` 现在已经不是“编不过”或“provider object 炸了”，而是卡在真实运行断言 `block1 appliedCount`。这说明新的第一处真 blocker 已经前移到冷轨状态机本体。 |
| 新发现 | 当前 `BFT` 的真根不是账本规则本身，而是状态机把权威索引 `index` 包在 `V3BftStateMachine` 的嵌套复合字段里。`consensus.v3ConsensusApplyChecked(...)` 单独跑是好的，但一旦通过 `machine.index` 这类嵌套 `var` 路径去改，ordinary 产物就不稳定；下一步必须把冷轨状态收成更硬的 flat state / out-param 形状，不能继续依赖嵌套 record 更新。 |

| 项目 | 结论 |
|---|---|
| 新发现 | 冷热双轨这轮新增的 `overlay_contracts_smoke` 和 `bft_state_machine_smoke` 还没来得及暴露各自业务面的 ordinary 缺口，就先被同一个更前面的宿主壳错误拦住了：`src/runtime/native/system_helpers.c` 在 `cheng_v3_os_dir_exists_bridge(...)` 里调用了 `cheng_dir_exists(...)`，但文件顶部手写原型列表没同步声明，导致 fresh provider object 直接编译失败。 |
| 新发现 | 这个拦路点说明当前 `overlay/BFT` 两条新 smoke 还没有真正开始竞争资源或互相污染；它们共享的是同一层 runtime provider object。要继续定位 `BFT-SMI` 的复合输出缺口，必须先把这个宿主编译错误消掉。 |
| 新发现 | `LSMR` 文档里原先那段“LSMR 区块链共识”风险分析现在只能当历史设计归档，不能再和 `v3` 默认主线并排摆成现状。当前默认主线已经明确改成 `libp2p underlay + LSMR overlay + BFT-SMI cold track`，因此文档必须把旧共识叙述降格成背景材料。 |

| 项目 | 结论 |
|---|---|
| 新发现 | `v3BftStateMachineCheckTx(...)` 里 `var probe = machine` 这种写法在当前实现里只是浅拷贝；一旦沿着 `v3BftApplyTx -> v3ConsensusApplyChecked` 预演，底层切片就会把原 `machine` 一起污染。`CheckTx` 这层必须改成 clone-index 纯预演，不能再拿共享状态直接试跑。 |
| 新发现 | `Cheng <-> BFT` 这条新链已经暴露出一个比业务逻辑更底层的 ordinary 限制：fresh driver 对这层“复合 tx 结果输出面”还不稳。现在标量摘要能写回，但逐 tx 结果一旦继续走复合返回/复合输出 shape，就会在 fresh compile 或 fresh runtime 上暴露 ordinary lowering 缺口；后续要么继续把接口压进更硬的 out-param/fixed-layout 子集，要么直接补 lowering。 |
| 新发现 | `Cheng <-> BFT` 这层不能直接把 `chain_node` 暴露成外部共识接口。`chain_node/LSMR` 现在还带着网络拓扑、反熵和本地推进时钟；强一致性结算面应该只复用 `consensus` 里的账本规则，单独形成固定布局状态机。 |
| 新发现 | `BFT` 状态机里最该先钉死的不是具体 `Tendermint/CometBFT` 宿主细节，而是“块高和交易序如何派生确定性账本元数据”。现在这层已经改成由 `block height + tx index` 派生 `epoch/ganzhi/tick`，这样同一块输入在所有节点上都会落成同一批事件顺序和同一 `appHash`。 |
| 新发现 | `BFT` 边界的字节面必须先独立出来，否则后面很容易把内部 `record/str` 形态重新漏给外部共识壳。新增的 `V3BftTx` 已固定成 `32` 字节 wire format，`checkTxBytes/finalizeBlockBytes` 直接吃这层，不再让文本接口回流。 |

| 项目 | 结论 |
|---|---|
| 新发现 | `fixed_surface_smoke` 现在不该再作为唯一热核入口来读日志了。把它拆成 `fixed256_sha256_smoke`、`ref10_ashr_smoke`、`fixed256_curve25519_smoke` 之后，已经确认 `sha256` 固定布局热链和 `fixed_surface` 组合面都能 fresh 真编真跑通过。 |
| 新发现 | `curve25519` 这次真正的真根不是 `ref10` 算法文本本身，而是 ordinary codegen 把所有 `>>` 都错发成了 `lsrv`。`ref10_ashr_smoke` 第一条 `ashr64(-1, 1) == -1` 一开始就炸，说明 signed right shift 语义从后端就坏了。 |
| 新发现 | 正确修法落在 `v3/bootstrap/cheng_v3_seed.c`：`>>` 现在已经按 `signed/unsigned + i32/i64` 正式分流成 `asrv/lsrv`，并给 signed `i32` 路补了 `sxtw`。这刀落下后，`fixed256_curve25519_smoke` 打出来的错误公钥 `de2f913413e4be337fd0dd513b48c97184d5e5dedb0074478ed360dbd2181d06` 直接回正为 RFC 向量 `de9edb7d7b7dc1b4d35b61c2ece435373f8343c85b78674dadfc7e146f882b4f`。 |
| 新发现 | `src/std/crypto/ed25519/ref10.cheng` 里的 `ashr64/ashr32` 不能在没有单独 probe 的前提下乱改。把它们改成手搓算术右移，只会把更前面的 `sha256` 路也一起带坏；正确做法是先用 `ref10_ashr_smoke` 钉死机器码语义，再改后端，不直接手改算法层。 |
| 新发现 | `LSMR` 文档之前把很多安全边界写在大段愿景和风险分析里，但没有升成默认约束。现在 `v3/docs/LSMR.md` 和 `v3/docs/cheng语言特性矩阵和开发计划.md` 已明确写死：洛书前缀不是绝对地理、反熵不等于金融即时最终性、空间证明和 `CSG` 过滤不是完整抗作恶方案、`RWAD` 只认 `global finalized`，并把坏 proof / 冲突 proof / 超预算载荷 / 单区域失活切路这些要求抬进正式 gate 口径。 |

| 项目 | 结论 |
|---|---|
| 新发现 | `v3BuildBaguaPrefixTreeFill(...)` 之前的 `SIGSEGV` 真根不是 `LSMR` 算法，也不是 `state forest` 数据结构本身，而是 seed 对 `add(seq, complex rhs)` 的 lowering 错了：`cheng_seq_set_grow` 返回的新元素槽位没有先 spill，后面的右值求值把 `x0` 冲掉，最后把元素写到了地址 `0x2`。 |
| 新发现 | 正确修法不是改 tree 算法，而是在 `v3/bootstrap/cheng_v3_seed.c` 里把 `add(...)` 收成“先 spill 新槽位地址，再用更深一层 `call_depth` 计算右值，最后 reload 地址写回”。新增 `seq_add_member_index_rhs_smoke` 后，这条 bug 已被最小用例正式钉死。 |
| 新发现 | `bagua_prefix_tree_fill_smoke` 现在已经 fresh 真编真跑输出 `v3 bagua_prefix_tree_fill_smoke ok`。这说明 prefix-tree 构建的运行时段错已经闭合，当前不能再把旧 crash 继续归到 `LSMR` 算法上。 |
| 新发现 | `chain_node` 新阶段暴露出来的不是共识算法错，而是 `v3ChainNodeTransfer(...)` 里“原地逐字段拼 transfer event”这个 source shape 在当前 ordinary lowering 下不稳；同样语义一旦抽成 `v3ChainNodeBuildTransferEvent(...)` 再 apply，就能稳定通过 `chain_node_transfer_wrapper_forms_smoke`、`chain_node_smoke` 和正式 `chain_node_main` 自测。 |

| 项目 | 结论 |
|---|---|
| 新发现 | `lsmr_types_smoke` 这次真正缺的不是 `Result[V3PrefixRef] + [6]`，而是 ordinary lowering 里 `str/composite` 参与 `==` 时还被硬塞进 `i64` compare。把比较分支改成“先物化 `str`，再转 C string 走 `cheng_strcmp`”以后，`lt.v3PrefixRefText(Value(childRes)) == "6/1/5"` 已经能编过去。 |
| 新发现 | `str` 这条链还藏着一个更底层的真 bug：ordinary 生成的 compare 和 `out = out + ...` 拼接都把 `x15/x14` 当成跨 `bl` 还活着的地址寄存器在用。实际一旦进了 `cheng_str_param_to_cstring_compat / __cheng_str_concat / intToStr`，这些 caller-saved 寄存器就会被改写，随后直接把 `str` 地址写坏并在运行时 `SIGSEGV`。 |
| 新发现 | 正确修法不是给 smoke 改写法，而是在 seed 里把这两类路径统一收成“调用后重取地址”。现在 compare 分支会在每次字符串物化后重新取 scratch slot 地址，拼接分支也会先 spill `dest`、调用后再 reload，然后再落 `data/len/store_id/flags`。 |
| 新发现 | 这条修完以后，`lsmr_types_smoke` 已经 fresh 真编真跑输出 `v3 lsmr_types_smoke ok`。host smokes 的新第一处真实阻塞因此前移到 `fixed_surface_smoke`，当前暴露的是大 crypto ordinary 子集缺口：`if-expr` 标量化、`uint32/uint64` cast、若干复合类型 layout/alias 解析，以及反向 `for` range 还没接全。 |

| 项目 | 结论 |
|---|---|
| 新发现 | 现在顶层 `artifacts` 里只剩 fresh `v3_bootstrap`、`v3_backend_driver`、`v3_hostrun`、`v3_chain_node` 等 `v3` 产物；`rg -n 'cheng_v2|v2/' artifacts` 已为空，说明旧 `v2` 编译残渣已经清空。 |
| 新发现 | `v3` 里最后一条活的 `v2` 依赖其实就是 [run_v3_host_smokes.sh](/Users/lbcheng/cheng-lang/v3/tooling/run_v3_host_smokes.sh) 默认绑死 `v2/artifacts/bootstrap/cheng_v2c`。把它改成只认 `artifacts/v3_backend_driver/cheng` 并在缺 driver 时自动 fresh 构建后，`rg '\\bv2/' v3` 已经清零，`v2` 可以物理删除。 |
| 新发现 | 物理删除 `v2`、`artifacts`、`chengcache`、`.cheng-cache` 再 fresh 重建以后，`bootstrap_bridge_v3.sh` 和 `build_backend_driver_v3.sh` 都还能前台通过，说明当前 `v3` 自举根已经不再偷吃旧目录。 |
| 新发现 | clean rebuild 同时也把一批被旧产物遮住的真 bug 直接掀出来了。`fixed_surface_smoke` 现在诚实停在 `ordinary body semantics missing`，而 `bagua_prefix_tree_fill_smoke/chain_node` 则把真根收窄到 `v3BuildBaguaPrefixTreeFill(...)` 的 ordinary ABI/值表示问题。 |
| 新发现 | `v3SortStateCells(...)` 里的 `while j >= 0 && v3StateCellLess(...)` 不能假设 short-circuit 一定守住索引安全；当前 ordinary 产物会先读右侧，直接踩到 `cells[-1]`。拆成“先判 `j >= 0`，再单独比较”的两段式之后，这个越界读已经消失。 |
| 新发现 | `v3BuildBaguaPrefixTreeFill(...)` 里 `cells = cellsRaw` 这种 seq 头整体拷贝在当前 ordinary ABI 下是不稳的。改成逐元素复制后，prefix-tree 构建已经越过了最早那层坏 header 崩溃，说明后续 crash 才是真正剩下的前段活根。 |
| 新发现 | 这轮为了绕 clean rebuild 暴露出来的固定布局 ABI 噪音，`v3LsmrAddressFromSeed(...)` 已先收成纯 seed-bytes 的确定性寻址，`v3ChainNodeAccountAddress(...)` 也已收成整型确定性映射；这两条都只承担“稳定可复算地址”，不承担密码学语义。 |

| 项目 | 结论 |
|---|---|
| 新发现 | `FixedBytes32[]` 那个断言失败的真根，不在 `bytes_layout` 算法，而在 ordinary seed 的复合索引复制链。`v3_emit_index_access_address(...)` 之前把 `base` 写进了和外层 `dest_addr` 同一个 spill 槽，导致 `_copyMem` 最后不是把 `xs[0]` 拷到 `loaded`，而是拷回了序列头。 |
| 新发现 | 正确修法不是改 smoke，也不是改 `fixedBytes32Equal`，而是把 `dest_addr`、`base`、`index` 三者彻底分槽。现在 `v3/bootstrap/cheng_v3_seed.c` 已把 `seq[index]` 内部 spill 扩到独立地址槽和独立 index 槽，`fixedbytes32_seq_index_smoke` fresh 真编真跑已经输出 `v3 fixedbytes32_seq_index_smoke ok`。 |
| 新发现 | `v3/src/chain/lsmr.cheng` 之前真的缺了 `v3LsmrAddressBagua(...)` 顶层函数，几个 smoke 只是一直没 fresh 撞到而已。补回这个 helper 以后，`lsmr_locality_storage_smoke` 里那条 `unresolved module field module=v3/chain/lsmr field=v3LsmrAddressBagua` 已经消失。 |
| 新发现 | `v3LsmrLocalityHashText(...)` 之前只哈原始文本，和 smoke 期待的“大小写/空白规范化后再哈”不一致。现在收成 `splitWhitespace -> join(\" \") -> toLowerAscii` 后，`"Hangzhou West Lake"` 和 `"hangzhou west lake"` 已经能落到同一个 CID。 |
| 新发现 | `run_v3_host_smokes.sh` 不能继续复用旧输出路径上的旧产物，否则会把源码真缺口藏掉。现在脚本每个 smoke 开始前都会先删掉旧 `bin/bin.*/*.log`，host smoke 终于能诚实暴露新的第一处真 blocker。 |
| 新发现 | 继续 fresh 跑下去后，当前新的第一处普通编译真缺口已经很清楚：`lsmr_types_smoke` 不是 `lsmr` 模块丢字段，而是 ordinary lowering 还不会吃 `Result[V3PrefixRef]` 这种复合返回、非空列表字面量参数，以及 `lt.v3PrefixRefText(...) == \"6/1/5\"` 这类 `str` 复合返回比较。 |

## Findings

| 项目 | 结论 |
|---|---|
| 新发现 | 之前说 `bounds` 这条“源码栈已接通”只对 runtime 半边成立，对 ordinary compile 还不成立：`v3/bootstrap/cheng_v3_seed.c` 的 `v3_emit_index_access_address(...)` 在取 `[]` 地址时根本没发 `cheng_bounds_check`，所以越界会直接静默读错地址，连异常都没有。 |
| 新发现 | 正确修法不是只改 runtime 打印，而是两层一起收：runtime 侧把 `cheng_bounds_check` 收进统一异常栈入口，seed 侧则必须在 ordinary `[]` 地址计算里先做 `cheng_bounds_check(len, idx)`，再取元素地址。 |
| 新发现 | `[]` 越界检查一旦变成真实函数调用，`base/index` 寄存器就会被 clobber。真正稳的写法是：base 地址先 spill，索引表达式求完后 reload；发 `cheng_bounds_check` 前再把 `base/index` 各自 spill 一次，返回后再 reload，再继续做元素地址计算。 |
| 新发现 | `build_bounds_trace_v3.sh` 现在已经把这条链路钉死：fixture 会先验证 `xs[0] == 7` 仍正常，再故意读 `xs[1]`；前台输出已真包含 `[cheng] bounds check failed: idx=1 len=1`、`[cheng-crash-trace] reason=bounds`、`ordinary_bounds_trace_fixture.cheng:4-10` 和 native backtrace。 |

## Findings

| 项目 | 结论 |
|---|---|
| 新发现 | 这轮看起来像 seed 回归的 `consensus_*wrapper*_smoke`，真根不是 `precall + var index + 多复合参数` lowering，而是测试自己把 `accountIds.cap == 1` 写成了硬规则。 |
| 新发现 | `src/runtime/native/system_helpers.c` 里的 `cheng_seq_set_grow()` 首次扩容会把 `cap` 至少提到 `4`，这是 runtime 增长策略，不是 Cheng 语义。所以 gate 只能断言“`cap >= 1` 或 `cap >= len`”，不能把“第一次刚好等于 1”当 correctness。 |
| 新发现 | 新补的 `member_index_call_smoke` 和 `bytes_param_helper_smoke` 已经把这轮真正担心的两条线钉死：`member[index(call)]` 现在能稳定通过，`Bytes, Bytes` 复合参数 helper 也能稳定通过。当前没有证据表明 seed 在这两条表达式 lowering 上重新回退。 |
| 新发现 | 这类假红如果不及时收掉，会把后续人错误地引回 `v3/bootstrap/cheng_v3_seed.c`。正确做法是先核对 runtime 不变量，再决定是不是编译器 bug，避免围着实现细节做无意义 ABI 猜测。 |

## Findings

| 项目 | 结论 |
|---|---|
| 新发现 | `v3` 这轮的真根不是 `sha256` 算法，而是 seed 编译器自己的二元表达式 lowering。左值寄存器在右子树函数调用里会被冲掉，补成 spill 以后，又暴露出第二层真根：嵌套二元表达式和保存的 `x29/x30` 共用了栈顶区域，`getU32BE` 会把返回地址直接写成垃圾。 |
| 新发现 | 正确修法不是改 Cheng 算法源码，而是同时收两条硬规则：一是每层二元表达式都要把左值落到专用 spill，再算右子树；二是 spill 区和保存的 `x29/x30` 之间必须有独立 guard，不能共享 frame 顶部。 |
| 新发现 | 这两条修完以后，fresh `stage2/backend driver` 已重新打通 `sha256_core_smoke`、`get_u32be_smoke`、`sha256_distinct_smoke`、`consensus_init_smoke`、`consensus_transfer_apply_smoke`。说明当前 `v3` 已经不再卡在 `sha256/getU32BE/consensus transfer` 这条前缀链上。 |
| 新发现 | 还有一类表达式 lowering 没完全收口：像 `index.balances[v3ConsensusAccountPos(...)]` 和带用户自定义 `Bytes, Bytes` 复合参数的 helper，仍然比拆步写法更脆。当前最稳口径是先用已通过的 smoke 继续推进主链，再单独给这类“嵌套 index/call、多复合参数”补专门回归和 lowering 修复。 |

| 项目 | 结论 |
|---|---|
| 新发现 | 当前可用的 program runtime 还跑不了 `importc_fn`。不只是 `v3 host smoke`，连这轮拿来编 backend fixture 的现成 driver 路径也会在运行时直接报 `unsupported callee ... top=importc_fn`，所以 `@ffi_handle` 这边现在只能先收成 compile gate，不能伪装成 runtime gate。 |
| 新发现 | `tests/cheng/backend` 之前没有正式 `cheng-package.toml`，stage0 一走 source manifest 就会直接报 `missing package root`。给 fixture 目录补正式包根后，这批 backend fixture 才能稳定参与 system-link-exec 验收。 |
| 新发现 | linkerless signal 这层不能把 handler 安装绑死在 `.v3.map` 读成功上。embedded line-map 本来就不依赖 sidecar，所以 `cheng_v3_register_line_map_from_argv0` 必须先装 handler，再尽力加载 sidecar。 |
| 新发现 | host compiler 产物不会自带 `_cheng_v3_embedded_line_map_*`，但 runtime 仍然可能链接 `system_helpers.c`。正确口径不是让链接炸掉，而是在安全时机用 `dlsym` 缓存 getter；有就走源码栈，没有就安静跳过。 |
| 新发现 | Darwin 上不能把源码映射表放进 `__TEXT,__const`。只要内嵌表里有函数/字符串地址引用，`ld64` 就会直接报 `Illegal text-relocations`；正确段位必须是 `__DATA_CONST,__const`。 |
| 新发现 | linkerless dev 轨的 signal 源码栈现在已经能走严谨口径：seed 直接导出内嵌 line-map getter，runtime 的 `SA_SIGINFO` handler 只拿 `PC + frame pointer` 扫内嵌表，再用 `write(2,...)` 打印 Cheng 栈，不需要 `DWARF/dSYM`，也不碰 `backtrace/dladdr`。 |
| 新发现 | 做 signal smoke 时，不能依赖重定向 stdout 里的 `ready` 文本做同步。`puts` 在文件重定向下会被缓冲，脚本看不到即时输出；稳定做法是确认进程还活着、留一点时间让它进入 Cheng 循环，再外部注入 `SIGSEGV`。 |
| 新发现 | 当前 `v3` ordinary 子集还没把任意 `importc` 调用接成稳定验收面。最开始那条 `@importc -> SIGSEGV` 测试会卡在 `stmt_call/call target missing`，所以 signal 回归必须改成“不依赖 importc lowering”的外部注入路径。 |

| 项目 | 结论 |
|---|---|
| 新发现 | `v3/bootstrap/cheng_v3_seed.c` 这轮已经把函数级源码 span 收成真数据面：lowered function 现在会记录 `signature_line_number / body_first_line_number / body_last_line_number`，ordinary 成功链接后会在可执行旁边真写 `<out>.v3.map`。 |
| 新发现 | `system_helpers.c` 已经足够承接这条 dev 轨，不需要再发明新 provider 模块。普通 `v3` 可执行本来就会链接 `runtime/program_support_v3 -> system_helpers.c`，所以只要在 `v3_program_argv_native.c / v3_tooling_argv_native.c` 启动时注册 line-map，`panic/assert/bounds` 就能直接反查源码。 |
| 新发现 | `panic` 这层以前只是 `puts + _exit`，所以哪怕旁边有 sidecar 也完全不会用。现在已经统一改成走 `cheng_v3_panic_cstring_and_exit`，运行时会先打印消息，再打印函数级 source-trace；`ordinary_panic_fixture` 已真跑出 `ordinary_panic_fixture.cheng:2-3`。 |
| 新发现 | `signal` 这条线不能直接照搬同步 `panic` 方案；真正可落地的口径只能是“内嵌 line-map + async-safe handler”。现在这条安全口径已经补上，`SIGSEGV` 会直接回 `.cheng` 行号。 |

| 项目 | 结论 |
|---|---|
| 新发现 | `FFI handle` 这块仓库现状其实已经比旧文档更严格：运行时现在就是 `u64 = generation:32 | index:32`，解析时同时校验槽位和世代号，旧 handle 在槽位复用后会直接失配，不会再静默撞到新对象。 |
| 新发现 | 当前 debug 口径还没完全收正：Darwin 上已经有外部 `dSYM/DWARF` gate，但 runtime 自己只有 crash ring 和裸 `backtrace()`，还没有 linkerless dev 轨的源码行号反查表。所以正式规范必须收成双轨，而不是把其中一条说成能替代另一条。 |
| 新发现 | `LSMR` 旧文案把“洛书前缀”和“绝对地理位置”绑得过死，还把 `O(log_9 N)` 写成了无条件真理。正确边界是：地址前缀稳定，前缀内部路由靠延迟/负载/空间证明排序，复杂度只在桶上限成立时才成立。 |
| 新发现 | 仓库现有 `RWAD` 接口本身已经是“先导出 settlement batch，再由外部 finalize 回 ack”的分离结构，所以正式口径必须跟实现对齐：只有 finalized reserve 才能铸流通 RWAD、进入 NAV；待结算积分/claim 必须留在隔离账本。 |
| 新发现 | `UniMaker` 如果继续把商户目录价格直接绑成 RWAD 浮动计价，后面一定会把汇率风险偷偷塞进 escrow。正确做法只能是把 `quote_currency / settlement_asset / fx_lock_moment / risk_owner` 明写进 `Asset Manifest`。 |

| 项目 | 结论 |
|---|---|
| 新发现 | `v3` seed 这轮已经把“无注解 `let/var` 必须手写类型”这层假限制切掉了。`v3/bootstrap/cheng_v3_seed.c` 现在会从字符串字面量、标量字面量、top-level const、本地槽位、单参 cast、普通函数调用返回类型和首字母大写 record 构造里反推 `type_text/abi_class`，`V3AsmLocalSlot` 也开始显式记 `type_text`。 |
| 新发现 | 子代理把当前最小固定布局表收清楚了：`bool=1`、`ptr=8`、`str=24`、`T[]/ChengSeqHeader=16`、`Bytes=16`、`ErrorInfo=32`，`Result[T]` 按普通 record 对齐规则排。后面做复合 ABI 不用再猜活布局。 |
| 新发现 | 即使补了无注解绑定类型反推，`chain_node` 的首个 blocker 还是稳稳停在 `v3ChainNodeMainSelfTest` 的 `var server = node.v3ChainNodeInit("node-a", "chain-node-a")`。这说明当前真缺的已经不是“推不出类型”，而是“推出来了也还不会按固定布局搬运复合值”。 |
| 新发现 | `build_program_selfhost_v3.sh` 这轮继续通过，说明这次类型反推没有把已经跑通的 ordinary 标量子集打坏。下一刀可以直接上固定布局表和复合 ABI，不用担心先把 `program_selfhost` 弄回退。 |

| 项目 | 结论 |
|---|---|
| 新发现 | `v3` ordinary compile 这轮已经把一批会误导方向的假 blocker 清掉了。给 seed 接上 top-level `const` 收集和标量常量解析后，`chain_node` 的 unsupported 列表里已经不再包含 `cheng/v3/chain/binary_types::v3LsmrDigitOk` 和 `cheng/v3/chain/lsmr_types::v3BaguaValid`。这说明前面那批 `return_expr/if` 标量叶子本来就不是当前真根，只是 seed 之前认不出常量名。 |
| 新发现 | `rawmemAsVoid` 的重载身份还没真正做成签名键，但 name-only 错绑这颗雷已经先收住了。`v3/bootstrap/cheng_v3_seed.c` 现在只允许“单候选 name 解析”继续往下走，多候选直接不解析，至少不会再静默绑到第一条重载。 |
| 新发现 | 当前 `chain_node` 的首个阻塞没有再漂，仍稳定是 `cheng/v3/project/chain_node_main::v3ChainNodeMainSelfTest` 的 `stmt_var`：`var server = node.v3ChainNodeInit("node-a", "chain-node-a")`。这和子代理给出的最短链结论一致，说明下一刀不能再碰 `Mint/Transfer/Sync` 或 `fixedBytes32Equal` 这种后层逻辑，只该正面补 `复合 ABI + 无注解 let/var + 字段读写 + record/[]`。 |
| 新发现 | 当前最短执行顺序已经很明确：先补 `selftest -> v3ChainNodeInit -> v3ChainNodeZero / v3ChainNodeRebuildDerived` 这条链，不要再被 `LSMR`/`bagua` 里已经转绿的标量叶子带偏。 |

| 项目 | 结论 |
|---|---|
| 新发现 | `v3` ordinary compile 这轮已经真越过了 `primary_object_machine_words_missing` 和 `native link not implemented` 两层旧阻塞。`build_zero_exit_v3.sh` 现在能真发 `primary .o`、真编 provider `.o`、真链接并真跑返回码 `0`，说明 object/link 链路已接通。 |
| 新发现 | 普通 program 入口 ABI 不能导出 `_main`。`v3/runtime/native/v3_program_argv_native.c` 真正要找的是 `_cheng_v3_program_argv_entry`，所以 primary object 的 entry symbol 必须和 runtime provider bridge 对齐；这层一对齐，zero-exit 才能真链过。 |
| 新发现 | `program-selfhost` 和 `chain_node` 现在统一稳定报 `v3 compiler: primary object body semantics missing`。这说明后面的主根已经不是 argv、contract、report、provider 选择、`.o` 物化或 native link，而是 ordinary body 语义子集只覆盖了严格 `return_zero_i32`。 |
| 新发现 | 当前最短路已经变成“按子集扩 body lowering”，不是再补 object/link 壳。优先顺序应该是 `tail-call no-arg int32`，再到最小 `let/assert/echo`，最后才轮到 `program_selfhost` 和 `chain_node` 主体。 |

| 项目 | 结论 |
|---|---|
| 新发现 | `v3` ordinary compile 这轮先后暴露的三层假根已经全部切掉：最先是 seed 只认 `--in:<path>`，随后是 `system-link-exec` 把普通源码 `--in` 误当 bootstrap contract，最后是 report/closure 构建自己把自己炸掉。现在这条线已经只剩语义真阻塞，不再被参数、合同和内存布局噪声劫持。 |
| 新发现 | `v3/bootstrap/cheng_v3_seed.c` 必须把 ordinary runtime contract 和 bootstrap contract 分开。`print-contract/self-check/compile-bootstrap` 读 contract 文件是对的，但 `status/print-build-plan/system-link-exec` 必须只认内嵌 runtime contract 或显式 `--contract-in`；否则普通源码一进 `system-link-exec` 就会在第一行 `import std/system` 上炸成 “invalid bootstrap line”。 |
| 新发现 | `v3` 当前工具链不能只接受 `--flag:value`。仓库里现成的 `v3/tooling/*.sh` 和 `v2/bootstrap/Makefile` 都在用 `--flag value`，所以 seed CLI 必须同时接受空格、冒号和等号三种 flag 形式，否则 ordinary compile 永远死在 `argv` 解析层。 |
| 新发现 | `chain_node` 的 `rc=139` 不是链算法根错，而是 seed C 自己的两个实现 bug：一是 report 里把长列表继续塞进固定 `strcat` 缓冲区，二是 `v3_collect_source_closure` 每层递归都在栈上放 `V3PlanImportEdge[256]` 大 scratch。把 report 改成动态拼接、把大 scratch 改成 heap 后，`chain_node` 已稳定从段错误推进到 typed plan 阻塞。 |
| 新发现 | 现在 `program-selfhost` 和 `chain_node` 已经统一停在同一个真语义缺口：`runtime_targets_not_lowered` 和 `runtime_provider_modules_not_selected`。这说明 canonical backend driver、ordinary CLI、closure/report 内存问题都已经不再是主根。 |
| 新发现 | `run_v3_host_smokes.sh` 现在继续全绿，`run_slice_gate.sh` 也已经重新收敛成单一失败位：`program-selfhost`。后面该继续打的是 `runtime_targets/provider_modules` 的 typed lowering，不该再回头修 seed argv、contract loader 或 report builder。 |

| 项目 | 结论 |
|---|---|
| 新发现 | `v3` 这轮新增的 `ordinary compile` 面已经不再只是“报 not implemented”的空壳。`v3/src/lang/parser.cheng` 现在已经真实计算 `workspace_root/package_root/package_id/owner_modules/import_edges/closure_paths/entry_symbol/missing_reasons`，`v3/src/backend/system_link_plan.cheng` 也已经把这些事实收进 typed plan。 |
| 新发现 | `v3/src/tests/compiler_pipeline_stub_smoke.cheng` 之所以一开始反复炸，不是算法根错，而是三个执行面问题叠在一起：它先用假的 `/tmp/cheng-lang/...` 路径，随后又踩到 `std/strings.contains` 没落到当前 host runtime，最后再暴露 `strutil.join` 在这条 host 口径下会把大 report 和字符串数组拼接吞空。把路径改成真实 workspace root、把 `contains` 改成 `strutil.contains`、把 report/list 拼接改成手工串接后，这条 smoke 已稳定转绿。 |
| 新发现 | `run_v3_host_smokes.sh` 之前默认继承调用者 cwd，这会让要读真实源码路径的 smoke 漂。把脚本固定成先 `cd "$root"` 之后，`compiler_pipeline_stub_smoke` 这类需要真读 `v3/src/...` 的 ordinary plan smoke 才真正稳定。 |
| 新发现 | `hot-path forbidden scan` 这轮真抓到了我自己刚写回去的退化：`sourceKind: str`、`emitKind: str`、`parserSourceKind: str`。把它们改成枚举后，`scan_forbidden_hotpath.sh` 已重新通过，说明 `v3` 这条新 ordinary plan 没再把 `kind: str` 热壳带回来。 |
| 新发现 | `build_backend_driver_v3.sh` 以前最大的问题不是“构不出来”，而是“把 bootstrap 壳当 ordinary compiler 也算成功”。现在这层已经被收严：脚本会强制验证 built artifact 必须支持 `status` 和 `print-build-plan`，所以当前 gate 会更早、更准确地暴露真正主根：`artifacts/v3_backend_driver/cheng` 仍然只是 `cheng_v3_seed`，只会 `print-contract/self-check/compile-bootstrap`。 |
| 新发现 | `run_slice_gate.sh` 现在的最早真失败位已经从 `program-selfhost` 前移到 `canonical bootstrap compiler`。这不是退步，而是把假绿切掉后的真实状态：backend driver 还没从 `stage1_bootstrap.cheng` 切到 `compiler_main.cheng`，所以后面的 `system-link-exec` 根本不该被继续尝试。 |

| 项目 | 结论 |
|---|---|
| 新发现 | `artifacts/v3_backend_driver/cheng` 不是“还差几个子命令”的完整编译器，而是 `v3/bootstrap/cheng_v3_seed.c` 对 `v3/bootstrap/stage1_bootstrap.cheng` 再物化一次出来的 bootstrap 壳。它的源码和帮助面天然只支持 `print-contract / self-check / compile-bootstrap`。 |
| 新发现 | 当前 `v3` 真缺的不是 `--help` 文本，而是普通编译入口源码。`v3/bootstrap/stage1_bootstrap.cheng` 只是 key-value 合同，不是 `system-link-exec --root --in --emit exe --target --out` 这种 ordinary program 编译入口。 |
| 新发现 | `run_slice_gate.sh` 以前只看 `artifacts/v3_backend_driver/cheng --help` 会给假进展留口子；现在改成直接跑 `build_program_selfhost_v3.sh` 和 `build_chain_node_v3.sh`，失败点已经钉死在真实 ordinary program 编译命令上。 |
| 新发现 | `v3/src/project/chain_node_main.cheng` 已经落成 `chain_node` artifact 的源码入口，但 host compiler 直接编它还会死在 `program_entry_exec_plan_missing local_payload_label0=type`。这说明 `chain_node` 这条线除了 `stage2/stage3` 还不会编 ordinary program 之外，`project` 入口还暴露了一条额外执行面缺口。 |
| 新发现 | `v3/src/tooling/{compiler_main,compiler_runtime,compiler_request}.cheng` 已经构成真正的 compiler control-plane 骨架。host compiler 现已能真编 `compiler_main`，`help/status` 正常，`system-link-exec` 也会稳定吐出我们自己的 `ordinary pipeline not implemented`。 |
| 新发现 | 现在“分离没做完”的根因已经从源码层收窄到脚本和 artifact 合同层：`bootstrap_bridge_v3.sh` 和 `build_backend_driver_v3.sh` 之前还只围着 `stage1_bootstrap.cheng` 转；这轮之后 env/report 已经把 `planned_entry_source=*compiler_main.cheng` 和 `materialized_source=*stage1_bootstrap.cheng` 同时写出来，后面谁没切入口一眼就能看出来。 |
| 新发现 | `compiler_main` 下面的最小新主线现在已经存在：`v3/src/lang/parser.cheng` 先给 ordinary source 一个 typed parser stub，`v3/src/backend/system_link_exec.cheng` 再给 `system-link-exec` 一个 typed backend stub。后面往真 parser / 真 backend 扩，不用再拆 compiler 入口壳。 |

| 项目 | 结论 |
|---|---|
| 新发现 | `src/tooling/cheng_sidecar_rewrite_*.cheng` 这批随机名文件不是活源码，而是被脚本显式排除的 sidecar 改写残片。把已跟踪的 32 个文件和源码树里的未跟踪残留一起清掉后，`src/tooling` 顶层已经不再被它们污染。 |
| 新发现 | `src/tooling/cheng_tooling_embedded_stable.cheng` 和 `src/backend/tooling/backend_driver_uir_sidecar_direct_build.cheng` 当前都是零引用死文件。真正活着的入口还是 `cheng_tooling.cheng + embedded_part1..6`，以及 `backend_driver_proof / backend_driver_uir_sidecar_wrapper` 这条链。 |
| 新发现 | native `cheng_tooling` 之前只把 `probe_currentsrc_proof/cheng.stage2` 当 strict bootstrap driver，所以一旦 `cheng.stage2{,.proof}` 缺失，就会把现成活着的 `cheng_stage0_currentsrc.proof` 直接判死。正确做法是把 `currentsrc.proof.bootstrap` 作为严格 bootstrap bridge 接进 snapshot、fresh gate 和源码 launcher。 |
| 新发现 | `backend_driver_currentsrc_sidecar_wrapper.sh` 以前只认 published stage0 surface 或裸 binary 的 direct-export surface，不会从 `.proof.meta` 里追 `outer_driver`，也不会在 sidecar 子调用里自动保留 `BACKEND_UIR_SIDECAR_*` 合同。这就是 fresh gate 先卡“missing strict direct-export surface”，再卡“missing strict sidecar mode contract”的真根。 |
| 新发现 | 这轮把 wrapper 的 `.proof -> outer_driver` 追踪、bootstrap meta 继承、sidecar 子调用自动 preserve，以及 `verify_backend_sidecar_cheng_fresh.sh` 对 bootstrap sidecar compiler 合同的刷新一起补上之后，fresh gate 已经不再死在旧合同识别上，当前剩余阻塞已经收窄到 wrapper-source 真编译阶段。 |
| 新发现 | `v3/tooling/run_slice_gate.sh` 默认直接绑 `artifacts/tooling_cmd/cheng_tooling` 是错的，因为它很容易是旧 binary。现在默认入口切到 `src/tooling/cheng_tooling_embedded_scripts/cheng_tooling.sh` 才符合“在 v3 重建编译工具链”的目标。 |
| 新发现 | `v3/tooling/run_slice_gate.sh` 现在已经稳定先过 `scan + c_ref + frozen-vs-latest`，然后在 fresh bridge 这一步失败，错误与单独执行 `verify_backend_sidecar_cheng_fresh.sh` 完全一致：`strict bootstrap sidecar driver failed wrapper-source build`。这说明 `v3` gate 已经切到新的主链入口，没有再偷偷走回旧 binary。 |

| 项目 | 结论 |
|---|---|
| 新发现 | `src/std/system.cheng` 顶部那条 `import std/seqs` 在源码层是死边；删掉后，`system -> seqs -> system` 这个显式导入环已经从源码面消失。 |
| 新发现 | `tooling_stage0CapabilitySourceList()` 继续手写整串 `uir_core_builder*.cheng` 名单不稳，正确口径应该是“固定主链文件 + 运行时扫描 `src/backend/uir/uir_internal/uir_core_builder*.cheng` + 扫描失败时回落到静态名单”。 |
| 新发现 | `src/tooling/cheng_tooling.cheng` 默认不会把输出路径直接编成 native binary，而是给同机 `dev` 口径的 tooling main 发 launcher 壳；只有显式设 `TOOLING_EMIT_SELFHOST_LAUNCHER=0`，才会尝试直出 native。之前反复拿 launcher 当新 binary 验证，是假进展。 |
| 新发现 | 当前真正没闭合的，不只是 `build-backend-driver`，还有“如何把改过的 `cheng_tooling.cheng` 真编成当前 native binary”这条执行面。强制 native compile 现在仍在 `artifacts/backend_driver/cheng` 上 `rc=223`，而旧 canonical tooling/sidecar compiler 继续把旧 source-list 缺项带出来。 |

| 项目 | 结论 |
|---|---|
| 新发现 | `v3` 现在已经有冷路径字符串 interner 和固定 ID 的 `HIR/MIR/LIR` 骨架，`kind/opName/runtimeTarget: str` 不再只是口头说不能用，而是已经在 `v3/src/lang/intern.cheng` 和 `v3/src/ir/core_types.cheng` 里被固定成 `interned id + enum/layout/borrow facts`。 |
| 新发现 | `v3/tooling/scan_forbidden_hotpath.sh` 已真跑通过，说明当前 `v3/src` 里没有回流 `local_payload / exec_plan_payload / entry_bridge / payloadText / kind: str / BigInt` 这些已知热路径壳。 |
| 新发现 | `v3/tooling/run_slice_gate.sh` 已把 `scan -> 同机 C 基线 -> 冻结样本对拍 -> build-backend-driver -> v3 smokes` 收成一条真命令。当前它稳定卡在仓库现有 backend driver 全局故障：一边是 `uir_core_builder_*` source list 缺项，一边继续露出 `src/std/system.cheng -> src/std/seqs.cheng -> src/std/system.cheng` 导入环；这依然不是 `v3` 新代码单点错误。 |

| 项目 | 结论 |
|---|---|
| 新发现 | 当前 `v3` Cheng smoke 过不去，不是 `v3` 单点语义炸点。仓库现有 canonical backend driver 对现成 smoke 也同样全局 `rc=223`；继续执行 `build-backend-driver` 后，真正继续露出来的是已有 `src/std/system.cheng -> src/std/seqs.cheng -> src/std/system.cheng` 导入环。 |
| 新发现 | 当前 compiler 的 package import 路本身就会把仓库现成 fixture 打崩，所以 `v3` 内部导入先改成 `v3/src` 里的相对路径才是最短路。这不是降级，而是绕开现有仓库故障，保证 `v3` 代码仍然完整留在 `v3` 目录。 |
| 新发现 | `v3/bench/c_ref` 已经真跑出第一批同机 C 基线：`sha256=177.84ns/op`、`x25519_shared=18.93us/op`、`p256_pubkey=2.50us/op`、`p256_sign=16.49us/op`、`p256_verify=32.91us/op`、`chain_encode=234.10ns/op`、`chain_decode=224.16ns/op`。这批数值现在可以直接拿来卡后续 `stage2/stage3`。 |

| 项目 | 结论 |
|---|---|
| 新发现 | `array.elem_type_text / record.type_text` 这类 runtime shape 文本不该继续按实例 `dup/free`。它们本质上是长生命周期 shape 元数据，正确做法是全局 intern 成稳定指针，否则热路径每次建 `array/record` 都会白白烧分配和释放。 |
| 新发现 | `program-selfhost` 不能只证明 `chain_node balance/mint/balance`。真正该卡死的是 stage2 编译器能不能真编真跑多进程 `chain_node serve-once/sync-once`，所以 `chain_node_process_smoke` 必须进正式 gate。 |
| 新发现 | 这轮把 runtime `type_text` intern 和 `program-selfhost` 真实多进程 gate 一起收口后，完整前台 gate 已在 `manifest_fnv1a64=b55b66018e18ab44` 下重新稳定。当前真主根没有变，还是固定布局 `slot/shape` 和 aggregate `field/index update copy`。 |

| 项目 | 结论 |
|---|---|
| 新发现 | `fresh slot` 上的 aggregate 赋值不该再回走通用 `assign_slot`。在 `MAKE_ARRAY/MAKE_RECORD/array_push` 这种“槽位一定是新建”的路径里，直接把值准备好再落槽，能稳定切掉一层无意义的 `materialize + clone`。 |
| 新发现 | `nil slot` 上新建 `array/record` 之前一直在白白 clone 一个刚创建的空 aggregate。正确做法不是改 `assign_slot` 语义，而是把这类值显式标成 temp 所有权，再让 root 清标记落地。 |
| 新发现 | 这轮前台 3 次中位数已经更新到 `pubkey=1.1300s`、`sign=1.6300s`、`mul_xonly=1.3200s`、`kinv=0.7000s`；`maximum resident set size` 中位数约 `125MB/262MB`，`peak memory footprint` 中位数约 `116MB/253MB`。这说明白 clone 已不是主根，下一刀该继续打固定布局 `slot/shape` 和 aggregate field/index update copy。 |

| 项目 | 结论 |
|---|---|
| 新发现 | typed `record` 的 shared shape 必须覆盖所有构造路径，不只是 decl init。只要 `zero_value_from_plan/zero_record_shell_from_plan/MAKE_RECORD/clone` 里还有一条先按普通 record 分配 names 再切回 shared names，这条 shape 成本就还在热路径里白烧。 |
| 新发现 | 这轮把 shared-shape record 的构造和 clone 收成“只扩 `values`，直接复用共享 `field_names/field_lookup`”之后，`compiler_core_system_link_exec/program-selfhost/full-selfhost` 继续闭合，说明这条修法是 runtime 真收益，不是 probe 假象。 |
| 新发现 | 当前前台 3 次中位数已经更新到 `pubkey=1.1300s`、`sign=1.6500s`、`mul_xonly=1.3200s`、`kinv=0.7100s`；`maximum resident set size` 中位数约 `125MB/262MB`，`peak memory footprint` 中位数约 `116MB/253MB`。这说明 shared-shape 构造/clone 已不是主根，下一刀该继续打固定布局 `slot/shape` 和 aggregate field/index update copy。 |

| 项目 | 结论 |
|---|---|
| 新发现 | `field ordinal` 这条路必须在 source、stage0、runtime 三层一起落。只在 source 里加结构化字段但不改 stage0 lowering，最后还是会在 `addr_of_field/load_field/store_*field` 上退回字段名路径。 |
| 新发现 | `driver_c_prog_record_slot_at(...)` 这种 slot 级入口必须先成为 runtime 正式接口，后面才有资格继续打固定布局 `slot/shape`。否则编译器前面就算把 `field ordinal` 前移了，运行面还是会在 record 名称查找里把收益吃掉。 |
| 新发现 | runtime 里最先该吃掉的 record 名称查找，不是所有 `record_slot(name)`，而是“decl 顺序已经确定”的两条活路：`infer_type_arg_from_record_fields` 和 `make_record`。这两处已经改成按 ordinal 直达，下一刀才该继续往 `slot/shape` 本体推进。 |
| 新发现 | `driver_c_prog_assign_slot` 里“同一 array/record 又写回原 slot”这条路径之前会白走一次 aggregate 深拷贝。把它切掉后，语义不变，因为没有引入新别名，只是去掉了对同一对象的重复 clone。 |
| 新发现 | `driver_c_prog_clone_value_deep` 之前每次都会重建 `array cap` 和 `record lookup shape`。这不是语义必须成本，把 clone 改成保留 `array cap / record cap / record lookup_indices` 后，`sign/mul/kinv` 都继续小幅下降，说明这条 shape 重建确实是活根。 |
| 新发现 | `MAKE_RECORD` 的“字段名 -> decl 槽位”缓存不是当前最短路。它 correctness 绿，但 `mul/kinv` 回退，已经撤回；当前真根仍然是固定布局 slot/shape 和 aggregate field/index update copy。 |
| 新发现 | 把几处 `nil slot -> fresh aggregate` 初始化直接改成裸写 `*slot = fresh_value` 没有净收益，反而会把前台 3 次中位数拉回到大约 `pubkey=1.2000s`、`sign=1.7200s`、`mul_xonly=1.4000s`、`kinv=0.7400s`。这条试刀已经撤回，下一刀不再碰。 |
| 新发现 | 在当前 runtime 不变量下，`EPHEMERAL_AGGREGATE` 标记是 root 级事实，不需要递归清理整个子树。把 `driver_c_prog_value_clear_ephemeral_flags_deep` 收成 `driver_c_prog_value_clear_ephemeral_flag_root` 之后，`p256_fixed_core_probe`、`compiler-core-system-link-exec`、`program-selfhost`、`full-selfhost` 都继续闭合。 |
| 新发现 | 这条 root-only clear 是小幅真收益，不是回归也不是假收益。前台 3 次中位数当前是 `pubkey=1.1700s`、`sign=1.6800s`、`mul_xonly=1.3500s`、`kinv=0.7300s`；`maximum resident set size` 约 `125MB/262MB`，`peak memory footprint` 约 `116MB/254MB`。 |
| 新发现 | 当前 `program` 热层的主根已经继续收缩：不是 nested `ZERO_PLAN`，也不是 recursive flag clear，而是固定布局 slot/shape 和 aggregate update copy。下一刀该直接打这条，不该再回头犹豫 flag 清理。 |

| 项目 | 结论 |
|---|---|
| 新发现 | `driver_c_prog_clone_value_deep` 和 `driver_c_prog_value_clear_ephemeral_flags_deep` 之前会把 nested `ZERO_PLAN` 提前整棵物化成真实 aggregate；这不是语义必须成本，而是 program 热路径里的值表示噪音。把它改成“只解 `REF`，保留 nested `ZERO_PLAN`”之后，`p256_fixed_core_probe`、`program-selfhost`、`full-selfhost` 都继续闭合。 |
| 新发现 | 这刀的收益主要落在值表示和内存口径，不是秒数大跳水。前台 3 次中位数当前是 `pubkey=1.1900s`、`sign=1.7100s`、`mul_xonly=1.3600s`、`kinv=0.7300s`；`maximum resident set size` 约 `125MB/262MB`，`peak memory footprint` 约 `116MB/254MB`。这说明 nested `ZERO_PLAN` eager materialize 已经不是当前主根。 |
| 新发现 | 子代理建议过“临时 aggregate 直接接管 slot、只清 root flag”，这条不能进生产路径。因为嵌套 child 还可能保留 `EPHEMERAL_AGGREGATE` 标记，后续一旦被 field/index 读出来再赋值，就会制造真实别名。下一刀必须继续走固定布局 slot/shape 和 aggregate update copy，不走这条错路。 |

| 项目 | 结论 |
|---|---|
| 新发现 | 把 typed `param/local_decl` 默认零值收成 lazy `ZERO_PLAN` 是真收益，不只是小修补。它把 eager 零值物化从热路径里拿掉后，前台 3 次中位数已经继续压到 `pubkey=0.9900s`、`sign=1.4100s`，单次 RSS 仍稳定在 `148MB/332MB` 左右。 |
| 新发现 | `ZERO_PLAN` 路一旦先物化成临时 aggregate 但不写回 slot，nested field store 就会静默丢写入。这轮真出问题的不是密码学，而是 `Result.err.msg` 被写到临时 record 上，所以 `lsmr_advanced_features_smoke` 会直接 `fail` 且 error text 为空。slot 级原地 materialize 才是对的修法。 |
| 新发现 | 当前 `program` 热层的下一处真根已经继续收缩：不是 selfhost，不是 `ZERO_PLAN` 基本语义，而是 aggregate field update 剩下的物化/拷贝。下一刀该继续打这条，不该回头试已证伪的 `TrustedInto` 直连。 |

| 项目 | 结论 |
|---|---|
| 新发现 | `program` 热路径里“值表示本身”现在已经被前台钉死是主收益，不只是附属优化。把小 `array/record` 直接内联到 `DriverCProgArray/DriverCProgRecord` 后，`pubkey/sign` 中位数已经进一步压到 `1.0700s/1.5300s`，而且单次前台 RSS 也稳定在 `148MB/332MB` 左右。 |
| 新发现 | `LOCAL_DECL` 之前每次都会重跑 `zero_value_from_type -> type text 归一化 -> zero plan`，这不是语义必要成本。把 `param/local zero plans` 预解码进 `DriverCProgFrame` 后，这条动态查找链已经不再处在每次局部变量初始化的热路径里。 |
| 新发现 | 当前 program 热层的下一处真根已经继续收缩：不是 frame 泄露，不是小 aggregate 堆分配，而是“固定布局零值原型 + 剩余物化/拷贝”。下一刀该继续打这条，不该回头再试已证伪的 `TrustedInto` 直连。 |

| 项目 | 结论 |
|---|---|
| 新发现 | `program` 轨最近这一处最重的内存根不是链算法，也不是 `TLS`，而是 `driver_c_prog_eval_item(...)` 每次调用都会新分配 `params/locals/stack/labels/loops`，但退出时不释放。这个根已经切掉。 |
| 新发现 | 把小帧直接内联进 `DriverCProgFrame` 是真收益，不是微调：`pubkey/sign` 峰值 RSS 已从 `1.2GB/2.0GB` 级别直接掉到 `147.9MB/331.6MB` 左右，同时前台 3 次中位数继续压到 `1.2500s/1.7700s`。 |
| 新发现 | 当前 `program` 热路径的下一处活根已经不是 frame 泄露，而是 `zero_value_from_type/local_decl` 的固定布局原型化和值物化；下一刀该打这条，不该回头继续试已证伪的 `TrustedInto` 直连。 |

| 项目 | 结论 |
|---|---|
| 新发现 | `pointJacobianDoubleFixedInPlace/pointJacobianAddAffineFixedInPlace` 里把 `double/triple/quad/eight` 收成 `TrustedInto` 虽然 correctness 继续全绿，但 `pubkey/sign` 单次前台样本回到 `1.50s/2.22s`，比稳定基线更差，所以这条路不能保留。 |
| 新发现 | 现在最快的路不是整仓重建，也不是继续补丁式乱试，而是保留已闭合的 `full-selfhost / LSMR / chain_node / comb6 correctness`，只重建 `program` 执行热层和值表示、纯 Cheng 热核接口。 |
| 新发现 | 进度记录口径已经改成“只写开始时间，完成时间由用户统计真实时间”；历史条目不追改，后续新条目统一按这套规则写。 |

| 项目 | 结论 |
|---|---|
| 新发现 | 如果目标是“自举完成时就不低于 C”，那性能不能被当成后端补丁，而必须从第一天写进语言语义、值表示、IR、标准库和验收标准。先做 selfhost 再补优化，最后一定会落成“能编自己，但用户程序很慢”。 |
| 新发现 | 当前仓库最该坚持的顺序仍然是：先把普通程序热路径和纯 Cheng 热核压强，再继续收缩 seed/C 壳；“先完全零 C 自举”不是当前最短路。 |

| 项目 | 结论 |
|---|---|
| 新发现 | `comb6` correctness 现在继续稳定，不是活根。真正还能继续降秒数的最短路，仍然是 `pointJacobianDoubleFixedInPlace/pointJacobianAddAffineFixedInPlace` 内部 field 运算的值流。 |
| 新发现 | 这轮新增的真收益不是再改上层 `ecnist` 语义，而是把 `p256_fixed` 低层 fake `Into` 收成真原地输出，直接砍掉 `mod add/sub` 和条件减模里的整块 `P256Fixed` 回拷贝。 |
| 新发现 | 这条路已经前台钉死为安全：`_tmp_p256_mul_double_cmp_probe_bin=ok`、`_tmp_p256_mul_add_cmp_probe_bin=ok`、`_tmp_p256_comb_cmp_probe_bin=ok`、`_tmp_p256_repr_sign_r_cmp_probe_bin=repr_r_eq=1`，完整主 gate 也通过。 |
| 新发现 | 这一刀后当前 3 次前台中位数已更新到：`pubkey=1.2830s`、`sign=1.8210s`、`mul=1.4628s`、`kinv=0.7521s`。按同机 C `1:1` 算，当前仍慢约 `52581x` 和 `152885x`，比上轮 `1.3159s/1.8989s` 继续小幅下降。 |
| 新发现 | 现在下一刀还是不能回头重查 correctness，也不能再做整段 trusted 直连；最短路继续是 active `comb6` 主循环里的非自乘乘法和值流，优先 `pointJacobianAddAffineFixedInPlace`。 |

| 项目 | 结论 |
|---|---|
| 新发现 | 之前“`comb6 == Python`”的结论是旧二进制假阳性。重新前台编译后，`wnaf == Python` 仍成立，但 `comb6` 继续不等。 |
| 新发现 | `comb6` 的表常量和索引本身不是根。`_tmp_p256_comb_table_entry_probe_bin` 证明 `p256GComb6[1/2/3]` 和可变索引读取都正确；`_tmp_p256_comb_digit_lookup_probe_bin` 也证明“现算 `digit` 再读 `p256GComb6[digit]`”在小函数里是对的。 |
| 新发现 | 真正坏的是完整函数形状。`_tmp_p256_comb_two_bit_probe_bin` 稳定显示：只要 `digit` 的最低位参与，完整 `comb6` 路就会把它吃掉，表现成 `1 -> 0`、`3 -> 2`；但 `_tmp_p256_comb_first_digit_probe_bin` 又证明同样的 `find first nonzero row -> setFromAffine -> toAffine` 在小函数里完全正确。 |
| 新发现 | 这说明当前 `comb6` 的真根已经不是数学、不是表、不是索引，而是 `ecnist` 里这段完整函数形状在当前代码生成下被打坏了。下一刀应该查函数形状/值流/codegen，不该再碰表常量和算法权重。 |
| 新发现 | 当前不能把 `comb6` 接回 `publicKey` 或签名 active 路；这轮试接后的生产改动已全部撤回，稳定路径仍是 `wnaf`。 |

| 项目 | 结论 |
|---|---|
| 新发现 | `_tmp_p256_jacobian_valueflow_probe_bin` 现在已经全绿，旧的 imported/local fixed-record `local_copy/local_return` 老根已经过期，不能再把它当当前性能主根。 |
| 新发现 | `comb6` 不是整体都能直接接回 active 路径。`_tmp_p256_comb_cmp_probe_bin` 当前仍报 `neq`，`_tmp_p256_repr_sign_r_cmp_probe_bin` 也继续不等价；所以完整 affine 点乘和现有 `wnaf` 还没完全对齐。 |
| 新发现 | `_tmp_p256_sign_r_comb_cmp_probe_bin` 的 `ok` 只能说明 `x-only` 局部路径曾经对拍通过，不能直接推出整条签名 active 路就安全；上一轮临时 active 接线已经撤回。 |
| 新发现 | 回滚 unsafe `comb6` active 接线后，安全前台基线重新量准为 `pubkey=4.47s`、`sign=6.98s`、`mul_xonly=3.67s`、`kinv=0.95s`。按同机 C `1:1` 算，当前仍分别慢约 `183194x` 和 `586018x`。 |
| 新发现 | 现在最短路已经很明确：值流不是根，`comb6` 完整 affine correctness 才是根。只有把这条收掉，`publicKey` 和签名的固定基点热路径才有资格安全切过去。 |

| 项目 | 结论 |
|---|---|
| 新发现 | `msquicNativeDial` 之前报 `unsupported callee` 不是 runtime 分发错，而是函数本体掉成了 `outline/local_payload`。真根是 `native_runtime.cheng` 里新加的 `if-expr` body shape 踢出了当前 program exec 子集；改回显式 `var + if` 后，同一个函数已经重新变成 `compiler_fn_exec/exec_plan_payload`。 |
| 新发现 | `chain frame: short read` 的真根不是链帧编码错，而是边界错：`chainReadExact` 假定底层是阻塞流读取，但 `Connection/msquicNativePipeRead` 实际给的是“现在手上有多少就立刻返回多少”的非阻塞 app recv。把 pipe 读收成带缓存的流语义，并在 native pipe read 上按 client/server side 真阻塞等 UDP datagram 后，真实多进程 `serve-once/sync-once` 已经稳定得到 `synced=1` 和 `balance=11`。 |
| 新发现 | 真正的生产联网证明应该是多进程 `chain_node_process_smoke`，不是单进程 `msquic_chain_smoke`。前者跨进程 `listener/dial/accept` 已闭合；后者把 listener 和 client 塞进同一进程共享全局 session，本质上是 synthetic harness，不该继续当主合同。 |
| 新发现 | `msquic_chain_smoke` 之前最后两条真根不是链算法，也不是 TLS 数学：一条是 `FrameData` 长度只按裸 payload 估算，另一条是 app stream 收包被错误当成“必须严格连续 append”。把真实编码长度、精确切片和 `offset` 重组三条一起收掉后，双节点 smoke 才真正穿过去。 |
| 新发现 | 现在 `LSMR.md` 里提到的新技术不只是“算法层完成”，而是已经有真实双节点联网 smoke。最小闭环口径应该改成 `lsmr prefix/locality/advanced + state_tree_sync + msquic_chain_smoke` 五条，不该再写“真实 QUIC 联网仍部分完成”。 |
| 新发现 | `LSMR.md` 里没真正落地的主干不是前缀树本身，而是“正文只有内容 CID、没有局部敏感双重寻址”和“只有单棵状态树、没有边缘/区域/全局三层快照”。这两块现在已经补上。 |
| 新发现 | `LocalityCid` 不能继续依赖当前普通程序运行面上的字符串归一化 helper；那条路会把归一化文本打空。稳定做法是直接在 `lsmr.cheng` 里用有界 `int64` token rolling hash 做最小签名，再把签名文本哈成固定 `localityHash`。 |
| 新发现 | 三层状态模型当前最稳的第一刀不是直接把共识/反熵全改成多层同步，而是先让同一批 state cell 同时投影出 `edge/regional/global` 三棵前缀树，并导出 `localOpCount/regionalBatchCount/globalAnchorCount`。这样链算法层先闭合，节点层后续再接。 |
| 新发现 | `lsmr_locality_storage_smoke` 已经证明这条路闭了：`token` 顺序不同的同义正文会得到同一个 `localityHash`，不同语义集会分开，`ChainIndex` 也能稳定派生三层状态快照。 |
| 新发现 | `comb6` 算法本身不是当前根。`_tmp_p256_generator_comb6_step_probe_bin` 前台继续 `ok`，说明手工内联的 `double/add` 路是通的。 |
| 新发现 | 真正坏的是 `program` 轨里一条更窄的值流：在非 owner 模块里，直接接 imported 函数返回的 `EcPointJacobianFixed` 会把结果打成 `inf=1 z=[0..]`。`_tmp_p256_jacobian_valueflow_probe_bin` 已把边界量准：`inline/local_nocopy/local_return_nocopy/imported/imported_return` 全部通过；只有 `local_copy` 和 `local_return` 失败。 |
| 新发现 | 这说明根既不是 `comb` 数学，也不是 return 本身；更像是“非 owner 模块里，对 imported record-return 做本地赋值”这条语义/代码生成有问题。下一刀该查 `program/runtime` 的 record-return/assign 链，不该继续碰 `P-256` 数学热核。 |
| 新发现 | 之前 active 路径里的 `pointJacobianDoubleFixedInPlace/pointJacobianAddAffineFixedInPlace` 并不等价于纯函数实现。用生成元 `G` 做逐步 affine 对拍后，`double` 和 `addAffine` 都在第 1 步就能复现偏差；现在已经收回到和纯函数同构的实现，`_tmp_p256_mul_double_cmp_probe`、`_tmp_p256_mul_add_cmp_probe` 都已前台 `ok`。 |
| 新发现 | 因为上面这两个 in-place 热核之前是错的，旧的 `pubkey=7.18s/sign=9.41s` 基线不再可信，已经作废。当前 correctness 回收后的真基线是：`pubkey=7.84s`、`sign=10.29s`、`mul_xonly=7.65s`、`kinv=2.65s`。 |
| 新发现 | 新增的子路径 probe 证明下一刀不该先碰 `kinv`：`_tmp_p256_mul_double_probe_bin` 前台真跑 `1.14s / 16 doubles`，`_tmp_p256_mul_add_probe_bin` 前台真跑 `16.54s / 64 adds`，折算后 `addAffine` 单步约比 `double` 慢 `3.63x`。主根现在更明确地落在 `pointJacobianAddAffineFixedInPlace`。 |
| 新发现 | `pointJacobianDoubleFixedInPlace/pointJacobianAddAffineFixedInPlace` 里真正还活着的一层大开销，是每次 field `mul/square` 都在 helper 内部新建一份 `P256FixedMontgomeryScratch`；把 scratch 提到调用层复用后，常规 `pubkey` 中位数从 `7.38s` 压到 `7.18s`，`mul_xonly` 中位数从 `7.46s` 压到 `7.32s`，`kinv` 也顺带从 `2.10s` 掉到 `2.04s`。 |
| 新发现 | 把 `ecdsaSign` 的 `r/rd/sum/kinv/s` 强行改成固定宽度链不是当前最短路；这刀不仅回退，而且 `Result[pfix.P256Fixed]` 还会撞普通 program runtime 的 imported fixed record 边界，所以已经整块撤回。 |
| 新发现 | 当前 3 次前台中位数已经重新量准：`sign=9.41s`、`pubkey=7.18s`、`mul_xonly=7.32s`、`kinv=2.04s`。按同机 C `1:1` 算，当前仍分别慢约 `790033x` 和 `294259x`；主根继续是生成元点乘，份额约 `77.79%`，`kinv` 约 `21.68%`。 |
| 新发现 | 之前那层 `...Into()` 只是名字叫 `Into`，内部还是“先返回整块 `P256Fixed` 再赋值”；把它收成真正的原地输出后，常规 `pubkey` 中位数直接从 `15.8525s` 压到 `7.38s`，整签名从 `18.6038s` 压到 `9.54s`。这说明热路径的大头还真是 record 返回值流，不是 `Montgomery` 算法方向错了。 |
| 新发现 | 这轮复测后，`mul_xonly=7.46s`、`kinv=2.10s`，按当前 `sign=9.54s` 算，生成元点乘约占 `78.20%`，`kinv` 约占 `22.01%`。主根仍然是 `mul_xonly`，但 `kinv` 已经不再小到能长期忽略。 |
| 新发现 | 当前最短路仍然不是更大窗口表，也不是重新折腾 `kinv`。先继续把 `pointJacobianDoubleFixed/pointJacobianAddAffineFixed` 的非自乘乘法和值拷贝压下去；等 `mul_xonly` 再掉一刀，再重新量一次 `kinv` 占比。 |
| 新发现 | `epochTimeNs()` 不能再拿来给 `20s` 级热段做分段计时；它会产出假负值。正确口径是前台外部计时，把 `mul/kinv` 拆成独立可执行探针分别量。 |
| 新发现 | 当前整签名的真实占比已经量准：`mul_xonly=16.2622s`，`kinv=2.3078s`，按当前 `sign=18.6038s` 计算，生成元点乘约占 `87.41%`，`kinv` 只占 `12.40%`。下一刀再碰 `kinv` 就是在偏离主根。 |
| 新发现 | `Result[pfix.P256Fixed]` 这类 imported fixed record 一旦跨函数边界，就会撞当前普通程序运行面的 `unresolved module field module=std/crypto/p256_fixed field=P256Fixed`。正确修法不是放宽 runtime，而是把固定宽度值留在函数体内部，跨边界只传 `BigInt` 或本模块自有记录。 |
| 新发现 | 把签名主链改成 `Jacobian -> x-only -> 固定宽度单次减 n` 后，常规 `sign` 中位数已经从 `19.0723s` 压到 `18.6038s`；这说明“只拿 `x`、不建完整点、不过通用 `BigInt mod`”这条路是有效的。 |
| 新发现 | 这刀对 `pubkey` 没有副作用，常规中位数继续稳定到 `15.8525s`。当前真正该继续压的只剩 `pointMulGeneratorJacobianWnafFixed` 里的 `double/add` 非自乘乘法和值拷贝。 |
| 新发现 | 固定宽度 `modmul/modexp` 也切进 trusted 无 `Result` 路之后，`pubkey/sign` 的中位数还会继续掉，说明逆元和标量模乘主循环也一直在为对象层和错误通用性付成本。 |
| 新发现 | 这条收益比单纯改 `double/add` 更稳定：`pubkey` 三次前台样本压到 `21.7294s/15.5675s/15.9608s`，`sign` 三次样本压到 `19.0723s/18.0080s/24.0553s`，比上轮 active 基线继续下降。 |
| 新发现 | 现在最该做的不是再堆窗口表，而是把 `sign stage probe` 改成真分段计时；只有先把 `mul/kinv` 的真实占比量出来，后面才不会把力气花错地方。 |
| 新发现 | 继续把 `double/add` 主链往 `trusted p256_fixed` 内核里收是对的。只把 `pointJacobianDoubleFixedInPlace/pointJacobianAddAffineFixedInPlace` 切到无 `Result` 的 trusted 固定域算子后，常规 `pubkey` 已压到 `16.8481s/19.2838s`，整签名已压到 `19.8186s/21.5582s`。 |
| 新发现 | 这说明当前主要成本还真在热路径里的 `Result` 盒子、helper 层和 record 返回值流，不在 `Montgomery/square` 大方向本身。 |
| 新发现 | 现在剩下的真根又缩小了一层：不是初始化，不是自乘，不是窗口表，而是 `pointJacobianDoubleFixed/pointJacobianAddAffineFixed` 里剩下的非自乘乘法和值复制。下一刀该继续压这两条，不该再回头碰逆元、签名收尾和更大窗口。 |
| 新发现 | 给固定域热算子再包一层 `Ref helper` 是错路。它把 `pubkey` 拉回 `23.4243s`、把整签名拉回 `27.6701s`，说明当前 Cheng 执行面对“多一层小函数”比对 record 结果做一次原地写回更敏感。 |
| 新发现 | 真正该保留的是“原地写回 result”，不是“再造 helper 层”。只保留 `pointJacobianDoubleFixedInPlace/pointJacobianAddAffineFixedInPlace` 后，常规 `pubkey` 压到 `21.0261s`，整签名压到 `25.1703s`。 |
| 新发现 | 这说明下一刀不能继续在 API 皮层加薄封装，而该直接把固定域热算子收成 trusted 内核，减少 `Result` 和 helper 调用层，而不是再试图从参数传法上抠小利。 |
| 新发现 | 固定逆元加法链不是当前最短路。它虽然能跑通，但会把常规 `pubkey` 拉回 `23s`、把整签名拉回 `27s`，比稳定基线更差，所以不能留在 active 路径。 |
| 新发现 | `x-only` 签名收尾也不是当前最短路。直接返回 `Result[pfix.P256Fixed]` 会撞上 `program runtime` 的 imported fixed record 返回面；就算包回本地 `EcPointFixed`，净收益也不成立。 |
| 新发现 | 当前最稳的 active 基线是：常规 generator `pubkey=21.0261s`，整签名 `25.1703s`，`stage=mul -> stage=r` 仍是最大热段。所以下一刀必须继续打 `pointJacobianDoubleFixed/pointJacobianAddAffineFixed` 的非自乘乘法，不该再碰逆元和坐标收尾。 |
| 真根因 | `compiler_core` 的名字解析还在用 `same source -> global scan` 这条错路，未按模块可见域解析 |
| 新发现 | `EcPointFixed/EcPointJacobianFixed` 之前最大的结构性浪费不是 `wNAF` 本身，而是每一次 `p256FieldFixedMul(...)` 都在做“普通域 -> Montgomery 域 -> 乘 -> 普通域”的往返转换；把坐标改成常驻 `Montgomery` 域后，`pubkey` probe 直接从 `8.61s` 掉到 `7.55s` |
| 新发现 | 当前 `ecdsaSign` 的 20 秒内剩余热根仍然只在 `stage=mul`；`priv/hash/nmod_hash/detk` 都已经快速通过，所以继续抠 `deterministicK` 或 `nModInv` 不是最短路 |
| 新发现 | 这轮两条看起来更激进的路都证伪了：`4-bit fixed window generator` 更慢，`comb generator` 虽然 double 更少，但运行时预计算把初始化直接拖爆。说明当前最短路不是换生成元算法，而是继续压 `pointJacobianDoubleFixed/pointJacobianAddAffineFixed` 这条固定点主链本身 |
| 新发现 | 对当前 Cheng 普通程序执行面，热点里层层 `Result[T]` 包装本身就是明显成本。把固定点 `double/add/mixed-add` 改成内部 crash 版本后，结构更对，也能直接减掉热路径上的分支和对象流量 |
| 新发现 | 纯 Cheng 数值热路径当前最坏点不是单独 `bigMul`，而是 `bigModMul/bigModExp` 还在走“先乘再位串行取模”；这对 `P-256` 和 `RSA` 都是同一个坏核 |
| 新发现 | `Montgomery` 是对的最短路：保留 `BigInt` 外形，直接补 `n0Inv + r2 + oneMont + limb级 Montgomery mul`，就能同时替换 `ECDSA`、`RSA` 的核心模乘/模幂路径 |
| 新发现 | 当前普通 program runtime 对 `T[N]` 本地 scratch 还不够稳：它只认字面量或顶层 `const int32` 的长度，复杂长度表达式会误回退到 `type item` 查找，炸出误导性的 `missing type item for zero init type=int32` |
| 新发现 | 仓库里当前真正稳的 fixed-layout 形态仍是 `BigInt` 这种“对象里包 primitive array + count”，而不是新开裸 `T[N]` 本地 scratch；对热核来说，先站在这条稳定形态上推进更对 |
| 新发现 | 这刀把 `_tmp_ecdsa_sign_probe` 从“旧位串行模乘路径”切到了真 `Montgomery` 路径，但运行面仍然在 39 秒量级、峰值内存 8GB 量级。说明算法方向已经换对，接下来真根是普通程序执行面对大对象值传递的热路径拷贝，不是再继续怀疑 `ECDSA` 数学本身 |
| 结果 | `add` 这类常见名字会把闭包里本不该可见的同名函数一起算进来，直接造成错误重载歧义 |
| 循环 import | `manifest_resolver_v2` 的闭包收集本身已是 BFS + 去重，天然能吃环；现在卡的是后续名字解析没按 import 可见域做 |
| 零前置声明 | 正确做法不是再加 predeclare，而是先收完整闭包，再按模块可见域统一解析类型/全局/例程 |
| 裸指针现状 | `v2/cheng-quic` 现在剩下的裸指针活口主要在 `tests/msquic_transport_smoke.cheng`，不是全包泛滥 |
| 本轮策略 | 先修解析架构，再清 `cheng-quic` 的旧裸指针语法；不再补单个症状 |
| 新发现 | `compiler_core` 这条主线还残留大量裸 `len/add/panic` 的隐式模块调用；一旦名字解析收紧，这些点会逐层暴露出来，必须系统迁移到显式模块函数 |
| 新发现 | `system-link-exec` 真缺的是最小 selflink string bridge，不是整块 `min_runtime`；正确闭包是 `system_helpers_selflink_cmdline_bridge.c + system_helpers_selflink_str_eq_bridge.c` |
| 新发现 | 把 `min_runtime` 整块接进 `system-link-exec` 会把 `stage1/build_emit_obj` 依赖链一起拖进来，方向是错的 |
| 新发现 | `compiler_core_pipeline_surface.cheng` 之前还保留了顶层 `ptr`、裸 `@importc("free")/@importc("cheng_fread")` 和地址样本；这正是 `stage3 taiji-seed-set` 报 `ZRPC no-pointer policy forbids ptr_type` 的真根因 |
| 新发现 | `stage0` 虽然能编过，但之前在透明 type alias、模块内 type 查找、索引元素类型、routine 返回类型、type match 这几处 helper 还残留旧扫描语义；这批尾巴已经统一切回 `header-arena + visibility + routine-arena`，source/stage0 现在是一套架构 |
| 新发现 | `v2/cheng-quic` 当前 Cheng 源码层已经没有真实裸指针表面；之前把 `msquic_transport_smoke` 当成活口是误判，真正该补的是编译器自己的 ZRPC contract |
| 新发现 | `semantic_facts` 里的 ZRPC 以前只有 fail-fast `panic`，没有正式 gate；现在最稳的做法是把“第一条违规”抽成纯函数，再用固定 probe 做 source/stage0 同构验证 |
| 新发现 | 当前 24 个 probe 里，安全表面是 `str/普通字段/sizeof/backtick/importc(str)`，违规表面集中在 `ptr/ref/*/->/alloc/raw seq bridge`；这批已经足够作为 `v2` 零裸指针主 contract |
| 新发现 | 循环 import 这条线真正缺的不是 Tarjan 算法本身，而是入口闭包收集还在手扫 `import` 文本、alias/enum 解析还残留 `sourcePath/type-count` 旧口径；改成 parser 驱动 import 收集和 `module_path + header_arena` 后，真实 cycle fixture 已稳定通过 |
| 新发现 | `enum member` 是剩下最典型的旧架构洞：之前每次解析都回扫 `program.topLevelKinds/typeBlocks`；正确做法是头阶段冻结成 `EnumMemberHeaderArena`，后面只查 arena |
| 新发现 | `field` 推断里那种“数前面有几个 `type` 来算 `typeIdx`”本质也是 source-order 假设；必须统一改成 `itemId -> typeBodyIdx` |
| 新发现 | 这刀过后，剩下的名字解析旧口子已经从“扫整份 program”收缩成“线性扫 header arena”；下一刀该做的是 `ImportHeaderIndex / TypeHeaderIndex / VisibleRoutineBuckets`，不是再补单个 case |
| 新发现 | `import/type/value/routine` 这四条线已经能收成显式模块可见桶；真正还没切掉的只剩 `module path -> id`、`type itemId -> bodyIdx`、`enum member` 这些 residual lookup，不该再回头补 noise case |
| 新发现 | `cycle/noise` fixture 证明闭包收集和显式可见域已经分开：同名模块只要没 import，就不该进 manifest，也不该被名字解析看到 |
| 新发现 | 这刀的主 gate 全绿说明 source 侧索引重构没有打穿 stage0/selfhost；下一刀可以继续沿 residual lookup 清尾，不需要先做大块 stage0 内部重写 |
| 新发现 | source 侧这三个 residual lookup 已经收干净：`module path` 走排序查表，`type itemId` 走稠密表，`enum member` 走排序查表；剩下的架构债已经从 source 主线转到 stage0 根的同构镜像 |
| 新发现 | `stage0` 现在也跟 source 同构：`module path` 走 `VisibilityIndex.lookupPaths/lookupIds` 二分，`type itemId` 走稠密表，`enum member` 走排序查表；这三处不再是 stage0 根里的旧扫描特例 |
| 新发现 | `routine itemId -> declIdx` 也是同类 residual lookup；现在 `source + stage0` 都已经改成 `RoutineHeaderArena.itemIdToRoutineDeclIdxs` 稠密表，routine 返回类型不再靠源码顺序现算 |
| 新发现 | 当前真正剩下的老根已经从 header arena 查询转成 closure 收集链：`collect_top_level_import_paths -> collect_compiler_core_closure_paths -> compile_compiler_core_source_closure` 还在决定闭包成员和装配顺序 |
| 新发现 | `taiji-seed-set` 的真阻塞不是 seed 逻辑，而是 `compiler_core` 合法源码根集合太窄，没把 `v2/tests/contracts` 当作 package 内合法源码根 |
| 新发现 | 正确修法不是给单个样本开特判，而是把 `compiler_core` 源码根显式化成集合：`package/src`、`package/tests/contracts`、`package/examples`、`workspace/src`，并让 source/stage0 共用这套规则 |
| 新发现 | 一旦根集合收正，`cheng_v2c taiji-seed-set`、native `taiji-seed-set`、stage0/stage3 seed 对拍会一起恢复，不需要再碰 seed 内部算法 |
| 新发现 | 零裸指针正式 gate 不能混扫 `cheng-quic` 和 `network/unimaker` 示例；它们不是同一条 `compiler_core` 生产 closure，而且当前 parser 表面也不一致 |
| 新发现 | 正确的第一刀是把 no-pointer gate 固定在真正的 `compiler_core` 生产根 `v2/src`，同时继续保留 `zrpc surface probe` 去覆盖正反样本 |
| 新发现 | 这样做以后，`probe` 负责语义边界，`closure gate` 负责真实生产源码闭包，两条线分工清楚，不会互相污染 |
| 新发现 | `manifest_resolver` 的 closure graph 里还残留最后一条老式线性查找：`module_path -> module_id`；这条现在已经和 `VisibilityIndex` 一样改成排序索引二分，source/stage0 不再在闭包边构造时扫 `modulePaths[]` |
| 新发现 | `topLevelOwnerModules` 现在已经足够闭合；一旦把 fallback 删掉也不会炸，这说明模块归属可以正式只认语义字段，不该再从 `sourcePath` 反推 |
| 新发现 | routine 语义链之前虽然 closure 和 top-level owner 已经显式化，但 `expr/type/call/assign` 主链里还在局部现推 `ownerSourcePath -> modulePath`；正确做法是 `buildCompilerCoreSemanticFacts` 对每个 routine 一次拿到 `ownerModulePath`，后面的可见性和调用解析都只吃这个 |
| 新发现 | source/stage0 里还留着一层已经完全不用的 `source_path` 型 type 可见性 helper 和 cache；删掉以后 `compiler_core` 表面条目少了 `2` 个，fixed point 也同步收缩，这说明那层确实只是旧架构尸体，不是必要兼容面 |
| 新发现 | 现在真正剩下的老根不在 routine semantic chain 里，而在 closure/import 图本身：`manifest_resolver_v2` 和 `cheng_v2c_tooling.c` 还在用 `sourcePath -> modulePath` 模型装配闭包；下一刀该直接上 `DeclaredModuleIndex + ModuleGraph + SCC`，不再追 dead wrapper |
| 新发现 | `DeclaredModuleIndex` 正确方向不是预扫整个 `workspace/src`；那样会把旧 `src/backend/...` 一起拉进来。必须按 import module path 惰性解析 source file，再冻结这个文件的 owner modules |
| 新发现 | legacy `std/*` 里存在 `import path != file owner module` 的历史现实，例如 `std/cmdline -> module cmdline`；正确处理不是回退到 `sourcePath -> modulePath`，而是“单 owner 文件允许 alias 归一化，多 owner 直接 crash” |
| 新发现 | stage0 里单 owner alias 分支最容易出错的点不是算法，而是生命周期：canonical module path 如果直接借临时 owner list 的字符串，释放后就会变成脏指针。必须先用 index 查成稳定地址，再释放临时列表 |
| 新发现 | closure 图这条线真正该收的不是更多 case，而是顺序主权：owner modules/imports 不能再排序，ready SCC 也不能再让最小 `modulePath` 决定；一旦这样收口，closure 顺序就真正只由图和首次发现顺序决定 |
| 新发现 | 当前真正剩下的老根已经继续收缩：不是 closure 图本身，而是 parser 和 semantic/import mirror 里还在用 `owner_source_path -> modulePath` 反推 owner；下一刀应该打 `strict owner parse + owner_module_path` 直传 |
| 新发现 | `semantic_facts` 这条链和 stage0 manifest 这条链需要两种不同索引：前者是 `declared import path -> owner module path`，后者是 `canonical module path -> source path`；把两者塞进同一个 helper 名字下只会反复炸编译和 mirror |
| 新发现 | `std/cmdline as cmdline` 这类历史 alias 的真正断点不在 machine matcher，而在 import 目标模块身份混了两套语义：一边拿 `std/cmdline`，一边拿 owner module `cmdline`，最后 qualified call 只能退化成 `load_import/load_field` |
| 新发现 | 最稳的修法不是回退 owner 语义，也不是补局部 fallback，而是把 `importTargetModulePaths` 作为结构化字段贯穿 `semantic_facts -> high_uir -> low_uir -> machine`，stage0 也单独做 `DeclaredOwnerModuleIndex` mirror |
| 新发现 | `compiler_core_plan_item_matches_owner_scope` 是 stage0 里最后一个还活着的 same-module 反推根；正确数据源不是 `source_path -> modulePath`，而是 `low_plan.owner_modules` 本身 |
| 新发现 | source/stage0 里那批 `owner_source_path -> owner_module_path -> _in_module` 薄 wrapper 已经完全没调用；删掉后 `surface_count/compiler_core_entry_count` 从 `2673` 收到 `2669`，说明它们确实只是旧兼容尸体，不是语义面的一部分 |
| 新发现 | `compilerCoreCallTargetFacts/AssignTargetFacts` 这两个 source wrapper 也已经完全死掉了；主调用链早就只走 `...InModule`，保留外壳只是在语义面继续暗示可以从 `ownerSourcePath` 反推模块 |
| 新发现 | stage0 的 `compiler_core_program_find_visible_top_level_{item_id,kind}` 也是同类死外壳；删掉以后 source/stage0 都只剩 `_in_module` 真接口，`owner_source_path` 反推链又少了一整层 |
| 新发现 | 这刀之后还活着的 `owner_source_path` 大头已经不在这些 helper wrapper，而在 parser 默认 owner、manifest/import mirror 和少数仅用于诊断文本的参数线；下一刀该直接打前两类活根，不该再扫死接口 |
| 新发现 | parser 末尾那段默认 owner 回填现在还不能删。仓里真实文件确实存在“先 `import/fn`，后 `module`”的写法；直接删掉会把 `topLevelOwnerModules` 非空这个当前输出契约打坏 |
| 新发现 | 真正值得继续清的是 `manifest_resolver` 和 stage0 closure mirror 里“每次拿到 `ownerSourcePath` 就重 parse 一遍文件再分 owner/import”的链，这才是活着的图装配老根 |
| 新发现 | 正确修法不是继续传 `ownerSourcePath`，而是把 `DeclaredModuleIndex` 升成真正的数据面：`module -> source` 之外，再显式保存 `module -> import targets`，让 closure 图后半段只查索引，不再反复解析文件 |
| 新发现 | 这刀之后 source/stage0 都已经按这套架构收口；剩下的 `owner_source_path` 活根主要是 parser 默认 owner 和少数诊断文案参数，不再是 closure/import 主算法本身 |
| 新发现 | `sourcePath -> modulePath` 现在还保留的一条活线，本质已经不是 owner 推导，而是 `declared module path` 的 manifest 语义；继续把它叫 `sourcePathToModulePath` 只会混淆“文件路径”和“模块归属” |
| 新发现 | stage0 里还活着的一条真根是未限定 enum 成员解析：之前它拿 `low_plan.source_paths[current]` 去找枚举；这条现在已经改成直接按 `low_plan.owner_modules[current]` 解析，source/stage0 终于同构 |
| 新发现 | source `semantic_facts`、source `low_uir`、stage0 facts 里那批 `ResolveEnumMemberOrdinalInSourcePath` 都已经是死外壳，删掉后不影响语义面，只会让 owner/source_path 关系更干净 |
| 新发现 | 更硬的全局 no-pointer gate 现在还不能直接扩到 `v2/examples/cheng-quic`。断点不是裸指针策略，而是当前统一 parser 还吃不下这些非 `compiler_core` surface；这一步必须等 parser 覆盖补齐，不能靠放宽 gate 或文本特判硬混过去 |
| 新发现 | 真正挡住全局 no-pointer gate 的不是 `cheng-quic` 里还有多少裸指针，而是 source/stage0 对非 `compiler_core` line surface 没有统一 parser 和统一违规提取面；先补 parser，再开硬 gate 才是正路 |
| 新发现 | stage0 之前缺的不是整套 `if-expr`，而是 `return/let/var/assign` 行尾那段 multiline tail 收集和 inline `elif` 递归；把这两处补齐以后，source/stage0 在非 `compiler_core` surface 上重新同构 |
| 新发现 | line-surface no-pointer 检查不能把 `->` 当成 pointer 语法。`fn ... -> Result[...]` 是正常返回签名；把它当违规只会把合法 `v2/examples` 全炸掉 |
| 新发现 | `cheng-quic` 的 `quic/tests/*`、`quic/project/*` 不是越界样本，而是 package 内合法模块空间。正确修法不是加单文件特判，而是让 package source roots 除 `src/tests/examples` 外，还显式包含 package root 自身 |
| 新发现 | `cheng-quic` 里旧 `$()` 语法该迁走，不该要求 `v2` parser 回头兼容。当前正确方向是继续收紧到显式 `intToStr(...)` 一类现语法 |
| 新发现 | 新 gate 一旦扩到 `v2/examples + v2/cheng-quic`，contract 口径必须跟着切到 `version=v2/root_count=4/checked_file_count=267`；继续保留旧 `v1/root_count=1` 只会让 fixed-point 永远假失败 |
| 新发现 | no-pointer 只挂在 verifier 上是不够的；真正正确的边界是在 `release_artifact` 的生产编译入口直接 parse 并执行同一条 `requireV2NoPointerSurfaceFree(...)`，这样 `release-compile/system-link-plan/system-link-exec` 才会一起硬失败 |
| 新发现 | 把 no-pointer 直接扩到整个仓库会立刻撞上旧 `src/std/rawmem_support.cheng`，这说明 gate 必须先严格钉在 `v2` 层，而不是拿 legacy `src` 混进来后再靠特判解释 |
| 新发现 | 非 `compiler_core` surface 的最小正式验收不该继续借 `compiler_core` probe；正确做法是单独补 `verify-v2-no-pointer-surface`，用 `compiler_core/unimaker/topology/network_distribution` 四类 surface 的统一 probe contract 收口 |
| 新发现 | source/stage0 的 no-pointer 诊断文案也必须同构；否则 fixed-point 看起来绿了，真正切到下一阶段编译器时还是会在同一条规则上漂移 |
| 新发现 | `release-compile` 的 no-pointer 负例 contract 不需要新 tooling 命令，也不需要新 C bridge；直接用前台 `cheng_v2c release-compile` 捕获稳定诊断和退出码就够了 |
| 新发现 | 这类负例不该塞进正向 `release_artifact` fixed-point 文件；正确形状是单独的 compile-failure contract，这样正向产物 contract 继续只描述成功编译路径 |
| 新发现 | 真实 package 入口的 no-pointer 硬 gate 也不需要新 verifier；直接固定前台 `cheng_v2c release-compile` 的成功关键字段就够了 |
| 新发现 | 现在最适合纳入硬 gate 的真实入口只有 `v2/examples` 里的 3 个：`unimaker_robot_node`、`network_distribution_module`、`topology_code_sync`；`v2/cheng-quic/src/project/*` 仍会和 `program` runtime 缺口缠在一起，不该混进这条 gate |
| 新发现 | 入口级成功 contract 还不够，真正稳的 no-pointer 产线口径应该看 package 闭包；最短路径不是再包一层 shell，而是补 source/stage0 同构的 `verify-v2-no-pointer-package-closures`，直接对 manifest 闭包逐个 parse 并查第一条违规 |
| 新发现 | `v2/examples` 这 3 个真实 package 入口当前闭包都只有 `2` 个文件、`1` 个被实际检查的 `.cheng` surface 文件；这说明 package closure gate 已经足够真实，但还没被 `v2/cheng-quic/src/project/*` 的 `program runtime` 缺口污染 |
| 新发现 | `release-compile` 的非 `compiler_core` manifest 根就是“入口文件的父目录”。所以负例如果直接放在 `v2/tests/contracts`，会把整目录里别的历史 fixture 一起卷进闭包，先被无关旧语法打死，根本测不到 no-pointer |
| 新发现 | 真实 compile-path no-pointer 负例必须放进各自独立的 fixture 根；这样 `release-compile` 才会只看到这一份文件，稳定死在 no-pointer gate，而不是死在旁边的旧 `$name` 样本 |
| 新发现 | 一旦 no-pointer 从单文件 surface 推进到 manifest 闭包，`release_artifact`、`system_link_plan`、`system_link_exec`、native smoke、`tooling release`、`full-selfhost` 的 fixed-point 都会一起漂；这不是多条逻辑分叉，而是同一组 compile-closure 字段自然向下游传播 |
| 新发现 | shared no-pointer compile-closure 不需要再加新 verifier；`system-link-exec --emit shared` 现成数据面已经自然携带 `manifest_file_count/no_pointer_checked_*/no_pointer_closure_ok`，正确做法是直接收既有 shared contract |
| 新发现 | `system-link-plan --emit shared` 之前缺的不是算法，而是数据面：`SystemLinkPlanBundle` 没把 manifest/no-pointer 闭包字段带出来，所以 shared plan contract 没法固定真实 package compile-closure 结果 |
| 新发现 | stage0 真 bug 在 `build_release_artifact(...)` 的实参链：`build_system_link_plan(...)` 误吃了未初始化的 `out.manifest_file_count`，所以 `manifest_file_count` 被打成 `0`；`no_pointer_checked_*` 之所以正常，只是因为那几个实参没走错位 |
| 新发现 | 正确修法不是再算一遍 manifest，也不是在 report 文本里补丁，而是 source/stage0 都让 `buildSystemLinkPlan/build_system_link_plan` 直接接收 release-artifact 已经算好的 compile-closure 字段，然后让 shared plan contract 自然固定下来 |
| 新发现 | `chain_node` 的真缺口不在 `program argv bridge`、不在 provider、也不在 `program_local_payload_bridge`；那条 native 链本来就是通的，真正缺的是 low_uir 没给 `program` 轨剩下那批 `ast_ready` 函数产出 exec ops |
| 新发现 | 挡住 `chain_node` 的公共语义面是 `break/continue`。正确修法不是新 runtime 模式，而是在 source/stage0 low_uir 都维护 loop label 栈，把 `break/continue` 直接 lower 成已有 `jump`；`for_range` 的 `continue` 必须跳到 `step` label，不能偷跳 `head` |
| 新发现 | stage0 那条“lower 失败就 truncate exec ops 然后把 `routine_op_count` 写成 `0`”的旧回退会把 source 的 crash 语义重新吞掉；删掉它以后，`program_local_payload_exec_plan_missing` 才能反映真实缺口 |
| 新发现 | 这刀之后 `chain_node` 已经从 `compiler_core_program_local_payload_missing_exec_plan_function_count=57` 收到 `0`，`system_link_ready=1`，而且 `system-link-exec --emit exe` 已经能真链接；下一条真阻塞已经不在 lowering，而在生成出来的程序运行时路径上 |
| 新发现 | 当前生成的 `./v2/artifacts/bootstrap/chain_node_test` 执行 `balance --root ... --account alice --asset TEST` 仍然静默退出 `1`；这说明 `program runtime` 还剩下一条新的活缺口，但它已经和 `exec_plan_missing` 无关了 |
| 新发现 | `256` 项生成元运行时大表不是正确方向。虽然理论上能少一半窗口加法，但当前纯 Cheng 里它把启动时预计算成本直接推爆，`_tmp_p256_pubkey_probe_bin` 冷启动会退化到 `20s timeout`；这条路已经证伪并撤回 |
| 新发现 | `bigModInvOddBinary` 在当前 `BigInt` 表示上不是现成捷径。把 `p256ModInv/nModInv/p256FieldFixedInv` 全切过去后，`pubkey` 反而比固定 Montgomery 幂更慢，所以二进制模逆不能直接拿来当 active 路径 |
| 新发现 | 当前真正有效的固定基点优化是“小表 + 对的算法”，不是“大表硬堆”：`16` 项仿射表配合一次 batch inverse、`mixed Jacobian-affine add` 和 `wNAF`，已经把 `pubkey` 冷启动压到 `11.63s` |
| 新发现 | `sign` 在 `40.05s` 仍然超时，说明剩余主热根已经不在 `pointMulGenerator` 这条线本身；下一刀不该再继续扩 `G` 预计算，而该直接拆 `sign` 的剩余热段，优先查 `nModInv/deterministicK` |

| 新发现 | `full-selfhost` 之前卡的不是 stage2 算法，也不是 contract 内容，而是 `tooling-selfhost-check` 只存在于 stage0 命令面，不存在于 source/runtime/native 的同构命令链；stage2 binary 打印对了内容却走成 `rc=1` |
| 新发现 | 正确修法不是加回退，也不是在 host bridge 上吞退出码，而是把 `tooling-selfhost-check` 变成 source 里的真实命令，并给 native/runtime 补一条直接回到 `cheng_v2c_tooling_handle(argc, argv)` 的 bridge |
| 新发现 | Cheng-Chain 这条线当前真正缺的已经不是链算法：洛书多播树、信息分散、被动反熵、batch 共识都已落库并有 smoke。剩下的是普通程序运行面，把 `chain_node_test` 从“能链接”推进到“真能跑” |
| 新发现 | `chain_node_test balance ...` 现在不是静默 `1` 了，错误已经暴露成精确 runtime 缺口：`driver_c compiler_core program local payload bridge: exec-plan interpreter missing label=main ... exec_op_count=298`；下一刀该补的是 program 轨执行器，不是链层逻辑 |
| 新发现 | `v2/docs/LSMR.md` 里“洛书-八卦多维前缀树”之前只有文档，没有真正的数据面实现；正确结构不是再造一棵通用 map，而是 `bagua root -> luoshu prefix path -> entry/value cid` 的 SoA 树，节点和整棵树都要能独立 canonical 化并求 cid |
| 新发现 | 这棵前缀树要想可证明稳定，必须先按 `(bagua, resolution, address, key, valueCid)` 排 entry，再按 `(bagua, depth, prefix)` 排 node，child 还要按 luoshu digit 排；否则同一批 entry 换输入顺序会导致树 cid 漂移 |
| 新发现 | `lsmr_bagua_prefix_tree_smoke` 证明当前实现已经满足最小闭环：4 条 entry 会稳定生成 9 个节点，输入重排后 `treeCid` 不变，`zhen/kan/kun` 三个 bagua 的直系 entry 和 descendant 计数都正确 |
| 新发现 | `msquic_verify_runner` 现在能编过，但运行仍卡 `driver_c program runtime: unresolved module field module=quic/msquictransport field=initMsQuicTransport`；这是一条独立的普通程序运行时老根，不该反过来阻塞前缀树算法收口 |
| 新发现 | 前缀树真正该挂的不是 event 集，而是 `account head/state` 视图。这样共识、反熵、重放、后续状态证明都能共用一棵树，不会把 DAG 历史和当前状态混成两套索引 |
| 新发现 | 正确的数据分层是：共识层只维护账户 SoA 真相，状态树是派生索引。共识写路径只打 `stateTreeDirty`，读取签名或做状态摘要时再按需精确重建；不能反过来把树维护逻辑塞回共识主算法里 |
| 新发现 | `anti_entropy` 的原 `head_set_cid` 还不够表达局部敏感状态；加入 `state_tree_cid + per-bagua root summary` 后，链节点已经有了下一刀做“子树差异同步”而不是“全量 event_cid 扫描”的稳定数据面 |
| 新发现 | 这刀之后 `msquic_chain_smoke` 已经能固定验证状态树闭环：mint 后有树、sync 后两端 `stateTreeCid` 相等、transfer 后 entry 数变成 `2`、replay 后树根不漂；这说明状态树已经进入真实节点路径，不再只是独立算法样例 |
| 新发现 | 真正正确的同步方向不是“前缀树直接传状态快照覆盖本地”，而是“前缀树只负责缩小差异面，真正落库仍只靠事件 DAG”。所以这轮把协议收成了 `state_root -> subtree -> account headCid -> event ancestors`，没有引入不可信快照 |
| 新发现 | `chainAntiEntropyPlan` 里最值得删掉的不是 `provideEventCids` 文本，而是“全事件集合差分”这个主算法；只要 state tree 可用，主路径就该先比 root/subtree cid，再按账户 head 拉祖先链 |
| 新发现 | 子树差分要想可证明正确，不能只比较 node cid；还要同时比较 `entryDescCount`。这样即使实现层误用旧 cid 或局部节点丢子树，也能直接 crash 暴露，而不是静默漏同步 |
| 新发现 | 同步方向必须考虑时钟单调性。远端子树叶子如果 `remoteClock < localClock`，说明远端更旧，只能跳过，不能反向回滚本地；如果 `remoteClock == localClock` 但 `stateCid` 不同，必须直接报冲突，不能“选一个覆盖” |
| 新发现 | `chain_node_test` 现在已经能真跑 `mint/balance`，说明不含 QUIC 的节点日志/索引/状态树路径是通的；`serve` 仍卡 `maxMsQuicPending`，这是独立的 QUIC runtime 老根，不是这轮树差分协议的问题 |
| 新发现 | 新增一个 `src/tests/chain_state_tree_sync_smoke.cheng` 就会让全局 no-pointer closure contract 自然加 `1` 个文件；这类 contract 漂移不是噪音，而是说明链代码已经正式进入 v2 生产闭包 |
| 新发现 | 当前 `chain_state_tree_sync_smoke` 能 `system-link-exec` 真链接，但运行仍卡 `driver_c program runtime: unresolved name label=chainSeenClockValue owner=quic/chain/anti_entropy`；这说明普通程序 runtime 对 `anti_entropy` 这批新 label 还没覆盖，下一刀如果要真跑 QUIC 反熵，必须先补这条运行时老根 |
| 新发现 | 上面这条 `chainSeenClockValue` 运行时错误已经过期。当前真正的根因不是 registry 缺项，而是子树同步算法顺序错了：父子树还没补完就先验父节点，必然报 `subtree not converged` |
| 新发现 | 正确算法必须是后序遍历：先扩 child subtree，再补 leaf account headCid 祖先链，最后回到 parent subtree 做 `nodeCid + entryDescCount` 收敛校验。只要顺序反了，就会制造假失败 |
| 新发现 | 这刀把 `chainNodePullStateDiff(...)` 和 `chain_state_tree_sync_smoke` 同时收成同一套后序遍历后，smoke 已经真运行通过；说明当前树差分数据面和祖先链补齐算法是闭合的，下一刀该进真实 QUIC `sync-once/serve` 路径，而不是回头怀疑状态树本身 |
| 新发现 | 单靠把 `bigModMul/bigModExp` 切到 Montgomery 还不够；`_tmp_ecdsa_sign_probe` 仍要前台跑几十秒，说明普通程序热路径的大对象值传递/拷贝仍然是主瓶颈 |
| 新发现 | 仓库里真能复用的高性能纯 Cheng 内核只有两条：`bigint` 的 `32-bit limb + Montgomery` 算法骨架，和 `ed25519/ref10` 的固定宽度 field；没有现成 `P-256 8x32/4x64/Jacobian` 可以直接拿来用 |
| 新发现 | 按理论最短路继续推进时，`4x64` 不是当前 Cheng 的最佳第一刀，因为缺稳定 `uint128` 乘法承接；正确起点是 `8x32` 固定 limb |
| 新发现 | `src/std/crypto/p256_fixed.cheng` 这条新 `8x32` 专用 Montgomery 核已经能前台编过，但运行 probe 立刻在第一拍 `a * R^2 mod p` 暴露出 `carry overflow`，不再是模糊的“还是很慢” |
| 新发现 | 用同样的 `8x32 Montgomery` 算法和同样的 `P/R^2` 常量做 Python 参考时，`a * R^2 mod p` 的理论 scratch 最高只到 `idx=15`，说明当前 Cheng 版 `carry overflow idx=32` 不是算法本身需要这么大 headroom，而是实现语义或运行面有偏差 |
| 新发现 | 把 `p256_fixed` 再压到最小 stage probe 后，`a * R^2` 的第一拍低位和 `mWord` 都已经对上了；真正出错的是紧接着的 `+ mWord * modulus`，稳定断在 `mul reduce i=0 / carry overflow offset=0 idx=17` |
| 新发现 | 这说明当前真根不是 `ECDSA` Jacobian 公式，也不是 `P/N/R^2` 常量；根更靠近普通 program 轨的 `uint64` 乘法/移位或固定宽度 Montgomery 语义实现 |
| 新发现 | 因为专用核还没闭合，这轮不能把它接进 `ecnist`；正确做法是撤回 active 接线，保持 `ecdsaSign` 继续走稳定的 generic Montgomery + Jacobian 主线，然后单独查 program 轨的 64 位热算子 |
| 新发现 | 普通 program runtime 的真 bug 在 `src/runtime/native/system_helpers_stdio_bridge.c`：`binary_op shift_right` 和整组整数运算一直把 `uint32/uint64` 压成 `i32/i64`；只要最高位被置位，`prod >> 32` 就会发生符号扩展，直接污染 `Montgomery` carry 链 |
| 新发现 | `_tmp_p256_runtime_probe` 证明旧问题不是 scratch zero-init，也不是 `uint64` 乘法低位错，而是高位提取错：修之前 `mul_hi_ok=0` 且 `mul_hi_bit32=1`、`mul_hi_bit63=1`；修之后恢复成 `mul_hi_ok=1`、`mul_hi_bit32=0`、`mul_hi_bit63=0` |
| 新发现 | `p256_fixed` 里把 `scratch[16] != 0` 直接当 `montgomery high carry overflow` 是错误的。对 `n` 这样的模数，末尾结果允许带一个高字，正确做法是把它当成 `N+1` 字结果做一次条件减模，而不是直接 crash |
| 新发现 | 把 runtime 的 unsigned 值语义收正，再把 `p256_fixed` 的末尾正规化成“带高字条件减模”后，`p256_fixed_core_probe` 已经前台真跑到 `probe=ok p_mul=1 n_mul=1`；这说明当前 `P-256 8x32` 专用核的最小 Montgomery 内核已经闭合 |
| 新发现 | `missing type item for zero init type=pfix.P256Fixed` 的真根不是 alias import 失效，而是 `Result[T]` 这类泛型类型在零初始化 field 替换后，仍拿声明模块 `std/result` 去解实例化实参里的 `pfix.P256Fixed`；正确修法是把 zero-init 类型解析改成“双作用域”：声明模块作用域优先，实例化模块作用域补充 |
| 新发现 | 最小 alias probe 只会死在 `printLine` 这条无关路径，说明 `import std/crypto/p256_fixed as pfix; var x: pfix.P256Fixed` 本身是通的；问题只出现在泛型实例化后跨模块解类型 |
| 新发现 | `ecdsaSignBytes` 当前不再先天崩在类型解析；前台 probe 已经能真跑进热核并在 8 秒后超时，说明下一层真瓶颈不是类型系统，而是普通程序热路径执行性能 |
| 新发现 | `ecdsa` 打点 probe 已经把热点压到 `stage=mul`：`priv/hash/nMod/deterministicK` 都能快速过去，卡的是 `pointMul` |
| 新发现 | `ecnist` 里虽然已经有 `pointMulFixedWindowed`，但它每次调用都会现场重建 16 项窗口表；对固定基点 `G` 来说这纯属浪费。正确修法是把 `G` 的窗口表预计算进 `p256EnsureInit()`，并让 `ecdsaSign/publicKeyFromPrivateKey` 走生成元专用路径 |
| 新发现 | 即便把 `G` 基点窗口表预计算后，`ecdsa` 打点 probe 仍然卡在 `stage=mul`，这说明当前更深的根已经不是 `ECDSA` 数学算法种类，而是普通程序热路径仍挂在 `runtime/compiler_core.program_local_payload_entry` 这条解释执行架构上 |
| 新发现 | `p256EnsureInit()` 之前那段 `hexDecode + bigFromBytes + Jacobian 建表` 本身就是大热点；把 `P/N/B/G` 和 `1..15*G` 的 `Montgomery` 表固化进源码后，签名探针里 `stage=priv -> stage=hash` 已从大约 `4.6s` 直接掉到大约 `0.006s` |
| 新发现 | `k=1` 还慢的另一半不是主循环，而是尾部 affine 化在 `z==1` 时还傻做模逆；给 `pointJacobianToAffineFixed()` 加 `z == oneMont` 快路后，`publicKey` 单探针已从 `7.55s` 掉到 `1.32s` |
| 新发现 | `ecdsaSign()` 原来在 `r = x(kG) mod n` 之前还会把 `y` 也一起从 `Montgomery` 域转回 `BigInt`；这对签名没有任何价值，直接浪费了两次大对象转换 |
| 新发现 | 这轮之后 `ecdsaSignBytes` 已经能前台真完成，`_tmp_ecdsa_sign_probe_bin` 当前 `rc=0 real=29.73`；说明初始化和无用坐标转换这两层已经收掉，剩下的真根只剩固定点 `mul` 主循环本身 |
| 新发现 | 生成器标量重编码之前仍然走的是 `BigInt` 路：`bigIsOdd + bigGetBit(low5) + bigAdd/bigSub + bigShiftRight1`。这条路对固定 256 位标量是纯浪费，正确结构就是固定 `8x32` 原地右移和小常数加减。 |
| 新发现 | 把生成器重编码改成固定 `8x32` 后，`publicKey` 单探针已经从 `1.32s` 继续压到 `0.44s`，说明这层不是小优化，而是真热点。 |
| 新发现 | `pointMulGeneratorWnaf()` 主循环里每次遇到负 digit 现算 `-P` 也是纯浪费；把 `p256GWindow4Neg` 一次性预好之后，签名单探针又从 `29.73s` 压到 `26.81s`。 |
| 新发现 | 现在剩下的真根已经更纯：不是初始化，不是重编码，不是负点构造，主要就是 `pointJacobianDoubleFixed()` 和 `pointJacobianAddAffineFixed()` 本身的乘法条数与数据流。 |
| 新发现 | `field square` 方向本身是对的，但实现形状很关键：第一版“逐项积累 + 可变 carry 链”的 square 虽然算对了，却把 `stage=mul` 从 `26.83s` 拉坏到 `31.84s`，说明这条热核必须用固定列式 Comba，不能在热循环里跑可变长 carry 传播。 |
| 新发现 | `pointMulGeneratorJacobianComb6FixedInto(...)` 的 `digits[]` 预计算不是最短路。它能把 `comb` 单独压到约 `1.1910s`，但 `digits` 填充和 `startRow` 扫描会把总链路拉回 `pubkey=1.3587s`、`sign=2.0194s`、`mul=1.5392s`、`kinv=0.7741s`，整体比稳定基线更差，所以不能保留。 |
| 新发现 | 当前 `comb6` 主根仍然在“每轮 digit 提取 + 后续热循环”这条线上，但正确方向不是先把 digit 全存进本地数组；下一刀该直接压 `p256ScalarFixedComb6Digit(...)` 本体和循环内 digit 生成成本。 |

| 新发现 | 固定 `8x32` Comba square 接回 `p256_fixed` 后，`p256_fixed_core_probe` 已能同时对拍 `p_mul/n_mul/p_square/n_square` 四项，说明 field 层的专用自乘核已经闭合，不再只是乘法核顺带复用。 |
| 新发现 | 这条 square 核一旦接进 `pointJacobianDoubleFixed/pointJacobianAddFixed/pointJacobianAddAffineFixed + modexp`，`_tmp_ecdsa_sign_stage_probe_bin` 的 `stage=mul -> stage=r` 会从 `26.83s` 压到 `21.84s`，而 plain `ecdsaSignBytes` 也会从上轮 `26.81s` 压到 `25.94s/24.32s`。 |
| 新发现 | 现在新的真根已经进一步缩小：不再是 field 自乘，而是 `pointJacobianDoubleFixed/pointJacobianAddAffineFixed` 里剩下那些非自乘的 `mul` 和 record 值流。下一刀该继续压乘法条数和数据流，不该再回头碰初始化、窗口表或 `bigint`。 |
| 新发现 | “第一梯队”统计口径必须按同机 C `1:1` 写死。当前基线已测：`ecdsa p256 sign=11.9109us/op`，`ecdh p256=24.4003us/op`。任何 `2x` 口径都是错的。 |
| 新发现 | 现有 `_tmp_p256_pubkey_probe_bin` 的 `priv=1` 只说明快路没回归，不代表常规标量生成元点乘。补上 `_tmp_p256_pubkey_scalar_probe_bin` 后，常规 generator 点乘当前稳定在 `20.9950s` 量级，和同机 C `24.4003us/op` 还差约 `860440x`。 |
| 新发现 | 按主文档四条主线加权后，总工程进度当前只能算约 `32%`。分数被“普通程序执行面”和“纯 Cheng 性能内核”这两条高权重主线压住，不能被 fixed point 和链算法 smoke 的完成度掩盖。 |
| 新发现 | `pointJacobianAddAffineFixedInPlace` 的 trusted 重写之前不是公式错，而是 `p256FixedModAddTrustedInto/p256FixedModSubTrustedInto` 的 reduction/borrow 分支在普通 program 轨上会把结果打成零；不先修这层，继续接 trusted 热核只会反复炸在 `h/i/yDiff/r`。 |
| 新发现 | 用 `_tmp_p256_mul_add_stage_cmp_probe` 逐拍对拍后，这条老根已经被修掉：第一处错位现在稳定推进到 `x3`，说明下一刀该只盯 `x3/y3/z3` 的 trusted `sub/mul` 值流，不再回头查基础 `add/sub`。 |
| 新发现 | 继续把 `z3a/z3` 也拆成 `value/into` 后，`_tmp_p256_mul_add_stage_cmp_probe` 已经全绿；真正坏的不是 `p256FixedModSubTrustedInto` 本体，而是 probe 自己那层 `fieldSubTrustedInto` 包装。 |
| 新发现 | 把 `pointJacobianAddAffineFixedInPlace` 整段切到 trusted `Into` 活路是错误方向。虽然对拍能过，但实际 `pubkey` 会回归到 `19.88/19.49/19.47s`，`sign` 会回归到 `23.20s`，说明当前 Cheng 热路径里“满屏本地副本 + direct Into”比现有 `Crash/Value` 链更慢。 |
| 新发现 | 把 `p256FieldFixed{Add,Sub,Mul,Square}Crash` 直接连到 trusted 核也不是捷径；`pubkey` 单样本会直接变成 `30.73s`。这说明现在最该砍的不是最外层 `Result` 壳，而是 `addAffine` 主链内部剩余的非自乘乘法和值流。 |
| 新发现 | 这轮进一步证明 helper 也不是主根：`pointJacobianAddAffineFixedInPlace` 里只把 7 次 `mul` 换成 helper 版 shared-scratch trusted 路，两个 compare probe 都绿，但整链还是明显回归到约 `19.50s/22.38s`。 |
| 新发现 | 把同样的 7 次 `mul` 改成完全 inline 的 `pfix.p256FixedMontgomeryMulCoreTrustedScratchInto` 也救不回来；`_tmp_p256_mul_add_probe_bin` 单样本直接到 `26.71s`。这说明现在错的不是 helper 调用边界，而是 active `ecnist` 直连 trusted scratch 这条思路本身。 |
| 新发现 | `pointJacobianDoubleFixedInPlace` 对 trusted scratch 更敏感：只换 3 次 `mul`，`_tmp_p256_mul_double_cmp_probe` 就会直接崩。下一刀不能再碰 active `trusted scratch`，只能回 pure `Crash/Value` 路里继续拆 `addAffine/double` 的值流。 |
| 新发现 | `pure out-parameter` 这条路在当前普通程序执行面也还不可信。字段级 direct `p256FixedModAdd/Sub/Mul/Square Into` 单独对拍能过，但一旦组合成 `double/quad/eight` 再接进 `pointJacobianDoubleFixedInPlace/pointJacobianAddAffineFixedInPlace`，就会出现 `step=0` 偏差，补完“同一输入重复传两次”的自别名拆解后还会直接崩。 |
| 新发现 | 所以下一刀不能继续赌 fixed-record `out-parameter` 会自动更快。当前最短路已经不在“再砍一层返回值包装”，而要回到算法级：重新评估更强的固定基点乘法，而不是继续在现有 `double/addAffine` 纯值流上硬抠。 |
| 新发现 | `cheng-quic` 这轮重新炸出来的不是链算法错，而是和 `chacha20poly1305` 同类的 `var` 语义真 bug：只读函数如果同时声明 `Bytes` 和 `var Bytes` 两个重载，`var` 实参一进来就会在低层 lowering 里变成二义性。正确修法不是放宽重载解析，而是删掉 `var Bytes` 假重载。 |
| 新发现 | `v2/cheng-quic/src/connection.cheng` 的 `copyBytesRange` 修掉以后，`chain_node.cheng` 和 `chain_state_tree_sync_smoke.cheng` 已经都能真链过，说明当前链主线的编译阻塞不在 `LSMR/反熵/共识` 算法本身，而在 `cheng-quic` 表层 API 的 `var` 语义兼容残留。 |
| 新发现 | `chain_node_test` 现在已经能真实执行 `balance/mint`，但 `chain_state_tree_sync_smoke_bin` 会长时间卡在 `runtime_compiler_core_program_local_payload_entry -> driver_c_prog_eval_item -> driver_c_prog_resolve_visible_item`。这说明下一处真根已经不是编译器前端，也不是链状态树算法，而是普通 `program` 运行面仍在解释执行并且名字解析成本过高。 |
| 新发现 | `driver_c_prog_try_builtin` 之前会对每一次普通 Cheng 调用都跑一遍，即使 callee 既不是 builtin 也没有 `importc_symbol`。这不是“builtin 慢一点”的局部问题，而是调用面设计错了。正确做法是把 builtin 身份前移成 `builtin_tag`，并且只让 `builtin/importc` 进入那条分发链。 |
| 新发现 | 把 `builtin_tag` 前移后，`try_builtin` 不再是普通 Cheng 函数调用的必经路径；这说明当前 `program` 运行面的大热点已经从“名字解析 + builtin 白扫”收缩到更深一层的 `zero_value_from_type / zero_value_from_type_decl`。下一刀该打零值预解码，不该回头继续碰 builtin 字符串分发。 |
| 新发现 | `msquic` 这次把内存打到 `311GB` 的真根不是递归，而是握手重传包被重复喂进 TLS：`native_runtime` 在 `Initial/Handshake` 上只要看到 `FrameCrypto` 就直接 `msquicTls13HandshakeFeed(frame.data)`，没有按 `CRYPTO offset` 去重重组。只要握手慢、重传触发，同一条握手消息就会被反复解析。 |
| 新发现 | `msquicTls13HandshakeAppendTranscript()` 是单向追加，而 `ByteBuffer.appendBytes()` 每次都是重新分配 + 整块拷贝；再叠加 `msquicTls13TranscriptHash()` 每次 `toBytes(state.transcript)` 的整块复制，就会把“重复握手消息”放大成内存和时间的二次方灾难。 |
| 新发现 | 正确修法不是加兜底或限流，而是让 `Initial/Handshake` 的 `CRYPTO` 输入先走 `MsQuicCryptoStream` 的 `offset` 重组，再把 ready bytes 喂进 TLS。这样重复重传即使换了新的 packet number，也不会再次污染 transcript。 |
| 新发现 | 在根因彻底收完前，`TLS 1.3` 握手 transcript 和缓冲区必须有硬上限。这里不是兜底，而是 fail-fast：只要 transcript 或握手缓冲异常增长，就应该直接报错，不允许再把机器吃到 311GB。 |
| 新发现 | `chain_state_tree_sync_smoke` 当前的 `driver_c program runtime: var arg not ref caller=chainAntiEntropyEventLines callee=add callee_owner=std/seqs` 真根不在 source 侧语义，也不在 `anti_entropy` 算法；真根是 `stage0` 的 `compiler_core_program_lower_expr(call)` 之前按“半成品 low_plan”反查 callee 参数签名。只要真实 callee 比当前 caller 更晚进入 `low_plan`，`var_param` 就会丢成空串，第一参数被错误降成值。 |
| 新发现 | 这条问题不能在 runtime 自动把 array 值补成 ref，那是兜底。正确修法是编译期直接按完整 `program/itemId` 查真实 routine 参数签名，再决定 `addr_of_local/addr_of_param`。这才和 source 侧“基于完整图做 lowering”同构。 |
| 新发现 | `chain_state_tree_sync_smoke` 里新的 `len` 歧义根不是抽象的“名字解析偶发失败”，而是 `anti_entropy` 里仍有裸 `len(state.headCid)`，而 smoke 本身也还在用裸 `len(...)` 处理字符串。把这些字符串场景显式收成 `strings.len(...)` 之后，smoke 已经能前台真跑通过。 |
| 新发现 | `msquic_chain_smoke` 现在已经稳定越过 `lsmr/dispersal/broadcast/anti_entropy/consensus` 五段算法，新的最小根因是 `lsmr.cheng` 中大量裸 `add(...)` 还停在 `load_name|add`。`machine-plan` 已直接证明 `chainLsmrDigitOrder()` 等函数仍是 `load_name|616464`，而不是 `load_routine|add`。这不是链算法错，而是基础库里留下了未显式模块化的序列追加调用。 |
| 新发现 | [LSMR.md](/Users/lbcheng/cheng-lang/v2/docs/LSMR.md) 之前主要还是理论文档，现状和代码落点不够清楚。现在已经补上“已完成/部分完成/未完成/代码落点/验证”的状态表，后续不该再把它当成纯愿景文档引用。 |
| 新发现 | `LSMR.md` 之前真正没落地的是 3 块：`大衍流转 PubSub`、`CSG 子图过滤`、`空间证明`。这轮已经把它们补成正式算法模块；剩下没闭的不是技术面，而是“接进真实 QUIC runtime 后还能稳定联网”这条运行面。 |
| 新发现 | `大衍流转 PubSub` 这条正确结构不是字符串 topic + 随机 fanout，而是 `topic geolocation + resolution/layer scope + deterministic branch plan`。`xun` 模式下最短路就是“每个 branch 只留最近订阅者”，而不是再加随机网格。 |
| 新发现 | `CSG` 网络层过滤的真边界不该等到执行器。正确位置就是传输层上方的 `gen barrier`：按 `capability` 和 `unsafe_write` 这类图边直接拒绝危险子图。 |
| 新发现 | `空间证明` 的第一阶段正确口径不是伪造 GPS，而是 `witness -> route plan -> sector coverage -> RTT bucket ceiling` 的拓扑证明。只要 `3` 个独立 witness 和 `3` 个独立 sector 不成立，就该直接拒绝 claimed address。 |
| 新发现 | `msquic core: packet too small` 的真根不是 QUIC 协议本身，而是 `msquicConnImplQueueData(...)` 之前把整块 `Bytes` 直接塞成单个 `FrameData`，同时 `msquicFrameSize(...)` 还只按裸 payload 估长，连五段 varint 头都没算。只要 sync payload 稍大，首帧天然就塞不进 `maxPacketSize`。 |
| 新发现 | 这类问题不能靠放大 `maxPacketSize`、增加队列长度或失败后重试修补。正确结构是两层：先按真实 QUIC 编码长度求单帧大小，再按 `maxPacketSize - packet base` 精确切片。否则就算暂时躲过 `packet too small`，后面也会继续在 payload 编码和 wire size 上漂。 |
| 新发现 | 只在 `connection_impl` 做单帧切片还不够。`native_runtime` 之前一次 `pipeWrite(...)` 会把整个 payload 全压进本地 frame 队列，理论上还会继续撞 `msQuicMaxQueuedFrames=128`。正确上层模型是“计算当前队列理论可容纳字节数 -> 分批 queueData -> 每批立刻 flush+pump”。这不是兜底，而是发送面的真实流控结构。 |
| 新发现 | `stage0` 现在真正卡住 compiler source 的，不是语义图，也不是 LSMR runtime，而是 module-qualified overloaded routine 调用。`system.panic(...)` 在 `v2/src/compiler` 里一多，`cheng_v2c` 就会在 imported-field overload 解析上不稳定。最短路不是继续改解析器，而是给 `std/system` 一个非重载入口 `panicStr`，然后把 compiler source 全切过去。 |
| 新发现 | 这条 `panicStr` 路收掉以后，`compiler-core-release -> compiler-core-system-link -> compiler-core-system-link-exec -> tooling-selfhost -> selfhost -> full-selfhost` 已经重新前台贯通。说明前一轮新加的 LSMR runtime smoke 并没有打坏主线，真问题只是 compiler source 还留着不稳定的 module-qualified overload 调用。 |
| 新发现 | 这轮之后 `lsmr-contracts` 已经不是“算法层合同”，而是“算法层 + 两条真实 runtime smoke”的正式 gate。后面再谈 `cheng-quic`，不能再把 `chain_state_tree_sync_smoke/msquic_chain_smoke` 当手工样例，它们现在就是主线合同的一部分。 |
| 新发现 | `full-selfhost` 只能证明“编译器主体能编自己”，不能证明普通程序也由 Cheng 产物自己编自己跑。正确边界是单独补一条 `program-selfhost` gate，把 `lsmr_advanced_features_smoke`、`chain_state_tree_sync_smoke` 和 `chain_node balance/mint/balance` 全挂进去。 |
| 新发现 | `program-selfhost` 这条线里真正该卡死的不是“是否能调宿主 C 编译器”，而是 `compiler_core` 运行面是否还依赖外部 C provider。现在 stage1/stage2/stage3 都已经固定到 `external_cc_provider_count=0`，这才是普通程序自举开始成立的边界。 |
| 新发现 | `compare_expected_text_or_die(...)` 和 `compare_expected_binary_or_die(...)` 之前把同一缓冲同时传给 `resolve_existing_input_path(...)` 的 `abs_out/norm_out`，只要不在 repo root 下跑，就会把绝对路径覆盖回相对路径。这不是 contract 文本问题，而是 stage0 自己的路径解析活 bug。现在已改成分离 `expected_abs/expected_norm`。 |
| 新发现 | typed `record` 的 field lookup 之前虽然已经在运行时对象上存在，但 `zero_value_from_plan()` 和 `zero_record_shell_from_plan()` 还会每次重新 rebuild。正确做法是把 lookup 前移到 `TypeDecl/ZeroPlan`，然后在零值物化时直接复用。 |
| 新发现 | typed `record` 的共享 shape 不能只停在 `zero_value/zero_record_shell`。`MAKE_RECORD` 自己如果还在 rebuild record lookup、再按字段名回查 slot，热路径就会继续白白花掉固定布局收益。正确结构是直接按 `TypeDecl` 初始化 record，并在 op 字段顺序与 decl 一致时直接按 ordinal 写槽。 |
| 新发现 | 这轮把 `driver_c_prog_record_init_from_decl()` 接进 `MAKE_RECORD` 后，`compiler_core_system_link_exec/program_selfhost/full_selfhost` 已在当前 tree manifest `manifest_fnv1a64=fee6dad00582e08f` 下重新闭合。前台重编后的中位数是 `pubkey=1.1800s`、`sign=1.7100s`、`mul_xonly=1.3500s`、`kinv=0.7200s`。这说明结构继续收正了，但当前主根还没变：固定布局 `slot/shape` 和 aggregate `field/index update copy` 仍是最短路。 |
2026-04-10
- `src/tooling/cheng_tooling.cheng` 属于旧链，这件事必须在结构上体现：`v3` 不能继续把它当命令入口，只能把 seed binary 当外根，把控制面、合同、build plan 收回 `v3/src/tooling`。
- `std/os` 已经足够支撑 `v3` gate：真正该复用的是 `execFileCapture/execFileStatus/getEnvDefault/readFile/writeFile/fileExists/dirExists/createDir/joinPath/absolutePath/parentDir/walkDirRec`，不是旧 tooling 的整套命令分发壳。
- `v3` 新热路径扫描器不能把禁词字面量写回 `v3/src`，否则 gate 会先扫到自己。`hotpath_scan.cheng` 已改成分段拼接模式，`scan_forbidden_hotpath.sh` 重新通过。
- `v3/tooling/cheng_v3.sh` 现在已经是 `v3` 目录下的纯 Cheng launcher，但它还没到语义错误阶段，就先被现有 seed/backend driver `rc=223` 挡住；说明当前主阻塞仍是仓库全局编译根，不是新 `v3/src/tooling` 单点。
- `v3/tooling/cheng_v3.sh` 之前一直只剩一句 `compile gate_main failed`，真根不是 seed 没报错，而是外层把 stderr 重定向到了和 seed 内层 `out.compile.log` 同名的路径，seed 一进来就把这个文件删掉了。把外层日志改到 `cheng_v3_gate.seed.stderr.log` 后，`223` 和 seed 原始错误已经能稳定前台暴露。
- `strict bootstrap sidecar driver failed wrapper-source build` 这条一开始看起来像 `223`，但真相更细：proof wrapper/outer driver 旧链先把它拖成 timeout 黑盒。把 `verify_backend_sidecar_cheng_fresh.sh` 的失败口径改成 `rc/kind/hint/log` 之后，才能明确分清 `124 timeout` 和 `223 deterministic_exit_223`。
- `proof launcher` 之前无视上游已经明确传下来的 `BACKEND_UIR_SIDECAR_DISABLE=1`，会偷把默认 sidecar compiler 再塞回去；这会让 fresh bridge 的失败变成递归 timeout。现在它已经会尊重 `disable=1`。
- 真正把失败从 timeout 拉回确定性 `223` 的关键，不是再调 timeout，而是去掉 `backend_driver_currentsrc_sidecar_wrapper.sh` 对 bootstrap proof surface 的自动 `wrapper_preserve_sidecar=1`。这层一拿掉，`bootstrap_bridge_v3` 立刻稳定进入 `rc=223 kind=deterministic_exit_223`，并把真正的下一处活根暴露成 `backend_driver sidecar: missing strict sidecar mode contract`。
- `v3` 的设计文档不能再混在根 README 或 tooling README 里。正确结构是单独建 `v3/docs`，README 只做入口和摘要，长文计划、硬禁令、踩坑清单都收进 `v3/docs`。
- 这轮文档化之后，`v2` 已证明过的坑已经收敛成三组最关键禁令：一是不能再把 `fixed point/full-selfhost` 冒充完全自举，二是不能再让字符串壳、通用 `BigInt/Bytes/Seq`、解释执行回到 `program` 热链，三是不能再让 bootstrap/sidecar 失败保持黑盒。
- 把根目录 `artifacts/chengcache/build/dist`、根旧二进制和 `v2/artifacts` 全部清空后，`v3` 的 clean bootstrap 真实缺口已经露出来了：不是 `223`，也不是 sidecar timeout，而是根本没有新的 `probe_currentsrc_proof/cheng_stage0_currentsrc.proof`。旧 bridge driver 之前确实在遮挡真实状态。
- `v2` clean seed 现在只能稳定重建出 `v2/artifacts/bootstrap/cheng_v2_bootstrap` 和 `v2/artifacts/bootstrap/cheng_v2c`；继续跑 `tooling-selfhost-host` 仍然会卡 `release stdout mismatch`，说明旧 `v2` tooling 自举合同本身已经和当前仓库漂移，不是缓存问题。
- 继续手拆 `tooling_stage1_bootstrap` 时，stage0 会直接报 `program_entry_exec_plan_missing ... local_payload_*`。这说明 `v2` 老 bootstrap 真正的结构性断点就是 `local_payload/exec_plan` 这一代设计，而不是路径、权限或 seed 二进制脏状态。
- `v3` 的 C 基线已经能在纯净环境下稳定重编，但本次 clean run 相比 frozen baseline 普遍慢了约 `7%` 到 `28%`。这次口径是真实 clean run，不是旧缓存热态结果，后续所有 `v3` 性能判断都应该以这条干净口径为准。
- `LSMR` 在 `v3` 里可以直接迁算法语义和 smoke，不可以直接搬 `v2` 实现壳。`v2/cheng-quic/src/chain/{lsmr,pubsub,csg,location_proof,anti_entropy,dispersal}.cheng` 普遍依赖 `str[]`、canonical text 和 `chainCidFromText(...)`，这和 `v3` 已写死的二进制固定布局链数据面正面冲突。
- `v3` 当前链地址语义还有一个必须先修的硬冲突：现有 [binary_types.cheng](/Users/lbcheng/cheng-lang/v3/src/chain/binary_types.cheng) 和 [codec_binary.cheng](/Users/lbcheng/cheng-lang/v3/src/chain/codec_binary.cheng) 把 LSMR digit 约束成 `0..7`，但 `LSMR` 文档和 `v2` 实现使用的是洛书 `1..9` 且 `5` 为中心。不先收正，后续路由、多播树、前缀树、位置证明都会迁错语义。 
- `v3` 的 bootstrap/tooling 现在已经能完全绕开旧 `probe_currentsrc_proof/sidecar/tooling_cmd` 主线：`bootstrap_bridge_v3.sh` 直接用 [cheng_v3_seed.c](/Users/lbcheng/cheng-lang/v3/bootstrap/cheng_v3_seed.c) 编出 `stage0`，再真跑到 `stage1/stage2/stage3`；`build_backend_driver_v3.sh` 直接用 `stage2` 物化出 [artifacts/v3_backend_driver/cheng](/Users/lbcheng/cheng-lang/artifacts/v3_backend_driver/cheng)；`run_slice_gate.sh` 也已经前台通过。 |
- Darwin 下不能把 `stage2 == stage3` 简化成“可执行字节完全相等”。这轮实测过，给 Mach-O 强行加 `-Wl,-no_uuid` 虽然能去掉链接器的随机 UUID，但产物会被 `dyld` 直接拒绝，报 `missing LC_UUID load command`。所以当前可执行的正确 fixed-point 口径是：`stage2/stage3 self-check` 一致，且 `print-contract` 输出逐字一致。 |
- `run_slice_gate.sh` 里哪怕只剩一段“如果旧 `artifacts/tooling_cmd/cheng_tooling` 存在就顺手跑个 smoke”的壳，也会把 `v3` 新主线重新拖回旧链。这轮删掉那段以后，gate 才真正只剩 `v3/bootstrap` 主线。 |
- `v3` 里最后一批结构漂移不在 shell 脚本，而在活源码和 README：`gate_main.cheng` 之前还默认找 `artifacts/tooling_cmd/cheng_tooling`，README 也还在教人走旧入口。只要这些残留还在，后续任何人补一段 compile path，都很容易把 `v3` 又拽回旧链。 |
- `gate_main` 的 `run-smokes` 不能再假装存在“完整 Cheng 编译面”。当前真能稳定验收的 smoke 是 `stage0/stage1/stage2/stage3` 对同一份 bootstrap 子集源码的 `self-check` 和 `print-contract` 一致性，所以源码入口也必须收成这个事实。 |
- `v3/bootstrap/stage1_bootstrap.cheng` 和 `v3/bootstrap/cheng_v3_seed.c` 里的禁词如果继续写具体旧 proof 路径名，本身就会把旧链词汇永久保存在 `v3` 活代码里。更稳的做法是改成泛化的 `legacy_proof_surface/legacy_sidecar_mode`，既保留禁令语义，也不再把旧实现名嵌回新根。 |
- `v3/cheng-package.toml` 缺 `module_prefix = "v3"` 时，host compiler 会把 `v3` 包导入解析错成“声明 owner 不匹配”；这不是测试写法问题，而是 package manifest 根没写完整。加上后 `cheng_v2c system-link-exec --root v3 ...` 才能真编 `v3/src/tests/*`。 |
- `v3` 现在最短路已经不是“继续补旧 proof/tooling”，而是“收 host runtime 对 `v3` test/chain 模块的 zero-init/type-decl 闭环”。`run_v3_host_smokes.sh` 已固定证明：`fixed_surface/csg/consensus/pubsub/location_proof` 都能真跑，正式 gate 当前唯一失败位是 `chain_codec_binary_smoke -> missing type decl for zero init type=int32 inst_owner=v3/tests/chain_codec_binary_smoke`。 |
- `chain_codec_binary_smoke` 这轮已经从 `V3FrameHeader/V3StateRootSummary` 的 imported-record runtime 缺口推进到了更底层的 `int32 zero-init` 缺口，说明 codec 自身的跨模块 record 依赖已经基本被挤干净，下一刀不该再回头改 frame 语义，而该直接修宿主 runtime。 |
- `anti_entropy` 也已经被逼到同一层宿主缺口：现在不是 `anti_entropy` 规则错，也不是 `lsmr` 数学错，而是 `driver_c program runtime` 还没闭合 `v3/chain/lsmr_types` 这条 zero-init/type-decl 路。 |
- `csg/pubsub/location_proof/consensus` 这 4 条线已经证明当前 `v3` fixed-layout 语义核是可落地的，而且可以在同一套 host compiler/runtime 下真编真跑通过。也就是说，剩下的阻塞不是“v3 链方案太激进”，而是宿主运行时还有一段类型零初始化没接完。 |
- `parser_path`、`runtime/provider targets`、`lowering inventory` 这些之前挡在 ordinary compile 前面的假阻塞已经收掉了；当前 `program_selfhost` 和 `chain_node` 会统一走到 `object/native-link` 之后才停，说明主根已经被推进到更后面。 |
- `object/native-link` 这一层如果继续写 `kind: str` 会被 `scan_forbidden_hotpath.sh` 当场打回。`native_link_plan.outputKind` 这轮已经从字符串改成枚举，说明 `v3` 热路径约束需要一直在代码层硬守，不是文档口头要求。 |
- 当前宿主 `cheng_v2c` 对复杂 builder 函数和大 record 构造器还有执行面老洞，所以 `v3` host smoke 不能硬拿它去跑完整 `system_link_exec` 总 builder。最稳做法是把 host smoke 收成“宿主真能稳定执行的最小类型面”，而把完整 ordinary pipeline 留给 seed/backend-driver 真路径去验。 |
- `v3` 当前普通编译主链已经稳定能给出 `primary_object_path/provider_object_paths/object_link_inputs/native_link_inputs`，并且 `build_program_selfhost_v3.sh` 与 `build_chain_node_v3.sh` 会统一报 `object and native link plans ready, machine code emission and final link not implemented`。这说明下一刀不该再补 lowering/report stub，而该直接落 `primary object` 的机器码和最终链接。 |
- 同机 C 基线这轮重新跑后，`x25519/p256_pubkey/p256_sign/p256_verify/chain_encode` 都在 `0.985x~1.032x` 摆动，像正常波动；但 `sha256` 直接飘到 `1.148x`，已经超过自然抖动，后面需要单独追这条基线，而不是把它和 ordinary compile 主阻塞混在一起。 |
- `v3` ordinary 最小主链这轮已经越过“只能 `return 0`”这一关。新增 `ordinary_call_chain_fixture.cheng` 后，`build_zero_exit_v3.sh` 和 `build_call_chain_v3.sh` 都已真编、真链、真跑通过，说明当前 seed 至少已经能稳定处理 `return_zero_i32 + return_call_noarg_i32` 这两类 body。 |
- ordinary entry bridge 之前真正的运行时死根不是“递归跳错”，而是 `bl callee; ret` 覆盖了 `LR`。入口桥是 tail-call 形态时必须直接发 `b callee`；否则用户函数返回后会直接回到桥里的 `ret` 自旋。这条已经在 `v3/bootstrap/cheng_v3_seed.c` 收掉，并用全新 seed 二进制前台验证过。 |
- `program_selfhost_smoke` 和 `chain_node_smoke` 现在已经都能在 host smoke 主线上前台跑绿，但 ordinary compiler 直接真编 `program_selfhost` 仍稳定死在 `primary object body semantics missing`。这说明当前真断点已经收窄到“普通函数体语义子集不够”，不是 `.o/link/argv bridge`。 |
- `program_selfhost` 的下一批最小语义不是循环，也不是链算法，而是“有参调用 + named record 构造/返回 + 字段/嵌套字段读取 + if 提前返回 + assert/echo”。`chain_node` 则是在这之后再补 `Result[T]`、记录写回、循环和序列索引。后面不该回头再补入口壳。 |
- 这轮把 ordinary lowering 改成按 entry 可达函数裁剪后，`program_selfhost` 已从 `598` 个函数压到 `16` 个，只剩 `program_selfhost_smoke -> bootstrap_contracts -> path` 的真实最短链；`chain_node` 也从 `1120` 个函数压到 `112` 个，失败面终于收敛到主路径，而不是整包导入噪音。 |
- reachability 裁剪生效后，`program_selfhost` 的 compile log 已经不再带 `primary_object_call_target_missing`，只剩 `primary_object_body_semantics_missing`。这说明下一刀不用再查 call inventory，而该直接补 `let + named record + 字段读取 + if + assert/echo` 这组 body 语义。 |
- `chain_node` 在同一轮裁剪后仍保留一个 `primary_object_call_target_missing`，说明它现在暴露的已经是可达主链上的真实缺口，不是整仓 inventory 泄洪。后续要按 `chain_node_main -> chain_node -> consensus/lsmr/anti_entropy/fixed256` 这条顺序补，不该回头重扫所有导入模块。 |
- `program_selfhost` 之前那次 `rc=139` 不是新的语义 bug，而是 consteval 自己把栈打爆了。`v3_const_eval_expr` 里的 `args[32][4096]` 和 `v3_const_eval_function` 里的 `V3ImportAlias aliases[...]` 都太大，ARM64 Darwin 会直接在 `___chkstk_darwin` 处爆掉。正确修法就是改堆对象，不是调 timeout。 |
- consteval 一开始没能吃下 `v3BootstrapArtifactNew`，真根不是 record/if/assert 语义，而是函数签名边界错了两次：一是 `v3_load_function_source_lines` 边读文件边找多行签名，第二行还没进来就提前失败；二是 `collect_lowering_functions_from_source` 先用 `v3_lowering_function_name_from_line(...)` 原地把源码行截成了 `fn main`，后面的签名采集自然再也找不到 `=`。 |
- `program_selfhost` 真编真链后之所以还挂住，不是 shell 卡住，也不是 `_puts` 卡住，而是 consteval 生成的函数在 `bl _puts` 之后没保存 `x30/lr`。LLDB 直接证明了 `pc` 和 `lr` 都停在 `mov w0,#0` 那一拍；`ret` 会回到自己，形成死循环。 |
- `build_program_selfhost_v3.sh` 现在已经真通过，说明当前 `v3` ordinary compile 至少已经闭合了这 6 组最小句型：`return literal/module.fn(...)`、`len(callee(...)) > 0`、`let + if 早退 + 条件返回 + joinPath`、单层记录构造返回、多层嵌套记录构造返回、入口顺序 `let/assert/echo/return`。下一刀不该再回头补这组。 |
- `chain_node` 之前剩下的 `primary_object_call_target_missing` 不是主算法错，而是 no-arg tail-call 的 callee 解析方式错了：按“同模块裸函数名”回找太脆。直接复用 lowering 已经采集好的完整 `callee_symbols[0]` 后，这个假阻塞已经消失。 |
- 现在 `run_slice_gate.sh` 的真实失败位已经收得很干净：`scan`、同机 C baseline、bootstrap bridge、backend driver、zero-exit、call-chain、program-selfhost、host smokes 全都过了，只剩 `chain_node` 的 `primary_object_body_semantics_missing`。这说明下一刀该正面补 `chain_node_main -> chain_node -> consensus/lsmr/anti_entropy/fixed256` 的函数体语义，不该再查 bootstrap、runtime target、call graph 或 linker。 |
- 之前 `v3` seed 的 bare import 调用解析会把 `bytesAlloc/bytesLen/bytesSet/intToStr/rawmemCopy` 这类裸调用默认记成“当前模块::函数名”。这会让 reachable 图系统性漏边，导致 `chain_node` 的 `lowering_function_count` 和 unsupported 前沿都偏瘦，属于典型假绿源头。 |
- 把 callee 收集改成“两阶段”以后，`build_chain_node_v3.sh` 的 `lowering_function_count` 已经从 `110` 变成 `126`。新增出现的 `std/system::str*`、`std/rawbytes::*`、`std/strings::intToStr`、`std/rawmem_support::*` 说明先前不是这些函数“不需要”，而是它们被漏边藏掉了。 |
- `CHENG_V3_MAX_UNSUPPORTED_DETAILS=8` 会把 `chain_node` 的 ordinary 主根压成一层假前沿。放大到 `64` 之后，真实 unsupported 现在已经同时覆盖 `std/system`、`rawbytes`、`bytes_layout`、`anti_entropy`、`lsmr`、`sha256`。也就是说，当前真问题不是“先补 8 个函数就通”，而是普通函数体子集还差一整组基础能力。 |
- 现在 `build_chain_node_v3.sh` 的第一层真实能力缺口已经很清楚：不仅要补 `echo/panic/byteBuf*`，还要系统性补 `return expr`、记录构造返回、`var/let` 本地绑定、字段写回、`if/while/for`、数组/Bytes 读写、整型算术和比较。再围着旧的 8 条 unsupported 做点状修补，只会继续走偏。 |
- `build_backend_driver_v3.sh` 本身不会自动吃到最新 seed 改动，它只会拿现有 `artifacts/v3_bootstrap/cheng.stage2` 再物化一遍 backend driver。所以凡是改了 [cheng_v3_seed.c](/Users/lbcheng/cheng-lang/v3/bootstrap/cheng_v3_seed.c)，都必须先重跑 [bootstrap_bridge_v3.sh](/Users/lbcheng/cheng-lang/v3/tooling/bootstrap_bridge_v3.sh)，再重跑 [build_backend_driver_v3.sh](/Users/lbcheng/cheng-lang/v3/tooling/build_backend_driver_v3.sh)，否则会继续看到旧行为。 |
- 把 seed 变更重新灌进 `stage0~stage3` 之后，`chain_node` 的 `primary_object_unsupported_body_kinds` 已经从全 `unsupported` 变成真实句型族：`stmt_call`、`stmt_var`、`stmt_let`、`stmt_if`、`stmt_for`、`return_expr`。这说明 ordinary lowering 现在最该补的是句型能力，不再是“继续猜函数名”。 |
- 当前 compile log 已经证明 `v3ChainNodeMainPrint -> echo(text)` 是 `stmt_call`，`byteSpanFromBytes` 是 `return_expr`，`byteBufInit` 是 `stmt_var`，`byteBufEnsure` 是 `stmt_if`，`fixedBytes32Equal` 和多处 LSMR/consensus 搜索函数是 `stmt_for`。这个分布已经给出了正确实现顺序：先打 `stmt_call + return_expr`，再打 `stmt_var/stmt_let + if`，最后打 `for/while`。 |
- 只看 `body_kind` 还不够，`chain_node` 现在真正的第二维阻塞是 ABI 形态。把参数名、参数类型、返回类型和 ABI 分类写进 compile log 以后，当前前 64 个 unsupported 已经明确分成两层：`int32/int64/bool/ptr` 这层可以先打，`str/Result/record/array/Bytes/ByteBuf/FixedBytes32/V3LsmrAddress` 这层是后面的 composite 硬区。 |
- 这轮把 [chain_node_main.cheng](/Users/lbcheng/cheng-lang/v3/src/project/chain_node_main.cheng) 里的 `v3ChainNodeMainPrint` 空包装删掉之后，`build_chain_node_v3.sh` 的首个 blocker 已经从 `stmt_call + str/composite` 前移到 `v3ChainNodeMainSelfTest` 的 `stmt_var` 主体。这说明继续围着 `echo(text)` 这种薄包装打补丁已经没有意义，下一刀必须正面补 `stmt_var/let/if` 和 composite 内部语义。 |
- `panicStr` 在 `v3` 当前 ordinary compile 里只是多余薄包装，不是独立语义面。[fixed256.cheng](/Users/lbcheng/cheng-lang/v3/src/std/crypto/fixed256.cheng) 把它收成 `panic` 之后，reachable 集和 blocker 都更干净，后面应继续优先删这种无信息增益的包装层。 |
- `chain_node` 这轮的第一刀已经证明：真正把裸类型变成 `caller_module::Type` 的地方不是 call 本身，而是 caller 侧再次归一化。只要 internal callee 的 `param_types/return_type` 没按 callee 模块先 canonicalize，`V3ChainNode` 这类跨模块复合返回值就一定会再次串味。现在这条已经在 `v3/bootstrap/cheng_v3_seed.c` 收掉。 |
- `v3` 后面的复合 ABI 不该按 `Bytes/str/FixedBytes32` 各拆一套。当前 seed 已经把所有非 `i32/i64/bool/ptr` 值都统一收成 `composite`，而且已有规范布局计算；最短正确路就是“复合返回统一 sret，复合参数统一单寄存器指针，caller 统一先准备可取址存储再传地址”。 |
- 当前新的最前沿不是 `V3ChainNode` 落槽，也不是 composite param 总拒绝，而是标量调用链里最后一个复合实参漏口：`std/crypto/sha256::getU32BE` 的 `int64(bytesGet(data, offset))`。它说明 `Bytes/str` 单层字段读取和复合参数入口已经开始生效，下一刀该补的是“标量调用 + 复合实参”的剩余一层，而不是回头再修旧 ABI 口子。 |
- bare `assert/echo` 不能走 ordinary 的普通调用解析。它们在 surface 上本来就是 intrinsic/重载名，继续硬塞进 bare resolver 只会撞多定义。最短正确路是直接在 ordinary emitter 里做语句级 lowering：`assert(cond, "msg") -> if !cond { puts(msg); exit(1) }`，`echo("msg") -> puts(msg)`。这条已经在 `v3/bootstrap/cheng_v3_seed.c` 落地。 |
- ordinary 调用之前“边算边塞 `x0..x7`”是错的。只要后一个参数求值里再发生调用，前一个参数就会被冲掉。现在 `v3_codegen_call_scalar` 和 `v3_codegen_call_into_slot` 都改成了“按 `call_depth` 先落到调用暂存区，再统一装参”，这不是优化，是正确性前提。 |
- `composite-arg local missing` 的真根不是 ABI 规则不清，而是 ordinary emitter 以前只认“裸 local/param 名字”能取地址，不会先把复合临时表达式 materialize 成可取址槽位。现在这条已经补上，`v3AntiEntropySignatureCid(local)`、`layout.fixedBytes32ToBytes(value)` 这类复合临时实参都能先落槽再传地址。 |
- 这轮把 `assert` 和复合临时实参收掉以后，`build_chain_node_v3.sh` 还是失败，但 compile log 已经不再有 `callee=assert` 或 `composite-arg local missing`。说明当前真主根已经换层了：不是调用边和临时地址，而是更深的普通语义面，主要是 `sret/composite return function` 和 `composite field projection`。 |
- 现在 compile log 里大片 unsupported 函数都带 `ret=.../composite`，例如 `std/system::strEmpty`、`byteBufInit`、`fixedBytes32Zero`、`v3ChainNodeInit`。这说明 `v3_try_emit_scalar_function` 还把“复合返回函数”整体挡在 ordinary emitter 外面；不先补 `sret`，`chain_node` 不可能闭合。 |
- `chain_node_main` 里下一个高概率真 blocker不是 `var server = ...` 本身，而是 `mintCidRes.value`、`served.tipEventCid`、`served.forest.forestCid` 这类复合字段投影。当前 ordinary 只支持单层标量字段读取，复合字段还没有直接传址或 materialize 路。 |
- `v3` 里的复合返回不该继续伪装成“把隐藏返回地址塞进 `x0` 再把用户参数整体右移”。当前 ordinary 主链已经收正成 `x8 sret`：caller 直接把目标地址装进 `x8`，callee 进函数先 spill `x8`，用户参数继续占 `x0..x7`。这条既更接近标准 AArch64 调用约定，也能让 call spill 布局继续维持固定 `64` 字节，不用自毁参数区。 |
- 复合 ABI 这轮真正打通的不是某一个函数，而是一组统一数据面：无初始化 `var/let` 声明、普通赋值语句、复合 return、字段路径读写和 `Result[T]` 字段布局。现在 ordinary emitter 已经能处理 `local = expr`、`local.field = expr`、`return local`、`return Constructor(...)`，还补上了 `Result[T].ok/value/err` 和 `str.store_id` 的字段解析。 |
- `build_chain_node_v3.sh` 这轮之后的 `first unsupported function` 已经稳定前移到 `std/system::strViewWithFlags body_kind=stmt_if`。这说明 `sret/composite return/field assign` 这一层已经不是主根，当前真正的下一堵墙是控制流；继续围着复合 ABI 打补丁，收益会明显变低。 |
- 现在 compile log 里仍然残留几类未收口点，但它们已经降到 `if` 后面：一是 `bytesFromString(tag)` 这类“复合返回值作为复合参数”的临时槽位仍有漏口；二是 `[assetId, accountId]` 这种非空序列字面量还没进入 ordinary emitter；三是 `intToStr -> strFromCStringBorrow(...)` 这类复合前缀调用还有一处返回路径没闭合。它们都是真问题，但都不再是当前最前沿。 |
- 这轮把 `base[index]` 正式接进 seed 之后，`chain_node` compile log 里已经不再出现 `index.accountHeads[pos]`、`index.balances[pos]`、`lt.v3AppendFixed32(buf, index.accountHeads[pos])` 这类索引读值/复合返回错误。说明当前 `Bytes/str/T[]/T[N]` 的索引读值、复合取址和 indexed lvalue 写回已经进入 `stage2/stage3` 主链。 |
- `chain_node` 现在新的第一批真阻塞已经很清楚：`bytesFromString(...)` 作为 `layout.byteBufAppendBytes(...)` 的复合临时实参仍然没有稳定临时槽位；`lt.v3HashInts("...", [assetId, accountId])` 说明非空序列字面量还没进入 ordinary emitter；`add(seq,val)` 则已经不是“只有 resolver 错”，因为 `std/seqs::{add,setLen}` 自己也还在 unsupported 列表里。 |
- 这意味着下一刀不该再回头补索引或字段路径，而该正面补三件事：复合临时实参的严格数据面、非空序列字面量、以及 `std/seqs` 自身 ordinary 语义。谁先收，谁就会继续把 `chain_node` 从 `bytes_layout/lsmr_types/consensus` 这层往后推。 |
- 只把 `driver_c` fatal 接到原生 `backtrace()` 还不够。解释执行时 C 栈只会看到 `driver_c_prog_eval_item + offset`，看不到是哪一个 `.cheng` 函数炸了；所以必须把 `DriverCProgFrame` 串成 caller 链，在 `driver_c_die(...)` 里先打 `[driver_c stack] module/label/source/pc/op`，再补原生栈。现在 `ffi_importc_handle_annotated_i32.cheng` 的 `unsupported callee` 已经能直接给出 `main -> call pc=4`。 |
- 当前 Darwin 环境里 `ordinary_signal_trace_fixture` 会进入 `UE` 状态，`build_signal_trace_v3.sh` 卡在 `wait`，连外部 `kill -SEGV/-KILL` 都不立即收掉进程。这条和“默认出栈”不是一回事；`panic` 已经证明默认 trace/bt 正常，`signal` 这条要单独按 fixture/宿主进程状态拆。 |
- `signal` 用例之前查不到 Cheng 源码帧，不是行号树失效，而是 fixture 写成了尾调用：`return cheng_force_segv()` 会发 `b _cheng_force_segv`，直接把 `main` 栈帧吃掉。把它改成普通调用 `let rc = cheng_force_segv(); return rc` 以后，signal handler 的 frame-pointer unwind 立刻能反查到 `ordinary_signal_trace_fixture.cheng:4-5`。 |
- ordinary `importc fn` 不能只补到通用 `v3_resolve_call_target(...)`。`return_call_noarg_i32` 这条快路径会绕开通用 resolver，所以同文件 `importc fn cheng_force_segv(): int32` 依旧会报 `primary object call target missing`。正确修法就是把尾调用快路径也接回统一 resolver，而不是继续堆特判。 |
- `stage2` 之前的 external `var` 实参有一处硬错：地址先按 `ptr` spill，回装寄存器时却按 `target.param_abi_classes[i]` reload。对 `var int32` 这种 C out-param，会把 64 位地址按 `i32` 截断，真机直接在 callee 里 `SIGSEGV`。正确修法不是改 wrapper，而是 call-lowering 在 `param_is_var` 上统一按 `ptr` reload。 |
- ordinary caller 之前允许 `target.return_abi_class=i32` 喂给 `expected_abi=i64`，但收返回值时只做了 `mov xN, x0`，没做 `sxtw`。结果所有负错误码都会从 `-1` 变成 `4294967295`，最典型就是 generational handle stale get/release 在 `v3` 里永远比不过 `invalidCode()`。这必须在 call-lowering 层修，而不是在测试里绕。 |
- `v3` 目前直接跨模块调用 `importc fn` 还不是标准面，因为 ordinary resolver 只会在“当前源码文件”里扫描 `importc fn` 声明。要让其他模块稳定用 FFI，必须像这轮一样提供同模块 wrapper `fn`，再由 wrapper 去命中同文件 `importc fn`。 |
- `BFT-SMI` 这轮已经把“是不是 `consensus` 算法坏了”彻底排除掉了：`/Users/lbcheng/cheng-lang/artifacts/v3_tmp_bft_apply_single_smoke` 和 `/Users/lbcheng/cheng-lang/artifacts/v3_tmp_bft_apply_to_index_smoke` 都稳定输出 `ok`。这说明 `mint -> consensusApplyChecked -> index/event log` 主链是通的。 |
- `BFT` 现在真正的根在 `CheckTx` 预演链。`/Users/lbcheng/cheng-lang/artifacts/v3_tmp_bft_finalize_summary_smoke` 只要补上和 self-test 一样的 `CheckTx` 前导，就会稳定失败在 `v3 bft finalize summary: applied count`；而不补前导时，症状更接近后续 `machine` 写回口径。说明真正被污染的是“预演之后的后续 finalize 视图”，不是单次 authoritative apply。 |
- 这也解释了为什么 `/Users/lbcheng/cheng-lang/artifacts/v3_tmp_bft_state_machine_smoke` 一直停在 `v3 bft self-test: block1 applied count`：`block1` 之前唯一新增的高风险动作就是 `v3BftStateMachineCheckTx(...)` 和 `v3BftStateMachineCheckTxBytes(...)`。继续围着 `FinalizeBatchSummary` 自身打补丁，收益已经明显变低。 |
- `bytesFromString(\"...\")` 直接塞给复合参数调用在当前 seed 里还会触发 `direct str-arg scratch overflow`。把它改成 `let prefix = bytesFromString(\"...\"); byteBufAppendBytes(buf, prefix)` 之后，`bft_finalize_summary_smoke` 才重新回到可 fresh 编译状态。这不是测试偶发现象，而是 ordinary lowering 目前对“直接字符串字面量 -> 复合值 -> 复合参数”这条链还不稳。 |
- `run_v3_host_smokes.sh` 这轮的首个 host blocker 已经不在 crypto/BFT 这边，而是稳定前移到 `parser_path_smoke`。这说明热核和当前 `BFT` bring-up 的前置宿主面已经基本够用了，后面该并行盯 `parser path` 与 `CheckTx` 预演链。 |
- `lowering_plan_smoke` 这次真正的崩点不是 `@importc` 过滤本身，也不是 `v3LoweringFunctionSymbolText` 这一个函数名；根在 seed 里 `str +` 的 lowering 走了“裸 `char*` 拼接 + 手写回 `str`”这套脆弱路径。只要碰上嵌套拼接、slice 出来的局部字符串或更深的 call-depth，`str` 布局和 ABI 就很容易被写坏。把它收正成 `driver_c_str_concat_bridge(ChengStrBridge, ChengStrBridge) -> ChengStrBridge` 之后，`lowering_plan_smoke`、`lowering_line_local_smoke`、`compiler_pipeline_stub_smoke` 全部重新稳定通过。 |
- `v3PassiveAntiEntropySyncRequired(...)` 这种“只包一层 `!signatureEqual(...)`”的布尔包装在当前 ordinary/host 主线里不值得信任。把它改成显式 `if equal -> false else -> true` 之后，`chain_node` 的“second sync idle”假失败才被剥掉，说明这种一层 negation wrapper 以后要尽量少留在热路径判断里。 |
- `chain_node` 二次同步真正的机器级坏点不是反熵语义，而是 `LSMR` 树构建里对嵌套 `seq` 字段原地 grow。LLDB 已经把崩溃钉到 [lsmr.cheng](/Users/lbcheng/cheng-lang/v3/src/chain/lsmr.cheng) 的 `entryKeyIds` grow 上；所以后续凡是类似 `tree/forest/index` 这种大复合结构，优先用“局部值构建 -> 一次性赋回”，不要在 `var out.fieldSeq` 上边 grow 边写。 |
- `anti_entropy`、`lsmr_locality_storage`、`lsmr_bagua_prefix_tree` 这几条 smoke 之前失败，不是算法错，而是测试自己把 `AddressFromSeed/LocalityHashText/StateCellFromText` 的文本壳带进了 ordinary 主线。现在改成 fixed hash、直接 digits、直接 `StateCellFill` 以后全部通过，说明 `v3` host smoke 以后要坚持“只测 fixed-layout 数据面，不顺带测文本兼容层”。 |
- `chain_node_libp2p_smoke` 现在的剩余问题已经不是 ordinary compile。最新 targeted 结果说明它已经能完整编译和链接，真正失败点前移到运行期 `decodeSnapshot`。也就是说，下一刀该查的是 [chain_node_libp2p.cheng](/Users/lbcheng/cheng-lang/v3/src/project/chain_node_libp2p.cheng) 里的快照编解码一致性，不要再把它误判成 host compiler 句型缺口。 |
- `v3_parse_fixed_array_type(...)` 之前把 `int32[]` 误判成了 `int32[0]`。这个 bug 平时不容易一眼看出来，因为 `var xs: int32[]` 默认零初始化还能过，但一旦把 `[1,2,3]` 物化到推导出来的 `int32[]`，lowering 就会先走 fixed-array 分支，再被“长度必须等于 0”卡死。`var zs = [1,2,3]` 这条假失败的真根就在这里。 |
- `list literal -> T[N]` 真闭环不能只验声明初始化，参数和返回也要一起验。现在 [default_init_literals_smoke.cheng](/Users/lbcheng/cheng-lang/v3/src/tests/default_init_literals_smoke.cheng) 已经把 `sumFixed([1,2,3])` 和 `return [7,8,9]` 都钉进去，能直接防住“声明能过、参数或返回又掉回去”的回归。 |
- `if/ternary` 的 fixed-array 假失败，根也不在 list literal 物化本体，而在 [cheng_v3_seed.c](/Users/lbcheng/cheng-lang/v3/bootstrap/cheng_v3_seed.c) 的 `v3_materialize_composite_expr_into_address(...)` 入口预判：它先拿无上下文 `v3_infer_expr_type(...)` 比较两边分支是否“类型完全相等”，而 `[1,2,3]` 在无上下文下永远只会被看成 `T[]`。所以 `var xs: T[N] = if ...` 和 `return if ...` 之前会被挡在真正 materialize 之前。 |
- `if/ternary` 的参数面还有第二层坑：即使绑定/返回已经能按目标类型落地，`sumFixed(if ... )` 这类调用如果先让 `v3_prepare_composite_call_arg_temp(...)` 自己推导临时槽类型，还是会把槽位开成 `T[]`，运行期再把错布局的值按 `T[N]` 传进去，表面上就只剩断言失败。正确修法就是在“目标参数类型已知且是 composite”时，让 `if/ternary` 实参的临时槽直接按目标类型开。 |
- `chain_node_libp2p_smoke` 这条并行核对到的当前状态已经是绿的：host、`stage2`、`stage3` 三条线现在都直接输出 `v3 chain_node_libp2p_smoke ok`。所以旧的 `decodeSnapshot` 失败结论要当历史故障看，不能再把它当作当前 blocker。 |
- 模块级 `var g: T[N] = ...` 这条之前缺的不是 fixed-array 物化，而是“全局初始化执行阶段”本身。`v3_append_module_global_storage(...)` 只会分配 `.space`，入口桥也只 `b entry`；只要这两点不改，`[...]`、`if ...`、`cond ? ... : ...` 放在顶层 `var` 上都会掉成零内存。正确修法就是补 `__v3_module_init` 真执行表达式，再在 exe bridge 里先 `bl module_init`，不能用“字面量静态写 `.word`、分支初始化另算”这种分裂语义的做法。 |
- 给 entry bridge 增加 `bl module_init` 时，不能直接在原来的 `b entry` 前插调用。桥本来靠保留调用者传下来的 `x30`，让 `entry` 的 `ret` 直接回 runtime；一旦先 `bl` 而不保存恢复 `x30`，`entry` 返回地址就会被改写到桥内部，运行期会跳回错误位置。正确形态必须是“桥先保存 `x30`，调完所有 module init 后恢复，再 `b entry`”。 |
- 模块 init 接上以后，还要把“顶层 `var` 初始化表达式里的函数调用”接进 reachable/lowering。否则像 `std/crypto/ed25519/ref10::initCurveOrder()` 这种只被全局初始化引用、从 `main` 本身不可达的函数，虽然源码存在，`module_init` 真发码时还是会因为找不到 callee 而失败。正确修法不是给 `module_init` 特判裸名字，而是让顶层 `var` 初始化表达式也参与 reachable seed。 |
- `Wrap(xs: if ... )` 现在暴露的是另一条独立洞：同模块 record 类型布局/复合返回链还没稳，不属于这轮模块初始化根因。把它和模块级 global init 混在一个 smoke 里，只会让验收结果失真。 |
- `libp2p_quic_tls_smoke` 这轮并行核对后，`unresolved_import_count=1` 的唯一真名已经坐实是 `core/option`，不是文件缺失。根在 `v3` 普通编译的模块解析没把 `core/` 前缀映射到 `<workspace>/src/core/*.cheng`；补上以后，这条假红会直接消失。
- `std/system::strToCStringTemp` 的第一层真阻塞不是函数逻辑本身，而是 `v3_codegen_compare_expr_scalar(...)` 把 `strDataPtr(s) == nil` 两侧错按 `i64` 发射。把 `ptr == nil` 收成按 `ptr/x` 比较以后，QUIC/TLS 定向编译已经能越过这层，首个真缺口前移到 `bitandCompat(s.flags, chengStrFlagOwned) != 0`。
- 继续把“窄标量比较”也改成按 `w` 路发射时，fresh `stage1/2/3` 在 `libp2p_quic_tls_smoke` 上会直接崩进 `libsystem_c::__vfprintf`。这说明当前 self-host compiler 还存在更深一层“unsupported/诊断打印链”稳定性问题；在没把这条栈钉死前，不能再盲推 compare lowering。
- 同一份 fresh `cheng.stage2` 仍能稳定编过 `libp2p_host_smoke`，所以当前崩溃不是整条自举链都坏了，而是 QUIC/TLS 深闭包特有的 compile-time 崩点。下一刀该直接查 self-host compiler 在大 unsupported 集合上的诊断字符串/明细收集，而不是再回头查 `core/option` 或 `ptr == nil`。 
- `libp2p_quic_tls_smoke` 这轮继续前推后，`std/os` 真 blocker 已经被清空。`openImpl*` 的 `str mode` 局部绑定和 `udpRecvFromFdAddrEx(...)` 的 `addrLen`/指针混合路径确实会卡 ordinary；压平成 `pathText + literal mode` 和显式 `payloadPtr/addrPtr` 以后，首个 unsupported 已经不再落在 `std/os`。 |
- `std/times` 这里真正需要的是“整数秒”而不是 seed 里还没打通的 `float64 -> int64` cast。当前 runtime 本身就是 `time(NULL)` 秒级，所以补 `cheng_epoch_time_seconds()` 是把真实宿主能力显式化，不是兜底。补完以后，`std/times::epochTimeSeconds` 已从 `libp2p_quic_tls_smoke` 的第一处 unsupported 消失。 |
- seed 的 prepare 阶段之前把 builtin cast 当普通调用，也会把 `var` 复合参数误当“需要先 materialize 的值参数”。这两条都已经在 [cheng_v3_seed.c](/Users/lbcheng/cheng-lang/v3/bootstrap/cheng_v3_seed.c) 收掉；它们不是 QUIC 特例，而是 ordinary prepare 面的通用缺口。 |
- 现在 `libp2p_quic_tls_smoke` 的真前沿已经正式进入 [native_runtime.cheng](/Users/lbcheng/cheng-lang/v3/src/quic/native_runtime.cheng)。最新日志显示最前面的 prepare 失败集中在 `msquicTls13BuildClientHello(...)` 和 `msquicTls13BuildEncryptedExtensions(...)`，也就是 `native_runtime + tls/crypto` 主体，不再是 `std/os/std/times` 入口层。 |
- `libp2p_quic_tls_smoke` 这轮钉实了一个普通编译真洞：seed 之前只收“标量顶层 const”，不收 `const str`。所以像 [native_runtime.cheng](/Users/lbcheng/cheng-lang/v3/src/quic/native_runtime.cheng) 的 `msquicNativeAlpn` 和 [test_pki.cheng](/Users/lbcheng/cheng-lang/v3/src/quic/tls/test_pki.cheng) 的 `msquicLocalhostCertDerHex`，一旦进复合实参就会在 `prepare/infer/materialize` 任一环掉下去。现在 [cheng_v3_seed.c](/Users/lbcheng/cheng-lang/v3/bootstrap/cheng_v3_seed.c) 已经把这条主线补齐。 |
- 顶层整型常量也不能只吃字面量。`msquicNativePipeToken = msquicNativePipeBase + 1` 之前没被 ordinary 识别，直接把 `msquicNativePipeIdxForSession/msquicNativeIsPipeIdx` 顶成首个 unsupported。现在 seed 已补最小整型顶层 `const expr` 收集，这类 `a + 1 / a - 1` 表达式已经能进 `top_level_const`。 |
- 直接把“全局 record 成员链”塞进 `var` 复合参数，当前 `v3` ordinary 仍然很危险。把 [native_runtime.cheng](/Users/lbcheng/cheng-lang/v3/src/quic/native_runtime.cheng) 的 client/server handshake 入口压成 `var handshake`、`let serverName`、`let alpn` 局部后，`msquicNativeSendClientHello / msquicNativeSendServerFlight / msquicNativeSendClientFinished` 已经全部让出前沿。这个形状应该继续作为 `v2 -> v3` Cheng 源码迁移准则。 |
- 这轮之后，`libp2p_quic_tls_smoke` 的首个 unsupported 已经不在 QUIC native 入口层，而是 [x509.cheng](/Users/lbcheng/cheng-lang/src/std/tls/x509.cheng) 的 `x509VerifyLeafConstraints`。同时剩余 unsupported 数量从 `64` 压到 `53`，说明现在该正面补 `x509/packet_model/connection_impl/datapath_runtime` 这些更深层 ordinary 语义，不该再回头怀疑 `native_runtime` 入口壳。 |
- `x509` 这轮真正的根不是 `if`，而是 seed 顶层 const 之前不会算 `1 << 5`。给 [cheng_v3_seed.c](/Users/lbcheng/cheng-lang/v3/bootstrap/cheng_v3_seed.c) 的 `v3_try_resolve_i64_top_level_const_expr(...)` 补上 `<<` 以后，`x509KeyUsageDigitalSignature / x509KeyUsageKeyCertSign` 才正式进 lowering const 表；这两条函数随之让开。 |
- `packet_model/datapath_runtime` 这批已经证明优先改源码就够：把 `let frame = MsQuicFrame(...)` 改成 `var + 字段赋值`，把 `MsQuicFrameType(int32(...))` 改成显式 helper，把 `quicVarIntAppend(...)` 和 datapath 记账改成显式 `Result` 收口，ordinary 立即稳定。 |
- `std/crypto/minasn1::asn1IntToString` 的真 blocker 不是 while，而是 `char(...)` 这种 cast 形状。改成 `ByteBuffer` 累字节、最后反转成 `Bytes -> str` 后，这条直接消失。 |
- `std/crypto/rand::randomFillNative` 这里的源码逻辑错不大，真缺的是 ordinary 对现成 runtime bridge 的识别。把 `cheng_epoch_time_seconds` 和 `chengSystemEntropyFill` 补进 [cheng_v3_seed.c](/Users/lbcheng/cheng-lang/v3/bootstrap/cheng_v3_seed.c) 的 external signature 表后，`epochTimeSeconds` 的 unresolved 和 `randomFillNative` 的 unresolved 一起消失。 |
- 这轮收完以后，`libp2p_quic_tls_smoke` 的 unsupported 总数已经从 `53` 压到 `42`，新的第一层真骨头正式变成 [bigint.cheng](/Users/lbcheng/cheng-lang/src/std/crypto/bigint.cheng) 起头的 `bigint/p256_fixed/hkdf/handshake13` 算术链。后面再推就该正面收 `bigint`，不用再回头碰 `x509/packet/datapath/rand`。 |
- `MsQuicTls13PeerVerifyState.peerChain: X509CertList` 其实不需要跨 TLS 消息常驻。它只在 [msquicTls13ParseCertificate(...)](/Users/lbcheng/cheng-lang/v3/src/quic/tls/handshake13.cheng) 里临时参与 `x509VerifyChain/x509VerifyRevocation`，放进握手态只会额外制造 `stage2` 的大对象 layout 压力。当前已安全改成局部链，不影响后续 `CertificateVerify` 因为那一步真正需要常驻的只有 `peerLeaf`。
- `trustRoots: X509CertList` 仍是 `handshake13` 里剩下最值的大对象，但这块不能在不扩面的前提下硬砍。要真去掉它，必须把 [policy.cheng](/Users/lbcheng/cheng-lang/v3/src/quic/tls/policy.cheng) 到 [handshake13.cheng](/Users/lbcheng/cheng-lang/v3/src/quic/tls/handshake13.cheng) 的 trust-roots 入口从“按值传解析后证书链”改成“原始 DER/Into”路径，否则只是把大对象从一个字段挪到另一个字段。
- `stage2` 这批 `QUIC/TLS` 假红里，[test_pki.cheng](/Users/lbcheng/cheng-lang/v3/src/quic/tls/test_pki.cheng) 的真根不是 hex decode 算法，而是“顶层超大 `str const` 直接落进 ordinary 复合绑定/实参”。把常量搬进函数体返回，再由 `Into` helper 消费，就能把 `msquicLocalhost{Cert,TrustRoot,Key}*` 一整串 unsupported 一起拿掉。
- [msquictransport_native.cheng](/Users/lbcheng/cheng-lang/v3/src/quic/msquictransport_native.cheng) 的 `handles(...)` 这次也钉实了：当前 `stage2` 对 `enum` 等值链和多层布尔或判断仍更脆，改成“纯整数范围判断 + 早返回”以后就直接让开了。对这类 transport capability 判断，继续优先用数值区间而不是枚举相等链。
- `quic_transport_loopback_smoke` 这轮 host fresh 编继续是绿的，但运行时真失败点已经钉死成 `v3 libp2p quic open: dial: msquic native: server client key share missing after initial`。这说明最前面的黑盒已经拆开，当前不是“错误消息没模块前缀”，也不是“open/accept 壳层判错”，而是服务端在吃完 initial 里的 `ClientHello` 后，握手状态里的 `clientKeyShareGroup` 仍是 `0`。 |
- 并行只读核对已经确认，这个 `client key share missing` 的主嫌疑不在 [packet_model.cheng](/Users/lbcheng/cheng-lang/v3/src/quic/core/packet_model.cheng) 或 [crypto_stream.cheng](/Users/lbcheng/cheng-lang/v3/src/quic/core/crypto_stream.cheng)。`packet payload` 这层如果截断会直接报错，`crypto stream` 这层对 `offset=0` 单块 `CRYPTO` 也没有静默丢字节的分支；当前最短路就是正面查 [handshake13.cheng](/Users/lbcheng/cheng-lang/v3/src/quic/tls/handshake13.cheng) 的 `msquicTls13ParseClientHello / msquicTls13ParseKeyShareExtension / msquicTls13ValidateExtensions`。 |
- `native_runtime` 这轮已经把一批最脆的复合字段写回拆掉了：`client/server packet number`、`largest recv` 不再写 `MsQuicNativeSession.*`，`ackFrame.value = ...` 已收成 `msquicFrameAckNumber(...)`，临时诊断文件也已删掉。这些改动没有把 host fresh 编打回去，说明方向是对的。 |
- `stage2` 这轮没有回退到旧的 `connection_impl/peerChain` 假根，但 [native_runtime.cheng](/Users/lbcheng/cheng-lang/v3/src/quic/native_runtime.cheng) 仍剩三类真形状没收完：1）`TlsRole* / QuicPacket*` 这类 enum 常量装参；2）`msquicNativeMultiAddressBindText(...)` 的字符串构造；3）[handshake13.cheng](/Users/lbcheng/cheng-lang/v3/src/quic/tls/handshake13.cheng) 自己的大对象 layout。下一刀不该再回头怀疑 UDP/packet 壳。 |
- `server client key share missing after initial` 这轮已经钉成时序真根，不是 `TLS parser / packet encode / UDP wire`。新增的 [tls_client_hello_parse_smoke.cheng](/Users/lbcheng/cheng-lang/v3/src/tests/tls_client_hello_parse_smoke.cheng)、[tls_initial_packet_roundtrip_smoke.cheng](/Users/lbcheng/cheng-lang/v3/src/tests/tls_initial_packet_roundtrip_smoke.cheng)、[udp_datapath_wire_roundtrip_smoke.cheng](/Users/lbcheng/cheng-lang/v3/src/tests/udp_datapath_wire_roundtrip_smoke.cheng)、[native_initial_crypto_frame_smoke.cheng](/Users/lbcheng/cheng-lang/v3/src/tests/native_initial_crypto_frame_smoke.cheng)、[native_client_hello_wire_smoke.cheng](/Users/lbcheng/cheng-lang/v3/src/tests/native_client_hello_wire_smoke.cheng) 已经把三层底面全部验绿；真正把问题收掉的是 [native_runtime.cheng](/Users/lbcheng/cheng-lang/v3/src/quic/native_runtime.cheng) 把服务端首包初始化前移到 `header decode` 之后、`payload/frame decode` 之前。 |
- `native_runtime` 对大复合对象不能再侥幸走“先 decode 大包，再调用重型 reset/init，再继续读局部 frame”这种顺序。当前最稳的形状已经坐实是：先用最小 header 信息初始化会话，再做 payload/frame decode；同时 `CRYPTO/app recv` 一律走“局部副本修改后写回”，关键路径只传 `offset + data`，不要再让整帧或整包复合对象跨函数边界。 |
- 这轮 host 运行时主线已经重新闭合：[quic_transport_loopback_smoke.cheng](/Users/lbcheng/cheng-lang/v3/src/tests/quic_transport_loopback_smoke.cheng)、[libp2p_quic_tls_smoke.cheng](/Users/lbcheng/cheng-lang/v3/src/tests/libp2p_quic_tls_smoke.cheng)、[content_quic_smoke.cheng](/Users/lbcheng/cheng-lang/v3/src/tests/content_quic_smoke.cheng)、[libp2p_protocols_smoke.cheng](/Users/lbcheng/cheng-lang/v3/src/tests/libp2p_protocols_smoke.cheng) 都已前台输出 `ok`；随后 `sh /Users/lbcheng/cheng-lang/v3/tooling/run_v3_stage23_libp2p_smokes.sh` 也重新输出 `v3 stage2/stage3 libp2p: ok`。 |
- `run_v3_host_smokes.sh` 这轮并发跑时，曾短暂把 [content_quic_smoke.cheng](/Users/lbcheng/cheng-lang/v3/src/tests/content_quic_smoke.cheng) 误炸到 `peerstore::v3Libp2pPeerstoreUpsert` compile blocker；单独复跑 `content_quic_smoke` 为绿，随后用 `BACKEND_INCREMENTAL=0 BACKEND_MULTI_MODULE_CACHE=0 sh /Users/lbcheng/cheng-lang/v3/tooling/run_v3_host_smokes.sh` 也重新输出 `v3 host smokes: ok`。这说明当前 host 总 gate 仍带一层并发/缓存噪声，可信验收口径应优先看“单独复跑 + 关闭增量缓存”的结果。 |
- `content_stub_smoke` 这轮在长链 host runner 里复现出来的 compile blocker，真根不是 `content_stub` 或 `content_runtime` 逻辑，而是 [native_runtime.cheng](/Users/lbcheng/cheng-lang/v3/src/quic/native_runtime.cheng:639) 的 `msquicNativeMultiAddressBindText(...)`。这条 `MultiAddress -> str -> str` 复合返回链单独编时偶尔不炸，但串在 `content_quic_smoke -> content_stub_smoke` 后会稳定触发 `prepare composite arg temp failed`，说明 ordinary 长链编译下这类嵌套复合返回仍然脆。
- 对这类路径，最短正确解不是再赌 compiler 会自己吃下去，而是直接把逻辑摊平到字段级：先判 `addr.raw`，再判 `addr.data`，最后才走默认值。当前这条修补已经让 `/tmp/repro_content_pair.sh` 的 `content_quic_smoke -> content_stub_smoke` 串行编译稳定转绿，并且 `run_v3_host_smokes.sh` 重新全绿。
- 这类修补还要守住公开 helper 边界。`msquicNativeMultiAddressBindText(...)` 虽然主路径里已经被内联让开，但外部 smoke 仍会直接 import 它；所以 callsite 简化不等于可以删 helper，本轮 review 里已经把这个接口按同一套简化逻辑补回。
- [content_runtime.cheng](/Users/lbcheng/cheng-lang/v3/src/chain/content_runtime.cheng) 目前仍无条件 `import cheng/v3/libp2p/host/host_quic`，所以即便某个内容 smoke 实际只走 TCP，它的编译面也会把 `host_quic -> quic_transport -> native_runtime` 整串拖进来。后面如果还要继续减噪，最值钱的一刀是把内容运行时的 sync/content 拉取接口和具体 QUIC 宿主实现拆开。
- 额外单独试跑 [udp_bind_bindtext_smoke.cheng](/Users/lbcheng/cheng-lang/v3/src/tests/udp_bind_bindtext_smoke.cheng) 仍停在该 smoke 自身的 ordinary `stmt_let` 缺口，说明“bind-text 自测未进 gate”是独立旧问题，不是这次 `native_runtime` 修补带出来的新回归。 |
- `content_runtime` 和 `host_quic` 这条线最值的一刀，不是去改 smoke，也不是让 `host_base` 硬吃 QUIC，而是把“metadata 回拉宿主选择”单独搬到适配层。现在 [content_runtime_host.cheng](/Users/lbcheng/cheng-lang/v3/src/chain/content_runtime_host.cheng) 只承接 `SelectSyncRequest + FetchAdvert/Manifest/RecoveryPlan`，`content_runtime` 核心模块只保留内容缓存、pull request、blob rebuild 这些纯内容逻辑。
- 这类切法的直接收益已经可见：`content_runtime_smoke` 的 `source_closure_paths` 已从会拖进 `host_quic/quic_transport/native_runtime` 的形态，变成只剩 `tcp_transport` 的 `34` 条闭包；也就是说，纯内容 smoke 现在终于和 QUIC/native 编译面解耦了。
- 反过来，[content_stub_smoke.cheng](/Users/lbcheng/cheng-lang/v3/src/tests/content_stub_smoke.cheng) 明确要做 metadata 回拉，所以它保留对新宿主适配层的依赖是对的；这不是耦合泄漏，而是把“网络入口”放回该在的边界。
- 这轮也说明 `host_base.v3Libp2pSyncRequest(...)` 不该为了这件事强行吃掉 QUIC。`host_base` 现在保持 TCP 基线，QUIC 仍由 `host_quic` 负责；内容面只通过新的宿主适配层挑 transport，这个分层更干净，也更稳。 |
- `content_quic_smoke` 这轮曾怀疑又回到 [handshake13.cheng](/Users/lbcheng/cheng-lang/v3/src/quic/tls/handshake13.cheng) 的 `msquicTls13ResetClientDefault(...)` compile fail，但重新做三层复核后没有复现：单独 fresh 编是绿的，最短串行链 `quic_transport_loopback_smoke -> libp2p_quic_tls_smoke -> content_quic_smoke` 是绿的，`BACKEND_INCREMENTAL=0 BACKEND_MULTI_MODULE_CACHE=0 sh /Users/lbcheng/cheng-lang/v3/tooling/run_v3_host_smokes.sh` 也重新是绿的。所以这次不能把它当活动根，更不能在总 gate 已绿时反向去改 `handshake13`。
- 并行只读排查还是给了一个有价值的边界：`msquicTls13ResetClientDefault/ServerDefault` 当前实现走的是“全局大握手对象整块 init + 嵌套 `var` helper”链，这仍是 ordinary/ABI 的已知脆点。如果后面它再次稳定复现，最短正解不是继续堆 `Into` helper，而是把 client/server reset 改成直接叶子写字段。 |
- `content_runtime` 这条分层现在还剩的真口子，不在 metadata fetch，而在 QUIC shard pull 之前还挂着独立的 [content_runtime_quic.cheng](/Users/lbcheng/cheng-lang/v3/src/chain/content_runtime_quic.cheng) 入口。把 `v3ContentRuntimeRequestShardPull(...)` 收进 [content_runtime_host_quic.cheng](/Users/lbcheng/cheng-lang/v3/src/chain/content_runtime_host_quic.cheng) 以后，内容面的网络边界终于统一成“宿主适配层负责分 transport，核心 runtime 不碰 transport”。 |
- 这轮也验证了兼容薄壳的正确位置：[content_runtime_quic.cheng](/Users/lbcheng/cheng-lang/v3/src/chain/content_runtime_quic.cheng) 继续保留对外入口，但内部只转发到新适配层。这样可以一边清理真实边界，一边不把历史调用点一次性打断。 |
- [content_runtime_host.cheng](/Users/lbcheng/cheng-lang/v3/src/chain/content_runtime_host.cheng) 那条 `quic content requires content_runtime_quic` 的错误文案已经过时。边界迁完后，如果提示文本还指旧模块，后面排障会被误导；这次顺手收正到 `content_runtime_host_quic` 是必要的，不是 cosmetic。 |
- 这轮又钉死了一条更硬的边界：在当前 seed/no-cache ordinary 编译面下，[content_runtime_host_quic.cheng](/Users/lbcheng/cheng-lang/v3/src/chain/content_runtime_host_quic.cheng) 还不能被收成“纯 QUIC 薄层”。一旦把里面的 metadata fetch 和 TCP fallback 全删掉，`BACKEND_INCREMENTAL=0 BACKEND_MULTI_MODULE_CACHE=0` 下的 [content_quic_smoke.cheng](/Users/lbcheng/cheng-lang/v3/src/tests/content_quic_smoke.cheng) 会稳定前移到 `std/tls/x509::x509ParsePssParams` 和 `std/tls/x509::x509ParseExtensions` unsupported。 |
- 这不是漂亮不漂亮的问题，而是当前 compiler/order 真的在吃这层形状。默认 targeted compile 还能过，但 no-cache compile 和总 host gate 会坏，所以这类“继续压薄 QUIC 宿主层”的尝试现在不能算生产级推进，必须当场回滚。 |
- 也因此，现阶段正确口径不是“继续把 `content_runtime_host_quic` 刮到只剩一条函数”，而是先承认当前稳定形状需要保留那层 `host_quic + runtime_host + fetch/fallback` 壳，后面如果真要再压薄，必须先正面收 `x509` 首屏的 ordinary unsupported，而不是从内容面宿主层硬抠。 |
- 这轮已经把 [x509.cheng](/Users/lbcheng/cheng-lang/src/std/tls/x509.cheng) 从 `stage2` 前沿里整体让开了。真正有效的形状不是“继续优化算法”，而是把热路径统一收成三种模板：本地 `ASN.1 tag` 标量常量、显式 `int32 code -> enum` 映射、以及只在边界处一次性拆 `Result` 复合值。`x509ParseMgf1Hash(...)` 改成直接回 `hashCode(int32)` 后，`x509ParsePssParams(...)` 和 `x509VerifyOcspResponse(...)` 里最容易炸的 enum 本地绑定一起消失，`stage2` 首批 unsupported 已不再出现任何 `std/tls/x509::*`。 |
- 这也顺手钉死了一个更可靠的排障口径：如果 seed/ordinary 还在 enum 上抖，最短正解不是再写 `Enum(code)` 或 `var status: Enum = EnumUnknown`，而是先用 `int32 code` 走完整个热路径，最后只在 `return Ok(...)` 或 `out.field = XxxKnown` 这种末端位置落枚举。`x509ParsePssParams(...)`、`x509ParseSignatureAlgorithm(...)`、`x509VerifyOcspResponse(...)` 这轮都证明了这个口径是有效的。 |
- host 侧回归也说明这次不是“拿 stage2 过编译换运行时倒退”。[quic_transport_loopback_smoke.cheng](/Users/lbcheng/cheng-lang/v3/src/tests/quic_transport_loopback_smoke.cheng) 和 [libp2p_quic_tls_smoke.cheng](/Users/lbcheng/cheng-lang/v3/src/tests/libp2p_quic_tls_smoke.cheng) 在当前 x509 形状下都继续前台输出 `ok`，所以接下来该正面切的是 `std/crypto/rsa` 的 `RsaBigPrivateKey` 复合布局和 [handshake13.cheng](/Users/lbcheng/cheng-lang/v3/src/quic/tls/handshake13.cheng) 的大对象写回，不该再回头怀疑 x509。 |
- 这轮顺手还钉死了 host 总 gate 的一个独立旧洞：`BACKEND_INCREMENTAL=0 BACKEND_MULTI_MODULE_CACHE=0 sh /Users/lbcheng/cheng-lang/v3/tooling/run_v3_host_smokes.sh` 连跑两次都不是倒在 QUIC/TLS，而是稳定死在 [udp_importc_smoke.cheng](/Users/lbcheng/cheng-lang/v3/src/tests/udp_importc_smoke.cheng) 的运行期 `v3 udp importc: recv`。同一条 smoke 单独用 host driver 复跑却又稳定 `ok`，所以这不是 `x509` 回归，而是 full runner 场景里独立存在的 UDP 时序/环境问题。 |
- `std/crypto/rand::randomFillNative` 当前真根仍是外部熵 bridge 直接出现在 if 条件里：`if chengSystemEntropyFill(out.data, out.len) != 0`。这条不是 `out.data == nil || out.len <= 0` 那层表皮，stage2 日志已经明确钉到 external scalar call 条件发码。 |
- `std/crypto/rand::randomFillUrandom` 当前第一处真根仍是 `let f: bos.File = randomOpenUrandom()`；而 `randomOpenUrandom()` 本身又直接卡在 `bos.openRead(randUrandomPath)`。所以这条现在不是循环体问题，先收“open file 返回值绑定”才有意义。 |
- [handshake13.cheng](/Users/lbcheng/cheng-lang/v3/src/quic/tls/handshake13.cheng) 这轮把 owner 包装层从旧的 `initMsQuicTls13HandshakeStateInto/msquicTls13HandshakeSetRequireCert/msquicTls13HandshakeAddTrustRoot` 热路径上挪开后，`stage2` 最前的 handshake 句型已经收敛到 `initMsQuicTls13PeerVerifyState(...)` 里的 `out.revocationMode = X509RevocationOff`。这说明下一刀该先收 enum 写回，而不是继续改 reset wrapper 名字。 |
- `std/crypto/rsa` 这轮再次确认：当前最前的三条 `private-key` 解析函数还不是算法层问题，而是 ordinary 对“可变 offset + if IsErr(...) 立即 return Err(Error(...))”这组句型的组合支持没收完。最短入口仍是 [rsaBigPrivateKeyFromPkcs1Value(...)](/Users/lbcheng/cheng-lang/src/std/crypto/rsa.cheng) 的第一句 `var offset: int32 = 0`。 |
- [handshake13.cheng](/Users/lbcheng/cheng-lang/v3/src/quic/tls/handshake13.cheng) 的 `peer verify` revocation 默认值可以直接靠零值初始化，不需要显式写 `X509RevocationOff`。把这条写回删掉以后，fresh `stage2` 的 unsupported 列表里已经不再出现 `initMsQuicTls13PeerVerifyState`。 |
- [policy.cheng](/Users/lbcheng/cheng-lang/v3/src/quic/tls/policy.cheng) 的 `policy.certLeafKeyKind == X509KeyUnknown` 这类 enum 比较，在当前 ordinary 下仍比直接比较 `0` 更脆。把它收成标量后，`msquicTlsPolicyValidateForServer` 已从 fresh `stage2` unsupported 列表里消失，而且 host `quic_transport_loopback_smoke` 继续前台 `ok`。 |
- 这轮还钉死了一条不能再试错的坑：把 [rsa.cheng](/Users/lbcheng/cheng-lang/src/std/crypto/rsa.cheng) 的 `rsaBigSignPssBytes` 强行改成统一走 `rsaBigPrivateKeyFromBytesInto(...)`，host 运行期会直接报 `v3 libp2p quic open: dial: rsa: pss em too short`。这不是编译形状问题，而是语义回归，所以这种“把 PKCS1/PKCS8 手工解析链硬折叠进 Into helper”的改法当前不能保留。 |
- [quic_transport_loopback_smoke.cheng](/Users/lbcheng/cheng-lang/v3/src/tests/quic_transport_loopback_smoke.cheng) 那条 `assert(strContains(msg, "msquic"))` 没有验收价值，只会把真实错误文本盖掉。删掉以后，真正露出来的根因就是 `v3 libp2p quic open: dial: rsa: pss em too short`，排障才重新回到正确链路上。 |
- [os.cheng](/Users/lbcheng/cheng-lang/src/std/os.cheng) 这轮又钉实了两个 ordinary 句型坑。第一，`c_iometer_call(hook, ...)` 这种 Cheng 里直接调函数指针的形状，在 no-cache host 编译下不稳，最短正解是直接走现成 native `@importc("c_iometer_call")`；第二，`readFileSizedInto(...)` 里 `let f: File = openRead(path)` 再配 `c_fclose f` 的写法会把 [native_initial_crypto_frame_smoke.cheng](/Users/lbcheng/cheng-lang/v3/src/tests/native_initial_crypto_frame_smoke.cheng) 打回编译失败，收成 `var f` 和 `c_fclose(f)` 后就重新稳定。 |
- `run_v3_host_smokes.sh` 这轮已经重新全绿，所以主阻塞正式从 host runner 回到了 `stage2`。最新 fresh `stage2` 定向编 [quic_transport_loopback_smoke.cheng](/Users/lbcheng/cheng-lang/v3/src/tests/quic_transport_loopback_smoke.cheng) 时，首个 unsupported 已经前移到 [rsa.cheng](/Users/lbcheng/cheng-lang/src/std/crypto/rsa.cheng) 的 `rsaPkcs1ValueFromPrivateKeyBytes`；后面才是 [rand.cheng](/Users/lbcheng/cheng-lang/src/std/crypto/rand.cheng)、[ecnist.cheng](/Users/lbcheng/cheng-lang/src/std/crypto/ecnist.cheng) 和 [handshake13.cheng](/Users/lbcheng/cheng-lang/v3/src/quic/tls/handshake13.cheng) 的大对象 layout。 |
- [rsa.cheng](/Users/lbcheng/cheng-lang/src/std/crypto/rsa.cheng) 这轮又钉死了一个边界：把 `rsaBigSignPssBytes(...)` 直接折到 `rsaBigPrivateKeyFromBytesInto(...)` 这条已过 ordinary 的 `Into` 主线，host 运行期会稳定报 `v3 libp2p quic open: dial: rsa: pss em too short`。这不是 stage2 形状问题，而是签名语义回归，所以这条路不能再走。 |
- fresh `stage2` 对 [rsaPkcs1ValueFromPrivateKeyBytes(...)](/Users/lbcheng/cheng-lang/src/std/crypto/rsa.cheng) 的首屏报错，表面是 `if nextTag == asn1TagInteger`，但这轮并行只读核对已经坐实：`let xRes`、`Value(res)`、`if nextTag == ...` 本身都不是根，真热点在“分支里写复合 `Bytes` 出参”。把它改成分支直接 `return Ok[Bytes](...)` 后，host 继续稳定；但 `stage2` 计数仍不降，说明 `rsa` 当前已经不是最短收益点。 |
- [rand.cheng](/Users/lbcheng/cheng-lang/src/std/crypto/rand.cheng) 现在保留下来的 `randomFillNative -> randomFillUrandom -> bos.openRead(randUrandomPath) + bos.c_fread(...)` 形状，已经由 fresh host 编跑 `/tmp/quic_transport_loopback_smoke.host.current` 和 `/tmp/quic_transport_loopback_smoke.host.rsa_branchret` 两次坐实不会把 QUIC/TLS 主线打坏。`stage2` 里它仍是第二个 unsupported，所以接下来最短链已经从 `rsa` 转到 `randomFillUrandom(...)` 的 `openRead` 返回值绑定。 |
- [rand.cheng](/Users/lbcheng/cheng-lang/src/std/crypto/rand.cheng) 这轮又钉死了一条 compiler 边界：`strToCStringTemp + c_fopen` 这组普通 C 风格打开文件写法，会把 host ordinary 重新打回 `randomFillUrandom`；收回到 `bos.openRead(randUrandomPath) + bos.c_fread(...) + bos.close(f)` 以后，fresh host 的 [quic_transport_loopback_smoke.cheng](/Users/lbcheng/cheng-lang/v3/src/tests/quic_transport_loopback_smoke.cheng) 和 [libp2p_quic_tls_smoke.cheng](/Users/lbcheng/cheng-lang/v3/src/tests/libp2p_quic_tls_smoke.cheng) 都重新前台 `ok`。 |
- [ecnist.cheng](/Users/lbcheng/cheng-lang/src/std/crypto/ecnist.cheng) 当前真正会把 host 顶坏的，不是 `RFC6979` 算法本身，而是 `p256DeterministicK(...)` 里的嵌套复合实参：`bytesConcat(bytesConcat3(v, marker, xBytes), h1)` 和 `bytesConcat(v, zero)`。把这条热路径改成显式的 [p256DeterministicConcat4(...)](/Users/lbcheng/cheng-lang/src/std/crypto/ecnist.cheng) 后，host ordinary 重新收住，`stage2` 也把首屏稳定压到只剩 `rsa/rand/ecnist` 三条。 |
- fresh `stage2` 当前已经不再需要猜了：最前沿稳定就是 [rsaPkcs1ValueFromPrivateKeyBytes(...)](/Users/lbcheng/cheng-lang/src/std/crypto/rsa.cheng)、[randomFillUrandom(...)](/Users/lbcheng/cheng-lang/src/std/crypto/rand.cheng)、[p256DeterministicK(...)](/Users/lbcheng/cheng-lang/src/std/crypto/ecnist.cheng)。后面的 [handshake13.cheng](/Users/lbcheng/cheng-lang/v3/src/quic/tls/handshake13.cheng) 和 [tls13.cheng](/Users/lbcheng/cheng-lang/v3/src/quic/tls/tls13.cheng) 还在，但现在已经不是最近一刀。 |
- [handshake13.cheng](/Users/lbcheng/cheng-lang/v3/src/quic/tls/handshake13.cheng) 这轮给了一个稳定结论：`BuildCertificateVerify/BuildFinished` 和 `ResetClientState/ResetServerState` 这两组 API 的大对象链可以安全裁掉，而且裁掉后 host 真跑不回退。这说明 `MsQuicTls13HandshakeState` 的“只为取 transcript/hash/secret 而整块搬运”确实是纯形状成本，不是协议必要性。
- 反过来，[rsa.cheng](/Users/lbcheng/cheng-lang/src/std/crypto/rsa.cheng)、[rand.cheng](/Users/lbcheng/cheng-lang/src/std/crypto/rand.cheng)、[ecnist.cheng](/Users/lbcheng/cheng-lang/src/std/crypto/ecnist.cheng) 这三条标准库前沿当前已经碰到 ordinary 的真实编译器缺口，不是业务代码还能继续靠换皮规避的层面了：`Result[Bytes]` 分支直返、`openRead` 返回值绑定、`bytesAlloc -> Bytes` 物化，只要再包一层 helper，fresh `stage2` 就会把 unsupported 前移到 helper 本身。
- 所以下一刀如果还要继续推 `stage2`，最短正路已经不是再折腾业务代码样式，而是正面补编译器对这三种句型的 lowering/emit 支持。继续在业务代码里来回切 `Into/helper/branch-return`，只会制造无收益来回。 |
- [rsaBigSignPssBytes(...)](/Users/lbcheng/cheng-lang/src/std/crypto/rsa.cheng) 这轮还钉死了另一条边界：`rsaBigPrivateKeyFromBytes(...) -> rsaBigSignPss(...)` 这条看上去更直接的路径，在 wider host/no-cache 图里并不稳，会把 [libp2p_quic_tls_smoke.cheng](/Users/lbcheng/cheng-lang/v3/src/tests/libp2p_quic_tls_smoke.cheng) 打回 ordinary compile fail；而 `rsaPkcs1ValueFromPrivateKeyBytes(...) -> rsaBigSignPssPkcs1Value(...)` 这条窄路径能同时通过 targeted、no-cache 和总 host runner。
- 所以当前 `rsa` 的正确口径已经很明确：业务代码侧不要再动 `rsaBigSignPssBytes(...)` 的语义路径，后面如果还要继续推 `stage2`，就该直接修编译器对 `Result[Bytes]` 这类复合返回句型的支持，而不是再去折腾 `rsa` 的解析路线。 |
- `stage2 QUIC/TLS` 这轮真正的总开关不是某个单独业务函数，而是 seed 对三种基础句型的支持一起不完整：1）顶层常量表达式不会折叠 `+ / - / <<`；2）裸常量名只按 owner/alias 查，缺全局唯一回退；3）标量比较统一按 `x` 宽度发射。三处一起补上以后，fresh `stage2` 的 [quic_transport_loopback_smoke.cheng](/Users/lbcheng/cheng-lang/v3/src/tests/quic_transport_loopback_smoke.cheng) 直接从 `64` 个 unsupported 收到 `0`，说明这条链前面卡的是编译器普通句型，不是 `rsa/rand/ecnist` 的协议语义。
- [cheng_v3_seed.c](/Users/lbcheng/cheng-lang/v3/bootstrap/cheng_v3_seed.c) 当前已经证明：顶层常量表必须同时保留数值和字符串值，且常量解析不能只认字面量或单符号。`x509/rsa/handshake13` 这批模块里大量 `asn1Tag* / x509* / TLS*` 常量都依赖别名、位移和简单算术表达式；少其中任一环，ordinary lowering 就会把本来正常的 `if lhs == CONST` 误炸成 unsupported。
- host 总 runner 这轮中途出现过一次 `libp2p_quic_tls_smoke` 编译进程被外部 `SIGTERM` 终止，但 `compile.log` 是空文件，且随后 targeted `libp2p_quic_tls_smoke` 和 fresh no-cache `run_v3_host_smokes.sh` 都完整通过。当前口径应记作 runner 级外部抖动，不应误判成 seed 或 TLS 逻辑回归。 |
- 这轮把内容平面的 `stage2/stage3` 覆盖也钉死了：fresh 前台补验显示 [content_codec_smoke.cheng](/Users/lbcheng/cheng-lang/v3/src/tests/content_codec_smoke.cheng)、[content_runtime_smoke.cheng](/Users/lbcheng/cheng-lang/v3/src/tests/content_runtime_smoke.cheng)、[content_stub_smoke.cheng](/Users/lbcheng/cheng-lang/v3/src/tests/content_stub_smoke.cheng)、[content_quic_smoke.cheng](/Users/lbcheng/cheng-lang/v3/src/tests/content_quic_smoke.cheng) 在 `stage2 + stage3` 下都已经是 `unsupported=0 + runtime ok`。内容链现在不再只是 host green，而是 selfhost 两级编译面也已经贯通。
- [run_v3_stage23_libp2p_smokes.sh](/Users/lbcheng/cheng-lang/v3/tooling/run_v3_stage23_libp2p_smokes.sh) 之前最大的盲区不是 libp2p 本身，而是内容平面主链一直靠手工 targeted 命令补验。把四条 `content_*` smoke 正式接进 runner 以后，这类回归终于变成持续门禁，不需要再靠人工记忆“这轮顺手跑过”。 |
- 这轮继续证明，`stage23` 最值钱的不是再单独抬某一个 smoke，而是把已经坐实稳定的一整串主链放进同一条 runner。把 [overlay_contracts_smoke.cheng](/Users/lbcheng/cheng-lang/v3/src/tests/overlay_contracts_smoke.cheng)、[pubsub_smoke.cheng](/Users/lbcheng/cheng-lang/v3/src/tests/pubsub_smoke.cheng)、[dag_mempool_smoke.cheng](/Users/lbcheng/cheng-lang/v3/src/tests/dag_mempool_smoke.cheng)、[plumtree_smoke.cheng](/Users/lbcheng/cheng-lang/v3/src/tests/plumtree_smoke.cheng)、[erasure_swarm_smoke.cheng](/Users/lbcheng/cheng-lang/v3/src/tests/erasure_swarm_smoke.cheng) 纳进以后，runner 的价值才从“抽样 smoke”变成“协议主链门禁”。
- 这条 runner 当前实际覆盖面已经比文件名大得多：除了 libp2p core/tcp/overlay/protocols，它还串了 `native_initial_crypto_frame -> native_client_hello_wire -> tls_client_hello_parse -> tls_initial_packet_roundtrip -> quic_transport_loopback -> libp2p_quic_tls -> overlay/pubsub/dag/plumtree/erasure/content -> chain_node`。后面再扩，不该按名字猜，而要按“哪条仍只存在 host runner、且一旦坏掉会拖主链”的原则选。 |
- [anti_entropy_signature_fields_smoke.cheng](/Users/lbcheng/cheng-lang/v3/src/tests/anti_entropy_signature_fields_smoke.cheng) 这轮钉死了一条很具体的 ordinary 边界：测试代码里直接用 `lsmr.v3LsmrLocalityHashText(...)` 会把 `std/strutils` 整串拖进闭包，结果是假红，不是协议逻辑坏。对这种“只为造测试数据”的 locality hash，最短正路就是在 smoke 本地直接走 `fixed256.sha256Fixed(layout.byteSpanFromBytes(bytesFromString(text)))`。
- [handshake13.cheng](/Users/lbcheng/cheng-lang/v3/src/quic/tls/handshake13.cheng) 的 `peer verify` 路径现在已经证明：能删掉的就是那些单字段 setter/reset 薄壳，不能碰的是 `LoadRevocationCrls/ResetRuntime/ResetConfig` 这类真入口。继续把两者混着压，只会把稳定逻辑和纯形状成本搅在一起。
- 一条 smoke 只有在 `targeted host + targeted stage2 + targeted stage3 + host runner + stage23 runner` 五层都绿以后，才算真正进门禁。`anti_entropy_signature_fields_smoke` 这轮补齐后，后面的扩容也应该按这个口径做，而不是只看单条 targeted 通过。 |
- [lsmr_types_smoke.cheng](/Users/lbcheng/cheng-lang/v3/src/tests/lsmr_types_smoke.cheng) 和 [lsmr_locality_storage_smoke.cheng](/Users/lbcheng/cheng-lang/v3/src/tests/lsmr_locality_storage_smoke.cheng) 这轮给了一个很干净的结论：LSMR 这块适合继续往 `stage23` 主 gate 抬，因为它们是纯算法、纯内存、无宿主 I/O，且正好补上 `anti_entropy + bagua_prefix_tree` 没直接钉死的类型/存储层。
- [udp_importc_smoke.cheng](/Users/lbcheng/cheng-lang/v3/src/tests/udp_importc_smoke.cheng) 则相反。它测的是真 `socket bind/send/recv/close`，价值高，但本质是宿主桥 smoke，不是协议主线 smoke。它应该留在 host runner 或独立 targeted，不能和 `stage23` 主 gate 混在一起。
- [handshake13.cheng](/Users/lbcheng/cheng-lang/v3/src/quic/tls/handshake13.cheng) 这轮再次证明，最危险的不是“大逻辑错”，而是“删了一半”的历史薄壳：调试 `echo`、单字段 setter、定义和调用不同步的残留最容易把 `stage2/stage3` 拖回 ordinary compile fail。处理这种文件，必须一次把定义、调用、日志输出一起收口，不能半步停。 |
- compiler/tooling smoke 这轮证明了另一件事：`stage23` 的价值不该只停在“协议主链跑通”，还要前置一层编译器自检。把 [compiler_runtime_smoke.cheng](/Users/lbcheng/cheng-lang/v3/src/tests/compiler_runtime_smoke.cheng)、[compiler_pipeline_stub_smoke.cheng](/Users/lbcheng/cheng-lang/v3/src/tests/compiler_pipeline_stub_smoke.cheng)、[lowering_plan_smoke.cheng](/Users/lbcheng/cheng-lang/v3/src/tests/lowering_plan_smoke.cheng)、[primary_object_plan_smoke.cheng](/Users/lbcheng/cheng-lang/v3/src/tests/primary_object_plan_smoke.cheng)、[object_native_link_plan_smoke.cheng](/Users/lbcheng/cheng-lang/v3/src/tests/object_native_link_plan_smoke.cheng)、[program_selfhost_smoke.cheng](/Users/lbcheng/cheng-lang/v3/src/tests/program_selfhost_smoke.cheng) 纳进以后，很多“协议 smoke 顺带暴露”的编译器漂移能更早、更短链地炸出来。 |
- [handshake13.cheng](/Users/lbcheng/cheng-lang/v3/src/quic/tls/handshake13.cheng) 这次被新 runner 再次钉实：`PeerVerifySetOcspResponse` 这种看起来很小的 helper，只要还活着，就足够把 `stage2/stage3` ordinary 重新拖回去。这里的经验不是“这个 helper 特别坏”，而是 TLS 这类重闭包文件里不能允许任何单字段 setter 残留。 |
- 现在 `stage23` 的正确分层已经很清楚：最前面是 compiler/tooling 预检层，中间是 QUIC/TLS + libp2p 协议层，后面才是 LSMR + overlay/pubsub/dag/plumtree/erasure/content/chain_node 业务主链。后面再扩 smoke，应该优先补这三层里仍然空着的洞，而不是随便往后段堆业务测试。 |
2026-04-12 18:09
- `headscale/DERP` 这层最稳的 Cheng 建模，不是新造一个 `tailscale transport` 或 `derp transport`，而是继续保留 `TCP/QUIC` 作为真实传输，把 `headscale` 放进 `provider/config/status`，把 `DERP` 放进 `reachability = relayed`。这样选路、地址可见性和上层协议都不会被带歪。
- `DERP` 和 `WebRTC TURN` 不能混成一个东西。`DERP` 是 tailnet underlay 的 relay，`TURN` 是 WebRTC 数据面的 relay；这轮代码里两层已经分开：`headscale/DERP` 只在 [tailnet_transport.cheng](/Users/lbcheng/cheng-lang/v3/src/libp2p/transports/tailnet_transport.cheng)，[webrtc_signal.cheng](/Users/lbcheng/cheng-lang/v3/src/libp2p/protocols/webrtc_signal.cheng) 里的 `turnServers` 继续只表达 WebRTC policy。
- 只要把 `relayed` 固定排到 direct 后面，`tailnet` 和公网主线就能共存而不互相抢路。现在 [peerstore.cheng](/Users/lbcheng/cheng-lang/v3/src/libp2p/core/peerstore.cheng) 已经把这条规则钉死：trusted 节点遇到 `tailnet relayed + public direct` 时会选 public direct；只有没有 direct 路时，才退回 `DERP`。 |
2026-04-12 18:42
- WebRTC 这轮最值钱的不是先碰浏览器 native bridge，而是先把 Cheng 源码层的会话模型补齐：`offer/answer/candidate/turn-policy` 的 codec、session 状态、transport policy、stream 选路，都已经能在不引入假的 `tailscale transport` 的前提下落到 [webrtc_signal.cheng](/Users/lbcheng/cheng-lang/v3/src/libp2p/protocols/webrtc_signal.cheng) 和 [webrtc_transport.cheng](/Users/lbcheng/cheng-lang/v3/src/libp2p/transports/webrtc_transport.cheng)。
- 这轮也再次钉死了一个更硬的 ordinary 经验：`Result[Bytes]` 不能跨 transport 分支悬着用。只要像 [host.cheng](/Users/lbcheng/cheng-lang/v3/src/libp2p/host/host.cheng) 之前那样，把 `servePayloadRes` 留到 TCP/WebRTC 分支里再 `Value(...)`，通用 `sync` 主线就会被一并带坏。最稳形状就是先本地物化 `servePayload`，再交给 transport helper。
- 运输层一复杂，应该直接拆叶子 helper，不要继续把所有 transport 分支塞在一个大函数里和编译器较劲。[host.cheng](/Users/lbcheng/cheng-lang/v3/src/libp2p/host/host.cheng) 这轮把 `v3Libp2pSyncRequest(...)` 拆成 `SyncDecodePayload/SyncRequestTcp/SyncRequestWebrtc` 之后，[libp2p_protocols_smoke.cheng](/Users/lbcheng/cheng-lang/v3/src/tests/libp2p_protocols_smoke.cheng)、[chain_node_libp2p_smoke.cheng](/Users/lbcheng/cheng-lang/v3/src/tests/chain_node_libp2p_smoke.cheng)、[chain_node_tailnet_smoke.cheng](/Users/lbcheng/cheng-lang/v3/src/tests/chain_node_tailnet_smoke.cheng) 一起恢复，说明这条才是正路。
- `pin runtime` 这轮顺手证明了另一条类似边界：把 runtime 附件放进全局复合表 [pin_registry.cheng](/Users/lbcheng/cheng-lang/v3/src/libp2p/host/pin_registry.cheng) 很容易踩全局复合状态的 lowering/初始化坑；直接收回 [host.cheng](/Users/lbcheng/cheng-lang/v3/src/libp2p/host/host.cheng) 自身字段更稳，也更符合状态归属。
- 当时 [pin_runtime_quic_smoke.cheng](/Users/lbcheng/cheng-lang/v3/src/tests/pin_runtime_quic_smoke.cheng) 的确停到过 `std_crypto_bigint__bigSub`，但后续沿宿主正式协议分发表、`servePayload` 本地物化和 QUIC 宿主边界继续收口后，这条烟已经在 host 与 no-cache `stage2/stage3` 正式 gate 下全部重新真跑 `ok`。 |
2026-04-12 19:08
- [content_runtime_host.cheng](/Users/lbcheng/cheng-lang/v3/src/chain/content_runtime_host.cheng) 这轮又复现了和 `host sync` 一样的规律：当 `endpoint 选路 + serve payload + transport 分支 + response decode/validate` 全塞在一个函数里时，host 也许还绿，`stage2` 运行期却会直接在 `bytesSlice` 这种底层地方炸。最短正解不是补断言，而是拆成 `Tcp/Webrtc/Decode` 三个叶子 helper，让大函数只做 transport 分发。
- `content` 这条再次证明，光把 `servePayload = Value(servePayloadRes)` 提前还不够；一旦 `response decode` 还和 transport 分支搅在一起，`stage2` 仍可能在 runtime 上坏掉。把 decode/validate 本身也抽成单独 helper 以后，`content_runtime_smoke.stage2` 才重新稳定。
- 现在混合网络这条主线已经有了很清楚的结构经验：host/content 这类共享 I/O 边界函数，只要同时承载多个 transport，就应该第一时间拆成“边界校验 + transport-specific helper + decode helper”，不要等到 runner 段错以后再被动拆。 |
2026-04-12 21:36
- `tailnet` 这轮钉死了一个很具体的规则：`DialPlanFill(...)` 里 session payload decode 只能做“补 hint”，不能做“决定 direct 能不能走”。只要把 `direct` 的可达性建立在 payload parse 成功之上，`peerSession` 一旦稍微换个存储形状，`libp2p_tailnet_transport_smoke` 就会假红到 `direct plan`。正确顺序是先按 `providerReady/proxyListenersReady/listenerNeedsRepair/relayOnly` 这些硬状态判 `directPossible`，再用 session hints 补 `remoteTailnetIp/boundAddrText`。
- 大复合 transport 状态里的 `seq` 字段，当前最稳的写法仍然是“局部 seq 构建 -> 一次性赋回”。`peerSessions` 这次已经再次验证：直接在 `transport.peerSessions` 这类嵌套字段上边找边改边 `add`，很容易把 status/dial plan 读写带歪；拆成 `peerSessionIds + peerSessionRelayOnlys + peerSessions` 再分别局部写回后，`sessionCount/tailnetPeers/direct plan` 才一起稳定。
- `tailnet_transport` 这类选路模块也再次说明，`provider/control/status` 和 `route decision` 不能缠在一块儿。把 `providerReady/proxyListenersReady/listenerNeedsRepair/derpAvailable/exactRouting` 先压成局部标量以后，direct/DERP/repair/pending 的顺序才真正可控；不这么做，哪怕逻辑表面没错，也会被当前 ordinary/runtime 形状拖出假失败。 |
2026-04-12 22:41
- [content_runtime_smoke.cheng](/Users/lbcheng/cheng-lang/v3/src/tests/content_runtime_smoke.cheng) 这次已经把问题钉死了：preview payload 构造留在本地 helper 里，即便 helper 逻辑完全正确，fresh host ordinary 也会稳定炸成 `emit stmt call failed`。同样的构造改成和 [content_codec_smoke.cheng](/Users/lbcheng/cheng-lang/v3/src/tests/content_codec_smoke.cheng) 一样的 `main` 内联形状后，host、browser、stage23 三条 gate 一起恢复。
- 当前 seed 对这类长 `Bytes/ByteBuf/RSA` 复合构造的真实边界很清楚：不要再赌“把大段 payload build 封进一个本地 void helper，主函数会更稳”。对现在这套编译器来说，稳定形状反而是把热路径直接摊平在调用点，让 lowering 不需要再处理那条 stmt-call。
- 这轮也再次验证了总验收顺序的必要性：先修 targeted host 单点，再跑全量 host，再跑 browser runner，最后跑 no-cache `stage23`。只看其中任何一条都不够，因为这次问题一开始就是在 host 总 gate 里暴露、但 browser/stage23 也必须跟着回归确认。 |
2026-04-12 23:49
- browser-webrtc 端点的正确主线已经钉死：只要 endpoint 的 `networkDomain == browser-webrtc`，host 侧 `sync/request-response` 就应该直接走 [webrtc_transport.cheng](/Users/lbcheng/cheng-lang/v3/src/libp2p/transports/webrtc_transport.cheng) 的 `BrowserRequestResponse(...)`，再把 signal transcript 通过 [host.cheng](/Users/lbcheng/cheng-lang/v3/src/libp2p/host/host.cheng) 的 `WebrtcRecordSignalWire(...)` 回灌进 host 状态。继续让这类端点走 native bridge，只会把“浏览器链路”测试成宿主链路。
- `ProtocolIngress` 这条现在也证明了最短正路：不要再单独发明一套 browser pubsub 协议，直接复用 [store_sync.cheng](/Users/lbcheng/cheng-lang/v3/src/libp2p/protocols/store_sync.cheng) 的 `StoreEntriesEncode/DecodeFill(...)` 做 request-response 载荷，在 [host.cheng](/Users/lbcheng/cheng-lang/v3/src/libp2p/host/host.cheng) 里落成 “decode -> store -> publish -> encode ack” 就够了，协议面和存储面也不会分叉。
- `tailnet_transport` 这轮进一步说明，provider 生命周期最好收成显式 helper，而不是靠测试里散落的 `SetProviderReady/SetProxyListenersReady/SetDerpHealthy/SetListenerNeedsRepair` 组合。把 `start/proxy-ready/repair-needed/repair-complete/derp-reconnect/derp-ready` 固化后，dial plan 和 status 的语义才稳定，也更接近 `tsnettransport.nim` 那类真实 transport 运行时。 |
2026-04-13 00:08
- browser WebRTC 这轮钉死了一个更硬的运行时边界：单次 `runBridge(...)` 只够做 codec/smoke，一旦 browser 节点要连续跑 `content + sync + pubsub`，就必须收成真正的 `session open/exchange/close` 句柄模型。现在 [webrtc_datachannel_runtime.js](/Users/lbcheng/cheng-lang/v3/runtime/browser/webrtc_datachannel_runtime.js) 和 [webrtc_transport.cheng](/Users/lbcheng/cheng-lang/v3/src/libp2p/transports/webrtc_transport.cheng) 已经证明，host 侧按 peer 复用 browser session 才是正确形状；继续每次请求重建 browser bridge，只会把 signal transcript、计数和性能一起带歪。
- `system_helpers.c` 这里又钉死了一条宿主桥规则：从 `cheng_fd_read_wait(...)` 拿到的 buffer 只能用 `cheng_free(...)` 释放，不能混用 `free(...)`。这轮 browser session bridge 一开始就是因为这里用错释放函数，host browser smoke 直接崩进 `mfm_free`；修成 `cheng_free(...)` 后才稳定。
- `tailnet provider lifecycle` 这轮说明，光有 Cheng 状态位不够，必须有真句柄 runtime。把 `provider start/proxy-ready/repair-needed/repair-listeners/derp-reconnect/derp-ready` 打到 [system_helpers.c](/Users/lbcheng/cheng-lang/src/runtime/native/system_helpers.c) 的 provider handle 上以后，[libp2p_tailnet_transport_smoke.cheng](/Users/lbcheng/cheng-lang/v3/src/tests/libp2p_tailnet_transport_smoke.cheng) 和 [libp2p_tailnet_derp_smoke.cheng](/Users/lbcheng/cheng-lang/v3/src/tests/libp2p_tailnet_derp_smoke.cheng) 才真正从“状态壳断言”变成“runtime 生命周期断言”。
- `pin_runtime_quic_smoke.stage2` 这轮把 QUIC transport 的真实性能边界钉死了：当前 `stage2` 下用 transport 专用大 RSA 测试证书链，会在第一次 `CertificateVerify` 的 `rsaBigSignPssBytes(...)` 里跑到近似挂死；但把这条链换成 transport 专用 `1024-bit RSA` 后，算法路径不变、内容面 RSA 逻辑不变、[pin_runtime_quic_smoke.cheng](/Users/lbcheng/cheng-lang/v3/src/tests/pin_runtime_quic_smoke.cheng) 的 `stage2` 则能稳定在十几秒内跑完。现在应该把“transport test PKI 的 key size”和“内容/签名业务的 RSA key size”分开，不该继续拿同一套大 key 同时压两条完全不同的链路。 |
2026-04-13 01:18
- `android_bridge` 这轮又钉死了一条 ordinary/runtime 边界：不要把 `host.peers.peers[pos]` 这种嵌套复合对象直接当 `var` 传给 helper。`DisconnectText(...)` 和 `ReconnectBootstrap(...)` 只有改成“先复制本地 record，改完再整块写回”以后，host/runtime 才稳定。这个规则后面同样适用于其它嵌套 peer/session 容器。
- mobile 这层当前最稳的对外 API 形状不是“helper 返回 `Result[bool]` 再转手”，而是直接在公开入口里做本地状态更新和尾返回。`ConnectPreferredText(...)` / `ConnectRegisteredText(...)` 这次已经证明，继续让 seed 处理那条复合返回 helper，只会反复掉进 `emit composite return failed`。
- `tailnet_control_core` 这轮说明，CLI 真链路和纯 ordinary decode 不是一回事。`configure -> listen -> dial-plan` 的 CLI runner 现在已经稳定，`stateWire` 也确实带上了 `peer.<i>.lastPath`；但我试过把 `stateWire -> decode -> listen` 直接做成 ordinary smoke，当前还会卡在 `decode listen`。这条能力还没坐实，所以没有硬塞进正式门禁。
- `tailnet` 对外控制面现在最稳的验收方式，是继续走 CLI runner 把 `provider/config/listen/peer/dial-plan` 整串打通，而不是先相信库内 encode/decode 自测。当前 runner 已经能真实验证 `routeText=/ip4/127.0.0.1/tcp/7101`、`sessionCount=1`、`peer.0.lastPath=direct`，这比只看内部状态可靠得多。 |
2026-04-13 06:48
- `tailnet_control_core` 这轮把那条没坐实的 ordinary decode 真钉死了：`stateWire` 内容本身没问题，问题出在 restore 热路径还在做 `"listen." + intToStr(i) + ".host"` 这种动态 key 拼接，外加把 `str/str[]` 大对象一路穿进 helper。稳定形状是直接在调用点内联恢复，并统一改走 `tailnetCtlIndexedLineValue(...)` 这种“按前缀扫描 + 读 index”的路径。
- `tailnet control` 的 `providerRuntimeActive=2` 不是 native runtime 自己漏，而是 [tailnet_control_core.cheng](/Users/lbcheng/cheng-lang/v3/src/project/tailnet_control_core.cheng) 里同一进程重复初始化 transport：`tailnetCtlMainRun(...)` 先建一次，`configure` 分支又建一次。把 `configure` 改成只读现有 transport，并去掉 `tailnetCtlMainRun(...)` 开头那次白初始化以后，active count 就稳定回到 `1`。
- 这轮也再次说明，证伪的 helper 路径不要留在树上。[tailnetCtlDecodeStateLinesFill(...)](/Users/lbcheng/cheng-lang/v3/src/project/tailnet_control_core.cheng) / `tailnetCtlDecodeStateWireFill(...)` 已经证明会把 ordinary restore 带回旧坑，留着只会让后面的人误用；删掉它们以后，主线更短，也不会再被“看起来更抽象”的错误形状勾回去。
- `tailnet_transport` 的 runtime probe 现在必须以 native handle 为准。只靠 Cheng 侧 `providerReady/proxyListenersReady/derpHealthy/listenerNeedsRepair/startupStage` 自己记状态，会让 `status/dial-plan` 出现“状态看着对，runtime 其实没到”的假绿；把 probe 直接打到 native provider handle 上后，`tailnet_control_core_smoke`、`run_v3_tailnet_control_smoke.sh` 和总门禁的结果才真正一致。 |
2026-04-13 08:36
- 这轮把 libp2p 在线节点资源采集钉成了两条硬规则：1）资源导出必须是单独协议，不能混进普通 `identify` 地址宣告里一起裸发；2）资源可见性必须跟 `trustedInfra` 绑定，不能因为 peer 已经连上就默认公开 CPU/内存/磁盘/GPU/NPU。现在 [host.cheng](/Users/lbcheng/cheng-lang/v3/src/libp2p/host/host.cheng) 的 `v3Libp2pIdentifyResourcesForPeer(...)` 已经按这条收口。 |
- `identify` 这条又验证了当前 ordinary 最稳的发码形状：不要在 helper 里对 `var Bytes` 连续 `add(...)`。我一开始这么写，[identify.cheng](/Users/lbcheng/cheng-lang/v3/src/libp2p/protocols/identify.cheng) 就直接卡到 `primary object body semantics missing`。换成 [bytes_layout.cheng](/Users/lbcheng/cheng-lang/v3/src/std/bytes_layout.cheng) 的 `ByteBuf` 和 [codec_binary.cheng](/Users/lbcheng/cheng-lang/v3/src/chain/codec_binary.cheng) 的 `v3BufAppendU32BE(...)` 后，host/stage2/stage3 才一起稳定。后面这类小协议都该直接复用这条形状。 |
- `CoreML MLAllComputeDevices()` 这条宿主桥也钉死了一条运行时规则：`objc_msgSend` 不能偷懒用变参签名。第一次实现里 `objectAtIndex:` 就因为变参 ABI 把 index 寄存器传歪，直接炸出 `NSRangeException`。改成固定签名 `void* (*)(void*, void*, uint64_t)` 以后，GPU/NPU 设备枚举才稳定。 |
- 资源采集这轮的可信验收口径已经坐实：`libp2p_resource_smoke` 要同时验三件事，trusted peer 能拿到样本、public peer 会被拒绝、本地 peerstore 会缓存远端样本。只验“能采到本机数据”不够，因为真正的业务点是在线节点资源交换，不是单机系统调用。 |
- 全量 `host` runner 这轮同时冒出的 [pin_runtime_host_smoke.cheng](/Users/lbcheng/cheng-lang/v3/src/tests/pin_runtime_host_smoke.cheng) `reward proof1` 不在这次资源链路上。它说明当前工作区还带着别的活动改动，所以这刀的最可信结果是 `resource smoke + 相关 host 子集 + stage2/stage3 定向` 全绿，而不是把无关回归硬算在资源采集头上。 |
2026-04-13 20:07
- `fresh-node selfhost` 这轮钉死了一个原则：正式门禁不要再依赖那条会随机把编译器/ABI撞坏的 `.cheng` smoke。之前试图把 fresh-node 做成单独 smoke 和中转 helper，最后不是 compile 崩就是 runtime 崩；真正稳的口径是直接走编译器命令面，真实跑 `world-sync -> stable equivalence gate -> fresh-node receipt reverify`。
- seed/bootstrp 这条也被坐实了：如果 `supported_commands` 合同里不把 `fresh-node-selfhost` 写死，host backend driver 就永远只是“源码里看起来支持”，不是真支持。现在 [stage1_bootstrap.cheng](/Users/lbcheng/cheng-lang/v3/bootstrap/stage1_bootstrap.cheng) 与 [cheng_v3_seed.c](/Users/lbcheng/cheng-lang/v3/bootstrap/cheng_v3_seed.c) 都已经同步到同一口径。 |
- 这轮最后又把完整 [run_v3_stage23_libp2p_smokes.sh](/Users/lbcheng/cheng-lang/v3/tooling/run_v3_stage23_libp2p_smokes.sh) 从头前台跑完了，`content_stub_smoke.stage2` 没再掉空日志，最终明确输出 `v3 stage2/stage3 libp2p: ok`。所以当前真实口径应该收成：fresh-node gate 已正式落地，而且整条 `stage23` 总 runner 也已经重新全绿。 |
2026-04-14 14:08
- `consteval` 解释器这轮又钉死了一个规律：真正会反复炸纯内建 cross-target 的，不是 `PE/ELF` 发射器，而是前面的表达式和控制流语义面。一旦 `truthy/cast/for-range/break-continue/std-os` 没补齐，最终症状几乎都会伪装成 `native_link_consteval_entry_missing`，看起来像 linker 问题，实质是 entry 根本没算出来。
- 顶层运算符查找必须按“完整操作符”匹配，不能只做裸 `strncmp`。这轮 `return_shift` 已经证明，只要 `<` 或 `>` 会误命中 `<<` / `>>`，或者 `&` / `|` 会误命中 `&&` / `||`，整个 `consteval` 路径就会在很后面才以 `entry_missing` 的形式炸出来，排查成本很高。把长操作符避让写进 `v3_find_top_level_binary_op(...)` 和 `v3_find_top_level_binary_op_last(...)` 才是正解。
- `windows/riscv64` 的 builtin gate 不能只盯最小样例。之前只验 `hello/add/call/if/while`，会把很多真实会回归的句型漏掉；这轮把 `return_and_expr / return_bitwise / return_shift / return_for_sum / return_while_break / return_while_continue / return_inline_if_and_ternary / return_call_mixed / return_cast_i32 / return_cast_i64 / return_std_os_getenv_fileexists` 接进正式脚本以后，纯内建 cross-target 才算有基本门禁密度。 |
2026-04-14 15:05
- `float32(...)` 这轮钉死了一个很容易漏的边界：解释器里把 intrinsic 实现出来还不够，`v3_is_intrinsic_call_name(...)` 也必须同步收口。否则日志只会表现成统一的 `native_link_consteval_entry_missing`，表面像“入口没算出来”，实际只是 callee 在最前面就走丢了。
- `consteval` 的数值模型如果只保留 `bool + i32`，后面很多看起来像“float 算术不支持”的红灯其实根因都更早。把 `V3_CONST_F64`、literal 解析、float cast、比较、`+ - * /` 和一元负号补齐以后，`return_float32_* / return_float64_*` 这批一下子全绿，说明这条线需要的是统一值模型，不是零碎特判。
- 顶层标量全局这轮也证明了另一个规律：很多 `global_*` 夹具并不是 primary object 真不会发，而是 consteval function env 从来没把顶层 `var` 带进来。先把可解释的全局零值和初始化值预热进 `env`，`return_float32_roundtrip / return_global_add / return_global_assign` 就直接通了。
- 重新全扫 builtin `return_*.cheng` 后，当前剩余红灯已经比前一轮更聚焦：`63` 过、`36` 失败。剩下主要分成三簇，一簇是 `object/container`，一簇是 `import/pkg/spawn`，还有一簇是“primary object 能过但 consteval 还不会跑”的深语义样例。下一刀不该再碰 float/global。 |
## Findings

| 项目 | 结论 |
|---|---|
| 新发现 | 真正的自举编译器命令面在 [cheng_v3_seed.c](/Users/lbcheng/cheng-lang/v3/bootstrap/cheng_v3_seed.c)，不是单改 [compiler_main.cheng](/Users/lbcheng/cheng-lang/v3/src/tooling/compiler_main.cheng) 就会自动体现在 [cheng.stage3](/Users/lbcheng/cheng-lang/artifacts/v3_bootstrap/cheng.stage3) / [v3_backend_driver/cheng](/Users/lbcheng/cheng-lang/artifacts/v3_backend_driver/cheng) 上。要做零脚本入口，必须直接改 seed 命令分发表和 `stage1` contract。 |
| 新发现 | contract 新字段不能只补校验表，必须同时补 [v3_contract_normalized_text(...)](/Users/lbcheng/cheng-lang/v3/bootstrap/cheng_v3_seed.c) 的序列化顺序。不然 `compile-bootstrap` 虽然吃的是新 contract 文件，产物里嵌进去的还是旧字段集，运行时会以“缺字段”形式炸回头。 |
| 新发现 | [bootstrap_contracts_smoke.cheng](/Users/lbcheng/cheng-lang/v3/src/tests/bootstrap_contracts_smoke.cheng) 之前那种“只 `echo ok` 不显式 `return 0`”的写法会在 runner 里伪装成失败。对这类基础 smoke，入口必须固定 `fn main(): int32 ... return 0`。 |
| 新发现 | 只把 [build_backend_driver_v3.sh](/Users/lbcheng/cheng-lang/v3/tooling/build_backend_driver_v3.sh) 做锁不够。只要 `stage3` 本体里的 [v3_cmd_build_backend_driver(...)](/Users/lbcheng/cheng-lang/v3/bootstrap/cheng_v3_seed.c) 仍然无锁，并发跑 `slice-gate` 和任意一个 `build-*` 命令时，`cheng.generated.c`、driver 二进制和日志都会互相截断。锁必须收进编译器本体。 |
| 新发现 | bootstrap/build 这类热点入口最稳的锁形状不是目录锁加 shell trap，而是编译器内部的原子 `symlink(pid)` 锁。这样既能让其他进程直接读出 owner pid 做存活检查，也能在持锁进程异常退出后自动回收陈旧锁，不会留下永远卡死的空目录。 |
| 新发现 | `slice-gate` 真正稳定下来以后，不能每个叶子命令都无脑重编 backend driver。把 freshness 快路径直接做到 [cheng_v3_seed.c](/Users/lbcheng/cheng-lang/v3/bootstrap/cheng_v3_seed.c) 里，按 `stage2/stage3/env/source` 的时间戳判断是否需要重编，才能让并发 gate 既不漂也不白白重编。 |
| 新发现 | 内建 profiler 汇总这条线不能混用 Cheng 堆和宿主 `malloc/free` 语义。[cheng_copy_string_bytes(...)](/Users/lbcheng/cheng-lang/src/runtime/native/system_helpers.c) 返回的是带 `cheng_strmeta` 的 Cheng 字符串，`cheng_v3_profile_counts_free(...)` 如果直接 `free()`，进程会在 `atexit` 写 `v3_profile_v1` 时炸进 `mfm_free`；这类 key/value 聚合代码必须统一按 `cheng_malloc/cheng_free` 成对释放。 |
| 新发现 | debug runtime shim 不能只隐藏常规 libc/allocator 符号，panic 桥也必须一起隐藏。[v3_debug_runtime_shim.c](/Users/lbcheng/cheng-lang/v3/runtime/native/v3_debug_runtime_shim.c) 如果漏掉 `cheng_v3_panic_cstring_and_exit`，program support 导出的 panic 桥和 native debug provider 会互相回跳，表面像“panic fixture 挂死”，本质是 provider 组合时的符号递归。 |
| 新发现 | profile gate 的最小可行 fixture 不能太短。200Hz 采样下，几毫秒就跑完的 busy loop 会稳定得到 `total_samples=0`；像 [ordinary_profile_hotspots_fixture.cheng](/Users/lbcheng/cheng-lang/v3/src/tests/ordinary_profile_hotspots_fixture.cheng) 这种正式门禁样例，必须把工作量调到定时器真能打到 `hot_mul/hot_mix` 两个热点。 |
| 新发现 | `panic/bounds` 这类高频运行时入口，不该继续挂在 native provider 上做“直接转发再回 C”。把文本拼装和导出逻辑放回 [program_support_backend_v3.cheng](/Users/lbcheng/cheng-lang/v3/src/runtime/program_support_backend_v3.cheng)，再统一落到 `cheng_v3_native_dump_backtrace_and_exit`，bridge 面会明显变短，而且能直接避开 `program_support <-> debug shim` 的符号回跳。 |
| 新发现 | 要确认 bridge 真的缩短，不能只看 gate 绿灯。用内建 [print-object](/Users/lbcheng/cheng-lang/v3/tooling/cheng_v3.sh) 直接看 provider object 的未定义符号更可靠；这轮 `runtime_program_support_v3.o` 已只剩 `native_dump_backtrace/register_line_map` 两个 native 依赖，说明 panic/bounds 已经真从宿主桥里退出。 |
| 新发现 | bridge 缩短之后，contract 也必须同步收窄，不然后面很容易有人照着旧 `services` 把退役入口又接回去。[debug_runtime_v3.cheng](/Users/lbcheng/cheng-lang/v3/src/runtime/debug_runtime_v3.cheng) 应该只保留 `register_line_map / dump_backtrace / profile`，把 `panic/bounds` 留在 program support provider。 |
| 新发现 | 只删源码里的 import 还不够，退役 native 入口本身也要从 [system_helpers.c](/Users/lbcheng/cheng-lang/src/runtime/native/system_helpers.c) / [system_helpers.h](/Users/lbcheng/cheng-lang/src/runtime/native/system_helpers.h) 暴露面拿掉，再用 [verify_debug_runtime_v3.sh](/Users/lbcheng/cheng-lang/v3/tooling/verify_debug_runtime_v3.sh) 对 provider object 做未定义符号白名单检查，才能防止旧桥被悄悄接回。 |
| 新发现 | 这轮最隐蔽的真根不是 `panic`，而是 `register_line_map` 的符号面。只要 native debug bridge 还同时导出 `cheng_v3_register_line_map_from_argv0` 和 `cheng_v3_native_register_line_map_from_argv0`，而 Cheng provider 也导出同名 `cheng_v3_register_line_map_from_argv0`，启动入口就会在 `register_line_map -> native_register_line_map -> register_line_map` 之间自递归，表现成 `ordinary_panic_fixture` 空日志挂死。这个入口必须固定成“Cheng provider 负责对外 `register_line_map`，native bridge 只保留 `native_register_line_map`”。 |
| 新发现 | 把 debug bridge 从 [system_helpers.c](/Users/lbcheng/cheng-lang/src/runtime/native/system_helpers.c) 真切出来以后，最可靠的 sanity check 不是只看能不能编，而是直接对挂住进程做一次前台采样。这轮 `sample ordinary_panic_fixture` 直接把栈顶钉在 `cheng_v3_register_line_map_from_argv0` 无限递归，比继续猜 `panic/backtrace/fflush` 哪层卡住快得多。 |
| 新发现 | 只给外层 shell 加 `trap kill $pid` 不够，真正会留下 `PPID=1` 孤儿的是“shell 死了，但它拉起的 stage3/backend_driver/bin 还在自己的进程树里继续跑”。这类长命令必须统一放进独立 session，再由独立 watchdog 在父进程消失时 `killpg` 整组。 |
| 新发现 | `kill(pid, 0)` 不能拿来单独当 watchdog 的存活判定。被 `SIGKILL` 打死但还没被回收的 wrapper 会先变成 zombie，这时 `kill(pid, 0)` 仍然返回成功；必须再看 `ps -o state=`，把 `Z*` 直接视为已死，不然 watchdog 会卡住，真正的工作子进程反而活成 `PPID=1`。 |
| 新发现 | 只修 [cheng_v3.sh](/Users/lbcheng/cheng-lang/v3/tooling/cheng_v3.sh)、[bootstrap_bridge_v3.sh](/Users/lbcheng/cheng-lang/v3/tooling/bootstrap_bridge_v3.sh)、[build_backend_driver_v3.sh](/Users/lbcheng/cheng-lang/v3/tooling/build_backend_driver_v3.sh) 还不够。像 [build_zero_exit_v3.sh](/Users/lbcheng/cheng-lang/v3/tooling/build_zero_exit_v3.sh)、[verify_windows_builtin_linker_v3.sh](/Users/lbcheng/cheng-lang/v3/tooling/verify_windows_builtin_linker_v3.sh)、[run_v3_tailnet_control_smoke.sh](/Users/lbcheng/cheng-lang/v3/tooling/run_v3_tailnet_control_smoke.sh)、[run_v3_wasm_smokes.sh](/Users/lbcheng/cheng-lang/v3/tooling/run_v3_wasm_smokes.sh) 这种叶子脚本如果还直接裸跑 `system-link-exec`、fixture 或后台 probe server，父进程一死照样会漏出 `PPID=1` 孤儿。UE 收口必须按“所有长命令统一走 guard”做到底。 |
| 新发现 | seed 侧用 `system(...)` 拼 guard 命令时，不能把 [guarded_exec_v3.py](/Users/lbcheng/cheng-lang/v3/tooling/guarded_exec_v3.py) 的参数写成 `--log:/path --timeout:1800`。这个 CLI 只认空格形式 `--log /path --timeout 1800`；一旦继续用冒号格式，`stage3 bootstrap-bridge/c-ref` 这种 shell 子命令会直接失败，看起来像 bootstrap 坏了，真根只是参数语法不对。 |
| 新发现 | `bootstrap.env` 存在不等于 `cheng.stage3` 可信。只要 [cheng_v3_seed.c](/Users/lbcheng/cheng-lang/v3/bootstrap/cheng_v3_seed.c) 或 [stage1_bootstrap.cheng](/Users/lbcheng/cheng-lang/v3/bootstrap/stage1_bootstrap.cheng) 比现有 `cheng.stage3` 新，wrapper 就必须先回 seed 外根重建；否则外层命令会继续执行旧 stage3，把已经修掉的老 bug 原样带回来。 |
| 新发现 | UE 防线要想不回退，不能只靠“大家记得用 guard”。必须再加一条结构门禁，像 [verify_orphan_guard_v3.sh](/Users/lbcheng/cheng-lang/v3/tooling/verify_orphan_guard_v3.sh) 这样直接扫描 `v3/tooling/*.sh`，硬禁裸跑 `system-link-exec`、裸起后台进程、裸写 `>*.log 2>&1` 和直接写 `guarded_exec_v3.py`，这样以后谁再写出旁路脚本会立刻红。 |
| 新发现 | 这轮继续往前推后，最短路径不是再造一层新的 native process guard。现有 [host_ops.cheng](/Users/lbcheng/cheng-lang/v3/src/tooling/host_ops.cheng) + [std/os.cheng](/Users/lbcheng/cheng-lang/src/std/os.cheng) 已经自带 `spawn / wait / process-group signal / detached orphan guard`；真正缺的是把 `run-host-smokes / run-stage23-libp2p-smokes / verify-orphan-guard` 这些主编排从脚本壳搬回 [gate_main.cheng](/Users/lbcheng/cheng-lang/v3/src/tooling/gate_main.cheng)。 |
| 新发现 | stage3 freshness 不能只靠 shell wrapper 卡。只要 [gate_main.cheng](/Users/lbcheng/cheng-lang/v3/src/tooling/gate_main.cheng) 自己还会在 `contracts.v3BootstrapBridgeReady(...)` 为真时直接复用 `cheng.stage3`，那外层壳一旦被绕开，旧 stage3 就会重新复活。mtime 检查必须落在编译器本体里。 |
| 新发现 | [verify_orphan_guard_v3.sh](/Users/lbcheng/cheng-lang/v3/tooling/verify_orphan_guard_v3.sh) 这种扫描脚本也不该继续持有真实规则。最稳的形状是脚本只做薄壳，真正的脚本文本扫描和 banned pattern 判定留在 [gate_main.cheng](/Users/lbcheng/cheng-lang/v3/src/tooling/gate_main.cheng)；否则“门禁源码”和“stage3 真执行逻辑”迟早还会再漂一次。 |
| 新发现 | 这批最后的叶子入口没必要再膨胀新的顶层命令。把 `run-host-smokes / run-stage23-libp2p-smokes` 扩成 `--tail-only`，就够承载 `chain_node_cli / tailnet_control / fresh_node_selfhost / migration / browser_webrtc`；这样既能把 shell 脚本压成纯转发壳，又不会把 seed/stage1/命令表再扩大一轮。 |
| 新发现 | `chain_node process/three-node` 这种会在 host 和 stage2/stage3 两套 suite 里复用的流程，不能只靠环境变量取编译器。只要内部 helper 没有显式 `compiler + label + envOverrides` 形参，stage23 suite 就会很容易悄悄退回 host compiler，表面仍然是绿的，实际已经丢了覆盖。 |
| 新发现 | `tailnet_control` 和 `browser_webrtc` 这类需要宿主 `python3/npm/npx` 的流程，最稳的边界是“Cheng 编排，宿主工具只做外部动作”，而不是继续把整条 gate 留在 shell。这样主逻辑已经回到编译器本体，外壳也不会再成为 UE、freshness、日志校验和参数漂移的复发点。 |
| 新发现 | [bootstrap_bridge_v3.sh](/Users/lbcheng/cheng-lang/v3/tooling/bootstrap_bridge_v3.sh) 这种带目录锁的壳脚本，不能在持锁后直接 `exec` 真命令。shell 一旦 `exec` 成功，`trap cleanup` 就不会执行，`bootstrap.lock` 会永久泄漏；正确做法是写入 owner pid、等待时回收陈旧 pid，最后用普通子进程执行，让 shell 自己走到 `EXIT` 释放锁。 |
| 新发现 | 不能把被压成薄壳的 smoke wrapper 再当 stage3 命令的后端。只要 seed/stage3 的 `run-host-smokes` 仍然是“shell out 到 [run_v3_host_smokes.sh](/Users/lbcheng/cheng-lang/v3/tooling/run_v3_host_smokes.sh)”这种形状，而脚本又被压成“转发给 `cheng_v3.sh run-host-smokes`”，就会立刻形成 `shell -> stage3 -> shell` 递归。像 host/stage23 这种总 smoke，要么真收回 Cheng，要么脚本必须保留真实执行体，不能两边同时做薄壳。 |
| 新发现 | shell 里把多测试名先拼成换行字符串再转发时，尾段分支必须显式把它还原成位置参数。像 [run_v3_stage23_libp2p_smokes.sh](/Users/lbcheng/cheng-lang/v3/tooling/run_v3_stage23_libp2p_smokes.sh) 的 `--tail-only:browser_webrtc`，如果直接写成 `${tests:+"$tests"}`，多个测试名会塌成一个参数，后面的 smoke runner 看起来像“正常启动却没跑对测试”。 |
| 新发现 | 当前 live 的 `run-host-smokes / run-stage23-libp2p-smokes` 入口不在 [gate_main.cheng](/Users/lbcheng/cheng-lang/v3/src/tooling/gate_main.cheng)，而在 [cheng_v3_seed.c](/Users/lbcheng/cheng-lang/v3/bootstrap/cheng_v3_seed.c)。所以只改 Cheng 侧 gate 源码，不会自动改变现有 `cheng.stage3` 的真实执行路径；要把总 smoke 真从 shell 拉回编译器，必须先改 seed，再重建 bootstrap。 |
| 新发现 | 总 smoke 和叶子 smoke 不能一刀切地一起压壳。总 smoke 必须只有一个真实执行体，不然很容易形成 `shell -> stage3 -> shell` 自回环；叶子脚本可以暂时保留真实执行体，等对应命令路径真内建后再压成纯转发壳。 |
| 新发现 | `verify-orphan-guard` 不能只看“有没有递归扫描”。这轮的真挂死一共有两个：Cheng 侧把 `v3/tooling` 目录项当文件直接读，seed C 侧又把 `verify-orphan-guard` 做成脚本 passthrough，最后形成 `stage3 -> verify_orphan_guard_v3.sh -> cheng_v3.sh -> stage3` 自递归。两边都得一起改，单修一侧不够。 |
| 新发现 | orphan guard 的规则必须只针对真正危险的旧 guard 旁路，不能把普通 `python3` 工具用法一刀切打成违规。像 [run_v3_tailnet_control_smoke.sh](/Users/lbcheng/cheng-lang/v3/tooling/run_v3_tailnet_control_smoke.sh) 这种用 Python 起测试探针的脚本，如果被误封，门禁就会先坏在无关处。真正该禁的是 `guarded_exec_v3.py / orphan_guard_run.sh` 这类旧守护入口和它们的直接残留引用。 |
| 新发现 | 当前 live 的 stage3/backend-driver 叶子命令面仍然不完整，所以“全部叶子脚本一刀切压成纯壳”不是收口，而是回归。像 `tailnet_control / migration / browser_webrtc / tcp/udp importc` 这类入口，只有等 live 命令面真公开后才能压壳；在那之前必须保留真实执行体，并把门禁写死成“能直转发的才直转发”。 |
| 新发现 | debug/runtime bridge 再缩时，不能只改 Cheng provider 的 `providerNativeSources`，seed C 里的 runtime source resolver 和对象级 smoke 也要一起改到同一份 native 源。否则正式编译、对象 smoke 和 gate 看的是三套不同 bridge，结构上一定再漂。 |
| 新发现 | `stage1_bootstrap/gate_main` 里把新命令名写全，不等于当前 live 的 `cheng.stage3` 已经有这些命令。只要 [cheng_v3_seed.c](/Users/lbcheng/cheng-lang/v3/bootstrap/cheng_v3_seed.c) 还没真的 dispatch 到对应实现，薄壳脚本就不能先压；否则帮助文本是新的，真执行面还是旧的，收口会直接变成回归。 |
| 新发现 | seed 里凡是自己组 argv 调 `execv` 的地方，数组必须显式留最后一个 `NULL`。这轮 `run-wasm-smokes` 和 `run-browser-host-wasm-smoke` 的 `execv: Bad address` 已经证明，少这个结尾位不会温和失败，而是直接把 smoke 打死。 |
| 新发现 | 像 `tailnet_control` 这种 flag 很长的外部命令，不能再手写 argc 常量。只要后面多加几个参数却忘了同步计数，前面的命令看起来还能跑，真正关键的末尾 flag 会被静默截断；最稳的写法就是统一用 `sizeof(array)/sizeof(array[0])`。 |
| 新发现 | seed runner 自己也要按严格宿主编译规则过。当前 Darwin 下用 `cc -std=c11 -pedantic` 编 [cheng_v3_seed.c](/Users/lbcheng/cheng-lang/v3/bootstrap/cheng_v3_seed.c) 时，`strsep` 和 `st_mtimespec` 只有在顶层显式开 `_GNU_SOURCE/_DARWIN_C_SOURCE` 才是稳定形状；不把这层收住，bootstrap 会先死在外根编译。 |
| 新发现 | `v3_run_shell_command_logged(...)` 这种共享 helper 里一个多余的 `snprintf(...)` 实参就会把整条命令串污染掉。seed 侧的 shell bridge 不能想当然复用“看起来只是日志”的格式化代码，命令构造和日志构造必须严格分开。 |
| 新发现 | `x509: signature invalid` 这次真根不在 transport 证书，也不在 `ECDSA` 数学，而在 [x509DecodeEcdsaSignature(...)](/Users/lbcheng/cheng-lang/src/std/tls/x509.cheng) 这条 DER 解码链。当前编译路径下，把 ASN.1 整数再绕一层中间 `Bytes` 组合，会让 `CertificateVerify` 用到错误的 `r||s`；这类固定宽度签名最稳的写法就是直接解进最终 64 字节缓冲。 |
| 新发现 | ordinary `v3` smoke 里临时排障不能再用动态 `echo(ErrorText(...))`。seed 只支持字面量 `echo`，这类写法会把 lowering 编译链直接打坏；要么直接 `panic(ErrorText(...))`，要么用 `assert`。 |
| 新发现 | Darwin 平台符号补名前缀必须幂等。像 Mach-O 这种目标既会遇到裸 `foo`，也会遇到已经带 `_foo` 的符号；如果统一无脑补 `_`，最终汇编里就会变成 `__foo`，症状只会在 native link 阶段表现成一串“明明 provider 已定义、调用侧却还是未定义”的假闭包问题。 |
| 新发现 | 长跑 smoke 里不能只对默认 backend driver 做“缺失即重建”。`stage2/stage3` 这类 bootstrap 编译器路径在并行 gate、bootstrap 刷新或共享产物目录被重写时，同样可能短暂失效；如果 `compile helper` 只会补 backend driver，就会把真正的产物刷新问题伪装成 `missing compiler during fixture compile`，然后随机炸在 `stage23` 中段。 |
| 新发现 | 这轮真正的断点不在 `gate_main`、也不在壳脚本，而在 [cheng_v3_seed.c](/Users/lbcheng/cheng-lang/v3/bootstrap/cheng_v3_seed.c) 的 live command dispatch。只要 seed 没把新命令接上，`stage1_bootstrap / compiler_runtime / cheng_v3.sh / smoke` 全部追平也还是会报 `unknown command`。 |
| 新发现 | build 命令只支持可配置 `out/report` 还不够，必须先 `v3_ensure_parent_dir(...)`。`build-linux-nolibc-exe` 这轮第一次真跑时挂的不是链接，而是 `content_stub_smoke.compile.log` 的父目录根本没建。 |
| 新发现 | `host_ops` 真把并行/进程监管接回 Cheng 以后，像 [failfast_parallel.sh](/Users/lbcheng/cheng-lang/v3/tooling/failfast_parallel.sh) 这种死 helper 不能继续留着。最稳的形状是直接删掉，再把名字写进 `verify-orphan-guard` 的硬门禁，防止以后被悄悄复活。 |
| 新发现 | debug runtime bridge 再缩时，不需要把 live provider 再拆成多份 `providerNativeSources`。当前最稳形状是“对外仍一份正式 provider 源文件，内部再按 `trace/profile` 拆 `.inc`”；这样 `provider source path` 合同、seed resolver、system_link_exec 和对象级 gate 都还能继续指向同一条正式路径。 |
| 新发现 | 只查 `provider_source_paths` 和导出符号还不够，debug bridge 还会悄悄从未定义符号面长回宿主依赖。最稳门禁是再加一层对象级未定义符号白名单，直接卡死 `runtime_debug_runtime_v3.o` 允许依赖的宿主符号集合。 |
| 新发现 | `bootstrap_bridge_v3.sh` 这种带锁 wrapper 继续压薄时，不能因为想直转发 `stage3` 就回到 `exec`。持锁 shell 一旦 `exec`，`trap cleanup` 就没机会清锁；正确形状是“同锁内普通子进程调用 `cheng.stage3 bootstrap-bridge`，shell 自己走到 EXIT 再清锁”。 |
| 新发现 | bootstrap 冷启动的最佳分层已经更明确了：只有 [cheng_v3_seed.c](/Users/lbcheng/cheng-lang/v3/bootstrap/cheng_v3_seed.c) 变了，才值得重编临时 seed runner；如果只是 `stage1_bootstrap/compiler_main/compiler_runtime/compiler_request/gate_main` 变了，直接让 fresh `cheng.stage3 bootstrap-bridge` 处理才是最短路径。 |
| 新发现 | debug/runtime 宿主桥继续往下拆时，第一刀应该先拆“host-only base”，不是先拆更多 provider 文件。把 env/path/stderr/write/getter-resolve 这类纯宿主底座单独收走以后，`trace/profile` 两块才能继续各自缩，不会再把共享宿主细节散落回各个 include。 |
| 新发现 | Linux 这条门禁不能假设会有 `runtime_debug_runtime_v3.o`。当前 `aarch64-unknown-linux-gnu` 真在跑的是 `system_helpers_backend_nolibc_linux_aarch64.o`；要卡 Linux 对象依赖面，就必须直接检查这份 runtime object，而不是拿 Darwin host debug bridge 的命名去套 Linux。 |
| 新发现 | `bootstrap_bridge_v3.sh` 继续收薄时，`cheng.stage0` 也是现成可复用的冷启动层，不该被忽略。只要 `stage0` 和当前 seed 同代，就应优先直接复用 `cheng.stage0 bootstrap-bridge`，而不是每次都回去重编临时 seed runner。 |
| 新发现 | 试过但没接进 live 链的 helper 不能留在树上冒充能力。像这轮的 `profile_report.cheng/profile_report_main.cheng`，只要 `profile-run` 最终没走它们，`build_plan/contract/README` 就必须一起撤回真实口径，不然下一轮排障会先被假入口误导。 |
| 新发现 | 当前 profiler 最稳的分层不是“runtime 直接吐最终 `v3_profile_v1`”，而是“runtime 只产原始 `v3_profile_raw_v1`，编译器侧再汇总成 `v3_profile_v1`”。这样 live debug bridge 只保留取样和原始 frame 材料，不把热点聚合文本格式重新塞回宿主 runtime。 |
| 新发现 | 这条 profiler 分层不能只停在 seed C。只要 Cheng 侧 `verify-debug-profile` 还在直接吃 `CHENG_V3_PROFILE_OUT`，调试 gate 仍然绑着 C 侧最终报告逻辑；最稳收法是让 gate 也改成 `CHENG_V3_PROFILE_RAW_OUT -> live profile-report`，这样编译器侧和 gate 侧才是同一条真实主线。 |
| 新发现 | `bootstrap_bridge_v3.sh` 不该继续自己维护 `seed/stage1/compiler_*` 的 freshness 表。冷启动壳最稳的边界就是“只负责找到现成的 `stage3`、再退到 `stage0`、最后才回临时 seed runner”；真正哪些源变了、要不要重建，必须只由 `stage0/stage3 bootstrap-bridge` 本体裁决。 |
| 新发现 | 只把 Cheng gate 改成 `raw -> profile-report` 还不够。只要 live debug bridge 里还保留 `CHENG_V3_PROFILE_OUT` 和 `v3_debug_profile_write_final_report()`，runtime 就仍然背着第二条最终报告语义；要把 profile 真收口，必须把这条 C 分支物理删掉。 |
| 新发现 | profile C 分支删掉以后，`verify-debug-runtime` 的 Darwin 未定义符号白名单也必须同步追平；否则 gate 还会继续期待 `_qsort/_realloc/_strcmp` 这类只在旧最终聚合路径里才会出现的宿主依赖，直接把正确收口误报成回归。 |
| 新发现 | `cheng_v3.sh` 这种薄壳继续收时，最稳的不是再发明新 helper，而是把命令分成“需要 contract”和“plain stage3”两组统一分派。这样 shell 入口仍然够薄，但不会因为一长串重复 case 再次把命令面和 live stage3 漂开。 |
| 新发现 | `profile-run` 这条线已经证明，单独再编一个 Cheng helper 可执行文件不等于“能力回到 Cheng”。如果 live stage3/backend-driver 根本没有同名命令面，最后只会得到一份不会被调用的 sidecar 代码；这类能力必须先在 seed/live dispatch 里坐实，再谈从 C 往 Cheng 迁。 |
| 新发现 | `profile-report` 这类 tooling 能力一旦已经进了 live compiler 命令面，seed 就不能再保留“再编一个 helper 再跑”的第二条实现。两条路径并存时，freshness、锁和 README 会一起漂；最稳形状就是 seed 统一直调 live compiler，自身只保留最小分发。 |
| 新发现 | `compiler_main.cheng` 里挂上一条新 dispatch，不等于当前 backend driver 真有这个 live 命令。`profile-report` 这轮已经证明，只要 seed/stage3 真实命令面没一起换代，`compiler_main` 里的同名入口就是假路径；最稳做法是先以 [cheng_v3_seed.c](/Users/lbcheng/cheng-lang/v3/bootstrap/cheng_v3_seed.c) 的 live dispatch 为准，确认命令真能被当前 `cheng.stage3/backend-driver` 直接调用，再决定要不要保留 Cheng 侧入口。 |
| 新发现 | 只收 [system_helpers_debug_trace_profile.c](/Users/lbcheng/cheng-lang/src/runtime/native/system_helpers_debug_trace_profile.c) 还不够，只要共享宿主大 runtime [system_helpers.c](/Users/lbcheng/cheng-lang/src/runtime/native/system_helpers.c) 还保留 `.v3.map` 文本加载或 `CHENG_V3_PROFILE_OUT` 最终报告，backend-driver 和旧 host runtime 就仍然背着第二套依赖面。要真收口，必须同步改 shared runtime，再让 gate 直接禁止旧词面回流。 |
| 新发现 | `system_helpers.c` 和 [system_helpers_debug_trace_profile.c](/Users/lbcheng/cheng-lang/src/runtime/native/system_helpers_debug_trace_profile.c) 不能只做到“词面上都删掉旧依赖”。只要 shared runtime 还手抄一份独立的 trace/profile 实现，两边迟早再漂；真正稳定的收法是让 [system_helpers.c](/Users/lbcheng/cheng-lang/src/runtime/native/system_helpers.c) 直接复用同一份 [system_helpers_debug_trace.inc](/Users/lbcheng/cheng-lang/src/runtime/native/system_helpers_debug_trace.inc) / [system_helpers_debug_profile.inc](/Users/lbcheng/cheng-lang/src/runtime/native/system_helpers_debug_profile.inc)，把旧本地实现物理删掉。 |
| 新发现 | 共享了 `trace/profile` 还不够，只要 `register_line_map` 和 `dump_backtrace` 入口桥还在 [system_helpers.c](/Users/lbcheng/cheng-lang/src/runtime/native/system_helpers.c) 与 [system_helpers_debug_trace_profile.c](/Users/lbcheng/cheng-lang/src/runtime/native/system_helpers_debug_trace_profile.c) 各留一份，同样会在后续改 `argv0/self-path/init` 流程时再漂。最稳做法是再抽一层共享 [system_helpers_debug_entry.inc](/Users/lbcheng/cheng-lang/src/runtime/native/system_helpers_debug_entry.inc)，再让 gate 直接检查两边都包含它。 |
| 新发现 | 入口桥共享以后，shared runtime 里那套 `host-only base` 也不能继续单独留着。像 `flag/env/path/write/getter resolve` 这种底座如果 [system_helpers.c](/Users/lbcheng/cheng-lang/src/runtime/native/system_helpers.c) 和 [system_helpers_debug_trace_profile.c](/Users/lbcheng/cheng-lang/src/runtime/native/system_helpers_debug_trace_profile.c) 不共用同一份 [system_helpers_debug_host_base.inc](/Users/lbcheng/cheng-lang/src/runtime/native/system_helpers_debug_host_base.inc)，后面改一边另一边迟早再漂。 |
| 新发现 | 这轮 `ordinary fixture compile failed` 的真根不是源码，而是 `artifacts/v3_bootstrap` / `artifacts/v3_backend_driver` 没完全换代。只要 seed/runtime 大改过一轮，重新验证时最稳做法就是直接清掉这两份生成物强制重建；只看脚本打印 `ok` 不够。 |
| 新发现 | `host-only base` 真收口以后，gate 也必须直接检查两边都包含 `system_helpers_debug_host_base.inc`。只靠人记得“现在是共享的”不够，下一轮一旦有人在 [system_helpers.c](/Users/lbcheng/cheng-lang/src/runtime/native/system_helpers.c) 里手抄回一份 env/path/write/getter resolve，结构上又会漂回两套实现。 |
| 新发现 | `system_helpers.c` 和 `system_helpers_debug_trace_profile.c` 就算已经共享了 `host_base`、`entry`、`trace`、`profile`，只要 crash trace、embedded line-map getter、raw profile 的常量、typedef 和 static global 还各写一份，本质上仍然是第二套 live 状态实现。最稳做法是把这层也抽成同一份 [system_helpers_debug_state.inc](/Users/lbcheng/cheng-lang/src/runtime/native/system_helpers_debug_state.inc)，再让 gate 直接检查两边都包含它。 |
| 新发现 | `profile` 收口以后，`crash` 也不能继续让 C runtime 直接吐最终 `[cheng-v3] v3_crash_report_v1`。只要 runtime 还负责最终崩溃文本格式，seed/stage3 的 live 命令面和宿主桥就还是两条语义；最稳做法是 runtime 只写 `v3_crash_raw_v1` 原始材料，再让 live `crash-report` 统一生成最终报告。 |
| 新发现 | 删 `v1/v2` 旧代码不能只看 `v3` 有没有 import。像 [src/std/system.cheng](/Users/lbcheng/cheng-lang/src/std/system.cheng) 和 [src/std/cmdline.cheng](/Users/lbcheng/cheng-lang/src/std/cmdline.cheng) 这种 shared std 模块，虽然名字不在 `v3/` 目录下，实际仍被 `v3` 主线大量直接依赖；真正适合一口气删的，是 [v2/cheng-quic](/Users/lbcheng/cheng-lang/v2/cheng-quic) 这种目录里全仓零引用的 tracked Mach-O probe 和临时 linkobj 产物。 |
| 新发现 | 仓库当前没有 tracked `v1/` 树；`v2/tests/contracts/*.expected` 和一批 `v2/tests/contracts/*probe.cheng` 仍被 [v2/bootstrap/cheng_v2c_tooling.c](/Users/lbcheng/cheng-lang/v2/bootstrap/cheng_v2c_tooling.c) / [v2/src/tooling/cheng_tooling_v2.cheng](/Users/lbcheng/cheng-lang/v2/src/tooling/cheng_tooling_v2.cheng) 直接引用，[v2/vendor/cheng-quic](/Users/lbcheng/cheng-lang/v2/vendor/cheng-quic) 也是 live symlink。删 `v2` 时必须按全仓真实闭包判断，不能把 contracts、vendor 或 legacy 入口误当垃圾文件。 |
| 新发现 | 按你定义的口径，“不在 `v2/`、`v3/` 里的 shared 代码都算 `v1`”，但真正适合直接删的只是一类很窄的文件：`src/*.cheng` 中既不在 `v2/v3` import 图里，也没有任何全仓显式路径引用的内部模块。`src/std/*`、`src/runtime/native/*`、`src/web/*`、`src/tests/*` 即使当前零仓内调用，也更像共享/外部面，不能跟内部旧编译链模块混着一把删。 |
| 新发现 | 清 `src/std/*`、`src/web/*` 时，光看“没有全路径引用”还不够，必须再补一层“模块名精确 import 为零”。像 `std/syncio`、`std/net/stream/connection` 这种文件，虽然没有人写全路径字符串，但仍会被脚本字符串或 `v2` 测试直接 `import`；真正能删的，是 `std/math`、`std/tables_compat`、`std/unicode`、`web/runtime/runtime` 这类连模块名导入都为零的文件。 |
| 新发现 | `src/web/examples` 这类仓内零 import 目录，不等于就能删。只要 README 还把它当公开示例面暴露，它就是对外 surface；像当前的 `counter/router_app/server_demo`，即使没有 live import，也应该保留。 |
| 新发现 | `.meta` 这类 sidecar 不能只按“没被 import/显式路径引用”判断。像 `backend_driver_currentsrc_sidecar_wrapper.sh.meta`，wrapper 仍按 `$0.meta` 约定直接读取，它本身就是 live 合同。 |
| 新发现 | `src/backend/tooling` 下的 stage0/compat/proof 残留 C 文件，要同时过三道门才适合删：basename 零引用、全路径零引用、并且没有通配构建入口把它们扫进 native 源集。像 `backend_driver_stage0_backend_main_shim.c`、`backend_driver_stage0_compat_wrap.c`、`backend_driver_proof_runtime_bridge_min.c` 这种三道都过了，才属于真死代码。 |
| 新发现 | `backend_driver_uir_sidecar_runtime_compat.c` 这种“只在 README 和 PURE-01 禁词里出现”的旧 compat 文件，和 live 构建输入不是一回事。只要构建入口、source list、helper script 都不再引用源码本体，就应该删源码本体，同时保留禁词扫描，防止以后又把这条 compat 旁路加回来。 |
| 新发现 | `sets.cheng` 这类 basename 很容易被 `hashsets.cheng` 误撞。清共享模块时，不能拿模糊 basename 命中当 live 证据；真正该看的是精确 import、精确路径，以及是否还在 README 公开面里。 |
| 新发现 | `web/cli/*` 和 `web/examples/*` 就算仓内零 import，也不能跟普通共享模块一锅端。只要 [src/web/cli/README.md](/Users/lbcheng/cheng-lang/src/web/cli/README.md) 和 [src/web/examples/README.md](/Users/lbcheng/cheng-lang/src/web/examples/README.md) 还把它们当公开入口，它们就是对外 surface。 |
| 新发现 | `std/c`、`std/hashes`、`std/system_c` 这种模块就算没有 import，也可能在 `stage1` 留着旧路径特判；`std/syncio` 这种则可能只剩 `verify_std_layout_sync` 的硬清单。清这类 shared std 时，除了删源码，还要把编译器特判和布局门禁里的陈旧名字一起删掉，不然就是假收口。 |
| 新发现 | `src/backend/tooling/*.cheng` 这层不能沿用 C sidecar 的“basename/path 为零就删”。像 `backend_driver_runtime_keepalive.cheng`、`backend_driver_uir_exports_keepalive.cheng` 虽然没有路径字符串命中，但会被 `import backend/tooling/...` 直接吃进去；这层必须按“精确模块 import + 精确路径 + basename”三道门判死活。 |
| 新发现 | 缩 `src/runtime/native` 最稳的方式不是继续在 `v2/bootstrap/tooling` 的 source list 里散着一堆小 bridge `.c`，而是保持符号名不变、把多个 live 小桥收成少数正式 provider `.c`，旧实现退回 `.inc`。这样 source list 只需要追少数正式 provider 路径，bootstrap/tooling/current runtime 三条链不会再一起漂。 |
| 新发现 | `selflink_cmdline_bridge` 和 `selflink_str_eq_bridge` 这种“小而稳定、只被 source list 当 provider 吃”的文件，不该继续各自占一个正式 `.c`。最稳形状是收成同一个 `selflink_support_bridge.c`，旧实现退回 `.inc`，这样 `v2/bootstrap/tooling` 的 live source list 能再少一层。 |
| 新发现 | `selflink_exe_entry_bridge` 和 `program_exe_entry_bridge` 不能靠 runtime 猜 argv 或弱符号去合并。最稳做法是保留一份 `system_helpers_selflink_entry_bridge.c`，再由 `system_link_exec/bootstrap` 在 program track 编译时显式加 `-DCHENG_SELFLINK_PROGRAM_ENTRY=1`。这样 track 选择仍然是编译期决定，不会把 tooling/program 入口语义混起来。 |
| 新发现 | `system_helpers_float_bits.c` 这类“live runtime 已经内建同名符号、只剩验证脚本还在单独拼接”的文件，应该直接删掉并把验证层改回 live 实现。继续保留这种 sidecar，只会制造第二条假的宿主 ABI 依赖面。 |
| 新发现 | 拆 `system_helpers_stdio_bridge.c` 时，最稳的第一刀不是碰 `driver_c` 解释器主循环，而是先抽只读 `machine_target_*` 常量桥。这一簇只依赖 `ChengStrBridge` 和 `driver_c_str_eq_raw_bridge`，最容易独立成正式 provider，并且能一次性追平 `runtime_provider_object_v2 / system_link_exec_v2 / cheng_v2c_tooling` 三条 live source list。 |
| 新发现 | `system_helpers_stdio_bridge.c` 里第二批适合抽离的，不是 selfhost 或 payload 入口，而是 `absolute_path/join_path/write_text/compare_files/plan field` 这类纯工具桥。它们虽然依赖的 helper 比 machine-target 多，但闭包仍然停在 `path/file/text` 这一层，不需要碰 `driver_c` 解释器状态。 |
| 新发现 | `system_helpers_stdio_bridge.c` 再往下拆时，第三刀优先抽 `print_usage/print_status/tooling-selfhost-check/program-selfhost-check/compiler_core_tooling_local_payload` 这簇 tooling entry，不要先碰还绑着 `driver_c_prog_current_frame`、`abort_with_label_output`、`build_stage_artifacts` 的 selfhost 主入口。只要导出闭包还需要解释器栈状态，就不适合先独立成 provider。 |
| 新发现 | `system_helpers_stdio_bridge.c` 第四刀可以抽 selfhost host 主入口，但不能把程序运行时 builtin helper 一起带走。像 `driver_c_die_errno`、`driver_c_parent_dir_inplace` 这种虽然挨着 selfhost 代码，实际仍被 `driver_c_prog` builtin 直接调用；一旦跟着搬走，剩余 `stdio bridge` 会先在单编译阶段就红。 |
| 新发现 | `driver_c_dump_prog_stack_to_stderr_bridge` 这种为了切 selfhost host 闭包新增的弱导出，声明必须先于 `driver_c_die(...)`。不然 C99 会先把它当隐式声明，再在真正定义处报冲突，症状看起来像 provider 切分错了，真根只是声明顺序。 |
| 新发现 | `system_helpers_stdio_bridge.c` 每再切一刀，最稳流程都是“先单编译剩余 `stdio bridge`，再单编译新 provider，再重建 backend driver 和 host smoke”。这一层如果直接跳到全量 build，最容易把还留在程序运行时里的 helper 缺口误判成 source list 或 bootstrap 问题。 |
| 新发现 | 把宿主弱桥从 `system_helpers_stdio_bridge.c` 挪到独立 provider 时，不能只搬定义，还要把原型留在 `stdio bridge` 顶部。像 `cheng_fopen/cheng_fclose/cheng_fread/cheng_fwrite` 这类符号，程序运行时 builtin 仍会在文件后半段直接调用；没有前置原型，C99 会立刻报隐式声明。 |
| 新发现 | `cheng_fopen/get_stdout/cheng_file_exists/driver_c_read_file_all` 这类宿主 `stdio/fs` 弱桥和 `driver_c_prog` 状态完全解耦，适合作为 `system_helpers_stdio_bridge.c` 第五刀。先把这层抽掉后，剩余文件就更清楚地只剩解释器和 program 入口，不会再和宿主 IO 混在一起。 |
| 新发现 | 当剩余文件已经基本只剩 program runtime 时，继续沿用 `system_helpers_stdio_bridge.c` 这个旧名字只会制造认知噪音。最稳做法是直接把 live provider 正式换名为 `system_helpers_program_runtime_bridge.c`，再把旧路径降成 fail-fast stub，这样任何陈旧 source list 都会在编译期立刻暴露。 |
| 新发现 | provider 大换名时，不要先改文档再猜 live 入口是否追平。最稳流程是先用 `rg` 把 live 代码面旧路径清零，再单编译新 provider、单编译 `cheng_v2c_tooling.c`、重建 backend driver、跑 host smoke；全绿以后再把“正式换名”写进记录。 |
| 新发现 | 当前 `React.js` 已经漂过旧 truth trace 时，`home_default` 不能再按 `app/App.tsx` 的静态 `jsx_elements.len` 当 base snapshot。最稳做法是：只要 `compile` 显式带了 `truth_trace_v2`，`codegen-surface` 就直接把 `home_default` 和全量 route catalog 压成 truth-seeded 语义面；否则单态 `truth compare` 会先卡在 `92 vs 83`，多态 route matrix 还会在 `home_app_channel/node_detail/group_draft/...` 上白跑整批静态猜数。 |
| 新发现 | controller 读 `*.summary.env` 不能继续复用当前 `startsWith` 辅助函数做 key 判断。这轮 `route_count` 已经验证它会把别的 `*_matrix_*` 键误认成目标行；最稳口径是“切头定长 slice，再做精确相等”。 |
| 新发现 | `path/file/plan` 这簇 helper 真想从 live C provider 里撤掉，不能只改 `.cheng` 源，还要先查 [v2/bootstrap/cheng_v2c_tooling.c](/Users/lbcheng/cheng-lang/v2/bootstrap/cheng_v2c_tooling.c) 的真实依赖面。这轮验证它只剩 source list 和 symbol whitelist，没有深层调用；最稳顺序就是“先把 Cheng helper 补齐，再删 live source list 和白名单，最后把旧 provider 降成 fail-fast stub”。 |
| 新发现 | `v3` 源码面继续收 `path/file` helper 时，不能想当然把 [std/os::fileExists](/Users/lbcheng/cheng-lang/src/std/os.cheng) 当成当前 seed ordinary lowering 一定能吃的稳定形状。这轮 `chain_node_cli` 真 smoke 首次直接炸出 `scalar call resolve failed callee=os.fileExists`；最稳替代是用已经长期跑在 `std/os.readFile` 主链里的 `os.openRead + os.close` 自己实现存在性判断，再继续把上层 `v3path` 和 `chain_node_cli_core` 收回 Cheng。 |
| 新发现 | `std/cmdline` 不能无脑整包拉进所有 ordinary fixture 编译链。`tailnet_control_core` 这轮已经证明：只要走到 `std/cmdline.parseInt32 -> cmdTrimAsciiSpace -> cmdIsSpace`，seed ordinary fixture compile 仍可能在 `if c == ' ':` 这类 `char` 条件上直接报 `primary object body semantics missing`。最稳做法是只借 `readFirstFlagValue` 统一 flag 语义，int32 解析继续在调用侧用本地纯 Cheng helper 收掉。 |
| 新发现 | `chain_node_cli` 里 `v3ChainNodeCliReadRequiredInt64Flag` 之前的签名和调用是错位的：定义只返 `Result[int64]`，调用却一直按 `var out` 读 `amount`。这种问题如果只看编译或只看一条 wrapper 很容易漏过去；最稳修法是把 helper 明确收成 `Result[bool] + var out`，再让 wrapper 直接回到 `cli.v3ChainNodeCmd*Main()`，避免同一套读参逻辑在多个 `*_main` 里继续复制。 |
| 新发现 | `selfhost_host` 里像 `exec_file_capture_or_panic`、`compare_text_files_or_panic` 这种只有“起进程抓输出 / 对比文本文件”职责的薄桥，只要 `std/os` 已经有同语义正式 API，就不要再维持一份平行 C 入口。最稳顺序是先把 Cheng 调用点改到 `std/os`，再把 `system_helpers.h`、native provider 和 bootstrap/machine whitelist 一起删干净；只停在“源码不再 import”会把死 bridge 留成回流口。 |
| 新发现 | 这次把 `quic_tls_transport_ecdsa_smoke` 顶到 `178GB+` 的真根，不是递归展开源码，也不是 `p256` 乘法表初始化，而是 `ecdsaSignBytes(...)` 里的 `nModInv(k)` 还在走通用 `bigModInvOddBinary(...)`。这条旧路径一旦不收敛，就会不断创建新 `BigInt` 把整机拖进 swap；对 `P-256` 这种固定素数模数，最稳做法是直接改成固定模数 Montgomery 指数逆元，并给通用逆元加硬步数上限做 fail-fast 保险。 |
| 新发现 | `msquic` listener 不能只记住请求绑定的 `/udp/0` 地址，启动成功后必须立刻把 datapath 返回的真实端口回写到 transport/session。否则上层看起来像“listener 已启动”，但 client 实际仍在往 `0` 端口拨号，症状就会稳定卡在 `msquic_native_dial_stage=client_hello_ok`。 |
| 新发现 | UDP datapath 的 canonical 地址不能一半是 `udp://...`、一半是 multiaddress。只要 bind/recv 两侧格式不统一，`parseMultiAddress(...)` 就会在 server session 初始化、peer dial 地址回填和 accept 元数据上随机掉空；最稳口径是 bind/recv 全部统一回 `/ip4|ip6/.../udp/.../quic-v1`。 |
| 新发现 | `driver_c_prog` 运行时里不要直接碰 `std/os.cheng_close_fd` 这类未公开字段。像这轮 [v2/cheng-quic/src/platform/datapath_udp.cheng](/Users/lbcheng/cheng-lang/v2/cheng-quic/src/platform/datapath_udp.cheng) 的 stop 路径，前半段握手和同步都过了，最后却在 unbind 时因为字段没暴露而 panic；最稳做法是只走已经正式导出的 `std/os.udpCloseFd(...)`。 |
| 新发现 | `v3/docs/移动端硬件调用.md` 里虽然写了 `biometric`/`nfc` typed API 叙事，但当前仓库并没有 live 的 `mobile/hardware/biometric` Cheng 模块可直接唤起手机指纹硬件。最稳口径不是伪造桥接，而是让宿主先完成系统指纹验证，再把 `verified + feature32 + deviceBindingSeed + deviceLabel` 交给 Cheng 侧正式编排链。 |
| 新发现 | `chain_node` 当前不用改共识层，也能严格承载“公开 DID 文档 + 生物证明束”存证：做法是固定一个专用 `assetId`，把 bundle 文本切成 32 字节块塞进 mint 事件的 `fromParentCid`，并用 `fromAccount=bundleSeq`、`amount=chunkOrdinal` 编码顺序。因为 mint 共识只检查 `toParentCid`，但事件 `bodyCid` 会把 `fromAccount/fromParentCid` 一起提交，所以这条链是严格可回读、可验签、可 fail-fast 的，不是拍脑袋外挂字段。 |
| 新发现 | `v3 public QUIC` 当前剩余真 blocker 已经收敛到 server `accept` 这一跳：公网实测里 client 已稳定打出 `probe_stage=dial_ok`、`pipe_write side=client len=45`、`client_handshake_done`，server 也稳定打出 `server_finished_ok=true`、`accept_pending=true`、`data_frame side=server len=45`，但 `msquic_transport_accept_stage=native_ok` 始终不出现，说明握手和应用短包都已到位，卡点就是 accept loop 没把“可接收连接”状态收口成真正的返回。 |
| 新发现 | `v3 public QUIC` 在 `accept` 打通以后，`x86_64-unknown-linux-gnu` 还会继续卡在 app stream 收尾：现在 `64` 上已经稳定出现 `msquic_transport_accept_stage=native_ok`、`pipe_queue side=client batch=45 has_queued=true ack_only=false`、`recv_wire side=server len=61`、`data_frame side=server len=45`，说明握手、短包发送和 `DATA` 解包都已通过；但 server 仍不出 `pipe_read_ready side=server`，真 blocker 已缩到 `msquicNativeAppRecvAdd/msquicCryptoStreamAdd/msquicNativePipeRead` 这一层的纯 Cheng buffer 回写。 |
| 新发现 | 这轮 `x86_64-unknown-linux-gnu` 上真正把 QUIC proposal 主线卡死的，不是 `acceptPending` 之后的 app buffer 本身，而是 [udp_syscall.cheng](/Users/lbcheng/cheng-lang/src/std/net/transports/udp_syscall.cheng) 把 `MSG_DONTWAIT` 写死成 `128`。在 Linux 上这个值不是非阻塞标志，结果两边都静默卡进 `recvfrom(fd=3, flags=128)`，表面看起来像 `pipe_read`/`app_recv`/`select` 都失灵；把它收成平台导出以后，`64` 上 loopback 和公网 proposal 都已经稳定 `decode_ok`。 |
| 新发现 | [artifacts/v3_debug_public_quic/proposal.hex](/root/cheng-lang-deploy/artifacts/v3_debug_public_quic/proposal.hex) 原先只有 `00`，它只能验传输，不能验 proposal 解码。要做完整 proposal path 验收，必须先用 proposer 主线生成真 proposal 文件；这轮用 `validator-proposer-daemon` 写出的 [sample_proposer/proposal.hex](/root/cheng-lang-deploy/artifacts/v3_debug_public_quic/sample_proposer/proposal.hex) 大小是 `271` 字节，之后 `probe-proposal` 才真正读回 `proposal_height=1 / proposal_txs=1`。 |
| 新发现 | 当前编译器还吃不稳“导入模块里再调 provider-exported Cheng 符号”这条链。`tcp_bridge_provider_shared.cheng` 一旦接进 live 主线，就会在 `@importc` 包装调用上报 `scalar call resolve failed`；最稳收法不是硬留共享 wrapper，而是只共享 [tcp_bridge_abi.cheng](/Users/lbcheng/cheng-lang/v3/src/runtime/tcp_bridge_abi.cheng) 这份 ABI 声明，Darwin/Linux final wrapper 继续各自在本地 provider 收口。 |
| 新发现 | `run_slice_gate.sh` 这类长链 gate 补完新回归点后，一定要真从头跑到尾并归档完整日志。这轮就是靠 [run_slice_gate.full_after_tcp_abi.log](/Users/lbcheng/cheng-lang/artifacts/run_slice_gate.full_after_tcp_abi.log) 先炸出 `_cheng_v3_udp_platform_msg_dontwait_bridge` 缺口，修完以后又把剩余问题精确收敛成 stage2 专属 [libp2p_webrtc_sync_smoke.stage2.run.log](/Users/lbcheng/cheng-lang/artifacts/v3_stage23_libp2p/libp2p_webrtc_sync_smoke.stage2.run.log)；不跑全链，后面的随机 smoke 会一直把真红灯遮住。 |
| 新发现 | 目标如果只是复验 `v3 public QUIC` 的公网 proposal 读回，不要重跑整套 `deploy-bft-validator-three-node`。最短真链路已经坐实成：先确认本机 [bft_state_machine](/Users/lbcheng/cheng-lang/artifacts/v3_bft_state_machine/bft_state_machine) 直接 `self-test ok`，再把 `64` 上已经验过的 x86 Linux [bft_state_machine](/root/cheng-lang-deploy/artifacts/v3_bft_state_machine/x86_64-unknown-linux-gnu/bft_state_machine) 同步给 `dmit`，在 `64` 用真实 [sample_proposer/proposal.hex](/root/cheng-lang-deploy/artifacts/v3_debug_public_quic/sample_proposer/proposal.hex) 起 `proposal-serve-daemon`，最后让本机和 `dmit` 各自跑 `probe-proposal` 读回 `decode_ok`。这样钉住的是客户端真读链，不会再被 `dmit` 上 `build-backend-driver` 的长链噪音拖住。 |
## Findings

| 项目 | 结论 |
|---|---|
| 新发现 | `r2c-react-v3` 这条 `native_gui_runtime` 如果继续走 ordinary `system-link-exec` 可执行，当前 ordinary lowering 仍会在 `str/composite` builder、状态组装和运行时文本生成上报 `primary_object_body_semantics_missing`。这轮已经把真阻塞收敛到 [native_gui_runtime_main.report.txt](/tmp/r2c-native-render-runtime-stage3-bundle-v1/native_gui_runtime_main.report.txt) 里的 6 个函数，不是 `/Users/lbcheng/UniMaker/React.js` 业务源码，也不是 AppKit host 递归或内存泄露。 |
| 新发现 | WebRTC bridge 这条链一旦继续跨 `@importc` 走 `var str` 出参回写，stage2 和 stage3 会各炸一半：stage2 会在 primary object 上把同函数里的多条表达式字符串撞成同一个局部符号，stage3 ordinary 会在 `const str` 直接做 composite 实参时报 `composite-arg local missing`。当前最稳口径是桥返回 buffer handle，错误文本先落本地槽，再把本地槽传进 `cheng_webrtc_last_error_set(...)`。 |
| 新发现 | 这轮 [run-stage23-libp2p-smokes](/Users/lbcheng/cheng-lang/v3/tooling/run_v3_stage23_libp2p_smokes.sh) 尾段里 `run-migration-gate publish-blocked failed` 那两行不是 live 红灯，而是 gate 主动验证“stable proof 没带 baseline 时必须拒绝 publish”的预期失败路径；只看中途 stderr 很容易误判，真口径只认最后的 `v3 migration gate ok` 和总收尾 `v3 stage2/stage3 libp2p: ok`。 |
| 新发现 | 只改 [system_link_exec.cheng](/Users/lbcheng/cheng-lang/v3/src/backend/system_link_exec.cheng) 还不够，活跃的 backend driver 仍然会沿用 [cheng_v3_seed.c](/Users/lbcheng/cheng-lang/v3/bootstrap/cheng_v3_seed.c) 里那份旧 `v3_provider_source_for_module(...)`。这轮就是先在源码侧把 provider 换成 Cheng 以后，`object_native_link_plan_smoke` 报告仍旧吐出旧 `.c` 路径；必须把 seed 里的同一逻辑一起追平，再重建 backend driver，执行面才会真切过去。 |
| 新发现 | `program_support_backend_v3` 里补 `cheng_exec_cmd_ex` 这种普通 helper 时，当前 seed 仍然吃不稳“多条裸调用语句 + 字面量拼接”的形状。最稳口径不是在一个 helper 里连写多次 `cheng_bytes_copy(...)`，而是像这轮最后这样把命令拼接收成 `cheng_raw_concat_export(...)` 的赋值链，避免再次撞上 `emit lvalue infer failed`。 |
| 新发现 | `cheng_exec_cmd_ex` 这类 provider ABI 只要还通过 `var int64` 把退出码回写给 ordinary/controller，当前主线上就可能直接在 provider 入口把 `exitCode` 指针写炸，表现成 `EXC_BAD_ACCESS address=0x100000000/0x100000017`，而且 fresh `native-gui-bundle` 和 direct `native-gui-runtime` 都会中。最稳收法是像这轮这样把 ABI 改成“capture 返回输出 + 单独读取 `last_exit_code`”，彻底删掉这类出参写回。 |
| 新发现 | `r2c-react-v3` 的 direct `native-gui-runtime` 本质上只需要 helper 退出码，不需要抓 stdout。最稳口径不是继续复用 `execCmdEx`，而是让 controller 走 `execCmdStatus(...)`，再从 `native_gui_runtime_direct_v1.json` 取真结果；这样 direct runtime 和 bundle/runtime 主链会彻底解耦。 |
| 新发现 | [cheng_v3_seed.c](/Users/lbcheng/cheng-lang/v3/bootstrap/cheng_v3_seed.c) 里这类冷启动 helper 只要会在第一次调用前用到本地函数，就必须把前置声明放到第一次调用前。像这轮 `v3_host_target_triple()`，平时用现成 `stage3` 看不出来，但一旦 fresh 走到 `bootstrap-bridge stage0_cc`，隐式声明就会把整条 `native-gui-bundle` 冷启动打死。 |
| 新发现 | `run-migration-gate` 里的 `publish-blocked` 是“预期拒绝 stable publish”的正路径，不能继续用会在非零退出时主动向 stderr 喷 `failed rc=...` 的执行器。像这轮 [cheng_v3_seed.c](/Users/lbcheng/cheng-lang/v3/bootstrap/cheng_v3_seed.c) 之前那两行 `[cheng_v3_seed] run-migration-gate publish-blocked failed ...`，本质上只是 gate 在验拒绝规则，不是真红灯；真正口径只认最后的 `v3 migration gate ok`。 |
| 新发现 | `compile-bootstrap` 生成的 wrapper 不能继续写相对 `#include "v3/bootstrap/cheng_v3_seed.c"`。预处理会先按 generated 文件所在目录找，像这轮 [cheng.stage1.generated.c](/Users/lbcheng/cheng-lang/artifacts/v3_bootstrap/cheng.stage1.generated.c) 就会天然在 `artifacts/v3_bootstrap/` 下报 `file not found`；最稳口径是生成时直接写绝对 `seed.c` 路径。 |
| 新发现 | 当前 seed 发射器对 [program_support_backend_v3.cheng](/Users/lbcheng/cheng-lang/v3/src/runtime/program_support_backend_v3.cheng) 这类 provider helper，仍然不稳地吃不下“`for/if` 嵌套里调用 `cheng_str_copy_cstring(...)` 做 JSON 转义”的形状。像这轮 `cheng_v3_quote_json_text(...)`，stage2/stage3 都会报 `emit if/for nested stmt failed`；最稳收法是直接写字节缓冲，不要在热循环里再做字符串 helper 调用。 |
