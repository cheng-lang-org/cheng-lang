# Runtime

Responsibilities:
- Define the WASM ABI (imports/exports, memory, strings)
- Provide a minimal DOM host contract
- Implement signals scheduling and lifecycle hooks
- Support hydration and optional islands

Artifacts:
- abi.cheng: WASM import stubs
- host_glue.js: JS host reference implementation
- memory.cheng: pointer/string helpers
- events.cheng: host event dispatch + handler registry
- signals.cheng: minimal reactive signals
- app.cheng: cwc_* entrypoints + app hook registry
- bindings.cheng: DOM bindings for signals/events
- view.cheng: minimal view tree helpers + block helpers for conditional/loop rendering and cleanup
- async.cheng: minimal await state + fetch helpers
- runtime.cheng: umbrella import for runtime modules
- dev_client.js: polling live reload + build error overlay (dev server helper)
- router.cheng: history-driven router state + path signal + match helper + router outlet + registry helpers (incl. register-by-path + route load context/result + query params + async updates via ctx.result + load task cancellation helpers)
- server_app.cheng: server_handle export + registerServerApp for adapters
