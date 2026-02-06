# Cheng 直出 C Profile（全规范映射与降级/重写）

本文定义 **Cheng 直出 C 后端** 面向正式规范的映射策略：通过降级/重写与运行时支撑，将语法稳定映射到 C，并用正交原子任务矩阵跟踪落地。  
当前实现仅走直出 C 路径，旧 IR 链路已移除；历史命名仅供参考。  
该 Profile 不等同于 HRT：**不强制硬实时约束**，允许运行时/系统调用，但要求语义可稳定映射到 C。

## 1. 适用范围
- 目标：前端直接输出 C 源码（不再经过旧 IR），用于通用 C 目标或可移植构建。
- 依赖：需要 C 运行时 shim（字符串/容器/内存管理），运行时接口以 `chengbase.h`/`std` 为准。
- 约定入口（现状）：生产统一走 backend-only 主链路 `chengc.sh --backend:obj`（默认）；`chengc.sh --backend:c` + `CHENG_C_FRONTEND=./stage1_runner` 仅作为兼容回退链路。
- 可选：`CHENG_C_SYSTEM=1` 尝试加载 `src/stdlib/bootstrap/system.cheng`（当前仍含未支持语义，默认关闭）。
- 基线规范：`docs/cheng-formal-spec.md`；本 Profile 目标覆盖正式规范，通过降级/重写/运行时支撑映射到 C，差距用矩阵追踪。
- 适用后端：直出 C 后端（ABI/运行时统一口径）。

## 1.1 使用说明（v0）
最小闭环示例：
```
sh src/tooling/chengc.sh examples/c_backend_smoke.cheng --backend:c
```

最小 ABI 样例（覆盖 FULLC.A01/FULLC.A04）：
- `examples/c_backend_smoke.cheng`：object case（tagged union）+ slice view + bounds helper。
- 验证入口：`sh src/tooling/verify_c_backend.sh`（期望退出码 7）。

最小 async/并发样例（覆盖 FULLC.R03/FULLC.R04）：
- `examples/c_backend_async_runtime_smoke.cheng`：async/await（含 async 内 await 拆分）+ channel/spawn + scheduler shim。
- 验证入口：`sh src/tooling/verify_c_backend_async_runtime.sh`（期望退出码 7）。

直出 C fullspec 子集（C backend v0 可覆盖）：
- `examples/c_backend_fullspec.cheng`：基础类型/控制流/切片/联合体等子集回归。
- 验证入口：`sh src/tooling/verify_c_backend_fullspec.sh`（期望输出 `fullspec ok`；脚本默认 `CHENG_C_SYSTEM=1` + `CHENG_EMIT_C_MODULES=0`）。

仅生成 C 文件：
```
sh src/tooling/chengc.sh examples/c_backend_smoke.cheng --emit-c --c-out:chengcache/hello.c
```

模块级增量（C 后端）：
```
sh src/tooling/chengc.sh examples/asm_modules/main.cheng --backend:c --emit-c-modules
```

模块级并行/增量（推荐生产默认）：
```
sh src/tooling/chengc.sh examples/asm_modules/main.cheng --backend:c --emit-c-modules --jobs:8 --modules-out:chengcache/c_backend.modules
```

说明：
- 当前实现处于 v0 阶段，正式规范的差距通过降级/重写与运行时扩展补齐（见 §2 与 §8 矩阵）。
- C 后端默认不自动加载 `system` 模块（需要时可设 `CHENG_C_SYSTEM=1`；仍可能因未支持语义失败）。
- 运行时/stdlib 依赖仍需手动链接；`chengc.sh --backend:c` 仅自动编译 `system_helpers.c`。
- 模块级并行增量通过 `--jobs:<N>` 控制并行度，产物与缓存键记录在 `<modules_out>/modules.map`，并生成 `<modules_out>/*.modsym` 模块导出符号表（混合后端强制产出）。
- `.modsym` 记录模块真实导出符号（C 实际符号名，已剔除 `importc` 声明），用于链接冲突检查；可用 `CHENG_MODSYM_CHECK=0` 跳过检查。
- `.modsym` 格式：首行 `# cheng modsym v1`，随后 `module=<path>`，并以 `func <name>` / `global <name>` 列出导出符号。
- `.modsym` 示例：
```
# cheng modsym v1
module=/abs/path/to/examples/asm_modules/mod_a.cheng
func modA_init
func modA_calc
global modA_counter
```
- 模块级 C 后端要求 `stage1_runner`（Stage1 C 直出）。
- 直出 C 后端使用 `--emit-c-modules`。

环境变量速查：
- `CHENG_C_FRONTEND=./stage1_runner`：指定直出 C 前端（默认 `./stage1_runner`；生产路径不使用 stage0）。
- `CHENG_C_SYSTEM=1`：加载 `system_c`/`system` 模块（默认关闭）。
- `CHENG_EMIT_C_MODULES=0`：禁用模块级 C 输出（`--backend:c` 默认开启）。
- `CHENG_C_MODULE=<path>`：单模块编译时指定模块路径（由 `chengc.sh` 传入）。
- `CHENG_MODSYM_CHECK=0`：禁用模块符号重复检查。
- `CHENG_DEPS_MODE=deps|deps-list`：依赖输出格式（默认 `deps-list`）。
- `CHENG_STAGE1_OWNERSHIP_DEEP=1`：强制 ownership 预分析深度遍历（默认浅遍历以提升 stage1 性能，仅用于对照/排障）。
- `CHENG_STAGE1_PROFILE=1`：输出 stage1 的 sem/mono/ownership/cgen 分析与 top-k 节点耗时；包含 `[cgen-cache] strip/mangle` 命中统计，用于定位 determinism 热点与字符串热路径。
- 当 `stage1_runner` 旧于 `src/stage1/*` 时，验证脚本会提示使用旧 runner；需先执行 `./src/tooling/bootstrap.sh --fullspec` 以刷新 runner 再做性能对比。

