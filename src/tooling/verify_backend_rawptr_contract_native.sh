#!/usr/bin/env sh
set -eu

if command -v rg >/dev/null 2>&1; then
  :
else
  echo "[verify_backend_rawptr_contract] rg is required" >&2
  exit 2
fi

ROOT_DIR="${TOOLING_ROOT:-$(pwd)}"
TOOL_BIN="${TOOLING_SELF_BIN:-$ROOT_DIR/artifacts/tooling_cmd/cheng_tooling.real}"
BASELINE="src/tooling/backend_rawptr_contract.env"
DOC="docs/raw-pointer-safety.md"
FORMAL_DOC="docs/cheng-formal-spec.md"
TOOLING_DOC="src/tooling/README.md"

case "$TOOL_BIN" in
  */artifacts/tooling_cmd/cheng_tooling|*/artifacts/tooling_cmd/cheng_tooling.bin)
    REAL_LAUNCHER_CANDIDATE="${TOOL_BIN%/*}/cheng_tooling.real"
    REALBIN_CANDIDATE="${TOOL_BIN%/*}/cheng_tooling.real.bin"
    if [ -x "$REAL_LAUNCHER_CANDIDATE" ]; then
      TOOL_BIN="$REAL_LAUNCHER_CANDIDATE"
    elif [ -x "$REALBIN_CANDIDATE" ]; then
      TOOL_BIN="$REALBIN_CANDIDATE"
    fi
    ;;
esac

for arg in "$@"; do
  case "$arg" in
    --baseline:*)
      BASELINE="${arg#--baseline:}"
      ;;
    --doc:*)
      DOC="${arg#--doc:}"
      ;;
    --formal-doc:*)
      FORMAL_DOC="${arg#--formal-doc:}"
      ;;
    --tooling-doc:*)
      TOOLING_DOC="${arg#--tooling-doc:}"
      ;;
    --help|-h)
      echo "verify_backend_rawptr_contract options:"
      echo "  --baseline:<path>    baseline env (default src/tooling/backend_rawptr_contract.env)"
      echo "  --doc:<path>         raw-pointer contract doc"
      echo "  --formal-doc:<path>  formal spec doc"
      echo "  --tooling-doc:<path> tooling README"
      exit 0
      ;;
    *)
      echo "[verify_backend_rawptr_contract] unknown arg: $arg" >&2
      exit 2
      ;;
  esac
done

cd "$ROOT_DIR"

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

for required in "$BASELINE" "$DOC" "$FORMAL_DOC" "$TOOLING_DOC" "src/tooling/cheng_tooling.cheng" "src/tooling/cheng_tooling_embedded_inline.cheng"; do
  if [ ! -f "$required" ]; then
    echo "[verify_backend_rawptr_contract] missing file: $required" >&2
    exit 2
  fi
done

out_dir="artifacts/backend_rawptr_contract"
rm -rf "$out_dir"
mkdir -p "$out_dir"
generated="$out_dir/backend_rawptr_contract.generated.env"
report="$out_dir/verify_backend_rawptr_contract.report.txt"
snapshot="$out_dir/verify_backend_rawptr_contract.snapshot.env"
diff_file="$out_dir/backend_rawptr_contract.diff.txt"

"$TOOL_BIN" build_backend_rawptr_contract --doc:"$DOC" --formal-doc:"$FORMAL_DOC" --tooling-doc:"$TOOLING_DOC" --out:"$generated" >/dev/null

status="ok"
if ! cmp -s "$BASELINE" "$generated"; then
  status="drift"
  if ! diff -u "$BASELINE" "$generated" >"$diff_file" 2>/dev/null; then
    diff "$BASELINE" "$generated" >"$diff_file" 2>/dev/null || true
  fi
else
  : >"$diff_file"
fi

required_gates="$(awk -F= '$1=="BACKEND_RAWPTR_CONTRACT_REQUIRED_GATES" {sub(/^[^=]*=/, "", $0); print $0; exit}' "$generated")"
if [ "$required_gates" = "" ]; then
  status="drift"
fi
if ! rg -q 'backend\.rawptr_contract' src/tooling/cheng_tooling.cheng; then
  status="drift"
fi
if ! rg -q 'verify_backend_rawptr_contract' src/tooling/cheng_tooling.cheng; then
  status="drift"
fi
if ! rg -q 'backend\.rawptr_contract' src/tooling/cheng_tooling_embedded_inline.cheng; then
  status="drift"
fi
if ! rg -q 'verify_backend_rawptr_contract' src/tooling/cheng_tooling_embedded_inline.cheng; then
  status="drift"
fi

baseline_sha="$(hash_file "$BASELINE")"
generated_sha="$(hash_file "$generated")"
{
  echo "status=$status"
  echo "baseline=$BASELINE"
  echo "generated=$generated"
  echo "required_gates=$required_gates"
  echo "baseline_sha256=$baseline_sha"
  echo "generated_sha256=$generated_sha"
  echo "diff=$diff_file"
} >"$report"
{
  echo "backend_rawptr_contract_status=$status"
  echo "backend_rawptr_contract_report=$report"
  echo "backend_rawptr_contract_diff=$diff_file"
} >"$snapshot"

if [ "$status" != "ok" ]; then
  echo "[verify_backend_rawptr_contract] rawptr contract baseline drift detected" >&2
  echo "  baseline: $BASELINE" >&2
  echo "  generated: $generated" >&2
  if [ -s "$diff_file" ]; then
    sed -n '1,160p' "$diff_file" >&2 || true
  fi
  echo "  fix: $TOOL_BIN build_backend_rawptr_contract --doc:$DOC --formal-doc:$FORMAL_DOC --tooling-doc:$TOOLING_DOC --out:$BASELINE" >&2
  exit 1
fi

echo "verify_backend_rawptr_contract ok"
