#!/usr/bin/env sh
:
set -eu
(set -o pipefail) 2>/dev/null && set -o pipefail

usage() {
  cat <<'EOF'
Usage:
  src/tooling/backend_seed_pure.sh [--seed:<path>] [--out:<path>]

Notes:
  - Internal transitional bootstrap only: rebuilds the backend stage2 driver
    using a prebuilt seed driver (no C backend).
  - Seed must be an executable backend driver binary (typically a previously published stage2).
  - The seed is copied into chengcache/ before running so it won't be clobbered.
  - Output stage2 is produced at: artifacts/backend_selfhost_self_obj/cheng.stage2

Examples:
  # Use a previously built stage2 as the seed and copy the new seed out:
  ${TOOLING_SELF_BIN:-artifacts/tooling_cmd/cheng_tooling} backend_seed_pure \
    --seed:artifacts/backend_selfhost_self_obj/cheng.stage2 \
    --out:artifacts/backend_seed/cheng.stage2

  # Use a custom seed binary:
  ${TOOLING_SELF_BIN:-artifacts/tooling_cmd/cheng_tooling} backend_seed_pure --seed:/path/to/cheng.stage2
EOF
}

seed=""
out=""
while [ "${1:-}" != "" ]; do
  case "$1" in
    --seed:*)
      seed="${1#--seed:}"
      ;;
    --out:*)
      out="${1#--out:}"
      ;;
    --help|-h)
      usage
      exit 0
      ;;
    *)
      echo "[Error] unknown arg: $1" 1>&2
      usage
      exit 2
      ;;
  esac
  shift || true
done

root="$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)"
cd "$root"

if [ "$seed" = "" ]; then
  seed="artifacts/backend_selfhost_self_obj/cheng.stage2"
fi

if [ ! -x "$seed" ]; then
  echo "[Error] missing seed driver (expected executable): $seed" 1>&2
  echo "  tip: pass --seed:<path> to a prebuilt stage2 driver" 1>&2
  exit 2
fi

seed_is_script_shim() {
  cand="$1"
  [ -f "$cand" ] || return 1
  first_line="$(sed -n '1p' "$cand" 2>/dev/null || true)"
  case "$first_line" in
    '#!'*) return 0 ;;
  esac
  return 1
}

if seed_is_script_shim "$seed"; then
  echo "[Error] backend_seed_pure requires a real executable seed binary, not a script shim: $seed" 1>&2
  exit 2
fi

mkdir -p chengcache
seed_base="$(basename "$seed")"
seed_key=""
if command -v shasum >/dev/null 2>&1; then
  seed_key="$(printf '%s' "$seed" | shasum -a 256 | awk '{print substr($1, 1, 12)}')"
elif command -v sha256sum >/dev/null 2>&1; then
  seed_key="$(printf '%s' "$seed" | sha256sum | awk '{print substr($1, 1, 12)}')"
else
  seed_key="$(printf '%s' "$seed" | cksum | awk '{print $1}')"
fi
seed_tmp_dir="$(mktemp -d "$root/chengcache/backend_seed_stage0.${seed_key}.XXXXXX")"
seed_copy="$seed_tmp_dir/$seed_base"
cp "$seed" "$seed_copy"
chmod +x "$seed_copy" 2>/dev/null || true
seed_stage0="$seed_copy"
seed_lineage_surface=""
seed_lineage_dir=""
seed_meta="${seed}.meta"
seed_stamp="${seed}.compile_stamp.txt"
seed_label=""
seed_lineage_name=""
if [ -f "$seed_meta" ]; then
  seed_label="$(sed -n 's/^label=//p' "$seed_meta" | head -n 1)"
fi
if [ "$seed_label" = "currentsrc.proof.bootstrap" ]; then
  echo "[Error] backend_seed_pure does not accept bootstrap proof wrapper as seed: $seed" 1>&2
  exit 2
fi
case "$seed_label" in
  stage1)
    seed_lineage_name="cheng.stage1"
    ;;
  stage2)
    seed_lineage_name="cheng.stage2"
    ;;
  stage2.proof)
    seed_lineage_name="cheng.stage2.proof"
    ;;
esac
if [ "$seed_lineage_name" != "" ]; then
  seed_lineage_dir="$seed_tmp_dir/lineage"
  mkdir -p "$seed_lineage_dir"
  seed_lineage_surface="$seed_lineage_dir/$seed_lineage_name"
  cp "$seed" "$seed_lineage_surface"
  chmod +x "$seed_lineage_surface" 2>/dev/null || true
  if [ -f "$seed_meta" ]; then
    cp "$seed_meta" "${seed_lineage_surface}.meta"
  fi
  if [ -f "$seed_stamp" ]; then
    cp "$seed_stamp" "${seed_lineage_surface}.compile_stamp.txt"
  fi
fi

bootstrap_script="$root/src/tooling/cheng_tooling_embedded_scripts/verify_backend_selfhost_bootstrap_self_obj.sh"
if [ ! -f "$bootstrap_script" ]; then
  echo "[Error] missing bootstrap verifier: $bootstrap_script" 1>&2
  exit 1
fi

echo "== backend.seed_pure.bootstrap =="
seed_bridge_input="src/backend/tooling/backend_driver_seed_bridge.cheng"
if [ ! -f "$seed_bridge_input" ]; then
  echo "[Error] missing internal transitional driver input: $seed_bridge_input" 1>&2
  exit 1
fi
if [ "$seed_lineage_dir" != "" ]; then
  SELF_OBJ_BOOTSTRAP_CURRENTSRC_LINEAGE_DIR="$seed_lineage_dir" \
  SELF_OBJ_BOOTSTRAP_DRIVER_INPUT="$seed_bridge_input" \
  SELF_OBJ_BOOTSTRAP_STAGE0="$seed_stage0" \
  SELF_OBJ_BOOTSTRAP_STAGE0_COMPAT=0 \
    sh "$bootstrap_script"
else
  SELF_OBJ_BOOTSTRAP_DRIVER_INPUT="$seed_bridge_input" \
  SELF_OBJ_BOOTSTRAP_STAGE0="$seed_stage0" \
  SELF_OBJ_BOOTSTRAP_STAGE0_COMPAT=0 \
    sh "$bootstrap_script"
fi

stage2="artifacts/backend_selfhost_self_obj/cheng.stage2"
if [ ! -x "$stage2" ]; then
  echo "[Error] missing stage2 output: $stage2" 1>&2
  exit 1
fi

stage2_sha=""
if command -v shasum >/dev/null 2>&1; then
  stage2_sha="$(shasum -a 256 "$stage2" | awk '{print $1}')"
elif command -v sha256sum >/dev/null 2>&1; then
  stage2_sha="$(sha256sum "$stage2" | awk '{print $1}')"
fi

if [ "$out" != "" ]; then
  out_dir="$(dirname "$out")"
  mkdir -p "$out_dir"
  cp "$stage2" "$out"
  if [ -f "$stage2.o" ]; then
    cp "$stage2.o" "$out.o"
  fi
fi

echo "backend_seed_pure ok: stage2=$stage2 sha256=$stage2_sha"