### 1.1.1 完全移除旧 IR 链路的前提
以下前提满足后，直出 C 后端才可替代旧 IR 链路（并可移除 SRT/旧链路）：
- **模块图替代**：直出 C 后端必须产出模块图/依赖清单与失效键（当前使用 `.deps.list`），并驱动并行/增量调度。
- **跨模块元数据**：最小闭环已提供 `.modsym`（模块导出符号表）用于链接协议；接口/布局元数据（例如 `.cmeta`/`.h`/JSON）仍作为后续补充。
- **自举闭环**：Stage0C/直出 C 能完整编译 Stage1 并产出 runner；seed/确定性验收不再依赖旧 IR。
- **缓存键闭环**：增量缓存键需覆盖编译器版本、目标平台/ABI、运行时配置、依赖锁与环境变量。
- **运行时头文件迁移**：`chengbase.h` 与 C shim 头文件统一在 `runtime/include`，工具链引用与 ABI 口径一致。
- **工具链替换**：旧任务图需替换为 C 后端构建图（或新格式），相关脚本/CI/验收随之切换。

## 1.2 兼容策略概览
- 语义等价优先：把高层语法降级到 C 可表达的核心语义（显式控制流/赋值/状态机）。
- 前端降级/重写：新增独立 lowering pass（case/when、具名参数、`?`、defer、迭代器、lambda、async 等）。
- 运行时支撑：补齐容器/字符串/错误/协程/并发原语，保证语义稳定可链接。
- ABI/布局扩展：tagged union、closure env、slice view、vtable 等布局进入 C 侧约定。
- 缺口处理：未映射能力须给出诊断并标注矩阵任务 ID。

## 1.3 已知缺口与改进方向（C-Profile）

- **语义缺口**：`enum` 显式值、`tuple`、`case/of`、`set pattern`、`for-in`、`yield`、`await` 表达式仍有 not-mapped/unsupported，需要明确降级或短期禁用路径。
- **统一主链路**：stage0c/ASM/HRT 已从生产入口移除；泛型/trait/复杂 pattern 的支持与诊断以 stage1/backend-only 实现为准。
- **ABI 约束**：`importc` 聚合返回强制 wrapper/out；`varargs` 仅限受控签名；捕获型回调必须 `ctx: void*` 携带环境。
- **缓存键一致性**：fullspec/示例输入需参与缓存失效；determinism hash 必须覆盖 seed/编译参数/目标平台/关键环境变量。
- **诊断可用性**：降级失败必须带任务 ID 与 rewrite hint，禁止静默生成不安全代码。

## 2. 全规范映射策略

### 2.1 降级/重写优先级
- 语法糖优先降级为核心控制流（`if/while/for-range`）与显式赋值。
- `case/when/of` → `if/elif` 链 + tag/guard 判定。
- **控制流表达式**（`if/when/case` 作为表达式）→ `tmp` 临时变量 + 分支赋值（要求可推导类型）。
- **短路语义**：`&&`/`||` 右侧含控制流表达式时，降级需保留短路，仅在需要时求值右侧。
- 具名/默认参数 → 生成 wrapper + 位置参数调用。
- `?` → 显式错误检查与早退。
- `defer`/`do`/`yield` → cleanup 块或显式状态机。
- `for-in`/iterator → 索引循环或迭代器状态机。
- `lambda`/closure → 提升为命名函数 + 捕获环境结构。
- `async/await` → 协程状态机与 await 点拆分。

### 2.2 运行时支撑边界
- 容器索引/切片/迭代器（`str/seq/Table`）与边界检查。
- `Option/Result/Error` 与 `panic/abort` 语义。
- 协程调度与 awaitable 原语、`channel/spawn` 并发原语。
- 字符串/字节处理（UTF‑8、比较、拼接）。

### 2.3 ABI/代码生成扩展
- tagged union 布局与 tag/variant 访问器。
- closure env 与 vtable 调用约定。
- slice view（`ptr+len`）与 bounds-check helper。
- 泛型实例化符号命名与跨模块链接规则。

## 3. C ABI / 命名 / 类型映射（合并）

本文定义 Cheng 直出 C 后端的 **ABI 基线**：符号命名、调用约定与类型映射规则。  
目标是保证前端直出 C 的可链接性与跨模块可追溯性，为后续后端实现与验证提供统一口径。

### 3.1 适用范围
- 目标：Cheng 前端直接输出 C（不经旧 IR 链路）。
- 约束：仅规定 **C ABI 与类型映射**，不引入 HRT 约束。
- 依赖：`runtime/include/chengbase.h`（类型别名/调用约定宏）。
- 兼容：符号命名沿用现行 mangling 规则（见 3.3）。

### 3.2 调用约定（v0）
- **ABI**：目标平台的 C ABI（SysV / MSVC / Darwin 由编译器决定）。
- **调用约定宏**：
  - Cheng 定义函数：`C_CHENGCALL(ret, name)`。
  - `importc` 声明：默认 `C_CDECL(ret, name)`（如需其它约定须显式注明）。
- **`importc` varargs**：仅支持在参数末尾使用 `varargs[T]` 或 `varargs` pragma，并生成 `va_list` wrapper；要求至少一个固定参数且 `T` 为标量/指针类型。
  - 自研后端 call lowering 口径：varargs 调用点会显式构建栈参数区并保持 16B 对齐；x86_64 额外设置 `AL=0`（向量寄存器计数）以符合 SysV varargs 规则。
  - AArch64 Darwin 口径：命名溢出参数与 varargs 参数统一按 8 字节槽传递；`varargs[int32]` 在写栈前会做符号扩展。
- **返回值**：
  - 标量/指针按值返回（`NI32/NI64/NF32/NF64/ptr`）。
  - 结构体/tuple/array **不按值返回**；需要时改为 `out` 参数（指针）+ `void` 返回。
- **参数传递**：
  - 标量/指针按值传递。
  - 聚合类型默认 **按指针传递**（`const T*` 或 `T*`），避免 ABI 差异。

> 说明：本规则是 “v0 的稳定可落地口径”，与未来编译器实现对齐后可细化。

### 3.3 符号命名与 mangling
直出 C 后端沿用现行符号编码规则：
- 普通符号：`.` 替换为 `_`，`_` 编码为 `Q_`。
- `importc` 符号（带 `.c` 后缀）**不做 mangling**，直接使用原 C 名。
- 其他特殊字符按既有映射表处理（如 `+`→`plusQ`，`?`→`qmarkQ`）。

