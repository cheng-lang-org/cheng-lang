# Progress

## 当前目标

- 阶段目标：把 `src/tests/cold_bootstrap_kernel_combined.cheng` 扩成约 2000 行真实 bootstrap kernel 子集。
- 性能指标：只看冷编译报告里的 `cold_compile_elapsed_ms`。
- 不看指标：`*_external_ms`，它包含进程启动、文件系统、codesign、运行可执行等外层成本。
- 当前阶段验收：source-direct、source->CSG、facts direct 三条路径都能编译并运行到预期 exit；报告只输出毫秒字段，不出现 `elapsed_us`。
- 最终目标：`bootstrap/cheng_cold.c` 替代 cold seed 启动路径，编译 10万-30万行编译器核心并稳定落入 30-80ms 目标窗。

## 当前完成度

- 阶段完成度：99.7%。
- 当前 combined kernel：2035 行已验证通过；约 2000 行 bootstrap 子集 milestone 已达成。
- 已打通主线：Arena + SoA BodyIR、source->CSG v2 facts、CSG->BodyIR、direct ARM64 Mach-O、毫秒报告。
- 已覆盖真实 bootstrap slice：frontend scanner、outline parser scanner、parser statement sequence scanner、expression token scanner/facts、phase arena、dense layout cursor、work-stealing CAS model、AArch64 encode、object/field/index 写回、dynamic `int32[]`、ADT/match、Result `?`、str payload、复合返回、inline suite。

## 最新通过基线

- `cc_cheng_cold_ms≈2040 rc=0`，无 warning。
- combined direct：报告 `cold_compile_elapsed_ms=14.405`，exit `42`。
- combined source->CSG：报告 `cold_compile_elapsed_ms=14.907`，exit `42`。
- combined facts direct：报告 `cold_compile_elapsed_ms=14.511`，exit `42`。
- combined 当前观测：`cold_frontend_function_count=133`，`cold_body_op_count=4094`，`cold_max_frame_size=2764`，`cold_max_slot_count=390`。
- v2 回归矩阵：23 个核心 cold slice 的 source->CSG 与 facts direct 全部通过，报告范围 `11.042ms-23.098ms`。
- ASan/UBSan：combined direct/source->CSG/facts direct 报告 `19.799ms/16.411ms/15.829ms`，均 exit `42`。

## 当前进行中

- CSG v2 facts writer/reader/loader 的真实 byte-buffer slice 已并入 combined kernel。
- loader 现在单次扫描 v2 record，保存字段 span/len，流式 exact 识别 record kind，统计 record/field/byte/kind 计数，不复制字段字符串。
- parser statement sequence scanner 已并入 combined kernel：逐行记录 indent/token/payload span/kindCode，单次扫描统计 checksum，不复制 statement 字符串。
- statement sequence 现在可直接写入 v2 `cold_csg_stmt` records：int 字段按十进制字节的 hex field 输出，reader 流式解码 `fieldInt/fieldIsDecimal`，facts checksum 与 scanner checksum 一致。
- expression token scanner/facts 已并入 combined kernel：token meta/span 使用 dense packed int32 arrays，v2 `cold_csg_expr_token` facts 单次读取校验。
- phase arena、dense layout cursor、work-stealing CAS SoA 队列已并入 combined kernel，覆盖极限线的 arena reset、layout offset、steal/pop 顺序语义。
- 已修通 `cold function arg kind mismatch`：call 参数改为本地收集后统一追加 dense side table，嵌套 call 不再污染外层 `arg_start`。
- 已修通多行函数签名 source->CSG 参数/statement 错位、scalar 后复合数组字段 4 字节对齐导致的 payload 读写错位、复合拷贝 8 字节越界、以及 cold 表达式优先级左结合导致的 work-deque 算术错降。

## 下一步

