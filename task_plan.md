# 当前任务

- 当前目标：把 provider object 秒级物化成本收成严格缓存，并把 cache 命中/未命中挂进 perf/memory gate。

- 已完成。
  - seed `system-link-exec` 已在 provider object 物化入口加缓存；命中直接复制缓存 object，未命中才编译并写入缓存。
  - cache key 包含 cache version、source CID、workspace/package/root/target/emit/symbol visibility、真实 C compiler 路径、codegen CID 和 suppressed exports。
  - 编译报告和 `*_compile_gap_breakdown` 已输出 provider cache lookup/copy/store、Cheng/C provider compile、hit/miss 计数。
  - `perf_memory_gate_contract_smoke` 已验证字段解析，`perf_memory_contract_smoke` 已验证真实样本里 provider cache 统计存在且有观测。
  - 最新 gate：`provider_objects_ms=13/14/14/14/14ms`，`provider_cache_hits=5 provider_cache_misses=0`。

- 补充目标：修掉 no-handoff 核心 smoke 暴露的 parser/typed 边界漂移。

- 已完成。
  - `cstring(x)` 这类标量/指针 cast 已从 parser constructor 分类入口排除，不再误进 `ConstructorExpr`。
  - type-call 复合物化校验保持硬失败；真正修的是 parser 归类边界，不是放宽 typed validation。
  - `typed_expr` 的 seed 不友好 helper 形状已压平，`lowering_plan_smoke` 的空字段读取也已修掉。
  - 新 backend driver 已重建，parser/no-handoff/perf/skill 相关 gate 已重新过绿。

- 当前目标：把 baseline、编译理论下界、内存/ORC、crypto 热核和热路径字节搬运收进同一轮正式验收。

- 已完成。
  - `perf_memory_contract_smoke` 现在同时覆盖 `object_native_link_plan_smoke`、`chain_node_smoke`、`content_stub_smoke`、`orc_perf_contract_smoke` 和 `crypto_hot_kernel_perf_smoke`。
  - 编译理论下界只看 `*_compile_exec_phase_summary.planner_total_ms`；当前稳定样本分别是 `421ms / 692ms / 1699ms / 352ms / 1522ms`。
  - `*_compile_gap_breakdown` 已把 planner 外耗时拆成 provider object/cache、primary object emit、native link 和 line-map；当前未归因缝隙只剩几十毫秒。
  - `Bytes` 拼接、SHA-256 padding copy、P-256 deterministic concat/fill、公钥/签名字节打包已改成 rawmem bulk copy/set，去掉逐字节 helper 边界。
  - ORC 合同继续按 retain/release 与 alloc/free/live 验收，当前 `live_delta=0`；没有 tracing GC 口径。
  - 新增 crypto 热核合同：SHA-256、X25519 pubkey、P-256 pubkey、P-256 sign，P-256 sign 产物会真实验签。
  - `dev_hotpatch_100ms_scope_contract_smoke` 已接入默认 host smoke 和 production regression smoke contract，防止 100ms/热补丁口径漂到 release `system-link`。

- 当前目标：把 `lower generic function` 和 `type-call` 的最后一处串味收干净，并让 ordinary parser 的同类 helper 也能走通 seed no-handoff。

- 已完成。
  - seed `v3_classify_type_call_surface(...)` 现在在规格化后按“已知类型 / lower generic function”分流，`elemSize[T]()` 不会再误报 `seq/fixed-array T()`。
  - `v3/src/lang/parser.cheng` 的 `v3ParserTypeExprLooksLikeLowerGenericFunction(...)` 已改成 seed 友好的线性扫描实现，`typed_expr_call_validate_probe` / `typed_expr_cross_file_probe` 已恢复全绿。
  - fresh `build-backend-driver` 和 `stage3_system_link_exec_handoff_smoke` / `stage3_command_surface_smoke` 已再次通过。
  - `parser_path_smoke` / `composite_default_init_smoke` 已补上 `elemSize[int32]()` 正例，和 `int32[]()` / `int32[2]()` 负例一起固定规则矩阵。
  - `v3_validate_type_field_defaults(...)` 的 C 接口漂移已修到声明、定义、调用一致，stage0 重新能 0 warning 编译。

- 当前目标：把 seed fallback 里旧的 default-init 专用再解析薄壳删掉，统一到一条 `expr -> type-call surface` 入口。

- 已完成。
  - `v3/bootstrap/cheng_v3_seed.c` 已新增统一 `v3_classify_expr_type_call_surface(...)`，删除 `v3_parse_default_expr_text_with_context(...)`。
  - const-eval、prepare/infer、native/wasm materialize、removed-default 诊断都只吃这一个 expr 入口，再由它落到既有 `v3_classify_type_call_surface(...)`。
  - 直接用新编 `/tmp/cheng_v3_seed.check` 跑 no-handoff 核心 smokes 已全绿。

