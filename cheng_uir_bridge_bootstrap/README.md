# `cheng_uir_bridge_bootstrap`

Bootstrap `cdylib` that implements:

- `cheng_uir_validate_v1`
- `cheng_uir_compile_v1`
- `cheng_uir_validate_v2`
- `cheng_uir_compile_v2`
- `cheng_uir_free_result_v1`

Current scope:

- consumes the manifest ABI emitted by `rustc_codegen_uir`
- accepts Rust semantic tables / symbol lists either from sidecar paths or the split-buffer `v2` ABI
- writes a linkable object for supported targets
- lowers a bootstrap AArch64 Mach-O subset from Rust semantic tables into real assembly/object code
- current real subset covers scalar locals, `assign`, `return`, direct `call`, `operand.list`,
  `operand.copy`/`operand.move`/`rvalue.use`, thin-pointer `rvalue.ref` / `place.deref`,
  simple `rvalue.aggregate`, common scalar `cast`, integer `rvalue.binary`, `AddWithOverflow`,
  `term.assert`, tuple-or-single-field `place.field`, indirect `fn` pointer calls, layout-driven
  scalar/newtype lowering, scalar-pair argument passing, `PtrMetadata`, and bootstrap
  `PointerCoercion(Unsize, Implicit)` for the current dyn-trait and slice fat-pointer subsets
- supports a bootstrap `_main -> entry_symbol` wrapper for Rust `bin` crates via manifest metadata
- verified end-to-end for a Rust `staticlib` integer add function, a minimal `#![no_main]` bin,
  a plain `fn main() {}` std bin, an `opt-level=0` closure-capture staticlib, and a `&[i32] -> usize`
  `staticlib` slice-length harness
- the current strict `strict_std_main.rs` smoke now compiles with `real_fn_symbols=9` and `stub_fn_symbols=0`
- reports `real_fn_symbols` / `stub_fn_symbols` and per-symbol `stub_fn_reason` lines so bootstrap coverage is measurable per compile
- supports strict `rustc -Z codegen-backend` smoke tests without LLVM fallback inside the Rust backend

Current limitation:

- when a Cheng-native bridge is available, `rustc_codegen_uir` should prefer that dylib and leave this crate as compatibility-only fallback
- the native bridge build entry is `/Users/lbcheng/cheng-lang/scripts/build_cheng_uir_native_bridge.sh`
- unsupported semantic shapes still fall back to bootstrap stubs inside this bridge
- this is still a bootstrap subset bridge, not the final Cheng-native `MIR -> High-UIR -> Low-UIR -> machine` pipeline
- long-term boundary: schema ownership and semantic lowering authority should stay in Cheng;
  this crate is only a temporary bootstrap implementation, not the canonical home for UIR semantics
