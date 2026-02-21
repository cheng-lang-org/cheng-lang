#!/usr/bin/env sh
. "$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)/env_prefix_bridge.sh"
set -euo pipefail

usage() {
  cat <<'USAGE'
Usage:
  src/tooling/backend_release_manifest.sh [--out:<path>] [--name:<binName>]
                                         [--driver:<path>]

Notes:
  - Emits a backend release manifest (JSON).
  - By default it builds ./<binName> via build_backend_driver.sh.
  - Use --driver:<path> to record the SHA of a self-hosted stage2 driver (or any custom driver binary).
  - Uses SOURCE_DATE_EPOCH when provided for build_id; otherwise uses the latest git commit timestamp.
USAGE
}

out="artifacts/backend_prod/release_manifest.json"
name="cheng"
driver=""
backend_ir_version="${BACKEND_IR_VERSION:-uir-v1}"
backend_abi_version="${BACKEND_ABI_VERSION:-v2_noptr-aarch64}"

while [ "${1:-}" != "" ]; do
  case "$1" in
    --out:*)
      out="${1#--out:}"
      ;;
    --name:*)
      name="${1#--name:}"
      ;;
    --driver:*)
      driver="${1#--driver:}"
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

resolve_path() {
  p="$1"
  case "$p" in
    /*) printf '%s\n' "$p" ;;
    *) printf '%s\n' "$root/$p" ;;
  esac
}

# Determine which driver binary to hash.
driver_path="$driver"
if [ "$driver_path" = "" ]; then
  src/tooling/build_backend_driver.sh --name:"$name" >/dev/null
  driver_path="./$name"
fi

driver_abs="$(resolve_path "$driver_path")"
driver_sha=""
if [ -f "$driver_abs" ]; then
  driver_sha="$(sha256_file "$driver_abs")"
fi

build_id=""
build_epoch="${SOURCE_DATE_EPOCH:-}"
if [ "$build_epoch" = "" ] && command -v git >/dev/null 2>&1; then
  build_epoch="$(git log -1 --format=%ct 2>/dev/null || echo "")"
fi
if [ "$build_epoch" != "" ]; then
  if command -v python3 >/dev/null 2>&1; then
    build_id="$(python3 - "$build_epoch" <<'PY'
import datetime, sys
try:
    epoch = int(sys.argv[1])
except Exception:
    epoch = 0
print(datetime.datetime.fromtimestamp(epoch, tz=datetime.timezone.utc).strftime('%Y-%m-%dT%H:%M:%SZ'))
PY
)"
  elif command -v date >/dev/null 2>&1; then
    build_id="$(date -u -r "$build_epoch" +"%Y-%m-%dT%H:%M:%SZ" 2>/dev/null || date -u -d "@$build_epoch" +"%Y-%m-%dT%H:%M:%SZ" 2>/dev/null || echo "")"
  fi
fi
if [ "$build_id" = "" ] && command -v date >/dev/null 2>&1; then
  build_id="$(date -u +"%Y-%m-%dT%H:%M:%SZ" 2>/dev/null || echo "")"
fi
if [ "$build_id" = "" ]; then
  build_id="unknown"
fi

git_rev="unknown"
git_dirty="unknown"
if command -v git >/dev/null 2>&1; then
  git_rev="$(git rev-parse --short HEAD 2>/dev/null || echo unknown)"
  if git diff --quiet --ignore-submodules -- 2>/dev/null; then
    git_dirty="false"
  else
    git_dirty="true"
  fi
fi

host_os="$(uname -s 2>/dev/null || echo unknown)"
host_arch="$(uname -m 2>/dev/null || echo unknown)"
cc_path="$(command -v cc 2>/dev/null || echo cc)"
cc_version="$("$cc_path" --version 2>/dev/null | head -n 1 | tr -d '\r' || echo unknown)"

out_dir="$(dirname "$out")"
mkdir -p "$out_dir"

cat >"$out" <<EOF_JSON
{
  "build_id": "$build_id",
  "git_rev": "$git_rev",
  "git_dirty": "$git_dirty",
  "host_os": "$host_os",
  "host_arch": "$host_arch",
  "cc_path": "$cc_path",
  "cc_version": "$cc_version",
  "backend_ir_version": "$backend_ir_version",
  "backend_abi_version": "$backend_abi_version",
  "backend_driver": "$name",
  "backend_driver_sha256": "$driver_sha"
}
EOF_JSON

echo "backend release manifest ok: $out"
