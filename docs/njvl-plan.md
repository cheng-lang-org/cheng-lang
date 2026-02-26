## 实施计划：NJVL-lite + SSU/dup（分析级）+ 清零 `uir_core_opt2` 占位实现（默认开启）

### 摘要
本次一次性完成三件事，并按你的选择直接默认开启：
1. 落地 NJVL-lite（`unknown` + `kill-lite`）到 noalias 通路。  
2. 引入 SSU/dup 的分析级实现（不引入运行时 `=dup/=wasMoved`）。  
3. 把 [uir_core_opt2.cheng](/Users/lbcheng/cheng-lang/src/backend/uir/uir_internal/uir_core_opt2.cheng) 中全部占位函数从 `return false` 改为真实实现。

### 当前实现状态（2026-02-25）
1. `verify_backend_opt2_impl_surface` 已作为 `cheng_tooling` 原生命令落地，并接入 `backend_prod_closure` required gate（`backend.opt2_impl_surface`）。  
2. `UIR_SSU=1` 与 `UIR_NOALIAS_NJVL_LITE=1` 默认开启与 noalias 扩展字段（`unknown_slot_clobbers/unknown_global_clobbers/kill_events`）已可观测。  
3. `uir_core_opt2` 全量占位清零仍在推进中；当前 gate 会阻断残留 `return false` 占位实现。  

### 已锁定决策
1. `opt2` 范围：一次性全量实现。  
2. 落地方式：直接默认开启。  
3. SSU/dup：分析级实现（不做 runtime hook）。

### 对外接口/类型/门禁变更
1. 新增环境开关（默认开启）：
`UIR_SSU=1`、`UIR_NOALIAS_NJVL_LITE=1`。  
2. 新增可观测输出：
`ssu_report`（`dup_candidates/move_candidates/use_version_max` 等）。  
3. 扩展 `noalias_report`：
新增 `unknown_slot_clobbers/unknown_global_clobbers/kill_events`。  
4. 新增门禁命令：
`verify_backend_opt2_impl_surface`，并接入 `backend_prod_closure`。

### 实施步骤

#### 1. `uir_core_opt2` 基础能力框架
文件： [uir_core_opt2.cheng](/Users/lbcheng/cheng-lang/src/backend/uir/uir_internal/uir_core_opt2.cheng)  
实现统一的分析/改写 helper（纯函数 + 稳定顺序）：
1. 纯表达式判定与副作用判定。  
2. `use/def` 收集与槽位 liveness（按 block 逆序）。  
3. CFG 辅助：前驱/后继、可达块、块等价比较。  
4. 表达式结构相等与稳定 key（用于 CSE/去重）。  

#### 2. 清零所有占位函数（逐个给出明确语义）
文件： [uir_core_opt2.cheng](/Users/lbcheng/cheng-lang/src/backend/uir/uir_internal/uir_core_opt2.cheng)

实现如下函数，不允许保留 `return false` 占位：
1. `uirCoreOptimizeModuleNoopAssign`：删除 `x=x`、恒等 cast 赋值。  
2. `uirCoreOptimizeModuleDseStores`：块内同地址被覆盖且中间无读/call 时删前 store。  
3. `uirCoreOptimizeModuleForwardSubstitute`：单次前向替换，遇 clobber 停止。  
4. `uirCoreOptimizeModuleFoldPureExprs`：删除无副作用 `msExpr`。  
5. `uirCoreOptimizeModuleConstProp`：块内常量传播（跨 call/store 保守失效）。  
6. `uirCoreOptimizeModuleNormalizeExpr`：交换律表达式规范化（常量右置、稳定顺序）。  
7. `uirCoreOptimizeModuleAlgebraic`：`x+0/x*1/x*0/x|0/x&-1` 等代数化简。  
8. `uirCoreOptimizeModuleCopyProp`：局部复制传播（含 alias 失效规则）。  
9. `uirCoreFuncDceStmts`：基于 liveness 删除 dead `let/var/assign`。  
10. `uirCoreFuncCse`：块内局部 CSE（仅 pure expr）。  
11. `uirCoreFuncSroa`：`meAddr(slot)+load/store` 的局部标量化（保守别名规则）。  
12. `uirCoreFuncLicm`：自然循环内 invariant 语句外提到 preheader。  
13. `uirCoreFuncMergeIdenticalRetBlocks`：相同 ret 值块合并。  
14. `uirCoreFuncMergeEquivalentBlocks`：语句+终结一致块合并。  
15. `uirCoreFuncFoldCbrToRetExpr`：`if c return a else return b` 折叠。  
16. `uirCoreFuncFoldCbrToRetBool`：布尔 cbr-ret 归一。  
17. `uirCoreFuncFoldCbrConstantTarget`：常量条件直接转 `br`。  
18. `uirCoreOptimizeModuleFoldJumpToRet`：`br -> ret` 链折叠。  
19. `uirCoreFuncSimplifyBranches`：`cbr`/`br` 简化与冗余分支移除。  
20. `uirCoreFuncSimplifyCbrZeroCmp`：`cmp x,0` 条件规范化。  
21. `uirCoreFuncThreadCbrToBr`：threading 简化可判定分支。  
22. `uirCoreFuncFoldCbrSameTarget`：true/false 同目标折叠。  
23. `uirCoreFuncNormalizeCbrTruthiness`：条件表达式统一规范。  
24. `uirCoreFuncFoldRetAlias`：ret 前局部 alias 折叠。  
25. `uirCoreFuncThreadBrToCbr`：分支链规整。  
26. `uirCoreFuncFoldEmptyJumpBlocks`：空跳板块折叠。  
27. `uirCoreFuncMergeUncondJumpChain`：无条件跳链压缩。  
28. `uirCoreFuncPruneUnreachableBlocks`：删除不可达块。  
29. `uirCoreOptimizeModuleCfg`：统一驱动 CFG 轮次与收敛。  
30. `uirCoreInlineModuleOnce`：一次内联（仅小函数、无递归、无副作用风险）。  
31. `uirCorePruneUnreachableFuncs`：从入口可达函数裁剪。  
32. `uirCoreOptimizeModule2Once`：稳定顺序执行一轮 opt2。  
33. `uirCoreOptimizeModule2`：迭代到不变或上限。  

