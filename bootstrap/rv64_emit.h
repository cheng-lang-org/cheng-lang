/* rv64_emit.h -- RISC-V 64-bit (RV64G) instruction encoder for cold compiler.
 *
 * Fixed 32-bit instruction encoding.  Uses standard RISC-V instruction formats.
 *
 * Register encoding: x0=zero, x1=ra, x2=sp, x3=gp, x4=tp, x5-x7=t0-t2,
 *   x8=s0/fp, x9=s1, x10-x17=a0-a7, x18-x27=s2-s11, x28-x31=t3-t6
 *
 * Calling convention: a0-a7 for args, a0-a1 for return values.
 * Callee-saved: s0-s11 (x8-x9, x18-x27), sp (x2).
 */

#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include <stdlib.h>

/* RISC-V instruction formats (all 32-bit) */

/* R-type: funct7 | rs2 | rs1 | funct3 | rd | opcode */
#define RV_R(rd, rs1, rs2, f3, f7, op) \
    ((((uint32_t)(f7) & 0x7F) << 25) |   \
     (((uint32_t)(rs2) & 0x1F) << 20) |   \
     (((uint32_t)(rs1) & 0x1F) << 15) |   \
     (((uint32_t)(f3) & 0x07) << 12) |    \
     (((uint32_t)(rd) & 0x1F) << 7)  |    \
     ((uint32_t)(op) & 0x7F))

/* I-type: imm[11:0] | rs1 | funct3 | rd | opcode */
#define RV_I(rd, rs1, imm, f3, op)           \
    ((((uint32_t)(imm) & 0xFFF) << 20) |      \
     (((uint32_t)(rs1) & 0x1F) << 15) |       \
     (((uint32_t)(f3) & 0x07) << 12) |        \
     (((uint32_t)(rd) & 0x1F) << 7)  |        \
     ((uint32_t)(op) & 0x7F))

/* S-type: imm[11:5] | rs2 | rs1 | funct3 | imm[4:0] | opcode */
#define RV_S(rs2, rs1, imm, f3, op)           \
    ((((uint32_t)(imm) & 0xFE0) << 20) |       \
     (((uint32_t)(rs2) & 0x1F) << 20) |        \
     (((uint32_t)(rs1) & 0x1F) << 15) |        \
     (((uint32_t)(f3) & 0x07) << 12) |         \
     (((uint32_t)(imm) & 0x1F) << 7)  |        \
     ((uint32_t)(op) & 0x7F))

/* B-type: imm[12|10:5] | rs2 | rs1 | funct3 | imm[4:1|11] | opcode */
#define RV_B(rs1, rs2, imm, f3, op)                       \
    ((((uint32_t)(imm) & 0x800) << 20) |  /* imm[11] */    \
     (((uint32_t)(imm) & 0x7E0) << 20) |  /* imm[10:5] */  \
     (((uint32_t)(rs2) & 0x1F) << 20) |                    \
     (((uint32_t)(rs1) & 0x1F) << 15) |                    \
     (((uint32_t)(f3) & 0x07) << 12) |                     \
     (((uint32_t)(imm) & 0x1E) << 7)  |  /* imm[4:1] */    \
     (((uint32_t)(imm) & 0x1) << 7)   |  /* imm[11] bit 0? */  \
     ((uint32_t)(op) & 0x7F))

/* U-type: imm[31:12] | rd | opcode */
#define RV_U(rd, imm, op) \
    ((((uint32_t)(imm) & 0xFFFFF000u) >> 12) | \
     (((uint32_t)(rd) & 0x1F) << 7) |          \
     ((uint32_t)(op) & 0x7F))

