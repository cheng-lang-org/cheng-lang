#!/usr/bin/env sh
set -euo pipefail

usage() {
  cat <<'EOF'
Usage:
  src/tooling/chengb.sh <file.cheng>
    [--emit:<obj|exe>] [--target:<triple|auto>] [--out:<path>]
    [--frontend:<mvp|stage1>]
    [--abi:<v1|v2_noptr>]
    [--linker:<self>]
    [--multi]
    [--pkg-roots:<p1[:p2:...]>]
    [--run]

Builds a Cheng program using the self-developed backend (MIR -> AArch64/x86_64 -> obj -> link).

Notes:
  - Backend MVP supports AArch64 + x86_64 subset.
    - Native exe+run: `arm64-apple-darwin`, `x86_64-apple-darwin` (Rosetta may be required), `aarch64-linux-android` (via adb)
    - Cross targets (e.g. `x86_64-unknown-linux-gnu`, `*-windows-msvc`) are typically obj-only unless you provide a cross toolchain.
  - Default target is `auto` (detected from current host platform).
  - Default (emit=exe): links via the self linker and uses the pure Cheng runtime object.
  - `--run` executes the output:
      - macOS: runs locally
      - android: pushes to `/data/local/tmp/chengb/` and runs via `adb shell`
  - Memory model defaults to `CHENG_MM=orc` (set `CHENG_MM=off` only for diagnostics).
EOF
}

cleanup_backend_exe_sidecar() (
  cleanup_out="$1"
  case "${CHENG_BACKEND_KEEP_EXE_OBJ:-0}" in
    1|true|TRUE|yes|YES|on|ON)
      return 0
      ;;
  esac
  cleanup_sidecar="${cleanup_out}.o"
  if [ -f "$cleanup_sidecar" ]; then
    rm -f "$cleanup_sidecar"
  fi
)

if [ "${1:-}" = "" ] || [ "${1:-}" = "--help" ] || [ "${1:-}" = "-h" ]; then
  usage
  exit 0
fi

in="$1"
shift || true

if [ ! -f "$in" ]; then
  echo "[Error] file not found: $in" 1>&2
  exit 2
fi

emit="exe"
target="auto"
frontend="mvp"
out=""
linker=""
multi="0"
pkg_roots=""
run="0"
abi=""

while [ "${1:-}" != "" ]; do
  case "$1" in
    --emit:*)
      emit="${1#--emit:}"
      ;;
    --target:*)
      target="${1#--target:}"
      ;;
    --frontend:*)
      frontend="${1#--frontend:}"
      ;;
    --abi:*)
      abi="${1#--abi:}"
      ;;
    --out:*)
      out="${1#--out:}"
      ;;
    --linker:*)
      linker="${1#--linker:}"
      ;;
    --multi)
      multi="1"
      ;;
    --pkg-roots:*)
      pkg_roots="${1#--pkg-roots:}"
      ;;
    --run)
      run="1"
      ;;
    *)
      echo "[Error] unknown arg: $1" 1>&2
      usage
      exit 2
      ;;
  esac
  shift || true
done

case "$emit" in
  obj|exe) ;;
  *)
    echo "[Error] invalid --emit:$emit (expected obj|exe)" 1>&2
    exit 2
    ;;
esac

case "$frontend" in
  mvp|stage1) ;;
  *)
    echo "[Error] invalid --frontend:$frontend (expected mvp|stage1)" 1>&2
    exit 2
    ;;
esac

case "$abi" in
  ""|v1|v2_noptr) ;;
  *)
    echo "[Error] invalid --abi:$abi (expected v1|v2_noptr)" 1>&2
    exit 2
    ;;
esac

root="$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)"
cd "$root"

if [ "$target" = "" ] || [ "$target" = "auto" ]; then
  target="$(sh src/tooling/detect_host_target.sh)"
fi

if [ -z "${CHENG_MM:-}" ]; then
  export CHENG_MM=orc
fi

if [ "${CHENG_CLEAN_CHENG_LOCAL:-1}" = "1" ] && [ "${CHENG_TOOLING_CLEANUP_DEPTH:-0}" = "0" ]; then
  export CHENG_TOOLING_CLEANUP_DEPTH=1
  cleanup_backend_driver_on_exit() {
    status=$?
    set +e
    sh src/tooling/cleanup_cheng_local.sh
    exit "$status"
  }
  trap cleanup_backend_driver_on_exit EXIT
fi

# Default output under artifacts/ to avoid writing into source dirs.
if [ "$out" = "" ]; then
  base="$(basename "$in" .cheng)"
  out_dir="$root/artifacts/chengb"
  mkdir -p "$out_dir"
  case "$emit" in
    obj) out="$out_dir/${base}.o" ;;
    exe) out="$out_dir/${base}" ;;
  esac
fi

# Select the backend driver (defaults to ./cheng; override via CHENG_BACKEND_DRIVER).
driver="$(sh src/tooling/backend_driver_path.sh)"

