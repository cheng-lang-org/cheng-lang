#!/usr/bin/env sh
set -euo pipefail

usage() {
  cat <<'EOF'
Usage:
  src/tooling/verify_stage1_fullspec.sh [--file:<path>] [--name:<bin>] [--jobs:<N>]
                                        [--backend:<c|obj>] [--target:<triple>] [--linker:<self|cc>]
                                        [--frontend:<path>]
                                        [--mm:<orc|off>] [--skip-run] [--allow-skip]
                                        [--log:<path>]

Notes:
  - Builds and runs the stage1 fullspec sample (expects output: "fullspec ok").
  - `--backend:c` (default): requires a stage1 frontend executable (default: ./stage1_runner).
  - `--backend:obj`: compiles via backend driver (frontend=stage1) and can use `--linker:self` for .o-only closure.
EOF
}

file="examples/stage1_codegen_fullspec.cheng"
name=""
jobs=""
mm=""
skip_run=""
allow_skip=""
backend="${CHENG_STAGE1_FULLSPEC_BACKEND:-c}"
target="${CHENG_STAGE1_FULLSPEC_TARGET:-}"
linker="${CHENG_STAGE1_FULLSPEC_LINKER:-}"
frontend="./stage1_runner"
log="chengcache/stage1_fullspec.out"

while [ "${1:-}" != "" ]; do
  case "$1" in
    --file:*)
      file="${1#--file:}"
      ;;
    --name:*)
      name="${1#--name:}"
      ;;
    --jobs:*)
      jobs="${1#--jobs:}"
      ;;
    --backend:*)
      backend="${1#--backend:}"
      ;;
    --target:*)
      target="${1#--target:}"
      ;;
    --linker:*)
      linker="${1#--linker:}"
      ;;
    --frontend:*)
      frontend="${1#--frontend:}"
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

