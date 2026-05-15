# Handoff — Cheng Cold Compiler C Seed Replacement

## 环境

- 仓库：`/Users/lbcheng/cheng-lang`，分支 `main`，领先 origin 57 个提交
- 构建：`cc -std=c11 -O2 -o /tmp/cheng_cold bootstrap/cheng_cold.c`（cold_parser.c 在 cheng_cold.c 行 20238 处 `#include`）
- 回归：`bash tools/cold_regression_test.sh`
- 目标文件：`src/core/backend/primary_object_plan.cheng`
- 编译测试命令：
```
/tmp/cheng_cold system-link-exec \
  --root:/Users/lbcheng/cheng-lang \
  --in:src/core/backend/primary_object_plan.cheng \
  --target:arm64-apple-darwin \
  --out:/tmp/ct_pop.o --emit:obj \
  --report-out:/tmp/ct_pop.report.txt 2>&1
```

## 提交版缺陷

HEAD (`60637226`) 的 `cold_parser.c` 引用了 `alias_type` 字段和 `symbols_set_type_alias()` 函数，但 `cheng_cold.c` 未定义。需要最小修复才能编译：

```c
// cheng_cold.c TypeDef 结构体加字段（行 4047 附近，bool is_enum 之后）:
Span alias_type;

// symbols_add_type 内初始化:
type->alias_type = (Span){0};

// symbols_add_type 之后加函数:
static void symbols_set_type_alias(TypeDef *type, Span alias_type) {
    if (!type) die("type alias target missing");
    type->alias_type = alias_type;
}
```

仅加这 3 处，回归 **~102/108**（6 个失败是预存的，跟 alias_type 无关）。

## 4 个已验证的 Bug 修复

### Fix 1: parse_field_assign — 接受 STR_REF 和 SEQ_REF 类型

**文件**：`bootstrap/cold_parser.c`，`parse_field_assign` 函数

**Kind 检查行**（改前）：
```c
if (local->kind != SLOT_OBJECT && local->kind != SLOT_OBJECT_REF &&
    local->kind != SLOT_PTR && local->kind != SLOT_STR) {
```
**改后**：
```c
if (local->kind != SLOT_OBJECT && local->kind != SLOT_OBJECT_REF &&
    local->kind != SLOT_PTR && local->kind != SLOT_STR &&
    local->kind != SLOT_STR_REF &&
    local->kind != SLOT_SEQ_I32_REF && local->kind != SLOT_SEQ_STR_REF &&
    local->kind != SLOT_SEQ_OPAQUE_REF) {
```

**STR_REF 字段赋值**：在 SLOT_STR 分支的 `return block;` 之前插入，用 `BODY_OP_PTR_STORE_I64/I32` 带 offset（`c` 参数）解引用指针后再存字段。结构同 SLOT_STR 分支，但用 PTR_STORE 代替 SLOT_STORE。

### Fix 2: parse_for — range() 语法跳过残留体

**文件**：`bootstrap/cold_parser.c`，`parse_for` 函数

**位置**：`/* skip malformed for range */` 注释所在行

**问题**：`for i in range(n):` 语法不走 `..` 路径，`parser_take(".")` 失败后直接 `return block` 但没消费 `:` 和缩进体，导致后续解析错乱（var/let 被当成赋值语句）。

**修复**：`return block` 改为 skip 逻辑：
```c
if (!parser_take(parser, ".") || !parser_take(parser, ".")) {
    parser_line(parser);                     // 跳过当前行（含 :）
    int32_t skip_indent = parser_next_indent(parser);
    for (;;) {
        if (parser->pos >= parser->source.len) break;
        int32_t ni = parser_next_indent(parser);
        if (ni < 0 || ni < skip_indent) break;
        parser_line(parser);                 // 跳过缩进体行
    }
    return block;
}
```

### Fix 3: locals_add_global_shadow — 全局变量值初始化

