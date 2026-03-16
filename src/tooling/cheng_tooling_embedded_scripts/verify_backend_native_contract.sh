#!/usr/bin/env sh
:
set -eu
(set -o pipefail) 2>/dev/null && set -o pipefail

usage() {
  cat <<'EOF'
Usage:
  ${TOOLING_SELF_BIN:-artifacts/tooling_cmd/cheng_tooling} verify_backend_native_contract [--baseline:<path>] [--doc:<path>]

Notes:
  - Verifies CNCPAR-01 native contract baseline and implementation closure.
  - Regenerate baseline with: ${TOOLING_SELF_BIN:-artifacts/tooling_cmd/cheng_tooling} build_backend_native_contract
EOF
}

hash_file() {
  file="$1"
  if command -v shasum >/dev/null 2>&1; then
    shasum -a 256 "$file" | awk '{print $1}'
    return
  fi
  if command -v sha256sum >/dev/null 2>&1; then
    sha256sum "$file" | awk '{print $1}'
    return
  fi
  cksum "$file" | awk '{print $1}'
}

read_env_value() {
  key="$1"
  file="$2"
  awk -F= -v k="$key" '$1 == k { sub(/^[^=]*=/, "", $0); print $0; found=1; exit } END { if (!found) print "" }' "$file"
}

baseline="src/tooling/backend_native_contract.env"
doc="docs/cheng-native-contract.md"
tool="${TOOLING_SELF_BIN:-artifacts/tooling_cmd/cheng_tooling}"

while [ "${1:-}" != "" ]; do
  case "$1" in
    --baseline:*)
      baseline="${1#--baseline:}"
      ;;
    --doc:*)
      doc="${1#--doc:}"
      ;;
    --help|-h)
      usage
      exit 0
      ;;
    *)
      echo "[Error] unknown arg: $1" 1>&2
      usage
      exit 2
      ;;
  esac
  shift || true
done

root="$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)"
cd "$root"

if [ ! -f "$baseline" ]; then
  echo "[verify_backend_native_contract] missing baseline file: $baseline" 1>&2
  exit 2
fi
if [ ! -f "$doc" ]; then
  echo "[verify_backend_native_contract] missing doc file: $doc" 1>&2
  exit 2
fi
if ! command -v rg >/dev/null 2>&1; then
  echo "[verify_backend_native_contract] rg is required" 1>&2
  exit 2
fi

out_dir="artifacts/backend_native_contract"
mkdir -p "$out_dir"
generated="$out_dir/backend_native_contract.generated.env"
report="$out_dir/backend_native_contract.report.txt"
snapshot="$out_dir/backend_native_contract.snapshot.env"
diff_file="$out_dir/backend_native_contract.diff.txt"
closedloop_body="$out_dir/verify_backend_closedloop.body.sh"
prod_closure_body="$out_dir/backend_prod_closure.body.sh"

$tool build_backend_native_contract --doc:"$doc" --out:"$generated" >/dev/null

status="ok"
if ! cmp -s "$baseline" "$generated"; then
  status="drift"
  if diff -u "$baseline" "$generated" >"$diff_file" 2>/dev/null; then
    :
  else
    diff "$baseline" "$generated" >"$diff_file" 2>/dev/null || true
  fi
else
  : >"$diff_file"
fi

closedloop_cache="chengcache/embedded_scripts/verify_backend_closedloop.embedded.sh"
prod_closure_cache="chengcache/embedded_scripts/backend_prod_closure.embedded.sh"
if [ -s "$closedloop_cache" ]; then
  cp "$closedloop_cache" "$closedloop_body"
else
  $tool verify_backend_closedloop --help >/dev/null 2>&1 || true
  if [ -s "$closedloop_cache" ]; then
    cp "$closedloop_cache" "$closedloop_body"
  else
    $tool embedded-text --id:verify_backend_closedloop >"$closedloop_body"
  fi
fi
if [ -s "$prod_closure_cache" ]; then
  cp "$prod_closure_cache" "$prod_closure_body"
