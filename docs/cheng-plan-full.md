# Cheng 全量计划（Backend-only）

更新时间：2026-02-07

## 目标

- 维持单一 production toolchain：backend-only。
- 以 stage2 backend driver 为 seed 完成可复现自举与发布。
- 持续提升跨平台一致性、确定性与可观测性。

## 计划分层

1. 编译器内核：MIR/isel/obj/linker 正确性与稳定性。
2. 工具链脚本：统一入口、去除历史兼容、减少路径分叉。
3. 验证体系：obj/exe determinism、FFI/ABI、stress、fullchain。
4. 发布体系：seed、manifest、bundle、sign、rollback。

## 统一入口

```sh
./cheng
sh src/tooling/chengc.sh
sh src/tooling/chengb.sh
sh src/tooling/bootstrap_pure.sh --fullspec
```

## 退出标准

- backend_prod_closure 默认口径稳定通过。
- CI obj-only gate 持续通过。
- 文档与脚本不再包含 legacy 依赖链路。
