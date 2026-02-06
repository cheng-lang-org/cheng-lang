#!/usr/bin/env sh
set -eu

root="$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)"

if [ ! -x "$root/stage1_runner" ]; then
  echo "[Error] missing stage1_runner (build via src/tooling/bootstrap.sh)" 1>&2
  exit 2
fi

tmp_dir="$(mktemp -d "${TMPDIR:-/tmp}/cheng-stage1-import.XXXXXX")"
cleanup() {
  rm -rf "$tmp_dir"
}
trap cleanup EXIT

mkdir -p "$tmp_dir/cheng/modA" "$tmp_dir/cheng/modB"

cat > "$tmp_dir/cheng/modA/a.cheng" <<'EOF'
import cheng / modB / b

fn a(): int32 =
    return 1
EOF

cat > "$tmp_dir/cheng/modB/b.cheng" <<'EOF'
import cheng / modA / a

fn b(): int32 =
    return 2
EOF

ROOT_DIR="$root" TMP_DIR="$tmp_dir" CHENG_PKG_ROOTS="$tmp_dir" python3 - <<'PY'
import os
import subprocess
import sys

root = os.environ["ROOT_DIR"]
tmp_dir = os.environ["TMP_DIR"]
entry = os.path.join(tmp_dir, "cheng", "modA", "a.cheng")
out_path = os.path.join(tmp_dir, "out.deps.list")
cmd = [os.path.join(root, "stage1_runner"), "--mode:deps-list", f"--file:{entry}", f"--out:{out_path}"]

try:
    result = subprocess.run(
        cmd,
        check=False,
        timeout=10,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        text=True,
    )
except subprocess.TimeoutExpired:
    print("[Error] stage1_runner timed out on cyclic imports", file=sys.stderr)
    sys.exit(1)
if result.returncode != 0:
    print("[Error] stage1_runner failed on cyclic imports", file=sys.stderr)
    if result.stderr:
        sys.stderr.write(result.stderr)
    sys.exit(1)
if not os.path.exists(out_path) or os.path.getsize(out_path) == 0:
    print("[Error] stage1_runner did not produce output", file=sys.stderr)
    sys.exit(1)
PY
