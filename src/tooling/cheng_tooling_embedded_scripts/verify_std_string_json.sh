#!/usr/bin/env sh
:
set -eu
(set -o pipefail) 2>/dev/null && set -o pipefail

root="$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)"
cd "$root"

tool="${TOOLING_SELF_BIN:-$root/artifacts/tooling_cmd/cheng_tooling}"
artifacts_dir="$root/artifacts"
final_out_dir="$artifacts_dir/std_string_json"
mkdir -p "$artifacts_dir"
work_dir="$(mktemp -d "$artifacts_dir/std_string_json.work.XXXXXX")"
publish_dir=""

cleanup() {
  if [ "$publish_dir" != "" ] && [ -e "$publish_dir" ]; then
    rm -rf "$publish_dir" 2>/dev/null || true
  fi
  if [ "$work_dir" != "" ] && [ -e "$work_dir" ]; then
    rm -rf "$work_dir" 2>/dev/null || true
  fi
}
trap cleanup EXIT INT TERM

write_text() {
  out_path="$1"
  cat >"$out_path"
}

ensure_ok_file() {
  path="$1"
  kind="$2"
  if [ ! -e "$path" ]; then
    echo "[verify_std_string_json] missing $kind: $path" 1>&2
    return 1
  fi
  return 0
}

ensure_no_overlap_warning() {
  compile_log="$1"
  if rg -q 'within another string' "$compile_log"; then
    echo "[verify_std_string_json] Mach-O cstring overlap warning in compile log: $compile_log" 1>&2
    sed -n '1,200p' "$compile_log" 1>&2 || true
    return 1
  fi
  return 0
}

compile_source() {
  src="$1"
  out="$2"
  compile_log="$3"
  set +e
  "$tool" cheng "$src" --out:"$out" >"$compile_log" 2>&1
  compile_rc="$?"
  set -e
  if [ "$compile_rc" -ne 0 ]; then
    echo "[verify_std_string_json] compile failed rc=$compile_rc src=$src" 1>&2
    sed -n '1,200p' "$compile_log" 1>&2 || true
    return 1
  fi
  if [ ! -x "$out" ]; then
    echo "[verify_std_string_json] missing executable output: $out" 1>&2
    sed -n '1,200p' "$compile_log" 1>&2 || true
    return 1
  fi
  ensure_no_overlap_warning "$compile_log"
}

run_probe() {
  label="$1"
  src="$2"
  probe_src="$work_dir/${label}.cheng"
  probe_out="$work_dir/${label}"
  probe_compile_log="$work_dir/${label}.compile.log"
  write_text "$probe_src" <<EOF
$src
EOF
  compile_source "$probe_src" "$probe_out" "$probe_compile_log"
}

import_src="$work_dir/std_string_json_import.cheng"
import_out="$work_dir/std_string_json_import"
import_compile_log="$work_dir/std_string_json_import.compile.log"
import_run_log="$work_dir/std_string_json_import.run.log"
write_text "$import_src" <<'EOF'
import std/strings
import std/strutils
import std/strformat
import std/json
import std/parseutils

fn main(): int32 =
    return 0
EOF
compile_source "$import_src" "$import_out" "$import_compile_log"

smoke_src="$root/src/tests/std_string_json_smoke.cheng"
smoke_out="$work_dir/std_string_json_smoke"
smoke_compile_log="$work_dir/std_string_json_smoke.compile.log"
ensure_ok_file "$smoke_src" "smoke source"
compile_source "$smoke_src" "$smoke_out" "$smoke_compile_log"

run_probe \
  "std_strformat_singleton" \
  'import std/strformat

fn main(): int32 =
    let single: str[] = ["z"]
    echo fmt(single)
    return 0'

run_probe \
  "std_strformat_concat" \
  'import std/strformat

fn main(): int32 =
    let parts: str[] = ["a", "", "bc"]
    echo fmt(parts)
    return 0'

run_probe \
  "std_strformat_lines" \
  'import std/strformat

fn main(): int32 =
    let rows: str[] = ["x", "y"]
    echo lines(rows)
    return 0'

report="$work_dir/std_string_json.report.txt"
{
  echo "result=ok"
  echo "tool=$tool"
  echo "import_compile_log=$final_out_dir/$(basename -- "$import_compile_log")"
  echo "import_run_skipped=1"
  echo "smoke_src=$smoke_src"
  echo "smoke_compile_log=$final_out_dir/$(basename -- "$smoke_compile_log")"
  echo "smoke_run_skipped=1"
  echo "smoke_macho_cstring_overlap=0"
  echo "strformat_singleton_compile_log=$final_out_dir/std_strformat_singleton.compile.log"
  echo "strformat_singleton_run_skipped=1"
  echo "strformat_concat_compile_log=$final_out_dir/std_strformat_concat.compile.log"
  echo "strformat_concat_run_skipped=1"
  echo "strformat_lines_compile_log=$final_out_dir/std_strformat_lines.compile.log"
  echo "strformat_lines_run_skipped=1"
} >"$report"

publish_dir="$(mktemp -d "$artifacts_dir/std_string_json.publish.XXXXXX")"
rmdir "$publish_dir"
mv "$work_dir" "$publish_dir"
work_dir=""
rm -rf "$final_out_dir"
mv "$publish_dir" "$final_out_dir"
publish_dir=""

echo "verify_std_string_json ok"
