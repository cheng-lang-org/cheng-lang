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

target_supports_self_linker() {
  t="$1"
  case "$t" in
    *darwin*)
      case "$t" in
        *arm64*|*aarch64*|*x86_64*|*amd64*) return 0 ;;
      esac
      return 1
      ;;
    *linux*|*android*)
      case "$t" in
        *arm64*|*aarch64*|*riscv64*) return 0 ;;
      esac
      return 1
      ;;
    *windows*|*msvc*)
      case "$t" in
        *arm64*|*aarch64*) return 0 ;;
      esac
      return 1
      ;;
  esac
  return 1
}

stamp_read() {
  key="$1"
  file="$2"
  sed -n "s/^${key}=//p" "$file" | head -n 1
}

assert_eq() {
  name="$1"
  got="$2"
  want="$3"
  if [ "$got" != "$want" ]; then
    echo "[verify_backend_dual_track] $name mismatch: got '$got', want '$want'" 1>&2
    exit 1
  fi
}

target="${BACKEND_TARGET:-}"
if [ "$target" = "" ] || [ "$target" = "auto" ]; then
  target="$(sh src/tooling/detect_host_target.sh)"
fi
if [ "$target" = "" ] || [ "$target" = "auto" ]; then
  echo "[verify_backend_dual_track] failed to resolve target" 1>&2
  exit 1
fi

fixture="${BACKEND_DUAL_TRACK_FIXTURE:-tests/cheng/backend/fixtures/return_add.cheng}"
if [ ! -f "$fixture" ]; then
  fixture="tests/cheng/backend/fixtures/return_i64.cheng"
fi
if [ ! -f "$fixture" ]; then
  echo "[verify_backend_dual_track] missing fixture: $fixture" 1>&2
  exit 1
fi

out_dir="artifacts/backend_dual_track"
mkdir -p "$out_dir"

run_chengc_with_stamp() {
  label="$1"
  allow_fail="$2"
  shift 2
  out="$out_dir/${label}"
  stamp="$out_dir/${label}.compile_stamp.txt"
  rm -f "$out" "$out.o" "$stamp"
  rm -rf "${out}.objs" "${out}.objs.lock"
  set +e
  env -u BACKEND_LINKER -u BACKEND_BUILD_TRACK -u BACKEND_NO_RUNTIME_C \
    BACKEND_TARGET="$target" \
    BACKEND_COMPILE_STAMP_OUT="$stamp" \
    BACKEND_SYSTEM_LINKER_PRIORITY="${BACKEND_SYSTEM_LINKER_PRIORITY:-mold,lld,default}" \
    sh src/tooling/chengc.sh "$fixture" --skip-pkg --out:"$out" "$@" >/dev/null
  status="$?"
  set -e
  if [ "$status" -ne 0 ] && [ "$allow_fail" != "1" ]; then
    echo "[verify_backend_dual_track] chengc failed (label=$label, status=$status)" 1>&2
    exit "$status"
  fi
  if [ ! -s "$stamp" ]; then
    echo "[verify_backend_dual_track] missing compile stamp: $stamp" 1>&2
    exit 1
  fi
}

expected_default_linker="system"
if target_supports_self_linker "$target"; then
  expected_default_linker="self"
fi

run_chengc_with_stamp "default_dev" "0"
default_stamp="$out_dir/default_dev.compile_stamp.txt"
assert_eq "default.build_track" "$(stamp_read build_track "$default_stamp")" "dev"
assert_eq "default.resolved_linker" "$(stamp_read resolved_linker "$default_stamp")" "$expected_default_linker"

run_chengc_with_stamp "release_default" "1" --release
release_stamp="$out_dir/release_default.compile_stamp.txt"
assert_eq "release.build_track" "$(stamp_read build_track "$release_stamp")" "release"
assert_eq "release.resolved_linker" "$(stamp_read resolved_linker "$release_stamp")" "system"
assert_eq "release.no_runtime_c" "$(stamp_read no_runtime_c "$release_stamp")" "0"

if target_supports_self_linker "$target"; then
  out="$out_dir/cli_override"
  stamp="$out_dir/cli_override.compile_stamp.txt"
  rm -f "$out" "$out.o" "$stamp"
  rm -rf "${out}.objs" "${out}.objs.lock"
  set +e
  env -u BACKEND_BUILD_TRACK -u BACKEND_NO_RUNTIME_C \
    BACKEND_TARGET="$target" \
    BACKEND_LINKER=system \
    BACKEND_COMPILE_STAMP_OUT="$stamp" \
    sh src/tooling/chengc.sh "$fixture" --skip-pkg --out:"$out" --linker:self >/dev/null
  status="$?"
  set -e
  if [ "$status" -ne 0 ]; then
    echo "[verify_backend_dual_track] warn: cli override compile failed (self override), continue with stamp check" 1>&2
  fi
  assert_eq "override.resolved_linker" "$(stamp_read resolved_linker "$stamp")" "self"
else
  out="$out_dir/cli_override"
  stamp="$out_dir/cli_override.compile_stamp.txt"
  rm -f "$out" "$out.o" "$stamp"
  rm -rf "${out}.objs" "${out}.objs.lock"
  set +e
  env -u BACKEND_BUILD_TRACK -u BACKEND_NO_RUNTIME_C \
    BACKEND_TARGET="$target" \
    BACKEND_LINKER=self \
    BACKEND_COMPILE_STAMP_OUT="$stamp" \
    sh src/tooling/chengc.sh "$fixture" --skip-pkg --out:"$out" --linker:system >/dev/null
  status="$?"
  set -e
  if [ "$status" -ne 0 ]; then
    echo "[verify_backend_dual_track] warn: cli override compile failed (system override), continue with stamp check" 1>&2
  fi
  assert_eq "override.resolved_linker" "$(stamp_read resolved_linker "$stamp")" "system"
fi

report="$out_dir/backend_dual_track.report.txt"
{
  echo "gate=backend.dual_track"
  echo "target=$target"
  echo "fixture=$fixture"
  echo "default_expected_linker=$expected_default_linker"
  echo "default_build_track=$(stamp_read build_track "$default_stamp")"
  echo "default_resolved_linker=$(stamp_read resolved_linker "$default_stamp")"
  echo "release_build_track=$(stamp_read build_track "$release_stamp")"
  echo "release_resolved_linker=$(stamp_read resolved_linker "$release_stamp")"
  echo "release_no_runtime_c=$(stamp_read no_runtime_c "$release_stamp")"
} >"$report"

echo "verify_backend_dual_track ok"
