# Cheng 导入/路径设计评审

> 基准：`docs/cheng-formal-spec.md` §1.4/§1.5  
> 对照：`bootstrap/cheng_cold.c` 冷编译器实现、`src/core/tooling/backend_driver_dispatch_min.cheng` 实际导入面  
> 日期：2026-05-07

## 一、规范与实现断层

**规范**描述完整包系统：`cheng-package.toml` → `package_id` → registry/lock → `cid` → `chengcache/packages/<cid>/`，支持 SemVer、IPNS 内容寻址、锁文件校验和。

**当前实现**（冷编译器 + 生产入口）全部走 §1.5 的"源码根回退"：`<workspace>/src/<module>.cheng`。无 registry、无内容寻址。

**进度**：`cheng.lock.toml` 已创建（最小可用版本）。registry/cache 未实现。

**建议**：短期标注"源码根回退是当前唯一生产路径"。中期按最小可用子集推进（本地路径映射 → `cheng-package.toml` 依赖声明 → lock → registry）。

## 二、Manifest 与 import 双源冗余

Bootstrap manifest 显式列出每个源文件：

```
backend_lowering_plan_source = src/core/backend/lowering_plan.cheng
backend_primary_object_plan_source = src/core/backend/primary_object_plan.cheng
...
```

每个 `.cheng` 文件首行就是 `import` 声明，已表达完整依赖图。Manifest 是第二份副本，两处必须手工同步——加/删文件都要改 manifest，漏改导致编译成功但依赖缺失。

**建议**：manifest 只保留 entry point 和元数据（target、build plan），源文件闭包由编译器从 `import` 图自动推导。加 `cheng_tooling verify-manifest` 自动校验一致性。

## 三、别名策略

### 3.1 当前设计：Nim 风格 + 小写 alias

Cheng 采用 **Nim 风格包管理**（不是 Go）：`import path` 将所有导出符号直接可见，模块前缀可选，仅用于消歧义。**可见性使用 Go 风格首字母大写导出**。两个轴完全正交：

| 轴 | 规则 | 影响 |
|----|------|------|
| 可见性 | 首字母大写 = 导出 | 决定**哪些**符号可被 import |
| 访问语法 | 带不带 `as` | 决定**怎么**引用那些符号 |

```cheng
import std/os           // 全导入：os 下所有导出符号直接可见
WriteLine(...)          // ✅ 不需要前缀
os.WriteLine(...)       // ✅ 也可以用前缀

import std/os as os     // 限定导入：os 是命名空间
os.WriteLine(...)       // ✅ 必须带前缀
WriteLine(...)          // ❌ 编译错误
```

两种情况导入的符号集合完全相同——`as` 只决定访问要不要前缀，不改可见性。

### 3.2 隐式默认别名的问题

`import std/os` 不写 `as` 时，编译器自动分配 `alias = path.last`（即 `os`）。用户容易把"没写 as"和"没写前缀"当成两件独立的事，实际是一件——`os.` 就是因为默认 alias 是 `os`。

`import std/result` 后用裸名 `Result.Ok(1)`，前缀丢失。对读者来说，单行代码看不出 `Result` 来自哪个 import。

### 3.3 推荐方案

**去掉隐式默认 alias**。不写 `as` = 全导入（所有导出符号直接可见）。写 `as X` = 限定导入（全走 `X.` 前缀）。

```cheng
import std/os as os                               // 限定导入
import cheng/core/backend/primary_object_plan as pobj   // 限定导入
import std/result                                 // 全导入：Ok, Err 直接可见
from std/result import Result, Ok                 // 中期可选：按需导入
```

**中期可选**：加 `from X import Y` 按需导入，语义明确。

## 四、模块路径 → 文件路径映射

### 4.1 当前硬编码

根目录 `cheng-package.toml`：
```toml
package_id = "pkg://cheng"
module_prefix = "cheng"
```

冷编译器路径解析：
```c
if (memcmp(import_path, "cheng/", 6) == 0) offset = 6;  // strip "cheng/"
// → <workspace>/src/<remaining>.cheng
```

