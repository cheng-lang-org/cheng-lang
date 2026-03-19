#!/usr/bin/env sh
:
set -eu
(set -o pipefail) 2>/dev/null && set -o pipefail

root="$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)"
cd "$root"

host_tag="$(uname -s 2>/dev/null | tr '[:upper:]' '[:lower:]')_$(uname -m 2>/dev/null | tr '[:upper:]' '[:lower:]')"
case "$host_tag" in
  darwin_arm64) ;;
  *)
    echo "[verify_backend_linker_abi_core] host-only gate currently requires darwin host; host=$host_tag" 1>&2
    exit 2
    ;;
esac

target="arm64-apple-darwin"
driver="${BACKEND_DRIVER:-}"
fixture="$root/tests/cheng/backend/fixtures/hello_importc_puts.cheng"
out_dir="$root/artifacts/backend_linker_abi_core"
out="$out_dir/hello_importc_puts.$target.self"
log="$out_dir/build.host.log"
report="$out_dir/verify_backend_linker_abi_core.report.txt"
snapshot="$out_dir/verify_backend_linker_abi_core.snapshot.env"
tool="${TOOLING_SELF_BIN:-artifacts/tooling_cmd/cheng_tooling}"

if [ "$driver" = "" ] && [ -x "$root/artifacts/backend_selfhost_self_obj/probe_currentsrc_proof/cheng.stage2.proof" ]; then
  driver="$root/artifacts/backend_selfhost_self_obj/probe_currentsrc_proof/cheng.stage2.proof"
fi
if [ "$driver" = "" ] && [ -x "$root/artifacts/backend_driver/cheng" ]; then
  driver="$root/artifacts/backend_driver/cheng"
fi
if [ "$driver" = "" ]; then
  driver="$("$tool" backend_driver_path --path-only 2>/dev/null | tail -n 1 | tr -d '\r' || true)"
fi
if [ ! -x "$driver" ]; then
  echo "[verify_backend_linker_abi_core] missing canonical driver: $driver" 1>&2
  echo "  hint: artifacts/tooling_cmd/cheng_tooling build-backend-driver" 1>&2
  exit 1
fi
if [ ! -x "$tool" ]; then
  echo "[verify_backend_linker_abi_core] missing tooling binary: $tool" 1>&2
  exit 1
fi

rm -rf "$out_dir"
mkdir -p "$out_dir"

set +e
env -u BACKEND_DRIVER -u CHENG_BACKEND_DRIVER \
  ABI=v2_noptr \
  CACHE=0 \
  STAGE1_NO_POINTERS_NON_C_ABI=0 \
  STAGE1_NO_POINTERS_NON_C_ABI_INTERNAL=0 \
  BACKEND_OPT_LEVEL=2 \
  BACKEND_LINKER=self \
  BACKEND_TARGET="$target" \
  BACKEND_INPUT="$fixture" \
  BACKEND_OUTPUT="$out" \
  "$driver" >"$log" 2>&1
rc="$?"
set -e
if [ "$rc" -ne 0 ] || [ ! -s "$out" ]; then
  echo "[verify_backend_linker_abi_core] host self-link build failed rc=$rc" 1>&2
  tail -n 80 "$log" 1>&2 || true
  exit 1
fi

magic="$(od -An -tx1 -N4 "$out" 2>/dev/null | tr -d ' \n')"
fmt="unknown"
case "$magic" in
  cffaedfe*) fmt="mach-o64" ;;
  7f454c46*) fmt="elf64" ;;
  4d5a*) fmt="pe64" ;;
esac
if [ "$fmt" != "mach-o64" ]; then
  echo "[verify_backend_linker_abi_core] expected mach-o64 host output, got $fmt" 1>&2
  exit 1
fi
if ! command -v otool >/dev/null 2>&1; then
  echo "[verify_backend_linker_abi_core] missing otool" 1>&2
  exit 1
fi

otool -l "$out" >"$out_dir/host.meta.txt"
strings "$out" >"$out_dir/host.strings.txt" 2>/dev/null || true
main01="0"
puts01="0"
if command -v nm >/dev/null 2>&1 && nm "$out" >"$out_dir/host.nm.txt" 2>/dev/null; then
  if grep -Eq '[[:space:]]+[Tt][[:space:]]+_?main$' "$out_dir/host.nm.txt"; then
    main01="1"
  fi
  if grep -Eq '[[:space:]]+[Uu][[:space:]]+_?puts(@.*)?$' "$out_dir/host.nm.txt"; then
    puts01="1"
  fi
else
  if grep -Eq '(^|[^A-Za-z0-9_])_?main([^A-Za-z0-9_]|$)' "$out_dir/host.strings.txt"; then
    main01="1"
  fi
  if grep -Eq '(^|[^A-Za-z0-9_])_?puts([^A-Za-z0-9_]|$)' "$out_dir/host.strings.txt"; then
    puts01="1"
  fi
fi
dynamic01="0"
if grep -Eq 'LC_DYLD_INFO|LC_LOAD_DYLINKER' "$out_dir/host.meta.txt"; then
  dynamic01="1"
fi
if [ "$main01" != "1" ] || [ "$puts01" != "1" ] || [ "$dynamic01" != "1" ]; then
  echo "[verify_backend_linker_abi_core] manifest checks failed main=$main01 puts=$puts01 dynamic=$dynamic01" 1>&2
  exit 1
fi

{
  echo "platform.os=darwin"
  echo "binary.format=$fmt"
  echo "linker.mode=self"
  echo "entry.main=$main01"
  echo "import.puts=$puts01"
  echo "meta.dynamic=$dynamic01"
  echo "target=$target"
  echo "driver=$driver"
  echo "output=$out"
} >"$report"

{
  echo "backend_linker_abi_core_status=ok"
  echo "backend_linker_abi_core_target=$target"
  echo "backend_linker_abi_core_driver=$driver"
  echo "backend_linker_abi_core_report=$report"
} >"$snapshot"

echo "verify_backend_linker_abi_core ok"
