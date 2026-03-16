#!/usr/bin/env sh
:
set -eu
(set -o pipefail) 2>/dev/null && set -o pipefail

root="$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)"
cd "$root"

report_script="src/tooling/cheng_tooling_embedded_scripts/report_backend_string_literal_regression.sh"
artifacts_dir="$root/artifacts"
final_report_dir="$artifacts_dir/backend_string_literal_regression"
mkdir -p "$artifacts_dir"
report_dir="$(mktemp -d "$artifacts_dir/backend_string_literal_regression.verify.work.XXXXXX")"
report="$report_dir/backend_string_literal_regression.report.txt"
verify_report="$report_dir/backend_string_literal_regression.verify.txt"
publish_dir=""

cleanup() {
  if [ "$publish_dir" != "" ] && [ -e "$publish_dir" ]; then
    rm -rf "$publish_dir" 2>/dev/null || true
  fi
  if [ "$report_dir" != "" ] && [ -e "$report_dir" ]; then
    rm -rf "$report_dir" 2>/dev/null || true
  fi
}
trap cleanup EXIT INT TERM

if [ ! -f "$report_script" ]; then
  echo "[verify_backend_string_literal_regression] missing report script: $report_script" 1>&2
  exit 2
fi

BACKEND_STRING_LITERAL_REPORT_DIR="$report_dir" sh "$report_script"

if [ ! -f "$report" ]; then
  echo "[verify_backend_string_literal_regression] missing report: $report" 1>&2
  exit 1
fi

expect_pass_fixture() {
  fixture_name="$1"
  if ! grep -F "[fixture] $fixture_name " "$report" | grep -F " status=pass" >/dev/null 2>&1; then
    echo "[verify_backend_string_literal_regression] fixture did not pass: $fixture_name" 1>&2
    sed -n '1,160p' "$report" 1>&2 || true
    exit 1
  fi
}

if grep '^\[fixture\] ' "$report" | grep -F -v " status=pass" >/dev/null 2>&1; then
  echo "[verify_backend_string_literal_regression] regression detected" 1>&2
  grep '^\[fixture\] ' "$report" 1>&2 || true
  exit 1
fi

expect_pass_fixture "str_literal_len_probe"
expect_pass_fixture "str_literal_not_empty_probe"
expect_pass_fixture "str_literal_call_arg_probe"
expect_pass_fixture "str_literal_rawbytes_probe"

{
  echo "result=ok"
  echo "report=$report"
  echo "timeout_secs=${BACKEND_STRING_LITERAL_TIMEOUT:-60}"
  echo "verified_fixtures=str_literal_len_probe,str_literal_not_empty_probe,str_literal_call_arg_probe,str_literal_rawbytes_probe"
} >"$verify_report"

publish_dir="$(mktemp -d "$artifacts_dir/backend_string_literal_regression.verify.publish.XXXXXX")"
rmdir "$publish_dir"
mv "$report_dir" "$publish_dir"
report_dir=""
rm -rf "$final_report_dir"
mv "$publish_dir" "$final_report_dir"
publish_dir=""

echo "verify_backend_string_literal_regression ok"
