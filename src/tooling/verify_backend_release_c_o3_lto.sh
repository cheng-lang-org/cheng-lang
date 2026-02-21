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

if [ "${BACKEND_DRIVER:-}" != "" ]; then
  driver="${BACKEND_DRIVER}"
else
  driver="$(env \
    BACKEND_DRIVER_PATH_PREFER_REBUILD="${BACKEND_DRIVER_PATH_PREFER_REBUILD:-1}" \
    BACKEND_DRIVER_ALLOW_FALLBACK="${BACKEND_DRIVER_ALLOW_FALLBACK:-0}" \
    sh src/tooling/backend_driver_path.sh)"
fi
if [ ! -x "$driver" ]; then
  echo "[verify_backend_release_c_o3_lto] backend driver not executable: $driver" 1>&2
  exit 1
fi

if ! command -v cc >/dev/null 2>&1; then
  echo "[verify_backend_release_c_o3_lto] missing cc (required for O3/LTO toolchain probe)" 1>&2
  exit 1
fi

target="${BACKEND_TARGET:-}"
if [ "$target" = "" ] || [ "$target" = "auto" ]; then
  target="$(sh src/tooling/detect_host_target.sh)"
fi
if [ "$target" = "" ] || [ "$target" = "auto" ]; then
  echo "[verify_backend_release_c_o3_lto] failed to resolve target" 1>&2
  exit 1
fi

out_dir="artifacts/backend_release_c_o3_lto"
mkdir -p "$out_dir"

fixture="${BACKEND_RELEASE_FIXTURE:-tests/cheng/backend/fixtures/return_add.cheng}"
if [ ! -f "$fixture" ]; then
  fixture="tests/cheng/backend/fixtures/return_i64.cheng"
fi
if [ ! -f "$fixture" ]; then
  echo "[verify_backend_release_c_o3_lto] missing fixture: $fixture" 1>&2
  exit 1
fi

contains_token() {
  hay="$1"
  needle="$2"
  case " $hay " in
    *" $needle "*) return 0 ;;
  esac
  return 1
}

append_token() {
  flags="$1"
  token="$2"
  if contains_token "$flags" "$token"; then
    printf '%s\n' "$flags"
    return
  fi
  if [ "$flags" = "" ]; then
    printf '%s\n' "$token"
  else
    printf '%s %s\n' "$flags" "$token"
  fi
}

sha256_file() {
  if command -v shasum >/dev/null 2>&1; then
    shasum -a 256 "$1" | awk '{print $1}'
    return
  fi
  if command -v sha256sum >/dev/null 2>&1; then
    sha256sum "$1" | awk '{print $1}'
    return
  fi
  echo ""
}

has_fuse_ld_flag() {
  case "${1:-}" in
    *-fuse-ld=*) return 0 ;;
  esac
  return 1
}

host_os="$(uname -s 2>/dev/null || echo unknown)"
host_arch="$(uname -m 2>/dev/null || echo unknown)"

release_cflags="${BACKEND_RELEASE_CFLAGS:--O3 -flto}"
release_ldflags="${BACKEND_RELEASE_LDFLAGS:-$release_cflags}"
darwin_no_uuid="${BACKEND_RELEASE_DARWIN_NO_UUID:-0}"
system_link_kind="default"
case "$host_os" in
  Darwin)
    case "$darwin_no_uuid" in
      1|true|TRUE|yes|YES|on|ON)
        release_ldflags="$(append_token "$release_ldflags" "-Wl,-no_uuid")"
        ;;
    esac
    ;;
  Linux)
    release_ldflags="$(append_token "$release_ldflags" "-Wl,--build-id=none")"
    ;;
esac
if [ "${BACKEND_LD:-}" = "" ] && ! has_fuse_ld_flag "$release_ldflags"; then
  resolver_out="$(env \
    BACKEND_SYSTEM_LINKER_PRIORITY="${BACKEND_SYSTEM_LINKER_PRIORITY:-mold,lld,default}" \
    sh src/tooling/resolve_system_linker.sh --ldflags:"$release_ldflags")"
  resolver_kind="$(printf '%s\n' "$resolver_out" | sed -n 's/^kind=//p' | head -n 1)"
  resolver_ldflags="$(printf '%s\n' "$resolver_out" | sed -n 's/^ldflags=//p' | head -n 1)"
  if [ "$resolver_kind" != "" ]; then
    system_link_kind="$resolver_kind"
  fi
  if [ "$resolver_ldflags" != "" ]; then
    release_ldflags="$(append_token "$release_ldflags" "$resolver_ldflags")"
  fi
elif [ "${BACKEND_LD:-}" != "" ]; then
  system_link_kind="explicit_ld"
else
  system_link_kind="user_ldflags"
fi

