# Stage 0 Compiler

> 冷编译器自举的迷你编译器，证明冷编译器可以编译一个自洽的编译器源文件。
> 347 行，7/7 回归通过，171ms 编译 dispatch_min。

## 能力

| 特性 | 状态 | 说明 |
|---|---|---|
| 词法分析 | ✅ | lex_all: ident / digit / punct (13 种) |
| 多函数定义 | ✅ | fn name(params):type=body |
| 参数扫描 | ✅ | tok_len<=2 过滤类型名，仅注册参数 |
| let x = <int> | ✅ | 立即数绑定 + 寄存器分配 |
| return <ident> | ✅ | arm_mov_reg |
| return a+b | ✅ | arm_add_reg(0, vr, vr+1)，利用参数顺序注册 |
| return fn(a,b) | ✅ | 跨函数调用，movz 参数 + bl |
| 独立 fn 调用 | ✅ | fn(args) 作为语句 |
| 编译产物 | ✅ | 原生 Mach-O .o 文件 (arm64) |
| bl 指令 | ✅ | 带偏移的跨函数调用 |
| symbol 表 | ✅ | _main 导出 |
| 256 字节 code section | ✅ | 支持最多 64 条指令 |

## 暂时不支持

| 特性 | 原因 |
|---|---|
| if / elif / else | 冷编译器 compile_body 函数 slot 数已达上限 (304)，无法增码 |
| while 循环 | 同上 |
| 前向引用 | 需两遍编译 (先扫描后生成)，main() 同样触达冷编译器上限 |
| let x = a+b (表达式) | let handler 仅支持右侧为立即数 |
| 嵌套函数调用 | ret handler 仅支持 fn(a,b) 形式，不支持 fn(fn2()) |
| 多返回值 / struct | 超出冷编译器自举范畴 |
| import 模块 | 嵌入源无模块系统 |

## 使用

```
/tmp/cheng_cold system-link-exec \
  --in:bootstrap/stage0_compiler.cheng \
  --target:arm64-apple-darwin \
  --out:/tmp/stage0_bin
/tmp/stage0_bin
# exit 42 = 成功编译嵌入源 → /tmp/compiler_out.o
```

## 诊断

冷编译器支持 `--diag:dump_per_fn` 和 `--diag:dump_slots` 查看各函数代码字和 slot 统计：

```
/tmp/cheng_cold system-link-exec \
  --in:bootstrap/stage0_compiler.cheng \
  --target:arm64-apple-darwin \
  --out:/tmp/stage0 \
  --diag:dump_per_fn
```

compile_body 的 slot 统计 (正常版本):
```
fn[36] compile_body at word=14146 slots=304 frame=5332
fn[36] end at word=16753 (count=2607)
```
