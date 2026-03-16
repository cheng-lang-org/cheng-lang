#!/usr/bin/env sh
:
set -eu
(set -o pipefail) 2>/dev/null && set -o pipefail

root="$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)"
cd "$root"

fail() {
  echo "[verify_backend_riscv64_quic_boundary] $1" >&2
  exit 1
}

if ! command -v rg >/dev/null 2>&1; then
  fail "rg is required"
fi

riscv_crt="src/tooling/rtos/riscv64/crt0.S"
riscv_linker="src/tooling/rtos/riscv64/linker.ld"
riscv_toolchain="src/tooling/toolchains/rtos_riscv64_elf.env"
riscv_linker_src="src/backend/obj/elf_linker_riscv64.cheng"

[ -f "$riscv_crt" ] || fail "missing riscv crt0: $riscv_crt"
[ -f "$riscv_linker" ] || fail "missing riscv linker script: $riscv_linker"
[ -f "$riscv_toolchain" ] || fail "missing riscv toolchain env: $riscv_toolchain"
[ -f "$riscv_linker_src" ] || fail "missing riscv linker source: $riscv_linker_src"

out_dir="artifacts/backend_riscv64_quic_boundary"
mkdir -p "$out_dir"
hits_file="$out_dir/riscv64_quic_boundary.disallowed_hits.txt"
report="$out_dir/riscv64_quic_boundary.report.txt"

: >"$hits_file"
rg -n --no-messages '(openssl_quic|msquictransport|msquic|openssl|@importc\(|socket\(|sendto\(|recvfrom\()' \
  "$riscv_crt" "$riscv_linker" "$riscv_toolchain" "$riscv_linker_src" >"$hits_file" || true

if [ -s "$hits_file" ]; then
  sed -n '1,120p' "$hits_file" >&2 || true
  fail "riscv64/rtos path contains forbidden C-lib or socket surface markers"
fi

core_quic_dir="src/quic"
core_quic_present="0"
if [ -d "$core_quic_dir" ]; then
  core_quic_present="1"
fi

{
  echo "verify_backend_riscv64_quic_boundary report"
  echo "status=ok"
  echo "riscv_crt=$riscv_crt"
  echo "riscv_linker=$riscv_linker"
  echo "riscv_toolchain=$riscv_toolchain"
  echo "riscv_linker_src=$riscv_linker_src"
  echo "core_quic_present=$core_quic_present"
  echo "forbidden_hits=$hits_file"
} >"$report"

echo "verify_backend_riscv64_quic_boundary ok"
