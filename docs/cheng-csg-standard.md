# Cheng CSG Standard

Cheng CSG 是语言无关的编译事实标准。它允许 TypeScript、Rust、Cheng 或其它前端把本语言语义提取成同一种结构化事实，再交给 Cheng 后端或 cold backend 生成 object/exe。

## 分层

```text
Language source
  -> language extractor
  -> CSG-Core facts
  -> Cheng-ABI mapper
  -> CHENG_CSG_V2 object facts
  -> cold backend
```

### CSG-Core

CSG-Core 描述源码语义，不描述某个 CPU 的指令。

必须包含：

- module：源文件、模块名、import/export 边。
- symbol：函数、类型、全局数据、外部声明。
- type：标量、对象、数组、tuple、enum、union、函数类型、泛型实例。
- layout：size、align、field offset、variant tag、payload layout。
- function：参数、返回值、可见性、所有权、异常/错误边界。
- cfg：block、terminator、dominance 可复算信息。
- op：typed canonical operation，不允许携带源码字符串当语义。
- call：目标符号、参数 ABI、返回 ABI、是否外部。
- data：常量数据、字符串、alignment、reloc。
- debug_map：source span 到 IR/object range 的映射。

当前 TypeScript extractor 的 `csg-core.v1` 实现位置：

```text
ts-csg/src/csg-core.ts
```

命令：

```sh
node ts-csg/dist/cli.js \
  --emit csg-core \
  --project /Users/lbcheng/cursor-restored/cursor-agents-window/tsconfig.json \
  --runtime node,browser \
  --entry-root src/index.ts \
  --entry-root src/main.ts \
  --entry-root src/renderer.ts \
  --out ts-csg/tmp/cursor-agents-window.csgcore \
  --report-out ts-csg/tmp/cursor-agents-window.report.json
```

report 固定包含：

- `complete`：是否已无 unsupported 语义且无需未实现 runtime provider。
- `counts`：module/symbol/type/function/block/op/call/data/runtime/unsupported 计数。
- `unsupported`：每个未闭合语义的 code、message、source span、owner function。
- `runtimeRequirements`：Node、Browser DOM、ECMAScript builtin、external package 的显式需求。
- `externalSymbols`：外部符号集合。
- `entryRoots`：实际进入 TypeScript Program 的项目入口。

`complete:false` 表示 Cheng-ABI lowering 被阻断；不能继续生成 native artifact。

禁止：

- 从源码字符串二次猜语义。
- 把 unresolved symbol 当 external 自动注册。
- 用降级、占位、模拟、猜测式重写补语义缺口。
- 忽略动态语义后继续生成成功 artifact。

### Cheng-ABI

Cheng-ABI 把 CSG-Core 降成 Cheng 后端能消费的 object facts。

必须固定：

- target triple。
- object format。
- pointer width。
- endian。
- scalar size/align。
- aggregate layout。
- string layout：`str` 为 24 字节，`str[]/uint8[]` 为 16 字节 sequence header。
- calling convention。
- reloc kind。
- provider/archive 输入。

## CHENG_CSG_V2

`CHENG_CSG_V2` 是 cold backend 当前 public object facts 格式。它不是通用语义层，而是已经接近 object 的后端输入。

文本格式：

```text
CHENG_CSG_V2\n
R0001 payload(target_triple)
R0002 payload(object_format)
R0003 payload(entry_symbol)
R0004 payload(function_record)
R0005 payload(instruction_word)
R0006 payload(code_reloc)
R0007 payload(data_record)
R0008 payload(data_reloc)
```

record 行固定为：

```text
R + kind_u16_hex_4 + payload_byte_len_u32_hex_8 + payload_hex
```

payload 内部字段使用 little-endian `u32`，字符串使用：

```text
u32 byte_len + utf8 bytes
```

当前 `CHENG_CSG_V2` 可作为跨语言最小落地层，但长期标准入口应是 CSG-Core。原因是其它语言 extractor 不应直接负责 CPU 指令选择；直接输出 instruction words 只适合小范围 bootstrap、fixture 或已经完成 lowering/codegen 的前端。

## 语言前端责任

每个语言 extractor 必须做三件事：

1. 调用本语言权威 parser/typechecker。
2. 生成 CSG-Core 结构化 facts。
3. 对无法静态闭合的语义 hard-fail。

TypeScript extractor 必须失败：

- `any`。
- `eval`。
- 动态 `import()`。
- 非字面量 `require()`。
- runtime prototype mutation。
- 无法静态解析的 computed declaration name。
- 依赖 JS VM object model 才能确定的语义。

Rust extractor 必须失败：

- 宏展开前的未解析 token stream。
- trait object 或泛型未单态化。
- unsafe 裸指针语义无法映射到 no-pointer surface。
- target feature/ABI 与 Cheng-ABI 不一致。
- panic/unwind 策略未显式声明。

Cheng extractor 必须失败：

- typed/lowering/PrimaryObjectPlan 缺字段。
- 未解析 import/call。
- slot/layout/reloc 越界。
- provider 输入缺失或 hash 不匹配。

## 最小合法子集

跨语言最小 CSG-Core 子集：

```text
module
function main() -> i32
block entry
return i32_literal
```

降到 `CHENG_CSG_V2` 的 Darwin ARM64 object facts 时，等价于：

```text
target = arm64-apple-darwin
format = macho
entry = _main
function _main
instruction words:
  movz w0, literal
  ret
```

该子集是真实语义闭合，不是占位：源码函数返回的整数 literal 被直接物化为目标 ISA 指令。超出子集的源码必须失败，直到对应 CSG-Core op 和 Cheng-ABI lowering 被实现。

第一版 TypeScript 实现已经把最小子集扩展到单文件 `export function main(): number`，支持同文件 int32 函数、位置参数、直接调用、局部 int32 变量、`+`、`-`、`*`、数值比较、`if` 和 `return`。对象、数组、循环、mutation、浮点值、动态输入、嵌套调用参数和外部调用仍必须 hard-fail。

## 验收

任一语言 extractor 不能只验证“文件写出来了”。必须通过：

- 同一输入重复导出 facts 字节一致。
- schema 版本、record kind、payload 长度校验通过。
- unsupported 输入非零退出。
- `CHENG_CSG_V2` 输入 cold backend 后生成 object。
- object 链接成真实可执行文件并运行，退出码或 marker 与源码语义一致。

TypeScript 项目级门禁：

```sh
cd ts-csg
npm run smoke:projects
```

当前固定项目：

```text
/Users/lbcheng/cursor-restored/cursor-agents-window
/Users/lbcheng/UniMaker/React.js
```

门禁动作：

- 运行目标项目 typecheck。
- 同一 tsconfig 导出两次 CSG-Core facts。
- facts 与 report 必须字节一致。
- report 的计数必须与 facts 对齐。
- report 必须显式列出 unsupported/runtime requirements，不能空白通过。

UniMaker React 门禁使用根 `tsconfig.json`，typecheck 命令固定为
`./node_modules/.bin/tsc --noEmit -p tsconfig.json`，覆盖生产 app 编译面。

## 演进顺序

1. 固化 CSG-Core schema。
2. 每个语言前端先覆盖最小合法子集。
3. 加类型 layout。
4. 加 CFG。
5. 加 call/data/reloc。
6. 加 provider/archive。
7. 再扩大语言特性。

不要反过来从大型语言特性开始做字符串转译。那会把动态语义、ABI、runtime model 混在一起，最终无法证明正确性。
