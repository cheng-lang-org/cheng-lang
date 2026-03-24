#!/usr/bin/env sh
:
set -eu
(set -o pipefail) 2>/dev/null && set -o pipefail

root="$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)"
cd "$root"

tool="${TOOLING_SELF_BIN:-$root/artifacts/tooling_cmd/cheng_tooling}"
driver="${BACKEND_DRIVER:-$root/artifacts/backend_driver/cheng}"
src="$root/src/tooling/cheng_tooling.cheng"
artifacts_dir="$root/artifacts"
final_out_dir="$artifacts_dir/backend_tooling_launcher_fastpath"
mkdir -p "$artifacts_dir"
work_dir="$(mktemp -d "$artifacts_dir/backend_tooling_launcher_fastpath.work.XXXXXX")"
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

if [ ! -x "$driver" ]; then
  echo "[verify_backend_tooling_launcher_fastpath] missing backend driver: $driver" 1>&2
  echo "  hint: $tool build-backend-driver --require-rebuild" 1>&2
  exit 1
fi
if [ ! -f "$src" ]; then
  echo "[verify_backend_tooling_launcher_fastpath] missing tooling source: $src" 1>&2
  exit 1
fi

host_target="$("$tool" detect_host_target)"
target="${BACKEND_TARGET:-$host_target}"
runtime_obj="${BACKEND_RUNTIME_OBJ:-$root/chengcache/runtime_selflink/system_helpers.backend.combined.${target}.o}"
if [ ! -f "$runtime_obj" ]; then
  echo "[verify_backend_tooling_launcher_fastpath] missing runtime obj: $runtime_obj" 1>&2
  exit 1
fi

launcher_out="$work_dir/cheng_tooling.fastpath"
compile_log="$work_dir/cheng_tooling.fastpath.compile.log"
help_log="$work_dir/cheng_tooling.fastpath.help.log"
report="$work_dir/backend_tooling_launcher_fastpath.report.txt"

set +e
env \
  TOOLING_EMIT_SELFHOST_LAUNCHER=1 \
  BACKEND_DEBUG_BOOT=1 \
  MM=orc \
  CACHE=0 \
  STAGE1_AUTO_SYSTEM=0 \
  STAGE1_STD_NO_POINTERS=0 \
  STAGE1_STD_NO_POINTERS_STRICT=0 \
  STAGE1_NO_POINTERS_NON_C_ABI=0 \
  STAGE1_NO_POINTERS_NON_C_ABI_INTERNAL=0 \
  STAGE1_SKIP_CPROFILE=1 \
  STAGE1_PROFILE=0 \
  BACKEND_PROFILE=0 \
  UIR_PROFILE=0 \
  BACKEND_ENABLE_CLI=1 \
  BACKEND_BUILD_TRACK=dev \
  BACKEND_ALLOW_DIRECT_DRIVER=1 \
  BACKEND_LINKER=self \
  BACKEND_STAGE1_BUILDER=stage1 \
  BACKEND_SKIP_GLOBAL_INIT=0 \
  BACKEND_IR=uir \
  BACKEND_VALIDATE=0 \
  BACKEND_ALLOW_NO_MAIN=0 \
  BACKEND_FAST_DEV_PROFILE=1 \
  BACKEND_STAGE1_PARSE_MODE=outline \
  BACKEND_FN_SCHED=ws \
  BACKEND_DIRECT_EXE=1 \
  BACKEND_LINKERLESS_INMEM=1 \
  BACKEND_FAST_FALLBACK_ALLOW=0 \
  BACKEND_MULTI=0 \
  BACKEND_MULTI_FORCE=0 \
  BACKEND_INCREMENTAL=1 \
  BACKEND_JOBS=1 \
  BACKEND_FN_JOBS=1 \
  BACKEND_EMIT=exe \
  GENERIC_MODE=dict \
  GENERIC_SPEC_BUDGET=0 \
  GENERIC_LOWERING=mir_dict \
  BACKEND_TARGET="$target" \
  BACKEND_INPUT="$src" \
  BACKEND_OUTPUT="$launcher_out" \
  BACKEND_NO_RUNTIME_C=1 \
  BACKEND_RUNTIME_OBJ="$runtime_obj" \
  "$driver" \
  --input:"$src" \
  --output:"$launcher_out" \
  --target:"$target" \
  --linker:self >"$compile_log" 2>&1
compile_rc="$?"
set -e

if [ "$compile_rc" -ne 0 ]; then
  echo "[verify_backend_tooling_launcher_fastpath] compile failed rc=$compile_rc" 1>&2
  sed -n '1,160p' "$compile_log" 1>&2 || true
  exit 1
fi
if [ ! -f "$launcher_out" ]; then
  echo "[verify_backend_tooling_launcher_fastpath] missing launcher output: $launcher_out" 1>&2
  exit 1
fi
if ! head -n 1 "$launcher_out" | rg -q '^#!/usr/bin/env sh$'; then
  echo "[verify_backend_tooling_launcher_fastpath] output is not a shell launcher: $launcher_out" 1>&2
  sed -n '1,20p' "$launcher_out" 1>&2 || true
  exit 1
fi
if ! rg -q 'src/tooling/cheng_tooling_embedded_scripts/cheng_tooling.sh' "$launcher_out"; then
  echo "[verify_backend_tooling_launcher_fastpath] launcher missing repo wrapper exec path" 1>&2
  sed -n '1,40p' "$launcher_out" 1>&2 || true
  exit 1
fi
if ! rg -q 'tooling_launcher_rc=0' "$compile_log"; then
  echo "[verify_backend_tooling_launcher_fastpath] compile log missing launcher fast-path marker" 1>&2
  sed -n '1,160p' "$compile_log" 1>&2 || true
  exit 1
fi
if rg -q 'sidecar_rc=|emit_obj_rc=|\.tmp\.linkobj' "$compile_log"; then
  echo "[verify_backend_tooling_launcher_fastpath] compile log fell through to obj emit path" 1>&2
  sed -n '1,160p' "$compile_log" 1>&2 || true
  exit 1
fi

set +e
"$launcher_out" --help >"$help_log" 2>&1
help_rc="$?"
set -e
if [ "$help_rc" -ne 0 ]; then
  echo "[verify_backend_tooling_launcher_fastpath] launcher help failed rc=$help_rc" 1>&2
  sed -n '1,120p' "$help_log" 1>&2 || true
  exit 1
fi
if ! rg -qi '^usage:|^commands:' "$help_log"; then
  echo "[verify_backend_tooling_launcher_fastpath] launcher help missing expected surface" 1>&2
  sed -n '1,120p' "$help_log" 1>&2 || true
  exit 1
fi

{
  echo "result=ok"
  echo "driver=$driver"
  echo "target=$target"
  echo "runtime_obj=$runtime_obj"
  echo "launcher_output=$final_out_dir/$(basename -- "$launcher_out")"
  echo "compile_log=$final_out_dir/$(basename -- "$compile_log")"
  echo "help_log=$final_out_dir/$(basename -- "$help_log")"
} >"$report"

publish_dir="$(mktemp -d "$artifacts_dir/backend_tooling_launcher_fastpath.publish.XXXXXX")"
rmdir "$publish_dir"
mv "$work_dir" "$publish_dir"
work_dir=""
rm -rf "$final_out_dir"
mv "$publish_dir" "$final_out_dir"
publish_dir=""

echo "verify_backend_tooling_launcher_fastpath ok"
