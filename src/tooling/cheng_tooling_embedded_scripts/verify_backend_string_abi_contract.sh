#!/usr/bin/env sh
:
set -eu
(set -o pipefail) 2>/dev/null && set -o pipefail

root="$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)"
cd "$root"

tool="${TOOLING_SELF_BIN:-$root/artifacts/tooling_cmd/cheng_tooling}"
baseline="${BACKEND_STRING_ABI_CONTRACT_BASELINE:-src/tooling/backend_string_abi_contract.env}"
doc="${BACKEND_STRING_ABI_CONTRACT_DOC:-docs/backend-string-abi-contract.md}"
formal_doc="${BACKEND_STRING_ABI_CONTRACT_FORMAL_DOC:-docs/cheng-formal-spec.md}"
tooling_doc="${BACKEND_STRING_ABI_CONTRACT_TOOLING_DOC:-src/tooling/README.md}"
matrix_doc="${BACKEND_STRING_ABI_CONTRACT_MATRIX_DOC:-docs/backend-techdebt-matrix.md}"

for arg in "$@"; do
  case "$arg" in
    --help|-h)
      echo "Usage: verify_backend_string_abi_contract"
      echo "  env BACKEND_STRING_ABI_CONTRACT_BASELINE=<path>"
      echo "      BACKEND_STRING_ABI_CONTRACT_DOC=<path>"
      echo "      BACKEND_STRING_ABI_CONTRACT_FORMAL_DOC=<path>"
      echo "      BACKEND_STRING_ABI_CONTRACT_TOOLING_DOC=<path>"
      echo "      BACKEND_STRING_ABI_CONTRACT_MATRIX_DOC=<path>"
      exit 0
      ;;
    *)
      echo "[verify_backend_string_abi_contract] unknown arg: $arg" >&2
      exit 2
      ;;
  esac
done

if ! command -v rg >/dev/null 2>&1; then
  echo "[verify_backend_string_abi_contract] rg is required" >&2
  exit 2
fi

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

read_marker_value() {
  file="$1"
  marker_regex="$2"
  rg -o "${marker_regex}=[A-Za-z0-9_]+" "$file" | head -n 1 | cut -d= -f2 || true
}

require_marker_value() {
  file="$1"
  marker_regex="$2"
  marker_name="$3"
  expected="$4"
  value="$(read_marker_value "$file" "$marker_regex")"
  if [ "$value" != "$expected" ]; then
    echo "[verify_backend_string_abi_contract] ${marker_name} must be ${expected} in: $file" >&2
    exit 2
  fi
}

machine_types="src/backend/machine/machine_types.cheng"
isel_shared="src/backend/machine/select_internal/isel_parallel_shared.cheng"
coff_writer="src/backend/obj/coff_writer_x86_64.cheng"
elf_writer="src/backend/obj/elf_writer.cheng"
uir_codegen="src/backend/uir/uir_codegen.cheng"
tooling_file="src/tooling/cheng_tooling.cheng"
default_plan_file="src/tooling/cheng_tooling_default_plan.cheng"

for required in \
  "$baseline" \
  "$doc" \
  "$formal_doc" \
  "$tooling_doc" \
  "$matrix_doc" \
  "$machine_types" \
  "$isel_shared" \
  "$coff_writer" \
  "$elf_writer" \
  "$uir_codegen" \
  "$tooling_file" \
  "$default_plan_file"; do
  if [ ! -f "$required" ]; then
    echo "[verify_backend_string_abi_contract] missing file: $required" >&2
    exit 2
  fi
done

out_dir="artifacts/backend_string_abi_contract"
rm -rf "$out_dir"
mkdir -p "$out_dir"
generated="$out_dir/backend_string_abi_contract.generated.env"
report="$out_dir/backend_string_abi_contract.report.txt"
snapshot="$out_dir/backend_string_abi_contract.snapshot.env"
diff_file="$out_dir/backend_string_abi_contract.diff.txt"

tmp_markers="$(mktemp "${TMPDIR:-/tmp}/backend_string_abi_contract_markers.XXXXXX")"
tmp_source_markers="$(mktemp "${TMPDIR:-/tmp}/backend_string_abi_contract_sources.XXXXXX")"
cleanup() {
  rm -f "$tmp_markers" "$tmp_source_markers"
}
trap cleanup EXIT INT TERM

