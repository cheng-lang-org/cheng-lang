#!/usr/bin/env sh
:
set -eu

root="$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)"
cd "$root"

driver="${BACKEND_DRIVER:-$root/artifacts/backend_driver/cheng}"
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
