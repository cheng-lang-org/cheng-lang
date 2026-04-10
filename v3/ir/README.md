# v3/ir

这里的职责是把 `HIR -> MIR -> LIR` 收成真数据面。

硬规则：

- `MIR` 必须显式写出 `move / borrow / noalias / escape / layout`
- `LIR` 必须显式写出 ABI、栈槽、调用约定
- 不允许把运行时字符串分发塞回 IR

当前源文件：

- `v3/src/ir/core_types.cheng`：先把 `interned id`、`layout`、`borrow`、`escape`、`ABI/callconv/stack slot` 收成真类型，不再口头约束