示例：
- `foo.1.mod` → `foo_1_mod`
- `bar.c` → `bar`（保持原名）
- `[]` → `getQ`

### 3.4 类型映射（v0）
类型别名来自 `chengbase.h`：

| Cheng 类型 | C 类型 |
| --- | --- |
| `bool` | `NB8` |
| `char` | `NC8` |
| `int8/int16/int32/int64` | `NI8/NI16/NI32/NI64` |
| `uint8/uint16/uint32/uint64` | `NU8/NU16/NU32/NU64` |
| `int/uint` | `NI/NU`（由 `CHENG_INTBITS` 决定） |
| `float32/float64` | `NF32/NF64` |
| `ptr T` | `T*` |
| `ref T` / `var T`（借用语义） | `T*`（ABI 视角） |
| `cstring` | `const char*` |
| `str` | `char*`（UTF‑8，0 结尾；由运行时管理） |

聚合类型映射：
- `object` → `struct`（按字段声明顺序布局）
- `tuple` → `struct`（字段名 `f0/f1/...`）
- `array[T, N]` → `T[N]`
- `seq[T]` → `struct { NI32 len; NI32 cap; void* buffer; }`
- `seq[T, N]`（固定容量，内部实现名 `seq_fixed`）→ `struct { NI32 len; NI32 cap; void* buffer; T storage[N]; }`

> 注：容器与字符串语义依赖运行时 shim；ABI 仅规定布局/指针形态。

**实现注记（v0）**：
- 直出 C 发射器已将 `int/uint` 映射为 `NI/NU`，并在输出中提供 `CHENG_INTBITS` 默认值（32）；可通过编译参数 `-DCHENG_INTBITS=64` 覆盖。
- 类型位置允许模块限定名 `Module.Type`；在 C-Profile 中会消解模块前缀，仅以 `Type` 参与映射与 ABI。

### 3.5 布局与对齐
- 字段顺序与源声明一致，不做重排。
- 对齐与填充遵循 C 编译器默认规则（与 `CHENG_ALIGNOF` 一致）。
- `enum` 在 v0 统一按 `NI32` 表示（不支持自定义宽度）。
- 任何跨模块依赖布局的场景必须使用 `--layout-out` 产物或头文件中显式 `struct` 定义。

#### 3.5.1 struct/tuple/enum 布局与访问规则
**struct/object**
- 直出为 `struct <Name> { ... }`，字段按声明顺序排列。
- 访问规则：值语义用 `.`，指针语义用 `->`。
- 若 `ref/var` 以指针传递，直出 C 必须在访问时使用 `->`。
- 含 `case` 字段的 object 采用 tagged union：先放 tag 字段，随后是匿名 union（每个分支为匿名 struct），便于 `obj.field` 直接访问。

示意（Cheng → C）：
```cheng
type Payload = object
    case kind: Kind
    of kA:
        a: int
    of kB:
        b: int
```

```c
struct Payload {
  Kind kind;
  union { struct { NI a; }; struct { NI b; }; };
};
```

**tuple**
- 直出为 `struct Tuple_<hash> { T0 f0; T1 f1; ... }`。
- 字段命名固定为 `f0/f1/...`，访问映射为 `tuple.f0`。

**enum**
- 支持带 payload 的 sum-type/variant，直出为 tag + union 的 tagged union 结构。
- 无 payload 枚举可退化为 `NI32` 常量集合。
- 访问规则：读取 tag 后分支，payload 通过 union 字段访问。

**最小实现指引**
- 不生成跨编译单元的隐式布局；必须在生成的 `.c/.h` 中显式 `struct`。
- 任何需要字段偏移的场景，直接使用 C 访问语法，不手写 offset。
- 若某类变体尚未落地，给出诊断并标注矩阵任务 ID。

#### 3.5.2 slice view ABI（FULLC.A04）
- 约定类型：`slice_<T>` 直出为 `{ ptr: ptr T; len: int32 }`，字段顺序固定。
- 数组字面量 `[a, b, c]` 直出为 `((slice_<T>){ .ptr = (T[]){a, b, c}, .len = 3 })`，`T` 由元素类型推断。
- 边界检查使用 helper：`cheng_bounds_check(len, idx)`（越界直接退出）。

最小样例：
```cheng
type slice_int = object
    ptr: ptr int
    len: int32

let xs: slice_int = [1, 2, 3]
```

验证：`sh src/tooling/verify_c_backend.sh`（示例见 `examples/c_backend_smoke.cheng`）。

### 3.6 `importc` 规则
- `importc` 函数必须显式声明参数与返回类型，并与 C 头文件一致。
- `importc` 变参通过 `va_list` wrapper 支持（要求至少一个固定参数；`varargs[T]` 仅允许标量/指针类型；并假定导入符号可接收 `va_list`）。
- `importc` 聚合返回自动降级为 **wrapper + out 参数**，调用点使用 out 形式，避免直接按值返回聚合。
- `@packed` / `@align(N)` 仅对 `object` 类型生效，直出 C 会映射为显式属性并插入布局校验（`offsetof/sizeof/alignof`）。

诊断建议：
```
[C-ABI] unsupported: importc-aggregate-return. hint: assign to local and use out-wrapper.
```

**映射路径（落地）**
- FULLC.F01：`importc` 聚合返回 → 生成 wrapper/out 参数并在调用点降级。
- FULLC.F03：`packed/align` → 显式属性映射 + 布局校验（见 §8 与示例）。
- FULLC.F04：回调/函数指针桥接 → trampoline + env 绑定（见 §3.6.3 与示例）。

#### 3.6.1 `importc` 聚合返回降级（FULLC.F01）
Cheng：
```cheng
@importc("ffi_make_packed")
fn makePacked(a: int8, b: int32): PackedPair
```

直出 C（示意）：
```c
N_CDECL(PackedPair, ffi_make_packed)(NI8 a, NI32 b);
static inline void __cheng_importc_out_ffi_make_packed(NI8 a, NI32 b, PackedPair* __out) {
  if (__out != CHENG_NIL) { *__out = ffi_make_packed(a, b); }
}
```

