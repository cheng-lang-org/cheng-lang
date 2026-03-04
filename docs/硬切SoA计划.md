# Cheng 全栈硬切 SoA（前后端）一次性交付计划（Host-only 严格口径）

## 摘要
- 目标：把 Cheng 编译链路从 `ref/tree + nil` 主表示硬切到 `SoA + int32 ID` 单表示，覆盖 `stage1` 与 `UIR` 全栈。
- 已锁定决策：
- 范围：前后端全栈硬切。
- 迁移策略：不保留桥层（不做长期/短期双轨运行路径）。
- 指针边界：仅允许 C ABI 影子桥（runtime C 内部）；Cheng 源码层禁止裸指针。
- 性能门禁：迁移阶段 `selfhost-100ms-host` 报告模式，收尾恢复硬门禁。
- 交付形式：单分支一次并入；内部按阶段执行，但每阶段必须可复验并阻断回归。

## 当前基线（已勘察）
- `UIR` 已有 SoA 列定义，但主路径仍大量依赖 `UirCoreExpr/UirCoreStmt/UirCoreBlock ref`。
- `stage1` 当前核心是 `Node = ref`，`parser/semantics/monomorphize/ownership` 深度依赖 `nil/ref`。
- 现有 gate 已含 `verify_backend_uir_soa_surface` 与 `verify_backend_rawptr_surface_forbid`，但尚未做到“主执行路径唯一 SoA”。

## 对外接口与类型变更（必须一次性对齐）

### 1) Stage1 AST 硬切（SoA）
- 文件：`/Users/lbcheng/cheng-lang/src/stage1/ast.cheng`
- 删除 `Node = ref` 作为主表示，改为：
- `AstNodeId = int32`（`-1` 为 invalid）。
- `AstArena` 列式存储：`kind[]/posLine[]/posCol[]/identId[]/strId[]/intVal[]/floatVal[]/firstKid[]/kidLen[]/...`。
- 统一访问 API：
- `astNodeKind(id)`、`astKidCount(id)`、`astKidAt(id, i)`、`astSetKid(id, i, child)`。
- 删除/禁用所有 `void*(node)` 判空语义，统一 `astNodeIdValid(id)`。

### 2) Parser 输出硬切到 NodeId
- 文件：`/Users/lbcheng/cheng-lang/src/stage1/parser.cheng`
- `parse*` 系列返回 `AstNodeId`（不再返回 `Node ref`）。
- outline/deferred body 继续保留，但 body span 绑定到 `AstNodeId`。
- `parseModuleWithDiagnostics/parseModuleOutlineWithDiagnostics/materializeRoutineBodyFromTokens` 全部改为 ID 版本。

### 3) Semantics/Ownership/Monomorphize 全改 ID
- 文件：
- `/Users/lbcheng/cheng-lang/src/stage1/semantics.cheng`
- `/Users/lbcheng/cheng-lang/src/stage1/ownership.cheng`
- `/Users/lbcheng/cheng-lang/src/stage1/monomorphize.cheng`
- `/Users/lbcheng/cheng-lang/src/stage1/type_syntax_lowering.cheng`
- 全部函数签名从 `Node` 改为 `AstNodeId`。
- `ownershipNodes: ptr[]` 等结构改为 `AstNodeId[]` 与 SoA 索引表。
- 移除 stage1 内部 `void*` / `ptr_add` 参与 AST 逻辑。

### 4) UIR 主表示硬切 SoA-ID
- 文件：`/Users/lbcheng/cheng-lang/src/backend/uir/uir_internal/uir_core_types.cheng`
- 删除 `UirCoreExpr/UirCoreStmt/UirCoreBlock ref` 作为运行主表示（保留仅用于兼容文档不可接受，需删除执行路径引用）。
- 统一 ID：
- `UirExprId/UirStmtId/UirBlockId/UirFuncId = int32`。
- `UirCoreFunc` 改为持有 SoA span（expr/stmt/block/edge 区间），不持有 `blocks[] ref`。

