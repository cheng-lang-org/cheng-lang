#!/usr/bin/env sh
:
set -eu
(set -o pipefail) 2>/dev/null && set -o pipefail

root="$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)"
cd "$root"

preferred_backend_driver() {
  "${TOOLING_SELF_BIN:-$root/artifacts/tooling_cmd/cheng_tooling}" backend_driver_path
}

driver="${BACKEND_DRIVER:-}"
if [ "$driver" = "" ]; then
  driver="$(preferred_backend_driver)"
fi
artifacts_dir="$root/artifacts"
final_out_dir="$artifacts_dir/backend_default_output_safety"
mkdir -p "$artifacts_dir"
work_dir="$(mktemp -d "$artifacts_dir/backend_default_output_safety.work.XXXXXX")"
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

report="$work_dir/backend_default_output_safety.report.txt"

exe_src="$work_dir/default_output_probe.cheng"
exe_src_expected="$work_dir/default_output_probe.expected.cheng"
exe_out="${exe_src%.cheng}"
exe_compile_log="$work_dir/default_output_probe.compile.log"

obj_src="$work_dir/default_output_obj_probe.cheng"
obj_src_expected="$work_dir/default_output_obj_probe.expected.cheng"
obj_out="${obj_src%.cheng}.o"
obj_compile_log="$work_dir/default_output_obj_probe.compile.log"

printf '%s\n' \
  'fn main(): int32 =' \
  '    return 0' >"$exe_src"
cp "$exe_src" "$exe_src_expected"

printf '%s\n' \
  'fn main(): int32 =' \
  '    return 7' >"$obj_src"
cp "$obj_src" "$obj_src_expected"

set +e
env BACKEND_INPUT="$exe_src" BACKEND_LINKER=system \
  "$driver" >"$exe_compile_log" 2>&1
exe_compile_rc="$?"
set -e
if [ "$exe_compile_rc" -ne 0 ]; then
  echo "[verify_backend_default_output_safety] exe compile failed rc=$exe_compile_rc" 1>&2
  sed -n '1,120p' "$exe_compile_log" 1>&2 || true
  exit 1
fi
if ! cmp -s "$exe_src_expected" "$exe_src"; then
  echo "[verify_backend_default_output_safety] source mutated during exe compile: $exe_src" 1>&2
  exit 1
fi
if [ ! -x "$exe_out" ]; then
  echo "[verify_backend_default_output_safety] missing default exe output: $exe_out" 1>&2
  exit 1
fi
set +e
"$exe_out" >/dev/null 2>&1
exe_run_rc="$?"
set -e
if [ "$exe_run_rc" -ne 0 ]; then
  echo "[verify_backend_default_output_safety] default exe output failed rc=$exe_run_rc" 1>&2
  exit 1
fi

set +e
env BACKEND_INPUT="$obj_src" BACKEND_EMIT=obj BACKEND_INTERNAL_ALLOW_EMIT_OBJ=1 \
  "$driver" >"$obj_compile_log" 2>&1
obj_compile_rc="$?"
set -e
if [ "$obj_compile_rc" -ne 0 ]; then
  echo "[verify_backend_default_output_safety] obj compile failed rc=$obj_compile_rc" 1>&2
  sed -n '1,120p' "$obj_compile_log" 1>&2 || true
  exit 1
fi
if ! cmp -s "$obj_src_expected" "$obj_src"; then
  echo "[verify_backend_default_output_safety] source mutated during obj compile: $obj_src" 1>&2
  exit 1
fi
if [ ! -s "$obj_out" ]; then
  echo "[verify_backend_default_output_safety] missing default obj output: $obj_out" 1>&2
  exit 1
fi

{
  echo "result=ok"
  echo "driver=$driver"
  echo "verified_exe_source=$final_out_dir/$(basename -- "$exe_src")"
  echo "verified_exe_output=$final_out_dir/$(basename -- "$exe_out")"
  echo "verified_obj_source=$final_out_dir/$(basename -- "$obj_src")"
  echo "verified_obj_output=$final_out_dir/$(basename -- "$obj_out")"
  echo "exe_compile_log=$final_out_dir/$(basename -- "$exe_compile_log")"
  echo "obj_compile_log=$final_out_dir/$(basename -- "$obj_compile_log")"
} >"$report"

publish_dir="$(mktemp -d "$artifacts_dir/backend_default_output_safety.publish.XXXXXX")"
rmdir "$publish_dir"
mv "$work_dir" "$publish_dir"
work_dir=""
rm -rf "$final_out_dir"
mv "$publish_dir" "$final_out_dir"
publish_dir=""

echo "verify_backend_default_output_safety ok"
