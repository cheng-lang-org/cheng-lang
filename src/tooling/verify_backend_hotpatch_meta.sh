#!/usr/bin/env sh
. "$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)/env_prefix_bridge.sh"
set -eu
(set -o pipefail) 2>/dev/null && set -o pipefail

root="$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)"
cd "$root"

if [ "${CLEAN_CHENG_LOCAL:-1}" = "1" ] && [ "${TOOLING_CLEANUP_DEPTH:-0}" = "0" ]; then
  export TOOLING_CLEANUP_DEPTH=1
  cleanup_backend_driver_on_exit() {
    status=$?
    set +e
    sh src/tooling/cleanup_cheng_local.sh
    exit "$status"
  }
  trap cleanup_backend_driver_on_exit EXIT
fi

hash_file() {
  file="$1"
  if command -v shasum >/dev/null 2>&1; then
    shasum -a 256 "$file" | awk '{print $1}'
    return
  fi
  if command -v sha256sum >/dev/null 2>&1; then
    sha256sum "$file" | awk '{print $1}'
    return
  fi
  cksum "$file" | awk '{print $1 ":" $2}'
}

hash_text() {
  text="$1"
  if command -v shasum >/dev/null 2>&1; then
    printf '%s' "$text" | shasum -a 256 | awk '{print $1}'
    return
  fi
  if command -v sha256sum >/dev/null 2>&1; then
    printf '%s' "$text" | sha256sum | awk '{print $1}'
    return
  fi
  printf '%s' "$text" | cksum | awk '{print $1}'
}

is_uint() {
  case "$1" in
    ''|*[!0-9]*)
      return 1
      ;;
  esac
  return 0
}

