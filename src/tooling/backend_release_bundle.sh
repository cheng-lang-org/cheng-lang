#!/usr/bin/env sh
. "$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)/env_prefix_bridge.sh"
set -euo pipefail

usage() {
  cat <<'USAGE'
Usage:
  src/tooling/backend_release_bundle.sh [--out:<path>] [--name:<binName>] [--manifest:<path>]
                                       [--driver:<path>] [--extra:<path> ...]

Notes:
  - Creates a minimal release bundle (tar.gz).
  - Bundle includes: backend driver + release manifest + optional extras + sha256sum file (if available).
  - By default it bundles ./<binName> built via build_backend_driver.sh.
  - Use --driver:<path> to bundle a self-hosted stage2 driver (or any custom driver binary).
  - Uses SOURCE_DATE_EPOCH (or git commit timestamp) to stabilize bundle mtimes when possible.
USAGE
}

out="artifacts/backend_prod/backend_release.tar.gz"
name="cheng"
manifest="artifacts/backend_prod/release_manifest.json"
driver=""
extras=""

while [ "${1:-}" != "" ]; do
  case "$1" in
    --out:*)
      out="${1#--out:}"
      ;;
    --name:*)
      name="${1#--name:}"
      ;;
    --manifest:*)
      manifest="${1#--manifest:}"
      ;;
    --driver:*)
      driver="${1#--driver:}"
      ;;
    --extra:*)
      extras="$extras\n${1#--extra:}"
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

resolve_path() {
  p="$1"
  case "$p" in
    /*) printf '%s\n' "$p" ;;
    *) printf '%s\n' "$root/$p" ;;
  esac
}

# Determine which driver binary to bundle.
driver_path="$driver"
if [ "$driver_path" = "" ]; then
  if [ ! -x "./$name" ]; then
    src/tooling/build_backend_driver.sh --name:"$name" >/dev/null
  fi
  driver_path="./$name"
fi

if [ ! -f "$manifest" ]; then
  echo "[Error] missing manifest: $manifest" 1>&2
  exit 2
fi

bundle_dir="$(dirname "$out")/bundle_tmp"
rm -rf "$bundle_dir"
mkdir -p "$bundle_dir"

cp "$(resolve_path "$driver_path")" "$bundle_dir/$name"
cp "$manifest" "$bundle_dir/$(basename "$manifest")"

extra_files=""
if [ "$extras" != "" ]; then
  printf '%b\n' "$extras" | while IFS= read -r p; do
    if [ "$p" = "" ]; then
      continue
    fi
    srcp="$(resolve_path "$p")"
    if [ ! -f "$srcp" ]; then
      echo "[Error] extra not found: $p" 1>&2
      exit 1
    fi
    base="$(basename "$srcp")"
    cp "$srcp" "$bundle_dir/$base"
    printf '%s\n' "$base"
  done >"$bundle_dir/.extras.list"
  if [ -s "$bundle_dir/.extras.list" ]; then
    extra_files="$(tr '\n' ' ' <"$bundle_dir/.extras.list" | tr -s ' ' | sed -E 's/[[:space:]]+$//' || true)"
  fi
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

sha_out="$bundle_dir/sha256sum.txt"
rm -f "$sha_out"
sha_any="0"

emit_sha() {
  f="$1"
  sha="$(sha256_file "$bundle_dir/$f")"
  if [ "$sha" != "" ]; then
    printf '%s  %s\n' "$sha" "$f" >>"$sha_out"
    sha_any="1"
  fi
}

emit_sha "$name"
emit_sha "$(basename "$manifest")"
if [ "$extra_files" != "" ]; then
  for f in $extra_files; do
    emit_sha "$f"
  done
fi

if [ "$sha_any" != "1" ]; then
  rm -f "$sha_out"
fi

out_dir="$(dirname "$out")"
mkdir -p "$out_dir"

# Stabilize mtimes inside the bundle when possible (for reproducible tarballs).
bundle_epoch="${SOURCE_DATE_EPOCH:-}"
if [ "$bundle_epoch" = "" ] && command -v git >/dev/null 2>&1; then
  bundle_epoch="$(git log -1 --format=%ct 2>/dev/null || echo "")"
fi
if [ "$bundle_epoch" != "" ] && command -v python3 >/dev/null 2>&1; then
  python3 - "$bundle_dir" "$bundle_epoch" <<'PY'
import os, sys
root = sys.argv[1]
try:
    epoch = int(sys.argv[2])
except Exception:
    epoch = 0
for base, dirs, files in os.walk(root):
    for n in dirs + files:
        p = os.path.join(base, n)
        try:
            os.utime(p, (epoch, epoch), follow_symlinks=False)
        except Exception:
            pass
PY
fi

# Create tarball with deterministic ordering + no gzip timestamps (best-effort).
tmp_tar="$out_dir/backend_release.tar"
rm -f "$tmp_tar" "$tmp_tar.gz" "$out" 2>/dev/null || true

files="$name $(basename "$manifest")"
if [ "$extra_files" != "" ]; then
  files="$files $extra_files"
fi
if [ -f "$bundle_dir/sha256sum.txt" ]; then
  files="$files sha256sum.txt"
fi

COPYFILE_DISABLE=1 tar -cf "$tmp_tar" -C "$bundle_dir" $files
if command -v gzip >/dev/null 2>&1; then
  gzip -n -f "$tmp_tar"
  mv "$tmp_tar.gz" "$out"
else
  tar -czf "$out" -C "$bundle_dir" .
fi
rm -rf "$bundle_dir"

echo "backend release bundle ok: $out"
