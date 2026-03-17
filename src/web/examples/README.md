# Cheng Web Examples

Counter demo (WASM runtime):
- Build the SFC helper: `artifacts/tooling_cmd/cheng_tooling cheng --in:src/web/cli/webc.cheng --name:cheng_webc`
- Compile `counter.cwc`: `./artifacts/chengc/cheng_webc src/web/examples/counter.cwc --out:src/web/examples/counter.generated.cheng --css:src/web/examples/counter.generated.css --map`
- Serve this folder over HTTP.
- Open `index.html`.

Build (compile `.cwc` + copy common assets):
- Build the project compiler: `artifacts/tooling_cmd/cheng_tooling cheng --in:src/web/cli/build.cheng --name:cheng_web_build`
- Run: `WEB_ROOT=src/web/examples ./artifacts/chengc/cheng_web_build --out:src/web/examples/dist --map`

Dev / preview native server:
- Historical helper `cheng_tooling web_build_native_server` has been removed and now exits with an error.
- Current native-server sources are `src/web/cli/native_server.c`, `src/web/cli/dev_server_main.cheng`, `src/web/cli/preview_server_main.cheng`, and `src/web/examples/server_main.cheng`.
- If you need standalone native dev/preview binaries, link `native_server.c` in your own native build flow and run them with `WEB_ROOT=src/web/examples`.
- Optional: set `WEB_BUILD="<your build command>"` for rebuild-on-change behavior in the dev server path.

Files:
- counter.cheng: Cheng UI entrypoint (registers app hooks)
- counter.cwc: SFC source (compile via `webc` to Cheng)
- counter.cwc includes `if`/`each` + `else` empty and `await` template usage for basic structural rendering.
- client.js: loads client.wasm via host_glue.js
- index.html/style.css: minimal host page

Router demo:
- Route files live under `src/web/examples/routes` (file-based routing shape only).
- Build the manifest tool: `artifacts/tooling_cmd/cheng_tooling cheng --in:src/web/cli/route_manifest.cheng --name:cheng_route_manifest`
- Generate `routes_manifest.cheng`: `./artifacts/chengc/cheng_route_manifest --root:src/web/examples/routes --out:src/web/examples/routes_manifest.cheng`
- Compile `router_app.cheng` to `client.wasm` to try routing.
  - Alternatively, set `WEB_MANIFEST_ROOT=src/web/examples/routes` in your dev workflow to auto-generate.

Router files:
- router_app.cheng: runtime router + outlet wiring
- routes_manifest.cheng: generated route list (imported by router_app)
- router_app.cheng includes `load(ctx)` usage, async updates via `ctx.result` + `routeSetResult`, and query param access.

Server demo:
- server_demo.cheng: std/server skeleton usage (router + middleware + in-memory static assets).
- Call `demoRequest("/hello/Cheng?lang=zh")` or `demoRequest("/health.txt")` in a test harness to inspect responses.

Server app:
- server_app.cheng: uses runtime `server_app` + std/server to expose `server_handle` for adapters.
- server_main.cheng: entrypoint that calls the native server adapter.
- native adapter source: `src/web/cli/native_server.c`
- standalone native server binaries now need a project-local native link step; the old `web_build_native_server` helper is no longer usable.
