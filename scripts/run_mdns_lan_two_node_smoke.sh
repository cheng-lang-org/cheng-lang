#!/usr/bin/env bash
. "$(CDPATH= cd -- "$(dirname -- "$0")/../src/tooling" && pwd)/env_prefix_bridge.sh"
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
ROOT="${ROOT:-$ROOT}"
DEFAULT_BIN="${ROOT}/mdns_lan_two_node_smoke_mvp_exe"
BIN="${1:-${MDNS_LAN_SMOKE_BIN:-$DEFAULT_BIN}}"

LAN_IP_ENV="MDNS_LAN_IP"
LAN_IP="${!LAN_IP_ENV-}"
LAN_IP_SOURCE="environment:${LAN_IP_ENV}"

is_private_or_lan_ip() {
  local ip="$1"
  if [ -z "$ip" ]; then
    return 1
  fi
  case "$ip" in
    127.*|169.254.*|0.*|255.*|::*)
      return 1
      ;;
    10.*|192.168.*)
      return 0
      ;;
    172.*)
      local a b
      a="${ip%%.*}"
      b="${ip#*.}"
      b="${b%%.*}"
      case "$b" in
        1[6-9]|2[0-9]|3[0-1]) return 0 ;;
      esac
      return 1
      ;;
  esac
  return 1
}

if [ -z "$LAN_IP" ]; then
  ROUTE_IFACE=""
  if command -v route >/dev/null 2>&1; then
    ROUTE_IFACE="$(route -n get default 2>/dev/null | awk '/interface:/{print $2; exit}')"
  fi

  candidates=()
  if [ -n "$ROUTE_IFACE" ] && [[ "$ROUTE_IFACE" != utun* ]]; then
    candidates+=("$ROUTE_IFACE")
  fi
  if command -v ifconfig >/dev/null 2>&1; then
    for iface in en0 en1 en2 en3 en4 en5 en6 eth0 eth1 awdl0; do
      candidates+=("$iface")
    done
  fi

  for iface in "${candidates[@]:-}"; do
    [ -z "$iface" ] && continue
    if [ ! -d "/sys/class/net" ] && [ "$iface" = "eth0" ]; then
      continue
    fi
    if [ -z "$LAN_IP" ]; then
      if command -v ip >/dev/null 2>&1 && uname -s | grep -qi "linux"; then
        LAN_IP="$(ip -4 -o addr show dev "$iface" 2>/dev/null | awk '$1=="inet" {split($4,a,"/"); print a[1]; exit}')"
      elif command -v ifconfig >/dev/null 2>&1 && uname -s | grep -qi "darwin"; then
        LAN_IP="$(ifconfig "$iface" 2>/dev/null | awk '/inet / && $2 !~ /^127\\./ && $2 !~ /^169\\.254\\./ {print $2; exit}')"
      fi
      if is_private_or_lan_ip "$LAN_IP"; then
        LAN_IP_SOURCE="interface:$iface"
        break
      fi
      LAN_IP=""
    fi
  done
fi

if ! is_private_or_lan_ip "$LAN_IP"; then
  echo "ERROR: no usable LAN IPv4 found. Set MDNS_LAN_IP manually." >&2
  echo "Examples: export MDNS_LAN_IP=192.168.3.17" >&2
  exit 1
fi

if [ ! -f "$BIN" ] || [ ! -x "$BIN" ]; then
  echo "ERROR: mdns smoke binary missing or not executable: $BIN" >&2
  exit 2
fi

echo "mdns-lan smoke bin: $BIN"
echo "MDNS_LAN_IP=${LAN_IP} (source=${LAN_IP_SOURCE})"
export MDNS_LAN_IP="$LAN_IP"
exec "$BIN"
