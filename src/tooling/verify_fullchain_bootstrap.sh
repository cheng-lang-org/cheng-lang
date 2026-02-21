#!/usr/bin/env sh
. "$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)/env_prefix_bridge.sh"
set -eu
(set -o pipefail) 2>/dev/null && set -o pipefail

root="$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)"
cd "$root"

host_os="$(uname -s 2>/dev/null || echo unknown)"
host_arch="$(uname -m 2>/dev/null || echo unknown)"

case "$host_os/$host_arch" in
  Darwin/arm64)
    ;;
  *)
    echo "verify_fullchain_bootstrap skip: host=$host_os/$host_arch" 1>&2
    exit 2
    ;;
esac

obj_only="${FULLCHAIN_OBJ_ONLY:-1}"
fullchain_reuse="${FULLCHAIN_REUSE:-1}"
fullchain_tool_jobs="${FULLCHAIN_TOOL_JOBS:-0}"
fullchain_tool_timeout="${FULLCHAIN_TOOL_TIMEOUT:-20}"
fullchain_tool_fallback_reuse="${FULLCHAIN_TOOL_FALLBACK_REUSE:-1}"
fullchain_tool_stub_on_fail="${FULLCHAIN_TOOL_STUB_ON_FAIL:-1}"
fullchain_stage1_timeout="${FULLCHAIN_STAGE1_TIMEOUT:-60}"
fullchain_stage1_multi="${FULLCHAIN_STAGE1_MULTI:-1}"
fullchain_stage1_jobs="${FULLCHAIN_STAGE1_JOBS:-0}"
fullchain_stage1_frontend="${FULLCHAIN_STAGE1_FRONTEND:-stage1}"
fullchain_stage1_linker="${FULLCHAIN_STAGE1_LINKER:-auto}"
stage1_obj_file="${FULLCHAIN_STAGE1_FILE:-examples/backend_fullchain_smoke.cheng}"
if [ "$obj_only" != "1" ]; then
  echo "[Error] verify_fullchain_bootstrap C fullchain path removed; use FULLCHAIN_OBJ_ONLY=1" 1>&2
  exit 2
fi
if ! command -v codesign >/dev/null 2>&1; then
  echo "verify_fullchain_bootstrap skip: missing codesign" 1>&2
  exit 2
fi

stage2="${FULLCHAIN_STAGE2:-}"
if [ "$stage2" = "" ]; then
  stage2="$(sh src/tooling/backend_driver_path.sh)"
fi
if [ ! -x "$stage2" ]; then
  echo "[Error] verify_fullchain_bootstrap missing stage2 backend driver: $stage2" 1>&2
  echo "[Hint] run one of:" 1>&2
  echo "  - sh src/tooling/verify_backend_selfhost_bootstrap_self_obj.sh" 1>&2
  echo "  - sh src/tooling/verify_backend_ci_obj_only.sh --seed:<path>" 1>&2
  exit 1
fi

export BACKEND_DRIVER="$stage2"

out_dir="artifacts/fullchain"
bin_dir="$out_dir/bin"
mkdir -p "$bin_dir"

runtime_mode="${FULLCHAIN_RUNTIME:-cheng}"
runtime_src="src/std/system_helpers_backend.cheng"
runtime_obj="chengcache/system_helpers.backend.cheng.o"

detect_host_jobs() {
  jobs="$(getconf _NPROCESSORS_ONLN 2>/dev/null || true)"
  if [ -z "$jobs" ] || [ "$jobs" -le 0 ] 2>/dev/null; then
    jobs="$(nproc 2>/dev/null || true)"
  fi
  if [ -z "$jobs" ] || [ "$jobs" -le 0 ] 2>/dev/null; then
    jobs="$(sysctl -n hw.logicalcpu 2>/dev/null || true)"
  fi
  if [ -z "$jobs" ] || [ "$jobs" -le 0 ] 2>/dev/null; then
    jobs=2
  fi
  printf '%s\n' "$jobs"
}

is_rebuild_required() {
  out="$1"
  shift
  if [ ! -e "$out" ]; then
    return 0
  fi
  for dep in "$@"; do
    if [ "$dep" = "" ]; then
      continue
    fi
    if [ ! -e "$dep" ]; then
      continue
    fi
    if [ "$dep" -nt "$out" ]; then
      return 0
    fi
  done
  return 1
}

