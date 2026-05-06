# Progress (2026-05-06)

## Cold compiler bootstrap slice 扩展
- 完成度：91%（四个本轮缺口已打通；剩余完成度取决于真实 2000 行 bootstrap kernel 覆盖，不靠再堆 fixture 计算）
- `type A =` 后缩进字段块按 Cheng 正式默认 object 写法解析，不要求 `object` 关键字；字段 layout 走 `object_finalize_fields`
- `*` `>>` `&` 已进入 tokenizer、BodyIR opcode 和 ARM64 codegen；`&`/`*` source-direct 已验证
- 非 int32 泛型参数已支持：`Result[ObjType, Diag]` 会从泛型 ADT 单态化出真实 payload layout，object payload 和 object error 均按实际 slot size 传递/返回/match
- `var int32[]` 参数和 `add(xs, value)` 已支持；动态序列 slot 为 `len/cap/buffer`，容量不足时用 Darwin `mmap` 扩容，object 字段 `b.items` 可通过 field ref 传给 `add`
- 新增验证切片：`cold_bootstrap_slice_var_add` exit 26，`cold_bootstrap_slice_generic_result` exit 42
- 最新耗时：`cc_cheng_cold_ms=1929.078 rc=0`；`var_add` 直编外层 448.593ms/报告 20.266ms，source->CSG 19.312ms/报告 16.368ms，facts direct 16.717ms/报告 14.037ms；`generic_result` 直编 18.621ms/报告 15.913ms，source->CSG 18.277ms/报告 15.458ms，facts direct 18.028ms/报告 15.318ms
- ASan/UBSan：`cc_cheng_cold_asan_ms=2894.314 rc=0`；`var_add` source->CSG 611.681ms/报告 15.056ms，facts direct 46.161ms/报告 16.119ms；`generic_result` source->CSG 43.264ms/报告 15.662ms，facts direct 45.778ms/报告 16.493ms
- 回归矩阵：17 个旧 CSG/source->facts 切片全部通过，报告内 `cold_compile_elapsed_ms` 范围 14.959ms-24.199ms，退出码符合预期
- 剩余缺口：真实 bootstrap kernel 还未成片迁入；`add` 当前只承诺 `int32[]`；facts 文本格式对嵌套泛型字段仍需结构化化，不能继续靠逗号文本切分扩展

# Progress (2026-05-05)

## Cold compiler (`bootstrap/cheng_cold.c`, 310 loc)
- Arena + SoA BodyIR + direct ARM64. `fn main(): int32 = return 42` → exit 42.
- Parser: `type` (algebraic), `fn` (multi-line body), `return <int>`.
- `if/else` IR construction done; branch patching: true-block correct, false-block off by 1.
- `macho_direct.h`: 280 loc, 12 load commands. Codesign alignment WIP.

## Full compiler (`src/`)
- Bootstrap: `bootstrap-bridge` ok (1373 fns, 0 new missing). `build-backend-driver` ok.
- Body kind: 9 converged to BodyIR CFG. `&&` compound conditions in if/elif.
- Parallel: 4-phase `thread.Spawn` chunked. Serial 495s→130s. Parallel est. ~33s (15x).
- Types: `PrimaryObjectError` nested type (36 fields→1).
- Docs: algebraic type + `match` syntax in `docs/cheng-formal-spec.md` §1.2.

## Remaining
- Cold: fix false-block branch, add `match` codegen, extend to bootstrap subset.
- Full: CSG incremental, `BACKEND_JOBS>1` with stage3.
- Mach-O: codesign page alignment.

# Progress (2026-05-06)

## Cold bootstrap completion score
- 当前完成度：91%。
- 最终 100% 定义：`bootstrap/cheng_cold.c` 能在 cold path 中编译 10万-30万行编译器核心，直写可运行 Mach-O，替代当前 cold seed 启动路径；默认验证包含 source->CSG、CSG->BodyIR、direct Mach-O、ASan、真实自举闭包回归，冷编译耗时稳定落在 30-80ms 目标窗内。
- 当前阶段 milestone：先打通约 2000 行真实 bootstrap 子集，作为从 fixture 走向全量冷自举的第一段，不等于最终完成。
- 进度权重：基础 Arena/SoA/Mach-O/毫秒报告 15/15；source->CSG facts 与 Cheng exporter 15/15；CFG 12/15；ADT/str/object/composite ABI 20/20；Result/泛型错误流 13/15；真实 bootstrap 子集覆盖 11/15；替代 seed gate 与性能门禁 5/5。
- 下一个涨点：迁入真实 bootstrap kernel 的源码片段，减少手写 slice 与真实编译器源码之间的距离。

