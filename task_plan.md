## 当前任务

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
| 主线 | 这轮已经在 `src/runtime/native/system_helpers_stdio_bridge.c` 里给 `TypeDecl/ZeroPlan` 增加共享 `field_lookup`，并让 `zero_value_from_plan()/zero_record_shell_from_plan()` 直接复用，不再为 typed record 每次重建 field lookup。随后 `compiler_core_system_link_exec/program_selfhost/full_selfhost` 全部在新 fixed point `manifest_fnv1a64=e4aa17192dd2cde3` 下前台重新收口。下一刀不再碰这条已闭合链，直接打固定布局 `slot/shape` 和 aggregate `field/index update copy`。 |
| 文件 | `src/runtime/native/system_helpers_stdio_bridge.c` `v2/tests/contracts/compiler_core_system_link_exec.expected` `v2/tests/contracts/program_selfhost.expected` `v2/tests/contracts/full_selfhost.expected` `v2/docs/自举和性能.md` `task_plan.md` `progress.md` `findings.md` |
| 不做 | 不回头再做 `field name lookup` 兜底；不继续 rebuild typed record lookup；不把已过的 `program-selfhost/full-selfhost` 再当成当前主根 |
| 验收 | `make -j1 -C /Users/lbcheng/cheng-lang/v2/bootstrap compiler-core-release compiler-core-system-link compiler-core-system-link-exec tooling-selfhost lsmr-contracts selfhost full-selfhost` 必须前台通过；`pubkey/sign/mul/kinv` probe 必须以前台重编后的二进制重新量到真实中位数。 |
