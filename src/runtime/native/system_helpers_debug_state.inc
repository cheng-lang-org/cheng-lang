#define CHENG_CRASH_TRACE_RING 128
#define CHENG_V3_PROFILE_MAX_SAMPLES 65536
#define CHENG_V3_PROFILE_MAX_DEPTH 8

typedef struct ChengCrashTraceEntry {
    const char* tag;
    int32_t a;
    int32_t b;
    int32_t c;
    int32_t d;
} ChengCrashTraceEntry;

typedef struct ChengV3EmbeddedLineMapEntry {
    const void* start_pc;
    const void* end_pc;
    const char* function_name;
    const char* source_path;
    int32_t signature_line;
    int32_t body_first_line;
    int32_t body_last_line;
    int32_t reserved;
} ChengV3EmbeddedLineMapEntry;

typedef const ChengV3EmbeddedLineMapEntry* (*ChengV3EmbeddedLineMapEntriesGetter)(void);
typedef uint64_t (*ChengV3EmbeddedLineMapCountGetter)(void);

typedef struct ChengV3ProfileSample {
    uint8_t depth;
    uintptr_t pcs[CHENG_V3_PROFILE_MAX_DEPTH];
} ChengV3ProfileSample;

static ChengCrashTraceEntry cheng_crash_trace_ring[CHENG_CRASH_TRACE_RING];
static volatile int32_t cheng_crash_trace_next = 0;
static volatile int32_t cheng_crash_trace_enabled_cache = -1;
static volatile int32_t cheng_crash_trace_installed = 0;
static volatile sig_atomic_t cheng_crash_trace_handling = 0;
static const char* cheng_crash_trace_phase = NULL;

static ChengV3EmbeddedLineMapEntriesGetter cheng_v3_embedded_line_map_entries_getter = NULL;
static ChengV3EmbeddedLineMapCountGetter cheng_v3_embedded_line_map_count_getter = NULL;
static volatile int32_t cheng_v3_embedded_line_map_getters_resolved = 0;

static ChengV3ProfileSample* cheng_v3_profile_samples = NULL;
static int32_t cheng_v3_profile_capacity = 0;
static volatile sig_atomic_t cheng_v3_profile_sample_count = 0;
static volatile sig_atomic_t cheng_v3_profile_dropped_samples = 0;
static volatile sig_atomic_t cheng_v3_profile_initialized = 0;
static volatile sig_atomic_t cheng_v3_profile_enabled_cache = -1;
static volatile sig_atomic_t cheng_v3_profile_installed = 0;
static char cheng_v3_profile_raw_out_path[PATH_MAX];
