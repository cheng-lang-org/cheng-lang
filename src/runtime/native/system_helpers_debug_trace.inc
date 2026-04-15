static const ChengV3EmbeddedLineMapEntry* cheng_v3_embedded_line_map_find_pc_in_entries(
    const ChengV3EmbeddedLineMapEntry* entries,
    uint64_t count,
    uintptr_t pc
) {
    uint64_t i;
    if (entries == NULL || count == 0U || pc == 0U) return NULL;
    for (i = 0U; i < count; ++i) {
        uintptr_t start = (uintptr_t)entries[i].start_pc;
        uintptr_t end = (uintptr_t)entries[i].end_pc;
        if (start == 0U) continue;
        if (pc < start) continue;
        if (end != 0U && pc >= end) continue;
        return &entries[i];
    }
    return NULL;
}

static void v3_debug_dump_async_crash_trace(const char* reason, int32_t sig) {
    int32_t end;
    int32_t start;
    int32_t i;
    v3_debug_write_cstr("[cheng-crash-trace] reason=");
    v3_debug_write_cstr(reason);
    if (sig != 0) {
        v3_debug_write_cstr(" sig=");
        v3_debug_write_i32(sig);
    }
    if (cheng_crash_trace_phase != NULL) {
        v3_debug_write_cstr(" phase=");
        v3_debug_write_cstr(cheng_crash_trace_phase);
    }
    v3_debug_write_cstr("\n");
    end = cheng_crash_trace_next;
    start = end > CHENG_CRASH_TRACE_RING ? end - CHENG_CRASH_TRACE_RING : 0;
    for (i = start; i < end; ++i) {
        const ChengCrashTraceEntry* entry =
            &cheng_crash_trace_ring[(uint32_t)i % CHENG_CRASH_TRACE_RING];
        if (entry->tag == NULL) continue;
        v3_debug_write_cstr("[cheng-crash-trace] #");
        v3_debug_write_i32(i);
        v3_debug_write_cstr(" ");
        v3_debug_write_cstr(entry->tag);
        v3_debug_write_cstr(" a=");
        v3_debug_write_i32(entry->a);
        v3_debug_write_cstr(" b=");
        v3_debug_write_i32(entry->b);
        v3_debug_write_cstr(" c=");
        v3_debug_write_i32(entry->c);
        v3_debug_write_cstr(" d=");
        v3_debug_write_i32(entry->d);
        v3_debug_write_cstr("\n");
    }
}

static int32_t cheng_v3_line_span_start(int32_t signature_line, int32_t body_first_line) {
    return body_first_line > 0 ? body_first_line : signature_line;
}

static int32_t cheng_v3_line_span_end(int32_t line_start, int32_t body_last_line) {
    return body_last_line > 0 ? body_last_line : line_start;
}

static void v3_debug_async_write_line_span(const ChengV3EmbeddedLineMapEntry* entry) {
    int32_t line_start;
    int32_t line_end;
    if (entry == NULL) return;
    line_start = cheng_v3_line_span_start(entry->signature_line, entry->body_first_line);
    line_end = cheng_v3_line_span_end(line_start, entry->body_last_line);
    v3_debug_write_i32(line_start);
    if (line_end > line_start) {
        v3_debug_write_cstr("-");
        v3_debug_write_i32(line_end);
    }
}

static void v3_debug_async_write_source_frame(int32_t frame_index,
                                              const ChengV3EmbeddedLineMapEntry* entry,
                                              uintptr_t pc) {
    if (entry == NULL) return;
    v3_debug_write_cstr("[cheng-v3] #");
    v3_debug_write_i32(frame_index);
    v3_debug_write_cstr(" ");
    v3_debug_write_cstr(
        entry->function_name != NULL && entry->function_name[0] != '\0' ? entry->function_name : "?"
    );
    v3_debug_write_cstr(" ");
    v3_debug_write_cstr(
        entry->source_path != NULL && entry->source_path[0] != '\0' ? entry->source_path : "?"
    );
    v3_debug_write_cstr(":");
    v3_debug_async_write_line_span(entry);
    v3_debug_write_cstr(" pc=");
    v3_debug_write_hex_uintptr(pc);
    v3_debug_write_cstr("\n");
}

