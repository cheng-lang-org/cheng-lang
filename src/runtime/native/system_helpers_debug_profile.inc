static int v3_debug_path_is_sep(char ch) {
    return ch == '/' || ch == '\\';
}

static void v3_debug_mkdir_all_raw(const char* path, int include_leaf) {
    size_t len = 0U;
    char* scratch = NULL;
    if (path == NULL || path[0] == '\0') return;
    len = strlen(path);
    scratch = (char*)malloc(len + 1U);
    if (scratch == NULL) {
        fprintf(stderr, "[cheng] mkdir scratch alloc failed\n");
        abort();
    }
    memcpy(scratch, path, len + 1U);
    for (size_t i = 0U; i < len; ++i) {
        char ch = scratch[i];
        if (!v3_debug_path_is_sep(ch)) continue;
        if (i == 0U) continue;
        scratch[i] = '\0';
        v3_debug_path_mkdir_if_needed(scratch);
        scratch[i] = ch;
    }
    if (include_leaf) {
        v3_debug_path_mkdir_if_needed(scratch);
    }
    free(scratch);
}

static void v3_debug_profile_ensure_parent_dir(const char* path) {
    char parent[PATH_MAX];
    size_t len;
    const char* slash;
    if (path == NULL || path[0] == '\0') return;
    slash = strrchr(path, '/');
    if (slash == NULL) return;
    len = (size_t)(slash - path);
    if (len == 0U || len >= sizeof(parent)) return;
    memcpy(parent, path, len);
    parent[len] = '\0';
    v3_debug_mkdir_all_raw(parent, 1);
}

static uint8_t v3_debug_profile_capture_frames(void* signal_context,
                                               uintptr_t* pcs,
                                               uint8_t pcs_cap) {
    uintptr_t current_pc = cheng_v3_signal_context_pc(signal_context);
    uintptr_t current_fp = cheng_v3_signal_context_fp(signal_context);
    uint8_t depth = 0U;
    if (pcs == NULL || pcs_cap == 0U || current_pc == 0U) return 0U;
    while (current_pc != 0U && depth < pcs_cap) {
        pcs[depth++] = current_pc;
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
#else
        break;
#endif
    }
    return depth;
}

static void v3_debug_profile_signal_handler(int sig, siginfo_t* info, void* signal_context) {
    int32_t index;
    (void)sig;
    (void)info;
    if (cheng_v3_profile_samples == NULL || cheng_v3_profile_capacity <= 0) return;
    index = __sync_fetch_and_add(&cheng_v3_profile_sample_count, 1);
    if (index < 0 || index >= cheng_v3_profile_capacity) {
        __sync_fetch_and_add(&cheng_v3_profile_dropped_samples, 1);
        return;
    }
    cheng_v3_profile_samples[index].depth =
        v3_debug_profile_capture_frames(signal_context,
                                        cheng_v3_profile_samples[index].pcs,
                                        CHENG_V3_PROFILE_MAX_DEPTH);
}

static void v3_debug_profile_labels_for_pc(uintptr_t pc,
                                           char* function_out,
                                           size_t function_cap,
                                           char* source_out,
                                           size_t source_cap) {
    const ChengV3EmbeddedLineMapEntry* embedded_entries = NULL;
    uint64_t embedded_count = 0U;
    const ChengV3EmbeddedLineMapEntry* embedded_entry = NULL;
    Dl_info info;
    memset(&info, 0, sizeof(info));
    if (function_cap > 0U) function_out[0] = '\0';
    if (source_cap > 0U) source_out[0] = '\0';
    cheng_v3_try_resolve_embedded_line_map_getters();
    if (cheng_v3_embedded_line_map_entries_getter != NULL &&
        cheng_v3_embedded_line_map_count_getter != NULL) {
        embedded_entries = cheng_v3_embedded_line_map_entries_getter();
        embedded_count = cheng_v3_embedded_line_map_count_getter();
        embedded_entry = cheng_v3_embedded_line_map_find_pc_in_entries(embedded_entries, embedded_count, pc);
    }
    if (embedded_entry != NULL) {
        snprintf(function_out,
                 function_cap,
                 "%s",
                 embedded_entry->function_name != NULL && embedded_entry->function_name[0] != '\0' ?
                     embedded_entry->function_name : "?");
        snprintf(source_out,
                 source_cap,
                 "%s:%d",
                 embedded_entry->source_path != NULL && embedded_entry->source_path[0] != '\0' ?
                     embedded_entry->source_path : "?",
                 cheng_v3_line_span_start(embedded_entry->signature_line,
                                          embedded_entry->body_first_line));
        return;
    }
    if (dladdr((void*)pc, &info) != 0 && info.dli_sname != NULL) {
        snprintf(function_out, function_cap, "%s", info.dli_sname);
        snprintf(source_out,
                 source_cap,
                 "%s",
                 info.dli_fname != NULL && info.dli_fname[0] != '\0' ? info.dli_fname : info.dli_sname);
        return;
    }
    snprintf(function_out, function_cap, "pc_%zx", (size_t)pc);
    snprintf(source_out, source_cap, "pc_%zx", (size_t)pc);
}