hex_to_dec() {
  raw="$1"
  raw="${raw#0x}"
  raw="${raw#0X}"
  echo $((16#$raw))
}

detect_format() {
  file_path="$1"
  if command -v otool >/dev/null 2>&1 && otool -h "$file_path" >/dev/null 2>&1; then
    echo "macho"
    return 0
  fi
  if command -v readelf >/dev/null 2>&1 && readelf -h "$file_path" >/dev/null 2>&1; then
    echo "elf"
    return 0
  fi
  echo "unknown"
  return 1
}

read_text_layout() {
  file_path="$1"
  fmt="$2"
  if [ "$fmt" = "macho" ]; then
    info="$(otool -l "$file_path" | awk '
      $1=="sectname" && $2=="__text" { in_text=1; next }
      in_text && $1=="addr" && addr=="" { addr=$2; next }
      in_text && $1=="size" && size=="" { size=$2; next }
      in_text && $1=="offset" && off=="" { off=$2; print addr, off, size; exit }
    ')"
    if [ "$info" = "" ]; then
      return 1
    fi
    addr_hex="$(echo "$info" | awk '{print $1}')"
    off_dec="$(echo "$info" | awk '{print $2}')"
    size_hex="$(echo "$info" | awk '{print $3}')"
    printf '%s %s %s\n' "$(hex_to_dec "$addr_hex")" "$off_dec" "$(hex_to_dec "$size_hex")"
    return 0
  fi
  if [ "$fmt" = "elf" ]; then
    info="$(readelf -W -S "$file_path" | awk '
      $0 ~ /] \.text[[:space:]]/ { print $4, $5, $6; exit }
    ')"
    if [ "$info" = "" ]; then
      return 1
    fi
    addr_hex="$(echo "$info" | awk '{print $1}')"
    off_hex="$(echo "$info" | awk '{print $2}')"
    size_hex="$(echo "$info" | awk '{print $3}')"
    printf '%s %s %s\n' "$(hex_to_dec "$addr_hex")" "$(hex_to_dec "$off_hex")" "$(hex_to_dec "$size_hex")"
    return 0
  fi
  return 1
}

read_window_fingerprint() {
  file_path="$1"
  file_off="$2"
  size_bytes="$3"
  if ! is_uint "$file_off" || ! is_uint "$size_bytes"; then
    return 1
  fi
  if [ "$size_bytes" -le 0 ]; then
    return 1
  fi
  tmp="$(mktemp "${TMPDIR:-/tmp}/cheng-hotpatch-meta-bytes.XXXXXX")"
  dd if="$file_path" of="$tmp" bs=1 skip="$file_off" count="$size_bytes" 2>/dev/null
  fp="$(hash_file "$tmp")"
  rm -f "$tmp"
  printf '%s\n' "$fp"
}

resolve_symbol_row() {
  map_file="$1"
  symbol="$2"
  row="$(awk -F'\t' -v sym="$symbol" '
    NR == 1 { next }
    {
      raw = $1
      norm = raw
      while (substr(norm, 1, 1) == "_") norm = substr(norm, 2)
      req = sym
      while (substr(req, 1, 1) == "_") req = substr(req, 2)
      if (raw == sym || norm == req) {
        print $0
        exit
      }
    }
  ' "$map_file")"
  printf '%s\n' "$row"
}

build_thunk_map() {
  exe="$1"
  abi="$2"
  out="$3"
  fmt="$(detect_format "$exe" || true)"
  if [ "$fmt" = "" ] || [ "$fmt" = "unknown" ]; then
    echo "[verify_backend_hotpatch_meta] unsupported binary format: $exe" >&2
    return 1
  fi
  text_layout="$(read_text_layout "$exe" "$fmt" || true)"
  if [ "$text_layout" = "" ]; then
    echo "[verify_backend_hotpatch_meta] failed to resolve .text layout: $exe" >&2
    return 1
  fi
  text_vaddr="$(echo "$text_layout" | awk '{print $1}')"
  text_fileoff="$(echo "$text_layout" | awk '{print $2}')"
  text_size="$(echo "$text_layout" | awk '{print $3}')"
  text_end=$((text_vaddr + text_size))

  raw_syms="$(mktemp "${TMPDIR:-/tmp}/cheng-hotpatch-syms.XXXXXX")"
  spans="$(mktemp "${TMPDIR:-/tmp}/cheng-hotpatch-spans.XXXXXX")"
  cleanup_map() {
    rm -f "$raw_syms" "$spans"
  }

  nm -n "$exe" 2>/dev/null | awk '
    /^[0-9A-Fa-f]+ [A-Za-z] / {
      if ($2 == "T" || $2 == "t") {
        print $1 "\t" $3
      }
    }
  ' >"$raw_syms"
  if [ ! -s "$raw_syms" ]; then
    echo "[verify_backend_hotpatch_meta] no text symbols found: $exe" >&2
    return 1
  fi

  awk -F'\t' '
    {
      if (NR > 1) {
        print prev_addr "\t" prev_sym "\t" $1
      }
      prev_addr = $1
      prev_sym = $2
      count = NR
    }
    END {
      if (count > 0) {
        print prev_addr "\t" prev_sym "\t-"
      }
    }
  ' "$raw_syms" >"$spans"

  {
    echo "symbol	thunk_id	target_slot_fileoff_or_memoff	code_pool_offset	size	format	fingerprint	abi"
  } >"$out"

  idx=0
  while IFS="$(printf '\t')" read -r start_hex sym next_hex; do
    start_dec="$(hex_to_dec "$start_hex")"
    if [ "$next_hex" = "-" ]; then
      next_dec="$text_end"
    else
      next_dec="$(hex_to_dec "$next_hex")"
    fi
    if [ "$next_dec" -le "$start_dec" ]; then
      continue
    fi
    rel_off=$((start_dec - text_vaddr))
    if [ "$rel_off" -lt 0 ]; then
      continue
    fi
    file_off=$((text_fileoff + rel_off))
    size_bytes=$((next_dec - start_dec))
    fp="$(read_window_fingerprint "$exe" "$file_off" "$size_bytes" || true)"
    if [ "$fp" = "" ]; then
      fp="-"
    fi
    printf '%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s\n' \
      "$sym" "$idx" "$file_off" "0" "$size_bytes" "$fmt" "$fp" "$abi" >>"$out"
    idx=$((idx + 1))
  done <"$spans"
  cleanup_map
}

compute_meta_hash() {
  thunk_id="$1"
  target_slot="$2"
  code_pool_offset="$3"
  layout_hash="$4"
  commit_epoch="$5"
  abi="$6"
  symbol="$7"
  canon="patch_meta.thunk_id=$thunk_id
patch_meta.target_slot_fileoff_or_memoff=$target_slot
patch_meta.code_pool_offset=$code_pool_offset
patch_meta.layout_hash=$layout_hash
patch_meta.commit_epoch=$commit_epoch
patch_meta.abi=$abi
patch_meta.symbol=$symbol"
  hash_text "$canon"
}

write_patch_meta() {
  out_file="$1"
  thunk_id="$2"
  target_slot="$3"
  code_pool_offset="$4"
  layout_hash="$5"
  commit_epoch="$6"
  abi="$7"
  symbol="$8"
  meta_hash="$(compute_meta_hash "$thunk_id" "$target_slot" "$code_pool_offset" "$layout_hash" "$commit_epoch" "$abi" "$symbol")"
  cat >"$out_file" <<EOF
patch_meta.schema.version=2
patch_meta.thunk_id=$thunk_id
patch_meta.target_slot_fileoff_or_memoff=$target_slot
patch_meta.code_pool_offset=$code_pool_offset
patch_meta.layout_hash=$layout_hash
patch_meta.commit_epoch=$commit_epoch
patch_meta.abi=$abi
patch_meta.symbol=$symbol
patch_meta.hash=$meta_hash
EOF
}

read_meta_value() {
  key="$1"
  file_path="$2"
  awk -F= -v key="$key" '
    $1 == key {
      sub(/^[^=]*=/, "", $0)
      print $0
      exit
    }
  ' "$file_path"
}

precheck_meta() {
  meta_file="$1"
  expected_abi="$2"
  expected_symbol="$3"
  expected_layout_hash="$4"

  if [ ! -f "$meta_file" ]; then
    echo "[verify_backend_hotpatch_meta] missing metadata file: $meta_file" >&2
    return 1
  fi
  schema="$(read_meta_value patch_meta.schema.version "$meta_file")"
  thunk_id="$(read_meta_value patch_meta.thunk_id "$meta_file")"
  target_slot="$(read_meta_value patch_meta.target_slot_fileoff_or_memoff "$meta_file")"
  code_pool_offset="$(read_meta_value patch_meta.code_pool_offset "$meta_file")"
  layout_hash="$(read_meta_value patch_meta.layout_hash "$meta_file")"
  commit_epoch="$(read_meta_value patch_meta.commit_epoch "$meta_file")"
  abi="$(read_meta_value patch_meta.abi "$meta_file")"
  symbol="$(read_meta_value patch_meta.symbol "$meta_file")"
  meta_hash="$(read_meta_value patch_meta.hash "$meta_file")"

  if [ "$schema" != "2" ]; then
    echo "[verify_backend_hotpatch_meta] invalid schema version: $schema" >&2
    return 1
  fi
  for n in "$thunk_id" "$target_slot" "$code_pool_offset" "$commit_epoch"; do
    if ! is_uint "$n"; then
      echo "[verify_backend_hotpatch_meta] non-numeric field in patch meta" >&2
      return 1
    fi
  done
  if [ "$abi" != "$expected_abi" ]; then
    echo "[verify_backend_hotpatch_meta] abi mismatch: expected=$expected_abi got=$abi" >&2
    return 1
  fi
  if [ "$symbol" != "$expected_symbol" ]; then
    echo "[verify_backend_hotpatch_meta] symbol mismatch: expected=$expected_symbol got=$symbol" >&2
    return 1
  fi
  if [ "$layout_hash" != "$expected_layout_hash" ]; then
    echo "[verify_backend_hotpatch_meta] layout hash mismatch: expected=$expected_layout_hash got=$layout_hash" >&2
    return 1
  fi
  expected_hash="$(compute_meta_hash "$thunk_id" "$target_slot" "$code_pool_offset" "$layout_hash" "$commit_epoch" "$abi" "$symbol")"
  if [ "$expected_hash" != "$meta_hash" ]; then
    echo "[verify_backend_hotpatch_meta] patch meta hash mismatch" >&2
    return 1
  fi
  return 0
}

host_target="$(sh src/tooling/detect_host_target.sh 2>/dev/null || true)"
if [ "$host_target" = "" ]; then
  host_target="unknown"
fi

is_supported_target() {
  target="$1"
  case "$target" in
    *apple*darwin*|*darwin*)
      case "$target" in
        *arm64*|*aarch64*|*x86_64*|*amd64*) return 0 ;;
      esac
      return 1
      ;;
    *linux*|*android*)
      case "$target" in
        *arm64*|*aarch64*|*riscv64*) return 0 ;;
      esac
      return 1
      ;;
  esac
  return 1
}