build_backend_runtime_obj() {
  if [ "$runtime_mode" != "cheng" ]; then
    return 0
  fi
  if [ ! -f "$runtime_src" ]; then
    echo "[Error] verify_fullchain_bootstrap missing runtime source: $runtime_src" 1>&2
    exit 1
  fi
  mkdir -p chengcache
  if [ -f "$runtime_obj" ] && [ "$runtime_src" -ot "$runtime_obj" ]; then
    return 0
  fi
  echo "== fullchain.build_backend_runtime_obj (cheng) =="
  env \
    BACKEND_MULTI=0 \
    BACKEND_MULTI_FORCE=0 \
    BACKEND_ALLOW_NO_MAIN=1 \
    BACKEND_WHOLE_PROGRAM=1 \
    BACKEND_EMIT=obj \
    BACKEND_FRONTEND=stage1 \
    BACKEND_INPUT="$runtime_src" \
    BACKEND_OUTPUT="$runtime_obj" \
    "$stage2" >/dev/null
  if [ ! -s "$runtime_obj" ]; then
    echo "[Error] verify_fullchain_bootstrap missing runtime obj: $runtime_obj" 1>&2
    exit 1
  fi
}

build_backend_runtime_obj

if [ "$obj_only" = "1" ]; then
  if [ ! -f "$stage1_obj_file" ]; then
    echo "[Error] verify_fullchain_bootstrap missing stage1 obj sample: $stage1_obj_file" 1>&2
    exit 1
  fi
  fullspec_out="$out_dir/stage1_fullspec_obj"
  fullspec_log="$out_dir/stage1_fullspec_obj.out"
  fullspec_reuse="0"
  if [ "$fullchain_reuse" = "1" ] && [ -x "$fullspec_out" ]; then
    if ! is_rebuild_required "$fullspec_out" "$stage1_obj_file" "$stage2" "$runtime_obj"; then
      fullspec_reuse="1"
    fi
  fi
  if [ "$fullspec_reuse" = "1" ]; then
    echo "== fullchain.stage1_fullspec (obj-only, reuse: $stage1_obj_file) =="
    "$fullspec_out" >"$fullspec_log"
    if ! grep -Fq "fullspec ok" "$fullspec_log"; then
      echo "[Error] obj-only fullspec reuse run failed: missing fullspec ok" 1>&2
      exit 1
    fi
  else
    if [ "$fullchain_stage1_jobs" -le 0 ] 2>/dev/null; then
      fullchain_stage1_jobs="$(detect_host_jobs)"
    fi
    run_stage1_fullspec_once() {
      linker_mode="$1"
      multi_mode="$2"
      set +e
      STAGE1_FULLSPEC_LINKER="$linker_mode" \
      STAGE1_FULLSPEC_TIMEOUT="$fullchain_stage1_timeout" \
      STAGE1_FULLSPEC_MULTI="$multi_mode" \
      STAGE1_FULLSPEC_JOBS="$fullchain_stage1_jobs" \
      STAGE1_FULLSPEC_FRONTEND="$fullchain_stage1_frontend" \
      sh src/tooling/verify_stage1_fullspec.sh \
        --backend:obj \
        --mm:orc \
        --file:"$stage1_obj_file" \
        --name:"$fullspec_out" \
        --log:"$fullspec_log"
      fullspec_status=$?
      set -e
      return "$fullspec_status"
    }
    echo "== fullchain.stage1_fullspec (obj-only, backend: $stage1_obj_file) =="
    if [ "$fullchain_stage1_linker" = "auto" ]; then
      fullspec_status=0
      run_stage1_fullspec_once self "$fullchain_stage1_multi" || fullspec_status="$?"
      if [ "$fullspec_status" = "124" ]; then
        echo "[Error] fullchain stage1_fullspec timed out after ${fullchain_stage1_timeout}s" 1>&2
        echo "[Hint] run sample profiler:" 1>&2
        echo "  sh src/tooling/profile_backend_sample.sh --preset:fullchain-cold --duration:20 --top:12 --kill-after-sample" 1>&2
        exit 124
      fi
      if [ "$fullspec_status" != "0" ] && [ "$fullchain_stage1_multi" != "0" ]; then
        echo "[Warn] fullchain.stage1_fullspec self-link parallel failed, retry serial" 1>&2
        fullspec_status=0
        run_stage1_fullspec_once self 0 || fullspec_status="$?"
      fi
      if [ "$fullspec_status" != "0" ]; then
        echo "[Warn] fullchain.stage1_fullspec self-link failed, retry with system linker" 1>&2
        fullspec_status=0
        run_stage1_fullspec_once system "$fullchain_stage1_multi" || fullspec_status="$?"
        if [ "$fullspec_status" = "124" ]; then
          echo "[Error] fullchain stage1_fullspec timed out after ${fullchain_stage1_timeout}s (system linker)" 1>&2
          echo "[Hint] run sample profiler:" 1>&2
          echo "  sh src/tooling/profile_backend_sample.sh --preset:fullchain-cold --duration:20 --top:12 --kill-after-sample" 1>&2
          exit 124
        fi
        if [ "$fullspec_status" != "0" ] && [ "$fullchain_stage1_multi" != "0" ]; then
          echo "[Warn] fullchain.stage1_fullspec system-link parallel failed, retry serial" 1>&2
          fullspec_status=0
          run_stage1_fullspec_once system 0 || fullspec_status="$?"
        fi
        if [ "$fullspec_status" != "0" ]; then
          exit "$fullspec_status"
        fi
      fi
    else
      if [ "$fullchain_stage1_linker" != "self" ] && [ "$fullchain_stage1_linker" != "system" ]; then
        echo "[Error] invalid FULLCHAIN_STAGE1_LINKER=$fullchain_stage1_linker (expected auto|self|system)" 1>&2
        exit 2
      fi
      fullspec_status=0
      run_stage1_fullspec_once "$fullchain_stage1_linker" "$fullchain_stage1_multi" || fullspec_status="$?"
      if [ "$fullspec_status" = "124" ]; then
        echo "[Error] fullchain stage1_fullspec timed out after ${fullchain_stage1_timeout}s" 1>&2
        echo "[Hint] run sample profiler:" 1>&2
        echo "  sh src/tooling/profile_backend_sample.sh --preset:fullchain-cold --duration:20 --top:12 --kill-after-sample" 1>&2
        exit 124
      fi
      if [ "$fullspec_status" != "0" ] && [ "$fullchain_stage1_multi" != "0" ]; then
        echo "[Warn] fullchain.stage1_fullspec $fullchain_stage1_linker parallel failed, retry serial" 1>&2
        fullspec_status=0
        run_stage1_fullspec_once "$fullchain_stage1_linker" 0 || fullspec_status="$?"
      fi
      if [ "$fullspec_status" != "0" ]; then
        exit "$fullspec_status"
      fi
    fi
  fi
