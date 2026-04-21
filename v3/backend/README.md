# v3/backend

这里只做目标后端和对象/链接计划。

硬规则：

- 先打穿 `arm64-apple-darwin`
- 只产直接调用机器码
- 不再产 `local_payload / exec_plan_payload / entry_bridge`
- 固定 spill / call spill / address spill 一律走稳定 frame base；`sub sp` 只允许给当前临时 scratch 用，不能再拿漂移后的 `sp` 反推旧槽地址。
- ORC record 生命周期里，父地址必须先落稳定 spill，再按字段重载；`retain/release` 这类 helper 会 clobber 临时寄存器，不能把调用后的寄存器值再当父地址真源。
- 编译“理论下界”统一看 `artifacts/v3_perf_memory_contract/<label>/perf_memory_contract.report.txt` 里的 `*_compile_exec_phase_summary`，实际引用只认 `planner_total_ms <= compile_elapsed_ms` 的样本。
- 内存性能统一看同一份 perf 合同里的 `orc_perf_contract` 行和 `orc_perf_contract_smoke.run.log`；Cheng 这里没有 tracing GC，口径只谈 ORC retain/release 与 alloc/free/live。
