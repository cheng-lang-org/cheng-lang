# Backend 生产闭环值班手册

更新时间：2026-02-23

## 1. 值班目标
- 在主机环境上可重复执行一次 backend 生产闭环。
- 第一时间定位失败 gate，并给出可回滚、可复现的处置结论。

## 2. 标准收口命令（当前值班口径）
```sh
sh src/tooling/backend_prod_closure.sh --no-publish
```

通过判定：输出包含 `backend_prod_closure ok`。
若命中 `backend.profile_baseline` 漂移，先执行
`sh src/tooling/build_backend_profile_baseline.sh --schema:src/tooling/backend_profile_schema.env --out:src/tooling/backend_profile_baseline.env`
再复测。
若命中 `backend.mem_contract` 漂移，先执行
`sh src/tooling/build_backend_mem_contract.sh --doc:docs/backend-mem-hotpatch-contract.md --out:src/tooling/backend_mem_contract.env`
再复测。

注：`--no-obj/--no-obj-determinism/--no-self-obj-writer` 已废弃并被闭环脚本忽略（新口径不再走 obj/self-obj gate）。
注：生产主链默认不跑 selfhost；仅在排查自举兼容问题时显式加 `--selfhost`。
注：`--no-publish` 默认启用稳定收口参数集（`BACKEND_PROD_NO_PUBLISH_STABLE_PROFILE=1`），仅保留 required 收口链路；如需恢复完整可选 gate，可设 `BACKEND_PROD_NO_PUBLISH_STABLE_PROFILE=0`。
注：可用 `BACKEND_PROD_TARGET=<triple>` 显式覆盖优化/可执行门禁目标；未设置时会忽略外部非 darwin `BACKEND_TARGET` 污染并固定 darwin 默认口径。
注：值班收口统一 driver 口径：固定 `artifacts/backend_driver/cheng`，并保持 `BACKEND_DRIVER_ALLOW_FALLBACK=0`（无自动回退）。
注：`BACKEND_OPT_DRIVER` 覆盖 `determinism/opt/ffi/debug/sanitizer` 子门禁 driver；当前值班建议与主 driver 一致固定 `artifacts/backend_driver/cheng`。
注：`backend.self_linker.elf` / `backend.self_linker.coff` 已是默认 required gate（`BACKEND_RUN_SELF_LINKER_GATES=1`）。
注：`backend.no_obj_artifacts` 已是默认 required gate，闭环产物中不允许残留 `.o/.obj/.objs`。
注：`backend.release_rollback_drill` 已是默认 required gate，会执行 `publish -> rollback -> restore` 演练。

发布演练（含 publish）：
```sh
env BACKEND_DRIVER=artifacts/backend_driver/cheng \
    BACKEND_DRIVER_ALLOW_FALLBACK=0 \
    BACKEND_SELF_LINKER_DRIVER=artifacts/backend_driver/cheng \
    BACKEND_OPT_DRIVER=artifacts/backend_driver/cheng \
    BACKEND_LINKERLESS_DRIVER=artifacts/backend_driver/cheng \
    BACKEND_MULTI_DRIVER=artifacts/backend_driver/cheng \
    BACKEND_STAGE1_SMOKE_DRIVER=artifacts/backend_driver/cheng \
    BACKEND_MM_DRIVER=artifacts/backend_driver/cheng \
    PAR05_DRIVER_REFRESH=0 \
    sh src/tooling/backend_prod_closure.sh \
      --seed:artifacts/backend_seed/cheng.stage2
```

## 3. 关键产物
- `artifacts/backend_prod/release_manifest.json`
- `artifacts/backend_prod/backend_release.tar.gz`
- `artifacts/backend_prod/release_manifest.json.sig`
- `artifacts/backend_prod/backend_release.tar.gz.sig`
- `artifacts/backend_release_rollback_drill/backend_release_rollback_drill.report.txt`

## 4. 常见失败与处理
- `backend.release_system_link` 触发 no-pointer policy：
  - 先确认 `src/tooling/verify_backend_release_c_o3_lto.sh` 内 gate 编译环境显式设置了：
    - `STAGE1_STD_NO_POINTERS=0`
    - `STAGE1_NO_POINTERS_NON_C_ABI=0`
- `backend.release_system_link` 链接器异常（mold/lld 缺失或行为不一致）：
  - 优先重跑：`BACKEND_SYSTEM_LINKER_PRIORITY=mold,lld,default sh src/tooling/verify_backend_release_c_o3_lto.sh`
  - 若需临时回退发布口径：`BACKEND_LINKER=system BACKEND_LD=cc sh src/tooling/verify_backend_release_c_o3_lto.sh`
  - 若需强制开发态回退排障：`chengc --release --linker:self <fixture>.cheng`（仅支持 self-link 目标）。
- `backend.hotpatch*` 失败（新热更链路）：
  - 先看 `artifacts/backend_hotpatch/hotpatch.<target>.report.txt` 的 `append_commit_kind/growth_restart_commit_kind/layout_restart_commit_kind`。
  - 若是布局变化误判：显式检查 `layout_hash_same/layout_hash_changed`，并确认 `BACKEND_HOTPATCH_LAYOUT_HASH_MODE=full_program`、`BACKEND_HOTPATCH_ON_LAYOUT_CHANGE=restart`。
  - 若是池耗尽导致频繁重启：调大 `BACKEND_HOSTRUNNER_POOL_MB`（默认 `512`）。
  - 若需临时绕过 host runner 排障：改用 `chengc --run:file <fixture>.cheng`。
- `backend.noptr_exemption_scope` 报 allowlist 外例外：
  - 将新增脚本加入 `src/tooling/verify_backend_noptr_exemption_scope.sh` 的 `allowlist`。
  - 复验：`sh src/tooling/verify_backend_noptr_exemption_scope.sh`。
- `backend.driver_selfbuild_smoke` 失败：
  - 先执行 `BACKEND_BUILD_DRIVER_FORCE=1 BACKEND_BUILD_DRIVER_NO_RECOVER=1 sh src/tooling/build_backend_driver.sh --name:artifacts/backend_driver/cheng`；
  - 再执行 `sh src/tooling/verify_backend_driver_selfbuild_smoke.sh` 定位 stage0/build/smoke 明细。
- `backend.no_obj_artifacts` 失败：
  - 先查看 `artifacts/backend_no_obj_artifacts/backend_no_obj_artifacts.matches.txt`；
  - 再按路径回溯到对应 gate 脚本补齐 sidecar 清理。

## 5. 值班收尾清单
- 闭环命令结果（成功/失败）已记录到 `docs/cheng-plan-full.md` 3.3/本轮验收。
- 新增 gate 例外已同步 `verify_backend_noptr_exemption_scope.sh` allowlist。
- 失败日志路径和复现命令已在当日记录中给出。
