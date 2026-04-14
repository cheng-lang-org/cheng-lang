#!/usr/bin/env sh
set -eu

root="$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)"
target="${CHAIN_NODE_TARGET:-aarch64-unknown-linux-gnu}"
artifact_kind="${CHAIN_NODE_LINUX_ARTIFACT:-obj}"

case "$artifact_kind" in
  obj|exe)
    ;;
  *)
    echo "v3 chain_node linux: unsupported artifact kind=$artifact_kind" >&2
    echo "v3 chain_node linux: use CHAIN_NODE_LINUX_ARTIFACT=obj or exe" >&2
    exit 1
    ;;
esac

case "$target" in
  aarch64-unknown-linux-gnu|arm64-unknown-linux-gnu)
    if [ "$artifact_kind" = "obj" ]; then
      CHAIN_NODE_OBJ_TARGET="$target" \
      CHAIN_NODE_OBJ_OUT_DIR="${CHAIN_NODE_OUT_DIR:-}" \
      CHAIN_NODE_OBJ_OUT="${CHAIN_NODE_OUT_BIN:-}" \
      CHENG_V3_CHAIN_NODE_COMPILER="${CHENG_V3_CHAIN_NODE_COMPILER:-}" \
      exec sh "$root/v3/tooling/build_chain_node_linux_obj_v3.sh" "$@"
    fi
    default_out_dir="$root/artifacts/v3_chain_node/$target"
    out_dir="${CHAIN_NODE_OUT_DIR:-$default_out_dir}"
    out_bin="${CHAIN_NODE_OUT_BIN:-$out_dir/chain_node}"
    run_log="$out_dir/chain_node.self-test.log"
    if ! CHENG_V3_LINUX_EXE_SOURCE="$root/v3/src/project/chain_node_main.cheng" \
         CHENG_V3_LINUX_EXE_TARGET="$target" \
         CHENG_V3_LINUX_EXE_OUT="$out_bin" \
         CHENG_V3_LINUX_EXE_COMPILER="${CHENG_V3_CHAIN_NODE_COMPILER:-$root/artifacts/v3_backend_driver/cheng}" \
         CHENG_V3_LINUX_LINKER="${CHENG_V3_LINUX_LINKER:-}" \
         sh "$root/v3/tooling/build_linux_nolibc_exe_v3.sh"; then
      exit 1
    fi
    host_os="$(uname -s 2>/dev/null || true)"
    host_arch="$(uname -m 2>/dev/null || true)"
    host_target=""
    case "$host_os/$host_arch" in
      Darwin/arm64)
        host_target="arm64-apple-darwin"
        ;;
      Linux/x86_64)
        host_target="x86_64-unknown-linux-gnu"
        ;;
      Linux/aarch64|Linux/arm64)
        host_target="aarch64-unknown-linux-gnu"
        ;;
    esac
    run_self_test="${CHAIN_NODE_RUN_SELF_TEST:-auto}"
    if [ "$run_self_test" = "auto" ]; then
      if [ "$target" = "$host_target" ]; then
        run_self_test="1"
      else
        run_self_test="0"
      fi
    fi
    if [ "$run_self_test" != "0" ] && ! "$out_bin" self-test >"$run_log" 2>&1; then
      echo "v3 chain_node linux: self-test failed log=$run_log" >&2
      tail -n 80 "$run_log" >&2 || true
      exit 1
    fi
    if [ "$run_self_test" != "0" ]; then
      cat "$run_log"
    else
      echo "v3 chain_node linux: build ok target=$target out=$out_bin"
    fi
    exit 0
    ;;
  x86_64-unknown-linux-gnu)
    echo "v3 chain_node linux: unsupported target=$target" >&2
    echo "v3 chain_node linux: current v3 seed linux path only guarantees generic aarch64 ELF relocatable object" >&2
    echo "v3 chain_node linux: x86_64 ordinary chain_node still has no verified Linux object or executable path" >&2
    exit 1
    ;;
  *)
    echo "v3 chain_node linux: unsupported target=$target" >&2
    echo "v3 chain_node linux: supported native/cross executable targets stay arm64-apple-darwin, wasm32-unknown-unknown, aarch64-linux-android, aarch64-linux-ohos, aarch64-unknown-linux-ohos" >&2
    exit 1
    ;;
esac
