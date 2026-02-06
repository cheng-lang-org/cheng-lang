#!/usr/bin/env sh
set -euo pipefail

usage() {
  cat <<'EOF'
Usage:
  src/tooling/verify_c_backend_hybrid.sh [--example:<path>] [--map:<path>]
                                         [--name:<exe>] [--modules-out:<dir>] [--jobs:<N>]

Notes:
  - Verifies hybrid C+ASM module build + link via chengc.sh --backend:hybrid.
EOF
}

example="${CHENG_C_HYBRID_EXAMPLE:-examples/c_asm_hybrid/main.cheng}"
map="${CHENG_C_HYBRID_MAP:-examples/c_asm_hybrid/hybrid.map}"
name="${CHENG_C_HYBRID_NAME:-c_backend_hybrid}"
modules_out="${CHENG_C_HYBRID_OUT:-chengcache/c_backend_hybrid.modules}"
jobs="${CHENG_C_HYBRID_JOBS:-}"

while [ "${1:-}" != "" ]; do
  case "$1" in
    --example:*)
      example="${1#--example:}"
      ;;
    --map:*)
      map="${1#--map:}"
      ;;
    --name:*)
      name="${1#--name:}"
      ;;
    --modules-out:*)
      modules_out="${1#--modules-out:}"
      ;;
    --jobs:*)
      jobs="${1#--jobs:}"
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

job_arg=""
if [ -n "$jobs" ]; then
  job_arg="--jobs:$jobs"
fi

if [ ! -f "$example" ]; then
  echo "[Error] missing example: $example" 1>&2
  exit 2
fi
if [ ! -f "$map" ]; then
  echo "[Error] missing hybrid map: $map" 1>&2
  exit 2
fi

check_grep() {
  pattern="$1"
  file="$2"
  if command -v rg >/dev/null 2>&1; then
    if ! rg -q "$pattern" "$file"; then
      echo "[Error] expected pattern not found: $pattern ($file)" 1>&2
      exit 2
    fi
  else
    if ! grep -q "$pattern" "$file"; then
      echo "[Error] expected pattern not found: $pattern ($file)" 1>&2
      exit 2
    fi
  fi
}

rm -rf "$modules_out"
mkdir -p "$modules_out"

sh src/tooling/chengc.sh "$example" --name:"$name" --backend:hybrid \
  --hybrid-map:"$map" --hybrid-default:c --modules-out:"$modules_out" $job_arg

modules_map="$modules_out/modules.map"
if [ ! -s "$modules_map" ]; then
  echo "[Error] missing modules map: $modules_map" 1>&2
  exit 2
fi

check_grep "mod_c\\.cheng[[:space:]]+c" "$modules_map"
check_grep "mod_asm\\.cheng[[:space:]]+asm" "$modules_map"

check_modsym() {
  mod_path="$1"
  modsym_out="$2"
  if [ -z "$modsym_out" ] || [ ! -s "$modsym_out" ]; then
    echo "[Error] missing module symbol output: $modsym_out" 1>&2
    exit 2
  fi
  header_line="$(head -n 1 "$modsym_out" 2>/dev/null || true)"
  if [ "$header_line" != "# cheng modsym v1" ]; then
    echo "[Error] invalid modsym header: $modsym_out" 1>&2
    exit 2
  fi
  mod_line="$(grep -m 1 "^module=" "$modsym_out" 2>/dev/null || true)"
  if [ -z "$mod_line" ]; then
    echo "[Error] missing module line: $modsym_out" 1>&2
    exit 2
  fi
  if [ "$mod_line" != "module=$mod_path" ]; then
    echo "[Error] module mismatch in modsym: $modsym_out" 1>&2
    exit 2
  fi
}

while IFS="$(printf '\t')" read -r mod_path backend mod_out obj_out modsym_out; do
  if [ -z "$mod_path" ]; then
    continue
  fi
  check_modsym "$mod_path" "$modsym_out"
done <"$modules_map"

if [ ! -x "$name" ]; then
  echo "[Error] missing hybrid binary: ./$name" 1>&2
  exit 2
fi

set +e
"./$name"
status=$?
set -e
if [ "$status" -ne 15 ]; then
  echo "[Error] unexpected exit code: $status (expected 15)" 1>&2
  exit 2
fi

echo "verify_c_backend_hybrid ok"
