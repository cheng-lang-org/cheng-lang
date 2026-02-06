#!/usr/bin/env sh
set -euo pipefail

usage() {
  cat <<'EOF'
Usage:
  src/tooling/verify_c_backend_fullspec.sh [--example:<path>] [--name:<exe>] [--out-dir:<dir>]
                                           [--mm:<orc|off>] [--skip-run] [--allow-skip]
                                           [--log:<path>] [--use-env]

Notes:
  - Verifies C backend fullspec subset via stage1_runner (direct C).
  - Uses CHENG_C_SYSTEM=1 and disables module emit for stability.
  - Default uses --backend:c and CHENG_C_FRONTEND (defaults to ./stage1_runner).
EOF
}

example="${CHENG_C_FULLSPEC_EXAMPLE:-examples/c_backend_fullspec.cheng}"
name="${CHENG_C_FULLSPEC_NAME:-c_backend_fullspec}"
out_dir="${CHENG_C_FULLSPEC_OUT_DIR:-chengcache/c_backend_fullspec}"
mm=""
skip_run=""
allow_skip=""
log="chengcache/c_backend_fullspec.out"
use_env=""

while [ "${1:-}" != "" ]; do
  case "$1" in
    --example:*)
      example="${1#--example:}"
      ;;
    --name:*)
      name="${1#--name:}"
      ;;
    --out-dir:*)
      out_dir="${1#--out-dir:}"
      ;;
    --mm:*)
      mm="${1#--mm:}"
      ;;
    --skip-run)
      skip_run="1"
      ;;
    --allow-skip)
      allow_skip="1"
      ;;
    --log:*)
      log="${1#--log:}"
      ;;
    --use-env)
      use_env="1"
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

root="$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)"
cd "$root"

case "$example" in
  /*) ;;
  *) example="$root/$example" ;;
esac

if [ ! -f "$example" ]; then
  echo "[Error] missing fullspec file: $example" 1>&2
  exit 2
fi

frontend="${CHENG_C_FRONTEND:-./stage1_runner}"
frontend_path="$frontend"
case "$frontend" in
  /*) ;;
  *) frontend_path="$root/$frontend" ;;
esac
if [ ! -x "$frontend_path" ]; then
  if [ "$allow_skip" != "" ]; then
    echo "[skip] C frontend missing: $frontend_path" 1>&2
    exit 0
  fi
  echo "[Error] missing C frontend: $frontend_path" 1>&2
  exit 2
fi

if [ "$name" = "" ]; then
  base="$(basename "$example")"
  name="${base%.cheng}"
fi

envs=""
if [ "$mm" != "" ]; then
  envs="CHENG_MM=$mm"
fi
backend_arg="--backend:c"
if [ "$use_env" != "" ]; then
  if [ "$envs" != "" ]; then
    envs="$envs CHENG_C_FULL=1"
  else
    envs="CHENG_C_FULL=1"
  fi
fi

run_cmd() {
  label="$1"
  shift
  echo "== $label =="
  "$@"
}

run_cmd "build.c_fullspec" env $envs CHENG_C_SYSTEM=1 CHENG_EMIT_C_MODULES=0 CHENG_DEPS_MODE=deps-list \
  CHENG_C_FRONTEND="$frontend" sh src/tooling/chengc.sh "$example" --name:"$name" $backend_arg

if [ ! -f "$name" ]; then
  echo "[Error] missing output binary: $name" 1>&2
  exit 2
fi

mkdir -p "$out_dir"
mkdir -p "$(dirname "$log")"

if [ "$skip_run" != "" ]; then
  mv -f "$name" "$out_dir/$name"
  echo "c-full fullspec build ok: $name"
  exit 0
fi

output="$(env $envs "$root/$name" 2>&1)"
status=$?
printf "%s\n" "$output" >"$log"
if [ "$status" != "0" ]; then
  echo "[Error] c-full fullspec failed (exit $status, see $log)" 1>&2
  exit 2
fi

if command -v rg >/dev/null 2>&1; then
  if ! printf "%s\n" "$output" | rg -q "fullspec ok"; then
    echo "[Error] c-full fullspec missing 'fullspec ok' (see $log)" 1>&2
    exit 2
  fi
else
  if ! printf "%s\n" "$output" | grep -q "fullspec ok"; then
    echo "[Error] c-full fullspec missing 'fullspec ok' (see $log)" 1>&2
    exit 2
  fi
fi

mv -f "$name" "$out_dir/$name"

echo "verify_c_backend_fullspec ok"