- 当前目标：确认 bootstrap 真源边界，避免误把 `stage1_bootstrap.cheng` 当 helper 源码。

- 已完成。
  - `stage1_bootstrap.cheng` 已确认只是 bootstrap 合同清单；helper 真逻辑不在这里。
  - 当前正确收口动作是：重编 `cheng.stage0`，再跑 `bootstrap-bridge` 和 `build-backend-driver` 固定 bootstrap 链。
  - `v3/bootstrap/README.md`、`findings.md`、`lessons.md` 已同步这条口径。

- 当前目标：把 seed 里已经收好的 type-call 分类上推回 ordinary 真源，补成 typed fact / lowering rule / report 的显式字段。

- 已完成。
  - `v3/src/lang/typed_expr.cheng` 已新增 `V3TypedExprTypeCallKind`，`typeCallKind` 进入 fact、rule、唯一 key、report 和 validate。
  - `v3/src/tooling/compiler_csg.cheng` 已把 `typeCallKind` 写进 typed fact 序列化，CSG 不再只能靠 `kind=default_init|constructor` 间接推断。
  - `v3/src/tests/call_hir_matrix_smoke.cheng`、`v3/src/tests/compiler_csg_smoke.cheng`、`v3/src/tests/lowering_plan_smoke.cheng` 已把 `constructor/default-init -> typeCallKind -> lowering_rule_type_call_*` 整条链钉成门禁。
  - `build-backend-driver` 和相关 no-handoff smoke 已通过。

- 当前目标：把 seed 里残留的 `default-init` / `constructor-like` 双轨文本判断并成一个分类真源。

- 已完成。
  - `v3/bootstrap/cheng_v3_seed.c` 已新增统一 `V3TypeCallSurface` 分类，正向构造、默认物化、标量 `T()`、`ref object T()` 都从这里出。
  - duplicate constructor field、prepare/infer/materialize、wasm analyze/emit、removed-default 诊断、`v3_parse_default_expr_text_with_context(...)` 都已经切到这份分类。
  - no-handoff seed host smokes 已用临时 `/tmp/cheng_v3_seed.check` 直接通过。

- 当前目标：把 seed 里剩余的 removed-default / constructor-like 文本前置判断继续缩到最小，并把用户可见 `stage3 system-link-exec` 切到 parser 真源主路径。

- 已完成。
  - `v3/bootstrap/cheng_v3_seed.c` 已新增单一 source 级校验相位，在 `build_lowering_plan_stub` 进入函数收集前一次性扫描并报错。
  - `collect_lowering_functions_from_source` 里的旧 removed-default 兜底块已删除。
  - `stage3 system-link-exec` 在 backend driver fresh/ready 时已优先 handoff 到 `backend_driver/cheng`；用户可见入口现在先走 parser/source 真源，再按需要回桥到 stage3 落地机器码。
  - `stage3_system_link_exec_handoff_smoke` 已接进 host gate，直接验这条 handoff。
  - `parser_path_smoke` 已补齐 `default[T]`、标量 `T()`、`ref object T()` 的前端拒绝负例。
  - `docs/cheng-skill/SKILL.md` 与 `$HOME/.codex/skills/cheng语言/SKILL.md` 已同步，`cheng_skill_consistency_smoke` 通过。

- 当前目标：把 `ConstructorExpr/DefaultInitExpr` 真源收口，固定 parser -> typed -> csg/lowering/report 口径，不再把复合构造混进普通 `CallExpr`。

- 已完成。
  - 复合 `T(args)` 已显式落成 `ConstructorExpr`，复合 `T()` 继续是 `DefaultInitExpr`。
  - `expr_surface_constructor_count` 已接进 `compiler_csg` / `lowering_plan` / `system_link_plan`，typed fact 也能直接验证 constructor type。
  - fixed-array 判定已改成顶层尾部数字下标，`Ok[T]` / `Err[T]` 不再混进 constructor 统计。
  - `parser_normalized_expr_smoke call_hir_matrix_smoke compiler_csg_smoke lowering_plan_smoke composite_default_init_smoke composite_default_init_negative_smoke typed_expr_call_validate_probe` 已通过。

- 当前目标：把 bootstrap/build-backend-driver 这条 ready 主线彻底收口，不再被旧 compiler、旧 contract、共享测试产物伪造回归。

- 已完成。
  - typed report 单一出口落在 `v3/src/lang/typed_expr.cheng`，`compiler_csg` / `lowering_plan` 不再各拼一套。
  - `lowering_plan` typed report 拼接换行缺口已修，`lowering_plan_smoke` 恢复真实字段验收。
  - support-matrix / runtime / bootstrap contract 已统一为 `canonical_csg_verified_primary_object_codegen_ready`。
  - bootstrap-bridge live compiler 改成按 bootstrap 输入新鲜度选取，旧 `stage3` 不会再验新 contract。
  - `verify-export-visibility-parallel` 改成独立 label，不再共享输出目录。
  - `bootstrap-bridge`、`build-backend-driver`、核心 no-handoff smoke、export visibility smoke 已通过。

