#!/usr/bin/env sh
set -euo pipefail

usage() {
  cat <<'EOF'
Usage:
  src/tooling/verify_mixed_backend.sh [--example:<path>] [--map:<path>]
                                      [--c-name:<exe>] [--hybrid-name:<exe>]
                                      [--modules-out:<dir>] [--out:<path>] [--jobs:<N>]

Notes:
  - Builds C-only and hybrid C+ASM binaries, then checks output consistency.
  - Writes basic size/time metrics to the output file.
EOF
}

example="${CHENG_MIXED_EXAMPLE:-examples/c_asm_hybrid/main.cheng}"
map="${CHENG_MIXED_MAP:-examples/c_asm_hybrid/hybrid.map}"
c_name="${CHENG_MIXED_C_NAME:-c_backend_mixed_c}"
hybrid_name="${CHENG_MIXED_HYBRID_NAME:-c_backend_mixed_hybrid}"
modules_out="${CHENG_MIXED_OUT:-chengcache/c_backend_mixed.modules}"
out="${CHENG_MIXED_METRICS_OUT:-artifacts/hybrid/metrics.env}"
jobs="${CHENG_MIXED_JOBS:-}"

if [ -z "${CHENG_C_FRONTEND:-}" ]; then
  if [ -x "./stage1_runner" ]; then
    export CHENG_C_FRONTEND="./stage1_runner"
  else
    echo "[Error] missing C frontend; build stage1_runner or set CHENG_C_FRONTEND" 1>&2
    exit 2
  fi
fi

while [ "${1:-}" != "" ]; do
  case "$1" in
    --example:*)
      example="${1#--example:}"
      ;;
    --map:*)
      map="${1#--map:}"
      ;;
    --c-name:*)
      c_name="${1#--c-name:}"
      ;;
    --hybrid-name:*)
      hybrid_name="${1#--hybrid-name:}"
      ;;
    --modules-out:*)
      modules_out="${1#--modules-out:}"
      ;;
    --out:*)
      out="${1#--out:}"
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

now_s() {
  date +%s
}

file_bytes() {
  file="$1"
  if [ ! -f "$file" ]; then
    echo "0"
    return
  fi
  if stat -f%z "$file" >/dev/null 2>&1; then
    stat -f%z "$file"
    return
  fi
  if stat -c%s "$file" >/dev/null 2>&1; then
    stat -c%s "$file"
    return
  fi
  wc -c <"$file" | tr -d ' '
}

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

rm -f "$c_name" "$hybrid_name"
rm -rf "$modules_out"
mkdir -p "$modules_out"

start_c="$(now_s)"
sh src/tooling/chengc.sh "$example" --name:"$c_name" --backend:c $job_arg
end_c="$(now_s)"

start_hybrid="$(now_s)"
sh src/tooling/chengc.sh "$example" --name:"$hybrid_name" --backend:hybrid \
  --hybrid-map:"$map" --hybrid-default:c --modules-out:"$modules_out" $job_arg
end_hybrid="$(now_s)"

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

if [ ! -x "$c_name" ]; then
  echo "[Error] missing C binary: ./$c_name" 1>&2
  exit 2
fi
if [ ! -x "$hybrid_name" ]; then
  echo "[Error] missing hybrid binary: ./$hybrid_name" 1>&2
  exit 2
fi

set +e
"./$c_name"
c_status=$?
"./$hybrid_name"
hybrid_status=$?
set -e

if [ "$c_status" -ne "$hybrid_status" ]; then
  echo "[Error] mixed backend mismatch: c=$c_status hybrid=$hybrid_status" 1>&2
  exit 2
fi

c_bytes="$(file_bytes "$c_name")"
hybrid_bytes="$(file_bytes "$hybrid_name")"
c_build_s=$((end_c - start_c))
hybrid_build_s=$((end_hybrid - start_hybrid))

out_dir="$(dirname "$out")"
mkdir -p "$out_dir"
cat >"$out" <<EOF
c.exit_code=$c_status
hybrid.exit_code=$hybrid_status
c.build_s=$c_build_s
hybrid.build_s=$hybrid_build_s
c.bin_bytes=$c_bytes
hybrid.bin_bytes=$hybrid_bytes
EOF

echo "verify_mixed_backend ok: $out"
