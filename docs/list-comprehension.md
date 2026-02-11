# 列表生成式（List Comprehension）

Cheng 支持 Python 风格列表生成式，语法为：

```cheng
[expr for pat in iter]
[expr for pat in iter if cond]
```

生成式的结果类型为 `T[]`（动态序列）。

## 语法

- 普通列表字面量：
  - `[a, b, c]`
  - `[]`（空字面量需要类型上下文）
- 列表生成式：
  - `[expr for pat in iter]`
  - `[expr for pat in iter if cond]`
- 当前只支持 1 个 `for` 子句与可选 1 个 `if` 过滤子句。

说明：

- `pat` 是 `for-in` 的 pattern（与 `for pat in iter:` 语句一致）。
- `iter` 是可迭代对象（与 `for-in` 支持范围一致）。

## 语义

列表生成式是编译期语法糖，语义等价于：

```cheng
var out: T[] 
for pat in iter:
    if cond:
        addPtr[T](&out, expr)
return out
```

- `if cond` 子句可省略。
- `pat` 引入的名字只在生成式内部可见。

## 类型要求

结果类型必须能在生成式出现的位置被确定，否则会报错。

常见写法：

```cheng
fn doubleNotThree(xs: int32[]): int32[] =
    return [x * 2 for x in xs if x != 3]

fn main(): int32 =
    let xs: int32[] = [1, 2, 3, 4]
    let ys: int32[] = [x for x in xs if x > 1]
    return 0
```

## 后端限制（当前实现）

目前后端对 `[...]` 字面量与列表生成式都要求出现在“可落地”的位置，以便生成循环与内存写入：

- 变量初始化（`let/var` 初始化）
- 对简单标识符的赋值（`x = [...]`）
- `return [...]`
- 全局初始化

不支持把生成式直接嵌入更复杂的子表达式（例如作为函数实参）；
解决方式是先绑定到临时变量再使用。

## 与 `for pat in iter : body` 的关系

仓库仍保留 `for pat in iter : body` 形式（表达式/语句生成式），用于表达“遍历并执行 body”。
列表生成式专门用于构造 `T[]` 结果序列。

## 迁移提示

- `@[...]` 序列字面量已移除，统一使用 `[...]`。
