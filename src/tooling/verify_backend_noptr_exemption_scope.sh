#!/usr/bin/env sh
. "$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)/env_prefix_bridge.sh"
set -eu
(set -o pipefail) 2>/dev/null && set -o pipefail

root="$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)"
cd "$root"

out_dir="artifacts/backend_noptr_exemption_scope"
hits_file="$out_dir/noptr_exemption_scope.hits.txt"
bad_file="$out_dir/noptr_exemption_scope.bad.txt"
mkdir -p "$out_dir"
: >"$hits_file"
: >"$bad_file"

allowlist='
src/tooling/backend_driver_path.sh
src/tooling/backend_prod_closure.sh
src/tooling/build_backend_driver.sh
src/tooling/cheng_tooling.sh
src/tooling/verify_backend_abi_v2_noptr.sh
src/tooling/verify_backend_android.sh
src/tooling/verify_backend_closedloop.sh
src/tooling/verify_backend_determinism.sh
src/tooling/verify_backend_dod_soa.sh
src/tooling/verify_backend_dod_opt_regression.sh
src/tooling/verify_backend_driver_selfbuild_smoke.sh
src/tooling/verify_backend_egraph_cost.sh
src/tooling/verify_backend_float.sh
src/tooling/verify_backend_hotpatch.sh
src/tooling/verify_backend_hotpatch_meta.sh
src/tooling/verify_backend_linker_abi_core.sh
src/tooling/verify_backend_linkerless_dev.sh
src/tooling/verify_backend_mem_image_core.sh
src/tooling/verify_backend_metering_stream.sh
src/tooling/verify_backend_mir_borrow.sh
src/tooling/verify_backend_multi.sh
src/tooling/verify_backend_noalias_opt.sh
src/tooling/verify_backend_noptr_default_cli.sh
src/tooling/verify_backend_plugin_system.sh
src/tooling/verify_backend_release_c_o3_lto.sh
src/tooling/verify_backend_self_linker_elf.sh
src/tooling/verify_backend_selfhost_bootstrap_self_obj.sh
src/tooling/verify_backend_spawn_api_gate.sh
src/tooling/verify_backend_stage0_no_compat.sh
src/tooling/verify_backend_x86_64_darwin.sh
'

is_allowed_file() {
  cand="$1"
  while IFS= read -r allowed; do
    if [ "$allowed" = "" ]; then
      continue
    fi
    if [ "$cand" = "$allowed" ]; then
      return 0
    fi
  done <<EOF
$allowlist
EOF
  return 1
}

set +e
rg -n "STAGE1_NO_POINTERS_NON_C_ABI=[0]|STAGE1_NO_POINTERS_NON_C_ABI_INTERNAL=[0]" src/tooling/*.sh -g '!src/tooling/verify_backend_noptr_exemption_scope.sh' >"$hits_file"
rg_status="$?"
set -e
case "$rg_status" in
  0|1) ;;
  *)
    echo "[Error] failed to scan tooling scripts for no-pointer exemptions" 1>&2
    exit 1
    ;;
esac

if [ "$rg_status" -eq 0 ]; then
  while IFS= read -r hit; do
    if [ "$hit" = "" ]; then
      continue
    fi
    hit_file="${hit%%:*}"
    if ! is_allowed_file "$hit_file"; then
      printf '%s\n' "$hit" >>"$bad_file"
    fi
  done <"$hits_file"
fi

if [ -s "$bad_file" ]; then
  echo "[Error] no-pointer exemption found outside allowlist:" 1>&2
  sed 's/^/  - /' "$bad_file" 1>&2
  exit 1
fi

echo "verify_backend_noptr_exemption_scope ok"
