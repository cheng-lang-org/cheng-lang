# Cheng 开发计划（Backend-only）

更新时间：2026-02-07

## 1. 当前基线

- 编译器主入口：`./cheng`（由 `src/backend/tooling/backend_driver.cheng` 构建）。
- 生产链路：backend-only，统一 `obj/exe`，默认 `MM=orc`。
- 自举入口：`src/tooling/bootstrap_pure.sh`（selfhost stage2 + fullchain obj-only）。
- 发布 seed：`artifacts/backend_selfhost_self_obj/cheng.stage2`。

## 2. 已完成

- 移除 legacy C bootstrap 与 legacy runner 依赖路径。
- `chengc.sh` 收敛为 `--backend:obj` 入口，支持并行/增量对象构建。
- `backend_release_manifest.sh`/`backend_prod_closure.sh` 收敛为 stage2 backend driver 口径。
- CI/验证链路默认使用 backend-only obj-only 闭环。

## 3. 近期任务

1. 持续增强跨目标稳定性（darwin/linux/windows/riscv64）。
2. 完善自研链接器与对象写入器的严格确定性门禁。
3. 扩展 FFI/ABI 与并发压力回归覆盖。
4. 收敛并清理历史文档，保持单一路径叙述。

## 4. 验收命令

```sh
sh src/tooling/bootstrap_pure.sh --fullspec
sh src/tooling/verify_backend_ci_obj_only.sh
sh src/tooling/backend_prod_closure.sh --no-publish
```

## 5. 约束

- 不再新增 legacy 兼容分支。
- 生产脚本与门禁仅接受 backend-only 路径。
- 任何新功能需先给出对应的 verify 脚本入口。
