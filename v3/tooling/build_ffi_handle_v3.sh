#!/usr/bin/env sh
set -eu

root="$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)"
driver="$root/artifacts/v3_backend_driver/cheng"
out_dir="$root/artifacts/v3_ffi_handle"
ok_src="$root/v3/src/tests/ffi_handle_smoke.cheng"
trap_src="$root/v3/src/tests/ffi_handle_stale_trap_smoke.cheng"
ok_bin="$out_dir/ffi_handle_smoke"
trap_bin="$out_dir/ffi_handle_stale_trap_smoke"
ok_compile_log="$out_dir/ffi_handle_smoke.compile.log"
trap_compile_log="$out_dir/ffi_handle_stale_trap_smoke.compile.log"
ok_run_log="$out_dir/ffi_handle_smoke.run.log"
trap_run_log="$out_dir/ffi_handle_stale_trap_smoke.run.log"

mkdir -p "$out_dir"

build_one() {
  src="$1"
  out_bin="$2"
  compile_log="$3"
  rm -f "$out_bin" "$out_bin.v3.map" "$compile_log"
  set +e
  "$driver" system-link-exec \
    --root "$root/v3" \
    --in "$src" \
    --emit exe \
    --target arm64-apple-darwin \
    --out "$out_bin" >"$compile_log" 2>&1
  build_rc="$?"
  set -e
  if [ "$build_rc" -ne 0 ]; then
    echo "v3 ffi-handle: backend driver failed rc=$build_rc log=$compile_log" >&2
    tail -n 80 "$compile_log" >&2 || true
    exit 1
  fi
  if [ ! -x "$out_bin" ]; then
    echo "v3 ffi-handle: backend driver returned success but no executable was produced: $out_bin" >&2
    exit 1
  fi
  if [ ! -f "$out_bin.v3.map" ]; then
    echo "v3 ffi-handle: missing line-map sidecar: $out_bin.v3.map" >&2
    exit 1
  fi
}

if [ ! -x "$driver" ]; then
  sh "$root/v3/tooling/build_backend_driver_v3.sh"
fi

if [ ! -x "$driver" ]; then
  echo "v3 ffi-handle: missing backend driver: $driver" >&2
  exit 1
fi

if [ ! -f "$ok_src" ] || [ ! -f "$trap_src" ]; then
  echo "v3 ffi-handle: missing source fixture" >&2
  exit 1
fi

build_one "$ok_src" "$ok_bin" "$ok_compile_log"
build_one "$trap_src" "$trap_bin" "$trap_compile_log"

if ! "$ok_bin" >"$ok_run_log" 2>&1; then
  echo "v3 ffi-handle: built binary failed log=$ok_run_log" >&2
  tail -n 80 "$ok_run_log" >&2 || true
  exit 1
fi

set +e
"$trap_bin" >"$trap_run_log" 2>&1
trap_status="$?"
set -e
if [ "$trap_status" -eq 0 ]; then
  echo "v3 ffi-handle: stale handle trap binary unexpectedly exited 0 log=$trap_run_log" >&2
  tail -n 80 "$trap_run_log" >&2 || true
  exit 1
fi
if ! rg -q '^\[cheng\] ffi handle invalid: op=resolve_ptr detail=released handle=[0-9]+$' "$trap_run_log"; then
  echo "v3 ffi-handle: missing invalid handle message log=$trap_run_log" >&2
  tail -n 80 "$trap_run_log" >&2 || true
  exit 1
fi
if ! rg -q '^\[cheng-crash-trace\] reason=ffi_handle$' "$trap_run_log"; then
  echo "v3 ffi-handle: missing ffi_handle crash trace reason log=$trap_run_log" >&2
  tail -n 80 "$trap_run_log" >&2 || true
  exit 1
fi
if ! rg -q 'ffi_handle_stale_trap_smoke\.cheng:[0-9]+' "$trap_run_log"; then
  echo "v3 ffi-handle: missing stale trap source frame log=$trap_run_log" >&2
  tail -n 80 "$trap_run_log" >&2 || true
  exit 1
fi

cat "$ok_run_log"
cat "$trap_run_log"