{
  rg -o 'string_abi_contract\.[a-z0-9_.]+=[A-Za-z0-9_]+' "$doc" || true
  rg -o 'string_abi_contract\.[a-z0-9_.]+=[A-Za-z0-9_]+' "$formal_doc" || true
  rg -o 'string_abi_contract\.[a-z0-9_.]+=[A-Za-z0-9_]+' "$tooling_doc" || true
  rg -o 'string_abi_contract\.[a-z0-9_.]+=[A-Za-z0-9_]+' "$matrix_doc" || true
} | LC_ALL=C sort -u >"$tmp_markers"

marker_count="$(wc -l < "$tmp_markers" | tr -d ' ')"
if [ "$marker_count" -eq 0 ]; then
  echo "[verify_backend_string_abi_contract] no contract markers found" >&2
  exit 2
fi

contract_version="$(read_marker_value "$doc" 'string_abi_contract\.version')"
if [ "$contract_version" = "" ]; then
  echo "[verify_backend_string_abi_contract] missing string_abi_contract.version marker in: $doc" >&2
  exit 2
fi

require_marker_value "$doc" 'string_abi_contract\.scheme\.id' 'string_abi_contract.scheme.id' 'SABI'
require_marker_value "$doc" 'string_abi_contract\.scheme\.name' 'string_abi_contract.scheme.name' 'backend_string_abi_contract'
require_marker_value "$doc" 'string_abi_contract\.scheme\.normative' 'string_abi_contract.scheme.normative' '1'
require_marker_value "$doc" 'string_abi_contract\.enforce\.mode' 'string_abi_contract.enforce.mode' 'default_verify'
require_marker_value "$doc" 'string_abi_contract\.language\.str' 'string_abi_contract.language.str' 'value_semantics'
require_marker_value "$doc" 'string_abi_contract\.abi\.cstring' 'string_abi_contract.abi.cstring' 'ffi_boundary'
require_marker_value "$doc" 'string_abi_contract\.nil_compare\.cstring_only' 'string_abi_contract.nil_compare.cstring_only' '1'
require_marker_value "$doc" 'string_abi_contract\.selector_cstring_lowering\.user_abi' 'string_abi_contract.selector_cstring_lowering.user_abi' '0'
require_marker_value "$doc" 'string_abi_contract\.closure' 'string_abi_contract.closure' 'required'
require_marker_value "$formal_doc" 'string_abi_contract\.scheme\.id' 'string_abi_contract.scheme.id' 'SABI'
require_marker_value "$formal_doc" 'string_abi_contract\.scheme\.name' 'string_abi_contract.scheme.name' 'backend_string_abi_contract'
require_marker_value "$formal_doc" 'string_abi_contract\.scheme\.normative' 'string_abi_contract.scheme.normative' '1'
require_marker_value "$formal_doc" 'string_abi_contract\.enforce\.mode' 'string_abi_contract.enforce.mode' 'default_verify'

if ! rg -q 'string_abi_contract\.formal_spec\.synced=1' "$formal_doc"; then
  echo "[verify_backend_string_abi_contract] missing formal spec sync marker: $formal_doc" >&2
  exit 2
fi
if ! rg -q 'string_abi_contract\.tooling_readme\.synced=1' "$tooling_doc"; then
  echo "[verify_backend_string_abi_contract] missing tooling README sync marker: $tooling_doc" >&2
  exit 2
fi
if ! rg -q 'string_abi_contract\.matrix\.synced=1' "$matrix_doc"; then
  echo "[verify_backend_string_abi_contract] missing matrix sync marker: $matrix_doc" >&2
  exit 2
fi
if ! rg -q 'string_abi_contract\.selector_cstring_lowering\.user_abi=0' "$tooling_doc"; then
  echo "[verify_backend_string_abi_contract] missing tooling README selector/user ABI marker" >&2
  exit 2
fi

{
  rg -o 'selector cstring lowering is always enabled on the production machine surface' "$machine_types" || true
  rg -o 'selector-owned cstring lowering materializes MachineCString payloads before object writers' "$isel_shared" || true
  rg -o 'Selector owns cstring materialization' "$coff_writer" || true
  rg -o 'ELF writer consumes selector-owned cstring materialization' "$elf_writer" || true
  rg -o 'selector_missing cstring payloads is a hard error' "$uir_codegen" || true
} | LC_ALL=C sort -u >"$tmp_source_markers"

