/* macho_direct.h -- Self-contained ARM64 Mach-O executable writer.
 *
 * Writes a minimal but complete Mach-O with all required load commands.
 * Only external dependency: codesign for adhoc signing (macOS requirement).
 *
 * Usage:
 *   MachOWriter mw = {0};
 *   macho_init(&mw, code_words, code_bytes);
 *   macho_write_text(&mw, code_words);
 *   macho_finalize(&mw, output_path);
 */

#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <stdlib.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <mach/machine.h>

/* Mach-O constants */
#define MH_MAGIC_64   0xFEEDFACF
#define MH_OBJECT     1
#define MH_EXECUTE    2
#define LC_SEGMENT64  0x19
#define LC_MAIN       0x80000028
#define LC_DYLD_INFO  0x80000022
#define LC_SYMTAB     0x02
#define LC_DYSYMTAB   0x0B
#define LC_LOAD_DYLINKER 0x0E
#define LC_LOAD_DYLIB 0x0C
#define LC_FUNCTION_STARTS 0x26
#define LC_DATA_IN_CODE 0x29
#define LC_CODE_SIGNATURE 0x1D
#define PAGE_SIZE 16384

static int32_t macho_align_i32(int32_t value, int32_t align) {
    return ((value + align - 1) / align) * align;
}

typedef struct {
    uint8_t  *buf;
    int32_t   cap;
    int32_t   len;
    int32_t   ncmds;
    int32_t   sizeofcmds_pos;  /* offset of sizeofcmds field in header */
    int32_t   code_offset;     /* file offset where code will be written */
    int32_t   code_limit;      /* max code bytes available */
} MachOWriter;

/* Initialize: allocate buffer, write header, reserve space for load commands */
static bool macho_init(MachOWriter *mw, int32_t code_words, int32_t code_size) {
    (void)code_words;
    /* Layout: [header 32] [load commands] [padding to 728] [code] [padding to page] [LINKEDIT page] */
    int32_t text_file_size = macho_align_i32(728 + code_size, PAGE_SIZE);
    mw->cap = text_file_size + PAGE_SIZE;
    mw->buf = (uint8_t *)calloc(1, mw->cap);
    if (!mw->buf) return false;
    mw->len = 0;
    
    uint32_t *w = (uint32_t*)mw->buf;
    
    /* Header placeholder: fill after load commands are written */
    w[0] = MH_MAGIC_64;
    w[1] = CPU_TYPE_ARM64;
    w[2] = CPU_SUBTYPE_ARM64_ALL;
    w[3] = MH_EXECUTE;
    w[4] = 0;  /* ncmds: fill later */
    mw->sizeofcmds_pos = 5;
    w[5] = 0;  /* sizeofcmds: fill later */
    w[6] = 0x00200085;  /* NOUNDEFS | DYLDLINK | PIE */
    w[7] = 0;
    mw->len = 32;
    mw->ncmds = 0;
    
    /* Code will be written at offset 728 (matching cc output layout) */
    mw->code_offset = 728;
    mw->code_limit = code_size;
    
    return true;
}

/* Append a load command, return its offset */
static int32_t macho_append_cmd(MachOWriter *mw, int32_t cmd, int32_t cmdsize) {
    uint32_t *w = (uint32_t*)(mw->buf + mw->len);
    w[0] = cmd;
    w[1] = cmdsize;
    mw->len += cmdsize;
    mw->ncmds++;
    return mw->len - cmdsize;
}

/* Write segment name (16 bytes, null-padded) */
static void macho_segname(uint8_t *dst, const char *name) {
    int i; for (i = 0; i < 16 && name[i]; i++) dst[i] = name[i];
    for (; i < 16; i++) dst[i] = 0;
}

/* Add LC_SEGMENT_64 command header (72 bytes). nsects starts at 0, patched later. */
static int32_t macho_add_segment(MachOWriter *mw, const char *name,
                               uint64_t vmaddr, uint64_t vmsize,
                               uint64_t fileoff, uint64_t filesize,
                               int32_t maxprot, int32_t initprot,
                               int32_t flags) {
    /* Start with nsects=0, cmdsize=72. Will patch after sections added. */
    int32_t off = macho_append_cmd(mw, LC_SEGMENT64, 72);
    uint32_t *w = (uint32_t*)(mw->buf + off);
    macho_segname(mw->buf + off + 8, name);
    w[6] = (uint32_t)vmaddr;         w[7] = (uint32_t)(vmaddr >> 32);
    w[8] = (uint32_t)vmsize;         w[9] = (uint32_t)(vmsize >> 32);
    w[10] = (uint32_t)fileoff;       w[11] = (uint32_t)(fileoff >> 32);
    w[12] = (uint32_t)filesize;      w[13] = (uint32_t)(filesize >> 32);
    w[14] = maxprot; w[15] = initprot;
    w[16] = 0; /* nsects: patch after */ w[17] = flags;
    return off;
}

