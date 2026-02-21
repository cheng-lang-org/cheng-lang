#!/usr/bin/env sh
. "$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)/env_prefix_bridge.sh"
set -euo pipefail

usage() {
  cat <<'EOF'
usage: src/tooling/cheng_pkg_fetch.sh --lock:<file> [--cache:<dir>] [--root:<dir>] [--mode:local|p2p] \
  [--listen:<addr>] [--peer:<addr>] [--source-listen:<addr>] [--source-peer:<addr>] [--out:<file>] [--print-roots]

Notes:
  - Fetches package snapshots listed in a lock file.
  - Supports tarball (format=tar) and source (format=source) packages.
  - Extracts tarballs into cache/<cid>/ or pulls source files into cache/<cid>/.
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

lock=""
cache_root="${PKG_CACHE:-chengcache/packages}"
storage_root="${PKG_STORAGE_ROOT:-build/cheng_storage}"
mode="${PKG_MODE:-local}"
listen_addr="${PKG_LISTEN:-}"
peer_env="${PKG_PEERS:-}"
peer_args=""
source_listen_addr="${PKG_SOURCE_LISTEN:-}"
source_peer_env="${PKG_SOURCE_PEERS:-}"
source_peer_args=""
out=""
print_roots=""

while [ "${1:-}" != "" ]; do
  case "$1" in
    --lock:*)
      lock="${1#--lock:}"
      ;;
    --cache:*)
      cache_root="${1#--cache:}"
      ;;
    --root:*)
      storage_root="${1#--root:}"
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
    --source-listen:*)
      source_listen_addr="${1#--source-listen:}"
      ;;
    --source-peer:*)
      source_peer_args="$source_peer_args --source-peer:${1#--source-peer:}"
      ;;
    --out:*)
      out="${1#--out:}"
      ;;
    --print-roots)
      print_roots="1"
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

if [ "$lock" = "" ]; then
  echo "[Error] missing --lock" 1>&2
  usage
  exit 2
fi

if [ ! -f "$lock" ]; then
  echo "[Error] lock not found: $lock" 1>&2
  exit 2
fi

if [ ! -x "$CHENGC" ]; then
  echo "[Error] chengc.sh not found: $CHENGC" 1>&2
  exit 2
fi

storage_bin="$(resolve_chengc_bin cheng_storage)"
pkg_source_bin="$(resolve_chengc_bin cheng_pkg_source)"

if [ ! -x "$storage_bin" ] || [ "$ROOT/src/tooling/cheng_storage.cheng" -nt "$storage_bin" ]; then
  (cd "$ROOT" && sh src/tooling/chengc.sh src/tooling/cheng_storage.cheng --name:cheng_storage)
fi

if [ "$peer_env" != "" ]; then
  old_ifs="$IFS"
  IFS=','
  for peer in $peer_env; do
    if [ "$peer" != "" ]; then
      peer_args="$peer_args --peer:$peer"
    fi
  done
  IFS="$old_ifs"
fi

listen_arg=""
if [ "$listen_addr" != "" ]; then
  listen_arg="--listen:$listen_addr"
fi
storage_listen_arg=""
if [ "$listen_addr" != "" ]; then
  storage_listen_arg="--storage-listen:$listen_addr"
fi

if [ "$source_peer_env" != "" ]; then
  old_ifs="$IFS"
  IFS=','
  for peer in $source_peer_env; do
    if [ "$peer" != "" ]; then
      source_peer_args="$source_peer_args --source-peer:$peer"
    fi
  done
  IFS="$old_ifs"
fi

source_listen_arg=""
if [ "$source_listen_addr" != "" ]; then
  source_listen_arg="--listen:$source_listen_addr"
fi

