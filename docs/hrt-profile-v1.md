# HRT Profile v1（实时子集规范）

本文定义 Cheng 硬实时后端（HRT）在 v1 阶段允许/禁止的语法与语义范围，用于：
- 指导开发者在 HRT 模式下编写可通过的代码
- 为 `hrt_verify` 提供一致的验收口径

## 1. 适用范围
- 目标：保证可静态界定、可确定生成的子集
- 模式：`bin/cheng --mode:hrt` 或 `CHENG_ASM_RT=hard`
- 说明：HRT v2 扩展（如 case/of）需设置 `CHENG_HRT_PROFILE=2`；本文档仍以 v1 默认约束为准

## 2. 允许的语法（v1）
### 2.1 顶层
- `import` / `include`
- `fn` 声明
- 简单 `type` / `object` / `enum` 声明（仅用于静态布局）

### 2.2 语句
- `let` / `var` / `const`（仅允许单一标识符绑定）
- `if` / `while` / `for-range`
- `break` / `continue` / `return`
- 表达式语句

### 2.3 表达式
- 字面量：`int` / `char` / `bool` / `nil`
- 标识符、括号、`cast`
- 前缀：`+ - ! ~`
- 中缀：`+ - * / % & | ^ << >>`、`== != < <= > >=`、`&& ||`
- 调用：直接函数调用（无具名参数）

### 2.4 全语法 allow/reject 矩阵（AST 视角）
来源：`stage0c` 的 HRT profile 检查（`hard_rt_check_profile_node`）及其配套约束（类型/循环/调用/并发）。

图例：✅ 允许，❌ 拒绝，⚠️ 允许但有额外约束。  
说明：v2 仅放开 `case/of`（并强制“简单模式”），其余与 v1 相同。

#### 2.4.1 顶层/声明
| 构造 | v1 | v2 | 约束 |
| --- | --- | --- | --- |
| `module`/`import`/`include` | ✅ | ✅ | 仅语法层允许，仍受 importc/runtime 规则约束 |
| `fn` | ✅ | ✅ | 禁止递归；调用图可达分析 |
| `type`/`object`/`enum`/`enum field` | ✅ | ✅ | 仅静态布局；类型需满足固定容量约束 |
| `fn`/`iterator` | ❌ | ❌ | 直接拒绝 |
| `macro`/`template` | ❌ | ❌ | HRT 直接拒绝，且不展开 |
| `trait`/`concept` | ❌ | ❌ | 直接拒绝 |
| `generic params` | ❌ | ❌ | HRT v1/v2 禁止泛型声明 |

#### 2.4.2 语句
| 构造 | v1 | v2 | 约束 |
| --- | --- | --- | --- |
| `let`/`var`/`const` | ⚠️ | ⚠️ | 仅允许单一标识符绑定 |
| `if`/`while`/`for-range` | ⚠️ | ⚠️ | `while` 必须 `@bound(N)`；`for` 需常量范围或 `@bound` |
| `break`/`continue`/`return` | ✅ | ✅ | - |
| `block`/`else` | ✅ | ✅ | - |
| `assignment` | ✅ | ✅ | - |
| `fast assignment`（`+=` 等） | ❌ | ❌ | 直接拒绝 |
| `when`/`defer`/`yield` | ❌ | ❌ | 直接拒绝 |
| `case`/`of` | ❌ | ⚠️ | v2 允许，且仅允许“简单模式” |

#### 2.4.3 表达式与语法糖
| 构造 | v1 | v2 | 约束 |
| --- | --- | --- | --- |
| `call` | ⚠️ | ⚠️ | 禁止具名参数；禁止 runtime/importc；禁并发 API |
| `cast`/`paren`/`ident`/`symbol` | ✅ | ✅ | - |
| `prefix` | ⚠️ | ⚠️ | 仅允许 `+ - ! ~ & *` |
| `infix` | ⚠️ | ⚠️ | 仅允许 `+ - * / % & | ^ << >> == != < <= > >= && ||` |
| `dot` | ⚠️ | ⚠️ | 允许字段访问；禁止 `string.len` |
| `postfix` | ❌ | ❌ | 包含 `Result ?` 解包与后缀表达式 |
| `bracket expr`/`[]` | ❌ | ❌ | 禁止索引；固定容量需显式 API |
| `curly expr`/`{}` | ❌ | ❌ | 直接拒绝 |
| `if/when/case expression` | ❌ | ❌ | 直接拒绝（含 `?:`） |
| `lambda`/`comprehension`/`do`/`guard` | ❌ | ❌ | 直接拒绝 |
| `range`（`NK_RANGE`） | ❌ | ❌ | 直接拒绝；for-range 仅允许 `..`/`..<` 中缀形式 |
| `deref expr` | ❌ | ❌ | 禁止显式解引用节点 |
| `hidden deref` | ✅ | ✅ | 仅 IR 内部使用 |

