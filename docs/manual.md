# Installation

This repository uses a pure C/Cheng toolchain. Bootstrap the direct C backend:

```bash
src/tooling/bootstrap_pure.sh --fullspec
```

# Usage

```bash
sh src/tooling/chengb.sh examples/hello_puts.cheng --run
```

Notes:
- 默认内存模型为 `CHENG_MM=orc`；仅在回退/排障时显式设置 `CHENG_MM=off`（或 `--off`）。
- 跨平台编译到可执行文件请优先参考：`docs/cheng-build-any-platform.md`。

# ORC closedloop (direct C backend)

```bash
CHENG_MM=orc sh src/tooling/chengc.sh examples/test_orc_closedloop.cheng --name:test_orc_closedloop
CHENG_MM=orc ./test_orc_closedloop
```

# Backend Verify

```bash
CHENG_MM=orc CHENG_BACKEND_LINKER=self sh src/tooling/verify_backend_closedloop.sh
```

Linux AArch64 no-libc（独立门禁，不接入默认 `verify.sh`）：

```bash
CHENG_BACKEND_ELF_PROFILE=nolibc sh src/tooling/verify_backend_nolibc_linux_aarch64.sh
```

# Legacy

The legacy IR pipeline has been removed; historical notes may remain in git history.
