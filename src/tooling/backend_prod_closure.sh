#!/usr/bin/env sh
set -eu
(set -o pipefail) 2>/dev/null && set -o pipefail

usage() {
  cat <<'EOF'
Usage:
  src/tooling/backend_prod_closure.sh [--no-validate] [--debug] [--no-debug] [--no-ffi] [--no-determinism-strict]
                                     [--no-opt] [--no-obj] [--no-obj-determinism] [--no-exe-determinism]
                                     [--no-opt2]
                                     [--no-sanitizer]
                                     [--stress|--no-stress] [--no-bundle]
                                     [--no-sign]
                                     [--no-ssa]
                                     [--no-selfhost]
                                     [--selfhost-fast|--selfhost-strict]
                                     [--selfhost-strict-gate|--no-selfhost-strict-gate]
                                     [--selfhost-strict-noreuse-probe|--no-selfhost-strict-noreuse-probe]
                                     [--fullchain|--no-fullchain]
                                     [--seed:<path>] [--seed-id:<id>] [--seed-tar:<path>] [--require-seed]
                                     [--only-self-obj-bootstrap]
                                     [--no-self-obj-writer]
                                     [--no-mm]
                                     [--no-publish]
                                     [--strict] [--allow-skip]
                                     [--manifest:<path>] [--bundle:<path>]

Notes:
  - Runs the self-hosted backend production closure (includes best-effort target matrix checks).
  - Default includes MIR validation (CHENG_BACKEND_VALIDATE=1) and emits a release manifest.
  - Gate timeout defaults to 60s (`CHENG_BACKEND_PROD_GATE_TIMEOUT`; set 0 to disable).
  - Timeout diagnostics are enabled by default (`CHENG_BACKEND_PROD_TIMEOUT_DIAG=1`).
  - Selfhost performance regression gate is enabled by default (`CHENG_BACKEND_RUN_SELFHOST_PERF=1`).
  - Selfhost performance baseline defaults to `src/tooling/selfhost_perf_baseline.env` (`CHENG_SELFHOST_PERF_BASELINE`).
  - Fullchain bootstrap gate is opt-in (`--fullchain` or `CHENG_BACKEND_RUN_FULLCHAIN=1`).
  - Default is strict: any step that exits with skip code (2) fails the closure.
  - Use `--allow-skip` to permit optional steps to skip.
  - `--require-seed` requires explicit `--seed`/`--seed-id`/`--seed-tar`.
  - Publish path requires explicit seed (`--seed`/`--seed-id`/`--seed-tar`).
  - Default selfhost mode is `fast` (`CHENG_BACKEND_PROD_SELFHOST_MODE` / `CHENG_SELF_OBJ_BOOTSTRAP_MODE`).
  - Optional strict selfhost gate is off by default (`CHENG_BACKEND_RUN_SELFHOST_STRICT=1` to enable).
  - Optional strict no-reuse probe is off by default (`CHENG_BACKEND_RUN_SELFHOST_STRICT_NOREUSE_PROBE=1` to enable).
EOF
}

validate="1"
run_debug=""
run_ffi="1"
run_det_strict="1"
run_opt="1"
run_opt2="1"
run_ssa="1"
run_selfhost="1"
run_fullchain=""
if [ "${CHENG_BACKEND_RUN_FULLCHAIN:-}" = "1" ]; then
  run_fullchain="1"
fi
seed=""
seed_id=""
seed_tar=""
require_seed="0"
run_obj="1"
run_obj_det="1"
run_exe_det="1"
run_sanitizer="1"
run_stress=""
if [ "${CHENG_BACKEND_RUN_STRESS:-}" = "1" ]; then
  run_stress="1"
fi
run_bundle="1"
run_sign="1"
run_mm="1"
run_self_obj_writer="1"
run_publish="1"
only_self_obj_bootstrap=""
allow_skip=""
manifest="artifacts/backend_prod/release_manifest.json"
bundle="artifacts/backend_prod/backend_release.tar.gz"
debug_explicit="0"
selfhost_timeout="${CHENG_BACKEND_PROD_SELFHOST_TIMEOUT:-60}"
selfhost_gate_timeout="${CHENG_BACKEND_PROD_GATE_TIMEOUT:-60}"
selfhost_strict_noreuse_gate_timeout="${CHENG_BACKEND_PROD_SELFHOST_STRICT_NOREUSE_GATE_TIMEOUT:-75}"
selfhost_strict_noreuse_probe_timeout="${CHENG_BACKEND_PROD_SELFHOST_STRICT_NOREUSE_PROBE_TIMEOUT:-$selfhost_timeout}"
selfhost_reuse="${CHENG_BACKEND_PROD_SELFHOST_REUSE:-${CHENG_SELF_OBJ_BOOTSTRAP_REUSE:-1}}"
selfhost_session="${CHENG_BACKEND_PROD_SELFHOST_SESSION:-${CHENG_SELF_OBJ_BOOTSTRAP_SESSION:-prod}}"
selfhost_mode="${CHENG_BACKEND_PROD_SELFHOST_MODE:-${CHENG_SELF_OBJ_BOOTSTRAP_MODE:-fast}}"
run_selfhost_strict="${CHENG_BACKEND_RUN_SELFHOST_STRICT:-0}"
run_selfhost_strict_noreuse_probe="${CHENG_BACKEND_RUN_SELFHOST_STRICT_NOREUSE_PROBE:-0}"
run_selfhost_perf="${CHENG_BACKEND_RUN_SELFHOST_PERF:-1}"
selfhost_perf_use_strict_session="${CHENG_BACKEND_SELFHOST_PERF_USE_STRICT_SESSION:-1}"
timeout_diag_enabled="${CHENG_BACKEND_PROD_TIMEOUT_DIAG:-1}"
timeout_diag_seconds="${CHENG_BACKEND_PROD_TIMEOUT_DIAG_SECONDS:-5}"
timeout_diag_dir="${CHENG_BACKEND_PROD_TIMEOUT_DIAG_DIR:-chengcache/backend_timeout_diag}"
timeout_diag_tool="${CHENG_BACKEND_PROD_TIMEOUT_DIAG_TOOL:-sample}"
timeout_diag_summary_enabled="${CHENG_BACKEND_PROD_TIMEOUT_DIAG_SUMMARY:-1}"
timeout_diag_summary_top="${CHENG_BACKEND_PROD_TIMEOUT_DIAG_SUMMARY_TOP:-12}"
timeout_diag_last_file=""

