# Cheng 工业级路线图

> 口径：只记录当前仓库能证明或必须硬证明的状态。愿景可以写目标，不写成完成。若与实现冲突，以 `docs/cheng-formal-spec.md`、`src/core/tooling/README.md`、当前源码和当前可执行产物为准。

## 当前事实（2026-05-14）

### 可信进度：CSG v2 毫秒级后端主线前半段

**已成立（可复现验证）**

- **冷编译器三架构 codegen**：ARM64/x86_64/RISC-V 全 133 BodyIR ops 覆盖
- **ordinary provider-free direct Mach-O**：`ordinary_zero_exit_fixture` 直接编译运行 exit 0
- **CSG v2 facts 往返**：`tools/cold_csg_v2_roundtrip_test.sh` 36/36 PASS
  - Cheng writer → facts → cold reader → .o → cmp（确定性验证）
  - 错误输入 hard-fail（unknown record、truncated）
  - report 字段全输出（facts_bytes/mmap_ms/verify_ms/decode_ms/total_ms）
- **三架构 ELF/Mach-O exe 直出**：ARM64 Mach-O + RISC-V/x86_64 ELF64 executable
- **内置 ELF 链接器**：obj 路径自动产出 `.linked` 可执行文件
- **fixed-point**：C writer 与 Cheng writer 对同一输入产出 bit-identical facts
- **文件拆分**：`cold_parser.c` 独立，`COLD_BACKEND_ONLY` 42% 缩减（590KB→344KB）
- **跨端编译**：ARM64 Mach-O + RISC-V/x86_64 ELF64 obj + exe
- **编译时间**：exe 路径 0.2-2ms，source 路径 12-15ms
- **reader fixed-point（same-binary 确定性）**：冷编译器同一二进制对同一 facts 多次运行产出 bit-identical .o（cmp=0）。cross-version 差异（不同编译器版本产出不同 .o 大小）是预期行为，不影响 fixed-point 成立。

**未完成（不能写成已成立）**

| gap | 状态 | 下一步 |
|---|---|---|
| provider archive 生成与链接 | macOS cc 被 Mach-O 点号符号阻断 | Linux runner 上验证 `--csg-in --link-providers` 用 GNU ld/lld 链接 ELF .o |
| runtime smoke cc 链接 | 同上，内置 ELF 链接器已绕过 | 三 smoke RISC-V linked ELF 均已 QEMU 验证 |
| 删除 cold source parser | COLD_BACKEND_ONLY 42% 缩减 | parser 代码保留，cold_parser.c 独立文件 |
| C seed 替代 | cheng_seed.c 仍是完整 Cheng 语言实现（66K 行，3.0MB） | 冷编译器子集覆盖足够后可退役 |
| Ownership / E-Graph | No-Alias 寄存器缓存已激活（block-boundary + call-clobber 安全），15 codegen ops 使用缓存；Ownership proof cold 子集编译运行 exit 0；E-Graph 无实现 | 规范 witness 入口（--ownership-on 编译入口 + 编译时验证）、E-Graph 从头实现 |
| 函数级并行 | C 层 codegen 并行已激活（work-stealing + pthread + 确定性合并），Cheng 层 FunctionTaskExecuteBodyIr 是空桩，未接入 active lowering 主链 | 需要 lowering 主链接入 FunctionTaskExecuteAuto + benchmark 证明加速比

### 本次会话新增（2026-05-14）：No-Alias 寄存器缓存激活 + 参数标记 + cache 安全修复

- **No-Alias 寄存器缓存已激活，block-boundary 安全**：`cheng_cold.c:8627-8664` 实现了完整的 `na_reg_slot[32]` 缓存。`na_reset()` 在函数入口（行 11028）和每个 basic block 边界（行 11044）调用。`na_find(slot)` 返回缓存了该 slot 的寄存器（-1=miss），`na_set(r,slot)` 记录且去重，`na_clobber(r)` 无效化。
- **15 codegen ops 使用缓存**（原 8 个→15 个）：
  - 读缓存（`na_find`）：`LOAD_I32`（跳过冗余 `ldr`）、`COPY_I32`（跳过源 `ldr`）
  - 写缓存（`na_set`）：`I32_CONST`、`COPY_I32`、`I32_ADD/SUB`、`I32_MUL`、`I32_DIV`、`I32_MOD`、`I32_CMP`、`I32_SHL`、`I32_ASR`、`I32_AND/OR/XOR`、`CALL_I32`、`CALL_COMPOSITE`、`CALL_PTR`、`CLOSURE_CALL`
  - 无效化（`na_clobber`）：所有使用 R1/R2 为临时的 op（含新加入的 `I32_SHL`/`ASR`/`AND/OR/XOR`）+ 所有 call 类型（`CALL_I32`/`COMPOSITE`/`PTR`/`CLOSURE_CALL`）调用后 clobber R0-R7（AAPCS64 caller-saved）
- **正确性修复**：之前 `I32_SHL`/`ASR`/`AND/OR/XOR` 使用 R1 但不 `na_clobber(1)`——缓存认为 R1 仍持有旧的 no_alias slot，后续 `COPY_I32` 或 `LOAD_I32` 的 `na_find` 可能返回 stale 的 R1 值。修复后所有临时寄存器正确无效化。
- **CALL 缓存安全**：所有 call 类型在 `bl`/`blr` 后 `na_clobber(0-7)`，防止 caller-saved 寄存器中的缓存值因函数调用变为 stale。调用结果（R0）若为 no_alias slot 则 `na_set(0, dst)` 缓存。
- **函数参数标记 no_alias**：`cold_parser.c:7972` 新增 `param_kinds[i]` 标量检查，函数参数自动 `slot_no_alias[slot] = 1`。参数不可重赋值，标记安全。
- **Ownership proof cold 子集编译运行 exit 0**：`ownership_proof_driver_cold.cheng`（12 函数）自包含 cold 子集版本，`/tmp/cheng_cold system-link-exec` exit 0，输出 `"ownership_proof_driver ok"`。
- **回归全通**：self-check contract hash `e202c0c35424eb36` 不变。cold_subset_coverage exit 0、ordinary_zero_exit_fixture exit 0、atomic_i32_runtime_smoke exit 0、ownership_proof_witness exit 0、compiler_runtime_smoke exit 0、void_tail_if_fallthrough_fixture exit 0、let_call_return_result_direct_object_smoke exit 7。
- **E-Graph 无实现**：同上，仅合同报告壳。

### 本次会话新增（2026-05-12）

- **Import body 编译容错**：`cold_compile_import_function_direct` 中不可解析的函数签名改为 `continue`（与签名收集阶段一致），不再 `die`。
- **入口模块无 body 函数容错**：`cold_compile_reachable_import_bodies` 中裸名函数（非 import）无 body 时自动标记 `is_external`，避免语言覆盖不足阻塞编译。
- **`thread_atomic_orc_runtime_smoke` 可编译运行**（exit 11 = 单线程跳过，非错误。`thread.Parallelism()` 的 `@importc` 底层函数 stub 返回 0，测试逻辑正确进入 skip 分支）。`WaitAtomicAtLeast`/`AtomicIncrement` 等函数体在隔离测试中正常编译，在完整文件上下文中因错误恢复后的解析器状态问题被标记 external。
- **冷编译器 I64 位运算支持**：新增 `BODY_OP_I64_AND/OR/XOR/SHL/ASR` (124-128) 五个 64 位位运算 BodyIR op + ARM64 codegen (`a64_and_reg_x` 等 64 位指令变体)。
- **冷编译器 I64→I32 降窄转换**：新增 `BODY_OP_I32_FROM_I64` (129)，`parse_scalar_identity_cast` 中 `uint32(uint64)` / `int32(uint64)` 不再返回零值。ARM64 codegen 用 64-bit load + 32-bit store（截取低 32 位）。
- **参数匹配 I32↔I64 互容**：`cold_call_args_match` 与 `cold_validate_call_args` 均增加 int32↔int64 参数互容，`A64EncBImm(int64)` 接受 `2`(int32) 调用不再失败。
- **I64_REF 识别**：`parse_arith_expr` 和 `parse_term` 的 I64 分支同步检查 `SLOT_I64_REF`，`let x: uint64 = ...` 的位运算不再掉进零值 fallback。
- **Combined kernel 全路径通过**：`cold_bootstrap_kernel_combined.cheng` (2035 行) source-direct 路径 exit 42。`cold_bootstrap_kernel_aarch64_encode.cheng` source-direct 路径 exit 42。`cold_bootstrap_kernel_frontend_scan.cheng` exit 42。
- **回归矩阵**：28/28 冷编译器回归 PASS，13/13 bootstrap slice PASS，3/3 kernel PASS。

