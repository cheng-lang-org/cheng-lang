#!/usr/bin/env sh
. "$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)/env_prefix_bridge.sh"
set -eu
(set -o pipefail) 2>/dev/null && set -o pipefail

usage() {
  cat <<'EOF'
Usage:
  src/tooling/verify_backend_driver_selfbuild_smoke.sh [--help]

Env:
  DRIVER_SELFBUILD_SMOKE_STAGE0=<path>             optional stage0 override
  DRIVER_SELFBUILD_SMOKE_TIMEOUT=<seconds>         default: 55
  DRIVER_SELFBUILD_SMOKE_OUTPUT=<path>             default: artifacts/backend_driver/cheng.selfbuild_smoke
  DRIVER_SELFBUILD_SMOKE_REPORT=<path>             default: artifacts/backend_driver_selfbuild_smoke/selfbuild_smoke_report.tsv
  DRIVER_SELFBUILD_SMOKE_REQUIRE_REBUILD=<0|1>     default: 0
  DRIVER_SELFBUILD_SMOKE_MAX_STAGE0_ATTEMPTS=<N>   default: 1
  DRIVER_SELFBUILD_SMOKE_FORCE=<0|1>               default: auto (1 when REQUIRE_REBUILD=1, else 0)
  DRIVER_SELFBUILD_SMOKE_NO_RECOVER=<0|1>          default: auto (1 when REQUIRE_REBUILD=1, else 0)
  DRIVER_SELFBUILD_SMOKE_MULTI=<0|1>               default: 0
  DRIVER_SELFBUILD_SMOKE_MULTI_FORCE=<0|1>         default: 0
  DRIVER_SELFBUILD_SMOKE_INCREMENTAL=<0|1>         default: 1
  DRIVER_SELFBUILD_SMOKE_JOBS=<N>                  default: 0
  DRIVER_SELFBUILD_SMOKE_STAGE1_TIMEOUT=<seconds>  default: 30
  DRIVER_SELFBUILD_SMOKE_TARGETS=<csv>             default: host

Notes:
  - Default mode blocks on runnable smoke and reuses seeded output when available.
  - Strict mode (`REQUIRE_REBUILD=1`) forces a fresh selfbuild and disables recover fallback.
  - On failure it emits a structured root-cause report with build log / attempts log / crash report paths.
EOF
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

driver_help_ok() {
  bin="$1"
  [ -x "$bin" ] || return 1
  set +e
  run_with_timeout 5 "$bin" --help >/dev/null 2>&1
  rc="$?"
  set -e
  case "$rc" in
    0|1|2) return 0 ;;
  esac
  return 1
}

driver_stage1_smoke_ok() {
  bin="$1"
  smoke_timeout="$2"
  [ -x "$bin" ] || return 1
  smoke_src="$root/tests/cheng/backend/fixtures/return_add.cheng"
  if [ ! -f "$smoke_src" ]; then
    smoke_src="$root/tests/cheng/backend/fixtures/return_i64.cheng"
  fi
  [ -f "$smoke_src" ] || return 1
  smoke_targets="${DRIVER_SELFBUILD_SMOKE_TARGETS:-host}"
  old_ifs="$IFS"
  IFS=','
  for smoke_target in $smoke_targets; do
    IFS="$old_ifs"
    case "$smoke_target" in
      ""|host|auto)
        smoke_target="$(sh src/tooling/detect_host_target.sh 2>/dev/null || echo auto)"
        ;;
    esac
    safe_target="$(printf '%s' "$smoke_target" | tr -c 'A-Za-z0-9._-' '_' | tr -s '_')"
    smoke_out="$root/chengcache/.driver_selfbuild_smoke.stage1.${safe_target}.bin"
    rm -f "$smoke_out" "$smoke_out.tmp" "$smoke_out.tmp.linkobj" "$smoke_out.o"
    rm -rf "$smoke_out.objs" "$smoke_out.objs.lock"
    set +e
    run_with_timeout "$smoke_timeout" env \
      MM=orc \
      STAGE1_NO_POINTERS_NON_C_ABI=0 \
      STAGE1_NO_POINTERS_NON_C_ABI_INTERNAL=0 \
      BACKEND_VALIDATE=0 \
      STAGE1_SKIP_SEM=1 \
      STAGE1_SKIP_CPROFILE=1 \
      STAGE1_SKIP_OWNERSHIP=1 \
      GENERIC_MODE=dict \
      GENERIC_SPEC_BUDGET=0 \
      BACKEND_LINKER=self \
      BACKEND_NO_RUNTIME_C=1 \
      BACKEND_RUNTIME_OBJ= \
      BACKEND_EMIT=exe \
      BACKEND_TARGET="$smoke_target" \
      BACKEND_FRONTEND=stage1 \
      BACKEND_INPUT="$smoke_src" \
      BACKEND_OUTPUT="$smoke_out" \
      "$bin" >/dev/null 2>&1
    rc="$?"
    set -e
    if [ "$rc" -ne 0 ] || [ ! -s "$smoke_out" ]; then
      IFS="$old_ifs"
      return 1
    fi
  done
  IFS="$old_ifs"
  return 0
}

