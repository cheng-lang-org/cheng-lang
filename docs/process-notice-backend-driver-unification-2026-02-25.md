# 进程通知：Backend Driver 单例收口

日期：2026-02-25  
状态：生效中（高优先级）

## 目标
- 将 backend driver 统一收口为单一 canonical 路径，避免历史 `cheng*` 变体继续扩散。

## 强制口径
1. canonical driver 固定为 `artifacts/backend_driver/cheng`。
2. 默认不启用历史 stage0 回退链。
3. 仅当显式设置 `TOOLING_STAGE0_ALLOW_LEGACY_FALLBACK=1` 时，允许回退到历史候选做应急排障。

## 工具行为更新
1. `cheng_tooling build-backend-driver` 默认自动清理历史 `cheng*` 变体（`BACKEND_BUILD_DRIVER_AUTO_CLEAN_HISTORY=1`）。
2. build 临时产物（attempt/tmp）迁移至 `chengcache/backend_driver_build_tmp`，不再污染 `artifacts/backend_driver` 顶层。
3. 新增命令：
   - `cheng_tooling cleanup-backend-driver-history --dry-run`
   - `cheng_tooling cleanup-backend-driver-history`

## 值班执行建议
1. 先跑 `cheng_tooling cleanup-backend-driver-history --dry-run` 观察删除计划。
2. 再跑 `cheng_tooling cleanup-backend-driver-history` 执行收敛。
3. 最后确认：`artifacts/backend_driver` 顶层仅保留 `cheng`（以及 `cheng.objs` 目录/锁文件）。