### 5) Builder/SSA/Opt/NoAlias/SSU/Select 全改 ID API
- 文件：
- `/Users/lbcheng/cheng-lang/src/backend/uir/uir_internal/uir_core_builder*.cheng`
- `/Users/lbcheng/cheng-lang/src/backend/uir/uir_internal/uir_core_ssa.cheng`
- `/Users/lbcheng/cheng-lang/src/backend/uir/uir_internal/uir_core_opt.cheng`
- `/Users/lbcheng/cheng-lang/src/backend/uir/uir_internal/uir_core_opt2.cheng`
- `/Users/lbcheng/cheng-lang/src/backend/uir/uir_internal/uir_core_ssu.cheng`
- `/Users/lbcheng/cheng-lang/src/backend/uir/uir_noalias_pass.cheng`
- `/Users/lbcheng/cheng-lang/src/backend/machine/select_internal/*.cheng`
- 严禁继续访问 `func.blocks[bi].stmts[si]` 风格；统一 `soa` 列访问与 ID 迭代。
- 遍历顺序固定按升序 ID，保证 determinism。

### 6) 零裸指针边界固定
- Cheng 源码层：禁裸指针。
- C ABI 影子桥：仅保留在 runtime C（例如 `system_helpers.c`）。
- `cmdline` 继续使用 runtime bridge：`__cheng_rt_paramCount/__cheng_rt_paramStr/...`，不回流裸指针到 Cheng 语法层。

## 实施步骤（决策完备）

## P0 基线冻结与阻断开关准备
- 产出快照：
- `stage1_ref_surface.txt`（`Node = ref` 依赖点）
- `uir_ref_surface.txt`（`UirCoreExpr/Stmt/Block ref` 依赖点）
- `rawptr_surface.txt`（Cheng 层裸指针命中）
- 命令快照：`backend_prod_closure --no-publish`、`verify_backend_uir_soa_surface`、`verify_backend_rawptr_surface_forbid`、`verify_backend_selfhost_100ms_host --enforce:0`。
- DoD：快照可复现并用于后续 diff 判定“清零”。

## P1 Stage1 AST SoA 类型层切换
- 在 `ast.cheng` 定义 `AstArena + AstNodeId` 与完整 accessor。
- 替换 `kid/kidCount/addSon/setKid/newNode` 的实现为 ID 版本。
- 删除 `Node ref` 分配/retain/release 路径。
- DoD：`stage1` 可在 AST 层独立编译通过，且 `rg "type\\s+Node\\s*=\\s*ref"` 不再出现在执行路径文件。

## P2 Parser 全量 ID 化
- 全部 `parse*` 返回 `AstNodeId`。
- outline/deferred materialize 逻辑迁移到 ID + token span。
- while/for/if/elif/case 等控制流节点均走 SoA children。
- DoD：parser fixtures 输出 AST 结构与旧语义一致（节点计数、关键 kind、诊断文本一致）。

## P3 Semantics/Ownership/Monomorphize/TypeLowering 全量 ID 化
- 逐文件替换：
- `semantics` 所有 `Node` 参数与局部状态转 `AstNodeId`。
- `ownership` 把节点追踪与 class 标记改为 `AstNodeId`，移除 `ptr[]/void*` 节点索引逻辑。
- `monomorphize/type_syntax_lowering` 全改 ID。
- DoD：`checkSemantics`、ownership、mono 在 core fixtures 行为一致，无 `Node ref/nil` 判空。

## P4 UIR SoA 主表示硬切
- 删除 `UirCoreExpr/UirCoreStmt/UirCoreBlock ref` 主通路构造与消费。
- `uir_core_builder` 直接发射 SoA 列，不落地 ref block/stmts。
- `UirCoreModule` 以 SoA spans + func metadata 为核心。
- DoD：`UIR_PROFILE` 可完整输出，且不再依赖 ref UIR 结构。

