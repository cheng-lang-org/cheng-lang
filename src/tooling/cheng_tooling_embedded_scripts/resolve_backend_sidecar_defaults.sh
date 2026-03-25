#!/usr/bin/env sh
:
set -eu
(set -o pipefail) 2>/dev/null && set -o pipefail

usage() {
  cat <<'USAGE'
Usage:
  src/tooling/cheng_tooling_embedded_scripts/resolve_backend_sidecar_defaults.sh --field:mode|bundle|compiler|driver|real_driver|child_mode|outer_companion [--target:<triple>] [--root:<path>]

Resolves the default backend UIR sidecar contract from the fresh Cheng sidecar gate.
Pure Cheng sidecar is strict-fresh only: missing or stale fresh snapshot is an error.
USAGE
}

detect_host_target() {
  host_os="$(uname -s 2>/dev/null || echo unknown)"
  host_arch="$(uname -m 2>/dev/null || echo unknown)"
  case "$host_os/$host_arch" in
    Darwin/arm64)
      printf '%s\n' "arm64-apple-darwin"
      return 0
      ;;
    Linux/aarch64|Linux/arm64)
      printf '%s\n' "aarch64-unknown-linux-gnu"
      return 0
      ;;
  esac
  printf '%s\n' ""
  return 0
}

strict_stage0_lineage_dir() {
  printf '%s\n' "$root/artifacts/backend_selfhost_self_obj/probe_currentsrc_proof"
}

strict_stage0_meta_field() {
  meta_path="$1"
  key="$2"
  if [ ! -f "$meta_path" ]; then
    printf '\n'
    return 0
  fi
  awk -F= -v key="$key" '$1 == key { print substr($0, index($0, "=") + 1); exit }' "$meta_path"
}

strict_stage0_published_surface() {
  driver_path="$1"
  case "$driver_path" in
    "$(strict_stage0_lineage_dir)"/cheng.stage1|\
    "$(strict_stage0_lineage_dir)"/cheng.stage2|\
    "$(strict_stage0_lineage_dir)"/cheng.stage2.proof)
      return 0
      ;;
  esac
  return 1
}

strict_stage0_bootstrap_surface() {
  driver_path="$1"
  case "$driver_path" in
    "$(strict_stage0_lineage_dir)"/cheng_stage0_currentsrc.proof)
      return 0
      ;;
  esac
  return 1
}

strict_stage0_expected_label() {
  driver_path="$1"
  case "$driver_path" in
    */cheng_stage0_currentsrc.proof)
      printf '%s\n' "currentsrc.proof.bootstrap"
      return 0
      ;;
    */cheng.stage1)
      printf '%s\n' "stage1"
      return 0
      ;;
    */cheng.stage2)
      printf '%s\n' "stage2"
      return 0
      ;;
    */cheng.stage2.proof)
      printf '%s\n' "stage2.proof"
      return 0
      ;;
  esac
  printf '\n'
}

strict_stage0_direct_export_driver_path() {
  driver_path="$1"
  case "$driver_path" in
    *.proof)
      outer_driver="$(strict_stage0_meta_field "${driver_path}.meta" "outer_driver")"
      if [ "$outer_driver" != "" ]; then
        printf '%s\n' "$outer_driver"
        return 0
      fi
      ;;
  esac
  printf '%s\n' "$driver_path"
}

strict_stage0_direct_export_surface_ok() {
  driver_path="$1"
  export_driver="$(strict_stage0_direct_export_driver_path "$driver_path")"
  [ "$export_driver" != "" ] || return 1
  [ -x "$export_driver" ] || return 1
  command -v nm >/dev/null 2>&1 || return 1
  surface_log="$(mktemp "${TMPDIR:-/tmp}/strict_stage0_nm.XXXXXX")"
  set +e
  (nm -gU "$export_driver" 2>/dev/null || nm "$export_driver" 2>/dev/null) >"$surface_log"
  nm_status="$?"
  set -e
  if [ "$nm_status" -ne 0 ]; then
    rm -f "$surface_log"
    return 1
  fi
  if ! grep -q 'driver_export_build_emit_obj_from_file_stage1_target_impl' "$surface_log"; then
    rm -f "$surface_log"
    return 1
  fi
  if ! grep -q 'driver_export_prefer_sidecar_builds' "$surface_log"; then
    rm -f "$surface_log"
    return 1
  fi
  rm -f "$surface_log"
  return 0
}