static void v3_debug_async_write_machine_frame(int32_t frame_index,
                                               uintptr_t pc,
                                               uintptr_t fp,
                                               uintptr_t sp,
                                               uintptr_t lr,
                                               const ChengV3EmbeddedLineMapEntry* entry) {
    uintptr_t offset = 0U;
    if (entry != NULL && entry->start_pc != NULL && pc >= (uintptr_t)entry->start_pc) {
        offset = pc - (uintptr_t)entry->start_pc;
    }
    v3_debug_write_cstr("[cheng-v3] m#");
    v3_debug_write_i32(frame_index);
    v3_debug_write_cstr(" pc=");
    v3_debug_write_hex_uintptr(pc);
    v3_debug_write_cstr(" fp=");
    v3_debug_write_hex_uintptr(fp);
    v3_debug_write_cstr(" sp=");
    if (sp != 0U) {
        v3_debug_write_hex_uintptr(sp);
    } else {
        v3_debug_write_cstr("?");
    }
    v3_debug_write_cstr(" lr=");
    if (lr != 0U) {
        v3_debug_write_hex_uintptr(lr);
    } else {
        v3_debug_write_cstr("?");
    }
    v3_debug_write_cstr(" off=");
    v3_debug_write_hex_uintptr(offset);
    if (entry != NULL) {
        v3_debug_write_cstr(" fn=");
        v3_debug_write_cstr(
            entry->function_name != NULL && entry->function_name[0] != '\0' ? entry->function_name : "?"
        );
        v3_debug_write_cstr(" src=");
        v3_debug_write_cstr(
            entry->source_path != NULL && entry->source_path[0] != '\0' ? entry->source_path : "?"
        );
        v3_debug_write_cstr(":");
        v3_debug_async_write_line_span(entry);
    }
    v3_debug_write_cstr("\n");
}

static uintptr_t cheng_v3_signal_context_pc(void* signal_context) {
#if defined(__APPLE__)
    const ucontext_t* context = (const ucontext_t*)signal_context;
    if (context == NULL || context->uc_mcontext == NULL) return 0U;
#if defined(__arm64__)
    return (uintptr_t)context->uc_mcontext->__ss.__pc;
#elif defined(__x86_64__)
    return (uintptr_t)context->uc_mcontext->__ss.__rip;
#elif defined(__i386__)
    return (uintptr_t)context->uc_mcontext->__ss.__eip;
#else
    return 0U;
#endif
#else
#if defined(__linux__) && defined(__aarch64__)
    const ucontext_t* context = (const ucontext_t*)signal_context;
    if (context == NULL) return 0U;
    return (uintptr_t)context->uc_mcontext.pc;
#else
    (void)signal_context;
    return 0U;
#endif
#endif
}

static uintptr_t cheng_v3_signal_context_fp(void* signal_context) {
#if defined(__APPLE__)
    const ucontext_t* context = (const ucontext_t*)signal_context;
    if (context == NULL || context->uc_mcontext == NULL) return 0U;
#if defined(__arm64__)
    return (uintptr_t)context->uc_mcontext->__ss.__fp;
#elif defined(__x86_64__)
    return (uintptr_t)context->uc_mcontext->__ss.__rbp;
#elif defined(__i386__)
    return (uintptr_t)context->uc_mcontext->__ss.__ebp;
#else
    return 0U;
#endif
#else
#if defined(__linux__) && defined(__aarch64__)
    const ucontext_t* context = (const ucontext_t*)signal_context;
    if (context == NULL) return 0U;
    return (uintptr_t)context->uc_mcontext.regs[29];
#else
    (void)signal_context;
    return 0U;
#endif
#endif
}

static uintptr_t cheng_v3_signal_context_sp(void* signal_context) {
#if defined(__APPLE__)
    const ucontext_t* context = (const ucontext_t*)signal_context;
    if (context == NULL || context->uc_mcontext == NULL) return 0U;
#if defined(__arm64__)
    return (uintptr_t)context->uc_mcontext->__ss.__sp;
#elif defined(__x86_64__)
    return (uintptr_t)context->uc_mcontext->__ss.__rsp;
#elif defined(__i386__)
    return (uintptr_t)context->uc_mcontext->__ss.__esp;
#else
    return 0U;
#endif
#else
#if defined(__linux__) && defined(__aarch64__)
    const ucontext_t* context = (const ucontext_t*)signal_context;
    if (context == NULL) return 0U;
    return (uintptr_t)context->uc_mcontext.sp;
#else
    (void)signal_context;
    return 0U;
#endif
#endif
}

