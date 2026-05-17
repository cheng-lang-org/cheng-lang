#!/usr/bin/env bash
set -uo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
cd "$ROOT" || exit 1

fail() {
    printf 'FAIL %s\n' "$*" >&2
    exit 1
}

pass() {
    printf 'PASS %s\n' "$*"
}

resolve_runner() {
    os_name="$(uname -s 2>/dev/null || true)"
    machine_name="$(uname -m 2>/dev/null || true)"
    if [ "$os_name" = "Linux" ] && { [ "$machine_name" = "aarch64" ] || [ "$machine_name" = "arm64" ]; }; then
        printf '__native_linux_aarch64__\n'
        return 0
    fi

    if [ -n "${CHENG_LINUX_AARCH64_RUNNER:-}" ]; then
        if [ -x "$CHENG_LINUX_AARCH64_RUNNER" ]; then
            printf '%s\n' "$CHENG_LINUX_AARCH64_RUNNER"
            return 0
        fi
        if command -v "$CHENG_LINUX_AARCH64_RUNNER" >/dev/null 2>&1; then
            command -v "$CHENG_LINUX_AARCH64_RUNNER"
            return 0
        fi
        printf 'FAIL runner_not_executable %s\n' "$CHENG_LINUX_AARCH64_RUNNER" >&2
        return 1
    fi

    if command -v qemu-aarch64 >/dev/null 2>&1; then
        command -v qemu-aarch64
        return 0
    fi
    if command -v qemu-aarch64-static >/dev/null 2>&1; then
        command -v qemu-aarch64-static
        return 0
    fi

    if command -v qemu-system-aarch64 >/dev/null 2>&1; then
        printf 'FAIL qemu-system-aarch64_found_but_not_user_mode_runner\n' >&2
    else
        printf 'FAIL missing_linux_aarch64_user_mode_runner\n' >&2
    fi
    printf 'Set CHENG_LINUX_AARCH64_RUNNER to qemu-aarch64/qemu-aarch64-static or run this gate on native Linux AArch64.\n' >&2
    return 1
}

RUNNER="$(resolve_runner)" || exit 1
COLD="${CHENG_COLD:-/tmp/cheng_cold_linux_aarch64_provider_runtime}"
if [ ! -x "$COLD" ] || [ "$ROOT/bootstrap/cheng_cold.c" -nt "$COLD" ]; then
    cc -std=c11 -O2 -o "$COLD" "$ROOT/bootstrap/cheng_cold.c" || fail "build_cold"
fi

WORK="${CHENG_LINUX_AARCH64_PROVIDER_RUNTIME_WORK:-/tmp/cheng_linux_aarch64_provider_runtime}"
rm -rf "$WORK"
mkdir -p "$WORK" || fail "mkdir_work"

expect_report_value() {
    report="$1"
    key="$2"
    value="$3"
    if ! grep -q "^${key}=${value}$" "$report" 2>/dev/null; then
        fail "report_expected_${key}_${value}"
    fi
}

expect_report_regex() {
    report="$1"
    key="$2"
    regex="$3"
    if ! grep -Eq "^${key}=${regex}$" "$report" 2>/dev/null; then
        fail "report_expected_${key}_${regex}"
    fi
}

compile_case() {
    name="$1"
    source="$2"
    exe="$WORK/$name"
    report="$WORK/$name.report.txt"
    stdout="$WORK/$name.stdout"
    stderr="$WORK/$name.stderr"

    "$COLD" system-link-exec \
        --in:"$source" \
        --target:aarch64-unknown-linux-gnu \
        --out:"$exe" \
        --report-out:"$report" \
        --link-providers >"$stdout" 2>"$stderr" || fail "${name}_compile"

    [ -s "$exe" ] || fail "${name}_missing_exe"
    file "$exe" 2>/dev/null | grep -q 'ELF 64-bit.*executable.*aarch64' || fail "${name}_not_aarch64_elf"
    expect_report_value "$report" "system_link_exec" "1"
    expect_report_value "$report" "system_link_exec_scope" "cold_runtime_provider_archive"
    expect_report_value "$report" "provider_archive" "1"
    expect_report_value "$report" "provider_object_count" "2"
    expect_report_value "$report" "provider_archive_member_count" "2"
    expect_report_regex "$report" "provider_export_count" "[1-9][0-9]*"
    expect_report_regex "$report" "provider_resolved_symbol_count" "[1-9][0-9]*"
    expect_report_value "$report" "unresolved_symbol_count" "0"
    expect_report_value "$report" "system_link" "0"

    pass "${name}_link_report"
}

run_case() {
    name="$1"
    exe="$WORK/$name"
    stdout="$WORK/$name.run.stdout"
    stderr="$WORK/$name.run.stderr"
    expected="$2"

    if [ "$RUNNER" = "__native_linux_aarch64__" ]; then
        chmod +x "$exe" || fail "${name}_chmod"
        "$exe" >"$stdout" 2>"$stderr"
    else
        "$RUNNER" "$exe" >"$stdout" 2>"$stderr"
    fi
    status=$?
    if [ "$status" -ne "$expected" ]; then
        fail "${name}_exit_${status}_expected_${expected}"
    fi
}

compile_case "runtime_provider_autolink_trace" "testdata/runtime_provider_autolink_trace.cheng"
run_case "runtime_provider_autolink_trace" 21
grep -Fq 'core runtime getpid' "$WORK/runtime_provider_autolink_trace.run.stderr" || fail "runtime_provider_autolink_trace_missing_getpid_marker"
pass "runtime_provider_autolink_trace_getpid_run_marker"

compile_case "runtime_provider_autolink_cpu_cores" "testdata/runtime_provider_autolink_cpu_cores.cheng"
if [ "$RUNNER" = "__native_linux_aarch64__" ]; then
    chmod +x "$WORK/runtime_provider_autolink_cpu_cores" || fail "runtime_provider_autolink_cpu_cores_chmod"
    "$WORK/runtime_provider_autolink_cpu_cores" \
        >"$WORK/runtime_provider_autolink_cpu_cores.run.stdout" \
        2>"$WORK/runtime_provider_autolink_cpu_cores.run.stderr"
else
    "$RUNNER" "$WORK/runtime_provider_autolink_cpu_cores" \
        >"$WORK/runtime_provider_autolink_cpu_cores.run.stdout" \
        2>"$WORK/runtime_provider_autolink_cpu_cores.run.stderr"
fi
status=$?
if [ "$status" -lt 1 ] || [ "$status" -gt 255 ]; then
    fail "runtime_provider_autolink_cpu_cores_exit_${status}_expected_1_255"
fi
pass "runtime_provider_autolink_cpu_cores_run_marker"

compile_case "thread_join_pool_runtime_smoke" "src/tests/thread_join_pool_runtime_smoke.cheng"
run_case "thread_join_pool_runtime_smoke" 0
grep -Fq ' thread_join_pool_runtime_smoke ok' "$WORK/thread_join_pool_runtime_smoke.run.stdout" || fail "thread_join_pool_runtime_smoke_missing_marker"
pass "thread_join_pool_runtime_smoke_run_marker"

pass "linux_aarch64_provider_runtime_smoke"