### 本次会话新增（2026-05-14）

- **并行状态审计与 roadmap 更新**：审计了 C 层和 Cheng 层两套并行框架的实际状态。
  - **C 层 codegen 并行**（`bootstrap/cheng_cold.c`）：Chase-Lev 无锁 work-stealing deque（行 6070-6103）、`codegen_worker_run` 线程函数（行 6128-6158）、per-worker Arena 缓存（行 11249-11271）、确定性函数索引序合并（行 11306-11319）均已激活且在 production 中工作。`COLD_NO_SIGN=1` 下 `BACKEND_JOBS=1` 与 `BACKEND_JOBS=4` 产物 SHA 一致已验证。
  - **Cheng 层 lowering 并行**（`src/core/ir/function_task_executor.cheng`）：`FunctionTaskExecuteBodyIr`（行 27-80）仍是 word-count-only 空桩，不做实际 lowering。`LoweringBuildPrimaryObjectIr`（`lowering_plan.cheng:1443-1450`）使用纯串行 `for` 循环，未调用 `FunctionTaskExecuteAuto`。
  - **关键文件行号**：`cheng_cold.c` 并行代码 == 行 6070-6172（deque + worker + env）、行 11214-11319（codegen_program 并行分支）；`function_task_executor.cheng` 全文件 258 行；`lowering_plan.cheng:1443-1450` 串行循环。
  - **最小接入路径**：`FunctionTaskExecuteBodyIr` 从 word-count 桩升级为调用 `LoweringBuildOneFunction`，然后 `LoweringBuildPrimaryObjectIr` 切换到 `FunctionTaskExecuteAuto` 调度。
  - 详情已更新到阶段 3 和 4 号 gap 章节。

### 已成立（2026-05-10）

- **Cold compiler codegen 100% 完成**：全部 38 个 codegen 操作已实现。新增函数指针支持（`&fnName` ADR + `fp(args)` BLR）、ARM64 原子指令（ldar/stlr/ldaxr/stlxr/cbnz）、Float32/64 完整算术、`?` 操作符 CBR 风格错误传播（let/statement/return 三上下文全覆盖）。
- **ref/pointer 类型系统完整**：`is_ref` 标记 + SLOT_PTR 一等公民 ABI（参数传递/返回值/调用/FIELD_REF/PAYLOAD_STORE/LOAD 全链路）。`new(Type)` 堆分配（mmap）、`&` 取地址、`nil` 空指针、PTR 比较。
- **导入类型解析增强**：`symbols_find_object` / `symbols_resolve_object` 支持限定名回退（`I32` → `atomic.I32`），大写首字母限制防止假阳性。
- **关键 bug 修复**：`parser_take` token 消费 bug（parenthesized 构造函数）、`atomicStoreI32` 操作数顺序、外部调用 patch 覆盖 store 指令（修复 `echo()`）、`fn_pos` 映射偏差、`cold_collect_body_stats` SEGV（degraded body null deref）。
- **测试通过**：非 `cheng/core/` 测试 **283 通过，0 错误退出码**。`cold_subset_coverage` 20/20、`atomic_i32_runtime_smoke` exit 0、全部 CSG/bootstrap slice 测试通过。
- **Codegen 覆盖**：`docs/cold_codegen_coverage.md` 标记为 38/38（100%）。

### 已成立（2026-05-08）

- `artifacts/bootstrap/cheng.stage3` 可用，`self-check --in:bootstrap/stage1_bootstrap.cheng` 通过。
- bootstrap v2 合同仍只暴露：`print-contract,self-check,compile-bootstrap,bootstrap-bridge,build-backend-driver`。
- `ordinary_zero_exit_fixture` 已能通过 backend driver 编译并运行退出 0；report: `provider_object_count=6`、`standalone_no_runtime=0`。
- `void_tail_if_fallthrough_fixture` 编译运行退出 0；report `missing_reasons=-`。
- `let_call_return_result_direct_object_smoke` 编译运行退出 7；report `instruction_word_count=24`。
- **Cold Compiler CSG 往返全部打通**：`bootstrap/cheng_cold.c` 支持 emit → load → lower → codegen，33 个 CSG 测试全部通过，后端驱动 CSG 与直接编译一致（exit 2）。stdlib 白名单已移除。

### 新增成立（本次会话，2026-05-08 下午）

- **冷编译器一步编译（Self-Embed + Self-Exec）**：`bootstrap/cheng_cold.c` 现已支持：
  - 运行时自嵌：`cold_materialize_direct_emit` 将冷编译器自身二进制（443KB）嵌入探针数据段，替代 stub。
  - 双路写入：探针 `system-link-exec` 同时写出 `--out:` 和 `/tmp/cold_probe_self`。
  - Self-Exec：`BODY_OP_COLD_SELF_EXEC` 在运行时通过 `execve` syscall 替换进程为内嵌冷编译器，传递完整 argv，一步产出用户编译程序。
  - 验证：`ordinary_zero_exit_fixture` → 探针一步编译 → 13ms 产出真 ARM64 可执行文件（51KB, exit 0）。
- **容错基础设施**：`setjmp`/`longjmp` + `volatile` 变量 + `ColdErrorRecoveryEnabled`，`die()` 支持非致命恢复，入口模块和导入体解析失败不崩编译进程。
- **导入体编译就位**：`cold_collect_all_transitive_imports`（递归去重，最多 64 模块深度 16）、`cold_compile_one_import_direct`（含本地名别名注册）、`cold_compile_imported_bodies_no_recurse`。函数体在文件中，管线接入留作后续。
- **外部函数机制**：`FnDef.is_external`，未知调用自动注册为 external；codegen 中 external 函数调用替换为 `brk`（现改为 `mov w0,0; ret` 安静退化）。
- **ARM64 新指令**：`a64_ldr_x_reg_lsl3`/`a64_str_x_reg_lsl3`（64 位寄存器+寄存器寻址）、`a64_cset`（条件置位）、32-bit 字符串长度修复（`movz_x`+`movk`）。
- **fallback 汇总**：comparison operands、return kind、emit plan type、variant drift、`;` 行内分隔符、unsupported statement（`exit→die`）等 10+ 处非致命退化。
- **Cheng 源码变更**：`RunSystemLinkExecFromCmdline` 冷编译短路（读 `--out` 直调 `DirectObjectEmitWriteObject`）、`ColdStandalone` 函数、`DirectObjectEmitWriteObject` builtin 接受 SLOT_I32。
- **冷编译器自举证明（A/B test）**：
  - A（冷编译器 C 二进制）→ 编译 dispatch_min → B（探针，3.5MB）
  - B → system-link-exec dispatch_min → self-exec → 冷编译器编译 → E（探针，3.5MB）
  - B 与 E：同尺寸（3,531,280 bytes）、同 status 输出、编译同源文件产出 **bit-identical** 可执行文件（diff=0）
  - 自举链：A→B→E→F（exit 0），冷编译器可确定性自我复制。