static uintptr_t cheng_v3_signal_context_lr(void* signal_context) {
#if defined(__APPLE__)
    const ucontext_t* context = (const ucontext_t*)signal_context;
    if (context == NULL || context->uc_mcontext == NULL) return 0U;
#if defined(__arm64__)
    return (uintptr_t)context->uc_mcontext->__ss.__lr;
#else
    return 0U;
#endif
#else
#if defined(__linux__) && defined(__aarch64__)
    const ucontext_t* context = (const ucontext_t*)signal_context;
    if (context == NULL) return 0U;
    return (uintptr_t)context->uc_mcontext.regs[30];
#else
    (void)signal_context;
    return 0U;
#endif
#endif
}

static void v3_debug_async_write_crash_header(const char* reason,
                                              const char* mode,
                                              int32_t sig,
                                              uintptr_t pc,
                                              uintptr_t fp,
                                              uintptr_t sp,
                                              uintptr_t lr,
                                              uintptr_t fault_addr) {
    v3_debug_write_cstr("[cheng-v3] v3_crash_report_v1\n");
    v3_debug_write_cstr("[cheng-v3] reason=");
    v3_debug_write_cstr(reason != NULL ? reason : "?");
    v3_debug_write_cstr(" mode=");
    v3_debug_write_cstr(mode != NULL ? mode : "?");
    v3_debug_write_cstr("\n");
    v3_debug_write_cstr("[cheng-v3] regs pc=");
    v3_debug_write_hex_uintptr(pc);
    v3_debug_write_cstr(" fp=");
    v3_debug_write_hex_uintptr(fp);
    v3_debug_write_cstr(" sp=");
    v3_debug_write_hex_uintptr(sp);
    v3_debug_write_cstr(" lr=");
    v3_debug_write_hex_uintptr(lr);
    if (sig != 0) {
        v3_debug_write_cstr(" sig=");
        v3_debug_write_i32(sig);
    }
    if (sig != 0 || fault_addr != 0U) {
        v3_debug_write_cstr(" fault=");
        v3_debug_write_hex_uintptr(fault_addr);
    }
    v3_debug_write_cstr("\n");
}

static int32_t v3_debug_dump_machine_signal_frames(const ChengV3EmbeddedLineMapEntry* entries,
                                                   uint64_t count,
                                                   uintptr_t pc,
                                                   uintptr_t fp,
                                                   uintptr_t sp,
                                                   uintptr_t lr) {
    int32_t depth = 0;
    int32_t emitted = 0;
    uintptr_t current_pc = pc;
    uintptr_t current_fp = fp;
    uintptr_t current_sp = sp;
    uintptr_t current_lr = lr;
    while (current_pc != 0U && depth < 48) {
        const ChengV3EmbeddedLineMapEntry* entry =
            cheng_v3_embedded_line_map_find_pc_in_entries(entries, count, current_pc);
#if (defined(__APPLE__) && (defined(__arm64__) || defined(__x86_64__) || defined(__i386__))) || \
    (defined(__linux__) && defined(__aarch64__))
        if (depth > 0 &&
            v3_debug_probably_valid_ptr((const void*)current_fp) &&
            (current_fp & (uintptr_t)(sizeof(uintptr_t) - 1U)) == 0U) {
            const uintptr_t* current_frame = (const uintptr_t*)current_fp;
            current_lr = current_frame[1];
        }
#endif
        v3_debug_async_write_machine_frame(depth,
                                           current_pc,
                                           current_fp,
                                           current_sp,
                                           current_lr,
                                           entry);
        emitted += 1;
#if (defined(__APPLE__) && (defined(__arm64__) || defined(__x86_64__) || defined(__i386__))) || \
    (defined(__linux__) && defined(__aarch64__))
        if (!v3_debug_probably_valid_ptr((const void*)current_fp) ||
            (current_fp & (uintptr_t)(sizeof(uintptr_t) - 1U)) != 0U) {
            break;
        }
        {
            const uintptr_t* frame = (const uintptr_t*)current_fp;
            uintptr_t next_fp = frame[0];
            uintptr_t return_pc = frame[1];
            if (next_fp <= current_fp || (next_fp - current_fp) > (uintptr_t)(8U * 1024U * 1024U)) {
                break;
            }
            if (return_pc == 0U) {
                break;
            }
#if defined(__arm64__) || defined(__aarch64__)
            current_pc = return_pc > 4U ? return_pc - 4U : return_pc;
#else
            current_pc = return_pc > 1U ? return_pc - 1U : return_pc;
#endif
            current_fp = next_fp;
            current_sp = 0U;
            current_lr = 0U;
        }
        depth += 1;
#else
        break;
#endif
    }
    return emitted;
}

