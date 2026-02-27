# Cheng C-Drop Emergency Seed

This directory stores an emergency-only C-Drop bootstrap seed.

- `cheng_boot.c`: offline-generated C entry used only for emergency recovery.
- `manifest.toml`: pinned metadata (`source_commit`, `builder_id`, `sha256`, `generated_at`).

## Verification

Run:

```bash
cheng_tooling verify_backend_cdrop_emergency
```

The gate verifies manifest hash, builds the seed with `clang`, probes `--help`, then runs strict selfhost fixed-point.

## Policy

- Not part of default production closure.
- Optional integration toggle: `BACKEND_RUN_CDROP_EMERGENCY=1`.