selector_marker_count="$(wc -l < "$tmp_source_markers" | tr -d ' ')"
if [ "$selector_marker_count" -lt 5 ]; then
  echo "[verify_backend_string_abi_contract] missing selector-owned source markers" >&2
  exit 2
fi

doc_sha="$(hash_file "$doc")"
formal_doc_sha="$(hash_file "$formal_doc")"
tooling_doc_sha="$(hash_file "$tooling_doc")"
matrix_doc_sha="$(hash_file "$matrix_doc")"
tooling_sha="$(hash_file "$tooling_file")"
marker_sha="$(hash_file "$tmp_markers")"
source_marker_sha="$(hash_file "$tmp_source_markers")"

{
  echo "BACKEND_STRING_ABI_CONTRACT_BASELINE_VERSION=1"
  echo "BACKEND_STRING_ABI_CONTRACT_DOC=$doc"
  echo "BACKEND_STRING_ABI_CONTRACT_FORMAL_DOC=$formal_doc"
  echo "BACKEND_STRING_ABI_CONTRACT_TOOLING_DOC=$tooling_doc"
  echo "BACKEND_STRING_ABI_CONTRACT_MATRIX_DOC=$matrix_doc"
  echo "BACKEND_STRING_ABI_CONTRACT_DOC_SHA256=$doc_sha"
  echo "BACKEND_STRING_ABI_CONTRACT_FORMAL_DOC_SHA256=$formal_doc_sha"
  echo "BACKEND_STRING_ABI_CONTRACT_TOOLING_DOC_SHA256=$tooling_doc_sha"
  echo "BACKEND_STRING_ABI_CONTRACT_MATRIX_DOC_SHA256=$matrix_doc_sha"
  echo "BACKEND_STRING_ABI_CONTRACT_TOOLING_SHA256=$tooling_sha"
  echo "BACKEND_STRING_ABI_CONTRACT_VERSION=$contract_version"
  echo "BACKEND_STRING_ABI_CONTRACT_SCHEME_ID=SABI"
  echo "BACKEND_STRING_ABI_CONTRACT_SCHEME_NAME=backend_string_abi_contract"
  echo "BACKEND_STRING_ABI_CONTRACT_SCHEME_NORMATIVE=1"
  echo "BACKEND_STRING_ABI_CONTRACT_ENFORCE_MODE=default_verify"
  echo "BACKEND_STRING_ABI_CONTRACT_MARKER_COUNT=$marker_count"
  echo "BACKEND_STRING_ABI_CONTRACT_MARKER_SHA256=$marker_sha"
  echo "BACKEND_STRING_ABI_CONTRACT_SOURCE_MARKER_SHA256=$source_marker_sha"
  echo "BACKEND_STRING_ABI_CONTRACT_SELECTOR_MARKER_COUNT=$selector_marker_count"
  echo "BACKEND_STRING_ABI_CONTRACT_DEFAULT_VERIFY_GATES=verify_backend_string_abi_contract"
  echo "BACKEND_STRING_ABI_CONTRACT_CLOSURE_TIER=required"
} >"$generated"

status="ok"
if ! cmp -s "$baseline" "$generated"; then
  status="drift"
  if ! diff -u "$baseline" "$generated" >"$diff_file" 2>/dev/null; then
    diff "$baseline" "$generated" >"$diff_file" 2>/dev/null || true
  fi
else
  : >"$diff_file"
fi

default_verify_ok="1"
if ! rg -q 'add\((steps|out\.steps), "verify_backend_string_abi_contract"\)' "$tooling_file" "$default_plan_file"; then
  default_verify_ok="0"
  status="drift"
fi

prod_closure_required_ok="1"
if ! rg -q 'tooling_cmdBackendProdRunGate\(root, toolBin, "backend\.string_abi_contract"' "$tooling_file"; then
  prod_closure_required_ok="0"
  status="drift"
fi

source_markers_ok="1"
if ! rg -q 'selector cstring lowering is always enabled on the production machine surface' "$machine_types"; then
  source_markers_ok="0"
  status="drift"
