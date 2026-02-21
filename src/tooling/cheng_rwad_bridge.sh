#!/usr/bin/env bash
. "$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)/env_prefix_bridge.sh"
set -euo pipefail

usage() {
  cat <<'EOF'
usage: cheng_rwad_bridge.sh <command> [args]

commands:
  export --epoch:<n> [--ledger:<path>] [--root:<dir>] [--out:<file>]
         [--batch-id:<id>] [--schema:<id>] [--top:<n>]
  apply  --result:<file> [--batch:<file>] [--out:<file>] [--batch-id:<id>] [--require-status:<status>]
         [--require-result-schema:<id>] [--require-request-schema:<id>]
EOF
}

REPO_ROOT="$(CDPATH= cd -- "$(dirname -- "${BASH_SOURCE[0]}")/../.." && pwd)"

resolve_chengc_bin() {
  local name="$1"
  case "$name" in
    /*|*/*)
      printf '%s\n' "$name"
      return
      ;;
  esac
  printf '%s/artifacts/chengc/%s\n' "$REPO_ROOT" "$name"
}

cheng_storage_bin="${STORAGE_BIN:-$(resolve_chengc_bin cheng_storage)}"

ensure_cheng_storage_bin() {
  if [ -x "$cheng_storage_bin" ] && [ "$REPO_ROOT/src/tooling/cheng_storage.cheng" -ot "$cheng_storage_bin" ]; then
    return 0
  fi
  (cd "$REPO_ROOT" && sh src/tooling/chengc.sh src/tooling/cheng_storage.cheng --name:cheng_storage >/dev/null)
  if [ ! -x "$cheng_storage_bin" ]; then
    echo "export: missing cheng_storage binary: $cheng_storage_bin" 1>&2
    return 1
  fi
  return 0
}

json_escape() {
  printf '%s' "$1" | sed 's/\\/\\\\/g; s/"/\\"/g'
}

extract_json_string() {
  local key="$1"
  local file="$2"
  grep -o "\"${key}\"[[:space:]]*:[[:space:]]*\"[^\"]*\"" "$file" | head -n 1 | sed -n 's/^[^"]*"[^"]*"[[:space:]]*:[[:space:]]*"\([^"]*\)"$/\1/p'
}

extract_json_int() {
  local key="$1"
  local file="$2"
  grep -o "\"${key}\"[[:space:]]*:[[:space:]]*-*[0-9][0-9]*" "$file" | head -n 1 | sed -n 's/^[^0-9-]*\(-*[0-9][0-9]*\)$/\1/p'
}

extract_json_object() {
  local key="$1"
  local file="$2"
  sed -n "s/.*\"${key}\"[[:space:]]*:[[:space:]]*\\({[^}]*}\\).*/\\1/p" "$file" | head -n 1
}

sha256_file() {
  local file="$1"
  if command -v shasum >/dev/null 2>&1; then
    shasum -a 256 "$file" | awk '{print $1}'
    return
  fi
  if command -v sha256sum >/dev/null 2>&1; then
    sha256sum "$file" | awk '{print $1}'
    return
  fi
  return 1
}

cmd_export() {
  local root="build/cheng_storage"
  local epoch="-1"
  local ledger=""
  local out=""
  local top="0"
  local batch_id=""
  local schema="cheng-rwad-settlement/v1"

  while [ $# -gt 0 ]; do
    case "$1" in
      --root:*) root="${1#--root:}" ;;
      --epoch:*) epoch="${1#--epoch:}" ;;
      --ledger:*) ledger="${1#--ledger:}" ;;
      --out:*) out="${1#--out:}" ;;
      --top:*) top="${1#--top:}" ;;
      --batch-id:*) batch_id="${1#--batch-id:}" ;;
      --schema:*) schema="${1#--schema:}" ;;
      *) echo "export: unknown arg: $1" 1>&2; return 1 ;;
    esac
    shift
  done

  if [ -z "$ledger" ]; then
    ledger="$(cd "$(dirname "$root")" >/dev/null 2>&1 && pwd)/$(basename "$root")/ledger.jsonl"
    if [ ! -f "$ledger" ] && [ -d "$root" ]; then
      ledger="$(cd "$root" >/dev/null 2>&1 && pwd)/ledger.jsonl"
    fi
  fi

  if [ -z "$batch_id" ]; then
    if [ "$epoch" = "-1" ]; then
      batch_id="cheng-epoch-all"
    else
      batch_id="cheng-epoch-$epoch"
    fi
  fi

  local tmp
  tmp="$(mktemp)"
  ensure_cheng_storage_bin || return 1
  "$cheng_storage_bin" settle --epoch:"$epoch" --ledger:"$ledger" --root:"$root" --format:json --top:"$top" --out:"$tmp"
  local settlement_sha256
  settlement_sha256="$(sha256_file "$tmp")"
  if [ -z "$settlement_sha256" ]; then
    echo "export: failed to compute settlement sha256" 1>&2
    rm -f "$tmp"
    return 1
  fi

  local payload
  payload="$(mktemp)"
  {
    printf '{"schema_version":"%s","source":"cheng-lang/cheng_rwad_bridge.sh","batch_id":"%s","epoch":%s,"ledger_path":"%s","settlement_sha256":"%s","settlement":' \
      "$(json_escape "$schema")" "$(json_escape "$batch_id")" "$epoch" "$(json_escape "$ledger")" "$(json_escape "$settlement_sha256")"
    cat "$tmp"
    printf '}\n'
  } > "$payload"

  if [ -n "$out" ]; then
    mkdir -p "$(dirname "$out")"
    cp "$payload" "$out"
  else
    cat "$payload"
  fi
  rm -f "$tmp" "$payload"
}

