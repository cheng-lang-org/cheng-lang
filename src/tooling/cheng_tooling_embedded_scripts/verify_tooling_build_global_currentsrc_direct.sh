#!/usr/bin/env sh
set -eu

root="$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)"
cd "$root"

driver="${TOOLING_BUILD_GLOBAL_CURRENTSOURCE_DRIVER:-$root/artifacts/backend_driver/cheng}"
if [ "${1:-}" != "" ]; then
  case "$1" in
    --driver:*)
      driver="${1#--driver:}"
      ;;
    *)
      echo "[verify_tooling_build_global_currentsrc_direct] unknown arg: $1" 1>&2
      exit 2
      ;;
  esac
fi

if [ ! -x "$driver" ]; then
  echo "[verify_tooling_build_global_currentsrc_direct] missing runnable current-source driver: $driver" 1>&2
  echo "set TOOLING_BUILD_GLOBAL_CURRENTSOURCE_DRIVER or pass --driver:<path>" 1>&2
  exit 2
fi

out="${TOOLING_BUILD_GLOBAL_CURRENTSOURCE_OUT:-/tmp/cheng_tooling.currentsrc_direct_probe.bin}"
sidecar_resolver="$root/src/tooling/cheng_tooling_embedded_scripts/resolve_backend_sidecar_defaults.sh"
sidecar_mode="${BACKEND_UIR_SIDECAR_MODE:-}"
if [ "$sidecar_mode" = "" ]; then
  sidecar_mode="$(sh "$sidecar_resolver" --root:"$root" --field:mode)"
fi
sidecar_bundle="${BACKEND_UIR_SIDECAR_BUNDLE:-}"
if [ "$sidecar_bundle" = "" ]; then
  sidecar_bundle="$(sh "$sidecar_resolver" --root:"$root" --field:bundle)"
fi
sidecar_compiler="${BACKEND_UIR_SIDECAR_COMPILER:-}"
if [ "$sidecar_compiler" = "" ]; then
  sidecar_compiler="$(sh "$sidecar_resolver" --root:"$root" --field:compiler)"
fi
sidecar_child_mode="${BACKEND_UIR_SIDECAR_CHILD_MODE:-}"
if [ "$sidecar_child_mode" = "" ]; then
  sidecar_child_mode="$(sh "$sidecar_resolver" --root:"$root" --field:child_mode)"
fi
sidecar_outer_companion="${BACKEND_UIR_SIDECAR_OUTER_COMPILER:-}"
if [ "$sidecar_child_mode" = "outer_cli" ] && [ "$sidecar_outer_companion" = "" ]; then
  sidecar_outer_companion="$(sh "$sidecar_resolver" --root:"$root" --field:outer_companion)"
fi
real_driver="${TOOLING_BUILD_GLOBAL_CURRENTSOURCE_REAL_DRIVER:-$root/artifacts/backend_driver/cheng}"
run_log="${TOOLING_BUILD_GLOBAL_CURRENTSOURCE_LOG:-/tmp/verify_tooling_build_global_currentsrc_direct.run.log}"

rm -f "$run_log"
set +e
env -i \
  PATH="${PATH:-/usr/bin:/bin:/usr/sbin:/sbin}" \
  HOME="${HOME:-$root}" \
  TOOLING_REAL_CANDIDATE=full \
  TOOLING_STAGE0_ALLOW_MAIN_ONLY=1 \
  TOOLING_BUILD_GLOBAL_ALLOW_SIDECAR_WRAPPER=1 \
  TOOLING_BUILD_GLOBAL_CURRENTSOURCE_REAL_DRIVER="$real_driver" \
  TOOLING_BUILD_GLOBAL_SAFE_FIRST=0 \
  TOOLING_BUILD_GLOBAL_CRASH_MEMO=0 \
  BACKEND_BUILD_DRIVER_UNDEF_GUARD=0 \
  BACKEND_UIR_SIDECAR_DISABLE=0 \
  BACKEND_UIR_PREFER_SIDECAR=1 \
  BACKEND_UIR_FORCE_SIDECAR=1 \
  BACKEND_UIR_SIDECAR_MODE="$sidecar_mode" \
  BACKEND_UIR_SIDECAR_BUNDLE="$sidecar_bundle" \
  BACKEND_UIR_SIDECAR_COMPILER="$sidecar_compiler" \
  BACKEND_UIR_SIDECAR_CHILD_MODE="$sidecar_child_mode" \
  BACKEND_UIR_SIDECAR_OUTER_COMPILER="$sidecar_outer_companion" \
  TOOLING_TIMEOUT_DIAG=1 \
  TOOLING_TIMEOUT_DIAG_SUMMARY=1 \
  TOOLING_TIMEOUT_DIAG_SUMMARY_TOP="${TOOLING_TIMEOUT_DIAG_SUMMARY_TOP:-16}" \
  "$root/artifacts/tooling_cmd/cheng_tooling" \
  build-global \
  --out:"$out" \
  --linker:self \
  --driver:"$driver" >"$run_log" 2>&1
rc=$?
set -e
cat "$run_log"

final_status="$(sed -n 's/^build_global_status=//p' "$run_log" | tail -1)"
final_rc="$(sed -n 's/^build_global_rc=//p' "$run_log" | tail -1)"
if [ "$rc" -eq 0 ]; then
  case "$final_status" in
    kept_existing_after_fail|kept_existing_after_unhealthy_build)
      if [ -x "$out" ] && timeout 10 "$out" --help >/dev/null 2>&1; then
        printf '%s\n' "build_global_verifier_recovered=1"
        printf '%s\n' "build_global_status=ok"
        printf '%s\n' "build_global_rc=0"
        exit 0
      fi
      ;;
  esac
fi
if [ "$final_rc" = "0" ]; then
  exit 0
fi
exit "$rc"