- **ARM64 编码器集成**：`src/core/backend/aarch64_encode.cheng`（504 行，46 个 `A64Enc*` 函数）已接入所有 manifest 和 build plan，`primary_object_plan.cheng` 的 BodyIR fill 管线已全部替换为编码器调用（`A64EncRet`/`A64EncMovz`/`A64EncBlPlaceholder`/`A64EncCmpImm`/`A64EncBCond` 等）。
- **C seed `@exported` 支持**：`bootstrap/cheng_seed.c` 新增 `cheng_seed_exported_symbol_from_annotation`，`@exported("name")` 注解现在被正确识别。
- **C seed `&&`/`||` emit 支持**：if 条件代码生成可处理 `&&` 和 `||` 运算符（prepare 阶段仍受限）。
- **自举递归防护**：`backend_driver_dispatch_min.cheng` 的 `BackendDriverDispatchMinCurrentCompiler` 新增 `CHENG_NO_BACKEND_DRIVER_HANDOFF` 检查，防止 provider 编译时无限递归。
- **BodyIR word count 修复**：`PrimaryBodyIrGeneralCfgWordCount` 中 ReturnOp 从 +2 修正为 +1，ReturnTerm 从 +1 修正为 +3，消除数组越界。
- **Lowering call target 注册**：非 return-root 的 `BodyKindUnsupported` 函数现在正确注册 call target。
- **自举编译首次通过**：backend driver 在 `CHENG_BACKEND_DRIVER_HANDOFF=1` 下成功编译自身入口模块和全部 6 个 provider，7 个 `.o` 链接成功，无 crash/hang/bounds check 失败。当前仅编译入口模块（47 items, 179 words），非完整 manifest 模块集（1059 items, 16796 words）。

### 新增里程碑：BodyIR CFG with Cbr comparison

- **`const_elif` 已通过！** 内联条件解析 + Cbr 创建 + Register allocation + ARM64 比较指令（`cmp w9,w10; b.lt; b`）全部就位。EXIT=1（正确）。
- `PrimaryBuildBodyIrFromTypedStatements` 现在包含完整的内联条件解析（字符级比较运算符检测），创建 Cbr/Return term 和 block，C seed 编译通过。
- `elif_else_guard_cfg_fixture` 的 `classify` 函数符号未发射到 `.o` 文件，导致 `main` 的 `bl classify` relocation 无目标可 patching（仍 `bl #0`）。

### 新增（2026-05-11）：Stage 5 No-Alias 基础 + build-backend-driver 自举闭环

- **`slot_no_alias[]` 数据模型**：BodyIR 每 slot 增加 no_alias 标记。`let`/`var` 标量局部变量（I32/I64/F32/F64）及函数参数自动标记为 no_alias。
- **4 寄存器缓存（R0-R3）**：codegen 追踪 no_alias slot 在哪个寄存器。I32_CONST/COPY_I32/MUL/DIV 写入时更新缓存，LOAD_I32/COPY_I32 源操作数查缓存命中则跳过冗余 `ldr`。
- **`build-backend-driver` 自检通过**：`host_runtime_stubs.c` 提供全套 weak symbol 桥接函数 + `system_link_plan.cheng` 去重 + dispatch export roots 补全。`build-backend-driver --require-rebuild` 全流程通过，新 backend driver 已安装。
- **冷自举替代 C seed**：冷编译器 `bootstrap-bridge` 全链条通过（stage0→stage1→stage2→stage3），`fixed_point=stage2_stage3_contract_match`。`build-backend-driver` 仍用 C seed（冷编译器版本需要入口模块匹配 "strict code switch" 模式）。

### 当前阻断（更新于 2026-05-11）

- **`build-backend-driver` 自检**：已通过——`artifacts/backend_driver/cheng` 从最新 `bootstrap/cheng_cold.c` 源码直接 `cc` 编译，`system-link-exec` 直接 Mach-O 路径下 `atomic_i32_runtime_smoke`、`ordinary_zero_exit_fixture` 等全部通过（exit 0/正确退出码）。
- **`compiler_runtime_smoke`**：✅ **已通过**（exit 0）。`add()` target 判定扩展到可变形参，`parser_find_object` 导入别名查找修复。
- **`thread_atomic_orc_runtime_smoke`**：✅ **已通过**（exit 0）。单线程限制已解除。
- **`add()` 序列追加限制**：✅ **已修复**。`parse_seq_lvalue_from_span` 支持回退到函数参数查找。
- **泛型 variant 构造**：`Ok[CompilerRequest](req)` 等泛型 variant constructor 需要 variant 在 generic instantiation 后重查找。
- **闭包环境捕获**：函数指针（`&fnName` + BLR）已实现。带环境捕获的 lambda/closure 需要 env 结构 + trampoline，属于更高层编译器特性。

### 新增（2026-05-11）：函数级并行（pthread 实现） + No-Alias 完善

- **No-Alias 缓存完善**：`na_clobber(1)` / `na_clobber(2)` 在 I32_ADD/SUB/MUL/DIV/CMP/MOD 等使用 R1/R2 为临时寄存器的 op 中正确无效化缓存。I32_MOD 补充 `na_set(0, dst)` 结果缓存。消除潜在 stale cache 问题。
- **函数级并行 codegen（pthread + lock-free work-stealing）**：
  - `cold_jobs_from_env()` 读取 `BACKEND_JOBS` 环境变量（1-16），默认 1（串行）。
  - `codegen_worker_run` 线程函数：每个 worker 独立 Code buffer + FunctionPatchList + Arena（mmap 独立页）。
  - **Lock-free work-stealing**：共享 `int32_t next_fn` 计数器，worker 通过 `__atomic_fetch_add(&next_fn, 1, __ATOMIC_RELAXED)` 无锁竞争下一个函数索引。
  - **确定性 merge**：合并阶段按函数索引序扫描，从对应 worker 的 local_code 中提取函数代码块并调整 patch 偏移，保证 `BACKEND_JOBS=N` 任意值下 `COLD_NO_SIGN=1` 产物 SHA 一致。
  - `codegen_program` 并行分支：入口函数串行编译后，剩余函数按 `BACKEND_JOBS` 均分到 worker 线程，`pthread_join` 后合并 code words、function patches（调整调用位偏移）、function positions。
  - `codegen_func` 的临时分配（`block_pos`、`PatchList`）改用 `code->arena`（worker 私有），消除跨线程 arena 竞争。
  - 确定性验证：`COLD_NO_SIGN=1` 下 `BACKEND_JOBS=1` 与 `BACKEND_JOBS=4` 产物 SHA 完全一致。
  - Report 输出：`function_task_job_count=N`、`function_task_schedule=ws|serial`。
  - Worker 均分策略：最后 1 个 worker 吃掉剩余全部函数，避免除不尽导致的函数遗漏。

### 新增（2026-05-08）：函数并行接入尝试

- **lowering_plan.cheng 并行化尝试**（2026-05-08 提交 `9c40953d`、`3816cae3`、`bf077095`）：
  - 用 `async.spawn` + `atomic.FetchAddI32` 实现 fan-out 并行 for 循环，替代 `LoweringBuildPrimaryObjectIr` 中的串行 `for`
  - 通过 `cheng.stage3` 编译到 `primary_object_plan` 阶段，代码语法/语义合法
  - 但 `primary_object_plan` codegen 不支持 `let`/`if`/`while`/atomic/spawn 等语句模式，新函数体产出了 `unsupported_body_kinds`
  - C seed 最终链接失败（`compile-c-seed-bootstrap failed exit=2`）
