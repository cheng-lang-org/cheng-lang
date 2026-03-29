#!/usr/bin/env sh
:
set -eu
(set -o pipefail) 2>/dev/null && set -o pipefail

root="$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)"
cd "$root"

if ! command -v cc >/dev/null 2>&1; then
  echo "[verify_backend_stage1_fixed0_envs] missing cc" 1>&2
  exit 2
fi

tmpdir="$(mktemp -d "${TMPDIR:-/tmp}/backend_stage1_fixed0_envs.XXXXXX")"
cleanup() {
  rm -rf "$tmpdir" 2>/dev/null || true
}
trap cleanup EXIT INT TERM

driver="$tmpdir/backend_driver_fixed0_outer"
fixture="tests/cheng/backend/fixtures/return_add.cheng"
report_dir="artifacts/backend_stage1_fixed0_envs"
report="$report_dir/backend_stage1_fixed0_envs.report.txt"

mkdir -p "$report_dir"
rm -f "$report"

cc -std=c11 -Wno-deprecated-declarations -O0 \
  src/backend/tooling/backend_driver_sidecar_outer_main.c \
  src/backend/tooling/backend_driver_sidecar_outer_exports.c \
  src/runtime/native/system_helpers.c \
  -o "$driver"

expect_gate() {
  env_name="$1"
  env_value="$2"
  expect_msg="$3"
  log="$tmpdir/${env_name}.log"
  set +e
  env "$env_name=$env_value" "$driver" \
    --emit:exe \
    --target:arm64-apple-darwin \
    --frontend:stage1 \
    --linker:self \
    --no-runtime-c \
    --output:"$tmpdir/${env_name}.bin" \
    "$fixture" >"$log" 2>&1
  rc="$?"
  set -e
  if [ "$rc" -ne 2 ]; then
    echo "[verify_backend_stage1_fixed0_envs] expected rc=2 for $env_name, got $rc" 1>&2
    sed -n '1,80p' "$log" 1>&2 || true
    exit 1
  fi
  if ! rg -F "$expect_msg" "$log" >/dev/null 2>&1; then
    echo "[verify_backend_stage1_fixed0_envs] missing diagnostic for $env_name: $expect_msg" 1>&2
    sed -n '1,80p' "$log" 1>&2 || true
    exit 1
  fi
}

expect_no_gate() {
  log="$tmpdir/fixed0_neutral.log"
  set +e
  env STAGE1_SEM_FIXED_0=0 STAGE1_OWNERSHIP_FIXED_0=0 "$driver" >"$log" 2>&1
  rc="$?"
  set -e
  case "$rc" in
    0|1|2) ;;
    *)
      echo "[verify_backend_stage1_fixed0_envs] unexpected neutral rc=$rc" 1>&2
      sed -n '1,80p' "$log" 1>&2 || true
      exit 1
      ;;
  esac
  if rg -F "removed env STAGE1_SKIP_" "$log" >/dev/null 2>&1; then
    echo "[verify_backend_stage1_fixed0_envs] neutral run unexpectedly hit removed-env gate" 1>&2
    sed -n '1,80p' "$log" 1>&2 || true
    exit 1
  fi
  if rg -F "fixed=0" "$log" >/dev/null 2>&1; then
    echo "[verify_backend_stage1_fixed0_envs] neutral run unexpectedly hit fixed=0 gate" 1>&2
    sed -n '1,80p' "$log" 1>&2 || true
    exit 1
  fi
}

expect_gate "STAGE1_SKIP_SEM" "1" "backend_driver: removed env STAGE1_SKIP_SEM, use STAGE1_SEM_FIXED_0=0"
expect_gate "STAGE1_SKIP_OWNERSHIP" "1" "backend_driver: removed env STAGE1_SKIP_OWNERSHIP, use STAGE1_OWNERSHIP_FIXED_0=0"
expect_gate "STAGE1_SEM_FIXED_0" "1" "backend_driver: STAGE1_SEM_FIXED_0 is fixed=0"
expect_gate "STAGE1_OWNERSHIP_FIXED_0" "1" "backend_driver: STAGE1_OWNERSHIP_FIXED_0 is fixed=0"
expect_no_gate

{
  echo "result=ok"
  echo "driver=$driver"
  echo "fixture=$fixture"
  echo "verified_removed_envs=STAGE1_SKIP_SEM,STAGE1_SKIP_OWNERSHIP"
  echo "verified_fixed_zero_envs=STAGE1_SEM_FIXED_0,STAGE1_OWNERSHIP_FIXED_0"
} >"$report"

echo "verify_backend_stage1_fixed0_envs ok"