strict_stage0_meta_ok() {
  driver_path="$1"
  if ! strict_stage0_published_surface "$driver_path" && ! strict_stage0_bootstrap_surface "$driver_path"; then
    return 1
  fi
  meta_path="${driver_path}.meta"
  expected_label="$(strict_stage0_expected_label "$driver_path")"
  [ "$expected_label" != "" ] || return 1
  [ -f "$meta_path" ] || return 1
  [ "$(strict_stage0_meta_field "$meta_path" "meta_contract_version")" = "2" ] || return 1
  [ "$(strict_stage0_meta_field "$meta_path" "label")" = "$expected_label" ] || return 1
  meta_outer_driver="$(strict_stage0_meta_field "$meta_path" "outer_driver")"
  case "$expected_label" in
    stage2.proof)
      [ "$meta_outer_driver" != "" ] || return 1
      [ -x "$meta_outer_driver" ] || return 1
      [ "$(strict_stage0_meta_field "$meta_path" "sidecar_mode")" = "cheng" ] || return 1
      [ "$(strict_stage0_meta_field "$meta_path" "sidecar_bundle")" != "" ] || return 1
      [ "$(strict_stage0_meta_field "$meta_path" "sidecar_compiler")" != "" ] || return 1
      ;;
    *)
      [ "$meta_outer_driver" = "$driver_path" ] || return 1
      ;;
  esac
  [ "$(strict_stage0_meta_field "$meta_path" "driver_input")" = "src/backend/tooling/backend_driver_proof.cheng" ] || return 1
  [ "$(strict_stage0_meta_field "$meta_path" "stage1_ownership_fixed_0_effective")" = "0" ] || return 1
  [ "$(strict_stage0_meta_field "$meta_path" "stage1_ownership_fixed_0_default")" = "0" ] || return 1
  [ "$(strict_stage0_meta_field "$meta_path" "generic_mode")" = "dict" ] || return 1
  [ "$(strict_stage0_meta_field "$meta_path" "generic_lowering")" = "mir_dict" ] || return 1
  strict_stage0_direct_export_surface_ok "$driver_path" || return 1
  return 0
}

strict_stage0_sources_newer_than() {
  out="$1"
  [ -e "$out" ] || return 0
  search_dirs=""
  for d in src/backend src/stage1 src/std src/core src/system; do
    if [ -d "$d" ]; then
      search_dirs="$search_dirs $d"
    fi
  done
  if [ "$search_dirs" = "" ]; then
    return 1
  fi
  # shellcheck disable=SC2086
  find $search_dirs -type f \( -name '*.cheng' -o -name '*.c' -o -name '*.h' \) \
    -newer "$out" -print -quit | grep -q .
}

strict_stage0_current_enough() {
  driver_path="$1"
  strict_stage0_published_surface "$driver_path" || return 0
  [ -x "$driver_path" ] || return 1
  if strict_stage0_sources_newer_than "$driver_path"; then
    return 1
  fi
  return 0
}

field=""
root="$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)"
target=""
while [ "${1:-}" != "" ]; do
  case "$1" in
    --help|-h)
      usage
      exit 0
      ;;
    --field:*)
      field="${1#--field:}"
      ;;
    --root:*)
      root="${1#--root:}"
      ;;
    --target:*)
      target="${1#--target:}"
      ;;
    *)
      echo "[resolve_backend_sidecar_defaults] unknown arg: $1" 1>&2
      usage
      exit 2
      ;;
  esac
  shift || true
done

case "$field" in
  mode|bundle|compiler|driver|real_driver|child_mode|outer_companion)
    ;;
  *)
    echo "[resolve_backend_sidecar_defaults] missing or invalid --field" 1>&2
    usage
    exit 2
    ;;
esac

if [ "$target" = "" ]; then
  target="$(detect_host_target)"
fi

snapshot="$root/artifacts/backend_sidecar_cheng_fresh/verify_backend_sidecar_cheng_fresh.snapshot.env"
if [ ! -f "$snapshot" ]; then
  echo "[resolve_backend_sidecar_defaults] missing fresh snapshot: $snapshot" 1>&2
  exit 1
fi

snapshot_status="$(sed -n 's/^backend_sidecar_cheng_fresh_status=//p' "$snapshot" | head -n 1)"
snapshot_mode="$(sed -n 's/^backend_sidecar_cheng_fresh_mode=//p' "$snapshot" | head -n 1)"
snapshot_compiler="$(sed -n 's/^backend_sidecar_cheng_fresh_compiler=//p' "$snapshot" | head -n 1)"
snapshot_driver="$(sed -n 's/^backend_sidecar_cheng_fresh_driver=//p' "$snapshot" | head -n 1)"
snapshot_real_driver="$(sed -n 's/^backend_sidecar_cheng_fresh_real_driver=//p' "$snapshot" | head -n 1)"
snapshot_bundle="$(sed -n 's/^backend_sidecar_cheng_fresh_bundle=//p' "$snapshot" | head -n 1)"
snapshot_child_mode="$(sed -n 's/^backend_sidecar_cheng_fresh_child_mode=//p' "$snapshot" | head -n 1)"
snapshot_outer_companion="$(sed -n 's/^backend_sidecar_cheng_fresh_outer_companion=//p' "$snapshot" | head -n 1)"