#### 3.6.2 `packed/align` 显式映射（FULLC.F03）
Cheng：
```cheng
@ packed
type PackedPair =
    a: int8
    b: int32

@ align(8)
type AlignedPair =
    a: int8
    b: int32
    c: int64
```

直出 C：结构体带显式 packed/align 属性，并生成 `offsetof/sizeof` 布局校验断言。

#### 3.6.3 `lambda/closure` 回调桥接（FULLC.L07/FULLC.A02/FULLC.F04）
- `fnLiteral` 产生闭包值，`callSuffix` 触发调用；C-profile 在调用点识别回调并改写。
- 降级路径：提升为命名函数 + 生成 env struct + trampoline。
    - env：捕获字段组成 `__cheng_env_N`。
    - body：`fn __cheng_lambda_N(__env: __cheng_env_N*, ...)`。
    - tramp：`fn __cheng_trampoline_N(__ctx: void*, ...)` → cast ctx 后调用 body。
- 匿名函数 vs 函数指针：
    - **匿名函数（closure）**可捕获外部变量，值语义包含环境；C-Profile 中表现为 `env + body + trampoline`。
    - **函数指针**不携带环境，仅代码地址；ABI 更直接，适合 `importc` 回调与 C 交互。
- 当 `importc` 形参是 `fn (void*, ...)` 且紧随 `ctx: void*` 时，调用点自动改写为：
  `(__cheng_trampoline_N, void*(__addr(env)))`；若缺失 ctx，报 FULLC.F04 诊断。
- 若回调不捕获环境，可直接传函数指针；否则必须通过 `ctx` 携带环境（由 C-Profile 自动降级）。

最小样例：
```cheng
@importc("call_cb")
fn callCb(cb: fn(cbCtx: void*, v: int32): int32, ctx: void*, v: int32): int32

fn main(): int32 =
    let base: int32 = 40
    let cb = fn(v: int32): int32 =
        return v + base
    let out: int32 = callCb(cb, nil, 2)
    return out != 42 ? 1 : 7
```

**最小 FFI 样例 + 验证**
- 样例：`examples/c_backend_importc_ffi.cheng` + `examples/c_backend_importc_ffi.c`
- 验证：`sh src/tooling/verify_c_backend_importc_ffi.sh`
- 样例：`examples/c_backend_closure_callback.cheng` + `examples/c_backend_closure_callback.c`
- 验证：`sh src/tooling/verify_c_backend_closure_callback.sh`

### 3.7 最小示例（C 侧签名）

Cheng：
```cheng
fn add(a: int32, b: int32): int32 =
    return a + b
```

直出 C（示意）：
```c
#include "chengbase.h"
C_CHENGCALL(NI32, add_0_mod)(NI32 a, NI32 b) {
  return a + b;
}
```

### 3.8 版本策略
- v0：最小 ABI 口径（本文）。
- v1：明确 `importc` 调用约定注解、聚合按值 ABI（可选）与更完整运行时结构。
- v2：与 C 后端实现对齐并加入验证脚本/示例。

### 3.9 C+ASM 混合后端（已移除）

`--backend:hybrid` 与 ASM/HRT 相关入口已从生产工具链移除。  
当前仅保留：
- backend-only 主链路：`chengc.sh --backend:obj`（默认）
- C 兼容回退链路：`chengc.sh --backend:c`

## 4. 运行时 C shim 接口（合并）

本文定义直出 C 后端在 v0 阶段依赖的 **运行时 C shim 接口**。  
接口分为“核心必需”和“可选扩展”，用于指导运行时链接与错误诊断。

### 4.1 适用范围
- 目标：Cheng → C 直出后，可由 C 编译器链接生成可执行文件/库。
- 约束：不强制硬实时；仅要求符号可链接与语义稳定。
- 依赖：`src/stdlib/bootstrap/system.cheng` 中的 `importc` 声明与 `src/stdlib/bootstrap/system_helpers.c` 的实现。

### 4.2 核心必需接口（v0）
这些符号被 **语言核心与 std/bootstrap** 直接使用，缺失会导致链接失败。

#### 4.2.1 内存分配与拷贝
| 符号 | C 签名 | 说明 |
| --- | --- | --- |
| `cheng_malloc` | `void* cheng_malloc(int32_t size)` | 分配字节数 |
| `cheng_free` | `void cheng_free(void* p)` | 释放指针 |
| `cheng_realloc` | `void* cheng_realloc(void* p, int32_t size)` | 重新分配 |
| `cheng_memcpy` | `void* cheng_memcpy(void* dest, void* src, int64_t n)` | 复制内存 |
| `cheng_memset` | `void* cheng_memset(void* dest, int32_t val, int64_t n)` | 填充内存 |
| `cheng_memcmp` | `int32_t cheng_memcmp(void* a, void* b, int64_t n)` | 比较内存 |
| `ptr_add` | `void* ptr_add(void* p, int32_t offset)` | 指针偏移 |

#### 4.2.2 引用计数与原子
| 符号 | C 签名 | 说明 |
| --- | --- | --- |
| `cheng_mem_retain` | `void cheng_mem_retain(void* p)` | 非原子 retain |
| `cheng_mem_release` | `void cheng_mem_release(void* p)` | 非原子 release |
| `cheng_mem_refcount` | `int32_t cheng_mem_refcount(void* p)` | 读取 refcount |
| `cheng_mem_retain_atomic` | `void cheng_mem_retain_atomic(void* p)` | 原子 retain |
| `cheng_mem_release_atomic` | `void cheng_mem_release_atomic(void* p)` | 原子 release |
| `cheng_mem_refcount_atomic` | `int32_t cheng_mem_refcount_atomic(void* p)` | 原子 refcount |
| `cheng_atomic_cas_i32` | `int32_t cheng_atomic_cas_i32(int32_t* p, int32_t expect, int32_t desired)` | CAS |
| `cheng_atomic_store_i32` | `void cheng_atomic_store_i32(int32_t* p, int32_t val)` | store |
| `cheng_atomic_load_i32` | `int32_t cheng_atomic_load_i32(int32_t* p)` | load |
| `cheng_mm_retain_count` | `int64_t cheng_mm_retain_count(void)` | 计数器 |
| `cheng_mm_release_count` | `int64_t cheng_mm_release_count(void)` | 计数器 |
| `cheng_mm_diag_reset` | `void cheng_mm_diag_reset(void)` | 诊断复位 |

