# Cheng v2 Host Bootstrap

This directory holds the independent host bootstrap for `v2/`.

It is intentionally separate from the current Cheng compiler because the
existing compile chain is not reliable enough to bootstrap the new tree.

Policy:

- this bootstrap does not delegate to the current Cheng compiler
- it only validates the `v2/` tree and emits deterministic bootstrap metadata
- language semantics remain defined by the Cheng sources and docs, not by this C tool
- it now also includes `cheng_v2c`, a host bootstrap compiler for the minimal
  Unimaker surface slice under `v2/examples/`

Typical usage:

```bash
cd v2/bootstrap
make
make selfhost
../artifacts/bootstrap/cheng_v2_bootstrap check-tree ..
../artifacts/bootstrap/cheng_v2_bootstrap manifest ..
../artifacts/bootstrap/cheng_v2c pipeline ../examples/unimaker_robot_node.cheng
../artifacts/bootstrap/cheng_v2c hash-pipeline ../examples/unimaker_robot_node.cheng
../artifacts/bootstrap/cheng_v2c emit-c ../examples/unimaker_robot_node.cheng ../artifacts/bootstrap/unimaker_robot_node.c
../artifacts/bootstrap/cheng_v2c emit-stage1 ../examples/unimaker_robot_node.cheng ../artifacts/bootstrap/unimaker_stage1_bootstrap.generated.cheng
../artifacts/bootstrap/cheng_v2c selfhost-check ../examples/unimaker_robot_node.cheng
../artifacts/bootstrap/cheng_v2c resolve-manifest --root ../examples
../artifacts/bootstrap/cheng_v2c resolve-manifest --in ../src/tooling/cheng_v2.cheng
../artifacts/bootstrap/cheng_v2c publish-rule-pack --in ../examples/network_distribution_module.cheng
../artifacts/bootstrap/cheng_v2c lsmr-address --cid 499128e86e4bf16650ba638299a0f91638a6c5edcc0f3e2674d4785b89632900 --depth 4
../artifacts/bootstrap/cheng_v2c lsmr-route-plan --cid 499128e86e4bf16650ba638299a0f91638a6c5edcc0f3e2674d4785b89632900 --depth 4 --priority urgent --dispersal none
../artifacts/bootstrap/cheng_v2c release-compile --in ../examples/network_distribution_module.cheng --emit exe --target arm64-apple-darwin
../artifacts/bootstrap/cheng_v2c outline-parse --in ../src/tooling/cheng_v2.cheng
../artifacts/bootstrap/cheng_v2c obj-file --in ../examples/network_distribution_module.cheng --target arm64-apple-darwin
../artifacts/bootstrap/cheng_v2c system-link-plan --in ../src/tooling/cheng_v2.cheng --emit exe --target arm64-apple-darwin
../artifacts/bootstrap/cheng_v2c system-link-exec --in ../src/tooling/cheng_v2.cheng --emit exe --target arm64-apple-darwin --out ../artifacts/bootstrap/cheng_v2_compiler_core
../artifacts/bootstrap/cheng_v2c release-compile --in ../src/tooling/cheng_v2.cheng --emit exe --target arm64-apple-darwin
../artifacts/bootstrap/cheng_v2c tooling-selfhost-host --out-dir ../artifacts/full_selfhost/tooling_selfhost
../artifacts/bootstrap/cheng_v2c stage-selfhost-host --out-dir ../artifacts/full_selfhost
../artifacts/bootstrap/cheng_v2c tooling-selfhost-check --root ../examples --in ../examples/network_distribution_module.cheng
make tooling-selfhost
make full-selfhost
make lsmr-contracts
make obj-file-layout
make compiler-core-release
make compiler-core-system-link
make compiler-core-system-link-exec
```

`resolve-manifest --in <compiler_core_source>` and
`publish-source-manifest --in <compiler_core_source>` now resolve the formal
import-closure manifest rooted at the repository workspace. For
`cheng_tooling_v2.cheng` this means the checked artifact covers both
`v2/src/...` and `src/std/...`, not a single-file or parent-directory manifest.

`make selfhost` is the current fixed bootstrap proof entry. It enforces:

- stage0, generated stage1, and generated stage2 pipeline hashes must reach a fixed point
- generated stage1 source must match generated stage2 source
- generated stage1 source must match `src/bootstrap/unimaker_stage1_bootstrap.cheng`
- checked-in stage1 source must `emit-c -> compile -> run` with stable output
- the tracked v2 bootstrap tree must still satisfy `check-tree` and `manifest`

