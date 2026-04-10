# v3/lang

这里只放前端：词法、语法、名字解析、类型解析。

硬规则：

- 不在这里补 runtime 语义
- 不在这里偷偷引入 payload/bridge 入口
- 所有权、借用、布局事实必须下沉到 `v3/ir`

当前源文件：

- `v3/src/lang/intern.cheng`：唯一允许接收源码字符串名字的冷路径入口；输出只给 `V3InternId`
