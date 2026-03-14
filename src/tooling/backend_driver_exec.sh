#!/usr/bin/env sh
:
set -eu
(set -o pipefail) 2>/dev/null && set -o pipefail

root="$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)"
cd "$root"

driver_real="${BACKEND_DRIVER_REAL:-${BACKEND_DRIVER:-$root/artifacts/backend_driver/cheng}}"
case "$driver_real" in
  /*) ;;
  *) driver_real="$root/$driver_real" ;;
esac

if [ ! -x "$driver_real" ]; then
  echo "[backend_driver_exec] backend driver not executable: $driver_real" >&2
  exit 2
fi

tool=""
if [ -x "$root/artifacts/tooling_cmd/cheng_tooling.realbin" ]; then
  tool="$root/artifacts/tooling_cmd/cheng_tooling.realbin"
elif [ -x "$root/artifacts/tooling_cmd/cheng_tooling" ]; then
  tool="$root/artifacts/tooling_cmd/cheng_tooling"
fi

qdir="${CHENG_STAGE0_QUARANTINE_DIR:-$root/chengcache/stage0_quarantine}"
lock_dir="$qdir/stage0.lock"
mkdir -p "$qdir"

timeout_s="${BACKEND_DRIVER_EXEC_TIMEOUT:-${TOOLING_COMPILE_TIMEOUT:-180}}"
case "$timeout_s" in
  ''|*[!0-9]*) timeout_s=180 ;;
esac
if [ "$timeout_s" -lt 1 ]; then
  timeout_s=180
fi

wait_sec="${BACKEND_DRIVER_EXEC_LOCK_WAIT_SEC:-$timeout_s}"
case "$wait_sec" in
  ''|*[!0-9]*) wait_sec="$timeout_s" ;;
esac
if [ "$wait_sec" -lt 0 ]; then
  wait_sec="$timeout_s"
fi

stale_sec="${BACKEND_DRIVER_EXEC_LOCK_STALE_SEC:-600}"
case "$stale_sec" in
  ''|*[!0-9]*) stale_sec=600 ;;
esac
if [ "$stale_sec" -lt 0 ]; then
  stale_sec=0
fi

ue_status_count() {
  if [ -z "$tool" ]; then
    printf '0\n'
    return 0
  fi
  "$tool" stage0-ue-status --all 2>/dev/null | awk -F= '
    /^stage0_ue_count=/ { print $2; found=1 }
    END { if (!found) print "0" }'
}

ue_block_if_dirty() {
  rechecks="${BACKEND_DRIVER_EXEC_UE_RECHECKS:-3}"
  case "$rechecks" in
    ''|*[!0-9]*) rechecks=3 ;;
  esac
  if [ "$rechecks" -lt 0 ]; then
    rechecks=0
  fi
  settle_ms="${BACKEND_DRIVER_EXEC_UE_SETTLE_MS:-250}"
  case "$settle_ms" in
    ''|*[!0-9]*) settle_ms=250 ;;
  esac
  if [ "$settle_ms" -lt 0 ]; then
    settle_ms=0
  fi
  attempt=0
  while :; do
    ue_count="$(ue_status_count || true)"
    case "$ue_count" in
      ''|*[!0-9]*) ue_count=0 ;;
    esac
    if [ "$ue_count" -le 0 ]; then
      return 0
    fi
    if [ "$attempt" -ge "$rechecks" ]; then
      break
    fi
    ue_cleanup_best_effort
    if [ "$settle_ms" -gt 0 ]; then
      perl -e 'select undef, undef, undef, shift' "$(awk "BEGIN { printf \"%.3f\", $settle_ms / 1000 }")" 2>/dev/null || sleep 1
    fi
    attempt=$((attempt + 1))
  done
  echo "[backend_driver_exec] blocked by existing stage0 UE count=$ue_count" >&2
  if [ -n "$tool" ]; then
    "$tool" stage0-ue-status --all >&2 || true
  fi
  return 1
}

ue_cleanup_best_effort() {
  if [ -n "$tool" ]; then
    "$tool" stage0-ue-clean --global --strict:0 >&2 || true
  fi
}

lock_start="$(date +%s 2>/dev/null || echo 0)"
while ! mkdir "$lock_dir" 2>/dev/null; do
  owner_pid=""
  owner_pgid=""
  lock_ts=""
  now="$(date +%s 2>/dev/null || echo 0)"
  if [ -f "$lock_dir/owner.pid" ]; then
    owner_pid="$(cat "$lock_dir/owner.pid" 2>/dev/null || true)"
  fi
  if [ -f "$lock_dir/owner.pgid" ]; then
    owner_pgid="$(cat "$lock_dir/owner.pgid" 2>/dev/null || true)"
  fi
  if [ -f "$lock_dir/since.epoch" ]; then
    lock_ts="$(cat "$lock_dir/since.epoch" 2>/dev/null || true)"
  fi
  case "$owner_pid" in
    ''|*[!0-9]*) owner_pid="" ;;
  esac
  case "$owner_pgid" in
    ''|*[!0-9]*) owner_pgid="" ;;
  esac
  case "$lock_ts" in
    ''|*[!0-9]*) lock_ts="" ;;
  esac
  owner_alive=0
  if [ -n "$owner_pid" ] && kill -0 "$owner_pid" 2>/dev/null; then
    owner_alive=1
  fi
  if [ "$owner_alive" -eq 0 ]; then
    rm -rf "$lock_dir" 2>/dev/null || true
    continue
  fi
  if [ -n "$lock_ts" ] && [ "$stale_sec" -gt 0 ] && [ "$now" -gt 0 ] &&
     [ $((now - lock_ts)) -ge "$stale_sec" ]; then
    if [ -n "$owner_pgid" ]; then
      kill -TERM -"$owner_pgid" >/dev/null 2>&1 || true
      sleep 0.2
      kill -KILL -"$owner_pgid" >/dev/null 2>&1 || true
    fi
    if [ -n "$owner_pid" ]; then
      kill -TERM "$owner_pid" >/dev/null 2>&1 || true
      sleep 0.2
      kill -KILL "$owner_pid" >/dev/null 2>&1 || true
    fi
    rm -rf "$lock_dir" 2>/dev/null || true
    ue_cleanup_best_effort
    continue
  fi
  if [ "$wait_sec" -gt 0 ] && [ "$now" -gt 0 ] && [ "$lock_start" -gt 0 ] &&
     [ $((now - lock_start)) -ge "$wait_sec" ]; then
    echo "[backend_driver_exec] stage0 lock timeout: $lock_dir" >&2
    exit 125
  fi
  sleep 0.1
done

mirror_path="$qdir/cheng.exec.$$"
trap 'rm -rf "$lock_dir" >/dev/null 2>&1 || true; rm -f "$mirror_path" >/dev/null 2>&1 || true' EXIT INT TERM
echo $$ > "$lock_dir/owner.pid"
owner_pgid="$(ps -o pgid= -p $$ 2>/dev/null | tr -d "[:space:]")"
case "$owner_pgid" in
  ''|*[!0-9]*) owner_pgid=$$ ;;
esac
echo "$owner_pgid" > "$lock_dir/owner.pgid" 2>/dev/null || true
date +%s > "$lock_dir/since.epoch" 2>/dev/null || true

if ! ue_block_if_dirty; then
  exit 125
fi

tmp_mirror="${mirror_path}.tmp"
attempts="${BACKEND_DRIVER_EXEC_ATTEMPTS:-2}"
case "$attempts" in
  ''|*[!0-9]*) attempts=2 ;;
esac
if [ "$attempts" -lt 1 ]; then
  attempts=1
fi

attempt=1
rc=0
while :; do
  rm -f "$tmp_mirror" "$mirror_path"
  cp "$driver_real" "$tmp_mirror"
  chmod 755 "$tmp_mirror"
  mv -f "$tmp_mirror" "$mirror_path"

  set +e
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
    }' "$timeout_s" "$mirror_path" "$@"
  rc="$?"
  set -e

  if [ "$rc" -ne 0 ]; then
    ue_cleanup_best_effort
    if [ "$attempt" -lt "$attempts" ]; then
      attempt=$((attempt + 1))
      echo "[backend_driver_exec] retry rc=$rc attempt=$attempt/$attempts" >&2
      sleep 1
      continue
    fi
  fi
  break
done

exit "$rc"
