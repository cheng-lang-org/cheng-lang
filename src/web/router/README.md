# Router

Responsibilities:
- File-based routing with dynamic segments
- load(ctx) data pipeline for SSR/CSR
- Route-to-component mapping and metadata

Status:
- `router.cheng` provides path parsing and matching for `[id]` / `[...rest]` patterns.
- `filePathToRoute` converts file paths to route strings (basic, no root trimming yet).
- `route_manifest.cheng` (CLI) can emit a `routes_manifest.cheng` that builds Route lists.
- `RouteEntry` type lives in `router.cheng` for manifest output sharing.
- `parsePathInfo` splits path/query/hash; `parseQueryParams` + `queryGet` provide query access for load contexts (percent decoding via std/web_url).
