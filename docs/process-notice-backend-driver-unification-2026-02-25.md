# 进程通知：Backend Driver 单例收口

日期：2026-02-25  
状态：生效中（高优先级）

命令前缀约定（文内命令可直接执行）：
- `TOOLING=artifacts/tooling_cmd/cheng_tooling`
- 示例中的 `$TOOLING <subcmd>` 等价于直接调用 canonical tooling binary。

## 目标
- 将 backend driver 统一收口为单一 canonical 路径，避免历史 `cheng*` 变体继续扩散。

## 强制口径
1. canonical driver 固定为 `artifacts/backend_driver/cheng`。
2. 默认不启用历史 stage0 回退链。
3. 历史 stage0 回退链已下线；`build/compile/verify` 不再接受 legacy fallback。
4. `BACKEND_DRIVER` / `CHENG_BACKEND_DRIVER` 覆盖入口已移除（非空即配置错误）。
5. `SELF_OBJ_BOOTSTRAP_STAGE0` / `BACKEND_BUILD_DRIVER_STAGE0` 与 `--stage0` / `--seed*` driver 覆盖语义已移除。

## 工具行为更新
1. `$TOOLING build-backend-driver` 默认自动清理历史 `cheng*` 变体（`BACKEND_BUILD_DRIVER_AUTO_CLEAN_HISTORY=1`）。
2. build 临时产物（attempt/tmp）迁移至 `chengcache/backend_driver_build_tmp`，不再污染 `artifacts/backend_driver` 顶层。
3. 新增命令：
   - `$TOOLING cleanup-backend-driver-history --dry-run`
   - `$TOOLING cleanup-backend-driver-history`

## 值班执行建议
1. 先跑 `$TOOLING cleanup-backend-driver-history --dry-run` 观察删除计划。
2. 再跑 `$TOOLING cleanup-backend-driver-history` 执行收敛。
3. 最后确认：`artifacts/backend_driver` 顶层仅保留 `cheng`（以及 `cheng.objs` 目录/锁文件）。
