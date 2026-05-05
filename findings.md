# Findings (2026-05-05)

## Architecture
- Cold compiler (`cheng_cold.c` 310loc C) + full compiler (`src/` Cheng) coexistence model validated.
- `bootstrap-bridge` path works (stage3→stage2). C seed cold-start has known limits (thread/memRetain/atomic).
- Mach-O direct write: 12 load commands in `macho_direct.h` (280loc). Codesign page alignment is the remaining gap.

## BodyIR CFG
- `&&` compound conditions: factorized `EmitConditionOps` eliminated 200 lines of duplicated inline parsing.
- `pendingCompoundFalseTerms` chain correctly resolves elif/else/end-of-function false blocks.
- 9 body kinds converged to BodyIR CFG. `buildBodyIR` generalized to `wordCounts[i]<=0` auto-trigger.
- `BackendDriverDispatchMinRunCommand` and `BackendDriverDispatchMinCommandCode` both released to StatementSequence.

## Parallelization
- Chunked fill infrastructure ready: `PrimaryFillFunctionChunk` + `PrimaryFillInstructionWordsChunked`.
- Each function's `instructionWords` range is non-overlapping → zero-contention parallel write.
- `thread.Spawn` activation requires stage3 compilation; C seed cold-start takes serial path.
- 4-phase parallel: lowering function classification + BodyIR build + instruction fill + CSG source processing.

## Cold compiler
- `return <int>` verified (exit 42/77/88/99).
- Multi-line function body with indentation works.
- `if/else` IR construction correct; branch patching: true block resolved, false block index off by 1.
- Algebraic type parser (`type X = A | B`) with variant counting.
- `match`/`let`/`if`/`else` keywords recognized; body parsing stubbed for non-return statements.

## Docs
- Algebraic type + `match` syntax added to `docs/cheng-formal-spec.md` §1.2.
- `algebraicType ::= variantType { "|" variantType }`, `matchStmt`, `variantPattern`.