latest_attempt_report() {
  set +e
  latest="$(ls -1t "$root"/chengcache/build_backend_driver/attempts.*.tsv 2>/dev/null | head -n 1)"
  rc="$?"
  set -e
  if [ "$rc" -ne 0 ]; then
    latest=""
  fi
  printf '%s\n' "$latest"
}

latest_crash_report() {
  crash_dir="$HOME/Library/Logs/DiagnosticReports"
  if [ ! -d "$crash_dir" ]; then
    printf '\n'
    return 0
  fi
  set +e
  latest="$(ls -1t \
    "$crash_dir"/cheng.selfbuild_smoke-*.ips \
    "$crash_dir"/cheng.tmp-*.ips \
    "$crash_dir"/cheng-*.ips \
    2>/dev/null | head -n 1)"
  rc="$?"
  set -e
  if [ "$rc" -ne 0 ]; then
    latest=""
  fi
  printf '%s\n' "$latest"
}

seed_output_from_stage0() {
  src="$1"
  dst="$2"
  [ -x "$src" ] || return 1
  mkdir -p "$(dirname "$dst")"
  cp "$src" "$dst"
  chmod +x "$dst" 2>/dev/null || true
  src_obj="${src}.o"
  dst_obj="${dst}.o"
  if [ -s "$src_obj" ] && [ ! -e "$dst_obj" ]; then
    cp "$src_obj" "$dst_obj" 2>/dev/null || true
  fi
  return 0
}

first_match_line() {
  f="$1"
  if [ "$f" = "" ] || [ ! -f "$f" ]; then
    printf '\n'
    return 0
  fi
  set +e
  line="$(grep -E -m 1 \
    'unsupported expr kind|Symbol not found|dyld|EXC_BAD_ACCESS|Segmentation fault|Abort trap|unresolved|timed out|timeout' \
    "$f" 2>/dev/null)"
  rc="$?"
  set -e
  if [ "$rc" -ne 0 ]; then
    line=""
  fi
  printf '%s\n' "$line"
}

classify_cause() {
  build_rc="$1"
  smoke_rc="$2"
  attempt_status="$3"
  hint="$4"
  build_elapsed="$5"
  build_timeout="$6"
  case "$build_rc:$smoke_rc:$attempt_status" in
    124:*:*|*:124:*|*:*:124)
      printf 'timeout\n'
      return 0
      ;;
    *:139:*|*:*:139|*:*:86|*:*:87)
      printf 'smoke_crash_or_segv\n'
      return 0
      ;;
    *:134:*|*:*:134)
      printf 'abort_or_dyld\n'
      return 0
      ;;
  esac
  case "$attempt_status" in
    ''|*[!0-9]*)
      ;;
    *)
      if [ "$attempt_status" -ge 128 ]; then
        case "$attempt_status" in
          137|143)
            case "$build_elapsed:$build_timeout" in
              *[!0-9:]*)
                printf 'timeout_or_killed\n'
                return 0
                ;;
            esac
            if [ "$build_elapsed" -ge "$build_timeout" ]; then
              printf 'timeout_or_killed\n'
            else
              printf 'driver_crash_or_killed\n'
            fi
            return 0
            ;;
        esac
        printf 'driver_crash_or_killed\n'
        return 0
      fi
      ;;
  esac
  case "$build_rc" in
    ''|*[!0-9]*)
      ;;
    *)
      if [ "$build_rc" -ge 128 ]; then
        printf 'driver_crash_or_killed\n'
        return 0
      fi
      ;;
  esac
  case "$hint" in
    *"unsupported expr kind"*)
      printf 'stage0_legacy_lowering_regression\n'
      return 0
      ;;
    *"Symbol not found"*|*"dyld"*)
      printf 'dyld_symbol_unresolved\n'
      return 0
      ;;
    *"EXC_BAD_ACCESS"*|*"Segmentation fault"*)
      printf 'segv_or_bad_access\n'
      return 0
      ;;
    *"timed out"*|*"timeout"*)
      printf 'timeout\n'
      return 0
      ;;
  esac
  if [ "$build_rc" -ne 0 ]; then
    printf 'driver_rebuild_failed\n'
    return 0
  fi
  if [ "$smoke_rc" -ne 0 ]; then
    printf 'stage1_smoke_failed\n'
    return 0
  fi
  printf 'unknown\n'
}

