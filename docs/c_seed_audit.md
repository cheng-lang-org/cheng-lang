# C Seed 最小化审计 (2026-05-13)

## 当前 C 编译器

| 文件 | 行数 | 状态 |
|---|---|---|
| `bootstrap/cheng_seed.c` | ~66K | 旧 seed，被 cold 替代，仅保留在 bootstrap 链中作为 stage3 目标 |
| `bootstrap/cheng_cold.c` | ~21K | 新冷编译器，已是旧 seed 的 1/3 |

## 冷编译器命令面

| 命令 | 行数 | 分类 | 可移除？ |
|---|---|---|---|
| `print-contract` | 7 | bootstrap 必需 | 否 |
| `self-check` | 20 | bootstrap 必需 | 否 |
| `compile-bootstrap` | 95 | bootstrap 必需 | 否 |
| `bootstrap-bridge` | 130 | bootstrap 必需 | 否 |
| `build-backend-driver` | 244 | bootstrap 必需 | 否 |
| `system-link-exec` | 252 | 用户面编译 | **是**（目标） |
| ~~`status`~~ | ~~16~~ | ~~调试~~ | ✅ 已移除 |
| ~~`print-build-plan`~~ | ~~32~~ | ~~调试~~ | ✅ 已移除 |
| ~~`unimplemented`~~ | ~~4~~ | ~~占位~~ | ✅ 已移除 |

**bootstrap 命令合计：~496 行，占总代码 2.3%。**
**已移除：74 行（4 函数 + dispatch + help）。**

## system-link-exec 依赖分析

`system-link-exec` 是冷编译器最大的非 bootstrap 命令（252 行）。它调用：
- `cold_compile_source_to_object()` — 源文件→目标文件编译
- `cold_compile_source_path_to_macho()` — 源文件→Mach-O 直接编译

这些函数是冷编译器的核心编译管线，也是 bootstrap 命令的底层依赖。**函数本身不能移除，但命令入口可以。**

### 阻塞移除的原因
1. **回归测试**：全部 28 个测试通过 `system-link-exec` 运行
2. **开发验证**：kernel 测试、手动验证都依赖它
3. **自举证明**：需要展示冷编译器能直接编译 Cheng 源码

### 移除路径
1. 将回归测试改用 `build-backend-driver` + 后端驱动间接编译
2. 或保留为 conditional（`#ifdef COLD_KEEP_SYSTEM_LINK_EXEC`）
3. 等 Cheng 后端驱动能完全替代后删除

## cheng_seed.c 状态

`cheng_seed.c` 在 bootstrap 链中的角色：
```
cc → cheng_cold → (system-link-exec) → backend_driver → (compile-bootstrap) → cheng_seed → cheng.stage3
```

- 由 **Cheng 编译器**（stage2/stage3）编译，不走 C 编译器
- 是完整 Cheng 语言的 seed 实现（66K 行）
- 当前阶段不移除——它是 bootstrap fixed_point 证明的关键环节
- 未来可被冷编译器直接替代（当冷编译器语言覆盖足够时）

## 总结

| 指标 | 当前 | 目标 |
|---|---|---|
| C 编译器数量 | 2（cold + seed） | 1（cold only） |
| 冷编译器命令 | 9→6（本次 -3） | 5（仅 bootstrap） |
| 冷编译器行数 | 21135→21061（本次 -74） | < 20000 |
| system-link-exec | 252 行 | 移除或 conditional |
| cheng_seed.c | 66K 行 | 被 cold 替代后移除 |
