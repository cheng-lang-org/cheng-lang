/* elf64_direct.h -- Self-contained ELF64 object file (.o) writer.
 *
 * Writes a minimal but complete ELF64 relocatable object for aarch64
 * or x86_64 linux targets.  Symbols + relocations for external linkage.
 *
 * Usage:
 *   elf64_write_object(path, code, code_words, names, offsets,
 *                      name_count, local_count,
 *                      reloc_offsets, reloc_symbols, reloc_count,
 *                      machine);
 */

#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <stdlib.h>
#include <sys/mman.h>

/* ELF64 constants */
#define ELF64_MAGIC      0x464C457F  /* "\x7FELF" */
#define ELFCLASS64       2
#define ELFDATA2LSB      1
#define EV_CURRENT       1
#define ELFOSABI_SYSV    0
#define ET_REL           1
#define EM_AARCH64       183
#define EM_X86_64        62

#define SHT_PROGBITS     1
#define SHT_SYMTAB       2
#define SHT_STRTAB       3
#define SHT_RELA         4
#define SHT_NOBITS       8

#define SHF_WRITE        0x1
#define SHF_ALLOC        0x2
#define SHF_EXECINSTR    0x4
#define SHF_INFO_LINK    0x40

#define STB_LOCAL        0
#define STB_GLOBAL       1
#define STT_NOTYPE       0
#define STT_FUNC         2
#define ELF64_ST_INFO(b,t) (((b) << 4) + ((t) & 0x0F))

/* ARM64 relocations */
#define R_AARCH64_CALL26     283
#define R_AARCH64_ADR_PREL_PG_HI21  275

/* x86_64 relocations */
#define R_X86_64_PLT32       4
#define R_X86_64_PC32        2

typedef struct {
    uint32_t sh_name;
    uint32_t sh_type;
    uint64_t sh_flags;
    uint64_t sh_addr;
    uint64_t sh_offset;
    uint64_t sh_size;
    uint32_t sh_link;
    uint32_t sh_info;
    uint64_t sh_addralign;
    uint64_t sh_entsize;
} Elf64_Shdr;

typedef struct {
    uint32_t st_name;
    uint8_t  st_info;
    uint8_t  st_other;
    uint16_t st_shndx;
    uint64_t st_value;
    uint64_t st_size;
} Elf64_Sym;

typedef struct {
    uint64_t r_offset;
    uint64_t r_info;
    int64_t  r_addend;
} Elf64_Rela;

