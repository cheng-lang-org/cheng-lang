#!/usr/bin/env sh
. "$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)/env_prefix_bridge.sh"
set -euo pipefail

usage() {
  cat <<'EOF'
Usage:
  src/tooling/verify_stage1_fullspec.sh [--file:<path>] [--name:<bin>] [--jobs:<N>]
                                        [--backend:<obj>] [--frontend:<stage1>]
                                        [--target:<triple>] [--linker:<self|system|auto>]
                                        [--mm:<orc>] [--timeout:<sec>] [--multi:<0|1>]
                                        [--validate:<0|1>]
                                        [--reuse:<0|1>]
                                        [--skip-run] [--allow-skip]
                                        [--log:<path>]

Notes:
  - Builds and runs the stage1 fullspec sample (expects output: "fullspec ok").
  - Backend obj path supports `self`/`system` linker modes (`auto` defaults to host-compatible mode).
EOF
}

file="examples/stage1_codegen_fullspec.cheng"
name=""
jobs=""
mm=""
skip_run=""
allow_skip=""
backend="${STAGE1_FULLSPEC_BACKEND:-obj}"
frontend="${STAGE1_FULLSPEC_FRONTEND:-stage1}"
target="${STAGE1_FULLSPEC_TARGET:-}"
linker="${STAGE1_FULLSPEC_LINKER:-}"
# Stage1 fullspec compiles the full semantic pipeline and can be much slower
# than smoke fixtures on cold caches.
timeout_s="${STAGE1_FULLSPEC_TIMEOUT:-60}"
multi="${STAGE1_FULLSPEC_MULTI:-1}"
validate="${STAGE1_FULLSPEC_VALIDATE:-0}"
reuse="${STAGE1_FULLSPEC_REUSE:-1}"
timeout_diag="${STAGE1_FULLSPEC_TIMEOUT_DIAG:-1}"
timeout_diag_timeout="${STAGE1_FULLSPEC_TIMEOUT_DIAG_TIMEOUT:-20}"
timeout_diag_dir="${STAGE1_FULLSPEC_TIMEOUT_DIAG_DIR:-chengcache/stage1_fullspec_timeout_diag}"
timeout_health_probe="${STAGE1_FULLSPEC_TIMEOUT_HEALTH_PROBE:-1}"
timeout_health_timeout="${STAGE1_FULLSPEC_TIMEOUT_HEALTH_TIMEOUT:-10}"
timeout_health_fixture="${STAGE1_FULLSPEC_TIMEOUT_HEALTH_FIXTURE:-tests/cheng/backend/fixtures/return_add.cheng}"
stage1_diag_health_status="unknown"
stage1_diag_health_log=""
log="chengcache/stage1_fullspec.out"

while [ "${1:-}" != "" ]; do
  case "$1" in
    --file:*)
      file="${1#--file:}"
      ;;
    --name:*)
      name="${1#--name:}"
      ;;
    --jobs:*)
      jobs="${1#--jobs:}"
      ;;
    --backend:*)
      backend="${1#--backend:}"
      ;;
    --frontend:*)
      frontend="${1#--frontend:}"
      ;;
    --target:*)
      target="${1#--target:}"
      ;;
    --linker:*)
      linker="${1#--linker:}"
      ;;
    --mm:*)
      mm="${1#--mm:}"
      ;;
    --timeout:*)
      timeout_s="${1#--timeout:}"
      ;;
    --multi:*)
      multi="${1#--multi:}"
      ;;
    --validate:*)
      validate="${1#--validate:}"
      ;;
    --reuse:*)
      reuse="${1#--reuse:}"
      ;;
    --skip-run)
      skip_run="1"
      ;;
    --allow-skip)
      allow_skip="1"
      ;;
    --log:*)
      log="${1#--log:}"
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

root="$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)"
cd "$root"