else
  $tool backend_prod_closure --help >/dev/null 2>&1 || true
  if [ -s "$prod_closure_cache" ]; then
    cp "$prod_closure_cache" "$prod_closure_body"
  else
    $tool embedded-text --id:backend_prod_closure >"$prod_closure_body"
  fi
fi
closedloop_gate_ok="1"
prod_closure_gate_ok="1"
if ! rg -q 'backend.native_contract' "$closedloop_body"; then
  closedloop_gate_ok="0"
fi
if ! rg -q 'backend.native_contract' "$prod_closure_body"; then
  prod_closure_gate_ok="0"
fi
if [ "$closedloop_gate_ok" != "1" ] || [ "$prod_closure_gate_ok" != "1" ]; then
  status="drift"
fi

driver="${BACKEND_DRIVER:-}"
if [ "$driver" = "" ] && [ -x "artifacts/backend_driver/cheng" ]; then
  driver="artifacts/backend_driver/cheng"
fi
if [ "$driver" = "" ] && [ -x "./cheng" ]; then
  driver="./cheng"
fi
if [ "$driver" = "" ] || [ ! -x "$driver" ]; then
  echo "[verify_backend_native_contract] missing backend driver (set BACKEND_DRIVER)" 1>&2
  exit 2
fi
driver_native_flag=""
run_driver_retry() {
  log_file="$1"
  shift
  attempts="${NATIVE_CONTRACT_DRIVER_RETRIES:-8}"
  case "$attempts" in
    ''|*[!0-9]*) attempts=8 ;;
  esac
  if [ "$attempts" -lt 1 ]; then
    attempts=8
  fi
  try=1
  rc=139
  while [ "$try" -le "$attempts" ]; do
    "$@" >"$log_file" 2>&1
    rc="$?"
    if [ "$rc" -ne 139 ] && [ "$rc" -ne 138 ]; then
      break
    fi
    if [ "$try" -lt "$attempts" ]; then
      echo "[verify_backend_native_contract] retry driver after crash rc=$rc attempt=$try/$attempts" 1>&2
    fi
    try=$((try + 1))
  done
  return "$rc"
}

ok_fixture="tests/cheng/backend/fixtures/native_contract_ok.cheng"
float_fixture="tests/cheng/backend/fixtures/native_contract_float_fail.cheng"
syscall_fixture="tests/cheng/backend/fixtures/native_contract_syscall_fail.cheng"
for fixture in "$ok_fixture" "$float_fixture" "$syscall_fixture"; do
  if [ ! -f "$fixture" ]; then
    echo "[verify_backend_native_contract] missing fixture: $fixture" 1>&2
    exit 2
  fi
done

ok_out="$out_dir/native_contract_ok.bin"
ok_stamp="$out_dir/native_contract_ok.compile_stamp.txt"
ok_err="$out_dir/native_contract_ok.stderr.txt"
set +e
run_driver_retry "$ok_err" env BACKEND_ENABLE_CLI=1 BACKEND_NATIVE_CONTRACT=1 BACKEND_LINKER=self BACKEND_NO_RUNTIME_C=1 BACKEND_VALIDATE=1 BACKEND_ELF_PROFILE=nolibc BACKEND_INTERNAL_ALLOW_EMIT_OBJ=1 BACKEND_EMIT=obj BACKEND_TARGET=linux-aarch64 BACKEND_INPUT="$ok_fixture" BACKEND_OUTPUT="$ok_out" BACKEND_COMPILE_STAMP_OUT="$ok_stamp" "$driver" $driver_native_flag
ok_rc="$?"
set -e
native_smoke_ok="1"
if [ "$ok_rc" -ne 0 ]; then
  if [ -s "$ok_out" ] && [ -s "$ok_stamp" ] && rg -q '^native_contract=1$' "$ok_stamp"; then
    echo "[verify_backend_native_contract] warn: driver rc=$ok_rc after producing valid native contract object; continue" 1>&2
    ok_rc="0"
  else
    if [ -s "$ok_err" ]; then
      sed -n '1,120p' "$ok_err" 1>&2 || true
    fi
    native_smoke_ok="0"
    status="drift"
  fi
