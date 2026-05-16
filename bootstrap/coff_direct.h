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

/* ================================================================
 * Minimal PE/COFF executable (.exe) writer for x86_64 and ARM64.
 *
 * Writes a PE32+ executable with one .text section mapped at
 * IMAGE_BASE + section_rva.  No imports, no CRT, no relocations.
 * ================================================================ */

#define IMAGE_DOS_SIGNATURE     0x5A4D   /* "MZ" */
#define IMAGE_PE_SIGNATURE      0x00004550 /* "PE\0\0" */
#define IMAGE_FILE_EXECUTABLE   0x0002
#define IMAGE_FILE_LARGE_ADDRESS_AWARE 0x0020
#define IMAGE_FILE_LINE_NUMS_STRIPPED  0x0004
#define IMAGE_FILE_LOCAL_SYMS_STRIPPED 0x0008
#define IMAGE_FILE_32BIT_MACHINE       0x0100

#define IMAGE_SUBSYSTEM_WINDOWS_CUI    3  /* console */

/* PE32+ optional header (112 bytes for x86_64/ARM64) */
typedef struct {
    uint16_t magic;              /* 0x020B = PE32+ */
    uint8_t  major_linker;
    uint8_t  minor_linker;
    uint32_t size_of_code;
    uint32_t size_of_initialized_data;
    uint32_t size_of_uninitialized_data;
    uint32_t address_of_entry_point;
    uint32_t base_of_code;
    uint64_t image_base;
    uint32_t section_alignment;
    uint32_t file_alignment;
    uint16_t major_os_version;
    uint16_t minor_os_version;
    uint16_t major_image_version;
    uint16_t minor_image_version;
    uint16_t major_subsystem_version;
    uint16_t minor_subsystem_version;
    uint32_t win32_version_value;
    uint32_t size_of_image;
    uint32_t size_of_headers;
    uint32_t checksum;
    uint16_t subsystem;
    uint16_t dll_characteristics;
    uint64_t size_of_stack_reserve;
    uint64_t size_of_stack_commit;
    uint64_t size_of_heap_reserve;
    uint64_t size_of_heap_commit;
    uint32_t loader_flags;
    uint32_t number_of_rva_and_sizes;
    /* Data directories (16 entries x 8 bytes = 128 bytes) follow.
       For minimal exe all zeros. */
} __attribute__((packed)) Pe32PlusOptHdr;