static void v3_debug_profile_stop_timer(void) {
#if !defined(_WIN32)
    struct itimerval timer;
    memset(&timer, 0, sizeof(timer));
    setitimer(ITIMER_REAL, &timer, NULL);
#endif
}

static void v3_debug_profile_write_raw_report(void) {
    FILE* file = NULL;
    int32_t sample_count = cheng_v3_profile_sample_count;
    int32_t i;
    if (cheng_v3_profile_raw_out_path[0] == '\0') return;
    if (sample_count < 0) sample_count = 0;
    if (sample_count > cheng_v3_profile_capacity) sample_count = cheng_v3_profile_capacity;
    v3_debug_profile_ensure_parent_dir(cheng_v3_profile_raw_out_path);
    file = fopen(cheng_v3_profile_raw_out_path, "wb");
    if (file == NULL) {
        return;
    }
    fprintf(file, "v3_profile_raw_v1\n");
    fprintf(file, "total_samples=%d\n", sample_count);
    fprintf(file, "dropped_samples=%d\n", (int)cheng_v3_profile_dropped_samples);
    for (i = 0; i < sample_count; i += 1) {
        char function_name[PATH_MAX];
        char source_name[PATH_MAX];
        uintptr_t pc;
        uint8_t depth;
        uint8_t j;
        if (cheng_v3_profile_samples == NULL || cheng_v3_profile_samples[i].depth == 0U) {
            continue;
        }
        pc = cheng_v3_profile_samples[i].pcs[0];
        depth = cheng_v3_profile_samples[i].depth;
        v3_debug_profile_labels_for_pc(pc,
                                       function_name,
                                       sizeof(function_name),
                                       source_name,
                                       sizeof(source_name));
        fprintf(file,
                "sample\t0x%zx\t%s\t%s\t",
                (size_t)pc,
                function_name[0] != '\0' ? function_name : "?",
                source_name[0] != '\0' ? source_name : "?");
        for (j = 0U; j < depth; j += 1U) {
            if (j > 0U) {
                fputc(',', file);
            }
            fprintf(file, "0x%zx", (size_t)cheng_v3_profile_samples[i].pcs[j]);
        }
        fputc('\n', file);
    }
    fclose(file);
}

static void v3_debug_profile_write_reports(void) {
    if (!v3_debug_profile_env_enabled()) return;
    if (cheng_v3_profile_raw_out_path[0] == '\0') {
        return;
    }
    v3_debug_profile_stop_timer();
    v3_debug_profile_write_raw_report();
}

static void v3_debug_profile_maybe_init(const char* exe_path) {
#if defined(_WIN32)
    (void)exe_path;
    cheng_v3_profile_enabled_cache = 0;
    return;
#else
    struct sigaction sa;
    struct itimerval timer;
    int32_t hz;
    const char* raw_out_env;
    if (cheng_v3_profile_initialized) return;
    if (!v3_debug_profile_env_enabled()) {
        cheng_v3_profile_enabled_cache = 0;
        return;
    }
#if !(defined(__APPLE__) && defined(__arm64__)) && !(defined(__linux__) && defined(__aarch64__))
    cheng_v3_profile_enabled_cache = 0;
    return;
#endif
    cheng_v3_profile_capacity = CHENG_V3_PROFILE_MAX_SAMPLES;
    cheng_v3_profile_samples = (ChengV3ProfileSample*)calloc((size_t)cheng_v3_profile_capacity,
                                                             sizeof(ChengV3ProfileSample));
    if (cheng_v3_profile_samples == NULL) {
        cheng_v3_profile_enabled_cache = 0;
        return;
    }
    raw_out_env = getenv("CHENG_V3_PROFILE_RAW_OUT");
    if (raw_out_env != NULL && raw_out_env[0] != '\0') {
        snprintf(cheng_v3_profile_raw_out_path, sizeof(cheng_v3_profile_raw_out_path), "%s", raw_out_env);
    } else if (exe_path != NULL && exe_path[0] != '\0') {
        snprintf(cheng_v3_profile_raw_out_path, sizeof(cheng_v3_profile_raw_out_path), "%s.v3.profile.raw.txt", exe_path);
    } else {
        snprintf(cheng_v3_profile_raw_out_path, sizeof(cheng_v3_profile_raw_out_path), "cheng_v3.profile.raw.txt");
    }
    if (__sync_lock_test_and_set(&cheng_v3_profile_installed, 1) == 0) {
        memset(&sa, 0, sizeof(sa));
        sa.sa_sigaction = v3_debug_profile_signal_handler;
        sa.sa_flags = SA_SIGINFO;
        sigemptyset(&sa.sa_mask);
        sigaction(SIGALRM, &sa, NULL);
    }
    hz = v3_debug_profile_hz_value();
    memset(&timer, 0, sizeof(timer));
    timer.it_interval.tv_sec = 0;
    timer.it_interval.tv_usec = 1000000 / hz;
    if (timer.it_interval.tv_usec <= 0) {
        timer.it_interval.tv_usec = 1000;
    }
    timer.it_value = timer.it_interval;
    setitimer(ITIMER_REAL, &timer, NULL);
    atexit(v3_debug_profile_write_reports);
    cheng_v3_profile_enabled_cache = 1;
    cheng_v3_profile_initialized = 1;
#endif
}
