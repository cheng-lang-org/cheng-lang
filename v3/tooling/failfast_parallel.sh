#!/usr/bin/env sh

v3_parallel_jobs=""
v3_parallel_trap_installed=0

v3_parallel_reset() {
  v3_parallel_jobs=""
  if [ "$v3_parallel_trap_installed" = "0" ]; then
    trap 'v3_parallel_kill_all' EXIT INT TERM HUP
    v3_parallel_trap_installed=1
  fi
}

v3_parallel_start() {
  name="$1"
  log="$2"
  shift 2
  if [ -n "$v3_parallel_jobs" ]; then
    : > "$log"
    "$@" >"$log" 2>&1 &
    pid=$!
    v3_parallel_jobs="$v3_parallel_jobs
$pid:$name:$log"
  else
    : > "$log"
    "$@" >"$log" 2>&1 &
    pid=$!
    v3_parallel_jobs="$pid:$name:$log"
  fi
}

v3_parallel_kill_all() {
  old_ifs=$IFS
  IFS='
'
  for job in $v3_parallel_jobs
  do
    [ -n "$job" ] || continue
    pid=${job%%:*}
    kill "$pid" 2>/dev/null || true
  done
  IFS=$old_ifs
  wait 2>/dev/null || true
}

v3_parallel_wait_failfast() {
  while :
  do
    old_ifs=$IFS
    IFS='
'
    remaining=""
    active=0
    for job in $v3_parallel_jobs
    do
      [ -n "$job" ] || continue
      pid=${job%%:*}
      rest=${job#*:}
      name=${rest%%:*}
      log=${rest#*:}
      if kill -0 "$pid" 2>/dev/null; then
        if [ -n "$remaining" ]; then
          remaining="$remaining
$job"
        else
          remaining="$job"
        fi
        active=1
        continue
      fi
      if wait "$pid"; then
        :
      else
        IFS=$old_ifs
        echo "v3 gate: $name failed" >&2
        tail -n 80 "$log" >&2 || true
        v3_parallel_jobs="$remaining"
        v3_parallel_kill_all
        return 1
      fi
    done
    IFS=$old_ifs
    v3_parallel_jobs="$remaining"
    if [ "$active" -eq 0 ]; then
      break
    fi
    sleep 1
  done
  if [ "$v3_parallel_trap_installed" = "1" ]; then
    trap - EXIT INT TERM HUP
    v3_parallel_trap_installed=0
  fi
  return 0
}