/* J-type: imm[20|10:1|11|19:12] | rd | opcode */
#define RV_J(rd, imm, op)                             \
    ((((uint32_t)(imm) & 0x100000u) >> 20) |          \
     (((uint32_t)(imm) & 0x7FE)   << 20)  |           \
     (((uint32_t)(imm) & 0x800)   << 9)   |           \
     (((uint32_t)(imm) & 0xFF000u) >> 12)  |          \
     (((uint32_t)(rd) & 0x1F) << 7)  |                \
     ((uint32_t)(op) & 0x7F))

/* Opcodes */
#define RV_OP_IMM   0x13  /* ADDI, SLTI, etc. */
#define RV_OP       0x33  /* ADD, SUB, etc. */
#define RV_LUI      0x37
#define RV_AUIPC    0x17
#define RV_OP_IMM32 0x1B  /* ADDIW, etc. */
#define RV_OP32     0x3B  /* ADDW, SUBW, etc. */
#define RV_LOAD     0x03
#define RV_STORE    0x23
#define RV_BRANCH   0x63
#define RV_JALR     0x67
#define RV_JAL      0x6F

/* funct3 */
#define F3_ADD   0  /* ADD/SUB/MUL */
#define F3_SLL   1
#define F3_SLT   2
#define F3_SLTU  3
#define F3_XOR   4
#define F3_SR    5  /* SRL/SRA */
#define F3_OR    6
#define F3_AND   7

#define F3_BYTE  0  /* LB/SB */
#define F3_HALF  1  /* LH/SH */
#define F3_WORD  2  /* LW/SW */
#define F3_DWORD 3  /* LD/SD */

#define F3_BEQ   0
#define F3_BNE   1
#define F3_BLT   4
#define F3_BGE   5
#define F3_BLTU  6
#define F3_BGEU  7

/* Registers */
enum { RV_ZERO=0, RV_RA=1, RV_SP=2, RV_GP=3, RV_TP=4,
       RV_T0=5, RV_T1=6, RV_T2=7,
       RV_S0=8, RV_S1=9, RV_A0=10, RV_A1=11,
       RV_A2=12, RV_A3=13, RV_A4=14, RV_A5=15,
       RV_A6=16, RV_A7=17, RV_S2=18, RV_S3=19,
       RV_S4=20, RV_S5=21, RV_S6=22, RV_S7=23,
       RV_S8=24, RV_S9=25, RV_S10=26, RV_S11=27,
       RV_T3=28, RV_T4=29, RV_T5=30, RV_T6=31 };

/* ---- RISC-V instruction constructors ---- */

