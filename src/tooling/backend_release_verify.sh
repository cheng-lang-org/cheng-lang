#!/usr/bin/env sh
. "$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)/env_prefix_bridge.sh"
set -euo pipefail

usage() {
  cat <<'EOF'
Usage:
  src/tooling/backend_release_verify.sh --manifest:<path> --bundle:<path>
                                       [--out-dir:<path>] [--pub:<path>]

Notes:
  - Verifies sha256 (if .sha256 files exist) and verifies Ed25519 signatures via OpenSSL.
  - Defaults:
      pub = <out-dir>/signing_ed25519_pub.pem (or signing_rsa_pub.pem)
      sig/sha files are expected in <out-dir> named:
        <manifest>.sig / <manifest>.sha256
        <bundle>.sig   / <bundle>.sha256
  - Algorithm:
      reads <out-dir>/signing_algo.txt if present (ed25519 | rsa-sha256),
      otherwise infers from pub key file existence.
EOF
}

manifest=""
bundle=""
out_dir=""
pub=""
algo=""

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
    --pub:*)
      pub="${1#--pub:}"
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

if ! command -v openssl >/dev/null 2>&1; then
  echo "backend_release_verify skip: missing openssl" 1>&2
  exit 2
fi

if [ "$out_dir" = "" ]; then
  out_dir="$(dirname "$manifest")"
fi
if [ "$pub" = "" ]; then
  if [ -f "$out_dir/signing_algo.txt" ]; then
    algo="$(head -n 1 "$out_dir/signing_algo.txt" | tr -d '\r\n' || true)"
  fi
  if [ "$algo" = "rsa-sha256" ]; then
    pub="$out_dir/signing_rsa_pub.pem"
  else
    pub="$out_dir/signing_ed25519_pub.pem"
    if [ ! -f "$pub" ] && [ -f "$out_dir/signing_rsa_pub.pem" ]; then
      algo="rsa-sha256"
      pub="$out_dir/signing_rsa_pub.pem"
    fi
  fi
fi
if [ "$algo" = "" ] && [ -f "$out_dir/signing_algo.txt" ]; then
  algo="$(head -n 1 "$out_dir/signing_algo.txt" | tr -d '\r\n' || true)"
fi
if [ "$algo" = "" ]; then
  algo="ed25519"
fi
if [ ! -f "$pub" ]; then
  echo "[Error] missing pub key: $pub" 1>&2
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

man_base="$(basename "$manifest")"
bun_base="$(basename "$bundle")"

sig_manifest="$out_dir/$man_base.sig"
sig_bundle="$out_dir/$bun_base.sig"
sha_manifest="$out_dir/$man_base.sha256"
sha_bundle="$out_dir/$bun_base.sha256"

if [ ! -f "$sig_manifest" ] || [ ! -f "$sig_bundle" ]; then
  echo "[Error] missing signature files in $out_dir" 1>&2
  exit 2
fi

if [ -f "$sha_manifest" ]; then
  expect="$(awk '{print $1}' "$sha_manifest")"
  actual="$(sha256_file "$manifest")"
  if [ "$expect" != "" ] && [ "$actual" != "" ] && [ "$expect" != "$actual" ]; then
    echo "[Error] manifest sha256 mismatch" 1>&2
    exit 1
  fi
fi
if [ -f "$sha_bundle" ]; then
  expect2="$(awk '{print $1}' "$sha_bundle")"
  actual2="$(sha256_file "$bundle")"
  if [ "$expect2" != "" ] && [ "$actual2" != "" ] && [ "$expect2" != "$actual2" ]; then
    echo "[Error] bundle sha256 mismatch" 1>&2
    exit 1
  fi
fi

if [ "$algo" = "rsa-sha256" ]; then
  openssl dgst -sha256 -verify "$pub" -signature "$sig_manifest" "$manifest" >/dev/null
  openssl dgst -sha256 -verify "$pub" -signature "$sig_bundle" "$bundle" >/dev/null
else
  openssl pkeyutl -verify -pubin -inkey "$pub" -sigfile "$sig_manifest" -in "$manifest" >/dev/null
  openssl pkeyutl -verify -pubin -inkey "$pub" -sigfile "$sig_bundle" -in "$bundle" >/dev/null
fi

echo "backend release verify ok (algo=$algo)"