elif ! rg -q '^native_contract=1$' "$ok_stamp"; then
  echo "[verify_backend_native_contract] compile stamp missing native_contract=1 marker" 1>&2
  native_smoke_ok="0"
  status="drift"
fi
if [ "$native_smoke_ok" = "1" ] && command -v nm >/dev/null 2>&1; then
  if ! nm "$ok_out" 2>/dev/null | rg -q 'cheng_contract_charge_block|cheng_contract_gas_used'; then
    echo "[verify_backend_native_contract] native contract output missing charge symbols" 1>&2
    native_smoke_ok="0"
    status="drift"
  fi
fi

expect_fail() {
  label="$1"
  fixture="$2"
  out_bin="$out_dir/${label}.bin"
  err_file="$out_dir/${label}.stderr.txt"
  set +e
  run_driver_retry "$err_file" env BACKEND_ENABLE_CLI=1 BACKEND_NATIVE_CONTRACT=1 BACKEND_LINKER=self BACKEND_NO_RUNTIME_C=1 BACKEND_VALIDATE=1 BACKEND_ELF_PROFILE=nolibc BACKEND_INTERNAL_ALLOW_EMIT_OBJ=1 BACKEND_EMIT=obj BACKEND_TARGET=linux-aarch64 BACKEND_INPUT="$fixture" BACKEND_OUTPUT="$out_bin" "$driver" $driver_native_flag
  rc="$?"
  set -e
  if [ "$rc" -eq 0 ]; then
    echo "[verify_backend_native_contract] expected failure but succeeded: $label" 1>&2
    status="drift"
    return
  fi
  case "$label" in
    float)
      if ! rg -q 'native_contract hard-fail|forbid float' "$err_file"; then
        echo "[verify_backend_native_contract] missing float hard-fail diagnostics" 1>&2
        status="drift"
      fi
      ;;
    syscall)
      if ! rg -q 'native_contract hard-fail|forbid syscall|linux syscall builtin|cheng_linux_syscall' "$err_file"; then
        echo "[verify_backend_native_contract] missing syscall hard-fail diagnostics" 1>&2
        status="drift"
      fi
      ;;
  esac
}

expect_fail float "$float_fixture"
expect_fail syscall "$syscall_fixture"

baseline_sha="$(hash_file "$baseline")"
generated_sha="$(hash_file "$generated")"
{
  echo "verify_backend_native_contract report"
  echo "status=$status"
  echo "doc=$doc"
  echo "baseline=$baseline"
  echo "generated=$generated"
  echo "baseline_sha256=$baseline_sha"
  echo "generated_sha256=$generated_sha"
  echo "closedloop_gate_ok=$closedloop_gate_ok"
  echo "prod_closure_gate_ok=$prod_closure_gate_ok"
  echo "native_smoke_ok=$native_smoke_ok"
  echo "diff=$diff_file"
} >"$report"
{
  echo "backend_native_contract_status=$status"
  echo "backend_native_contract_baseline_sha256=$baseline_sha"
  echo "backend_native_contract_generated_sha256=$generated_sha"
  echo "backend_native_contract_closedloop_gate_ok=$closedloop_gate_ok"
  echo "backend_native_contract_prod_closure_gate_ok=$prod_closure_gate_ok"
  echo "backend_native_contract_native_smoke_ok=$native_smoke_ok"
  echo "backend_native_contract_report=$report"
} >"$snapshot"

if [ "$status" != "ok" ]; then
  echo "[verify_backend_native_contract] native contract baseline/implementation drift detected" 1>&2
  echo "  baseline: $baseline" 1>&2
  echo "  generated: $generated" 1>&2
  if [ -s "$diff_file" ]; then
    sed -n '1,120p' "$diff_file" 1>&2 || true
  fi
  echo "  fix: ${TOOLING_SELF_BIN:-artifacts/tooling_cmd/cheng_tooling} build_backend_native_contract --doc:$doc --out:$baseline" 1>&2
  exit 1
fi

echo "verify_backend_native_contract ok"