static bool coff_write_exe(const char *path,
                           const uint32_t *code, int32_t code_words,
                           uint16_t machine) {
    int32_t code_sz = code_words * 4;

    /* Layout */
    int32_t dos_hdr_sz     = 64;   /* DOS header + stub */
    int32_t pe_sig_sz      = 4;    /* "PE\0\0" */
    int32_t coff_hdr_sz    = 20;   /* COFF file header */
    int32_t opt_hdr_sz     = 112;  /* PE32+ optional header */
    int32_t data_dir_sz    = 128;  /* 16 data directories (all zero) */
    int32_t sect_hdr_sz    = 40;   /* one .text section header */

    int32_t hdrs_sz  = dos_hdr_sz + pe_sig_sz + coff_hdr_sz + opt_hdr_sz + data_dir_sz + sect_hdr_sz;

    /* Align headers to file_alignment (512) */
    int32_t file_align = 512;
    int32_t hdrs_padded = ((hdrs_sz + file_align - 1) / file_align) * file_align;

    int32_t sect_align = 0x1000;   /* 4KB section alignment */
    uint64_t image_base = 0x140000000ULL; /* default PE image base */
    int32_t code_rva  = sect_align;       /* .text starts at first section-aligned offset */

    int32_t total_sz  = hdrs_padded + code_sz;
    int32_t image_sz  = code_rva + ((code_sz + sect_align - 1) / sect_align) * sect_align;

    uint8_t *buf = (uint8_t *)calloc(1, (size_t)total_sz);
    if (!buf) return false;

    /* ---- DOS Header (64 bytes) ---- */
    {
        uint16_t *d16 = (uint16_t *)buf;
        d16[0] = (uint16_t)IMAGE_DOS_SIGNATURE;      /* e_magic = "MZ" */
        /* bytes at offset 2-57: DOS stub (keep zero) */
        /* e_lfanew at offset 60 (uint32_t) points to PE signature */
        uint32_t *d32 = (uint32_t *)(buf + 60);
        d32[0] = (uint32_t)dos_hdr_sz;
    }

    /* ---- PE Signature (4 bytes) ---- */
    uint32_t *pe_sig = (uint32_t *)(buf + dos_hdr_sz);
    pe_sig[0] = IMAGE_PE_SIGNATURE;

    /* ---- COFF File Header (20 bytes) ---- */
    {
        uint8_t *coff = buf + dos_hdr_sz + pe_sig_sz;
        uint16_t *c16 = (uint16_t *)coff;
        c16[0] = machine;                    /* Machine */
        c16[1] = 1;                          /* NumberOfSections */
        /* TimeDateStamp (4 bytes at off+4) = 0 */
        /* PointerToSymbolTable (4 bytes at off+8) = 0 */
        /* NumberOfSymbols (4 bytes at off+12) = 0 */
        c16[2] = (uint16_t)opt_hdr_sz;       /* SizeOfOptionalHeader */
        uint16_t chars = IMAGE_FILE_EXECUTABLE;
        if (machine != IMAGE_FILE_MACHINE_ARM64) {
            /* x86_64: large-address-aware, no 32-bit flag */
            chars |= IMAGE_FILE_LARGE_ADDRESS_AWARE;
        }
        chars |= IMAGE_FILE_LINE_NUMS_STRIPPED | IMAGE_FILE_LOCAL_SYMS_STRIPPED;
        c16[3] = chars;                      /* Characteristics */
    }

    /* ---- PE32+ Optional Header (112 bytes) ---- */
    {
        uint8_t *opt = buf + dos_hdr_sz + pe_sig_sz + coff_hdr_sz;
        uint16_t *o16 = (uint16_t *)opt;
        o16[0] = 0x020B;                     /* Magic = PE32+ */
        /* major/minor linker = 0 */
        uint32_t *o32 = (uint32_t *)(opt + 4);
        o32[0] = (uint32_t)code_sz;          /* SizeOfCode */
        /* SizeOfInitializedData = code_sz (code is the only data) */
        o32[1] = (uint32_t)code_sz;
        /* SizeOfUninitializedData = 0 */
        o32[2] = 0;
        o32[3] = (uint32_t)code_rva;         /* AddressOfEntryPoint */
        o32[4] = (uint32_t)code_rva;         /* BaseOfCode */
        uint64_t *o64 = (uint64_t *)(opt + 24);
        o64[0] = image_base;                 /* ImageBase */
        o32 = (uint32_t *)(opt + 32);
        o32[0] = (uint32_t)sect_align;       /* SectionAlignment */
        o32[1] = (uint32_t)file_align;       /* FileAlignment */
        /* OS/Image/Subsystem versions all 0 */
        o32 = (uint32_t *)(opt + 56);
        o32[0] = (uint32_t)image_sz;         /* SizeOfImage */
        o32[1] = (uint32_t)hdrs_padded;      /* SizeOfHeaders */
        /* Checksum = 0 */
        uint16_t *opt16 = (uint16_t *)(opt + 68);
        opt16[0] = IMAGE_SUBSYSTEM_WINDOWS_CUI; /* Subsystem = console */
        /* DLL characteristics = 0 (NX compat not needed for minimal) */
        o64 = (uint64_t *)(opt + 72);
        o64[0] = 0x100000ULL;                /* SizeOfStackReserve = 1MB */
        o64[1] = 0x1000ULL;                  /* SizeOfStackCommit = 4KB */
        o64[2] = 0x100000ULL;                /* SizeOfHeapReserve = 1MB */
        o64[3] = 0x1000ULL;                  /* SizeOfHeapCommit = 4KB */
        /* LoaderFlags = 0 */
        o32 = (uint32_t *)(opt + 108);
        o32[0] = 16;                         /* NumberOfRvaAndSizes */
    }
    /* Data directories (128 bytes) follow – all zeros */

    /* ---- Section Header (.text, 40 bytes) ---- */
    {
        uint8_t *sect = buf + dos_hdr_sz + pe_sig_sz + coff_hdr_sz + opt_hdr_sz + data_dir_sz;
        memset(sect, 0, 40);
        memcpy(sect, ".text", 5);            /* Name */
        uint32_t *s32 = (uint32_t *)(sect + 8);
        s32[0] = (uint32_t)code_sz;          /* VirtualSize */
        s32[1] = (uint32_t)code_rva;         /* VirtualAddress */
        s32[2] = (uint32_t)code_sz;          /* SizeOfRawData */
        s32[3] = (uint32_t)hdrs_padded;      /* PointerToRawData */
        /* PointerToRelocations = 0 */
        /* PointerToLinenumbers = 0 */
        /* NumberOfRelocations = 0, NumberOfLinenumbers = 0 */
        s32 = (uint32_t *)(sect + 36);
        s32[0] = 0x60000020;                 /* Characteristics: TEXT, CODE, EXECUTE, READ */
    }

    /* ---- Code ---- */
    memcpy(buf + hdrs_padded, code, (size_t)code_sz);

    int fd = open(path, O_WRONLY | O_CREAT | O_TRUNC, 0755);
    if (fd < 0) { free(buf); return false; }
    write(fd, buf, (size_t)total_sz);
    close(fd);
    free(buf);
    return true;
}