while [ "${1:-}" != "" ]; do
  case "$1" in
    --no-validate)
      validate=""
      ;;
    --debug)
      run_debug="1"
      debug_explicit="1"
      ;;
    --no-debug)
      run_debug=""
      debug_explicit="1"
      ;;
    --no-ffi)
      run_ffi=""
      ;;
    --no-determinism-strict)
      run_det_strict=""
      ;;
    --no-opt)
      run_opt=""
      ;;
    --no-opt2)
      run_opt2=""
      ;;
    --no-ssa)
      run_ssa=""
      ;;
    --no-selfhost)
      run_selfhost=""
      ;;
    --selfhost-fast)
      selfhost_mode="fast"
      ;;
    --selfhost-strict)
      selfhost_mode="strict"
      ;;
    --selfhost-strict-gate)
      run_selfhost_strict="1"
      ;;
    --no-selfhost-strict-gate)
      run_selfhost_strict="0"
      ;;
    --selfhost-strict-noreuse-probe)
      run_selfhost_strict_noreuse_probe="1"
      ;;
    --no-selfhost-strict-noreuse-probe)
      run_selfhost_strict_noreuse_probe="0"
      ;;
    --no-fullchain)
      run_fullchain=""
      ;;
    --fullchain)
      run_fullchain="1"
      ;;
    --seed:*)
      seed="${1#--seed:}"
      ;;
    --seed-id:*)
      seed_id="${1#--seed-id:}"
      ;;
    --seed-tar:*)
      seed_tar="${1#--seed-tar:}"
      ;;
    --require-seed)
      require_seed="1"
      ;;
    --only-self-obj-bootstrap)
      only_self_obj_bootstrap="1"
      run_debug=""
      run_ffi=""
      run_det_strict=""
      run_opt=""
      run_opt2=""
      run_ssa=""
      run_fullchain=""
      run_obj=""
      run_obj_det=""
      run_exe_det=""
      run_sanitizer=""
      run_stress=""
      run_bundle=""
      run_sign=""
      run_mm=""
      run_self_obj_writer=""
      run_publish=""
      ;;
    --no-obj)
      run_obj=""
      ;;
    --no-obj-determinism)
      run_obj_det=""
      ;;
    --no-exe-determinism)
      run_exe_det=""
      ;;
    --no-sanitizer)
      run_sanitizer=""
      ;;
    --no-stress)
      run_stress=""
      ;;
    --stress)
      run_stress="1"
      ;;
    --no-bundle)
      run_bundle=""
      ;;
    --no-sign)
      run_sign=""
      ;;
    --no-self-obj-writer)
      run_self_obj_writer=""
      ;;
    --no-mm)
      run_mm=""
      ;;
    --no-publish)
      run_publish=""
      ;;
    --strict)
      allow_skip=""
      ;;
    --allow-skip)
      allow_skip="1"
      ;;
    --manifest:*)
      manifest="${1#--manifest:}"
      ;;
    --bundle:*)
      bundle="${1#--bundle:}"
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

if [ "$selfhost_mode" != "strict" ] && [ "$selfhost_mode" != "fast" ]; then
  echo "[Error] invalid selfhost mode: $selfhost_mode (expected strict|fast)" 1>&2
  exit 2
fi

root="$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)"
cd "$root"

# Keep local backend driver for the whole closure; many sub-gates rely on it.
export CHENG_CLEAN_CHENG_LOCAL=0
if [ "${CHENG_BACKEND_DRIVER_ALLOW_FALLBACK:-}" = "" ]; then
  export CHENG_BACKEND_DRIVER_ALLOW_FALLBACK=1
fi

if [ "$debug_explicit" = "0" ] && [ "$(uname -s 2>/dev/null || echo unknown)" = "Darwin" ]; then
  run_debug="1"
fi

backend_driver_explicit="0"
if [ "${CHENG_BACKEND_DRIVER:-}" != "" ]; then
  backend_driver_explicit="1"
fi

seed_from_tar() {
  tar_path="$1"
  if [ ! -f "$tar_path" ]; then
    echo "[Error] seed tar not found: $tar_path" 1>&2
    exit 2
  fi
  out_dir="chengcache/backend_seed_prod_$$"
  mkdir -p "$out_dir"
  tar -xzf "$tar_path" -C "$out_dir"
  extracted="$out_dir/cheng"
  if [ ! -f "$extracted" ]; then
    echo "[Error] seed tar missing cheng: $tar_path" 1>&2
    exit 2
  fi
  chmod +x "$extracted" 2>/dev/null || true
  printf "%s\n" "$extracted"
}

seed_path=""
seed_requested="0"
if [ "$require_seed" = "1" ] || [ "$seed" != "" ] || [ "$seed_id" != "" ] || [ "$seed_tar" != "" ]; then
  seed_requested="1"
fi