static int32_t v3_debug_dump_source_signal_frames(const ChengV3EmbeddedLineMapEntry* entries,
                                                  uint64_t count,
                                                  uintptr_t pc,
                                                  uintptr_t fp) {
    int32_t depth = 0;
    int32_t mapped = 0;
    uintptr_t current_pc = pc;
    uintptr_t current_fp = fp;
    while (current_pc != 0U && depth < 48) {
        const ChengV3EmbeddedLineMapEntry* entry =
            cheng_v3_embedded_line_map_find_pc_in_entries(entries, count, current_pc);
        if (entry != NULL) {
            v3_debug_async_write_source_frame(depth, entry, current_pc);
            mapped += 1;
        }
#if (defined(__APPLE__) && (defined(__arm64__) || defined(__x86_64__) || defined(__i386__))) || \
    (defined(__linux__) && defined(__aarch64__))
        if (!v3_debug_probably_valid_ptr((const void*)current_fp) ||
            (current_fp & (uintptr_t)(sizeof(uintptr_t) - 1U)) != 0U) {
            break;
        }
        {
            const uintptr_t* frame = (const uintptr_t*)current_fp;
            uintptr_t next_fp = frame[0];
            uintptr_t return_pc = frame[1];
            if (next_fp <= current_fp || (next_fp - current_fp) > (uintptr_t)(8U * 1024U * 1024U)) {
                break;
            }
            if (return_pc == 0U) {
                break;
            }
#if defined(__arm64__) || defined(__aarch64__)
            current_pc = return_pc > 4U ? return_pc - 4U : return_pc;
#else
            current_pc = return_pc > 1U ? return_pc - 1U : return_pc;
#endif
            current_fp = next_fp;
        }
        depth += 1;
#else
        break;
#endif
    }
    return mapped;
}

static void v3_debug_dump_source_signal_trace_if_available(const char* reason,
                                                           int32_t sig,
                                                           const siginfo_t* info,
                                                           void* signal_context) {
    const ChengV3EmbeddedLineMapEntry* entries = NULL;
    uint64_t count = 0U;
    uintptr_t pc = 0U;
    uintptr_t fp = 0U;
    uintptr_t sp = 0U;
    uintptr_t lr = 0U;
    int32_t mapped = 0;
    if (cheng_v3_embedded_line_map_entries_getter == NULL ||
        cheng_v3_embedded_line_map_count_getter == NULL) {
        return;
    }
    entries = cheng_v3_embedded_line_map_entries_getter();
    count = cheng_v3_embedded_line_map_count_getter();
    if (entries == NULL || count == 0U) return;
    pc = cheng_v3_signal_context_pc(signal_context);
    if (pc == 0U) return;
    fp = cheng_v3_signal_context_fp(signal_context);
    sp = cheng_v3_signal_context_sp(signal_context);
    lr = cheng_v3_signal_context_lr(signal_context);
    v3_debug_async_write_crash_header(reason,
                                      "signal",
                                      sig,
                                      pc,
                                      fp,
                                      sp,
                                      lr,
                                      info != NULL && info->si_addr != NULL ? (uintptr_t)info->si_addr : 0U);
    v3_debug_dump_machine_signal_frames(entries, count, pc, fp, sp, lr);
    mapped = v3_debug_dump_source_signal_frames(entries, count, pc, fp);
    if (mapped <= 0) {
        v3_debug_write_cstr("[cheng-v3] source_frames=0\n");
    }
}