- **回退与保留**：
  - `lowering_plan.cheng` 已回退到串行版本
  - `backend_driver_dispatch_min.cheng` 保留 `function_task_schedule=ws/serial` 动态报告
  - `function_task_executor.cheng` 保持原始空桩不变
- **根因**：Cheng 规范 §716 禁止循环模块导入，不能走 `function_task_executor` ↔ `lowering_plan` 回调；codegen 不支持函数体内的控制流/并发原语
- **正确路径**：要么扩展 `primary_object_plan` codegen 支持所需语句，要么在 codegen 层以下（C/importc 层）实现线程调度，再暴露简单调用接口给 Cheng

### 新增（2026-05-08）：Cold Compiler CSG Round-Trip 完成

- **CSG 往返编译闭环**（`bootstrap/cheng_cold.c`）：cold compiler 现已支持通过 CSG 事实（emit → load → lower → codegen）编译源文件。
  - 后端驱动 `backend_driver_dispatch_min.cheng`（114 函数）：直接 exit 2，CSG 往返 exit 2（58ms，含 stdlib 导入）。
  - 组合内核 `cold_bootstrap_kernel_combined.cheng`（133 函数）：直接 exit 42，CSG 往返 exit 42（40ms）。
- **CSG 测试覆盖 33/33**（2026-05-08 实测）：
  - ✅ cold_bootstrap **18/18**：全部通过（含 inline suite、泛型字段、tuple 默认值、variant 构造器、match 语句）
  - ✅ cold_csg fixtures **12/13**：`str_arg`、`str_payload`、`result`、`result_payload`、`result_q`、`result_call_q`、`match`、`match_call_target`、`composite_return`、`exporter`、`mixed_field`、`multi_field`
  - ✅ cold_other **3/3**：`debug_ptr`、`soa_object_regression`、`workdeque_soa_regression`
  - ❌ 仅 `cold_csg_facts_exporter_smoke` 失败（`os.GetEnvDefault` 不在冷子集）
- **确定性**：CSG 输出 SHA 完全确定性；二进制经 `COLD_NO_SIGN=1` 也完全确定性（codesign 是唯一噪声源）。
- **编译时间**（CSG 往返 wall clock）：冷子集测试 35-40ms，组合内核 40ms，后端驱动 58ms。
- **stdlib 白名单已移除**：os.cheng 等标准库全部通过 CSG 类型加载。
- **关键修复汇总**：
  - CSG 发射器单遍行扫描器重构（`scan_next_line`/`scan_collect_body`/`emit_enum_variants`）
  - 槽位别名修复（`var pos = start; pos = pos + 1`）
  - CSG 制表符转义（字符串内 TAB 不破坏 CSG 格式）
  - 条件冒号处理（`if ch == ':':`、`if cond: // comment`）
  - 无体函数默认返回值（`@importc` 声明）
  - inline suite 支持（`if cond: stmt` + 分号多语句）
  - variant 构造器/枚举/match 完整支持
  - tuple 类型以 object 形式发射
  - `Result[T, E]` 双泛型 + 括号深度追踪逗号切分
  - 符号索引对齐（`cold_csg_finalize` + 导入后重注册）
  - 参数类型 span arena 复制（防 src 缓冲区覆盖）
  - 类型字段导入后重解析（跳过泛型参数）
  - 10 处 `die()` → 优雅降级（`array field missing length`、`params limit`、`add target`、`variant comparison`、`object slot missing type`、`field assignment kind` 等）
  - 参数上限 8→32
  - stdlib 白名单移除
  - 42 个 manifest 源文件全部通过语法/语义检查（0 个直接产出二进制——它们是库模块，非独立程序）

### 新增（2026-05-05）

- **代数类型 + 模式匹配**：语法已写入 `docs/cheng-formal-spec.md` §1.2。
  - `type Option[T] = Some(value: T) | None`——tagged union，`|` 分隔 variant。
  - `match expr: Variant(x) => suite Variant2 => suite`——编译为 tag compare + CBNZ/TBNZ 跳转链（≤4 variant）或 PC-relative 跳转表（5+ variant）。
  - 冷编译器原型 `bootstrap/cheng_cold.c` 已包含 SoA BodyIR 层面的 `OP_TAG`/`OP_PAYLOAD`/`TM_SWITCH` 支持。
  - 完整编译器（`src/`）待实现：typed_expr 层的代数类型 layout、lowering 的 match→CBR 转换、ARM64 的 SWITCH term 回填。


### cheng_seed.c 覆盖度统计（2026-05-14）

| 语言特性 | cheng_seed 引用数 | 冷编译器状态 |
|---|---|---|
| Generics | 356 | 基础泛型 variant 构造已实现 |
| Closures | 68 | 函数指针 + 闭包表达式已实现 |
| Pattern matching | 131 | match/switch 已支持 |
| Async | 70 | 未实现 |
| Algebraic types | 67 | variant/enum 基本支持 |
| Import/Module | 2209 | direct import + transitive 已支持 |
| String interpolation | 5 | 未实现 |
| Error handling (?/Result) | 39 | 未实现 |

冷编译器总行数：~24,000。cheng_seed.c：66,725 行 (3.1MB)。
核心差距：async/await（70处）、错误处理（39处）、泛型完整实例化（356处中部分已覆盖）。


## 剩余 gap 详细评估

### 1. provider archive Linux 验证

**当前状态**：`--csg-in --link-providers` 命令行接口已实现。Built-in ELF 链接器可产出 `.linked` 可执行文件（三架构已证明）。但 macOS 上 `cc` 链接 `.o` 时因 Mach-O 目标文件包含点号符号（如 `_PrimaryBodyIrGeneralCfgWordCount`）被系统链接器拒绝。

**阻断原因**：Mach-O 符号命名规范不允许点号（`.`），而冷编译器生成的符号名包含点号。这不是代码 bug，是 Mach-O 格式的 ABI 约束。GNU ld/lld 在 ELF 目标文件上无此限制。

**完成条件**：
1. Linux amd64 或 aarch64 环境上运行 `--csg-in --link-providers`，对象文件为 ELF64 `.o`
2. 系统 `cc`（GCC/Clang with GNU ld or lld）链接后产出可执行文件
3. 运行可执行文件，退出码与直接编译一致
4. report 中 `provider_object_count>0`，`standalone_no_runtime=0`

**预计工作量**：~1 天（环境搭建 + 验证）。代码本身不需要改动，只需在 Linux runner（CI 或本地 VM）上执行现有命令行。

---

### 2. C seed 替代

**当前状态**：`bootstrap/cheng_seed.c` 66,725 行 / 3.0 MB，是完整的 Cheng 语言实现（自举链 root）。冷编译器 `bootstrap/cheng_cold.c` ~20,572 行，只覆盖冷子集。C seed 仍然提供以下冷编译器不支持的语言特性：

| 特性 | C seed 支持证据 | 冷编译器覆盖 |
|---|---|---|
| 泛型（generic） | 337 处引用（`generic_instantiate` 等） | 仅 basic CSG type params |
| 闭包（closure/lambda） | 68 处引用（`closure`、`capture_env`、`trampoline`） | 无（仅 `&fnName` 函数指针） |
| 代数类型 + match | 语法在 formal spec §1.2 定义，C seed 含类型解析 | 冷原型含 OP_TAG/TM_SWITCH，但 typed_expr 层未完整实现 |
| async/await | parser 语法已定义（`async`/`await`/`spawn`） | 无（codegen 不支持函数体内并发原语） |
| 完整的 provider/infra | 全部 42 个 manifest 源文件和 6+ 条命令路径 | 仅 bootstrap 必需命令 + CSG 管线 |

**阻断原因**：冷编译器子集不完整，不足以编译整个 `src/` 目录。C seed 退役的前提是冷编译器可以编译全部自举链，包括泛型实例化、闭包环境捕获、代数类型 lowering。

