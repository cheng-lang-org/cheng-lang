#!/usr/bin/env sh
:
set -eu
(set -o pipefail) 2>/dev/null && set -o pipefail

root="$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)"
cd "$root"

driver="${BACKEND_DRIVER:-$root/artifacts/backend_driver/cheng}"
artifacts_dir="$root/artifacts"
final_out_dir="$artifacts_dir/std_strformat"
mkdir -p "$artifacts_dir"
work_dir="$(mktemp -d "$artifacts_dir/std_strformat.work.XXXXXX")"
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

report="$work_dir/std_strformat.report.txt"
run_probe() {
  probe_name="$1"
  probe_body="$2"
  probe_expected_text="$3"
  probe_src="$work_dir/$probe_name.cheng"
  probe_out="${probe_src%.cheng}"
  probe_compile_log="$work_dir/$probe_name.compile.log"
  probe_run_log="$work_dir/$probe_name.run.txt"
  probe_expected="$work_dir/$probe_name.expected.txt"

  printf '%s\n' \
    'import std/strformat' \
    '' \
    'fn main() =' \
    "$probe_body" >"$probe_src"

  printf '%s' "$probe_expected_text" >"$probe_expected"

  set +e
  env BACKEND_INPUT="$probe_src" BACKEND_OUTPUT="$probe_out" BACKEND_LINKER=system \
    "$driver" >"$probe_compile_log" 2>&1
  probe_compile_rc="$?"
  set -e
  if [ "$probe_compile_rc" -ne 0 ]; then
    echo "[verify_std_strformat] compile failed ($probe_name) rc=$probe_compile_rc" 1>&2
    sed -n '1,120p' "$probe_compile_log" 1>&2 || true
    exit 1
  fi
  if [ ! -x "$probe_out" ]; then
    echo "[verify_std_strformat] missing executable output ($probe_name): $probe_out" 1>&2
    exit 1
  fi

  set +e
  "$probe_out" >"$probe_run_log" 2>&1
  probe_run_rc="$?"
  set -e
  if [ "$probe_run_rc" -ne 0 ]; then
    echo "[verify_std_strformat] runtime failed ($probe_name) rc=$probe_run_rc" 1>&2
    sed -n '1,120p' "$probe_run_log" 1>&2 || true
    exit 1
  fi
  if ! cmp -s "$probe_expected" "$probe_run_log"; then
    echo "[verify_std_strformat] output mismatch ($probe_name)" 1>&2
    echo "expected:" 1>&2
    sed -n '1,40p' "$probe_expected" 1>&2 || true
    echo "actual:" 1>&2
    sed -n '1,40p' "$probe_run_log" 1>&2 || true
    exit 1
  fi
}

run_probe "std_strformat_singleton" \
  '    let single: str[] = ["z"]
    echo fmt(single)' \
  'z
'

run_probe "std_strformat_concat" \
  '    let parts: str[] = ["a", "", "bc"]
    echo fmt(parts)' \
  'abc
'

run_probe "std_strformat_lines" \
  '    let rows: str[] = ["x", "y"]
    echo lines(rows)' \
  'x
y
'

{
  echo "result=ok"
  echo "driver=$driver"
  echo "verified=fmt(singleton str[]),fmt(str[]),lines(str[])"
  echo "singleton_compile_log=$final_out_dir/std_strformat_singleton.compile.log"
  echo "singleton_run_log=$final_out_dir/std_strformat_singleton.run.txt"
  echo "concat_compile_log=$final_out_dir/std_strformat_concat.compile.log"
  echo "concat_run_log=$final_out_dir/std_strformat_concat.run.txt"
  echo "lines_compile_log=$final_out_dir/std_strformat_lines.compile.log"
  echo "lines_run_log=$final_out_dir/std_strformat_lines.run.txt"
} >"$report"

publish_dir="$(mktemp -d "$artifacts_dir/std_strformat.publish.XXXXXX")"
rmdir "$publish_dir"
mv "$work_dir" "$publish_dir"
work_dir=""
rm -rf "$final_out_dir"
mv "$publish_dir" "$final_out_dir"
publish_dir=""

echo "verify_std_strformat ok"
