#!/usr/bin/env sh
. "$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)/env_prefix_bridge.sh"
set -euo pipefail

usage() {
  cat <<'EOF'
usage: src/tooling/cheng_pkg_publish.sh --src:<dir> --package:<id> --author:<id> --channel:<edge|stable|lts> \
  --epoch:<n> --priv:<key> [--format:<tar|source>] [--source-addr:<addr>] [--manifest-out:<file>] \
  [--registry:<path>] [--storage-root:<dir>] [--mode:local|p2p] [--listen:<addr>] [--peer:<addr>] \
  [--artifact:<file>]

Notes:
  - Packs a package directory (tar) or publishes source manifest (source) and publishes a registry snapshot.
  - format=tar uses a .tar.gz snapshot; format=source stores a manifest and serves files read-only.
EOF
}

ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
CHENGC="$ROOT/src/tooling/chengc.sh"

resolve_chengc_bin() {
  name="$1"
  case "$name" in
    /*|*/*)
      printf '%s\n' "$name"
      return
      ;;
  esac
  printf '%s/artifacts/chengc/%s\n' "$ROOT" "$name"
}

src=""
package_id=""
author_id=""
channel=""
epoch=""
priv_key=""
registry_path="build/cheng_registry/registry.jsonl"
storage_root="build/cheng_storage"
mode="${PKG_MODE:-local}"
listen_addr="${PKG_LISTEN:-}"
peer_args=""
artifact=""
format="tar"
source_addrs=""
manifest_out=""

while [ "${1:-}" != "" ]; do
  case "$1" in
    --src:*)
      src="${1#--src:}"
      ;;
    --package:*)
      package_id="${1#--package:}"
      ;;
    --author:*)
      author_id="${1#--author:}"
      ;;
    --channel:*)
      channel="${1#--channel:}"
      ;;
    --epoch:*)
      epoch="${1#--epoch:}"
      ;;
    --priv:*)
      priv_key="${1#--priv:}"
      ;;
    --registry:*)
      registry_path="${1#--registry:}"
      ;;
    --storage-root:*)
      storage_root="${1#--storage-root:}"
      ;;
    --mode:*)
      mode="${1#--mode:}"
      ;;
    --listen:*)
      listen_addr="${1#--listen:}"
      ;;
    --peer:*)
      peer_args="$peer_args --peer:${1#--peer:}"
      ;;
    --artifact:*)
      artifact="${1#--artifact:}"
      ;;
    --format:*)
      format="${1#--format:}"
      ;;
    --source)
      format="source"
      ;;
    --source-addr:*)
      source_addrs="$source_addrs --source-addr:${1#--source-addr:}"
      ;;
    --manifest-out:*)
      manifest_out="${1#--manifest-out:}"
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

format="$(printf "%s" "$format" | tr 'A-Z' 'a-z')"
if [ "$format" != "tar" ] && [ "$format" != "source" ]; then
  echo "[Error] unknown format: $format" 1>&2
  exit 2
fi

if [ "$src" = "" ] || [ "$package_id" = "" ] || [ "$author_id" = "" ] || [ "$channel" = "" ] || [ "$epoch" = "" ] || [ "$priv_key" = "" ]; then
  echo "[Error] missing required args" 1>&2
  usage
  exit 2
fi

if [ ! -d "$src" ]; then
  echo "[Error] src dir not found: $src" 1>&2
  exit 2
fi

if [ ! -x "$CHENGC" ]; then
  echo "[Error] chengc.sh not found: $CHENGC" 1>&2
  exit 2
fi

pkg_source_bin="$(resolve_chengc_bin cheng_pkg_source)"
storage_bin="$(resolve_chengc_bin cheng_storage)"
registry_bin="$(resolve_chengc_bin cheng_registry)"

if [ "$format" = "source" ]; then
  if [ ! -x "$pkg_source_bin" ] || [ "$ROOT/src/tooling/cheng_pkg_source.cheng" -nt "$pkg_source_bin" ]; then
    (cd "$ROOT" && sh src/tooling/chengc.sh src/tooling/cheng_pkg_source.cheng --name:cheng_pkg_source)
  fi
  manifest_arg=""
  if [ "$manifest_out" != "" ]; then
    manifest_arg="--manifest-out:$manifest_out"
  fi
  listen_arg=""
  if [ "$listen_addr" != "" ]; then
    listen_arg="--storage-listen:$listen_addr"
  fi
  "$pkg_source_bin" publish --src:"$src" --package:"$package_id" --author:"$author_id" --channel:"$channel" \
    --epoch:"$epoch" --priv:"$priv_key" --registry:"$registry_path" --root:"$storage_root" --mode:"$mode" \
    $listen_arg $peer_args $source_addrs $manifest_arg
  exit 0
fi

sanitize_pkg() {
  echo "$1" | tr ':/.' '_' | tr -s '_'
}

if [ "$artifact" = "" ]; then
  safe_name="$(sanitize_pkg "$package_id")"
  artifact="$ROOT/chengcache/pkg_artifacts/${safe_name}-epoch${epoch}.tar.gz"
fi

sh "$ROOT/src/tooling/cheng_pkg_pack.sh" --src:"$src" --out:"$artifact"

if [ ! -x "$storage_bin" ] || [ "$ROOT/src/tooling/cheng_storage.cheng" -nt "$storage_bin" ]; then
  (cd "$ROOT" && sh src/tooling/chengc.sh src/tooling/cheng_storage.cheng --name:cheng_storage)
fi
if [ ! -x "$registry_bin" ] || [ "$ROOT/src/tooling/cheng_registry.cheng" -nt "$registry_bin" ]; then
  (cd "$ROOT" && sh src/tooling/chengc.sh src/tooling/cheng_registry.cheng --name:cheng_registry)
fi

listen_arg=""
if [ "$listen_addr" != "" ]; then
  listen_arg="--listen:$listen_addr"
fi

cid="$("$storage_bin" put --file:"$artifact" --root:"$storage_root" --mode:"$mode" $listen_arg $peer_args | tail -n 1)"
if [ "$cid" = "" ]; then
  echo "[Error] missing cid from cheng_storage" 1>&2
  exit 2
fi

"$registry_bin" publish --package:"$package_id" --author:"$author_id" --channel:"$channel" --epoch:"$epoch" \
  --cid:"$cid" --priv:"$priv_key" --format:tar --registry:"$registry_path"

echo "[pkg] published $package_id@$channel epoch=$epoch cid=$cid"
echo "[pkg] artifact: $artifact"