while [ "${1:-}" != "" ]; do
  case "$1" in
    --help|-h)
      usage
      exit 0
      ;;
    *)
      echo "[driver_selfbuild_smoke] unknown arg: $1" 1>&2
      usage 1>&2
      exit 2
      ;;
  esac
  shift || true
done

root="$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)"
cd "$root"

stage0="${DRIVER_SELFBUILD_SMOKE_STAGE0:-}"
build_timeout="${DRIVER_SELFBUILD_SMOKE_TIMEOUT:-55}"
output_rel="${DRIVER_SELFBUILD_SMOKE_OUTPUT:-artifacts/backend_driver/cheng.selfbuild_smoke}"
report="${DRIVER_SELFBUILD_SMOKE_REPORT:-artifacts/backend_driver_selfbuild_smoke/selfbuild_smoke_report.tsv}"
require_rebuild="${DRIVER_SELFBUILD_SMOKE_REQUIRE_REBUILD:-0}"
max_stage0_attempts="${DRIVER_SELFBUILD_SMOKE_MAX_STAGE0_ATTEMPTS:-1}"
force_rebuild="${DRIVER_SELFBUILD_SMOKE_FORCE:-}"
build_no_recover="${DRIVER_SELFBUILD_SMOKE_NO_RECOVER:-}"
driver_multi="${DRIVER_SELFBUILD_SMOKE_MULTI:-0}"
driver_multi_force="${DRIVER_SELFBUILD_SMOKE_MULTI_FORCE:-0}"
driver_incremental="${DRIVER_SELFBUILD_SMOKE_INCREMENTAL:-1}"
driver_jobs="${DRIVER_SELFBUILD_SMOKE_JOBS:-0}"
stage1_timeout="${DRIVER_SELFBUILD_SMOKE_STAGE1_TIMEOUT:-30}"

if [ "$force_rebuild" = "" ]; then
  force_rebuild="$require_rebuild"
fi
if [ "$build_no_recover" = "" ]; then
  build_no_recover="$require_rebuild"
fi

report_dir="$(dirname "$report")"
mkdir -p "$report_dir" chengcache
if [ ! -f "$report" ]; then
  printf 'ts\tresult\tcause\tstage0\tbuild_rc\tsmoke_rc\tattempt_status\tattempt_file\tattempt_log\tbuild_log\tcrash_report\thint\n' >"$report"
fi

choose_stage0() {
  if [ "$stage0" != "" ]; then
    printf '%s\n' "$stage0"
    return 0
  fi
  if [ "${BACKEND_DRIVER:-}" != "" ] && [ -x "${BACKEND_DRIVER:-}" ]; then
    printf '%s\n' "${BACKEND_DRIVER:-}"
    return 0
  fi
  if [ -x "artifacts/backend_seed/cheng.stage2" ]; then
    printf '%s\n' "artifacts/backend_seed/cheng.stage2"
    return 0
  fi
  if [ -x "artifacts/backend_selfhost_self_obj/cheng_stage0_default" ]; then
    printf '%s\n' "artifacts/backend_selfhost_self_obj/cheng_stage0_default"
    return 0
  fi
  if [ -x "dist/releases/current/cheng" ]; then
    printf '%s\n' "dist/releases/current/cheng"
    return 0
  fi
  if [ -x "artifacts/backend_driver/cheng" ]; then
    printf '%s\n' "artifacts/backend_driver/cheng"
    return 0
  fi
  sh src/tooling/backend_driver_path.sh
}

stage0="$(choose_stage0)"
if [ ! -x "$stage0" ]; then
  echo "[driver_selfbuild_smoke] stage0 not executable: $stage0" 1>&2
  exit 1
fi
if ! driver_help_ok "$stage0"; then
  echo "[driver_selfbuild_smoke] stage0 not runnable: $stage0" 1>&2
  exit 1
fi

