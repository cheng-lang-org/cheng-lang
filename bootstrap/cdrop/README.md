# Cheng C-Drop Emergency Seed

> 迁移说明（2026-04-16）：旧 `cheng_tooling verify_backend_cdrop_emergency` 入口已经随
> `src/tooling/cheng_tooling.cheng` 退役。当前这份目录只保留“仓库离线应急种子”角色，
> 真正执行时请显式指向 v3 兼容 delegate，例如
> `CHENG_CDROP_DELEGATE=artifacts/v3_backend_driver/cheng`。

This directory stores an emergency-only C-Drop bootstrap seed.

- `cheng_boot.c`: offline-generated C entry used only for emergency recovery.
- `manifest.toml`: pinned metadata (`source_commit`, `builder_id`, `sha256`, `generated_at`).

## Verification

Run a local emergency probe manually:

```bash
mkdir -p artifacts/cdrop && \
cc -O2 -o artifacts/cdrop/cheng_boot bootstrap/cdrop/cheng_boot.c && \
CHENG_CDROP_DELEGATE=artifacts/v3_backend_driver/cheng artifacts/cdrop/cheng_boot --help
```

The recommended delegate is `artifacts/v3_backend_driver/cheng`; if that binary is
missing, `cheng_boot.c` now falls through to `artifacts/v3_bootstrap/cheng.stage3`
and then `artifacts/v3_bootstrap/cheng.stage0`.

## Policy

- Not part of the default v3 production closure.
- Only for offline recovery when the normal `artifacts/v3_backend_driver/cheng` /
  `artifacts/v3_bootstrap/cheng.stage3` path is unavailable.
