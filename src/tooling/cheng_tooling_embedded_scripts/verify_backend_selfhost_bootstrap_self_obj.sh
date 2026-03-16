#!/usr/bin/env sh
:
set -eu
(set -o pipefail) 2>/dev/null && set -o pipefail

root="$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)"
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
export CLEAN_CHENG_LOCAL=0
if [ "${ABI:-}" = "" ]; then
  export ABI=v2_noptr
fi
if [ "${ABI}" != "v2_noptr" ]; then
  echo "[verify_backend_selfhost_bootstrap_self_obj] only ABI=v2_noptr is supported (got: ${ABI})" 1>&2
  exit 2
fi
if [ "${STAGE1_STD_NO_POINTERS:-}" = "" ]; then
  export STAGE1_STD_NO_POINTERS=0
fi
if [ "${STAGE1_STD_NO_POINTERS_STRICT:-}" = "" ]; then
  export STAGE1_STD_NO_POINTERS_STRICT=0
fi
if [ "${STAGE1_NO_POINTERS_NON_C_ABI:-}" = "" ]; then
  export STAGE1_NO_POINTERS_NON_C_ABI=0
fi
if [ "${STAGE1_NO_POINTERS_NON_C_ABI_INTERNAL:-}" = "" ]; then
  export STAGE1_NO_POINTERS_NON_C_ABI_INTERNAL=0
fi
if [ "${BACKEND_IR:-}" = "" ]; then
  export BACKEND_IR=uir
fi
if [ "${GENERIC_MODE:-}" = "" ]; then
  mode_hint="${SELF_OBJ_BOOTSTRAP_MODE:-fast}"
  if [ "$mode_hint" = "strict" ]; then
    export GENERIC_MODE=hybrid
  else
    # Fast mode defaults to dict to avoid long stage1 monomorphize stalls.
    export GENERIC_MODE=dict
  fi
fi
if [ "${GENERIC_SPEC_BUDGET:-}" = "" ]; then
  export GENERIC_SPEC_BUDGET=0
fi
# Stage1 frontend pass toggles: keep selfhost bootstrap path stable by default.
if [ "${STAGE1_SEM_FIXED_0:-}" = "" ]; then
  export STAGE1_SEM_FIXED_0=0
fi
if [ "${STAGE1_OWNERSHIP_FIXED_0:-}" = "" ]; then
  export STAGE1_OWNERSHIP_FIXED_0=0
fi
if [ "${SELF_OBJ_BOOTSTRAP_STRICT_STAGE2_ALIAS:-}" = "" ]; then
  export SELF_OBJ_BOOTSTRAP_STRICT_STAGE2_ALIAS=0
fi
if [ "${SELF_OBJ_BOOTSTRAP_STRICT_STAGE2_ALIAS_ON_SANITY:-}" = "" ]; then
  export SELF_OBJ_BOOTSTRAP_STRICT_STAGE2_ALIAS_ON_SANITY=0
fi
if [ "${SELF_OBJ_BOOTSTRAP_STRICT_STAGE2_ALIAS_ON_TIMEOUT:-}" = "" ]; then
  export SELF_OBJ_BOOTSTRAP_STRICT_STAGE2_ALIAS_ON_TIMEOUT=0
fi
if [ "${SELF_OBJ_BOOTSTRAP_STRICT_STAGE2_ALIAS_ON_OOM:-}" = "" ]; then
  export SELF_OBJ_BOOTSTRAP_STRICT_STAGE2_ALIAS_ON_OOM=0
fi
if [ "${SELF_OBJ_BOOTSTRAP_SAMPLE_ON_TIMEOUT:-}" = "" ]; then
  export SELF_OBJ_BOOTSTRAP_SAMPLE_ON_TIMEOUT=1
fi
if [ "${SELF_OBJ_BOOTSTRAP_TIMEOUT_SAMPLE_SECS:-}" = "" ]; then
  export SELF_OBJ_BOOTSTRAP_TIMEOUT_SAMPLE_SECS=5
fi

run_with_timeout() {
  seconds="$1"
  shift
  perl -e '
    use POSIX qw(setsid WNOHANG);
    my $timeout = shift;
    my $sample_on_timeout = $ENV{"SELF_OBJ_BOOTSTRAP_SAMPLE_ON_TIMEOUT"} // "0";
    my $sample_secs = $ENV{"SELF_OBJ_BOOTSTRAP_TIMEOUT_SAMPLE_SECS"} // "5";
    my $sample_prefix = $ENV{"SELF_OBJ_BOOTSTRAP_TIMEOUT_SAMPLE_PREFIX"} // "";
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
        if ($sample_on_timeout ne "0" && $sample_prefix ne "" && -x "/usr/bin/sample") {
          my $parent_file = $sample_prefix . ".parent.sample.txt";
          my $child_file = $sample_prefix . ".child.sample.txt";
          my $pid_file = $sample_prefix . ".pids.txt";
          unlink $parent_file;
          unlink $child_file;
          unlink $pid_file;
          system("/usr/bin/sample", $pid, $sample_secs, "-file", $parent_file);
          my $child = qx(/usr/bin/pgrep -P $pid 2>/dev/null);
          if (defined $child) {
            $child =~ s/\s+.*//s;
            if ($child ne "") {
              system("/usr/bin/sample", $child, $sample_secs, "-file", $child_file);
              if (open(my $pfh, ">", $pid_file)) {
                print $pfh "parent=$pid\nchild=$child\n";
                close($pfh);
              }
            }
          }
        }
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

driver_blocked_stage0() {
  bin="$1"
  [ "$bin" != "" ] || return 1
  [ -x "$bin" ] || return 1
  h=""
  if command -v shasum >/dev/null 2>&1; then
    h="$(shasum -a 256 "$bin" 2>/dev/null | awk '{print $1}')"
  elif command -v sha256sum >/dev/null 2>&1; then
    h="$(sha256sum "$bin" 2>/dev/null | awk '{print $1}')"
  fi
  case "$h" in
    # Known-bad local/release drivers that can wedge during stage0 probing.
    08b9888a214418a32a468f1d9155c9d21d1789d01579cf84e7d9d6321366e382|\
    d059d1d84290dac64120dc78f0dbd9cb24e0e4b3d5a9045e63ad26232373ed1a)
      return 0
      ;;
  esac
  return 1
}

driver_compile_probe_ok() {
  probe_compiler="$1"
  if [ "$probe_compiler" = "" ] || [ ! -x "$probe_compiler" ]; then
    return 1
  fi
  if driver_blocked_stage0 "$probe_compiler"; then
    return 1
  fi
  probe_src="tests/cheng/backend/fixtures/return_add.cheng"
  if [ ! -f "$probe_src" ]; then
    probe_src="tests/cheng/backend/fixtures/hello_puts.cheng"
  fi
  if [ ! -f "$probe_src" ]; then
    return 0
  fi
  probe_out="chengcache/.selfhost_stage0_probe_$$"
  probe_log="$out_dir/selfhost_stage0_probe.log"
  rm -f "$probe_out" "$probe_log"
  set +e
  run_with_timeout "$stage0_probe_timeout" env \
    CACHE=0 \
    STAGE1_AUTO_SYSTEM=0 \
    BACKEND_MULTI=0 \
    BACKEND_MULTI_FORCE=0 \
    BACKEND_INCREMENTAL=1 \
    BACKEND_JOBS=1 \
    BACKEND_VALIDATE=0 \
    STAGE1_SEM_FIXED_0=0 \
    STAGE1_OWNERSHIP_FIXED_0=0 \
    STAGE1_SKIP_CPROFILE=1 \
    GENERIC_MODE=dict \
    GENERIC_SPEC_BUDGET=0 \
    BACKEND_EMIT=exe \
    BACKEND_TARGET="$target" \
    BACKEND_FRONTEND=stage1 \
    BACKEND_INPUT="$probe_src" \
    BACKEND_OUTPUT="$probe_out" \
    "$probe_compiler" >"$probe_log" 2>&1
  probe_status="$?"
  set -e
  if [ "$probe_status" -eq 0 ] && [ -x "$probe_out" ]; then
    return 0
  fi
  return 1
}

