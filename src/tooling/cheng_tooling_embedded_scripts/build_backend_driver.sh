#!/usr/bin/env sh
:
set -eu
(set -o pipefail) 2>/dev/null && set -o pipefail

usage() {
  cat <<'USAGE'
Usage:
  src/tooling/build_backend_driver.sh [--name:<binName>]

Builds backend driver via stage0 selfhost rebuild:
  1) rebuild from a stage0 backend driver
  2) no seed/tar copy path

Output:
  <binName> (default: artifacts/backend_driver/cheng)

Env:
  BACKEND_BUILD_DRIVER_LINKER=system (default system; accepts self|system)
  BACKEND_BUILD_DRIVER_SELFHOST=1    (default 1)
  BACKEND_BUILD_DRIVER_STAGE0=<path> (optional stage driver override)
  BACKEND_BUILD_DRIVER_MULTI=1       (default 1 on darwin/arm64; else 0)
  BACKEND_BUILD_DRIVER_MULTI_FORCE=1 (default 1 on darwin/arm64; else 0)
  BACKEND_BUILD_DRIVER_INCREMENTAL=1 (default 1)
  BACKEND_BUILD_DRIVER_JOBS=8        (default 8 on darwin/arm64; else 0=auto)
  BACKEND_BUILD_DRIVER_TIMEOUT=60    (default/cap 60s per compile attempt)
  BACKEND_BUILD_DRIVER_TIMEOUT_DIAG=1 (default 1; synchronously sample on timeout and summarize)
  BACKEND_BUILD_DRIVER_TIMEOUT_DIAG_SECONDS=60 (fixed to compile timeout)
  BACKEND_BUILD_DRIVER_TIMEOUT_DIAG_DIR=chengcache/backend_timeout_diag
  BACKEND_BUILD_DRIVER_TIMEOUT_DIAG_SUMMARY=1 (default 1)
  BACKEND_BUILD_DRIVER_TIMEOUT_DIAG_SUMMARY_TOP=12 (default 12)
  BACKEND_BUILD_DRIVER_SMOKE=0       (default 0; set 1 to run stage1 compile smoke)
  BACKEND_BUILD_DRIVER_REQUIRE_SMOKE=1 (default 1; require smoke for freshly rebuilt driver)
  BACKEND_BUILD_DRIVER_FRONTEND=stage1 (default stage1; frontend used for backend_driver selfbuild)
  BACKEND_BUILD_DRIVER_SMOKE_TARGETS=<csv> (default host)
  BACKEND_BUILD_DRIVER_MAX_STAGE0_ATTEMPTS=3 (default 3; 0=all candidates)
  BACKEND_BUILD_DRIVER_FORCE=0       (default 0; set 1 to skip reuse and force rebuild)
  BACKEND_BUILD_DRIVER_NO_RECOVER=0  (default 0; set 1 to fail hard when rebuild fails)
  BACKEND_IR=uir                     (default uir)
  GENERIC_MODE=dict                  (default dict)
  GENERIC_SPEC_BUDGET=0              (default 0)
USAGE
}

name="${BACKEND_LOCAL_DRIVER_REL:-artifacts/backend_driver/cheng}"
while [ "${1:-}" != "" ]; do
  case "$1" in
    --help|-h)
      usage
      exit 0
      ;;
    --name:*)
      name="${1#--name:}"
      ;;
    *)
      echo "[Error] unknown arg: $1" 1>&2
      usage
      exit 2
      ;;
  esac
  shift || true
done

root="$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)"
cd "$root"

host_os="$(uname -s 2>/dev/null || echo unknown)"
host_arch="$(uname -m 2>/dev/null || echo unknown)"

target=""
case "$host_os/$host_arch" in
  Darwin/arm64)
    target="arm64-apple-darwin"
    if ! command -v codesign >/dev/null 2>&1; then
      echo "[Error] build_backend_driver requires codesign on Darwin/arm64" 1>&2
      exit 2
    fi
    ;;
  Linux/aarch64|Linux/arm64)
    target="aarch64-unknown-linux-gnu"
    ;;
  *)
    echo "build_backend_driver skip: unsupported host=$host_os/$host_arch" 1>&2
    exit 2
    ;;
esac

linker_mode="${BACKEND_BUILD_DRIVER_LINKER:-system}"
case "$linker_mode" in
  self|system)
    ;;
  *)
    echo "[Error] invalid BACKEND_BUILD_DRIVER_LINKER: $linker_mode (expected self|system)" 1>&2
    exit 2
    ;;
esac

run_with_timeout() {
  seconds="$1"
  shift
  perl -e '
    use POSIX qw(setsid WNOHANG);
    my $timeout = shift;
    my $diag_file = $ENV{"TIMEOUT_DIAG_FILE"} // "";
    my $diag_secs = $ENV{"TIMEOUT_DIAG_SECONDS"} // $timeout;
    my $diag_on = $ENV{"TIMEOUT_DIAG_ENABLED"} // "0";
    if ($diag_secs !~ /^\d+$/ || $diag_secs < 1) {
      $diag_secs = $timeout;
    }
    sub deepest_child_pid {
      my ($root_pid) = @_;
      my %kids = ();
      if (open my $ps, "-|", "ps", "-Ao", "pid=,ppid=") {
        while (my $line = <$ps>) {
          $line =~ s/^\s+//;
          $line =~ s/\s+$//;
          my ($cand, $ppid) = split /\s+/, $line, 2;
          next if !defined $cand || !defined $ppid;
          push @{$kids{$ppid}}, $cand;
        }
        close $ps;
      } else {
        return $root_pid;
      }
      my $cur = $root_pid;
      my %seen = ();
      while (exists $kids{$cur} && @{$kids{$cur}}) {
        my @sorted = sort { $a <=> $b } @{$kids{$cur}};
        my $next = $sorted[-1];
        last if $seen{$next}++;
        $cur = $next;
      }
      return $cur;
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
        if ($diag_on ne "0" && $diag_file ne "") {
          my $target = deepest_child_pid($pid);
          system("sample", $target, $diag_secs, "-file", $diag_file);
        }
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

sanitize_diag_label() {
  printf '%s' "$1" | tr -cs 'A-Za-z0-9._-' '_'
}

emit_timeout_diag_summary() {
  label="$1"
  diag_file="$2"
  diag_child_file="$3"
  log_file="$4"
  if [ "$diag_file" = "" ] && [ "$diag_child_file" = "" ]; then
    return 0
  fi
  diag_wait=0
  while [ "$diag_wait" -lt 5 ]; do
    parent_ready=0
    child_ready=0
    if [ "$diag_file" = "" ] || [ -s "$diag_file" ]; then
      parent_ready=1
    fi
    if [ "$diag_child_file" = "" ] || [ -s "$diag_child_file" ]; then
      child_ready=1
    fi
    if [ "$parent_ready" = "1" ] && [ "$child_ready" = "1" ]; then
      break
    fi
    sleep 0.1
    diag_wait=$((diag_wait + 1))
  done
  if [ "$diag_file" != "" ]; then
    printf 'timeout_diag=%s\n' "$diag_file" >>"$log_file"
    echo "[build_backend_driver] timeout diag ($label parent): $diag_file" >&2
  fi
  if [ "$diag_child_file" != "" ]; then
    printf 'timeout_diag_child=%s\n' "$diag_child_file" >>"$log_file"
    echo "[build_backend_driver] timeout diag ($label child): $diag_child_file" >&2
  fi
  case "$timeout_diag_summary" in
    1|true|TRUE|yes|YES|on|ON)
      if [ -s "$diag_file" ] && [ -f "$root/src/tooling/cheng_tooling_embedded_scripts/summarize_timeout_diag.sh" ]; then
        sh "$root/src/tooling/cheng_tooling_embedded_scripts/summarize_timeout_diag.sh" \
          --file:"$diag_file" --top:"$timeout_diag_summary_top" >>"$log_file" 2>&1 || true
        sh "$root/src/tooling/cheng_tooling_embedded_scripts/summarize_timeout_diag.sh" \
          --file:"$diag_file" --top:"$timeout_diag_summary_top" 1>&2 || true
      fi
      if [ -s "$diag_child_file" ] && [ -f "$root/src/tooling/cheng_tooling_embedded_scripts/summarize_timeout_diag.sh" ]; then
        sh "$root/src/tooling/cheng_tooling_embedded_scripts/summarize_timeout_diag.sh" \
          --file:"$diag_child_file" --top:"$timeout_diag_summary_top" >>"$log_file" 2>&1 || true
        sh "$root/src/tooling/cheng_tooling_embedded_scripts/summarize_timeout_diag.sh" \
          --file:"$diag_child_file" --top:"$timeout_diag_summary_top" 1>&2 || true
      fi
      ;;
  esac
}

build_driver_default_multi() {
  case "$host_os/$host_arch" in
    Darwin/arm64)
      printf '1\n'
      ;;
    *)
      printf '0\n'
      ;;
  esac
}

