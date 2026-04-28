# String And JSON

Modules: `std/strings`, `std/strutils`, `std/strformat`, `std/json`, `std/parseutils`

Key APIs: `len/+`, `split/join/strip`, `fmt/lines`, `parseInt32/parseBiggestFloat`, `jsonObjectLit/jsonArrayLit`, `getStr/getBool/getInt64`, `parseJsonSafe`

Example: `examples/std/string_json.cheng`

Notes: `parseJsonSafe` is part of the documented surface, but the current bootstrap runtime still reports parsing as unimplemented; the baseline locks that behavior until the parser is upgraded.
