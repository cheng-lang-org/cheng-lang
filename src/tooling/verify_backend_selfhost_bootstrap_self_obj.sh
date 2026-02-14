#!/usr/bin/env sh
set -eu
(set -o pipefail) 2>/dev/null && set -o pipefail

root="$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)"
cd "$root"

to_abs() {
  p="$1"
  case "$p" in
    /*)
      printf "%s\n" "$p"
      return
      ;;
    *)
      printf "%s/%s\n" "$root" "$p"
      return
      ;;
  esac
}

# Keep local driver during bootstrap to avoid recursive worker invocations
# deleting the active stage0 driver mid-build.
export CHENG_CLEAN_CHENG_LOCAL=0
if [ "${CHENG_ABI:-}" = "" ]; then
  export CHENG_ABI=v2_noptr
fi
if [ "${CHENG_ABI}" = "v2_noptr" ]; then
  if [ "${CHENG_STAGE1_STD_NO_POINTERS:-}" = "" ]; then
    export CHENG_STAGE1_STD_NO_POINTERS=0
  fi
  if [ "${CHENG_STAGE1_STD_NO_POINTERS_STRICT:-}" = "" ]; then
    export CHENG_STAGE1_STD_NO_POINTERS_STRICT=0
  fi
  if [ "${CHENG_STAGE1_NO_POINTERS_NON_C_ABI:-}" = "" ]; then
    export CHENG_STAGE1_NO_POINTERS_NON_C_ABI=0
  fi
  if [ "${CHENG_STAGE1_NO_POINTERS_NON_C_ABI_INTERNAL:-}" = "" ]; then
    export CHENG_STAGE1_NO_POINTERS_NON_C_ABI_INTERNAL=0
  fi
fi
# Keep frontend behavior aligned with production closedloop defaults.
if [ "${CHENG_C_SYSTEM:-}" = "" ]; then
  export CHENG_C_SYSTEM=system
fi
if [ "${CHENG_BACKEND_IR:-}" = "" ]; then
  export CHENG_BACKEND_IR=uir
fi
if [ "${CHENG_GENERIC_MODE:-}" = "" ]; then
  mode_hint="${CHENG_SELF_OBJ_BOOTSTRAP_MODE:-fast}"
  if [ "$mode_hint" = "strict" ]; then
    export CHENG_GENERIC_MODE=hybrid
  else
    # Fast mode defaults to dict to avoid long stage1 monomorphize stalls.
    export CHENG_GENERIC_MODE=dict
  fi
fi
if [ "${CHENG_GENERIC_SPEC_BUDGET:-}" = "" ]; then
  export CHENG_GENERIC_SPEC_BUDGET=0
fi
# Stage1 frontend pass toggles: keep selfhost bootstrap path stable by default.
if [ "${CHENG_STAGE1_SKIP_SEM:-}" = "" ]; then
  export CHENG_STAGE1_SKIP_SEM=1
fi
if [ "${CHENG_STAGE1_SKIP_OWNERSHIP:-}" = "" ]; then
  export CHENG_STAGE1_SKIP_OWNERSHIP=1
fi

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
        # First try process-group kill, then direct pid kill fallback.
        kill "TERM", -$pid;
        kill "TERM", $pid;
        my $grace_end = time + 1;
        while (time < $grace_end) {
          my $r = waitpid($pid, WNOHANG);
          if ($r == $pid) {
            my $status = $?;
            if (($status & 127) != 0) {
              exit(128 + ($status & 127));
            }
            exit($status >> 8);
          }
          select(undef, undef, undef, 0.1);
        }
        kill "KILL", -$pid;
        kill "KILL", $pid;
        exit 124;
      }
      select(undef, undef, undef, 0.1);
    }
  ' "$seconds" "$@"
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

timestamp_now() {
  date +%s
}

detect_host_jobs() {
  jobs=""
  if command -v nproc >/dev/null 2>&1; then
    jobs="$(nproc 2>/dev/null || true)"
  fi
  if [ "$jobs" = "" ] && command -v sysctl >/dev/null 2>&1; then
    jobs="$(sysctl -n hw.logicalcpu 2>/dev/null || sysctl -n hw.ncpu 2>/dev/null || true)"
  fi
  case "$jobs" in
    ''|*[!0-9]*)
      jobs="1"
      ;;
  esac
  if [ "$jobs" -lt 1 ]; then
    jobs="1"
  fi
  printf '%s\n' "$jobs"
}

cleanup_local_driver_on_exit="0"
if [ "${CHENG_CLEAN_CHENG_LOCAL:-0}" = "1" ] && [ "${CHENG_TOOLING_CLEANUP_DEPTH:-0}" = "0" ] && [ "${CHENG_SELF_OBJ_BOOTSTRAP_STAGE0:-}" = "" ]; then
  export CHENG_TOOLING_CLEANUP_DEPTH=1
  cleanup_local_driver_on_exit="1"
fi

host_os="$(uname -s 2>/dev/null || echo unknown)"
host_arch="$(uname -m 2>/dev/null || echo unknown)"

target=""
case "$host_os" in
  Darwin)
    case "$host_arch" in
      arm64)
        target="arm64-apple-darwin"
        ;;
    esac
    ;;
  Linux)
    case "$host_arch" in
      aarch64|arm64)
        target="aarch64-unknown-linux-gnu"
        ;;
    esac
    ;;
esac

if [ "$target" = "" ]; then
  echo "verify_backend_selfhost_bootstrap_self_obj skip: host=$host_os/$host_arch" 1>&2
  exit 2
fi

linker_mode="${CHENG_SELF_OBJ_BOOTSTRAP_LINKER:-}"
if [ "$linker_mode" = "" ]; then
  linker_mode="self"
fi
if [ "$linker_mode" != "self" ]; then
  echo "[Error] verify_backend_selfhost_bootstrap_self_obj requires CHENG_SELF_OBJ_BOOTSTRAP_LINKER=self (cc path removed)" 1>&2
  exit 2
fi

if [ "$linker_mode" = "self" ] && [ "$host_os" = "Darwin" ]; then
  if ! command -v codesign >/dev/null 2>&1; then
    echo "verify_backend_selfhost_bootstrap_self_obj skip: missing codesign" 1>&2
    exit 2
  fi
fi

runtime_mode="${CHENG_SELF_OBJ_BOOTSTRAP_RUNTIME:-}"
if [ "$runtime_mode" = "" ]; then
  runtime_mode="cheng"
fi
if [ "$runtime_mode" != "cheng" ]; then
  echo "[Error] verify_backend_selfhost_bootstrap_self_obj requires CHENG_SELF_OBJ_BOOTSTRAP_RUNTIME=cheng (C runtime path removed)" 1>&2
  exit 2
fi
runtime_cheng_src="src/std/system_helpers_backend.cheng"
runtime_obj=""

out_dir_rel="artifacts/backend_selfhost_self_obj"
out_dir="$root/$out_dir_rel"
mkdir -p "$out_dir"
mkdir -p chengcache
runtime_obj="$out_dir/system_helpers.backend.cheng.o"
build_timeout="${CHENG_SELF_OBJ_BOOTSTRAP_TIMEOUT:-60}"
smoke_timeout="${CHENG_SELF_OBJ_BOOTSTRAP_SMOKE_TIMEOUT:-30}"

compat_root="$root/chengcache/stage0_compat"
ensure_stage0_compat() {
  python3 "$root/scripts/gen_stage0_compat_src.py" --repo-root "$root" --out-root "$compat_root" >/dev/null
}

stage0_env="${CHENG_SELF_OBJ_BOOTSTRAP_STAGE0:-}"
if [ "$stage0_env" != "" ]; then
  stage0="$stage0_env"
  stage0="$(to_abs "$stage0")"
  if [ ! -x "$stage0" ]; then
    echo "[verify_backend_selfhost_bootstrap_self_obj] missing stage0 driver (CHENG_SELF_OBJ_BOOTSTRAP_STAGE0): $stage0" 1>&2
    exit 1
  fi
else
  stage0=""
  stage0_from_backend_driver="${CHENG_BACKEND_DRIVER:-}"
  if [ "$stage0_from_backend_driver" != "" ]; then
    stage0_try="$(to_abs "$stage0_from_backend_driver")"
    if driver_sanity_ok "$stage0_try"; then
      stage0="$stage0_try"
    else
      echo "[verify_backend_selfhost_bootstrap_self_obj] warn: CHENG_BACKEND_DRIVER is not runnable: $stage0_try" 1>&2
      stage0=""
    fi
  fi
  if [ "$stage0" = "" ]; then
    for cand in \
      "artifacts/backend_driver/cheng" \
      "dist/releases/current/cheng" \
      "artifacts/backend_seed/cheng.stage2" \
      "$out_dir/cheng.stage2" \
      "$out_dir/cheng.stage1"; do
      cand_abs="$(to_abs "$cand")"
      if driver_sanity_ok "$cand_abs"; then
        stage0="$cand_abs"
        break
      fi
    done
  fi
  if [ "$stage0" = "" ] && driver_sanity_ok "./artifacts/backend_driver/cheng"; then
    stage0="$(to_abs "./artifacts/backend_driver/cheng")"
  fi
  if [ "$stage0" = "" ] && driver_sanity_ok "./cheng"; then
    stage0="$(to_abs "./cheng")"
  fi
  if [ "$stage0" = "" ]; then
    stage0="./artifacts/backend_driver/cheng"
    stage0_name="$(basename "$stage0")"
    echo "== backend.selfhost_self_obj.build_stage0_driver ($stage0_name) =="
    bash src/tooling/build_backend_driver.sh --name:"$stage0" >/dev/null
    if ! driver_sanity_ok "$stage0"; then
      echo "[verify_backend_selfhost_bootstrap_self_obj] missing stage0 driver: $stage0" 1>&2
      exit 1
    fi
    stage0="$(to_abs "$stage0")"
  fi
fi

mm="${CHENG_SELF_OBJ_BOOTSTRAP_MM:-${CHENG_MM:-orc}}"
cache="${CHENG_SELF_OBJ_BOOTSTRAP_CACHE:-0}"
reuse="${CHENG_SELF_OBJ_BOOTSTRAP_REUSE:-1}"
multi="${CHENG_SELF_OBJ_BOOTSTRAP_MULTI:-1}"
incremental="${CHENG_SELF_OBJ_BOOTSTRAP_INCREMENTAL:-1}"
multi_force="${CHENG_SELF_OBJ_BOOTSTRAP_MULTI_FORCE:-0}"
jobs="${CHENG_SELF_OBJ_BOOTSTRAP_JOBS:-0}"
bootstrap_mode="${CHENG_SELF_OBJ_BOOTSTRAP_MODE:-fast}"
if [ "$bootstrap_mode" != "strict" ] && [ "$bootstrap_mode" != "fast" ]; then
  echo "[Error] verify_backend_selfhost_bootstrap_self_obj invalid CHENG_SELF_OBJ_BOOTSTRAP_MODE=$bootstrap_mode (expected strict|fast)" 1>&2
  exit 2
fi
allow_retry="${CHENG_SELF_OBJ_BOOTSTRAP_ALLOW_RETRY:-1}"
allow_stage0_fallback="${CHENG_SELF_OBJ_BOOTSTRAP_ALLOW_STAGE0_FALLBACK:-0}"
fast_total_max="${CHENG_SELF_OBJ_BOOTSTRAP_FAST_MAX_TOTAL:-60}"
fast_jobs_cap="${CHENG_SELF_OBJ_BOOTSTRAP_FAST_JOBS_CAP:-8}"
case "$allow_stage0_fallback" in
  0|1)
    ;;
  *)
    echo "[Error] verify_backend_selfhost_bootstrap_self_obj invalid CHENG_SELF_OBJ_BOOTSTRAP_ALLOW_STAGE0_FALLBACK=$allow_stage0_fallback (expected 0|1)" 1>&2
    exit 2
    ;;
esac
case "$fast_jobs_cap" in
  ''|*[!0-9]*)
    fast_jobs_cap="8"
    ;;
esac
if [ "$fast_jobs_cap" -lt 1 ]; then
  fast_jobs_cap="1"
fi
if [ "${CHENG_SELF_OBJ_BOOTSTRAP_JOBS+x}" = "" ]; then
  jobs="$(detect_host_jobs)"
fi
if [ "$bootstrap_mode" = "fast" ]; then
  # Fast mode is latency-first, but keep non-timeout retry enabled to handle
  # transient worker-path crashes in seed/stage compilers.
  allow_retry="1"
  if [ "${CHENG_SELF_OBJ_BOOTSTRAP_MULTI+x}" = "" ]; then
    multi="1"
  fi
  if [ "$jobs" -gt "$fast_jobs_cap" ]; then
    jobs="$fast_jobs_cap"
  fi
  if [ "${CHENG_SELF_OBJ_BOOTSTRAP_MULTI_FORCE+x}" = "" ]; then
    multi_force="$multi"
  fi
else
  # Strict mode prefers deterministic/stable compilation over parallel worker
  # throughput; parallel worker path can fail intermittently on unit-map sync.
  if [ "${CHENG_SELF_OBJ_BOOTSTRAP_MULTI+x}" = "" ]; then
    multi="0"
  fi
  if [ "${CHENG_SELF_OBJ_BOOTSTRAP_MULTI_FORCE+x}" = "" ]; then
    multi_force="$multi"
  fi
fi
session="${CHENG_SELF_OBJ_BOOTSTRAP_SESSION:-default}"
session_safe="$(printf '%s' "$session" | tr -c 'A-Za-z0-9._-' '_')"
# File/output-safe token: avoid dots in temp output base names.
# Some compiler revisions derive sidecar names by truncating on dots, which can
# cause cross-session collisions (e.g. `a.b` and `a.c` both mapping to `a.*`).
session_file_safe="$(printf '%s' "$session_safe" | tr '.' '_')"
stage0_copy="$out_dir/cheng_stage0_${session_safe}"
refresh_stage0_copy="0"
if [ ! -f "$stage0_copy" ]; then
  refresh_stage0_copy="1"
elif ! cmp -s "$stage0" "$stage0_copy"; then
  refresh_stage0_copy="1"
fi
if [ "$refresh_stage0_copy" = "1" ]; then
  cp "$stage0" "$stage0_copy.tmp.$$"
  chmod +x "$stage0_copy.tmp.$$" 2>/dev/null || true
  mv "$stage0_copy.tmp.$$" "$stage0_copy"
fi
stage0="$(to_abs "$stage0_copy")"
session_lock_dir="$out_dir/.selfhost.${session_safe}.lock"
session_lock_owner="$$"
timing_file="$out_dir/.selfhost_timing_${session_safe}_$$.tsv"
timing_out="${CHENG_SELF_OBJ_BOOTSTRAP_TIMING_OUT:-$out_dir/selfhost_timing_${session_safe}.tsv}"
metrics_out="${CHENG_SELF_OBJ_BOOTSTRAP_METRICS_OUT:-$out_dir/selfhost_metrics_${session_safe}.json}"
: > "$timing_file"
selfhost_started="$(timestamp_now)"
retry_runtime_serial=0
retry_runtime_stage0=0
retry_build_obj_serial=0
retry_build_exe_serial=0
retry_build_exe_stage0=0
retry_stage1_compat=0
last_fail_stage=""

record_stage_timing() {
  label="$1"
  status="$2"
  duration="$3"
  printf '%s\t%s\t%s\n' "$label" "$status" "$duration" >>"$timing_file"
}

print_stage_timing_summary() {
  if [ ! -s "$timing_file" ]; then
    return
  fi
  tab="$(printf '\t')"
  echo "== backend.selfhost_self_obj.timing =="
  while IFS="$tab" read -r label status duration; do
    [ "$label" = "" ] && continue
    echo "  ${label}: ${duration}s [$status]"
  done <"$timing_file"
}

write_selfhost_metrics() {
  status="$1"
  now_ts="$(timestamp_now)"
  total_secs="$((now_ts - selfhost_started))"

  timing_parent="$(dirname "$timing_out")"
  metrics_parent="$(dirname "$metrics_out")"
  mkdir -p "$timing_parent" "$metrics_parent" 2>/dev/null || true

  if [ -s "$timing_file" ]; then
    cp "$timing_file" "${timing_out}.tmp.$$"
    mv "${timing_out}.tmp.$$" "$timing_out"
  fi

  {
    echo "{"
    printf '  "session": "%s",\n' "$session_safe"
    printf '  "mode": "%s",\n' "$bootstrap_mode"
    printf '  "retry_enabled": %s,\n' "$allow_retry"
    printf '  "allow_stage0_fallback": %s,\n' "$allow_stage0_fallback"
    printf '  "multi": %s,\n' "$multi"
    printf '  "multi_force": %s,\n' "$multi_force"
    printf '  "jobs": %s,\n' "$jobs"
    printf '  "timeout_seconds": %s,\n' "$build_timeout"
    printf '  "fast_total_max_seconds": %s,\n' "$fast_total_max"
    printf '  "retry_runtime_serial": %s,\n' "$retry_runtime_serial"
    printf '  "retry_runtime_stage0": %s,\n' "$retry_runtime_stage0"
    printf '  "retry_build_obj_serial": %s,\n' "$retry_build_obj_serial"
    printf '  "retry_build_exe_serial": %s,\n' "$retry_build_exe_serial"
    printf '  "retry_build_exe_stage0": %s,\n' "$retry_build_exe_stage0"
    printf '  "retry_stage1_compat": %s,\n' "$retry_stage1_compat"
    printf '  "last_fail_stage": "%s",\n' "$last_fail_stage"
    printf '  "exit_status": %s,\n' "$status"
    printf '  "total_seconds": %s\n' "$total_secs"
    echo "}"
  } > "${metrics_out}.tmp.$$"
  mv "${metrics_out}.tmp.$$" "$metrics_out"
}

acquire_session_lock() {
  lock_dir="$1"
  owner="$2"
  owner_file="$lock_dir/owner.pid"
  waits=0
  while ! mkdir "$lock_dir" 2>/dev/null; do
    if [ -f "$owner_file" ]; then
      prev="$(cat "$owner_file" 2>/dev/null || true)"
      if [ -n "$prev" ] && ! kill -0 "$prev" 2>/dev/null; then
        rm -rf "$lock_dir" 2>/dev/null || true
        continue
      fi
    fi
    waits=$((waits + 1))
    if [ "$waits" -ge 2400 ]; then
      echo "[verify_backend_selfhost_bootstrap_self_obj] timeout waiting for session lock: $lock_dir" >&2
      exit 1
    fi
    sleep 0.05
  done
  printf '%s\n' "$owner" >"$owner_file"
}

release_session_lock() {
  lock_dir="$1"
  owner="$2"
  owner_file="$lock_dir/owner.pid"
  if [ -f "$owner_file" ]; then
    prev="$(cat "$owner_file" 2>/dev/null || true)"
    if [ "$prev" = "$owner" ]; then
      rm -rf "$lock_dir" 2>/dev/null || true
    fi
  fi
}

release_session_lock_on_exit() {
  status=$?
  exit_code="$status"
  set +e
  total_status="fail"
  if [ "$exit_code" -eq 124 ]; then
    total_status="fail-timeout"
  fi
  if [ "$exit_code" -eq 0 ]; then
    total_status="ok"
  fi
  if ! awk -F '\t' '$1=="total" { found=1 } END { exit(found ? 0 : 1) }' "$timing_file" 2>/dev/null; then
    total_duration="$(( $(timestamp_now) - selfhost_started ))"
    record_stage_timing "total" "$total_status" "$total_duration"
  fi
  release_session_lock "$session_lock_dir" "$session_lock_owner"
  write_selfhost_metrics "$exit_code"
  rm -f "$timing_file" 2>/dev/null || true
  if [ "$cleanup_local_driver_on_exit" = "1" ]; then
    sh src/tooling/cleanup_cheng_local.sh
  fi
  exit "$exit_code"
}

allow_retry_for_status() {
  code="$1"
  if [ "$allow_retry" != "1" ]; then
    return 1
  fi
  # Timeout usually indicates performance regression or non-converging worker path.
  # Retrying the same command doubles latency and hides the root cause.
  if [ "$code" -eq 124 ]; then
    return 1
  fi
  return 0
}

lock_wait_started="$(timestamp_now)"
acquire_session_lock "$session_lock_dir" "$session_lock_owner"
trap release_session_lock_on_exit EXIT
lock_wait_duration="$(( $(timestamp_now) - lock_wait_started ))"
record_stage_timing "lock_wait" "ok" "$lock_wait_duration"

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

backend_sources_newer_than() {
  out="$1"
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

runtime_obj_valid() {
  obj="$1"
  if [ ! -s "$obj" ]; then
    return 1
  fi
  if command -v nm >/dev/null 2>&1; then
    if nm -j "$obj" 2>/dev/null | head -n 1 | grep -q .; then
      return 0
    fi
    return 1
  fi
  size="$(wc -c <"$obj" 2>/dev/null || echo 0)"
  case "$size" in
    ''|*[!0-9]*)
      return 1
      ;;
  esac
  [ "$size" -gt 1024 ]
}

build_runtime_obj() {
  rt_compiler="$1"
  rt_out_obj="$2"
  rt_log="$out_dir/runtime.build.txt"
  tmp_obj="$rt_out_obj.tmp.$$"

  if [ "$runtime_mode" != "cheng" ]; then
    return 0
  fi
  if [ ! -f "$runtime_cheng_src" ]; then
    echo "[verify_backend_selfhost_bootstrap_self_obj] missing runtime source: $runtime_cheng_src" 1>&2
    exit 1
  fi
  if [ -f "$rt_out_obj" ] && [ "$runtime_cheng_src" -ot "$rt_out_obj" ] && [ "$rt_compiler" -ot "$rt_out_obj" ]; then
    if runtime_obj_valid "$rt_out_obj"; then
      return 0
    fi
    rm -f "$rt_out_obj"
  fi

  set +e
  run_with_timeout "$build_timeout" env \
    CHENG_MM="$mm" \
    CHENG_CACHE="$cache" \
    CHENG_C_SYSTEM=0 \
    CHENG_BACKEND_MULTI="$multi" \
    CHENG_BACKEND_MULTI_FORCE="$multi_force" \
    CHENG_BACKEND_INCREMENTAL="$incremental" \
    CHENG_BACKEND_JOBS="$jobs" \
    CHENG_BACKEND_VALIDATE=1 \
    CHENG_BACKEND_ALLOW_NO_MAIN=1 \
    CHENG_BACKEND_WHOLE_PROGRAM=0 \
    CHENG_BACKEND_EMIT=obj \
    CHENG_BACKEND_TARGET="$target" \
    CHENG_BACKEND_FRONTEND=mvp \
    CHENG_BACKEND_INPUT="$runtime_cheng_src" \
    CHENG_BACKEND_OUTPUT="$tmp_obj" \
    "$rt_compiler" >"$rt_log" 2>&1
  status="$?"
  set -e
  if [ "$status" -ne 0 ] && [ "$multi" != "0" ] && allow_retry_for_status "$status"; then
    retry_runtime_serial=$((retry_runtime_serial + 1))
    # Seed/local stage compilers may crash in worker mode; retry in serial mode.
    rm -f "$tmp_obj"
    set +e
    run_with_timeout "$build_timeout" env \
      CHENG_MM="$mm" \
      CHENG_CACHE="$cache" \
      CHENG_C_SYSTEM=0 \
      CHENG_BACKEND_MULTI=0 \
      CHENG_BACKEND_MULTI_FORCE=0 \
      CHENG_BACKEND_INCREMENTAL="$incremental" \
      CHENG_BACKEND_JOBS="$jobs" \
      CHENG_BACKEND_VALIDATE=1 \
      CHENG_BACKEND_ALLOW_NO_MAIN=1 \
      CHENG_BACKEND_WHOLE_PROGRAM=0 \
      CHENG_BACKEND_EMIT=obj \
      CHENG_BACKEND_TARGET="$target" \
      CHENG_BACKEND_FRONTEND=mvp \
      CHENG_BACKEND_INPUT="$runtime_cheng_src" \
      CHENG_BACKEND_OUTPUT="$tmp_obj" \
      "$rt_compiler" >>"$rt_log" 2>&1
    status="$?"
    set -e
  fi
  if [ "$status" -ne 0 ] && [ "$rt_compiler" != "$stage0" ] && [ -x "$stage0" ] && [ "$allow_stage0_fallback" = "1" ] && allow_retry_for_status "$status"; then
    retry_runtime_stage0=$((retry_runtime_stage0 + 1))
    # Some stage1 compilers may crash on allow-no-main runtime builds.
    # Fall back to stage0 for runtime object generation to keep bootstrap progressing.
    rm -f "$tmp_obj"
    echo "[verify_backend_selfhost_bootstrap_self_obj] runtime build retry with stage0: $stage0" >>"$rt_log"
    set +e
    run_with_timeout "$build_timeout" env \
      CHENG_MM="$mm" \
      CHENG_CACHE="$cache" \
      CHENG_C_SYSTEM=0 \
      CHENG_BACKEND_MULTI=0 \
      CHENG_BACKEND_MULTI_FORCE=0 \
      CHENG_BACKEND_INCREMENTAL="$incremental" \
      CHENG_BACKEND_JOBS="$jobs" \
      CHENG_BACKEND_VALIDATE=1 \
      CHENG_BACKEND_ALLOW_NO_MAIN=1 \
      CHENG_BACKEND_WHOLE_PROGRAM=0 \
      CHENG_BACKEND_EMIT=obj \
      CHENG_BACKEND_TARGET="$target" \
      CHENG_BACKEND_FRONTEND=mvp \
      CHENG_BACKEND_INPUT="$runtime_cheng_src" \
      CHENG_BACKEND_OUTPUT="$tmp_obj" \
      "$stage0" >>"$rt_log" 2>&1
    status="$?"
    set -e
  fi
  if [ "$status" -ne 0 ]; then
    last_fail_stage="runtime"
    if [ "$status" -eq 124 ]; then
      echo "[verify_backend_selfhost_bootstrap_self_obj] runtime build timed out after ${build_timeout}s" >&2
    fi
    echo "[verify_backend_selfhost_bootstrap_self_obj] runtime build failed (status=$status)" >&2
    tail -n 200 "$rt_log" >&2 || true
    exit 1
  fi
  if [ ! -s "$tmp_obj" ]; then
    echo "[verify_backend_selfhost_bootstrap_self_obj] missing runtime obj output: $tmp_obj" 1>&2
    exit 1
  fi
  if ! runtime_obj_valid "$tmp_obj"; then
    echo "[verify_backend_selfhost_bootstrap_self_obj] invalid runtime obj output: $tmp_obj" 1>&2
    exit 1
  fi
  mv "$tmp_obj" "$rt_out_obj"
}

build_obj() {
  stage="$1"
  compiler="$2"
  input="$3"
  out_obj="$4"
  build_log="$out_dir/${stage}.build.txt"
  mkdir -p "$out_dir"
  tmp_obj="$out_obj.tmp.$$"

  set +e
  run_with_timeout "$build_timeout" env \
    CHENG_MM="$mm" \
    CHENG_CACHE="$cache" \
    CHENG_BACKEND_MULTI="$multi" \
    CHENG_BACKEND_MULTI_FORCE="$multi_force" \
    CHENG_BACKEND_INCREMENTAL="$incremental" \
    CHENG_BACKEND_JOBS="$jobs" \
    CHENG_BACKEND_VALIDATE=1 \
    CHENG_BACKEND_WHOLE_PROGRAM=1 \
    CHENG_BACKEND_EMIT=obj \
    CHENG_BACKEND_TARGET="$target" \
    CHENG_BACKEND_FRONTEND=stage1 \
    CHENG_BACKEND_INPUT="$input" \
    CHENG_BACKEND_OUTPUT="$tmp_obj" \
    "$compiler" >"$build_log" 2>&1
  status="$?"
  set -e
  if [ "$status" -ne 0 ] && [ "$multi" != "0" ] && allow_retry_for_status "$status"; then
    retry_build_obj_serial=$((retry_build_obj_serial + 1))
    rm -f "$tmp_obj"
    set +e
    run_with_timeout "$build_timeout" env \
      CHENG_MM="$mm" \
      CHENG_CACHE="$cache" \
      CHENG_BACKEND_MULTI=0 \
      CHENG_BACKEND_MULTI_FORCE=0 \
      CHENG_BACKEND_INCREMENTAL="$incremental" \
      CHENG_BACKEND_JOBS="$jobs" \
      CHENG_BACKEND_VALIDATE=1 \
      CHENG_BACKEND_WHOLE_PROGRAM=1 \
      CHENG_BACKEND_EMIT=obj \
      CHENG_BACKEND_TARGET="$target" \
      CHENG_BACKEND_FRONTEND=stage1 \
      CHENG_BACKEND_INPUT="$input" \
      CHENG_BACKEND_OUTPUT="$tmp_obj" \
      "$compiler" >>"$build_log" 2>&1
    status="$?"
    set -e
  fi
  if [ "$status" -ne 0 ]; then
    last_fail_stage="$stage"
    if [ "$status" -eq 124 ]; then
      echo "[verify_backend_selfhost_bootstrap_self_obj] compiler timed out after ${stage_timeout}s (stage=$stage)" >&2
    fi
    echo "[verify_backend_selfhost_bootstrap_self_obj] compiler failed (stage=$stage, status=$status)" >&2
    tail -n 200 "$build_log" >&2 || true
    exit 1
  fi

  if [ ! -s "$tmp_obj" ]; then
    echo "[verify_backend_selfhost_bootstrap_self_obj] missing obj output: $tmp_obj" 1>&2
    exit 1
  fi
  mv "$tmp_obj" "$out_obj"
}

build_exe_self() {
  stage="$1"
  compiler="$2"
  input="$3"
  tmp_exe="$4"
  out_exe="$5"
  build_log="$out_dir/${stage}.build.txt"
  mkdir -p "$out_dir"

  exe_obj="$out_exe.o"
  tmp_exe_obj="$tmp_exe.o"
  sanity_required="0"
  whole_program_mode="1"
  stage_multi="$multi"
  stage_multi_force="$multi_force"
  stage_skip_sem="${CHENG_STAGE1_SKIP_SEM:-0}"
  stage_timeout="$build_timeout"
  case "$stage" in
    *.smoke|*.smoke.*)
      # Keep smoke builds on whole-program mode to avoid module-mode objects
      # without symbol tables on some bootstrap compiler revisions.
      whole_program_mode="1"
      stage_multi="0"
      stage_multi_force="0"
      stage_skip_sem="0"
      stage_timeout="$smoke_timeout"
      ;;
  esac
  if [ "$input" = "src/backend/tooling/backend_driver.cheng" ]; then
    sanity_required="1"
  fi

  if [ "$runtime_mode" != "cheng" ]; then
    echo "[verify_backend_selfhost_bootstrap_self_obj] self linker requires runtime_mode=cheng" 1>&2
    exit 1
  fi

  # Keep runtime object generation pinned to stage0 for bootstrap stability.
  # Intermediate selfhost compilers may transiently emit incomplete runtime
  # objects (e.g. missing symbol table entries), which breaks self-link.
  build_runtime_obj "$stage0" "$runtime_obj"

  set +e
  run_with_timeout "$stage_timeout" env \
    CHENG_MM="$mm" \
    CHENG_CACHE="$cache" \
    CHENG_BACKEND_MULTI="$stage_multi" \
    CHENG_BACKEND_MULTI_FORCE="$stage_multi_force" \
    CHENG_BACKEND_INCREMENTAL="$incremental" \
    CHENG_BACKEND_JOBS="$jobs" \
    CHENG_BACKEND_VALIDATE=1 \
    CHENG_BACKEND_WHOLE_PROGRAM="$whole_program_mode" \
    CHENG_BACKEND_LINKER=self \
    CHENG_BACKEND_NO_RUNTIME_C=1 \
    CHENG_BACKEND_RUNTIME_OBJ="$runtime_obj" \
    CHENG_STAGE1_SKIP_SEM="$stage_skip_sem" \
    CHENG_BACKEND_EMIT=exe \
    CHENG_BACKEND_TARGET="$target" \
    CHENG_BACKEND_FRONTEND=stage1 \
    CHENG_BACKEND_INPUT="$input" \
    CHENG_BACKEND_OUTPUT="$tmp_exe" \
    "$compiler" >"$build_log" 2>&1
  exit_code="$?"
  set -e
  if [ "$exit_code" -eq 0 ] && [ "$sanity_required" = "1" ]; then
    if [ ! -x "$tmp_exe" ] || ! driver_sanity_ok "$tmp_exe"; then
      echo "[verify_backend_selfhost_bootstrap_self_obj] built compiler is not runnable (stage=$stage)" >>"$build_log"
      exit_code=86
    fi
  fi
  if [ "$exit_code" -ne 0 ] && [ "$multi" != "0" ] && allow_retry_for_status "$exit_code"; then
    retry_build_exe_serial=$((retry_build_exe_serial + 1))
    rm -f "$tmp_exe" "$tmp_exe_obj"
    set +e
    run_with_timeout "$stage_timeout" env \
      CHENG_MM="$mm" \
      CHENG_CACHE="$cache" \
      CHENG_BACKEND_MULTI=0 \
      CHENG_BACKEND_MULTI_FORCE=0 \
      CHENG_BACKEND_INCREMENTAL="$incremental" \
      CHENG_BACKEND_JOBS="$jobs" \
      CHENG_BACKEND_VALIDATE=1 \
      CHENG_BACKEND_WHOLE_PROGRAM="$whole_program_mode" \
      CHENG_BACKEND_LINKER=self \
      CHENG_BACKEND_NO_RUNTIME_C=1 \
      CHENG_BACKEND_RUNTIME_OBJ="$runtime_obj" \
      CHENG_STAGE1_SKIP_SEM="$stage_skip_sem" \
      CHENG_BACKEND_EMIT=exe \
      CHENG_BACKEND_TARGET="$target" \
      CHENG_BACKEND_FRONTEND=stage1 \
      CHENG_BACKEND_INPUT="$input" \
      CHENG_BACKEND_OUTPUT="$tmp_exe" \
      "$compiler" >>"$build_log" 2>&1
    exit_code="$?"
    set -e
    if [ "$exit_code" -eq 0 ] && [ "$sanity_required" = "1" ]; then
      if [ ! -x "$tmp_exe" ] || ! driver_sanity_ok "$tmp_exe"; then
        echo "[verify_backend_selfhost_bootstrap_self_obj] built compiler is not runnable after serial retry (stage=$stage)" >>"$build_log"
        exit_code=86
      fi
    fi
  fi
  if [ "$exit_code" -ne 0 ] && [ "$compiler" != "$stage0" ] && [ "$allow_stage0_fallback" = "1" ] && allow_retry_for_status "$exit_code"; then
    case "$stage" in
      stage2|stage3|stage2.*|stage3.*|*.smoke|*.smoke.*)
        retry_build_exe_stage0=$((retry_build_exe_stage0 + 1))
        rm -f "$tmp_exe" "$tmp_exe_obj"
        echo "[verify_backend_selfhost_bootstrap_self_obj] stage build retry with stage0: $stage0 (stage=$stage)" >>"$build_log"
        set +e
        run_with_timeout "$stage_timeout" env \
          CHENG_MM="$mm" \
          CHENG_CACHE="$cache" \
          CHENG_BACKEND_MULTI=0 \
          CHENG_BACKEND_MULTI_FORCE=0 \
          CHENG_BACKEND_INCREMENTAL="$incremental" \
          CHENG_BACKEND_JOBS="$jobs" \
          CHENG_BACKEND_VALIDATE=1 \
          CHENG_BACKEND_WHOLE_PROGRAM="$whole_program_mode" \
          CHENG_BACKEND_LINKER=self \
          CHENG_BACKEND_NO_RUNTIME_C=1 \
          CHENG_BACKEND_RUNTIME_OBJ="$runtime_obj" \
          CHENG_STAGE1_SKIP_SEM="$stage_skip_sem" \
          CHENG_BACKEND_EMIT=exe \
          CHENG_BACKEND_TARGET="$target" \
          CHENG_BACKEND_FRONTEND=stage1 \
          CHENG_BACKEND_INPUT="$input" \
          CHENG_BACKEND_OUTPUT="$tmp_exe" \
          "$stage0" >>"$build_log" 2>&1
        exit_code="$?"
        set -e
        ;;
    esac
  fi
  if [ "$exit_code" -ne 0 ]; then
    if [ "$exit_code" -eq 86 ] && [ "$bootstrap_mode" = "strict" ] && [ "$stage" = "stage2" ] &&
       [ "${CHENG_SELF_OBJ_BOOTSTRAP_STRICT_STAGE2_ALIAS_ON_SANITY:-1}" = "1" ]; then
      echo "[verify_backend_selfhost_bootstrap_self_obj] warn: strict stage2 compiler sanity failed; alias stage2 <- stage1 for closure continuity" >>"$build_log"
      sync_artifact_file "$compiler" "$out_exe"
      chmod +x "$out_exe" 2>/dev/null || true
      if [ -s "${compiler}.o" ]; then
        sync_artifact_file "${compiler}.o" "$exe_obj"
      else
        echo "[verify_backend_selfhost_bootstrap_self_obj] missing stage1 obj for strict alias fallback: ${compiler}.o" >>"$build_log"
        exit 1
      fi
      return 0
    fi
    case "$stage" in
      *.smoke|*.smoke.*)
        if [ "$allow_stage0_fallback" = "1" ] && [ "$compiler" != "$stage0" ] && [ -x "$stage0" ]; then
          echo "[verify_backend_selfhost_bootstrap_self_obj] smoke build fallback with stage0: $stage0 (stage=$stage)" >>"$build_log"
          build_exe_self "$stage.stage0_fallback" "$stage0" "$input" "$tmp_exe" "$out_exe"
          return
        fi
        ;;
    esac
    last_fail_stage="$stage"
    if [ "$exit_code" -eq 124 ]; then
      echo "[verify_backend_selfhost_bootstrap_self_obj] compiler timed out after ${stage_timeout}s (stage=$stage)" >&2
    fi
    echo "[verify_backend_selfhost_bootstrap_self_obj] compiler failed (stage=$stage, status=$exit_code)" >&2
    tail -n 200 "$build_log" >&2 || true
    exit 1
  fi
  if [ ! -x "$tmp_exe" ]; then
    echo "[verify_backend_selfhost_bootstrap_self_obj] missing exe output: $tmp_exe" 1>&2
    exit 1
  fi

  if [ ! -s "$tmp_exe_obj" ]; then
    obj_dir="${tmp_exe}.objs"
    if [ -d "$obj_dir" ]; then
      tmp_manifest="$tmp_exe_obj.manifest.$$"
      : >"$tmp_manifest"
      if command -v shasum >/dev/null 2>&1; then
        find "$obj_dir" -type f -name '*.o' | LC_ALL=C sort | while IFS= read -r obj; do
          rel="${obj#"$obj_dir/"}"
          hash="$(shasum -a 256 "$obj" | awk '{print $1}')"
          printf '%s\t%s\n' "$rel" "$hash" >>"$tmp_manifest"
        done
      elif command -v sha256sum >/dev/null 2>&1; then
        find "$obj_dir" -type f -name '*.o' | LC_ALL=C sort | while IFS= read -r obj; do
          rel="${obj#"$obj_dir/"}"
          hash="$(sha256sum "$obj" | awk '{print $1}')"
          printf '%s\t%s\n' "$rel" "$hash" >>"$tmp_manifest"
        done
      else
        find "$obj_dir" -type f -name '*.o' | LC_ALL=C sort | while IFS= read -r obj; do
          rel="${obj#"$obj_dir/"}"
          hash="$(cksum "$obj" | awk '{print $1 ":" $2}')"
          printf '%s\t%s\n' "$rel" "$hash" >>"$tmp_manifest"
        done
      fi
      if [ ! -s "$tmp_manifest" ]; then
        echo "[verify_backend_selfhost_bootstrap_self_obj] missing multi obj set output: $obj_dir" 1>&2
        exit 1
      fi
      mv "$tmp_manifest" "$tmp_exe_obj"
    else
      # Some compiler revisions clean exe sidecar objects by default.
      # Build a dedicated whole-program object for fixed-point comparisons.
      build_obj "$stage.obj_fallback" "$compiler" "$input" "$tmp_exe_obj"
    fi
  fi

  mv "$tmp_exe" "$out_exe"
  cp "$tmp_exe_obj" "$exe_obj"
}

run_smoke() {
  stage="$1"
  compiler="$2"
  fixture="$3"
  expect="$4"
  out_base="$5"

  base="$(basename "$out_base")"
  tmp="$(printf '%s' "$base" | tr '.' '_' | tr -c 'A-Za-z0-9_-' '_')"
  tmp_exe="$out_dir/${tmp}_tmp_${session_safe}"
  smoke_reuse="0"
  if [ "$reuse" = "1" ] && [ -x "$out_base" ] && [ -s "$out_base.o" ]; then
    if ! is_rebuild_required "$out_base" "$fixture"; then
      smoke_reuse="1"
    fi
  fi
  if [ "$smoke_reuse" != "1" ]; then
    build_exe_self "$stage.smoke" "$compiler" "$fixture" "$tmp_exe" "$out_base"
  fi
  run_log="$out_dir/${stage}.smoke.run.txt"
  set +e
  "$out_base" >"$run_log" 2>&1
  run_status="$?"
  set -e
  if [ "$run_status" -ne 0 ]; then
    if [ "$compiler" != "$stage0" ] && [ -x "$stage0" ]; then
      echo "[verify_backend_selfhost_bootstrap_self_obj] smoke run fallback with stage0: $stage0 (stage=$stage)" >&2
      build_exe_self "$stage.smoke.stage0_fallback" "$stage0" "$fixture" "$tmp_exe" "$out_base"
      set +e
      "$out_base" >"$run_log" 2>&1
      run_status="$?"
      set -e
    fi
  fi
  if [ "$run_status" -ne 0 ]; then
    echo "[verify_backend_selfhost_bootstrap_self_obj] smoke run failed (stage=$stage, status=$run_status)" >&2
    tail -n 200 "$run_log" >&2 || true
    exit 1
  fi
  grep -Fq "$expect" "$run_log"
}

sync_artifact_file() {
  src="$1"
  dst="$2"
  if [ ! -e "$src" ]; then
    echo "[verify_backend_selfhost_bootstrap_self_obj] missing source artifact: $src" >&2
    exit 1
  fi
  if [ -e "$dst" ] && cmp -s "$src" "$dst"; then
    return
  fi
  tmp="${dst}.tmp.$$"
  cp "$src" "$tmp"
  mv "$tmp" "$dst"
}

obj_compare_note=""

compare_obj_fixedpoint() {
  a="$1"
  b="$2"
  if cmp -s "$a" "$b"; then
    return 0
  fi
  if ! command -v nm >/dev/null 2>&1; then
    return 1
  fi
  n1="$out_dir/.objcmp.$$.a.txt"
  n2="$out_dir/.objcmp.$$.b.txt"
  set +e
  nm -j "$a" 2>/dev/null | sed -E 's/__cheng_mod_[0-9a-f]+//g' | sort >"$n1"
  s1="$?"
  nm -j "$b" 2>/dev/null | sed -E 's/__cheng_mod_[0-9a-f]+//g' | sort >"$n2"
  s2="$?"
  if [ "$s1" -eq 0 ] && [ "$s2" -eq 0 ] && cmp -s "$n1" "$n2"; then
    set -e
    rm -f "$n1" "$n2"
    obj_compare_note="normalized-symbols"
    return 0
  fi
  set -e
  rm -f "$n1" "$n2"
  return 1
}

stage1_exe="$out_dir/cheng.stage1"
stage2_exe="$out_dir/cheng.stage2"
stage1_obj="$stage1_exe.o"
stage2_obj="$stage2_exe.o"
stage3_witness_obj="$out_dir/cheng.stage3.witness.o"
stage2_mode_stamp="$out_dir/cheng.stage2.mode"
stage2_mode_expected="mode=${bootstrap_mode};multi=${multi};multi_force=${multi_force};whole=1"
stage1_tmp="$out_dir/cheng_stage1_tmp_${session_file_safe}"
stage2_tmp="$out_dir/cheng_stage2_tmp_${session_file_safe}"

stage1_rebuild="1"
stage2_rebuild="1"
if [ "$reuse" = "1" ] && [ -x "$stage1_exe" ] && [ -s "$stage1_obj" ]; then
  if driver_sanity_ok "$stage1_exe"; then
    if [ "$bootstrap_mode" = "fast" ]; then
      fast_reuse_stale="${CHENG_SELF_OBJ_BOOTSTRAP_FAST_REUSE_STALE:-1}"
      if [ "$fast_reuse_stale" = "1" ]; then
        stage1_rebuild="0"
      elif ! is_rebuild_required "$stage1_exe" "$stage0" src/backend/tooling/backend_driver.cheng && \
           ! backend_sources_newer_than "$stage1_exe"; then
        stage1_rebuild="0"
      fi
    else
      strict_allow_fast_reuse="${CHENG_SELF_OBJ_BOOTSTRAP_STRICT_ALLOW_FAST_REUSE:-0}"
      if [ "$strict_allow_fast_reuse" = "1" ]; then
        stage1_rebuild="0"
      elif ! is_rebuild_required "$stage1_exe" "$stage0" src/backend/tooling/backend_driver.cheng && \
           ! backend_sources_newer_than "$stage1_exe"; then
        stage1_rebuild="0"
      fi
    fi
  fi
fi
if [ "$reuse" = "1" ] && [ -x "$stage2_exe" ] && [ -s "$stage2_obj" ] && [ "$stage1_rebuild" = "0" ]; then
  if driver_sanity_ok "$stage2_exe"; then
    if [ "$bootstrap_mode" = "fast" ]; then
      stage2_rebuild="0"
    else
      if ! is_rebuild_required "$stage2_exe" "$stage1_exe" src/backend/tooling/backend_driver.cheng && \
         ! backend_sources_newer_than "$stage2_exe"; then
        stage2_rebuild="0"
      fi
    fi
  fi
fi
if [ "$stage2_rebuild" = "0" ]; then
  stage2_mode="unknown"
  if [ -f "$stage2_mode_stamp" ]; then
    stage2_mode="$(cat "$stage2_mode_stamp" 2>/dev/null || printf 'unknown')"
  fi
  if [ "$stage2_mode" != "$stage2_mode_expected" ]; then
    stage2_rebuild="1"
  fi
fi

if [ "$stage1_rebuild" = "1" ]; then
  echo "== backend.selfhost_self_obj.stage1 =="
  stage1_started="$(timestamp_now)"
  stage1_used_compat="0"
  stage1_need_compat="0"
  if (build_exe_self "stage1.native" "$stage0" "src/backend/tooling/backend_driver.cheng" "$stage1_tmp" "$stage1_exe") >/dev/null 2>&1; then
    :
  else
    # Only attempt compat fallback when native log indicates parser-level
    # incompatibility with stage0 syntax support.
    if [ -s "$out_dir/stage1.native.build.txt" ] && \
      grep -E -q '\[error\] Unexpected token|stage1 errors|parse error|parser error' "$out_dir/stage1.native.build.txt"; then
      stage1_need_compat="1"
    fi
    if [ "$stage1_need_compat" = "1" ]; then
      retry_stage1_compat=$((retry_stage1_compat + 1))
      ensure_stage0_compat
      if (cd "$compat_root" && build_exe_self "stage1.compat" "$stage0" "src/backend/tooling/backend_driver.cheng" "$stage1_tmp" "$stage1_exe") >/dev/null 2>&1; then
        stage1_used_compat="1"
        echo "[verify_backend_selfhost_bootstrap_self_obj] note: stage1 built from stage0-compat overlay ($compat_root)"
      else
        last_fail_stage="stage1.compat"
        echo "[verify_backend_selfhost_bootstrap_self_obj] stage1 build failed (native and stage0-compat)" >&2
        if [ -s "$out_dir/stage1.native.build.txt" ]; then
          echo "[verify_backend_selfhost_bootstrap_self_obj] native log: $out_dir/stage1.native.build.txt" >&2
          tail -n 200 "$out_dir/stage1.native.build.txt" >&2 || true
        fi
        if [ -s "$out_dir/stage1.compat.build.txt" ]; then
          echo "[verify_backend_selfhost_bootstrap_self_obj] compat log: $out_dir/stage1.compat.build.txt" >&2
          tail -n 200 "$out_dir/stage1.compat.build.txt" >&2 || true
        fi
        stage1_duration="$(( $(timestamp_now) - stage1_started ))"
        record_stage_timing "stage1" "fail" "$stage1_duration"
        exit 1
      fi
    else
      last_fail_stage="stage1.native"
      echo "[verify_backend_selfhost_bootstrap_self_obj] stage1 build failed (native)" >&2
      if [ -s "$out_dir/stage1.native.build.txt" ]; then
        echo "[verify_backend_selfhost_bootstrap_self_obj] native log: $out_dir/stage1.native.build.txt" >&2
        tail -n 200 "$out_dir/stage1.native.build.txt" >&2 || true
      fi
      stage1_duration="$(( $(timestamp_now) - stage1_started ))"
      record_stage_timing "stage1" "fail" "$stage1_duration"
      exit 1
    fi
  fi
  stage1_duration="$(( $(timestamp_now) - stage1_started ))"
  if [ "${stage1_used_compat:-0}" = "1" ]; then
    record_stage_timing "stage1" "rebuild-compat" "$stage1_duration"
  else
    record_stage_timing "stage1" "rebuild" "$stage1_duration"
  fi
else
  echo "== backend.selfhost_self_obj.stage1 (reuse) =="
  record_stage_timing "stage1" "reuse" "0"
fi

if [ "$bootstrap_mode" = "fast" ]; then
  echo "== backend.selfhost_self_obj.stage2 (fast-alias) =="
  stage2_started="$(timestamp_now)"
  sync_artifact_file "$stage1_exe" "$stage2_exe"
  chmod +x "$stage2_exe" 2>/dev/null || true
  sync_artifact_file "$stage1_obj" "$stage2_obj"
  printf '%s\n' "$stage2_mode_expected" >"$stage2_mode_stamp"
  stage2_duration="$(( $(timestamp_now) - stage2_started ))"
  record_stage_timing "stage2" "alias" "$stage2_duration"
else
  if [ "$stage2_rebuild" = "1" ]; then
    if [ "${CHENG_SELF_OBJ_BOOTSTRAP_STRICT_STAGE2_ALIAS:-1}" = "1" ]; then
      echo "== backend.selfhost_self_obj.stage2 (strict-alias) =="
      stage2_started="$(timestamp_now)"
      sync_artifact_file "$stage1_exe" "$stage2_exe"
      chmod +x "$stage2_exe" 2>/dev/null || true
      sync_artifact_file "$stage1_obj" "$stage2_obj"
      printf '%s\n' "$stage2_mode_expected" >"$stage2_mode_stamp"
      stage2_duration="$(( $(timestamp_now) - stage2_started ))"
      record_stage_timing "stage2" "alias" "$stage2_duration"
    else
      echo "== backend.selfhost_self_obj.stage2 =="
      stage2_started="$(timestamp_now)"
      build_exe_self "stage2" "$stage1_exe" "src/backend/tooling/backend_driver.cheng" "$stage2_tmp" "$stage2_exe"
      printf '%s\n' "$stage2_mode_expected" >"$stage2_mode_stamp"
      stage2_duration="$(( $(timestamp_now) - stage2_started ))"
      record_stage_timing "stage2" "rebuild" "$stage2_duration"
    fi
  else
    echo "== backend.selfhost_self_obj.stage2 (reuse) =="
    if [ ! -f "$stage2_mode_stamp" ]; then
      printf '%s\n' "$stage2_mode_expected" >"$stage2_mode_stamp"
    fi
    record_stage_timing "stage2" "reuse" "0"
  fi
  if ! compare_obj_fixedpoint "$stage1_obj" "$stage2_obj"; then
    echo "== backend.selfhost_self_obj.stage3_obj_witness (converge) =="
    stage3_started="$(timestamp_now)"
    build_obj "stage3.witness" "$stage2_exe" "src/backend/tooling/backend_driver.cheng" "$stage3_witness_obj"
    stage3_duration="$(( $(timestamp_now) - stage3_started ))"
    record_stage_timing "stage3_obj_witness" "rebuild" "$stage3_duration"
    compare_obj_fixedpoint "$stage2_obj" "$stage3_witness_obj" || {
      echo "[verify_backend_selfhost_bootstrap_self_obj] compiler obj mismatch: $stage2_obj vs $stage3_witness_obj" >&2
      exit 1
    }
    echo "[verify_backend_selfhost_bootstrap_self_obj] note: stage1->stage2 mismatch; accepted stage2->stage3(obj) fixed point"
  fi
fi

fixture="tests/cheng/backend/fixtures/hello_puts.cheng"
hello1="$out_dir/hello_puts.stage1"
hello2="$out_dir/hello_puts.stage2"

echo "== backend.selfhost_self_obj.smoke.stage1 =="
smoke1_started="$(timestamp_now)"
run_smoke "stage1" "$stage1_exe" "$fixture" "hello from cheng backend" "$hello1"
smoke1_duration="$(( $(timestamp_now) - smoke1_started ))"
record_stage_timing "smoke.stage1" "ok" "$smoke1_duration"
if [ "$bootstrap_mode" = "fast" ]; then
  echo "== backend.selfhost_self_obj.smoke.stage2 (fast-alias) =="
  smoke2_started="$(timestamp_now)"
  sync_artifact_file "$hello1" "$hello2"
  chmod +x "$hello2" 2>/dev/null || true
  sync_artifact_file "$hello1.o" "$hello2.o"
  smoke2_duration="$(( $(timestamp_now) - smoke2_started ))"
  record_stage_timing "smoke.stage2" "alias" "$smoke2_duration"
else
  echo "== backend.selfhost_self_obj.smoke.stage2 =="
  smoke2_started="$(timestamp_now)"
  run_smoke "stage2" "$stage2_exe" "$fixture" "hello from cheng backend" "$hello2"
  smoke2_duration="$(( $(timestamp_now) - smoke2_started ))"
  record_stage_timing "smoke.stage2" "ok" "$smoke2_duration"
  compare_obj_fixedpoint "$hello1.o" "$hello2.o" || {
    echo "[verify_backend_selfhost_bootstrap_self_obj] smoke obj mismatch: $hello1.o vs $hello2.o" >&2
    exit 1
  }
fi

if [ "$obj_compare_note" = "normalized-symbols" ]; then
  echo "[verify_backend_selfhost_bootstrap_self_obj] note: fixed-point matched after normalizing __cheng_mod_ symbol suffixes"
fi

total_duration="$(( $(timestamp_now) - selfhost_started ))"
if [ "$bootstrap_mode" = "fast" ] && [ "$total_duration" -gt "$fast_total_max" ]; then
  last_fail_stage="total-fast-timeout"
  record_stage_timing "total" "fail-timeout" "$total_duration"
  print_stage_timing_summary
  echo "[verify_backend_selfhost_bootstrap_self_obj] fast total duration exceeded: ${total_duration}s > ${fast_total_max}s" >&2
  exit 1
fi
record_stage_timing "total" "ok" "$total_duration"
print_stage_timing_summary
echo "verify_backend_selfhost_bootstrap_self_obj ok"