- 固定验收。
  - `git diff --check`
  - `cc -std=c11 -O2 -Wall -Wextra -pedantic v3/bootstrap/cheng_v3_seed.c -o artifacts/v3_bootstrap/cheng.stage0`
  - `artifacts/v3_bootstrap/cheng.stage0 bootstrap-bridge`
  - `artifacts/v3_bootstrap/cheng.stage3 build-backend-driver`
  - `stage3_system_link_exec_handoff_smoke stage3_command_surface_smoke composite_default_init_negative_smoke parser_path_smoke cheng_skill_consistency_smoke`
  - `compiler_csg_smoke lowering_plan_smoke call_hir_matrix_smoke lowering_matrix_smoke typed_expr_call_validate_probe typed_expr_cross_file_probe`
  - `export_visibility_smoke export_visibility_negative_smoke strformat_export_negative_smoke`

- 当前这条主线已完成。
  - `Bytes[] add/setLen` 仍然不插队；如果后面还要继续，只剩内部 bootstrap fallback 直接消费 parser 真源，不是当前阻塞。

- 当前目标：把 ordinary 函数体真实 witness 补进默认 host gate，固定 `stmt_let / stmt_if / 复合 out-param` 的正式验收。

- 已完成。
  - `v3/src/tooling/backend_driver_main.cheng` 和 `v3/src/tooling/gate_main.cheng` 已把 `if_enum_composite_return_smoke`、`out_param_writeback_smoke`、`nested_out_param_writeback_smoke`、`out_param_direct_call_writeback_smoke`、`var_out_param_writeback_probe`、`consensus_event_varparam_roundtrip_smoke`，以及 gate 侧缺的两个 wrapper varparam smoke 挂进默认 host gate。
  - 这轮没有再改 ordinary 语义实现；真实结论是 generic scalar 路径已经能跑通这些形状，缺口只是默认门禁没把它们固定下来。

- `r2c-react-v3` 当前补充收口。
  - 状态报告把真正阻塞纯 Cheng 的 active Node helper 和仅剩薄壳的 route/truth helper 分开统计，避免 blocker 口径漂移。
  - `discover-truth-routes` / `exec-route-matrix` 新增 controller smoke，并接入 `verify-r2c-react-v3-surface`。
  - `exec-route-matrix` 现有 helper 契约要和 controller 保持一致；给了 `--route-catalog` 就不能再暗中要求 `--tsx-ast`。
  - `native-gui-bundle` 现在由 Cheng controller 最终写回 `native_gui_bundle_v1.json`、`native_gui_bundle.summary.env`、`native_gui_bundle_report_v1.json`，并用 `cheng_controller_native_gui_bundle_finalizer_v1` 固定可验证边界；重 layout/runtime payload 仍在 Node helper 内，不能把 blocker 口径提前清零。
  - `native-gui-bundle` 的 layout/style/native-layout 发布边界已继续加上 Cheng controller sidecar：`style_layout_surface_controller_v1.json`、`native_layout_plan_controller_v1.json`，并用 `cheng_controller_layout_payload_finalizer_v1` 写入 bundle/summary/report/status；原 Node payload 文件暂不覆盖，避免破坏现有 GUI 运行数据。
  - `native_layout_plan_controller_v1.json` 已改成 `cheng_controller_items_source_checked_v1`：Cheng controller 会硬校验 `native_layout_plan_v1.json` 存在且包含 `items[]` / `viewport_items[]`，发布 item/viewport 计数、source path 和 layout policy；不再把大数组在 Cheng 字符串里内联拼接。
  - `native_gui_runtime` 与 truth compare 共享生成模板已去掉显式默认初始化，避免当前 Cheng 编译器用 `redundant explicit default init` 硬失败。

- 当前目标：把 `r2c-react-v3` surface 和 composite-zero guard 正式挂进 `run-production-regression`，消掉 gate 代码和 README 的回归口径漂移。

- 已完成。
  - `v3/src/tooling/gate_main.cheng` 已新增 `v3GateVerifyR2cReactV3Surface(...)`，`verify-r2c-react-v3-surface` 命令分发和 `run-production-regression` 现在共用同一份 smoke 列表。
  - `run-production-regression` 已补回 `composite_zero_helper_gate_smoke`，并新增 `r2c-react-v3 surface` 验收相位。
  - `v3/bootstrap/cheng_v3_seed.c` 的用户可见 `verify-r2c-react-v3-surface` 已从旧的单 smoke 壳改成 backend driver passthrough；`run-production-regression` 也已串上这条真 surface。
  - `README.md` 与 `v3/tooling/README.md` 已同步到这条聚合回归新口径。
