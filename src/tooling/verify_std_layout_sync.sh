#!/usr/bin/env sh
. "$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)/env_prefix_bridge.sh"
set -eu
(set -o pipefail) 2>/dev/null && set -o pipefail

root="$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)"
cd "$root"

std_root="${STD_ROOT:-src/std}"

fail() {
  echo "[verify_std_layout_sync] $1" 1>&2
  exit 1
}

[ -d "$std_root" ] || fail "missing std root: $std_root"
[ ! -d "src/stdlib/bootstrap" ] || fail "legacy path still exists: src/stdlib/bootstrap"

if find "$std_root" -maxdepth 1 -type f -name '* copy.cheng' -print -quit | grep -q .; then
  find "$std_root" -maxdepth 1 -type f -name '* copy.cheng' -print 1>&2
  fail "found unmerged copy files in $std_root"
fi

required_modules="
seqs.cheng
strings.cheng
system.cheng
os.cheng
tables.cheng
hashmaps.cheng
hashsets.cheng
cmdline.cheng
bytes.cheng
result.cheng
option.cheng
"
for mod in $required_modules; do
  [ -f "$std_root/$mod" ] || fail "missing std module: $std_root/$mod"
done

echo "verify_std_layout_sync ok"