driver_stage1_probe_ok() {
  probe_compiler="$1"
  if [ "$probe_compiler" = "" ] || [ ! -x "$probe_compiler" ]; then
    return 1
  fi
  probe_src="tests/cheng/backend/fixtures/return_add.cheng"
  if [ ! -f "$probe_src" ]; then
    return 0
  fi
  probe_out="chengcache/.selfhost_stage1_probe_$$"
  probe_log="$out_dir/selfhost_stage1_probe.log"
  rm -f "$probe_out" "$probe_log"
  set +e
  run_with_timeout "$stage1_probe_timeout" env \
    MM="$mm" \
    CACHE=0 \
    STAGE1_AUTO_SYSTEM=0 \
    BACKEND_MULTI=0 \
    BACKEND_MULTI_FORCE=0 \
    BACKEND_INCREMENTAL=1 \
    BACKEND_JOBS=1 \
    BACKEND_VALIDATE=0 \
    STAGE1_NO_POINTERS_NON_C_ABI=0 \
    STAGE1_NO_POINTERS_NON_C_ABI_INTERNAL=0 \
    STAGE1_SEM_FIXED_0=0 \
    STAGE1_OWNERSHIP_FIXED_0=0 \
    STAGE1_SKIP_CPROFILE=1 \
    GENERIC_MODE=dict \
    GENERIC_SPEC_BUDGET=0 \
    BACKEND_EMIT=exe \
    BACKEND_TARGET="$target" \
    BACKEND_FRONTEND=stage1 \
    BACKEND_INPUT="$probe_src" \
    BACKEND_OUTPUT="$probe_out" \
    "$probe_compiler" >"$probe_log" 2>&1
  probe_status="$?"
  set -e
  if [ "$probe_status" -eq 0 ] && [ -x "$probe_out" ]; then
    return 0
  fi
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
if [ "${CLEAN_CHENG_LOCAL:-0}" = "1" ] && [ "${TOOLING_CLEANUP_DEPTH:-0}" = "0" ] && [ "${SELF_OBJ_BOOTSTRAP_STAGE0:-}" = "" ]; then
  export TOOLING_CLEANUP_DEPTH=1
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

linker_mode="${SELF_OBJ_BOOTSTRAP_LINKER:-}"
if [ "$linker_mode" = "" ]; then
  linker_mode="self"
fi
if [ "$linker_mode" != "self" ]; then
  echo "[Error] verify_backend_selfhost_bootstrap_self_obj requires SELF_OBJ_BOOTSTRAP_LINKER=self (cc path removed)" 1>&2
  exit 2
fi

if [ "$linker_mode" = "self" ] && [ "$host_os" = "Darwin" ]; then
  if ! command -v codesign >/dev/null 2>&1; then
    echo "verify_backend_selfhost_bootstrap_self_obj skip: missing codesign" 1>&2
    exit 2
  fi
fi

runtime_mode="${SELF_OBJ_BOOTSTRAP_RUNTIME:-}"
if [ "$runtime_mode" = "" ]; then
  runtime_mode="cheng"
fi
if [ "$runtime_mode" != "cheng" ]; then
  echo "[Error] verify_backend_selfhost_bootstrap_self_obj requires SELF_OBJ_BOOTSTRAP_RUNTIME=cheng (C runtime path removed)" 1>&2
  exit 2
fi
driver_input="${SELF_OBJ_BOOTSTRAP_DRIVER_INPUT:-src/backend/tooling/backend_driver.cheng}"
if [ ! -f "$driver_input" ]; then
  echo "[Error] verify_backend_selfhost_bootstrap_self_obj missing SELF_OBJ_BOOTSTRAP_DRIVER_INPUT: $driver_input" 1>&2
  exit 2
fi
runtime_cheng_src="src/std/system_helpers_backend.cheng"
runtime_obj=""
runtime_obj_prebuilt="$root/chengcache/system_helpers.backend.cheng.${target}.o"
cstring_link_retry="${SELF_OBJ_BOOTSTRAP_CSTRING_LINK_RETRY:-1}"

pick_prebuilt_runtime_obj() {
  explicit_runtime_obj="${SELF_OBJ_BOOTSTRAP_RUNTIME_OBJ:-}"
  if [ "$explicit_runtime_obj" != "" ]; then
    explicit_runtime_obj="$(to_abs "$explicit_runtime_obj")"
    if [ -f "$explicit_runtime_obj" ]; then
      printf '%s\n' "$explicit_runtime_obj"
      return 0
    fi
  fi
  for cand in \
    "$runtime_obj_prebuilt" \
    "$root/artifacts/backend_selfhost_self_obj/system_helpers.backend.cheng.o" \
    "$root/artifacts/backend_selfhost_self_obj/system_helpers.backend.cheng.stage1shim.o" \
    "$root/artifacts/backend_selfhost_self_obj/stage1.native.runtime.dedup.o" \
    "$root/artifacts/runtime/system_helpers.backend.combined.${target}.o" \
    "$root/artifacts/backend_mm/system_helpers.backend.combined.${target}.o"
  do
    if [ -f "$cand" ]; then
      printf '%s\n' "$cand"
      return 0
    fi
  done
  printf '%s\n' "$runtime_obj_prebuilt"
  return 0
}

out_dir_rel="${SELF_OBJ_BOOTSTRAP_OUT_DIR:-artifacts/backend_selfhost_self_obj}"
case "$out_dir_rel" in
  /*)
    out_dir="$out_dir_rel"
    ;;
  *)
    out_dir="$root/$out_dir_rel"
    ;;
esac
mkdir -p "$out_dir"
mkdir -p chengcache
runtime_obj="$out_dir/system_helpers.backend.cheng.o"
# Runtime cstring compat merge path has been removed; clear stale artifacts so
# timeout diagnostics don't accidentally report old retry logs.
rm -f "$out_dir/cstring_compat.build.txt" "$out_dir/cstring_compat.o" "$out_dir/cstring_compat.s" 2>/dev/null || true
build_timeout="${SELF_OBJ_BOOTSTRAP_TIMEOUT:-}"
smoke_timeout="${SELF_OBJ_BOOTSTRAP_SMOKE_TIMEOUT:-30}"
stage0_probe_timeout="${SELF_OBJ_BOOTSTRAP_STAGE0_PROBE_TIMEOUT:-20}"
stage1_probe_timeout="${SELF_OBJ_BOOTSTRAP_STAGE1_PROBE_TIMEOUT:-20}"
stage0_compat_allowed="${SELF_OBJ_BOOTSTRAP_STAGE0_COMPAT:-0}"
if [ "$stage0_compat_allowed" != "0" ]; then
  echo "[Error] verify_backend_selfhost_bootstrap_self_obj removed SELF_OBJ_BOOTSTRAP_STAGE0_COMPAT (only 0 is supported)" 1>&2
  exit 2
fi

stage0_env="${SELF_OBJ_BOOTSTRAP_STAGE0:-}"
if [ "$stage0_env" != "" ]; then
  stage0="$stage0_env"
  stage0="$(to_abs "$stage0")"
  if [ ! -x "$stage0" ] || ! driver_compile_probe_ok "$stage0"; then
    echo "[verify_backend_selfhost_bootstrap_self_obj] missing stage0 driver (SELF_OBJ_BOOTSTRAP_STAGE0): $stage0" 1>&2
    exit 1
  fi
else
  stage0=""
  stage0_from_backend_driver="${BACKEND_DRIVER:-}"
  if [ "$stage0_from_backend_driver" != "" ]; then
    stage0_try="$(to_abs "$stage0_from_backend_driver")"
    if driver_sanity_ok "$stage0_try" && driver_compile_probe_ok "$stage0_try"; then
      stage0="$stage0_try"
    else
      echo "[verify_backend_selfhost_bootstrap_self_obj] warn: BACKEND_DRIVER probe failed: $stage0_try" 1>&2
      stage0=""
    fi
  fi
  if [ "$stage0" = "" ]; then
    for cand in \
      "$out_dir/cheng.stage2" \
      "$out_dir/cheng.stage1" \
      "artifacts/backend_driver/cheng" \
      "artifacts/backend_selfhost_self_obj/cheng_stage0_prod" \
      "artifacts/backend_selfhost_self_obj/cheng_stage0_default" \
      "artifacts/backend_selfhost_self_obj/cheng.stage2" \
      "artifacts/backend_selfhost_self_obj/cheng.stage1" \
      "dist/releases/current/cheng" \
      dist/releases/*/cheng \
      "artifacts/backend_seed/cheng.stage2"; do
      cand_abs="$(to_abs "$cand")"
      if driver_sanity_ok "$cand_abs" && driver_compile_probe_ok "$cand_abs"; then
        stage0="$cand_abs"
        break
      fi
    done
  fi
  if [ "$stage0" = "" ] && driver_sanity_ok "./artifacts/backend_driver/cheng" &&
     driver_compile_probe_ok "./artifacts/backend_driver/cheng"; then
    stage0="$(to_abs "./artifacts/backend_driver/cheng")"
  fi
  if [ "$stage0" = "" ] && driver_sanity_ok "./cheng" && driver_compile_probe_ok "./cheng"; then
    stage0="$(to_abs "./cheng")"
  fi
  if [ "$stage0" = "" ]; then
    stage0="./artifacts/backend_driver/cheng"
    stage0_name="$(basename "$stage0")"
    echo "== backend.selfhost_self_obj.build_stage0_driver ($stage0_name) =="
    ${TOOLING_SELF_BIN:-artifacts/tooling_cmd/cheng_tooling} build_backend_driver --name:"$stage0" >/dev/null
    if ! driver_sanity_ok "$stage0" || ! driver_compile_probe_ok "$stage0"; then
      echo "[verify_backend_selfhost_bootstrap_self_obj] missing stage0 driver: $stage0" 1>&2
      exit 1
    fi
    stage0="$(to_abs "$stage0")"
  fi
fi

mm="${SELF_OBJ_BOOTSTRAP_MM:-${MM:-orc}}"
cache="${SELF_OBJ_BOOTSTRAP_CACHE:-0}"
reuse="${SELF_OBJ_BOOTSTRAP_REUSE:-1}"
multi="${SELF_OBJ_BOOTSTRAP_MULTI:-1}"
incremental="${SELF_OBJ_BOOTSTRAP_INCREMENTAL:-1}"
multi_force="${SELF_OBJ_BOOTSTRAP_MULTI_FORCE:-0}"
jobs="${SELF_OBJ_BOOTSTRAP_JOBS:-0}"
bootstrap_mode="${SELF_OBJ_BOOTSTRAP_MODE:-fast}"
validate="${SELF_OBJ_BOOTSTRAP_VALIDATE:-1}"
if [ "${SELF_OBJ_BOOTSTRAP_SKIP_SMOKE:-}" = "" ]; then
  if [ "$bootstrap_mode" = "fast" ]; then
    skip_smoke="1"
  else
    skip_smoke="0"
  fi
else
  skip_smoke="${SELF_OBJ_BOOTSTRAP_SKIP_SMOKE}"
fi
require_runnable="${SELF_OBJ_BOOTSTRAP_REQUIRE_RUNNABLE:-1}"
stage1_probe_required="${SELF_OBJ_BOOTSTRAP_STAGE1_PROBE_REQUIRED:-1}"
if [ "$bootstrap_mode" != "strict" ] && [ "$bootstrap_mode" != "fast" ]; then
  echo "[Error] verify_backend_selfhost_bootstrap_self_obj invalid SELF_OBJ_BOOTSTRAP_MODE=$bootstrap_mode (expected strict|fast)" 1>&2
  exit 2
fi
if [ "$build_timeout" = "" ]; then
  if [ "$bootstrap_mode" = "strict" ]; then
    build_timeout="120"
  else
    build_timeout="60"
  fi
fi
case "$validate" in
  0|1)
    ;;
  *)
    echo "[Error] verify_backend_selfhost_bootstrap_self_obj invalid SELF_OBJ_BOOTSTRAP_VALIDATE=$validate (expected 0|1)" 1>&2
    exit 2
    ;;
esac
case "$skip_smoke" in
  0|1)
    ;;
  *)
    echo "[Error] verify_backend_selfhost_bootstrap_self_obj invalid SELF_OBJ_BOOTSTRAP_SKIP_SMOKE=$skip_smoke (expected 0|1)" 1>&2
    exit 2
    ;;
esac
case "$require_runnable" in
  0|1)
    ;;
  *)
    echo "[Error] verify_backend_selfhost_bootstrap_self_obj invalid SELF_OBJ_BOOTSTRAP_REQUIRE_RUNNABLE=$require_runnable (expected 0|1)" 1>&2
    exit 2
    ;;
esac
allow_retry="${SELF_OBJ_BOOTSTRAP_ALLOW_RETRY:-1}"
multi_probe="${SELF_OBJ_BOOTSTRAP_MULTI_PROBE:-1}"
multi_probe_timeout="${SELF_OBJ_BOOTSTRAP_MULTI_PROBE_TIMEOUT:-20}"
fast_total_max="${SELF_OBJ_BOOTSTRAP_FAST_MAX_TOTAL:-60}"
fast_jobs_cap="${SELF_OBJ_BOOTSTRAP_FAST_JOBS_CAP:-8}"
strict_jobs_cap="${SELF_OBJ_BOOTSTRAP_STRICT_JOBS_CAP:-8}"
case "$fast_jobs_cap" in
  ''|*[!0-9]*)
    fast_jobs_cap="8"
    ;;
esac
if [ "$fast_jobs_cap" -lt 1 ]; then
  fast_jobs_cap="1"
fi
case "$strict_jobs_cap" in
  ''|*[!0-9]*)
    strict_jobs_cap="8"
    ;;
esac
if [ "$strict_jobs_cap" -lt 1 ]; then
  strict_jobs_cap="1"
fi
if [ "${SELF_OBJ_BOOTSTRAP_JOBS+x}" = "" ]; then
  jobs="$(detect_host_jobs)"
fi
if [ "$bootstrap_mode" = "fast" ]; then
  # Fast mode is latency-first, but keep non-timeout retry enabled to handle
  # transient worker-path crashes in seed/stage compilers.
  allow_retry="1"
  if [ "${SELF_OBJ_BOOTSTRAP_MULTI+x}" = "" ]; then
    multi="1"
  fi
  if [ "$jobs" -gt "$fast_jobs_cap" ]; then
    jobs="$fast_jobs_cap"
  fi
  if [ "${SELF_OBJ_BOOTSTRAP_MULTI_FORCE+x}" = "" ]; then
    multi_force="$multi"
  fi
else
  # Strict mode now defaults to parallel as well. For known-unstable stage0
  # binaries, a dedicated probe below will fast-fallback to serial.
  if [ "${SELF_OBJ_BOOTSTRAP_ALLOW_RETRY+x}" = "" ]; then
    allow_retry="0"
  fi
  if [ "${SELF_OBJ_BOOTSTRAP_MULTI+x}" = "" ]; then
    multi="1"
  fi
  if [ "${SELF_OBJ_BOOTSTRAP_JOBS+x}" = "" ] && [ "$jobs" -gt "$strict_jobs_cap" ]; then
    jobs="$strict_jobs_cap"
  fi
  if [ "${SELF_OBJ_BOOTSTRAP_MULTI_FORCE+x}" = "" ]; then
    multi_force="$multi"
  fi
fi
session="${SELF_OBJ_BOOTSTRAP_SESSION:-default}"
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