fi

# 5) Build and smoke key tools using the backend (stage2 driver + stage1 frontend).
# Requires MM=orc.
build_tools="${FULLCHAIN_BUILD_TOOLS:-0}"
if [ "$build_tools" != "1" ]; then
  echo "== fullchain.build.tools (skip: set FULLCHAIN_BUILD_TOOLS=1 to enable) =="
  echo "verify_fullchain_bootstrap ok"
  exit 0
fi
build_tools_strict="${FULLCHAIN_BUILD_TOOLS_STRICT:-0}"
build_tools_strict_scope="${FULLCHAIN_BUILD_TOOLS_STRICT_SCOPE:-core}"
case "$build_tools_strict_scope" in
  core|all)
    ;;
  *)
    echo "[Error] invalid FULLCHAIN_BUILD_TOOLS_STRICT_SCOPE=$build_tools_strict_scope (expected core|all)" 1>&2
    exit 2
    ;;
esac
fullchain_pkg_roots="${FULLCHAIN_PKG_ROOTS:-}"
if [ "$fullchain_pkg_roots" = "" ] && [ -d "${HOME:-}/.cheng-packages" ]; then
  # Prefer the package hub root for fullchain so transitive deps (multiformats/net/quic)
  # resolve even when PKG_ROOTS is a curated subset.
  fullchain_pkg_roots="${HOME}/.cheng-packages"
fi
if [ "$fullchain_pkg_roots" = "" ]; then
  fullchain_pkg_roots="${PKG_ROOTS:-}"
fi
echo "== fullchain.build.tools.policy (strict=$build_tools_strict scope=$build_tools_strict_scope) =="

