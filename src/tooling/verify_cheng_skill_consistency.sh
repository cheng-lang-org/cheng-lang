#!/usr/bin/env sh
. "$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)/env_prefix_bridge.sh"
set -eu
(set -o pipefail) 2>/dev/null && set -o pipefail

root="$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)"
cd "$root"

skill_root="${SKILL_ROOT:-docs/cheng-skill}"
skill_md="$skill_root/SKILL.md"
grammar_md="$skill_root/references/grammar.md"
ownership_md="$skill_root/references/ownership.md"
stdlib_md="$skill_root/references/stdlib.md"
template_md="$skill_root/assets/hello-cheng/main.cheng"
fixture_cheng="tests/cheng/skill/hello_cheng_ci_sample.cheng"
run_fixture_cheng="tests/cheng/backend/fixtures/return_add.cheng"
if [ ! -f "$run_fixture_cheng" ]; then
  run_fixture_cheng="tests/cheng/backend/fixtures/return_i64.cheng"
fi
skill_compile="${SKILL_COMPILE:-1}"
skill_run="${SKILL_RUN:-1}"
skill_frontend="${SKILL_FRONTEND:-stage1}"

fail() {
  echo "[verify_cheng_skill_consistency] $1" 1>&2
  exit 1
}

require_file() {
  f="$1"
  [ -f "$f" ] || fail "missing file: $f"
}

check_forbidden() {
  file="$1"
  regex="$2"
  label="$3"
  if grep -nE "$regex" "$file" >/dev/null 2>&1; then
    echo "[verify_cheng_skill_consistency] forbidden syntax ($label): $file" 1>&2
    grep -nE "$regex" "$file" 1>&2 || true
    exit 1
  fi
}

check_forbidden_in_markdown_code() {
  file="$1"
  regex="$2"
  label="$3"
  if awk '
    /^```/ { in_code = !in_code; next }
    in_code { print }
  ' "$file" | grep -nE "$regex" >/dev/null 2>&1; then
    echo "[verify_cheng_skill_consistency] forbidden syntax ($label): $file" 1>&2
    awk '
      /^```/ { in_code = !in_code; next }
      in_code { print }
    ' "$file" | grep -nE "$regex" 1>&2 || true
    exit 1
  fi
}

compare_if_exists() {
  src_file="$1"
  dst_file="$2"
  label="$3"
  if [ ! -f "$dst_file" ]; then
    fail "missing synced file for $label: $dst_file"
  fi
  if ! cmp -s "$src_file" "$dst_file"; then
    fail "drift detected for $label: $src_file != $dst_file"
  fi
}

run_with_timeout() {
  limit_secs="$1"
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
  ' "$limit_secs" "$@"
}

require_file "$skill_md"
require_file "$grammar_md"
require_file "$ownership_md"
require_file "$stdlib_md"
require_file "$template_md"
require_file "$fixture_cheng"
if [ "$skill_compile" = "1" ] && [ "$skill_run" = "1" ]; then
  require_file "$run_fixture_cheng"
fi

case "$skill_frontend" in
  stage1)
    ;;
  *)
    fail "invalid SKILL_FRONTEND=$skill_frontend (expected stage1)"
    ;;
esac

if ! grep -qE '^1\. .*cheng-formal-spec\.md' "$skill_md"; then
  fail "authority order missing formal spec as #1 in $skill_md"
fi
if ! grep -qE '一律以 `?cheng-formal-spec\.md`? 为准' "$skill_md"; then
  fail "missing conflict rule (formal spec wins) in $skill_md"
fi
if ! grep -q 'references/ownership.md' "$skill_md"; then
  fail "missing ownership reference in $skill_md"
fi
if ! grep -qi 'no-pointer' "$skill_md"; then
  fail "missing no-pointer policy in $skill_md"
fi
if ! grep -qi 'no-pointer' "$grammar_md"; then
  fail "missing no-pointer policy note in $grammar_md"
fi

check_forbidden_in_markdown_code "$grammar_md" '^[[:space:]]*import[[:space:]]+std[[:space:]]+/' 'legacy spaced std import'
check_forbidden_in_markdown_code "$grammar_md" '^[[:space:]]*import[[:space:]]+"' 'string import'
check_forbidden_in_markdown_code "$grammar_md" '^[[:space:]]*import[[:space:]]+\.{1,2}/' 'relative import'
check_forbidden_in_markdown_code "$grammar_md" '^[[:space:]]*import[[:space:]]+[A-Za-z0-9_/]+[[:space:]]*,[[:space:]]*[A-Za-z0-9_/]+[[:space:]]*$' 'comma import list'
check_forbidden_in_markdown_code "$grammar_md" '^[[:space:]]*(method|converter)[[:space:]]+[A-Za-z_]' 'legacy top-level routine keyword'
check_forbidden_in_markdown_code "$grammar_md" 'TypeExpr[[:space:]]+expr' 'legacy TypeExpr expr conversion'
check_forbidden_in_markdown_code "$grammar_md" 'cast\[[^]]+\]\(' 'legacy cast[T](x)'

