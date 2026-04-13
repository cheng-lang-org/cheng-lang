#!/usr/bin/env sh
set -eu

root="$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)"
out="$root/artifacts/v3_browser_host_wasm/cheng_browser_host_abi.wasm"

sh "$root/v3/tooling/build_browser_host_wasm_v3.sh" "$out"

node - <<'EOF' "$out"
const fs = require('fs');

const wasmPath = process.argv[2];
const bytes = fs.readFileSync(wasmPath);

WebAssembly.instantiate(bytes, {
  env: {
    cheng_v3_browser_now_ms_bridge: () => 24680,
    cheng_v3_browser_echo_i32_bridge: (value) => value | 0,
    cheng_v3_browser_dm_active_count_bridge: () => 7,
    cheng_v3_browser_dm_connect_start_bridge: (peerHandle) => (peerHandle | 0) + 1000,
    cheng_v3_browser_dm_wait_start_bridge: (peerHandle, timeoutMs) => ((peerHandle | 0) ^ (timeoutMs | 0)) | 0,
    cheng_v3_browser_dm_send_start_bridge: (peerHandle, conversationHandle, messageHandle, timestampMs) =>
      ((peerHandle | 0) + (conversationHandle | 0) + (messageHandle | 0) + (timestampMs | 0)) | 0,
  },
}).then(({ instance }) => {
  const exports = instance.exports;
  if (typeof exports.main !== 'function') {
    throw new Error('missing export: main');
  }
  if (typeof exports.browserNowMs !== 'function') {
    throw new Error('missing export: browserNowMs');
  }
  if (typeof exports.browserEchoI32 !== 'function') {
    throw new Error('missing export: browserEchoI32');
  }
  if (typeof exports.browserDmActiveCount !== 'function') {
    throw new Error('missing export: browserDmActiveCount');
  }
  if (typeof exports.browserDmConnectStart !== 'function') {
    throw new Error('missing export: browserDmConnectStart');
  }
  if (typeof exports.browserDmWaitStart !== 'function') {
    throw new Error('missing export: browserDmWaitStart');
  }
  if (typeof exports.browserDmSendStart !== 'function') {
    throw new Error('missing export: browserDmSendStart');
  }
  if ((exports.main() | 0) !== 0) {
    throw new Error('main != 0');
  }
  if ((exports.browserNowMs() | 0) !== 24680) {
    throw new Error('browserNowMs != 24680');
  }
  if ((exports.browserEchoI32(31337) | 0) !== 31337) {
    throw new Error('browserEchoI32 != 31337');
  }
  if ((exports.browserDmActiveCount() | 0) !== 7) {
    throw new Error('browserDmActiveCount != 7');
  }
  if ((exports.browserDmConnectStart(9) | 0) !== 1009) {
    throw new Error('browserDmConnectStart mismatch');
  }
  if ((exports.browserDmWaitStart(11, 22) | 0) !== ((11 ^ 22) | 0)) {
    throw new Error('browserDmWaitStart mismatch');
  }
  if ((exports.browserDmSendStart(1, 2, 3, 4) | 0) !== 10) {
    throw new Error('browserDmSendStart mismatch');
  }
  console.log('v3 browser host wasm smoke: ok');
}).catch((error) => {
  console.error(error);
  process.exit(1);
});
EOF