if [ "$seed_requested" = "1" ]; then
  if [ "$seed" != "" ]; then
    seed_path="$seed"
  elif [ "$seed_tar" != "" ]; then
    seed_path="$(seed_from_tar "$seed_tar")"
  elif [ "$seed_id" != "" ]; then
    try_tar="dist/releases/$seed_id/backend_release.tar.gz"
    if [ ! -f "$try_tar" ]; then
      echo "[Error] missing seed tar for --seed-id:$seed_id ($try_tar)" 1>&2
      exit 2
    fi
    seed_tar="$try_tar"
    seed_path="$(seed_from_tar "$seed_tar")"
  elif [ "$require_seed" = "1" ]; then
    echo "[Error] --require-seed requires explicit --seed/--seed-id/--seed-tar" 1>&2
    exit 2
  fi

  if [ "$seed_path" = "" ] && [ "$require_seed" = "1" ]; then
    echo "[Error] missing seed: pass --seed/--seed-id/--seed-tar" 1>&2
    exit 2
  fi

  if [ "$seed_path" != "" ]; then
    case "$seed_path" in
      /*) ;;
      *) seed_path="$root/$seed_path" ;;
    esac
    if [ ! -x "$seed_path" ]; then
      echo "[Error] seed driver is not executable: $seed_path" 1>&2
      exit 2
    fi
  fi
fi

if [ "$run_publish" != "" ] && [ "$seed_path" = "" ]; then
  echo "[Error] publish requires explicit seed: pass --seed/--seed-id/--seed-tar (or use --no-publish)" 1>&2
  exit 2
fi

mkdir -p chengcache
timing_file="chengcache/backend_prod_closure.timings.$$"
: > "$timing_file"

cleanup_timing_file() {
  rm -f "$timing_file"
}
trap cleanup_timing_file EXIT

timestamp_now() {
  date +%s
}

record_timing() {
  printf '%s\t%s\t%s\n' "$1" "$2" "$3" >>"$timing_file"
}

print_timing_summary() {
  if [ ! -s "$timing_file" ]; then
    return
  fi
  tab="$(printf '\t')"
  echo "== backend_prod_closure.timing_top =="
  sort -t "$tab" -k3,3nr "$timing_file" | head -n 12 | while IFS="$tab" read -r label status duration; do
    [ "$label" = "" ] && continue
    echo "  ${duration}s [$status] $label"
  done
}

sanitize_diag_label() {
  printf '%s' "$1" | tr -cs 'A-Za-z0-9._-' '_'
}

prepare_timeout_diag() {
  label="$1"
  timeout_diag_last_file=""
  case "$timeout_diag_enabled" in
    1|true|TRUE|yes|YES|on|ON)
      ;;
    *)
      return 0
      ;;
  esac
  if ! command -v "$timeout_diag_tool" >/dev/null 2>&1; then
    return 0
  fi
  mkdir -p "$timeout_diag_dir"
  safe_label="$(sanitize_diag_label "$label")"
  if [ "$safe_label" = "" ]; then
    safe_label="gate"
  fi
  stamp="$(date +%Y%m%dT%H%M%S)"
  timeout_diag_last_file="$timeout_diag_dir/${stamp}_${safe_label}.sample.txt"
  export CHENG_TIMEOUT_DIAG_FILE="$timeout_diag_last_file"
  export CHENG_TIMEOUT_DIAG_SECONDS="$timeout_diag_seconds"
  export CHENG_TIMEOUT_DIAG_TOOL="$timeout_diag_tool"
}

finish_timeout_diag() {
  status="$1"
  label="$2"
  if [ "$status" -eq 124 ] && [ "$timeout_diag_last_file" != "" ]; then
    echo "[backend_prod_closure] timeout diag ($label): $timeout_diag_last_file" 1>&2
    case "$timeout_diag_summary_enabled" in
      1|true|TRUE|yes|YES|on|ON)
        if [ -f "src/tooling/summarize_timeout_diag.sh" ]; then
          set +e
          sh src/tooling/summarize_timeout_diag.sh --file:"$timeout_diag_last_file" --top:"$timeout_diag_summary_top"
          set -e
        fi
        ;;
    esac
  fi
  unset CHENG_TIMEOUT_DIAG_FILE CHENG_TIMEOUT_DIAG_SECONDS CHENG_TIMEOUT_DIAG_TOOL
  timeout_diag_last_file=""
}

run_with_timeout_labeled() {
  label="$1"
  seconds="$2"
  shift 2
  prepare_timeout_diag "$label"
  run_with_timeout "$seconds" "$@"
  status="$?"
  finish_timeout_diag "$status" "$label"
  return "$status"
}

run_required() {
  label="$1"
  shift
  run_required_timeout "$label" "$selfhost_gate_timeout" "$@"
}

run_required_timeout() {
  label="$1"
  timeout_sec="$2"
  shift 2
  start="$(timestamp_now)"
  set +e
  if [ "$timeout_sec" -gt 0 ] 2>/dev/null; then
    run_with_timeout_labeled "$label" "$timeout_sec" "$@"
  else
    "$@"
  fi
  status="$?"
  set -e
  end="$(timestamp_now)"
  duration=$((end - start))
  if [ "$status" -eq 0 ]; then
    record_timing "$label" "ok" "$duration"
  else
    if [ "$status" -eq 124 ]; then
      echo "[backend_prod_closure] $label timed out after ${timeout_sec}s" 1>&2
    fi
    record_timing "$label" "fail" "$duration"
    exit "$status"
  fi
}

run_optional() {
  label="$1"
  shift
  start="$(timestamp_now)"
  set +e
  if [ "$selfhost_gate_timeout" -gt 0 ] 2>/dev/null; then
    run_with_timeout_labeled "$label" "$selfhost_gate_timeout" "$@"
  else
    "$@"
  fi
  status="$?"
  set -e
  end="$(timestamp_now)"
  duration=$((end - start))
  if [ "$status" -eq 0 ]; then
    record_timing "$label" "ok" "$duration"
  elif [ "$status" -eq 2 ]; then
    if [ "$allow_skip" != "" ]; then
      echo "== $label (skip) =="
      record_timing "$label" "skip" "$duration"
    else
      echo "[backend_prod_closure] $label requested skip, but --strict is enabled" 1>&2
      record_timing "$label" "fail" "$duration"
      exit 1
    fi
  else
    if [ "$status" -eq 124 ]; then
      echo "[backend_prod_closure] $label timed out after ${selfhost_gate_timeout}s" 1>&2
    fi
    record_timing "$label" "fail" "$duration"
    exit "$status"
  fi
}

run_with_timeout() {
  seconds="$1"
  shift
  perl -e '
    use POSIX qw(setsid WNOHANG);
    my $timeout = shift;
    my $diag_file = $ENV{"CHENG_TIMEOUT_DIAG_FILE"} // "";
    my $diag_tool = $ENV{"CHENG_TIMEOUT_DIAG_TOOL"} // "sample";
    my $diag_secs = $ENV{"CHENG_TIMEOUT_DIAG_SECONDS"} // 5;
    if ($diag_secs !~ /^\d+$/ || $diag_secs <= 0) {
      $diag_secs = 5;
    } elsif ($diag_secs > 30) {
      $diag_secs = 30;
    }
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
        if ($diag_file ne "" && $^O eq "darwin") {
          system($diag_tool, "$pid", "$diag_secs", "-mayDie", "-file", $diag_file);
        }
        kill "TERM", -$pid;
        select(undef, undef, undef, 0.5);
        kill "KILL", -$pid;
        exit 124;
      }
      select(undef, undef, undef, 0.1);
    }
  ' "$seconds" "$@"
}

driver_stage0_probe_ok() {
  cand="$1"
  [ "$cand" != "" ] || return 1
  [ -x "$cand" ] || return 1
  set +e
  resolved="$(CHENG_BACKEND_DRIVER="$cand" CHENG_BACKEND_DRIVER_PATH_STAGE1_SMOKE=0 CHENG_BACKEND_DRIVER_PATH_STAGE1_DICT_SMOKE=1 \
    sh src/tooling/backend_driver_path.sh 2>/dev/null)"
  status="$?"
  set -e
  if [ "$status" -ne 0 ] || [ "$resolved" = "" ]; then
    return 1
  fi
  probe_mode="${CHENG_BACKEND_PROD_STAGE0_PROBE_MODE:-path}"
  case "$probe_mode" in
    path)
      printf "%s\n" "$resolved"
      return 0
      ;;
    light)
      probe_input="${CHENG_BACKEND_PROD_STAGE0_PROBE_INPUT:-tests/cheng/backend/fixtures/return_add.cheng}"
      if [ ! -f "$probe_input" ]; then
        printf "%s\n" "$resolved"
        return 0
      fi
      probe_target="$(sh src/tooling/detect_host_target.sh 2>/dev/null || echo auto)"
      probe_out="chengcache/.backend_prod_stage0_probe_light_$$.o"
      probe_timeout="${CHENG_BACKEND_PROD_STAGE0_PROBE_TIMEOUT:-20}"
      rm -f "$probe_out"
      set +e
      run_with_timeout_labeled "backend.stage0_probe.light" "$probe_timeout" env \
        CHENG_ABI=v2_noptr \
        CHENG_STAGE1_STD_NO_POINTERS=1 \
        CHENG_STAGE1_STD_NO_POINTERS_STRICT=0 \
        CHENG_STAGE1_NO_POINTERS_NON_C_ABI=0 \
        CHENG_STAGE1_NO_POINTERS_NON_C_ABI_INTERNAL=0 \
        CHENG_MM=orc \
        CHENG_BACKEND_VALIDATE=0 \
        CHENG_BACKEND_EMIT=obj \
        CHENG_BACKEND_TARGET="$probe_target" \
        CHENG_BACKEND_FRONTEND=mvp \
        CHENG_BACKEND_INPUT="$probe_input" \
        CHENG_BACKEND_OUTPUT="$probe_out" \
        "$resolved" >/dev/null 2>&1
      status="$?"
      set -e
      if [ "$status" -ne 0 ] || [ ! -s "$probe_out" ]; then
        rm -f "$probe_out"
        return 1
      fi
      rm -f "$probe_out"
      ;;
    full)
      probe_target="$(sh src/tooling/detect_host_target.sh 2>/dev/null || echo auto)"
      probe_out="chengcache/.backend_prod_stage0_probe_$$.o"
      probe_timeout="${CHENG_BACKEND_PROD_STAGE0_PROBE_TIMEOUT:-$selfhost_timeout}"
      rm -f "$probe_out"
      set +e
      run_with_timeout_labeled "backend.stage0_probe.full" "$probe_timeout" env \
        CHENG_ABI=v2_noptr \
        CHENG_STAGE1_STD_NO_POINTERS=1 \
        CHENG_STAGE1_STD_NO_POINTERS_STRICT=0 \
        CHENG_STAGE1_NO_POINTERS_NON_C_ABI=0 \
        CHENG_STAGE1_NO_POINTERS_NON_C_ABI_INTERNAL=0 \
        CHENG_STAGE1_SKIP_SEM=1 \
        CHENG_GENERIC_MODE=hybrid \
        CHENG_GENERIC_SPEC_BUDGET=0 \
        CHENG_STAGE1_SKIP_OWNERSHIP=1 \
        CHENG_MM=orc \
        CHENG_CACHE=chengcache \
        CHENG_BACKEND_MULTI=0 \
        CHENG_BACKEND_MULTI_FORCE=0 \
        CHENG_BACKEND_INCREMENTAL=1 \
        CHENG_BACKEND_JOBS=0 \
        CHENG_BACKEND_VALIDATE=1 \
        CHENG_BACKEND_WHOLE_PROGRAM=1 \
        CHENG_BACKEND_EMIT=obj \
        CHENG_BACKEND_TARGET="$probe_target" \
        CHENG_BACKEND_FRONTEND=stage1 \
        CHENG_BACKEND_INPUT=src/backend/tooling/backend_driver.cheng \
        CHENG_BACKEND_OUTPUT="$probe_out" \
        "$resolved" >/dev/null 2>&1
      status="$?"
      set -e
      if [ "$status" -ne 0 ] || [ ! -s "$probe_out" ]; then
        rm -f "$probe_out"
        return 1
      fi
      rm -f "$probe_out"
      ;;
    *)
      echo "[Error] invalid CHENG_BACKEND_PROD_STAGE0_PROBE_MODE: $probe_mode (expected path|light|full)" 1>&2
      return 1
      ;;
  esac
  printf "%s\n" "$resolved"
  return 0
}

if [ "$allow_skip" = "" ]; then
  export CHENG_BACKEND_MATRIX_STRICT=1
fi
if [ "${CHENG_ABI:-}" != "" ] && [ "${CHENG_ABI}" != "v2_noptr" ]; then
  echo "[backend_prod_closure] only CHENG_ABI=v2_noptr is supported (got: ${CHENG_ABI})" 1>&2
  exit 2
fi
export CHENG_ABI=v2_noptr
if [ "${CHENG_STAGE1_STD_NO_POINTERS:-}" = "" ]; then
  export CHENG_STAGE1_STD_NO_POINTERS=1
fi
if [ "${CHENG_STAGE1_STD_NO_POINTERS_STRICT:-}" = "" ]; then
  export CHENG_STAGE1_STD_NO_POINTERS_STRICT=0
fi
if [ "${CHENG_STAGE1_NO_POINTERS_NON_C_ABI:-}" = "" ]; then
  export CHENG_STAGE1_NO_POINTERS_NON_C_ABI=1
fi
if [ "${CHENG_STAGE1_NO_POINTERS_NON_C_ABI_INTERNAL:-}" = "" ]; then
  export CHENG_STAGE1_NO_POINTERS_NON_C_ABI_INTERNAL=1
fi

main_allow_selfhost_driver="${CHENG_BACKEND_MAIN_ALLOW_SELFHOST_DRIVER:-auto}"
if [ "$main_allow_selfhost_driver" = "auto" ]; then
  if [ "${CHENG_STAGE1_NO_POINTERS_NON_C_ABI:-0}" = "1" ]; then
    main_allow_selfhost_driver="0"
  else
    main_allow_selfhost_driver="1"
  fi
fi
case "$main_allow_selfhost_driver" in
  0|1)
    ;;
  *)
    echo "[Error] invalid CHENG_BACKEND_MAIN_ALLOW_SELFHOST_DRIVER: $main_allow_selfhost_driver (expected auto|0|1)" 1>&2
    exit 2
    ;;
esac

selfhost_stage0="${seed_path:-}"
if [ "$selfhost_stage0" = "" ] && [ "$backend_driver_explicit" = "1" ]; then
  if [ -x "${CHENG_BACKEND_DRIVER:-}" ]; then
    selfhost_stage0="${CHENG_BACKEND_DRIVER}"
  fi
fi
if [ "$selfhost_stage0" = "" ]; then
  for cand in \
    "artifacts/backend_selfhost_self_obj/cheng.stage2" \
    "artifacts/backend_selfhost_self_obj/cheng.stage1" \
    "artifacts/backend_driver/cheng" \
    "dist/releases/current/cheng" \
    "artifacts/backend_seed/cheng.stage2"; do
    echo "[backend_prod_closure] stage0 probe: $cand" 1>&2
    resolved=""
    if resolved="$(driver_stage0_probe_ok "$cand")"; then
      selfhost_stage0="$resolved"
      break
    fi
  done
fi
if [ "$selfhost_stage0" = "" ] && [ "$seed_requested" != "1" ] && [ "$backend_driver_explicit" != "1" ]; then
  fallback_stage0="$(sh src/tooling/backend_driver_path.sh)"
  resolved=""
  if resolved="$(driver_stage0_probe_ok "$fallback_stage0")"; then
    selfhost_stage0="$resolved"
  fi
fi
if [ "$run_selfhost" != "" ] && [ "$selfhost_stage0" = "" ]; then
  echo "[backend_prod_closure] no healthy selfhost stage0 driver found" 1>&2
  echo "  hint: provide --seed:<path> or CHENG_BACKEND_DRIVER=<path>" 1>&2
  exit 1
fi

driver_runnable() {
  cand="$1"
  [ "$cand" != "" ] || return 1
  [ -x "$cand" ] || return 1
  set +e
  env CHENG_BACKEND_DRIVER="$cand" CHENG_BACKEND_DRIVER_PATH_STAGE1_SMOKE=0 CHENG_BACKEND_DRIVER_PATH_STAGE1_DICT_SMOKE=1 \
    sh src/tooling/backend_driver_path.sh >/dev/null 2>&1
  status="$?"
  set -e
  [ "$status" -eq 0 ]
}

driver_help_ok() {
  cand="$1"
  [ "$cand" != "" ] || return 1
  [ -x "$cand" ] || return 1
  set +e
  "$cand" --help >/dev/null 2>&1
  status="$?"
  set -e
  case "$status" in
    0|1|2) return 0 ;;
  esac
  return 1
}

if [ "$only_self_obj_bootstrap" != "" ]; then
  if [ "$selfhost_stage0" != "" ]; then
    run_optional "backend.selfhost_bootstrap_self_obj" env CHENG_STAGE1_NO_POINTERS_NON_C_ABI=0 CHENG_STAGE1_NO_POINTERS_NON_C_ABI_INTERNAL=0 CHENG_SELF_OBJ_BOOTSTRAP_TIMEOUT="$selfhost_timeout" CHENG_SELF_OBJ_BOOTSTRAP_REUSE="$selfhost_reuse" CHENG_SELF_OBJ_BOOTSTRAP_SESSION="$selfhost_session" CHENG_SELF_OBJ_BOOTSTRAP_MODE="$selfhost_mode" CHENG_SELF_OBJ_BOOTSTRAP_STAGE0="$selfhost_stage0" \
      sh src/tooling/verify_backend_selfhost_bootstrap_self_obj.sh
  else
    run_optional "backend.selfhost_bootstrap_self_obj" env CHENG_STAGE1_NO_POINTERS_NON_C_ABI=0 CHENG_STAGE1_NO_POINTERS_NON_C_ABI_INTERNAL=0 CHENG_SELF_OBJ_BOOTSTRAP_TIMEOUT="$selfhost_timeout" CHENG_SELF_OBJ_BOOTSTRAP_REUSE="$selfhost_reuse" CHENG_SELF_OBJ_BOOTSTRAP_SESSION="$selfhost_session" CHENG_SELF_OBJ_BOOTSTRAP_MODE="$selfhost_mode" sh src/tooling/verify_backend_selfhost_bootstrap_self_obj.sh
  fi
  if [ "$run_selfhost_perf" = "1" ]; then
    run_required "backend.selfhost_perf_regression" env \
      CHENG_SELFHOST_PERF_SESSION="$selfhost_session" \
      CHENG_SELFHOST_PERF_AUTO_BUILD=0 \
      sh src/tooling/verify_backend_selfhost_perf_regression.sh
  fi
  print_timing_summary
  echo "backend_prod_closure ok"
  exit 0
fi

if [ "$run_selfhost" != "" ]; then
  selfhost_label="backend.selfhost_bootstrap"
  if [ "$selfhost_mode" = "fast" ]; then
    selfhost_label="backend.selfhost_bootstrap.fast"
  else
    selfhost_label="backend.selfhost_bootstrap.strict"
  fi
  if [ "$selfhost_stage0" != "" ]; then
    run_optional "$selfhost_label" env CHENG_STAGE1_NO_POINTERS_NON_C_ABI=0 CHENG_STAGE1_NO_POINTERS_NON_C_ABI_INTERNAL=0 CHENG_SELF_OBJ_BOOTSTRAP_TIMEOUT="$selfhost_timeout" CHENG_SELF_OBJ_BOOTSTRAP_REUSE="$selfhost_reuse" CHENG_SELF_OBJ_BOOTSTRAP_SESSION="$selfhost_session" CHENG_SELF_OBJ_BOOTSTRAP_MODE="$selfhost_mode" CHENG_SELF_OBJ_BOOTSTRAP_STAGE0="$selfhost_stage0" \
      sh src/tooling/verify_backend_selfhost_bootstrap.sh
  else
    run_optional "$selfhost_label" env CHENG_STAGE1_NO_POINTERS_NON_C_ABI=0 CHENG_STAGE1_NO_POINTERS_NON_C_ABI_INTERNAL=0 CHENG_SELF_OBJ_BOOTSTRAP_TIMEOUT="$selfhost_timeout" CHENG_SELF_OBJ_BOOTSTRAP_REUSE="$selfhost_reuse" CHENG_SELF_OBJ_BOOTSTRAP_SESSION="$selfhost_session" CHENG_SELF_OBJ_BOOTSTRAP_MODE="$selfhost_mode" sh src/tooling/verify_backend_selfhost_bootstrap.sh
  fi
  if [ "$run_selfhost_strict" = "1" ] && [ "$selfhost_mode" = "fast" ]; then
    strict_session="${CHENG_BACKEND_PROD_SELFHOST_STRICT_SESSION:-$selfhost_session}"
    strict_allow_fast_reuse="${CHENG_BACKEND_PROD_SELFHOST_STRICT_ALLOW_FAST_REUSE:-1}"
    if [ "$selfhost_stage0" != "" ]; then
      run_optional "backend.selfhost_bootstrap.strict" env CHENG_STAGE1_NO_POINTERS_NON_C_ABI=0 CHENG_STAGE1_NO_POINTERS_NON_C_ABI_INTERNAL=0 CHENG_SELF_OBJ_BOOTSTRAP_TIMEOUT="$selfhost_timeout" CHENG_SELF_OBJ_BOOTSTRAP_REUSE="$selfhost_reuse" CHENG_SELF_OBJ_BOOTSTRAP_SESSION="$strict_session" CHENG_SELF_OBJ_BOOTSTRAP_MODE=strict CHENG_SELF_OBJ_BOOTSTRAP_STRICT_ALLOW_FAST_REUSE="$strict_allow_fast_reuse" CHENG_SELF_OBJ_BOOTSTRAP_STAGE0="$selfhost_stage0" \
        sh src/tooling/verify_backend_selfhost_bootstrap.sh
    else
      run_optional "backend.selfhost_bootstrap.strict" env CHENG_STAGE1_NO_POINTERS_NON_C_ABI=0 CHENG_STAGE1_NO_POINTERS_NON_C_ABI_INTERNAL=0 CHENG_SELF_OBJ_BOOTSTRAP_TIMEOUT="$selfhost_timeout" CHENG_SELF_OBJ_BOOTSTRAP_REUSE="$selfhost_reuse" CHENG_SELF_OBJ_BOOTSTRAP_SESSION="$strict_session" CHENG_SELF_OBJ_BOOTSTRAP_MODE=strict CHENG_SELF_OBJ_BOOTSTRAP_STRICT_ALLOW_FAST_REUSE="$strict_allow_fast_reuse" sh src/tooling/verify_backend_selfhost_bootstrap.sh
    fi
  fi
  if [ "$run_selfhost_strict_noreuse_probe" = "1" ]; then
    strict_probe_timeout="$selfhost_strict_noreuse_probe_timeout"
    gate_timeout_numeric="1"
    probe_timeout_numeric="1"
    case "$selfhost_strict_noreuse_gate_timeout" in
      ''|*[!0-9]*)
        gate_timeout_numeric="0"
        ;;
    esac
    case "$strict_probe_timeout" in
      ''|*[!0-9]*)
        probe_timeout_numeric="0"
        ;;
    esac
    if [ "$gate_timeout_numeric" = "1" ] && [ "$probe_timeout_numeric" = "1" ]; then
      if [ "$selfhost_strict_noreuse_gate_timeout" -gt 0 ] && [ "$strict_probe_timeout" -ge "$selfhost_strict_noreuse_gate_timeout" ]; then
        strict_probe_timeout=$((selfhost_strict_noreuse_gate_timeout - 5))
        if [ "$strict_probe_timeout" -lt 1 ]; then
          strict_probe_timeout=1
        fi
      fi
    fi
    strict_probe_session_base="${CHENG_BACKEND_PROD_SELFHOST_STRICT_SESSION:-$selfhost_session}"
    strict_probe_session="${CHENG_BACKEND_PROD_SELFHOST_STRICT_NOREUSE_SESSION:-${strict_probe_session_base}.noreuse}"
    strict_probe_require="${CHENG_BACKEND_SELFHOST_STRICT_NOREUSE_PROBE_REQUIRE:-0}"
    strict_probe_allow_stage0_fallback="${CHENG_BACKEND_PROD_SELFHOST_STRICT_NOREUSE_ALLOW_STAGE0_FALLBACK:-0}"
    if [ "$selfhost_stage0" != "" ]; then
      run_required_timeout "backend.selfhost_strict_noreuse_probe" "$selfhost_strict_noreuse_gate_timeout" env CHENG_STAGE1_NO_POINTERS_NON_C_ABI=0 CHENG_STAGE1_NO_POINTERS_NON_C_ABI_INTERNAL=0 CHENG_SELFHOST_STRICT_PROBE_SESSION="$strict_probe_session" CHENG_SELFHOST_STRICT_PROBE_TIMEOUT="$strict_probe_timeout" CHENG_SELFHOST_STRICT_PROBE_REQUIRE="$strict_probe_require" CHENG_SELFHOST_STRICT_PROBE_REUSE=0 CHENG_SELFHOST_STRICT_PROBE_STRICT_ALLOW_FAST_REUSE=0 CHENG_SELFHOST_STRICT_PROBE_ALLOW_STAGE0_FALLBACK="$strict_probe_allow_stage0_fallback" CHENG_SELFHOST_STRICT_PROBE_STAGE0="$selfhost_stage0" sh src/tooling/verify_backend_selfhost_strict_noreuse_probe.sh
    else
      run_required_timeout "backend.selfhost_strict_noreuse_probe" "$selfhost_strict_noreuse_gate_timeout" env CHENG_STAGE1_NO_POINTERS_NON_C_ABI=0 CHENG_STAGE1_NO_POINTERS_NON_C_ABI_INTERNAL=0 CHENG_SELFHOST_STRICT_PROBE_SESSION="$strict_probe_session" CHENG_SELFHOST_STRICT_PROBE_TIMEOUT="$strict_probe_timeout" CHENG_SELFHOST_STRICT_PROBE_REQUIRE="$strict_probe_require" CHENG_SELFHOST_STRICT_PROBE_REUSE=0 CHENG_SELFHOST_STRICT_PROBE_STRICT_ALLOW_FAST_REUSE=0 CHENG_SELFHOST_STRICT_PROBE_ALLOW_STAGE0_FALLBACK="$strict_probe_allow_stage0_fallback" sh src/tooling/verify_backend_selfhost_strict_noreuse_probe.sh
    fi
  fi
fi

if [ "$run_selfhost" != "" ] && [ "$run_selfhost_perf" = "1" ]; then
  selfhost_perf_session="$selfhost_session"
  if [ "$selfhost_mode" = "fast" ] && [ "$run_selfhost_strict" = "1" ]; then
    case "$selfhost_perf_use_strict_session" in
      1|true|TRUE|yes|YES|on|ON)
        selfhost_perf_session="${CHENG_BACKEND_PROD_SELFHOST_STRICT_SESSION:-$selfhost_session}"
        ;;
    esac
  fi
  run_required "backend.selfhost_perf_regression" env \
    CHENG_SELFHOST_PERF_SESSION="$selfhost_perf_session" \
    CHENG_SELFHOST_PERF_AUTO_BUILD=0 \
    sh src/tooling/verify_backend_selfhost_perf_regression.sh
fi

if [ "$run_selfhost" != "" ] && [ "$main_allow_selfhost_driver" = "1" ] && [ -x "artifacts/backend_selfhost_self_obj/cheng.stage2" ]; then
  if driver_runnable "artifacts/backend_selfhost_self_obj/cheng.stage2"; then
    export CHENG_BACKEND_DRIVER="artifacts/backend_selfhost_self_obj/cheng.stage2"
  else
    echo "[backend_prod_closure] warn: selfhost stage2 is not runnable, fallback to stable driver" 1>&2
  fi
fi
if [ "${CHENG_BACKEND_DRIVER:-}" = "" ] && [ "$run_selfhost" != "" ] && [ "$main_allow_selfhost_driver" = "1" ] && [ -x "artifacts/backend_selfhost_self_obj/cheng.stage1" ]; then
  if driver_runnable "artifacts/backend_selfhost_self_obj/cheng.stage1"; then
    export CHENG_BACKEND_DRIVER="artifacts/backend_selfhost_self_obj/cheng.stage1"
  else
    echo "[backend_prod_closure] warn: selfhost stage1 is not runnable, fallback to stable driver" 1>&2
  fi
fi
if [ "$run_selfhost" != "" ] && [ "$main_allow_selfhost_driver" = "0" ]; then
  echo "[backend_prod_closure] info: keep stable driver for main gates (strict non-C-ABI policy); selfhost is used by abi_v2 gate" 1>&2
fi
if [ "${CHENG_BACKEND_DRIVER:-}" = "" ]; then
  export CHENG_BACKEND_DRIVER="$(sh src/tooling/backend_driver_path.sh)"
fi
if ! driver_runnable "${CHENG_BACKEND_DRIVER:-}"; then
  echo "[backend_prod_closure] warn: selected main driver is not runnable, try fallback" 1>&2
  fallback_main_driver=""
  if [ "$run_selfhost" != "" ] && driver_runnable "artifacts/backend_selfhost_self_obj/cheng.stage1"; then
    fallback_main_driver="artifacts/backend_selfhost_self_obj/cheng.stage1"
  elif [ "$run_selfhost" != "" ] && driver_runnable "artifacts/backend_selfhost_self_obj/cheng.stage2"; then
    fallback_main_driver="artifacts/backend_selfhost_self_obj/cheng.stage2"
  else
    fallback_main_driver="$(env CHENG_BACKEND_DRIVER= sh src/tooling/backend_driver_path.sh)"
  fi
  if [ "$fallback_main_driver" = "" ] || ! driver_runnable "$fallback_main_driver"; then
    echo "[backend_prod_closure] no healthy main driver found" 1>&2
    exit 1
  fi
  export CHENG_BACKEND_DRIVER="$fallback_main_driver"
  echo "[backend_prod_closure] fallback main driver: $CHENG_BACKEND_DRIVER" 1>&2
fi

# Main driver has been validated above; avoid repeated stage1 smoke in each gate.
if [ "${CHENG_BACKEND_DRIVER_PATH_STAGE1_SMOKE:-}" = "" ]; then
  export CHENG_BACKEND_DRIVER_PATH_STAGE1_SMOKE=0
fi

closedloop_fullspec="${CHENG_BACKEND_RUN_FULLSPEC:-0}"
closedloop_timeout="$selfhost_gate_timeout"
if [ "$closedloop_fullspec" = "1" ]; then
  closedloop_timeout="${CHENG_BACKEND_PROD_CLOSEDLOOP_TIMEOUT:-120}"
fi

if [ "$validate" != "" ]; then
  run_required_timeout "backend.closedloop" "$closedloop_timeout" env CHENG_ABI=v2_noptr CHENG_STAGE1_STD_NO_POINTERS=1 CHENG_STAGE1_STD_NO_POINTERS_STRICT=0 CHENG_STAGE1_NO_POINTERS_NON_C_ABI=1 CHENG_STAGE1_NO_POINTERS_NON_C_ABI_INTERNAL=1 CHENG_BACKEND_RUN_FULLSPEC="$closedloop_fullspec" CHENG_BACKEND_VALIDATE=1 sh src/tooling/verify_backend_closedloop.sh
else
  run_required_timeout "backend.closedloop" "$closedloop_timeout" env CHENG_ABI=v2_noptr CHENG_STAGE1_STD_NO_POINTERS=1 CHENG_STAGE1_STD_NO_POINTERS_STRICT=0 CHENG_STAGE1_NO_POINTERS_NON_C_ABI=1 CHENG_STAGE1_NO_POINTERS_NON_C_ABI_INTERNAL=1 CHENG_BACKEND_RUN_FULLSPEC="$closedloop_fullspec" sh src/tooling/verify_backend_closedloop.sh
fi

abi_v2_driver_env=""
abi_v2_driver=""
if [ "${CHENG_BACKEND_ABI_V2_DRIVER:-}" != "" ]; then
  abi_v2_driver="${CHENG_BACKEND_ABI_V2_DRIVER}"
elif [ "$run_selfhost" != "" ] && driver_help_ok "artifacts/backend_selfhost_self_obj/cheng.stage1"; then
  abi_v2_driver="artifacts/backend_selfhost_self_obj/cheng.stage1"
elif [ "$run_selfhost" != "" ] && driver_help_ok "artifacts/backend_selfhost_self_obj/cheng.stage2"; then
  abi_v2_driver="artifacts/backend_selfhost_self_obj/cheng.stage2"
elif [ "${CHENG_BACKEND_DRIVER:-}" != "" ]; then
  abi_v2_driver="${CHENG_BACKEND_DRIVER}"
fi
if [ "$abi_v2_driver" != "" ]; then
  echo "[backend_prod_closure] abi_v2 gate driver: $abi_v2_driver"
  abi_v2_driver_env="CHENG_BACKEND_DRIVER=$abi_v2_driver"
fi
# shellcheck disable=SC2086
run_required "backend.abi_v2_noptr" env CHENG_ABI=v2_noptr CHENG_STAGE1_STD_NO_POINTERS=1 CHENG_STAGE1_STD_NO_POINTERS_STRICT=1 CHENG_STAGE1_NO_POINTERS_NON_C_ABI=1 CHENG_STAGE1_NO_POINTERS_NON_C_ABI_INTERNAL=1 $abi_v2_driver_env sh src/tooling/verify_backend_abi_v2_noptr.sh

run_optional "backend.obj_fullspec_gate" env CHENG_MM=orc sh src/tooling/verify_backend_obj_fullspec_gate.sh

if [ "$run_fullchain" != "" ]; then
  run_optional "backend.fullchain_bootstrap.obj_only" env CHENG_FULLCHAIN_OBJ_ONLY=1 sh src/tooling/verify_fullchain_bootstrap.sh
fi

if [ "$run_det_strict" != "" ]; then
  run_required "backend.determinism_strict" sh src/tooling/verify_backend_determinism_strict.sh
  run_optional "backend.obj_determinism_strict" sh src/tooling/verify_backend_obj_determinism_strict.sh
  run_optional "backend.exe_determinism_strict" sh src/tooling/verify_backend_exe_determinism_strict.sh
fi

if [ "$run_opt" != "" ]; then
  run_required "backend.opt" sh src/tooling/verify_backend_opt.sh
fi

if [ "$run_opt2" != "" ]; then
  run_required "backend.opt2" sh src/tooling/verify_backend_opt2.sh
  run_required "backend.multi_lto" sh src/tooling/verify_backend_multi_lto.sh
fi

if [ "$run_ssa" != "" ]; then
  run_required "backend.ssa" sh src/tooling/verify_backend_ssa.sh
fi

if [ "$run_ffi" != "" ]; then
  run_required "backend.ffi_abi" sh src/tooling/verify_backend_ffi_abi.sh
fi

if [ "$run_self_obj_writer" != "" ]; then
  run_optional "backend.self_obj_writer.elf" sh src/tooling/verify_backend_self_obj_writer.sh
  run_optional "backend.self_obj_writer.elf_determinism" sh src/tooling/verify_backend_self_obj_writer_elf_determinism.sh
  run_optional "backend.self_linker.elf" sh src/tooling/verify_backend_self_linker_elf.sh
  run_optional "backend.self_obj_writer.macho" sh src/tooling/verify_backend_self_obj_writer_macho.sh
  run_optional "backend.self_obj_writer.macho_determinism" sh src/tooling/verify_backend_self_obj_writer_macho_determinism.sh
  run_optional "backend.self_obj_writer.coff" sh src/tooling/verify_backend_self_obj_writer_coff.sh
  run_optional "backend.self_obj_writer.coff_determinism" sh src/tooling/verify_backend_self_obj_writer_coff_determinism.sh
  run_optional "backend.self_linker.coff" sh src/tooling/verify_backend_self_linker_coff.sh
  run_optional "backend.coff_lld_link" sh src/tooling/verify_backend_coff_lld_link.sh
fi

if [ "$run_obj" != "" ]; then
  run_optional "backend.obj" sh src/tooling/verify_backend_obj.sh
fi

if [ "$run_obj_det" != "" ]; then
  run_optional "backend.obj_determinism" sh src/tooling/verify_backend_obj_determinism.sh
fi

if [ "$run_exe_det" != "" ]; then
  run_optional "backend.exe_determinism" sh src/tooling/verify_backend_exe_determinism.sh
fi

if [ "$run_sanitizer" != "" ]; then
  run_optional "backend.sanitizer" sh src/tooling/verify_backend_sanitizer.sh
fi

if [ "$run_debug" != "" ]; then
  run_optional "backend.debug" sh src/tooling/verify_backend_debug.sh
fi

stage2_driver="artifacts/backend_selfhost_self_obj/cheng.stage2"
manifest_args=""
bundle_args=""
if [ -x "$stage2_driver" ]; then
  manifest_args="$manifest_args --driver:$stage2_driver"
  bundle_args="$bundle_args --driver:$stage2_driver"
fi

fullchain_bin="artifacts/fullchain/bin"
for extra in "$fullchain_bin/cheng_pkg_source" "$fullchain_bin/cheng_pkg" "$fullchain_bin/cheng_storage"; do
  if [ -f "$extra" ]; then
    bundle_args="$bundle_args --extra:$extra"
  fi
done
for extra in \
  "src/tooling/chengb.sh" \
  "src/tooling/chengc.sh" \
  "src/tooling/detect_host_target.sh" \
  "src/tooling/backend_link_env.sh" \
  "src/tooling/summarize_timeout_diag.sh" \
  "src/tooling/verify_backend_selfhost_perf_regression.sh" \
  "src/tooling/verify_backend_selfhost_strict_noreuse_probe.sh" \
  "src/tooling/selfhost_perf_baseline.env" \
  "src/tooling/verify_libp2p_frontier.sh" \
  "src/tooling/verify_backend_selfhost_bootstrap_fast.sh" \
  "src/tooling/verify_backend_obj_fullspec_gate.sh" \
  "examples/backend_obj_fullspec.cheng"; do
  if [ -f "$extra" ]; then
    bundle_args="$bundle_args --extra:$extra"
  fi
done

# shellcheck disable=SC2086
run_required "backend.release_manifest" sh src/tooling/backend_release_manifest.sh --out:"$manifest" $manifest_args
if [ "$run_bundle" != "" ]; then
  # shellcheck disable=SC2086
  run_required "backend.release_bundle" sh src/tooling/backend_release_bundle.sh --out:"$bundle" --manifest:"$manifest" $bundle_args
  if [ "$run_sign" != "" ]; then
    run_optional "backend.release_sign" sh src/tooling/backend_release_sign.sh --manifest:"$manifest" --bundle:"$bundle"
    run_optional "backend.release_verify" sh src/tooling/backend_release_verify.sh --manifest:"$manifest" --bundle:"$bundle"
    if [ "$run_publish" != "" ]; then
      run_required "backend.release_publish" sh src/tooling/backend_release_publish.sh --manifest:"$manifest" --bundle:"$bundle" --dst:"${CHENG_BACKEND_RELEASE_DST:-dist/releases}"
    fi
  fi
fi
if [ "$run_stress" != "" ]; then
  run_required "backend.stress" sh src/tooling/verify_backend_stress.sh
  run_required "backend.concurrency_stress" sh src/tooling/verify_backend_concurrency_stress.sh
fi
if [ "$run_mm" != "" ]; then
  run_required "backend.mm" sh src/tooling/verify_backend_mm.sh
fi

print_timing_summary
echo "backend_prod_closure ok"