# Step 1: GCC/Clang O3+LTO capability probe.
cc_probe_c="$out_dir/cc_o3_lto_probe.c"
cc_probe_log="$out_dir/cc_o3_lto_probe.log"
cc_probe_exe="$out_dir/cc_o3_lto_probe"
cat >"$cc_probe_c" <<'EOF'
int main(void) { return 0; }
EOF
set +e
cc -O3 -flto "$cc_probe_c" -o "$cc_probe_exe" >"$cc_probe_log" 2>&1
cc_probe_status="$?"
set -e
if [ "$cc_probe_status" -ne 0 ]; then
  tail -n 120 "$cc_probe_log" 1>&2 || true
  echo "[verify_backend_release_c_o3_lto] cc O3/LTO probe failed" 1>&2
  exit "$cc_probe_status"
fi
"$cc_probe_exe"

# Step 2: release system-link + O3/LTO (required).
system_exe="$out_dir/return_add.system_o3_lto"
system_log="$out_dir/return_add.system_o3_lto.log"
system_status="ok"
rm -f "$system_exe" "$system_exe.o" "$system_log"
rm -rf "${system_exe}.objs" "${system_exe}.objs.lock"
set +e
env \
  ABI=v2_noptr \
  STAGE1_STD_NO_POINTERS=0 \
  STAGE1_STD_NO_POINTERS_STRICT=0 \
  STAGE1_NO_POINTERS_NON_C_ABI=0 \
  STAGE1_NO_POINTERS_NON_C_ABI_INTERNAL=0 \
  MM=orc \
  BACKEND_BUILD_TRACK=release \
  BACKEND_LINKER=system \
  BACKEND_NO_RUNTIME_C=0 \
  BACKEND_EMIT=exe \
  BACKEND_MULTI=0 \
  BACKEND_MULTI_FORCE=0 \
  BACKEND_WHOLE_PROGRAM=1 \
  BACKEND_TARGET="$target" \
  BACKEND_CFLAGS="$release_cflags" \
  BACKEND_LDFLAGS="$release_ldflags" \
  BACKEND_INPUT="$fixture" \
  BACKEND_OUTPUT="$system_exe" \
  "$driver" >"$system_log" 2>&1
system_build_status="$?"
set -e
if [ "$system_build_status" -ne 0 ]; then
  cat "$system_log" 1>&2 || true
  echo "[verify_backend_release_c_o3_lto] release system-link O3/LTO compile failed" 1>&2
  exit "$system_build_status"
fi
set +e
"$system_exe" >>"$system_log" 2>&1
system_run_status="$?"
set -e
if [ "$system_run_status" -ne 0 ]; then
  cat "$system_log" 1>&2 || true
  echo "[verify_backend_release_c_o3_lto] release system-link O3/LTO run failed" 1>&2
  exit "$system_run_status"
fi

manifest="$out_dir/release_manifest.json"
bundle="$out_dir/backend_release.tar.gz"
release_name="${BACKEND_RELEASE_NAME:-cheng}"
sh src/tooling/backend_release_manifest.sh --out:"$manifest" --name:"$release_name" --driver:"$driver" >/dev/null
sh src/tooling/backend_release_bundle.sh --out:"$bundle" --name:"$release_name" --manifest:"$manifest" --driver:"$driver" --extra:"$system_exe" >/dev/null

if [ ! -s "$manifest" ] || [ ! -s "$bundle" ]; then
  echo "[verify_backend_release_c_o3_lto] release artifacts missing (manifest/bundle)" 1>&2
  exit 1
fi

timestamp="$(date -u +"%Y-%m-%dT%H:%M:%SZ" 2>/dev/null || echo unknown)"
cc_path="$(command -v cc 2>/dev/null || echo cc)"
cc_version="$("$cc_path" --version 2>/dev/null | head -n 1 | tr -d '\r' || echo unknown)"
system_exe_sha="$(sha256_file "$system_exe")"
bundle_sha="$(sha256_file "$bundle")"

report="$out_dir/backend_release_c_o3_lto.report.txt"
cat >"$report" <<EOF
gate=backend.release_system_link
timestamp=$timestamp
host_os=$host_os
host_arch=$host_arch
target=$target
driver=$driver
fixture=$fixture
cc_path=$cc_path
cc_version=$cc_version
release_cflags=$release_cflags
release_ldflags=$release_ldflags
darwin_no_uuid=$darwin_no_uuid
system_exe=$system_exe
system_exe_sha256=$system_exe_sha
system_link_status=$system_status
system_link_kind=$system_link_kind
system_link_log=$system_log
release_manifest=$manifest
release_bundle=$bundle
release_bundle_sha256=$bundle_sha
EOF

echo "verify_backend_release_c_o3_lto ok"
