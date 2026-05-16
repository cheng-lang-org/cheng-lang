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
    x64_emit1(c, REX_W | (src >= 8 ? REX_R : 0) | (dst >= 8 ? REX_B : 0));
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

/* sub %r64, %r64 */
static void x64_sub_r64_r64(X64Code *c, int dst, int src) {
    x64_emit1(c, REX_W | (dst >= 8 ? REX_R : 0) | (src >= 8 ? REX_B : 0));
    x64_emit1(c, 0x29);
    x64_emit1(c, MODRM(3, src & 7, dst & 7));
}
/* add %r64, %r64 */
static void x64_add_r64_r64(X64Code *c, int dst, int src) {
    x64_emit1(c, REX_W | (dst >= 8 ? REX_R : 0) | (src >= 8 ? REX_B : 0));
    x64_emit1(c, 0x01);
    x64_emit1(c, MODRM(3, src & 7, dst & 7));
}
/* xor %r32, %r32 */
static void x64_xor_r32_r32(X64Code *c, int dst, int src) {
    if (dst >= 8 || src >= 8) x64_emit1(c, (dst >= 8 ? REX_R : 0) | (src >= 8 ? REX_B : 0));
    x64_emit1(c, 0x31);
    x64_emit1(c, MODRM(3, src & 7, dst & 7));
}
/* xor %r64, %r64 */
static void x64_xor_r64_r64(X64Code *c, int dst, int src) {
    x64_emit1(c, REX_W | (dst >= 8 ? REX_R : 0) | (src >= 8 ? REX_B : 0));
    x64_emit1(c, 0x31);
    x64_emit1(c, MODRM(3, src & 7, dst & 7));
}
/* neg %r32 */
static void x64_neg_r32(X64Code *c, int reg) {
    if (reg >= 8) x64_emit1(c, REX_B);
    x64_emit1(c, 0xF7);
    x64_emit1(c, MODRM(3, 3, reg & 7));
}
/* not %r32 */
static void x64_not_r32(X64Code *c, int reg) {
    if (reg >= 8) x64_emit1(c, REX_B);
    x64_emit1(c, 0xF7);
    x64_emit1(c, MODRM(3, 2, reg & 7));
}
/* cmp %r64, %r64 */
static void x64_cmp_r64_r64(X64Code *c, int a, int b) {
    x64_emit1(c, REX_W | (a >= 8 ? REX_R : 0) | (b >= 8 ? REX_B : 0));
    x64_emit1(c, 0x39);
    x64_emit1(c, MODRM(3, a & 7, b & 7));
}
/* cmp $imm32, %r64 */
static void x64_cmp_r64_imm8(X64Code *c, int reg, int8_t imm) {
    x64_emit1(c, REX_W | (reg >= 8 ? REX_B : 0));
    x64_emit1(c, 0x83);
    x64_emit1(c, MODRM(3, 7, reg & 7));
    x64_emit1(c, (uint8_t)imm);
}
/* mov $imm32, [base + disp32] (64-bit) */
static void x64_mov_mr64_imm32(X64Code *c, int base, int32_t disp, int32_t imm) {
    x64_emit1(c, REX_W | (base >= 8 ? REX_B : 0));
    x64_emit1(c, 0xC7);
    if ((base & 7) == 4) {
        x64_emit1(c, MODRM(2, 0, 4));
        x64_emit1(c, SIB(0, 4, 4));
    } else {
        x64_emit1(c, MODRM(2, 0, base & 7));
    }
    x64_emit4(c, (uint32_t)disp);
    x64_emit4(c, (uint32_t)imm);
}
/* mov [base + disp32], %r32 (store, explicit) */
static void x64_mov_mr32_imm32(X64Code *c, int base, int32_t disp, int32_t imm) {
    if (base >= 8) x64_emit1(c, REX_B);
    x64_emit1(c, 0xC7);
    if ((base & 7) == 4) {
        x64_emit1(c, MODRM(2, 0, 4));
        x64_emit1(c, SIB(0, 4, 4));
    } else {
        x64_emit1(c, MODRM(2, 0, base & 7));
    }
    x64_emit4(c, (uint32_t)disp);
    x64_emit4(c, (uint32_t)imm);
}
/* SETcc %r8 (set byte on condition) */
static void x64_setcc_r8(X64Code *c, int cc, int reg) {
    if (reg >= 8) x64_emit1(c, REX_B);
    x64_emit1(c, 0x0F);
    x64_emit1(c, 0x90 + cc);
    x64_emit1(c, MODRM(3, 0, reg & 7));
}
/* movzb %r8, %r32 */
static void x64_movzb_r8_r32(X64Code *c, int dst, int src) {
    if (dst >= 8 || src >= 8) x64_emit1(c, (dst >= 8 ? REX_R : 0) | (src >= 8 ? REX_B : 0));
    x64_emit1(c, 0x0F);
    x64_emit1(c, 0xB6);
    x64_emit1(c, MODRM(3, dst & 7, src & 7));
}
/* shl %cl, %r32 */
static void x64_shl_r32_cl(X64Code *c, int reg) {
    if (reg >= 8) x64_emit1(c, REX_B);
    x64_emit1(c, 0xD3);
    x64_emit1(c, MODRM(3, 4, reg & 7));
}
/* shr %cl, %r32 */
static void x64_shr_r32_cl(X64Code *c, int reg) {
    if (reg >= 8) x64_emit1(c, REX_B);
    x64_emit1(c, 0xD3);
    x64_emit1(c, MODRM(3, 5, reg & 7));
}
/* sar %cl, %r32 */
static void x64_sar_r32_cl(X64Code *c, int reg) {
    if (reg >= 8) x64_emit1(c, REX_B);
    x64_emit1(c, 0xD3);
    x64_emit1(c, MODRM(3, 7, reg & 7));
}
/* test %r32, %r32 */
static void x64_test_r32_r32(X64Code *c, int a, int b) {
    if (a >= 8 || b >= 8) x64_emit1(c, (a >= 8 ? REX_R : 0) | (b >= 8 ? REX_B : 0));
    x64_emit1(c, 0x85);
    x64_emit1(c, MODRM(3, a & 7, b & 7));
}
/* cdq (sign-extend EAX→EDX:EAX) */
static void x64_cdq(X64Code *c) { x64_emit1(c, 0x99); }
/* cqo (sign-extend RAX→RDX:RAX) */
static void x64_cqo(X64Code *c) { x64_emit1(c, REX_W); x64_emit1(c, 0x99); }
/* idiv %r32 (EDX:EAX / r32 → EAX, remainder in EDX) */
static void x64_idiv_r32(X64Code *c, int reg) {
    if (reg >= 8) x64_emit1(c, REX_B);
    x64_emit1(c, 0xF7);
    x64_emit1(c, MODRM(3, 7, reg & 7));
}
/* idiv %r64 (RDX:RAX / r64 → RAX, remainder in RDX) */
static void x64_idiv_r64(X64Code *c, int reg) {
    x64_emit1(c, REX_W | (reg >= 8 ? REX_B : 0));
    x64_emit1(c, 0xF7);
    x64_emit1(c, MODRM(3, 7, reg & 7));
}
/* imul %r64, %r64 */
static void x64_imul_r64_r64(X64Code *c, int dst, int src) {
    x64_emit1(c, REX_W | (dst >= 8 ? REX_R : 0) | (src >= 8 ? REX_B : 0));
    x64_emit1(c, 0x0F); x64_emit1(c, 0xAF);
    x64_emit1(c, MODRM(3, dst & 7, src & 7));
}
/* and %r64, %r64 */
static void x64_and_r64_r64(X64Code *c, int dst, int src) {
    x64_emit1(c, REX_W | (dst >= 8 ? REX_R : 0) | (src >= 8 ? REX_B : 0));
    x64_emit1(c, 0x21);
    x64_emit1(c, MODRM(3, src & 7, dst & 7));
}
/* or %r64, %r64 */
static void x64_or_r64_r64(X64Code *c, int dst, int src) {
    x64_emit1(c, REX_W | (dst >= 8 ? REX_R : 0) | (src >= 8 ? REX_B : 0));
    x64_emit1(c, 0x09);
    x64_emit1(c, MODRM(3, src & 7, dst & 7));
}
/* xor %r64, %r64 (use REX.W 0x31 form) */
static void x64_xor_r64_r64_alt(X64Code *c, int dst, int src) {
    x64_emit1(c, REX_W | (dst >= 8 ? REX_R : 0) | (src >= 8 ? REX_B : 0));
    x64_emit1(c, 0x31);
    x64_emit1(c, MODRM(3, src & 7, dst & 7));
}
/* shl %cl, %r64 */
static void x64_shl_r64_cl(X64Code *c, int reg) {
    x64_emit1(c, REX_W | (reg >= 8 ? REX_B : 0));
    x64_emit1(c, 0xD3);
    x64_emit1(c, MODRM(3, 4, reg & 7));
}
/* shr %cl, %r64 */
static void x64_shr_r64_cl(X64Code *c, int reg) {
    x64_emit1(c, REX_W | (reg >= 8 ? REX_B : 0));
    x64_emit1(c, 0xD3);
    x64_emit1(c, MODRM(3, 5, reg & 7));
}
/* sar %cl, %r64 */
static void x64_sar_r64_cl(X64Code *c, int reg) {
    x64_emit1(c, REX_W | (reg >= 8 ? REX_B : 0));
    x64_emit1(c, 0xD3);
    x64_emit1(c, MODRM(3, 7, reg & 7));
}
/* movsxd %r32, %r64 (sign-extend 32→64) */
static void x64_movsxd_r64_r32(X64Code *c, int dst, int src) {
    x64_emit1(c, REX_W | (dst >= 8 ? REX_R : 0) | (src >= 8 ? REX_B : 0));
    x64_emit1(c, 0x63);
    x64_emit1(c, MODRM(3, dst & 7, src & 7));
}
/* xchg %rax, %r64 (special short encoding) */
static void x64_xchg_rax_r64(X64Code *c, int reg) {
    x64_emit1(c, REX_W | (reg >= 8 ? REX_B : 0));
    x64_emit1(c, 0x90 + (reg & 7));
}
/* lock cmpxchg %r32, [base] */
static void x64_lock_cmpxchg_r32_mr(X64Code *c, int base_reg, int reg) {
    x64_emit1(c, 0xF0); /* LOCK prefix */
    if (base_reg >= 8 || reg >= 8) x64_emit1(c, (base_reg >= 8 ? REX_B : 0) | (reg >= 8 ? REX_R : 0));
    x64_emit1(c, 0x0F);
    x64_emit1(c, 0xB1);
    x64_emit1(c, MODRM(0, reg & 7, base_reg & 7));
}
/* mov (load) 64-bit from [base] */
static void x64_mov_r64_mr64_base(X64Code *c, int reg, int base_reg) {
    x64_emit1(c, REX_W | (reg >= 8 ? REX_R : 0) | (base_reg >= 8 ? REX_B : 0));
    x64_emit1(c, 0x8B);
    x64_emit1(c, MODRM(0, reg & 7, base_reg & 7));
}
/* mov (store) 64-bit to [base] */
static void x64_mov_mr64_base_r64(X64Code *c, int base_reg, int reg) {
    x64_emit1(c, REX_W | (reg >= 8 ? REX_R : 0) | (base_reg >= 8 ? REX_B : 0));
    x64_emit1(c, 0x89);
    x64_emit1(c, MODRM(0, reg & 7, base_reg & 7));
}
/* mov (load) 32-bit from [base] */
static void x64_mov_r32_mr32_base(X64Code *c, int reg, int base_reg) {
    if (reg >= 8 || base_reg >= 8) x64_emit1(c, (reg >= 8 ? REX_R : 0) | (base_reg >= 8 ? REX_B : 0));
    x64_emit1(c, 0x8B);
    x64_emit1(c, MODRM(0, reg & 7, base_reg & 7));
}
/* mov (store) 32-bit to [base] */
static void x64_mov_mr32_base_r32(X64Code *c, int base_reg, int reg) {
    if (base_reg >= 8 || reg >= 8) x64_emit1(c, (base_reg >= 8 ? REX_R : 0) | (reg >= 8 ? REX_B : 0));
    x64_emit1(c, 0x89);
    x64_emit1(c, MODRM(0, reg & 7, base_reg & 7));
}
/* jmp rel8 (short jump, ±127 bytes) */
static void x64_jmp_rel8(X64Code *c, int8_t rel) { x64_emit1(c, 0xEB); x64_emit1(c, (uint8_t)rel); }
/* jcc rel8 (short conditional jump) */
static void x64_jcc_rel8(X64Code *c, int cc, int8_t rel) {
    x64_emit1(c, 0x70 + cc);
    x64_emit1(c, (uint8_t)rel);
}
/* lea [base + disp32], %r32 */
static void x64_lea_r32_mr(X64Code *c, int reg, int base, int32_t disp) {
    if (reg >= 8 || base >= 8) x64_emit1(c, (reg >= 8 ? REX_R : 0) | (base >= 8 ? REX_B : 0));
    x64_emit1(c, 0x8D);
    if ((base & 7) == 4) {
        x64_emit1(c, MODRM(2, reg & 7, 4));
        x64_emit1(c, SIB(0, 4, 4));
    } else {
        x64_emit1(c, MODRM(2, reg & 7, base & 7));
    }
    x64_emit4(c, (uint32_t)disp);
}
/* mov %r32, [base_reg + index_reg*4] (scaled index store) */
static void x64_mov_mr32_base_index4_r32(X64Code *c, int base_reg, int index_reg, int reg) {
    if (reg >= 8 || index_reg >= 8 || base_reg >= 8)
        x64_emit1(c, (reg >= 8 ? REX_R : 0) | (index_reg >= 8 ? REX_X : 0) | (base_reg >= 8 ? REX_B : 0));
    x64_emit1(c, 0x89);
    x64_emit1(c, MODRM(0, reg & 7, 4));
    x64_emit1(c, SIB(2, index_reg & 7, base_reg & 7));
}
/* mov [base_reg + index_reg*4], %r32 (scaled index load) */
static void x64_mov_r32_mr32_base_index4(X64Code *c, int reg, int base_reg, int index_reg) {
    if (reg >= 8 || index_reg >= 8 || base_reg >= 8)
        x64_emit1(c, (reg >= 8 ? REX_R : 0) | (index_reg >= 8 ? REX_X : 0) | (base_reg >= 8 ? REX_B : 0));
    x64_emit1(c, 0x8B);
    x64_emit1(c, MODRM(0, reg & 7, 4));
    x64_emit1(c, SIB(2, index_reg & 7, base_reg & 7));
}
/* mov [base_reg + index_reg*8], %r64 (scaled index store 64-bit) */
static void x64_mov_mr64_base_index8_r64(X64Code *c, int base_reg, int index_reg, int reg) {
    x64_emit1(c, REX_W | (reg >= 8 ? REX_R : 0) | (index_reg >= 8 ? REX_X : 0) | (base_reg >= 8 ? REX_B : 0));
    x64_emit1(c, 0x89);
    x64_emit1(c, MODRM(0, reg & 7, 4));
    x64_emit1(c, SIB(3, index_reg & 7, base_reg & 7));
}
/* mov [base_reg + index_reg*8], %r64 (load 64-bit) */
static void x64_mov_r64_mr64_base_index8(X64Code *c, int reg, int base_reg, int index_reg) {
    x64_emit1(c, REX_W | (reg >= 8 ? REX_R : 0) | (index_reg >= 8 ? REX_X : 0) | (base_reg >= 8 ? REX_B : 0));
    x64_emit1(c, 0x8B);
    x64_emit1(c, MODRM(0, reg & 7, 4));
    x64_emit1(c, SIB(3, index_reg & 7, base_reg & 7));
}
/* movb [base], %r8 (store byte) */
static void x64_mov_mr8_r8(X64Code *c, int base_reg, int reg) {
    if (base_reg >= 8 || reg >= 8) x64_emit1(c, (base_reg >= 8 ? REX_B : 0) | (reg >= 8 ? REX_R : 0));
    x64_emit1(c, 0x88);
    x64_emit1(c, MODRM(0, reg & 7, base_reg & 7));
}
/* movzb [base], %r32 (load zero-extend byte) */
static void x64_movzb_r32_mr8(X64Code *c, int reg, int base_reg) {
    if (reg >= 8 || base_reg >= 8) x64_emit1(c, (reg >= 8 ? REX_R : 0) | (base_reg >= 8 ? REX_B : 0));
    x64_emit1(c, 0x0F); x64_emit1(c, 0xB6);
    x64_emit1(c, MODRM(0, reg & 7, base_reg & 7));
}
/* movzw [base], %r32 (load zero-extend 16-bit) */
static void x64_movzw_r32_mr16(X64Code *c, int reg, int base_reg) {
    if (reg >= 8 || base_reg >= 8) x64_emit1(c, (reg >= 8 ? REX_R : 0) | (base_reg >= 8 ? REX_B : 0));
    x64_emit1(c, 0x0F); x64_emit1(c, 0xB7);
    x64_emit1(c, MODRM(0, reg & 7, base_reg & 7));
}
/* movw [base], %r16 (store 16-bit) */
static void x64_mov_mr16_r16(X64Code *c, int base_reg, int reg) {
    x64_emit1(c, 0x66); /* operand size override */
    if (base_reg >= 8 || reg >= 8) x64_emit1(c, (base_reg >= 8 ? REX_B : 0) | (reg >= 8 ? REX_R : 0));
    x64_emit1(c, 0x89);
    x64_emit1(c, MODRM(0, reg & 7, base_reg & 7));
}
/* push imm32 */
static void x64_push_imm32(X64Code *c, int32_t imm) { x64_emit1(c, 0x68); x64_emit4(c, (uint32_t)imm); }
/* int3 */
static void x64_int3(X64Code *c) { x64_emit1(c, 0xCC); }
/* syscall (x86_64) */
static void x64_syscall(X64Code *c) { x64_emit1(c, 0x0F); x64_emit1(c, 0x05); }

