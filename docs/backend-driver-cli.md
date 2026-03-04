# Backend Driver CLI（Canonical-Only）

## 目标

- 全生态统一单一 backend driver：`artifacts/backend_driver/cheng`。
- 所有仓库统一通过 `cheng_tooling` 入口调用，不直接探测或执行 driver 二进制。
- 禁止环境变量覆盖 driver：`BACKEND_DRIVER` / `CHENG_BACKEND_DRIVER` 非空即配置错误。

## canonical 查询

```sh
TOOLING=artifacts/tooling_cmd/cheng_tooling

$TOOLING toolchain-root
$TOOLING driver-path
$TOOLING driver-path --path-only
```

`driver-path` 输出字段：

- `toolchain_root`
- `driver_path`
- `driver_sha256`
- `driver_kind=canonical`

说明：

- `driver-path` 为“只读定位”命令，只校验 canonical 文件是否存在，不依赖 stage0 健康探针。
- `selfhost/bootstrap/100ms/build-backend-driver/backend_prod_closure` 不再接受 `--stage0` 或 `--seed*` driver 覆盖参数。
- `BACKEND_DRIVER` / `CHENG_BACKEND_DRIVER` 在 `cheng_tooling` 启动即硬错误（配置错误，`rc=2`）。

## 编译入口（dev-only + 显式发布入口）

```sh
TOOLING=artifacts/tooling_cmd/cheng_tooling

$TOOLING cheng --in:examples/hello.cheng --out:artifacts/chengc/hello.dev
$TOOLING release-compile --in:examples/hello.cheng --out:artifacts/chengc/hello.rel
$TOOLING backend-prod-publish --dst:dist/releases
```

说明：

- `cheng` 是 canonical 编译入口（dev 轨、内存直出 exe）；`compile/chengc` 已移除并返回配置错误（rc=2）。`--release` 也会返回配置错误（rc=2）。
- `release-compile` 是唯一 release 编译入口（`system-link + runtime C`）。
- `cheng/release-compile` 均不接受 `--linker:*`，也不接受 `BACKEND_LINKER` 环境覆盖；链接器由命令轨道固定（dev=self，release=system）。
- `backend_prod_closure` 固定 dev gate 收口；发布尾链改由 `backend-prod-publish` 执行。
- `emit=obj` 不属于对外口径；公共入口固定 `emit=exe`。

## UE/orphan 清理（跨工作区）

```sh
TOOLING=artifacts/tooling_cmd/cheng_tooling

$TOOLING stage0-ue-status --all
$TOOLING stage0-ue-clean --global
```

## required 稳定性 gate（新增）

```sh
TOOLING=artifacts/tooling_cmd/cheng_tooling

$TOOLING verify_backend_symbol_closure
$TOOLING verify_backend_release_compile_stability
$TOOLING verify_backend_zero_script_residual
```

说明：

- `verify_backend_symbol_closure`：阻断 dev 产物运行期 `_alloc/_c_strlen/_zeroMem` 未定义符号。
- `verify_backend_release_compile_stability`：固定 `return_new_ref_seq_growth`，默认 30 次 release-compile 稳定运行，`rc=139` 直接失败。
- `verify_backend_zero_script_residual`：阻断 required 路径 compile-only/skip 语义与 legacy `CHENG_*` 执行路径读取残留。

## 移除项

- legacy release-driver builder：已移除。
- `driver-path --stage0`：已移除。
- legacy stage0 fallback：已移除。
