#!/usr/bin/env sh
. "$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)/env_prefix_bridge.sh"
set -eu
(set -o pipefail) 2>/dev/null && set -o pipefail

root="$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)"
cd "$root"

out_dir="artifacts/backend_plugin_system"
mkdir -p "$out_dir"

snapshot_file="$out_dir/plugin_system.snapshot.env"
report_file="$out_dir/plugin_system.report.txt"
disabled_log="$out_dir/plugin_system.disable_core.log"
enabled_log="$out_dir/plugin_system.enable_core.log"
disabled_obj="$out_dir/plugin_system_disable_core.o"
enabled_obj="$out_dir/plugin_system_enable_core.o"

scan_timestamp="$(date -u +'%Y-%m-%dT%H:%M:%SZ' 2>/dev/null || echo "<unknown>")"
git_head="$(git -C "$root" rev-parse --verify HEAD 2>/dev/null || echo "<unknown>")"

driver="$(sh src/tooling/backend_driver_path.sh 2>/dev/null || true)"
if [ "$driver" = "" ] || [ ! -x "$driver" ]; then
  echo "[verify_backend_plugin_system] backend driver is not executable: ${driver:-<empty>}" 1>&2
  exit 2
fi

case "${PLUGIN_ENABLE:-1}" in
  0|1) ;;
  *)
    echo "[verify_backend_plugin_system] invalid PLUGIN_ENABLE (must be 0 or 1): ${PLUGIN_ENABLE:-<empty>}" 1>&2
    exit 1
    ;;
esac

hash_file_func() {
  file="$1"
  if command -v shasum >/dev/null 2>&1; then
    shasum -a 256 "$file" | awk '{print $1}'
  elif command -v sha256sum >/dev/null 2>&1; then
    sha256sum "$file" | awk '{print $1}'
  else
    echo "unavailable"
  fi
}

run_core_compile() {
  plugin_enable="$1"
  out_obj="$2"
  out_log="$3"

  if env \
      PLUGIN_ENABLE="$plugin_enable" \
      PLUGIN_PATHS= \
      METERING_PLUGIN= \
      ABI="${ABI:-v2_noptr}" \
      STAGE1_STD_NO_POINTERS=0 \
      STAGE1_STD_NO_POINTERS_STRICT=0 \
      STAGE1_NO_POINTERS_NON_C_ABI=0 \
      STAGE1_NO_POINTERS_NON_C_ABI_INTERNAL=0 \
      MM="${MM:-orc}" \
      BACKEND_TARGET="${BACKEND_TARGET:-auto}" \
      BACKEND_FRONTEND="${BACKEND_FRONTEND:-stage1}" \
      STAGE1_SKIP_SEM="${STAGE1_SKIP_SEM:-0}" \
      STAGE1_SKIP_OWNERSHIP="${STAGE1_SKIP_OWNERSHIP:-1}" \
      BACKEND_EMIT=obj \
      BACKEND_LINKER="${BACKEND_LINKER:-self}" \
      BACKEND_INPUT=tests/cheng/backend/fixtures/return_add.cheng \
      BACKEND_OUTPUT="$out_obj" \
      "$driver" >"$out_log" 2>&1; then
    status=0
  else
    status=$?
  fi

  if [ "$status" -ne 0 ]; then
    return "$status"
  fi
  if [ ! -s "$out_obj" ]; then
    return 2
  fi
  return 0
}

echo "== backend.plugin_system.disable_core =="
set +e
run_core_compile 0 "$disabled_obj" "$disabled_log"
status_disable="$?"
set -e
if [ "$status_disable" -ne 0 ]; then
  echo "[verify_backend_plugin_system] core compile failed when PLUGIN_ENABLE=0" 1>&2
  echo "  status: $status_disable" 1>&2
  cat "$disabled_log" | sed -n '1,120p' 1>&2 || true
  exit 1
fi

echo "== backend.plugin_system.enable_core =="
set +e
run_core_compile 1 "$enabled_obj" "$enabled_log"
status_enable="$?"
set -e
if [ "$status_enable" -ne 0 ]; then
  echo "[verify_backend_plugin_system] core compile failed when PLUGIN_ENABLE=1 with empty plugin paths" 1>&2
  echo "  status: $status_enable" 1>&2
  cat "$enabled_log" | sed -n '1,120p' 1>&2 || true
  exit 1
fi

# Keep one deterministic artifact for backward compatibility.
cp "$disabled_obj" "$out_dir/plugin_system_core.o"

disable_obj_hash="$(hash_file_func "$disabled_obj")"
enable_obj_hash="$(hash_file_func "$enabled_obj")"
log_lines_disable="$(wc -l < "$disabled_log" | tr -d ' ')"
log_lines_enable="$(wc -l < "$enabled_log" | tr -d ' ')"

{
  echo "plugin_system_snapshot_time_utc=$scan_timestamp"
  echo "git_head=$git_head"
  echo "driver=$driver"
  echo "disable_core_object=$disabled_obj"
  echo "disable_core_object_sha256=$disable_obj_hash"
  echo "enable_core_object=$enabled_obj"
  echo "enable_core_object_sha256=$enable_obj_hash"
  echo "disable_core_log=$disabled_log"
  echo "disable_core_log_lines=$log_lines_disable"
  echo "enable_core_log=$enabled_log"
  echo "enable_core_log_lines=$log_lines_enable"
  echo "plugin_enable_env=${PLUGIN_ENABLE:-1}"
  echo "plugin_paths_env=${PLUGIN_PATHS:-}"
  echo "metering_plugin_env=${METERING_PLUGIN:-}"
} > "$snapshot_file"

{
  echo "verify_backend_plugin_system report"
  echo "status=ok"
  echo "plugin_enable_env=${PLUGIN_ENABLE:-1}"
  echo "disable_core_object=$disabled_obj"
  echo "disable_core_object_sha256=$disable_obj_hash"
  echo "enable_core_object=$enabled_obj"
  echo "enable_core_object_sha256=$enable_obj_hash"
  echo "disable_core_log=$disabled_log (lines=$log_lines_disable)"
  echo "enable_core_log=$enabled_log (lines=$log_lines_enable)"
  echo "artifact_core=$out_dir/plugin_system_core.o"
} > "$report_file"

echo "verify_backend_plugin_system ok"