/* mov %r64, [base_reg + disp8] (load 64-bit, disp8 form) */
static void x64_mov_r64_mr64_base_disp8(X64Code *c, int reg, int base_reg, int8_t disp) {
    x64_emit1(c, REX_W | (reg >= 8 ? REX_R : 0) | (base_reg >= 8 ? REX_B : 0));
    x64_emit1(c, 0x8B);
    if ((base_reg & 7) == 4) {
        x64_emit1(c, MODRM(1, reg & 7, 4));
        x64_emit1(c, SIB(0, 4, 4));
    } else {
        x64_emit1(c, MODRM(1, reg & 7, base_reg & 7));
    }
    x64_emit1(c, (uint8_t)disp);
}
/* mov [base_reg + disp8], %r64 (store 64-bit, disp8 form) */
static void x64_mov_mr64_base_disp8_r64(X64Code *c, int base_reg, int8_t disp, int reg) {
    x64_emit1(c, REX_W | (reg >= 8 ? REX_R : 0) | (base_reg >= 8 ? REX_B : 0));
    x64_emit1(c, 0x89);
    if ((base_reg & 7) == 4) {
        x64_emit1(c, MODRM(1, reg & 7, 4));
        x64_emit1(c, SIB(0, 4, 4));
    } else {
        x64_emit1(c, MODRM(1, reg & 7, base_reg & 7));
    }
    x64_emit1(c, (uint8_t)disp);
}
/* mov %r32, [base_reg + disp8] (load 32-bit, disp8 form) */
static void x64_mov_r32_mr32_base_disp8(X64Code *c, int reg, int base_reg, int8_t disp) {
    if (reg >= 8 || base_reg >= 8) x64_emit1(c, (reg >= 8 ? REX_R : 0) | (base_reg >= 8 ? REX_B : 0));
    x64_emit1(c, 0x8B);
    if ((base_reg & 7) == 4) {
        x64_emit1(c, MODRM(1, reg & 7, 4));
        x64_emit1(c, SIB(0, 4, 4));
    } else {
        x64_emit1(c, MODRM(1, reg & 7, base_reg & 7));
    }
    x64_emit1(c, (uint8_t)disp);
}
/* mov [base_reg + disp8], %r32 (store 32-bit, disp8 form) */
static void x64_mov_mr32_base_disp8_r32(X64Code *c, int base_reg, int8_t disp, int reg) {
    if (base_reg >= 8 || reg >= 8) x64_emit1(c, (base_reg >= 8 ? REX_B : 0) | (reg >= 8 ? REX_R : 0));
    x64_emit1(c, 0x89);
    if ((base_reg & 7) == 4) {
        x64_emit1(c, MODRM(1, reg & 7, 4));
        x64_emit1(c, SIB(0, 4, 4));
    } else {
        x64_emit1(c, MODRM(1, reg & 7, base_reg & 7));
    }
    x64_emit1(c, (uint8_t)disp);
}
/* movzb [base_reg + disp8], %r32 (load zero-extend byte from disp8) */
static void x64_movzb_r32_mr8_base_disp8(X64Code *c, int reg, int base_reg, int8_t disp) {
    if (reg >= 8 || base_reg >= 8) x64_emit1(c, (reg >= 8 ? REX_R : 0) | (base_reg >= 8 ? REX_B : 0));
    x64_emit1(c, 0x0F); x64_emit1(c, 0xB6);
    if ((base_reg & 7) == 4) {
        x64_emit1(c, MODRM(1, reg & 7, 4));
        x64_emit1(c, SIB(0, 4, 4));
    } else {
        x64_emit1(c, MODRM(1, reg & 7, base_reg & 7));
    }
    x64_emit1(c, (uint8_t)disp);
}
/* movb [base_reg + disp8], %r8 (store byte, disp8 form) */
static void x64_mov_mr8_base_disp8_r8(X64Code *c, int base_reg, int8_t disp, int reg) {
    if (base_reg >= 8 || reg >= 8) x64_emit1(c, (base_reg >= 8 ? REX_B : 0) | (reg >= 8 ? REX_R : 0));
    x64_emit1(c, 0x88);
    if ((base_reg & 7) == 4) {
        x64_emit1(c, MODRM(1, reg & 7, 4));
        x64_emit1(c, SIB(0, 4, 4));
    } else {
        x64_emit1(c, MODRM(1, reg & 7, base_reg & 7));
    }
    x64_emit1(c, (uint8_t)disp);
}