envs=""
backend_whole_program="${CHENG_BACKEND_WHOLE_PROGRAM:-1}"
envs="$envs CHENG_BACKEND_EMIT=$emit"
envs="$envs CHENG_BACKEND_TARGET=$target"
envs="$envs CHENG_BACKEND_FRONTEND=$frontend"
envs="$envs CHENG_BACKEND_INPUT=$in"
envs="$envs CHENG_BACKEND_OUTPUT=$out"
envs="$envs CHENG_BACKEND_WHOLE_PROGRAM=$backend_whole_program"
linker_env="${CHENG_BACKEND_LINKER:-}"
if [ "$linker" = "" ]; then
  linker="$linker_env"
fi
if [ "$linker" = "" ] && [ "$emit" = "exe" ]; then
  linker="self"
fi
case "$linker" in
  ""|self) ;;
  *)
    echo "[Error] invalid --linker:$linker (expected self)" 1>&2
    exit 2
    ;;
esac
if [ "$multi" = "1" ]; then
  envs="$envs CHENG_BACKEND_MULTI=1"
else
  envs="$envs CHENG_BACKEND_MULTI=0"
  envs="$envs CHENG_BACKEND_MULTI_FORCE=0"
fi
if [ "$abi" != "" ]; then
  envs="$envs CHENG_ABI=$abi"
fi
abi_effective="$abi"
if [ "$abi_effective" = "" ]; then
  abi_effective="${CHENG_ABI:-}"
fi
if [ "$abi_effective" = "v2_noptr" ]; then
  envs="$envs CHENG_STAGE1_STD_NO_POINTERS=1"
  envs="$envs CHENG_STAGE1_STD_NO_POINTERS_STRICT=1"
fi
if [ "$pkg_roots" != "" ]; then
  envs="$envs CHENG_PKG_ROOTS=$pkg_roots"
fi

if [ "$emit" = "exe" ] && [ "$linker" = "self" ]; then
  mkdir -p chengcache
  backend_elf_profile="${CHENG_BACKEND_ELF_PROFILE:-}"
  runtime_src="src/std/system_helpers_backend.cheng"
  case "$backend_elf_profile/$target" in
    nolibc/aarch64-*-linux-*|nolibc/arm64-*-linux-*)
      runtime_src="src/std/system_helpers_backend_nolibc_linux_aarch64.cheng"
      ;;
  esac
  runtime_stem="$(basename "$runtime_src" .cheng)"
  safe_target="$(printf '%s' "$target" | tr -c 'A-Za-z0-9._-' '_' | tr -s '_')"
  runtime_obj="${CHENG_BACKEND_RUNTIME_OBJ:-chengcache/${runtime_stem}.${safe_target}.o}"
  if [ ! -f "$runtime_src" ]; then
    echo "[Error] missing backend runtime source: $runtime_src" 1>&2
    exit 2
  fi
  if [ ! -f "$runtime_obj" ] || [ "$runtime_src" -nt "$runtime_obj" ]; then
    env \
      CHENG_C_SYSTEM=0 \
      CHENG_BACKEND_ALLOW_NO_MAIN=1 \
      CHENG_BACKEND_WHOLE_PROGRAM=1 \
      CHENG_BACKEND_EMIT=obj \
      CHENG_BACKEND_TARGET="$target" \
      CHENG_BACKEND_FRONTEND=mvp \
      CHENG_BACKEND_INPUT="$runtime_src" \
      CHENG_BACKEND_OUTPUT="$runtime_obj" \
      "$driver" >/dev/null
  fi
  envs="$envs CHENG_BACKEND_LINKER=self"
  envs="$envs CHENG_BACKEND_NO_RUNTIME_C=1"
  envs="$envs CHENG_BACKEND_RUNTIME_OBJ=$runtime_obj"
fi

abi_log="${abi_effective:-v1}"
echo "[chengb] build: $in -> $out ($target, $emit, abi=$abi_log)" >&2
# shellcheck disable=SC2086
env $envs "$driver" >/dev/null
if [ "$emit" = "exe" ]; then
  cleanup_backend_exe_sidecar "$out"
fi

if [ "$run" != "1" ] || [ "$emit" != "exe" ]; then
  echo "chengb ok"
  exit 0
fi

case "$target" in
  *android*)
  if ! command -v adb >/dev/null 2>&1; then
    echo "[chengb] missing tool: adb (required for --run on android targets)" 1>&2
    exit 2
  fi
  serial="${ANDROID_SERIAL:-}"
  if [ -z "$serial" ]; then
    serial="$(adb devices | awk 'NR>1 && $2 == "device" {print $1; exit}')"
  fi
  if [ -z "$serial" ]; then
    echo "[chengb] no Android device/emulator detected (adb devices)" 1>&2
    exit 2
  fi
  remote_dir="/data/local/tmp/chengb"
  remote_path="$remote_dir/$(basename "$out")"
  adb -s "$serial" shell "mkdir -p '$remote_dir'" >/dev/null
  adb -s "$serial" push "$out" "$remote_path" >/dev/null
  adb -s "$serial" shell "chmod 755 '$remote_path'" >/dev/null
  echo "[chengb] run(android): $remote_path ($serial)" >&2
  adb -s "$serial" shell "$remote_path"
  exit $?
  ;;
esac

echo "[chengb] run(host): $out" >&2
"$out"