case "$file" in
  /*) ;;
  *) file="$root/$file" ;;
esac

if [ ! -f "$file" ]; then
  echo "[Error] missing fullspec file: $file" 1>&2
  exit 2
fi

case "$backend" in
  c|obj) ;;
  *)
    echo "[Error] invalid --backend:$backend (expected c|obj)" 1>&2
    exit 2
    ;;
esac

if [ "$name" = "" ]; then
  base="$(basename "$file")"
  name="${base%.cheng}"
fi

envs=""
if [ "$mm" != "" ]; then
  envs="CHENG_MM=$mm"
fi

job_arg=""
if [ "$jobs" != "" ]; then
  job_arg="--jobs:$jobs"
fi

run_cmd() {
  cmd_label="$1"
  shift
  echo "== $cmd_label =="
  "$@"
}

if [ "$backend" = "c" ]; then
  frontend_path="$frontend"
  case "$frontend" in
    /*) ;;
    *) frontend_path="$root/$frontend" ;;
  esac

  if [ ! -x "$frontend_path" ]; then
    if [ "$allow_skip" != "" ]; then
      echo "[skip] stage1 frontend missing: $frontend_path" 1>&2
      exit 0
    fi
    echo "[Error] missing stage1 frontend: $frontend_path" 1>&2
    exit 2
  fi

  run_cmd "build.stage1_fullspec" env $envs CHENG_FRONTEND="$frontend_path" \
    sh src/tooling/chengc.sh "$file" --name:"$name" --backend:c $job_arg
else
  host_os="$(uname -s 2>/dev/null || echo unknown)"
  host_arch="$(uname -m 2>/dev/null || echo unknown)"

  if [ "$target" = "" ]; then
    case "$host_os/$host_arch" in
      Darwin/arm64)
        target="arm64-apple-darwin"
        ;;
      Linux/aarch64|Linux/arm64)
        target="aarch64-unknown-linux-gnu"
        ;;
      *)
        if [ "$allow_skip" != "" ]; then
          echo "[skip] stage1 fullspec obj: unsupported host=$host_os/$host_arch" 1>&2
          exit 0
        fi
        echo "[Error] stage1 fullspec obj unsupported host=$host_os/$host_arch (pass --backend:c or --allow-skip)" 1>&2
        exit 2
        ;;
    esac
  fi

  if [ "$linker" = "" ]; then
    case "$host_os" in
      Darwin) linker="self" ;;
      Linux) linker="self" ;;
      *) linker="cc" ;;
    esac
  fi
  case "$linker" in
    self|cc) ;;
    *)
      echo "[Error] invalid --linker:$linker (expected self|cc)" 1>&2
      exit 2
      ;;
  esac

  if [ "$linker" = "self" ] && [ "$host_os" = "Darwin" ]; then
    if ! command -v codesign >/dev/null 2>&1; then
      if [ "$allow_skip" != "" ]; then
        echo "[skip] stage1 fullspec obj: missing codesign" 1>&2
        exit 0
      fi
      echo "[Error] stage1 fullspec obj missing codesign (required for self-linked Mach-O)" 1>&2
      exit 2
    fi
  fi

  driver="$(sh src/tooling/backend_driver_path.sh)"
  runtime_src="src/std/system_helpers_backend.cheng"
  runtime_obj="chengcache/system_helpers.backend.cheng.o"
  if [ ! -f "$runtime_src" ]; then
    echo "[Error] missing backend runtime source: $runtime_src" 1>&2
    exit 2
  fi
  mkdir -p chengcache
  if [ ! -f "$runtime_obj" ] || [ "$runtime_src" -nt "$runtime_obj" ]; then
    run_cmd "build.stage1_fullspec.runtime_obj" env \
      CHENG_BACKEND_ALLOW_NO_MAIN=1 \
      CHENG_BACKEND_WHOLE_PROGRAM=1 \
      CHENG_BACKEND_EMIT=obj \
      CHENG_BACKEND_TARGET="$target" \
      CHENG_BACKEND_FRONTEND=mvp \
      CHENG_BACKEND_INPUT="$runtime_src" \
      CHENG_BACKEND_OUTPUT="$runtime_obj" \
      "$driver"
  fi
  if [ ! -s "$runtime_obj" ]; then
    echo "[Error] missing backend runtime obj: $runtime_obj" 1>&2
    exit 2
  fi

  link_env=""
  if [ "$linker" = "self" ]; then
    link_env="CHENG_BACKEND_LINKER=self CHENG_BACKEND_RUNTIME_OBJ=$runtime_obj"
  fi

  # shellcheck disable=SC2086
  run_cmd "build.stage1_fullspec.obj" env $envs $link_env \
    CHENG_C_SYSTEM=system \
    CHENG_BACKEND_VALIDATE=1 \
    CHENG_BACKEND_FRONTEND=stage1 \
    CHENG_BACKEND_EMIT=exe \
    CHENG_BACKEND_TARGET="$target" \
    CHENG_BACKEND_INPUT="$file" \
    CHENG_BACKEND_OUTPUT="$root/$name" \
    "$driver"
fi

if [ "$skip_run" != "" ]; then
  echo "stage1 fullspec build ok: $name"
  exit 0
fi

if [ ! -x "$root/$name" ]; then
  echo "[Error] missing output binary: $root/$name" 1>&2
  exit 2
fi

mkdir -p "$(dirname "$log")"
output="$(env $envs "$root/$name" 2>&1)"
status=$?
printf "%s\n" "$output" >"$log"
if [ "$status" != "0" ]; then
  echo "[Error] stage1 fullspec failed (exit $status, see $log)" 1>&2
  exit 2
fi

if command -v rg >/dev/null 2>&1; then
  if ! printf "%s\n" "$output" | rg -q "fullspec ok"; then
    echo "[Error] stage1 fullspec missing 'fullspec ok' (see $log)" 1>&2
    exit 2
  fi
else
  if ! printf "%s\n" "$output" | grep -q "fullspec ok"; then
    echo "[Error] stage1 fullspec missing 'fullspec ok' (see $log)" 1>&2
    exit 2
  fi
fi

echo "stage1 fullspec verify ok: $name"
