/* x64_emit.h -- Minimal x86_64 instruction encoder for cold compiler.
 *
 * Produces AMD64 machine code in a byte buffer.
 * Instructions are variable-length (1-15 bytes).
 * Uses AT&T operand order in comments (src, dst).
 *
 * Register encoding:
 *   RAX=0, RCX=1, RDX=2, RBX=3, RSP=4, RBP=5, RSI=6, RDI=7
 *   R8-R15: 8-15 with REX.B prefix
 *   32-bit variants: same numbers with no REX prefix (or REX.W=0)
 */

#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include <stdlib.h>

/* REX prefix: 0x40 | (W<<3) | (R<<2) | (X<<1) | B */
#define REX_W   0x48  /* 64-bit operand size */
#define REX_R   0x44  /* ModRM.reg extension */
#define REX_X   0x42  /* SIB.index extension */
#define REX_B   0x41  /* ModRM.rm or SIB.base extension */

/* ModRM byte: (mod<<6) | (reg<<3) | rm */
#define MODRM(mod, reg, rm) (((mod)<<6) | ((reg)<<3) | (rm))

/* SIB byte: (scale<<6) | (index<<3) | base */
#define SIB(scale, index, base) (((scale)<<6) | ((index)<<3) | (base))

/* Condition codes for Jcc / SETcc / CMOVcc */
enum { CC_O=0, CC_NO, CC_B, CC_AE, CC_E, CC_NE, CC_BE, CC_A,
       CC_S, CC_NS, CC_P, CC_NP, CC_L, CC_GE, CC_LE, CC_G };

/* ---- Byte buffer for x86_64 code ---- */
typedef struct {
    uint8_t *buf;
    int32_t cap;
    int32_t len;
} X64Code;

static void x64_init(X64Code *c, int32_t cap) {
    c->buf = (uint8_t *)calloc(1, cap);
    c->cap = cap;
    c->len = 0;
}

static void x64_emit1(X64Code *c, uint8_t b) {
    if (c->len < c->cap) c->buf[c->len++] = b;
}

static void x64_emit4(X64Code *c, uint32_t v) {
    x64_emit1(c, (uint8_t)(v));
    x64_emit1(c, (uint8_t)(v >> 8));
    x64_emit1(c, (uint8_t)(v >> 16));
    x64_emit1(c, (uint8_t)(v >> 24));
}

static void x64_emit8(X64Code *c, uint64_t v) {
    x64_emit4(c, (uint32_t)(v));
    x64_emit4(c, (uint32_t)(v >> 32));
}

/* ---- Basic instructions ---- */

/* mov $imm32, %r32 */
static void x64_mov_r32_imm32(X64Code *c, int reg, int32_t imm) {
    x64_emit1(c, 0xB8 + (reg & 7));
    x64_emit4(c, (uint32_t)imm);
}

/* mov $imm64, %r64 */
static void x64_mov_r64_imm64(X64Code *c, int reg, uint64_t imm) {
    x64_emit1(c, REX_W + (reg >> 3 ? REX_B : 0));
    x64_emit1(c, 0xB8 + (reg & 7));
    x64_emit8(c, imm);
}

/* mov %r32, [base + disp32] */
static void x64_mov_mr32_r32(X64Code *c, int base, int32_t disp, int reg) {
    if (reg >= 8) { x64_emit1(c, REX_R); }
    if (base >= 8) { x64_emit1(c, REX_B); }
    x64_emit1(c, 0x89);
    if (base == 4) { /* RSP needs SIB */
        x64_emit1(c, MODRM(2, reg & 7, 4));
        x64_emit1(c, SIB(0, 4, 4));
    } else {
        x64_emit1(c, MODRM(2, reg & 7, base & 7));
    }
    x64_emit4(c, (uint32_t)disp);
}

/* mov [base + disp32], %r64 */
static void x64_mov_mr64_r64(X64Code *c, int base, int32_t disp, int reg) {
    x64_emit1(c, REX_W | (reg >= 8 ? REX_R : 0) | (base >= 8 ? REX_B : 0));
    x64_emit1(c, 0x89);
    if ((base & 7) == 4) {
        x64_emit1(c, MODRM(2, reg & 7, 4));
        x64_emit1(c, SIB(0, 4, 4));
    } else {
        x64_emit1(c, MODRM(2, reg & 7, base & 7));
    }
    x64_emit4(c, (uint32_t)disp);
}

