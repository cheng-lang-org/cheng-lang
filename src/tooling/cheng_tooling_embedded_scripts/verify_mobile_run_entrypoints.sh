#!/usr/bin/env sh
:
set -euo pipefail

root="$(cd "$(dirname "$0")/../.." && pwd)"
cd "$root"

echo "[verify-mobile-run] syntax checks"
sh -n ${TOOLING_SELF_BIN:-artifacts/tooling_cmd/cheng_tooling} chengc
sh -n ${TOOLING_SELF_BIN:-artifacts/tooling_cmd/cheng_tooling} build_mobile_export
sh -n ${TOOLING_SELF_BIN:-artifacts/tooling_cmd/cheng_tooling} mobile_run_android
sh -n ${TOOLING_SELF_BIN:-artifacts/tooling_cmd/cheng_tooling} mobile_run_ios
sh -n ${TOOLING_SELF_BIN:-artifacts/tooling_cmd/cheng_tooling} mobile_run_harmony

echo "[verify-mobile-run] help checks"
help_main="$(${TOOLING_SELF_BIN:-artifacts/tooling_cmd/cheng_tooling} chengc --help)"
printf '%s\n' "$help_main" | rg -q "run <android\\|ios\\|harmony>"
help_run="$(${TOOLING_SELF_BIN:-artifacts/tooling_cmd/cheng_tooling} chengc run --help)"
printf '%s\n' "$help_run" | rg -q "run android"
printf '%s\n' "$help_run" | rg -q "run ios"
printf '%s\n' "$help_run" | rg -q "run harmony"
printf '%s\n' "$help_run" | rg -q "\-\-no-build"
printf '%s\n' "$help_run" | rg -q "\-\-no-install"
printf '%s\n' "$help_run" | rg -q "\-\-no-run"

echo "[verify-mobile-run] argument error checks"
set +e
${TOOLING_SELF_BIN:-artifacts/tooling_cmd/cheng_tooling} chengc run android /no/such/file.cheng >/tmp/cheng_verify_mobile_run_android.log 2>&1
rc_android=$?
${TOOLING_SELF_BIN:-artifacts/tooling_cmd/cheng_tooling} chengc run ios /no/such/file.cheng >/tmp/cheng_verify_mobile_run_ios.log 2>&1
rc_ios=$?
${TOOLING_SELF_BIN:-artifacts/tooling_cmd/cheng_tooling} chengc run harmony /no/such/file.cheng >/tmp/cheng_verify_mobile_run_harmony.log 2>&1
rc_harmony=$?
set -e
if [ "$rc_android" -ne 2 ] || [ "$rc_ios" -ne 2 ] || [ "$rc_harmony" -ne 2 ]; then
  echo "[Error] run argument checks failed: android=$rc_android ios=$rc_ios harmony=$rc_harmony" 1>&2
  exit 2
fi

echo "[verify-mobile-run] export and kotlin-only checks"
out_android="/tmp/cheng_verify_mobile_run_android_$$"
out_ios="/tmp/cheng_verify_mobile_run_ios_$$"
out_harmony="/tmp/cheng_verify_mobile_run_harmony_$$"
trap 'rm -rf "$out_android" "$out_ios" "$out_harmony"' EXIT

${TOOLING_SELF_BIN:-artifacts/tooling_cmd/cheng_tooling} build_mobile_export examples/mobile_hello.cheng --with-android-project --out:"$out_android" --name:verify_mobile_android
${TOOLING_SELF_BIN:-artifacts/tooling_cmd/cheng_tooling} verify_android_kotlin_only --project:"$out_android/android_project"

${TOOLING_SELF_BIN:-artifacts/tooling_cmd/cheng_tooling} build_mobile_export examples/mobile_hello.cheng --with-ios-project --out:"$out_ios" --name:verify_mobile_ios
[ -d "$out_ios/ios_project" ]

${TOOLING_SELF_BIN:-artifacts/tooling_cmd/cheng_tooling} build_mobile_export examples/mobile_hello.cheng --with-harmony-project --out:"$out_harmony" --name:verify_mobile_harmony
[ -d "$out_harmony/harmony_project" ]

echo "[verify-mobile-run] smoke run checks"
${TOOLING_SELF_BIN:-artifacts/tooling_cmd/cheng_tooling} mobile_run_android examples/mobile_hello.cheng --out:"$out_android/run_android" --no-build --no-install --no-run
${TOOLING_SELF_BIN:-artifacts/tooling_cmd/cheng_tooling} mobile_run_harmony examples/mobile_hello.cheng --out:"$out_harmony/run_harmony"
${TOOLING_SELF_BIN:-artifacts/tooling_cmd/cheng_tooling} mobile_run_ios examples/mobile_hello.cheng --out:"$out_ios/run_ios"

echo "[verify-mobile-run] ok"
