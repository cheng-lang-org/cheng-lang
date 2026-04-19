# Lessons

## 总原则

- 从单一真源往下推，不要在 parser、CSG、lowering、seed、wasm 各层各猜一份。
- 发现未知就显式标未知，不要假装类型或 ABI 已经确定。
- 复合值统一按地址语义思考；不要在热路径上混用“按值/按句柄/按临时槽”。
- 验收优先用矩阵和正式 smoke，不靠一次性日志或临时手工命令。

## 前端与 lowering

- 语法糖必须在前端一次吃掉，后端不再识别 `Fmt/?:/range/result intrinsic` 这类表面。
- 表达式一旦跨层流动，第一刀先补 `exprId + source span`。
- `compiler_csg` 和 `lowering_plan` 必须共用同一份 typed fact 与 lowering rule，不要双份统计。
- lowering 真规则至少要显式带 `arg/result/materialize/copy/import` 五类字段。
- 局部槽查找不能再用 flat table 的 first-match；必须至少带 `source line` 可见区间，并在 `dedent / elif / else` 时收紧块内局部。
- native 和 wasm 共用 `V3AsmLocalSlot` 时，新增字段必须两边一起初始化、一起消费；只修一边等于没修。

## ABI 与复合值

- `str/rawbytes/seq/result` 当前统一按“显式地址优先 + region copy”收。
- `Bytes[] add/setLen` 这种序列能力缺口要直接修进编译器，不要再在 host/browser 业务代码里兜桥或换数据结构绕过去。
- `xor` 这种关键字二元运算要成套收进 `const/infer/prepare/native/wasm`，不能只在某一层临时认一下。
- `len(str/Bytes/seq)` 不要只为局部变量写特判；统一走 lvalue/global 地址再按 ABI 布局取长度槽更稳。
- browser wasm ABI 不要长期手写 `input_len/copy/raw_handle/text_handle`；先抽 schema/helper，再走自动生成。
- browser bridge 的最终验收必须看 fresh compiler no-handoff 的真实编译产物，同时确认 `.browser_bridge_plan.txt`、`.browser_bridge.generated.cheng` 和最终可执行都在。
- compiler-generated internal Cheng object 不能沿用默认 public 符号面；bridge source 单独编 object 时，closure 引进来的 `std_*` helper 也会一起外泄，显式 `symbol_visibility=internal` 才是正解。
- `--link-input` smoke 不能把 compiler-generated bridge source 再手工编一次陪跑；这只会稳定制造 duplicate symbol，正确做法是单独准备不重名的 manual fixture object。
- hand-built browser ABI / system-link plan smoke 必须显式补 `entryPath`，不然 fresh no-handoff 下的规则收集会和真实命令面分叉。
- seed 后端里剩下的 `composite` 文本判定，优先从 `call arg pass/materialize` 这种横跨 native+wasm 的热路径收；先抽共享 helper，再逐步往 `call result/field/local temp` 扩。
- `call arg pass kind` 和 `expr materialize kind` 这种规则，必须直接映射 typed lowering 的语义面；不要再让 native/wasm 各自用自己的布尔猜测。
- `call arg` 收完后，不要停在一半；紧跟着把 `seq/local/field/materialize` 这批同构分支一起接到同一个 helper，不然 wasm builtin、binding、assignment、frame init 很快又会长回第二套 `composite` 判断。
- `seq/local/field/materialize` 收完后，下一批最值的是 `result intrinsic / str equality / constructor field`；它们表面不同，但本质都在复用“复合值是否必须地址物化”这一条规则，不一起扫完，native compare、wasm result、constructor write 会继续平行残留第二套 `composite` 判定。
- `result intrinsic / equality / constructor field` 收完后，不要停；`result field/global/member/list elem copy` 还是同一条规则面，只是换成投影、复制和 literal 落位。如果这里不顺手一起收，materialize 主链和 wasm list/fixed-array 很快又会保留一排平行的 ABI 文本判断。
- `result field/global/member/list elem copy` 收完后，下一批最值的是 `param ABI / builtin str bridge`；它们看起来像 ABI 特判或消息桥，实质还是“这个参数是不是必须按地址传/物化”。不收，native external call 和 native/wasm `panic/echo` 仍会挂着一截平行文本规则。
- 当功能热路径里的裸 `composite` 文本判定已经清空、只剩 descriptor/helper 自身一两处定义分支时，就别再沉迷清尾巴；下一步收益更大的方向是把同一套 lowering rule 往更高层 dispatch 真接进去。
- 往更高层 dispatch 推时，优先收 `call target` 级 helper：参数 load ABI、参数 temp prepare、scalar call、call-into-address、wasm composite call return 这些口必须共用同一套 `target param/return prefers address`，不要让它们继续各自用 `scalar_or_ptr` 或 `return_abi` 猜。
- `call target` 级 dispatch 收完后，不要停；`call arg temp / expr temp / Result.Ok(...) / wasm call arg temp` 这层临时槽和结果物化入口也必须紧跟着切到 `v3_call_arg_passes_by_address(...)` 与 `v3_expr_materializes_to_address(...)`，不然高层很快又会重新长出一层裸 ABI 判断。
- 临时槽和结果物化入口收完后，下一批最值的是 `field/global/default/setitem` 这种已经拿到了完整 `type + abi` 的高层分发口；这里如果还只看 ABI class，字段默认值、构造器字段写入、local/global copy、`[]=` 写值很快又会平行长出第二套判断。
- `field/global/default/setitem` 收完后，不要停；`Result.Value(...)`、local binding/local assignment、module global init 这批入口也同样拿到了完整 `type + abi`，必须继续一起切到 `v3_expr_materializes_to_address(...)`，不然 native 语句发码层会残留第二套 `abi_class` 判断。
- `Result.Value(...)` 和 native 语句发码入口收完后，别漏掉 `v3_add_local_slot_for_type(...)`、scalar global load、`seq add` 元素写入、expr statement prepare/codegen、param-slot `indirect_value` 这批低层同构入口；只要它们还挂着 `scalar_or_ptr`，lowering 漂移就还没真正收干净。
- 当低层 `scalar_or_ptr` 也退回纯标量语义检查后，就不要再沉迷清这条尾巴；下一步更值的是把 return dispatch 也补成显式 `return pass kind` helper，让 function/call-target 的返回路径和 arg/materialize 共用同一套规则名。
- return dispatch 收完后，不要停；`Result projection / wasm region-copy` 这批热路径也要补成显式 `copy kind` helper，不然 `prefers_region_copy` 仍然只是散装布尔判断，`copy/materialize/import` 这条线就还没真正收口。
- `copy kind` 收完后，也不要停；`call-into-address / composite call-result / wasm static return slot` 这批入口还要继续补成显式 `call result kind` 和统一 helper，不然返回地址、静态返回槽和 composite call fallback 很快又会重新长出平行的布尔分支。
- `call result kind` 和 static return slot 收完后，还要继续把 wasm pointer-result fallback 收成单一入口；只要 result intrinsic、call-result 和 generic fallback 还各自手写 `emit expr -> local_set -> copy`，这条链就还会漂。
- pointer-result fallback 收完后，不要立刻跳去别的面；wasm `builtin message ptr` 这条 import/emit 热链也要紧跟着补成显式 mode 和共享 bridge helper。`panic/echo/assert` 如果继续在 analyze/emit 两边各写一份 `literal / ptr / str-address bridge` 三叉分支，import 规则很快又会重新漂成第二份真源。
- wasm `builtin message ptr` 收完后，native importc `ffi_handle` 参数链不要只补 scalar call；`arg load abi`、prepare、scalar call、`call-into-address` 必须一起切到同一个显式 `arg kind`，否则复合返回那条主链会继续把 handle 参数当普通标量过。
- importc `ffi_handle` 这类规则化入口，最稳的形状不是再堆布尔 helper，而是直接补显式枚举：`var / ffi_handle resolve / ffi_handle consume / value / address`。这样 `call_arg_load_abi`、spill、prepare、`call-into-address` 才不会各自重新长一套分支。
- 参数链收完后，不要让 importc 返回继续停在裸布尔特判；`ffi_handle register` 也必须补成显式 `return kind`，让 `return pass / call result / return fixup` 共吃同一条规则。
- importc `ffi_handle` arg/return 收完后，不要马上跳去别的面；wasm call/import 这层 `i32 param slot / i32 return slot / void` 也必须补成显式 `slot kind`，并一起切掉 `signature/analyze/emit` 三条主链上的平行 slot 布尔判断。
- wasm `slot kind` 收完后，还要继续把 `type_index / import_index / local callee index` 收回共享 helper；只要 `register_import / emit_call_target / collect_imports / encode return_call_*` 还各自手抄签名解析和索引查找，import 这一列就还没真正只有一份真源。
- `type_index / import_index / local callee index` 收完后，不要只停在 call target 这一半；function-side 的 `signature/type_index` 也必须立刻一起收回共享 helper。只修 target 不修 function，`context_init / collect_imports / module type section` 还是会继续各抄一份 slot/signature 规则。
- `signature/type_index` 收完后，就不要继续在签名层打转；下一步最值的是把 wasm composite copy 的数据搬运入口也收成单一 helper。`pointer-result / seq add / lvalue copy / static return slot` 如果继续各写一份 `local_set + copy_region_between_locals`，import/copy-materialize 这条线还是会在更低层留第二份真源。
- 验 C bootstrap codegen 改动时，stage0 本体不能直接当 ordinary smoke compiler；最稳流程是先 `bootstrap-bridge`，再拿 fresh `stage2` 配 `CHENG_V3_NO_BACKEND_DRIVER_HANDOFF=1` 跑回归。

## 记录与流程

- 账本只写当前有效信息；旧历史压缩，不要让长文档遮住当前决策。
- 开新任务前先看 `lessons.md`，避免把已经踩实的坑重新踩一遍。
- `backend_driver_command_surface_smoke` 这类会在命令面里再触发 `stage3 system-link-exec` 的长链验收，在 macOS 上很容易把 `cheng.stage3 system-link-exec` 卡住。正式收口要先把当前任务对应的命令直接跑绿。
- bootstrap freshness 必须跟真实 bridge 选路一致；如果 no-handoff 实际优先吃 `stage2`，freshness 也必须同时检查 `stage2/stage3`，不能只看 `stage3`。
