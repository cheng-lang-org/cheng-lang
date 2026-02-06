#!/usr/bin/env bash
set -euo pipefail

root="$(CDPATH= cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$root"

echo "== Build stage0c (bin/cheng) =="
src/tooling/build_stage0c.sh

tmpdir="$(mktemp -d)"
cleanup() {
  rm -rf "$tmpdir"
}
trap cleanup EXIT

cat >"$tmpdir/deps_sample.cheng" <<'EOF'
import std/strings as s
import foo/bar
fn main() =
    0
EOF

./bin/cheng --mode:deps-list --file:"$tmpdir/deps_sample.cheng" --out:"$tmpdir/out.deps.list"

if command -v rg >/dev/null 2>&1; then
  rg -F "std/strings" "$tmpdir/out.deps.list" >/dev/null
  rg -F "foo/bar" "$tmpdir/out.deps.list" >/dev/null
  rg -F "system" "$tmpdir/out.deps.list" >/dev/null
else
  grep -F "std/strings" "$tmpdir/out.deps.list" >/dev/null
  grep -F "foo/bar" "$tmpdir/out.deps.list" >/dev/null
  grep -F "system" "$tmpdir/out.deps.list" >/dev/null
fi

echo "stage0c deps ok"
