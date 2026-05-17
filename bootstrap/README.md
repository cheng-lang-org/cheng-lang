# bootstrap

`bootstrap` 是当前主线的最小外根，不再依赖旧 bootstrap 证据链和包装层。

当前 checked-in 外根只做一件事：

- 用一个极小 C seed 编译 `bootstrap/stage1_bootstrap.cheng`
- 先产出 `artifacts/bootstrap/cheng.stage0`
- 再由 `cheng.stage0 -> cheng.stage1 -> cheng.stage2 -> cheng.stage3` 继续重编同一份 bootstrap 子集源码
- 同时写出 `artifacts/bootstrap/bootstrap.env`

当前 live 主线已经切到 `selfhost`：

- 只要仓库里已有 fresh `cheng.stage3`，`bootstrap-bridge` 就直接拿它重编 `stage1 -> stage2 -> stage3`
- `cheng.stage0` 只再承担物化物和冷启动角色，不再是日常自举刷新时的权威真值
- 但只要 `cheng_cold.c` 或 `stage1_bootstrap.cheng` 比 live `stage3/stage0` 新，外层薄壳就必须直接回 cold bootstrap 真重建，不能继续信旧 `stage3`
- 只有完全没有 live Cheng 编译器时，外层薄壳才会回到 cold bootstrap 外根

当前 bootstrap 子集源码不是完整 Cheng 语法，而是当前第一版自举子集：

```text
key = value
```

这份 `stage1_bootstrap.cheng` 是 bootstrap 合同清单，不是 lowering/codegen helper 真源码。
真正的 helper 逻辑仍分别收在 `bootstrap/cheng_cold.c` 和 ordinary `src/**` 里。

规则：

- 空行忽略
- `#` 开头为注释
- 只允许单个 `key = value` 赋值
- 值使用逗号分隔的稳定文本，不做旧 tooling/proof/sidecar 兼容

当前 live 合同字段：

- `syntax`
- `bootstrap_name`
- `bootstrap_entry`
- `compiler_class`
- `target`
- `bootstrap_manifest`
- `supported_commands`

`stage1_bootstrap.cheng` 现在只保留 cold bootstrap 事实，源码路径统一收进 `bootstrap/compiler_bootstrap_manifest.cheng`。
seed cold bootstrap 合同现在声明：

- `print-contract` / `self-check` / `compile-bootstrap` / `bootstrap-bridge` / `build-backend-driver` 直接吃最小 v2 合同
- `system-link-exec` 是 backend driver 与 seed 内部构建恢复命令，不写入 cold bootstrap `supported_commands` 合同
- 由 live 合同编出来的 wrapped stage0/stage1/stage2/stage3 会沿用各自 embedded contract source path，不再硬绑 `stage1_bootstrap.cheng`
- ordinary 入口和 seed C 里的 remote compile / BFT fresh shell 也都已并回同一路径 helper，不再各自手写 `stage1_bootstrap.cheng`

旧 `v1` 合同已不再由 live seed 兼容读取；当前 seed 只认 `bootstrap-v2`。

当前命令：

```sh
artifacts/bootstrap/cheng.stage3 bootstrap-bridge

artifacts/bootstrap/cheng.stage0 self-check --in:bootstrap/stage1_bootstrap.cheng

artifacts/bootstrap/cheng.stage1 print-contract --in:bootstrap/stage1_bootstrap.cheng

artifacts/bootstrap/cheng.stage2 self-check --in:bootstrap/stage1_bootstrap.cheng

artifacts/bootstrap/cheng.stage3 print-contract --in:bootstrap/stage1_bootstrap.cheng
```
