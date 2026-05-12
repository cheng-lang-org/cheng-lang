/* coff_direct.h -- Minimal COFF/PE object file (.obj) writer for Windows x86_64.
 *
 * Writes a relocatable COFF object file for x86_64-pc-windows-msvc.
 * Symbols + relocations for external linkage.
 */

#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <stdlib.h>

#define IMAGE_FILE_MACHINE_AMD64      0x8664
#define IMAGE_FILE_MACHINE_ARM64      0xAA64

#define IMAGE_REL_AMD64_REL32         0x0004
#define IMAGE_REL_AMD64_ADDR64        0x0001
#define IMAGE_REL_ARM64_BRANCH26      0x0003

/* COFF symbol table entry (18 bytes) */
typedef struct {
    char     name[8];         /* 8-byte name (or offset into string table) */
    uint32_t value;
    int16_t  section_number;
    uint16_t type;
    uint8_t  storage_class;
    uint8_t  num_aux;
} CoffSym;

/* COFF relocation entry (10 bytes) */
typedef struct {
    uint32_t virtual_address;
    uint32_t symbol_table_index;
    uint16_t type;
} CoffReloc;

static bool coff_write_object(const char *path,
                              const uint32_t *code, int32_t code_words,
                              const char **names, const int32_t *offsets,
                              int32_t name_count,
                              int32_t local_count,
                              const int32_t *reloc_offsets,
                              const int32_t *reloc_symbols,
                              int32_t reloc_count,
                              uint16_t machine) {
    int32_t code_sz = code_words * 4;

    /* Build string table for long symbol names (> 8 chars) */
    char strtab[65536];
    int32_t str_off = 4; /* first 4 bytes = size of string table */
    int32_t name_stroff[512];
    bool use_strtab[512];
    for (int32_t i = 0; i < name_count && i < 512; i++) {
        int32_t nl = (int32_t)strlen(names[i]);
        if (nl <= 8) {
            /* Short name fits in CoffSym.name */
            name_stroff[i] = 0;
            use_strtab[i] = false;
        } else {
            /* Long name: store in string table */
            name_stroff[i] = str_off;
            use_strtab[i] = true;
            if (str_off + nl + 1 < (int32_t)sizeof(strtab)) {
                memcpy(strtab + str_off, names[i], nl);
                str_off += nl;
                strtab[str_off++] = '\0';
            }
        }
    }

    /* Build symbol table */
    int32_t nsyms = name_count;
    CoffSym *syms = (CoffSym *)calloc(nsyms, sizeof(CoffSym));
    for (int32_t i = 0; i < name_count; i++) {
        int32_t nl = (int32_t)strlen(names[i]);
        if (use_strtab[i]) {
            /* First 4 bytes are zeros, next 4 bytes = offset into string table */
            syms[i].name[0] = 0;
            syms[i].name[1] = 0;
            syms[i].name[2] = 0;
            syms[i].name[3] = 0;
            uint32_t off = (uint32_t)name_stroff[i];
            memcpy(&syms[i].name[4], &off, 4);
        } else {
            memset(syms[i].name, 0, 8);
            memcpy(syms[i].name, names[i], nl < 8 ? nl : 8);
        }
        syms[i].value          = (offsets[i] >= 0) ? (uint32_t)(offsets[i] * 4) : 0;
        syms[i].section_number = (offsets[i] >= 0) ? 1 : 0; /* section 1 or UNDEF */
        syms[i].type           = 0x20; /* function */
        syms[i].storage_class  = (offsets[i] >= 0 && i >= local_count) ? 2 : 2; /* external */
        syms[i].num_aux        = 0;
    }
    int32_t sym_size = nsyms * (int32_t)sizeof(CoffSym);

    /* Build relocation table */
    CoffReloc *relocs = (CoffReloc *)calloc(reloc_count > 0 ? reloc_count : 1, sizeof(CoffReloc));
    for (int32_t i = 0; i < reloc_count; i++) {
        relocs[i].virtual_address   = (uint32_t)reloc_offsets[i];
        relocs[i].symbol_table_index = (uint32_t)reloc_symbols[i];
        relocs[i].type              = (machine == IMAGE_FILE_MACHINE_ARM64) ?
                                        IMAGE_REL_ARM64_BRANCH26 : IMAGE_REL_AMD64_REL32;
    }
    int32_t reloc_size = reloc_count * (int32_t)sizeof(CoffReloc);

    /* COFF layout */
    int32_t hdr_sz    = 20;  /* COFF file header */
    int32_t sect_sz   = 40;  /* one section header */
    int32_t code_off  = hdr_sz + sect_sz;
    int32_t reloc_off = code_off + code_sz;
    int32_t sym_off   = reloc_off + reloc_size;
    int32_t str_off_file = sym_off + sym_size;
    int32_t total_sz  = str_off_file + str_off;

    uint8_t *buf = (uint8_t *)calloc(1, total_sz);
    if (!buf) { free(syms); free(relocs); return false; }
    uint32_t *w = (uint32_t *)buf;

    /* COFF File Header */
    w[0] = machine;            /* Machine */
    w[1] = 1;                  /* NumberOfSections */
    w[2] = 0;                  /* TimeDateStamp */
    w[3] = sym_off;            /* PointerToSymbolTable */
    w[4] = nsyms;              /* NumberOfSymbols */

    /* Optional header size: 0 for object files */
    uint16_t *h16 = (uint16_t *)(buf + 16);
    h16[0] = 0;                /* SizeOfOptionalHeader */
    uint16_t *flags = (uint16_t *)(buf + 18);
    flags[0] = 0;              /* Characteristics */

    /* Section Header */
    uint8_t *sect = buf + hdr_sz;
    memset(sect, 0, 40);
    memcpy(sect, ".text", 5);  /* Name */
    /* PhysicalAddress / VirtualSize */
    /* VirtualAddress = 0 */
    uint32_t *s32 = (uint32_t *)(sect + 8);
    s32[0] = (uint32_t)code_sz;    /* SizeOfRawData */
    s32[1] = (uint32_t)code_off;    /* PointerToRawData */
    s32[2] = (uint32_t)reloc_off;   /* PointerToRelocations */
    s32[3] = 0;                     /* PointerToLinenumbers */
    uint16_t *s16 = (uint16_t *)(sect + 32);
    s16[0] = (uint16_t)reloc_count; /* NumberOfRelocations */
    s16[1] = 0;                     /* NumberOfLinenumbers */
    s32 = (uint32_t *)(sect + 36);
    s32[0] = 0x60500020;            /* Characteristics: TEXT, CODE, EXECUTE, READ, ALIGN_16 */

    /* Code */
    memcpy(buf + code_off, code, code_sz);
    /* Relocations */
    memcpy(buf + reloc_off, relocs, reloc_size);
    /* Symbol table */
    memcpy(buf + sym_off, syms, sym_size);
    /* String table */
    memcpy(buf + str_off_file, strtab, 4); /* first 4 bytes = total size */
    memcpy(buf + str_off_file + 4, strtab + 4, str_off - 4);

    free(syms);
    free(relocs);

    int fd = open(path, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (fd < 0) { free(buf); return false; }
    write(fd, buf, total_sz);
    close(fd);
    free(buf);
    return true;
}