/* mov %r32, [base + disp32] (store 32-bit) */
static void x64_mov_mr32_r32_store(X64Code *c, int base, int32_t disp, int reg) {
    x64_mov_mr32_r32(c, base, disp, reg);
}

/* mov %r64, [base + disp32] (store 64-bit) */
static void x64_mov_mr64_r64_store(X64Code *c, int base, int32_t disp, int reg) {
    x64_mov_mr64_r64(c, base, disp, reg);
}

/* mov [base + disp32], %r32 (load 32-bit) */
static void x64_mov_r32_mr32(X64Code *c, int reg, int base, int32_t disp) {
    if (reg >= 8) { x64_emit1(c, REX_R); }
    if (base >= 8) { x64_emit1(c, REX_B); }
    x64_emit1(c, 0x8B);
    if ((base & 7) == 4) {
        x64_emit1(c, MODRM(2, reg & 7, 4));
        x64_emit1(c, SIB(0, 4, 4));
    } else {
        x64_emit1(c, MODRM(2, reg & 7, base & 7));
    }
    x64_emit4(c, (uint32_t)disp);
}

/* mov [base + disp32], %r64 (load 64-bit) */
static void x64_mov_r64_mr64(X64Code *c, int reg, int base, int32_t disp) {
    x64_emit1(c, REX_W | (reg >= 8 ? REX_R : 0) | (base >= 8 ? REX_B : 0));
    x64_emit1(c, 0x8B);
    if ((base & 7) == 4) {
        x64_emit1(c, MODRM(2, reg & 7, 4));
        x64_emit1(c, SIB(0, 4, 4));
    } else {
        x64_emit1(c, MODRM(2, reg & 7, base & 7));
    }
    x64_emit4(c, (uint32_t)disp);
}

/* add %r32, %r32 */
static void x64_add_r32_r32(X64Code *c, int dst, int src) {
    if (dst >= 8 || src >= 8) x64_emit1(c, (dst >= 8 ? REX_R : 0) | (src >= 8 ? REX_B : 0));
    x64_emit1(c, 0x01);
    x64_emit1(c, MODRM(3, src & 7, dst & 7));
}

/* sub %r32, %r32 */
static void x64_sub_r32_r32(X64Code *c, int dst, int src) {
    if (dst >= 8 || src >= 8) x64_emit1(c, (dst >= 8 ? REX_R : 0) | (src >= 8 ? REX_B : 0));
    x64_emit1(c, 0x29);
    x64_emit1(c, MODRM(3, src & 7, dst & 7));
}

/* imul %r32, %r32 */
static void x64_imul_r32_r32(X64Code *c, int dst, int src) {
    if (dst >= 8 || src >= 8) x64_emit1(c, (dst >= 8 ? REX_R : 0) | (src >= 8 ? REX_B : 0));
    x64_emit1(c, 0x0F); x64_emit1(c, 0xAF);
    x64_emit1(c, MODRM(3, dst & 7, src & 7));
}

/* cmp %r32, %r32 */
static void x64_cmp_r32_r32(X64Code *c, int a, int b) {
    if (a >= 8 || b >= 8) x64_emit1(c, (a >= 8 ? REX_R : 0) | (b >= 8 ? REX_B : 0));
    x64_emit1(c, 0x39);
    x64_emit1(c, MODRM(3, a & 7, b & 7));
}

/* cmp $imm8, %r32 (or %r64) */
static void x64_cmp_r32_imm8(X64Code *c, int reg, int8_t imm) {
    if (reg >= 8) x64_emit1(c, REX_B);
    x64_emit1(c, 0x83);
    x64_emit1(c, MODRM(3, 7, reg & 7));
    x64_emit1(c, (uint8_t)imm);
}

/* jmp rel32 (relative jump) */
static void x64_jmp_rel32(X64Code *c, int32_t rel) {
    x64_emit1(c, 0xE9);
    x64_emit4(c, (uint32_t)rel);
}

/* jcc rel32 (conditional jump) */
static void x64_jcc_rel32(X64Code *c, int cc, int32_t rel) {
    x64_emit1(c, 0x0F);
    x64_emit1(c, 0x80 + cc);
    x64_emit4(c, (uint32_t)rel);
}