/* ADDI rd, rs1, imm12 */
static uint32_t rv_addi(int rd, int rs1, int16_t imm) {
    return RV_I(rd, rs1, (uint32_t)(imm & 0xFFF), F3_ADD, RV_OP_IMM);
}
/* ADDIW rd, rs1, imm12 (32-bit add) */
static uint32_t rv_addiw(int rd, int rs1, int16_t imm) {
    return RV_I(rd, rs1, (uint32_t)(imm & 0xFFF), F3_ADD, RV_OP_IMM32);
}
/* ADD rd, rs1, rs2 */
static uint32_t rv_add(int rd, int rs1, int rs2) {
    return RV_R(rd, rs1, rs2, F3_ADD, 0, RV_OP);
}
/* ADDW rd, rs1, rs2 (32-bit) */
static uint32_t rv_addw(int rd, int rs1, int rs2) {
    return RV_R(rd, rs1, rs2, F3_ADD, 0, RV_OP32);
}
/* SUB rd, rs1, rs2 */
static uint32_t rv_sub(int rd, int rs1, int rs2) {
    return RV_R(rd, rs1, rs2, F3_ADD, 0x20, RV_OP);
}
/* SUBW rd, rs1, rs2 (32-bit) */
static uint32_t rv_subw(int rd, int rs1, int rs2) {
    return RV_R(rd, rs1, rs2, F3_ADD, 0x20, RV_OP32);
}
/* MUL rd, rs1, rs2 */
static uint32_t rv_mul(int rd, int rs1, int rs2) {
    return RV_R(rd, rs1, rs2, F3_ADD, 0x01, RV_OP);
}
/* MULW rd, rs1, rs2 (32-bit) */
static uint32_t rv_mulw(int rd, int rs1, int rs2) {
    return RV_R(rd, rs1, rs2, F3_ADD, 0x01, RV_OP32);
}
/* DIVW rd, rs1, rs2 (signed 32-bit) */
static uint32_t rv_divw(int rd, int rs1, int rs2) {
    return RV_R(rd, rs1, rs2, F3_SR, 0x01, RV_OP32);
}
/* SLLI rd, rs1, shamt (shift left logical immediate) */
static uint32_t rv_slli(int rd, int rs1, int shamt) {
    return RV_I(rd, rs1, (uint32_t)(shamt & 0x3F), F3_SLL, RV_OP_IMM);
}
/* SRAI rd, rs1, shamt (shift right arithmetic immediate) */
static uint32_t rv_srai(int rd, int rs1, int shamt) {
    return RV_I(rd, rs1, (uint32_t)((shamt & 0x3F) | 0x400), F3_SR, RV_OP_IMM);
}
/* AND rd, rs1, rs2 */
static uint32_t rv_and(int rd, int rs1, int rs2) {
    return RV_R(rd, rs1, rs2, F3_AND, 0, RV_OP);
}
/* OR rd, rs1, rs2 */
static uint32_t rv_or(int rd, int rs1, int rs2) {
    return RV_R(rd, rs1, rs2, F3_OR, 0, RV_OP);
}
/* XOR rd, rs1, rs2 */
static uint32_t rv_xor(int rd, int rs1, int rs2) {
    return RV_R(rd, rs1, rs2, F3_XOR, 0, RV_OP);
}
/* SLT rd, rs1, rs2 (set less than signed) */
static uint32_t rv_slt(int rd, int rs1, int rs2) {
    return RV_R(rd, rs1, rs2, F3_SLT, 0, RV_OP);
}
/* SLTI rd, rs1, imm12 */
static uint32_t rv_slti(int rd, int rs1, int16_t imm) {
    return RV_I(rd, rs1, (uint32_t)(imm & 0xFFF), F3_SLT, RV_OP_IMM);
}
/* SLTIU rd, rs1, imm12 (set less than immediate unsigned) */
static uint32_t rv_sltiu(int rd, int rs1, int16_t imm) {
    return RV_I(rd, rs1, (uint32_t)(imm & 0xFFF), F3_SLTU, RV_OP_IMM);
}
/* LD rd, imm12(rs1) -- 64-bit load */
static uint32_t rv_ld(int rd, int rs1, int16_t imm) {
    return RV_I(rd, rs1, (uint32_t)(imm & 0xFFF), F3_DWORD, RV_LOAD);
}
/* LW rd, imm12(rs1) -- 32-bit load */
static uint32_t rv_lw(int rd, int rs1, int16_t imm) {
    return RV_I(rd, rs1, (uint32_t)(imm & 0xFFF), F3_WORD, RV_LOAD);
}
/* LWU rd, imm12(rs1) -- 32-bit unsigned load */
static uint32_t rv_lwu(int rd, int rs1, int16_t imm) {
    return RV_I(rd, rs1, (uint32_t)(imm & 0xFFF), F3_HALF + 2, RV_LOAD);
}
/* SD rs2, imm12(rs1) -- 64-bit store */
static uint32_t rv_sd(int rs2, int rs1, int16_t imm) {
    return RV_S(rs2, rs1, (uint32_t)(imm & 0xFFF), F3_DWORD, RV_STORE);
}
/* SW rs2, imm12(rs1) -- 32-bit store */
static uint32_t rv_sw(int rs2, int rs1, int16_t imm) {
    return RV_S(rs2, rs1, (uint32_t)(imm & 0xFFF), F3_WORD, RV_STORE);
}
/* BEQ rs1, rs2, offset */
static uint32_t rv_beq(int rs1, int rs2, int16_t offset) {
    return RV_B(rs1, rs2, (uint32_t)(offset & 0x1FFF), F3_BEQ, RV_BRANCH);
}
/* BNE rs1, rs2, offset */
static uint32_t rv_bne(int rs1, int rs2, int16_t offset) {
    return RV_B(rs1, rs2, (uint32_t)(offset & 0x1FFF), F3_BNE, RV_BRANCH);
}
/* BLT rs1, rs2, offset */
static uint32_t rv_blt(int rs1, int rs2, int16_t offset) {
    return RV_B(rs1, rs2, (uint32_t)(offset & 0x1FFF), F3_BLT, RV_BRANCH);
}
/* BGE rs1, rs2, offset */
static uint32_t rv_bge(int rs1, int rs2, int16_t offset) {
    return RV_B(rs1, rs2, (uint32_t)(offset & 0x1FFF), F3_BGE, RV_BRANCH);
}
/* JAL rd, offset (rd=RA for call, rd=ZERO for jump) */
static uint32_t rv_jal(int rd, int32_t offset) {
    return RV_J(rd, (uint32_t)(offset & 0x1FFFFF), RV_JAL);
}
/* JALR rd, rs1, imm12 */
static uint32_t rv_jalr(int rd, int rs1, int16_t imm) {
    return RV_I(rd, rs1, (uint32_t)(imm & 0xFFF), F3_ADD, RV_JALR);
}
/* LUI rd, imm20 */
static uint32_t rv_lui(int rd, int32_t imm) {
    return RV_U(rd, (uint32_t)(imm & 0xFFFFF000u), RV_LUI);
}
/* AUIPC rd, imm20 */
static uint32_t rv_auipc(int rd, int32_t imm) {
    return RV_U(rd, (uint32_t)(imm & 0xFFFFF000u), RV_AUIPC);
}

