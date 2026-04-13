#!/usr/bin/env sh
set -eu

root="$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)"
compiler="${1:-}"
stage_name="${2:-host}"
out_dir="$root/artifacts/v3_tailnet_control"
mkdir -p "$out_dir"
cd "$root"

probe_port="$(python3 - <<'PY'
import socket
s = socket.socket()
s.bind(("127.0.0.1", 0))
print(s.getsockname()[1])
s.close()
PY
)"
probe_dir="$root/v3/testdata/tailnet"
probe_log="$out_dir/tailnet_probe_server.$stage_name.log"
python3 -m http.server "$probe_port" --bind 127.0.0.1 --directory "$probe_dir" >"$probe_log" 2>&1 &
probe_pid=$!
cleanup() {
  if [ "${probe_pid:-0}" -gt 0 ] 2>/dev/null; then
    kill "$probe_pid" 2>/dev/null || true
    wait "$probe_pid" 2>/dev/null || true
  fi
}
trap cleanup EXIT INT TERM
python3 - <<PY
import time
import urllib.request
url = "http://127.0.0.1:$probe_port/headscale_control_ready.json"
for _ in range(50):
    try:
        with urllib.request.urlopen(url, timeout=0.2) as resp:
            if resp.status == 200:
                raise SystemExit(0)
    except Exception:
        time.sleep(0.1)
raise SystemExit(1)
PY
control_probe_endpoint="http://127.0.0.1:$probe_port/headscale_control_ready.json"
relay_probe_endpoint="http://127.0.0.1:$probe_port/derp_ready.json"

if [ -z "$compiler" ] || [ ! -x "$compiler" ]; then
  echo "v3 tailnet control: missing compiler: $compiler" >&2
  exit 1
fi

compile_one() {
  src="$1"
  bin="$2"
  label="$3"
  zsh "$root/v3/tooling/run_v3_compile_exe.sh" \
    "v3 tailnet control" \
    "$stage_name $label" \
    "$compiler" \
    "$root/v3" \
    "$src" \
    "$bin"
}

baseline_src="$root/v3/src/tests/libp2p_tailnet_transport_smoke.cheng"
baseline_bin="$out_dir/libp2p_tailnet_transport_smoke.$stage_name"
core_smoke_src="$root/v3/src/tests/tailnet_control_core_smoke.cheng"
core_smoke_bin="$out_dir/tailnet_control_core_smoke.$stage_name"
core_src="$root/v3/src/project/tailnet_control_core.cheng"
core_bin="$out_dir/tailnet_control_core.$stage_name"

echo "[v3 tailnet control] compile $stage_name baseline tailnet transport smoke"
compile_one "$baseline_src" "$baseline_bin" "baseline tailnet transport smoke"
echo "[v3 tailnet control] run $stage_name baseline tailnet transport smoke"
"$baseline_bin" >"$out_dir/libp2p_tailnet_transport_smoke.$stage_name.run.log" 2>&1
cat "$out_dir/libp2p_tailnet_transport_smoke.$stage_name.run.log"

echo "[v3 tailnet control] compile $stage_name decode smoke"
compile_one "$core_smoke_src" "$core_smoke_bin" "decode smoke"
echo "[v3 tailnet control] run $stage_name decode smoke"
"$core_smoke_bin" >"$out_dir/tailnet_control_core_smoke.$stage_name.run.log" 2>&1
cat "$out_dir/tailnet_control_core_smoke.$stage_name.run.log"

echo "[v3 tailnet control] compile $stage_name control core"
compile_one "$core_src" "$core_bin" "control core"

echo "[v3 tailnet control] run $stage_name control core configure"
"$core_bin" \
  --command configure \
  --provider-headscale 1 \
  --control-url https://headscale.example \
  --control-endpoint https://headscale.example/control \
  --relay-endpoint https://derp.sfo.example:443 \
  --control-probe-endpoint "$control_probe_endpoint" \
  --relay-probe-endpoint "$relay_probe_endpoint" \
  --namespace infra \
  --noise-public-key noise-pub-cli \
  --derp-region-code sfo \
  --derp-region-name SanFrancisco \
  --derp-hostname derp.sfo.example \
  --synthetic-ipv4 100.64.0.44 \
  --synthetic-ipv6 fd7a:115c:a1e0::44 \
  --local-peer-id tailnet-control-test \
  --exact-routing 1 \
  --provider-ready 1 \
  --proxy-ready 1 >"$out_dir/tailnet_control_configure.$stage_name.run.log" 2>&1
cat "$out_dir/tailnet_control_configure.$stage_name.run.log"
grep -Fxq 'configured=1' "$out_dir/tailnet_control_configure.$stage_name.run.log"
grep -Fxq 'providerKind=headscale' "$out_dir/tailnet_control_configure.$stage_name.run.log"
grep -Fxq "controlProbeEndpoint=$control_probe_endpoint" "$out_dir/tailnet_control_configure.$stage_name.run.log"
grep -Fxq 'controlPlane=headscale' "$out_dir/tailnet_control_configure.$stage_name.run.log"
grep -Fxq 'providerReady=1' "$out_dir/tailnet_control_configure.$stage_name.run.log"
grep -Fxq 'proxyListenersReady=1' "$out_dir/tailnet_control_configure.$stage_name.run.log"
grep -Fxq 'providerRuntimeActive=1' "$out_dir/tailnet_control_configure.$stage_name.run.log"

