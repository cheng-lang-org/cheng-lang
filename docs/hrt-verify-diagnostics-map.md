# HRT 诊断映射表

本表由 `src/tooling/hrt_diag_map.cheng` 生成，错误码与 `asm_emit.c` 的 FNV-1a 规则一致；本文补齐阶段说明与缺失条目。

## 分类与修复建议

| 分类 | 说明 | 修复建议 |
| --- | --- | --- |
| D01 | 语法不允许 | 移除该语法，或改写为 HRT Profile v1 允许形式 |
| D02 | 模式与绑定约束 | 将解构模式改为单一标识符或常量模式 |
| D03 | 调用/参数约束 | 移除具名参数，改为位置参数 |
| D04 | 运算符约束 | 改用允许的运算符或等价写法 |
| D05 | 循环上界约束 | 为 while/for 添加 @bound(N) 或常量范围 |
| D06 | 运行时/外部调用约束 | 移除 runtime/importc 调用或替换为可验证实现 |
| D07 | 类型约束 | 改用固定容量容器或整型类型 |
| D08 | 递归约束 | 改为显式循环并加 @bound |
| D09 | 字符串长度约束 | 避免字符串类型或 len 访问 |
| D10 | 并发与原子约束 | 关闭原子 RC，移除 async/await 与并发类型 |
| D11 | 任务注解约束 | v1 禁用；v2 要求静态任务约束 |

## IR 阶段与诊断来源

| 分类 | 触发阶段 | 说明 |
| --- | --- | --- |
| D01 | `hrt_verify(profile)` | 语法节点级拒绝（AST 节点允许矩阵）；宏/模板声明在 `macro_expand` 阶段也会被拒绝 |
| D02 | `hrt_verify(pattern)` | pattern 仅允许简单标识符（v1）或标识符/常量（v2） |
| D03 | `hrt_verify(call)` | 禁止具名参数 |
| D04 | `hrt_verify(op)` | 限制前缀/中缀运算符集合 |
| D05 | `hrt_verify(loops)` | `@bound(N)` 与 for-range 常量上界 |
| D06 | `hrt_verify(calls)` | runtime/importc 调用与函数声明限制 |
| D07 | `hrt_verify(types)` | 动态容器/浮点类型拒绝 |
| D08 | `hrt_verify(recursion)` | 调用图递归检测 |
| D09 | `hrt_verify(string.len)` | 禁止 `string.len` |
| D10 | `hrt_verify(config)` | `CHENG_MM_ATOMIC=0` 与 `CHENG_MM=off` 等配置门禁 |
| D11 | `hrt_verify(pragma)` | v1 禁止；v2 要求静态任务约束 |

补充说明：
- `@thread_boundary` 与 `Send/Sync` 约束属于语义/所有权阶段诊断，不产生 `HRT-xxxx` 映射；HRT 验证仅在配置与子集层面兜底。

> 注：`macro_expand` 阶段触发的 HRT 拒绝消息不带 `HRT-xxxx` 前缀，但消息文本与本表一致。

## 诊断映射