cmd_apply() {
  local result=""
  local batch_file=""
  local out=""
  local batch_id_expect=""
  local require_status=""
  local require_result_schema="rwad-cheng-settlement-result/v1"
  local require_request_schema="cheng-rwad-settlement/v1"
  while [ $# -gt 0 ]; do
    case "$1" in
      --result:*) result="${1#--result:}" ;;
      --batch:*) batch_file="${1#--batch:}" ;;
      --out:*) out="${1#--out:}" ;;
      --batch-id:*) batch_id_expect="${1#--batch-id:}" ;;
      --require-status:*) require_status="${1#--require-status:}" ;;
      --require-result-schema:*) require_result_schema="${1#--require-result-schema:}" ;;
      --require-request-schema:*) require_request_schema="${1#--require-request-schema:}" ;;
      *) echo "apply: unknown arg: $1" 1>&2; return 1 ;;
    esac
    shift
  done

  if [ -z "$result" ]; then
    echo "apply: missing --result" 1>&2
    return 1
  fi
  if [ ! -f "$result" ]; then
    echo "apply: missing result file: $result" 1>&2
    return 1
  fi
  if [ -n "$batch_file" ] && [ ! -f "$batch_file" ]; then
    echo "apply: missing batch file: $batch_file" 1>&2
    return 1
  fi

  local result_schema
  local request_schema
  local batch_id
  local status
  local settlement_sha256
  local epoch
  local totals
  result_schema="$(extract_json_string "schema_version" "$result")"
  request_schema="$(extract_json_string "request_schema" "$result")"
  batch_id="$(extract_json_string "batch_id" "$result")"
  status="$(extract_json_string "status" "$result")"
  settlement_sha256="$(extract_json_string "settlement_sha256" "$result")"
  epoch="$(extract_json_int "epoch" "$result")"
  totals="$(extract_json_object "totals" "$result")"
  if [ -z "$epoch" ]; then
    epoch="0"
  fi

  if [ -z "$result_schema" ]; then
    echo "apply: missing schema_version" 1>&2
    return 1
  fi
  if [ -z "$request_schema" ]; then
    echo "apply: missing request_schema" 1>&2
    return 1
  fi
  if [ -z "$batch_id" ]; then
    echo "apply: missing batch_id" 1>&2
    return 1
  fi
  if [ -z "$settlement_sha256" ]; then
    echo "apply: missing settlement_sha256" 1>&2
    return 1
  fi
  if [ -n "$require_result_schema" ] && [ "$result_schema" != "$require_result_schema" ]; then
    echo "apply: result schema mismatch: got=$result_schema expect=$require_result_schema" 1>&2
    return 1
  fi
  if [ -n "$require_request_schema" ] && [ "$request_schema" != "$require_request_schema" ]; then
    echo "apply: request schema mismatch: got=$request_schema expect=$require_request_schema" 1>&2
    return 1
  fi
  if [ -n "$batch_id_expect" ] && [ "$batch_id" != "$batch_id_expect" ]; then
    echo "apply: batch id mismatch: got=$batch_id expect=$batch_id_expect" 1>&2
    return 1
  fi
  if [ -n "$require_status" ] && [ "$status" != "$require_status" ]; then
    echo "apply: status mismatch: got=$status expect=$require_status" 1>&2
    return 1
  fi
  if [ -z "$totals" ]; then
    echo "apply: missing totals" 1>&2
    return 1
  fi

  if [ -n "$batch_file" ]; then
    local batch_schema
    local batch_id_from_file
    local batch_sha256
    batch_schema="$(extract_json_string "schema_version" "$batch_file")"
    batch_id_from_file="$(extract_json_string "batch_id" "$batch_file")"
    batch_sha256="$(extract_json_string "settlement_sha256" "$batch_file")"
    if [ -z "$batch_schema" ] || [ -z "$batch_id_from_file" ] || [ -z "$batch_sha256" ]; then
      echo "apply: invalid batch file payload" 1>&2
      return 1
    fi
    if [ -n "$require_request_schema" ] && [ "$batch_schema" != "$require_request_schema" ]; then
      echo "apply: batch schema mismatch: got=$batch_schema expect=$require_request_schema" 1>&2
      return 1
    fi
    if [ -z "$batch_id_expect" ]; then
      batch_id_expect="$batch_id_from_file"
    fi
    if [ "$batch_id" != "$batch_id_from_file" ]; then
      echo "apply: batch id mismatch between result and batch: result=$batch_id batch=$batch_id_from_file" 1>&2
      return 1
    fi
    if [ "$settlement_sha256" != "$batch_sha256" ]; then
      echo "apply: settlement_sha256 mismatch between result and batch" 1>&2
      return 1
    fi
  fi

  local ack
  ack="$(mktemp)"
  printf '{"ok":true,"schema_version":"cheng-rwad-ack/v1","applied_from":"%s","request_schema":"%s","batch_id":"%s","status":"%s","epoch":%s,"settlement_sha256":"%s","totals":%s,"result_file":"%s"}\n' \
    "$(json_escape "$result_schema")" "$(json_escape "$request_schema")" "$(json_escape "$batch_id")" "$(json_escape "$status")" "$epoch" "$(json_escape "$settlement_sha256")" "$totals" "$(json_escape "$result")" > "$ack"

  if [ -n "$out" ]; then
    mkdir -p "$(dirname "$out")"
    cp "$ack" "$out"
  else
    cat "$ack"
  fi
  rm -f "$ack"
}

main() {
  if [ $# -lt 1 ]; then
    usage
    return 1
  fi
  local cmd="$1"
  shift
  case "$cmd" in
    export) cmd_export "$@" ;;
    apply) cmd_apply "$@" ;;
    --help|-h|help) usage ;;
    *) echo "unknown command: $cmd" 1>&2; usage; return 1 ;;
  esac
}

main "$@"
