# Lessons

## 总原则

- 从单一真源往下推，不要在 parser、CSG、lowering、seed、wasm 各层各猜一份。
- 发现未知就显式标未知，不要假装类型或 ABI 已经确定。
- 复合值统一按地址语义思考；不要在热路径上混用“按值/按句柄/按临时槽”。
- 验收优先用矩阵和正式 smoke，不靠一次性日志或临时手工命令。

## 前端与 lowering

- 语法糖必须在前端一次吃掉，后端不再识别 `Fmt/?:/range/result intrinsic` 这类表面。
- 表达式一旦跨层流动，第一刀先补 `exprId + source span`。
- `compiler_csg` 和 `lowering_plan` 必须共用同一份 typed fact 与 lowering rule，不要双份统计。
- lowering 真规则至少要显式带 `arg/result/materialize/copy/import` 五类字段。

## ABI 与复合值

- `str/rawbytes/seq/result` 当前统一按“显式地址优先 + region copy”收。
- `Bytes[] add/setLen` 这种序列能力缺口要直接修进编译器，不要再在 host/browser 业务代码里兜桥或换数据结构绕过去。
- `xor` 这种关键字二元运算要成套收进 `const/infer/prepare/native/wasm`，不能只在某一层临时认一下，不然会被误判成 prefix-call。
- `len(str/Bytes/seq)` 不要只为局部变量写特判；统一走 lvalue/global 地址再按 ABI 布局取长度槽更稳。
- browser wasm ABI 不要长期手写 `input_len/copy/raw_handle/text_handle`；先抽 schema/helper，再走自动生成。
- browser codec 第一刀先收 `bytes/text/optional/handle` 这四类最常见桥，不要一上来就试图在业务函数里直接内联所有 browser ABI 细节。
- `exe` 路径下的显式 `@exportc` 也必须进入 reachable roots；否则会伪装成“外部桥未定义”，把真问题藏住。
- runtime provider 遇到入口源码同名显式导出时，provider 那份外部符号要主动退掉，不能靠 duplicate symbol 再让链接器替你判。
- `stage0` 不是 live host smoke compiler，host/browser smoke 优先用 `stage3` 或 live backend driver。

## 记录与流程

- 账本只写当前有效信息；旧历史压缩，不要让 8000 行文档遮住当前决策。
- 开新任务前先看 `lessons.md`，避免把已经踩实的坑重新踩一遍。
