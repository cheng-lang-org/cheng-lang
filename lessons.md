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
- `primary object` 现阶段对大复合值最容易在“直接返回”“直接塞进 `Ok[...]`”“直接写 struct field”这三类形状上搬坏。热路径里优先改成 `into` + limb 拷贝，尤其是 `EcPoint / EcPointJacobian / BigInt` 这种大对象。
- `v3` seed 里的递归 helper 不能在栈上放 `V3ImportAlias[128]` 这种大数组。像 `v3_compute_type_layout_impl`、`v3_resolve_field_meta_impl` 这类递归函数，一旦沿着宿主大类型链展开，host compiler 自己会先栈爆；这类工作缓冲区必须搬到堆上或做外置缓存。
