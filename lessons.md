2026-04-03
- 不要用长时间探测命令反复直接拉起 `v2/artifacts/bootstrap/cheng_v2_system_link_exec`。优先用 `cheng_v2c` 做只读检查；必须跑 `cheng_v2_system_link_exec` 时，只跑前台、一次性、可立即验收的命令，不留后台或悬挂会话。
2026-04-06
- 性能统计里的“第一梯队”目标，必须按同机 C 1:1 记，不准私自放宽成 `2x` 或拿特例输入伪装成常规基线。
2026-04-09
- `progress.md` 从这天起只记录开始时间；完成时间不在文档里写，由用户自行统计真实时间。历史条目不追改。
2026-04-10
- 用户明确要求文档目录用 `v3/docs`，不是 `v3/doc`；后续 `v3` 新文档一律落到 `v3/docs`。
2026-04-13
- 用户要的是“保留原叙事，再补当前实现”，不是把文档直接改写成纯事实稿。后续更新叙事型文档时，默认追加 `当前实现补充`，不要整篇覆盖。
2026-04-14
- 用户明确要求“纯 Cheng 是最终目标”时，不能把 `libc memcpy/memset/memcmp` 这类临时绕路当完成态。先用它们定位根因可以，但最后必须回到修发射器/修语义面，再把 runtime provider 退回纯 Cheng 实现。
- 用户已明确：`/Users/lbcheng/UniMaker/React.js` 后续允许按打通编译链的真实需要直接修改；但不是默认随手改，只有遇到硬阻塞才动。
- `primary object` 现阶段对大复合值最容易在“直接返回”“直接塞进 `Ok[...]`”“直接写 struct field”这三类形状上搬坏。热路径里优先改成 `into` + limb 拷贝，尤其是 `EcPoint / EcPointJacobian / BigInt` 这种大对象。
- `v3` seed 里的递归 helper 不能在栈上放 `V3ImportAlias[128]` 这种大数组。像 `v3_compute_type_layout_impl`、`v3_resolve_field_meta_impl` 这类递归函数，一旦沿着宿主大类型链展开，host compiler 自己会先栈爆；这类工作缓冲区必须搬到堆上或做外置缓存。
- `var T` 形参在 `v3` 里是“值类型为 `T`、传参 ABI 为 `ptr`、本地槽 `indirect_value=true`”这一整套语义，不能只记 `param_is_var` 却还按标量值槽落栈。否则入口会把地址截成 `bool/i32`，裸本地赋值也会改坏槽本身而不是回写到目标地址。
- 固定数组类型规范化不能只认字面量长度。像 `bool[oracle_types.v3OracleCommitteeMax]` 这种常量长度，必须先保留 `[]` 里的表达式文本，再走 top-level const 求值，最后写回成确定长度的 canonical type text。
- `consteval entry` 不能无条件吃掉 `exe` 主路径。只要 source closure 里还有顶层 `var ... = ...` 初始化，就必须保留完整 lowering 发射；不然模块 init 还会引用那些初始化 helper，最后在 native link 阶段掉未定义符号。
- failfast 并行 job 日志不能和 smoke 可执行文件共用同前缀。`run_v3_compile_exe.sh` 会做 `rm -f "$bin".*`，如果 job log 也叫 `$bin.log`，失败时外层只会看到“日志不存在”。
- ready-file 握手的等待窗必须和 watchdog 同量级；2 秒轮询在 gate 满载时会把正常 server 误判成启动失败。`chain_node/libp2p tcp` 这类双进程 smoke，ready 轮询至少要覆盖整个 20 秒 watchdog 窗口。
- 用户已明确：`cheng_v3.sh` 的主要命令能力已经收进 `cheng.stage3`。后续讨论“零脚本开发”时，不能再把 `cheng_v3.sh` 当成核心能力缺口；真正剩下的是 bootstrap 编排、并行 gate、宿主工具链和外部运行环境收口。
- `tailnet` 的 headscale probe 不能再把 `http://` 端点直接当空串；纯 Cheng runtime 需要自己做本地 HTTP GET、验状态行、切 body。否则 `tailnet_control_smoke` 这类真实流程会在 provider runtime 上静默掉回 `init`。
- `program_support_backend_v3` 里别把长串请求报文写成一条内联 `\"a\" + x + \"b\" + y`。这一形状在当前主线上会被误编成字面串，server 直接收到 `GET \" + path + \" HTTP/1.0`。请求报文要按步骤追加。
- `build_backend_driver_v3.sh` 也是并发热点。`panic/bounds/signal/debug runtime` 会同时抢 `artifacts/v3_backend_driver`；没有锁时会互相截断 `cheng.generated.c` 和输出二进制，表现成随机的 `stage2 compile-bootstrap failed`。这个入口必须像 `bootstrap_bridge_v3.sh` 一样做锁和 freshness 快路径。
- generic Linux `aarch64` 这种跨目标链路不要再靠 README 记忆判断。先真跑 `build_linux_nolibc_exe_v3.sh` / `build_chain_node_linux_v3.sh` / `build_rwad_bft_state_machine_linux_v3.sh` 看产物和 report，再把结论写回 gate 和文档，不然最容易把“已经打通”的路径继续写成旧阻塞。
- `slice-gate` 里不要把已经包含在 `run-host-smokes` 和 `run-stage23-libp2p-smokes` 里的 `chain_node process/three-node` 再手工跑一遍。重复 gate 不增加覆盖，只会把 ready/log/bin 目录复用带来的串扰放大成假失败。
- `PPID=1 && state=U` 这类 UE 孤儿进程不能只在主入口修。凡是 `v3/tooling` 里直接 `system-link-exec`、直接跑产物、或者自己起后台 server 的叶子脚本，都必须统一走 `guarded_exec_v3.py`；只修 `cheng_v3.sh`、`bootstrap_bridge_v3.sh` 这层不够，旁路脚本一样会漏。
- `bootstrap_bridge_v3.sh` 也是并发热点。多个入口一起补 bootstrap 时，如果没有锁和 freshness 快路径，会把同一个 `cheng.bootstrap_bridge_runner` 抢坏，表面症状是“missing temporary seed runner”。这个入口必须先串行化，再判断 `seed/stage1` 是否真的比 `stage3` 新。
- 用户要的是“纯 Cheng 的零脚本开发”时，`cheng_v3.sh` 和各类 `build/run/verify` 外壳只能保留参数透传，不能再承载编译、进程编排和日志校验主逻辑；这些能力必须收回 `cheng.stage3`。
- `guarded_exec_v3.py` 的 CLI 只认空格参数：`--log PATH --timeout N`，不认 `--log:PATH --timeout:N`。seed 里凡是用 `system(...)` 拼 guard 命令，都必须按这个语法写，不然 `stage3` 看起来像 bootstrap/c-ref 坏了，真根只是参数格式错。
- `bootstrap.env` 存在不代表 `cheng.stage3` 还是新的。只要 `v3/bootstrap/cheng_v3_seed.c` 或 `v3/bootstrap/stage1_bootstrap.cheng` 比当前 `cheng.stage3` 新，wrapper 就必须先回 seed 外根重建；不能看到 env 在就直接信旧 stage3。
- `v3/src/tooling/gate_main.cheng` 里的新子命令写进源码，不等于当前 `artifacts/v3_bootstrap/cheng.stage3` 和 `artifacts/v3_backend_driver/cheng` 立刻就有同名入口。只要 bootstrap 产物还没完全换代，叶子 wrapper 就必须优先复用已经公开的 `run-host-smokes --tail-only:*` / `run-stage23-libp2p-smokes --tail-only:*` 或直接编译器调用，不能先假设新命令已经 live。
- `verify-orphan-guard` 不能只扫 `.sh`。`v3/tooling` 顶层无后缀入口和 `.py` 一样会把 Python/guard 旁路重新带回主线，守卫必须按“顶层入口文件全扫”来收口。
- `bootstrap_bridge_v3.sh` 这类带锁 wrapper 不能在持锁后直接 `exec` 真命令。`exec` 会让 shell 自己消失，`trap cleanup` 根本没机会释放锁；正确形状是“写 owner pid + 等待时回收陈旧 pid + 普通子进程执行 + shell 走到 EXIT 清锁”。
- 只要 seed/stage3 的某个命令实现还是 shell out 到旧脚本，就不能把那个旧脚本同时压成“纯转发到 `cheng_v3.sh`”的薄壳。`run-host-smokes / run-stage23-libp2p-smokes` 这类总 smoke 一旦两边都变壳，就会形成 `shell -> stage3 -> shell` 递归。
- `run-host-smokes / run-stage23-libp2p-smokes` 这类总 smoke 的 live 入口当前在 `v3/bootstrap/cheng_v3_seed.c`，不是只在 `v3/src/tooling/gate_main.cheng`。要把总 smoke 真从脚本拉回编译器，必须先改 seed 并重建 bootstrap，再去压薄对应 shell。
- 总 smoke 和叶子 smoke 要分开收口。总 smoke 必须先保证只有一个真实执行体，叶子脚本才适合随后压成纯转发壳；反过来做最容易再次制造 `shell -> stage3 -> shell` 递归。
- `verify-orphan-guard` 这种门禁不要用“图省事”的宽规则。普通 `python3` 辅助脚本并不等于旧 guard 旁路；真正该禁的是 `guarded_exec_v3.py / orphan_guard_run.sh` 这类已退役守护入口和它们的直接残留引用。误把普通测试探针也算违规，只会让门禁先坏在无关处。
- Cheng 侧和 seed C 侧同时维护同名门禁时，必须防两类 bug：一类是 Cheng 侧把目录当文件读，另一类是 seed 侧脚本 passthrough 把命令再绕回 `cheng_v3.sh`。`verify-orphan-guard` 这轮已经证明，少修任意一侧都会挂死。
- 叶子脚本能不能压成纯壳，不看意愿，只看 live 命令面。当前 stage3/backend-driver 没公开的叶子入口，宁可保留真实执行体，也不能先压成假薄壳再让 gate/smoke 全面回归。
- debug/runtime bridge 再缩时，`providerNativeSources`、`system_link_exec`、对象级 smoke 和 seed 里的 runtime source resolver 必须一起指向同一份 native 源文件；只改其中一处，桥接面会立刻分叉。
- `v3_cmd_system_link_exec` 这类 seed/backend-driver 入口绝不能把 `V3SystemLinkPlanStub / V3PrimaryObjectPlanStub / V3ObjectPlanStub / V3NativeLinkPlanStub` 这种大 plan 对象直接放栈上。Linux x86_64 默认 8MB 栈会在函数序言就撞死，表面看像 `system-link-exec` 随机段错误；这类大对象必须改成堆分配，再去看真正的 target/codegen 阻塞。
- `stage1_bootstrap.cheng`、`gate_main.cheng`、`compiler_runtime.cheng` 里的命令名补齐，不等于 live 的 `cheng.stage3` 已经有对应入口。真正的命令面以 `v3/bootstrap/cheng_v3_seed.c` 的 dispatch 为准；没改 seed、没重建 bootstrap 前，任何叶子壳都不能先压成纯转发。
- seed 里自己组 argv 跑 `execv` 时，数组最后一个槽必须显式写 `NULL`。像 `run-wasm-smokes`、`run-browser-host-wasm-smoke` 这种 Node harness，一旦漏掉结尾位，直接就是 `execv: Bad address`，不会给你温和报错。
- 长参数宿主命令不要再手写 argc 常量。`tailnet_control` 这轮已经证明，多一两个 flag 以后最容易把末尾关键参数静默截断；统一用 `sizeof(array)/sizeof(array[0])` 才是稳定写法。
- `v3/bootstrap/cheng_v3_seed.c` 自己也必须始终按严格宿主编译规则过。Darwin 下只用 `cc -std=c11 -pedantic` 编 seed 时，`strsep` 和 `st_mtimespec` 需要显式 `_GNU_SOURCE/_DARWIN_C_SOURCE`；这层不补齐，bootstrap 会先死在外根编译而不是死在 Cheng 逻辑。
- seed 里的 shell bridge helper 不能把命令构造和日志格式化混在一起。像 `v3_run_shell_command_logged(...)` 这种共享路径，只要 `snprintf(...)` 多塞一个实参，整条实际执行命令都会被污染，问题还会伪装成“下游脚本坏了”。 |
