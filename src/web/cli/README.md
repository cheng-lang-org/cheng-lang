# CLI

Responsibilities:
- cheng web dev/build/preview/test
- config discovery and defaults
- entrypoints for compiler/runtime

Current:
- `web.cheng` provides a single entrypoint for `dev/build/preview/route-manifest`.
  - source entry: `src/web/cli/web.cheng`
  - `dev` / `preview` 路径依赖 `src/web/cli/native_server.c` 提供的 `cheng_native_server_main`
- `build.cheng` compiles `.cwc` sources under a root into `.cheng` + `.css` and copies common static assets into the output folder.
  - standalone build: `artifacts/tooling_cmd/cheng_tooling cheng --in:src/web/cli/build.cheng --name:cheng_web_build`
  - flags: `--asset-exts:<list>` / `--no-assets` / `--map` / `--routes-root:<dir>` / `--routes-out:<file>` / `--routes-exts:<list>` (aliases: `--manifest-root` / `--manifest-out` / `--manifest-exts`).
  - env: `WEB_ASSET_EXTS` / `WEB_SOURCEMAP` / `WEB_ROUTES_ROOT` / `WEB_ROUTES_OUT` / `WEB_ROUTES_EXTS` (aliases: `WEB_MANIFEST_ROOT` / `WEB_MANIFEST_OUT` / `WEB_MANIFEST_EXTS`).
- `webc.cheng` compiles `.cwc` into `.cheng` + optional `.css`.
  - standalone build: `artifacts/tooling_cmd/cheng_tooling cheng --in:src/web/cli/webc.cheng --name:cheng_webc`
  - flags: `--map` to emit `.map.json` sidecars.
- `dev_server.cheng` runs a minimal static server with polling reload and optional build command (default build-exts: `.cwc`); can auto-run route manifest via `WEB_MANIFEST_ROOT` (aliases: `WEB_ROUTES_ROOT` / `--routes-root`).
  - supports `--spa` / `WEB_SPA` to serve `index.html` for client-side routes.
  - main entry source: `src/web/cli/dev_server_main.cheng`
- `preview_server.cheng` runs a static server for built assets.
  - supports `--spa` / `WEB_SPA` to serve `index.html` for client-side routes.
  - main entry source: `src/web/cli/preview_server_main.cheng`
- `route_manifest.cheng` scans a routes folder and emits `routes_manifest.cheng` for file-based routing.
  - standalone build: `artifacts/tooling_cmd/cheng_tooling cheng --in:src/web/cli/route_manifest.cheng --name:cheng_route_manifest`
- `native_server.c` provides a minimal POSIX HTTP adapter that calls `server_handle` in a Cheng server build.
  - current workflow is to link `native_server.c` with your native server build and expose `cheng_native_server_main`
  - historical helper `cheng_tooling web_build_native_server` is now a removed compatibility stub and exits with an error
  - supports `WEB_SERVER_HOST` / `WEB_SERVER_PORT` env overrides
  - supports `WEB_SERVER_MAX_HEADER` / `WEB_SERVER_MAX_BODY` / `WEB_SERVER_TIMEOUT_MS`
  - CLI flags: `--max-header:<bytes>` / `--max-body:<bytes>` / `--timeout-ms:<ms>`
  - keep-alive supported when request includes `Connection: keep-alive`
  - sends 400/408/413/431 for malformed/timeout/oversized requests
