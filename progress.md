# Progress (2026-05-05)

## Cold compiler (`bootstrap/cheng_cold.c`, 310 loc)
- Arena + SoA BodyIR + direct ARM64. `fn main(): int32 = return 42` → exit 42.
- Parser: `type` (algebraic), `fn` (multi-line body), `return <int>`.
- `if/else` IR construction done; branch patching: true-block correct, false-block off by 1.
- `macho_direct.h`: 280 loc, 12 load commands. Codesign alignment WIP.

## Full compiler (`src/`)
- Bootstrap: `bootstrap-bridge` ok (1373 fns, 0 new missing). `build-backend-driver` ok.
- Body kind: 9 converged to BodyIR CFG. `&&` compound conditions in if/elif.
- Parallel: 4-phase `thread.Spawn` chunked. Serial 495s→130s. Parallel est. ~33s (15x).
- Types: `PrimaryObjectError` nested type (36 fields→1).
- Docs: algebraic type + `match` syntax in `docs/cheng-formal-spec.md` §1.2.

## Remaining
- Cold: fix false-block branch, add `match` codegen, extend to bootstrap subset.
- Full: CSG incremental, `BACKEND_JOBS>1` with stage3.
- Mach-O: codesign page alignment.