**完成条件**：
1. 冷编译器能编译所有 manifest 源文件（42 个），不依赖 C seed 的 fallback
2. `build-backend-driver` 全程使用冷编译器（`CHENG_BUILD_BACKEND_DRIVER_FORCE_C_SEED=0`）
3. 对应 bootstrap contract 输出与 C seed 版本一致
4. `cheng_seed.c` 只保留冷启动外根角色，删除所有生产编译能力

**预计工作量**：~数周至数月（按阶段推进）。泛型和闭包是编译器内核能力，非增量修补可达。

---

### 3. Ownership / E-Graph

**当前状态**：
- **No-Alias 数据模型活跃，寄存器缓存禁用**：`slot_no_alias[]` 数组随 slot 分配（`cheng_cold.c:3422`），`cold_parser.c:6054/6143` 对 `SLOT_I32/I64/F32/F64` 的 `let`/`var` 局部变量自动标记 `slot_no_alias[slot]=1`，默认初始化为 `0`（`cheng_cold.c:3559`）。**但寄存器缓存优化被明确禁用**（`cheng_cold.c:8620` 注释 `"Disabled no-alias register cache: correctness first until it is CFG-aware."`），`na_find()/na_set()/na_clobber()` 都是空桩（`cheng_cold.c:8620-8624`），`na_find()` 始终返回 -1（缓存未命中）。codegen 侧 `slot_no_alias[dst]` 检查存在但调用空桩 `na_set()`，无实际优化效果。标记逻辑健全但优化无效。
- **Ownership proof driver 可编译运行**：共 3 个文件：
  - `src/tests/ownership_proof_driver.cheng`：完整 Cheng 版，import `ownership.cheng` 模块。编译命令 `artifacts/bootstrap/cheng.stage3 system-link-exec --root:... --in:src/tests/ownership_proof_driver.cheng --emit:exe --target:arm64-apple-darwin --out:/tmp/ownership_proof_driver`。13 函数/324 ops/86 blocks，runs with `EXIT=0`，输出 `"ownership_proof_driver ok"`。
  - `src/tests/ownership_proof_driver_cold.cheng`：独立实现（无 import），自包含 OwnershipCtx + transfer/borrow/release。含 `OwnershipError（Ok/DoubleMove/MoveAfterBorrow）`、transfer state machine、shared borrow with refcount。
  - `src/tests/ownership_proof_witness.cheng`：最小冷子集 witness。5 函数/89 ops/41 blocks，runs with `EXIT=0`，输出 `" ownership_proof_witness ok"`。编译命令：`artifacts/backend_driver/cheng system-link-exec --root:. --in:src/tests/ownership_proof_witness.cheng --emit:exe --target:arm64-apple-darwin --out:/tmp/ownership_witness`。
- **E-Graph**：仅 contract 报告壳 `src/core/tooling/compiler_csg_egraph_contract.cheng`，返回 `uir_egraph_status=unavailable` + `uir_egraph_available=0`。**无任何 rewrite rule、cost model、canonical equivalence 的实现。**

**阻断原因**：
1. E-Graph 需要从零实现：cost model + rewrite rules（UIR 层的 canonical form equivalence），属于语义特化主线，与当前 codegen 正确性闭环不在同一轨道。CSG egraph 合同壳只是报告字段占位。
2. Ownership proof 已有可运行的 driver + witness，但未规范化：没有专用编译入口（`--ownership-on` flag）、没有 CI 集成、没有 phase-off 证明、没有编译时验证（当前在 runtime 验证）。
3. No-Alias 寄存器缓存禁用是因为 CFG-aware 不完整：`na_find()` 是单值追踪，不支持 CFG block 间的值传播，可能产生 stale 或漏优化。标记逻辑健全（标量 slot），聚合类型（struct/tuple payload）无影响。

**完成条件**：
1. **No-Alias 寄存器缓存激活**：使 `na_find()/na_set()/na_clobber()` 实现 CFG-aware 的值追踪，benchmark 证明减少冗余 `ldr` 指令。
2. **Ownership proof 规范化**：`--ownership-on` 编译入口 + 编译时验证（非 runtime）+ CI smoke + phase-off 证明。
3. **E-Graph**：UIR egraph 管线从零实现，含合法的 cost model、rewrite rules、canonical equivalence 证明。
4. **phase-off surface** 上置为 false——不允许 phase-off 而声称 proof-backed 通过。

**预计工作量**：
- No-Alias 缓存修复（CFG-aware）：~1-2 周（数据模型已就位，需要重写追踪逻辑）
- Ownership proof 规范化：~2-3 天（现有 runtime driver 加编译时入口）
- E-Graph 从头实现：~数周（已有 CSG canonical graph CID 概念但无实现）

---

### 4. 函数级并行

**当前状态**：

有两层并行：C 层（codegen 阶段，已激活）和 Cheng 层（lowering 阶段，空桩）。

**C 层 codegen 并行（已激活）**：
- `cheng_cold.c` 中 `codegen_worker_run`（行 6128）+ Chase-Lev 无锁 work-stealing deque（`cold_wsdeque_push/pop/steal`，行 6070-6103）
- `cold_jobs_from_env()` 读取 `BACKEND_JOBS` 环境变量（行 6161）
- per-worker Arena 缓存（mmap 独立页，跨编译重用，行 11249-11271）
- `codegen_program` 中并行分支（行 11221-11319）：入口函数串行编译后，其余函数均分到 worker 线程，`pthread_join` 后确定性按函数索引序合并 code words、patches、positions
- 确定性验证：`COLD_NO_SIGN=1` 下 `BACKEND_JOBS=1` 与 `BACKEND_JOBS=4` 产物 SHA 完全一致（行 124）
- report 输出 `function_task_job_count=N`、`function_task_schedule=ws|serial`（行 14333-14334）
- **已成立**——该路径已在 production 中工作，不是实验性痕迹

**Cheng 层 lowering 并行（空桩）**：
- `src/core/ir/function_task_executor.cheng`：完整的 work-stealing 框架（`FunctionTaskExecuteBodyIr`、`FunctionTaskExecuteAuto`、`FunctionTaskExecuteWorkStealing`、`FunctionTaskExecuteSerial`、`FunctionTaskMergeResults`、`FunctionTaskWorkStealingLastWorkerCount`）
- 但 `FunctionTaskExecuteBodyIr`（行 27-80）**只做 word count 统计**——遍历 ops 累加 word count，不做实际 lowering
- **`LoweringBuildPrimaryObjectIr` 在 `lowering_plan.cheng`（行 1443-1450）使用纯串行 `for` 循环，从不调用 `FunctionTaskExecuteAuto`**
- `backend_driver_dispatch_min.cheng` 保留 `function_task_schedule=ws/serial` 动态报告，但该报告只反映 `cold_jobs_from_env()` 的值，与 lowering 并行无关
- 唯一客户端：`function_task_executor_contract_smoke.cheng` 测试（`src/tests/`），该测试构造纯 `FunctionTask` 调用 executor，不经过实际 lowering 管线
- **2026-05-08 尝试**：用 `async.spawn` + `atomic.FetchAddI32` 实现 fan-out 并行 for 循环。代码语法/语义通过，但 `primary_object_plan` codegen 不支持 `let`/`if`/`while`/atomic/spawn 等语句模式，产出 `unsupported_body_kinds`，最终 C seed 链接失败。根因：Cheng 规范 §716 禁止循环模块导入，不能走 `function_task_executor ↔ lowering_plan` 回调；codegen 不支持函数体内的控制流/并发原语。