check_forbidden_in_markdown_code "$stdlib_md" '^[[:space:]]*import[[:space:]]+std[[:space:]]+/' 'legacy spaced std import'
check_forbidden_in_markdown_code "$stdlib_md" '^[[:space:]]*import[[:space:]]+"' 'string import'
check_forbidden_in_markdown_code "$stdlib_md" '^[[:space:]]*import[[:space:]]+\.{1,2}/' 'relative import'
check_forbidden_in_markdown_code "$stdlib_md" '\.\./\.\./cheng/stdlib/bootstrap' 'legacy relative bootstrap import'
check_forbidden_in_markdown_code "$ownership_md" '^[[:space:]]*(method|converter)[[:space:]]+[A-Za-z_]' 'legacy top-level routine keyword'
check_forbidden_in_markdown_code "$ownership_md" 'cast\[[^]]+\]\(' 'legacy cast[T](x)'

check_forbidden "$template_md" '^[[:space:]]*(method|converter)[[:space:]]+[A-Za-z_]' 'legacy top-level routine keyword'
check_forbidden "$template_md" 'cast\[[^]]+\]\(' 'legacy cast[T](x)'
check_forbidden "$template_md" '^[[:space:]]*import[[:space:]]+"' 'string import'
check_forbidden "$template_md" '^[[:space:]]*import[[:space:]]+\.{1,2}/' 'relative import'

if ! cmp -s "$template_md" "$fixture_cheng"; then
  fail "template drift: $template_md != $fixture_cheng"
fi

