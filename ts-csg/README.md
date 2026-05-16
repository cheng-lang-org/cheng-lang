# TS CSG

`ts-csg` extracts deterministic TypeScript semantic facts for the Cheng CSG pipeline.

This is not a source-to-source rewriter. It freezes TypeScript semantics into
`ts-csg.semantic.v1` JSONL facts that a later Cheng lowering pass can consume.
It also emits project-level `csg-core.v1` facts as the language-neutral bridge
before Cheng ABI mapping. A strict minimal `CHENG_CSG_V2` writer remains for the
closed int32 subset of `main()`.

## Command

```sh
npm install
npm run build
node dist/cli.js --project fixtures/basic/tsconfig.json --out tmp/basic.csg.jsonl
node dist/cli.js --emit csg-core --project fixtures/basic/tsconfig.json --runtime node,browser --entry-root src/main.ts --out tmp/basic.csgcore --report-out tmp/basic.report.json
node dist/cli.js --emit cheng-csg-v2 --project fixtures/csg-v2/tsconfig.json --out tmp/main.csgv2
```

## Contract

- Input is TypeScript checked by the TypeScript compiler API.
- Output is deterministic JSONL with stable IDs and sorted object keys.
- Unsupported dynamic JavaScript semantics hard-fail instead of producing invalid facts.
- Facts describe modules, imports/exports, declarations, types, calls, control flow,
  JSX nodes, and explicit unsupported diagnostics.
- `--emit csg-core` emits language-neutral facts plus a JSON report containing
  coverage counts, runtime requirements, external symbols, and unsupported
  semantics. A report with `complete:false` is a blocked lowering state, not a
  successful native artifact.
- `--emit cheng-csg-v2` emits public `CHENG_CSG_V2` object facts only for the
  closed subset it can prove.

## Current Scope

Supported:

- strict TypeScript modules
- imports/exports
- functions, parameters, generics, async/generator flags
- async functions and `await` as CSG facts plus explicit Promise runtime requirements
- variables
- object and array destructuring binding facts
- classes, constructors, methods, properties, heritage facts
- interfaces, type aliases, enums
- calls, `new`, `await`, return/throw/control statements
- JSX element and expression facts plus explicit JSX runtime requirements
- spread, iterator loops, and exception regions as facts plus explicit runtime requirements
- project-level CSG-Core facts:
  - modules, imports, exports, symbols, types
  - function, block, term, op, call, data, debug map
  - Node, Browser DOM, ECMAScript builtin, and external package requirements
  - deterministic coverage report

Hard-fail:

- TypeScript semantic diagnostics
- `any` in project declarations/expressions
- `eval(...)`
- non-literal `require(...)`
- non-literal dynamic `import(...)`
- non-literal computed declaration names
- prototype mutation assignments
- `CHENG_CSG_V2` input outside exported zero-arg `main(): number`
- calls, objects, arrays, loops, mutation, floating-point values, dynamic input in `CHENG_CSG_V2`

## Output

The first record is always:

```json
{"kind":"csg.schema","language":"typescript","schema":"ts-csg.semantic","version":1}
```

Every later record is a fact with a stable `id` and optional source location.

`--emit csg-core` writes JSONL facts beginning with:

```json
{"kind":"csg.core.schema","language":"typescript","schema":"csg-core","version":1}
```

When `--report-out` is present, the report schema is:

```json
{"schema":"csg-core.report","version":1,"complete":false}
```

`--emit csg-core` always writes the report when TypeScript checking succeeds.
If `complete:false`, downstream Cheng-ABI/native generation must stop.
`--entry-root` is repeatable and only appears in the report when the file is
part of the checked TypeScript program.

The fixed project gate is:

```sh
npm run smoke:projects
```

It checks:

- `/Users/lbcheng/cursor-restored/cursor-agents-window` with `tsconfig.json`.
- `/Users/lbcheng/UniMaker/React.js` with root `tsconfig.json` and
  `./node_modules/.bin/tsc --noEmit -p tsconfig.json`.

Each gate exports CSG-Core twice and requires byte-identical facts and reports.
The report must expose unsupported/runtime requirements instead of claiming
blank completion.

`--emit cheng-csg-v2` writes canonical public records:

```text
CHENG_CSG_V2
R0001...
R0002...
R0003...
R0004...
R0005...
```

The current writer targets `arm64-apple-darwin` and materializes same-file int32
functions, positional number parameters, direct calls, local int32 variables,
`+`, `-`, `*`, numeric comparisons, `if`, and `return` as real ARM64 instruction
words plus `CHENG_CSG_V2` branch relocations.
