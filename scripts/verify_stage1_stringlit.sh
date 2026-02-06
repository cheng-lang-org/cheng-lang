#!/usr/bin/env sh
set -eu

root="$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)"
frontend="$root/stage1_runner"
entry="$root/tests/cheng/strings/stage1_stringlit_crash.cheng"
out="/tmp/stage1_stringlit_test.c"

if [ ! -x "$frontend" ]; then
  echo "[Error] missing ./stage1_runner (run src/tooling/bootstrap.sh first)" 1>&2
  exit 2
fi

rm -f "$out"
"$frontend" --mode:c --file:"$entry" --out:"$out" >/dev/null

if [ ! -f "$out" ]; then
  echo "[Error] missing output: $out" 1>&2
  exit 2
fi

echo "stage1 stringlit ok"