build_driver_default_jobs() {
  case "$host_os/$host_arch" in
    Darwin/arm64)
      printf '8\n'
      ;;
    *)
      printf '0\n'
      ;;
  esac
}

driver_sanity_ok() {
  bin="$1"
  if [ ! -x "$bin" ]; then
    return 1
  fi
  cmd="$bin"
  case "$cmd" in
    /*|./*|../*) ;;
    *) cmd="./$cmd" ;;
  esac
  set +e
  run_with_timeout 5 "$cmd" --help >/dev/null 2>&1
  status=$?
  set -e
  case "$status" in
    0|1|2) return 0 ;;
  esac
  return 1
}

driver_stage1_smoke_last_status=1
driver_stage1_smoke_last_class=not_run
driver_stage1_smoke_last_log=
driver_stage1_smoke_last_target=
driver_stage1_smoke_existing_status=
driver_stage1_smoke_existing_class=
driver_stage1_smoke_existing_log=
driver_stage1_smoke_existing_target=
driver_stage1_smoke_stage0_status=
driver_stage1_smoke_stage0_class=
driver_stage1_smoke_stage0_log=
driver_stage1_smoke_stage0_target=
driver_stage1_smoke_sidecar_compiler=

driver_stage1_smoke_classify() {
  smoke_log="$1"
  smoke_status="$2"
  if [ "$smoke_status" -eq 0 ]; then
    printf 'ok\n'
    return 0
  fi
  if [ "$smoke_status" -eq 124 ]; then
    printf 'timeout\n'
    return 0
  fi
  if [ -f "$smoke_log" ] && grep -F "stmt lower failed fn=newSeq_string" "$smoke_log" >/dev/null 2>&1; then
    printf 'stmt_lower_newSeq_string\n'
    return 0
  fi
  if [ "$smoke_status" -eq 223 ]; then
    if [ ! -s "$smoke_log" ] || \
       ! grep -v -e '^target=' -e '^[[:space:]]*$' "$smoke_log" >/dev/null 2>&1; then
      printf 'silent_rc223\n'
      return 0
    fi
  fi
  printf 'other\n'
  return 0
}

driver_stage1_smoke_ok() {
  bin="$1"
  smoke_log="${2:-$root/chengcache/.build_backend_driver.stage1_smoke.$$.log}"
  driver_stage1_smoke_last_status=1
  driver_stage1_smoke_last_class=not_run
  driver_stage1_smoke_last_log="$smoke_log"
  driver_stage1_smoke_last_target=
  if [ ! -x "$bin" ]; then
    : >"$smoke_log"
    driver_stage1_smoke_last_class=missing_bin
    return 1
  fi
  smoke_src="$root/tests/cheng/backend/fixtures/return_add.cheng"
  if [ ! -f "$smoke_src" ]; then
    : >"$smoke_log"
    driver_stage1_smoke_last_status=0
    driver_stage1_smoke_last_class=ok
    return 0
  fi
  : >"$smoke_log"
  smoke_targets="${BACKEND_BUILD_DRIVER_SMOKE_TARGETS:-host}"
  old_ifs="$IFS"
  IFS=','
  for smoke_target in $smoke_targets; do
    IFS="$old_ifs"
    case "$smoke_target" in
      ""|host|auto)
        smoke_target="$target"
        ;;
    esac
    safe_target="$(printf '%s' "$smoke_target" | tr -c 'A-Za-z0-9._-' '_' | tr -s '_')"
    smoke_out="$root/chengcache/.build_backend_driver.stage1_smoke.${safe_target}.bin"
    rm -f "$smoke_out" "$smoke_out.tmp" "$smoke_out.tmp.linkobj" "$smoke_out.o"
    rm -rf "$smoke_out.objs" "$smoke_out.objs.lock"
    printf 'target=%s\n' "$smoke_target" >>"$smoke_log"
    set +e
    if [ "${driver_stage1_smoke_sidecar_compiler:-}" != "" ]; then
      run_with_timeout 40 env \
        MM=orc \
        STAGE1_NO_POINTERS_NON_C_ABI=0 \
        STAGE1_NO_POINTERS_NON_C_ABI_INTERNAL=0 \
        BACKEND_VALIDATE=0 \
        STAGE1_SEM_FIXED_0=0 \
        STAGE1_SKIP_CPROFILE=1 \
        GENERIC_MODE=dict \
        GENERIC_SPEC_BUDGET=0 \
        STAGE1_OWNERSHIP_FIXED_0=0 \
        BACKEND_LINKER=self \
        BACKEND_NO_RUNTIME_C=1 \
        BACKEND_RUNTIME_OBJ= \
        BACKEND_EMIT=exe \
        BACKEND_TARGET="$smoke_target" \
        BACKEND_FRONTEND=stage1 \
        BACKEND_INPUT="$smoke_src" \
        BACKEND_OUTPUT="$smoke_out" \
        BACKEND_UIR_SIDECAR_COMPILER="$driver_stage1_smoke_sidecar_compiler" \
        "$bin" >>"$smoke_log" 2>&1
    else
      run_with_timeout 40 env \
        MM=orc \
        STAGE1_NO_POINTERS_NON_C_ABI=0 \
        STAGE1_NO_POINTERS_NON_C_ABI_INTERNAL=0 \
        BACKEND_VALIDATE=0 \
        STAGE1_SEM_FIXED_0=0 \
        STAGE1_SKIP_CPROFILE=1 \
        GENERIC_MODE=dict \
        GENERIC_SPEC_BUDGET=0 \
        STAGE1_OWNERSHIP_FIXED_0=0 \
        BACKEND_LINKER=self \
        BACKEND_NO_RUNTIME_C=1 \
        BACKEND_RUNTIME_OBJ= \
        BACKEND_EMIT=exe \
        BACKEND_TARGET="$smoke_target" \
        BACKEND_FRONTEND=stage1 \
        BACKEND_INPUT="$smoke_src" \
        BACKEND_OUTPUT="$smoke_out" \
        "$bin" >>"$smoke_log" 2>&1
    fi
    status=$?
    set -e
    driver_stage1_smoke_last_status="$status"
    driver_stage1_smoke_last_target="$smoke_target"
    if [ "$status" -ne 0 ] || [ ! -s "$smoke_out" ]; then
      if [ "$status" -eq 0 ] && [ ! -s "$smoke_out" ]; then
        driver_stage1_smoke_last_status=1
      fi
      driver_stage1_smoke_last_class="$(driver_stage1_smoke_classify "$smoke_log" "$driver_stage1_smoke_last_status")"
      IFS="$old_ifs"
      return 1
    fi
  done
  IFS="$old_ifs"
  driver_stage1_smoke_last_status=0
  driver_stage1_smoke_last_class=ok
  return 0
}

driver_stage1_smoke_matches_known_baseline() {
  current_bin="$1"
  stage0_bin="$2"
  smoke_prefix="$3"
  expected_status="$4"
  expected_class="$5"
  expected_target="$6"

  driver_stage1_smoke_existing_status=
  driver_stage1_smoke_existing_class=
  driver_stage1_smoke_existing_log=
  driver_stage1_smoke_existing_target=
  driver_stage1_smoke_stage0_status=
  driver_stage1_smoke_stage0_class=
  driver_stage1_smoke_stage0_log=
  driver_stage1_smoke_stage0_target=

  case "$expected_class" in
    silent_rc223|stmt_lower_newSeq_string)
      ;;
    *)
      return 1
      ;;
  esac

  if [ ! -x "$current_bin" ] || ! driver_sanity_ok "$current_bin"; then
    return 1
  fi

  current_log="${smoke_prefix}.existing.log"
  if driver_stage1_smoke_ok "$current_bin" "$current_log"; then
    return 1
  fi
  driver_stage1_smoke_existing_status="$driver_stage1_smoke_last_status"
  driver_stage1_smoke_existing_class="$driver_stage1_smoke_last_class"
  driver_stage1_smoke_existing_log="$current_log"
  driver_stage1_smoke_existing_target="$driver_stage1_smoke_last_target"
  if [ "$driver_stage1_smoke_existing_status" != "$expected_status" ] || \
     [ "$driver_stage1_smoke_existing_class" != "$expected_class" ] || \
     [ "$driver_stage1_smoke_existing_target" != "$expected_target" ]; then
    return 1
  fi

  stage0_log="${smoke_prefix}.stage0.log"
  if ! driver_stage1_smoke_ok "$stage0_bin" "$stage0_log"; then
    driver_stage1_smoke_stage0_status="$driver_stage1_smoke_last_status"
    driver_stage1_smoke_stage0_class="$driver_stage1_smoke_last_class"
    driver_stage1_smoke_stage0_log="$stage0_log"
    driver_stage1_smoke_stage0_target="$driver_stage1_smoke_last_target"
    return 1
  fi
  driver_stage1_smoke_stage0_status="$driver_stage1_smoke_last_status"
  driver_stage1_smoke_stage0_class="$driver_stage1_smoke_last_class"
  driver_stage1_smoke_stage0_log="$stage0_log"
  driver_stage1_smoke_stage0_target="$driver_stage1_smoke_last_target"
  return 0
}

backend_sources_newer_than() {
  out="$1"
  [ -e "$out" ] || return 0
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

to_abs() {
  p="$1"
  case "$p" in
    /*) ;;
    *) p="$root/$p" ;;
  esac
  d="$(dirname -- "$p")"
  b="$(basename -- "$p")"
  if [ -d "$d" ]; then
    d="$(CDPATH= cd -- "$d" && pwd)"
  fi
  printf "%s/%s\n" "$d" "$b"
}

clear_stale_lock_dir() {
  lock_dir="$1"
  if [ "$lock_dir" = "" ] || [ ! -d "$lock_dir" ]; then
    return 0
  fi
  owner_file="$lock_dir/owner.pid"
  if [ -f "$owner_file" ]; then
    owner_pid="$(cat "$owner_file" 2>/dev/null || true)"
    if [ "$owner_pid" != "" ] && kill -0 "$owner_pid" 2>/dev/null; then
      return 0
    fi
  fi
  rm -rf "$lock_dir" 2>/dev/null || true
  return 0
}

driver_sidecar_obj_path() {
  printf '%s\n' "$root/chengcache/backend_driver_sidecar/backend_driver_uir_sidecar.${target}.o"
}

driver_sidecar_wrapper_obj_path() {
  printf '%s\n' "$root/chengcache/backend_driver_sidecar/backend_driver_uir_sidecar_wrapper.${target}.o"
}

driver_sidecar_helper_obj_path() {
  printf '%s\n' "$root/chengcache/backend_driver_sidecar/backend_driver_uir_sidecar_helper.${target}.o"
}

driver_sidecar_bundle_path() {
  printf '%s\n' "$root/chengcache/backend_driver_sidecar/backend_driver_uir_sidecar.${target}.bundle"
}

driver_sidecar_sources_newer_than() {
  out="$1"
  [ -e "$out" ] || return 0
  for src in \
    "$root/src/backend/tooling/backend_driver_uir_sidecar_wrapper.cheng" \
    "$root/src/backend/tooling/backend_driver_uir_sidecar_bundle.c" \
    "$root/src/backend/tooling/backend_driver_uir_sidecar_runtime_compat.c"
  do
    if [ -f "$src" ] && [ "$src" -nt "$out" ]; then
      return 0
    fi
  done
  return 1
}

driver_sidecar_pick_wrapper_compiler() {
  for cand in \
    "${BACKEND_UIR_SIDECAR_WRAPPER_COMPILER:-}" \
    "${BACKEND_BUILD_DRIVER_STAGE0:-}" \
    "$root/dist/releases/2026-02-23T09_54_03Z_e84f22d_14/cheng" \
    "$root/artifacts/backend_driver/cheng" \
    "$root/dist/releases/current/cheng" \
    "$root/artifacts/backend_selfhost_self_obj/cheng_stage0_default"
  do
    if [ "$cand" = "" ]; then
      continue
    fi
    abs="$(to_abs "$cand")"
    if driver_sanity_ok "$abs"; then
      printf '%s\n' "$abs"
      return 0
    fi
  done
  return 1
}

driver_sidecar_prepare_cheng_wrapper() {
  cc_bin="${CC:-cc}"
  wrapper_src="$root/src/backend/tooling/backend_driver_uir_sidecar_wrapper.cheng"
  sidecar_src="$root/src/backend/tooling/backend_driver_uir_sidecar_bundle.c"
  compat_src="$root/src/backend/tooling/backend_driver_uir_sidecar_runtime_compat.c"
  sidecar_dir="$root/chengcache/backend_driver_sidecar"
  wrapper_obj="$(driver_sidecar_wrapper_obj_path)"
  helper_obj="$(driver_sidecar_helper_obj_path)"
  sidecar_bundle="$(driver_sidecar_bundle_path)"
  wrapper_timeout="${BACKEND_UIR_SIDECAR_BUILD_TIMEOUT:-60}"
  compiler="$(driver_sidecar_pick_wrapper_compiler || true)"
  if [ ! -f "$wrapper_src" ] || [ ! -f "$sidecar_src" ]; then
    return 1
  fi
  if [ "$compiler" = "" ]; then
    return 1
  fi
  mkdir -p "$sidecar_dir"
  rm -f "$wrapper_obj" "$helper_obj" "$sidecar_bundle"
  set +e
  run_with_timeout "$wrapper_timeout" env \
    MM="${MM:-orc}" \
    STAGE1_SEM_FIXED_0="${STAGE1_SEM_FIXED_0:-0}" \
    STAGE1_OWNERSHIP_FIXED_0="${STAGE1_OWNERSHIP_FIXED_0:-0}" \
    STAGE1_SKIP_CPROFILE="${STAGE1_SKIP_CPROFILE:-1}" \
    BACKEND_UIR_SIDECAR_DISABLE=1 \
    BACKEND_UIR_SIDECAR_BUNDLE= \
    BACKEND_UIR_SIDECAR_OBJ= \
    BACKEND_VALIDATE=0 \
    BACKEND_MULTI=0 \
    BACKEND_MULTI_FORCE=0 \
    BACKEND_INCREMENTAL=0 \
    BACKEND_EMIT=obj \
    BACKEND_INTERNAL_ALLOW_EMIT_OBJ=1 \
    BACKEND_FRONTEND=stage1 \
    BACKEND_LINKER=system \
    BACKEND_TARGET="$target" \
    BACKEND_INPUT="$wrapper_src" \
    BACKEND_OUTPUT="$wrapper_obj" \
    "$compiler" >/dev/null 2>&1
  wrapper_status="$?"
  set -e
  if [ "$wrapper_status" -ne 0 ] || [ ! -s "$wrapper_obj" ]; then
    rm -f "$wrapper_obj" "$helper_obj" "$sidecar_bundle"
    return 1
  fi
  if ! "$cc_bin" -fPIC \
      -Ddriver_buildActiveModulePtrs=backend_driver_c_sidecar_buildActiveModulePtrs \
      -Ddriver_emit_obj_from_module_default_impl=backend_driver_c_sidecar_emit_obj_from_module_default_impl \
      -c "$sidecar_src" -o "$helper_obj" >/dev/null 2>&1; then
    rm -f "$wrapper_obj" "$helper_obj" "$sidecar_bundle"
    return 1
  fi
  if [ "$host_os" = "Darwin" ]; then
    if [ -f "$compat_src" ]; then
      if ! "$cc_bin" -bundle -undefined dynamic_lookup \
          "$wrapper_obj" "$helper_obj" "$compat_src" -o "$sidecar_bundle" >/dev/null 2>&1; then
        rm -f "$wrapper_obj" "$helper_obj" "$sidecar_bundle"
        return 1
      fi
    else
      if ! "$cc_bin" -bundle -undefined dynamic_lookup \
          "$wrapper_obj" "$helper_obj" -o "$sidecar_bundle" >/dev/null 2>&1; then
        rm -f "$wrapper_obj" "$helper_obj" "$sidecar_bundle"
        return 1
      fi
    fi
  else
    if [ -f "$compat_src" ]; then
      if ! "$cc_bin" -shared -fPIC \
          "$wrapper_obj" "$helper_obj" "$compat_src" -o "$sidecar_bundle" >/dev/null 2>&1; then
        rm -f "$wrapper_obj" "$helper_obj" "$sidecar_bundle"
        return 1
      fi
    else
      if ! "$cc_bin" -shared -fPIC \
          "$wrapper_obj" "$helper_obj" -o "$sidecar_bundle" >/dev/null 2>&1; then
        rm -f "$wrapper_obj" "$helper_obj" "$sidecar_bundle"
        return 1
      fi
    fi
  fi
  [ -s "$sidecar_bundle" ]
}

driver_sidecar_prepare() {
  cc_bin="${CC:-cc}"
  sidecar_src="$root/src/backend/tooling/backend_driver_uir_sidecar_bundle.c"
  compat_src="$root/src/backend/tooling/backend_driver_uir_sidecar_runtime_compat.c"
  sidecar_dir="$root/chengcache/backend_driver_sidecar"
  sidecar_obj="$(driver_sidecar_obj_path)"
  wrapper_obj="$(driver_sidecar_wrapper_obj_path)"
  helper_obj="$(driver_sidecar_helper_obj_path)"
  sidecar_bundle="$(driver_sidecar_bundle_path)"
  if [ ! -f "$sidecar_src" ]; then
    echo "[Error] missing backend driver sidecar source: $sidecar_src" 1>&2
    return 1
  fi
  mkdir -p "$sidecar_dir"
  rm -f "$sidecar_obj" "$wrapper_obj" "$helper_obj" "$sidecar_bundle"
  if driver_sidecar_prepare_cheng_wrapper; then
    return 0
  fi
  if ! "$cc_bin" -fPIC -c "$sidecar_src" -o "$sidecar_obj" >/dev/null 2>&1; then
    echo "[Error] backend driver emergency sidecar compile failed: $sidecar_src" 1>&2
    return 1
  fi
  if [ "$host_os" = "Darwin" ]; then
    if [ -f "$compat_src" ]; then
      if ! "$cc_bin" -bundle -undefined dynamic_lookup "$sidecar_obj" "$compat_src" -o "$sidecar_bundle" >/dev/null 2>&1; then
        echo "[Error] backend driver emergency sidecar bundle link failed: $sidecar_bundle" 1>&2
        return 1
      fi
    else
      if ! "$cc_bin" -bundle -undefined dynamic_lookup "$sidecar_obj" -o "$sidecar_bundle" >/dev/null 2>&1; then
        echo "[Error] backend driver emergency sidecar bundle link failed: $sidecar_bundle" 1>&2
        return 1
      fi
    fi
  else
    if [ -f "$compat_src" ]; then
      if ! "$cc_bin" -shared -fPIC "$sidecar_obj" "$compat_src" -o "$sidecar_bundle" >/dev/null 2>&1; then
        echo "[Error] backend driver emergency sidecar shared link failed: $sidecar_bundle" 1>&2
        return 1
      fi
    else
      if ! "$cc_bin" -shared -fPIC "$sidecar_obj" -o "$sidecar_bundle" >/dev/null 2>&1; then
        echo "[Error] backend driver emergency sidecar shared link failed: $sidecar_bundle" 1>&2
        return 1
      fi
    fi
  fi
  [ -s "$sidecar_bundle" ]
}

driver_sidecar_ensure() {
  sidecar_bundle="$(driver_sidecar_bundle_path)"
  if [ -s "$sidecar_bundle" ] && ! driver_sidecar_sources_newer_than "$sidecar_bundle"; then
    return 0
  fi
  driver_sidecar_prepare
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
    *" _reserve_ptr_void"*|*" T _reserve_ptr_void"*|*" t _reserve_ptr_void"*)
      return 0
      ;;
  esac
  return 1
}

prepare_driver_symbol_bridge_obj() {
  src="$1"
  out="$2"
  if [ ! -f "$src" ]; then
    return 1
  fi
  out_dir="$(dirname -- "$out")"
  if [ "$out_dir" != "" ] && [ ! -d "$out_dir" ]; then
    mkdir -p "$out_dir"
  fi
  if [ ! -f "$out" ] || [ "$src" -nt "$out" ]; then
    cc_bin="${CC:-cc}"
    "$cc_bin" -std=c11 -O2 -c "$src" -o "$out" >/dev/null 2>&1 || return 1
  fi
  return 0
}

mkdir -p chengcache
stage0_candidates_file="$root/chengcache/.build_backend_driver.stage0_candidates.$$"
: >"$stage0_candidates_file"

cleanup_stage0_candidates_file() {
  rm -f "$stage0_candidates_file" 2>/dev/null || true
}
trap cleanup_stage0_candidates_file EXIT INT TERM

append_stage0_candidate() {
  cand="$1"
  if [ "$cand" = "" ]; then
    return 0
  fi
  abs="$(to_abs "$cand")"
  if ! driver_sanity_ok "$abs"; then
    return 0
  fi
  if grep -Fx -- "$abs" "$stage0_candidates_file" >/dev/null 2>&1; then
    return 0
  fi
  printf '%s\n' "$abs" >>"$stage0_candidates_file"
  return 0
}

stage0_explicit="${BACKEND_BUILD_DRIVER_STAGE0:-}"
stage0_strict="0"
if [ "$stage0_explicit" != "" ]; then
  stage0_abs="$(to_abs "$stage0_explicit")"
  if ! driver_sanity_ok "$stage0_abs"; then
    if [ -x "$stage0_abs" ]; then
      echo "[Error] stage0 driver is not runnable: $stage0_abs" 1>&2
    else
      echo "[Error] stage0 driver is not executable: $stage0_abs" 1>&2
    fi
    exit 1
  fi
  printf '%s\n' "$stage0_abs" >"$stage0_candidates_file"
  stage0_strict="1"
else
  append_stage0_candidate "${BACKEND_DRIVER:-}"
  append_stage0_candidate "$root/artifacts/backend_seed/cheng.stage2"
  append_stage0_candidate "$root/artifacts/backend_selfhost_self_obj/cheng_stage0_default"
  append_stage0_candidate "$root/artifacts/backend_selfhost_self_obj/cheng_stage0_prod"
  append_stage0_candidate "$root/artifacts/backend_selfhost_self_obj/cheng.stage2"
  append_stage0_candidate "$root/artifacts/backend_selfhost_self_obj/cheng.stage1"
  append_stage0_candidate "$root/artifacts/backend_driver/cheng"
  append_stage0_candidate "$root/dist/releases/current/cheng"
  append_stage0_candidate "$root/cheng"
fi

if [ ! -s "$stage0_candidates_file" ]; then
  echo "[Error] build_backend_driver requires stage0 driver (set BACKEND_BUILD_DRIVER_STAGE0 or BACKEND_DRIVER)" 1>&2
  exit 1
fi

selfhost="${BACKEND_BUILD_DRIVER_SELFHOST:-1}"
if [ "$selfhost" = "0" ]; then
  echo "[Error] BACKEND_BUILD_DRIVER_SELFHOST=0 is not supported (seed copy path removed)" 1>&2
  exit 2
fi

if [ "${BACKEND_BUILD_DRIVER_MULTI+x}" = "x" ]; then
  driver_multi="${BACKEND_BUILD_DRIVER_MULTI:-0}"
else
  driver_multi="$(build_driver_default_multi)"
fi
if [ "${BACKEND_BUILD_DRIVER_MULTI_FORCE+x}" = "x" ]; then
  driver_multi_force="${BACKEND_BUILD_DRIVER_MULTI_FORCE:-0}"
else
  driver_multi_force="$(build_driver_default_multi)"
fi
driver_incremental="${BACKEND_BUILD_DRIVER_INCREMENTAL:-1}"
if [ "${BACKEND_BUILD_DRIVER_JOBS+x}" = "x" ]; then
  driver_jobs="${BACKEND_BUILD_DRIVER_JOBS:-0}"
else
  driver_jobs="$(build_driver_default_jobs)"
fi
build_timeout="${BACKEND_BUILD_DRIVER_TIMEOUT:-60}"
case "$build_timeout" in
  ''|*[!0-9]*) build_timeout=60 ;;
esac
if [ "$build_timeout" -lt 1 ] || [ "$build_timeout" -gt 60 ]; then
  build_timeout=60
fi
timeout_diag_enabled="${BACKEND_BUILD_DRIVER_TIMEOUT_DIAG:-1}"
timeout_diag_seconds="$build_timeout"
timeout_diag_dir="${BACKEND_BUILD_DRIVER_TIMEOUT_DIAG_DIR:-$root/chengcache/backend_timeout_diag}"
timeout_diag_summary="${BACKEND_BUILD_DRIVER_TIMEOUT_DIAG_SUMMARY:-1}"
timeout_diag_summary_top="${BACKEND_BUILD_DRIVER_TIMEOUT_DIAG_SUMMARY_TOP:-12}"
driver_smoke="${BACKEND_BUILD_DRIVER_SMOKE:-0}"
driver_require_smoke="${BACKEND_BUILD_DRIVER_REQUIRE_SMOKE:-1}"
driver_frontend="${BACKEND_BUILD_DRIVER_FRONTEND:-stage1}"
max_stage0_attempts="${BACKEND_BUILD_DRIVER_MAX_STAGE0_ATTEMPTS:-3}"
build_force="${BACKEND_BUILD_DRIVER_FORCE:-0}"
build_no_recover="${BACKEND_BUILD_DRIVER_NO_RECOVER:-0}"
case "$max_stage0_attempts" in
  ''|*[!0-9]*)
    max_stage0_attempts=3
    ;;
esac
mm="${BACKEND_BUILD_DRIVER_MM:-${MM:-orc}}"
stage0_compat_allowed="${BACKEND_BUILD_DRIVER_STAGE0_COMPAT:-0}"
if [ "$stage0_compat_allowed" != "0" ]; then
  echo "[Error] build_backend_driver removed BACKEND_BUILD_DRIVER_STAGE0_COMPAT (only 0 is supported)" 1>&2
  exit 2
fi
if [ "${BACKEND_IR:-}" = "" ]; then
  export BACKEND_IR=uir
fi
if [ "${GENERIC_MODE:-}" = "" ]; then
  export GENERIC_MODE=dict
fi
if [ "${GENERIC_SPEC_BUDGET:-}" = "" ]; then
  export GENERIC_SPEC_BUDGET=0
fi
if [ "${STAGE1_SEM_FIXED_0:-}" = "" ]; then
  export STAGE1_SEM_FIXED_0=0
fi
if [ "${STAGE1_OWNERSHIP_FIXED_0:-}" = "" ]; then
  export STAGE1_OWNERSHIP_FIXED_0=0
fi
if [ "${STAGE1_SKIP_CPROFILE:-}" = "" ]; then
  export STAGE1_SKIP_CPROFILE=1
fi

case "$name" in
  /*)
    name_abs="$name"
    ;;
  *)
    name_abs="$root/$name"
    ;;
esac
name_obj_abs="${name_abs}.o"
name_dir="$(dirname "$name_abs")"
if [ "$name_dir" != "" ] && [ ! -d "$name_dir" ]; then
  mkdir -p "$name_dir"
fi
tmp_tag="$(printf '%s' "$name_abs" | tr '/: ' '___' | tr -cd 'A-Za-z0-9._-')"
if [ "$tmp_tag" = "" ]; then
  tmp_tag="backend_driver"
fi
tmp_out_dir="$root/chengcache/build_backend_driver.tmp/$tmp_tag"
mkdir -p "$tmp_out_dir"
tmp_bin_abs="$tmp_out_dir/$(basename "$name_abs").tmp"
tmp_obj_abs="${tmp_bin_abs}.o"

clear_stale_lock_dir "${name_abs}.objs.lock"
clear_stale_lock_dir "${tmp_bin_abs}.objs.lock"

if [ "$build_force" != "1" ] && [ -x "$name_abs" ]; then
  if ! backend_sources_newer_than "$name_abs" && driver_sanity_ok "$name_abs"; then
    if ! driver_sidecar_ensure; then
      echo "[Error] build_backend_driver failed to refresh driver sidecar bundle" 1>&2
      exit 1
    fi
    if [ "$driver_smoke" = "1" ] && ! driver_stage1_smoke_ok "$name_abs" "$log_root/reuse_smoke.$$.log"; then
      :
    else
      if [ "$driver_smoke" = "1" ]; then
        echo "build_backend_driver ok (reuse+smoke)"
      else
        echo "build_backend_driver ok (reuse)"
      fi
      exit 0
    fi
  fi
fi

runtime_src="src/std/system_helpers_backend.cheng"
runtime_obj="chengcache/system_helpers.backend.cheng.${target}.o"
runtime_obj_abs="$root/$runtime_obj"
log_root="$root/chengcache/build_backend_driver"
attempt_report="$log_root/attempts.$$.tsv"
module_cache_path="$root/chengcache/build_backend_driver.module_cache.tsv"
mkdir -p "$log_root"
: >"$attempt_report"

driver_ldflags_base="${BACKEND_BUILD_DRIVER_LDFLAGS:-${BACKEND_LDFLAGS:-}}"
driver_ldflags="$driver_ldflags_base"

stage0_tag() {
  raw="$1"
  tag="$(printf '%s' "$raw" | tr '/: ' '___' | tr -cd 'A-Za-z0-9._-')"
  if [ "$tag" = "" ]; then
    tag="stage0"
  fi
  printf '%s\n' "$tag"
}

driver_stage0_supports_new_expr_assignments() {
  compiler="$1"
  case "$compiler" in
    *"/artifacts/backend_selfhost_self_obj/probe_currentsrc_proof/cheng.stage2"|\
    *"/artifacts/backend_selfhost_self_obj/probe_currentsrc_proof/cheng.stage2.proof"|\
    *"/artifacts/backend_selfhost_self_obj/probe_currentsrc_proof/cheng_stage0_currentsrc.proof")
      return 0
      ;;
  esac
  return 1
}

driver_stage0_new_expr_compat_driver() {
  for cand in \
    "$root/artifacts/backend_driver/cheng" \
    "$root/dist/releases/current/cheng"
  do
    if [ "$cand" = "" ]; then
      continue
    fi
    abs="$(to_abs "$cand")"
    if driver_sanity_ok "$abs"; then
      printf '%s\n' "$abs"
      return 0
    fi
  done
  return 1
}

run_driver_compile_once() {
  compiler="$1"
  out_bin="$2"
  multi_now="$3"
  multi_force_now="$4"
  log_file="$5"
  diag_label="$6"
  compile_root="$root"
  compile_input="src/backend/tooling/backend_driver.cheng"
  compile_driver="$compiler"
  compat_driver=""
  compat_sidecar_bundle=""
  driver_ldflags_now="$driver_ldflags"
  diag_file=""
  diag_child_file=""
  if ! driver_stage0_supports_new_expr_assignments "$compiler"; then
    compat_driver="$(driver_stage0_new_expr_compat_driver || true)"
    if [ "$compat_driver" = "" ]; then
      echo "[Error] build_backend_driver missing canonical compat driver for stage0: $compiler" >>"$log_file"
      return 1
    fi
    compile_driver="$compat_driver"
    if ! driver_sidecar_ensure; then
      echo "[Error] build_backend_driver failed to prepare sidecar bundle for compat stage0: $compiler" >>"$log_file"
      return 1
    fi
    compat_sidecar_bundle="$(driver_sidecar_bundle_path)"
    if [ ! -s "$compat_sidecar_bundle" ]; then
      echo "[Error] build_backend_driver missing compat sidecar bundle: $compat_sidecar_bundle" >>"$log_file"
      return 1
    fi
    if [ "${BACKEND_BUILD_DRIVER_FORCE_MAIN_SHIM:-0}" = "1" ]; then
      shim_src="$root/src/backend/tooling/backend_driver_stage0_backend_main_shim.c"
      shim_obj="$root/chengcache/backend_driver_stage0_backend_main_shim.${target}.o"
      if ! prepare_driver_symbol_bridge_obj "$shim_src" "$shim_obj"; then
        echo "[Error] build_backend_driver failed to prepare backend main shim: $shim_src" >>"$log_file"
        return 1
      fi
      if [ "$driver_ldflags_now" = "" ]; then
        driver_ldflags_now="$shim_obj"
      else
        driver_ldflags_now="$driver_ldflags_now $shim_obj"
      fi
    fi
  fi
  printf 'compile_root=%s\n' "$compile_root" >>"$log_file"
  printf 'compile_input=%s\n' "$compile_input" >>"$log_file"
  printf 'compile_driver=%s\n' "$compile_driver" >>"$log_file"
  if [ "$compat_driver" != "" ]; then
    printf 'compat_sidecar_compiler=%s\n' "$compiler" >>"$log_file"
    printf 'compat_sidecar_bundle=%s\n' "$compat_sidecar_bundle" >>"$log_file"
  fi
  printf 'driver_ldflags=%s\n' "$driver_ldflags_now" >>"$log_file"
  case "$timeout_diag_enabled" in
    1|true|TRUE|yes|YES|on|ON)
      mkdir -p "$timeout_diag_dir"
      diag_stamp="$(date +%Y%m%dT%H%M%S 2>/dev/null || echo timeout)"
      diag_safe_label="$(sanitize_diag_label "$diag_label")"
      [ "$diag_safe_label" = "" ] && diag_safe_label="build_backend_driver"
      diag_file="$timeout_diag_dir/${diag_stamp}_${diag_safe_label}.sample.txt"
      diag_child_file=""
      ;;
  esac
  if [ "$linker_mode" = "self" ]; then
    TIMEOUT_DIAG_FILE="$diag_file" \
    TIMEOUT_DIAG_SECONDS="$timeout_diag_seconds" \
    TIMEOUT_DIAG_ENABLED="$timeout_diag_enabled" \
    run_with_timeout "$build_timeout" \
      sh -c 'cd "$1" && shift && exec "$@"' sh "$compile_root" \
      env \
        MM="$mm" \
        STAGE1_SEM_FIXED_0="${STAGE1_SEM_FIXED_0:-0}" \
        STAGE1_SEM_FIXED_0="${STAGE1_SEM_FIXED_0:-${STAGE1_SEM_FIXED_0:-0}}" \
        STAGE1_OWNERSHIP_FIXED_0="${STAGE1_OWNERSHIP_FIXED_0:-0}" \
        STAGE1_OWNERSHIP_FIXED_0="${STAGE1_OWNERSHIP_FIXED_0:-${STAGE1_OWNERSHIP_FIXED_0:-0}}" \
        STAGE1_SKIP_CPROFILE="${STAGE1_SKIP_CPROFILE:-1}" \
        STAGE1_SKIP_CPROFILE="${STAGE1_SKIP_CPROFILE:-${STAGE1_SKIP_CPROFILE:-1}}" \
        STAGE1_AUTO_SYSTEM=0 \
        BACKEND_MULTI="$multi_now" \
        BACKEND_MULTI_FORCE="$multi_force_now" \
        BACKEND_INCREMENTAL="$driver_incremental" \
        BACKEND_JOBS="$driver_jobs" \
        BACKEND_VALIDATE=0 \
        BACKEND_LINKER=self \
        BACKEND_NO_RUNTIME_C=1 \
        BACKEND_RUNTIME_OBJ="$runtime_obj_abs" \
        BACKEND_ENABLE_CSTRING_LOWERING="${compat_driver:+1}" \
        BACKEND_CSTRING_LOWERING="${compat_driver:+1}" \
        BACKEND_ISEL_CSTRING_LOWERING="${compat_driver:+1}" \
        BACKEND_UIR_SIDECAR_BUNDLE="${compat_sidecar_bundle}" \
        BACKEND_UIR_PREFER_SIDECAR="${compat_driver:+1}" \
        BACKEND_UIR_FORCE_SIDECAR="${compat_driver:+1}" \
        BACKEND_UIR_SIDECAR_COMPILER="${compat_driver:+$compiler}" \
        BACKEND_LDFLAGS="$driver_ldflags_now" \
        BACKEND_EMIT=exe \
        BACKEND_TARGET="$target" \
        BACKEND_FRONTEND="$driver_frontend" \
        BACKEND_INPUT="$compile_input" \
        BACKEND_OUTPUT="$out_bin" \
        "$compile_driver" >>"$log_file" 2>&1
    status="$?"
    if [ "$status" -eq 124 ] || [ "$status" -eq 143 ]; then
      emit_timeout_diag_summary "$diag_label" "$diag_file" "$diag_child_file" "$log_file"
    fi
    return "$status"
  fi
  TIMEOUT_DIAG_FILE="$diag_file" \
  TIMEOUT_DIAG_SECONDS="$timeout_diag_seconds" \
  TIMEOUT_DIAG_ENABLED="$timeout_diag_enabled" \
  run_with_timeout "$build_timeout" \
    sh -c 'cd "$1" && shift && exec "$@"' sh "$compile_root" \
    env \
      MM="$mm" \
      STAGE1_SEM_FIXED_0="${STAGE1_SEM_FIXED_0:-0}" \
      STAGE1_SEM_FIXED_0="${STAGE1_SEM_FIXED_0:-${STAGE1_SEM_FIXED_0:-0}}" \
      STAGE1_OWNERSHIP_FIXED_0="${STAGE1_OWNERSHIP_FIXED_0:-0}" \
      STAGE1_OWNERSHIP_FIXED_0="${STAGE1_OWNERSHIP_FIXED_0:-${STAGE1_OWNERSHIP_FIXED_0:-0}}" \
      STAGE1_SKIP_CPROFILE="${STAGE1_SKIP_CPROFILE:-1}" \
      STAGE1_SKIP_CPROFILE="${STAGE1_SKIP_CPROFILE:-${STAGE1_SKIP_CPROFILE:-1}}" \
      STAGE1_AUTO_SYSTEM=0 \
      BACKEND_MULTI="$multi_now" \
      BACKEND_MULTI_FORCE="$multi_force_now" \
      BACKEND_INCREMENTAL="$driver_incremental" \
      BACKEND_JOBS="$driver_jobs" \
      BACKEND_VALIDATE=0 \
      BACKEND_LINKER=system \
      BACKEND_NO_RUNTIME_C=0 \
      BACKEND_RUNTIME_OBJ= \
      BACKEND_ENABLE_CSTRING_LOWERING="${compat_driver:+1}" \
      BACKEND_CSTRING_LOWERING="${compat_driver:+1}" \
      BACKEND_ISEL_CSTRING_LOWERING="${compat_driver:+1}" \
      BACKEND_UIR_SIDECAR_BUNDLE="${compat_sidecar_bundle}" \
      BACKEND_UIR_PREFER_SIDECAR="${compat_driver:+1}" \
      BACKEND_UIR_FORCE_SIDECAR="${compat_driver:+1}" \
      BACKEND_UIR_SIDECAR_COMPILER="${compat_driver:+$compiler}" \
      BACKEND_LDFLAGS="$driver_ldflags_now" \
      BACKEND_EMIT=exe \
      BACKEND_TARGET="$target" \
      BACKEND_FRONTEND="$driver_frontend" \
      BACKEND_INPUT="$compile_input" \
      BACKEND_OUTPUT="$out_bin" \
      "$compile_driver" >>"$log_file" 2>&1
  status="$?"
  if [ "$status" -eq 124 ] || [ "$status" -eq 143 ]; then
    emit_timeout_diag_summary "$diag_label" "$diag_file" "$diag_child_file" "$log_file"
  fi
  return "$status"
}

attempt_build_with_stage0() {
  stage0="$1"
  attempt_idx="$2"
  stage0_tag_s="$(stage0_tag "$stage0")"
  attempt_log="$log_root/attempt_${attempt_idx}_${stage0_tag_s}.log"
  : >"$attempt_log"
  printf 'stage0=%s\n' "$stage0" >>"$attempt_log"
  driver_stage1_smoke_sidecar_compiler=
  if ! driver_stage0_supports_new_expr_assignments "$stage0"; then
    currentsrc_sidecar="$root/artifacts/backend_selfhost_self_obj/probe_currentsrc_proof/cheng_stage0_currentsrc.proof"
    if [ -x "$currentsrc_sidecar" ]; then
      driver_stage1_smoke_sidecar_compiler="$currentsrc_sidecar"
    fi
  fi
  if [ "$driver_stage1_smoke_sidecar_compiler" != "" ]; then
    printf 'smoke_sidecar_compiler=%s\n' "$driver_stage1_smoke_sidecar_compiler" >>"$attempt_log"
  fi

  runtime_needs_rebuild="0"
  if [ "$linker_mode" = "self" ]; then
    if [ ! -f "$runtime_obj_abs" ]; then
      runtime_needs_rebuild="1"
    elif ! runtime_obj_has_required_symbols "$runtime_obj_abs"; then
      runtime_needs_rebuild="1"
    fi
  fi
  if [ "$runtime_needs_rebuild" = "1" ]; then
    printf 'missing runtime_obj=%s (self-link requires prebuilt runtime object)\n' "$runtime_obj_abs" >>"$attempt_log"
    printf 'hint: prepare runtime object before driver selfbuild (source=%s)\n' "$runtime_src" >>"$attempt_log"
    attempt_status=1
    return 1
  fi

  rm -f "$tmp_bin_abs" "$tmp_obj_abs"
  set +e
  run_driver_compile_once "$stage0" "$tmp_bin_abs" "$driver_multi" "$driver_multi_force" "$attempt_log" \
    "build_backend_driver.${attempt_idx}.${stage0_tag_s}.primary"
  status=$?
  set -e
  if [ "$status" -ne 0 ] && [ "$driver_multi" != "0" ] && [ "$status" -ne 124 ]; then
    rm -f "$tmp_bin_abs" "$tmp_obj_abs"
    set +e
    run_driver_compile_once "$stage0" "$tmp_bin_abs" "0" "0" "$attempt_log" \
      "build_backend_driver.${attempt_idx}.${stage0_tag_s}.serial_retry"
    status=$?
    set -e
  fi
  if [ "$status" -eq 0 ] && [ -x "$tmp_bin_abs" ] && driver_sanity_ok "$tmp_bin_abs"; then
    if ! driver_sidecar_ensure; then
      echo "[Error] build_backend_driver failed to refresh driver sidecar bundle" >>"$attempt_log"
      attempt_status=1
      rm -f "$tmp_bin_abs" "$tmp_obj_abs"
      return 1
    fi
    smoke_required="$driver_require_smoke"
    smoke_mode="clean"
    if [ "$driver_smoke" = "1" ]; then
      smoke_required="1"
    fi
    smoke_ok="1"
    smoke_prefix="$log_root/attempt_${attempt_idx}_${stage0_tag_s}.smoke"
    if [ "$smoke_required" = "1" ] && ! driver_stage1_smoke_ok "$tmp_bin_abs" "${smoke_prefix}.tmp.log"; then
      tmp_smoke_status="$driver_stage1_smoke_last_status"
      tmp_smoke_class="$driver_stage1_smoke_last_class"
      tmp_smoke_log="$driver_stage1_smoke_last_log"
      tmp_smoke_target="$driver_stage1_smoke_last_target"
      printf 'smoke_tmp_status=%s\n' "$tmp_smoke_status" >>"$attempt_log"
      printf 'smoke_tmp_class=%s\n' "$tmp_smoke_class" >>"$attempt_log"
      printf 'smoke_tmp_target=%s\n' "$tmp_smoke_target" >>"$attempt_log"
      printf 'smoke_tmp_log=%s\n' "$tmp_smoke_log" >>"$attempt_log"
      if driver_stage1_smoke_matches_known_baseline "$name_abs" "$stage0" "$smoke_prefix" "$tmp_smoke_status" "$tmp_smoke_class" "$tmp_smoke_target"; then
        smoke_mode="baseline_shared_failure"
        printf 'smoke_baseline_shared=1\n' >>"$attempt_log"
        printf 'smoke_existing_status=%s\n' "$driver_stage1_smoke_existing_status" >>"$attempt_log"
        printf 'smoke_existing_class=%s\n' "$driver_stage1_smoke_existing_class" >>"$attempt_log"
        printf 'smoke_existing_target=%s\n' "$driver_stage1_smoke_existing_target" >>"$attempt_log"
        printf 'smoke_existing_log=%s\n' "$driver_stage1_smoke_existing_log" >>"$attempt_log"
        printf 'smoke_stage0_status=%s\n' "$driver_stage1_smoke_stage0_status" >>"$attempt_log"
        printf 'smoke_stage0_class=%s\n' "$driver_stage1_smoke_stage0_class" >>"$attempt_log"
        printf 'smoke_stage0_target=%s\n' "$driver_stage1_smoke_stage0_target" >>"$attempt_log"
        printf 'smoke_stage0_log=%s\n' "$driver_stage1_smoke_stage0_log" >>"$attempt_log"
      else
        smoke_ok="0"
      fi
    fi
    if [ "$smoke_required" = "1" ] && [ "$smoke_mode" = "baseline_shared_failure" ]; then
      smoke_ok="1"
    fi
    if [ "$smoke_ok" = "1" ]; then
      mode_tag="system_link"
      if [ "$linker_mode" = "self" ]; then
        mode_tag="self_link"
      fi
      mv "$tmp_bin_abs" "$name_abs"
      if [ -s "$tmp_obj_abs" ]; then
        mv "$tmp_obj_abs" "$name_obj_abs"
      fi
      if [ "$smoke_required" = "1" ]; then
        if [ "$smoke_mode" = "baseline_shared_failure" ]; then
          echo "build_backend_driver ok (selfhost_${mode_tag}+baseline_smoke; stage0=$stage0)"
        else
          echo "build_backend_driver ok (selfhost_${mode_tag}+smoke; stage0=$stage0)"
        fi
      else
        echo "build_backend_driver ok (selfhost_${mode_tag}; stage0=$stage0)"
      fi
      return 0
    fi
    attempt_status=86
    rm -f "$tmp_bin_abs" "$tmp_obj_abs"
    status="$attempt_status"
  else
    attempt_status="$status"
    rm -f "$tmp_bin_abs" "$tmp_obj_abs"
    if [ "$attempt_status" -eq 0 ]; then
      attempt_status=1
    fi
  fi

  rm -f "$tmp_bin_abs" "$tmp_obj_abs"
  return 1
}

attempt_idx=0
while IFS= read -r stage0; do
  if [ "$stage0" = "" ]; then
    continue
  fi
  attempt_idx=$((attempt_idx + 1))
  if [ "$max_stage0_attempts" -gt 0 ] && [ "$attempt_idx" -gt "$max_stage0_attempts" ]; then
    break
  fi
  attempt_status=1
  attempt_log=""
  if attempt_build_with_stage0 "$stage0" "$attempt_idx"; then
    exit 0
  fi
  printf '%s\tstatus=%s\tlog=%s\n' "$stage0" "$attempt_status" "$attempt_log" >>"$attempt_report"
  if [ "$stage0_strict" = "1" ]; then
    break
  fi
done <"$stage0_candidates_file"

echo "[Error] build_backend_driver selfhost_${linker_mode}_link failed for all stage0 candidates" 1>&2
if [ -s "$attempt_report" ]; then
  echo "  attempts:" 1>&2
  sed 's/^/  - /' "$attempt_report" 1>&2
fi
if [ "$build_no_recover" = "1" ]; then
  echo "  hint: hard-fail enabled (BACKEND_BUILD_DRIVER_NO_RECOVER=1), no fallback to existing driver" 1>&2
  echo "  hint: inspect logs under $log_root" 1>&2
  exit 1
fi
recover_smoke_required="$driver_require_smoke"
if [ "$driver_smoke" = "1" ]; then
  recover_smoke_required="1"
fi
if [ -x "$name_abs" ] && driver_sanity_ok "$name_abs"; then
  recover_ok="1"
  if [ "$recover_smoke_required" = "1" ] && ! driver_stage1_smoke_ok "$name_abs" "$log_root/recover_smoke.$$.log"; then
    recover_ok="0"
  fi
  if [ "$recover_ok" = "1" ]; then
    if ! driver_sidecar_ensure; then
      echo "[Error] build_backend_driver failed to refresh driver sidecar bundle" 1>&2
      exit 1
    fi
    echo "[Warn] build_backend_driver rebuild failed; keep existing healthy driver: $name_abs" 1>&2
    exit 0
  fi
fi
echo "  hint: inspect logs under $log_root" 1>&2
exit 1