case "$output_rel" in
  /*) output="$output_rel" ;;
  *) output="$root/$output_rel" ;;
esac
mkdir -p "$(dirname "$output")"
rm -f "$output" "${output}.o" "${output}.tmp" "${output}.tmp.o"

if [ "$require_rebuild" != "1" ] && [ ! -x "$output" ]; then
  seed_output_from_stage0 "$stage0" "$output" >/dev/null 2>&1 || true
fi

build_log="$report_dir/selfbuild_smoke.$$.log"
attempt_before="$(latest_attempt_report)"

echo "== backend.driver_selfbuild_smoke =="
echo "[driver_selfbuild_smoke] stage0=$stage0 timeout=${build_timeout}s output=$output"

set +e
build_start="$(date +%s)"
env \
  BACKEND_BUILD_DRIVER_SELFHOST=1 \
  BACKEND_BUILD_DRIVER_LINKER=system \
  BACKEND_BUILD_DRIVER_STAGE0="$stage0" \
  BACKEND_BUILD_DRIVER_TIMEOUT="$build_timeout" \
  BACKEND_BUILD_DRIVER_SMOKE=1 \
  BACKEND_BUILD_DRIVER_REQUIRE_SMOKE=1 \
  BACKEND_BUILD_DRIVER_FORCE="$force_rebuild" \
  BACKEND_BUILD_DRIVER_NO_RECOVER="$build_no_recover" \
  BACKEND_BUILD_DRIVER_MAX_STAGE0_ATTEMPTS="$max_stage0_attempts" \
  BACKEND_BUILD_DRIVER_MULTI="$driver_multi" \
  BACKEND_BUILD_DRIVER_MULTI_FORCE="$driver_multi_force" \
  BACKEND_BUILD_DRIVER_INCREMENTAL="$driver_incremental" \
  BACKEND_BUILD_DRIVER_JOBS="$driver_jobs" \
  sh src/tooling/build_backend_driver.sh --name:"$output" >"$build_log" 2>&1
build_rc="$?"
build_end="$(date +%s)"
set -e
build_elapsed="$((build_end - build_start))"

smoke_rc=0
if [ "$build_rc" -eq 0 ]; then
  set +e
  driver_stage1_smoke_ok "$output" "$stage1_timeout"
  smoke_rc="$?"
  set -e
fi

attempt_file="$(latest_attempt_report)"
attempt_status=""
attempt_log=""
if [ "$attempt_file" != "" ] && [ -f "$attempt_file" ]; then
  attempt_status="$(sed -n 's/.*status=\([0-9][0-9]*\).*/\1/p' "$attempt_file" | tail -n 1)"
  attempt_log="$(sed -n 's/.*\tlog=\(.*\)$/\1/p' "$attempt_file" | tail -n 1)"
fi

# If no new attempts report was produced, keep only the current one when it differs.
if [ "$attempt_before" = "$attempt_file" ] && [ "$build_rc" -eq 0 ]; then
  attempt_file=""
  attempt_status=""
  attempt_log=""
fi

crash_report="$(latest_crash_report)"
hint="$(first_match_line "$build_log")"
if [ "$hint" = "" ]; then
  hint="$(first_match_line "$attempt_log")"
fi
if [ "$hint" = "" ]; then
  hint="$(first_match_line "$crash_report")"
fi
if [ "$hint" = "" ]; then
  if [ "${attempt_status:-}" != "" ]; then
    hint="attempt_status=${attempt_status},elapsed=${build_elapsed}s,timeout=${build_timeout}s"
  elif [ "$build_rc" -ne 0 ]; then
    hint="build_rc=${build_rc},elapsed=${build_elapsed}s,timeout=${build_timeout}s"
  elif [ "$smoke_rc" -ne 0 ]; then
    hint="smoke_rc=${smoke_rc}"
  fi
fi
cause="$(classify_cause "$build_rc" "$smoke_rc" "${attempt_status:-}" "$hint" "$build_elapsed" "$build_timeout")"

ts="$(date +%Y-%m-%dT%H:%M:%S)"
result="ok"
if [ "$build_rc" -ne 0 ] || [ "$smoke_rc" -ne 0 ]; then
  result="fail"
fi

printf '%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s\n' \
  "$ts" "$result" "$cause" "$stage0" "$build_rc" "$smoke_rc" "${attempt_status:-}" "${attempt_file:-}" "${attempt_log:-}" "$build_log" "${crash_report:-}" "${hint:-}" \
  >>"$report"

if [ "$result" = "ok" ]; then
  echo "[driver_selfbuild_smoke] ok (report=$report, log=$build_log)"
  exit 0
fi

echo "[driver_selfbuild_smoke] fail: cause=$cause build_rc=$build_rc smoke_rc=$smoke_rc stage0=$stage0" 1>&2
echo "[driver_selfbuild_smoke] build_log=$build_log" 1>&2
if [ "$attempt_file" != "" ]; then
  echo "[driver_selfbuild_smoke] attempt_report=$attempt_file" 1>&2
fi
if [ "$attempt_log" != "" ]; then
  echo "[driver_selfbuild_smoke] attempt_log=$attempt_log" 1>&2
fi
if [ "$crash_report" != "" ]; then
  echo "[driver_selfbuild_smoke] crash_report=$crash_report" 1>&2
fi
if [ "$hint" != "" ]; then
  echo "[driver_selfbuild_smoke] hint=$hint" 1>&2
fi
echo "[driver_selfbuild_smoke] report=$report" 1>&2
exit 1