if [ "$snapshot_status" != "ok" ]; then
  echo "[resolve_backend_sidecar_defaults] stale fresh snapshot status: $snapshot_status" 1>&2
  exit 1
fi
if [ "$snapshot_mode" != "cheng" ]; then
  echo "[resolve_backend_sidecar_defaults] invalid fresh sidecar mode: $snapshot_mode" 1>&2
  exit 1
fi
if [ "$snapshot_compiler" = "" ] || [ ! -x "$snapshot_compiler" ]; then
  echo "[resolve_backend_sidecar_defaults] missing fresh sidecar compiler: $snapshot_compiler" 1>&2
  exit 1
fi
case "$snapshot_compiler" in
  "$root"/*) ;;
  *)
    echo "[resolve_backend_sidecar_defaults] unstable fresh sidecar compiler path: $snapshot_compiler" 1>&2
    exit 1
    ;;
esac
if [ "$snapshot_driver" = "" ] || [ ! -x "$snapshot_driver" ]; then
  echo "[resolve_backend_sidecar_defaults] missing fresh sidecar driver: $snapshot_driver" 1>&2
  exit 1
fi
case "$snapshot_driver" in
  "$root"/*) ;;
  *)
    echo "[resolve_backend_sidecar_defaults] unstable fresh sidecar driver path: $snapshot_driver" 1>&2
    exit 1
    ;;
esac
if [ "$snapshot_real_driver" = "" ] || [ ! -x "$snapshot_real_driver" ]; then
  echo "[resolve_backend_sidecar_defaults] missing fresh sidecar real driver: $snapshot_real_driver" 1>&2
  exit 1
fi
case "$snapshot_real_driver" in
  "$root"/*) ;;
  *)
    echo "[resolve_backend_sidecar_defaults] unstable fresh sidecar real driver path: $snapshot_real_driver" 1>&2
    exit 1
    ;;
esac
if ! strict_stage0_published_surface "$snapshot_real_driver"; then
  echo "[resolve_backend_sidecar_defaults] strict-fresh real driver must be a published strict stage0 surface: $snapshot_real_driver" 1>&2
  exit 1
fi
if ! strict_stage0_meta_ok "$snapshot_real_driver"; then
  echo "[resolve_backend_sidecar_defaults] strict-fresh real driver missing contract: $snapshot_real_driver" 1>&2
  exit 1
fi
if ! strict_stage0_current_enough "$snapshot_real_driver"; then
  echo "[resolve_backend_sidecar_defaults] strict-fresh real driver is stale against current sources: $snapshot_real_driver" 1>&2
  exit 1
fi
if [ "$snapshot_bundle" = "" ] || [ ! -s "$snapshot_bundle" ]; then
  echo "[resolve_backend_sidecar_defaults] missing fresh sidecar bundle: $snapshot_bundle" 1>&2
  exit 1
fi
case "$snapshot_bundle" in
  "$root"/*) ;;
  *)
    echo "[resolve_backend_sidecar_defaults] unstable fresh sidecar bundle path: $snapshot_bundle" 1>&2
    exit 1
    ;;
esac
case "$snapshot_child_mode" in
  cli)
    ;;
  outer_cli)
    if [ "$snapshot_outer_companion" = "" ] || [ ! -x "$snapshot_outer_companion" ]; then
      echo "[resolve_backend_sidecar_defaults] missing fresh sidecar outer companion: $snapshot_outer_companion" 1>&2
      exit 1
    fi
    case "$snapshot_outer_companion" in
      "$root"/*) ;;
      *)
        echo "[resolve_backend_sidecar_defaults] unstable fresh sidecar outer companion path: $snapshot_outer_companion" 1>&2
        exit 1
        ;;
    esac
    ;;
  *)
    echo "[resolve_backend_sidecar_defaults] invalid fresh sidecar child mode: $snapshot_child_mode" 1>&2
    exit 1
    ;;
esac

mode="cheng"
bundle="$snapshot_bundle"
compiler="$snapshot_compiler"

case "$field" in
  mode)
    printf '%s\n' "$mode"
    ;;
  bundle)
    printf '%s\n' "$bundle"
    ;;
  compiler)
    printf '%s\n' "$compiler"
    ;;
  driver)
    printf '%s\n' "$snapshot_driver"
    ;;
  real_driver)
    printf '%s\n' "$snapshot_real_driver"
    ;;
  child_mode)
    printf '%s\n' "$snapshot_child_mode"
    ;;
  outer_companion)
    printf '%s\n' "$snapshot_outer_companion"
    ;;
esac