`cheng/` 被硬编码为"指向本仓库 `src/`"。`codex/cheng-package.toml` 的 `module_prefix = "codex"` 说明每个包有自己的前缀，但冷编译器没读取。

### 4.2 当前修复

`std/` 已明确处理（映射到 `src/std/`）。`cheng/` 默认兼容。

### 4.3 推荐

路径解析应读取 `cheng-package.toml` 的 `module_prefix`，去掉硬编码。支持 `<prefix>/<path>` 和 `<other_pkg_id>/<path>` 两种形式并归一化。

## 五、包内相对导入

规范 §1.5 明确禁止 `.` 和 `..` 片段。这是**故意设计选择**，不是缺失功能。

优缺点：
- **优点**：导入路径全局唯一，重构移动文件时无需修改导入行（只要包根不变）
- **缺点**：深层目录结构中导入路径越来越长

建议保持当前设计，不引入相对导入。

## 六、导出/可见性机制

规范 §1.4："首字母大写导出"。当前生产链路中，导出可见性靠 manifest 中的 `tooling_export_visibility_gate_source` gate 额外校验，两个约束可能不同步。

**建议**：编译器在语义分析阶段直接检查首字母大小写决定可见性。gate 只做 snapshot test（实际导出列表 vs 期望列表），不参与编译决策。

## 七、循环导入检测

**状态**：✅ 已实现。`cold_compile_csg_load_imported_types` 有 `active_imports` 栈（`cold_active_imports[16][PATH_MAX]`），入栈/查重/出栈，遇环 `die("import cycle detected")`。

规范 §1.5 要求输出完整链路 `Import cycle detected: A -> ... -> A`。当前只输出当前路径，未输出全链路。可改进。

## 八、其他实现层问题

### 8.1 import 分组语法

规范允许 `import std/[os, strings, result]`。**状态**：✅ 冷编译器 `cold_parse_import_line` 已支持 `/[` 语法（返回第一个路径，后续路径由调用方循环处理）。

### 8.2 `as` 解析

**状态**：✅ 已从反向搜索 `" as "` 改为正向 `memcmp(ptr+pos, "as", 2)` 扫描。

### 8.3 锁文件

**状态**：✅ `cheng.lock.toml` 已创建（metadata + package + checksums）。格式为 `format = "source"`，当前仅本地路径映射。

### 8.4 CSG 字段类型编码

导入类型的 `missingReasons: str[]` 被误写为 kind `i` (int32)。**状态**：✅ 已修复 `cold_write_object_field_spec`（加 `cold_parse_str_seq_type` → `t`）。`coreir.BodyIR[]` 等限定类型序列 → `o:TypeName`。

### 8.5 符号表限定名回退

**状态**：✅ `symbols_find_object` / `symbols_find_type` 已加 `dot` 剥离回退（`pobj.X` → 短名 `X`）。中线推荐正式化（非 hack）。

## 九、优先级

| 优先级 | 项目 | 说明 |
|---|---|---|
| **立即** | Manifest 与 import 图一致性校验 | 双源冗余导致 CSG 类型缺失 |
| **立即** | `cold_write_object_field_spec` 补全 enum 类型 | CSG 往返编译卡在字段类型 |
| **短期** | 去掉隐式默认 alias（不写 as = 全导入） | Nim 风格一致化 |
| **短期** | 编译器直接判断首字母导出 | 去掉 gate 参与编译决策的耦合 |
| **短期** | 循环检测输出全链路 | 规范要求 `A -> ... -> A` |
| **中期** | `from X import Y` 语法 | 按需导入，语义更明确 |
| **中期** | 路径解析读 `module_prefix` | 去掉硬编码 |
| **长期** | 包系统落地（registry / cache） | 规范主体功能 |

---

> **已修复**（冷编译器）：§7 循环检测、§8.1 分组语法、§8.2 `as` 解析、§8.3 锁文件、§8.4 字段 kind、§8.5 限定名回退。