#### 3. NJVL-lite：`unknown` + `kill-lite`（noalias）
文件： [uir_noalias_pass.cheng](/Users/lbcheng/cheng-lang/src/backend/uir/uir_noalias_pass.cheng)
1. 调用 clobber 从“全量失效”改为“可解析槽位失效优先、不可解析再全量失效”。  
2. 增加 `unknown_slot_clobbers/unknown_global_clobbers` 计数。  
3. 增加 `kill-lite` 回收：按最后使用点回收 `knownVals/ptrBase` 状态并计 `kill_events`。  
4. 默认启用 `UIR_NOALIAS_NJVL_LITE=1`，可显式关闭回退。

#### 4. SSU/dup（分析级）
新增文件： [uir_core_ssu.cheng](/Users/lbcheng/cheng-lang/src/backend/uir/uir_internal/uir_core_ssu.cheng)  
接入文件： [uir_opt.cheng](/Users/lbcheng/cheng-lang/src/backend/uir/uir_opt.cheng)、[uir_core_opt2.cheng](/Users/lbcheng/cheng-lang/src/backend/uir/uir_internal/uir_core_opt2.cheng)
1. 构建每函数 use-version 与 `maxUseVersion`。  
2. 对 copy 语句（`dest <- local(src)`）产出决策：
`dup_candidate`（后续仍用 src）或 `move_candidate`（src 最后一次使用）。  
3. 在 copy-prop/coalesce 中消费决策：
`move_candidate` 允许破坏性合并，`dup_candidate` 禁止破坏性合并。  
4. 输出 `ssu_report` 并默认 `UIR_SSU=1`。  

#### 5. `uir_opt` 调度整合（默认开启）
文件： [uir_opt.cheng](/Users/lbcheng/cheng-lang/src/backend/uir/uir_opt.cheng)
1. `level>=2` 顺序固定为：`noalias(njvl-lite) -> ssu -> opt2 passes -> cleanup -> egraph`。  
2. 保留紧急回退开关但默认开启。  
3. 增加 profile 标签，确保门禁可观测到 SSU 与 noalias-lite 生效。

#### 6. 门禁与文档
文件：  
`verify_backend_noalias_opt`  
`verify_backend_opt2_impl_surface`（新增）  
`backend_prod_closure`  
[README.md](/Users/lbcheng/cheng-lang/src/tooling/README.md)  
[cheng-backend-arch.md](/Users/lbcheng/cheng-lang/docs/cheng-backend-arch.md)
1. noalias gate 增加新 report 字段断言。  
2. 新 gate 断言 `uir_core_opt2` 不含占位实现（关键函数禁止 `return false`）。  
3. 新 gate 至少跑 6 个 fixture（inline/cse/sroa/licm/cfg/forward_subst）并要求 `UIR_PROFILE` 出现对应 pass 标签。  
4. `backend_prod_closure` 新增 required gate：`backend.opt2_impl_surface`。  
5. 文档更新默认开关、指标与回退方式。

### 测试用例与场景
1. 现有回归：
`verify_backend_opt2`、`verify_backend_opt3`、`verify_backend_noalias_opt`、`verify_backend_closedloop`、`verify_backend_determinism_strict`。  
2. 新增/增强断言：
`verify_backend_opt2_impl_surface` 必须通过。  
3. 关键 fixture 组合（复用现有）：
`return_opt2_inline_dce`、`return_opt2_cse`、`return_opt2_sroa_deref`、`return_opt2_licm_while_cond`、`return_opt2_merge_jumps`、`return_opt2_forward_subst`。  
4. noalias 指标：
`unknown_*` 与 `kill_events` 可观测且为合法非负整数。  
5. 稳定性：
两次构建 determinism 结果一致，禁止引入非确定性遍历。

### 验收标准
1. [uir_core_opt2.cheng](/Users/lbcheng/cheng-lang/src/backend/uir/uir_internal/uir_core_opt2.cheng) 中上述函数全部具备非占位实现。  
2. `UIR_SSU=1`、`UIR_NOALIAS_NJVL_LITE=1` 默认生效，且可通过环境变量关闭。  
3. 新增 gate + 现有主 gate 全绿。  
4. `backend_prod_closure` required 流程无新增 skip/fallback 语义。  

### 假设与默认
1. SSU/dup 本期为分析级，不改 runtime hook。  
2. 语言语义与 ABI 口径不变，仅优化实现增强。  
3. 如遇极端回归，允许用 `UIR_SSU=0` 或 `UIR_NOALIAS_NJVL_LITE=0` 临时止血，但默认仍为开启。