if [ "$skill_compile" = "1" ]; then
  driver="${BACKEND_DRIVER:-}"
  allow_selfhost="${SKILL_DRIVER_ALLOW_SELFHOST:-0}"
  if [ "$driver" = "" ]; then
    driver="${SKILL_DRIVER:-}"
  fi
  if [ "$driver" = "" ]; then
    if [ -x "artifacts/backend_seed/cheng.stage2" ]; then
      driver="artifacts/backend_seed/cheng.stage2"
    elif [ -x "dist/releases/current/cheng" ]; then
      driver="dist/releases/current/cheng"
    elif [ -x "artifacts/backend_driver/cheng" ]; then
      driver="artifacts/backend_driver/cheng"
    else
      driver="$(BACKEND_DRIVER_PATH_ALLOW_SELFHOST="$allow_selfhost" sh src/tooling/backend_driver_path.sh 2>/dev/null || true)"
    fi
  fi
  if [ "$driver" = "" ] && [ "$allow_selfhost" = "1" ]; then
    if [ -x "artifacts/backend_selfhost_self_obj/cheng.stage2" ]; then
      driver="artifacts/backend_selfhost_self_obj/cheng.stage2"
    elif [ -x "artifacts/backend_selfhost_self_obj/cheng.stage1" ]; then
      driver="artifacts/backend_selfhost_self_obj/cheng.stage1"
    fi
  fi
  if [ ! -x "$driver" ]; then
    fail "backend driver not executable: $driver"
  fi
  driver_exec="$driver"
  driver_real_env=""
  if [ -x "src/tooling/backend_driver_exec.sh" ]; then
    driver_exec="src/tooling/backend_driver_exec.sh"
    driver_real_env="BACKEND_DRIVER_REAL=$driver"
  fi
  out_dir="artifacts/cheng_skill_consistency"
  out_obj="$out_dir/hello_cheng_ci_sample.o"
  out_exe="$out_dir/hello_cheng_ci_sample"
  out_exe_win="$out_dir/hello_cheng_ci_sample.exe"
  run_exe="$out_dir/return_add_smoke"
  run_exe_win="$out_dir/return_add_smoke.exe"
  compile_timeout="${SKILL_COMPILE_TIMEOUT:-60}"
  run_timeout="${SKILL_RUN_TIMEOUT:-10}"

  mkdir -p "$out_dir"
  rm -f "$out_obj" "$out_exe" "$out_exe_win" "$run_exe" "$run_exe_win"

  set +e
  run_with_timeout "$compile_timeout" env \
    $driver_real_env \
    MM=orc \
    BACKEND_MULTI=1 \
    BACKEND_MULTI_FORCE=1 \
    STAGE1_SKIP_SEM=0 \
    GENERIC_MODE=dict \
    GENERIC_SPEC_BUDGET=0 \
    STAGE1_SKIP_OWNERSHIP=1 \
    BACKEND_FRONTEND="$skill_frontend" \
    BACKEND_VALIDATE=1 \
    BACKEND_EMIT=obj \
    BACKEND_INPUT="$fixture_cheng" \
    BACKEND_OUTPUT="$out_obj" \
    "$driver_exec"
  status="$?"
  set -e
  if [ "$status" -eq 124 ]; then
    fail "compile timeout (${compile_timeout}s) for object sample: $fixture_cheng"
  fi
  [ "$status" -eq 0 ] || fail "compile failed for object sample: $fixture_cheng (status=$status)"

  [ -s "$out_obj" ] || fail "missing object output after compile: $out_obj"

  set +e
  run_with_timeout "$compile_timeout" env \
    $driver_real_env \
    MM=orc \
    BACKEND_MULTI=1 \
    BACKEND_MULTI_FORCE=1 \
    STAGE1_SKIP_SEM=0 \
    GENERIC_MODE=dict \
    GENERIC_SPEC_BUDGET=0 \
    STAGE1_SKIP_OWNERSHIP=1 \
    BACKEND_FRONTEND="$skill_frontend" \
    BACKEND_VALIDATE=1 \
    BACKEND_EMIT=exe \
    BACKEND_INPUT="$fixture_cheng" \
    BACKEND_OUTPUT="$out_exe" \
    "$driver_exec"
  status="$?"
  set -e
  if [ "$status" -eq 124 ]; then
    fail "compile timeout (${compile_timeout}s) for executable sample: $fixture_cheng"
  fi
  [ "$status" -eq 0 ] || fail "compile failed for executable sample: $fixture_cheng (status=$status)"

  exe_path=""
  if [ -x "$out_exe" ]; then
    exe_path="$out_exe"
  elif [ -x "$out_exe_win" ] || [ -f "$out_exe_win" ]; then
    exe_path="$out_exe_win"
  fi
  [ "$exe_path" != "" ] || fail "missing executable output after compile: $out_exe(.exe)"

  if [ "$skill_run" = "1" ]; then
    set +e
    run_with_timeout "$compile_timeout" env \
      $driver_real_env \
      MM=orc \
      BACKEND_MULTI=1 \
      BACKEND_MULTI_FORCE=1 \
      STAGE1_SKIP_SEM=0 \
      GENERIC_MODE=dict \
      GENERIC_SPEC_BUDGET=0 \
      STAGE1_SKIP_OWNERSHIP=1 \
      BACKEND_FRONTEND="$skill_frontend" \
      BACKEND_VALIDATE=1 \
      BACKEND_EMIT=exe \
      BACKEND_INPUT="$run_fixture_cheng" \
      BACKEND_OUTPUT="$run_exe" \
      "$driver_exec"
    status="$?"
    set -e
    if [ "$status" -eq 124 ]; then
      fail "compile timeout (${compile_timeout}s) for smoke sample: $run_fixture_cheng"
    fi
    [ "$status" -eq 0 ] || fail "compile failed for smoke sample: $run_fixture_cheng (status=$status)"

    run_path=""
    if [ -x "$run_exe" ]; then
      run_path="$run_exe"
    elif [ -x "$run_exe_win" ] || [ -f "$run_exe_win" ]; then
      run_path="$run_exe_win"
    fi
    [ "$run_path" != "" ] || fail "missing smoke executable output: $run_exe(.exe)"

    run_with_timeout "$run_timeout" "$run_path" >/dev/null
  fi
fi

home_skill_root="${SKILL_HOME_ROOT:-$HOME/.codex/skills/cheng语言}"
if [ "${SKILL_COMPARE_HOME:-1}" = "1" ] && [ -d "$home_skill_root" ]; then
  compare_if_exists "$skill_md" "$home_skill_root/SKILL.md" 'SKILL.md'
  compare_if_exists "$grammar_md" "$home_skill_root/references/grammar.md" 'references/grammar.md'
  compare_if_exists "$ownership_md" "$home_skill_root/references/ownership.md" 'references/ownership.md'
  compare_if_exists "$stdlib_md" "$home_skill_root/references/stdlib.md" 'references/stdlib.md'
  compare_if_exists "$template_md" "$home_skill_root/assets/hello-cheng/main.cheng" 'assets/hello-cheng/main.cheng'
fi

echo "verify_cheng_skill_consistency ok"
