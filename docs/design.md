>      I know a great deal, body guard. I recognise those marks on the back of your hand...
>      A gift from your friend. The one who talks to you in the dark. Talks to you when you
>      visit his shrines. I've visited those shrines, too. But I don't know you, who you are,
>      and who you fight for. You're a mystery, and I can't allow that.
>     -- "Daud"

# Goals

Version 3 targets a compiler and toolchain that supports:

1. Incremental recompilations.
2. No forward declarations for fns and types required.
3. Explicit cyclic module dependencies where intended.
4. Type-checked generics.
5. Clear, predictable lowering and diagnostics (no phase-order surprises).

# Implementation (Direct C Pipeline)

The legacy IR pipeline has been removed. Cheng now emits C directly.

## Stages

- **stage0c**: Minimal bootstrap compiler. Used for seed updates and basic diagnostics.
- **stage1**: Full compiler frontend + direct C backend (the production path).

## Frontend phases (stage1)

1. **Parse**: Cheng source to AST.
2. **Normalize/Desugar**: Canonicalize syntax (imports, call forms, literals).
3. **Semantics**: Name resolution, type checking, constraint checks.
4. **Ownership/ORC**: Move/borrow tracking and diagnostics.
5. **Monomorphize**: Instantiate generics and resolve trait/concept dispatch.
6. **Lowering**: Replace high-level constructs (closures, iterators, control-flow expressions).
7. **C-Profile Lowering**: Apply direct C ABI and layout rules.
8. **Codegen**: Emit C sources plus module dependency metadata.

## Outputs

- **C sources**: Generated C files for each module and/or unit.
- **Dependency graph**: `.deps.list` files replace old IR task graphs.
- **Determinism**: Bootstrap includes a determinism check to ensure repeatable output.

## Tooling

- `chengc.sh` is the main entry for direct C compilation (stage1 path).
- `bootstrap.sh` builds `stage1_runner`, runs optional fullspec, and validates determinism.
- C ABI, mangling, and type mapping are specified in `docs/cheng-direct-c-profile.md`.
