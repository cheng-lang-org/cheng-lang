# v3/bootstrap

`v3/bootstrap` 是 `v3` 自己的最小外根，不再依赖旧 bootstrap 证据链和包装层。

当前 checked-in 外根只做一件事：

- 用一个极小 C seed 编译 `v3/bootstrap/stage1_bootstrap.cheng`
- 先产出 `artifacts/v3_bootstrap/cheng.stage0`
- 再由 `cheng.stage0 -> cheng.stage1 -> cheng.stage2 -> cheng.stage3` 继续重编同一份 bootstrap 子集源码
- 同时写出 `artifacts/v3_bootstrap/bootstrap.env`

当前 bootstrap 子集源码不是完整 Cheng 语法，而是 `v3` 第一个自举子集：

```text
key = value
```

规则：

- 空行忽略
- `#` 开头为注释
- 只允许单个 `key = value` 赋值
- 值使用逗号分隔的稳定文本，不做旧 tooling/proof/sidecar 兼容

当前必须字段：

- `syntax`
- `bootstrap_name`
- `bootstrap_entry`
- `compiler_class`
- `bootstrap_source_kind`
- `target`
- `compiler_parallel_model`
- `program_parallel_model`
- `ir_facts`
- `data_layout`
- `aot`
- `forbidden`
- `supported_commands`
- `user_program_auto_parallel`
- `bagua_bpi`

当前默认契约：

- 编译器默认并行：`two_pass,function_scheduler,thread_local_arena`
- 用户程序默认非隐式并行：`deterministic_single_thread`
- IR 硬事实：`move,borrow,noalias,escape,layout,abi`
- 数据布局默认：`dod,soa,fixed_layout`
- 热路径禁词和自举禁词统一写在 `v3/bootstrap/stage1_bootstrap.cheng`

当前命令：

```sh
v3/tooling/bootstrap_bridge_v3.sh

artifacts/v3_bootstrap/cheng.stage0 self-check --in:v3/bootstrap/stage1_bootstrap.cheng

artifacts/v3_bootstrap/cheng.stage1 print-contract --in:v3/bootstrap/stage1_bootstrap.cheng

artifacts/v3_bootstrap/cheng.stage2 self-check --in:v3/bootstrap/stage1_bootstrap.cheng

artifacts/v3_bootstrap/cheng.stage3 print-contract --in:v3/bootstrap/stage1_bootstrap.cheng
```
