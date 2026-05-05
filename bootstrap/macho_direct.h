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
    /* Layout: [header 32] [load commands] [padding to 728] [code] [padding to page] */
    /* Reserve 16KB for header+commands+code (more than enough) */
    mw->cap = PAGE_SIZE * 2;
    mw->buf = calloc(1, mw->cap);
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
    
    /* Pad to page boundary, plus extra page for code signature */
    int32_t total = ((mw->code_offset + mw->code_limit + PAGE_SIZE - 1) / PAGE_SIZE) * PAGE_SIZE;
    total += PAGE_SIZE; /* extra page for codesign */
    if (total > mw->cap) total = mw->cap;
    
    int fd = open(path, O_WRONLY | O_CREAT | O_TRUNC, 0755);
    if (fd < 0) { free(mw->buf); return false; }
    write(fd, mw->buf, total);
    close(fd);
    free(mw->buf);
    
    /* Adhoc sign */
    char cmd[256]; snprintf(cmd, sizeof(cmd), "codesign -s - %s 2>/dev/null", path);
    system(cmd);
    return true;
}

/* Convenience: write a complete Mach-O in one call */
static bool macho_write_exec(const char *path, const uint32_t *code, int32_t code_words) {
    MachOWriter mw = {0};
    int32_t code_sz = code_words * 4;
    if (!macho_init(&mw, code_words, code_sz)) return false;

    int32_t code_off = mw.code_offset;
    uint64_t text_addr = 0x100000000ULL;

    /* PAGEZERO: 4GB guard page at vmaddr 0 */
    macho_add_segment(&mw, "__PAGEZERO", 0, 0x100000000ULL, 0, 0, 0, 0, 0);

    /* TEXT: contains __text section */
    int32_t text_seg = macho_add_segment(&mw, "__TEXT",
        text_addr, PAGE_SIZE,
        0, PAGE_SIZE,
        5, 5, 0);
    macho_add_section(&mw, "__text", "__TEXT",
        text_addr + code_off, code_sz, code_off,
        2, 0x80000400);
    macho_patch_segment(&mw, text_seg, 1);

    /* LINKEDIT: follows TEXT */
    uint64_t linkedit_addr = text_addr + PAGE_SIZE;
    macho_add_segment(&mw, "__LINKEDIT",
        linkedit_addr, PAGE_SIZE,
        PAGE_SIZE, 0,
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
    
    /* CODE_SIGNATURE: placeholder (filled by codesign) */
    {
        int32_t off = macho_append_cmd(&mw, LC_CODE_SIGNATURE, 16);
        uint32_t *w = (uint32_t*)(mw.buf + off);
        w[2] = PAGE_SIZE;  /* dataoff: at page boundary */
        w[3] = PAGE_SIZE;  /* datasize: one page for signature */
    }
    
    macho_write_text(&mw, code, code_words);
    return macho_finalize(&mw, path);
}