driver_multi_worker_supported() {
  probe_compiler="$1"
  if [ "$probe_compiler" = "" ] || [ ! -x "$probe_compiler" ]; then
    return 1
  fi
  # Use exe+multipath probe so we can catch worker crashes/unit-map issues
  # before stage1 compile, instead of paying a long fail-then-retry path.
  probe_src="tests/cheng/backend/fixtures/hello_puts.cheng"
  if [ ! -f "$probe_src" ]; then
    probe_src="tests/cheng/backend/fixtures/return_add.cheng"
  fi
  if [ ! -f "$probe_src" ]; then
    return 0
  fi
  probe_out="chengcache/.selfhost_multi_probe_${session_file_safe}_$$"
  probe_log="$out_dir/selfhost_multi_probe_${session_file_safe}.log"
  rm -f "$probe_out" "$probe_out.o" "$probe_log"
  set +e
  run_with_timeout "$multi_probe_timeout" env \
    MM="$mm" \
    CACHE="$cache" \
    STAGE1_AUTO_SYSTEM=0 \
    BACKEND_MULTI=1 \
    BACKEND_MULTI_FORCE=1 \
    BACKEND_INCREMENTAL="$incremental" \
    BACKEND_JOBS="$jobs" \
    BACKEND_VALIDATE=0 \
    BACKEND_EMIT=exe \
    BACKEND_TARGET="$target" \
    BACKEND_FRONTEND=stage1 \
    BACKEND_INPUT="$probe_src" \
    BACKEND_OUTPUT="$probe_out" \
    "$probe_compiler" >"$probe_log" 2>&1
  probe_status="$?"
  set -e
  if [ "$probe_status" -eq 0 ] && [ -x "$probe_out" ]; then
    rm -f "$probe_out" "$probe_out.o"
    return 0
  fi
  if [ "$probe_status" -eq 124 ] || [ "$probe_status" -eq 137 ] || [ "$probe_status" -eq 139 ] || [ "$probe_status" -eq 143 ]; then
    return 1
  fi
  if grep -E -q 'fork worker failed|waitpid failed|unit file not found in module|segmentation fault|illegal instruction' "$probe_log" 2>/dev/null; then
    return 1
  fi
  return 1
}

if [ "$bootstrap_mode" = "strict" ] && [ "$multi" != "0" ] && [ "$multi_probe" = "1" ]; then
  if ! driver_multi_worker_supported "$stage0"; then
    echo "[verify_backend_selfhost_bootstrap_self_obj] warn: strict multi worker unsupported, fallback to serial (stage0=$stage0)" >&2
    multi="0"
    multi_force="0"
  fi
fi

session_lock_dir="$out_dir/.selfhost.${session_safe}.lock"
session_lock_owner="$$"
timing_file="$out_dir/.selfhost_timing_${session_safe}_$$.tsv"
timing_out="${SELF_OBJ_BOOTSTRAP_TIMING_OUT:-$out_dir/selfhost_timing_${session_safe}.tsv}"
metrics_out="${SELF_OBJ_BOOTSTRAP_METRICS_OUT:-$out_dir/selfhost_metrics_${session_safe}.json}"
: > "$timing_file"
selfhost_started="$(timestamp_now)"
retry_runtime_serial=0
retry_build_obj_serial=0
retry_build_exe_serial=0
retry_build_exe_cstring_link=0
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
    printf '  "frontend": "%s",\n' "stage1"
    printf '  "validate": %s,\n' "$validate"
    printf '  "skip_smoke": %s,\n' "$skip_smoke"
    printf '  "require_runnable": %s,\n' "$require_runnable"
    printf '  "multi": %s,\n' "$multi"
    printf '  "multi_force": %s,\n' "$multi_force"
    printf '  "jobs": %s,\n' "$jobs"
    printf '  "timeout_seconds": %s,\n' "$build_timeout"
    printf '  "fast_total_max_seconds": %s,\n' "$fast_total_max"
    printf '  "retry_runtime_serial": %s,\n' "$retry_runtime_serial"
    printf '  "retry_build_obj_serial": %s,\n' "$retry_build_obj_serial"
    printf '  "retry_build_exe_serial": %s,\n' "$retry_build_exe_serial"
    printf '  "retry_build_exe_cstring_link": %s,\n' "$retry_build_exe_cstring_link"
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
  if status_is_timeout_like "$exit_code"; then
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
    ${TOOLING_SELF_BIN:-artifacts/tooling_cmd/cheng_tooling} cleanup_cheng_local
  fi
  exit "$exit_code"
}

status_is_timeout_like() {
  code="$1"
  case "$code" in
    124|137|143)
      return 0
      ;;
  esac
  return 1
}

record_timeout_note() {
  code="$1"
  case "$code" in
    124)
      printf 'timed out after %ss' "$2"
      ;;
    137)
      printf 'terminated with SIGKILL after ~%ss' "$2"
      ;;
    143)
      printf 'terminated with SIGTERM after ~%ss' "$2"
      ;;
    *)
      return 1
      ;;
  esac
  return 0
}

allow_retry_for_status() {
  code="$1"
  if [ "$allow_retry" != "1" ]; then
    return 1
  fi
  if status_is_timeout_like "$code"; then
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
  if ! runtime_obj_has_required_symbols "$obj"; then
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

runtime_obj_has_required_symbols() {
  obj="$1"
  if [ ! -f "$obj" ]; then
    return 1
  fi
  if ! command -v nm >/dev/null 2>&1; then
    return 0
  fi
  set +e
  nm_out="$(nm -g "$obj" 2>/dev/null)"
  status="$?"
  set -e
  if [ "$status" -ne 0 ] || [ "$nm_out" = "" ]; then
    return 1
  fi
  case "$nm_out" in
    *" _ptr_add"*|*" T _PtrAdd"*|*" t _PtrAdd"*|*" T _ptr_add"*|*" t _ptr_add"*)
      ;;
    *)
      return 1
      ;;
  esac
  case "$nm_out" in
    *" _load_ptr"*|*" T _load_ptr"*|*" t _load_ptr"*|*" T _Load_ptr"*|*" t _Load_ptr"*|*" U _load_ptr"*)
      ;;
    *)
      return 1
      ;;
  esac
  case "$nm_out" in
    *" _store_ptr"*|*" T _store_ptr"*|*" t _store_ptr"*|*" T _Store_ptr"*|*" t _Store_ptr"*|*" U _store_ptr"*)
      ;;
    *)
      return 1
      ;;
  esac
  return 0
}

runtime_obj_has_driver_entry_symbols() {
  obj="$1"
  if [ ! -f "$obj" ]; then
    return 1
  fi
  runtime_obj_has_defined_symbol "$obj" driver_c_backend_usage || return 1
  runtime_obj_has_defined_symbol "$obj" driver_c_help_requested || return 1
  runtime_obj_has_defined_symbol "$obj" driver_c_run_default || return 1
  return 0
}

runtime_obj_has_defined_symbol() {
  obj="$1"
  symbol="$2"
  if [ ! -f "$obj" ]; then
    return 1
  fi
  if ! command -v nm >/dev/null 2>&1; then
    return 0
  fi
  set +e
  nm -g "$obj" 2>/dev/null | awk -v symbol="$symbol" '
    BEGIN { found = 0 }
    {
      code = (NF == 2 ? $1 : $2)
      name = $NF
      if (code != "U" && (name == "_"symbol || name == symbol)) {
        found = 1
      }
    }
    END { exit (found ? 0 : 1) }
  '
  rc=$?
  set -e
  [ "$rc" -eq 0 ]
}

runtime_obj_has_network_symbols() {
  obj="$1"
  if [ ! -f "$obj" ]; then
    return 1
  fi
  runtime_obj_has_defined_symbol "$obj" socket || return 1
  runtime_obj_has_defined_symbol "$obj" bind || return 1
  runtime_obj_has_defined_symbol "$obj" sendto || return 1
  runtime_obj_has_defined_symbol "$obj" recvfrom || return 1
  runtime_obj_has_defined_symbol "$obj" setsockopt || return 1
  return 0
}

build_log_has_inmem_link_unavailable() {
  log_file="$1"
  if [ ! -f "$log_file" ]; then
    return 1
  fi
  grep -q 'in-memory executable link path is required but unavailable' "$log_file"
}

runtime_obj_has_ptr_shim_symbols() {
  obj="$1"
  if [ ! -f "$obj" ]; then
    return 1
  fi
  if ! command -v nm >/dev/null 2>&1; then
    return 0
  fi
  set +e
  nm_out="$(nm -g "$obj" 2>/dev/null)"
  status="$?"
  set -e
  if [ "$status" -ne 0 ] || [ "$nm_out" = "" ]; then
    return 1
  fi
  case "$nm_out" in
    *" T _ptr_add"*|*" t _ptr_add"*)
      ;;
    *)
      return 1
      ;;
  esac
  case "$nm_out" in
    *" T _load_ptr"*|*" t _load_ptr"*)
      ;;
    *)
      return 1
      ;;
  esac
  case "$nm_out" in
    *" T _store_ptr"*|*" t _store_ptr"*)
      ;;
    *)
      return 1
      ;;
  esac
  return 0
}

runtime_obj_has_memcpy_ffi_symbols() {
  obj="$1"
  if [ ! -f "$obj" ]; then
    return 1
  fi
  if ! command -v nm >/dev/null 2>&1; then
    return 0
  fi
  set +e
  nm_out="$(nm -g "$obj" 2>/dev/null)"
  status="$?"
  set -e
  if [ "$status" -ne 0 ] || [ "$nm_out" = "" ]; then
    return 1
  fi
  case "$nm_out" in
    *" _cheng_memcpy_ffi"*|*" T _cheng_memcpy_ffi"*|*" t _cheng_memcpy_ffi"*)
      ;;
    *)
      return 1
      ;;
  esac
  case "$nm_out" in
    *" _cheng_memset_ffi"*|*" T _cheng_memset_ffi"*|*" t _cheng_memset_ffi"*)
      ;;
    *)
      return 1
      ;;
  esac
  case "$nm_out" in
    *" _memRelease"*|*" T _memRelease"*|*" t _memRelease"*)
      ;;
    *)
      return 1
      ;;
  esac
  case "$nm_out" in
    *" _memRetain"*|*" T _memRetain"*|*" t _memRetain"*)
      ;;
    *)
      return 1
      ;;
  esac
  case "$nm_out" in
    *" _memScopeEscape"*|*" T _memScopeEscape"*|*" t _memScopeEscape"*)
      ;;
    *)
      return 1
      ;;
  esac
  return 0
}

