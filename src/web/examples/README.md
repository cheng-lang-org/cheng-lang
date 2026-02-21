# Cheng Web Examples

Counter demo (WASM runtime):
- Compile the Cheng entry to `client.wasm`.
- Serve this folder over HTTP.
- Open `index.html`.

Dev server (live reload):
- Build: `cheng/web/cli/build_native_server.sh --entry:cheng/web/cli/dev_server_main.cheng --name:cheng_dev_server`
- Run: `WEB_ROOT=cheng/web/examples ./cheng_dev_server --host:127.0.0.1 --port:5173`
- Optional: `WEB_BUILD="<your build command>"` to rebuild wasm on change.
  - Or build `src/tooling/chengc.sh cheng/web/cli/web.cheng --name:cheng_web` then run `WEB_ROOT=cheng/web/examples ./cheng_web dev --host:127.0.0.1 --port:5173`.

Build (compile .cwc):
- `src/tooling/chengc.sh cheng/web/cli/web.cheng --name:cheng_web`
- `WEB_ROOT=cheng/web/examples ./cheng_web build --out:cheng/web/examples/dist --map` (copies common assets + map)

Preview server (static):
- Build: `cheng/web/cli/build_native_server.sh --entry:cheng/web/cli/preview_server_main.cheng --name:cheng_preview_server`
- Run: `WEB_ROOT=cheng/web/examples ./cheng_preview_server --host:127.0.0.1 --port:4173`
  - Or run `WEB_ROOT=cheng/web/examples ./cheng_web preview --host:127.0.0.1 --port:4173`.

Files:
- counter.cheng: Cheng UI entrypoint (registers app hooks)
- counter.cwc: SFC source (compile via `webc` to Cheng)
- counter.cwc includes `if`/`each` + `else` empty and `await` template usage for basic structural rendering.
- client.js: loads client.wasm via host_glue.js
- index.html/style.css: minimal host page

Router demo:
- Route files live under `examples/routes` (file-based routing shape only).
- Generate `routes_manifest.cheng` via `src/tooling/chengc.sh cheng/web/cli/route_manifest.cheng --name:cheng_route_manifest` then run `./cheng_route_manifest --root:cheng/web/examples/routes --out:cheng/web/examples/routes_manifest.cheng`.
  - Or run `./cheng_web route-manifest --root:cheng/web/examples/routes --out:cheng/web/examples/routes_manifest.cheng`.
- Compile `router_app.cheng` to `client.wasm` to try routing.
  - Alternatively, set `WEB_MANIFEST_ROOT=cheng/web/examples/routes` and run the dev server to auto-generate.

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
- build with `cheng/web/cli/build_native_server.sh --entry:cheng/web/examples/server_main.cheng`.
