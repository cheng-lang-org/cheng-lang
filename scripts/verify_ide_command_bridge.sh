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

schema_doc="doc/cheng-ide-command-schema.md"
if [ ! -f "$schema_doc" ]; then
  echo "[Error] missing command schema doc: $schema_doc" 1>&2
  exit 2
fi

check_file "command_schema" "$schema_doc"
check_file "command_id" "$schema_doc"
check_file "command=open" "$schema_doc"
check_file "status=queued" "$schema_doc"
check_file "retryable=1" "$schema_doc"

ide_file="ide/gui/app.cheng"
if [ -n "$gui_root" ] && [ -f "$gui_root/app.cheng" ]; then
  ide_file="$gui_root/app.cheng"
fi
check_file "DesktopCommandFile" "$ide_file"
check_file "guiDesktopCommandTick" "$ide_file"
check_file "command_schema" "$ide_file"
check_file "command_id" "$ide_file"

state_file="examples/unimaker_desktop/state.cheng"
check_file "IdeCommandSchemaExpected" "$state_file"
check_file "ideCommandSend" "$state_file"
check_file "ideCommandTick" "$state_file"

main_file="examples/unimaker_desktop/main.cheng"
check_file "ideCommandTick" "$main_file"

ui_file="examples/unimaker_desktop/ui.cheng"
check_file "IDE Build" "$ui_file"
check_file "IDE Diag" "$ui_file"

locale_file="examples/unimaker_desktop/locale_unimaker.cheng"
check_file "IDE Build" "$locale_file"
check_file "IDE Diag" "$locale_file"

README="examples/unimaker_desktop/README.md"
check_file "desktop_command.txt" "$README"
check_file "desktop_command_ack.txt" "$README"

echo "ide command bridge ok"
