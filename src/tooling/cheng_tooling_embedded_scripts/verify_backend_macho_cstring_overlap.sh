#!/usr/bin/env sh
:
set -eu
(set -o pipefail) 2>/dev/null && set -o pipefail

root="$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)"
cd "$root"

tool="${TOOLING_SELF_BIN:-$root/artifacts/tooling_cmd/cheng_tooling}"
src="$root/src/tests/std_string_json_smoke.cheng"
artifacts_dir="$root/artifacts"
final_out_dir="$artifacts_dir/backend_macho_cstring_overlap"
mkdir -p "$artifacts_dir"
work_dir="$(mktemp -d "$artifacts_dir/backend_macho_cstring_overlap.work.XXXXXX")"
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

if [ ! -f "$src" ]; then
  echo "[verify_backend_macho_cstring_overlap] missing smoke source: $src" 1>&2
  exit 1
fi

target="${BACKEND_TARGET:-$("$tool" detect_host_target 2>/dev/null || true)}"
case "$target" in
  *apple-darwin*) ;;
  *)
    report="$work_dir/backend_macho_cstring_overlap.report.txt"
    {
      echo "result=skip"
      echo "reason=non_darwin_target"
      echo "target=$target"
      echo "smoke_src=$src"
    } >"$report"
    publish_dir="$(mktemp -d "$artifacts_dir/backend_macho_cstring_overlap.publish.XXXXXX")"
    rmdir "$publish_dir"
    mv "$work_dir" "$publish_dir"
    work_dir=""
    rm -rf "$final_out_dir"
    mv "$publish_dir" "$final_out_dir"
    publish_dir=""
    echo "verify_backend_macho_cstring_overlap skip: target=$target"
    exit 0
    ;;
esac

smoke_out="$work_dir/std_string_json_smoke"
compile_log="$work_dir/std_string_json_smoke.compile.log"
report="$work_dir/backend_macho_cstring_overlap.report.txt"

set +e
"$tool" cheng "$src" --out:"$smoke_out" --target:"$target" >"$compile_log" 2>&1
compile_rc="$?"
set -e
if [ "$compile_rc" -ne 0 ]; then
  echo "[verify_backend_macho_cstring_overlap] compile failed rc=$compile_rc" 1>&2
  sed -n '1,160p' "$compile_log" 1>&2 || true
  exit 1
fi
if [ ! -x "$smoke_out" ]; then
  echo "[verify_backend_macho_cstring_overlap] missing executable output: $smoke_out" 1>&2
  sed -n '1,160p' "$compile_log" 1>&2 || true
  exit 1
fi
if rg -q 'within another string' "$compile_log"; then
  echo "[verify_backend_macho_cstring_overlap] overlap warning still present" 1>&2
  sed -n '1,200p' "$compile_log" 1>&2 || true
  exit 1
fi

patch_line="$(rg -m1 '^patch_macho_cstrings:' "$compile_log" || true)"
{
  echo "result=ok"
  echo "target=$target"
  echo "tool=$tool"
  echo "smoke_src=$src"
  echo "smoke_output=$final_out_dir/$(basename -- "$smoke_out")"
  echo "compile_log=$final_out_dir/$(basename -- "$compile_log")"
  echo "warning_count=0"
  echo "patch_line=$patch_line"
} >"$report"

publish_dir="$(mktemp -d "$artifacts_dir/backend_macho_cstring_overlap.publish.XXXXXX")"
rmdir "$publish_dir"
mv "$work_dir" "$publish_dir"
work_dir=""
rm -rf "$final_out_dir"
mv "$publish_dir" "$final_out_dir"
publish_dir=""

echo "verify_backend_macho_cstring_overlap ok"
