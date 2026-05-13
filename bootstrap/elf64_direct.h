/* elf64_direct.h -- Self-contained ELF64 object file (.o) reader + writer.
 *
 * Writes/reads minimal ELF64 relocatable objects for aarch64, riscv64,
 * or x86_64 linux targets.  Symbols + relocations for external linkage.
 *
 * Reader: elf64_read_object() extracts .text + symbol table from a .o file.
 * Writer: elf64_write_object() / elf_write_exec() produce .o / executable.
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
#define ET_EXEC          2
#define EM_AARCH64       183
#define EM_X86_64        62
#define EM_RISCV         243

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

/* RISC-V relocations */
#define R_RISCV_CALL         18
#define R_RISCV_CALL_PLT     19

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
        uint32_t type = (machine == EM_AARCH64) ? R_AARCH64_CALL26 :
                        (machine == EM_RISCV)   ? R_RISCV_CALL :
                        R_X86_64_PLT32;
        relas[i].r_info = ((uint64_t)sym << 32) | (uint64_t)type;
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
    w[4] = ET_REL | ((uint32_t)machine << 16); /* e_type + e_machine */
    w[5] = 1;                                   /* e_version */
    w[6] = 0;             /* e_entry low */
    w[7] = 0;             /* e_entry high */
    w[8] = 0;             /* e_phoff low */
    w[9] = 0;             /* e_phoff high */
    w[10] = (uint32_t)hdr_sz; /* e_shoff low */
    w[11] = 0;            /* e_shoff high */
    w[12] = 0;            /* e_flags */
    buf[0x34] = (uint8_t)hdr_sz;
    buf[0x35] = (uint8_t)(hdr_sz >> 8);
    buf[0x36] = 0; buf[0x37] = 0; /* e_phentsize */
    buf[0x38] = 0; buf[0x39] = 0; /* e_phnum */
    buf[0x3A] = (uint8_t)sizeof(Elf64_Shdr);
    buf[0x3B] = (uint8_t)(sizeof(Elf64_Shdr) >> 8);
    buf[0x3C] = (uint8_t)shnum;
    buf[0x3D] = (uint8_t)(shnum >> 8);
    buf[0x3E] = (uint8_t)shstrtab_idx;
    buf[0x3F] = (uint8_t)(shstrtab_idx >> 8);

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

/* Minimal ELF64 static executable writer. Writes code words at 0x400000
   with a single PT_LOAD segment covering the code. No dynamic linking. */
static bool elf_write_exec(const char *path, const uint32_t *code,
                            int32_t code_words, uint16_t machine) {
    int32_t code_sz = code_words * 4;
    int32_t hdr_sz = 64;
    int32_t phdr_sz = 56; /* one PT_LOAD */
    int32_t phdr_off = hdr_sz;
    int32_t code_off = hdr_sz + phdr_sz;
    uint64_t entry = 0x400000 + code_off;
    int32_t total_sz = code_off + code_sz;

    uint8_t *buf = (uint8_t *)calloc(1, total_sz);
    if (!buf) return false;
    uint32_t *w = (uint32_t *)buf;

    /* ELF Header */
    w[0] = ELF64_MAGIC;
    buf[4] = ELFCLASS64; buf[5] = ELFDATA2LSB;
    buf[6] = EV_CURRENT; buf[7] = ELFOSABI_SYSV;
    w[4] = ET_EXEC | ((uint32_t)machine << 16);
    w[5] = 1;                        /* e_version */
    w[6] = (uint32_t)entry;          /* e_entry */
    w[7] = (uint32_t)(entry >> 32);
    w[8] = (uint32_t)phdr_off;       /* e_phoff */
    w[9] = 0;
    w[10] = 0;                       /* e_shoff */
    w[11] = 0;
    w[12] = 0;                       /* e_flags (4 bytes at 0x30) */
    /* e_ehsize (2) + e_phentsize (2) */
    buf[0x34] = 64; buf[0x35] = 0;
    buf[0x36] = (uint8_t)phdr_sz; buf[0x37] = (uint8_t)(phdr_sz >> 8);
    /* e_phnum (2) + e_shentsize (2) */
    buf[0x38] = 1; buf[0x39] = 0;
    buf[0x3A] = 0; buf[0x3B] = 0;
    /* e_shnum (2) + e_shstrndx (2) */
    buf[0x3C] = 0; buf[0x3D] = 0;
    buf[0x3E] = 0; buf[0x3F] = 0;

    /* Program Header: PT_LOAD covering code segment */
    {
        uint32_t *ph = (uint32_t *)(buf + phdr_off);
        ph[0] = 1;                    /* p_type = PT_LOAD */
        ph[1] = 5;                    /* p_flags = PF_R | PF_X */
        ph[2] = (uint32_t)code_off;   /* p_offset (low) */
        ph[3] = 0;                    /* p_offset (high) */
        ph[4] = (uint32_t)0x400000;   /* p_vaddr (low) */
        ph[5] = 0;                    /* p_vaddr (high) */
        ph[6] = (uint32_t)0x400000;   /* p_paddr (low) */
        ph[7] = 0;                    /* p_paddr (high) */
        ph[8] = (uint32_t)code_sz;    /* p_filesz (low) */
        ph[9] = 0;                    /* p_filesz (high) */
        ph[10] = (uint32_t)code_sz;   /* p_memsz (low) */
        ph[11] = 0;                   /* p_memsz (high) */
        ph[12] = 0x1000;              /* p_align (low) */
        ph[13] = 0;                   /* p_align (high) */
    }

    /* Copy code words */
    memcpy(buf + code_off, code, code_sz);

    int fd = open(path, O_WRONLY | O_CREAT | O_TRUNC, 0755);
    if (fd < 0) { free(buf); return false; }
    write(fd, buf, total_sz);
    close(fd);
    free(buf);
    return true;
}