case "$file" in
  /*) ;;
  *) file="$root/$file" ;;
esac

if [ ! -f "$file" ]; then
  echo "[Error] missing fullspec file: $file" 1>&2
  exit 2
fi

if [ "$backend" != "obj" ]; then
  echo "[Error] invalid --backend:$backend (only obj is supported)" 1>&2
  exit 2
fi
case "$frontend" in
  stage1)
    ;;
  *)
    echo "[Error] invalid --frontend:$frontend (expected stage1)" 1>&2
    exit 2
    ;;
esac

if [ "$name" = "" ]; then
  base="$(basename "$file")"
  name="${base%.cheng}"
fi
output_bin="$root/$name"
stamp_file="$root/$name.stage1_fullspec.stamp"

if [ "$mm" = "" ]; then
  mm="${MM:-orc}"
fi
if [ "$mm" != "orc" ]; then
  echo "[Error] invalid --mm:$mm (only orc is supported)" 1>&2
  exit 2
fi
envs="MM=orc"
stage1_skip_sem="${STAGE1_SKIP_SEM:-0}"
stage1_skip_ownership="${STAGE1_SKIP_OWNERSHIP:-1}"
envs="$envs STAGE1_SKIP_SEM=$stage1_skip_sem STAGE1_SKIP_OWNERSHIP=$stage1_skip_ownership"

if [ "$jobs" = "" ]; then
  jobs="${STAGE1_FULLSPEC_JOBS:-}"
fi

if [ "$multi" != "0" ]; then
  multi="1"
fi

case "$validate" in
  0|1)
    ;;
  *)
    echo "[Error] invalid --validate:$validate (expected 0|1)" 1>&2
    exit 2
    ;;
esac
case "$reuse" in
  0|1)
    ;;
  *)
    echo "[Error] invalid --reuse:$reuse (expected 0|1)" 1>&2
    exit 2
    ;;
esac
case "$timeout_diag" in
  0|1)
    ;;
  *)
    echo "[Error] invalid STAGE1_FULLSPEC_TIMEOUT_DIAG:$timeout_diag (expected 0|1)" 1>&2
    exit 2
    ;;
esac
case "$timeout_diag_timeout" in
  ''|*[!0-9]*)
    timeout_diag_timeout=20
    ;;
  *)
    if [ "$timeout_diag_timeout" -le 0 ]; then
      timeout_diag_timeout=20
    fi
    ;;
esac
case "$timeout_health_probe" in
  0|1)
    ;;
  *)
    echo "[Error] invalid STAGE1_FULLSPEC_TIMEOUT_HEALTH_PROBE:$timeout_health_probe (expected 0|1)" 1>&2
    exit 2
    ;;
esac
case "$timeout_health_timeout" in
  ''|*[!0-9]*)
    timeout_health_timeout=10
    ;;
  *)
    if [ "$timeout_health_timeout" -le 0 ]; then
      timeout_health_timeout=10
    fi
    ;;
esac
multi_force="${STAGE1_FULLSPEC_MULTI_FORCE:-$multi}"
jobs_env=""
if [ "$jobs" != "" ]; then
  jobs_env="BACKEND_JOBS=$jobs"
fi

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

run_cmd() {
  cmd_label="$1"
  shift
  echo "== $cmd_label =="
  tmp_log="$(mktemp "${TMPDIR:-/tmp}/stage1_fullspec_cmd.XXXXXX.log")"
  set +e
  run_with_timeout "$timeout_s" "$@" >"$tmp_log" 2>&1
  status=$?
  set -e
  cat "$tmp_log"
  rm -f "$tmp_log"
  if [ "$status" = "124" ]; then
    echo "[Error] $cmd_label timed out after ${timeout_s}s" 1>&2
    stage1_timeout_diag "$cmd_label" "$@"
    exit 124
  fi
  if [ "$status" != "0" ]; then
    exit "$status"
  fi
}

summary_clean() {
  printf "%s" "$1" | tr '\t\r\n' '   '
}

stage1_timeout_write_summary() {
  ts="$1"
  safe_label="$2"
  raw_label="$3"
  diag_log="$4"
  diag_status="$5"
  backend_after="$6"
  uir_after="$7"
  hint="$8"
  health_status="$9"
  health_log="${10:-}"
  summary_file="$timeout_diag_dir/summary.tsv"
  if [ ! -f "$summary_file" ]; then
    printf "ts\tlabel\tcmd\tdiag_status\tbackend_after\tuir_after\thealth\thint\tlog\thealth_log\n" >"$summary_file"
  fi
  printf "%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s\n" \
    "$(summary_clean "$ts")" \
    "$(summary_clean "$safe_label")" \
    "$(summary_clean "$raw_label")" \
    "$(summary_clean "$diag_status")" \
    "$(summary_clean "$backend_after")" \
    "$(summary_clean "$uir_after")" \
    "$(summary_clean "$health_status")" \
    "$(summary_clean "$hint")" \
    "$(summary_clean "$diag_log")" \
    "$(summary_clean "$health_log")" \
    >>"$summary_file"
  echo "[diag] summary: $summary_file" 1>&2
}

stage1_timeout_diag() {
  label="$1"
  shift
  if [ "$timeout_diag" != "1" ]; then
    return 0
  fi
  mkdir -p "$timeout_diag_dir"
  ts="$(date '+%Y%m%d_%H%M%S' 2>/dev/null || echo now)"
  safe_label="$(printf '%s' "$label" | tr -c 'A-Za-z0-9._-' '_' | tr -s '_')"
  diag_log="$timeout_diag_dir/${safe_label}_${ts}.log"
  inferred_backend="unknown"
  inferred_uir="unknown"
  inferred_hint="inspect profile tail + diag log for next missing stage transition."
  stage1_diag_health_status="not-run"
  stage1_diag_health_log=""
  echo "[diag] timeout probe start: label=$label timeout=${timeout_diag_timeout}s log=$diag_log" 1>&2
  set +e
  run_with_timeout "$timeout_diag_timeout" env \
    BACKEND_PROFILE=1 \
    UIR_PROFILE=1 \
    STAGE1_PROFILE=1 \
    "$@" >"$diag_log" 2>&1
  diag_status=$?
  set -e
  echo "[diag] timeout probe exit=$diag_status log=$diag_log" 1>&2
  if command -v rg >/dev/null 2>&1; then
    prof_lines="$(rg -n "backend_profile|uir_profile|\\[stage1\\] profile|generics_report" "$diag_log" || true)"
  else
    prof_lines="$(grep -nE "backend_profile|uir_profile|\\[stage1\\] profile|generics_report" "$diag_log" || true)"
  fi
  if [ "$prof_lines" != "" ]; then
    echo "[diag] profile snapshot:" 1>&2
    printf "%s\n" "$prof_lines" | tail -n 24 1>&2
    last_backend_label="$(printf "%s\n" "$prof_lines" | awk -F'\t' '/backend_profile/{label=$2} END{print label}')"
    last_uir_label="$(printf "%s\n" "$prof_lines" | awk -F'\t' '/uir_profile/{label=$2} END{print label}')"
    if [ "$last_backend_label" != "" ]; then
      inferred_backend="$last_backend_label"
    fi
    if [ "$last_uir_label" != "" ]; then
      inferred_uir="$last_uir_label"
    fi
    echo "[diag] inferred_blocker: backend_after=$inferred_backend uir_after=$inferred_uir" 1>&2
    case "$inferred_backend" in
      module_cache.load)
        inferred_hint="likely stuck in build_module (frontend/semantics/generics/lowering)."
        echo "[diag] hint: $inferred_hint" 1>&2
        ;;
      build_module)
        inferred_hint="likely stuck after IR build (emit/link path)."
        echo "[diag] hint: $inferred_hint" 1>&2
        ;;
      single.emit_obj|single.link)
        inferred_hint="likely stuck in object emit or link stage."
        echo "[diag] hint: $inferred_hint" 1>&2
        ;;
      *)
        echo "[diag] hint: $inferred_hint" 1>&2
        ;;
    esac
  else
    echo "[diag] no profile markers; recent output tail:" 1>&2
    tail -n 40 "$diag_log" 1>&2 || true
  fi
  stage1_timeout_health_probe "$safe_label" "$ts"
  stage1_timeout_write_summary \
    "$ts" \
    "$safe_label" \
    "$label" \
    "$diag_log" \
    "$diag_status" \
    "$inferred_backend" \
    "$inferred_uir" \
    "$inferred_hint" \
    "$stage1_diag_health_status" \
    "$stage1_diag_health_log"
}

stage1_timeout_health_probe() {
  safe_label="$1"
  ts="$2"
  if [ "$timeout_health_probe" != "1" ]; then
    stage1_diag_health_status="disabled"
    stage1_diag_health_log=""
    return 0
  fi
  if [ ! -f "$timeout_health_fixture" ]; then
    stage1_diag_health_status="skip-missing-fixture"
    stage1_diag_health_log=""
    echo "[diag] health probe skip: missing fixture $timeout_health_fixture" 1>&2
    return 0
  fi
  probe_out="$timeout_diag_dir/health_probe_${safe_label}_${ts}.o"
  probe_log="$timeout_diag_dir/health_probe_${safe_label}_${ts}.log"
  stage1_diag_health_status="running"
  stage1_diag_health_log="$probe_log"
  echo "[diag] health probe start: fixture=$timeout_health_fixture timeout=${timeout_health_timeout}s log=$probe_log" 1>&2
  set +e
  run_with_timeout "$timeout_health_timeout" env \
    MM=orc \
    STAGE1_SKIP_SEM="$stage1_skip_sem" \
    STAGE1_SKIP_OWNERSHIP="$stage1_skip_ownership" \
    BACKEND_MULTI=0 \
    BACKEND_MULTI_FORCE=0 \
    BACKEND_WHOLE_PROGRAM=1 \
    BACKEND_VALIDATE=0 \
    BACKEND_EMIT=obj \
    BACKEND_TARGET="$target" \
    BACKEND_FRONTEND="$frontend" \
    BACKEND_INPUT="$timeout_health_fixture" \
    BACKEND_OUTPUT="$probe_out" \
    "$driver" >"$probe_log" 2>&1
  probe_status=$?
  set -e
  case "$probe_status" in
    0)
      stage1_diag_health_status="ok"
      echo "[diag] health probe: ok (compiler path healthy for small fixture)" 1>&2
      ;;
    124)
      stage1_diag_health_status="timeout"
      echo "[diag] health probe: timeout after ${timeout_health_timeout}s (likely broader backend slowdown)" 1>&2
      tail -n 24 "$probe_log" 1>&2 || true
      ;;
    *)
      stage1_diag_health_status="fail:$probe_status"
      echo "[diag] health probe: fail rc=$probe_status (likely compiler/runtime instability)" 1>&2
      tail -n 24 "$probe_log" 1>&2 || true
      ;;
  esac
}

str_sig() {
  # cksum is POSIX and available on supported hosts.
  if [ "$#" = "0" ]; then
    cksum | awk '{printf "%s:%s", $1, $2}'
    return 0
  fi
  txt="$1"
  printf "%s" "$txt" | cksum | awk '{printf "%s:%s", $1, $2}'
}

cheng_env_sig() {
  env | LC_ALL=C sort | awk -F= '
    /^CHENG_/ {
      k=$1
      if (k == "STAGE1_FULLSPEC_REUSE") next
      if (k == "STAGE1_FULLSPEC_TIMEOUT") next
      if (k == "STAGE1_FULLSPEC_TIMEOUT_DIAG") next
      if (k == "STAGE1_FULLSPEC_TIMEOUT_DIAG_TIMEOUT") next
      if (k == "STAGE1_FULLSPEC_TIMEOUT_DIAG_DIR") next
      if (k == "STAGE1_FULLSPEC_TIMEOUT_HEALTH_PROBE") next
      if (k == "STAGE1_FULLSPEC_TIMEOUT_HEALTH_TIMEOUT") next
      if (k == "STAGE1_FULLSPEC_TIMEOUT_HEALTH_FIXTURE") next
      if (k == "BACKEND_INPUT") next
      if (k == "BACKEND_OUTPUT") next
      if (k == "BACKEND_PROFILE") next
      if (k == "UIR_PROFILE") next
      if (k == "STAGE1_PROFILE") next
      print $0
    }
  '
}

file_sig() {
  path="$1"
  if [ ! -e "$path" ]; then
    printf '%s' "missing"
    return 0
  fi
  perl -e '
    my @s = stat($ARGV[0]);
    if (@s) {
      print $s[9] . ":" . $s[7];
      exit 0;
    }
    print "missing";
  ' "$path"
}

host_os="$(uname -s 2>/dev/null || echo unknown)"
host_arch="$(uname -m 2>/dev/null || echo unknown)"

if [ "$target" = "" ]; then
  case "$host_os/$host_arch" in
    Darwin/arm64)
      target="arm64-apple-darwin"
      ;;
    Linux/aarch64|Linux/arm64)
      target="aarch64-unknown-linux-gnu"
      ;;
    *)
      if [ "$allow_skip" != "" ]; then
        echo "[skip] stage1 fullspec obj: unsupported host=$host_os/$host_arch" 1>&2
        exit 0
      fi
      echo "[Error] stage1 fullspec obj unsupported host=$host_os/$host_arch" 1>&2
      exit 2
      ;;
  esac
fi

if [ "$linker" = "" ] || [ "$linker" = "auto" ]; then
  if [ "${BACKEND_LINKER:-}" = "system" ]; then
    linker="system"
  else
    linker="self"
  fi
fi
if [ "$linker" != "self" ] && [ "$linker" != "system" ]; then
  echo "[Error] invalid --linker:$linker (expected self|system|auto)" 1>&2
  exit 2
fi

if [ "$linker" = "self" ] && [ "$host_os" = "Darwin" ] && ! command -v codesign >/dev/null 2>&1; then
  if [ "$allow_skip" != "" ]; then
    echo "[skip] stage1 fullspec obj: missing codesign" 1>&2
    exit 0
  fi
  echo "[Error] stage1 fullspec obj missing codesign (required for self-linked Mach-O)" 1>&2
  exit 2
fi

driver="${BACKEND_DRIVER:-}"
if [ "$driver" = "" ]; then
  if [ -x "artifacts/backend_driver/cheng" ]; then
    driver="artifacts/backend_driver/cheng"
  elif [ -x "artifacts/backend_selfhost_self_obj/cheng.stage2" ]; then
    driver="artifacts/backend_selfhost_self_obj/cheng.stage2"
  elif [ -x "artifacts/backend_selfhost_self_obj/cheng.stage1" ]; then
    driver="artifacts/backend_selfhost_self_obj/cheng.stage1"
  elif [ -x "artifacts/backend_seed/cheng.stage2" ]; then
    driver="artifacts/backend_seed/cheng.stage2"
  else
    driver="$(sh src/tooling/backend_driver_path.sh)"
  fi
fi
runtime_src="src/std/system_helpers_backend.cheng"
safe_target="$(printf '%s' "$target" | tr -c 'A-Za-z0-9._-' '_' | tr -s '_')"
runtime_obj="chengcache/system_helpers.backend.cheng.${safe_target}.o"
if [ "$linker" = "self" ]; then
  if [ ! -f "$runtime_src" ]; then
    echo "[Error] missing backend runtime source: $runtime_src" 1>&2
    exit 2
  fi
  mkdir -p chengcache
  if [ ! -f "$runtime_obj" ] || [ "$runtime_src" -nt "$runtime_obj" ]; then
    # shellcheck disable=SC2086
    run_cmd "build.stage1_fullspec.runtime_obj" env $jobs_env \
      STAGE1_SKIP_SEM="$stage1_skip_sem" \
      STAGE1_SKIP_OWNERSHIP="$stage1_skip_ownership" \
      BACKEND_MULTI="$multi" \
      BACKEND_MULTI_FORCE="$multi_force" \
      BACKEND_ALLOW_NO_MAIN=1 \
      BACKEND_WHOLE_PROGRAM=1 \
      BACKEND_EMIT=obj \
      BACKEND_TARGET="$target" \
      BACKEND_FRONTEND=stage1 \
      BACKEND_INPUT="$runtime_src" \
      BACKEND_OUTPUT="$runtime_obj" \
      "$driver"
  fi
  if [ ! -s "$runtime_obj" ]; then
    echo "[Error] missing backend runtime obj: $runtime_obj" 1>&2
    exit 2
  fi
fi

build_sig="input=$(file_sig "$file")"
build_sig="$build_sig;driver_path=$driver;driver_sig=$(file_sig "$driver")"
build_sig="$build_sig;target=$target;frontend=$frontend;linker=$linker;mm=$mm"
build_sig="$build_sig;multi=$multi;multi_force=$multi_force;validate=$validate"
build_sig="$build_sig;backend=$backend;jobs=$jobs"
build_sig="$build_sig;cheng_env_sig=$(cheng_env_sig | str_sig)"
if [ "$linker" = "self" ]; then
  build_sig="$build_sig;runtime_obj=$runtime_obj;runtime_sig=$(file_sig "$runtime_obj")"
fi

build_required="1"
if [ "$reuse" = "1" ] && [ -x "$output_bin" ] && [ -f "$stamp_file" ]; then
  saved_sig="$(cat "$stamp_file" 2>/dev/null || true)"
  if [ "$saved_sig" = "$build_sig" ]; then
    build_required="0"
    echo "== build.stage1_fullspec.obj (reuse) =="
  fi
fi

if [ "$build_required" = "1" ]; then
  if [ "$linker" = "self" ]; then
    # shellcheck disable=SC2086
    run_cmd "build.stage1_fullspec.obj" env $envs $jobs_env \
      BACKEND_MULTI="$multi" \
      BACKEND_MULTI_FORCE="$multi_force" \
      BACKEND_LINKER=self \
      BACKEND_WHOLE_PROGRAM=1 \
      BACKEND_NO_RUNTIME_C=1 \
      BACKEND_RUNTIME_OBJ="$runtime_obj" \
      BACKEND_VALIDATE="$validate" \
      BACKEND_FRONTEND="$frontend" \
      BACKEND_EMIT=exe \
      BACKEND_TARGET="$target" \
      BACKEND_INPUT="$file" \
      BACKEND_OUTPUT="$output_bin" \
      "$driver"
  else
    # shellcheck disable=SC2086
    run_cmd "build.stage1_fullspec.obj" env $envs $jobs_env \
      BACKEND_MULTI="$multi" \
      BACKEND_MULTI_FORCE="$multi_force" \
      BACKEND_LINKER=system \
      BACKEND_WHOLE_PROGRAM=1 \
      BACKEND_NO_RUNTIME_C=0 \
      BACKEND_RUNTIME_OBJ= \
      BACKEND_VALIDATE="$validate" \
      BACKEND_FRONTEND="$frontend" \
      BACKEND_EMIT=exe \
      BACKEND_TARGET="$target" \
      BACKEND_INPUT="$file" \
      BACKEND_OUTPUT="$output_bin" \
      "$driver"
  fi
  if [ "$reuse" = "1" ]; then
    printf "%s" "$build_sig" > "$stamp_file"
  fi
fi

if [ "$skip_run" != "" ]; then
  echo "stage1 fullspec build ok: $name"
  exit 0
fi

if [ ! -x "$output_bin" ]; then
  echo "[Error] missing output binary: $output_bin" 1>&2
  exit 2
fi

mkdir -p "$(dirname "$log")"
# shellcheck disable=SC2086
set +e
output="$(run_with_timeout "$timeout_s" env $envs "$output_bin" 2>&1)"
status=$?
set -e
printf "%s\n" "$output" >"$log"
if [ "$status" = "124" ]; then
  echo "[Error] stage1 fullspec run timed out after ${timeout_s}s (see $log)" 1>&2
  exit 124
fi
if [ "$status" != "0" ]; then
  echo "[Error] stage1 fullspec failed (exit $status, see $log)" 1>&2
  exit 2
fi

if command -v rg >/dev/null 2>&1; then
  if ! printf "%s\n" "$output" | rg -q "fullspec ok"; then
    echo "[Error] stage1 fullspec missing 'fullspec ok' (see $log)" 1>&2
    exit 2
  fi
else
  if ! printf "%s\n" "$output" | grep -q "fullspec ok"; then
    echo "[Error] stage1 fullspec missing 'fullspec ok' (see $log)" 1>&2
    exit 2
  fi
fi

echo "stage1 fullspec verify ok: $name"