/* Patch a segment's nsects and cmdsize */
static void macho_patch_segment(MachOWriter *mw, int32_t seg_off, int32_t nsects) {
    uint32_t *w = (uint32_t*)(mw->buf + seg_off);
    w[1] = 72 + nsects * 80;  /* cmdsize */
    w[16] = nsects;            /* nsects */
}

/* Add section header at current position */
static void macho_add_section(MachOWriter *mw, const char *name, const char *segname,
                               uint64_t addr, uint64_t size, uint32_t offset,
                               int32_t align, uint32_t flags) {
    uint32_t *w = (uint32_t*)(mw->buf + mw->len);
    macho_segname(mw->buf + mw->len, name);
    macho_segname(mw->buf + mw->len + 16, segname);
    w[8] = (uint32_t)addr;           w[9] = (uint32_t)(addr >> 32);
    w[10] = (uint32_t)size;          w[11] = (uint32_t)(size >> 32);
    w[12] = offset;                  w[13] = align;
    w[14] = 0;                       w[15] = 0;
    w[16] = flags;                   w[17] = 0;
    w[18] = 0;                       w[19] = 0;
    mw->len += 80;
}

/* Write the ARM64 code at the pre-determined offset */
static void macho_write_text(MachOWriter *mw, const uint32_t *code, int32_t code_words) {
    int32_t code_size = code_words * 4;
    if (mw->code_offset + code_size <= mw->cap)
        memcpy(mw->buf + mw->code_offset, code, code_size);
}

/* Finalize: fill header fields, pad, codesign, write to file */
static bool macho_finalize(MachOWriter *mw, const char *path) {
    /* Fill header ncmds and sizeofcmds */
    int32_t cmds_start = 32;
    int32_t sizeofcmds = mw->len - cmds_start;
    uint32_t *h = (uint32_t*)mw->buf;
    h[4] = mw->ncmds;
    h[5] = sizeofcmds;
    
    /* Pad to page boundary and keep one LINKEDIT page for codesign. */
    int32_t total = ((mw->code_offset + mw->code_limit + PAGE_SIZE - 1) / PAGE_SIZE) * PAGE_SIZE;
    total += PAGE_SIZE;
    if (total > mw->cap) total = mw->cap;
    
    int fd = open(path, O_WRONLY | O_CREAT | O_TRUNC, 0755);
    if (fd < 0) { free(mw->buf); return false; }
    write(fd, mw->buf, total);
    close(fd);
    free(mw->buf);
    
    /* Adhoc sign (skip if COLD_NO_SIGN=1 for deterministic builds) */
    if (!getenv("COLD_NO_SIGN")) {
        char cmd[256]; snprintf(cmd, sizeof(cmd), "codesign --force -s - %s 2>/dev/null", path);
        system(cmd);
    }
    return true;
}