#### 2.4.4 类型/字面量/模式
| 构造 | v1 | v2 | 约束 |
| --- | --- | --- | --- |
| `ref`/`ptr`/`var` type | ⚠️ | ⚠️ | 仍需通过类型约束检查 |
| `tuple`/`set`/`fn` type | ❌ | ❌ | 直接拒绝 |
| `record case` | ❌ | ❌ | 直接拒绝 |
| `pattern` | ⚠️ | ⚠️ | v1 仅允许单一标识符；v2 允许标识符或常量 |
| `int/char/bool/nil` literal | ✅ | ✅ | - |
| `float` literal/type | ❌ | ❌ | 直接拒绝 |
| `string` literal/type | ❌ | ❌ | 直接拒绝（即使固定容量） |
| `tuple/seq/table` literal | ❌ | ❌ | 直接拒绝 |

#### 2.4.5 注解与并发
| 构造 | v1 | v2 | 约束 |
| --- | --- | --- | --- |
| `@bound` | ✅ | ✅ | 仅允许常量参数 |
| `@task/@period/@deadline/@priority` | ❌ | ⚠️ | v1 拒绝；v2 仅允许静态任务（顶层 `fn`、无参、`int32`、需 `@period/@deadline/@priority`） |
| `async/await` | ❌ | ❌ | v1/v2 均禁用 |
| 原子 RC/并发类型 | ❌ | ❌ | 强制 `CHENG_MM_ATOMIC=0`，禁 `Arc/Atomic/Mutex/RwLock` |

## 3. 必须满足的约束
- 循环必须有界：
  - `while` 需 `@bound(N)`（N 为常量）
  - `for` 需常量范围或 `@bound(N)`
- 禁止递归（静态调用图检查）
- 禁止 runtime/importc 调用
- 并发与原子约束：
  - HRT 模式强制 `CHENG_MM_ATOMIC=0`；禁止原子 RC 路径与 `share_mt/Arc/Atomic/Mutex/RwLock`
  - HRT 模式强制 `CHENG_MM=off` 或 `CHENG_ARC=0`（禁用 ARC/ORC）
  - 禁止 `async/await` 与并发 API（`spawn/channel send` 等）
- 禁止 `?` 隐式解包（Err 分支不可触发隐式 return/panic）

### 3.1 固定容量容器（P1-02 扩展）
在 HRT v1 中，动态容器仍被禁止；P1-02 扩展为固定容量容器：
- `seq[T, N]`：固定容量序列，`N` 必须是编译期常量整数
- `Table[V, N]`：固定容量表（key 为 `str` 的 Table），`N` 必须是编译期常量整数
- `str[N]`/`cstring[N]`：固定长度字符串/字节串（仅类型层面约束）

仍然禁止：
- `seq[T]`、`Table[V]`、`str` 等动态容器类型
- `@[...]`、`{...}` 动态字面量

允许的 importc 白名单（确定性内存操作 + HRT ring helper）：
- `ptr_add`、`c_memcpy`、`c_memset`、`c_memcmp`、`c_strlen`、`c_strcmp`、`load_ptr`、`store_ptr`
- `cheng_hrt_net_slot_ptr`、`cheng_hrt_file_req_ptr`、`cheng_hrt_file_cpl_ptr`

## 4. 禁止的语法与语义
- 控制流扩展：`when` / `case` / `of` / `if` 表达式 / `do` / `defer`
- 并发语义：`async` / `await`
- 任务注解：`@task/@period/@deadline/@priority`（v1 禁止；v2 仅允许静态任务，见 §2.4.5）
- 成员访问与索引：允许结构体/固定容量容器字段访问（`.len/.cap/.buffer` 等）；`[]` 索引仍禁止（需显式 API）
- 宏与模板：`macro` / `template` / `iterator`
- 动态与不可界定数据：
  - 动态容器类型 `seq/str/cstring/Table` 与非固定容量 `seq_*/Table_*`（固定容量 `seq[T,N]`/`str[N]`/`Table[V,N]` 例外）
  - 动态字面量 `@[...]` / `{...}` / 字符串字面量
- 浮点类型与字面量：`float/float32/float64/double`
- 具名参数与不允许的运算符（前缀仅允许 `+ - ! ~ & *`）
- Result 解包：`?`

## 5. 典型示例
允许：
```
fn main(): int32 =
    var sum: int64 = 0
    @bound(4)
    while sum < 4:
        sum = sum + 1
    return 0
```

禁止（动态容器/字符串/字段访问）：
```
fn main(): int32 =
    let s: str = "hi"
    if s.len > 0:
        return 1
    return 0
```

## 6. 验证入口
- `CHENG_MM_ATOMIC=0 CHENG_MM=off bin/cheng --mode:hrt --file:<input.cheng> --out:<output.s>`
- `sh src/tooling/verify_hrt_backend.sh`
