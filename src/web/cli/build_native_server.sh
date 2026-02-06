#!/usr/bin/env sh
set -euo pipefail

usage() {
  cat <<'EOF'
Usage:
  cheng/web/cli/build_native_server.sh [--entry:<file.cheng>] [--name:<exe>]

Defaults:
  --entry: cheng/web/examples/server_main.cheng
  --name:  cheng_native_server

Notes:
  - Requires ./stage1_runner or bin/cheng + gcc.
  - Links native_server.c with the Cheng-generated C output.
EOF
}

entry="cheng/web/examples/server_main.cheng"
name="cheng_native_server"

while [ "${1:-}" != "" ]; do
  case "$1" in
    --entry:*)
      entry="${1#--entry:}"
      ;;
    --name:*)
      name="${1#--name:}"
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

if [ ! -f "$entry" ]; then
  echo "[Error] entry not found: $entry" 1>&2
  exit 2
fi

frontend="${CHENG_FRONTEND:-}"
if [ "$frontend" = "" ]; then
  if [ -x "./stage1_runner" ]; then
    frontend="./stage1_runner"
  else
    frontend="bin/cheng"
  fi
fi
if [ ! -x "$frontend" ]; then
  echo "[Error] frontend not found or not executable: $frontend" 1>&2
  exit 2
fi

cfile="chengcache/${name}.c"
obj="chengcache/${name}.o"
native_obj="chengcache/${name}_native_server.o"
sys_obj="chengcache/system_helpers.o"

"$frontend" --mode:c --file:"$entry" --out:"$cfile"

cc="${CC:-gcc}"
cflags="${CFLAGS:-}"

$cc $cflags -c "$cfile" -o "$obj"
$cc $cflags -c "cheng/web/cli/native_server.c" -o "$native_obj"
$cc $cflags -c "src/runtime/native/system_helpers.c" -o "$sys_obj"
$cc $cflags -o "$name" "$obj" "$native_obj" "$sys_obj"

echo "native server ok: ./$name"