#### 4.2.3 作用域/区域（ORC）
| 符号 | C 签名 | 说明 |
| --- | --- | --- |
| `cheng_mem_scope_push` | `void* cheng_mem_scope_push(void)` | 进入作用域 |
| `cheng_mem_scope_pop` | `void cheng_mem_scope_pop(void)` | 离开作用域 |
| `cheng_mem_scope_escape` | `void cheng_mem_scope_escape(void* p)` | 逃逸到上层 |
| `cheng_mem_scope_escape_global` | `void cheng_mem_scope_escape_global(void* p)` | 逃逸到全局 |

#### 4.2.4 字符串/基础运算
| 符号 | C 签名 | 说明 |
| --- | --- | --- |
| `cheng_strlen` | `int32_t cheng_strlen(const char* s)` | 字符串长度 |
| `cheng_strcmp` | `int32_t cheng_strcmp(const char* a, const char* b)` | 字符串比较 |
| `mul_0` | `int32_t mul_0(int32_t a, int32_t b)` | `*` helper |
| `div_0` | `int32_t div_0(int32_t a, int32_t b)` | `/` helper |
| `mod_0` | `int32_t mod_0(int32_t a, int32_t b)` | `%` helper |
| `shl_0` | `int32_t shl_0(int32_t a, int32_t b)` | `<<` helper |
| `shr_0` | `int32_t shr_0(int32_t a, int32_t b)` | `>>` helper |
| `bitand_0` | `int32_t bitand_0(int32_t a, int32_t b)` | `&` helper |
| `bitor_0` | `int32_t bitor_0(int32_t a, int32_t b)` | `|` helper |
| `xor_0` | `int32_t xor_0(int32_t a, int32_t b)` | `^` helper |
| `bitnot_0` | `int32_t bitnot_0(int32_t a)` | `~` helper |
| `not_0` | `bool not_0(bool a)` | `!` helper |
| `cheng_bounds_check` | `void cheng_bounds_check(int32_t len, int32_t idx)` | 索引边界检查 |
| `cheng_bits_to_f32` | `double cheng_bits_to_f32(int32_t bits)` | f32 位模式 |
| `cheng_f32_to_bits` | `int32_t cheng_f32_to_bits(double value)` | f32 位模式 |

#### 4.2.5 容器索引辅助
| 符号 | C 签名 | 说明 |
| --- | --- | --- |
| `cheng_seq_get` | `void* cheng_seq_get(void* buffer, int32_t len, int32_t idx, int32_t elem_size)` | seq 索引取址（读） |
| `cheng_seq_set` | `void* cheng_seq_set(void* buffer, int32_t len, int32_t idx, int32_t elem_size)` | seq 索引取址（写） |
| `cheng_slice_get` | `void* cheng_slice_get(void* ptr, int32_t len, int32_t idx, int32_t elem_size)` | slice 索引取址（读） |
| `cheng_slice_set` | `void* cheng_slice_set(void* ptr, int32_t len, int32_t idx, int32_t elem_size)` | slice 索引取址（写） |

### 4.3 可选扩展接口（按模块启用）
仅当使用对应标准库模块时才需要提供。

#### 4.3.1 `std/os` 与文件 I/O
`cheng_fopen/cheng_fclose/cheng_fread/cheng_fwrite/cheng_fflush/cheng_fgetc`  
`get_stdin/get_stdout/get_stderr`  
`cheng_file_exists/cheng_dir_exists/cheng_mkdir1/cheng_file_mtime/cheng_file_size`  
`cheng_getcwd/cheng_list_dir/rename/remove/getenv`  
`cheng_exec_cmd_ex`

#### 4.3.2 `std/times` / `std/monotimes`
`cheng_epoch_time`、`cheng_monotime_ns`

#### 4.3.3 `std/bytes`
`cheng_jpeg_decode`、`cheng_jpeg_free`

#### 4.3.4 PTY/进程与管道
`cheng_pty_is_supported/cheng_pty_spawn/cheng_pipe_spawn`  
`cheng_pty_read/cheng_fd_read/cheng_fd_read_wait`  
`cheng_pty_write/cheng_pty_close/cheng_pty_wait`

#### 4.3.5 HRT/RTOS 扩展（已移除）
HRT/RTOS 扩展不再属于当前生产工具链支持范围。

#### 4.3.6 协程/并发运行时（FULLC.R03/FULLC.R04）
`scheduler/awaitable` shim：  
`cheng_spawn`、`cheng_sched_run`、`cheng_sched_run_once`、`cheng_sched_pending`  
`cheng_async_pending_i32`、`cheng_async_ready_i32`、`cheng_async_set_i32`、`cheng_await_i32`  
`cheng_async_pending_void`、`cheng_async_ready_void`、`cheng_async_set_void`、`cheng_await_void`

`channel` shim：  
`cheng_chan_i32_new`、`cheng_chan_i32_send`、`cheng_chan_i32_recv`

说明：
- v0 为 **单线程队列调度**（无 OS 线程）；`send/recv` 满/空时返回 0，可配合 scheduler 轮询。
- 类型/导入：`std/async_rt` 提供 `await_i32/await_void` 与 `chan_i32` ABI 对齐定义与 importc 声明。
- `chanI32Recv` 在 Cheng 侧使用 `out: var int32`（调用写作 `chanI32Recv(ch, v)`）；编译器会自动取址并对齐到 C shim 的 `int32_t* out`。
- 跨线程共享需使用 `share_mt`/`mem_retain_atomic`（参见 §0.3 多线程内存安全）。
- v0 协程为轮询式状态机：lowering 将 `await` 点拆分为状态跳转 + `spawn` 轮询驱动。
- await 仅支持语句级（`let/var` 绑定、赋值、return）；表达式内 await 仍报 not-mapped。
- awaitable 目前仅覆盖 `int32/void`，其余类型待扩展。