static void v3_debug_dump_native_backtrace_if_enabled(void);

static void v3_debug_crash_trace_signal_handler(int sig, siginfo_t* info, void* signal_context) {
    if (cheng_crash_trace_handling) _exit(128 + sig);
    cheng_crash_trace_handling = 1;
    v3_debug_dump_source_signal_trace_if_available("signal", sig, info, signal_context);
    v3_debug_dump_async_crash_trace("signal", sig);
    v3_debug_dump_native_backtrace_if_enabled();
    _exit(128 + sig);
}

static void v3_debug_crash_trace_install_handlers(void) {
    if (__sync_lock_test_and_set(&cheng_crash_trace_installed, 1) != 0) return;
    cheng_v3_try_resolve_embedded_line_map_getters();
#if !defined(_WIN32)
    {
        struct sigaction sa;
        memset(&sa, 0, sizeof(sa));
        sa.sa_sigaction = v3_debug_crash_trace_signal_handler;
        sa.sa_flags = SA_SIGINFO;
        sigemptyset(&sa.sa_mask);
        sigaction(SIGSEGV, &sa, NULL);
        sigaction(SIGBUS, &sa, NULL);
        sigaction(SIGABRT, &sa, NULL);
        sigaction(SIGILL, &sa, NULL);
        sigaction(SIGTRAP, &sa, NULL);
        sigaction(SIGFPE, &sa, NULL);
#if defined(SIGSYS)
        sigaction(SIGSYS, &sa, NULL);
#endif
    }
#endif
}

CHENG_V3_DEBUG_WEAK int32_t cheng_crash_trace_enabled(void) {
    int32_t cached = cheng_crash_trace_enabled_cache;
    if (cached >= 0) return cached;
    cached = v3_debug_crash_trace_env_enabled() ? 1 : 0;
    cheng_crash_trace_enabled_cache = cached;
    if (cached) v3_debug_crash_trace_install_handlers();
    return cached;
}

CHENG_V3_DEBUG_WEAK void cheng_crash_trace_set_phase(const char* phase) {
    if (!cheng_crash_trace_enabled()) return;
    cheng_crash_trace_phase = phase;
}

CHENG_V3_DEBUG_WEAK void cheng_crash_trace_mark(const char* tag,
                                                int32_t a,
                                                int32_t b,
                                                int32_t c,
                                                int32_t d) {
    int32_t serial;
    ChengCrashTraceEntry* entry;
    if (!cheng_crash_trace_enabled()) return;
    serial = __sync_fetch_and_add(&cheng_crash_trace_next, 1);
    entry = &cheng_crash_trace_ring[(uint32_t)serial % CHENG_CRASH_TRACE_RING];
    entry->a = a;
    entry->b = b;
    entry->c = c;
    entry->d = d;
    __sync_synchronize();
    entry->tag = tag;
}

static void v3_debug_crash_trace_dump_now(const char* reason) {
    if (!cheng_crash_trace_enabled()) return;
    v3_debug_dump_async_crash_trace(reason, 0);
}

static int v3_debug_runtime_resolve_self_path(char* out, size_t out_cap) {
    char raw[PATH_MAX];
    char* resolved = NULL;
    if (out == NULL || out_cap == 0U) return 0;
    out[0] = '\0';
#if defined(__APPLE__)
    {
        uint32_t size = (uint32_t)sizeof(raw);
        if (_NSGetExecutablePath(raw, &size) == 0) {
            resolved = realpath(raw, out);
            if (resolved != NULL) return 1;
            if (strlen(raw) < out_cap) {
                snprintf(out, out_cap, "%s", raw);
                return 1;
            }
        }
    }
#elif !defined(_WIN32)
    {
        ssize_t n = readlink("/proc/self/exe", raw, sizeof(raw) - 1U);
        if (n > 0 && (size_t)n < sizeof(raw)) {
            raw[n] = '\0';
            resolved = realpath(raw, out);
            if (resolved != NULL) return 1;
            if ((size_t)n < out_cap) {
                snprintf(out, out_cap, "%s", raw);
                return 1;
            }
        }
    }
#endif
    return 0;
}

