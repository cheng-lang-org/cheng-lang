# Cheng toolchain

This repository hosts the Cheng toolchain with a backend-only bootstrap path.
The toolchain no longer depends on legacy C bootstrap.

## Quick start

```bash
sh src/tooling/tooling_exec.sh bootstrap_pure --fullspec
```

This builds/refreshes backend selfhost `stage2` (`artifacts/backend_selfhost_self_obj/cheng.stage2`),
then runs fullchain obj-only checks.

## Compile a program

```bash
sh src/tooling/tooling_exec.sh chengb examples/hello_puts.cheng --run
```

## ORC closedloop (backend-only)

```bash
MM=orc sh src/tooling/tooling_exec.sh chengc examples/test_orc_closedloop.cheng --name:test_orc_closedloop
MM=orc ./artifacts/chengc/test_orc_closedloop
```

## Pure Cheng bootstrap

```bash
sh src/tooling/tooling_exec.sh bootstrap_pure --fullspec
```

## Notes

- The toolchain uses backend-only obj/exe pipeline.
