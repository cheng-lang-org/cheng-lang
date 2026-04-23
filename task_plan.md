# 当前任务计划

- 迁移目标：仓库根包固定为 `pkg://cheng`，唯一源码树为 `src`，编译器内核在 `src/core`。
- 已完成：源码、测试、seed、r2c 工具路径和公开环境变量去掉旧版本标识。
- 已完成：`chain_node` 从核心编译门禁移除，保留为应用/领域手动命令。
- 已完成：自托管编译主线补资源硬门禁，避免 backend driver / follow-world 长编译无上限吃内存。
- 已完成：C seed `build-backend-driver` 长路径改成实时进度、默认 8GiB RSS 上限，并刷新出新的 `artifacts/backend_driver/cheng`。
- 已完成：旧根 shared builtin、旧根 tooling、驼峰应用目录收口到当前模块布局。
- 已完成：`run-host-smokes <smoke...>` 的普通 fixture 控制面从 C seed 迁到 backend driver 的纯 Cheng host smoke gate。
- 已完成：world/libp2p 自托管主线 smoke 通过，覆盖 managed dependency mirror、world bundle sync、fresh-node selfhost receipt、multi-hop migration proof。
- 已完成：`world-receipt` 控制面从 C seed 转入 `src/core/tooling/world_receipt_gate.cheng`，backend driver 不再把该命令转发给 `stage3 system-link-exec`；它是 world/proof 命令，不进入核心编译门禁。
- 下一步：继续把 `system-link-exec` 的 object/native-link materializer 从 C seed 拆到小 Cheng 模块；`run-stage23-libp2p-smokes`、tail/domain gates 和大型应用/领域 smoke 保持显式运行，不放回核心门禁。
- 验收口径：源码、路径名、二进制字符串 grep 清零；`bootstrap-bridge`、路径 smoke、共享 builtin smoke、tooling 文档 smoke、应用迁移 smoke 通过。
