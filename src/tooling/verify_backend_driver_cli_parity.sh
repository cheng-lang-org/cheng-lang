#!/usr/bin/env sh
. "$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)/env_prefix_bridge.sh"
set -eu
(set -o pipefail) 2>/dev/null && set -o pipefail

root="$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)"
cd "$root"

driver="${BACKEND_DRIVER:-}"
if [ "$driver" = "" ]; then
  driver="$(sh src/tooling/backend_driver_path.sh 2>/dev/null || true)"
fi
if [ "$driver" = "" ] || [ ! -x "$driver" ]; then
  echo "[verify_backend_driver_cli_parity] backend driver not executable: ${driver:-<unset>}" 1>&2
  exit 1
fi

target="${BACKEND_TARGET:-$(sh src/tooling/detect_host_target.sh 2>/dev/null || echo arm64-apple-darwin)}"
fixture="${BACKEND_CLI_PARITY_FIXTURE:-tests/cheng/backend/fixtures/return_i64.cheng}"
safe_target="$(printf '%s' "$target" | tr -c 'A-Za-z0-9._-' '_' | tr -s '_')"
runtime_obj="${BACKEND_RUNTIME_OBJ:-chengcache/system_helpers.backend.cheng.${safe_target}.o}"
out_dir="artifacts/backend_driver_cli_parity"
report="$out_dir/backend_driver_cli_parity.report.txt"
env_exe="$out_dir/env_path.bin"
cli_exe="$out_dir/cli_path.bin"
env_stamp="$out_dir/env_path.compile_stamp.txt"
cli_stamp="$out_dir/cli_path.compile_stamp.txt"
stamp_diff="$out_dir/compile_stamp.diff"

mkdir -p "$out_dir"
rm -f "$env_exe" "$cli_exe" "$env_stamp" "$cli_stamp" "$stamp_diff" "$report"

if [ ! -f "$runtime_obj" ]; then
  echo "[verify_backend_driver_cli_parity] missing runtime object for self-link: $runtime_obj" 1>&2
  exit 2
fi

echo "== backend.driver_cli_parity.env_path =="
env \
  BACKEND_EMIT=exe \
  BACKEND_TARGET="$target" \
  BACKEND_FRONTEND=stage1 \
  BACKEND_LINKER=self \
  BACKEND_NO_RUNTIME_C=1 \
  BACKEND_RUNTIME_OBJ="$runtime_obj" \
  BACKEND_MULTI=0 \
  BACKEND_MULTI_FORCE=0 \
  BACKEND_INCREMENTAL=0 \
  BACKEND_VALIDATE=0 \
  GENERIC_MODE=dict \
  GENERIC_SPEC_BUDGET=0 \
  ABI=v2_noptr \
  BACKEND_INPUT="$fixture" \
  BACKEND_OUTPUT="$env_exe" \
  BACKEND_COMPILE_STAMP_OUT="$env_stamp" \
  "$driver"

echo "== backend.driver_cli_parity.cli_path =="
env \
  -u BACKEND_INPUT \
  BACKEND_ENABLE_CLI=1 \
  BACKEND_EMIT=exe \
  BACKEND_TARGET="$target" \
  BACKEND_FRONTEND=stage1 \
  BACKEND_LINKER=self \
  BACKEND_NO_RUNTIME_C=1 \
  BACKEND_RUNTIME_OBJ="$runtime_obj" \
  BACKEND_MULTI=0 \
  BACKEND_MULTI_FORCE=0 \
  BACKEND_INCREMENTAL=0 \
  BACKEND_VALIDATE=0 \
  GENERIC_MODE=dict \
  GENERIC_SPEC_BUDGET=0 \
  ABI=v2_noptr \
  BACKEND_OUTPUT="$cli_exe" \
  BACKEND_COMPILE_STAMP_OUT="$cli_stamp" \
  "$driver" \
  "$fixture"

if [ ! -s "$env_exe" ]; then
  echo "[verify_backend_driver_cli_parity] missing env executable: $env_exe" 1>&2
  exit 1
fi
if [ ! -s "$cli_exe" ]; then
  echo "[verify_backend_driver_cli_parity] missing cli executable: $cli_exe" 1>&2
  exit 1
fi
if [ ! -s "$env_stamp" ] || [ ! -s "$cli_stamp" ]; then
  echo "[verify_backend_driver_cli_parity] missing compile stamp outputs" 1>&2
  exit 1
fi

if ! cmp -s "$env_stamp" "$cli_stamp"; then
  diff -u "$env_stamp" "$cli_stamp" >"$stamp_diff" 2>/dev/null || true
  echo "[verify_backend_driver_cli_parity] compile stamp mismatch between env and cli paths" 1>&2
  if [ -s "$stamp_diff" ]; then
    tail -n 120 "$stamp_diff" 1>&2 || true
  fi
  exit 1
fi

status_env=0
status_cli=0
if [ "${BACKEND_DRIVER_CLI_PARITY_RUN:-0}" = "1" ]; then
  set +e
  "$env_exe"
  status_env="$?"
  "$cli_exe"
  status_cli="$?"
  set -e
  if [ "$status_env" -ne "$status_cli" ]; then
    echo "[verify_backend_driver_cli_parity] executable exit status mismatch: env=$status_env cli=$status_cli" 1>&2
    exit 1
  fi
  if [ "$status_cli" -ne 0 ]; then
    echo "[verify_backend_driver_cli_parity] expected zero exit from parity sample, got $status_cli" 1>&2
    exit 1
  fi
fi

{
  echo "target=$target"
  echo "driver=$driver"
  echo "fixture=$fixture"
  echo "env_exe=$env_exe"
  echo "cli_exe=$cli_exe"
  echo "env_stamp=$env_stamp"
  echo "cli_stamp=$cli_stamp"
  echo "exit_status=$status_cli"
  echo "result=ok"
} >"$report"

echo "verify_backend_driver_cli_parity ok"