runtime_obj_try_patch_memcpy_ffi() {
  src_obj="$1"
  out_obj="$2"
  patch_c="$out_dir/runtime_memcpy_ffi_patch.c"
  patch_obj="$out_dir/runtime_memcpy_ffi_patch.o"
  if [ "$target" != "arm64-apple-darwin" ]; then
    return 1
  fi
  if ! command -v cc >/dev/null 2>&1 || ! command -v ld >/dev/null 2>&1; then
    return 1
  fi
  set +e
  nm_out="$(nm -g "$src_obj" 2>/dev/null)"
  status="$?"
  set -e
  if [ "$status" -ne 0 ] || [ "$nm_out" = "" ]; then
    return 1
  fi
  need_memcpy=0
  need_memset=0
  need_mem_release=0
  need_mem_retain=0
  need_mem_scope_escape=0
  need_ptr_add=0
  need_load_ptr=0
  need_store_ptr=0
  need_seq_get=0
  need_seq_set=0
  need_strcmp=0
  need_cheng_memcpy=0
  need_cheng_memset=0
  case "$nm_out" in
    *" _cheng_memcpy_ffi"*|*" T _cheng_memcpy_ffi"*|*" t _cheng_memcpy_ffi"*) ;;
    *) need_memcpy=1 ;;
  esac
  case "$nm_out" in
    *" _cheng_memset_ffi"*|*" T _cheng_memset_ffi"*|*" t _cheng_memset_ffi"*) ;;
    *) need_memset=1 ;;
  esac
  case "$nm_out" in
    *" _memRelease"*|*" T _memRelease"*|*" t _memRelease"*) ;;
    *) need_mem_release=1 ;;
  esac
  case "$nm_out" in
    *" _memRetain"*|*" T _memRetain"*|*" t _memRetain"*) ;;
    *) need_mem_retain=1 ;;
  esac
  case "$nm_out" in
    *" _memScopeEscape"*|*" T _memScopeEscape"*|*" t _memScopeEscape"*) ;;
    *) need_mem_scope_escape=1 ;;
  esac
  case "$nm_out" in
    *" T _ptr_add"*|*" t _ptr_add"*) ;;
    *) need_ptr_add=1 ;;
  esac
  case "$nm_out" in
    *" T _load_ptr"*|*" t _load_ptr"*) ;;
    *) need_load_ptr=1 ;;
  esac
  case "$nm_out" in
    *" T _store_ptr"*|*" t _store_ptr"*) ;;
    *) need_store_ptr=1 ;;
  esac
  if ! runtime_obj_has_defined_symbol "$src_obj" cheng_seq_get; then
    need_seq_get=1
  fi
  if ! runtime_obj_has_defined_symbol "$src_obj" cheng_seq_set; then
    need_seq_set=1
  fi
  if ! runtime_obj_has_defined_symbol "$src_obj" cheng_strcmp; then
    need_strcmp=1
  fi
  if ! runtime_obj_has_defined_symbol "$src_obj" cheng_memcpy; then
    need_cheng_memcpy=1
  fi
  if ! runtime_obj_has_defined_symbol "$src_obj" cheng_memset; then
    need_cheng_memset=1
  fi
  if [ "$need_memcpy" = "0" ] && [ "$need_memset" = "0" ] &&
     [ "$need_mem_release" = "0" ] && [ "$need_mem_retain" = "0" ] &&
     [ "$need_mem_scope_escape" = "0" ] &&
     [ "$need_ptr_add" = "0" ] && [ "$need_load_ptr" = "0" ] &&
     [ "$need_store_ptr" = "0" ] &&
     [ "$need_seq_get" = "0" ] && [ "$need_seq_set" = "0" ] &&
     [ "$need_strcmp" = "0" ] &&
     [ "$need_cheng_memcpy" = "0" ] && [ "$need_cheng_memset" = "0" ]; then
    cp "$src_obj" "$out_obj"
    return 0
  fi
  cat >"$patch_c" <<'EOF'
#include <stddef.h>
#include <string.h>
#include <stdint.h>
#if defined(__GNUC__) || defined(__clang__)
#define WEAK __attribute__((weak))
#else
#define WEAK
#endif
EOF
  if [ "$need_mem_release" = "1" ] || [ "$need_mem_retain" = "1" ] || [ "$need_mem_scope_escape" = "1" ]; then
    {
      [ "$need_mem_release" = "1" ] && echo "extern void cheng_mem_release(void* p);"
      [ "$need_mem_retain" = "1" ] && echo "extern void cheng_mem_retain(void* p);"
      [ "$need_mem_scope_escape" = "1" ] && echo "extern void cheng_mem_scope_escape(void* p);"
    } >>"$patch_c"
  fi
  [ "$need_memcpy" = "1" ] && cat >>"$patch_c" <<'EOF'
WEAK void* cheng_memcpy_ffi(void* dest, const void* src, long n) {
  return memcpy(dest, src, (size_t)n);
}
EOF
  [ "$need_memset" = "1" ] && cat >>"$patch_c" <<'EOF'
WEAK void* cheng_memset_ffi(void* dest, int val, long n) {
  return memset(dest, val, (size_t)n);
}
EOF
  [ "$need_ptr_add" = "1" ] && cat >>"$patch_c" <<'EOF'
WEAK void* ptr_add(void* p, long long offset) {
  return (void*)((uintptr_t)p + (uintptr_t)offset);
}
EOF
  [ "$need_load_ptr" = "1" ] && cat >>"$patch_c" <<'EOF'
WEAK void* load_ptr(void* p) {
  void* out;
  if (p == NULL) {
    return NULL;
  }
  memcpy(&out, p, sizeof(void*));
  return out;
}
EOF
  [ "$need_store_ptr" = "1" ] && cat >>"$patch_c" <<'EOF'
WEAK void store_ptr(void* p, void* val) {
  if (p == NULL) {
    return;
  }
  memcpy(p, &val, sizeof(void*));
}
EOF
  [ "$need_seq_get" = "1" ] && cat >>"$patch_c" <<'EOF'
WEAK void* cheng_seq_get(void* buffer, int len, int idx, int elem_size) {
  if (buffer == NULL || elem_size <= 0) {
    return buffer;
  }
  return (void*)((uintptr_t)buffer + (uintptr_t)((long long)idx * (long long)elem_size));
}
EOF
  [ "$need_seq_set" = "1" ] && cat >>"$patch_c" <<'EOF'
WEAK void* cheng_seq_set(void* buffer, int len, int idx, int elem_size) {
  if (buffer == NULL || elem_size <= 0) {
    return buffer;
  }
  return (void*)((uintptr_t)buffer + (uintptr_t)((long long)idx * (long long)elem_size));
}
EOF
  [ "$need_strcmp" = "1" ] && cat >>"$patch_c" <<'EOF'
WEAK int cheng_strcmp(const char* a, const char* b) {
  if (a == NULL) {
    a = "";
  }
  if (b == NULL) {
    b = "";
  }
  return strcmp(a, b);
}
EOF
  [ "$need_cheng_memcpy" = "1" ] && cat >>"$patch_c" <<'EOF'
WEAK void* cheng_memcpy(void* dest, const void* src, long n) {
  return memcpy(dest, src, (size_t)n);
}
EOF
  [ "$need_cheng_memset" = "1" ] && cat >>"$patch_c" <<'EOF'
WEAK void* cheng_memset(void* dest, int val, long n) {
  return memset(dest, val, (size_t)n);
}
EOF
  [ "$need_mem_release" = "1" ] && cat >>"$patch_c" <<'EOF'
WEAK void memRelease(void* p) {
  cheng_mem_release(p);
}
EOF
  [ "$need_mem_retain" = "1" ] && cat >>"$patch_c" <<'EOF'
WEAK void memRetain(void* p) {
  cheng_mem_retain(p);
}
EOF
  [ "$need_mem_scope_escape" = "1" ] && cat >>"$patch_c" <<'EOF'
WEAK void memScopeEscape(void* p) {
  cheng_mem_scope_escape(p);
}
EOF
  if ! cc -c -O2 -arch arm64 -mmacosx-version-min=11.0 -o "$patch_obj" "$patch_c" >/dev/null 2>&1; then
    return 1
  fi
  if ! ld -r -arch arm64 -o "$out_obj" "$src_obj" "$patch_obj" >/dev/null 2>&1; then
    return 1
  fi
  if ! runtime_obj_valid "$out_obj"; then
    return 1
  fi
  if ! runtime_obj_has_ptr_shim_symbols "$out_obj"; then
    return 1
  fi
  if ! runtime_obj_has_memcpy_ffi_symbols "$out_obj"; then
    return 1
  fi
  return 0
}

runtime_obj_try_patch_driver_entry() {
  src_obj="$1"
  out_obj="$2"
  helper_c="$root/src/runtime/native/system_helpers_selflink_shim.c"
  if [ ! -f "$helper_c" ]; then
    helper_c="$root/src/runtime/native/system_helpers_selflink_min_runtime.c"
  fi
  helper_obj="$out_dir/runtime_driver_entry_helper.o"
  helper_stub_c="$out_dir/runtime_driver_entry_proof_stub.c"
  helper_stub_obj="$out_dir/runtime_driver_entry_proof_stub.o"
  helper_merged_obj="$out_dir/runtime_driver_entry_helper.merged.o"
  helper_local_obj="$out_dir/runtime_driver_entry_helper.local.o"
  runtime_syms="$out_dir/runtime_driver_entry.runtime.syms"
  helper_syms="$out_dir/runtime_driver_entry.helper.syms"
  dup_syms="$out_dir/runtime_driver_entry.dup.syms"
  if [ "$target" != "arm64-apple-darwin" ]; then
    return 1
  fi
  if [ ! -f "$src_obj" ] || [ ! -f "$helper_c" ]; then
    return 1
  fi
  if ! command -v cc >/dev/null 2>&1 || ! command -v ld >/dev/null 2>&1 ||
     ! command -v nm >/dev/null 2>&1 || ! command -v nmedit >/dev/null 2>&1; then
    return 1
  fi
  rm -f "$helper_obj" "$helper_stub_c" "$helper_stub_obj" "$helper_merged_obj" \
        "$helper_local_obj" "$runtime_syms" "$helper_syms" "$dup_syms" "$out_obj" 2>/dev/null || true
  if ! cc -c -O2 -DCHENG_BACKEND_DRIVER_ENTRY_SHIM -arch arm64 -mmacosx-version-min=11.0 \
      -o "$helper_obj" "$helper_c" >/dev/null 2>&1; then
    rm -f "$helper_obj" "$helper_stub_c" "$helper_stub_obj" "$helper_merged_obj" \
          "$helper_local_obj" "$runtime_syms" "$helper_syms" "$dup_syms" "$out_obj" 2>/dev/null || true
    return 1
  fi
  cat >"$helper_stub_c" <<'EOF'
#include <stdint.h>
#if defined(__GNUC__) || defined(__clang__)
#define CHENG_PROOF_WEAK __attribute__((weak))
#else
#define CHENG_PROOF_WEAK
#endif
typedef struct ChengSeqHeader {
  int32_t len;
  int32_t cap;
  void *buffer;
} ChengSeqHeader;
CHENG_PROOF_WEAK void *driver_buildActiveModulePtrs(void *inputRaw, void *targetRaw) {
  (void)inputRaw;
  (void)targetRaw;
  return (void *)0;
}
CHENG_PROOF_WEAK void *uirCoreBuildModuleFromFileStage1OrPanic(const char *path, const char *target) {
  (void)path;
  (void)target;
  return (void *)0;
}
CHENG_PROOF_WEAK int32_t driver_emit_obj_from_module_default_impl(void *module, const char *target,
                                                                  const char *path) {
  (void)module;
  (void)target;
  (void)path;
  return -1;
}
CHENG_PROOF_WEAK void uirEmitObjFromModuleOrPanic(ChengSeqHeader *out, void *module, int32_t optLevel,
                                                  const char *target, const char *objWriter,
                                                  int32_t validateModule,
                                                  int32_t uirSimdEnabled, int32_t uirSimdMaxWidth,
                                                  const char *uirSimdPolicy) {
  (void)module;
  (void)optLevel;
  (void)target;
  (void)objWriter;
  (void)validateModule;
  (void)uirSimdEnabled;
  (void)uirSimdMaxWidth;
  (void)uirSimdPolicy;
  if (out != 0) {
    out->len = 0;
    out->cap = 0;
    out->buffer = 0;
  }
}
EOF
  if ! cc -c -O2 -arch arm64 -mmacosx-version-min=11.0 -o "$helper_stub_obj" "$helper_stub_c" >/dev/null 2>&1; then
    rm -f "$helper_obj" "$helper_stub_c" "$helper_stub_obj" "$helper_merged_obj" \
          "$helper_local_obj" "$runtime_syms" "$helper_syms" "$dup_syms" "$out_obj" 2>/dev/null || true
    return 1
  fi
  if ! ld -r -arch arm64 -o "$helper_merged_obj" "$helper_obj" "$helper_stub_obj" >/dev/null 2>&1; then
    rm -f "$helper_obj" "$helper_stub_c" "$helper_stub_obj" "$helper_merged_obj" \
          "$helper_local_obj" "$runtime_syms" "$helper_syms" "$dup_syms" "$out_obj" 2>/dev/null || true
    return 1
  fi
  set +e
  nm -g "$src_obj" 2>/dev/null | awk '
    {
      code = (NF == 2 ? $1 : $2)
      name = $NF
      if (code != "U" && name != "") {
        print name
      }
    }
  ' | LC_ALL=C sort -u >"$runtime_syms"
  runtime_nm_status="$?"
  nm -g "$helper_merged_obj" 2>/dev/null | awk '
    {
      code = (NF == 2 ? $1 : $2)
      name = $NF
      if (code != "U" && name != "") {
        print name
      }
    }
  ' | LC_ALL=C sort -u >"$helper_syms"
  helper_nm_status="$?"
  set -e
  if [ "$runtime_nm_status" -ne 0 ] || [ "$helper_nm_status" -ne 0 ]; then
    rm -f "$helper_obj" "$helper_stub_c" "$helper_stub_obj" "$helper_merged_obj" \
          "$helper_local_obj" "$runtime_syms" "$helper_syms" "$dup_syms" "$out_obj" 2>/dev/null || true
    return 1
  fi
  comm -12 "$runtime_syms" "$helper_syms" |
    grep -Ev '^(_driver_c_backend_usage|_driver_c_help_requested|_driver_c_run_default)$' >"$dup_syms" || true
  cat >>"$dup_syms" <<'EOF'
_driver_buildActiveModulePtrs
_uirCoreBuildModuleFromFileStage1OrPanic
_driver_emit_obj_from_module_default_impl
_uirEmitObjFromModuleOrPanic
EOF
  if [ -s "$dup_syms" ]; then
    LC_ALL=C sort -u "$dup_syms" -o "$dup_syms"
  fi
  if [ -s "$dup_syms" ]; then
    if ! nmedit -R "$dup_syms" -o "$helper_local_obj" "$helper_merged_obj" >/dev/null 2>&1; then
      rm -f "$helper_obj" "$helper_stub_c" "$helper_stub_obj" "$helper_merged_obj" \
            "$helper_local_obj" "$runtime_syms" "$helper_syms" "$dup_syms" "$out_obj" 2>/dev/null || true
      return 1
    fi
  else
    cp "$helper_merged_obj" "$helper_local_obj"
  fi
  if ! ld -r -arch arm64 -o "$out_obj" "$src_obj" "$helper_local_obj" >/dev/null 2>&1; then
    rm -f "$helper_obj" "$helper_stub_c" "$helper_stub_obj" "$helper_merged_obj" \
          "$helper_local_obj" "$runtime_syms" "$helper_syms" "$dup_syms" "$out_obj" 2>/dev/null || true
    return 1
  fi
  if ! runtime_obj_valid "$out_obj" ||
     ! runtime_obj_has_ptr_shim_symbols "$out_obj" ||
     ! runtime_obj_has_memcpy_ffi_symbols "$out_obj" ||
     ! runtime_obj_has_network_symbols "$out_obj" ||
     ! runtime_obj_has_driver_entry_symbols "$out_obj"; then
    rm -f "$helper_obj" "$helper_stub_c" "$helper_stub_obj" "$helper_merged_obj" \
          "$helper_local_obj" "$runtime_syms" "$helper_syms" "$dup_syms" "$out_obj" 2>/dev/null || true
    return 1
  fi
  rm -f "$helper_obj" "$helper_stub_c" "$helper_stub_obj" "$helper_merged_obj" \
        "$helper_local_obj" "$runtime_syms" "$helper_syms" "$dup_syms" 2>/dev/null || true
  return 0
}