select_default_target() {
  if [ "${BACKEND_HOTPATCH_TARGET:-}" != "" ]; then
    echo "${BACKEND_HOTPATCH_TARGET}"
    return
  fi
  if [ "$host_target" != "unknown" ]; then
    echo "$host_target"
    return
  fi
  echo "arm64-apple-darwin"
}

target="${BACKEND_TARGET:-}"
target_explicit="0"
if [ "$target" = "" ]; then
  target="$(select_default_target)"
else
  target_explicit="1"
fi

out_dir="artifacts/backend_hotpatch_meta"
mkdir -p "$out_dir"
safe_target="$(printf '%s' "$target" | tr -c 'A-Za-z0-9._-' '_' | tr -s '_')"
report="$out_dir/backend_hotpatch_meta.$safe_target.report.txt"

if ! is_supported_target "$target"; then
  if [ "$target_explicit" = "1" ]; then
    echo "[verify_backend_hotpatch_meta] unsupported BACKEND_TARGET: $target" >&2
    exit 1
  fi
  {
    echo "verify_backend_hotpatch_meta report"
    echo "status=skip"
    echo "reason=unsupported_target"
    echo "target=$target"
    echo "host_target=$host_target"
  } >"$report"
  echo "verify_backend_hotpatch_meta skip: unsupported target ($target)"
  exit 0