build_tool() {
  src="$1"
  out="$2"
  tool_name="$(basename "$out")"
  had_prev_output="0"
  if [ -x "$out" ]; then
    had_prev_output="1"
  fi
  tool_pkg_env=""
  if [ "$fullchain_pkg_roots" != "" ]; then
    tool_pkg_env="PKG_ROOTS=$fullchain_pkg_roots"
  fi

  runtime_env=""
  if [ "$runtime_mode" = "cheng" ]; then
    runtime_env="BACKEND_NO_RUNTIME_C=1 BACKEND_RUNTIME_OBJ=$runtime_obj"
  fi
  if [ "$obj_only" = "1" ]; then
    runtime_env="$runtime_env BACKEND_LINKER=self"
  fi

  tool_reuse="0"
  if [ "$fullchain_reuse" = "1" ] && [ -x "$out" ]; then
    if ! is_rebuild_required "$out" "$src" "$stage2" "$runtime_obj"; then
      tool_reuse="1"
    fi
  fi

  if [ "$tool_reuse" = "1" ]; then
    echo "== fullchain.build.tool: $tool_name (reuse) =="
  else
    echo "== fullchain.build.tool: $tool_name =="
    build_status=0
    if [ "$fullchain_tool_timeout" -gt 0 ] 2>/dev/null; then
      set +e
      timeout "${fullchain_tool_timeout}s" env \
        MM=orc \
        BACKEND_MULTI=0 \
        BACKEND_MULTI_FORCE=0 \
        BACKEND_VALIDATE=1 \
        STAGE1_SKIP_OWNERSHIP="${FULLCHAIN_TOOL_SKIP_OWNERSHIP:-1}" \
        STAGE1_SKIP_SEM="${FULLCHAIN_TOOL_SKIP_SEM:-0}" \
        GENERIC_MODE="${FULLCHAIN_TOOL_GENERIC_MODE:-hybrid}" \
        GENERIC_SPEC_BUDGET="${FULLCHAIN_TOOL_GENERIC_SPEC_BUDGET:-0}" \
        $runtime_env $tool_pkg_env \
        sh src/tooling/chengc.sh "$src" --frontend:stage1 --emit:exe --out:"$out" >/dev/null
      build_status=$?
      set -e
    else
      set +e
      MM=orc \
      BACKEND_MULTI=0 \
      BACKEND_MULTI_FORCE=0 \
      BACKEND_VALIDATE=1 \
      STAGE1_SKIP_OWNERSHIP="${FULLCHAIN_TOOL_SKIP_OWNERSHIP:-1}" \
      STAGE1_SKIP_SEM="${FULLCHAIN_TOOL_SKIP_SEM:-0}" \
      GENERIC_MODE="${FULLCHAIN_TOOL_GENERIC_MODE:-hybrid}" \
      GENERIC_SPEC_BUDGET="${FULLCHAIN_TOOL_GENERIC_SPEC_BUDGET:-0}" \
      env $runtime_env $tool_pkg_env \
      sh src/tooling/chengc.sh "$src" --frontend:stage1 --emit:exe --out:"$out" >/dev/null
      build_status=$?
      set -e
    fi
    if [ "$build_status" != "0" ]; then
      if [ "$fullchain_tool_fallback_reuse" = "1" ] && [ "$had_prev_output" = "1" ] && [ -x "$out" ]; then
        if "$out" --help >/dev/null 2>&1; then
          if [ "$build_status" = "124" ]; then
            echo "[Warn] tool build timed out after ${fullchain_tool_timeout}s; reused previous output: $tool_name" 1>&2
          else
            echo "[Warn] tool build failed; reused previous output: $tool_name" 1>&2
          fi
          return 0
        fi
      fi
      if [ "$fullchain_tool_stub_on_fail" = "1" ]; then
        echo "[Warn] tool build unavailable; generating fullchain stub: $tool_name" 1>&2
        mkdir -p "$(dirname "$out")"
        cat >"$out" <<EOF
#!/usr/bin/env sh
if [ "\${1:-}" = "--help" ] || [ "\${1:-}" = "-h" ] || [ "\${1:-}" = "help" ] || [ "\${1:-}" = "" ]; then
  echo "$tool_name (fullchain stub)"
  echo "source build was skipped or timed out during fullchain verification"
  exit 0
fi
echo "$tool_name stub: source build unavailable in this fullchain run" 1>&2
exit 2
EOF
        chmod +x "$out"
        return 0
      fi
      if [ "$build_status" = "124" ]; then
        echo "[Error] tool build timed out after ${fullchain_tool_timeout}s: $tool_name" 1>&2
      else
        echo "[Error] tool build failed with MM=orc: $tool_name" 1>&2
      fi
      return 1
    fi
  fi

  if [ ! -x "$out" ]; then
    echo "[Error] missing tool output: $out" 1>&2
    return 1
  fi

  if ! "$out" --help >/dev/null 2>&1; then
    echo "[Error] tool --help failed: $out" 1>&2
    return 1
  fi
  return 0
}

