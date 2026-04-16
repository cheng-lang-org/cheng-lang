#!/usr/bin/env sh
set -eu

root="$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)"
react_repo="${R2C_REACT_REPO:-/Users/lbcheng/UniMaker/React.js}"
status_out="/tmp/r2c-status-fresh-clean-gate.json"
bundle_out="/tmp/r2c-native-gui-fresh-clean-gate-home"
bundle_report="$bundle_out/native_gui_bundle_report_v1.json"
bundle_json="$bundle_out/native_gui_bundle_v1.json"
hostrun_dir="$root/artifacts/v3_hostrun"

while [ "$#" -gt 0 ]; do
  case "$1" in
    --repo)
      shift
      if [ "$#" -le 0 ]; then
        echo "r2c-react-v3-fresh-clean-gate: missing value for --repo" >&2
        exit 2
      fi
      react_repo="$1"
      ;;
    --repo=*)
      react_repo="${1#--repo=}"
      ;;
    *)
      echo "r2c-react-v3-fresh-clean-gate: unknown arg: $1" >&2
      exit 2
      ;;
  esac
  shift
done

if [ ! -d "$react_repo" ]; then
  echo "r2c-react-v3-fresh-clean-gate: missing repo: $react_repo" >&2
  exit 1
fi

rm -f "$status_out"
rm -rf "$bundle_out"
if [ -d "$hostrun_dir" ]; then
  find "$hostrun_dir" -maxdepth 1 \
    \( -name 'compiler_runtime_smoke*' \
    -o -name 'host_process_pipe_spawn_bridge_smoke*' \
    -o -name 'host_process_pipe_spawn_api_smoke*' \
    -o -name 'host_process_exec_file_capture_smoke*' \
    -o -name 'host_process_exec_file_direct_seq_smoke*' \) \
    -exec rm -rf {} +
fi

sh "$root/v3/tooling/bootstrap_bridge_v3.sh" >/dev/null
sh "$root/v3/tooling/build_backend_driver_v3.sh" >/dev/null

sh "$root/v3/tooling/cheng_v3.sh" run-host-smokes \
  compiler_runtime_smoke \
  host_process_pipe_spawn_bridge_smoke \
  host_process_pipe_spawn_api_smoke \
  host_process_exec_file_capture_smoke \
  host_process_exec_file_direct_seq_smoke

audit_text="$(sh "$root/v3/tooling/cheng_v3.sh" host-bridge-audit)"
printf '%s\n' "$audit_text"
printf '%s' "$audit_text" | grep -q 'pure_cheng_zero_c_ready=1'
printf '%s' "$audit_text" | grep -q 'remaining_non_pure_cheng_boundary=none'

"$root/artifacts/v3_bootstrap/cheng.stage3" r2c-react-v3 status --out "$status_out" >/dev/null
grep -q '"native_gui_runtime_helper": ""' "$status_out"
grep -q '"legacy_wrapper_required": false' "$status_out"
grep -q '"active_node_helper_count": 12' "$status_out"
grep -q '"retired_node_helper_count": 1' "$status_out"
grep -q '"retired_node_helpers": \["native_gui_runtime_helper"\]' "$status_out"

"$root/artifacts/v3_bootstrap/cheng.stage3" r2c-react-v3 native-gui-bundle \
  --repo "$react_repo" \
  --out-dir "$bundle_out" \
  --route-state home_default >/dev/null

grep -q '"ok": true' "$bundle_report"
grep -q '"native_gui_host_ready": true' "$bundle_report"
grep -q '"native_gui_renderer_ready": true' "$bundle_report"
grep -q '"runtime_mode": "cheng_compiled_json_v1"' "$bundle_json"

printf 'r2c-react-v3 fresh clean gate: ok\n'
printf 'status_report=%s\n' "$status_out"
printf 'bundle_report=%s\n' "$bundle_report"
