#!/usr/bin/env sh
set -eu

root="$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)"
cd "$root"

tool="${TOOLING_SELF_BIN:-artifacts/tooling_cmd/cheng_tooling.bin}"
probe_fixture="tests/cheng/backend/fixtures/return_add.cheng"
probe_dir="$root/artifacts/cheng_uir_native/tooling_probe"
mkdir -p "$probe_dir"
probe_summary="$probe_dir/summary.txt"
: >"$probe_summary"

tool_supports_detect_host() {
  cand="$1"
  [ -x "$cand" ] || return 1
  "$cand" detect_host_target >/dev/null 2>&1
}

tool_smoke_compile() {
  cand="$1"
  target="$2"
  log="$3"
  out="$4"
  rm -f "$log" "$out"
  "$cand" cheng --target:"$target" --in:"$probe_fixture" --out:"$out" >"$log" 2>&1
}

append_probe_summary() {
  cand="$1"
  rc="$2"
  log="$3"
  tail_text="$(tail -n 2 "$log" 2>/dev/null | tr '\n' ' ' | tr '\t' ' ')"
  printf '%s\trc=%s\t%s\n' "$(basename "$cand")" "$rc" "$tail_text" >>"$probe_summary"
}

resolve_working_tool() {
  explicit="$1"
  target="$2"
  if [ -n "$explicit" ] && [ -x "$explicit" ]; then
    log="$probe_dir/$(basename "$explicit").smoke.log"
    out="$probe_dir/$(basename "$explicit").smoke.bin"
    if tool_smoke_compile "$explicit" "$target" "$log" "$out"; then
      append_probe_summary "$explicit" 0 "$log"
      printf '%s\n' "$explicit"
      return 0
    else
      rc="$?"
      append_probe_summary "$explicit" "$rc" "$log"
    fi
  fi
  for cand in \
    "$root/artifacts/tooling_cmd/cheng_tooling.bin" \
    "$root/artifacts/tooling_cmd/cheng_tooling_new" \
    "$root/artifacts/tooling_cmd/cheng_tooling.new.release2" \
    "$root/artifacts/tooling_cmd/cheng_tooling.new.release" \
    "$root/artifacts/tooling_cmd/cheng_tooling.codex_bak_20260306_1850"
  do
    [ -x "$cand" ] || continue
    log="$probe_dir/$(basename "$cand").smoke.log"
    out="$probe_dir/$(basename "$cand").smoke.bin"
    if tool_smoke_compile "$cand" "$target" "$log" "$out"; then
      append_probe_summary "$cand" 0 "$log"
      printf '%s\n' "$cand"
      return 0
    else
      rc="$?"
      append_probe_summary "$cand" "$rc" "$log"
    fi
  done
  return 1
}

target=""
if tool_supports_detect_host "$tool"; then
  target="${BACKEND_TARGET:-$("$tool" detect_host_target 2>/dev/null || echo arm64-apple-darwin)}"
else
  target="${BACKEND_TARGET:-arm64-apple-darwin}"
fi

resolved_tool="$(resolve_working_tool "${TOOLING_SELF_BIN:-}" "$target" || true)"
if [ "$resolved_tool" = "" ]; then
  echo "build_cheng_uir_native_bridge: no working cheng_tooling candidate for $probe_fixture" >&2
  echo "probe logs: $probe_dir" >&2
  echo "probe summary: $probe_summary" >&2
  exit 1
fi
tool="$resolved_tool"

case "$(uname -s 2>/dev/null || echo unknown)" in
  Darwin) dylib_name="libcheng_uir_native.dylib" ;;
  Linux) dylib_name="libcheng_uir_native.so" ;;
  MINGW*|MSYS*|CYGWIN*|Windows_NT) dylib_name="cheng_uir_native.dll" ;;
  *) echo "unsupported host platform for native UIR bridge" >&2; exit 2 ;;
esac

out_dir="$root/artifacts/cheng_uir_native"
out_path="$out_dir/$dylib_name"
mkdir -p "$out_dir"

"$tool" release-compile \
  --target:"$target" \
  --emit:shared \
  --in:"$root/src/backend/uir/uir_semantic_capi_entry.cheng" \
  --out:"$out_path"

printf '%s\n' "$out_path"
