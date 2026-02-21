#!/usr/bin/env sh
. "$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)/env_prefix_bridge.sh"
set -euo pipefail

usage() {
  cat <<'EOF'
Usage:
  src/tooling/chengb.sh <file.cheng> [legacy chengb flags...]

Notes:
  - `chengb.sh` is now a compatibility wrapper around `src/tooling/chengc.sh`.
  - Legacy flags (`--emit/--frontend/--out/--multi/--pkg-roots/--run`) are forwarded.
  - When `--out` is omitted, default output stays under `artifacts/chengb/` for compatibility.
EOF
}

if [ "${1:-}" = "" ] || [ "${1:-}" = "--help" ] || [ "${1:-}" = "-h" ]; then
  usage
  exit 0
fi

in="$1"
shift || true

root="$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)"
cd "$root"

emit="exe"
has_out="0"
for arg in "$@"; do
  case "$arg" in
    --emit:*)
      emit="${arg#--emit:}"
      ;;
    --out:*)
      has_out="1"
      ;;
  esac
done

if [ "$has_out" = "0" ]; then
  base="$(basename "$in" .cheng)"
  out_dir="$root/artifacts/chengb"
  mkdir -p "$out_dir"
  case "$emit" in
    obj) default_out="$out_dir/${base}.o" ;;
    *) default_out="$out_dir/${base}" ;;
  esac
  set -- "$@" "--out:$default_out"
fi

exec sh src/tooling/chengc.sh "$in" "$@"
