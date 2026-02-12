#!/usr/bin/env sh
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
  if [ ! -f "$smoke_src" ]; then
    return 0
  fi
  smoke_target="$(sh "$root/src/tooling/detect_host_target.sh" 2>/dev/null || echo "")"
  if [ "$smoke_target" = "" ]; then
    return 0
  fi
  smoke_out="$root/chengcache/.backend_driver_path.smoke.o"
  smoke_out_stage1="$root/chengcache/.backend_driver_path.smoke.stage1.o"
  smoke_stage1="${CHENG_BACKEND_DRIVER_PATH_STAGE1_SMOKE:-1}"
  mkdir -p "$root/chengcache"
  rm -f "$smoke_out"
  rm -f "$smoke_out_stage1"
  set +e
  run_with_timeout 10 env \
    CHENG_C_SYSTEM=system \
    CHENG_BACKEND_VALIDATE=1 \
    CHENG_BACKEND_EMIT=obj \
    CHENG_BACKEND_TARGET="$smoke_target" \
    CHENG_BACKEND_FRONTEND=mvp \
    CHENG_BACKEND_INPUT="$smoke_src" \
    CHENG_BACKEND_OUTPUT="$smoke_out" \
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
    run_with_timeout 10 env \
      CHENG_MM=orc \
      CHENG_C_SYSTEM=system \
      CHENG_BACKEND_VALIDATE=1 \
      CHENG_STAGE1_SKIP_SEM=0 \
      CHENG_STAGE1_SKIP_MONO=1 \
      CHENG_STAGE1_SKIP_OWNERSHIP=1 \
      CHENG_BACKEND_EMIT=obj \
      CHENG_BACKEND_TARGET=auto \
      CHENG_BACKEND_FRONTEND=stage1 \
      CHENG_BACKEND_INPUT="$smoke_src" \
      CHENG_BACKEND_OUTPUT="$smoke_out_stage1" \
      "$bin" >/dev/null 2>&1
    status_stage1=$?
    set -e
    if [ "$status_stage1" -ne 0 ]; then
      return 1
    fi
    [ -s "$smoke_out_stage1" ] || return 1
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

driver="${CHENG_BACKEND_DRIVER:-}"
if [ "$driver" != "" ]; then
  abs="$(to_abs "$driver")"
  if [ ! -x "$abs" ]; then
    echo "[Error] CHENG_BACKEND_DRIVER is not executable: $abs" 1>&2
    exit 1
  fi
  if ! driver_sanity_ok "$abs" || ! driver_compile_smoke_ok "$abs"; then
    echo "[Error] CHENG_BACKEND_DRIVER is not runnable: $abs" 1>&2
    exit 1
  fi
  printf "%s\n" "$abs"
  exit 0
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

abs_default="$root/cheng"

# Fast path: use the local rebuilt driver if it's fresh.
if [ -x "$abs_default" ] && ! is_stale "$abs_default"; then
  if driver_sanity_ok "$abs_default" && driver_compile_smoke_ok "$abs_default"; then
    printf "%s\n" "$abs_default"
    exit 0
  fi
  echo "[Warn] backend_driver_path: local cheng is not runnable, rebuilding" 1>&2
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
  rebuild_log="$root/chengcache/backend_driver_path.rebuild.log"
  set +e
  CHENG_BACKEND_BUILD_DRIVER_SELFHOST=1 \
  CHENG_BACKEND_BUILD_DRIVER_LINKER="${CHENG_BACKEND_DRIVER_PATH_LINKER:-self}" \
    sh src/tooling/build_backend_driver.sh --name:cheng >"$rebuild_log" 2>&1
  rebuild_status="$?"
  set -e
  if [ "$rebuild_status" -ne 0 ]; then
    echo "[Warn] backend_driver_path: local cheng rebuild failed (status=$rebuild_status)" 1>&2
    if [ "${CHENG_BACKEND_DRIVER_PATH_DEBUG:-0}" = "1" ]; then
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

for cand in \
  "$root/artifacts/backend_seed/cheng.stage2" \
  "$root/artifacts/backend_selfhost_self_obj/cheng.stage2" \
  "$root/artifacts/backend_selfhost_self_obj/cheng.stage1"; do
  if [ -x "$cand" ] && driver_sanity_ok "$cand" && driver_compile_smoke_ok "$cand"; then
    printf "%s\n" "$cand"
    exit 0
  fi
done

if [ ! -x "$abs_default" ]; then
  echo "[Error] missing backend driver: $abs_default" 1>&2
else
  echo "[Error] backend driver exists but failed compile-smoke: $abs_default" 1>&2
fi
echo "  hint: run src/tooling/build_backend_driver.sh --name:cheng (selfhost only)" 1>&2
exit 1