`make tooling-selfhost` is the second fixed bootstrap proof entry for the
network/tooling slice. It enforces:

- `resolve-manifest`, `publish-source-manifest`, `publish-rule-pack`,
  `publish-compiler-rule-pack`, and `release-compile` must emit the exact
  checked-in deterministic artifacts under `tests/contracts/`
- `verify-network-selfhost` must emit the exact checked-in network closure report
- generated tooling stage1 source must match `src/bootstrap/tooling_stage1_bootstrap.cheng`
- `tooling-selfhost-check` must prove `stage0 == stage1 == stage2` over
  `source_manifest_cid`, `rule_pack_cid`, `compiler_rule_pack_cid`,
  `release_compile_key`, `lsmr_addressing_mode`, `lsmr_address_binding`,
  `lsmr_distance_metric`, `lsmr_route_plan_model`, and the fixed-point stage hash
- `release-compile` must keep `addressing / depth / address_binding /
  distance_metric / canonical_multipath_set` inside the checked compile key

`make lsmr-contracts` freezes the public stage0 LSMR contract artifacts:

- `lsmr-address`
- `lsmr-route-plan`

`make obj-file-layout` freezes the current byte-level Mach-O object-file layout
for `examples/network_distribution_module.cheng` and blocks drift in the stage0
writer surface.

`make compiler-core-system-link` freezes the current deterministic
`system-link-plan` for `src/tooling/cheng_v2.cheng`. It must keep the
plan-stage blocker honest and explicit: `system_link_execution_missing` is only
allowed to survive here because execution is proven by a separate gate.

`make compiler-core-system-link-exec` freezes the real host linker execution
for `src/tooling/cheng_v2.cheng`. It must:

- materialize the primary Mach-O object and all runtime provider objects on the
  formal `Machine -> ObjFile` path
- resolve the Darwin SDK via `xcrun --sdk <sdk> --show-sdk-path`
- invoke the deterministic host linker (`ld64` or `libtool`)
- produce a checked-in stable report under `tests/contracts/`
- produce a real host executable rather than a plan-only placeholder
- launch the produced executable with `status` and a real `release-compile`
  path, and freeze that native smoke output under
  `tests/contracts/compiler_core_system_link_exec_smoke.expected`

The checked-in native command-dispatch provider source of truth now lives at
`src/runtime/compiler_core_native_provider.c`. The bootstrap-side
`bootstrap/cheng_v2_native_provider.c` is only a thin include wrapper so the
host bootstrap tree can keep tracking a stable bootstrap entrypoint while
stage0/stage1+/source-side system-link planning all point at the same provider
implementation.

The provider identity is now part of the formal checked surface. For every
runtime provider, the bootstrap reports and checked-in artifacts freeze:

- `source_path`
- `source_kind`
- `compile_mode`
- `execution_model`
- `trace_symbol`
- `provider_symbol_count`
- `provider_symbol.*`

Today that means `compiler_core` is explicitly frozen as
`native_c_source + external_cc_obj`, while the trace-only runtime providers are
frozen as `cheng_module + machine_obj + native_trace_stub`.

`full-selfhost` now treats that as incomplete bootstrap state rather than a
passing final fixed point.

For `compiler_core`, the external-C closure is now explicit and multi-source:

- `src/runtime/compiler_core_native_provider.c`
- `src/runtime/compiler_core_native_dispatch.c`
- `src/runtime/compiler_core_native_dispatch.h`
- `bootstrap/cheng_v2c_tooling.c`
- `bootstrap/cheng_v2c_tooling.h`

The provider object is built by compiling those inputs separately and combining
the resulting objects with `ld -r`; it no longer relies on a provider source
file textually including `cheng_v2c_tooling.c`.

`make full-selfhost` is the authoritative native bootstrap gate. It must:

- build `cheng_v2.stage1` with `stage0`
- build `cheng_v2.stage2` with `stage1`
- build `cheng_v2.stage3` with `stage2`
- prove `stage2 == stage3` across `release.txt`,
  `system_link_plan.txt`, `system_link_exec.txt`, the native binary bytes, and
  `output_file_cid`
- rerun `tooling-selfhost-host` through `stage2`
- keep `emit_c_used_after_stage0=0`
