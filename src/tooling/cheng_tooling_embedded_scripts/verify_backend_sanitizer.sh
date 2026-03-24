#!/usr/bin/env sh
:
set -eu
(set -o pipefail) 2>/dev/null && set -o pipefail

root="$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)"
cd "$root"

if [ "${CLEAN_CHENG_LOCAL:-1}" = "1" ] && [ "${TOOLING_CLEANUP_DEPTH:-0}" = "0" ]; then
  export TOOLING_CLEANUP_DEPTH=1
  cleanup_backend_driver_on_exit() {
    status=$?
    set +e
    ${TOOLING_SELF_BIN:-artifacts/tooling_cmd/cheng_tooling} cleanup_cheng_local
    exit "$status"
  }
  trap cleanup_backend_driver_on_exit EXIT
fi

driver="$(${TOOLING_SELF_BIN:-artifacts/tooling_cmd/cheng_tooling} backend_driver_path)"

resolve_target() {
  if [ "${BACKEND_TARGET:-}" != "" ] && [ "${BACKEND_TARGET:-}" != "auto" ]; then
    printf '%s\n' "${BACKEND_TARGET}"
    return
  fi
  ${TOOLING_SELF_BIN:-artifacts/tooling_cmd/cheng_tooling} detect_host_target 2>/dev/null || printf '%s\n' arm64-apple-darwin
}

target="$(resolve_target)"

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

env \
  BACKEND_CFLAGS="-O1 -g -fno-omit-frame-pointer -fsanitize=address" \
  BACKEND_LDFLAGS="-fsanitize=address" \
  "$driver" "$fixture" \
    --frontend:stage1 \
    --emit:exe \
    --target:"$target" \
    --linker:system \
    --no-multi \
    --no-multi-force \
    --output:"$exe_path"

ASAN_OPTIONS=detect_leaks=0 "$exe_path"

echo "verify_backend_sanitizer ok"
