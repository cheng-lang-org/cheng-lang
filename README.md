# Cheng toolchain

This repository hosts the Cheng toolchain with a backend-only bootstrap path.
The toolchain no longer depends on legacy C bootstrap.

## Quick start

```bash
cheng_tooling bootstrap_pure --mode:strict --fullspec
```

This builds/refreshes backend selfhost `stage2` (`artifacts/backend_selfhost_self_obj/cheng.stage2`),
then runs fullchain obj-only checks.

## Compile a program

```bash
cheng_tooling chengc examples/hello_puts.cheng --run
```

## ORC closedloop (backend-only)

```bash
MM=orc cheng_tooling chengc examples/test_orc_closedloop.cheng --name:test_orc_closedloop
MM=orc ./artifacts/chengc/test_orc_closedloop
```

## Pure Cheng bootstrap

```bash
cheng_tooling bootstrap_pure --mode:strict --fullspec
```

## Notes

- The toolchain uses backend-only obj/exe pipeline.
- Emergency-only C-Drop gate: `cheng_tooling verify_backend_cdrop_emergency` (not in default release closure).
