#!/usr/bin/env bash
. "$(CDPATH= cd -- "$(dirname -- "$0")/../src/tooling" && pwd)/env_prefix_bridge.sh"
set -euo pipefail

root="$(CDPATH= cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$root"

schema_doc="doc/cheng-ide-bridge-schema.md"
if [ ! -f "$schema_doc" ]; then
  echo "[Error] missing bridge schema doc: $schema_doc" 1>&2
  exit 2
fi

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

ide_root="${IDE_ROOT:-}"
gui_root="${GUI_ROOT:-}"
if [ -z "$ide_root" ] && [ -d "$root/../cheng-ide" ]; then
  ide_root="$root/../cheng-ide"
fi
if [ -z "$gui_root" ] && [ -d "$root/../cheng-gui" ]; then
  gui_root="$root/../cheng-gui"
fi
if [ -z "$gui_root" ] && [ -n "$ide_root" ] && [ -d "$ide_root/gui" ]; then
  gui_root="$ide_root/gui"
fi

check_file "bridge_schema" "$schema_doc"
check_file "ide_version" "$schema_doc"
check_file "capabilities" "$schema_doc"
check_file "timestamp_ms" "$schema_doc"
check_file "heartbeat_ts" "$schema_doc"

ide_writer="ide/gui/app.cheng"
if [ -n "$gui_root" ] && [ -f "$gui_root/app.cheng" ]; then
  ide_writer="$gui_root/app.cheng"
fi
check_file "bridge_schema=" "$ide_writer"
check_file "ide_version=" "$ide_writer"
check_file "capabilities=" "$ide_writer"
check_file "timestamp_ms=" "$ide_writer"
check_file "heartbeat_ts=" "$ide_writer"

ide_reader="examples/unimaker_desktop/state.cheng"
check_file "key == \"bridge_schema\"" "$ide_reader"
check_file "key == \"ide_version\"" "$ide_reader"
check_file "key == \"capabilities\"" "$ide_reader"
check_file "IdeBridgeSchemaExpected" "$ide_reader"

echo "ide bridge schema ok"
