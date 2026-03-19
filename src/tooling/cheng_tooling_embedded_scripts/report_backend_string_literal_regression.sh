#!/usr/bin/env sh
:
set -eu

root="$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)"
cd "$root"

proof_meta_value() {
  driver_path="$1"
  key="$2"
  meta_path="$driver_path.meta"
  if [ ! -f "$meta_path" ]; then
    return 1
  fi
  sed -n "s/^$key=//p" "$meta_path" | sed -n '1p'
}

driver_selection=""
driver_selection_detail=""
driver_meta=""

choose_default_driver() {
  currentsrc_driver="$root/artifacts/backend_selfhost_self_obj/probe_currentsrc_proof/cheng.stage2.proof"
  strict_driver="$root/artifacts/backend_selfhost_self_obj/probe_prod.strict.noreuse/cheng.stage2.proof"
  currentsrc_publish_status=""
  currentsrc_degraded_reason=""

  if [ -x "$currentsrc_driver" ]; then
    currentsrc_publish_status="$(proof_meta_value "$currentsrc_driver" publish_status || true)"
    currentsrc_degraded_reason="$(proof_meta_value "$currentsrc_driver" degraded_reason || true)"
  fi

  if [ -x "$currentsrc_driver" ] && [ "$currentsrc_publish_status" != "degraded" ]; then
    driver_selection="probe_currentsrc_proof"
    driver_meta="$currentsrc_driver.meta"
    printf '%s\n' "$currentsrc_driver"
    return 0
  fi

  if [ -x "$strict_driver" ]; then
    driver_selection="probe_prod.strict.noreuse"
    driver_meta="$strict_driver.meta"
    if [ "$currentsrc_publish_status" = "degraded" ]; then
      driver_selection_detail="currentsrc_proof_degraded"
      if [ "$currentsrc_degraded_reason" != "" ]; then
        driver_selection_detail="$driver_selection_detail:$currentsrc_degraded_reason"
      fi
    fi
    printf '%s\n' "$strict_driver"
    return 0
  fi

  if [ -x "$currentsrc_driver" ]; then
    driver_selection="probe_currentsrc_proof_degraded"
    driver_meta="$currentsrc_driver.meta"
    driver_selection_detail="$currentsrc_publish_status"
    if [ "$currentsrc_degraded_reason" != "" ]; then
      driver_selection_detail="$driver_selection_detail:$currentsrc_degraded_reason"
    fi
    printf '%s\n' "$currentsrc_driver"
    return 0
  fi

  driver_selection="backend_driver"
  printf '%s\n' "$root/artifacts/backend_driver/cheng"
}

driver="${BACKEND_DRIVER:-}"
if [ "$driver" = "" ]; then
  driver="$(choose_default_driver)"
else
  driver_selection="env BACKEND_DRIVER"
  driver_meta="$driver.meta"
fi
timeout_secs="${BACKEND_STRING_LITERAL_TIMEOUT:-60}"
artifacts_dir="$root/artifacts"
requested_out_dir="${BACKEND_STRING_LITERAL_REPORT_DIR:-}"
final_out_dir="$root/artifacts/backend_string_literal_regression"
publish_dir=""
if [ "$requested_out_dir" != "" ]; then
  out_dir="$requested_out_dir"
else
  mkdir -p "$artifacts_dir"
  out_dir="$(mktemp -d "$artifacts_dir/backend_string_literal_regression.work.XXXXXX")"
fi
report="$out_dir/backend_string_literal_regression.report.txt"
mkdir -p "$out_dir"
: >"$report"

cleanup() {
  if [ "$publish_dir" != "" ] && [ -e "$publish_dir" ]; then
    rm -rf "$publish_dir" 2>/dev/null || true
  fi
  if [ "$requested_out_dir" = "" ] && [ "$out_dir" != "" ] && [ -e "$out_dir" ]; then
    rm -rf "$out_dir" 2>/dev/null || true
  fi
}
trap cleanup EXIT INT TERM

log() {
  printf '%s\n' "$1" | tee -a "$report"
}

