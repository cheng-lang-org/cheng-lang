#!/usr/bin/env sh
. "$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)/env_prefix_bridge.sh"
set -eu
(set -o pipefail) 2>/dev/null && set -o pipefail

manifest="artifacts/backend_prod/release_manifest.json"
bundle="artifacts/backend_prod/backend_release.tar.gz"
dst="dist/releases"

while [ "${1:-}" != "" ]; do
  case "$1" in
    --manifest:*)
      manifest="${1#--manifest:}"
      ;;
    --bundle:*)
      bundle="${1#--bundle:}"
      ;;
    --dst:*)
      dst="${1#--dst:}"
      ;;
    --help|-h)
      echo "Usage: src/tooling/verify_backend_release_rollback_drill.sh [--manifest:<path>] [--bundle:<path>] [--dst:<dir>]" >&2
      exit 0
      ;;
    *)
      echo "[Error] unknown arg: $1" >&2
      exit 2
      ;;
  esac
  shift || true
done

root="$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)"
cd "$root"

if [ ! -f "$manifest" ] || [ ! -f "$bundle" ]; then
  echo "[verify_backend_release_rollback_drill] missing manifest/bundle: $manifest $bundle" >&2
  exit 1
fi

out_dir="artifacts/backend_release_rollback_drill"
mkdir -p "$out_dir"
report="$out_dir/backend_release_rollback_drill.report.txt"

before=""
if [ -f "$dst/current_id.txt" ]; then
  before="$(head -n 1 "$dst/current_id.txt" | tr -d '\r\n')"
fi
if [ "$before" = "" ] && [ -L "$dst/current" ]; then
  before="$(basename "$(readlink "$dst/current" 2>/dev/null || true)")"
fi

publish_once() {
  sh src/tooling/backend_release_publish.sh --manifest:"$manifest" --bundle:"$bundle" --dst:"$dst" >/dev/null
  if [ -f "$dst/current_id.txt" ]; then
    head -n 1 "$dst/current_id.txt" | tr -d '\r\n'
    return 0
  fi
  basename "$(readlink "$dst/current" 2>/dev/null || true)"
}

new_id="$(publish_once)"
rollback_target="$before"
restore_target="$new_id"

if [ "$rollback_target" = "" ] || [ "$rollback_target" = "$restore_target" ]; then
  rollback_target="$restore_target"
  restore_target="$(publish_once)"
fi

sh src/tooling/backend_release_rollback.sh --to:"$rollback_target" --root:"$dst" >/dev/null
rolled="$(head -n 1 "$dst/current_id.txt" | tr -d '\r\n')"
if [ "$rolled" != "$rollback_target" ]; then
  echo "[verify_backend_release_rollback_drill] rollback mismatch: expected=$rollback_target got=$rolled" >&2
  exit 1
fi

sh src/tooling/backend_release_rollback.sh --to:"$restore_target" --root:"$dst" >/dev/null
restored="$(head -n 1 "$dst/current_id.txt" | tr -d '\r\n')"
if [ "$restored" != "$restore_target" ]; then
  echo "[verify_backend_release_rollback_drill] restore mismatch: expected=$restore_target got=$restored" >&2
  exit 1
fi

{
  echo "verify_backend_release_rollback_drill report"
  echo "status=ok"
  echo "manifest=$manifest"
  echo "bundle=$bundle"
  echo "dst=$dst"
  echo "before=$before"
  echo "rollback_target=$rollback_target"
  echo "restore_target=$restore_target"
  echo "rolled=$rolled"
  echo "restored=$restored"
} >"$report"

echo "verify_backend_release_rollback_drill ok"