deps="$(awk '
  function emit() {
    if (pkg == "" || cid == "") {
      if (section_seen) {
        exit 3
      }
      return
    }
    if (format == "") { format = "tar" }
    print pkg "|" cid "|" format
    section_seen = 0
  }
  /^\[\[dependencies\]\]/ { emit(); pkg=""; cid=""; format=""; section_seen=1; next }
  /^[[:space:]]*package_id[[:space:]]*=/ {
    if ($0 ~ /"/) { gsub(/.*="/, "", $0); gsub(/".*/, "", $0); pkg=$0 }
  }
  /^[[:space:]]*cid[[:space:]]*=/ {
    if ($0 ~ /"/) { gsub(/.*="/, "", $0); gsub(/".*/, "", $0); cid=$0 }
  }
  /^[[:space:]]*format[[:space:]]*=/ {
    if ($0 ~ /"/) { gsub(/.*="/, "", $0); gsub(/".*/, "", $0); format=$0 }
  }
  END { emit() }
' "$lock")" || {
  if [ "$?" -eq 3 ]; then
    echo "[Error] invalid lock file: dependency entry missing package_id or cid" 1>&2
    exit 2
  fi
  exit 2
}

roots=""
old_ifs="$IFS"
IFS='
'
for entry in $deps; do
  pkg="${entry%%|*}"
  rest="${entry#*|}"
  cid="${rest%%|*}"
  fmt="${rest#*|}"
  if [ "$fmt" = "" ]; then
    fmt="tar"
  fi
  fmt="$(printf "%s" "$fmt" | tr 'A-Z' 'a-z')"
  if [ "$fmt" != "tar" ] && [ "$fmt" != "source" ]; then
    echo "[Error] unknown package format: $fmt" 1>&2
    exit 2
  fi
  if [ "$cid" = "" ]; then
    continue
  fi
  cid_dir="$cid"
  case "$cid_dir" in
    cid://*) cid_dir="${cid_dir#cid://}" ;;
    cid:*) cid_dir="${cid_dir#cid:}" ;;
  esac
  if echo "$cid_dir" | grep -q '/'; then
    echo "[Error] invalid cid in lock: $cid" 1>&2
    exit 2
  fi
  pkg_dir="$cache_root/$cid_dir"
  if [ -f "$pkg_dir/cheng-package.toml" ]; then
    if [ "$roots" = "" ]; then
      roots="$pkg_dir"
    else
      roots="$roots,$pkg_dir"
    fi
    continue
  fi
  mkdir -p "$pkg_dir"
  if [ "$fmt" = "source" ]; then
    if [ ! -x "$pkg_source_bin" ] || [ "$ROOT/src/tooling/cheng_pkg_source.cheng" -nt "$pkg_source_bin" ]; then
      (cd "$ROOT" && sh src/tooling/chengc.sh src/tooling/cheng_pkg_source.cheng --name:cheng_pkg_source)
    fi
    "$pkg_source_bin" fetch --manifest:"$cid_dir" --out:"$pkg_dir" --root:"$storage_root" --mode:"$mode" \
      $storage_listen_arg $peer_args $source_listen_arg $source_peer_args
  else
    if ! command -v tar >/dev/null 2>&1; then
      echo "[Error] tar not found" 1>&2
      exit 2
    fi
    tmp="$(mktemp "$pkg_dir/pkg.XXXXXX.tar.gz")"
    "$storage_bin" get --cid:"$cid_dir" --out:"$tmp" --root:"$storage_root" --mode:"$mode" $listen_arg $peer_args
    tar -xzf "$tmp" -C "$pkg_dir"
    rm -f "$tmp"
  fi
  if [ "$roots" = "" ]; then
    roots="$pkg_dir"
  else
    roots="$roots,$pkg_dir"
  fi
done
IFS="$old_ifs"

if [ "$out" != "" ]; then
  out_dir="$(dirname "$out")"
  if [ "$out_dir" != "" ] && [ ! -d "$out_dir" ]; then
    mkdir -p "$out_dir"
  fi
  echo "$roots" > "$out"
fi

if [ "$print_roots" != "" ]; then
  echo "$roots"
fi
