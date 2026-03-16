#!/usr/bin/env sh
:
set -eu
(set -o pipefail) 2>/dev/null && set -o pipefail

usage() {
  cat <<'EOF'
Usage:
  ${TOOLING_SELF_BIN:-artifacts/tooling_cmd/cheng_tooling} chengc_daemon [status|start|stop|serve]
  ${TOOLING_SELF_BIN:-artifacts/tooling_cmd/cheng_tooling} chengc_daemon run -- <chengc args...>

Notes:
  - `run` enqueues one `chengc` request to a local daemon worker and waits for completion.
  - Enable automatic routing from `chengc` via `CHENGC_DAEMON=1`.
  - Daemon state dir default: `chengcache/chengc_daemon`.
EOF
}

root="$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)"
cd "$root"

daemon_dir="${CHENGC_DAEMON_DIR:-$root/chengcache/chengc_daemon}"
pid_file="$daemon_dir/daemon.pid"
log_file="$daemon_dir/daemon.log"
queue_dir="$daemon_dir/queue"
poll_interval="${CHENGC_DAEMON_POLL_INTERVAL:-0.05}"
wait_timeout="${CHENGC_DAEMON_WAIT_TIMEOUT:-60}"

quote_sh() {
  text="$1"
  esc="$(printf '%s' "$text" | sed "s/'/'\"'\"'/g")"
  printf "'%s'" "$esc"
}

pid_alive() {
  pid="$1"
  [ "$pid" != "" ] || return 1
  case "$pid" in
    *[!0-9]*)
      return 1
      ;;
  esac
  kill -0 "$pid" 2>/dev/null
}

read_pid() {
  if [ ! -f "$pid_file" ]; then
    printf '%s\n' ""
    return
  fi
  cat "$pid_file" 2>/dev/null || true
}

start_daemon() {
  mkdir -p "$queue_dir"
  old_pid="$(read_pid)"
  if pid_alive "$old_pid"; then
    printf '[chengc_daemon] already running pid=%s\n' "$old_pid"
    return 0
  fi
  rm -f "$pid_file"
  nohup ${TOOLING_SELF_BIN:-artifacts/tooling_cmd/cheng_tooling} chengc_daemon serve >>"$log_file" 2>&1 &
  daemon_pid="$!"
  printf '%s\n' "$daemon_pid" >"$pid_file"
  waits=0
  while [ "$waits" -lt 40 ]; do
    if pid_alive "$daemon_pid"; then
      printf '[chengc_daemon] started pid=%s\n' "$daemon_pid"
      return 0
    fi
    waits=$((waits + 1))
    sleep 0.05
  done
  echo "[chengc_daemon] failed to start daemon worker" 1>&2
  return 1
}

stop_daemon() {
  pid="$(read_pid)"
  if ! pid_alive "$pid"; then
    rm -f "$pid_file"
    printf '[chengc_daemon] not running\n'
    return 0
  fi
  kill "$pid" 2>/dev/null || true
  waits=0
  while pid_alive "$pid"; do
    waits=$((waits + 1))
    if [ "$waits" -gt 40 ]; then
      kill -9 "$pid" 2>/dev/null || true
      break
    fi
    sleep 0.05
  done
  rm -f "$pid_file"
  printf '[chengc_daemon] stopped pid=%s\n' "$pid"
}

status_daemon() {
  pid="$(read_pid)"
  if pid_alive "$pid"; then
    printf 'status=running\npid=%s\nlog=%s\nqueue=%s\n' "$pid" "$log_file" "$queue_dir"
    return 0
  fi
  printf 'status=stopped\nlog=%s\nqueue=%s\n' "$log_file" "$queue_dir"
  return 1
}

serve_loop() {
  mkdir -p "$queue_dir"
  printf '[chengc_daemon] serve loop started pid=%s dir=%s\n' "$$" "$daemon_dir"
  while true; do
    processed="0"
    for pending in "$queue_dir"/*/pending; do
      [ -e "$pending" ] || continue
      req_dir="$(dirname "$pending")"
      if ! mv "$pending" "$req_dir/working" 2>/dev/null; then
        continue
      fi
      processed="1"
      req_script="$req_dir/request.sh"
      out_file="$req_dir/stdout.log"
      err_file="$req_dir/stderr.log"
      rc_file="$req_dir/rc"
      done_file="$req_dir/done"
      rm -f "$done_file" "$rc_file"
      if [ ! -x "$req_script" ]; then
        printf '[chengc_daemon] missing request script: %s\n' "$req_script" >"$err_file"
        printf '%s\n' "127" >"$rc_file"
      else
        set +e
        "$req_script" >"$out_file" 2>"$err_file"
        req_rc="$?"
        set -e
        printf '%s\n' "$req_rc" >"$rc_file"
      fi
      touch "$done_file"
      rm -f "$req_dir/working"
    done
    if [ "$processed" = "0" ]; then
      sleep "$poll_interval"
    fi
  done
}

run_request() {
  if [ "$#" -le 0 ]; then
    echo "[chengc_daemon] missing chengc args for run" 1>&2
    exit 2
  fi
  start_daemon
  mkdir -p "$queue_dir"
  req_id="$(date +%s%N 2>/dev/null || date +%s).$$"
  req_dir="$queue_dir/$req_id"
  mkdir -p "$req_dir"
  req_script="$req_dir/request.sh"
  out_file="$req_dir/stdout.log"
  err_file="$req_dir/stderr.log"
  rc_file="$req_dir/rc"
  done_file="$req_dir/done"
  {
    echo '#!/usr/bin/env sh'
    echo 'set -eu'
    printf 'cd %s\n' "$(quote_sh "$root")"
    printf 'set --'
    for arg in "$@"; do
      printf ' %s' "$(quote_sh "$arg")"
    done
    printf '\n'
    echo 'exec env CHENGC_DAEMON_ACTIVE=1 ${TOOLING_SELF_BIN:-artifacts/tooling_cmd/cheng_tooling} chengc "$@"'
  } >"$req_script"
  chmod +x "$req_script"
  : >"$req_dir/pending"

  deadline="$(( $(date +%s) + wait_timeout ))"
  while [ ! -f "$done_file" ]; do
    now="$(date +%s)"
    if [ "$now" -ge "$deadline" ]; then
      echo "[chengc_daemon] request timeout (${wait_timeout}s): $req_id" 1>&2
      exit 124
    fi
    sleep 0.05
  done

  if [ -f "$out_file" ]; then
    cat "$out_file"
  fi
  if [ -f "$err_file" ]; then
    cat "$err_file" 1>&2
  fi
  req_rc="1"
  if [ -f "$rc_file" ]; then
    req_rc="$(cat "$rc_file" 2>/dev/null || echo 1)"
  fi
  if [ "${CHENGC_DAEMON_KEEP_REQUESTS:-0}" != "1" ]; then
    rm -rf "$req_dir"
  fi
  exit "$req_rc"
}

cmd="${1:-status}"
case "$cmd" in
  --help|-h|help)
    usage
    exit 0
    ;;
  start)
    start_daemon
    exit 0
    ;;
  stop)
    stop_daemon
    exit 0
    ;;
  status)
    status_daemon
    exit $?
    ;;
  serve)
    serve_loop
    exit 0
    ;;
  run)
    shift || true
    if [ "${1:-}" = "--" ]; then
      shift || true
    fi
    run_request "$@"
    ;;
  *)
    echo "[chengc_daemon] unknown command: $cmd" 1>&2
    usage 1>&2
    exit 2
    ;;
esac
