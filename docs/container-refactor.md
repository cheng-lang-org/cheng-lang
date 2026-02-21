# 容器语法重构：只保留 `T[]` 与 `T[N]`

源码层面只允许两种“容器/数组”类型写法：

- `T[]`：动态序列（growable sequence）
- `T[N]`：动态序列（`T[]`）的“初始化 reserve 容量提示”（cap hint）

旧语法 `seq[T]`、`openArray[T]` 以及历史固定容量容器（如 `Table_fixed`）已移除并在编译期报错。
`seq_fixed[T, N]` 只作为内部 lowering 目标存在（用户源码禁止直接写）。
`array[...]` 已移除；源码层面只保留 `T[]/T[N]` 两种写法。

同时，序列字面量只保留 `[...]`，`@[...]` 已移除。

## `T[]`（动态序列）

- 语义：动态序列，运行时布局与历史 `seq[T]` 等价（`len/cap/buffer`）。
- 默认值：等价空序列（`len=0 cap=0 buffer=nil`）。
- 隐式类型默认值初始化/重置：

```cheng
var xs: int32[] 
xs = []
```

- 访问与赋值：
  - `xs[i]` / `xs[i] = v` 会做 bounds check（运行时通过 `cheng_bounds_check`）。

## `T[N]`（reserve 容量提示）

> 注意：这里的 `T[N]` **不是**定长数组；它只是 `T[]` 的初始化 reserve 语法糖。

- 语义：`T[N]` 与 `T[]` 运行时布局相同（`len/cap/buffer`），仅在变量隐式默认值初始化时额外执行一次 `reserve(..., N)`。
- `N` 是整数表达式（可为字面量、常量或变量表达式），在运行时求值；`N <= 0` 时不分配 buffer。
- 类型等价：`T[N]` 与 `T[]` 是同一类型（`N` 不参与类型身份），可以互相赋值。
- 访问与赋值：
  - `xs[i]` / `xs[i] = v` 的 bounds check 依据 `len`（不是 `cap`）。

示例：

```cheng
var pending: str[uniqSet.items.len + 4]  # 等价于：var pending: str[]; reserve(pending, uniqSet.items.len + 4)
```

## 指针与后缀组合

类型后缀允许交错组合：

- `T*[]`、`T[]*`
- `T*[N]`、`T[N]*`

示例：

```cheng
var ps: void*[] 
var pArr: int32[4]* = nil
```

## 迁移指南

- `seq[T]` -> `T[]`
- `openArray[T]` -> `T[]`
  - 当前没有独立的 slice/borrow-view 类型；需要零拷贝时建议显式传 `ptr + len`。
- `newSeqWithCap[T](0, N)` / `var xs: T[]; reserve(xs, N)` -> `var xs: T[N]`
- `@[a, b, c]` -> `[a, b, c]`

## 自举说明（stage0 无兼容层）

生产链路已移除 stage0 overlay。自举固定走原生源码，不再允许 `T[]`/`[]` 到旧语法的 rewrite 兜底。

- `backend.stage0_no_compat` 默认阻断，并强制 `SELF_OBJ_BOOTSTRAP_STAGE0_COMPAT=0`。
- 若 seed stage0 不能直接编译当前语法，必须先刷新 stage0/seed，而不是生成 overlay。
- 主仓库 `src/` 始终保持新语法；stage1/stage2 与日常编译统一使用同一份源码树。

## 破坏性语法升级流程（Checklist）

这种“破坏性语法升级”（例如 `seq[T]` -> `T[]`、`@[...]` -> `[...]`）建议按下面流程落地，保证自举与文档同步：

1. 规范与文档先行：
   - 更新 `docs/`（本文件、相关语法文档）。
   - 同步更新 `docs/cheng-skill/` 与 `$HOME/.codex/skills/cheng语言/`（并跑 `sh src/tooling/verify_cheng_skill_consistency.sh`）。
2. 实现新语法：
   - 词法/语法支持（必要时加 parse recovery）。
   - `src/stage1/type_syntax_lowering.cheng` 里将新语法 lowering 到既有内部表示，尽量减小后端改动面。
3. 禁用旧语法（硬错误）：
   - 给出可执行的迁移提示（例如 “use `T[]` instead of `seq[T]`”）。
   - 旧语法只作为内部 lowering 目标存在，避免用户继续写。
4. 自举兼容（no-compat）：
   - 生产链路不再支持 stage0 overlay。
   - 若 seed stage0 不支持新语法，直接升级 stage0/seed，并确保 `backend.stage0_no_compat` 门禁通过。
5. 回归与种子更新：
   - 增加最小回归用例（`tests/` + `./verify.sh`）。
   - 跑 `sh src/tooling/bootstrap_pure.sh`；需要刷新 seed 时加 `BOOTSTRAP_UPDATE_SEED=1`。
