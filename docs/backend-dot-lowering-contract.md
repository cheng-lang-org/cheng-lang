# 后端 dot-lowering contract

- `dot_lowering_contract.version=1`
- `dot_lowering_contract.scheme.id=DOTLOW`
- `dot_lowering_contract.scheme.name=backend_dot_lowering_contract`
- `dot_lowering_contract.scheme.normative=1`
- `dot_lowering_contract.enforce.mode=report_only`
- `dot_lowering_contract.subdebt.semantic_dot=1`
- `dot_lowering_contract.subdebt.selector_cstring=1`
- `dot_lowering_contract.closure=report_only`

## 子债拆分
- 本项清的是边界/分层 debt；子债 A/B 的残余实现差距统一留在升级前提，不再视为当前 contract 漂移。
- 子债 A `semantic dot`：
  - 覆盖 `obj.field`、`call-dot`、`bracket-dot`、`global object probe` 等语义 lowering。
  - 规范锚点在 `src/backend/uir/uir_internal/uir_core_builder.cheng` 的 `debugDot`、`globalObjProbe`、`call-dot`、`bracket-dot` 与 field-access marker。
- 子债 B `selector cstring/object writer`：
  - 覆盖 machine selector 侧的 `cstring lowering` 残余路径，以及 object writer 为禁用 eager lowering 时提供的兼容 materialization/backfill。
  - 规范锚点在 `src/backend/machine/select_internal/{aarch64_select,x86_64_select}.cheng`、`src/backend/uir/uir_codegen.cheng`、`src/backend/obj/coff_writer_x86_64.cheng`。

## 口径
- dot 语义 lowering 的正式入口仍在 UIR builder，而不是 selector。
- selector 侧的 `cstring lowering` 兼容路径只记为实现债，不构成用户语言层 dot 语义契约。
- 当前 contract 只要求文档、tooling 接线、关键 source marker 与总表一致，不要求本轮补齐所有 lowering 实现。

## 闭环分层
- 默认 gate：`cheng_tooling verify_backend_dot_lowering_contract`
- `backend_prod_closure`：暂不纳入 required；仅进入 default `cheng_tooling verify`
- 升级前提：
  - 子债 A 与子债 B 都具备独立稳定实现；
  - UIR doc、README、总表、contract 对两块边界描述完全一致；
  - 不再依赖 selector/backfill 的 debt 路径维持用户可见行为。
