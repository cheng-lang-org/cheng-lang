# Compiler

Responsibilities:
- Parse .cwc single-file components
- Build component IR (template/script/style)
- Lower templates into DOM instruction sequences
- CSS scope hashing and extraction
- Emit WASM module plus JS glue metadata

Status:
- Minimal SFC parser + template AST + codegen into Cheng runtime calls.
- Template directives: `on:*`, `bind:value`/`bind:checked`, `class:*`, `style:*`, `if/else-if/else`, `each` (supports `item, idx in list` + `else` empty), `await` (expects `Signal[AwaitText]` and supports `await:pending`/`await:catch`; basic rerender).
- Scoped CSS: adds `data-cwc-*` attribute and rewrites selectors (basic, no full CSS parser; only `@media/@supports` are scoped).
- See `cheng/web/cli/webc.cheng` for the CLI entrypoint.