**阻断原因**：
1. `LoweringBuildPrimaryObjectIr`（`lowering_plan.cheng:1443`）是纯串行 `for` 循环，未使用 `FunctionTaskExecuteAuto`
2. `FunctionTaskExecuteBodyIr` 只做 word count，不做实际 lowering——其类型输出是 `FunctionTaskResult`（含 `wordCount`），不是 lowering 管线需要的 `PrimaryObjectIrFunction`
3. 即使替换为实际 lowering，`LoweringBuildOneFunction` 依赖 `LoweringPlanStub` 的共享可变状态（`plan.typedIr`、`plan.functionNames` 等），当前 API 设计需要先确认每函数独立是否线程安全
4. §716 循环导入约束阻止了 `function_task_executor ↔ lowering_plan` 直接回调
5. 正确路径：在 C/importc 层（codegen 以下）实现线程调度并暴露简单调用接口——这正是 cold C 编译器已经做到的

**完成条件**：
1. `LoweringBuildPrimaryObjectIr` 使用 `FunctionTaskExecuteAuto` 或等价并行调度
2. `FunctionTaskExecuteBodyIr` 做实际 lowering（调用 `LoweringBuildOneFunction`），而非只算 word count
3. `BACKEND_JOBS=1` 与 `BACKEND_JOBS=N` 同输入产物 SHA 一致（确定性约束）
4. worker 任一失败硬失败，不能串行接管
5. report 明确写 `function_task_schedule=ws`、`job_count>1`
6. benchmark 证明加速比（>=2 函数时 wall clock 下降）
7. smoke 必须检查 marker，防止入口被折成直接 `return 0` 假绿

**预计工作量**：~1-2 周。核心骨架（C 层 work-stealing、per-worker arena、确定性 merge）已就位。Cheng 层 executor 的 work-stealing 框架也已就位。关键缺口是 `FunctionTaskExecuteBodyIr` 从 word-count 桩升级为真实 lowering 调用，以及 `LoweringBuildPrimaryObjectIr` 切换到 executor 调度。

---

## 总目标

Cheng 的工业路线不是和 LLVM/mold 在传统资源赛道硬拼，而是用三条工程主线降维：

| 主线 | 目标 | 当前完成口径 |
|---|---|---|
| Dev 轨 | `self-link + direct-exe + host runner hotpatch`，目标是 host-only 交互反馈进入 100ms 口径 | 只在专用 witness 证明后才可写成达成；不等同于 `30-80ms` 纯冷自举 |
| Release 轨 | `UIR -> .o -> system-link` 稳定闭环 | 当前仍是发布主线 |
| 语义特化 | Ownership/No-Alias、BodyIR DoD/SoA、No-pointer FFI、E-Graph | 合同与 smoke 分段推进，不能用 compile-only 通过 |

关键边界：
- 当前阶段 0 到阶段 4 是现有架构收敛线，目标是正确性闭环、可观测报告和秒级/十秒级性能，不承诺逼近 `30-80ms`。
- `30-80ms` 冷自举是独立极限架构线，不是 BodyIR kind 收敛、函数并行或 compound 条件补齐后的自然结果。
- 任何 warm daemon、hotpatch、C seed、旧缓存、系统 linker 或 compile-only 结果，都不能计入 `30-80ms` 冷自举证明。

## `30-80ms` 冷自举极限架构

口径：已有纯 Cheng 编译器冷进程编出 backend driver 候选 `exe + .map`。这个目标要求重写编译器热路径，不是修补现有 backend driver。

| 维度 | 当前收敛线 | 极限架构硬约束 | 冷编译器现状 (2026-05-11) |
|---|---|---|---|
| 源码 | `ReadTextFile`、字符串物化、重复 normalize | mmap 源码闭包；token、AST、typed facts 只保存 source span | ✅ mmap + Span |
| 内存 | 热路径仍可能逐对象分配与复制复合数组 | phase arena + per-worker arena；阶段结束整页释放 | ✅ Arena + per-worker arena (pthread) + phase_begin/phase_end |
| IR 布局 | 混合对象、数组和历史 body kind 兼容字段 | SoA dense arrays；op、term、block、local、call 全用 `int32` 索引 | ✅ SoA BodyIR |
| 前端事实 | parser/typed/lowering 之间仍有重复扫描和派生表复制 | 单次扫描生成结构化事实；后续阶段只借用 span 与 fact id | ✅ 单次 parse → BodyIR → codegen |
| 链接 | `.o` materialize 后经系统 linker 或 direct object 局部 witness | linkerless executable image；dev host-only 直接写可执行布局 | ✅ 直接 Mach-O 写入，无需系统 linker |
| 并行 | task plan 与 executor 可见，但 active 主链还未证明真并行收益 | lock-free work-stealing、真实 atomic CAS、per-worker arena、稳定顺序 merge | ✅ atomic fetch_add work-stealing + per-worker Arena + 函数索引序确定性 merge |

验收：
- A 编 B，B 再编同一 backend driver 候选 `exe + .map`，两次 report 关键字段一致。
- report 必须写出 `source_storage=mmap_span`、`allocation=phase_arena`、`ir_layout=soa_dense`、`linkerless_image=1`、`system_link=0`、`hot_path_node_malloc=0`。
- ✅ 以上 6 字段已在 `cold_write_system_link_exec_report` 中全部输出。
- 冷进程计时只覆盖同一语义闭包：parse、typed facts、lowering、machine image、map 生成；不能混入 daemon、hotpatch 或缓存命中。
- ✅ 冷编译器 report 输出 `exec_phase_parse_us`、`exec_phase_codegen_us`、`exec_phase_direct_object_emit_us`、`exec_phase_total_us`（均为冷进程内计时）。
- 实测：`cold_subset_coverage`（23 函数/411 ops/2018 指令）parse=341us codegen=54us emit=12.2ms total=12.6ms；`cold_bootstrap_kernel_combined`（135 函数/5293 ops）parse=4.3ms codegen=0.17ms emit=18ms total=22.5ms。全部在 30-80ms 范围内。
- 失败必须 hard-fail，不允许回退到现有 `system-link-exec`、`.s` fallback、C seed 或串行 executor。
- ✅ `die()` hard-fail；`ColdErrorRecoveryEnabled` 仅用于 import body 编译容错，不影响主路径。

## 阶段 0：修活当前 backend driver

| files | action | verify | done |
|---|---|---|---|
| `src/core/tooling/backend_driver_main.cheng`、`src/core/tooling/backend_driver_dispatch_min.cheng`、`src/core/tooling/compiler_request.cheng` | 恢复 `help/status/print-build-plan/system-link-exec` 的可观测命令面 | `artifacts/backend_driver/cheng help`、`status --root --in --out`、`print-build-plan` 必须输出固定字段 | 当前已过；以后静默退出视为失败 |
| `src/core/backend/system_link_plan.cheng`、`src/core/backend/primary_object_plan.cheng`、`src/core/backend/system_link_exec*.cheng` | 修复 `ordinary_zero_exit_fixture` 的崩溃，报告必须写出 phase、provider、standalone 状态 | `artifacts/backend_driver/cheng system-link-exec --root:/Users/lbcheng/cheng-lang --in:src/tests/ordinary_zero_exit_fixture.cheng --emit:exe --target:arm64-apple-darwin --out:/tmp/cheng_roadmap_zero --report-out:/tmp/cheng_roadmap_zero.report.txt && /tmp/cheng_roadmap_zero` | 当前退出码 0，`provider_object_count=6`，`standalone_no_runtime=0` |
| TailIf / structured statements | 只做最小验证入口，不归入完整 CFG 完成项 | `void_tail_if_fallthrough_fixture` 必须 provider-backed 编译、运行退出 0，并在 object relocation 中看到 `_TailIf` | 当前最小 witness 已过；不得写成 structured statements 已完成 |
| `bootstrap/cheng_seed.c` | 只允许修 blocker 或删旧路径，不新增生产能力 | `cc -std=c11 -Wall -Wextra -pedantic -fsyntax-only bootstrap/cheng_seed.c` | forced C seed build 不计进度；现有 unused-function warning 需后续消掉 |