/* Minimal ELF64 .o reader for provider archive linking.
   Extracts .text section and global symbol table from a relocatable .o.
   Caller frees out_code, out_names, out_offsets. */
static bool elf64_read_object(const char *path,
                               uint32_t **out_code, int32_t *out_code_words,
                               char ***out_names, int32_t **out_offsets,
                               int32_t *out_name_count) {
    int fd = open(path, O_RDONLY);
    if (fd < 0) return false;
    off_t sz = lseek(fd, 0, SEEK_END);
    lseek(fd, 0, SEEK_SET);
    if (sz < 64) { close(fd); return false; }
    uint8_t *data = (uint8_t *)malloc((size_t)sz);
    if (!data) { close(fd); return false; }
    read(fd, data, (size_t)sz);
    close(fd);

    if (*(uint32_t *)data != ELF64_MAGIC) { free(data); return false; }
    uint64_t shoff = *(uint64_t *)(data + 0x28);
    uint16_t shnum = *(uint16_t *)(data + 0x3C);
    uint16_t shentsize = *(uint16_t *)(data + 0x3A);
    if (shnum == 0 || shentsize < 64) { free(data); return false; }

    uint64_t text_off = 0, text_sz = 0;
    uint64_t sym_off = 0, sym_sz = 0, sym_entsize = 0;
    uint64_t str_off = 0, str_sz = 0;
    for (int32_t i = 0; i < shnum; i++) {
        uint64_t s = shoff + (uint64_t)i * shentsize;
        if (s + 64 > (uint64_t)sz) break;
        uint32_t sh_type = *(uint32_t *)(data + s + 4);
        uint64_t sh_offset = *(uint64_t *)(data + s + 24);
        uint64_t sh_size = *(uint64_t *)(data + s + 32);
        if (sh_type == 1) { text_off = sh_offset; text_sz = sh_size; }        /* SHT_PROGBITS */
        else if (sh_type == 2) { sym_off = sh_offset; sym_sz = sh_size; sym_entsize = *(uint64_t *)(data + s + 56); } /* SHT_SYMTAB */
        else if (sh_type == 3) { str_off = sh_offset; str_sz = sh_size; }     /* SHT_STRTAB */
    }
    if (text_sz == 0 || sym_sz == 0 || str_sz == 0) { free(data); return false; }

    int32_t cw = (int32_t)(text_sz / 4);
    uint32_t *code = (uint32_t *)malloc((size_t)cw * sizeof(uint32_t));
    memcpy(code, data + text_off, (size_t)text_sz);

    int32_t max_syms = (int32_t)(sym_sz / (sym_entsize > 0 ? sym_entsize : 24));
    char **names = (char **)calloc((size_t)max_syms, sizeof(char *));
    int32_t *offsets = (int32_t *)calloc((size_t)max_syms, sizeof(int32_t));
    int32_t nc = 0;
    for (int32_t i = 0; i < max_syms && nc < max_syms; i++) {
        uint64_t sp = sym_off + (uint64_t)i * sym_entsize;
        if (sp + 24 > (uint64_t)sz) break;
        uint32_t st_name = *(uint32_t *)(data + sp);
        uint8_t st_info = *(uint8_t *)(data + sp + 4);
        uint64_t st_value = *(uint64_t *)(data + sp + 8);
        if (st_name > 0 && st_name < str_sz && (st_info >> 4) == 1) { /* STB_GLOBAL */
            const char *sn = (const char *)(data + str_off + st_name);
            if (sn[0]) { names[nc] = strdup(sn); offsets[nc] = (int32_t)(st_value / 4); nc++; }
        }
    }
    free(data);
    *out_code = code; *out_code_words = cw;
    *out_names = names; *out_offsets = offsets; *out_name_count = nc;
    return true;
}
