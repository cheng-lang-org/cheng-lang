# Cheng v2

`v2/` is the new compiler implementation root.

This tree is intentionally separated from the current `src/` implementation:

- `v2/src/` holds the new compiler and tooling sources.
- `v2/docs/` freezes the v2 architecture and contracts.
- `v2/tests/` holds contract and integration probes for the new tree.
- `v2/artifacts/` is reserved for v2-generated outputs.

Current scope of this initial landing:

- package root for v2
- compiler-layer contract modules
- minimal `cheng_tooling_v2` and `cheng_v2` entrypoints
- contract smoke programs for SoA/index storage and pipeline surface
- independent host bootstrap under `v2/bootstrap/`
- dedicated string surface design for performance and syntax cleanup
- initial `v2/src/runtime/strings_v2.cheng` runtime with Two-Way search and owned builder semantics
- frontend and low_uir string lowering contracts that bind sugar forms to runtime/string ops
- concrete string pipeline modules for `surface IR -> semantic facts -> high_uir -> low_uir -> runtime target`
- initial Unimaker language contracts for `asset/actor/state/hashref/capability/refinement/evolve`
- topology and network-distribution surface/pipeline contracts with fixed LSMR publication semantics
- deterministic source-manifest and release-artifact drivers with compile keys rooted in topology/content identity
- host bootstrap compiler `v2/bootstrap/cheng_v2c` for the minimal Unimaker source slice
- fixed subset selfhost proof entry via `make -C v2/bootstrap selfhost`
- implementation plan document in `v2/docs/plan.md`
- full bootstrap implementation handbook in `v2/docs/bootstrap-implementation.md`

The intent is to make `v2/` a self-contained place to grow the new compiler,
without mixing it into the existing production tree prematurely.