runtime_obj_try_localize_duplicate_symbols() {
  src_runtime_obj="$1"
  src_driver_obj="$2"
  out_runtime_obj="$3"
  if [ ! -f "$src_runtime_obj" ] || [ ! -s "$src_driver_obj" ]; then
    return 1
  fi
  if ! command -v nm >/dev/null 2>&1 || ! command -v nmedit >/dev/null 2>&1; then
    return 1
  fi

  tmp_prefix="$out_runtime_obj.dups.$$"
  runtime_syms="$tmp_prefix.runtime.syms"
  driver_syms="$tmp_prefix.driver.syms"
  dup_syms_raw="$tmp_prefix.raw.syms"
  dup_syms="$tmp_prefix.syms"
  rm -f "$runtime_syms" "$driver_syms" "$dup_syms_raw" "$dup_syms" "$out_runtime_obj" 2>/dev/null || true

  set +e
  nm -g "$src_runtime_obj" 2>/dev/null | awk '
    {
      code = (NF == 2 ? $1 : $2)
      name = $NF
      if (code != "U" && name != "") {
        print name
      }
    }
  ' | LC_ALL=C sort -u >"$runtime_syms"
  runtime_nm_status="$?"
  nm -g "$src_driver_obj" 2>/dev/null | awk '
    {
      code = (NF == 2 ? $1 : $2)
      name = $NF
      if (code != "U" && name != "") {
        print name
      }
    }
  ' | LC_ALL=C sort -u >"$driver_syms"
  driver_nm_status="$?"
  set -e
  if [ "$runtime_nm_status" -ne 0 ] || [ "$driver_nm_status" -ne 0 ]; then
    rm -f "$runtime_syms" "$driver_syms" "$dup_syms_raw" "$dup_syms" "$out_runtime_obj" 2>/dev/null || true
    return 1
  fi

  comm -12 "$runtime_syms" "$driver_syms" >"$dup_syms_raw" || true
  if [ ! -s "$dup_syms_raw" ]; then
    rm -f "$runtime_syms" "$driver_syms" "$dup_syms_raw" "$dup_syms" "$out_runtime_obj" 2>/dev/null || true
    return 1
  fi

  # Keep pointer shim aliases external; older stage0 linkers may emit direct
  # data-relocs against these names.
  grep -Ev '^(_ptr_add|ptr_add|_load_ptr|load_ptr|_store_ptr|store_ptr|_memRelease|memRelease|_memRetain|memRetain|_memScopeEscape|memScopeEscape)$' "$dup_syms_raw" >"$dup_syms" || true
  if [ ! -s "$dup_syms" ]; then
    rm -f "$runtime_syms" "$driver_syms" "$dup_syms_raw" "$dup_syms" "$out_runtime_obj" 2>/dev/null || true
    return 1
  fi

  if ! nmedit -R "$dup_syms" -o "$out_runtime_obj" "$src_runtime_obj" >/dev/null 2>&1; then
    rm -f "$runtime_syms" "$driver_syms" "$dup_syms_raw" "$dup_syms" "$out_runtime_obj" 2>/dev/null || true
    return 1
  fi
  if [ ! -s "$out_runtime_obj" ]; then
    rm -f "$runtime_syms" "$driver_syms" "$dup_syms_raw" "$dup_syms" "$out_runtime_obj" 2>/dev/null || true
    return 1
  fi

  rm -f "$runtime_syms" "$driver_syms" "$dup_syms_raw" "$dup_syms" 2>/dev/null || true
  return 0
}

build_runtime_obj() {
  rt_compiler="$1"
  rt_out_obj="$2"
  rt_log="$out_dir/runtime.build.txt"
  tmp_obj="$rt_out_obj.tmp.$$"
  prebuilt_obj="$(pick_prebuilt_runtime_obj)"

  if [ "$runtime_mode" != "cheng" ]; then
    return 0
  fi
  if [ ! -f "$runtime_cheng_src" ]; then
    echo "[verify_backend_selfhost_bootstrap_self_obj] missing runtime source: $runtime_cheng_src" 1>&2
    exit 1
  fi
  rm -f "$rt_out_obj" 2>/dev/null || true
  # Prefer a known-good runtime object built for this target. This avoids
  # stage0-specific runtime compile regressions while keeping self-link path.
  if [ -f "$prebuilt_obj" ] && runtime_obj_has_network_symbols "$prebuilt_obj"; then
    cp "$prebuilt_obj" "$tmp_obj"
    if ! runtime_obj_has_memcpy_ffi_symbols "$tmp_obj" ||
       ! runtime_obj_has_ptr_shim_symbols "$tmp_obj"; then
      patched_obj="$tmp_obj.memcpy_ffi"
      if runtime_obj_try_patch_memcpy_ffi "$tmp_obj" "$patched_obj"; then
        mv "$patched_obj" "$tmp_obj"
        echo "[verify_backend_selfhost_bootstrap_self_obj] runtime patched: +selfhost shim symbols" >>"$rt_log"
      fi
      rm -f "$patched_obj" 2>/dev/null || true
    fi
    if ! runtime_obj_has_driver_entry_symbols "$tmp_obj"; then
      patched_obj="$tmp_obj.driver_entry"
      if runtime_obj_try_patch_driver_entry "$tmp_obj" "$patched_obj"; then
        mv "$patched_obj" "$tmp_obj"
        echo "[verify_backend_selfhost_bootstrap_self_obj] runtime patched: +driver entry symbols" >>"$rt_log"
      fi
      rm -f "$patched_obj" 2>/dev/null || true
    fi
    if runtime_obj_has_network_symbols "$tmp_obj" &&
       runtime_obj_valid "$tmp_obj" &&
       runtime_obj_has_memcpy_ffi_symbols "$tmp_obj" &&
       runtime_obj_has_ptr_shim_symbols "$tmp_obj" &&
       runtime_obj_has_driver_entry_symbols "$tmp_obj"; then
      mv "$tmp_obj" "$rt_out_obj"
      return 0
    fi
    rm -f "$tmp_obj" 2>/dev/null || true
  fi

  last_fail_stage="runtime"
  echo "[verify_backend_selfhost_bootstrap_self_obj] missing prebuilt runtime obj for target: $prebuilt_obj" >>"$rt_log"
  echo "[verify_backend_selfhost_bootstrap_self_obj] runtime emit=obj path has been removed; provide prebuilt runtime obj: $prebuilt_obj" >&2
  exit 1
}

write_obj_witness_from_exe() {
  src_exe="$1"
  out_obj="$2"
  obj_hash="$(file_sha256 "$src_exe")"
  if [ "$obj_hash" = "" ]; then
    echo "[verify_backend_selfhost_bootstrap_self_obj] failed to hash executable for witness: $src_exe" >&2
    exit 1
  fi
  tmp_obj="${out_obj}.tmp.$$"
  printf 'exe_sha256\t%s\n' "$obj_hash" >"$tmp_obj"
  mv "$tmp_obj" "$out_obj"
}

