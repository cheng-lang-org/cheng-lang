#!/usr/bin/env sh
set -eu

root="$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)"
out_dir="$root/artifacts/v3_native_twoway"
smoke_src="$root/v3/runtime/native/v3_str_twoway_search_smoke.c"
smoke_bin="$out_dir/v3_str_twoway_search_smoke"

mkdir -p "$out_dir"

cc -std=c11 -O2 -Wall -Wextra -pedantic -c \
  "$root/src/runtime/native/system_helpers.c" \
  -o "$out_dir/system_helpers.o"
cc -std=c11 -O2 -Wall -Wextra -pedantic -c \
  "$root/src/runtime/native/system_helpers_selflink_min_runtime.c" \
  -o "$out_dir/system_helpers_selflink_min_runtime.o"
cc -std=c11 -O2 -Wall -Wextra -pedantic -c \
  "$root/src/runtime/native/system_helpers_selflink_shim.c" \
  -o "$out_dir/system_helpers_selflink_shim.o"
cc -std=c11 -O2 -Wall -Wextra -pedantic \
  "$smoke_src" \
  -o "$smoke_bin"

"$smoke_bin"