## P5 SSA/OPT/SSU/NoAlias 全部改 SoA-ID
- `ssa`：phi/rename/use-def/liveness 改为 ID 计算。
- `opt2/opt3`：表达式等价、CSE、DSE、CFG 简化全部列访问。
- `ssu/noalias`：继续输出 `ssu_report/noalias_report`，字段保持兼容。
- DoD：`verify_backend_opt2_impl_surface`、`verify_backend_noalias_opt` 通过且指标齐全。

## P6 Machine Select 与 Direct-EXE 输入改 SoA
- `select` 与后续发射层从 SoA spans 读函数体，不再依赖 ref block。
- 保持 dev 内存直出（host darwin arm64）与 release system-link 语义不变。
- DoD：dev/release 都能编译运行固定 fixtures。

## P7 Gate 与工具链收口（硬阻断）
- 强化/新增 gate：
- `verify_backend_uir_soa_surface`：新增“执行路径无 ref UIR”断言。
- 新增 `verify_stage1_ast_soa_surface`：断言 `stage1` 无 `Node ref` 执行路径与 `nil/ref` AST 判空。
- `verify_backend_rawptr_surface_forbid`：覆盖 `stage1` 与 `backend/uir` Cheng 层。
- `backend_prod_closure` required 接入以上 gate，禁止 skip/fallback。
- DoD：required 链路出现任何 skip/fallback 直接失败。

## P8 性能门禁恢复与最终收尾
- 迁移阶段：`selfhost-100ms-host` 报告模式（不阻断）。
- 收尾阶段：恢复 `--enforce:1` 硬门禁。
- 验收阈值：`p95 <= 100ms`、`p99 <= 120ms`（host 专用机口径）。
- DoD：功能/确定性/零指针全绿后恢复硬门禁并通过。

## 测试与验收场景（必须全过）

1. 语义一致性
- parser + semantics 回归：控制流、泛型、类型推导、borrow/ownership 诊断文本一致。

2. SoA 表面清零
- `stage1`：无 `Node = ref` 执行路径、无 AST 级 `void*` 判空。
- `UIR`：无 `UirCoreExpr/UirCoreStmt/UirCoreBlock ref` 执行路径依赖。

3. 零裸指针边界
- Cheng 层 `rawptr_surface_forbid` 全绿。
- runtime C 影子桥允许；不得泄漏到 Cheng API。

4. 编译运行闭环
- dev：内存直出可执行产物可运行。
- release：`release-compile` 产物可运行，无 `rc=139`。

5. 决定性
- full rebuild 连续 30 次 hash 一致。

6. 生产闭环
- `backend_prod_closure --no-publish` required 全绿，且无 skip/fallback 文案。

7. 性能
- 迁移阶段：输出 100ms 报告。
- 收尾阶段：恢复硬门禁并达标。

## 文档与规范同步（同批完成）
- 更新：
- `/Users/lbcheng/cheng-lang/docs/cheng-plan-full.md`
- `/Users/lbcheng/cheng-lang/docs/cheng-formal-spec.md`
- `/Users/lbcheng/cheng-lang/docs/UIR.md`
- `/Users/lbcheng/cheng-lang/docs/raw-pointer-safety.md`
- `/Users/lbcheng/.codex/skills/cheng语言/SKILL.md`
- 明确写入：
- 全栈 SoA 单表示。
- C ABI 影子桥边界。
- gate 名称、命令、阻断语义、验收阈值。

## 风险与缓解
- 风险：全栈无桥层改动面极大，单点错误可能阻断重编。
- 缓解：每阶段结束必须先跑最小 required 子集（stage1/uir/rawptr），再进入下一阶段。
- 风险：性能短期波动。
- 缓解：迁移期报告，收尾再硬阻断。
- 风险：隐藏的 ref 依赖漏网。
- 缓解：新增 surface gate + `rg` 白名单强校验。

## 假设与默认
- 平台默认 `Darwin arm64`（host-only）。
- 单一 canonical driver：`/Users/lbcheng/cheng-lang/artifacts/backend_driver/cheng`。
- 非发布链路保持 dev 内存直出语义；发布链路仍走 `release-compile`。
- required gate 默认禁止 skip/fallback。