/* call rel32 (relative call) */
static void x64_call_rel32(X64Code *c, int32_t rel) {
    x64_emit1(c, 0xE8);
    x64_emit4(c, (uint32_t)rel);
}

/* call *%reg (indirect call via register) */
static void x64_call_reg(X64Code *c, int reg) {
    if (reg >= 8) x64_emit1(c, REX_B);
    x64_emit1(c, 0xFF);
    x64_emit1(c, MODRM(3, 2, reg & 7));
}

/* ret */
static void x64_ret(X64Code *c) {
    x64_emit1(c, 0xC3);
}

/* nop (3-byte) */
static void x64_nop3(X64Code *c) {
    x64_emit1(c, 0x0F); x64_emit1(c, 0x1F); x64_emit1(c, 0x00);
}

/* lea [base + disp32], %r64 */
static void x64_lea_r64_mr(X64Code *c, int reg, int base, int32_t disp) {
    x64_emit1(c, REX_W | (reg >= 8 ? REX_R : 0) | (base >= 8 ? REX_B : 0));
    x64_emit1(c, 0x8D);
    if ((base & 7) == 4) {
        x64_emit1(c, MODRM(2, reg & 7, 4));
        x64_emit1(c, SIB(0, 4, 4));
    } else {
        x64_emit1(c, MODRM(2, reg & 7, base & 7));
    }
    x64_emit4(c, (uint32_t)disp);
}

/* push %r64 */
static void x64_push_r64(X64Code *c, int reg) {
    if (reg >= 8) x64_emit1(c, REX_B);
    x64_emit1(c, 0x50 + (reg & 7));
}

/* pop %r64 */
static void x64_pop_r64(X64Code *c, int reg) {
    if (reg >= 8) x64_emit1(c, REX_B);
    x64_emit1(c, 0x58 + (reg & 7));
}

/* xchg %rax, %reg (for saving/restoring) */
static void x64_mov_r64_r64(X64Code *c, int dst, int src) {
    x64_emit1(c, REX_W | (dst >= 8 ? REX_R : 0) | (src >= 8 ? REX_B : 0));
    x64_emit1(c, 0x89);
    x64_emit1(c, MODRM(3, src & 7, dst & 7));
}

/* sub $imm8, %rsp */
static void x64_sub_rsp_imm8(X64Code *c, int8_t imm) {
    x64_emit1(c, 0x48);
    x64_emit1(c, 0x83);
    x64_emit1(c, MODRM(3, 5, 4)); /* sub $imm8, %rsp */
    x64_emit1(c, (uint8_t)imm);
}

/* add $imm8, %rsp */
static void x64_add_rsp_imm8(X64Code *c, int8_t imm) {
    x64_emit1(c, 0x48);
    x64_emit1(c, 0x83);
    x64_emit1(c, MODRM(3, 0, 4)); /* add $imm8, %rsp */
    x64_emit1(c, (uint8_t)imm);
}

/* mov %r32, %r32 */
static void x64_mov_r32_r32(X64Code *c, int dst, int src) {
    if (dst >= 8 || src >= 8) x64_emit1(c, (dst >= 8 ? REX_R : 0) | (src >= 8 ? REX_B : 0));
    x64_emit1(c, 0x89);
    x64_emit1(c, MODRM(3, src & 7, dst & 7));
}

/* ---- Relocation patching ---- */
static void x64_patch_call_rel32(X64Code *c, int32_t pos, int32_t target) {
    /* pos points to the byte AFTER the E8 opcode */
    int32_t rel = target - (pos + 4); /* RIP-relative: offset from end of instruction */
    c->buf[pos]     = (uint8_t)(rel);
    c->buf[pos + 1] = (uint8_t)(rel >> 8);
    c->buf[pos + 2] = (uint8_t)(rel >> 16);
    c->buf[pos + 3] = (uint8_t)(rel >> 24);
}

static void x64_patch_jmp_rel32(X64Code *c, int32_t pos, int32_t target) {
    int32_t rel = target - (pos + 4);
    c->buf[pos]     = (uint8_t)(rel);
    c->buf[pos + 1] = (uint8_t)(rel >> 8);
    c->buf[pos + 2] = (uint8_t)(rel >> 16);
    c->buf[pos + 3] = (uint8_t)(rel >> 24);
}
