# 后端字符串 ABI contract

- `string_abi_contract.version=1`
- `string_abi_contract.scheme.id=SABI`
- `string_abi_contract.scheme.name=backend_string_abi_contract`
- `string_abi_contract.scheme.normative=1`
- `string_abi_contract.enforce.mode=report_only`
- `string_abi_contract.language.str=value_semantics`
- `string_abi_contract.abi.cstring=ffi_boundary`
- `string_abi_contract.nil_compare.cstring_only=1`
- `string_abi_contract.selector_cstring_lowering.user_abi=0`
- `string_abi_contract.closure=report_only`

## 口径
- 本项清的是 contract/closure 分层债；selector eager lowering 的残余问题继续只作为实现债存在于升级前提，不再视为当前文档漂移。
- `str` 是语言值语义字符串。
- `cstring` 是 ABI/FFI 边界类型，不是语言层默认字符串。
- `nil` 比较仅保留给 `cstring` 的 ABI 指针语义；`str == nil` / `str != nil` 继续视为编译错误。
- selector 侧 `cstring lowering` 不是用户 ABI 契约。当前 eager selector lowering 仍视为实现债，用户 ABI 口径以 `UIR metadata -> post-regalloc backfill` 可观测结果为准。

## 实现锚点
- `src/backend/machine/select_internal/aarch64_select.cheng`：记录 eager selector-side cstring lowering 仍不稳定，当前跳过该路径。
- `src/backend/machine/select_internal/x86_64_select.cheng`：与 AArch64 保持一致，selector 默认不走 eager cstring lowering。
- `src/backend/uir/uir_codegen.cheng`：保留 canonical backfill，用于补齐 module cstring payload。
- `src/backend/obj/coff_writer_x86_64.cheng`：在 selector cstring lowering 关闭时仍强制 materialize `.rdata`。

## 闭环分层
- 默认 gate：`cheng_tooling verify_backend_string_abi_contract`
- `backend_prod_closure`：暂不纳入 required；仅进入 default `cheng_tooling verify`
- 升级前提：
  - selector eager lowering 的实现债清零；
  - `str`/`cstring` 的 formal spec、README、总表、contract 四处口径保持一致；
  - 不再依赖后置 backfill 才能维持 ABI 正确性。
