# 当前任务

| 项目 | 内容 |
|---|---|
| 目标 | 把 HIR/typed lowering 往 seed 后端继续真落，让 native/wasm 的复合值热路径都按共享 lowering rule helper 发码，而不是继续散着 `strcmp("composite")`。 |
| 当前状态 | seed 后端这条主线已经把 `call arg / materialize / return / copy / call-result / import kind` 收平了；前端 typed 这边也不再只是报表口，`import_buffer` 已经有了真实归因路径，而且 `CallExpr` 已经安全扩到“同文件 importc + 同文件 local function + 同包跨文件已知函数”。 |
| 本轮真修复 | 1. `/Users/lbcheng/cheng-lang/v3/src/lang/parser.cheng` 的 `v3NormalizedExprCall` 现在按 per-source token 线性扫描识别 local/external/importc call，不再按名字表切整行。2. `/Users/lbcheng/cheng-lang/v3/src/lang/typed_expr.cheng` 的 importc 返回类型和同包已知函数返回类型都已经接进 typed fact，composite importc call 会真落成 `v3TypedExprLowerImportBuffer / v3TypedExprReturnImportBuffer`。3. `v3ParserCollectExternalCallNames(...)` 已改成“本地快照 + 显式 while”，掐掉了 `profiles[otherIndex].localCallNames[...]` 这条复合字段嵌套索引造成的 `idx=len` 真越界。4. parser/compiler_csg/lowering 三条 smoke 都重新跑绿。 |
| 关键产物 | `parser_normalized_expr_smoke` `compiler_csg_smoke` `lowering_plan_smoke` |
| 已验收 | `/Users/lbcheng/cheng-lang/artifacts/v3_bootstrap/cheng.stage3 run-host-smokes --compiler:/Users/lbcheng/cheng-lang/artifacts/v3_bootstrap/cheng.stage3 --label:typed_import_buffer7 parser_normalized_expr_smoke compiler_csg_smoke lowering_plan_smoke` `git -C /Users/lbcheng/cheng-lang diff --check` |
| 下一步 | 如果继续，最值的是把这条 `CallExpr` 从“同包跨文件已知函数”再扩成通用 call HIR，至少补到 import 别名和更一般的 closure 内可见函数；但仍要坚持 per-source token 扫描和显式边界分支，不能回到“按名字切整行”和复合字段嵌套索引。 |