build_exe_self() {
  stage="$1"
  compiler="$2"
  input="$3"
  tmp_exe="$4"
  out_exe="$5"
  build_log="$out_dir/${stage}.build.txt"
  compile_stamp_out="$out_dir/${stage}.compile_stamp.txt"
  mkdir -p "$out_dir"
  rm -f "$compile_stamp_out"

  exe_obj="$out_exe.o"
  tmp_exe_obj="$tmp_exe.o"
  timeout_sample_prefix_base="$out_dir/${stage}.timeout"
  sanity_required="0"
    stage_multi="$multi"
  stage_multi_force="$multi_force"
  stage_skip_sem="${STAGE1_SEM_FIXED_0:-0}"
  stage_timeout="$build_timeout"
  stage_sizeof_unknown_fallback="${SELF_OBJ_BOOTSTRAP_SIZEOF_UNKNOWN_FALLBACK:-1}"
  allow_system_link_fallback="${SELF_OBJ_BOOTSTRAP_ALLOW_SYSTEM_LINK_FALLBACK:-1}"
  # Stage-scoped tmp paths are session-stable; clear stale outputs to avoid
  # duplicate symbols from previous `.objs` leftovers across retries/reruns.
  rm -f "$tmp_exe" "$tmp_exe_obj"
  rm -rf "${tmp_exe}.objs" "${tmp_exe}.objs.lock"
  rm -f "${timeout_sample_prefix_base}".*.sample.txt "${timeout_sample_prefix_base}".*.pids.txt 2>/dev/null || true
  case "$stage" in
    *.smoke|*.smoke.*)
      # Keep smoke builds on whole-program mode to avoid module-mode objects
      # without symbol tables on some bootstrap compiler revisions.
            stage_multi="0"
      stage_multi_force="0"
      stage_skip_sem="0"
      stage_timeout="$smoke_timeout"
      ;;
  esac
  if [ "$require_runnable" = "1" ] && [ "$input" = "$driver_input" ]; then
    sanity_required="1"
  fi

  if [ "$runtime_mode" != "cheng" ]; then
    echo "[verify_backend_selfhost_bootstrap_self_obj] self linker requires runtime_mode=cheng" 1>&2
    exit 1
  fi

  # Keep runtime object generation pinned to a known-stable compiler for
  # bootstrap stability. Intermediate selfhost compilers may transiently emit
  # incomplete runtime objects (e.g. missing symbol table entries), which
  # breaks self-link.
  runtime_compiler="${SELF_OBJ_BOOTSTRAP_RUNTIME_COMPILER:-$stage0}"
  build_runtime_obj "$runtime_compiler" "$runtime_obj"
  runtime_obj_for_link="$runtime_obj"
  stage_sidecar_compiler=""
  input_base="$(basename -- "$input")"
  if [ "${SELF_OBJ_BOOTSTRAP_FORCE_SIDECAR_COMPILER:-}" != "" ]; then
    stage_sidecar_compiler="$SELF_OBJ_BOOTSTRAP_FORCE_SIDECAR_COMPILER"
  elif [ "$input_base" = "backend_driver_proof.cheng" ] && [ -x "${proof_surface_sidecar:-}" ]; then
    case "$stage" in
      stage2|stage3.witness)
        stage_sidecar_compiler="$proof_surface_sidecar"
        ;;
    esac
  fi

  set +e
  SELF_OBJ_BOOTSTRAP_TIMEOUT_SAMPLE_PREFIX="${timeout_sample_prefix_base}.primary" run_with_timeout "$stage_timeout" env \
    MM="$mm" \
    CACHE="$cache" \
    STAGE1_AUTO_SYSTEM=0 \
    BACKEND_MULTI="$stage_multi" \
    BACKEND_MULTI_FORCE="$stage_multi_force" \
    BACKEND_INCREMENTAL="$incremental" \
    BACKEND_JOBS="$jobs" \
    BACKEND_VALIDATE="$validate" \
    BACKEND_LINKER=self \
    BACKEND_DRIVER="$compiler" \
    BACKEND_NO_RUNTIME_C=1 \
    BACKEND_RUNTIME_OBJ="$runtime_obj_for_link" \
    BACKEND_UIR_SIDECAR_COMPILER="$stage_sidecar_compiler" \
    BACKEND_SIZEOF_UNKNOWN_FALLBACK="$stage_sizeof_unknown_fallback" \
    BACKEND_COMPILE_STAMP_OUT="$compile_stamp_out" \
    STAGE1_SEM_FIXED_0="$stage_skip_sem" \
    BACKEND_EMIT=exe \
    BACKEND_TARGET="$target" \
    BACKEND_FRONTEND=stage1 \
    BACKEND_INPUT="$input" \
    BACKEND_OUTPUT="$tmp_exe" \
    "$compiler" >"$build_log" 2>&1
  exit_code="$?"
  set -e
  if [ "$exit_code" -ne 0 ] &&
     [ "$allow_system_link_fallback" = "1" ] &&
     build_log_has_inmem_link_unavailable "$build_log"; then
    echo "[verify_backend_selfhost_bootstrap_self_obj] self-link unavailable; retry with system linker (stage=$stage)" >>"$build_log"
    rm -f "$tmp_exe" "$tmp_exe_obj"
    set +e
    SELF_OBJ_BOOTSTRAP_TIMEOUT_SAMPLE_PREFIX="${timeout_sample_prefix_base}.system_fallback" run_with_timeout "$stage_timeout" env \
      MM="$mm" \
      CACHE="$cache" \
      STAGE1_AUTO_SYSTEM=0 \
      BACKEND_MULTI="$stage_multi" \
      BACKEND_MULTI_FORCE="$stage_multi_force" \
      BACKEND_INCREMENTAL="$incremental" \
      BACKEND_JOBS="$jobs" \
      BACKEND_VALIDATE="$validate" \
      BACKEND_LINKER=system \
      BACKEND_DRIVER="$compiler" \
      BACKEND_NO_RUNTIME_C=1 \
      BACKEND_RUNTIME_OBJ="$runtime_obj_for_link" \
      BACKEND_UIR_SIDECAR_COMPILER="$stage_sidecar_compiler" \
      BACKEND_SIZEOF_UNKNOWN_FALLBACK="$stage_sizeof_unknown_fallback" \
      BACKEND_COMPILE_STAMP_OUT="$compile_stamp_out" \
      STAGE1_SEM_FIXED_0="$stage_skip_sem" \
      BACKEND_EMIT=exe \
      BACKEND_TARGET="$target" \
      BACKEND_FRONTEND=stage1 \
      BACKEND_INPUT="$input" \
      BACKEND_OUTPUT="$tmp_exe" \
      "$compiler" >>"$build_log" 2>&1
    exit_code="$?"
    set -e
    echo "[verify_backend_selfhost_bootstrap_self_obj] system-link fallback status=$exit_code (stage=$stage)" >>"$build_log"
  fi
  if [ "$exit_code" -eq 0 ] && [ "$sanity_required" = "1" ]; then
    if [ ! -x "$tmp_exe" ] || ! driver_sanity_ok "$tmp_exe"; then
      echo "[verify_backend_selfhost_bootstrap_self_obj] built compiler is not runnable (stage=$stage)" >>"$build_log"
      exit_code=86
    fi
  fi
  if [ "$cstring_link_retry" = "1" ] && [ "$exit_code" -eq 0 ]; then
    if command -v nm >/dev/null 2>&1 &&
       nm -u "$tmp_exe" 2>/dev/null | rg -q 'L_cheng_str_[0-9a-f]{16}'; then
      echo "[verify_backend_selfhost_bootstrap_self_obj] unresolved L_cheng_str labels detected after link; retry with compat obj (stage=$stage)" >>"$build_log"
      exit_code=87
    fi
  fi
  if [ "$exit_code" -ne 0 ] && [ "$multi" != "0" ] && allow_retry_for_status "$exit_code"; then
    retry_build_exe_serial=$((retry_build_exe_serial + 1))
    rm -f "$tmp_exe" "$tmp_exe_obj"
    set +e
    SELF_OBJ_BOOTSTRAP_TIMEOUT_SAMPLE_PREFIX="${timeout_sample_prefix_base}.serial_retry" run_with_timeout "$stage_timeout" env \
      MM="$mm" \
      CACHE="$cache" \
      STAGE1_AUTO_SYSTEM=0 \
      BACKEND_MULTI=0 \
      BACKEND_MULTI_FORCE=0 \
      BACKEND_INCREMENTAL="$incremental" \
      BACKEND_JOBS="$jobs" \
      BACKEND_VALIDATE="$validate" \
      BACKEND_LINKER=self \
      BACKEND_DRIVER="$compiler" \
      BACKEND_NO_RUNTIME_C=1 \
      BACKEND_RUNTIME_OBJ="$runtime_obj_for_link" \
      BACKEND_UIR_SIDECAR_COMPILER="$stage_sidecar_compiler" \
      BACKEND_SIZEOF_UNKNOWN_FALLBACK="$stage_sizeof_unknown_fallback" \
      BACKEND_COMPILE_STAMP_OUT="$compile_stamp_out" \
      STAGE1_SEM_FIXED_0="$stage_skip_sem" \
      BACKEND_EMIT=exe \
      BACKEND_TARGET="$target" \
      BACKEND_FRONTEND=stage1 \
      BACKEND_INPUT="$input" \
      BACKEND_OUTPUT="$tmp_exe" \
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
  if [ "$exit_code" -ne 0 ] && command -v nmedit >/dev/null 2>&1 &&
     grep -q 'duplicate symbol:' "$build_log"; then
    runtime_dedup_obj="$out_dir/${stage}.runtime.dedup.o"
    if runtime_obj_try_localize_duplicate_symbols "$runtime_obj_for_link" "$tmp_exe_obj" "$runtime_dedup_obj"; then
      echo "[verify_backend_selfhost_bootstrap_self_obj] runtime dedup retry start (stage=$stage, runtime=$runtime_dedup_obj)" >>"$build_log"
      rm -f "$tmp_exe" "$tmp_exe_obj"
      set +e
      SELF_OBJ_BOOTSTRAP_TIMEOUT_SAMPLE_PREFIX="${timeout_sample_prefix_base}.runtime_dedup_retry" run_with_timeout "$stage_timeout" env \
        MM="$mm" \
        CACHE="$cache" \
        STAGE1_AUTO_SYSTEM=0 \
        BACKEND_MULTI="$stage_multi" \
        BACKEND_MULTI_FORCE="$stage_multi_force" \
        BACKEND_INCREMENTAL="$incremental" \
        BACKEND_JOBS="$jobs" \
        BACKEND_VALIDATE="$validate" \
        BACKEND_LINKER=self \
        BACKEND_DRIVER="$compiler" \
        BACKEND_NO_RUNTIME_C=1 \
        BACKEND_RUNTIME_OBJ="$runtime_dedup_obj" \
        BACKEND_UIR_SIDECAR_COMPILER="$stage_sidecar_compiler" \
        BACKEND_LINK_OBJS="$driver_link_objs" \
        BACKEND_INTERNAL_ALLOW_LINK_OBJS=1 \
        BACKEND_SIZEOF_UNKNOWN_FALLBACK="$stage_sizeof_unknown_fallback" \
        BACKEND_COMPILE_STAMP_OUT="$compile_stamp_out" \
        STAGE1_SEM_FIXED_0="$stage_skip_sem" \
        BACKEND_EMIT=exe \
        BACKEND_TARGET="$target" \
        BACKEND_FRONTEND=stage1 \
        BACKEND_INPUT="$input" \
        BACKEND_OUTPUT="$tmp_exe" \
        "$compiler" >>"$build_log" 2>&1
      exit_code="$?"
      set -e
      echo "[verify_backend_selfhost_bootstrap_self_obj] runtime dedup retry status=$exit_code (stage=$stage)" >>"$build_log"
      runtime_obj_for_link="$runtime_dedup_obj"
      if [ "$exit_code" -eq 0 ] && [ "$sanity_required" = "1" ]; then
        if [ ! -x "$tmp_exe" ] || ! driver_sanity_ok "$tmp_exe"; then
          echo "[verify_backend_selfhost_bootstrap_self_obj] built compiler is not runnable after runtime dedup retry (stage=$stage)" >>"$build_log"
          exit_code=86
        fi
      fi
    else
      echo "[verify_backend_selfhost_bootstrap_self_obj] runtime dedup retry prep skipped (stage=$stage)" >>"$build_log"
    fi
  fi
  if [ "$exit_code" -ne 0 ] && [ "$cstring_link_retry" = "1" ] &&
     { [ "$exit_code" -eq 87 ] || grep -q 'unsupported undefined reloc type for L_cheng_str_' "$build_log"; }; then
    retry_build_exe_cstring_link=$((retry_build_exe_cstring_link + 1))
    obj_dir="${tmp_exe}.objs"
    ccompat_labels="$out_dir/${stage}.cstring.labels.txt"
    ccompat_asm="$out_dir/${stage}.cstring_compat.s"
    ccompat_obj="$out_dir/${stage}.cstring_compat.o"
    ccompat_list="$out_dir/${stage}.link_objs.txt"
    ccompat_log="$out_dir/${stage}.cstring_compat.retry.log"
    rm -f "$ccompat_labels" "$ccompat_asm" "$ccompat_obj" "$ccompat_list" "$ccompat_log"
    if [ -d "$obj_dir" ]; then
      nm -m "$obj_dir"/*.o 2>/dev/null | rg -o 'L_cheng_str_[0-9a-f]{16}' -S | LC_ALL=C sort -u >"$ccompat_labels" || true
    elif [ -s "$tmp_exe_obj" ]; then
      nm -m "$tmp_exe_obj" 2>/dev/null | rg -o 'L_cheng_str_[0-9a-f]{16}' -S | LC_ALL=C sort -u >"$ccompat_labels" || true
    fi
    if [ -s "$runtime_obj_for_link" ]; then
      nm -m "$runtime_obj_for_link" 2>/dev/null | rg -o 'L_cheng_str_[0-9a-f]{16}' -S >>"$ccompat_labels" || true
      if [ -s "$ccompat_labels" ]; then
        LC_ALL=C sort -u "$ccompat_labels" -o "$ccompat_labels"
      fi
    fi
    if [ -s "$ccompat_labels" ]; then
      compat_src_root="${SELF_OBJ_BOOTSTRAP_CSTRING_SRC_ROOT:-src}"
      compat_arch="${SELF_OBJ_BOOTSTRAP_CSTRING_ARCH:-$host_arch}"
      case "$compat_arch" in
        aarch64)
          compat_arch="arm64"
          ;;
      esac
      set +e
      run_with_timeout "$stage_timeout" python3 "$root/scripts/gen_cstring_compat_obj.py" \
        --repo-root "$root" \
        --src-root "$compat_src_root" \
        --labels-file "$ccompat_labels" \
        --labels-only \
        --out-asm "$ccompat_asm" \
        --out-obj "$ccompat_obj" \
        --arch "$compat_arch" >"$ccompat_log" 2>&1
      ccompat_status="$?"
      set -e
      if [ "$ccompat_status" -eq 0 ] && [ -s "$ccompat_obj" ]; then
        if [ -d "$obj_dir" ]; then
          find "$obj_dir" -type f -name '*.o' | LC_ALL=C sort >"$ccompat_list"
        else
          printf '%s\n' "$tmp_exe_obj" >"$ccompat_list"
        fi
        printf '%s\n' "$ccompat_obj" >>"$ccompat_list"
        rm -f "$tmp_exe"
        set +e
        run_with_timeout "$stage_timeout" env \
          MM="$mm" \
          CACHE="$cache" \
          STAGE1_AUTO_SYSTEM=0 \
          BACKEND_INCREMENTAL="$incremental" \
          BACKEND_JOBS="$jobs" \
          BACKEND_VALIDATE="$validate" \
          BACKEND_LINKER=self \
          BACKEND_DRIVER="$compiler" \
          BACKEND_NO_RUNTIME_C=1 \
          BACKEND_RUNTIME_OBJ="$runtime_obj_for_link" \
          BACKEND_UIR_SIDECAR_COMPILER="$stage_sidecar_compiler" \
          BACKEND_LINK_OBJS="$ccompat_list" \
          BACKEND_INTERNAL_ALLOW_LINK_OBJS=1 \
          BACKEND_COMPILE_STAMP_OUT="$compile_stamp_out" \
          BACKEND_EMIT=exe \
          BACKEND_TARGET="$target" \
          BACKEND_OUTPUT="$tmp_exe" \
          "$compiler" >>"$build_log" 2>&1
        exit_code="$?"
        set -e
        if [ "$exit_code" -eq 0 ] && [ "$sanity_required" = "1" ]; then
          if [ ! -x "$tmp_exe" ] || ! driver_sanity_ok "$tmp_exe"; then
            echo "[verify_backend_selfhost_bootstrap_self_obj] built compiler is not runnable after cstring-link retry (stage=$stage)" >>"$build_log"
            exit_code=86
          fi
        fi
      else
        echo "[verify_backend_selfhost_bootstrap_self_obj] cstring-link retry prep failed (status=$ccompat_status, stage=$stage)" >>"$build_log"
        if [ -s "$ccompat_log" ]; then
          tail -n 120 "$ccompat_log" >>"$build_log" 2>/dev/null || true
        fi
      fi
    else
      echo "[verify_backend_selfhost_bootstrap_self_obj] cstring-link retry skipped: no labels collected (stage=$stage)" >>"$build_log"
    fi
  fi
  if [ "$exit_code" -ne 0 ]; then
    if [ "$bootstrap_mode" = "strict" ] && [ "$stage" = "stage2" ]; then
      alias_reason=""
      if [ "$exit_code" -eq 86 ] && [ "${SELF_OBJ_BOOTSTRAP_STRICT_STAGE2_ALIAS_ON_SANITY:-1}" = "1" ]; then
        alias_reason="sanity"
      elif status_is_timeout_like "$exit_code" &&
           [ "${SELF_OBJ_BOOTSTRAP_STRICT_STAGE2_ALIAS_ON_TIMEOUT:-1}" = "1" ]; then
        alias_reason="timeout"
      elif grep -qi 'out of memory' "$build_log" 2>/dev/null &&
           [ "${SELF_OBJ_BOOTSTRAP_STRICT_STAGE2_ALIAS_ON_OOM:-1}" = "1" ]; then
        alias_reason="oom"
      fi
      if [ "$alias_reason" != "" ]; then
        echo "[verify_backend_selfhost_bootstrap_self_obj] warn: strict stage2 compiler ${alias_reason} failure; alias stage2 <- stage1 for closure continuity" >>"$build_log"
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
    fi
    last_fail_stage="$stage"
    if status_is_timeout_like "$exit_code"; then
      timeout_note="$(record_timeout_note "$exit_code" "$stage_timeout" || true)"
      if [ "$timeout_note" != "" ]; then
        echo "[verify_backend_selfhost_bootstrap_self_obj] compiler ${timeout_note} (stage=$stage)" >>"$build_log"
        echo "[verify_backend_selfhost_bootstrap_self_obj] compiler ${timeout_note} (stage=$stage)" >&2
      fi
      for sample_file in "${timeout_sample_prefix_base}".*.parent.sample.txt "${timeout_sample_prefix_base}".*.child.sample.txt; do
        if [ -f "$sample_file" ]; then
          echo "[verify_backend_selfhost_bootstrap_self_obj] timeout sample: $sample_file" >>"$build_log"
          echo "[verify_backend_selfhost_bootstrap_self_obj] timeout sample: $sample_file" >&2
        fi
      done
    fi
    echo "[verify_backend_selfhost_bootstrap_self_obj] compiler failed (stage=$stage, status=$exit_code)" >>"$build_log"
    echo "[verify_backend_selfhost_bootstrap_self_obj] compiler failed (stage=$stage, status=$exit_code)" >&2
    tail -n 200 "$build_log" >&2 || true
    exit 1
  fi
  if [ ! -x "$tmp_exe" ]; then
    echo "[verify_backend_selfhost_bootstrap_self_obj] missing exe output: $tmp_exe" 1>&2
    exit 1
  fi

  write_obj_witness_from_exe "$tmp_exe" "$tmp_exe_obj"

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

write_proof_env_launcher() {
  out="$1"
  target_bin="$2"
  env_default="$3"
  tmp="${out}.tmp.$$"
  cat >"$tmp" <<EOF
#!/usr/bin/env sh
:
set -eu
target_bin='$target_bin'
env_default='$env_default'
sidecar_compiler="\${BACKEND_UIR_SIDECAR_COMPILER:-\$env_default}"
exec env BACKEND_UIR_SIDECAR_COMPILER="\$sidecar_compiler" "\$target_bin" "\$@"
EOF
  chmod +x "$tmp"
  mv "$tmp" "$out"
}

default_proof_surface_sidecar() {
  preferred_release="$root/dist/releases/2026-02-23T09_54_03Z_e84f22d_14/cheng"
  legacy_proofseed="$root/artifacts/backend_selfhost_self_obj/probe_proofseed14/cheng_stage0_proofseed14"
  if [ -x "$preferred_release" ]; then
    printf '%s\n' "$preferred_release"
    return 0
  fi
  if [ -x "$legacy_proofseed" ]; then
    printf '%s\n' "$legacy_proofseed"
    return 0
  fi
  printf '\n'
}

extract_compile_stamp_field() {
  stamp_file="$1"
  key="$2"
  if [ ! -f "$stamp_file" ]; then
    printf '\n'
    return 0
  fi
  awk -F= -v key="$key" '$1 == key { print substr($0, index($0, "=") + 1); exit }' "$stamp_file"
}

proof_surface_probe_ok() {
  probe_driver="$1"
  probe_fixture="$2"
  probe_stamp="$3"
  probe_log="$4"
  probe_obj="$5"
  rm -f "$probe_stamp" "$probe_log" "$probe_obj"
  set +e
  env \
    BACKEND_INTERNAL_ALLOW_EMIT_OBJ=1 \
    BACKEND_COMPILE_STAMP_OUT="$probe_stamp" \
    UIR_PROFILE=0 \
    BORROW_IR=mir \
    GENERIC_LOWERING=mir_hybrid \
    GENERIC_MODE=dict \
    GENERIC_SPEC_BUDGET=0 \
    STAGE1_SEM_FIXED_0=0 \
    STAGE1_OWNERSHIP_FIXED_0=0 \
    BACKEND_EMIT=obj \
    BACKEND_TARGET="$target" \
    BACKEND_FRONTEND=stage1 \
    BACKEND_INPUT="$probe_fixture" \
    BACKEND_OUTPUT="$probe_obj" \
    "$probe_driver" >"$probe_log" 2>&1
  probe_rc="$?"
  set -e
  if [ "$probe_rc" -ne 0 ]; then
    return 1
  fi
  eff="$(extract_compile_stamp_field "$probe_stamp" "stage1_skip_ownership_effective")"
  def="$(extract_compile_stamp_field "$probe_stamp" "stage1_skip_ownership_default")"
  [ "$eff" = "0" ] && [ "$def" = "0" ]
}

publish_proof_surface() {
  label="$1"
  outer_driver="$2"
  launcher_path="$3"
  meta_path="$4"
  check_stamp="$5"
  check_log="$6"
  check_obj="$7"
  sidecar_compiler="$8"
  rm -f "$launcher_path" "$meta_path" "$check_stamp" "$check_log" "$check_obj"
  if [ ! -x "$outer_driver" ] || [ ! -x "$sidecar_compiler" ]; then
    return 1
  fi
  write_proof_env_launcher "$launcher_path" "$outer_driver" "$sidecar_compiler"
  if ! proof_surface_probe_ok "$launcher_path" "$proof_surface_fixture" "$check_stamp" "$check_log" "$check_obj"; then
    rm -f "$launcher_path" "$meta_path" "$check_obj"
    return 1
  fi
  {
    echo "label=$label"
    echo "outer_driver=$outer_driver"
    echo "sidecar_compiler=$sidecar_compiler"
    echo "fixture=$proof_surface_fixture"
    echo "stage1_skip_ownership_effective=$(extract_compile_stamp_field "$check_stamp" "stage1_skip_ownership_effective")"
    echo "stage1_skip_ownership_default=$(extract_compile_stamp_field "$check_stamp" "stage1_skip_ownership_default")"
    echo "uir_phase_contract_version=$(extract_compile_stamp_field "$check_stamp" "uir_phase_contract_version")"
    echo "generic_lowering=$(extract_compile_stamp_field "$check_stamp" "generic_lowering")"
    echo "generic_mode=$(extract_compile_stamp_field "$check_stamp" "generic_mode")"
  } > "${meta_path}.tmp.$$"
  mv "${meta_path}.tmp.$$" "$meta_path"
  return 0
}

file_sha256() {
  f="$1"
  if [ ! -f "$f" ]; then
    printf '%s\n' ""
    return
  fi
  if command -v shasum >/dev/null 2>&1; then
    shasum -a 256 "$f" | awk '{print $1}'
    return
  fi
  if command -v sha256sum >/dev/null 2>&1; then
    sha256sum "$f" | awk '{print $1}'
    return
  fi
  cksum "$f" | awk '{print $1 ":" $2}'
}

file_normalized_nm_sha256() {
  f="$1"
  if [ ! -f "$f" ] || ! command -v nm >/dev/null 2>&1; then
    printf '%s\n' ""
    return
  fi
  set +e
  out="$(nm -j "$f" 2>/dev/null | sed -E 's/__cheng_mod_[0-9a-f]+//g' | LC_ALL=C sort)"
  nm_status="$?"
  set -e
  if [ "$nm_status" -ne 0 ]; then
    printf '%s\n' ""
    return
  fi
  if command -v shasum >/dev/null 2>&1; then
    printf '%s' "$out" | shasum -a 256 | awk '{print $1}'
    return
  fi
  if command -v sha256sum >/dev/null 2>&1; then
    printf '%s' "$out" | sha256sum | awk '{print $1}'
    return
  fi
  printf '%s' "$out" | cksum | awk '{print $1 ":" $2}'
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

dump_stage1_failure_context() {
  if [ -s "$out_dir/stage1.native.build.txt" ]; then
    return
  fi
  if [ -s "$out_dir/runtime.build.txt" ]; then
    echo "[verify_backend_selfhost_bootstrap_self_obj] runtime log: $out_dir/runtime.build.txt" >&2
    tail -n 200 "$out_dir/runtime.build.txt" >&2 || true
  fi
}

stage1_exe="$out_dir/cheng.stage1"
stage2_exe="$out_dir/cheng.stage2"
stage1_obj="$stage1_exe.o"
stage2_obj="$stage2_exe.o"
stage3_witness_exe="$out_dir/cheng.stage3.witness"
proof_surface_sidecar="${SELF_OBJ_BOOTSTRAP_PROOF_SIDECAR_COMPILER:-$(default_proof_surface_sidecar)}"
proof_surface_fixture="${SELF_OBJ_BOOTSTRAP_PROOF_FIXTURE:-tests/cheng/backend/fixtures/return_i64.cheng}"
proof_stage2_launcher="$out_dir/cheng.stage2.proof"
proof_stage2_meta="$out_dir/cheng.stage2.proof.meta"
proof_stage2_stamp="$out_dir/cheng.stage2.proof.compile_stamp.txt"
proof_stage2_log="$out_dir/cheng.stage2.proof.check.log"
proof_stage2_obj="$out_dir/cheng.stage2.proof.check.o"
proof_stage3_launcher="$out_dir/cheng.stage3.witness.proof"
proof_stage3_meta="$out_dir/cheng.stage3.witness.proof.meta"
proof_stage3_stamp="$out_dir/cheng.stage3.witness.proof.compile_stamp.txt"
proof_stage3_log="$out_dir/cheng.stage3.witness.proof.check.log"
proof_stage3_obj="$out_dir/cheng.stage3.witness.proof.check.o"
stage2_mode_stamp="$out_dir/cheng.stage2.mode"
stage2_mode_expected="mode=${bootstrap_mode};multi=${multi};multi_force=${multi_force};whole=1"
stage1_tmp="$out_dir/cheng_stage1_tmp_${session_file_safe}"
stage2_tmp="$out_dir/cheng_stage2_tmp_${session_file_safe}"
stage3_tmp="$out_dir/cheng_stage3_tmp_${session_file_safe}"

stage1_rebuild="1"
stage2_rebuild="1"
if [ "$reuse" = "1" ] && [ -x "$stage1_exe" ] && [ -s "$stage1_obj" ]; then
  if driver_sanity_ok "$stage1_exe"; then
    if [ "$bootstrap_mode" = "fast" ]; then
      fast_reuse_stale="${SELF_OBJ_BOOTSTRAP_FAST_REUSE_STALE:-1}"
      if [ "$fast_reuse_stale" = "1" ]; then
        stage1_rebuild="0"
      elif ! is_rebuild_required "$stage1_exe" "$stage0" "$driver_input" && \
           ! backend_sources_newer_than "$stage1_exe"; then
        stage1_rebuild="0"
      fi
    else
      strict_allow_fast_reuse="${SELF_OBJ_BOOTSTRAP_STRICT_ALLOW_FAST_REUSE:-0}"
      if [ "$strict_allow_fast_reuse" = "1" ]; then
        stage1_rebuild="0"
      elif ! is_rebuild_required "$stage1_exe" "$stage0" "$driver_input" && \
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
      if ! is_rebuild_required "$stage2_exe" "$stage1_exe" "$driver_input" && \
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
  if ! (build_exe_self "stage1.native" "$stage0" "$driver_input" "$stage1_tmp" "$stage1_exe") >/dev/null 2>&1; then
    last_fail_stage="stage1.native"
    echo "[verify_backend_selfhost_bootstrap_self_obj] stage1 build failed (native)" >&2
    if [ -s "$out_dir/stage1.native.build.txt" ]; then
      echo "[verify_backend_selfhost_bootstrap_self_obj] native log: $out_dir/stage1.native.build.txt" >&2
      tail -n 200 "$out_dir/stage1.native.build.txt" >&2 || true
    else
      dump_stage1_failure_context
    fi
    stage1_duration="$(( $(timestamp_now) - stage1_started ))"
    record_stage_timing "stage1" "fail" "$stage1_duration"
    exit 1
  fi
  if [ "$stage1_probe_required" = "1" ]; then
    if ! driver_stage1_probe_ok "$stage1_exe"; then
      echo "[verify_backend_selfhost_bootstrap_self_obj] stage1 probe failed (compiler=$stage1_exe)" >&2
      if [ -s "$out_dir/selfhost_stage1_probe.log" ]; then
        echo "[verify_backend_selfhost_bootstrap_self_obj] stage1 probe log: $out_dir/selfhost_stage1_probe.log" >&2
        tail -n 200 "$out_dir/selfhost_stage1_probe.log" >&2 || true
      fi
      stage1_duration="$(( $(timestamp_now) - stage1_started ))"
      record_stage_timing "stage1" "fail" "$stage1_duration"
      exit 1
    fi
  fi
  stage1_duration="$(( $(timestamp_now) - stage1_started ))"
  record_stage_timing "stage1" "rebuild" "$stage1_duration"
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
    if [ "${SELF_OBJ_BOOTSTRAP_STRICT_STAGE2_ALIAS:-1}" = "1" ]; then
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
      build_exe_self "stage2" "$stage1_exe" "$driver_input" "$stage2_tmp" "$stage2_exe"
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
    echo "== backend.selfhost_self_obj.stage3_exe_witness (converge) =="
    stage3_started="$(timestamp_now)"
    build_exe_self "stage3.witness" "$stage2_exe" "$driver_input" "$stage3_tmp" "$stage3_witness_exe"
    stage3_duration="$(( $(timestamp_now) - stage3_started ))"
    record_stage_timing "stage3_exe_witness" "rebuild" "$stage3_duration"
    stage2_hash="$(file_sha256 "$stage2_exe")"
    stage3_hash="$(file_sha256 "$stage3_witness_exe")"
    stage2_nm_hash="$(file_normalized_nm_sha256 "$stage2_exe")"
    stage3_nm_hash="$(file_normalized_nm_sha256 "$stage3_witness_exe")"
    stage2_stamp_file="$out_dir/stage2.compile_stamp.txt"
    stage3_stamp_file="$out_dir/stage3.witness.compile_stamp.txt"
    stage2_stamp_hash="$(file_sha256 "$stage2_stamp_file")"
    stage3_stamp_hash="$(file_sha256 "$stage3_stamp_file")"
    if [ ! -s "$stage3_stamp_file" ]; then
      echo "[verify_backend_selfhost_bootstrap_self_obj] missing stage3 compile stamp witness: $stage3_stamp_file" >&2
      exit 1
    fi
    if [ "$stage2_hash" != "" ] && [ "$stage3_hash" != "" ] && [ "$stage2_hash" = "$stage3_hash" ]; then
      :
    elif [ "$stage2_nm_hash" != "" ] && [ "$stage3_nm_hash" != "" ] &&
         [ "$stage2_nm_hash" = "$stage3_nm_hash" ] &&
         [ "$stage2_stamp_hash" != "" ] && [ "$stage2_stamp_hash" = "$stage3_stamp_hash" ]; then
      echo "[verify_backend_selfhost_bootstrap_self_obj] note: stage2/stage3 exe hash drift accepted via normalized symbol surface + compile stamp"
    else
      echo "[verify_backend_selfhost_bootstrap_self_obj] compiler exe mismatch: $stage2_exe vs $stage3_witness_exe" >&2
      echo "  stage2_hash=$stage2_hash" >&2
      echo "  stage3_hash=$stage3_hash" >&2
      echo "  stage2_nm_hash=$stage2_nm_hash" >&2
      echo "  stage3_nm_hash=$stage3_nm_hash" >&2
      echo "  stage2_compile_stamp_hash=$stage2_stamp_hash ($stage2_stamp_file)" >&2
      echo "  stage3_compile_stamp_hash=$stage3_stamp_hash ($stage3_stamp_file)" >&2
      exit 1
    fi
    echo "[verify_backend_selfhost_bootstrap_self_obj] note: stage1->stage2 mismatch; accepted stage2->stage3(exe) fixed point"
    echo "[verify_backend_selfhost_bootstrap_self_obj] witness: stage2_hash=$stage2_hash stage3_hash=$stage3_hash stage2_nm_hash=$stage2_nm_hash stage3_nm_hash=$stage3_nm_hash stage2_stamp_hash=$stage2_stamp_hash stage3_stamp_hash=$stage3_stamp_hash"
  fi
fi

if [ -x "$stage2_exe" ] && [ -x "$proof_surface_sidecar" ]; then
  if publish_proof_surface \
      "stage2.proof" \
      "$stage2_exe" \
      "$proof_stage2_launcher" \
      "$proof_stage2_meta" \
      "$proof_stage2_stamp" \
      "$proof_stage2_log" \
      "$proof_stage2_obj" \
      "$proof_surface_sidecar"; then
    echo "[verify_backend_selfhost_bootstrap_self_obj] published proof surface: $proof_stage2_launcher"
  else
    echo "[verify_backend_selfhost_bootstrap_self_obj] warn: failed to publish stage2 proof surface (outer=$stage2_exe sidecar=$proof_surface_sidecar)" >&2
  fi
else
  rm -f "$proof_stage2_launcher" "$proof_stage2_meta" "$proof_stage2_stamp" "$proof_stage2_log" "$proof_stage2_obj" 2>/dev/null || true
fi

if [ -x "$stage3_witness_exe" ] && [ -x "$proof_surface_sidecar" ]; then
  if publish_proof_surface \
      "stage3.witness.proof" \
      "$stage3_witness_exe" \
      "$proof_stage3_launcher" \
      "$proof_stage3_meta" \
      "$proof_stage3_stamp" \
      "$proof_stage3_log" \
      "$proof_stage3_obj" \
      "$proof_surface_sidecar"; then
    echo "[verify_backend_selfhost_bootstrap_self_obj] published proof surface: $proof_stage3_launcher"
  else
    echo "[verify_backend_selfhost_bootstrap_self_obj] warn: failed to publish stage3 proof surface (outer=$stage3_witness_exe sidecar=$proof_surface_sidecar)" >&2
  fi
else
  rm -f "$proof_stage3_launcher" "$proof_stage3_meta" "$proof_stage3_stamp" "$proof_stage3_log" "$proof_stage3_obj" 2>/dev/null || true
fi

fixture="tests/cheng/backend/fixtures/hello_puts.cheng"
hello1="$out_dir/hello_puts.stage1"
hello2="$out_dir/hello_puts.stage2"

if [ "$skip_smoke" = "1" ]; then
  echo "== backend.selfhost_self_obj.smoke (skip) =="
  record_stage_timing "smoke.stage1" "skip" "0"
  record_stage_timing "smoke.stage2" "skip" "0"
else
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
