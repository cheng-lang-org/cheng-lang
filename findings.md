# Findings

| 项目 | 结论 |
|---|---|
| HIR 总体状态 | 现在完成的是“规范化声明 + 规范化表达式层 + typed fact + lowering rule”，还没到“seed/native/wasm 直接共吃完整 typed HIR”。 |
| 当前真卡点 | 真缺口不在 parser，而在 `/Users/lbcheng/cheng-lang/v3/bootstrap/cheng_v3_seed.c` 还残着很多按 `composite` 和 target 形状猜的热分支。 |
| 本轮收口点 | 最先该收的是 call arg 主链，因为它同时覆盖 native spill、wasm prepare 和 wasm emit，是最热也最容易反复冒错的地方。 |
| 第二刀主线 | `call arg` 收完后，最值的是顺手把 `seq/local/field/materialize` 同构分支一起接到 `v3_expr_materializes_to_address(...)`，这样不会又从 wasm local/builtin 旁路重新长出第二套判定。 |
| 第三刀主线 | `seq/local/field/materialize` 收完后，最值的是继续扫 `result intrinsic / str equality / constructor field`，因为它们本质上也都只是在问“这个值是不是必须按地址物化”；不一起收，native compare、wasm result、constructor 写入会继续平行残留第二套 `composite` 判断。 |
| 第四刀主线 | `result intrinsic / equality / constructor field` 收完后，最值的是顺着同一条规则继续扫 `result field/global/member/list elem copy`；这些路径表面是投影、复制、列表元素落位，但底层仍然只是在复用“这个值是不是必须按地址物化”，不一起收，materialize 和 wasm literal 会继续残留并行 ABI 文本判断。 |
| 第五刀主线 | `result field/global/member/list elem copy` 收完后，最值的是继续扫 `param ABI / builtin str bridge`。这些地方虽然看起来像 C ABI 或消息桥，但本质上仍然在问“这个参数是不是必须按地址传/物化”，如果不一起收，native/wasm 的 `panic/echo` 和 external call path 还是会残留一截并行文本规则。 |
| 当前尾巴 | 现在剩下的裸 `composite` 文本只剩 descriptor helper 自身一处，不再是功能热路径问题；继续清它的收益已经明显低于把同一套 lowering rule 往更高层 dispatch 真接进去。 |
| 第六刀主线 | 真正更高收益的下一步是把 `call target` 级 dispatch 口也收进 rule helper，而不是继续清 descriptor 尾巴。参数加载 ABI、临时参数准备、scalar call、call-into-address、wasm composite call return 这些地方一旦各自猜，typed lowering 的单一真源又会在上层被冲散。 |
| 第七刀主线 | `call target` 收完后，最值的不是停住，而是继续把 `call arg temp / expr temp / Result.Ok(...) / wasm call arg temp` 这层物化入口也一起切到地址规则；否则临时槽分配和结果物化很快又会重新长出第二套 ABI 猜测。 |
| 第八刀主线 | 临时槽入口收完后，最值的是继续扫 `field/global/default/setitem` 这批已经拿到了完整 `type + abi` 的高层分发口；这些地方如果还只看 ABI class，就会在字段默认值、构造器字段写入、local/global copy 和 `[]=` 写值上重新长出一层平行判断。 |
| 第九刀主线 | `field/global/default/setitem` 收完后，最值的是把 `Result.Value(...)`、local binding/local assignment/module global init 这批同样已经拿到完整 `type + abi` 的入口也一起切掉；否则 native 语句发码会在另一层继续保留一排平行的 `abi_class` 判断。 |
| 第十刀主线 | `Result.Value(...)` 和 native 语句发码入口收完后，最值的是把 `v3_add_local_slot_for_type(...)`、scalar global load、`seq add` 元素写入、expr statement prepare/codegen、param-slot `indirect_value` 这批低层同构入口也一起切到 `v3_expr_materializes_to_address(...)`；不顺手扫完，低层会继续残留一截 `scalar_or_ptr` 分发。 |
| 第十一刀主线 | 当 `scalar_or_ptr` 在低层分发里也退干净后，剩下的命中基本就是纯标量语义检查；下一步收益更大的方向不是再抠这些尾巴，而是把 return dispatch 也补成显式 rule helper，让 function/call-target 的返回路径和 arg/materialize 一样归一到同一套规则名。 |
| 第十二刀主线 | return dispatch 收完后，最值的是把 `Result projection / wasm region-copy` 这批热路径也补成显式 `copy kind`，不要继续让它们直接吃 `prefers_region_copy` 布尔值；不然 `copy/materialize/import` 这条线还是缺一块稳定规则名。 |
| 第十三刀主线 | `copy kind` 收完后，最值的是把 `call-into-address / composite call-result / wasm static return slot` 也补成显式 `call result kind` 和统一 helper；这些路径本质上都在问“结果是按值走，还是先得到地址/静态返回槽”，如果继续散着写布尔判断，后面 wasm fallback 和 native composite call 很快又会漂。 |
| 第十四刀主线 | `call result kind` 和 static return slot 收完后，最值的是把 `wasm composite pointer result` 这条链也统一成单一入口；不然 result intrinsic、call-result 和 generic fallback 还会各自维护一份 `emit expr -> local_set -> copy`。 |
| 第十五刀主线 | pointer-result fallback 收完后，最值的是把 wasm `builtin message ptr` 这条 import/emit 热链也补成显式 mode；`panic/echo/assert` 看起来只是 builtin，但 analyze/emit 两边如果都各写一份 `literal / ptr / str-address bridge` 三叉分支，import 规则很快又会漂成第二份真源。 |
| 第十六刀主线 | wasm `builtin message ptr` 收完后，最值的是回到 native importc `ffi_handle` 参数链；只把 scalar call 修成规则化还不够，`call-into-address` 这条复合返回入口如果还把 handle 参当普通标量过，importc 行为会在第二条主链上重新漂。 |
| 第十七刀主线 | native importc `ffi_handle` 收口时，不能只再加一层布尔 helper；更稳的是直接补显式 `arg kind`，把 `var / ffi_handle resolve / ffi_handle consume / value / address` 收成同一规则名，然后一次性切掉 `arg load abi`、prepare、scalar call、call-into-address 这几条入口。 |
| 第十八刀主线 | 参数链收完后，下一刀最值的是把 importc 返回也一起规则化；只把参数做成显式枚举还不够，`ffi_handle register` 如果继续只靠 `ffi_handle_return_is_handle` 裸布尔特判，`return pass / call result / return fixup` 这条返回链还会保留第二份真源。 |
| 第十九刀主线 | importc `ffi_handle` arg/return 收完后，下一刀最值的是把 wasm call/import 上那层 `i32 param slot / i32 return slot / void` 也补成显式 `slot kind`；不然 analyze/emit/signature 三条 call 主链还会继续各写一份 slot 布尔判断，import 规则还是会在 wasm 侧留第二份真源。 |
| 第二十刀主线 | wasm call/import 的 `slot kind` 收完后，下一刀最值的是把 `type_index / import_index / local callee index` 这层 import 分发表也收回共享 helper；不然 `register_import / emit_call_target / collect_imports / encode return_call_*` 还是会继续手抄同一套签名解析和索引查找。 |
| 第二十一刀主线 | `type_index / import_index / local callee index` 收完后，紧接着最值的是把 function-side `signature/type_index` 这一半也一起收回共享 helper；只修 call target 还不够，`context_init / collect_imports / module type section` 如果还各自手抄 `return_supported + param i32-slot`，wasm import 这一列仍然有两份签名真源。 |
| 第二十二刀主线 | `signature/type_index` 收完后，最值的已经不是再抠签名，而是把 wasm composite copy 的“数据搬运入口”收成单一 helper；只要 `pointer-result / seq add / lvalue copy / static return slot` 还各写一份 `local_set + copy_region_between_locals`，import/copy-materialize 这一列还是会在更低层保留第二份真源。 |
| 第二十三刀主线 | composite copy 收完后，下一步最值的是把“地址返回值写进 static return slot”也压成单一 helper；只要显式 `return x` 和 implicit return 还保留一条“直接物化”和一条“先 emit_expr 再 copy”的双轨，caller-side 的 import buffer 语义就还没有真正只剩一份分发。 |
| 第二十四刀主线 | static return slot 收完后，下一步最值的不是再让 wasm `call expr / call statement / composite call-result` 各自解释 `return_slot_kind`；这三条入口其实共享同一套“结果到底是 void、i32 value 还是 i32 address”语义，必须补成显式 `wasm call result kind`。 |
| 第二十六刀主线 | `wasm call result kind` 收完后，最值的是把 external import 这一列也补成显式 `import kind`，至少先把 `void / i32 value slot / i32 address slot` 从 `type_index` 侧面推断改成单点规则；不然 import 签名还是会继续借 `return_slot_kind` 影射。 |
| 第二十七刀主线 | `typed_expr` 里的 `import_buffer` 要真落地，前提不是再堆 seed helper，而是补最小 `CallExpr` 规范化；没有 call 节点，builder 根本没有地方把 importc 返回值打上 `import_buffer`。 |
| 当前真进展 | 现在已经补了最小 `CallExpr`，而且 composite importc call 已经能在 `/Users/lbcheng/cheng-lang/v3/src/lang/typed_expr.cheng` 里真落成 `lower=import_buffer / return=import_buffer`。 |
| 本轮真坑 | `compiler_csg_smoke` / `lowering_plan_smoke` 这次重新炸的根因不是 runtime，而是 `/Users/lbcheng/cheng-lang/v3/src/lang/parser.cheng` 的 `v3ParserCollectExternalCallNames(...)` 还保留了 `profiles[otherIndex].localCallNames[callIndex]` 这种复合字段嵌套索引，直接把 compiler 顶到 `idx=len`。 |
| 本轮真修法 | 这类复合字段嵌套索引不能继续依赖 compiler 自动发码；最稳形状是先把 profile 和 `localCallNames` 快照到本地，再用显式 `while` 走索引。这样 parser/csg/lowering 三条 smoke 才会稳定。 |
| 当前剩余缺口 | 这条新路径现在已经覆盖“同文件 importc + 同文件 local function + 同包跨文件已知函数”的 call expr；剩下的缺口是更通用的 call HIR，还没补到 import 别名和更一般的 closure 可见函数。 |
| 本轮额外发现 | call 规范化不能再走“名字表 × 行文本”切片扫描；哪怕只剩 importc 名表，这条路也会在 `compiler_csg_smoke` 里把 parser 拖进高成本字符串复制。正确形状必须是逐行 token 扫描，看到 `ident(` 再查当前源码自己的 call 名表。 |
| 本轮真坑 | parser 里任何带索引的边界判断都不能依赖短路，像 `i > 0 && line[i - 1]` 这种写法会直接重演 `idx=-1`。这里必须始终写成显式边界分支。 |
| fresh 验收面 | C bootstrap 改动不能直接拿 stage0 本体跑 ordinary smoke，因为它会先报 `missing embedded bootstrap contract`；稳定做法是 `stage0 bootstrap-bridge` 后，用 fresh `stage2` no-handoff 跑真 smoke。 |