| 错误码 | 分类 | 诊断消息 | 适用 |
| --- | --- | --- | --- |
| HRT-a2205edf | D01 | hard realtime asm backend does not allow bracket | v1+v2 |
| HRT-30d1afb3 | D01 | hard realtime asm backend does not allow bracket expression | v1+v2 |
| HRT-0bc7ca6d | D01 | hard realtime asm backend does not allow case | v1 |
| HRT-26839019 | D01 | hard realtime asm backend does not allow case expression | v1+v2 |
| HRT-5fa6b63b | D01 | hard realtime asm backend does not allow comprehension | v1+v2 |
| HRT-c27cc2ab | D01 | hard realtime asm backend does not allow concept declaration | v1+v2 |
| HRT-e808f5e2 | D01 | hard realtime asm backend does not allow curly | v1+v2 |
| HRT-f6b2d424 | D01 | hard realtime asm backend does not allow curly expression | v1+v2 |
| HRT-7af16327 | D01 | hard realtime asm backend does not allow defer | v1+v2 |
| HRT-4b6f44b7 | D01 | hard realtime asm backend does not allow deref | v1+v2 |
| HRT-a65ccf08 | D01 | hard realtime asm backend does not allow do | v1+v2 |
| HRT-d581a85a | D01 | hard realtime asm backend does not allow dynamic literals | v1+v2 |
| HRT-03385144 | D01 | hard realtime asm backend does not allow fast assignment | v1+v2 |
| HRT-7aead1d2 | D01 | hard realtime asm backend does not allow float literal | v1+v2 |
| HRT-8d3b24c0 | D01 | hard realtime asm backend does not allow generic params | v1+v2 |
| HRT-c09abdf2 | D01 | hard realtime asm backend does not allow guard | v1+v2 |
| HRT-abcd7e14 | D01 | hard realtime asm backend does not allow if expression | v1+v2 |
| HRT-de3b570f | D01 | hard realtime asm backend does not allow iterator declaration | v1+v2 |
| HRT-173b5666 | D01 | hard realtime asm backend does not allow lambda | v1+v2 |
| HRT-38411acb | D01 | hard realtime asm backend does not allow macro declaration | v1+v2 |
| HRT-8439e77c | D01 | hard realtime asm backend does not allow of branch | v1 |
| HRT-3d03f9af | D01 | hard realtime asm backend does not allow parse errors | v1+v2 |
| HRT-bde066e6 | D01 | hard realtime asm backend does not allow postfix | v1+v2 |
| HRT-c807619f | D01 | hard realtime asm backend does not allow fn type | v1+v2 |
| HRT-acf16176 | D01 | hard realtime asm backend does not allow range | v1+v2 |
| HRT-0588e1d4 | D01 | hard realtime asm backend does not allow record case | v1+v2 |
| HRT-a7a27a5f | D01 | hard realtime asm backend does not allow seq literal | v1+v2 |
| HRT-baad1e05 | D01 | hard realtime asm backend does not allow set type | v1+v2 |
| HRT-adadd713 | D01 | hard realtime asm backend does not allow string literal | v1+v2 |
| HRT-8130dfa0 | D01 | hard realtime asm backend does not allow table literal | v1+v2 |
| HRT-0fa2c19b | D01 | hard realtime asm backend does not allow template declaration | v1+v2 |
| HRT-bc2c7803 | D01 | hard realtime asm backend does not allow trait declaration | v1+v2 |
| HRT-4d383a0e | D01 | hard realtime asm backend does not allow tuple literal | v1+v2 |
| HRT-1af7fd6f | D01 | hard realtime asm backend does not allow tuple type | v1+v2 |
| HRT-99ee0565 | D01 | hard realtime asm backend does not allow when | v1+v2 |
| HRT-86f16541 | D01 | hard realtime asm backend does not allow when expression | v1+v2 |
| HRT-3f1d08d2 | D01 | hard realtime asm backend does not allow yield | v1+v2 |
| HRT-010105de | D02 | hard realtime asm backend requires simple identifier patterns | v1 |
| HRT-97be015f | D02 | hard realtime asm backend requires simple patterns | v2 |
| HRT-0f4e83e6 | D03 | hard realtime asm backend does not allow named arguments | v1+v2 |
| HRT-b9af64ad | D04 | hard realtime asm backend does not allow this infix operator | v1+v2 |
| HRT-b211dfe9 | D04 | hard realtime asm backend does not allow this prefix operator | v1+v2 |
| HRT-9b250587 | D05 | hard realtime asm backend expects @bound(<const int>) | v1+v2 |
| HRT-64852d67 | D05 | hard realtime asm backend requires bounded for-range or @bound | v1+v2 |
| HRT-5b4d009b | D05 | hard realtime asm backend requires bounded loops (@bound) | v1+v2 |
| HRT-f8a7ef38 | D06 | hard realtime asm backend does not allow importc calls | v1+v2 |
| HRT-a6b96de8 | D06 | hard realtime asm backend does not allow importc functions | v1+v2 |
| HRT-45edd0aa | D06 | hard realtime asm backend does not allow importc main | v1+v2 |
| HRT-24392636 | D06 | hard realtime asm backend does not allow runtime calls | v1+v2 |
| HRT-4909fe76 | D06 | hard realtime asm backend does not allow runtime functions | v1+v2 |
| HRT-9056bd8c | D07 | hard realtime asm backend does not allow dynamic container types | v1+v2 |
| HRT-69ed6c58 | D07 | hard realtime asm backend does not allow float types | v1+v2 |
| HRT-32352233 | D08 | hard realtime asm backend does not allow recursion | v1+v2 |
| HRT-236de9c6 | D08 | hard realtime asm backend failed to allocate recursion state | v1+v2 |
| HRT-cb853245 | D09 | hard realtime asm backend does not allow string.len | v1+v2 |
| HRT-6ad9f4f6 | D10 | hard realtime asm backend does not allow async/await | v1+v2 |
| HRT-9d848ad5 | D10 | hard realtime asm backend does not allow concurrency calls | v1+v2 |
| HRT-f870f24d | D10 | hard realtime asm backend does not allow concurrency types | v1+v2 |
| HRT-6a78896b | D10 | hard realtime asm backend requires CHENG_MM_ATOMIC=0 | v1+v2 |
| HRT-18d634c6 | D10 | hard realtime asm backend requires CHENG_MM=off | v1+v2 |
| HRT-ad279af4 | D11 | hard realtime asm backend does not allow @task annotations | v1 |
| HRT-17a3e5f4 | D11 | hard realtime asm backend expects @period(<const int>) | v2 |
| HRT-2e1893ab | D11 | hard realtime asm backend expects @deadline(<const int>) | v2 |
| HRT-8520a63f | D11 | hard realtime asm backend expects @priority(<const int>) | v2 |
| HRT-d945919d | D11 | hard realtime asm backend requires @period/@deadline/@priority for @task | v2 |
| HRT-e375d215 | D11 | hard realtime asm backend requires @task for @period/@deadline/@priority | v2 |
| HRT-eb7c248f | D11 | hard realtime asm backend requires @task function signature: fn <name>(): int32 | v2 |
| HRT-ff33e2b9 | D11 | hard realtime asm backend requires @task annotations to precede a function | v2 |