### 4.4 ARC/ORC 映射策略
直出 C 后端需要遵循以下 **内存管理映射**：

#### 4.4.1 ARC（引用计数）
- **retain/release**：所有权转移点插入 `cheng_mem_retain/Release`。
- **原子 RC**：跨线程共享或 `share_mt/Arc` 使用 `cheng_mem_retain_atomic/Release_atomic`。
- **禁用模式**：`CHENG_MM=off` 时 retain/release 为 no-op，`refcount` 返回 `0`。
- **调试**：`CHENG_MM_DIAG=1` 时打印 retain/release 轨迹。

#### 4.4.2 ORC（区域/作用域）
- **作用域**：进入函数或语义作用域时调用 `cheng_mem_scope_push`，离开时 `cheng_mem_scope_pop`。
- **逃逸**：值从作用域逃逸时调用 `cheng_mem_scope_escape`；逃逸到全局使用 `cheng_mem_scope_escape_global`。
- **混用规则**：同一对象不得同时走原子与非原子路径；编译器需统一选择。

> 说明：以上策略与 `system_helpers.c` 中的 RC/Scope 语义一致；实现可替换但需保持等价行为。

### 4.5 诊断建议
运行时符号缺失或类型不匹配时，建议统一诊断格式：

```
[C-Runtime] missing symbol: <name>. hint: link system_helpers.c or provide shim.
```

示例：
- `[C-Runtime] missing symbol: cheng_memcpy. hint: link src/stdlib/bootstrap/system_helpers.c.`
- `[C-Runtime] missing symbol: cheng_fopen. hint: enable std/os shim or avoid file IO.`

## 5. 诊断口径（建议）
未完成映射或降级失败采用统一模板，附矩阵任务 ID 便于追踪：

```
[C-Profile] not-mapped: <feature> (<task_id>). hint: <rewrite>.
```

工程约束：
- not-mapped/unsupported 需在前端/降级阶段直接产出诊断，避免进入后端再报错或静默错误代码生成。
- 诊断需附可行的 rewrite/降级提示，确保用户可在源层面修复。
- 默认初始化仅允许隐式：`default[T]` 仅用于表达式/返回/实参位置，禁止在 `let/var/const` 绑定中显式写 `= default[T]`。

示例：
- `[C-Profile] not-mapped: async return type (FULLC.L08). hint: only int32/void awaitable in v0.`
- `[C-Profile] not-mapped: case/of (FULLC.L01). hint: lower to if/elif with tag checks.`

## 6. 降级/重写示例

### 6.1 `defer`/`do`/`yield` 降级要点（FULLC.L05）
- `defer` 收集为当前作用域的 cleanup 列表（LIFO）；在 `return/break/continue` 与作用域自然结束处插入 cleanup 调用。
- `do` 视为“最后一个实参”的语法糖：改写为显式例程字面量并提升；无捕获直接提升为顶层 `fn`，有捕获则走 FULLC.L07 的 env 降级。
- `yield` 仅存在于 `iterator`；lowering 生成 `*_iter` frame（`state` + locals）与 `*_open/*_next/*_close` 例程，`yield` 变为写出值 + 保存 `state` + `return true`，耗尽返回 `false`。
- iterator body 支持 `if/while` 与简单 `let/var` 绑定；复杂控制流或嵌套 `for` 触发 not-mapped 诊断。
- iterator 内 `defer` 限制在顶层并提升到 `*_close`；`for-in` 自动包 `defer: *_close(frame)`，保证提前退出清理。

### 6.2 `for-in`/iterator 降级要点（FULLC.L06）
- `for-in` 对 `range/seq/str` 走索引循环：先取 `len`，再用索引变量递增读取元素；容器访问依赖 FULLC.R01 shim。
- `for-in` 对 iterator：`*_open` 生成 frame，反复调用 `*_next(frame, out)`；外层包 `defer` 调用 `*_close`，tuple yield 对应多 pattern 绑定。
- 迭代路径与元素类型转换需在 lowering 阶段显式化。
- 无可用迭代路径时，报 `[C-Profile] not-mapped` 并标注 FULLC.L06。

**示例 1：`?` 降级**
```cheng
fn main(): int32 =
    let res = fetch()
    if res.err != 0:
        return res.err
    return res.ok
```

**示例 2：`case` 降级**
```cheng
fn kindName(k: Kind): str =
    if k == kA:
        return "a"
    elif k == kB:
        return "b"
    return "?"
```

**示例 3：`defer`/`do`/`yield` 降级**
```cheng
fn readOne(path: str): str =
    let file = open path
    fn __cleanup_0(): void =
        close file
    let line = readLine file
    __cleanup_0()
    return line

fn __do_0(accum: int32, value: int32): int32 =
    return accum + value

let total = reduce(numbers, __do_0)
```

```cheng
type items_iter = object
    state: int32
    index: int32
    items: seq[int32]

fn items_open(items: seq[int32]): items_iter =
    var frame: items_iter
    frame.state = 0
    frame.index = 0
    frame.items = items
    return frame

fn items_next(frame: var items_iter, out: var int32): bool =
    if frame.state == 0:
        if frame.index < len frame.items:
            out = frame.items[frame.index]
            frame.index = frame.index + 1
            return true
        frame.state = -1
        return false
    return false

fn items_close(frame: var items_iter): void =
    frame.state = -1
```

**示例 4：`for-in`/iterator 降级**
```cheng
type Bag = object
    entries: seq[int32]

iterator items(bag: Bag): int32 =
    var i = 0
    while i < len bag.entries:
        yield bag.entries[i]
        i = i + 1

fn sumIter(items: seq[int32]): int32 =
    var total: int32 = 0
    var __iter_frame = items_open items
    defer:
        items_close __iter_frame
    var __iter_value: int32
    while items_next(__iter_frame, __iter_value):
        let x = __iter_value
        total = total + x
    return total
```

