#!/usr/bin/env sh
. "$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)/env_prefix_bridge.sh"
set -eu
(set -o pipefail) 2>/dev/null && set -o pipefail

root="$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)"
cd "$root"

out_dir="artifacts/tooling_cmdline"
mkdir -p "$out_dir"

list_out="$out_dir/tooling_cmdline.list.txt"
help_out="$out_dir/tooling_cmdline.backend_prod_closure.help.txt"
report="$out_dir/tooling_cmdline.report.txt"

sh src/tooling/cheng_tooling.sh list >"$list_out"

if ! rg -q '^backend_prod_closure$' "$list_out"; then
  echo "[verify_tooling_cmdline] missing backend_prod_closure in list output" 1>&2
  exit 1
fi
if ! rg -q '^verify_backend_closedloop$' "$list_out"; then
  echo "[verify_tooling_cmdline] missing verify_backend_closedloop in list output" 1>&2
  exit 1
fi
if ! rg -q '^chengc$' "$list_out"; then
  echo "[verify_tooling_cmdline] missing chengc in list output" 1>&2
  exit 1
fi

sh src/tooling/cheng_tooling.sh run backend_prod_closure --help >"$help_out"
if ! rg -q 'Usage:' "$help_out"; then
  echo "[verify_tooling_cmdline] backend_prod_closure help text missing" 1>&2
  exit 1
fi

sh src/tooling/cheng_tooling.sh backend_prod_closure.sh --help >/dev/null

{
  echo "tooling_cmdline_runner=ok"
  echo "list_output=$list_out"
  echo "help_output=$help_out"
} >"$report"

echo "verify_tooling_cmdline ok"
