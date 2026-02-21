# Cheng Web Stack

This folder hosts the long-term web frontend effort (WASM-first).

Layout:
- compiler: SFC parsing and template compilation
- runtime: WASM runtime and JS glue contract
- router: file routing and data loader
- cli: dev/build/preview/test commands
- std: web/server standard library modules
- examples: minimal runtime demos

Status: runtime + minimal SFC compiler/codegen + examples + dev/preview servers (static + polling reload + SPA fallback) + build/route-manifest CLI + history router runtime + router outlet + registry mapping + route load pipeline (async via ctx.result) + std/server skeleton (http/router/middleware/static) + native server adapter (server_handle). Template bindings cover `on:*`, `bind:value`/`bind:checked`, `class:*`, `style:*`, `if/each` (supports `item, idx in list` + `else` empty), `await` (expects `Signal[AwaitText]` + `await:pending/await:catch`); scoped CSS injects `data-cwc-*`. See doc/cheng-web-frontend-plan.md and doc/cheng-web-wasm-abi.md.
