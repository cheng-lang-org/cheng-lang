#!/usr/bin/env sh
. "$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)/env_prefix_bridge.sh"
set -euo pipefail

usage() {
  cat <<'EOF'
Usage:
  src/tooling/verify_buildmeta.sh --buildmeta:<path> [--ledger:<path>]

Notes:
  - Validate buildmeta lock/pkgmeta/snapshot fields and ensure referenced files exist.
  - If --ledger is provided, check ledger.jsonl exists and contains events.
EOF
}

buildmeta=""
ledger=""

while [ "${1:-}" != "" ]; do
  case "$1" in
    --buildmeta:*)
      buildmeta="${1#--buildmeta:}"
      ;;
    --ledger:*)
      ledger="${1#--ledger:}"
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

if [ "$buildmeta" = "" ]; then
  echo "[Error] missing --buildmeta" 1>&2
  exit 2
fi
if [ ! -f "$buildmeta" ]; then
  echo "[Error] buildmeta not found: $buildmeta" 1>&2
  exit 2
fi

sha256_file() {
  if command -v shasum >/dev/null 2>&1; then
    shasum -a 256 "$1" | awk '{print $1}'
    return
  fi
  if command -v sha256sum >/dev/null 2>&1; then
    sha256sum "$1" | awk '{print $1}'
    return
  fi
  echo ""
}

toml_string_in_section() {
  section="$1"
  key="$2"
  file="$3"
  awk -v section="$section" -v key="$key" '
    $0 ~ /^\[/ {
      in_section = ($0 == "[" section "]")
      next
    }
    in_section {
      pattern = "^[[:space:]]*" key "[[:space:]]*=[[:space:]]*\""
      if ($0 ~ pattern) {
        sub("^[[:space:]]*" key "[[:space:]]*=[[:space:]]*\"", "")
        sub("\"[[:space:]]*$", "")
        print
        exit
      }
    }
  ' "$file"
}

toml_int_in_section() {
  section="$1"
  key="$2"
  file="$3"
  awk -v section="$section" -v key="$key" '
    $0 ~ /^\[/ {
      in_section = ($0 == "[" section "]")
      next
    }
    in_section {
      pattern = "^[[:space:]]*" key "[[:space:]]*=[[:space:]]*[0-9]"
      if ($0 ~ pattern) {
        sub("^[[:space:]]*" key "[[:space:]]*=[[:space:]]*", "")
        sub("[[:space:]]*$", "")
        print
        exit
      }
    }
  ' "$file"
}

lock_path="$(toml_string_in_section lock path "$buildmeta")"
lock_sha="$(toml_string_in_section lock sha256 "$buildmeta")"
if [ "$lock_path" != "" ]; then
  if [ ! -f "$lock_path" ]; then
    echo "[Error] lock file missing: $lock_path" 1>&2
    exit 2
  fi
  if [ "$lock_sha" != "" ]; then
    actual="$(sha256_file "$lock_path")"
    if [ "$actual" != "$lock_sha" ]; then
      echo "[Error] lock sha256 mismatch" 1>&2
      exit 2
    fi
  fi
fi

meta_path="$(toml_string_in_section pkgmeta path "$buildmeta")"
meta_sha="$(toml_string_in_section pkgmeta sha256 "$buildmeta")"
if [ "$meta_path" != "" ]; then
  if [ ! -f "$meta_path" ]; then
    echo "[Error] pkgmeta file missing: $meta_path" 1>&2
    exit 2
  fi
  if [ "$meta_sha" != "" ]; then
    actual="$(sha256_file "$meta_path")"
    if [ "$actual" != "$meta_sha" ]; then
      echo "[Error] pkgmeta sha256 mismatch" 1>&2
      exit 2
    fi
  fi
fi

snap_cid="$(toml_string_in_section snapshot cid "$buildmeta")"
snap_author="$(toml_string_in_section snapshot author_id "$buildmeta")"
snap_sig="$(toml_string_in_section snapshot signature "$buildmeta")"
snap_epoch="$(toml_int_in_section snapshot epoch "$buildmeta")"
if [ "$snap_cid" != "" ] || [ "$snap_author" != "" ] || [ "$snap_sig" != "" ]; then
  if [ "$snap_cid" = "" ] || [ "$snap_author" = "" ]; then
    echo "[Error] snapshot fields incomplete" 1>&2
    exit 2
  fi
  if [ "$snap_epoch" = "" ]; then
    echo "[Error] snapshot epoch missing" 1>&2
    exit 2
  fi
fi

if [ "$ledger" != "" ]; then
  if [ ! -f "$ledger" ]; then
    echo "[Error] ledger not found: $ledger" 1>&2
    exit 2
  fi
  if command -v rg >/dev/null 2>&1; then
    if ! rg -q "\"type\"" "$ledger"; then
      echo "[Error] ledger has no events" 1>&2
      exit 2
    fi
  else
    if ! grep -q "\"type\"" "$ledger"; then
      echo "[Error] ledger has no events" 1>&2
      exit 2
    fi
  fi
fi

echo "verify ok"