## Cold compiler CSG path
- `bootstrap/cheng_cold.c` 新增 `system-link-exec --csg-out:<facts>`：源码先生成 cold CSG facts，再从 facts 降到 SoA BodyIR 和 direct Mach-O。
- backend-driver candidate smoke 已改为源码生成 facts，不再手写 `system-link-smoke.csg.txt`。
- 修复 `if/elif/else` fallthrough block 封口顺序：true 分支在进入 false/else 构建前先封口，避免后续分支 op 被错误计入同一 block。
- 验证耗时：fixture `--csg-out` 16.266ms，fixture source direct 19.379ms，fixture generated facts direct 15.811ms；旧 compound/or/elif facts 分别 16.705ms、13.280ms、16.231ms。
- backend-driver cold candidate：embedded compile 15.706ms，build-backend-driver 1088.999ms，生成 smoke compile 17.855ms，smoke exit 77。

## Cheng-side exporter
- `src/core/tooling/compiler_csg.cheng` 新增 cold CSG facts exporter，`backend_driver_dispatch_min.cheng` 暴露 `--cold-csg-out` sidecar。
- 当前 `artifacts/backend_driver/cheng` 已包含新 Cheng 侧改动；`run-host-smokes cold_csg_sidecar_smoke` 用轻量 sidecar gate 验证 exporter、facts direct 冷编译和最终退出码。
- targeted heavy exporter smoke 仍不作为默认通用 gate；默认门禁只跑已接入命令面的 cold sidecar smoke。

## Cold ADT/match facts
- `bootstrap/cheng_cold.c` 的 source-to-CSG facts 新增 `cold_csg_type`、`match`、`case` rows；CSG loader 直接把 ADT variant 写入 `Symbols`，lowerer 降为 `TAG_LOAD + SWITCH + PAYLOAD_LOAD`。
- 新增 `src/tests/cold_csg_match_fixture.cheng`：`type Option[T] = Some(value: T) | None` + `match`，源码直编、源码生成 facts 后编译、facts 直接编译均 exit 7。
- 最新验证耗时：match source direct 25.132ms，match `--csg-out` path 24.416ms，match facts direct 16.804ms；旧 for/if/elif fixture source direct 15.520ms，`--csg-out` path 14.391ms，facts direct 13.692ms，exit 11。
- 冷 backend-driver candidate 仍通过：build 1519.804ms，内置 `system-link-exec` smoke exit 77，报告 `cold_system_link_exec_smoke_csg_lowering=1`。

## Cold multi-payload Result
- BodyIR op table 新增第三操作数，`BODY_OP_MAKE_VARIANT` 通过 dense side table 携带 payload slot 和 payload offset 列表；variant frame slot 按父类型最大 payload layout 分配。
- `parse_constructor`、源码 `match`、CSG `match` 均支持多 payload binding；`cold_csg_type` facts 兼容 `Variant:field_count`。
- 新增 `src/tests/cold_csg_result_payload_fixture.cheng`：`Result[T,E] = Ok(value:T, extra:T) | Err(error:E, code:E)`，三条路径均 exit 42。
- 最新验证耗时：Result source direct 22.938ms，Result `--csg-out` path 22.261ms，Result facts direct 13.277ms；Option 回归 source direct 17.858ms，`--csg-out` path 14.522ms，facts direct 15.030ms；旧 CFG facts direct 14.575ms，exit 11。
- 冷 backend-driver candidate 仍通过：build 1367.243ms，内置 `system-link-exec` smoke exit 77，报告 `cold_system_link_exec_smoke_csg_lowering=1`。

