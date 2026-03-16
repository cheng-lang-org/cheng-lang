# Std Modules

Targets:
- std/web: DOM, fetch, history, WebSocket, timers, URL, TextEncoder/Decoder
- std/server: HTTP, router, middleware, static assets

Current modules:
- web_dom
- web_time
- web_fetch
- web_history
- web_log
- web_url
- web_text
- server_http
- server_router
- server_middleware
- server_static
- server

web_url:
- urlEncodeComponent/urlDecodeComponent
- urlParse (scheme/authority/host/port/path/query/hash)
- urlParseQuery/urlBuildQuery

web_text:
- TextEncoder/TextDecoder (utf-8 passthrough)
- textEncode/textDecode helpers

server_http:
- HttpRequest/HttpResponse + headers helpers
- method parse/format helpers
- requestFromUrl + responseToHttp helpers
- parseHttpRequest + requestQueryParams helpers
- requestTarget + requestContentLength + headerGetInt helpers

server_router:
- ServerRouter with method + path matching
- uses router patterns for params

server_middleware:
- applyMiddleware to compose ServerHandler chain

server_static:
- serveAssets (in-memory map)
- serveDir (filesystem-backed)
- serveDir supports index.html when a directory is requested
- serveAssets/serveDir add weak ETag support with If-None-Match
