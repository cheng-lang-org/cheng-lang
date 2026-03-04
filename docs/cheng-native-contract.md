# Cheng Native Contract（CNCPAR-01）

## 1. 机器可校验契约（冻结）
以下 10 条 marker 是 `build_backend_native_contract` / `verify_backend_native_contract` 的唯一规范输入，必须保持可解析：

```text
native_contract.version=1
native_contract.scheme.id=CNC
native_contract.scheme.name=cheng_native_contract
native_contract.scheme.normative=1
native_contract.enforce.mode=hard_fail
native_contract.pillar.distribution=uir_only
native_contract.pillar.compilation=aot_bb_gas
native_contract.pillar.execution=sfi_nolibc
native_contract.pillar.consensus=deterministic_runtime
native_contract.required_gate.backend.native_contract=1
```

## 2. 生产闭环状态（2026-02-25）
当前状态：已闭环（`status=ok`）。

最近一次验收命令：

```bash
TOOLING=artifacts/tooling_cmd/cheng_tooling
$TOOLING verify_backend_native_contract
$TOOLING verify_backend_native_contract_autosystem
```

最近一次结果：

- `verify_backend_native_contract ok`
- `verify_backend_native_contract_autosystem ok`
- `report=artifacts/backend_native_contract/backend_native_contract.report.txt`
- `snapshot=artifacts/backend_native_contract/backend_native_contract.snapshot.env`
- 关键字段：`status=ok`、`closedloop_gate_ok=1`、`prod_closure_gate_ok=1`、`native_smoke_ok=1`
- autosystem 关键字段：`backend_native_contract_autosystem_status=ok`、`backend_native_contract_autosystem_unset_case_rc=0`、`backend_native_contract_autosystem_force_on_case_rc=0`

## 3. Driver 单入口策略（统一）
为消除历史 `cheng* driver` 分叉，生产入口统一为：

- canonical driver：`artifacts/backend_driver/cheng`

说明：

- 该入口必须是 canonical native driver（`backend_prod_closure` required 链路下不接受 shim）。
- 生产 gate 只认 canonical path，不依赖其它历史可执行。

## 3.1 STAGE1_AUTO_SYSTEM 收口（2026-03-01）

- 规范行为：当 `BACKEND_NATIVE_CONTRACT=1` 时，stage1 前端必须强制关闭自动导入 `std/system`。
- 实现位置：`/Users/lbcheng/cheng-lang/src/stage1/frontend_lib.cheng` 的 `stage1_autoSystemEnabled()`。
- required gate：`backend.native_contract_autosystem`（`verify_backend_native_contract_autosystem`）已接入 `backend_prod_closure`。
- 收敛目标：native-contract 编译稳定性不再依赖脚本层注入 `STAGE1_AUTO_SYSTEM=0`。

## 4. 历史产物治理（通知其它进程）
`artifacts/backend_driver` 下其它 `cheng*`/`rebuild*`/`repro*` 文件属于历史调试产物，不属于生产入口。

其它进程在执行闭环前必须先做两件事：

1. 只使用 canonical driver 触发 gate（命令见第 2 节）。
2. 只读取 report/snapshot 判定状态，不以目录内历史可执行数量作为依据。

可选清理命令（只会清理顶层历史 `cheng*`，自动保留 `cheng`、`cheng.objs`、`cheng.objs.lock`）：

```bash
TOOLING=artifacts/tooling_cmd/cheng_tooling
$TOOLING cleanup-backend-driver-history
```

## 5. 变更同步规则
任何对本文件 marker 的修改，必须同步执行：

```bash
TOOLING=artifacts/tooling_cmd/cheng_tooling
$TOOLING build_backend_native_contract \
  --doc:docs/cheng-native-contract.md \
  --out:src/tooling/backend_native_contract.env

$TOOLING verify_backend_native_contract
$TOOLING verify_backend_native_contract_autosystem
```