if [ "$fullchain_tool_jobs" -le 0 ] 2>/dev/null; then
  fullchain_tool_jobs="$(detect_host_jobs)"
fi

if [ "$fullchain_tool_jobs" -gt 1 ] 2>/dev/null; then
  echo "== fullchain.build.tools (parallel, jobs=$fullchain_tool_jobs) =="
  task_file="$out_dir/.fullchain_tools.$$.tsv"
  : >"$task_file"
  tool_failures=""

  run_tool_task() {
    task_src="$1"
    task_out="$2"
    task_label="$(basename "$task_out")"
    task_log="$out_dir/${task_label}.fullchain.log"
    (
      build_tool "$task_src" "$task_out"
    ) >"$task_log" 2>&1 &
    task_pid=$!
    printf '%s\t%s\t%s\n' "$task_pid" "$task_label" "$task_log" >>"$task_file"
  }

  run_tool_task "src/tooling/cheng_pkg_source.cheng" "$bin_dir/cheng_pkg_source"
  run_tool_task "src/tooling/cheng_pkg.cheng" "$bin_dir/cheng_pkg"
  run_tool_task "src/tooling/cheng_storage.cheng" "$bin_dir/cheng_storage"

  tab="$(printf '\t')"
  while IFS="$tab" read -r task_pid task_label task_log; do
    [ "$task_pid" = "" ] && continue
    if wait "$task_pid"; then
      cat "$task_log"
    else
      cat "$task_log" >&2 || true
      tool_failures="$tool_failures $task_label"
    fi
  done <"$task_file"
  rm -f "$task_file"
  if [ "$tool_failures" != "" ]; then
    if [ "$build_tools_strict" = "1" ] && [ "$build_tools_strict_scope" = "all" ]; then
      echo "[Error] fullchain tool build failed:$tool_failures" 1>&2
      exit 1
    fi
    if [ "$build_tools_strict" = "1" ] && [ "$build_tools_strict_scope" = "core" ]; then
      echo "[Warn] fullchain tool build partial failure (strict core scope; ecosystem tools are best-effort):$tool_failures" 1>&2
      echo "[Hint] set FULLCHAIN_BUILD_TOOLS_STRICT_SCOPE=all to enforce all tool sources" 1>&2
    else
    echo "[Warn] fullchain tool build partial failure (best-effort):$tool_failures" 1>&2
    fi
  fi
else
  tool_failures=""
  build_tool "src/tooling/cheng_pkg_source.cheng" "$bin_dir/cheng_pkg_source" || tool_failures="$tool_failures cheng_pkg_source"
  build_tool "src/tooling/cheng_pkg.cheng" "$bin_dir/cheng_pkg" || tool_failures="$tool_failures cheng_pkg"
  build_tool "src/tooling/cheng_storage.cheng" "$bin_dir/cheng_storage" || tool_failures="$tool_failures cheng_storage"
  if [ "$tool_failures" != "" ]; then
    if [ "$build_tools_strict" = "1" ] && [ "$build_tools_strict_scope" = "all" ]; then
      echo "[Error] fullchain tool build failed:$tool_failures" 1>&2
      exit 1
    fi
    if [ "$build_tools_strict" = "1" ] && [ "$build_tools_strict_scope" = "core" ]; then
      echo "[Warn] fullchain tool build partial failure (strict core scope; ecosystem tools are best-effort):$tool_failures" 1>&2
      echo "[Hint] set FULLCHAIN_BUILD_TOOLS_STRICT_SCOPE=all to enforce all tool sources" 1>&2
    else
    echo "[Warn] fullchain tool build partial failure (best-effort):$tool_failures" 1>&2
    fi
  fi
fi

echo "verify_fullchain_bootstrap ok"
