#!/usr/bin/env sh
set -euo pipefail

usage() {
  cat <<'EOF'
Usage:
  src/tooling/verify_c_backend_modules.sh [--example:<path>] [--name:<exe>] [--modules-out:<dir>] [--jobs:<N>] [--timeout:<sec>]

Notes:
  - Verifies direct C backend module-level incremental build (requires stage1_runner).
EOF
}

example="${CHENG_C_MODULES_EXAMPLE:-examples/asm_modules/main.cheng}"
name="${CHENG_C_MODULES_NAME:-c_backend_modules}"
modules_out="${CHENG_C_MODULES_OUT:-chengcache/c_backend_modules}"
jobs="${CHENG_C_MODULES_JOBS:-}"
timeout="${CHENG_C_MODULES_TIMEOUT:-}"

while [ "${1:-}" != "" ]; do
  case "$1" in
    --example:*)
      example="${1#--example:}"
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
    --timeout:*)
      timeout="${1#--timeout:}"
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

if [ -z "${CHENG_FORCE_C_MODULES:-}" ]; then
  export CHENG_FORCE_C_MODULES=1
fi
if [ -n "$timeout" ]; then
  export CHENG_BUILD_TIMEOUT="$timeout"
fi
if [ -z "${CHENG_C_FRONTEND:-}" ]; then
  if [ -x "./stage1_runner" ]; then
    export CHENG_C_FRONTEND="./stage1_runner"
  else
    echo "[Error] stage1_runner is required for C modules" 1>&2
    exit 2
  fi
fi

if [ ! -f "$example" ]; then
  echo "[Error] missing example: $example" 1>&2
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

mtime() {
  f="$1"
  if stat -f %m "$f" >/dev/null 2>&1; then
    stat -f %m "$f"
  else
    stat -c %Y "$f"
  fi
}

rm -rf "$modules_out"
mkdir -p "$modules_out"

sh src/tooling/chengc.sh "$example" --name:"$name" --backend:c --emit-c-modules \
  --modules-out:"$modules_out" $job_arg

modules_map="$modules_out/modules.map"
if [ ! -s "$modules_map" ]; then
  echo "[Error] missing modules map: $modules_map" 1>&2
  exit 2
fi

check_grep "asm_modules/main\\.cheng" "$modules_map"
check_grep "asm_modules/mod_a\\.cheng" "$modules_map"
check_grep "asm_modules/mod_b\\.cheng" "$modules_map"
if ! grep -E "stdlib/bootstrap/system\\.cheng" "$modules_map" >/dev/null 2>&1; then
  check_grep "stdlib/bootstrap/system_c\\.cheng" "$modules_map"
fi

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

main_obj="$(awk -F '\t' 'index($1, "asm_modules/main.cheng") {print $3}' "$modules_map" | head -n 1)"
mod_a_obj="$(awk -F '\t' 'index($1, "asm_modules/mod_a.cheng") {print $3}' "$modules_map" | head -n 1)"
mod_b_obj="$(awk -F '\t' 'index($1, "asm_modules/mod_b.cheng") {print $3}' "$modules_map" | head -n 1)"
main_modsym="$(awk -F '\t' 'index($1, "asm_modules/main.cheng") {print $4}' "$modules_map" | head -n 1)"
mod_a_modsym="$(awk -F '\t' 'index($1, "asm_modules/mod_a.cheng") {print $4}' "$modules_map" | head -n 1)"
mod_b_modsym="$(awk -F '\t' 'index($1, "asm_modules/mod_b.cheng") {print $4}' "$modules_map" | head -n 1)"
main_path="$(awk -F '\t' 'index($1, "asm_modules/main.cheng") {print $1}' "$modules_map" | head -n 1)"
mod_a_path="$(awk -F '\t' 'index($1, "asm_modules/mod_a.cheng") {print $1}' "$modules_map" | head -n 1)"
mod_b_path="$(awk -F '\t' 'index($1, "asm_modules/mod_b.cheng") {print $1}' "$modules_map" | head -n 1)"

if [ -z "$main_obj" ] || [ -z "$mod_a_obj" ] || [ -z "$mod_b_obj" ]; then
  echo "[Error] missing module objects in map" 1>&2
  exit 2
fi
if [ -z "$main_modsym" ] || [ -z "$mod_a_modsym" ] || [ -z "$mod_b_modsym" ]; then
  echo "[Error] missing module symbols in map" 1>&2
  exit 2
fi
check_modsym "$main_path" "$main_modsym"
check_modsym "$mod_a_path" "$mod_a_modsym"
check_modsym "$mod_b_path" "$mod_b_modsym"

t_main="$(mtime "$main_obj")"
t_a="$(mtime "$mod_a_obj")"
t_b="$(mtime "$mod_b_obj")"

mod_b_src="examples/asm_modules/mod_b.cheng"
mod_b_bak="${mod_b_src}.bak"
mod_a_src="examples/asm_modules/mod_a.cheng"
mod_a_bak="${mod_a_src}.bak"
cp "$mod_b_src" "$mod_b_bak"
cp "$mod_a_src" "$mod_a_bak"
cleanup_mod_b() {
  if [ -f "$mod_b_bak" ]; then
    mv "$mod_b_bak" "$mod_b_src"
  fi
  if [ -f "$mod_a_bak" ]; then
    mv "$mod_a_bak" "$mod_a_src"
  fi
}
trap cleanup_mod_b EXIT

sleep 1
printf "\n# mod bump\n" >> "$mod_b_src"

sh src/tooling/chengc.sh "$example" --name:"$name" --backend:c --emit-c-modules \
  --modules-out:"$modules_out" $job_arg

t_main2="$(mtime "$main_obj")"
t_a2="$(mtime "$mod_a_obj")"
t_b2="$(mtime "$mod_b_obj")"

if [ "$t_b2" -le "$t_b" ]; then
  echo "[Error] expected mod_b to rebuild" 1>&2
  exit 2
fi
if [ "$t_main2" -ne "$t_main" ]; then
  echo "[Error] main should not rebuild" 1>&2
  exit 2
fi
if [ "$t_a2" -ne "$t_a" ]; then
  echo "[Error] mod_a should not rebuild" 1>&2
  exit 2
fi

sleep 1
printf "\n# mod_a bump\n" >> "$mod_a_src"

sh src/tooling/chengc.sh "$example" --name:"$name" --backend:c --emit-c-modules \
  --modules-out:"$modules_out" $job_arg

t_main3="$(mtime "$main_obj")"
t_a3="$(mtime "$mod_a_obj")"
t_b3="$(mtime "$mod_b_obj")"

if [ "$t_a3" -le "$t_a2" ]; then
  echo "[Error] expected mod_a to rebuild" 1>&2
  exit 2
fi
if [ "$t_main3" -ne "$t_main2" ]; then
  echo "[Error] main should not rebuild (mod_a)" 1>&2
  exit 2
fi
if [ "$t_b3" -ne "$t_b2" ]; then
  echo "[Error] mod_b should not rebuild (mod_a)" 1>&2
  exit 2
fi

echo "verify_c_backend_modules ok"