fi
if ! rg -q 'selector-owned cstring lowering materializes MachineCString payloads before object writers' "$isel_shared"; then
  source_markers_ok="0"
  status="drift"
fi
if ! rg -q 'Selector owns cstring materialization' "$coff_writer"; then
  source_markers_ok="0"
  status="drift"
fi
if ! rg -q 'ELF writer consumes selector-owned cstring materialization' "$elf_writer"; then
  source_markers_ok="0"
  status="drift"
fi
if ! rg -q 'selector_missing cstring payloads is a hard error' "$uir_codegen"; then
  source_markers_ok="0"
  status="drift"
fi

smoke_fixture="tests/cheng/backend/fixtures/dot_cstring_contract.cheng"
smoke_ok="1"
smoke_driver="${BACKEND_DRIVER:-}"
release_fallback_driver="$root/dist/releases/current/cheng"
proof_meta_value() {
  driver_path="$1"
  key="$2"
  meta_path="$driver_path.meta"
  if [ ! -f "$meta_path" ]; then
    return 1
  fi
  sed -n "s/^$key=//p" "$meta_path" | sed -n '1p'
}
preferred_smoke_driver() {
  currentsrc_driver="$root/artifacts/backend_selfhost_self_obj/probe_currentsrc_proof/cheng.stage2.proof"
  strict_driver="$root/artifacts/backend_selfhost_self_obj/probe_prod.strict.noreuse/cheng.stage2.proof"
  currentsrc_publish_status=""

  if [ -x "$currentsrc_driver" ]; then
    currentsrc_publish_status="$(proof_meta_value "$currentsrc_driver" publish_status || true)"
  fi
  if [ -x "$currentsrc_driver" ] && [ "$currentsrc_publish_status" != "degraded" ]; then
    printf '%s\n' "$currentsrc_driver"
    return 0
  fi
  if [ -x "$strict_driver" ]; then
    printf '%s\n' "$strict_driver"
    return 0
  fi
  if [ -x "$currentsrc_driver" ]; then
    printf '%s\n' "$currentsrc_driver"
    return 0
  fi
  return 1
}
if [ "$smoke_driver" = "" ]; then
  smoke_driver="$(preferred_smoke_driver || true)"
fi
if [ "$smoke_driver" = "" ]; then
  smoke_driver="$("$tool" backend_driver_path --path-only 2>/dev/null | tail -n 1 | tr -d '\r' || true)"
fi
if [ "$smoke_driver" = "" ] && [ -x "$root/artifacts/backend_driver/cheng" ]; then
  smoke_driver="$root/artifacts/backend_driver/cheng"
fi
smoke_target="${BACKEND_TARGET:-$("$tool" detect_host_target 2>/dev/null || echo arm64-apple-darwin)}"
smoke_dev_exe="$out_dir/dot_cstring_contract.dev"
smoke_release_exe="$out_dir/dot_cstring_contract.release"
smoke_dev_log="$out_dir/dot_cstring_contract.dev.log"
smoke_release_log="$out_dir/dot_cstring_contract.release.log"
smoke_dev_run_log="$out_dir/dot_cstring_contract.dev.run.log"
smoke_release_run_log="$out_dir/dot_cstring_contract.release.run.log"
run_smoke_pair() {
  driver_bin="$1"
  rm -f "$smoke_dev_exe" "$smoke_release_exe" \
    "$smoke_dev_log" "$smoke_release_log" "$smoke_dev_run_log" "$smoke_release_run_log"
  if ! env \
    MM=orc \
    BACKEND_BUILD_TRACK=dev \
    BACKEND_STAGE1_PARSE_MODE=outline \
    BACKEND_FN_SCHED=ws \
    BACKEND_DIRECT_EXE=1 \
    BACKEND_LINKERLESS_INMEM=1 \
    BACKEND_FAST_DEV_PROFILE=1 \
    BACKEND_LINKER=self \
    BACKEND_INPUT="$smoke_fixture" \
    BACKEND_OUTPUT="$smoke_dev_exe" \
    "$driver_bin" >"$smoke_dev_log" 2>&1; then
    return 1
  fi
  if ! env \
    MM=orc \
    BACKEND_BUILD_TRACK=release \
    BACKEND_STAGE1_PARSE_MODE=full \
    BACKEND_FN_SCHED=ws \
    BACKEND_DIRECT_EXE=0 \
    BACKEND_LINKERLESS_INMEM=0 \
    BACKEND_LINKER=system \
    BACKEND_NO_RUNTIME_C=0 \
    BACKEND_INPUT="$smoke_fixture" \
    BACKEND_OUTPUT="$smoke_release_exe" \
    "$driver_bin" >"$smoke_release_log" 2>&1; then
    return 1
  fi
  if ! "$smoke_release_exe" >"$smoke_release_run_log" 2>&1; then
    return 1
  fi
  : >"$smoke_dev_run_log"
  printf '%s\n' "skip_dev_run=1" >>"$smoke_dev_run_log"
}
if [ ! -f "$smoke_fixture" ] || [ "$smoke_driver" = "" ] || [ ! -x "$smoke_driver" ]; then
  smoke_ok="0"
  status="drift"