### 6.3 `case/when/of` 降级规则（FULLC.L01）
- `caseStmt`/`caseExpr` 先物化 selector：`let _case_sel = <expr>`，确保副作用仅发生一次。
- `caseBranch` 逐分支展开为 `if/elif` 链；`caseArm` 内多 entry 视为同一分支的 **或** 条件。
- `caseEntry` 为表达式时，匹配条件为 `_case_sel == <expr>`；为 `pattern` 时走 `lower_pattern_match`。
- tagged union 的 `pattern`：先做 tag 比较（如 `_case_sel.tag == TagX`），再按 payload 绑定字段；guard 在绑定后判定。
- `of <pattern> if <guard>:` 降级为 **先匹配 → 建立绑定 → 再判 guard**；guard 为 false 时继续下一分支。
- `whenStmt` 直接降级为等价的 `if/elif/else` 链；`whenExpr`/`caseExpr` 使用临时结果变量并在分支内赋值。

**示例 3：`case/when/of` + guard 降级（最小样例）**
源：
```cheng
type Kind = enum:
    KindA
    KindB

fn kindScore(kind: Kind, score: int32): int32 =
    case kind:
        of KindA if score > 0:
            return 1
        of KindB:
            return 2
        else:
            return 0
```

降级后（示意）：
```cheng
fn kindScore(kind: Kind, score: int32): int32 =
    let _case_sel = kind
    if _case_sel == KindA:
        if score > 0:
            return 1
    elif _case_sel == KindB:
        return 2
    return 0
```

### 6.4 解构/模式绑定降级（FULLC.L02）
- `let/var/const` 的 `pattern` 统一改写为：先物化 RHS 临时变量，再逐字段读取。
- tuple 模式 `(<p0>, <p1>, ...)` 读取 `f0/f1/...`；object 模式 `Type(field: pat, ...)` 读取 `.<field>`。
- seq 模式 `@[p0, p1, ...]` 先做长度检查，再通过运行时索引读取并递归匹配子模式。
- object pattern 的**位置参数**仅在对象**无继承、无 recCase**时允许；否则必须使用具名字段。
- set pattern `{...}` 目前仍为 not-mapped（FULLC.L02）。
- `_` 不产生绑定；literal pattern 在绑定上下文降级为断言/诊断（不匹配即失败）。
- case/for 等位置复用同一套解构展开规则，保证绑定只在分支/迭代体内可见。

**示例 5：解构/模式绑定降级（最小样例）**
源：
```cheng
type Point = object:
    x: int32
    y: int32

fn pointSum(point: Point): int32 =
    let Point(x: xCoord, y: yCoord) = point
    return xCoord + yCoord
```

降级后（示意）：
```cheng
fn pointSum(point: Point): int32 =
    let _pat_src = point
    let xCoord = _pat_src.x
    let yCoord = _pat_src.y
    return xCoord + yCoord
```

### 6.5 具名/默认参数降级（FULLC.L03）
- 具名参数调用统一生成 wrapper，并将调用点降级为位置参数。
- wrapper 形参顺序按**调用时参数顺序**展开，内部补齐缺省参数。
- 缺省参数在 wrapper 内显式 `let` 绑定，保证语义稳定可映射到 C。

**示例 6：具名/默认参数降级（最小样例）**
源：
```cheng
fn sum3(a: int32, b: int32 = 2, c: int32 = 3): int32 =
    return a + b + c

fn demo(): int32 =
    return sum3(c=10, a=1)
```

降级后（示意）：
```cheng
fn sum3__wrap_2_0(c: int32, a: int32): int32 =
    let b = 2
    return sum3(a, b, c)

fn demo(): int32 =
    return sum3__wrap_2_0(10, 1)
```

### 6.6 `?` 错误传播降级（FULLC.L04）
- `expr?` 先落地为临时 `Result` 变量，再做 `ok` 检查。
- 同 `Result` 返回类型下，Err 分支直接 `return`（早退）；非 `Result` 返回则显式 `panic`。
- Ok 分支使用 `.value` 替换原表达式，保证表达式语义一致。

**示例 7：`?` 错误传播降级（最小样例）**
源：
```cheng
fn parseNum(text: str): Result[int32] =
    if text == "0":
        return Ok(0)
    return Err("bad")

fn parsePlusOne(text: str): Result[int32] =
    let n = parseNum(text)?
    return Ok(n + 1)
```

降级后（示意）：
```cheng
fn parsePlusOne(text: str): Result[int32] =
    var __qres0: Result[int32] = parseNum(text)
    if !__qres0.ok:
        return __qres0
    let n = __qres0.value
    return Ok(n + 1)
```

### 6.7 泛型单态化与 trait/concept 调度（FULLC.G01/FULLC.D01/FULLC.A03）
- 单态化：以类型实参 `typeKey` 生成实例名，使用实例化缓存避免重复生成。
- 约束检查：在实例化阶段对 `T: Trait/Concept` 做结构性匹配（`Self` → 实参类型）。
- vtable ABI：为每个 `(Trait/Concept + Self + 约束类型实参)` 生成 vtable 结构与 `__init` 构造函数。
- 调度重写：约束上下文内 `x.Method(...)` 重写为 `VTable__init().Method(x, ...)`。

**示例 8：泛型 + trait 调度（最小样例）**
源：
```cheng
trait Drawable:
    fn Draw(self: Self): int32

fn Draw(self: int32): int32 =
    return self + 1

fn Render[T: Drawable](x: T): int32 =
    return x.Draw()
```

降级后（示意）：
```cheng
type Drawable__vtbl_int32 = object:
    Draw: fn(self: int32): int32

fn Drawable__vtbl_int32__init(): Drawable__vtbl_int32 =
    var __vt: Drawable__vtbl_int32
    __vt.Draw = Draw
    return __vt

fn Render_int32(x: int32): int32 =
    return Drawable__vtbl_int32__init().Draw(x)
```

## 7. 版本与扩展路线
- Phase 0：稳定 C 直出链路与 ABI/运行时基线。
- Phase 1：完成核心降级重写（case、`?`、defer、具名参数、迭代器）。
- Phase 2：泛型单态化、trait/closure 与布局扩展。
- Phase 3：async/并发原语与运行时支撑。
- Phase 4：性能/诊断/混合后端的工程化完善。

## 8. 全规范映射正交原子任务矩阵（FULLC）