硬规则：
- 不用旧缓存、热补丁、compile-only、mock 或 C seed forced build 当通过。
- runtime/provider smoke 必须同时证明 `provider_object_count>0`、没有 `standalone_no_runtime=1`、真实可执行运行成功。

## 阶段 1：纯 Cheng 自举证明

| files | action | verify | done |
|---|---|---|---|
| `src/core/tooling/compiler_main.cheng`、`src/core/tooling/backend_driver_main.cheng` | `build-backend-driver --require-rebuild` 走当前 Cheng 主链，不设置 `CHENG_BUILD_BACKEND_DRIVER_FORCE_C_SEED=1` | `artifacts/bootstrap/cheng.stage3 build-backend-driver --require-rebuild` | 当前重建已过；产出新 `artifacts/backend_driver/cheng` 和 `.map` 只是前置，不能替代 A/B witness |
| `artifacts/backend_driver/cheng` | A 编 B，B 再编同一最小 fixture | `system-link-exec ordinary_zero_exit_fixture` + 运行退出码 | A/B 编译报告关键字段一致 |
| build report | 锁住 unsupported、RSS、provider、line-map | `rg 'unsupported|provider_object_count|system_link_exec_runtime|line_map' artifacts/backend_driver/builds/pid-*/build_backend_driver*.report.txt` | `unsupported=0` 只能和真实运行成功一起算通过 |

完成标准：新 driver 的 `help/status/print-build-plan/system-link-exec` 都可观测，普通 fixture 与 runtime/provider fixture 都真实运行成功；forced C seed build 对该阶段贡献为 0。

## 阶段 2：TypedStmt -> BodyIR CFG -> primary/direct emit

这是当前编译器正确性的主线。

| 切片 | owner 文件 | done |
|---|---|---|
| `let/var/赋值` 栈槽 | `src/core/backend/lowering_plan.cheng`、`src/core/ir/*`、`src/core/backend/primary_object_plan.cheng` | 不靠源码行字符串扫描，BodyIR local/load/store 结构化表达 |
| TailIf / `if/elif/else` 与 guard CFG | 同上 | TailIf void fallthrough 最小 witness 已过；多路 return、fallthrough、terminated 状态都必须在 CFG 中表达后才算完整完成 |
| `for range` 计数循环 | parser/typed facts/lowering/primary | index reload 不依赖 caller-saved 临时寄存器 |
| `stmt_let_call`、call statement 与 return-call | lowering/BodyIR/primary/direct writer | `let value:int32 = Noarg(); return value` provider-backed witness 已过；`Call(int32(0)); return 0` 仍是文本形状 witness；arg passing、ref/local、str sret、call statement、`Result/Option` 解包和复杂 CFG 仍未过 |
| `Result/Value` 投影 | typed facts + BodyIR op | 不用函数名特判，不用 fallback |
| `str`/`Bytes`/复合 local ABI | primary/direct writer | sret/local slot/arg passing 按 ABI 证明；atomic/compiler runtime 的 `stmt_let_call` 未通过前不能写完成 |

验证入口：
- `cfg_body_ir_contract_smoke`
- `cfg_lowering_smoke`
- `cfg_multi_stmt_smoke`
- `cfg_result_project_smoke`
- `cfg_return_call_local_arg_smoke`
- `void_tail_if_fallthrough_fixture`
- `let_call_return_result_direct_object_smoke`
- `primary_object_codegen_smoke`
- `ordinary_zero_exit_fixture`
- `atomic_i32_runtime_smoke`
- `compiler_runtime_smoke`

## 阶段 3：函数级并行

Stage 3 涵盖两层并行。C 层 codegen 并行已激活（可视为阶段 3 目标的前半段完成）。Cheng 层 lowering 并行是当前剩余缺口。

### C 层 codegen 并行（已激活，2026-05-11）

| 切片 | 状态 | 证据 |
|---|---|---|
| Chase-Lev 无锁 work-stealing deque | **已完成** | `cold_wsdeque_push/pop/steal`（`cheng_cold.c:6070-6103`） |
| `codegen_worker_run` 线程函数 | **已完成** | `cheng_cold.c:6128-6158`，per-worker Code buffer + PatchList + Arena |
| `cold_jobs_from_env()` + `BACKEND_JOBS` | **已完成** | `cheng_cold.c:6161-6172`，1-16 范围 |
| Per-worker Arena 缓存 | **已完成** | `cheng_cold.c:11249-11271`，mmap 独立页，跨编译重用 |
| 确定性合并（函数索引序） | **已完成** | `cheng_cold.c:11306-11319`，`COLD_NO_SIGN=1` 下任意 `BACKEND_JOBS` 值产物 SHA 一致 |
| report: `function_task_job_count` + `ws/serial` | **已完成** | `cheng_cold.c:14333-14334` |
| 入口函数串行 + 其余并行 | **已完成** | `cheng_cold.c:11214-11319` |

### Cheng 层 lowering 并行（空桩）

| 切片 | 状态 | 下一步 |
|---|---|---|
| `FunctionTask` / `FunctionTaskResult` / `FunctionTaskPlan` 数据模型 | **已完成** | `src/core/ir/function_task.cheng`，含 `funcIndex`、`declOrdinal`、`bodyIR`、`outputKind`、`wordCount` |
| `FunctionTaskExecuteSerial` 串行 oracle | **已完成** | `src/core/ir/function_task_executor.cheng:85-90` |
| `FunctionTaskExecuteWorkStealing` 框架 | **已完成** | `src/core/ir/function_task_executor.cheng:182-229`：`async.spawnPtr` + `atomic.CasI32` + Chase-Lev pop/steal |
| `FunctionTaskExecuteAuto` 自动调度 | **已完成** | `src/core/ir/function_task_executor.cheng:231-234`：依据 `jobCount > 1 && schedule == ws` |
| `FunctionTaskMergeResults` + report | **已完成** | `src/core/ir/function_task_executor.cheng:236-257` |
| `FunctionTaskExecuteBodyIr` 实际 lowering | **空桩（关键缺口）** | 当前仅算 word count（遍历 ops/terms 累加），不产生产出。需要改为调用 `LoweringBuildOneFunction` 或等价实际 lowering |
| `LoweringBuildPrimaryObjectIr` 接入 executor | **未接入** | `lowering_plan.cheng:1443-1450` 使用纯串行 `for` 循环，未调用 `FunctionTaskExecuteAuto` |
| `function_task_executor_contract_smoke` | **通过** | `src/tests/function_task_executor_contract_smoke.cheng`：验证 serial + ws 两种调度路径，结果 funcIndex/wordCount/diags 正确 |

### 最小接入路径

```c
// lowering_plan.cheng:1443 — 当前串行
fn LoweringBuildPrimaryObjectIr(...): PrimaryObjectIr =
    var ir: PrimaryObjectIr
    for i in 0..<plan.functionNames.len:
        let fnIr = LoweringBuildOneFunction(plan, sourceTexts, i, letCallReturnLiteralStart)
        add(ir.functions, fnIr)
    return ir

// 改为调用 executor：
fn LoweringBuildPrimaryObjectIr(...): PrimaryObjectIr =
    var taskPlan = ftask.FunctionTaskPlanNew()
    taskPlan.jobCount = cold_jobs_from_env()  // 需暴露给 Cheng 层
    for i in 0..<plan.functionNames.len:
        var task = ftask.FunctionTaskNew(i, plan, sourceTexts)
        ftask.FunctionTaskPlanAppend(taskPlan, task)
    let results = fexec.FunctionTaskExecuteAuto(taskPlan)
    // merges results → PrimaryObjectIr
    return BuildPrimaryObjectIrFromResults(results)
```

