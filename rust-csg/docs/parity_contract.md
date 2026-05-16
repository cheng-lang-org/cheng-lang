# parity contract

`rust-csg` must never pretend to be Codex before the relevant source surface is ported.

## statuses

- `ported`: Cheng implementation exists, has parity tests, and passes its source oracle.
- `non-runtime asset`: source file is documentation, fixture, generated schema, or static oracle consumed by tests.
- `explicitly_blocked`: real dependency or subsystem not yet ported; any product entry touching it must hard-fail with a concrete reason.

## current status

All Rust workspace members and TS package surfaces are `explicitly_blocked`.

## hard-fail rule

Product entries return a non-zero exit code and print:

```text
rust-csg blocked: full Codex parity is not implemented
```

No mock responses, no fake model output, no empty success, no fallback to Rust binaries.
