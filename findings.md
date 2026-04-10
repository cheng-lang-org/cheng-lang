## Findings

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