- 已开始抽取 `backend_driver_dispatch_min.cheng` 的真实参数/返回类型面：冷端已能识别 `alias.Type`、`Result[alias.Type]`、`var str`、`str[]`、`var str[]`，参数槽位扩到 16。
- 新增 `cold_bootstrap_backend_dispatch_type_surface`，source-direct 已验证：`cold_compile_elapsed_ms=14.526`，可执行 exit `42`。
- 当前 `backend_driver_dispatch_min.cheng` 已越过签名类型层、bodyless `@importc fn`、直接 import module surface、qualified call 识别、imported overload 按参数 kind 解析、void 函数隐式 return、`Fmt"..."` lowering、top-level `const`、imported enum/object layout refinement、`Result[T]` object intrinsics、Darwin stack 参数 ABI、stdio 写入、真实 `argc/argv/envp`、`gettimeofday/getrusage/exit` syscall、path 文件读写、`GetEnv/ParseInt/Len/SliceBytes/Split/Strip/Join` 和 `CompilerCsgTextSet` 最小集合操作。
- 已越过 `slplan.BuildSystemLinkPlanStubWithFields`：冷端新增结构化 `SystemLinkPlanStub` materializer，生成物 `backend_driver_dispatch_min_probe status --root:. --in:... --out:...` 已输出 `flag_exec_edges=0`、`flag_exec_unresolved=0` 并 exit 0。
- 此前 `backend_driver_dispatch_min.cheng` 已可被 cold `system-link-exec` 消费并生成 Mach-O：`backend_driver_dispatch_min_probe` 编译成功，报告 `cold_compile_elapsed_ms=33.410`。
- 本轮重建 direct import body 编译：`cold_compile_one_import_direct` 按直接 import 解析 body，不递归编译；Parser 带 `import_alias` 仅用于限定当前模块边界，导入模块内本地名调用走模块局部精确查找，不自动拼 `alias.name`，也不做 bare-name fallback；bodyless/imported 签名注册为 `external`，有 body 后清掉 external；linkerless codegen 只发射入口可达函数闭包并跳过 external。
- 本轮继续收紧 cold codegen 正确性：入口可达函数闭包先做 DFS，任何可达 body 缺失、fallback body、未终结 block 或未解析 patch 都 hard-fail，不再生成 `mov x0,#0` 假返回；`assert` 和 `strings.strContains` 走精确 intrinsic；CSG 无体函数不再合成默认 return；`emit:obj` 不再为坏 body 生成 stub。
- `emit:obj` 的 C ABI 边界已硬化：未声明外部调用直接失败；显式 `fn puts(s: cstring): int32` 走单指针 cstring ABI，并为 `str` 实参生成 NUL 结尾缓冲；`examples/test_hello.cheng -> .o -> cc -> exe` 输出精确 `Hello World!\n`。
- direct Mach-O 路径已移除 upfront 全量 import body 编译；现在先解析入口 body，再按 BodyIR call/FN_ADDR 可达闭包精确加载 import source、精确匹配目标签名并编译对应 body。缺失 body、bodyless import、external 可达调用都 hard-fail，不再 setjmp 恢复、不再 skip body。
- Heisenbug 修复基线：普通 `emit=exe` 不再因 `import` 自动切到 provider/system linker；direct codegen 只发射入口可达闭包，坏 body、未终结 block、未解析 patch 均 hard-fail；`emit:obj` writer 改为动态符号/重定位表，不再 128/256 截断。
- 新验证：`tools/cold_regression_test.sh` 连续 3 次重建后均 `7/7 PASS`；`import_use` direct Mach-O 编译运行 exit `3`，报告 `provider_object_count=0`、`system_link=0`、`cold_body_op_count=6`；`backend_driver_dispatch_min.cheng` direct Mach-O 编译成功，报告 `cold_frontend_function_count=1479`、`cold_body_op_count=5481`、`cold_codegen_words=154007`、`cold_compile_elapsed_ms=31.112`。
- 回归已扩到 `12/12 PASS`：新增 `import_use_linkerless`、`import_unresolved_hard_fail`、`dispatch_min_self_ordinary_zero`、`dispatch_min_self_import_use`、`dispatch_min_self_import_linkerless`。生成版 `backend_driver_dispatch_min` 已能用自己的 linkerless direct Mach-O 路径编译 `ordinary_zero_exit_fixture` 和 `import_use`，产物分别 exit `0/3`。
- 回归继续扩到 `14/14 PASS`：新增 `dispatch_min_self_subset_coverage`、`dispatch_min_self_subset_linkerless`。生成版 `backend_driver_dispatch_min` 现在可自编译 `cold_subset_coverage.cheng`，生成物运行 exit `0`，report 锁定 `direct_macho=1`、`provider_object_count=0`、`system_link=0`。
- 回归继续扩到 `16/16 PASS`，并已连续 3 轮重建通过：新增 `dispatch_min_self_emit_obj_cross`、`dispatch_min_self_emit_obj_direct`。生成版 `backend_driver_dispatch_min` 现在可自生成 `import_use.cheng` 的 Mach-O `.o`，`cc` 链接后运行 exit `3`，report 锁定 `emit=obj`、`direct_macho=1`、`provider_object_count=0`、`system_link=0`。
- 回归继续扩到 `24/24 PASS`，并已连续 3 轮重建通过：新增 `dispatch_min_self_dispatch_min`、`dispatch_min_self_dispatch_direct`、`dispatch_min_self_dispatch_status`、`dispatch_min_self_dispatch_status_marker`、`build_backend_driver_cold_linkerless`、`build_backend_driver_no_cc_fallback`、`build_backend_driver_status`、`build_backend_driver_status_marker`。生成版 `backend_driver_dispatch_min` 已能编译自身，二代产物 `status` 输出 `bootstrap_mode=selfhost`、`flag_exec_edges=0`、`flag_exec_unresolved=0`；cold `build-backend-driver` 已删除 `.o -> cc` 补救路径，冷 linkerless 构建仍通过。
- 回归继续扩到 `27/27 PASS`，并已连续 3 轮重建通过：新增 `import_bare_helper`、`import_bare_helper_linkerless`、`import_deep_hard_fail`。浅导入内部裸名 helper 现在严格绑定当前 import 模块，生成 direct Mach-O 后运行 exit `39`；二级 import 保持 hard-fail，深层导入闭包仍由 Cheng 工具链处理。
- 已将 `artifacts/backend_driver/cheng` 覆盖为 cold linkerless 候选 `cheng.cold_candidate`。覆盖前候选通过 `help/status`、`ordinary_zero/import_use/cold_subset_coverage`、跨模块 `emit:obj`、自编译 `backend_driver_dispatch_min`；覆盖后正式入口通过同一组门禁，并能再次 `build-backend-driver` 生成下一代候选，输出中无 `cold_cc_link`。
- 新验证：`body_ir_noalias_proof_smoke`、`ownership_proof_driver_cold`、`compiler_csg_egraph_active_contract_smoke`、`ordinary_zero_exit_fixture --csg-out` 均通过；egraph smoke 编译 stderr 为 0，旧 import body 噪音已消除；`emit:obj` 显式 cstring hello 仍输出精确 `Hello World!\n`，未声明 `puts` 仍按预期失败。
- 新验证：`artifacts/cold_import_direct/cheng_cold system-link-exec --in:src/core/tooling/backend_driver_dispatch_min.cheng --out:artifacts/cold_import_direct/backend_driver_dispatch_min_probe` 成功，`cold_compile_elapsed_ms=153.175`，`cold_frontend_function_count=3135`，`cold_body_op_count=22535`，`cold_codegen_words=139614`。
- 生成物 `backend_driver_dispatch_min_probe status --root:. --in:src/core/tooling/backend_driver_dispatch_min.cheng --out:...` exit 0，输出 `flag_exec_edges=0`、`flag_exec_unresolved=0`；`backend_driver_dispatch_min_probe system-link-exec --in:src/tests/ordinary_zero_exit_fixture.cheng --out:...` exit 0，生成 Mach-O 可执行，运行 exit 0。
- 已移除 `RunSystemLinkExecFromCmdline` 的 cold probe `DirectObjectEmitWriteObject` 短路；生成物现在走真实 `system_link_plan -> compiler_csg` 路径，不再把 self-embed direct emit 当作普通 `system-link-exec` 成功。
- 新验证：`/tmp/cheng_cold_current` 可用 `-Werror` 编译，冷编译 `backend_driver_dispatch_min.cheng` 成功；生成物 `status/help` 均 exit 0。
- 生成物 `backend_driver_dispatch_min` 跑 `system-link-exec ordinary_zero_exit_fixture` 现在结构化失败于 `ccsg.BuildCompilerCsgInto`：exit `2`，stderr 写出 `cold full compiler integration: CompilerCsg materializer is not connected`，report sidecar 非空并包含 `exec_phase_*` key，不再 SIGTRAP、不再空 report、不再假写 output。
- 当前硬边界：`ccsg.BuildCompilerCsgInto` 仍是严格未接入边界；下一步必须接真实 CSG materializer（至少 `CompilerCsg.nodes/edges/exprLayer/typedExprFacts/sourceBundleCid` 的结构化对象写入），再推进 `lowering_plan -> primary_object_plan -> direct_object_emit`，不能返回空 CSG 或假成功。
- 距离完全体还差：真实 CSG source closure、真实 lowering plan、真实 primary object plan、direct object/exe/linkerless image、完整 ARM64 编码与重定位、每次编译稳定 phase/report sidecar、mmap span/phase arena/SoA/int32 index/单扫 facts、lock-free work-stealing 并行、10万-30万行核心 30-80ms 冷自举验证。
- source->CSG 仍未同步本轮能力：`cold_bootstrap_backend_dispatch_type_surface --csg-out` 当前失败，原因是旧 statement scanner 仍会把多行函数签名续行当成 statement；下一步修 CSG writer 必须按 `ColdFunctionSymbol.body` 边界跳过签名续行。
- 下一步继续把 expr token facts 接到 statement payload 并向 3000 行推进，同时保持 `cold_compile_elapsed_ms` 三路径一致。
- 每轮继续记录 `cold_compile_elapsed_ms`，并保持 source-direct、source->CSG、facts direct 三路径一致。
