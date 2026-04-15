static int v3_debug_flag_enabled(const char* raw) {
    if (raw == NULL || raw[0] == '\0') return 0;
    if (raw[0] == '0' && raw[1] == '\0') return 0;
    return 1;
}

static bool v3_debug_probably_valid_ptr(const void* p) {
    uintptr_t raw = (uintptr_t)p;
    if (p == NULL) return false;
    if (raw < (uintptr_t)65536) return false;
    if ((raw & (uintptr_t)(sizeof(void*) - 1U)) != 0U) return false;
    return true;
}

static int v3_debug_dir_exists(const char* path) {
    struct stat st;
    if (path == NULL || path[0] == '\0') return 0;
    if (stat(path, &st) != 0) return 0;
    return S_ISDIR(st.st_mode) ? 1 : 0;
}

static int v3_debug_mkdir1(const char* path) {
    if (path == NULL || path[0] == '\0') return -1;
    return mkdir(path, 0755);
}

static int v3_debug_profile_env_enabled(void) {
    return v3_debug_flag_enabled(getenv("CHENG_V3_PROFILE_ENABLE"));
}

static int32_t v3_debug_profile_hz_value(void) {
    const char* raw = getenv("CHENG_V3_PROFILE_HZ");
    char* end = NULL;
    long value = 200;
    if (raw != NULL && raw[0] != '\0') {
        value = strtol(raw, &end, 10);
        if (end == raw) value = 200;
    }
    if (value < 10) value = 10;
    if (value > 2000) value = 2000;
    return (int32_t)value;
}

static int v3_debug_crash_trace_env_enabled(void) {
    const char* raw = getenv("CHENG_CRASH_TRACE");
    if (raw != NULL && raw[0] != '\0') return v3_debug_flag_enabled(raw);
    raw = getenv("BACKEND_CRASH_TRACE");
    if (raw != NULL && raw[0] != '\0') return v3_debug_flag_enabled(raw);
    raw = getenv("STAGE1_CRASH_TRACE");
    if (raw != NULL && raw[0] != '\0') return v3_debug_flag_enabled(raw);
    return 1;
}

static size_t v3_debug_cstrnlen(const char* s, size_t limit) {
    size_t n = 0U;
    if (s == NULL) return 0U;
    while (n < limit && s[n] != '\0') n += 1U;
    return n;
}

static void v3_debug_write_buf(const char* buf, size_t len) {
    size_t off = 0U;
    while (off < len) {
        ssize_t wrote = write(2, buf + off, len - off);
        if (wrote <= 0) return;
        off += (size_t)wrote;
    }
}

static void v3_debug_write_cstr(const char* s) {
    const char* text = s != NULL ? s : "?";
    v3_debug_write_buf(text, v3_debug_cstrnlen(text, 160U));
}

static void v3_debug_write_i32(int32_t value) {
    char buf[24];
    char* out = buf + sizeof(buf);
    uint32_t mag;
    *--out = '\0';
    if (value < 0) {
        mag = (uint32_t)(-(value + 1)) + 1U;
    } else {
        mag = (uint32_t)value;
    }
    do {
        *--out = (char)('0' + (mag % 10U));
        mag /= 10U;
    } while (mag != 0U);
    if (value < 0) {
        *--out = '-';
    }
    v3_debug_write_cstr(out);
}

static void v3_debug_write_hex_uintptr(uintptr_t value) {
    static const char hex[] = "0123456789abcdef";
    char buf[2 + sizeof(uintptr_t) * 2 + 1];
    size_t digits = sizeof(uintptr_t) * 2U;
    size_t i;
    buf[0] = '0';
    buf[1] = 'x';
    for (i = 0U; i < digits; ++i) {
        unsigned shift = (unsigned)((digits - 1U - i) * 4U);
        buf[2U + i] = hex[(value >> shift) & 0xfU];
    }
    buf[2U + digits] = '\0';
    v3_debug_write_buf(buf, 2U + digits);
}

static void cheng_v3_try_resolve_embedded_line_map_getters(void) {
    if (__sync_lock_test_and_set(&cheng_v3_embedded_line_map_getters_resolved, 1) != 0) {
        return;
    }
    dlerror();
    cheng_v3_embedded_line_map_entries_getter =
        (ChengV3EmbeddedLineMapEntriesGetter)dlsym(RTLD_DEFAULT, "cheng_v3_embedded_line_map_entries_get");
    dlerror();
    cheng_v3_embedded_line_map_count_getter =
        (ChengV3EmbeddedLineMapCountGetter)dlsym(RTLD_DEFAULT, "cheng_v3_embedded_line_map_count_get");
}
