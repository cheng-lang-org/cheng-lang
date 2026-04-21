# 进度

- perf/memory 这轮把 1-5 收到同一个正式 gate 里。
  - baseline 样本已固定在 `perf_memory_contract_smoke` 报告：`object_native_link_plan` planner 下界 `462ms`、`chain_node` `800ms`、`content_stub` `1869ms`、`orc_perf` `584ms`；这些才是当前可引用的编译理论下界，外层 `elapsed_ms` 仍主要被执行/链接缝隙吃掉。
  - 热路径先收最短公共缺口：`bytesConcat/bytesConcat3`、SHA-256 输入 padding copy、P-256 deterministic 拼接和公钥/签名字节打包已从逐字节 `bytesGet/bytesSet` 改成 `RawmemCopy/RawmemSet`；`bytesCopyInto` 越界/nil 直接断言，不静默吞错。
  - 新增 `crypto_hot_kernel_perf_smoke`，同一份 gate 现在会测 SHA-256、X25519 pubkey、P-256 pubkey、P-256 sign，并在 P-256 sign 后做验签。
  - 当前报告值：`sha256_ns_per_op=29595`、`x25519_pubkey_ns_per_op=2201500`、`p256_pubkey_ns_per_op=275000`、`p256_sign_ns_per_op=22419000`。
  - ORC 合同仍闭合：`iterations=200000 retain_count=200000 release_count=200000 alloc_delta=1 free_delta=1 live_delta=0`；这不是 GC perf，Cheng 当前没有 tracing GC。
  - 已通过 `git diff --check`、`artifacts/v3_backend_driver/cheng run-host-smokes crypto_hot_kernel_perf_smoke`、`artifacts/v3_backend_driver/cheng run-host-smokes perf_memory_contract_smoke`、`artifacts/v3_backend_driver/cheng run-host-smokes fixed_surface_smoke fixed256_sha256_smoke fixed256_curve25519_smoke quic_tls_transport_ecdsa_smoke`。

- seed `type-call` 分类这轮又补了一次真正收口。
  - `v3/bootstrap/cheng_v3_seed.c` 现在会在规格化后先区分“真实类型”与“lower generic function”，不会再把 `elemSize[T]()` 误判成 `seq/fixed-array T()`，也不会把 `Pair()`、`int32[]()` 重新带坏。
  - `composite_default_init_smoke`、`composite_default_init_negative_smoke`、`artifacts/v3_bootstrap/cheng.stage3 build-backend-driver`、`CHENG_V3_NO_BACKEND_DRIVER_HANDOFF=1 artifacts/v3_bootstrap/cheng.stage0 run-host-smokes composite_default_init_smoke composite_default_init_negative_smoke typed_expr_call_validate_probe typed_expr_cross_file_probe` 已通过。

- ordinary parser 里同类 helper 也收成 seed 能稳编的形状。
  - `v3/src/lang/parser.cheng` 的 `v3ParserTypeExprLooksLikeLowerGenericFunction(...)` 已改成单次 trim + 线性扫描，不再用那种重复 `Trim/FindFirst/IdentPrefix` 的大表达式。
  - `typed_expr_call_validate_probe`、`typed_expr_cross_file_probe` 以及 fresh `artifacts/v3_backend_driver/cheng run-host-smokes stage3_system_link_exec_handoff_smoke stage3_command_surface_smoke` 已重新通过。

- lower-generic/type-call 规则矩阵这轮已经挂进正式 smoke。
  - `parser_path_smoke` 现在直接验 `elemSize[int32]()` 能解析，且当前 fixture 行不会被标成 `DefaultInitExpr` 或 `ConstructorExpr`。
  - `composite_default_init_smoke` 现在真实编译运行 `elemSize[int32]()`，和 `Pair()` 正例、`int32[]()` / `int32[2]()` 负例一起覆盖 parser 与 seed fallback 两边。
  - `v3_validate_type_field_defaults(...)` 的 C 声明、定义、调用已统一到 8 参数接口，stage0 C 编译重新恢复。