/* ---- float (SSE) helpers ---- */
/* movss [base+disp32], %xmm (store scalar single) */
static void x64_movss_mr_xmm(X64Code *c, int base, int32_t disp, int xmm) {
    x64_emit1(c, 0xF3);
    if (xmm >= 8 || base >= 8) x64_emit1(c, (xmm >= 8 ? REX_R : 0) | (base >= 8 ? REX_B : 0));
    x64_emit1(c, 0x0F); x64_emit1(c, 0x11);
    if ((base & 7) == 4) { x64_emit1(c, MODRM(2, xmm & 7, 4)); x64_emit1(c, SIB(0, 4, 4)); }
    else x64_emit1(c, MODRM(2, xmm & 7, base & 7));
    x64_emit4(c, (uint32_t)disp);
}
/* movss %xmm, [base+disp32] (load scalar single) */
static void x64_movss_xmm_mr(X64Code *c, int xmm, int base, int32_t disp) {
    x64_emit1(c, 0xF3);
    if (xmm >= 8 || base >= 8) x64_emit1(c, (xmm >= 8 ? REX_R : 0) | (base >= 8 ? REX_B : 0));
    x64_emit1(c, 0x0F); x64_emit1(c, 0x10);
    if ((base & 7) == 4) { x64_emit1(c, MODRM(2, xmm & 7, 4)); x64_emit1(c, SIB(0, 4, 4)); }
    else x64_emit1(c, MODRM(2, xmm & 7, base & 7));
    x64_emit4(c, (uint32_t)disp);
}
/* movsd [base+disp32], %xmm (store scalar double) */
static void x64_movsd_mr_xmm(X64Code *c, int base, int32_t disp, int xmm) {
    x64_emit1(c, 0xF2);
    if (xmm >= 8 || base >= 8) x64_emit1(c, (xmm >= 8 ? REX_R : 0) | (base >= 8 ? REX_B : 0));
    x64_emit1(c, 0x0F); x64_emit1(c, 0x11);
    if ((base & 7) == 4) { x64_emit1(c, MODRM(2, xmm & 7, 4)); x64_emit1(c, SIB(0, 4, 4)); }
    else x64_emit1(c, MODRM(2, xmm & 7, base & 7));
    x64_emit4(c, (uint32_t)disp);
}
/* movsd %xmm, [base+disp32] (load scalar double) */
static void x64_movsd_xmm_mr(X64Code *c, int xmm, int base, int32_t disp) {
    x64_emit1(c, 0xF2);
    if (xmm >= 8 || base >= 8) x64_emit1(c, (xmm >= 8 ? REX_R : 0) | (base >= 8 ? REX_B : 0));
    x64_emit1(c, 0x0F); x64_emit1(c, 0x10);
    if ((base & 7) == 4) { x64_emit1(c, MODRM(2, xmm & 7, 4)); x64_emit1(c, SIB(0, 4, 4)); }
    else x64_emit1(c, MODRM(2, xmm & 7, base & 7));
    x64_emit4(c, (uint32_t)disp);
}
/* movd %r32, %xmm */
static void x64_movd_xmm_r32(X64Code *c, int xmm, int reg) {
    x64_emit1(c, 0x66);
    if (xmm >= 8 || reg >= 8) x64_emit1(c, (xmm >= 8 ? REX_R : 0) | (reg >= 8 ? REX_B : 0));
    x64_emit1(c, 0x0F); x64_emit1(c, 0x6E);
    x64_emit1(c, MODRM(3, xmm & 7, reg & 7));
}
/* movd %xmm, %r32 */
static void x64_movd_r32_xmm(X64Code *c, int reg, int xmm) {
    x64_emit1(c, 0x66);
    if (xmm >= 8 || reg >= 8) x64_emit1(c, (xmm >= 8 ? REX_R : 0) | (reg >= 8 ? REX_B : 0));
    x64_emit1(c, 0x0F); x64_emit1(c, 0x7E);
    x64_emit1(c, MODRM(3, xmm & 7, reg & 7));
}
/* addss %xmm, %xmm */
static void x64_addss(X64Code *c, int dst, int src) {
    x64_emit1(c, 0xF3);
    if (dst >= 8 || src >= 8) x64_emit1(c, (dst >= 8 ? REX_R : 0) | (src >= 8 ? REX_B : 0));
    x64_emit1(c, 0x0F); x64_emit1(c, 0x58);
    x64_emit1(c, MODRM(3, dst & 7, src & 7));
}
/* subss %xmm, %xmm */
static void x64_subss(X64Code *c, int dst, int src) {
    x64_emit1(c, 0xF3);
    if (dst >= 8 || src >= 8) x64_emit1(c, (dst >= 8 ? REX_R : 0) | (src >= 8 ? REX_B : 0));
    x64_emit1(c, 0x0F); x64_emit1(c, 0x5C);
    x64_emit1(c, MODRM(3, dst & 7, src & 7));
}
/* mulss %xmm, %xmm */
static void x64_mulss(X64Code *c, int dst, int src) {
    x64_emit1(c, 0xF3);
    if (dst >= 8 || src >= 8) x64_emit1(c, (dst >= 8 ? REX_R : 0) | (src >= 8 ? REX_B : 0));
    x64_emit1(c, 0x0F); x64_emit1(c, 0x59);
    x64_emit1(c, MODRM(3, dst & 7, src & 7));
}
/* divss %xmm, %xmm */
static void x64_divss(X64Code *c, int dst, int src) {
    x64_emit1(c, 0xF3);
    if (dst >= 8 || src >= 8) x64_emit1(c, (dst >= 8 ? REX_R : 0) | (src >= 8 ? REX_B : 0));
    x64_emit1(c, 0x0F); x64_emit1(c, 0x5E);
    x64_emit1(c, MODRM(3, dst & 7, src & 7));
}
/* addsd %xmm, %xmm */
static void x64_addsd(X64Code *c, int dst, int src) {
    x64_emit1(c, 0xF2);
    if (dst >= 8 || src >= 8) x64_emit1(c, (dst >= 8 ? REX_R : 0) | (src >= 8 ? REX_B : 0));
    x64_emit1(c, 0x0F); x64_emit1(c, 0x58);
    x64_emit1(c, MODRM(3, dst & 7, src & 7));
}
/* subsd %xmm, %xmm */
static void x64_subsd(X64Code *c, int dst, int src) {
    x64_emit1(c, 0xF2);
    if (dst >= 8 || src >= 8) x64_emit1(c, (dst >= 8 ? REX_R : 0) | (src >= 8 ? REX_B : 0));
    x64_emit1(c, 0x0F); x64_emit1(c, 0x5C);
    x64_emit1(c, MODRM(3, dst & 7, src & 7));
}
/* mulsd %xmm, %xmm */
static void x64_mulsd(X64Code *c, int dst, int src) {
    x64_emit1(c, 0xF2);
    if (dst >= 8 || src >= 8) x64_emit1(c, (dst >= 8 ? REX_R : 0) | (src >= 8 ? REX_B : 0));
    x64_emit1(c, 0x0F); x64_emit1(c, 0x59);
    x64_emit1(c, MODRM(3, dst & 7, src & 7));
}
/* divsd %xmm, %xmm */
static void x64_divsd(X64Code *c, int dst, int src) {
    x64_emit1(c, 0xF2);
    if (dst >= 8 || src >= 8) x64_emit1(c, (dst >= 8 ? REX_R : 0) | (src >= 8 ? REX_B : 0));
    x64_emit1(c, 0x0F); x64_emit1(c, 0x5E);
    x64_emit1(c, MODRM(3, dst & 7, src & 7));
}
/* ucomiss %xmm, %xmm (unordered compare scalar single, sets EFLAGS) */
static void x64_ucomiss(X64Code *c, int a, int b) {
    if (a >= 8 || b >= 8) x64_emit1(c, (a >= 8 ? REX_R : 0) | (b >= 8 ? REX_B : 0));
    x64_emit1(c, 0x0F); x64_emit1(c, 0x2E);
    x64_emit1(c, MODRM(3, a & 7, b & 7));
}
/* ucomisd %xmm, %xmm (unordered compare scalar double, sets EFLAGS) */
static void x64_ucomisd(X64Code *c, int a, int b) {
    x64_emit1(c, 0x66);
    if (a >= 8 || b >= 8) x64_emit1(c, (a >= 8 ? REX_R : 0) | (b >= 8 ? REX_B : 0));
    x64_emit1(c, 0x0F); x64_emit1(c, 0x2E);
    x64_emit1(c, MODRM(3, a & 7, b & 7));
}
/* cvtsi2ss %r32, %xmm */
static void x64_cvtsi2ss(X64Code *c, int xmm, int reg) {
    x64_emit1(c, 0xF3);
    if (xmm >= 8 || reg >= 8) x64_emit1(c, (xmm >= 8 ? REX_R : 0) | (reg >= 8 ? REX_B : 0));
    x64_emit1(c, 0x0F); x64_emit1(c, 0x2A);
    x64_emit1(c, MODRM(3, xmm & 7, reg & 7));
}
/* cvtsi2sd %r32, %xmm */
static void x64_cvtsi2sd(X64Code *c, int xmm, int reg) {
    x64_emit1(c, 0xF2);
    if (xmm >= 8 || reg >= 8) x64_emit1(c, (xmm >= 8 ? REX_R : 0) | (reg >= 8 ? REX_B : 0));
    x64_emit1(c, 0x0F); x64_emit1(c, 0x2A);
    x64_emit1(c, MODRM(3, xmm & 7, reg & 7));
}
/* cvttss2si %xmm, %r32 */
static void x64_cvttss2si(X64Code *c, int reg, int xmm) {
    x64_emit1(c, 0xF3);
    if (xmm >= 8 || reg >= 8) x64_emit1(c, (xmm >= 8 ? REX_R : 0) | (reg >= 8 ? REX_B : 0));
    x64_emit1(c, 0x0F); x64_emit1(c, 0x2C);
    x64_emit1(c, MODRM(3, reg & 7, xmm & 7));
}
/* cvttsd2si %xmm, %r32 */
static void x64_cvttsd2si(X64Code *c, int reg, int xmm) {
    x64_emit1(c, 0xF2);
    if (xmm >= 8 || reg >= 8) x64_emit1(c, (xmm >= 8 ? REX_R : 0) | (reg >= 8 ? REX_B : 0));
    x64_emit1(c, 0x0F); x64_emit1(c, 0x2C);
    x64_emit1(c, MODRM(3, reg & 7, xmm & 7));
}
/* xorps %xmm, %xmm */
static void x64_xorps(X64Code *c, int dst, int src) {
    if (dst >= 8 || src >= 8) x64_emit1(c, (dst >= 8 ? REX_R : 0) | (src >= 8 ? REX_B : 0));
    x64_emit1(c, 0x0F); x64_emit1(c, 0x57);
    x64_emit1(c, MODRM(3, dst & 7, src & 7));
}
/* mulss %xmm, %xmm (for negation via sign bit) */
static void x64_xorps_alt(X64Code *c, int dst, int src) {
    if (dst >= 8 || src >= 8) x64_emit1(c, (dst >= 8 ? REX_R : 0) | (src >= 8 ? REX_B : 0));
    x64_emit1(c, 0x0F); x64_emit1(c, 0x57);
    x64_emit1(c, MODRM(3, dst & 7, src & 7));
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
