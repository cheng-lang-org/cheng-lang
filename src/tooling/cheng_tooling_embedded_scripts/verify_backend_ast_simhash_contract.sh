#!/usr/bin/env sh
:
set -eu
(set -o pipefail) 2>/dev/null && set -o pipefail

root="$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)"
cd "$root"

fail() {
  echo "[verify_backend_ast_simhash_contract] $1" >&2
  exit 1
}

line_no() {
  file="$1"
  pattern="$2"
  rg -n --no-messages "$pattern" "$file" | head -n 1 | cut -d: -f1
}

require_marker() {
  file="$1"
  pattern="$2"
  marker="$3"
  if ! rg -q "$pattern" "$file"; then
    fail "missing marker ($marker) in $file"
  fi
}

if ! command -v rg >/dev/null 2>&1; then
  fail "rg is required"
fi

simhash_file="src/decentralized/ast_simhash.cheng"
registry_file="src/decentralized/registry.cheng"
registry_local_file="src/decentralized/registry_local.cheng"

[ -f "$simhash_file" ] || fail "missing simhash source: $simhash_file"
[ -f "$registry_file" ] || fail "missing registry source: $registry_file"
[ -f "$registry_local_file" ] || fail "missing registry local source: $registry_local_file"

require_marker "$simhash_file" 'fn simhashStripStdImportLines' 'strip_std_import_fn'
require_marker "$simhash_file" 'lineHasStdImport' 'line_std_import_predicate'
require_marker "$simhash_file" 'strutil.contains\(line, "std/"\)' 'std_import_contains_marker'
require_marker "$simhash_file" 'filtered = simhashStripStdImportLines\(content\)' 'filter_before_tokenize'
require_marker "$simhash_file" 'tokenizeSourceShape\(filtered\)' 'tokenize_filtered_source'
require_marker "$registry_file" 'simhash.computeAstSimhash64ForDir\(root\)' 'registry_simhash_call'
require_marker "$registry_local_file" 'simhash.computeAstSimhash64ForDir\(root\)' 'registry_local_simhash_call'

if rg -q 'monomorphize|seqs_mono_' "$simhash_file"; then
  fail "ast simhash module must not depend on monomorphize/seqs_mono surfaces"
fi

strip_fn_line="$(line_no "$simhash_file" 'fn simhashStripStdImportLines')"
filter_use_line="$(line_no "$simhash_file" 'filtered = simhashStripStdImportLines\(content\)')"
tokenize_filtered_line="$(line_no "$simhash_file" 'tokenizeSourceShape\(filtered\)')"

for v in "$strip_fn_line" "$filter_use_line" "$tokenize_filtered_line"; do
  case "$v" in
    ''|*[!0-9]*)
      fail "failed to resolve ast simhash contract marker line numbers"
      ;;
  esac
done

if [ "$strip_fn_line" -ge "$filter_use_line" ]; then
  fail "std import filter function must be declared before compute call-site"
fi
if [ "$filter_use_line" -ge "$tokenize_filtered_line" ]; then
  fail "tokenization must use filtered source after std-import stripping"
fi

out_dir="artifacts/backend_ast_simhash_contract"
mkdir -p "$out_dir"
report="$out_dir/backend_ast_simhash_contract.report.txt"

{
  echo "verify_backend_ast_simhash_contract report"
  echo "status=ok"
  echo "simhash_file=$simhash_file"
  echo "registry_file=$registry_file"
  echo "registry_local_file=$registry_local_file"
  echo "strip_fn_line=$strip_fn_line"
  echo "filter_use_line=$filter_use_line"
  echo "tokenize_filtered_line=$tokenize_filtered_line"
  echo "std_import_filter_enabled=1"
  echo "monomorphize_coupling=0"
} >"$report"

echo "verify_backend_ast_simhash_contract ok"