**文件**：`bootstrap/cold_parser.c`，`locals_add_global_shadow` 函数

**位置**：`body_slot_set_type(body, value_slot, global->type_name);` 之后

**插入**：
```c
if (global->kind == SLOT_I32) {
    body_op(body, BODY_OP_I32_CONST, value_slot, global->init_value, 0);
} else if (global->kind == SLOT_I64) {
    body_op(body, BODY_OP_I64_CONST, value_slot, 0, 0);
} else {
    body_op(body, BODY_OP_I32_CONST, value_slot, 0, 0);
}
```

### Fix 4: parser_find_global — 导入别名回退查找

**文件**：`bootstrap/cold_parser.c`，`parser_find_global` 函数

**问题**：非 import_mode 下函数体引用导入模块全局变量时，`parser_find_global` 只用裸名查找。导入模块的全局变量存储为 `alias.varName` 格式，裸名查找失败。

**修复**：在现有 `return symbols_find_global(...)` 之后加回退：
```c
GlobalDef *global = symbols_find_global(parser->symbols, name);
if (global) return global;
if (!parser->import_mode && name.len > 0 && cold_span_find_char(name, '.') < 0) {
    for (int32_t si = 0; si < parser->import_source_count; si++) {
        Span alias = parser->import_sources[si].alias;
        if (alias.len <= 0) continue;
        Span scoped = cold_arena_join3(parser->arena, alias, ".", name);
        global = symbols_find_global(parser->symbols, scoped);
        if (global) return global;
    }
}
return 0;
```

**注意**：需要确认 `parser->import_sources` 在所有调用上下文中非空。

## primary_object_plan.cheng 编译状态

**当前阻塞**：`parse_primary` 不支持 `if` 三元表达式。文件中有 7 处使用：

```cheng
// 行 9265 - 函数调用参数内（最复杂的 case）
add(buf, if i < ttl: uint8(int32(targetTriple[i])) else: uint8(0))

// 行 9347-9349 - let 绑定内（已有 parse_inline_if_let_binding 可处理）
let opA = if op.operands.len > 0: op.operands[0] else: 0

// 行 9494 - let 绑定内
let functionName = if plan.symbolNames.len > 0: plan.symbolNames[0] else: ""
```

**实现建议**：`parse_inline_if_let_binding`（行 ~7199）已有完整的 if-expr 块分支逻辑（创建 true_block/false_block/join_block，parse_condition_span，parse_expr_from_span 解析 then/else 分支，cold_store_value_into_slot 存结果）。提取核心逻辑到新函数 `parse_if_expr`，在 `parse_primary` 中加 `if (span_eq(token, "if")) return parse_if_expr(...)`。

**注意**：`parse_primary` 没有 block 参数。用 `body->block_count - 1` 作为当前 block（entry_block），然后创建 true/false/join blocks。对于行 9265 的 case，else 表达式 `uint8(0)` 后有额外的 `)`（属于外层 add 调用），不能用 `parser_take_inline_rest_of_line`，需要用 `parse_expr` 或平衡括号的提取。

## 已确认不相关/不需要修

- **Default params**：后端编译代码中未使用，现有 i32 默认值支持够用
- **"str comparison supports == and !="**：是 die()，需要改成 soft skip 才能继续编译
- **PTR_STORE offset codegen**：需配合 Fix 1 的 STR_REF 分支一起改，把 `a64_str_imm(R0, R1, 0, ...)` 改成 `a64_str_imm(R0, R1, body->op_c[op], ...)`

## 注意事项

- `cold_parser.c` 会被其它工具修改，避免用 Read 后再 Edit 的方式，用 Python 脚本或 sed 批量改
- 回归测试结果可能因共享状态（/tmp 残留 .o 文件）在 92-102 之间波动，基线 102/108
- 不要在这个代码库用 `git stash` + 频繁 `checkout` 组合，提交版不一致，stash 容易丢失改动
