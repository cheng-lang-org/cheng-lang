#!/usr/bin/env sh
. "$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)/env_prefix_bridge.sh"
set -euo pipefail

usage() {
  cat <<'EOF'
Usage:
  src/tooling/backend_release_sign.sh --manifest:<path> --bundle:<path>
                                     [--out-dir:<path>]
                                     [--key:<path>] [--pub:<path>]

Notes:
  - Signs the manifest and bundle via OpenSSL.
  - Default algorithm is Ed25519; if OpenSSL lacks Ed25519 support, falls back to RSA-SHA256.
  - If --key/--pub are not provided, uses (or generates):
      <out-dir>/signing_ed25519_priv.pem + <out-dir>/signing_ed25519_pub.pem
    or (fallback):
      <out-dir>/signing_rsa_priv.pem + <out-dir>/signing_rsa_pub.pem
  - Outputs:
      <out-dir>/<manifest>.sig + .sha256
      <out-dir>/<bundle>.sig + .sha256
  - Writes:
      <out-dir>/signing_algo.txt (ed25519 | rsa-sha256)
EOF
}

manifest=""
bundle=""
out_dir=""
key=""
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
    --key:*)
      key="${1#--key:}"
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
  echo "backend_release_sign skip: missing openssl" 1>&2
  exit 2
fi

supports_ed25519() {
  tmp_key="$(mktemp 2>/dev/null || true)"
  if [ "$tmp_key" = "" ]; then
    return 1
  fi
  rm -f "$tmp_key" >/dev/null 2>&1 || true
  tmp_key="$(mktemp -t cheng_ed25519_key 2>/dev/null || true)"
  if [ "$tmp_key" = "" ]; then
    return 1
  fi
  set +e
  openssl genpkey -algorithm ed25519 -out "$tmp_key" >/dev/null 2>&1
  status="$?"
  set -e
  rm -f "$tmp_key" >/dev/null 2>&1 || true
  [ "$status" -eq 0 ]
}

supports_ed25519_sign() {
  tmp_dir="$(mktemp -d "${TMPDIR:-/tmp}/cheng_release_sign.XXXXXX" 2>/dev/null || true)"
  if [ "$tmp_dir" = "" ] || [ ! -d "$tmp_dir" ]; then
    return 1
  fi
  tmp_key="$tmp_dir/ed25519_priv.pem"
  tmp_pub="$tmp_dir/ed25519_pub.pem"
  tmp_msg="$tmp_dir/msg.txt"
  tmp_sig="$tmp_dir/msg.sig"
  printf "cheng-release-sign-smoke\n" >"$tmp_msg"
  set +e
  openssl genpkey -algorithm ed25519 -out "$tmp_key" >/dev/null 2>&1
  s0=$?
  if [ "$s0" -eq 0 ]; then
    openssl pkey -in "$tmp_key" -pubout -out "$tmp_pub" >/dev/null 2>&1
    s1=$?
  else
    s1=1
  fi
  if [ "$s0" -eq 0 ] && [ "$s1" -eq 0 ]; then
    openssl pkeyutl -sign -inkey "$tmp_key" -in "$tmp_msg" -out "$tmp_sig" >/dev/null 2>&1
    s2=$?
  else
    s2=1
  fi
  if [ "$s2" -eq 0 ]; then
    openssl pkeyutl -verify -pubin -inkey "$tmp_pub" -sigfile "$tmp_sig" -in "$tmp_msg" >/dev/null 2>&1
    s3=$?
  else
    s3=1
  fi
  set -e
  rm -rf "$tmp_dir" >/dev/null 2>&1 || true
  [ "$s0" -eq 0 ] && [ "$s1" -eq 0 ] && [ "$s2" -eq 0 ] && [ "$s3" -eq 0 ]
}

explicit_sign_material="0"
if [ "$key" != "" ] || [ "$pub" != "" ]; then
  explicit_sign_material="1"
fi

if [ "$out_dir" = "" ]; then
  out_dir="$(dirname "$manifest")"
fi
mkdir -p "$out_dir"

algo_file="$out_dir/signing_algo.txt"
if [ "$algo" = "" ] && [ -f "$algo_file" ]; then
  algo="$(head -n 1 "$algo_file" | tr -d '\r\n' || true)"
fi
ed25519_ready="0"
if supports_ed25519 && supports_ed25519_sign; then
  ed25519_ready="1"