## Cold str payload
- `SLOT_STR` 按 16 bytes `ptr,len` 表示；`BODY_OP_STR_LITERAL` 把短字符串字节写入 text 内联数据并把 ptr/len 写入 frame，`BODY_OP_STR_LEN` 读取 len 返回 i32。
- `cold_csg_type` facts 扩展为兼容格式：旧 `Variant:count` 仍按 int32 读；新 `Variant:count:kinds` 用 `i/s` 标记 int32/str 字段，例如 `Ok:2:si`。
- 新增 `src/tests/cold_csg_str_payload_fixture.cheng`：`TextResult = Ok(message: str, code: int32) | Err(error: str, code: int32)`，`Err("cold", 38)` 经 match 后 `len(error) + code`，三条路径均 exit 42。
- 最新验证耗时：str source direct 22.301ms，str `--csg-out` path 21.959ms，str facts direct 14.679ms；ASan 版 source direct 17.454ms 且 exit 42。
- 回归：Result source direct 19.101ms、`--csg-out` 21.742ms、facts direct 14.113ms，exit 42；Option source direct 14.097ms、`--csg-out` 14.651ms、facts direct 13.943ms，exit 7；旧 CFG source direct 15.404ms、`--csg-out` 13.429ms，exit 11。
- 冷 backend-driver candidate 仍通过：build 1411.361ms，内置 `system-link-exec` smoke exit 77，报告 `cold_system_link_exec_smoke_csg_lowering=1`。

## Cold composite return and str args
- `fn -> str` 与 `fn -> ADT` 改走 Darwin hidden sret ABI：caller 写 `x8 = &dst`，callee 进入时保存 `x8`，return 时把 `ptr,len` 或 variant slot 拷贝到 return buffer。
- 返回类型解析改为 hard-fail：只允许 `int32/int`、`str/cstring`、已登记 ADT；未知返回类型不再静默退回 i32。
- `cold_body_call_count` 现在同时统计 i32 call 和 composite call，避免复合调用报告为 0。
- 函数参数 facts 从纯名字扩展为 `name:i/name:s`；source parser 和 CSG lowerer 都按 typed param 分配 slot，`str` 参数走 `x0/x1`，后续 i32 参数顺延到 `w2`。
- 新增 `src/tests/cold_csg_composite_return_fixture.cheng`：`MakeText(): str`、`MakeResult(): TextResult`、`main` match 后 exit 42。
- 新增 `src/tests/cold_csg_str_arg_fixture.cheng`：`LenText(text: str)` + `AddText(text: str, extra: int32)`，source direct 21.581ms、`--csg-out` 18.388ms、facts direct 22.191ms，均 exit 42；ASan source direct 19.667ms，exit 42。
- 最新回归：typed str arg `--csg-out` 19.990ms exit 42，ASan source direct 18.019ms exit 42；composite `--csg-out` 14.736ms exit 42；Option match 16.114ms exit 7。上一轮完整回归中 str payload 18.817ms、Result payload 23.046ms、旧 exporter CFG 15.943ms 均通过。

## Cold Result question flow
- `source->CSG` 已识别 `let value = ResultExpr?` 为 `let_q`，`ResultCall()?` 语句为 `call_q`。
- `let_q` 降为 tag check：Err 分支早返回原 variant，Ok 分支加载 payload 继续执行；`call_q` 降为 Err 早返回、Ok 继续执行并丢弃 payload。
- `return expr?` 在 source parser / source->CSG exporter 入口硬失败；`expr?` 保留给 `let value = expr?` 和独立 `Call()?`，不能静默当 Result 原样返回。
- 新增 `src/tests/cold_csg_result_q_fixture.cheng`：Ok 解包后继续计算，Err 路径早返回；source->CSG 15.057ms exit 49，facts direct 16.402ms exit 49，ASan source->CSG 16.745ms exit 49。
- 新增 `src/tests/cold_csg_result_call_q_fixture.cheng`：`Check(1)?` 继续，`Check(0)?` 早返回 Err；source->CSG 19.045ms exit 7，facts direct 20.985ms exit 7。
- 本轮 fresh compiler 回归：composite return 23.531ms exit 42；typed str arg 23.995ms exit 42；Option match 20.248ms exit 7。

