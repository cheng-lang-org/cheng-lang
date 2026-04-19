# 当前任务

| 项目 | 内容 |
|---|---|
| 目标 | 把 HIR/typed lowering 往 seed 后端继续真落，让 native/wasm 的复合值热路径都按共享 lowering rule helper 发码，而不是继续散着 `strcmp("composite")`。 |
| 当前状态 | seed 后端这条主线已经把 `call arg / materialize / return / copy / call-result / import kind` 收平了；前端 typed 这边也不再只是报表口，`import_buffer` 已经有了第一条真实归因路径：同文件 `importc fn` 的 `CallExpr`。 |
| 本轮真修复 | 1. `/Users/lbcheng/cheng-lang/v3/src/lang/parser.cheng` 新增最小 `v3NormalizedExprCall`，只规范化同文件 `importc fn` 的 call。2. `/Users/lbcheng/cheng-lang/v3/src/lang/typed_expr.cheng` 新增 importc 返回类型上下文，把 composite importc call 真打成 `v3TypedExprLowerImportBuffer / v3TypedExprReturnImportBuffer`。3. parser/compiler_csg/lowering 三条 smoke 都追平了这条新路径。 |
| 关键产物 | `parser_normalized_expr_smoke` `compiler_csg_smoke` `lowering_plan_smoke` |
| 已验收 | `/Users/lbcheng/cheng-lang/artifacts/v3_bootstrap/cheng.stage3 run-host-smokes --compiler:/Users/lbcheng/cheng-lang/artifacts/v3_bootstrap/cheng.stage3 --label:typed_import_buffer parser_normalized_expr_smoke compiler_csg_smoke lowering_plan_smoke` `git -C /Users/lbcheng/cheng-lang diff --check` |
| 下一步 | 如果继续，最值的是把这条 `CallExpr` 从“同文件 importc call”扩成通用 call HIR，至少覆盖同包/跨文件已知函数；不然 `import_buffer` 现在只是先打通了一条最小真路径，还不是完整调用层。 |