/* Combined LI macro: LUI + ADDI for loading 32-bit immediates */
static void rv_li(uint32_t *code, int *pos, int rd, int32_t imm) {
    if (imm >= -2048 && imm <= 2047) {
        code[(*pos)++] = rv_addi(rd, RV_ZERO, (int16_t)imm);
    } else {
        int32_t upper = (imm + 0x800) & 0xFFFFF000;
        code[(*pos)++] = rv_lui(rd, upper);
        int16_t lower = (int16_t)(imm - upper);
        if (lower != 0)
            code[(*pos)++] = rv_addiw(rd, rd, lower);
    }
}

/* Load 64-bit immediate (multi-instruction sequence) */
static void rv_li64(uint32_t *code, int *pos, int rd, uint64_t imm) {
    int32_t lo = (int32_t)(imm & 0xFFFFFFFFu);
    int32_t hi = (int32_t)((imm >> 32) & 0xFFFFFFFFu);
    rv_li(code, pos, rd, lo);
    if (hi != 0) {
        /* Use SLLI + ADDI + SLLI + ADDI to construct upper 32 bits */
        code[(*pos)++] = rv_slli(rd, rd, 32);
    }
}

/* REMW rd, rs1, rs2 (signed 32-bit remainder) */
static uint32_t rv_remw(int rd, int rs1, int rs2) {
    return RV_R(rd, rs1, rs2, F3_SR, 0x07, RV_OP32);
}
/* DIV rd, rs1, rs2 (signed 64-bit) */
static uint32_t rv_div(int rd, int rs1, int rs2) {
    return RV_R(rd, rs1, rs2, F3_SR, 0x01, RV_OP);
}
/* REM rd, rs1, rs2 (signed 64-bit remainder) */
static uint32_t rv_rem(int rd, int rs1, int rs2) {
    return RV_R(rd, rs1, rs2, F3_SR, 0x07, RV_OP);
}
/* SRLI rd, rs1, shamt (shift right logical immediate) */
static uint32_t rv_srli(int rd, int rs1, int shamt) {
    return RV_I(rd, rs1, (uint32_t)(shamt & 0x3F), F3_SR, RV_OP_IMM);
}
/* SLLIW rd, rs1, shamt (32-bit shift left logical immediate) */
static uint32_t rv_slliw(int rd, int rs1, int shamt) {
    return RV_I(rd, rs1, (uint32_t)(shamt & 0x1F), F3_SLL, RV_OP_IMM32);
}
/* SRLIW rd, rs1, shamt (32-bit shift right logical immediate) */
static uint32_t rv_srliw(int rd, int rs1, int shamt) {
    return RV_I(rd, rs1, (uint32_t)(shamt & 0x1F), F3_SR, RV_OP_IMM32);
}
/* SRAIW rd, rs1, shamt (32-bit shift right arithmetic immediate) */
static uint32_t rv_sraiw(int rd, int rs1, int shamt) {
    return RV_I(rd, rs1, (uint32_t)((shamt & 0x1F) | 0x400), F3_SR, RV_OP_IMM32);
}
/* SLLW rd, rs1, rs2 (32-bit shift left logical) */
static uint32_t rv_sllw(int rd, int rs1, int rs2) {
    return RV_R(rd, rs1, rs2, F3_SLL, 0, RV_OP32);
}
/* SRLW rd, rs1, rs2 (32-bit shift right logical) */
static uint32_t rv_srlw(int rd, int rs1, int rs2) {
    return RV_R(rd, rs1, rs2, F3_SR, 0, RV_OP32);
}
/* SRAW rd, rs1, rs2 (32-bit shift right arithmetic) */
static uint32_t rv_sraw(int rd, int rs1, int rs2) {
    return RV_R(rd, rs1, rs2, F3_SR, 0x20, RV_OP32);
}
/* SLL rd, rs1, rs2 (64-bit shift left logical) */
static uint32_t rv_sll(int rd, int rs1, int rs2) {
    return RV_R(rd, rs1, rs2, F3_SLL, 0, RV_OP);
}
/* SRL rd, rs1, rs2 (64-bit shift right logical) */
static uint32_t rv_srl(int rd, int rs1, int rs2) {
    return RV_R(rd, rs1, rs2, F3_SR, 0, RV_OP);
}
/* SRA rd, rs1, rs2 (64-bit shift right arithmetic) */
static uint32_t rv_sra(int rd, int rs1, int rs2) {
    return RV_R(rd, rs1, rs2, F3_SR, 0x20, RV_OP);
}
/* ANDI rd, rs1, imm12 */
static uint32_t rv_andi(int rd, int rs1, int16_t imm) {
    return RV_I(rd, rs1, (uint32_t)(imm & 0xFFF), F3_AND, RV_OP_IMM);
}
/* ORI rd, rs1, imm12 */
static uint32_t rv_ori(int rd, int rs1, int16_t imm) {
    return RV_I(rd, rs1, (uint32_t)(imm & 0xFFF), F3_OR, RV_OP_IMM);
}
/* XORI rd, rs1, imm12 */
static uint32_t rv_xori(int rd, int rs1, int16_t imm) {
    return RV_I(rd, rs1, (uint32_t)(imm & 0xFFF), F3_XOR, RV_OP_IMM);
}
/* SLTU rd, rs1, rs2 (set less than unsigned) */
static uint32_t rv_sltu(int rd, int rs1, int rs2) {
    return RV_R(rd, rs1, rs2, F3_SLTU, 0, RV_OP);
}
/* BGEU rs1, rs2, offset */
static uint32_t rv_bgeu(int rs1, int rs2, int16_t offset) {
    return RV_B(rs1, rs2, (uint32_t)(offset & 0x1FFF), F3_BGEU, RV_BRANCH);
}
/* BLTU rs1, rs2, offset */
static uint32_t rv_bltu(int rs1, int rs2, int16_t offset) {
    return RV_B(rs1, rs2, (uint32_t)(offset & 0x1FFF), F3_BLTU, RV_BRANCH);
}
/* BLT rs1, rs2, offset (exists in original but may differ) */
/* EBREAK */
static uint32_t rv_ebreak(void) { return 0x00100073u; }
/* ECALL */
static uint32_t rv_ecall(void) { return 0x00000073u; }
/* FENCE */
static uint32_t rv_fence(void) { return 0x0FF0000Fu; }
/* LWU rd, imm12(rs1) -- 32-bit unsigned load (exists in original) */
/* LBU rd, imm12(rs1) -- load byte unsigned */
static uint32_t rv_lbu(int rd, int rs1, int16_t imm) {
    return RV_I(rd, rs1, (uint32_t)(imm & 0xFFF), 4, RV_LOAD);
}
/* SB rs2, imm12(rs1) -- store byte */
static uint32_t rv_sb(int rs2, int rs1, int16_t imm) {
    return RV_S(rs2, rs1, (uint32_t)(imm & 0xFFF), 0, RV_STORE);
}
/* MULW rd, rs1, rs2 (exists in original) */
/* DIVUW rd, rs1, rs2 (unsigned 32-bit) */
static uint32_t rv_divuw(int rd, int rs1, int rs2) {
    return RV_R(rd, rs1, rs2, F3_SR, 0x05, RV_OP32);
}
/* LR.W rd, (rs1) -- load reserved 32-bit */
static uint32_t rv_lr_w(int rd, int rs1) {
    return 0x1000202Fu | (((uint32_t)rd & 0x1F) << 7) | (((uint32_t)rs1 & 0x1F) << 15);
}
/* SC.W rd, rs2, (rs1) -- store conditional 32-bit */
static uint32_t rv_sc_w(int rd, int rs1, int rs2) {
    return 0x1800202Fu | (((uint32_t)rd & 0x1F) << 7) | (((uint32_t)rs1 & 0x1F) << 15) | (((uint32_t)rs2 & 0x1F) << 20);
}
/* AMOSWAP.W rd, rs2, (rs1) */
static uint32_t rv_amoswap_w(int rd, int rs1, int rs2) {
    return 0x0800202Fu | (((uint32_t)rd & 0x1F) << 7) | (((uint32_t)rs1 & 0x1F) << 15) | (((uint32_t)rs2 & 0x1F) << 20);
}
/* CSRRW rd, csr, rs1 -- CSR read/write */
static uint32_t rv_csrrw(int rd, int csr, int rs1) {
    return 0x00000073u | (((uint32_t)csr & 0xFFF) << 20) | (((uint32_t)rs1 & 0x1F) << 15) | (0x1 << 12) | (((uint32_t)rd & 0x1F) << 7);
}
/* CSRRWI rd, csr, uimm5 -- CSR read/write immediate */
static uint32_t rv_csrrwi(int rd, int csr, int uimm) {
    return 0x00000073u | (((uint32_t)csr & 0xFFF) << 20) | (((uint32_t)uimm & 0x1F) << 15) | (0x5 << 12) | (((uint32_t)rd & 0x1F) << 7);
}
/* Float: FADD.S rd, rs1, rs2 */
static uint32_t rv_fadd_s(int rd, int rs1, int rs2) {
    return 0x00000053u | (((uint32_t)rd & 0x1F) << 7) | (((uint32_t)rs1 & 0x1F) << 15) | (((uint32_t)rs2 & 0x1F) << 20) | (0x00u << 25);
}
/* FSUB.S rd, rs1, rs2 */
static uint32_t rv_fsub_s(int rd, int rs1, int rs2) {
    return 0x08000053u | (((uint32_t)rd & 0x1F) << 7) | (((uint32_t)rs1 & 0x1F) << 15) | (((uint32_t)rs2 & 0x1F) << 20);
}
/* FMUL.S rd, rs1, rs2 */
static uint32_t rv_fmul_s(int rd, int rs1, int rs2) {
    return 0x10000053u | (((uint32_t)rd & 0x1F) << 7) | (((uint32_t)rs1 & 0x1F) << 15) | (((uint32_t)rs2 & 0x1F) << 20);
}
/* FDIV.S rd, rs1, rs2 */
static uint32_t rv_fdiv_s(int rd, int rs1, int rs2) {
    return 0x18000053u | (((uint32_t)rd & 0x1F) << 7) | (((uint32_t)rs1 & 0x1F) << 15) | (((uint32_t)rs2 & 0x1F) << 20);
}
/* FSGNJN.S rd, rs1, rs2 (negate) */
static uint32_t rv_fsgnjn_s(int rd, int rs1, int rs2) {
    return 0x20000053u | (((uint32_t)rd & 0x1F) << 7) | (((uint32_t)rs1 & 0x1F) << 15) | (((uint32_t)rs2 & 0x1F) << 20) | (0x01u << 25);
}
/* FEQ.S rd, rs1, rs2 */
static uint32_t rv_feq_s(int rd, int rs1, int rs2) {
    return 0xA0000053u | (((uint32_t)rd & 0x1F) << 7) | (((uint32_t)rs1 & 0x1F) << 15) | (((uint32_t)rs2 & 0x1F) << 20) | (0x02u << 25);
}
/* FLT.S rd, rs1, rs2 */
static uint32_t rv_flt_s(int rd, int rs1, int rs2) {
    return 0xA0000053u | (((uint32_t)rd & 0x1F) << 7) | (((uint32_t)rs1 & 0x1F) << 15) | (((uint32_t)rs2 & 0x1F) << 20) | (0x01u << 25);
}
/* FCVT.S.W rd, rs1 (int32→float32) */
static uint32_t rv_fcvt_s_w(int rd, int rs1) {
    return 0xD2000053u | (((uint32_t)rd & 0x1F) << 7) | (((uint32_t)rs1 & 0x1F) << 15);
}
/* FCVT.W.S rd, rs1 (float32→int32, truncate) */
static uint32_t rv_fcvt_w_s(int rd, int rs1) {
    return 0xC2000053u | (((uint32_t)rd & 0x1F) << 7) | (((uint32_t)rs1 & 0x1F) << 15) | (0x03u << 20);
}
/* FLW rd, imm12(rs1) -- load float */
static uint32_t rv_flw(int rd, int rs1, int16_t imm) {
    return RV_I(rd, rs1, (uint32_t)(imm & 0xFFF), F3_WORD, 0x07);
}
/* FSW rs2, imm12(rs1) -- store float */
static uint32_t rv_fsw(int rs2, int rs1, int16_t imm) {
    return RV_S(rs2, rs1, (uint32_t)(imm & 0xFFF), F3_WORD, 0x27);
}
/* FADD.D rd, rs1, rs2 */
static uint32_t rv_fadd_d(int rd, int rs1, int rs2) {
    return 0x02000053u | (((uint32_t)rd & 0x1F) << 7) | (((uint32_t)rs1 & 0x1F) << 15) | (((uint32_t)rs2 & 0x1F) << 20);
}
/* FSUB.D rd, rs1, rs2 */
static uint32_t rv_fsub_d(int rd, int rs1, int rs2) {
    return 0x0A000053u | (((uint32_t)rd & 0x1F) << 7) | (((uint32_t)rs1 & 0x1F) << 15) | (((uint32_t)rs2 & 0x1F) << 20);
}
/* FMUL.D rd, rs1, rs2 */
static uint32_t rv_fmul_d(int rd, int rs1, int rs2) {
    return 0x12000053u | (((uint32_t)rd & 0x1F) << 7) | (((uint32_t)rs1 & 0x1F) << 15) | (((uint32_t)rs2 & 0x1F) << 20);
}
/* FDIV.D rd, rs1, rs2 */
static uint32_t rv_fdiv_d(int rd, int rs1, int rs2) {
    return 0x1A000053u | (((uint32_t)rd & 0x1F) << 7) | (((uint32_t)rs1 & 0x1F) << 15) | (((uint32_t)rs2 & 0x1F) << 20);
}
/* FSGNJN.D rd, rs1, rs2 (negate) */
static uint32_t rv_fsgnjn_d(int rd, int rs1, int rs2) {
    return 0x22000053u | (((uint32_t)rd & 0x1F) << 7) | (((uint32_t)rs1 & 0x1F) << 15) | (((uint32_t)rs2 & 0x1F) << 20) | (0x01u << 25);
}
/* FEQ.D rd, rs1, rs2 */
static uint32_t rv_feq_d(int rd, int rs1, int rs2) {
    return 0xA2000053u | (((uint32_t)rd & 0x1F) << 7) | (((uint32_t)rs1 & 0x1F) << 15) | (((uint32_t)rs2 & 0x1F) << 20) | (0x02u << 25);
}
/* FLT.D rd, rs1, rs2 */
static uint32_t rv_flt_d(int rd, int rs1, int rs2) {
    return 0xA2000053u | (((uint32_t)rd & 0x1F) << 7) | (((uint32_t)rs1 & 0x1F) << 15) | (((uint32_t)rs2 & 0x1F) << 20) | (0x01u << 25);
}
/* FCVT.D.W rd, rs1 (int32→float64) */
static uint32_t rv_fcvt_d_w(int rd, int rs1) {
    return 0xD2200053u | (((uint32_t)rd & 0x1F) << 7) | (((uint32_t)rs1 & 0x1F) << 15);
}
/* FCVT.W.D rd, rs1 (float64→int32, truncate) */
static uint32_t rv_fcvt_w_d(int rd, int rs1) {
    return 0xC2200053u | (((uint32_t)rd & 0x1F) << 7) | (((uint32_t)rs1 & 0x1F) << 15) | (0x03u << 20);
}
/* FLD rd, imm12(rs1) -- load double */
static uint32_t rv_fld(int rd, int rs1, int16_t imm) {
    return RV_I(rd, rs1, (uint32_t)(imm & 0xFFF), F3_DWORD, 0x07);
}
/* FSD rs2, imm12(rs1) -- store double */
static uint32_t rv_fsd(int rs2, int rs1, int16_t imm) {
    return RV_S(rs2, rs1, (uint32_t)(imm & 0xFFF), F3_DWORD, 0x27);
}
/* FMV.W.X rd, rs1 (move int32 to float reg) */
static uint32_t rv_fmv_w_x(int rd, int rs1) {
    return 0xF2000053u | (((uint32_t)rd & 0x1F) << 7) | (((uint32_t)rs1 & 0x1F) << 15);
}
/* FMV.X.W rd, rs1 (move float reg to int32) */
static uint32_t rv_fmv_x_w(int rd, int rs1) {
    return 0xE2000053u | (((uint32_t)rd & 0x1F) << 7) | (((uint32_t)rs1 & 0x1F) << 15);
}
/* FMV.D.X rd, rs1 (move int64 to float reg) */
static uint32_t rv_fmv_d_x(int rd, int rs1) {
    return 0xF2200053u | (((uint32_t)rd & 0x1F) << 7) | (((uint32_t)rs1 & 0x1F) << 15);
}
/* FMV.X.D rd, rs1 (move float reg to int64) */
static uint32_t rv_fmv_x_d(int rd, int rs1) {
    return 0xE2200053u | (((uint32_t)rd & 0x1F) << 7) | (((uint32_t)rs1 & 0x1F) << 15);
}