else
  if ! run_smoke_pair "$smoke_driver"; then
    if [ "$release_fallback_driver" != "" ] && [ -x "$release_fallback_driver" ] &&
       [ "$release_fallback_driver" != "$smoke_driver" ]; then
      smoke_driver="$release_fallback_driver"
      if ! run_smoke_pair "$smoke_driver"; then
        smoke_ok="0"
        status="drift"
      fi
    else
      smoke_ok="0"
      status="drift"
    fi
  fi
fi

baseline_sha="$(hash_file "$baseline")"
generated_sha="$(hash_file "$generated")"

{
  echo "status=$status"
  echo "baseline=$baseline"
  echo "generated=$generated"
  echo "default_verify_ok=$default_verify_ok"
  echo "prod_closure_required_ok=$prod_closure_required_ok"
  echo "source_markers_ok=$source_markers_ok"
  echo "smoke_ok=$smoke_ok"
  echo "baseline_sha256=$baseline_sha"
  echo "generated_sha256=$generated_sha"
  echo "diff=$diff_file"
  echo "smoke_fixture=$smoke_fixture"
  echo "smoke_driver=$smoke_driver"
  echo "smoke_target=$smoke_target"
  echo "smoke_dev_log=$smoke_dev_log"
  echo "smoke_release_log=$smoke_release_log"
  echo "smoke_dev_run_log=$smoke_dev_run_log"
  echo "smoke_release_run_log=$smoke_release_run_log"
} >"$report"

{
  echo "backend_string_abi_contract_status=$status"
  echo "backend_string_abi_contract_default_verify_ok=$default_verify_ok"
  echo "backend_string_abi_contract_prod_closure_required_ok=$prod_closure_required_ok"
  echo "backend_string_abi_contract_source_markers_ok=$source_markers_ok"
  echo "backend_string_abi_contract_smoke_ok=$smoke_ok"
  echo "backend_string_abi_contract_report=$report"
  echo "backend_string_abi_contract_diff=$diff_file"
} >"$snapshot"

if [ "$status" != "ok" ]; then
  echo "[verify_backend_string_abi_contract] string ABI contract drift detected" >&2
  echo "  baseline: $baseline" >&2
  echo "  generated: $generated" >&2
  if [ -s "$diff_file" ]; then
    sed -n '1,160p' "$diff_file" >&2 || true
  fi
  if [ "$default_verify_ok" != "1" ]; then
    echo "  reason: default verify wiring missing verify_backend_string_abi_contract" >&2
  fi
  if [ "$prod_closure_required_ok" != "1" ]; then
    echo "  reason: backend_prod_closure missing required string ABI contract gate" >&2
  fi
  if [ "$source_markers_ok" != "1" ]; then
    echo "  reason: selector/backfill source markers drifted" >&2
  fi
  if [ "$smoke_ok" != "1" ]; then
    echo "  reason: dot_cstring_contract smoke failed" >&2
    for log in "$smoke_dev_log" "$smoke_dev_run_log" "$smoke_release_log" "$smoke_release_run_log"; do
      if [ -s "$log" ]; then
        echo "  log: $log" >&2
        sed -n '1,120p' "$log" >&2 || true
      fi
    done
  fi
  exit 1
fi

echo "verify_backend_string_abi_contract ok"
