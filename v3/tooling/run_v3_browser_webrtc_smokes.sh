#!/usr/bin/env sh
set -eu

root="$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)"
browser_dir="$root/v3/tooling/browser_webrtc"
out_dir="$root/artifacts/v3_browser_webrtc"
default_hostc="$root/artifacts/v3_backend_driver/cheng"
hostc="${CHENG_V3_SMOKE_COMPILER:-$default_hostc}"
stage2="$root/artifacts/v3_bootstrap/cheng.stage2"
stage3="$root/artifacts/v3_bootstrap/cheng.stage3"
mkdir -p "$out_dir"
cd "$root"

if [ "${CHENG_V3_SMOKE_COMPILER:-}" = "" ] && [ ! -x "$hostc" ]; then
  sh "$root/v3/tooling/build_backend_driver_v3.sh"
fi

if [ ! -x "$hostc" ]; then
  echo "v3 browser webrtc smokes: missing host compiler: $hostc" >&2
  exit 1
fi

for compiler in "$stage2" "$stage3"
do
  if [ ! -x "$compiler" ]; then
    echo "v3 browser webrtc smokes: missing compiler: $compiler" >&2
    exit 1
  fi
done

if [ ! -d "$browser_dir/node_modules/playwright" ]; then
  echo "[v3 browser webrtc smokes] npm install"
  (cd "$browser_dir" && npm install --no-fund --no-audit)
fi
echo "[v3 browser webrtc smokes] playwright install chromium"
(cd "$browser_dir" && npx playwright install chromium)

run_browser_smoke() {
  compiler="$1"
  stage_name="$2"
  smoke_name="$3"
  src="$root/v3/src/tests/$smoke_name.cheng"
  bin="$out_dir/$smoke_name.$stage_name"
  run_log="$out_dir/$smoke_name.$stage_name.run.log"
  rm -f "$run_log"
  BACKEND_INCREMENTAL=0 BACKEND_MULTI_MODULE_CACHE=0 \
    zsh "$root/v3/tooling/run_v3_compile_exe.sh" \
      "v3 browser webrtc smokes" \
      "$stage_name $smoke_name" \
      "$compiler" \
      "$root/v3" \
      "$src" \
      "$bin"
  echo "[v3 browser webrtc smokes] run $stage_name $smoke_name"
  if ! "$bin" >"$run_log" 2>&1; then
    echo "v3 browser webrtc smokes: run failed: $stage_name $smoke_name" >&2
    tail -n 80 "$run_log" >&2 || true
    exit 1
  fi
  cat "$run_log"
}

tests="
webrtc_signal_stream_smoke
webrtc_datachannel_browser_smoke
webrtc_browser_content_smoke
libp2p_webrtc_sync_smoke
webrtc_browser_pubsub_smoke
webrtc_browser_light_node_smoke
browser_sdk_smoke
"

if [ "$#" -gt 0 ]; then
  tests="$*"
fi

for smoke_name in $tests
do
  run_browser_smoke "$hostc" host "$smoke_name"
  run_browser_smoke "$stage2" stage2 "$smoke_name"
  run_browser_smoke "$stage3" stage3 "$smoke_name"
done

echo "v3 browser webrtc smokes: ok"
