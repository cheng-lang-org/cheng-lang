# `rustc_codegen_uir`

Bootstrap crate for the Rust -> Cheng UIR path.

Current scope:

- stable-safe manifest builders for `UirSemanticModuleV1` and compile options
- MIR-driven semantic lowering for local Rust mono-items into function/local/block/expr/stmt/layout tables
- runtime-loaded bridge bindings for `cheng_uir_validate_v1/v2`, `cheng_uir_compile_v1/v2`, `cheng_uir_free_result_v1`
- a feature-gated nightly `CodegenBackend` that can be loaded by `rustc -Z codegen-backend`
- strict UIR-only Rust reporting: current Rust builds write
  `*.rust-uir-semantic.txt` and `*.rust-uir-report.txt` by default; semantic/symbol sidecars are now
  only written on failure or when `CHENG_UIR_WRITE_INPUT_SIDECARS=1`
- LLVM fallback is intentionally disabled in the backend

Notes:

- default builds do not require `rustc-dev`
- `--features rustc-backend` is intended for nightly + `rustc_private` environments
- local `dev` / `test` profiles intentionally disable debuginfo and incremental caches to keep
  `/Users/lbcheng/cheng-lang/rustc_codegen_uir/target` from ballooning during backend iteration
- architectural boundary: this crate exists because `rustc` backends must be Rust crates;
  it should stay limited to `rustc` integration, MIR/ABI harvesting, and semantic-table export
- the frontend-agnostic semantic schema and stable C ABI live on the Cheng side in
  `/Users/lbcheng/cheng-lang/src/backend/uir/uir_semantic_types.cheng`,
  `/Users/lbcheng/cheng-lang/src/backend/uir/uir_semantic_lowering.cheng`, and
  `/Users/lbcheng/cheng-lang/src/backend/uir/uir_semantic_capi.cheng`
- the backend first honors `CHENG_UIR_BRIDGE_DYLIB`; if unset, it now prefers a Cheng-native bridge at
  `/Users/lbcheng/cheng-lang/artifacts/cheng_uir_native/libcheng_uir_native.*` and only then falls back to
  `/Users/lbcheng/cheng-lang/cheng_uir_bridge_bootstrap/target/{debug,release}/libcheng_uir_bridge_bootstrap.*`
- the old Cheng-native bridge build entry
  `/Users/lbcheng/cheng-lang/scripts/build_cheng_uir_native_bridge.sh`
  is retired with the old tooling bridge and is no longer a maintained path
- when the bridge exports `cheng_uir_*_v2`, the backend sends manifest / semantic tables / symbol list
  as split buffers to avoid large manifest inlining and bootstrap file I/O
- this repository now includes a bootstrap bridge dylib at
  `/Users/lbcheng/cheng-lang/cheng_uir_bridge_bootstrap`
- direct Rust lowering of MIR into semantic tables is implemented for local mono-items
- MIR lowering now also emits bootstrap vtable descriptors for dyn-trait unsize casts
- successful bridge compilation is now handed back into rustc as an object file for final linking
- the bootstrap bridge can now emit real AArch64 Mach-O objects for a checked integer subset,
  including plain `a + b`, `wrapping_add`-style cases, thin-pointer refs/derefs, simple closure captures,
  indirect `fn` pointer calls, layout-driven newtype lowering, scalar-pair fat-pointer argument handling,
  `PtrMetadata`, and the current dyn-trait/slice bootstrap subsets
- bin manifests now carry an explicit `c_main_target_symbol` so the bridge can generate a bootstrap C entry wrapper
- that subset is verified end-to-end for local `staticlib` functions, a minimal explicit-`main` bin,
  a plain `fn main() {}` std bin, an `opt-level=0` closure-capture staticlib, and a `slice_len`
  `staticlib` harness using `&[i32]`
- the current `strict_std_main.rs` smoke now compiles as strict UIR-only with `real_fn_symbols=9` and `stub_fn_symbols=0`
- bridge reports now expose `real_fn_symbols` / `stub_fn_symbols` and per-symbol `stub_fn_reason` lines to show how much of a crate compiled as real subset codegen
- unsupported Rust semantics are still handled by the bootstrap bridge, not by the final Cheng machine-code lowering

Benchmarking:

- run `python3 /Users/lbcheng/cheng-lang/rustc_codegen_uir/benchmarks/uir_vs_llvm.py`
- it rebuilds the bridge/backend, compares `LLVM` vs strict `UIR-only` on the fixed `strict_*` samples,
  and writes `/Users/lbcheng/cheng-lang/artifacts/rust_uir_bench/latest_report.md`