- seed fallback 里的旧 `v3_parse_default_expr_text_with_context(...)` 已经删掉。
  - `v3/bootstrap/cheng_v3_seed.c` 现只保留统一 `v3_classify_expr_type_call_surface(...) -> v3_classify_type_call_surface(...)` 这条入口。
  - `const eval`、`prepare_expr_call_state`、`infer_expr_type`、native scalar emit、native composite materialize、wasm composite default match、wasm composite emit、removed-default 诊断都已切到这条入口，不再保留 default-init 专用再解析薄壳。
  - `git diff --check`、`cc -std=c11 -O2 -Wall -Wextra -pedantic v3/bootstrap/cheng_v3_seed.c -o /tmp/cheng_v3_seed.check`、`CHENG_V3_NO_BACKEND_DRIVER_HANDOFF=1 /tmp/cheng_v3_seed.check run-host-smokes parser_normalized_expr_smoke composite_default_init_smoke composite_default_init_negative_smoke call_hir_matrix_smoke compiler_csg_smoke lowering_plan_smoke lowering_matrix_smoke typed_expr_call_validate_probe typed_expr_cross_file_probe` 已通过。

- `stage1_bootstrap.cheng` 这轮确认只是 bootstrap 合同清单，不是 helper 真源码。
  - 真正把 seed 收口固定住的动作，是重编 `artifacts/v3_bootstrap/cheng.stage0` 并跑 `bootstrap-bridge` / `build-backend-driver`，不是去改 manifest 同步 helper。
  - `v3/bootstrap/README.md` 已补上这条口径。

- ordinary 真源里的 `typeCallKind` 这轮已经补成显式 typed fact。
  - `v3/src/lang/typed_expr.cheng` 现已新增 `V3TypedExprTypeCallKind`，`DefaultInitExpr` / `ConstructorExpr` 会直接落到 `fact.typeCallKind` 和 `loweringRule.typeCallKind`。
  - `v3/src/tooling/compiler_csg.cheng` 已把这条字段编进 typed fact 序列化，`call_hir_matrix_smoke`、`compiler_csg_smoke`、`lowering_plan_smoke` 现会同时校验 `expr_surface_*`、`typed_expr_type_call_*`、`lowering_rule_type_call_*` 三套计数一致。
  - `artifacts/v3_bootstrap/cheng.stage3 build-backend-driver` 与 `CHENG_V3_NO_BACKEND_DRIVER_HANDOFF=1 artifacts/v3_backend_driver/cheng run-host-smokes parser_normalized_expr_smoke composite_default_init_smoke composite_default_init_negative_smoke call_hir_matrix_smoke compiler_csg_smoke lowering_plan_smoke lowering_matrix_smoke typed_expr_call_validate_probe typed_expr_cross_file_probe` 已通过。

- seed 的 type-call 分类这轮已经收成一套。
  - `v3/bootstrap/cheng_v3_seed.c` 新增 `V3TypeCallSurface` / `V3TypeCallKind`，把 `default-init`、`constructor-like`、标量 `T()`、`ref object T()` 统一到一个分类点。
  - 旧的 `v3_callee_is_constructor_like(...)` 已删；duplicate-field 检查、native prepare、infer type、native composite materialize、wasm analyze、wasm composite emit 现在都吃同一份分类结果。
  - `v3_parse_default_expr_text_with_context(...)` 和 removed-default 诊断也已改成复用这份分类，不再各自再猜一遍。
  - `git diff --check`、`cc -std=c11 -O2 -Wall -Wextra -pedantic v3/bootstrap/cheng_v3_seed.c -o /tmp/cheng_v3_seed.check`、`CHENG_V3_NO_BACKEND_DRIVER_HANDOFF=1 /tmp/cheng_v3_seed.check run-host-smokes parser_normalized_expr_smoke composite_default_init_smoke composite_default_init_negative_smoke call_hir_matrix_smoke compiler_csg_smoke lowering_plan_smoke lowering_matrix_smoke` 已通过。

- seed 里的 removed-default / constructor-like 旧文本兜底已经收成单一相位。
  - `v3/bootstrap/cheng_v3_seed.c` 不再在 `collect_lowering_functions_from_source` 里一边扫函数一边报 `default[T]` / 标量 `T()` / `ref object T()`。
  - 现在只在 `build_lowering_plan_stub` 进入函数收集前做一次 source 级校验；报错仍保持原诊断文本，但不再拖到 lowering collect 阶段。
  - `artifacts/v3_bootstrap/cheng.stage3 system-link-exec` 在 backend driver 就绪时现在会先 handoff 到 `artifacts/v3_backend_driver/cheng`，所以用户可见主路径已经先走 ordinary parser 真源；seed 这条 source 校验只剩 `CHENG_V3_NO_BACKEND_DRIVER_HANDOFF=1` 或 backend driver 未就绪时的内部 fallback。
  - 新增 `stage3_system_link_exec_handoff_smoke`，直接验 `cheng.stage3 system-link-exec` 会产出 `artifacts/v3_forward/backend_driver.forward.log`，防止命令分发把这条 handoff 再回退掉。
  - `parser_path_smoke` 已补上 `default[T]`、`int32()`、`ref object T()`、unused `ref object T()` 的前端拒绝。
  - `docs/cheng-skill/SKILL.md` 和 `$HOME/.codex/skills/cheng语言/SKILL.md` 已同步这条口径，`cheng_skill_consistency_smoke` 通过。

