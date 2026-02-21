#!/usr/bin/env sh
. "$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)/env_prefix_bridge.sh"
set -eu

host_os="$(uname -s 2>/dev/null || echo unknown)"
host_arch="$(uname -m 2>/dev/null || echo unknown)"

case "$host_os" in
  Darwin)
    case "$host_arch" in
      arm64|aarch64)
        echo "arm64-apple-darwin"
        exit 0
        ;;
      x86_64|amd64)
        echo "x86_64-apple-darwin"
        exit 0
        ;;
    esac
    ;;
  Linux)
    case "$host_arch" in
      arm64|aarch64)
        echo "aarch64-unknown-linux-gnu"
        exit 0
        ;;
      x86_64|amd64)
        echo "x86_64-unknown-linux-gnu"
        exit 0
        ;;
    esac
    ;;
  MINGW*|MSYS*|CYGWIN*)
    case "$host_arch" in
      arm64|aarch64)
        echo "aarch64-pc-windows-msvc"
        exit 0
        ;;
      x86_64|amd64)
        echo "x86_64-pc-windows-msvc"
        exit 0
        ;;
    esac
    ;;
esac

echo "[Error] unsupported host for auto target detect: ${host_os}/${host_arch}" 1>&2
echo "  tip: pass --target:<triple> or set BACKEND_TARGET" 1>&2
exit 2