static int v3_debug_backtrace_enabled(void) {
    const char* raw = getenv("CHENG_PANIC_BACKTRACE");
    if (raw == NULL || raw[0] == '\0') raw = getenv("BACKEND_DEBUG_BACKTRACE");
    if (raw == NULL || raw[0] == '\0') return 1;
    if (raw[0] == '0' && raw[1] == '\0') return 0;
    return 1;
}

static void v3_debug_dump_native_backtrace_if_enabled(void) {
    if (!v3_debug_backtrace_enabled()) return;
#if CHENG_HAS_EXECINFO
    {
        void* frames[48];
        int count = backtrace(frames, 48);
        if (count > 0) {
            backtrace_symbols_fd(frames, count, 2);
            return;
        }
    }
#endif
    v3_debug_write_cstr("[cheng] backtrace unavailable\n");
}

static void v3_debug_dump_machine_backtrace_if_available(const char* reason) {
#if CHENG_HAS_EXECINFO
    void* frames[48];
    int count = backtrace(frames, 48);
    int i;
    const ChengV3EmbeddedLineMapEntry* embedded_entries = NULL;
    uint64_t embedded_count = 0U;
    (void)reason;
    cheng_v3_try_resolve_embedded_line_map_getters();
    if (cheng_v3_embedded_line_map_entries_getter != NULL &&
        cheng_v3_embedded_line_map_count_getter != NULL) {
        embedded_entries = cheng_v3_embedded_line_map_entries_getter();
        embedded_count = cheng_v3_embedded_line_map_count_getter();
    }
    for (i = 0; i < count; ++i) {
        uintptr_t pc = (uintptr_t)frames[i];
        uintptr_t symbol_pc = 0U;
        uintptr_t offset = 0U;
        const char* symbol_name = "?";
        const char* source_path = NULL;
        const char* function_name = NULL;
        int32_t line_start = 0;
        int32_t line_end = 0;
        const ChengV3EmbeddedLineMapEntry* embedded_entry =
            cheng_v3_embedded_line_map_find_pc_in_entries(embedded_entries, embedded_count, pc);
        Dl_info info;
        memset(&info, 0, sizeof(info));
        if (dladdr(frames[i], &info) != 0 && info.dli_sname != NULL) {
            symbol_name = info.dli_sname;
            symbol_pc = (uintptr_t)info.dli_saddr;
        }
        if (embedded_entry != NULL) {
            symbol_pc = (uintptr_t)embedded_entry->start_pc;
            function_name = embedded_entry->function_name;
            source_path = embedded_entry->source_path;
            line_start = cheng_v3_line_span_start(embedded_entry->signature_line,
                                                  embedded_entry->body_first_line);
            line_end = cheng_v3_line_span_end(line_start, embedded_entry->body_last_line);
        } else if (symbol_name != NULL && symbol_name[0] != '\0') {
            function_name = symbol_name;
        }
        if (symbol_pc != 0U && pc >= symbol_pc) {
            offset = pc - symbol_pc;
        }
        fprintf(stderr,
                "[cheng-v3] m#%d pc=0x%zx symbol=%s off=0x%zx",
                i,
                (size_t)pc,
                symbol_name != NULL && symbol_name[0] != '\0' ? symbol_name : "?",
                (size_t)offset);
        if (function_name != NULL && function_name[0] != '\0') {
            fprintf(stderr, " fn=%s", function_name);
        }
        if (source_path != NULL && source_path[0] != '\0') {
            fprintf(stderr, " src=%s:%d", source_path, line_start);
            if (line_end > line_start) {
                fprintf(stderr, "-%d", line_end);
            }
        }
        fprintf(stderr, "\n");
    }
    fflush(stderr);
#else
    (void)reason;
#endif
}