fi
if [ "$algo" = "" ]; then
  if [ "$ed25519_ready" = "1" ]; then
    algo="ed25519"
  else
    algo="rsa-sha256"
  fi
fi
if [ "$algo" = "ed25519" ] && [ "$ed25519_ready" != "1" ]; then
  if [ "$explicit_sign_material" = "1" ]; then
    echo "[Error] openssl cannot sign Ed25519 on this host (explicit key/pub provided)" 1>&2
    exit 1
  fi
  echo "[backend_release_sign] warn: Ed25519 sign unsupported on this host, fallback to rsa-sha256" 1>&2
  algo="rsa-sha256"
fi

if [ "$key" = "" ] && [ "$pub" = "" ]; then
  if [ "$algo" = "ed25519" ]; then
    key="$out_dir/signing_ed25519_priv.pem"
    pub="$out_dir/signing_ed25519_pub.pem"
  else
    key="$out_dir/signing_rsa_priv.pem"
    pub="$out_dir/signing_rsa_pub.pem"
  fi
fi

if [ ! -f "$key" ]; then
  if [ "$algo" = "ed25519" ]; then
    openssl genpkey -algorithm ed25519 -out "$key" >/dev/null
  else
    openssl genpkey -algorithm RSA -pkeyopt rsa_keygen_bits:2048 -out "$key" >/dev/null
  fi
  chmod 600 "$key" >/dev/null 2>&1 || true
fi
if [ ! -f "$pub" ]; then
  openssl pkey -in "$key" -pubout -out "$pub" >/dev/null
fi
printf "%s\n" "$algo" >"$algo_file"

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

if [ "$algo" = "ed25519" ]; then
  set +e
  openssl pkeyutl -sign -inkey "$key" -in "$manifest" -out "$sig_manifest" >/dev/null 2>&1
  sign_m_status=$?
  openssl pkeyutl -sign -inkey "$key" -in "$bundle" -out "$sig_bundle" >/dev/null 2>&1
  sign_b_status=$?
  set -e
  if [ "$sign_m_status" -ne 0 ] || [ "$sign_b_status" -ne 0 ]; then
    if [ "$explicit_sign_material" = "1" ]; then
      echo "[Error] Ed25519 signing failed with explicit key/pub" 1>&2
      exit 1
    fi
    echo "[backend_release_sign] warn: Ed25519 signing failed, fallback to rsa-sha256" 1>&2
    algo="rsa-sha256"
    key="$out_dir/signing_rsa_priv.pem"
    pub="$out_dir/signing_rsa_pub.pem"
    if [ ! -f "$key" ]; then
      openssl genpkey -algorithm RSA -pkeyopt rsa_keygen_bits:2048 -out "$key" >/dev/null
      chmod 600 "$key" >/dev/null 2>&1 || true
    fi
    if [ ! -f "$pub" ]; then
      openssl pkey -in "$key" -pubout -out "$pub" >/dev/null
    fi
    printf "%s\n" "$algo" >"$algo_file"
    openssl dgst -sha256 -sign "$key" -out "$sig_manifest" "$manifest" >/dev/null
    openssl dgst -sha256 -sign "$key" -out "$sig_bundle" "$bundle" >/dev/null
  fi
else
  openssl dgst -sha256 -sign "$key" -out "$sig_manifest" "$manifest" >/dev/null
  openssl dgst -sha256 -sign "$key" -out "$sig_bundle" "$bundle" >/dev/null
fi

sha_m="$(sha256_file "$manifest")"
sha_b="$(sha256_file "$bundle")"
if [ "$sha_m" != "" ]; then
  printf "%s  %s\n" "$sha_m" "$man_base" >"$sha_manifest"
fi
if [ "$sha_b" != "" ]; then
  printf "%s  %s\n" "$sha_b" "$bun_base" >"$sha_bundle"
fi

if [ "$algo" = "ed25519" ]; then
  openssl pkeyutl -verify -pubin -inkey "$pub" -sigfile "$sig_manifest" -in "$manifest" >/dev/null
  openssl pkeyutl -verify -pubin -inkey "$pub" -sigfile "$sig_bundle" -in "$bundle" >/dev/null
else
  openssl dgst -sha256 -verify "$pub" -signature "$sig_manifest" "$manifest" >/dev/null
  openssl dgst -sha256 -verify "$pub" -signature "$sig_bundle" "$bundle" >/dev/null
fi

echo "backend release sign ok: $sig_manifest $sig_bundle (algo=$algo)"
