#ifndef CHENG_STAGE0C_CORE_ASM_EMIT_LAYOUT_H
#define CHENG_STAGE0C_CORE_ASM_EMIT_LAYOUT_H

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

#include "ast.h"
#include "diagnostics.h"

typedef enum {
    ASM_DATA_RODATA = 0,
    ASM_DATA_DATA,
    ASM_DATA_BSS
} AsmDataSection;

typedef enum {
    ASM_DATA_INIT_NONE = 0,
    ASM_DATA_INIT_INT,
    ASM_DATA_INIT_LABEL,
    ASM_DATA_INIT_BYTES
} AsmDataInitKind;

typedef struct {
    size_t ptr_size;
    size_t ptr_align;
    size_t int_size;
    size_t int_align;
    size_t long_size;
    size_t long_align;
    size_t bool_size;
    size_t bool_align;
    size_t char_size;
    size_t char_align;
    size_t float_size;
    size_t float_align;
    size_t double_size;
    size_t double_align;
} AsmLayoutConfig;

typedef struct {
    const char *name;
    size_t offset;
    size_t size;
    size_t align;
} AsmFieldLayout;

typedef struct {
    const char *name;
    size_t size;
    size_t align;
    const AsmFieldLayout *fields;
    size_t field_len;
} AsmTypeLayout;

typedef struct {
    const char *name;
    AsmDataSection section;
    AsmDataInitKind init_kind;
    size_t size;
    size_t align;
    int is_global;
    int64_t int_value;
    size_t int_size;
    const char *label;
    int label_needs_prefix;
    const uint8_t *bytes;
    size_t bytes_len;
} AsmDataItem;

typedef struct AsmLayout AsmLayout;

void asm_layout_config_default(AsmLayoutConfig *config, size_t ptr_size);
void asm_layout_init(AsmLayout *layout, const AsmLayoutConfig *config);
void asm_layout_free(AsmLayout *layout);
AsmLayout *asm_layout_create(const AsmLayoutConfig *config);
void asm_layout_destroy(AsmLayout *layout);

int asm_layout_collect(AsmLayout *layout,
                       const ChengNode *root,
                       const char *filename,
                       ChengDiagList *diags);

const AsmTypeLayout *asm_layout_find_type(AsmLayout *layout, const char *name);
int asm_layout_field_offset(AsmLayout *layout,
                            const char *type_name,
                            const char *field_name,
                            size_t *out_offset);
int asm_layout_type_info(AsmLayout *layout,
                         const ChengNode *type_expr,
                         size_t *out_size,
                         size_t *out_align);

const AsmDataItem *asm_layout_data_items(const AsmLayout *layout, size_t *out_len);
const AsmDataItem *asm_layout_find_data_item(const AsmLayout *layout, const char *name);
const char *asm_layout_string_label(const AsmLayout *layout, const char *value);
int asm_layout_dump(const AsmLayout *layout, FILE *out);

#endif