- `ConstructorExpr` 这刀已经从 parser 打到 typed/csg/lowering/report。
  - 复合 `T(args)` 现在显式记为 `ConstructorExpr`，复合 `T()` 继续走 `DefaultInitExpr`，不再混进普通 `CallExpr`。
  - `compiler_csg` / `lowering_plan` / `system_link_plan` 已新增 `expr_surface_constructor_count`，typed fact 也能直接带出 constructor 的 type。
  - fixed-array 判定已收紧成“顶层尾部数字下标”；`Ok[T]` / `Err[T]` 继续只算 result intrinsic，不再误记成 constructor。
  - `parser_normalized_expr_smoke`、`call_hir_matrix_smoke`、`compiler_csg_smoke`、`lowering_plan_smoke`、`composite_default_init_smoke`、`composite_default_init_negative_smoke`、`typed_expr_call_validate_probe` 已通过。

- typed/call/lowering 报告真源已经固定在 `typed_expr`。
  - `compiler_csg` / `lowering_plan` 只追加 `texpr.v3TypedExprReportText(...)`，不再回写第二套 `expr_surface_call_*`、`typed_expr_*`、`lowering_rule_*` 字段。
  - `lowering_plan` 里把 typed report 直接拼到上一行后面的换行缺口已经修掉，`expr_surface_question_residual_count` 不会再和 `expr_surface_call_count` 粘成一行。

- primary-object 的 ready 状态已经贯穿 runtime / support-matrix / bootstrap contract。
  - `ordinary_pipeline_state`、`ordinary_primary_object_codegen_ready`、`canonical_csg_verified_primary_object_codegen_ready` 口径已统一。
  - `compiler_runtime_smoke`、`backend_driver_command_surface_smoke` 的支持矩阵断言已跟上新状态。

- bootstrap-bridge 不再优先复用陈旧 live compiler。
  - `v3/bootstrap/cheng_v3_seed.c` 和 `v3/src/tooling/compiler_main.cheng` 现在都先按 bootstrap 输入新鲜度选 live compiler。
  - 源码更新后旧 `cheng.stage3` 不会再拿自己去 `stage0_self_check` 新 contract；找不到新鲜编译器时直接报“先重编 stage0”。

- `build-backend-driver` 已经从这轮真实收绿。
  - `artifacts/v3_bootstrap/cheng.stage0 bootstrap-bridge` 通过。
  - `artifacts/v3_bootstrap/cheng.stage3 build-backend-driver` 通过。
  - `artifacts/v3_backend_driver/cheng` 当前 `status` 已稳定输出 `ordinary_pipeline=canonical_csg_verified_primary_object_codegen_ready`。

- ordinary 函数体主线这轮先把正式门禁补齐了。
  - `if_enum_composite_return_smoke`、`out_param_writeback_smoke`、`nested_out_param_writeback_smoke`、`out_param_direct_call_writeback_smoke`、`var_out_param_writeback_probe`、`consensus_event_varparam_roundtrip_smoke` 已确认能在当前 backend driver 直接通过。
  - `wrapper_result_forward_varparam_smoke`、`wrapper_rebuild_result_forward_varparam_smoke` 本来只在 backend driver 默认列表里；现在 gate 默认 host smokes 也补齐了。
  - `backend_driver_main` / `gate_main` 的默认 host smokes 现在会正式覆盖 `stmt_let`、`stmt_if`、复合 `var/out` 写回这批 ordinary body witness，不再依赖人工点名补跑。

- `verify-export-visibility-parallel` 的共享输出目录踩踏已经修掉。
  - backend driver 现在给 stage3/backend 两侧 run-host-smokes 生成带 monotime 的独立 label。
  - 反复重跑或有残留并发时，不会再因为 `verify_export_*_parallel` 复用同一批产物把 provider object 踩成空文件。

