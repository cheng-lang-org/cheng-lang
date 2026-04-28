# Cheng Core Tooling

`src/core/tooling` 是当前编译器命令面、bootstrap bridge、backend driver、gate 与发布闭环的唯一源码位置。

正式入口：

```sh
artifacts/bootstrap/cheng.stage3 bootstrap-bridge
artifacts/backend_driver/cheng build-backend-driver
artifacts/backend_driver/cheng status
artifacts/bootstrap/cheng.stage3 run-production-regression
artifacts/backend_driver/cheng run-host-smokes cheng_skill_consistency_smoke
```

生产回归集合：

```text
build_backend_driver_report_smoke function_task_contract_smoke function_task_executor_contract_smoke compiler_budget_contract_smoke runtime_c_baseline_contract_smoke perf_memory_gate_contract_smoke perf_memory_contract_smoke cheng_skill_consistency_smoke dev_hotpatch_100ms_scope_contract_smoke explicit_default_init_positive_smoke explicit_default_init_negative_smoke explicit_default_init_gate_smoke composite_zero_helper_gate_smoke latest_snapshot_port_preserves_live_str_smoke list_literal_nested_call_depth_smoke libp2p_quic_twoproc_server_pre_quic_smoke
```

内存与性能口径：

- `perf_memory_contract_smoke` 默认优先测 `artifacts/backend_driver/cheng`；只有显式 `CHENG_SMOKE_COMPILER` 才覆盖。
- Darwin 正式内存比较值优先用 `peak memory footprint`；`maximum resident set size` 只保留原始观测，不作为稳定合同阈值。
- `runtime_c_baseline_contract_smoke` 固定 Cheng runtime 对真实 C baseline 的 ratio 上界；case 固定为 `orc_retain_release`、`atomic_i32_add`、`thread_spawn_join`、`slice_bounds_loop`、`hash_lookup`、`syscall_write`。`perf_memory_contract_smoke` 读取 `CHENG_RUNTIME_C_BASELINE_REPORT` 的真实成对基线报告，报告行必须包含 `runtime_perf_ratio_x1000` 和 `limit_runtime_perf_ratio_x1000`；缺失 C baseline 采样必须 hard-fail，不允许填假数据。
- `artifacts/bootstrap/cheng.stage3 profile-run --in:/abs/path/file.cheng --target:arm64-apple-darwin --out:/tmp/app`
- `artifacts/bootstrap/cheng.stage3 profile-report --in:/tmp/app.cheng.profile.raw.txt --out:/tmp/app.profile.txt`
- `artifacts/backend_driver/cheng run-host-smokes perf_memory_contract_smoke`

dev hotpatch 边界：

- `100ms` 编译与二进制原地更新都只属于 dev host-only `self-link + direct-exe + host runner hotpatch` 的 dedicated witness 口径；release 主线仍然是 `system-link`。
- 当前 live 子命令面不再暴露 `backend_prod_closure`、`backend-prod-publish`、`verify_backend_abi_v2_noptr`、`verify_backend_selfhost_100ms_host`。
- `explicit_default_init_gate_smoke` 继续覆盖显式默认初始化门禁。

ABI 合同：

- 统一权威路径固定为 `src/core/tooling/backend_runtime_abi_contract.env`。
- runtime 兼容头为 `src/core/runtime/runtime_abi.h`。