static bool elf64_write_object(const char *path,
                               const uint32_t *code, int32_t code_words,
                               const char **names, const int32_t *offsets,
                               int32_t name_count,
                               int32_t local_count,
                               const int32_t *reloc_offsets,
                               const int32_t *reloc_symbols,
                               int32_t reloc_count,
                               uint16_t machine) {
    int32_t code_sz = code_words * 4;

    /* Build string table */
    char strtab[65536];
    int32_t str_off = 1; /* first byte is \0 */
    int32_t name_stroff[512];
    for (int32_t i = 0; i < name_count && i < 512; i++) {
        name_stroff[i] = str_off;
        int32_t nl = (int32_t)strlen(names[i]);
        if (str_off + nl + 1 < (int32_t)sizeof(strtab)) {
            memcpy(strtab + str_off, names[i], nl);
            str_off += nl;
            strtab[str_off++] = '\0';
        }
    }
    /* Build section name string table */
    char shstrtab[128];
    int32_t shstr_off = 1; /* first byte is \0 */
    int32_t shstr_text = shstr_off;
    memcpy(shstrtab + shstr_off, ".text", 6); shstr_off += 6;
    int32_t shstr_rela = shstr_off;
    memcpy(shstrtab + shstr_off, ".rela.text", 10); shstr_off += 10;
    int32_t shstr_symtab = shstr_off;
    memcpy(shstrtab + shstr_off, ".symtab", 8); shstr_off += 8;
    int32_t shstr_strtab = shstr_off;
    memcpy(shstrtab + shstr_off, ".strtab", 8); shstr_off += 8;
    int32_t shstr_shstrtab = shstr_off;
    memcpy(shstrtab + shstr_off, ".shstrtab", 10); shstr_off += 10;

    /* Build symbol table */
    int32_t nsyms = 1 + name_count; /* NULL symbol + defined + undefined */
    Elf64_Sym *syms = (Elf64_Sym *)calloc(nsyms, sizeof(Elf64_Sym));
    /* sym[0] is the NULL symbol */
    int32_t si = 1;
    for (int32_t i = 0; i < name_count; i++) {
        syms[si].st_name  = name_stroff[i];
        syms[si].st_other = 0;
        syms[si].st_size  = 0;
        if (offsets[i] < 0) {
            /* Undefined external symbol */
            syms[si].st_info  = ELF64_ST_INFO(STB_GLOBAL, STT_NOTYPE);
            syms[si].st_shndx = 0;
            syms[si].st_value = 0;
        } else {
            syms[si].st_info  = (i < local_count) ?
                ELF64_ST_INFO(STB_LOCAL, STT_FUNC) :
                ELF64_ST_INFO(STB_GLOBAL, STT_FUNC);
            syms[si].st_shndx = 1; /* .text */
            syms[si].st_value = (uint64_t)(offsets[i] * 4);
        }
        si++;
    }
    int32_t sym_size = nsyms * (int32_t)sizeof(Elf64_Sym);

    /* Build relocation table */
    int32_t nreloc = reloc_count > 0 ? reloc_count : 0;
    Elf64_Rela *relas = (Elf64_Rela *)calloc(nreloc, sizeof(Elf64_Rela));
    for (int32_t i = 0; i < nreloc; i++) {
        relas[i].r_offset = (uint64_t)reloc_offsets[i];
        uint32_t sym = (uint32_t)(reloc_symbols[i] + 1); /* +1 for NULL sym[0] */
        uint32_t type = (machine == EM_AARCH64) ? R_AARCH64_CALL26 : R_X86_64_PLT32;
        relas[i].r_info = ((uint64_t)type << 32) | (uint64_t)sym;
        relas[i].r_addend = 0;
    }
    int32_t rela_size = nreloc * (int32_t)sizeof(Elf64_Rela);

    /* Section count: NULL + .text + .rela.text + .symtab + .strtab + .shstrtab */
    int32_t shnum = 6;
    int32_t text_idx = 1, rela_idx = 2, symtab_idx = 3, strtab_idx = 4, shstrtab_idx = 5;

    /* Layout */
    int32_t hdr_sz = 64;
    int32_t shdr_sz = shnum * (int32_t)sizeof(Elf64_Shdr);
    int32_t code_off = hdr_sz + shdr_sz;
    int32_t rela_off = code_off + code_sz;
    int32_t sym_off = rela_off + rela_size;
    int32_t str_off_file = sym_off + sym_size;
    int32_t shstr_off_file = str_off_file + str_off;
    int32_t total_sz = shstr_off_file + shstr_off;

    uint8_t *buf = (uint8_t *)calloc(1, total_sz);
    if (!buf) return false;
    uint32_t *w = (uint32_t *)buf;

    /* ELF Header */
    w[0] = ELF64_MAGIC;  /* e_ident: magic */
    buf[4] = ELFCLASS64;  /* 64-bit */
    buf[5] = ELFDATA2LSB; /* little-endian */
    buf[6] = EV_CURRENT;  /* version */
    buf[7] = ELFOSABI_SYSV; /* OS/ABI */
    /* padding bytes 8-15 are zero */
    w[4] = ET_REL;        /* e_type */
    w[5] = machine;       /* e_machine */
    w[6] = 1;             /* e_version */
    w[7] = 0;             /* e_entry */
    w[8] = 0;             /* e_entry high */
    w[9] = 0;             /* e_phoff */
    w[10] = 0;            /* e_phoff high */
    w[11] = (uint32_t)(hdr_sz + shdr_sz); /* e_shoff: after header */
    w[12] = 0;            /* e_shoff high */
    w[13] = 0;            /* e_flags */
    w[14] = hdr_sz;       /* e_ehsize */
    w[15] = 0;            /* e_phentsize */
    w[16] = 0;            /* e_phnum */
    w[17] = (uint32_t)sizeof(Elf64_Shdr); /* e_shentsize */
    w[18] = shnum;        /* e_shnum */
    w[19] = shstrtab_idx; /* e_shstrndx */

    /* Section headers start after ELF header */
    Elf64_Shdr *shdrs = (Elf64_Shdr *)(buf + hdr_sz);

    /* Section 0: NULL */
    memset(&shdrs[0], 0, sizeof(Elf64_Shdr));

    /* Section 1: .text */
    shdrs[text_idx].sh_name      = (uint32_t)shstr_text;
    shdrs[text_idx].sh_type      = SHT_PROGBITS;
    shdrs[text_idx].sh_flags     = SHF_ALLOC | SHF_EXECINSTR;
    shdrs[text_idx].sh_addr      = 0;
    shdrs[text_idx].sh_offset    = (uint64_t)code_off;
    shdrs[text_idx].sh_size      = (uint64_t)code_sz;
    shdrs[text_idx].sh_link      = 0;
    shdrs[text_idx].sh_info      = 0;
    shdrs[text_idx].sh_addralign = 4;
    shdrs[text_idx].sh_entsize   = 0;

    /* Section 2: .rela.text */
    shdrs[rela_idx].sh_name      = (uint32_t)shstr_rela;
    shdrs[rela_idx].sh_type      = SHT_RELA;
    shdrs[rela_idx].sh_flags     = SHF_INFO_LINK;
    shdrs[rela_idx].sh_addr      = 0;
    shdrs[rela_idx].sh_offset    = (uint64_t)rela_off;
    shdrs[rela_idx].sh_size      = (uint64_t)rela_size;
    shdrs[rela_idx].sh_link      = (uint32_t)symtab_idx;
    shdrs[rela_idx].sh_info      = (uint32_t)text_idx;
    shdrs[rela_idx].sh_addralign = 8;
    shdrs[rela_idx].sh_entsize   = (uint64_t)sizeof(Elf64_Rela);

    /* Section 3: .symtab */
    shdrs[symtab_idx].sh_name      = (uint32_t)shstr_symtab;
    shdrs[symtab_idx].sh_type      = SHT_SYMTAB;
    shdrs[symtab_idx].sh_flags     = 0;
    shdrs[symtab_idx].sh_addr      = 0;
    shdrs[symtab_idx].sh_offset    = (uint64_t)sym_off;
    shdrs[symtab_idx].sh_size      = (uint64_t)sym_size;
    shdrs[symtab_idx].sh_link      = (uint32_t)strtab_idx;
    shdrs[symtab_idx].sh_info      = (uint32_t)(local_count + 1);
    shdrs[symtab_idx].sh_addralign = 8;
    shdrs[symtab_idx].sh_entsize   = (uint64_t)sizeof(Elf64_Sym);

    /* Section 4: .strtab */
    shdrs[strtab_idx].sh_name      = (uint32_t)shstr_strtab;
    shdrs[strtab_idx].sh_type      = SHT_STRTAB;
    shdrs[strtab_idx].sh_flags     = 0;
    shdrs[strtab_idx].sh_addr      = 0;
    shdrs[strtab_idx].sh_offset    = (uint64_t)str_off_file;
    shdrs[strtab_idx].sh_size      = (uint64_t)str_off;
    shdrs[strtab_idx].sh_link      = 0;
    shdrs[strtab_idx].sh_info      = 0;
    shdrs[strtab_idx].sh_addralign = 1;
    shdrs[strtab_idx].sh_entsize   = 0;

    /* Section 5: .shstrtab */
    shdrs[shstrtab_idx].sh_name      = (uint32_t)shstr_shstrtab;
    shdrs[shstrtab_idx].sh_type      = SHT_STRTAB;
    shdrs[shstrtab_idx].sh_flags     = 0;
    shdrs[shstrtab_idx].sh_addr      = 0;
    shdrs[shstrtab_idx].sh_offset    = (uint64_t)shstr_off_file;
    shdrs[shstrtab_idx].sh_size      = (uint64_t)shstr_off;
    shdrs[shstrtab_idx].sh_link      = 0;
    shdrs[shstrtab_idx].sh_info      = 0;
    shdrs[shstrtab_idx].sh_addralign = 1;
    shdrs[shstrtab_idx].sh_entsize   = 0;

    /* Copy data */
    memcpy(buf + code_off, code, code_sz);
    memcpy(buf + rela_off, relas, rela_size);
    memcpy(buf + sym_off, syms, sym_size);
    memcpy(buf + str_off_file, strtab, str_off);
    memcpy(buf + shstr_off_file, shstrtab, shstr_off);

    free(syms);
    free(relas);

    int fd = open(path, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (fd < 0) { free(buf); return false; }
    write(fd, buf, total_sz);
    close(fd);
    free(buf);
    return true;
}