fi

if ! command -v nm >/dev/null 2>&1; then
  echo "[verify_backend_hotpatch_meta] nm is required" >&2
  exit 2
fi

fixture_v1="tests/cheng/backend/fixtures/hotpatch_slot_v1.cheng"
fixture_v2="tests/cheng/backend/fixtures/hotpatch_slot_v2.cheng"
for f in "$fixture_v1" "$fixture_v2"; do
  if [ ! -f "$f" ]; then
    echo "[verify_backend_hotpatch_meta] missing fixture: $f" >&2
    exit 1
  fi
done

compile_fixture() {
  fixture="$1"
  out_exe="$2"
  out_log="$3"
  preferred_linker="${BACKEND_HOTPATCH_GATE_LINKER:-self}"
  no_runtime_c="1"
  if [ "$preferred_linker" = "system" ]; then
    no_runtime_c="0"
  fi

  set +e
  env \
    MM="${MM:-orc}" \
    ABI=v2_noptr \
    BACKEND_TARGET="$target" \
    BACKEND_LINKER="$preferred_linker" \
    BACKEND_LINKER_SYMTAB=all \
    BACKEND_RUNTIME=off \
    BACKEND_NO_RUNTIME_C="$no_runtime_c" \
    BACKEND_CODESIGN=0 \
    BACKEND_MULTI=0 \
    BACKEND_MULTI_FORCE=0 \
    BACKEND_INCREMENTAL=0 \
    BACKEND_KEEP_EXE_OBJ=0 \
    BACKEND_OPT=0 \
    BACKEND_OPT2=0 \
    BACKEND_OPT_LEVEL=0 \
    STAGE1_NO_POINTERS_NON_C_ABI=0 \
    STAGE1_NO_POINTERS_NON_C_ABI_INTERNAL=0 \
    STAGE1_SKIP_SEM="${STAGE1_SKIP_SEM:-0}" \
    STAGE1_SKIP_OWNERSHIP="${STAGE1_SKIP_OWNERSHIP:-1}" \
    sh src/tooling/chengc.sh "$fixture" --frontend:stage1 --emit:exe --out:"$out_exe" >"$out_log" 2>&1
  status="$?"
  if [ "$status" -ne 0 ] && [ "$preferred_linker" = "self" ]; then
    env \
      MM="${MM:-orc}" \
      ABI=v2_noptr \
      BACKEND_TARGET="$target" \
      BACKEND_LINKER=system \
      BACKEND_LINKER_SYMTAB=all \
      BACKEND_RUNTIME=off \
      BACKEND_NO_RUNTIME_C=0 \
      BACKEND_CODESIGN=0 \
      BACKEND_MULTI=0 \
      BACKEND_MULTI_FORCE=0 \
      BACKEND_INCREMENTAL=0 \
      BACKEND_KEEP_EXE_OBJ=0 \
      BACKEND_OPT=0 \
      BACKEND_OPT2=0 \
      BACKEND_OPT_LEVEL=0 \
      STAGE1_NO_POINTERS_NON_C_ABI=0 \
      STAGE1_NO_POINTERS_NON_C_ABI_INTERNAL=0 \
      STAGE1_SKIP_SEM="${STAGE1_SKIP_SEM:-0}" \
      STAGE1_SKIP_OWNERSHIP="${STAGE1_SKIP_OWNERSHIP:-1}" \
      sh src/tooling/chengc.sh "$fixture" --frontend:stage1 --emit:exe --out:"$out_exe" >>"$out_log" 2>&1
    status="$?"
  fi
  set -e
  if [ "$status" -ne 0 ]; then
    echo "[verify_backend_hotpatch_meta] build failed: fixture=$fixture status=$status log=$out_log" >&2
    sed -n '1,200p' "$out_log" >&2 || true
    exit 1
  fi
  if [ ! -s "$out_exe" ]; then
    echo "[verify_backend_hotpatch_meta] missing output executable: $out_exe" >&2
    exit 1
  fi
}