static void v3_debug_dump_source_backtrace_if_available(const char* reason) {
    int mapped = 0;
    (void)reason;
#if CHENG_HAS_EXECINFO
    {
        void* frames[48];
        int count = backtrace(frames, 48);
        int i;
        const ChengV3EmbeddedLineMapEntry* embedded_entries = NULL;
        uint64_t embedded_count = 0U;
        cheng_v3_try_resolve_embedded_line_map_getters();
        if (cheng_v3_embedded_line_map_entries_getter != NULL &&
            cheng_v3_embedded_line_map_count_getter != NULL) {
            embedded_entries = cheng_v3_embedded_line_map_entries_getter();
            embedded_count = cheng_v3_embedded_line_map_count_getter();
        }
        if (embedded_entries == NULL || embedded_count == 0U) {
            fprintf(stderr, "[cheng-v3] source_frames=0\n");
            fflush(stderr);
            return;
        }
        for (i = 0; i < count; ++i) {
            Dl_info info;
            const ChengV3EmbeddedLineMapEntry* embedded_entry;
            const char* function_name = NULL;
            const char* source_path = NULL;
            uintptr_t pc;
            uintptr_t symbol_pc;
            uintptr_t offset;
            int32_t line_start;
            int32_t line_end;
            pc = (uintptr_t)frames[i];
            symbol_pc = 0U;
            offset = 0U;
            memset(&info, 0, sizeof(info));
            if (dladdr(frames[i], &info) != 0 && info.dli_sname != NULL) {
                symbol_pc = (uintptr_t)info.dli_saddr;
            }
            embedded_entry =
                cheng_v3_embedded_line_map_find_pc_in_entries(embedded_entries, embedded_count, pc);
            if (embedded_entry == NULL) continue;
            symbol_pc = (uintptr_t)embedded_entry->start_pc;
            function_name = embedded_entry->function_name;
            source_path = embedded_entry->source_path;
            line_start = cheng_v3_line_span_start(embedded_entry->signature_line,
                                                  embedded_entry->body_first_line);
            line_end = cheng_v3_line_span_end(line_start, embedded_entry->body_last_line);
            offset = (symbol_pc != 0U && pc >= symbol_pc) ? (pc - symbol_pc) : 0U;
            fprintf(stderr,
                    "[cheng-v3] #%d %s %s:%d",
                    i,
                    function_name != NULL && function_name[0] != '\0' ?
                        function_name :
                        (info.dli_sname != NULL ? info.dli_sname : "?"),
                    source_path != NULL ? source_path : "?",
                    line_start);
            if (line_end > line_start) {
                fprintf(stderr, "-%d", line_end);
            }
            fprintf(stderr, " +0x%zx\n", (size_t)offset);
            mapped += 1;
        }
        if (mapped <= 0) {
            fprintf(stderr, "[cheng-v3] source_frames=0\n");
        }
        fflush(stderr);
    }
#else
    fprintf(stderr, "[cheng-v3] source_frames=unavailable\n");
    fflush(stderr);
#endif
}

static void v3_debug_dump_backtrace_now(const char* reason) {
    const char* actual_reason = reason != NULL && reason[0] != '\0' ? reason : "panic";
    uintptr_t pc = (uintptr_t)__builtin_return_address(0);
    uintptr_t fp = (uintptr_t)__builtin_frame_address(0);
    uintptr_t sp = (uintptr_t)&pc;
    uintptr_t lr = pc;
    v3_debug_crash_trace_dump_now(actual_reason);
    v3_debug_async_write_crash_header(actual_reason, "backtrace", 0, pc, fp, sp, lr, 0U);
    v3_debug_dump_machine_backtrace_if_available(actual_reason);
    v3_debug_dump_source_backtrace_if_available(actual_reason);
    v3_debug_dump_native_backtrace_if_enabled();
}

CHENG_V3_DEBUG_WEAK void cheng_dump_backtrace_if_enabled(void) {
    v3_debug_dump_backtrace_now("panic");
}

CHENG_V3_DEBUG_WEAK int32_t cheng_force_segv(void) {
#if defined(_WIN32)
    volatile int* p = (volatile int*)0;
    *p = 1;
#else
    raise(SIGSEGV);
#endif
    return 0;
}

static void v3_debug_path_mkdir_if_needed(const char* path) {
    if (path == NULL || path[0] == '\0' || v3_debug_dir_exists(path)) return;
    if (v3_debug_mkdir1(path) == 0) return;
    if (!v3_debug_dir_exists(path)) {
        fprintf(stderr, "[cheng] mkdir failed: %s\n", path);
        abort();
    }
}
