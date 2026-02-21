#!/usr/bin/env sh
. "$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)/env_prefix_bridge.sh"
set -euo pipefail

usage() {
  cat <<'EOF'
Usage:
  src/tooling/backend_release_publish.sh --manifest:<path> --bundle:<path>
                                        [--out-dir:<path>] [--dst:<dir>]

Notes:
  - Publishes a signed release into dist/ for local "current/rollback" workflows.
  - Expects signature + sha files to exist in --out-dir (defaults to dirname(manifest)).
  - Writes:
      <dst>/<release_id>/{release_manifest.json, backend_release.tar.gz, *.sig, *.sha256, signing_ed25519_pub.pem, ...bundle files...}
      <dst>/current -> <release_id>
      <dst>/current_id.txt
EOF
}

manifest=""
bundle=""
out_dir=""
dst="dist/releases"

while [ "${1:-}" != "" ]; do
  case "$1" in
    --manifest:*)
      manifest="${1#--manifest:}"
      ;;
    --bundle:*)
      bundle="${1#--bundle:}"
      ;;
    --out-dir:*)
      out_dir="${1#--out-dir:}"
      ;;
    --dst:*)
      dst="${1#--dst:}"
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

if [ "$manifest" = "" ] || [ "$bundle" = "" ]; then
  echo "[Error] missing --manifest/--bundle" 1>&2
  usage
  exit 2
fi
if [ ! -f "$manifest" ]; then
  echo "[Error] manifest not found: $manifest" 1>&2
  exit 2
fi
if [ ! -f "$bundle" ]; then
  echo "[Error] bundle not found: $bundle" 1>&2
  exit 2
fi

root="$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)"
cd "$root"

if [ "$out_dir" = "" ]; then
  out_dir="$(dirname "$manifest")"
fi

pub_src="$out_dir/signing_ed25519_pub.pem"
pub_base="signing_ed25519_pub.pem"
algo_file="$out_dir/signing_algo.txt"
algo=""
if [ -f "$algo_file" ]; then
  algo="$(head -n 1 "$algo_file" | tr -d '\r\n' || true)"
fi
if [ "$algo" = "rsa-sha256" ]; then
  pub_src="$out_dir/signing_rsa_pub.pem"
  pub_base="signing_rsa_pub.pem"
else
  pub_src="$out_dir/signing_ed25519_pub.pem"
  pub_base="signing_ed25519_pub.pem"
  if [ ! -f "$pub_src" ] && [ -f "$out_dir/signing_rsa_pub.pem" ]; then
    algo="rsa-sha256"
    pub_src="$out_dir/signing_rsa_pub.pem"
    pub_base="signing_rsa_pub.pem"
  fi
fi
if [ ! -f "$pub_src" ]; then
  echo "[Error] missing signing pub key in $out_dir (expected ed25519 or rsa pub)" 1>&2
  exit 2
fi

man_base="$(basename "$manifest")"
bun_base="$(basename "$bundle")"
sig_manifest="$out_dir/$man_base.sig"
sig_bundle="$out_dir/$bun_base.sig"
sha_manifest="$out_dir/$man_base.sha256"
sha_bundle="$out_dir/$bun_base.sha256"

if [ ! -f "$sig_manifest" ] || [ ! -f "$sig_bundle" ]; then
  echo "[Error] missing signatures in $out_dir" 1>&2
  exit 2
fi

release_id="$(python3 - "$manifest" <<'PY'
import json, re, sys
path = sys.argv[1]
with open(path, "r", encoding="utf-8") as f:
    m = json.load(f)
build_id = m.get("build_id", "unknown")
git_rev = m.get("git_rev", "unknown")
rid = f"{build_id}_{git_rev}"
rid = re.sub(r"[^0-9A-Za-z._-]+", "_", rid)
rid = rid.strip("_") or "unknown"
print(rid)
PY
)"

dst_root="$dst"
mkdir -p "$dst_root"

dest="$dst_root/$release_id"
if [ -e "$dest" ]; then
  n=2
  while [ -e "${dest}_$n" ]; do
    n=$((n + 1))
  done
  dest="${dest}_$n"
fi

mkdir -p "$dest"
cp "$manifest" "$dest/$man_base"
cp "$bundle" "$dest/$bun_base"
cp "$sig_manifest" "$dest/$man_base.sig"
cp "$sig_bundle" "$dest/$bun_base.sig"
if [ -f "$sha_manifest" ]; then
  cp "$sha_manifest" "$dest/$man_base.sha256"
fi
if [ -f "$sha_bundle" ]; then
  cp "$sha_bundle" "$dest/$bun_base.sha256"
fi
cp "$pub_src" "$dest/$pub_base"
if [ -f "$algo_file" ]; then
  cp "$algo_file" "$dest/$(basename "$algo_file")"
fi

sh src/tooling/backend_release_verify.sh \
  --manifest:"$dest/$man_base" \
  --bundle:"$dest/$bun_base" \
  --out-dir:"$dest" \
  --pub:"$dest/$pub_base"

# Extract the minimal distribution payload in-place so dist/releases/<id>/ can be used directly.
tar -xzf "$dest/$bun_base" -C "$dest"

ln -sfn "$(basename "$dest")" "$dst_root/current"
printf "%s\n" "$(basename "$dest")" >"$dst_root/current_id.txt"

echo "backend release publish ok: $dest"