symbol="${BACKEND_HOTPATCH_SYMBOL:-hotpatch_target}"
abi="${ABI:-v2_noptr}"
abi_expected="$abi"
base_exe="$out_dir/hotpatch_meta.base.$safe_target"
patch_exe="$out_dir/hotpatch_meta.patch.$safe_target"
build_log_v1="$out_dir/hotpatch_meta.v1.build.log"
build_log_v2="$out_dir/hotpatch_meta.v2.build.log"
thunk_map_base="$out_dir/hotpatch_thunk_map.base.$safe_target.tsv"
thunk_map_patch="$out_dir/hotpatch_thunk_map.patch.$safe_target.tsv"
meta_file="$out_dir/hotpatch_meta.$safe_target.env"
precheck_ok_log="$out_dir/hotpatch_meta.precheck.ok.log"
missing_log="$out_dir/hotpatch_meta.precheck.missing.log"
bad_layout_log="$out_dir/hotpatch_meta.precheck.bad_layout.log"
bad_abi_log="$out_dir/hotpatch_meta.precheck.bad_abi.log"

rm -f \
  "$base_exe" "$patch_exe" \
  "$base_exe.o" "$patch_exe.o" \
  "$build_log_v1" "$build_log_v2" \
  "$thunk_map_base" "$thunk_map_patch" \
  "$meta_file" "$precheck_ok_log" "$missing_log" "$bad_layout_log" "$bad_abi_log" \
  "$out_dir/hotpatch_meta.bad_layout.$safe_target.env" \
  "$out_dir/hotpatch_meta.bad_abi.$safe_target.env" \
  "$report"
rm -rf \
  "$base_exe.objs" "$patch_exe.objs" \
  "$base_exe.objs.lock" "$patch_exe.objs.lock"

compile_fixture "$fixture_v1" "$base_exe" "$build_log_v1"
compile_fixture "$fixture_v2" "$patch_exe" "$build_log_v2"
build_thunk_map "$base_exe" "$abi_expected" "$thunk_map_base"
build_thunk_map "$patch_exe" "$abi_expected" "$thunk_map_patch"

base_row="$(resolve_symbol_row "$thunk_map_base" "$symbol")"
patch_row="$(resolve_symbol_row "$thunk_map_patch" "$symbol")"
if [ "$base_row" = "" ] || [ "$patch_row" = "" ]; then
  echo "[verify_backend_hotpatch_meta] symbol not found in thunk map: $symbol" >&2
  exit 1
fi

base_symbol="$(echo "$base_row" | awk -F'\t' '{print $1}')"
base_thunk_id="$(echo "$base_row" | awk -F'\t' '{print $2}')"
base_target_slot="$(echo "$base_row" | awk -F'\t' '{print $3}')"
base_code_pool_offset="$(echo "$base_row" | awk -F'\t' '{print $4}')"
base_size="$(echo "$base_row" | awk -F'\t' '{print $5}')"
base_fmt="$(echo "$base_row" | awk -F'\t' '{print $6}')"
base_fingerprint="$(echo "$base_row" | awk -F'\t' '{print $7}')"

patch_thunk_id="$(echo "$patch_row" | awk -F'\t' '{print $2}')"
patch_target_slot="$(echo "$patch_row" | awk -F'\t' '{print $3}')"
patch_size="$(echo "$patch_row" | awk -F'\t' '{print $5}')"
patch_fmt="$(echo "$patch_row" | awk -F'\t' '{print $6}')"
patch_fingerprint="$(echo "$patch_row" | awk -F'\t' '{print $7}')"

if [ "$base_thunk_id" != "$patch_thunk_id" ]; then
  echo "[verify_backend_hotpatch_meta] thunk id drift detected: base=$base_thunk_id patch=$patch_thunk_id" >&2
  exit 1