## Cold bootstrap slice: types
- 新增并验证 `src/tests/cold_bootstrap_slice_types.cheng`：覆盖 enum-like ADT、带 `str + nested ADT` payload 的 ADT、跨函数复合返回、variant 参数 ABI、嵌套 `match`。
- source->CSG facts 支持多行 ADT 声明：`type TypeInfo =` 后续 indented variants 被收进同一 `cold_csg_type` row。
- `cold_csg_type` field kind 新增 `v`：用于 nested variant payload；constructor 和 payload load 按 slot size 拷贝。
- 语义边界：函数参数 facts 现在保留用户 ADT 类型名，`v` 只作为 legacy/field kind；大于 16 bytes 的 ADT 参数按地址传递并在 callee 复制完整 slot，避免静默截断。
- 验证：source->CSG 17.070ms exit 4，ASan source->CSG 14.956ms exit 4；facts 中 `TypeInfo` 输出为 `Named:2:sv,Ptr:1:s,Empty:0`。
- 本轮核心回归：Result `let_q` 20.356ms exit 49；Result `call_q` 24.224ms exit 7；composite return 22.856ms exit 42；typed str arg 24.198ms exit 42；str payload 24.146ms exit 42；Option match 22.269ms exit 7。

## Cold large ADT params and match-call target
- `FnDef` 新增参数 slot size；source->CSG 对用户 ADT 参数输出 `name:TypeName`，CSG finalize 用 type table 解析真实大小。
- ABI 规则：`int32` 走 1 个寄存器，`str` 走 2 个寄存器，ADT payload `<=16 bytes` 走 2 个寄存器，`>16 bytes` 走 1 个地址寄存器，callee 从该地址复制完整 slot。
- `BODY_OP_MAKE_VARIANT` 先清零整个 variant slot，再写 64-bit tag 和 payload；nested variant payload、payload load 按 slot size 拷贝，不再只搬 2 word。
- 修复 `span_trim({0})` 的 UBSan 未定义行为。
- 新增 `src/tests/cold_csg_match_call_target_fixture.cheng`，覆盖 `match Pick(1):` 表达式目标；source->CSG facts 中保留 `match\tPick(1)`。
- 最新验证：bootstrap slice source->CSG 24.305ms exit 4，facts direct 20.400ms exit 4；`match Pick(1)` source->CSG 24.539ms exit 42；Result `call_q` 23.950ms exit 7；composite return 24.345ms exit 42；ASan/UBSan bootstrap slice 21.197ms exit 4，ASan/UBSan `match Pick(1)` 21.370ms exit 42；报告中仅输出 `cold_compile_elapsed_ms`，无 `elapsed_us` 字段。

## Cold bootstrap slice: object/field/index
- 新增 `cold_csg_object` facts：`Name\tfield:i,field:aN`，当前覆盖 object 字段布局、`int32[N]` 定长数组字段、字段 offset 和 object slot size。
- `Symbols` 新增 object table；`BodyIR` slot 记录 type 和 array length；`SLOT_OBJECT/SLOT_ARRAY_I32` 进入 composite ABI。
- `Object(field: expr)` 构造、`[i32, ...]` 字面量、`obj.field`、`array[index]`、`array.len` 已进入 source->CSG/CSG lower/codegen。
- 修复 constructor 嵌套表达式污染全局 side table：variant/object/array constructor 先本地收集 payload slot/offset，再一次性写入 dense side table。
- 新增 `src/tests/cold_bootstrap_slice_object_field_index.cheng`：`ColdRecord` object 含 `base/step/values:int32[3]`，跨函数 sret 返回 object、object byref 参数、字段读取、数组下标读取，运行 exit 24。
- 最新验证：object slice source->CSG 19.404ms exit 24，facts direct 21.215ms exit 24；ASan/UBSan object slice 17.546ms exit 24；bootstrap types 16.097ms exit 4，ASan/UBSan 17.612ms exit 4；match-call 15.729ms exit 42；Result `call_q` 17.776ms exit 7；composite return 16.908ms exit 42；str payload 14.937ms exit 42。

