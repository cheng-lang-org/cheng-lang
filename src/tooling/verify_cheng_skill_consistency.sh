#!/usr/bin/env sh
set -eu
(set -o pipefail) 2>/dev/null && set -o pipefail

root="$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)"
cd "$root"

skill_root="${CHENG_SKILL_ROOT:-docs/cheng-skill}"
skill_md="$skill_root/SKILL.md"
grammar_md="$skill_root/references/grammar.md"
ownership_md="$skill_root/references/ownership.md"
stdlib_md="$skill_root/references/stdlib.md"
template_md="$skill_root/assets/hello-cheng/main.cheng"
fixture_cheng="tests/cheng/skill/hello_cheng_ci_sample.cheng"
run_fixture_cheng="tests/cheng/backend/fixtures/return_add.cheng"
skill_compile="${CHENG_SKILL_COMPILE:-1}"
skill_run="${CHENG_SKILL_RUN:-1}"

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
  if command -v timeout >/dev/null 2>&1; then
    timeout "$limit_secs" "$@"
    return $?
  fi
  if command -v gtimeout >/dev/null 2>&1; then
    gtimeout "$limit_secs" "$@"
    return $?
  fi
  "$@"
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

if ! grep -qE '^1\. .*cheng-formal-spec\.md' "$skill_md"; then
  fail "authority order missing formal spec as #1 in $skill_md"
fi
if ! grep -qE '一律以 `?cheng-formal-spec\.md`? 为准' "$skill_md"; then
  fail "missing conflict rule (formal spec wins) in $skill_md"
fi
if ! grep -q 'references/ownership.md' "$skill_md"; then
  fail "missing ownership reference in $skill_md"
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
  driver="${CHENG_BACKEND_DRIVER:-}"
  if [ "$driver" = "" ]; then
    if [ -x "artifacts/backend_selfhost_self_obj/cheng.stage2" ]; then
      driver="artifacts/backend_selfhost_self_obj/cheng.stage2"
    elif [ -x "artifacts/backend_selfhost_self_obj/cheng.stage1" ]; then
      driver="artifacts/backend_selfhost_self_obj/cheng.stage1"
    elif [ -x "artifacts/backend_seed/cheng.stage2" ]; then
      driver="artifacts/backend_seed/cheng.stage2"
    else
      driver="$(sh src/tooling/backend_driver_path.sh)"
    fi
  fi
  out_dir="artifacts/cheng_skill_consistency"
  out_obj="$out_dir/hello_cheng_ci_sample.o"
  out_exe="$out_dir/hello_cheng_ci_sample"
  out_exe_win="$out_dir/hello_cheng_ci_sample.exe"
  run_exe="$out_dir/return_add_smoke"
  run_exe_win="$out_dir/return_add_smoke.exe"

  mkdir -p "$out_dir"
  rm -f "$out_obj" "$out_exe" "$out_exe_win" "$run_exe" "$run_exe_win"

  CHENG_MM=orc \
  CHENG_C_SYSTEM=system \
  CHENG_STAGE1_SKIP_SEM=0 \
  CHENG_STAGE1_SKIP_MONO=1 \
  CHENG_STAGE1_SKIP_OWNERSHIP=1 \
  CHENG_BACKEND_FRONTEND=stage1 \
  CHENG_BACKEND_VALIDATE=1 \
  CHENG_BACKEND_EMIT=obj \
  CHENG_BACKEND_INPUT="$fixture_cheng" \
  CHENG_BACKEND_OUTPUT="$out_obj" \
  "$driver"

  [ -s "$out_obj" ] || fail "missing object output after compile: $out_obj"

  CHENG_MM=orc \
  CHENG_C_SYSTEM=system \
  CHENG_STAGE1_SKIP_SEM=0 \
  CHENG_STAGE1_SKIP_MONO=1 \
  CHENG_STAGE1_SKIP_OWNERSHIP=1 \
  CHENG_BACKEND_FRONTEND=stage1 \
  CHENG_BACKEND_VALIDATE=1 \
  CHENG_BACKEND_EMIT=exe \
  CHENG_BACKEND_INPUT="$fixture_cheng" \
  CHENG_BACKEND_OUTPUT="$out_exe" \
  "$driver"

  exe_path=""
  if [ -x "$out_exe" ]; then
    exe_path="$out_exe"
  elif [ -x "$out_exe_win" ] || [ -f "$out_exe_win" ]; then
    exe_path="$out_exe_win"
  fi
  [ "$exe_path" != "" ] || fail "missing executable output after compile: $out_exe(.exe)"

  if [ "$skill_run" = "1" ]; then
    CHENG_MM=orc \
    CHENG_C_SYSTEM=system \
    CHENG_STAGE1_SKIP_SEM=0 \
    CHENG_STAGE1_SKIP_MONO=1 \
    CHENG_STAGE1_SKIP_OWNERSHIP=1 \
    CHENG_BACKEND_FRONTEND=stage1 \
    CHENG_BACKEND_VALIDATE=1 \
    CHENG_BACKEND_EMIT=exe \
    CHENG_BACKEND_INPUT="$run_fixture_cheng" \
    CHENG_BACKEND_OUTPUT="$run_exe" \
    "$driver"

    run_path=""
    if [ -x "$run_exe" ]; then
      run_path="$run_exe"
    elif [ -x "$run_exe_win" ] || [ -f "$run_exe_win" ]; then
      run_path="$run_exe_win"
    fi
    [ "$run_path" != "" ] || fail "missing smoke executable output: $run_exe(.exe)"

    run_with_timeout 10 "$run_path" >/dev/null
  fi
fi

home_skill_root="${CHENG_SKILL_HOME_ROOT:-$HOME/.codex/skills/cheng语言}"
if [ "${CHENG_SKILL_COMPARE_HOME:-1}" = "1" ] && [ -d "$home_skill_root" ]; then
  compare_if_exists "$skill_md" "$home_skill_root/SKILL.md" 'SKILL.md'
  compare_if_exists "$grammar_md" "$home_skill_root/references/grammar.md" 'references/grammar.md'
  compare_if_exists "$ownership_md" "$home_skill_root/references/ownership.md" 'references/ownership.md'
  compare_if_exists "$stdlib_md" "$home_skill_root/references/stdlib.md" 'references/stdlib.md'
  compare_if_exists "$template_md" "$home_skill_root/assets/hello-cheng/main.cheng" 'assets/hello-cheng/main.cheng'
fi

echo "verify_cheng_skill_consistency ok"
