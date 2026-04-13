#!/usr/bin/env zsh
set -eu
setopt nonomatch

tooling_dir="$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)"
suite_label="$1"
compile_label="$2"
compiler="$3"
root_v3="$4"
src="$5"
bin="$6"

run_log="$bin.run.log"

zsh "$tooling_dir/run_v3_compile_exe.sh" \
  "$suite_label" \
  "$compile_label" \
  "$compiler" \
  "$root_v3" \
  "$src" \
  "$bin"

echo "[$suite_label] run $compile_label"
if ! "$bin" >"$run_log" 2>&1; then
  echo "$suite_label: run failed: $compile_label" >&2
  tail -n 80 "$run_log" >&2 || true
  exit 1
fi

cat "$run_log"
