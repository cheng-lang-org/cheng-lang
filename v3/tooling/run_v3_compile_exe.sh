#!/usr/bin/env zsh
set -eu
setopt nonomatch

suite_label="$1"
compile_label="$2"
compiler="$3"
root_v3="$4"
src="$5"
bin="$6"

compile_log="$bin.compile.log"

if [ ! -f "$src" ]; then
  echo "$suite_label: missing source: $src" >&2
  exit 1
fi

rm -f "$bin" "$bin".* "$compile_log"

echo "[$suite_label] compile $compile_label"
if ! zsh -lc 'DIAG_CONTEXT=1 "$1" system-link-exec --root "$2" --in "$3" --emit exe --target arm64-apple-darwin --out "$4" >"$5" 2>&1' \
  zsh "$compiler" "$root_v3" "$src" "$bin" "$compile_log"; then
  echo "$suite_label: compile failed: $compile_label" >&2
  tail -n 80 "$compile_log" >&2 || true
  exit 1
fi
