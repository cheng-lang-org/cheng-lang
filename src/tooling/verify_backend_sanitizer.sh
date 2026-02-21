#!/usr/bin/env sh
. "$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)/env_prefix_bridge.sh"
set -eu
(set -o pipefail) 2>/dev/null && set -o pipefail

root="$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)"
cd "$root"

if [ "${CLEAN_CHENG_LOCAL:-1}" = "1" ] && [ "${TOOLING_CLEANUP_DEPTH:-0}" = "0" ]; then
  export TOOLING_CLEANUP_DEPTH=1
  cleanup_backend_driver_on_exit() {
    status=$?
    set +e
    sh src/tooling/cleanup_cheng_local.sh
    exit "$status"
  }
  trap cleanup_backend_driver_on_exit EXIT
fi

driver="$(sh src/tooling/backend_driver_path.sh)"
target="${BACKEND_TARGET:-arm64-apple-darwin}"
link_env="$(sh src/tooling/backend_link_env.sh --driver:"$driver" --target:"$target" --linker:system)"

host_os="$(uname -s 2>/dev/null || echo unknown)"
case "$host_os" in
  Darwin)
    ;;
  Linux)
    ;;
  *)
    echo "verify_backend_sanitizer skip: unsupported host os: $host_os" 1>&2
    exit 2
    ;;
esac

out_dir="artifacts/backend_sanitizer"
mkdir -p "$out_dir"

probe_c="$out_dir/probe.c"
probe_exe="$out_dir/probe"
cat >"$probe_c" <<'EOF'
int main(void) { return 0; }
EOF

set +e
cc -O1 -g -fno-omit-frame-pointer -fsanitize=address "$probe_c" -o "$probe_exe" >/dev/null 2>&1
probe_status="$?"
set -e
if [ "$probe_status" -ne 0 ]; then
  echo "verify_backend_sanitizer skip: cc does not support -fsanitize=address" 1>&2
  exit 2
fi


fixture="tests/cheng/backend/fixtures/return_add.cheng"
exe_path="$out_dir/return_add_asan"

env $link_env \
  BACKEND_EMIT=exe \
  BACKEND_MULTI=0 \
  BACKEND_MULTI_FORCE=0 \
  BACKEND_WHOLE_PROGRAM=1 \
  BACKEND_TARGET="$target" \
  BACKEND_CFLAGS="-O1 -g -fno-omit-frame-pointer -fsanitize=address" \
  BACKEND_LDFLAGS="-fsanitize=address" \
  BACKEND_INPUT="$fixture" \
  BACKEND_OUTPUT="$exe_path" \
  "$driver"

ASAN_OPTIONS=detect_leaks=0 "$exe_path"

echo "verify_backend_sanitizer ok"
