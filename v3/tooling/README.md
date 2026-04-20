# v3/tooling

`v3/tooling` 顶层现在不再保留 shell wrapper。活入口只认 Cheng 二进制。

## 入口

- `artifacts/v3_bootstrap/cheng.stage0`
  用途：唯一外根，只负责把仓库从 `C seed -> stage1 -> stage2 -> stage3` 自举起来。
- `artifacts/v3_bootstrap/cheng.stage3`
  用途：tooling、gate、debug、build、smoke 主入口。
- `artifacts/v3_backend_driver/cheng`
  用途：ordinary compile、world/CSG、r2c focused smoke、自托管 compiler control-plane 主入口。

## 冷启动

```sh
mkdir -p artifacts/v3_bootstrap
cc -std=c11 -O2 -Wall -Wextra -pedantic \
  v3/bootstrap/cheng_v3_seed.c \
  -o artifacts/v3_bootstrap/cheng.stage0

artifacts/v3_bootstrap/cheng.stage0 bootstrap-bridge
artifacts/v3_bootstrap/cheng.stage3 build-backend-driver
```

## 常用命令

`cheng.stage3`：

```sh
artifacts/v3_bootstrap/cheng.stage3 scan-hotpath
artifacts/v3_bootstrap/cheng.stage3 print-bootstrap
artifacts/v3_bootstrap/cheng.stage3 debug-report --in:/abs/path/file.cheng --target:arm64-apple-darwin --emit:obj
artifacts/v3_bootstrap/cheng.stage3 print-symbols --in:/abs/path/file.cheng --target:arm64-apple-darwin --emit:obj
artifacts/v3_bootstrap/cheng.stage3 print-line-map --in:/abs/path/file.cheng --target:arm64-apple-darwin --emit:obj
artifacts/v3_bootstrap/cheng.stage3 print-object --object:/abs/path/file.o
artifacts/v3_bootstrap/cheng.stage3 print-asm --in:/abs/path/file.cheng --target:arm64-apple-darwin --emit:obj
artifacts/v3_bootstrap/cheng.stage3 profile-run --in:/abs/path/file.cheng --target:arm64-apple-darwin --out:/tmp/app
artifacts/v3_bootstrap/cheng.stage3 profile-report --in:/tmp/app.v3.profile.raw.txt --out:/tmp/app.profile.txt
artifacts/v3_bootstrap/cheng.stage3 crash-report --in:/tmp/app.crash.raw.txt --out:/tmp/app.crash.txt
artifacts/v3_bootstrap/cheng.stage3 verify-debug-tools
artifacts/v3_bootstrap/cheng.stage3 verify-debug-runtime
artifacts/v3_bootstrap/cheng.stage3 verify-debug-profile
artifacts/v3_bootstrap/cheng.stage3 verify-orphan-guard
artifacts/v3_bootstrap/cheng.stage3 slice-gate
artifacts/v3_bootstrap/cheng.stage3 build-chain-node
artifacts/v3_bootstrap/cheng.stage3 build-chain-node-linux
artifacts/v3_bootstrap/cheng.stage3 build-rwad-bft-linux
artifacts/v3_bootstrap/cheng.stage3 build-linux-nolibc-exe
artifacts/v3_bootstrap/cheng.stage3 verify-windows-builtin
artifacts/v3_bootstrap/cheng.stage3 verify-riscv64-builtin
artifacts/v3_bootstrap/cheng.stage3 run-cross-target-smokes
artifacts/v3_bootstrap/cheng.stage3 run-host-smokes
artifacts/v3_bootstrap/cheng.stage3 run-stage23-libp2p-smokes
```

`backend_driver/cheng`：

