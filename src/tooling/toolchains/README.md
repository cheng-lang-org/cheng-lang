# Toolchain Templates

This folder provides reusable env templates for SDK/NDK/RTOS toolchains.
Each template is a simple `.env` file you can `source` before running
toolchain-dependent commands manually.

Templates:
- macos_x64.env / macos_a64.env
- linux_x64.env
- windows_x64.env
- android_a64.env
- ios_a64.env
- harmony_a64.env
- rtos_aarch64_elf.env
- rtos_riscv64_elf.env

Manual use:
```bash
set -a
. src/tooling/toolchains/android_a64.env
set +a
sh src/tooling/chengb.sh tests/cheng/backend/fixtures/return_add.cheng --target:aarch64-linux-android
```
