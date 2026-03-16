#!/usr/bin/env sh
:
set -eu
(set -o pipefail) 2>/dev/null && set -o pipefail

root="$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)"
cd "$root"

fail() {
  echo "[verify_backend_rawptr_migration] $1" 1>&2
  exit 1
}

if ! command -v rg >/dev/null 2>&1; then
  fail "rg is required"
fi
if ! command -v perl >/dev/null 2>&1; then
  fail "perl is required"
fi

tool_exec="${TOOLING_SELF_BIN:-artifacts/tooling_cmd/cheng_tooling}"
tool_inline="src/tooling/rawptr_migrate_ffi.sh"
tool_repo_wrapper="src/tooling/cheng_tooling_embedded_scripts/rawptr_migrate_ffi.sh"
fixture_open_src="tests/cheng/backend/fixtures/compile_fail_ffi_importc_slice_openarray_i32.cheng"
fixture_risk_src="tests/cheng/backend/fixtures/compile_fail_ffi_slice_user_raw_ptr_surface.cheng"
doc_file="docs/raw-pointer-safety.md"
readme_file="src/tooling/README.md"
tooling_file="src/tooling/cheng_tooling.cheng"

for f in \
  "$tool_exec" \
  "$tool_inline" \
  "$tool_repo_wrapper" \
  "$fixture_open_src" \
  "$fixture_risk_src" \
  "$doc_file" \
  "$readme_file" \
  "$tooling_file"; do
  if [ ! -f "$f" ]; then
    fail "missing required file: $f"
  fi
done

if ! "$tool_exec" list | rg -qx 'rawptr_migrate_ffi'; then
  fail "rawptr_migrate_ffi command missing from tooling list"
fi
set +e
"$tool_exec" rawptr_migrate_ffi --help >/dev/null 2>&1
help_status="$?"
set -e
if [ "$help_status" -ne 0 ]; then
  fail "rawptr_migrate_ffi --help failed (status=$help_status)"
fi

out_dir="artifacts/backend_rawptr_migration"
mkdir -p "$out_dir"
tmp_dir="$out_dir/tmp"
rm -rf "$tmp_dir"
mkdir -p "$tmp_dir"

legacy_open="$tmp_dir/legacy_openarray.cheng"
legacy_risk="$tmp_dir/legacy_rawptr_surface.cheng"
cp "$fixture_open_src" "$legacy_open"
cp "$fixture_risk_src" "$legacy_risk"

apply_report="$out_dir/rawptr_migration_apply.report.txt"
apply_suggestions="$out_dir/rawptr_migration_apply.suggestions.txt"
apply_manifest="$out_dir/rawptr_migration_apply.backups.tsv"
apply_log="$out_dir/rawptr_migration_apply.log"
rollback_log="$out_dir/rawptr_migration_rollback.log"
risk_report="$out_dir/rawptr_migration_risk.report.txt"
risk_suggestions="$out_dir/rawptr_migration_risk.suggestions.txt"
risk_manifest="$out_dir/rawptr_migration_risk.backups.tsv"
risk_log="$out_dir/rawptr_migration_risk.log"
report="$out_dir/backend_rawptr_migration.report.txt"
snapshot="$out_dir/backend_rawptr_migration.snapshot.env"

rm -f "$apply_report" "$apply_suggestions" "$apply_manifest" "$apply_log" \
  "$rollback_log" "$risk_report" "$risk_suggestions" "$risk_manifest" "$risk_log" \
  "$report" "$snapshot"

set +e
"$tool_exec" rawptr_migrate_ffi \
  --file:"$legacy_open" \
  --apply \
  --check \
  --report:"$apply_report" \
  --suggestions:"$apply_suggestions" \
  --backup-manifest:"$apply_manifest" >"$apply_log" 2>&1
apply_status="$?"
set -e
if [ "$apply_status" -ne 0 ]; then
  sed -n '1,160p' "$apply_log" 1>&2 || true
  fail "migration apply/check failed (status=$apply_status)"
fi

if rg -q 'openArray\[' "$legacy_open"; then
  fail "apply mode did not rewrite legacy openArray syntax"
fi
if ! rg -q 'int32\[\]' "$legacy_open"; then
  fail "apply mode did not emit migrated T[] syntax"
fi
if [ ! -s "$apply_manifest" ]; then
  fail "missing backup manifest for apply mode"
fi
backup_path="$(awk -F '\t' 'NF >= 2 {print $2; exit}' "$apply_manifest")"
if [ "$backup_path" = "" ] || [ ! -f "$backup_path" ]; then
  fail "missing backup file recorded by migration apply"
fi

set +e
"$tool_exec" rawptr_migrate_ffi --rollback:"$apply_manifest" >"$rollback_log" 2>&1
rollback_status="$?"
set -e
if [ "$rollback_status" -ne 0 ]; then
  sed -n '1,160p' "$rollback_log" 1>&2 || true
  fail "migration rollback failed (status=$rollback_status)"
fi
if ! rg -q 'openArray\[' "$legacy_open"; then
  fail "rollback did not restore legacy openArray source"
fi

set +e
"$tool_exec" rawptr_migrate_ffi \
  --file:"$legacy_risk" \
  --check \
  --report:"$risk_report" \
  --suggestions:"$risk_suggestions" \
  --backup-manifest:"$risk_manifest" >"$risk_log" 2>&1
risk_status="$?"
set -e
if [ "$risk_status" -eq 0 ]; then
  fail "risk fixture unexpectedly passed --check"
fi
if ! rg -q '^status=needs_migration$' "$risk_report"; then
  fail "risk report missing needs_migration status"
fi
if ! rg -q '^risk_voidptr_hits=[1-9][0-9]*$' "$risk_report"; then
  fail "risk report missing void* findings"
fi
if ! rg -q '^risk_pointer_type_hits=[1-9][0-9]*$' "$risk_report"; then
  fail "risk report missing pointer-type findings"
fi
if ! rg -q '^risk_pointer_ops_hits=[1-9][0-9]*$' "$risk_report"; then
  fail "risk report missing pointer-op findings"
fi

if ! rg -q 'RPSPAR-07 Raw Pointer FFI 迁移' "$readme_file"; then
  fail "README missing RPSPAR-07 migration gate description"
fi
if ! rg -q 'cheng_tooling rawptr_migrate_ffi .*--apply' "$readme_file"; then
  fail "README missing migration apply command"
fi
if ! rg -q 'cheng_tooling rawptr_migrate_ffi --rollback:' "$readme_file"; then
  fail "README missing migration rollback command"
fi
if ! rg -q 'backend\.rawptr_migration' "$tooling_file"; then
  fail "tooling source missing backend.rawptr_migration gate wiring"
fi

{
  echo "status=ok"
  echo "tool=$tool_exec"
  echo "tool_inline=$tool_inline"
  echo "tool_repo_wrapper=$tool_repo_wrapper"
  echo "tooling_file=$tooling_file"
  echo "apply_report=$apply_report"
  echo "risk_report=$risk_report"
  echo "rollback_log=$rollback_log"
} >"$report"

{
  echo "backend_rawptr_migration_status=ok"
  echo "backend_rawptr_migration_report=$report"
} >"$snapshot"

echo "verify_backend_rawptr_migration ok"
