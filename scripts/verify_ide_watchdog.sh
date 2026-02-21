#!/usr/bin/env bash
. "$(CDPATH= cd -- "$(dirname -- "$0")/../src/tooling" && pwd)/env_prefix_bridge.sh"
set -euo pipefail

root="$(CDPATH= cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$root"

if command -v rg >/dev/null 2>&1; then
  search() { rg -F "$1" "$2" >/dev/null; }
else
  search() { grep -F "$1" "$2" >/dev/null; }
fi

check_file() {
  local needle="$1"
  local target="$2"
  if ! search "$needle" "$target"; then
    echo "[Error] missing '$needle' in $target" 1>&2
    exit 1
  fi
}

state_file="examples/unimaker_desktop/state.cheng"
main_file="examples/unimaker_desktop/main.cheng"

check_file "IdeBridgeWatchdogIntervalSec" "$state_file"
check_file "IdeBridgeRestartBackoffSec" "$state_file"
check_file "IdeBridgeRestartMax" "$state_file"
check_file "fn ideBridgeWatchdogTick" "$state_file"
check_file "ideBridgeWatchdogTick" "$main_file"

echo "ide bridge watchdog ok"
