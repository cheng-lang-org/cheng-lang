# HRT Verify 诊断规范（v1）

本文描述 `hrt_verify` 在 v1 阶段的诊断范围与修复建议，目标是：
- 统一错误口径，便于定位与修复
- 支持 CI 中的机器解析（建议前缀/分类）

> 说明：当前实现使用自然语言报错文本；本规范给出“分类 + 典型消息 + 修复建议”，作为一致性基线。

## 1. 诊断分类

### D01 语法不允许
典型消息：
- `hard realtime asm backend does not allow <kind>`
- `hard realtime asm backend does not allow parse errors`
  - 例如 `macro declaration` / `concept declaration` / `case expression` / `seq literal`

修复建议：
- 移除该语法，或改写为 HRT Profile v1 允许形式

### D02 模式与绑定约束
典型消息：
- `hard realtime asm backend requires simple identifier patterns`

修复建议：
- 将解构模式改为单一标识符绑定

### D03 调用/参数约束
典型消息：
- `hard realtime asm backend does not allow named arguments`

修复建议：
- 移除具名参数，改为位置参数

### D04 运算符约束
典型消息：
- `hard realtime asm backend does not allow this prefix operator`
- `hard realtime asm backend does not allow this infix operator`

修复建议：
- 改用允许的运算符或等价写法

### D05 循环上界约束
典型消息：
- `hard realtime asm backend requires bounded loops (@bound)`
- `hard realtime asm backend requires bounded for-range or @bound`
- `hard realtime asm backend expects @bound(<const int>)`

修复建议：
- 为 `while`/`for` 添加 `@bound(N)`，或使用常量上界范围

### D06 运行时/外部调用约束
典型消息：
- `hard realtime asm backend does not allow runtime calls`
- `hard realtime asm backend does not allow importc calls`

修复建议：
- 移除 runtime/importc 调用；改为纯计算或可验证替代实现

### D07 类型约束
典型消息：
- `hard realtime asm backend does not allow dynamic container types`
- `hard realtime asm backend does not allow float types`

修复建议：
- 改为固定容量容器（如 `seq[T, N]` / `Table[V, N]` / `str[N]`）或整型类型

### D08 递归约束
典型消息：
- `hard realtime asm backend does not allow recursion`

修复建议：
- 改为显式循环 + `@bound`

### D09 字符串长度约束
典型消息：
- `hard realtime asm backend does not allow string.len`

修复建议：
- 避免字符串类型或 `len` 访问

### D10 并发与原子约束
典型消息：
- `hard realtime asm backend requires CHENG_MM_ATOMIC=0`
- `hard realtime asm backend requires CHENG_MM=off`
- `hard realtime asm backend does not allow async/await`
- `hard realtime asm backend does not allow concurrency types`
- `hard realtime asm backend does not allow concurrency calls`

修复建议：
- HRT 模式强制关闭原子 RC 与内存管理；移除 `async/await`

### D11 任务注解约束
典型消息：
- `hard realtime asm backend does not allow @task annotations`
- `hard realtime asm backend requires @period/@deadline/@priority for @task`
- `hard realtime asm backend requires @task for @period/@deadline/@priority`
- `hard realtime asm backend requires @task function signature: fn <name>(): int32`
- `hard realtime asm backend requires @task annotations to precede a function`
- `hard realtime asm backend expects @period(<const int>)`
- `hard realtime asm backend expects @deadline(<const int>)`
- `hard realtime asm backend expects @priority(<const int>)`

修复建议：
- HRT v1 禁用 `@task/@period/@deadline/@priority`，请改为常规函数
- HRT v2 仅允许静态任务：`@task` 必须紧邻顶层 `fn`，且具备 `@period/@deadline/@priority` 与 `fn <name>(): int32` 签名

### D12 约束失败（where）
典型消息：
- `where condition not satisfied: <inst>`

修复建议：
- 检查泛型实参与 `where` 约束是否匹配，必要时调整类型参数或补齐约束实现

## 2. 建议的机器解析格式
建议 CI 在诊断文本前加入一致前缀，便于聚合（可后续实现）：
- `[HRT][D01] ...`
- `[HRT][D05] ...`

## 3. 验证入口
- `bin/cheng --mode:hrt --file:<input.cheng> --out:<output.s>`
- `sh src/tooling/verify_hrt_backend.sh`
