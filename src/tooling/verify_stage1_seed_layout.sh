#!/usr/bin/env sh
set -eu
(set -o pipefail) 2>/dev/null && set -o pipefail

root="$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)"
cd "$root"

fail() {
  echo "[verify_stage1_seed_layout] $1" 1>&2
  exit 1
}

legacy_seed="src/stage1/frontend_bootstrap.seed.c"
[ ! -f "$legacy_seed" ] || fail "legacy seed must not exist: $legacy_seed"

seed_file="src/stage1/stage1_runner.seed.c"
[ -f "$seed_file" ] || fail "missing required seed file: $seed_file"

seed_files="$(find src/stage1 -maxdepth 1 -type f -name '*.seed.c' | LC_ALL=C sort)"
if [ "$seed_files" != "$seed_file" ]; then
  echo "$seed_files" 1>&2
  fail "unexpected seed file set under src/stage1 (only $seed_file is allowed)"
fi

max_bytes="${CHENG_STAGE1_SEED_MAX_BYTES:-20000000}"
case "$max_bytes" in
  ''|*[!0-9]*)
    fail "invalid CHENG_STAGE1_SEED_MAX_BYTES: $max_bytes"
    ;;
esac
size_bytes="$(wc -c < "$seed_file" | tr -d '[:space:]')"
if [ "$size_bytes" -gt "$max_bytes" ]; then
  fail "seed file too large: $seed_file ($size_bytes bytes > $max_bytes bytes)"
fi

echo "verify_stage1_seed_layout ok"