/* Convenience: write a complete Mach-O in one call */
static bool macho_write_exec(const char *path, const uint32_t *code, int32_t code_words) {
    MachOWriter mw = {0};
    int32_t code_sz = code_words * 4;
    if (!macho_init(&mw, code_words, code_sz)) return false;

    int32_t code_off = mw.code_offset;
    int32_t text_file_size = macho_align_i32(code_off + code_sz, PAGE_SIZE);
    uint64_t text_addr = 0x100000000ULL;

    /* PAGEZERO: 4GB guard page at vmaddr 0 */
    macho_add_segment(&mw, "__PAGEZERO", 0, 0x100000000ULL, 0, 0, 0, 0, 0);

    /* TEXT: contains __text section */
    int32_t text_seg = macho_add_segment(&mw, "__TEXT",
        text_addr, (uint64_t)text_file_size,
        0, (uint64_t)text_file_size,
        5, 5, 0);
    macho_add_section(&mw, "__text", "__TEXT",
        text_addr + code_off, code_sz, code_off,
        2, 0x80000400);
    macho_patch_segment(&mw, text_seg, 1);

    /* LINKEDIT: follows TEXT */
    uint64_t linkedit_addr = text_addr + (uint64_t)text_file_size;
    macho_add_segment(&mw, "__LINKEDIT",
        linkedit_addr, PAGE_SIZE,
        (uint64_t)text_file_size, PAGE_SIZE,
        1, 1, 0);
    
    /* DYLD_INFO_ONLY: minimal (no rebase/bind/export) */
    {
        int32_t off = macho_append_cmd(&mw, LC_DYLD_INFO, 48);
        uint32_t *w = (uint32_t*)(mw.buf + off);
        /* All zeros = no rebase, no bind, no weak, no lazy, no export */
        for (int i = 2; i < 12; i++) w[i] = 0;
    }
    
    /* SYMTAB: empty symbol table */
    {
        int32_t off = macho_append_cmd(&mw, LC_SYMTAB, 24);
        uint32_t *w = (uint32_t*)(mw.buf + off);
        w[2] = 0; w[3] = 0; /* symoff, nsyms */
        w[4] = 0; w[5] = 0; /* stroff, strsize */
    }
    
    /* DYSYMTAB: empty dynamic symbol table */
    {
        int32_t off = macho_append_cmd(&mw, LC_DYSYMTAB, 80);
        uint32_t *w = (uint32_t*)(mw.buf + off);
        for (int i = 2; i < 20; i++) w[i] = 0;
    }
    
    /* LOAD_DYLINKER: path to dyld */
    {
        int32_t off = macho_append_cmd(&mw, LC_LOAD_DYLINKER, 28);
        uint32_t *w = (uint32_t*)(mw.buf + off);
        w[2] = 12; /* offset to path string (right after cmdsize) */
        memcpy(mw.buf + off + 12, "/usr/lib/dyld\0", 14);
    }
    
    /* LOAD_DYLIB: libSystem */
    {
        int32_t off = macho_append_cmd(&mw, LC_LOAD_DYLIB, 56);
        uint32_t *w = (uint32_t*)(mw.buf + off);
        w[2] = 24; w[3] = 0x10001; /* timestamp, current_version */
        w[4] = 0x10001;              /* compatibility_version */
        memcpy(mw.buf + off + 24, "/usr/lib/libSystem.B.dylib\0", 28);
    }
    
    /* FUNCTION_STARTS: empty */
    {
        int32_t off = macho_append_cmd(&mw, LC_FUNCTION_STARTS, 16);
        uint32_t *w = (uint32_t*)(mw.buf + off);
        w[2] = 0; w[3] = 0; /* dataoff, datasize */
    }
    
    /* DATA_IN_CODE: empty */
    {
        int32_t off = macho_append_cmd(&mw, LC_DATA_IN_CODE, 16);
        uint32_t *w = (uint32_t*)(mw.buf + off);
        w[2] = 0; w[3] = 0;
    }
    
    /* LC_MAIN: entry point */
    {
        int32_t off = macho_append_cmd(&mw, LC_MAIN, 24);
        uint32_t *w = (uint32_t*)(mw.buf + off);
        w[2] = code_off; w[3] = 0;  /* entryoff */
        w[4] = 0; w[5] = 0;         /* stacksize */
    }
    
    macho_write_text(&mw, code, code_words);
    return macho_finalize(&mw, path);
}

/* -- Mach-O object file (.o) writer (MH_OBJECT) -- */

#define N_SECT 0x0E
#define N_EXT  0x01

typedef struct {
    uint32_t n_strx;
    uint8_t  n_type;
    uint8_t  n_sect;
    int16_t  n_desc;
    uint64_t n_value;
} nlist_64_t;

