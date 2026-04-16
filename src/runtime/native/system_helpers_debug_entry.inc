static int32_t cheng_v3_line_map_registered = 0;

static void v3_debug_register_line_map_from_argv0_impl(const char* argv0) {
    char exe_path[PATH_MAX];
    if (cheng_v3_line_map_registered) return;
    cheng_v3_line_map_registered = 1;
    if (!v3_debug_runtime_resolve_self_path(exe_path, sizeof(exe_path))) {
        if (argv0 == NULL || argv0[0] == '\0') {
            v3_debug_crash_trace_install_handlers();
            return;
        }
        if (realpath(argv0, exe_path) == NULL) {
            snprintf(exe_path, sizeof(exe_path), "%s", argv0);
        }
    }
    v3_debug_crash_trace_install_handlers();
    v3_debug_profile_maybe_init(exe_path);
}

static void v3_debug_dump_backtrace_now_impl(const char* reason) {
    const char* actual_reason = reason != NULL && reason[0] != '\0' ? reason : "panic";
    uintptr_t pc = (uintptr_t)__builtin_return_address(0);
    uintptr_t fp = (uintptr_t)__builtin_frame_address(0);
    uintptr_t sp = (uintptr_t)&pc;
    uintptr_t lr = pc;
    v3_debug_crash_trace_dump_now(actual_reason);
    v3_debug_async_write_crash_raw_header(actual_reason, "backtrace", 0, pc, fp, sp, lr, 0U);
    v3_debug_dump_machine_backtrace_if_available(actual_reason);
    v3_debug_dump_source_backtrace_if_available(actual_reason);
    v3_debug_dump_native_backtrace_if_enabled();
}