## Cold bootstrap slice: tuple/default init
- `type` block 现在支持同一块内混合 `object`、`tuple[...]`、ADT；边界扫描遇到同级 entry 或顶层声明会回退到行首，不再吞掉下一条 type row。
- `tuple[...]` 在 cold subset 中降为无 tag 的 object layout：字段 offset/size、constructor、field load、sret/byref ABI 复用 `cold_csg_object`。
- `let name: Type` / `var name: Type` 已进入 source->CSG，生成 `default` row；CSG lowerer 按类型分配 slot，`int32` 写 0，复合类型整 slot 清零。
- 新增 `src/tests/cold_bootstrap_slice_tuple_default.cheng`：覆盖 type block、tuple-as-object、object/tuple `T()` 零值、`int32[2]` typed default、字段读取和数组下标，运行 exit 30。
- 最新验证：tuple/default source->CSG 21.270ms exit 30，facts direct 18.105ms exit 30；ASan/UBSan source->CSG 16.086ms exit 30；object slice 19.997ms exit 24，ASan/UBSan 17.613ms exit 24；bootstrap types 21.224ms exit 4，ASan/UBSan 17.085ms exit 4；match-call 19.567ms exit 42；Result `call_q` 17.330ms exit 7；composite return 17.480ms exit 42；str payload 21.396ms exit 42；报告中仅输出 `cold_compile_elapsed_ms`，无 `elapsed_us` 字段。

## Cold Result question source-direct
- 旧 source-direct parser 补齐 `let value = ResultExpr?` 与独立 `ResultCall()?`：Err 早返回原 Result，Ok 继续；`match { ... }` brace 形式不再只依赖 source->CSG path。
- `return expr?` 明确禁止：`expr?` 的值语义是解包后的 Ok payload，函数返回通常是 Result，当前不引入“return 自动包 Ok”的隐式规则。
- CSG `let_q/call_q` 不再固定读取 offset 8 的 int32；现在按 Result 的 `Ok` variant payload layout 取 offset/kind，并要求函数返回类型与 Result 类型一致。
- 验证：`return Need()?` 负例 639ms 内硬失败；Result `let_q` source-direct 18.632ms exit 49、source->CSG 15.352ms exit 49、facts direct 15.676ms exit 49；Result `call_q` source-direct 13.014ms exit 7；brace match str source-direct 14.581ms exit 42。
- 回归：tuple/default source->CSG 14.677ms exit 30、facts direct 14.082ms exit 30；object source->CSG 13.318ms exit 24、facts direct 13.280ms exit 24；bootstrap types source->CSG 17.470ms exit 4、facts direct 17.245ms exit 4；match-call 18.225ms exit 42；composite return 15.181ms exit 42；typed str arg 14.478ms exit 42。
- ASan/UBSan：Result `let_q` source-direct 14.075ms exit 49；tuple/default source->CSG 13.604ms exit 30；brace match str source->CSG 13.307ms exit 42。
- 冷 backend-driver candidate 仍通过：外层 1378ms，内置 cold compile 17.109ms，build report 1353.216ms，`cold_system_link_exec_smoke_exit=77`。

## Cold dynamic sequence local
- 语义口径修正：`int32[N]` 是固定长度数组；`int32[]` 才是动态序列，当前 cold slot layout 为 `len:int32 + cap:int32 + buffer:ptr`。
- `SLOT_SEQ_I32`、`BODY_OP_MAKE_SEQ_I32`、`BODY_OP_SEQ_I32_INDEX` 进入冷路径；支持局部 `let xs: int32[] = [..]`、`let xs: int32[] = []`、`let xs: int32[]` 默认空值、`.len`、常量下标。
- `source->CSG` 新增 typed binding rows：`var_t/let_q_t`，避免 `int32[]` initializer 在 facts 路径丢失类型上下文。
- 明确边界：`int32[]` 返回值、object 字段、ADT payload 暂不支持，避免返回或持久化栈上 backing buffer。
- 修复默认初始化 parser：`let x: Type` 判断 `=` 时不能用会消耗 token 的失败 `parser_take`，否则会吞掉下一行首 token。
- 新增 `src/tests/cold_bootstrap_slice_seq_local.cheng`：同测 `int32[]` 动态序列和 `int32[2]` 固定数组，预期 exit 47。
- 验证：source direct 16.404ms exit 47；source->CSG 15.579ms exit 47；facts direct 14.669ms exit 47；ASan/UBSan source->CSG 13.865ms exit 47；最终 q-alias 补丁后 source->CSG 19.224ms exit 47，`q` facts alias 15.929ms exit 47。
- 回归：tuple/default source->CSG 16.209ms exit 30；object source->CSG 16.196ms exit 24；Result `let_q` source->CSG 12.553ms exit 49；`return Need()?` 负例 9.509ms 硬失败。
- 冷 backend-driver candidate 仍通过：外层 1771.870ms，内置 cold compile 15.081ms，build report 1732.659ms，`cold_system_link_exec_smoke_exit=77`。