echo "[v3 tailnet control] run $stage_name control core listen"
"$core_bin" \
  --command listen \
  --provider-headscale 1 \
  --control-url https://headscale.example \
  --control-endpoint https://headscale.example/control \
  --relay-endpoint https://derp.sfo.example:443 \
  --control-probe-endpoint "$control_probe_endpoint" \
  --relay-probe-endpoint "$relay_probe_endpoint" \
  --namespace infra \
  --noise-public-key noise-pub-cli \
  --derp-region-code sfo \
  --derp-region-name SanFrancisco \
  --derp-hostname derp.sfo.example \
  --synthetic-ipv4 100.64.0.44 \
  --synthetic-ipv6 fd7a:115c:a1e0::44 \
  --local-peer-id tailnet-control-test \
  --exact-routing 1 \
  --provider-ready 1 \
  --proxy-ready 1 \
  --host 127.0.0.1 \
  --port 7101 \
  --transport tcp >"$out_dir/tailnet_control_listen.$stage_name.run.log" 2>&1
cat "$out_dir/tailnet_control_listen.$stage_name.run.log"
grep -Fxq 'publishedAddr=/ip4/100.64.0.44/tcp/7101' "$out_dir/tailnet_control_listen.$stage_name.run.log"
grep -Fxq 'listener.0.raw=/ip4/127.0.0.1/tcp/7101' "$out_dir/tailnet_control_listen.$stage_name.run.log"
grep -Fxq 'providerKind=headscale' "$out_dir/tailnet_control_listen.$stage_name.run.log"
grep -Fxq 'tailnetDerpMapSummary=headscale/infra@https://headscale.example#sfo/derp.sfo.example' "$out_dir/tailnet_control_listen.$stage_name.run.log"
grep -Fxq 'providerRuntimeActive=1' "$out_dir/tailnet_control_listen.$stage_name.run.log"

echo "[v3 tailnet control] run $stage_name control core dial-plan"
"$core_bin" \
  --command dial-plan \
  --provider-headscale 1 \
  --control-url https://headscale.example \
  --control-endpoint https://headscale.example/control \
  --relay-endpoint https://derp.sfo.example:443 \
  --control-probe-endpoint "$control_probe_endpoint" \
  --relay-probe-endpoint "$relay_probe_endpoint" \
  --namespace infra \
  --noise-public-key noise-pub-cli \
  --derp-region-code sfo \
  --derp-region-name SanFrancisco \
  --derp-hostname derp.sfo.example \
  --synthetic-ipv4 100.64.0.44 \
  --synthetic-ipv6 fd7a:115c:a1e0::44 \
  --local-peer-id tailnet-control-test \
  --exact-routing 1 \
  --provider-ready 1 \
  --proxy-ready 1 \
  --derp-healthy 1 \
  --bootstrap-listen-count 1 \
  --bootstrap-listen-raw-0 /ip4/127.0.0.1/tcp/7101 \
  --bootstrap-peer-count 1 \
  --bootstrap-peer-id-0 peer-direct \
  --bootstrap-peer-remote-tailnet-ip-0 100.64.0.88 \
  --bootstrap-peer-direct-reachable-0 1 \
  --bootstrap-peer-relay-allowed-0 1 \
  --bootstrap-peer-relay-only-0 0 \
  --bootstrap-peer-derp-region-code-0 sfo \
  --bootstrap-peer-signed-preface-0 test-preface \
  --bootstrap-peer-bound-addr-text-0 /ip4/127.0.0.1/tcp/7101 \
  --bootstrap-peer-last-path-0 direct \
  --bootstrap-peer-last-error-0 '' \
  --peer-id peer-direct \
  --addr /ip4/100.64.0.44/tcp/7101 >"$out_dir/tailnet_control_dial.$stage_name.run.log" 2>&1
cat "$out_dir/tailnet_control_dial.$stage_name.run.log"
grep -Fxq 'dial.pathKind=direct' "$out_dir/tailnet_control_dial.$stage_name.run.log"
grep -Fxq 'dial.routeText=/ip4/127.0.0.1/tcp/7101' "$out_dir/tailnet_control_dial.$stage_name.run.log"
grep -Fxq 'sessionCount=1' "$out_dir/tailnet_control_dial.$stage_name.run.log"
grep -Fxq 'relayHealthy=1' "$out_dir/tailnet_control_dial.$stage_name.run.log"
grep -Fxq 'startupStage=derp-ready' "$out_dir/tailnet_control_dial.$stage_name.run.log"
grep -Fxq 'tailnetDerpMapSummary=headscale/infra@https://headscale.example#sfo/derp.sfo.example' "$out_dir/tailnet_control_dial.$stage_name.run.log"
grep -Fxq 'peer.0.lastPath=direct' "$out_dir/tailnet_control_dial.$stage_name.run.log"
grep -Fxq 'providerRuntimeActive=1' "$out_dir/tailnet_control_dial.$stage_name.run.log"
grep -Fxq 'providerRuntimeCommands=3' "$out_dir/tailnet_control_dial.$stage_name.run.log"

echo "v3 tailnet_control_smoke ok"