fi
if [ "$base_fmt" != "$patch_fmt" ]; then
  echo "[verify_backend_hotpatch_meta] format drift detected: base=$base_fmt patch=$patch_fmt" >&2
  exit 1
fi
if [ "$base_target_slot" != "$patch_target_slot" ]; then
  echo "[verify_backend_hotpatch_meta] target slot drift detected: base=$base_target_slot patch=$patch_target_slot" >&2
  exit 1
fi

base_layout_hash="$(hash_file "$fixture_v1")"
patch_layout_hash="$(hash_file "$fixture_v2")"
write_patch_meta "$meta_file" "$base_thunk_id" "$base_target_slot" "$base_code_pool_offset" "$base_layout_hash" "0" "$abi_expected" "$base_symbol"

if ! precheck_meta "$meta_file" "$abi_expected" "$base_symbol" "$base_layout_hash" >"$precheck_ok_log" 2>&1; then
  echo "[verify_backend_hotpatch_meta] expected precheck to pass, but failed: $precheck_ok_log" >&2
  sed -n '1,120p' "$precheck_ok_log" >&2 || true
  exit 1
fi

set +e
precheck_meta "$out_dir/hotpatch_meta.missing.$safe_target.env" "$abi_expected" "$base_symbol" "$base_layout_hash" >"$missing_log" 2>&1
missing_status="$?"
set -e
if [ "$missing_status" -eq 0 ]; then
  echo "[verify_backend_hotpatch_meta] expected missing metadata precheck to fail" >&2
  exit 1
fi

bad_layout_meta="$out_dir/hotpatch_meta.bad_layout.$safe_target.env"
write_patch_meta "$bad_layout_meta" "$base_thunk_id" "$base_target_slot" "$base_code_pool_offset" "$patch_layout_hash" "0" "$abi_expected" "$base_symbol"
set +e
precheck_meta "$bad_layout_meta" "$abi_expected" "$base_symbol" "$base_layout_hash" >"$bad_layout_log" 2>&1
bad_layout_status="$?"
set -e
if [ "$bad_layout_status" -eq 0 ]; then
  echo "[verify_backend_hotpatch_meta] expected layout mismatch precheck to fail" >&2
  exit 1
fi

bad_abi_meta="$out_dir/hotpatch_meta.bad_abi.$safe_target.env"
write_patch_meta "$bad_abi_meta" "$base_thunk_id" "$base_target_slot" "$base_code_pool_offset" "$base_layout_hash" "0" "v1_legacy" "$base_symbol"
set +e
precheck_meta "$bad_abi_meta" "$abi_expected" "$base_symbol" "$base_layout_hash" >"$bad_abi_log" 2>&1
bad_abi_status="$?"
set -e
if [ "$bad_abi_status" -eq 0 ]; then
  echo "[verify_backend_hotpatch_meta] expected abi mismatch precheck to fail" >&2
  exit 1
fi

{
  echo "verify_backend_hotpatch_meta report"
  echo "status=ok"
  echo "target=$target"
  echo "host_target=$host_target"
  echo "symbol=$base_symbol"
  echo "abi=$abi_expected"
  echo "thunk_id=$base_thunk_id"
  echo "target_slot_fileoff_or_memoff=$base_target_slot"
  echo "code_pool_offset=$base_code_pool_offset"
  echo "base_layout_hash=$base_layout_hash"
  echo "patch_layout_hash=$patch_layout_hash"
  echo "base_size=$base_size"
  echo "patch_size=$patch_size"
  echo "base_fingerprint=$base_fingerprint"
  echo "patch_fingerprint=$patch_fingerprint"
  echo "base_exe=$base_exe"
  echo "patch_exe=$patch_exe"
  echo "thunk_map_base=$thunk_map_base"
  echo "thunk_map_patch=$thunk_map_patch"
  echo "meta_file=$meta_file"
  echo "precheck_ok_log=$precheck_ok_log"
  echo "missing_meta_status=$missing_status"
  echo "missing_meta_log=$missing_log"
  echo "bad_layout_status=$bad_layout_status"
  echo "bad_layout_log=$bad_layout_log"
  echo "bad_abi_status=$bad_abi_status"
  echo "bad_abi_log=$bad_abi_log"
  echo "build_log_v1=$build_log_v1"
  echo "build_log_v2=$build_log_v2"
} >"$report"

echo "verify_backend_hotpatch_meta ok"
