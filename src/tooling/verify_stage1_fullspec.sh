#!/usr/bin/env sh
set -euo pipefail

usage() {
  cat <<'EOF'
Usage:
  src/tooling/verify_stage1_fullspec.sh [--file:<path>] [--name:<bin>] [--jobs:<N>]
                                        [--backend:<obj>] [--frontend:<stage1|mvp>]
                                        [--target:<triple>] [--linker:<self>]
                                        [--mm:<orc|off>] [--timeout:<sec>] [--multi:<0|1>]
                                        [--skip-run] [--allow-skip]
                                        [--log:<path>]

Notes:
  - Builds and runs the stage1 fullspec sample (expects output: "fullspec ok").
  - Only backend obj/self-link path is supported (C fullspec path removed).
EOF
}

file="examples/stage1_codegen_fullspec.cheng"
name=""
jobs=""
mm=""
skip_run=""
allow_skip=""
backend="${CHENG_STAGE1_FULLSPEC_BACKEND:-obj}"
frontend="${CHENG_STAGE1_FULLSPEC_FRONTEND:-stage1}"
target="${CHENG_STAGE1_FULLSPEC_TARGET:-}"
linker="${CHENG_STAGE1_FULLSPEC_LINKER:-}"
timeout_s="${CHENG_STAGE1_FULLSPEC_TIMEOUT:-60}"
multi="${CHENG_STAGE1_FULLSPEC_MULTI:-1}"
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
    --frontend:*)
      frontend="${1#--frontend:}"
      ;;
    --target:*)
      target="${1#--target:}"
      ;;
    --linker:*)
      linker="${1#--linker:}"
      ;;
    --mm:*)
      mm="${1#--mm:}"
      ;;
    --timeout:*)
      timeout_s="${1#--timeout:}"
      ;;
    --multi:*)
      multi="${1#--multi:}"
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

if [ "$backend" != "obj" ]; then
  echo "[Error] invalid --backend:$backend (only obj is supported)" 1>&2
  exit 2
fi
case "$frontend" in
  stage1|mvp)
    ;;
  *)
    echo "[Error] invalid --frontend:$frontend (expected stage1|mvp)" 1>&2
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

if [ "$jobs" = "" ]; then
  jobs="${CHENG_STAGE1_FULLSPEC_JOBS:-}"
fi

if [ "$multi" != "0" ]; then
  multi="1"
fi
multi_force="${CHENG_STAGE1_FULLSPEC_MULTI_FORCE:-$multi}"
jobs_env=""
if [ "$jobs" != "" ]; then
  jobs_env="CHENG_BACKEND_JOBS=$jobs"
fi

run_with_timeout() {
  seconds="$1"
  shift
  perl -e '
    use POSIX qw(setsid WNOHANG);
    my $timeout = shift;
    my $pid = fork();
    if (!defined $pid) { exit 127; }
    if ($pid == 0) {
      setsid();
      exec @ARGV;
      exit 127;
    }
    my $end = time + $timeout;
    while (1) {
      my $res = waitpid($pid, WNOHANG);
      if ($res == $pid) {
        my $status = $?;
        if (($status & 127) != 0) {
          exit(128 + ($status & 127));
        }
        exit($status >> 8);
      }
      if (time >= $end) {
        kill "TERM", -$pid;
        select(undef, undef, undef, 0.5);
        kill "KILL", -$pid;
        exit 124;
      }
      select(undef, undef, undef, 0.1);
    }
  ' "$seconds" "$@"
}

run_cmd() {
  cmd_label="$1"
  shift
  echo "== $cmd_label =="
  set +e
  run_with_timeout "$timeout_s" "$@"
  status=$?
  set -e
  if [ "$status" = "124" ]; then
    echo "[Error] $cmd_label timed out after ${timeout_s}s" 1>&2
    exit 124
  fi
  if [ "$status" != "0" ]; then
    exit "$status"
  fi
}

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
      echo "[Error] stage1 fullspec obj unsupported host=$host_os/$host_arch" 1>&2
      exit 2
      ;;
  esac
fi

if [ "$linker" = "" ]; then
  linker="self"
fi
if [ "$linker" != "self" ]; then
  echo "[Error] invalid --linker:$linker (only self is supported)" 1>&2
  exit 2
fi

if [ "$host_os" = "Darwin" ] && ! command -v codesign >/dev/null 2>&1; then
  if [ "$allow_skip" != "" ]; then
    echo "[skip] stage1 fullspec obj: missing codesign" 1>&2
    exit 0
  fi
  echo "[Error] stage1 fullspec obj missing codesign (required for self-linked Mach-O)" 1>&2
  exit 2
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
  # shellcheck disable=SC2086
  run_cmd "build.stage1_fullspec.runtime_obj" env $jobs_env \
    CHENG_BACKEND_MULTI="$multi" \
    CHENG_BACKEND_MULTI_FORCE="$multi_force" \
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

# shellcheck disable=SC2086
run_cmd "build.stage1_fullspec.obj" env $envs $jobs_env \
  CHENG_BACKEND_MULTI="$multi" \
  CHENG_BACKEND_MULTI_FORCE="$multi_force" \
  CHENG_BACKEND_LINKER=self \
  CHENG_BACKEND_WHOLE_PROGRAM=1 \
  CHENG_BACKEND_NO_RUNTIME_C=1 \
  CHENG_BACKEND_RUNTIME_OBJ="$runtime_obj" \
  CHENG_BACKEND_VALIDATE=1 \
  CHENG_BACKEND_FRONTEND="$frontend" \
  CHENG_BACKEND_EMIT=exe \
  CHENG_BACKEND_TARGET="$target" \
  CHENG_BACKEND_INPUT="$file" \
  CHENG_BACKEND_OUTPUT="$root/$name" \
  "$driver"

if [ "$skip_run" != "" ]; then
  echo "stage1 fullspec build ok: $name"
  exit 0
fi

if [ ! -x "$root/$name" ]; then
  echo "[Error] missing output binary: $root/$name" 1>&2
  exit 2
fi

mkdir -p "$(dirname "$log")"
# shellcheck disable=SC2086
set +e
output="$(run_with_timeout "$timeout_s" env $envs "$root/$name" 2>&1)"
status=$?
set -e
printf "%s\n" "$output" >"$log"
if [ "$status" = "124" ]; then
  echo "[Error] stage1 fullspec run timed out after ${timeout_s}s (see $log)" 1>&2
  exit 124
fi
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
