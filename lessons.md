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
- 分片重组这类热路径不要直接依赖 `Result[复合]` 再 `Value(...).field` 继续往下跑；先补 `Fill(out)` 主实现，再让包装函数复用它。
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
- composite copy 收完后，紧跟着就该把“地址返回值写进 static return slot”压成单一 helper。显式 `return x` 和 implicit return 如果一条直接物化、一条先 `emit_expr` 再 copy，caller-side 的 import buffer 语义还是会残留双轨。
- static return slot 收完后，不要让 wasm `call expr / call statement / composite call-result materialize` 继续各自解释 `return_slot_kind`；它们共享的是更高一层的“call result 到底是 `void / i32 value / i32 address`”语义，直接补显式 `wasm call result kind` 更稳。
- `wasm call result kind` 收完后，external import 这一列也要立刻补显式 `import kind`，不要继续让 `type_index` 靠 `return_slot_kind != void` 这种影子语义选签名。
- 如果 typed/lowering 里已经暴露了 `import_buffer` 这样的枚举和报表字段，但 fact builder 从来不赋值，就必须明确记成“还没接通”，不要把报表口当成真能力。
- `import_buffer` 这类调用归因要真落地，前提是 expr 层先有最小 `CallExpr`；没有 call 节点，就不要假装 typed builder 能凭空推导调用返回语义。
- 补 `CallExpr` 时先走最小闭环最稳：先只抓“同文件 importc fn 的 call”，把 builder 和 smoke 跑通，再扩到通用 call HIR。
- call 规范化绝不能回到“名字表 × 行文本”的切片扫描；正确形状是逐行 token 扫描，看到 `ident(` 再查名表，不然 `compiler_csg` 这类 closure 级 smoke 会被字符串复制直接拖死。
- `CallExpr` 真扩面时，也不要立刻把 closure 里所有函数名灌成全局名表；先坚持 per-source 名表最稳，先收“同文件 importc + local function”，再扩同包/跨文件。
- qualified external call 的真源应该是“当前 source 的 direct import edge + alias/module-stem qualifier + 目标 source decl”，不是“整包 closure 里所有函数名”。只看最后那个函数名会把 alias/模块前缀信息丢掉，也很容易在同名函数上误归因。
- nested qualified external call 也不能按“整条 import 闭包”无上限展开；external call name 递归必须按当前源码里真实出现的 dotted call 深度裁剪，不然普通 source 会把 std/import 闭包整棵跑进 parser。
- grouped import 不能走 `FirstToken` 这种按空白截断的老路；`prefix/[a, b]` 必须先 strip comment，再显式展开成多条 direct import edge，不然 direct import qualifier 这条真源到了 grouped import 就会断掉。
- unknown dotted call 的 `member_call` fallback 不能混在 `local/external/importc` 三路扫描里补；必须最后按全量已知 call 名单单独跑一遍，不然 qualified external/importc/local 会被双记，typed fact 会平白长出 `call_expr / abi=polymorphic`。
- unknown dotted call 不能只留 `resolved=0`；`CallExpr.detail` 至少要显式带 `reason`，不然后面的 typed/report 只能看到“没解析”，看不到“为什么没解析”。
- qualified external lookup 不能只回传“返回类型字符串”；只要目标可能是 imported `importc`，就必须把“目标是否 importc”一起带回来，否则 composite imported importc call 会被误压成 `local_address/composite_local`，进不了 `import_buffer`。
- resolved call 的硬 gate 不能只盯 parser；local/importc 空返回如果继续和“没找到返回类型”共用空串，第一时间就会把 resolved void call 误炸成 `call_expr/deferred`。
- 处理返回类型 ABI 时，`preferredSourcePath` 不能当成硬覆盖；正确形状是“先切到 callee source context，再按当前 source 声明和无 alias direct import 解析 unqualified type”。像 `DateTime` 这种 imported return type，不这么做一定会回退成 `abi=polymorphic`。
- `T/refT` 这类显式泛型占位符和真正的“无解释回退”不是一回事；resolved call gate 要拦的是漏归因，不是把 generic wrapper 一起打红。
- parser 里任何带索引的边界判断都不要依赖短路，像 `i > 0 && line[i - 1]`、`pos < len && line[pos]` 这种写法都必须拆成显式分支；这类坑已经多次重演成 `idx=-1`。
- parser 里也不要长期保留 `profiles[otherIndex].localCallNames[callIndex]` 这种复合字段嵌套索引；一旦热路径里既要取 `.len` 又要按相同索引取元素，最稳的形状是先快照到本地 seq，再用显式 `while` 走索引，不要赌 compiler 一定能把这类组合安全 lowering。
- 验 C bootstrap codegen 改动时，stage0 本体不能直接当 ordinary smoke compiler；最稳流程是先 `bootstrap-bridge`，再拿 fresh `stage2` 配 `CHENG_V3_NO_BACKEND_DRIVER_HANDOFF=1` 跑回归。
- seed 里任何 `8192 * PATH_MAX` 这类 plan 数组都不能再上栈；像 `v3_materialize_provider_objects(...)` 这种 provider 编译热口，必须直接用堆分配并在所有返回路径显式释放，不要等到 fresh stage2/provider compile 时再炸栈。

## 记录与流程

- 账本只写当前有效信息；旧历史压缩，不要让长文档遮住当前决策。
- 开新任务前先看 `lessons.md`，避免把已经踩实的坑重新踩一遍。
- `backend_driver_command_surface_smoke` 这类会在命令面里再触发 `stage3 system-link-exec` 的长链验收，在 macOS 上很容易把 `cheng.stage3 system-link-exec` 卡住。正式收口要先把当前任务对应的命令直接跑绿。
- bootstrap freshness 必须跟真实 bridge 选路一致；如果 no-handoff 实际优先吃 `stage2`，freshness 也必须同时检查 `stage2/stage3`，不能只看 `stage3`。