run_with_timeout_and_sample() {
  timeout="$1"
  log_file="$2"
  sample_file="$3"
  shift 3
  rm -f "$log_file" "$sample_file"
  (
    "$@" >"$log_file" 2>&1
  ) >/dev/null 2>&1 &
  pid=$!
  elapsed=0
  while kill -0 "$pid" 2>/dev/null; do
    if [ "$elapsed" -ge "$timeout" ]; then
      if command -v sample >/dev/null 2>&1; then
        sample "$pid" 5 -file "$sample_file" >/dev/null 2>&1 || true
      fi
      kill -TERM "$pid" 2>/dev/null || true
      sleep 1
      kill -KILL "$pid" 2>/dev/null || true
      wait "$pid" 2>/dev/null || true
      return 124
    fi
    sleep 1
    elapsed=$((elapsed + 1))
  done
  wait "$pid"
  return $?
}

probe_fixture() {
  name="$1"
  fixture="$2"
  exe="$out_dir/$name.exe"
  compile_log="$out_dir/$name.compile.log"
  sample_log="$out_dir/$name.compile.sample.txt"

  compile_rc=0
  if run_with_timeout_and_sample "$timeout_secs" "$compile_log" "$sample_log" \
      env BACKEND_INPUT="$fixture" BACKEND_OUTPUT="$exe" BACKEND_LINKER=system "$driver"; then
    compile_rc=0
  else
    compile_rc=$?
  fi

  run_rc="-"
  if [ "$compile_rc" -eq 0 ]; then
    if "$exe" >/dev/null 2>&1; then
      run_rc=0
    else
      run_rc=$?
    fi
  fi

  status="fail"
  if [ "$compile_rc" -eq 0 ] && [ "$run_rc" = "0" ]; then
    status="pass"
  elif [ "$compile_rc" -eq 124 ]; then
    status="compile_timeout"
  elif [ "$compile_rc" -eq 139 ]; then
    status="compile_sigsegv"
  elif [ "$compile_rc" -eq 223 ]; then
    status="compile_rc223"
  elif [ "$compile_rc" -eq 0 ]; then
    status="runtime_fail"
  fi

  log "[fixture] $name compile_rc=$compile_rc run_rc=$run_rc status=$status"
  log "[artifact] $name exe=$exe compile_log=$compile_log"
  if [ -f "$sample_log" ] && [ -s "$sample_log" ]; then
    log "[sample] $name file=$sample_log"
  fi
}

log "[meta] date=$(date '+%Y-%m-%d %H:%M:%S %z')"
log "[meta] driver=$driver"
if [ "$driver_selection" != "" ]; then
  log "[meta] driver_selection=$driver_selection"
fi
if [ "$driver_selection_detail" != "" ]; then
  log "[meta] driver_selection_detail=$driver_selection_detail"
fi
if [ "$driver_meta" != "" ] && [ -f "$driver_meta" ]; then
  log "[meta] driver_meta=$driver_meta"
  driver_publish_status="$(proof_meta_value "$driver" publish_status || true)"
  if [ "$driver_publish_status" != "" ]; then
    log "[meta] driver_publish_status=$driver_publish_status"
  fi
fi
log "[meta] timeout_secs=$timeout_secs"

probe_fixture "str_literal_len_probe" \
  "$root/tests/cheng/backend/fixtures/str_literal_len_probe.cheng"
probe_fixture "str_literal_not_empty_probe" \
  "$root/tests/cheng/backend/fixtures/str_literal_not_empty_probe.cheng"
probe_fixture "str_literal_call_arg_probe" \
  "$root/tests/cheng/backend/fixtures/str_literal_call_arg_probe.cheng"
probe_fixture "str_literal_rawbytes_probe" \
  "$root/tests/cheng/backend/fixtures/str_literal_rawbytes_probe.cheng"

log "[done] report=$report"

if [ "$requested_out_dir" = "" ]; then
  publish_dir="$(mktemp -d "$artifacts_dir/backend_string_literal_regression.publish.XXXXXX")"
  rmdir "$publish_dir"
  mv "$out_dir" "$publish_dir"
  out_dir=""
  rm -rf "$final_out_dir"
  mv "$publish_dir" "$final_out_dir"
  publish_dir=""
fi