适用目标：**正式规范语法稳定映射到 C**。矩阵以降级/重写 + 运行时支撑为核心，所有任务保持原子与正交。

**适用范围（重要）**
- 本矩阵仅针对 **stage1 的 FULLC 路径**（`chengc.sh --backend:c` + `CHENG_C_FRONTEND=./stage1_runner`）。
- stage0c/ASM/HRT 入口已移除；生产链路以 backend-only（默认）与 stage1 C 回退为准。
- “完成”表示核心降级/运行时 **主路径可用**，不代表所有子集已覆盖；仍有明确 not‑mapped 约束（见 §6 与诊断示例）。

**验收建议（可操作）**
- `sh src/tooling/verify_c_backend_fullspec.sh`（C 后端 fullspec 子集）
- `sh src/tooling/verify_c_backend.sh`
- `sh src/tooling/verify_c_backend_async_runtime.sh`
- 以固定 seed 复跑并比对产物哈希（若有）

**合并前验收清单（执行顺序建议）**
1) 基线回归：
   - `sh src/tooling/verify_c_backend.sh`
2) C 后端 fullspec 回归：
   - `sh src/tooling/verify_c_backend_fullspec.sh`
3) async/并发回归：
   - `sh src/tooling/verify_c_backend_async_runtime.sh`
4) 自举链路（若已接入）：
   - 以 stage1 C‑Profile 产出 stage1 runner，并验证 seed/确定性（产物哈希一致）。
5) 差异比对：
   - 同一输入在合并前后产物一致；若不一致，记录差异白名单与原因。

**判定标准**
- 所有回归脚本退出码为 0。
- 关键 not‑mapped/unsupported 诊断数量稳定且有文档化例外说明。

命名：`FULLC.<Axis>-NN`（Axis = `L` 降级/重写、`G` 泛型、`D` 调度、`A` ABI/布局、`F` FFI、`R` 运行时、`T` 工具链、`V` 验证）。

| 任务 ID | 轴 | 任务说明 | 产出 | 验收 | 依赖 | 进度 |
| :--- | :--- | :--- | :--- | :--- | :--- | :--- |
| FULLC.L01 | L | `case/when/of` 模式匹配降级 | `lower_case` → `if/elif` + tag/guard | case/guard 样例通过 | FULLC.A01 | 完成 |
| FULLC.L02 | L | 解构/模式绑定降级 | 展开为临时变量 + 字段读取 | 解构样例通过 | FULLC.A01 | 完成 |
| FULLC.L03 | L | 具名/默认参数降级 | 生成 wrapper + 位置参数调用 | 具名/默认参数样例通过 | — | 完成 |
| FULLC.L04 | L | `?` 错误传播降级 | 插入显式错误检查与早退 | `?` 样例通过 | FULLC.R02 | 完成 |
| FULLC.L05 | L | `defer`/`do`/`yield` 降级 | cleanup 块或显式状态机 | 清理/延迟执行样例通过 | — | 完成 |
| FULLC.L06 | L | `for-in`/iterator 降级 | 索引循环或迭代器状态机 | 遍历样例通过 | FULLC.R01 | 完成 |
| FULLC.L07 | L | `lambda`/closure 降级 | 提升为命名函数 + 捕获环境结构 | 闭包捕获样例通过 | FULLC.A02 | 完成 |
| FULLC.L08 | L | `async/await` 降级 | 协程状态机 + await 点拆分 | async 样例通过 | FULLC.R03 | 完成 |
| FULLC.G01 | G | 泛型单态化（C 后端） | 实例化缓存/符号命名规则 | 泛型样例通过 | FULLC.T01 | 完成 |
| FULLC.D01 | D | trait/concept 调度 | vtable 生成与调用重写 | trait 调用样例通过 | FULLC.A03 | 完成 |
| FULLC.A01 | A | tagged union 布局 | tag + union 布局与访问器 | sum-type 布局测试通过 | — | 完成 |
| FULLC.A02 | A | closure env ABI | env struct + call 约定 | 闭包 ABI 测试通过 | — | 完成 |
| FULLC.A03 | A | vtable ABI | vtable 布局/函数指针约定 | trait ABI 测试通过 | — | 完成 |
| FULLC.A04 | A | slice view ABI | `{ptr,len}` + bounds helper | slice ABI 测试通过 | — | 完成 |
| FULLC.F01 | F | `importc` 聚合返回降级 | 自动生成 wrapper + out 参数 | importc 聚合返回样例通过 | FULLC.A04 | 完成 |
| FULLC.F02 | F | varargs 适配 | va_list wrapper + 受限签名 | importc varargs 样例通过 | — | 完成 |
| FULLC.F03 | F | packed/align 显式映射 | 布局校验 + 显式属性 | packed 结构体样例通过 | FULLC.A01 | 完成 |
| FULLC.F04 | F | 回调/函数指针桥接 | trampoline + env 绑定 | C 回调样例通过 | FULLC.A02 | 完成 |
| FULLC.R01 | R | 容器索引/切片/迭代器运行时 | `cheng_seq_get/set` 等 shim | 容器 API 回归通过 | — | 完成 |
| FULLC.R02 | R | `Option/Result/Error` 运行时 | 错误结构与 helper | 错误处理样例通过 | — | 完成 |
| FULLC.R03 | R | 协程运行时 | scheduler/awaitable API | async 回归通过 | — | 完成 |
| FULLC.R04 | R | channel/spawn 运行时 | 线程/队列 API shim | 并发样例通过 | — | 完成 |
| FULLC.T01 | T | 全规范 C 流水线开关 | `--backend:c` + stage1_runner | 完整 lowering 路径可编译 | — | 完成 |
| FULLC.V01 | V | C 后端 fullspec 回归集 | `verify_c_backend_fullspec.sh` | 回归集全绿 | FULLC.T01 | 完成 |

## 8.1 Codex 任务提示词（归档）

当前无待办；以下为已完成记录，供追溯。

**Prompt A（FULLC.L08 async/await 降级，已完成）**
已实现 `async/await` 降级为状态机：拆分 `await` 点、生成状态枚举/存储结构、驱动函数，并与 FULLC.R03 运行时接口对接。验收：async 最小样例可通过 C 后端编译。
