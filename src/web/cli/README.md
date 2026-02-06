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
  - env: `CHENG_WEB_ASSET_EXTS` / `CHENG_WEB_SOURCEMAP` / `CHENG_WEB_ROUTES_ROOT` / `CHENG_WEB_ROUTES_OUT` / `CHENG_WEB_ROUTES_EXTS` (aliases: `CHENG_WEB_MANIFEST_ROOT` / `CHENG_WEB_MANIFEST_OUT` / `CHENG_WEB_MANIFEST_EXTS`).
- `webc.cheng` compiles `.cwc` into `.cheng` + optional `.css`.
  - flags: `--map` to emit `.map.json` sidecars.
- `dev_server.cheng` runs a minimal static server with polling reload and optional build command (default build-exts: `.cwc`); can auto-run route manifest via `CHENG_WEB_MANIFEST_ROOT` (aliases: `CHENG_WEB_ROUTES_ROOT` / `--routes-root`).
  - supports `--spa` / `CHENG_WEB_SPA` to serve `index.html` for client-side routes.
  - build via `cheng/web/cli/build_native_server.sh --entry:cheng/web/cli/dev_server_main.cheng --name:cheng_dev_server`.
- `preview_server.cheng` runs a static server for built assets.
  - supports `--spa` / `CHENG_WEB_SPA` to serve `index.html` for client-side routes.
  - build via `cheng/web/cli/build_native_server.sh --entry:cheng/web/cli/preview_server_main.cheng --name:cheng_preview_server`.
- `route_manifest.cheng` scans a routes folder and emits `routes_manifest.cheng` for file-based routing.
  - build via `src/tooling/chengc.sh cheng/web/cli/route_manifest.cheng --name:cheng_route_manifest`.
- `native_server.c` provides a minimal POSIX HTTP adapter that calls `server_handle` in a Cheng server build.
  - build it by linking `native_server.c` with a native `server_app.cheng` output.
- `build_native_server.sh` compiles `server_main.cheng` and links `native_server.c` into a runnable binary.
  - supports `CHENG_WEB_SERVER_HOST` / `CHENG_WEB_SERVER_PORT` env overrides.
  - supports `CHENG_WEB_SERVER_MAX_HEADER` / `CHENG_WEB_SERVER_MAX_BODY` / `CHENG_WEB_SERVER_TIMEOUT_MS`.
  - CLI flags: `--max-header:<bytes>` / `--max-body:<bytes>` / `--timeout-ms:<ms>`.
  - keep-alive supported when request includes `Connection: keep-alive`.
  - sends 400/408/413/431 for malformed/timeout/oversized requests.