## Backend sidecar importc closure
- 当前完成度：88%。
- `artifacts/backend_driver/cheng` 已刷新成功：最近一次 `build-backend-driver` 234227.954ms rc 0，zero-exit selftest 通过，provider object 全链路 native link 通过。
- Typed IR 新增 importc 真实目标符号表，覆盖有返回值和 `void` importc；lowering/BodyIR call sequence 统一使用真实 C 符号，修掉 provider self-compile 中 `_c_malloc/_c_free/_raw_libc_*` 等本地符号泄漏。
- `PrimaryObjectPlan` reachability 对 importc call sequence 改按真实 importc symbol 比较，不再把 BodyIR 的真实符号和 Cheng 本地函数名误判为 drift。
- 手动 cold sidecar gate 通过：backend sidecar 编译 35259.824ms exit 0，生成 facts 含 `cold_csg_entry=main`、typed var/if/return rows；backend 输出运行 601.283ms exit 13。
- 冷 compiler facts 路径通过：`cc bootstrap/cheng_cold.c` 1524.486ms，`--csg-in` 外层 445.605ms，报告内 `cold_compile_elapsed_ms=19.961`，facts 输出运行 420.708ms exit 13；报告只含毫秒字段，无 `elapsed_us`。
- `compiler_csg -> cold facts` 主线已接入 ADT/match：`CompilerCsgExprLayerForProfile` 直接产 `match/case` normalized rows，`--cold-csg-out` 输出 `cold_csg_type + match + case` facts；`cheng_cold --csg-in` 降为 switch blocks 并运行 `Option.Some(7)` fixture exit 7。
- 最新 `run-host-smokes cold_csg_sidecar_smoke` 已覆盖旧 CFG fixture 和新增 ADT/match fixture：整条 gate 75131.076ms rc 0；旧 CFG backend 编译 35483ms、facts 冷编译 696ms、报告内 `cold_compile_elapsed_ms=18.279`、运行 exit 13；ADT/match backend 编译 35507ms、facts 冷编译 24ms、报告内 `cold_compile_elapsed_ms=13.018`、运行 exit 7。
- 命令面边界：当前 `run-host-smokes` 只支持 `cold_csg_sidecar_smoke`，缺失或未知 smoke 直接 hard-fail；不伪装成通用 smoke runner。

## Cold bridge boundary correction
- 当前完成度：88%。
- 已纠正：`cheng_cold.c` 主线不能为了命中旧 `stage3/cheng_seed.c` root 规则使用 `driver_c_read_file_all_bridge`；`src/core/tooling/path.cheng` 已恢复为中性 `cheng_read_file_bridge(str): str`，`backend_driver_dispatch_min` provider roots 也恢复为 `cheng_read_file_bridge`。
- 本轮实测：临时 `driver_c_read_file_all_bridge` 路线可让 backend candidate provider 链接通过，但被废弃；它会把 cold 主线语义绑到旧 seed/driver 命名，不作为进展。
- 当前阻塞：已安装旧 `artifacts/bootstrap/cheng.stage3` 二进制不包含 `cheng_read_file_bridge` root，不能继续用它证明 neutral cold 主线；下一步应推进 `cheng_cold.c` 自己的 neutral host bridge/root 表，或刷新 stage3 root 表后再跑 `build-backend-driver`。
- 本轮耗时记录：`stage3_build_backend_driver_ms=240384.089 rc=1`；临时 candidate relink `183.679ms rc=0`；手工 zero-exit 编译 `35555.331ms rc=2`；单独 host-runtime provider 少量 roots 编译 `1044.148ms rc=0`；完整旧 seed host roots 触发 bounds check `41.072ms rc=1`。
- 中性 cold 主线复验：`cc_cheng_cold_ms=1753.976 rc=0`；`cold_bootstrap_slice_seq_local` 编译外层 `665.964ms rc=0`，报告内 `cold_compile_elapsed_ms=20.344`，运行 exit 47。