```sh
artifacts/v3_backend_driver/cheng status
artifacts/v3_backend_driver/cheng run-host-smokes perf_memory_contract_smoke
artifacts/v3_backend_driver/cheng run-host-smokes cheng_skill_consistency_smoke
artifacts/v3_backend_driver/cheng print-build-plan --target:arm64-apple-darwin
artifacts/v3_backend_driver/cheng system-link-exec --root:/abs/root/v3 --in:/abs/path/file.cheng --emit:exe --target:arm64-apple-darwin --out:/tmp/app
artifacts/v3_backend_driver/cheng emit-csg --root:/abs/root/v3 --in:/abs/path/file.cheng --out:/tmp/csg
artifacts/v3_backend_driver/cheng migrate-csg --root:/abs/root/v3 --in:/abs/path/file.cheng --legacy-in:/abs/path/legacy.cheng --out:/tmp/migrate
artifacts/v3_backend_driver/cheng verify-world --root:/abs/root/v3 --lock:/abs/path/cheng.lock.toml
artifacts/v3_backend_driver/cheng world-sync --root:/abs/root/v3 --world-head:<cid> --out:/tmp/world
artifacts/v3_backend_driver/cheng prove-equivalence --root:/abs/root/v3 --in:/abs/path/file.cheng --baseline-surface:/abs/path/surface.txt --out:/tmp/equiv
artifacts/v3_backend_driver/cheng prove-migration --root:/abs/root/v3 --in:/abs/path/file.cheng --legacy-in:/abs/path/legacy.cheng --out:/tmp/proof
artifacts/v3_backend_driver/cheng publish-world --root:/abs/root/v3 --in:/abs/path/file.cheng --target:arm64-apple-darwin --channel:stable --out:/tmp/publish
artifacts/v3_backend_driver/cheng r2c-react-v3-codegen-surface-smoke --repo /Users/lbcheng/UniMaker/React.js --out-dir /tmp/r2c-codegen
artifacts/v3_backend_driver/cheng r2c-react-v3-truth-compare-smoke --repo /Users/lbcheng/UniMaker/React.js --out-dir /tmp/r2c-truth
artifacts/v3_backend_driver/cheng r2c-react-v3-compile-smoke --repo /Users/lbcheng/UniMaker/React.js --out-dir /tmp/r2c-compile
artifacts/v3_backend_driver/cheng r2c-react-v3-native-gui-bundle-smoke --repo /Users/lbcheng/UniMaker/React.js --route-state home_default --out-dir /tmp/r2c-bundle
artifacts/v3_backend_driver/cheng r2c-react-v3-fresh-clean-gate --repo /Users/lbcheng/UniMaker/React.js
```

## 当前规则

- `verify-orphan-guard` 现在会直接拒绝 `v3/tooling` 顶层任何 `.sh` 文件。零脚本是结构约束，不是约定。
- `debug-report / print-symbols / print-line-map / print-object / print-asm / profile-run / profile-report / crash-report` 都由 Cheng 本体提供，不再依赖 `lldb / nm / otool / sample`。
- `perf_memory_contract_smoke` 现在是 v3 正式性能/内存门禁；报告默认写到 `artifacts/v3_perf_memory_contract/<label>/perf_memory_contract.report.txt`。
- `perf_memory_contract_smoke` 报告里的 `orc_perf_contract` 记录 ORC runtime retain/release 与 alloc/free/live 闭环，`*_compile_exec_phase_summary` 记录正式 `system-link-exec` 编译报告里的 phase 摘要。
- 当前可以严格当作编译理论下界引用的稳定样本是 `object_native_link_plan_smoke`、`chain_node_smoke`、`content_stub_smoke`、`orc_perf_contract_smoke`；前提仍然是 `planner_total_ms <= compile_elapsed_ms`。
- `cheng_skill_consistency_smoke` 现在是文档与技能镜像的一致性门禁；会核对 formal spec、repo skill、README、`v3/tooling/README`，并在本地存在 `$HOME/.codex/skills/cheng语言/SKILL.md` 时要求它与 repo 镜像完全一致。
- Linux `aarch64` 默认已经能真产 `ELF relocatable object` 和 `nolibc exe`。
- Windows `COFF/PE` 和 `riscv64 ELF` 现在通过 `verify-windows-builtin / verify-riscv64-builtin / run-cross-target-smokes` 统一验收。
- `bootstrap-bridge` 和 `build-backend-driver` 都已经在 Cheng 主链里实现；外层脚本已退役。