static bool macho_write_object(const char *path,
                               const uint32_t *code, int32_t code_words,
                               const char **names, const int32_t *offsets,
                               int32_t name_count, int32_t global_count) {
    int32_t code_sz = code_words * 4;
    if (global_count < 0) global_count = 0;
    if (global_count > name_count) global_count = name_count;

    /* Build string table first to know its size */
    char strtab[4096];
    int32_t str_off = 1; /* first byte is \0 */
    int32_t name_stroff[128];
    for (int32_t i = 0; i < name_count && i < 128; i++) {
        name_stroff[i] = str_off;
        int32_t nl = (int32_t)strlen(names[i]);
        if (str_off + nl + 2 < (int32_t)sizeof(strtab)) {
            strtab[str_off++] = '_';
            memcpy(strtab + str_off, names[i], nl);
            str_off += nl;
            strtab[str_off++] = '\0';
        }
    }
    strtab[str_off++] = '\0';

    /* Build nlist entries */
    nlist_64_t syms[128];
    int32_t nsyms = name_count > 128 ? 128 : name_count;
    for (int32_t i = 0; i < nsyms; i++) {
        syms[i].n_strx  = name_stroff[i];
        syms[i].n_type  = (uint8_t)((i < global_count) ? (N_SECT | N_EXT) : N_SECT);
        syms[i].n_sect  = 1; /* section 1 = __text */
        syms[i].n_desc  = 0;
        syms[i].n_value = (uint64_t)(offsets[i] * 4);
    }
    int32_t sym_size = nsyms * (int32_t)sizeof(nlist_64_t);

    /* Layout */
    int32_t hdr_sz = 32;
    int32_t seg_cmd_sz = 72 + 80;        /* LC_SEGMENT_64 with one section */
    int32_t build_ver_cmd_sz = 24;        /* LC_BUILD_VERSION */
    int32_t symtab_cmd_sz = 24;           /* LC_SYMTAB */
    int32_t cmd_sz = seg_cmd_sz + build_ver_cmd_sz + symtab_cmd_sz;
    int32_t ncmds = 3;
    int32_t code_off = hdr_sz + cmd_sz;
    int32_t sym_off = code_off + code_sz;
    int32_t str_off_file = sym_off + sym_size;
    int32_t total_sz = str_off_file + str_off;

    uint8_t *buf = (uint8_t *)calloc(1, total_sz);
    if (!buf) return false;
    uint32_t *w = (uint32_t *)buf;

    /* Header */
    w[0] = MH_MAGIC_64;
    w[1] = CPU_TYPE_ARM64;
    w[2] = CPU_SUBTYPE_ARM64_ALL;
    w[3] = MH_OBJECT;
    w[4] = ncmds;
    w[5] = cmd_sz;
    w[6] = 0;
    w[7] = 0;
    int32_t pos = hdr_sz;

    /* LC_SEGMENT_64 (72 bytes) + one section (80 bytes) */
    uint32_t *seg = (uint32_t *)(buf + pos);
    seg[0]  = LC_SEGMENT64;
    seg[1]  = seg_cmd_sz;
    macho_segname(buf + pos + 8, "__TEXT");
    seg[6]  = 0; seg[7]  = 0;
    seg[8]  = (uint32_t)code_sz; seg[9]  = 0;
    seg[10] = (uint32_t)code_off; seg[11] = 0;
    seg[12] = (uint32_t)code_sz; seg[13] = 0;
    seg[14] = 7; seg[15] = 7;
    seg[16] = 1; /* nsects */
    seg[17] = 0;
    /* Section header follows immediately after segment command */
    uint32_t *sec = (uint32_t *)(buf + pos + 72);
    macho_segname(buf + pos + 72, "__text");
    macho_segname(buf + pos + 88, "__TEXT");
    sec[8]  = 0; sec[9]  = 0;
    sec[10] = (uint32_t)code_sz; sec[11] = 0;
    sec[12] = (uint32_t)code_off;
    sec[13] = 2; /* align */
    sec[14] = 0; /* reloff */
    sec[15] = 0; /* nreloc */
    sec[16] = 0x80000400; /* flags: S_REGULAR | S_ATTR_SOME_INSTRUCTIONS */
    sec[17] = 0; /* reserved1 */
    sec[18] = 0; /* reserved2 */
    sec[19] = 0; /* reserved3 if present */
    pos += seg_cmd_sz;

    /* LC_BUILD_VERSION (24 bytes) */
    uint32_t *bv = (uint32_t *)(buf + pos);
    bv[0] = 0x32; /* LC_BUILD_VERSION */
    bv[1] = build_ver_cmd_sz;
    bv[2] = 1;    /* PLATFORM_MACOS */
    bv[3] = 0x000e0000; /* minos 14.0 */
    bv[4] = 0x000e0000; /* sdk 14.0 */
    bv[5] = 0;    /* ntools = 0 */
    pos += build_ver_cmd_sz;

    /* LC_SYMTAB (24 bytes) */
    uint32_t *st = (uint32_t *)(buf + pos);
    st[0] = LC_SYMTAB;
    st[1] = symtab_cmd_sz;
    st[2] = (uint32_t)sym_off;
    st[3] = nsyms;
    st[4] = (uint32_t)str_off_file;
    st[5] = (uint32_t)str_off;

    /* Code */
    memcpy(buf + code_off, code, code_sz);
    /* Symbol table */
    memcpy(buf + sym_off, syms, sym_size);
    /* String table */
    memcpy(buf + str_off_file, strtab, str_off);

    int fd = open(path, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (fd < 0) { free(buf); return false; }
    write(fd, buf, total_sz);
    close(fd);
    free(buf);
    return true;
}
