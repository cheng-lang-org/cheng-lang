# Cheng toolchain

This repository hosts the Cheng toolchain with a pure C bootstrap path.
The toolchain no longer depends on the legacy compiler; bootstrap runs the
direct C backend end-to-end.

## Quick start

```bash
src/tooling/bootstrap_pure.sh --fullspec
```

This builds/refreshes `stage1_runner`, then runs the fullspec regression plus
determinism check.

## Compile a program

```bash
sh src/tooling/chengb.sh examples/hello_puts.cheng --run
```

## ORC closedloop (direct C backend)

```bash
CHENG_MM=orc sh src/tooling/chengc.sh examples/test_orc_closedloop.cheng --name:test_orc_closedloop
CHENG_MM=orc ./test_orc_closedloop
```

## Pure Cheng bootstrap

```bash
src/tooling/bootstrap_pure.sh --fullspec
```

## Notes

- The toolchain supports both the self-developed obj backend and the direct C backend.
