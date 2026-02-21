# CLI

Responsibilities:
- cheng web dev/build/preview/test
- config discovery and defaults
- entrypoints for compiler/runtime

Current:
- `web.cheng` provides a single entrypoint for `dev/build/preview/route-manifest`.
  - build via `src/tooling/chengc.sh cheng/web/cli/web.cheng --name:cheng_web`.
- `build.cheng` compiles `.cwc` sources under a root into `.cheng` + `.css` and copies common static assets into the output folder.
  - flags: `--asset-exts:<list>` / `--no-assets` / `--map` / `--routes-root:<dir>` / `--routes-out:<file>` / `--routes-exts:<list>` (aliases: `--manifest-root` / `--manifest-out` / `--manifest-exts`).
  - env: `WEB_ASSET_EXTS` / `WEB_SOURCEMAP` / `WEB_ROUTES_ROOT` / `WEB_ROUTES_OUT` / `WEB_ROUTES_EXTS` (aliases: `WEB_MANIFEST_ROOT` / `WEB_MANIFEST_OUT` / `WEB_MANIFEST_EXTS`).
- `webc.cheng` compiles `.cwc` into `.cheng` + optional `.css`.
  - flags: `--map` to emit `.map.json` sidecars.
- `dev_server.cheng` runs a minimal static server with polling reload and optional build command (default build-exts: `.cwc`); can auto-run route manifest via `WEB_MANIFEST_ROOT` (aliases: `WEB_ROUTES_ROOT` / `--routes-root`).
  - supports `--spa` / `WEB_SPA` to serve `index.html` for client-side routes.
  - build via `cheng/web/cli/build_native_server.sh --entry:cheng/web/cli/dev_server_main.cheng --name:cheng_dev_server`.
- `preview_server.cheng` runs a static server for built assets.
  - supports `--spa` / `WEB_SPA` to serve `index.html` for client-side routes.
  - build via `cheng/web/cli/build_native_server.sh --entry:cheng/web/cli/preview_server_main.cheng --name:cheng_preview_server`.
- `route_manifest.cheng` scans a routes folder and emits `routes_manifest.cheng` for file-based routing.
  - build via `src/tooling/chengc.sh cheng/web/cli/route_manifest.cheng --name:cheng_route_manifest`.
- `native_server.c` provides a minimal POSIX HTTP adapter that calls `server_handle` in a Cheng server build.
  - build it by linking `native_server.c` with a native `server_app.cheng` output.
- `build_native_server.sh` compiles `server_main.cheng` and links `native_server.c` into a runnable binary.
  - supports `WEB_SERVER_HOST` / `WEB_SERVER_PORT` env overrides.
  - supports `WEB_SERVER_MAX_HEADER` / `WEB_SERVER_MAX_BODY` / `WEB_SERVER_TIMEOUT_MS`.
  - CLI flags: `--max-header:<bytes>` / `--max-body:<bytes>` / `--timeout-ms:<ms>`.
  - keep-alive supported when request includes `Connection: keep-alive`.
  - sends 400/408/413/431 for malformed/timeout/oversized requests.