- 本轮验收已经通过。
  - `git diff --check`
  - `cc -std=c11 -O2 -Wall -Wextra -pedantic v3/bootstrap/cheng_v3_seed.c -o artifacts/v3_bootstrap/cheng.stage0`
  - `artifacts/v3_bootstrap/cheng.stage0 bootstrap-bridge`
  - `artifacts/v3_bootstrap/cheng.stage3 build-backend-driver`
  - `artifacts/v3_backend_driver/cheng run-host-smokes stage3_system_link_exec_handoff_smoke stage3_command_surface_smoke composite_default_init_negative_smoke parser_path_smoke cheng_skill_consistency_smoke`
  - `CHENG_V3_NO_BACKEND_DRIVER_HANDOFF=1 artifacts/v3_backend_driver/cheng run-host-smokes compiler_csg_smoke lowering_plan_smoke call_hir_matrix_smoke lowering_matrix_smoke typed_expr_call_validate_probe typed_expr_cross_file_probe`
  - `CHENG_V3_NO_BACKEND_DRIVER_HANDOFF=1 artifacts/v3_backend_driver/cheng run-host-smokes export_visibility_smoke export_visibility_negative_smoke strformat_export_negative_smoke`
  - `artifacts/v3_backend_driver/cheng run-host-smokes if_enum_composite_return_smoke out_param_writeback_smoke nested_out_param_writeback_smoke out_param_direct_call_writeback_smoke var_out_param_writeback_probe consensus_event_varparam_roundtrip_smoke wrapper_result_forward_varparam_smoke wrapper_rebuild_result_forward_varparam_smoke`

- `r2c-react-v3` 这轮又收掉一圈边界。
  - `v3/src/tooling/r2c_react_v3_status_support.cheng` 现在新增 `thin_node_helper_count` / `thin_node_helpers`，`active_node_helper_count` 从 6 收到 4，`exec_route_matrix_helper` 和 `truth_route_helper` 不再混进真正 blocker。
  - 新增 `v3/src/tests/r2c_react_v3_discover_truth_routes_controller_smoke.cheng` 和 `v3/src/tests/r2c_react_v3_exec_route_matrix_controller_smoke.cheng`，并接进 `gate_main`、`backend_driver_main`、`compiler_main` 的 `verify-r2c-react-v3-surface` 列表。
  - `v3/experimental/r2c-react-v3/r2c-react-v3-exec-route-matrix.mjs` 已修正：已有 `--route-catalog` 时不再错误强制 `--tsx-ast`。
  - 新 backend driver candidate 已重编，`CHENG_V3_NO_BACKEND_DRIVER_HANDOFF=1 artifacts/v3_backend_driver/cheng_candidate verify-r2c-react-v3-surface` 通过。
  - candidate 已覆盖回 `artifacts/v3_backend_driver/cheng`，两者当前字节级一致。

- `run-production-regression` 这轮把聚合回归口径重新对齐了。
  - `v3/src/tooling/gate_main.cheng` 里 `verify-r2c-react-v3-surface` 已提成单一 helper，命令分发和聚合回归不再各自维护一套 `r2c` smoke 列表。
  - 聚合回归已补回 `composite_zero_helper_gate_smoke`，并把 `verify-r2c-react-v3-surface` 正式串进主线。
  - `v3/bootstrap/cheng_v3_seed.c` 里用户可见 `verify-r2c-react-v3-surface` 之前只跑 `r2c_react_v3_surface_smoke`；现在已经改成直通 backend driver 的正式 surface 验收，`run-production-regression` 也会真实跑到这条 passthrough。
  - `artifacts/v3_backend_driver/cheng verify-r2c-react-v3-surface` 已完整通过，`artifacts/v3_bootstrap/cheng.stage3 run-production-regression` 也已重新通过。

- `T()` / `default[T]` 这轮已经收成当前稳定口径。
  - parser 已新增 `DefaultInitExpr`，合法复合默认物化不再混进普通 `CallExpr`。
  - `default[T]`、标量 `T()`、`ref object T()` 都会在 parser 直接报硬错误；同时 seed 在 lowering 收集入口也会前置拒绝，保证真实编译链路首个诊断就是这类语法错误。
  - 晚期 lowering/codegen 的重复 removed-default 兜底已经去掉，不再等到 `prepare binding infer type failed` / `primary object body semantics missing` 才间接炸出。
  - `parser_path_smoke`、`parser_normalized_expr_smoke`、`composite_default_init_smoke`、`composite_default_init_negative_smoke` 已通过。
