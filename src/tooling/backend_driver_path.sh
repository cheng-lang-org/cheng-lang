#!/usr/bin/env sh
. "$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)/env_prefix_bridge.sh"
set -eu

root="$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)"
cd "$root"

to_abs() {
  p="$1"
  case "$p" in
    /*)
      ;; 
    *)
      p="$root/$p"
      ;;
  esac
  d="$(CDPATH= cd -- "$(dirname -- "$p")" && pwd)"
  printf "%s/%s\n" "$d" "$(basename -- "$p")"
}

run_with_timeout() {
  seconds="$1"
  shift
  perl -e '
    use POSIX qw(setsid WNOHANG);
    my $timeout = shift;
    my $pid = fork();
    if (!defined $pid) { exit 127; }
    if ($pid == 0) {
      setsid();
      exec @ARGV;
      exit 127;
    }
    my $end = time + $timeout;
    while (1) {
      my $res = waitpid($pid, WNOHANG);
      if ($res == $pid) {
        my $status = $?;
        if (($status & 127) != 0) {
          exit(128 + ($status & 127));
        }
        exit($status >> 8);
      }
      if (time >= $end) {
        kill "TERM", -$pid;
        select(undef, undef, undef, 0.5);
        kill "KILL", -$pid;
        exit 124;
      }
      select(undef, undef, undef, 0.1);
    }
  ' "$seconds" "$@"
}

detect_smoke_target() {
  t="$(sh src/tooling/detect_host_target.sh 2>/dev/null || true)"
  if [ "$t" = "" ]; then
    echo "auto"
    return
  fi
  echo "$t"
}

resolve_runtime_obj_for_target() {
  target="$1"
  if [ "${BACKEND_RUNTIME_OBJ:-}" != "" ]; then
    echo "${BACKEND_RUNTIME_OBJ}"
    return
  fi
  safe_target="$(printf '%s' "$target" | tr -c 'A-Za-z0-9._-' '_' | tr -s '_')"
  echo "$root/chengcache/system_helpers.backend.cheng.${safe_target}.o"
}

driver_sanity_ok() {
  bin="$1"
  if [ ! -x "$bin" ]; then
    return 1
  fi
  set +e
  run_with_timeout 5 "$bin" --help >/dev/null 2>&1
  status=$?
  set -e
  case "$status" in
    0|1|2) return 0 ;;
  esac
  return 1
}

driver_compile_smoke_ok() {
  bin="$1"
  if [ ! -x "$bin" ]; then
    return 1
  fi
  smoke_src="$root/tests/cheng/backend/fixtures/return_add.cheng"
  smoke_dict_src="$root/tests/cheng/backend/fixtures/return_spawn_default_thread_entry_gate.cheng"
  if [ ! -f "$smoke_src" ]; then
    return 0
  fi
  if [ ! -f "$smoke_dict_src" ]; then
    return 0
  fi
  smoke_out="$root/chengcache/.backend_driver_path.smoke.bin"
  smoke_out_stage1="$root/chengcache/.backend_driver_path.smoke.stage1.bin"
  smoke_out_stage1_dict="$root/chengcache/.backend_driver_path.smoke.stage1.dict.bin"
  smoke_stage1="${BACKEND_DRIVER_PATH_STAGE1_STRICT_SMOKE:-0}"
  smoke_stage1_dict="${BACKEND_DRIVER_PATH_STAGE1_DICT_SMOKE:-0}"
  smoke_timeout="${BACKEND_DRIVER_PATH_SMOKE_TIMEOUT:-12}"
  smoke_target="$(detect_smoke_target)"
  smoke_runtime_obj="$(resolve_runtime_obj_for_target "$smoke_target")"
  if [ ! -f "$smoke_runtime_obj" ]; then
    return 1
  fi
  mkdir -p "$root/chengcache"
  rm -f "$smoke_out"
  rm -f "$smoke_out_stage1"
  rm -f "$smoke_out_stage1_dict"
  set +e
  run_with_timeout "$smoke_timeout" env \
    MM=orc \
    STAGE1_NO_POINTERS_NON_C_ABI=0 \
    STAGE1_NO_POINTERS_NON_C_ABI_INTERNAL=0 \
    BACKEND_VALIDATE=0 \
    STAGE1_SKIP_SEM=1 \
    GENERIC_MODE=dict \
    GENERIC_SPEC_BUDGET=0 \
    STAGE1_SKIP_OWNERSHIP=1 \
    BACKEND_LINKER=self \
    BACKEND_NO_RUNTIME_C=1 \
    BACKEND_RUNTIME_OBJ="$smoke_runtime_obj" \
    BACKEND_EMIT=exe \
    BACKEND_TARGET="$smoke_target" \
    BACKEND_FRONTEND=stage1 \
    BACKEND_INPUT="$smoke_src" \
    BACKEND_OUTPUT="$smoke_out" \
    "$bin" >/dev/null 2>&1
  status=$?
  set -e
  if [ "$status" -ne 0 ]; then
    return 1
  fi
  if [ ! -s "$smoke_out" ]; then
    return 1
  fi
  if [ "$smoke_stage1" = "1" ]; then
    set +e
    run_with_timeout 30 env \
      MM=orc \
      STAGE1_NO_POINTERS_NON_C_ABI=0 \
      STAGE1_NO_POINTERS_NON_C_ABI_INTERNAL=0 \
      BACKEND_VALIDATE=0 \
      STAGE1_SKIP_SEM=0 \
      GENERIC_MODE=hybrid \
      GENERIC_SPEC_BUDGET=0 \
      STAGE1_SKIP_OWNERSHIP=0 \
      BACKEND_LINKER=self \
      BACKEND_NO_RUNTIME_C=1 \
      BACKEND_RUNTIME_OBJ="$smoke_runtime_obj" \
      BACKEND_EMIT=exe \
      BACKEND_TARGET="$smoke_target" \
      BACKEND_FRONTEND=stage1 \
      BACKEND_INPUT="$smoke_src" \
      BACKEND_OUTPUT="$smoke_out_stage1" \
      "$bin" >/dev/null 2>&1
    status_stage1=$?
    set -e
    if [ "$status_stage1" -ne 0 ]; then
      return 1
    fi
    [ -s "$smoke_out_stage1" ] || return 1
  fi
  if [ "$smoke_stage1_dict" = "1" ]; then
    set +e
    run_with_timeout 60 env \
      MM=orc \
      STAGE1_NO_POINTERS_NON_C_ABI=0 \
      STAGE1_NO_POINTERS_NON_C_ABI_INTERNAL=0 \
      BACKEND_VALIDATE=0 \
      STAGE1_SKIP_SEM=0 \
      GENERIC_MODE=dict \
      GENERIC_SPEC_BUDGET=0 \
      STAGE1_SKIP_OWNERSHIP=1 \
      BACKEND_LINKER=self \
      BACKEND_NO_RUNTIME_C=1 \
      BACKEND_RUNTIME_OBJ="$smoke_runtime_obj" \
      BACKEND_EMIT=exe \
      BACKEND_TARGET="$smoke_target" \
      BACKEND_FRONTEND=stage1 \
      BACKEND_INPUT="$smoke_dict_src" \
      BACKEND_OUTPUT="$smoke_out_stage1_dict" \
      "$bin" >/dev/null 2>&1
    status_stage1_dict=$?
    set -e
    if [ "$status_stage1_dict" -ne 0 ]; then
      return 1
    fi
    [ -s "$smoke_out_stage1_dict" ] || return 1
  fi
  return 0
}

acquire_rebuild_lock() {
  lock_dir="$1"
  owner_file="$lock_dir/owner.pid"
  waits=0
  while ! mkdir "$lock_dir" 2>/dev/null; do
    if [ -f "$owner_file" ]; then
      owner="$(cat "$owner_file" 2>/dev/null || true)"
      if [ "$owner" != "" ] && ! kill -0 "$owner" 2>/dev/null; then
        rm -rf "$lock_dir" 2>/dev/null || true
        continue
      fi
    fi
    waits=$((waits + 1))
    if [ "$waits" -ge 2400 ]; then
      echo "[Error] backend_driver_path: timeout waiting rebuild lock: $lock_dir" 1>&2
      exit 1
    fi
    sleep 0.05
  done
  printf '%s\n' "$$" >"$owner_file"
}

release_rebuild_lock() {
  lock_dir="$1"
  owner_file="$lock_dir/owner.pid"
  if [ -f "$owner_file" ]; then
    owner="$(cat "$owner_file" 2>/dev/null || true)"
    if [ "$owner" = "$$" ]; then
      rm -rf "$lock_dir" 2>/dev/null || true
      return
    fi
  fi
}

driver="${BACKEND_DRIVER:-}"
explicit_driver_fallback="${BACKEND_DRIVER_ALLOW_FALLBACK:-0}"
if [ "$driver" != "" ]; then
  abs="$(to_abs "$driver")"
  if [ -x "$abs" ] && driver_sanity_ok "$abs" && driver_compile_smoke_ok "$abs"; then
    printf "%s\n" "$abs"
    exit 0
  fi
  if [ "$explicit_driver_fallback" != "1" ]; then
    if [ ! -x "$abs" ]; then
      echo "[Error] BACKEND_DRIVER is not executable: $abs" 1>&2
    else
      echo "[Error] BACKEND_DRIVER is not runnable: $abs" 1>&2
    fi
    exit 1
  fi
  if [ ! -x "$abs" ]; then
    echo "[Warn] BACKEND_DRIVER is not executable, fallback auto-select: $abs" 1>&2
  else
    echo "[Warn] BACKEND_DRIVER is not runnable, fallback auto-select: $abs" 1>&2
  fi
  driver=""
fi

is_stale() {
  bin="$1"
  if [ ! -x "$bin" ]; then
    return 0
  fi
  if find "$root/src/backend" "$root/src/stage1" "$root/src/std" "$root/src/core" "$root/src/system" \
      -type f \( -name '*.cheng' -o -name '*.c' -o -name '*.h' \) \
      -newer "$bin" -print -quit | grep -q .; then
    return 0
  fi
  return 1
}

find_fallback_driver() {
  allow_selfhost_fallback="${BACKEND_DRIVER_PATH_ALLOW_SELFHOST:-0}"
  for cand in \
    "$abs_default" \
    "$root/artifacts/backend_driver/cheng.fixed3" \
    "$root/artifacts/backend_seed/cheng.stage2" \
    "$root/dist/releases/current/cheng" \
    "$wrapper_driver"; do
    if [ -x "$cand" ] && driver_sanity_ok "$cand" && driver_compile_smoke_ok "$cand"; then
      printf "%s\n" "$cand"
      return 0
    fi
  done
  if [ "$allow_selfhost_fallback" != "1" ]; then
    return 1
  fi
  for cand in \
    "$root/artifacts/backend_selfhost_self_obj/cheng.stage2" \
    "$root/artifacts/backend_selfhost_self_obj/cheng.stage1"; do
    if [ -x "$cand" ] && driver_sanity_ok "$cand" && driver_compile_smoke_ok "$cand"; then
      printf "%s\n" "$cand"
      return 0
    fi
  done
  return 1
}

local_driver_rel="${BACKEND_LOCAL_DRIVER_REL:-artifacts/backend_driver/cheng}"
abs_default="$root/$local_driver_rel"
wrapper_driver="${BACKEND_WRAPPER_DRIVER:-$root/src/tooling/backend_driver_exec.sh}"
wrapper_enabled="${BACKEND_DRIVER_PATH_USE_WRAPPER:-0}"
prefer_rebuild="${BACKEND_DRIVER_PATH_PREFER_REBUILD:-0}"
fallback_driver=""
set +e
fallback_driver="$(find_fallback_driver)"
fallback_status=$?
set -e
if [ "$fallback_status" -ne 0 ]; then
  fallback_driver=""
fi

# Wrapper is now a fallback-only safety net; default production path should
# converge on artifacts/backend_driver/cheng.
if [ "$wrapper_enabled" = "1" ] && [ -x "$wrapper_driver" ] && driver_sanity_ok "$wrapper_driver" && driver_compile_smoke_ok "$wrapper_driver"; then
  printf "%s\n" "$wrapper_driver"
  exit 0
fi

# Fast path: use the local rebuilt driver if it's fresh.
if [ -x "$abs_default" ] && ! is_stale "$abs_default"; then
  if driver_sanity_ok "$abs_default" && driver_compile_smoke_ok "$abs_default"; then
    printf "%s\n" "$abs_default"
    exit 0
  fi
fi

rebuild_default="0"
if [ ! -x "$abs_default" ]; then
  rebuild_default="1"
else
  if is_stale "$abs_default"; then
    rebuild_default="1"
  elif ! driver_sanity_ok "$abs_default" || ! driver_compile_smoke_ok "$abs_default"; then
    rebuild_default="1"
  fi
fi

if [ "$rebuild_default" = "1" ] && [ "$prefer_rebuild" != "1" ] && [ "$fallback_driver" != "" ]; then
  printf "%s\n" "$fallback_driver"
  exit 0
fi

if [ "$rebuild_default" = "1" ]; then
  mkdir -p "$root/chengcache"
  rebuild_lock="$root/chengcache/.backend_driver_path.lock"
  acquire_rebuild_lock "$rebuild_lock"
  release_lock_on_exit() {
    release_rebuild_lock "$rebuild_lock"
  }
  trap release_lock_on_exit EXIT INT TERM
  rebuild_default="0"
  if [ ! -x "$abs_default" ]; then
    rebuild_default="1"
  else
    if is_stale "$abs_default"; then
      rebuild_default="1"
    elif ! driver_sanity_ok "$abs_default" || ! driver_compile_smoke_ok "$abs_default"; then
      rebuild_default="1"
    fi
  fi
fi

if [ "$rebuild_default" = "1" ]; then
  abs_default_dir="$(dirname "$abs_default")"
  if [ "$abs_default_dir" != "" ] && [ ! -d "$abs_default_dir" ]; then
    mkdir -p "$abs_default_dir"
  fi
  rebuild_log="$root/chengcache/backend_driver_path.rebuild.log"
  set +e
  BACKEND_BUILD_DRIVER_SELFHOST=1 \
  BACKEND_BUILD_DRIVER_LINKER="${BACKEND_DRIVER_PATH_LINKER:-system}" \
    sh src/tooling/build_backend_driver.sh --name:"$local_driver_rel" >"$rebuild_log" 2>&1
  rebuild_status="$?"
  set -e
  if [ "$rebuild_status" -ne 0 ]; then
    echo "[Warn] backend_driver_path: local driver rebuild failed (status=$rebuild_status)" 1>&2
    if [ "${BACKEND_DRIVER_PATH_DEBUG:-0}" = "1" ]; then
      tail -n 80 "$rebuild_log" 1>&2 || true
    fi
  fi
fi
if [ "${rebuild_lock:-}" != "" ]; then
  release_rebuild_lock "$rebuild_lock"
  trap - EXIT INT TERM
fi
if [ -x "$abs_default" ] && driver_sanity_ok "$abs_default" && driver_compile_smoke_ok "$abs_default"; then
  printf "%s\n" "$abs_default"
  exit 0
fi

if [ "$fallback_driver" = "" ]; then
  set +e
  fallback_driver="$(find_fallback_driver)"
  fallback_status=$?
  set -e
  if [ "$fallback_status" -ne 0 ]; then
    fallback_driver=""
  fi
fi
if [ "$fallback_driver" != "" ]; then
  printf "%s\n" "$fallback_driver"
  exit 0
fi

if [ ! -x "$abs_default" ]; then
  echo "[Error] missing backend driver: $abs_default" 1>&2
else
  echo "[Error] backend driver exists but failed compile-smoke: $abs_default" 1>&2
fi
echo "  hint: run src/tooling/build_backend_driver.sh --name:$local_driver_rel" 1>&2
exit 1