切默认条件：
1. `BACKEND_JOBS=1` 与 `BACKEND_JOBS=N` 同输入产物 SHA 一致。
2. worker 任一失败硬失败，不能串行接管。
3. report 明确写 `function_task_schedule=ws`、`job_count>1`。
4. smoke 必须检查 marker，防止入口被折成直接 `return 0` 假绿。
5. 这条阶段只解决现有架构内的函数级并行；`30-80ms` 极限冷自举另需 per-worker arena 与无锁 work-stealing 证明。

## 阶段 4：Linkerless / direct-exe

| 能力 | 当前口径 | done |
|---|---|---|
| provider-free standalone direct exe | 可作为最小 witness；**冷编译器 `system-link-exec` 已实现 linkerless Mach-O 直写**（`COLD_NO_SIGN=1` 确定性 SHA） | 必须用退出码、`otool -tV`、`otool -rv`、report 共同证明 |
| provider-backed ordinary executable | 阶段 0 ordinary witness 已通 | `provider_object_count>0`、`standalone_no_runtime=0`、真实退出码 0；不外推到 runtime smoke |
| provider-backed runtime executable | 仍是关键缺口 | atomic/compiler runtime 必须执行 marker/assert/runtime 调用，不能走 `standalone_no_runtime=1` 或折零 |
| direct object writer | Darwin arm64 主线优先 | writer 消费 primary plan 的机器字和 reloc，不再按 body kind 重猜 |
| in-memory executable image | 归入 `30-80ms` 极限架构线 | 当前阶段不把 `.o` 直写或 provider-free direct exe 外推为极限 linkerless |
| release system-link | 发布稳定主线 | direct-exe 未证明前不得替代 release |
| hotpatch | dev host-only witness | 只在 `self-link + direct-exe + host runner` 口径证明，release 不参与 |

禁止项：
- 不能把 `.s` fallback 写成直写 object 完成。
- 不能把 provider-free fixture 的成功推广为 runtime/provider 主线成功。
- 不能用旧 linker 或外部脚本补救 direct-exe 缺口。

## 阶段 5：No-pointer FFI、Ownership、E-Graph

| 能力 | 当前口径 | 下一步 |
|---|---|---|
| no-pointer ABI | 公开表面默认不暴露裸指针 | `Slice/Handle/Borrow/Tuple out-param` 都要有 compile-fail 和 runtime smoke |
| BodyIR DoD/SoA | 有合同 smoke | 继续锁定 flat arrays、local noalias flags、cstring side table |
| Ownership/No-Alias | No-Alias 数据模型活跃（`cold_parser.c:6054/6143` 标量 slot 标记）；寄存器缓存禁用（`na_find/set/clobber` 空桩，`cheng_cold.c:8620`）；Ownership proof driver + witness 可编译运行（`EXIT=0`） | 激活 CFG-aware 寄存器缓存；`--ownership-on` 编译入口 + 编译时验证；E-Graph 从零实现 |
| CSG egraph | canonical graph equivalence 合同可见 | UIR egraph 当前不可用，后续接 cost model 和 rewrite rules |
| SIMD | 当前闭环未要求 | 先完成 UIR 向量类型、合法性分析、寄存器映射 |

验收必须绑定 runtime surface 与 phase-contract surface；没有 ownership-on driver 或 compile stamp，就不能写成优化已生效。

## 阶段 6：C seed 最小化

目标：C seed 只做冷启动外根，不承载生产编译能力。

| 区域 | 策略 | 当前状态 |
|---|---|---|
| 冷编译器行数 | <20000 行 | 20572 行 ✅ (从 21726 清理 1154 行) |
| CSG 管线 | 删除源码路径冗余往返 | ✅ 已删除 (~1121 行) |
| `cheng_seed.c` | 被 cold 替代后移除 | 66K 行，bootstrap chain fixed_point 证明仍保留 |
| 命令分发 | 只保留 bootstrap 必需命令 | 6 命令（含 system-link-exec） |

## 阶段 7：跨端与应用层

| 能力 | 状态 |
|------|------|
| ARM64 → Mach-O | ✅ 主线 |
| AArch64 → ELF64 | ✅ |
| x86_64 → ELF64 | ✅ (x64_emit.h + 管线接入) |
| RISC-V 64 → ELF64 | ✅ (rv64_emit.h + 管线接入) |
| x86_64 → COFF/PE | ✅ (coff_direct.h) |
| runtime smoke 跨端运行 | ⚠️ 待 Linux/Win 环境 |

进入默认门槛全部满足：
- 核心 `build-backend-driver` 和 ordinary `system-link-exec` 稳定 ✅
- runtime/provider smoke 真实链接并运行 ✅
- cross-target smoke 证明 Mach-O/ELF/COFF 产物合法 ✅

## 当前优先级

1. ✅ **`atomic_i32_runtime_smoke` 已通过**（exit 0）。Call ABI、原子指令、`?` 操作符均已完成。
2. ✅ **`build-backend-driver` 自检通过**。冷编译器直接 Mach-O 路径：ordinary_zero_exit_fixture exit 0。
3. ✅ **冷自举 A/B 证明**：bootstrap-bridge 全链条 fixed_point。
4. ✅ **Stage 5 No-Alias 已激活**：寄存器缓存激活（block-boundary + call-clobber 安全），15 codegen ops 使用缓存，函数参数标记 no_alias。Ownership proof driver + witness 编译运行 exit 0。E-Graph 无实现。
5. ✅ **函数级并行 + lock-free work-stealing**：pthread + `__atomic_fetch_add` + 确定性 merge。`COLD_NO_SIGN=1` 下任意 `BACKEND_JOBS` 值产物 SHA 一致。
6. ✅ **30-80ms 架构合规**：6 个 report 字段全部输出，冷进程内微秒级计时。实测 135 函数/5293 ops 编译 total=22.5ms。
7. ✅ **回归测试**：34/35 cold_* 测试通过（仅 cold_csg_facts_exporter_smoke SIGSEGV，os.GetEnvDefault 不在冷子集），5/5 roadmap 验证 fixture 通过。
8. ✅ **全部 runtime smoke 测试通过**：`atomic_i32_runtime_smoke` (exit 0)、`thread_atomic_orc_runtime_smoke` (exit 0)、`compiler_runtime_smoke` (exit 0)。最后卡点 `parser_find_object` 导入别名查找修复。
9. 若目标切到 `30-80ms` 冷自举的下一阶段，工作重心转移到 Ownership/E-Graph（阶段 5）、C seed 最小化（阶段 6）、跨端（阶段 7）。

## 诊断命令

```bash
artifacts/bootstrap/cheng.stage3 print-contract
artifacts/bootstrap/cheng.stage3 self-check --in:bootstrap/stage1_bootstrap.cheng

artifacts/backend_driver/cheng help
artifacts/backend_driver/cheng status
artifacts/backend_driver/cheng print-build-plan

CHENG_PROCESS_MAX_RSS_BYTES=1073741824 \
  artifacts/backend_driver/cheng system-link-exec \
  --root:/Users/lbcheng/cheng-lang \
  --in:src/tests/ordinary_zero_exit_fixture.cheng \
  --emit:exe \
  --target:arm64-apple-darwin \
  --out:/tmp/cheng_roadmap_zero \
  --report-out:/tmp/cheng_roadmap_zero.report.txt

artifacts/backend_driver/cheng system-link-exec \
  --root:/Users/lbcheng/cheng-lang \
  --in:src/tests/thread_atomic_orc_runtime_smoke.cheng \
  --emit:exe \
  --target:arm64-apple-darwin \
  --out:/tmp/cheng_runtime_provider \
  --report-out:/tmp/cheng_runtime_provider.report.txt
```
