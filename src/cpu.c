#include "cpu.h"
#include "stdio.h"
#include "stdlib.h"

u8 read_cycle(GameBoy* gb, u16 addr) {
    u8 data = read(gb, addr);
    cycle(gb);
    return data;
}
u8 read_imm_cycle(GameBoy* gb) { return read_cycle(gb, gb->pc++); }
u16 read_cycle16(GameBoy* gb, u16 addr) {
    u8 lo = read_cycle(gb, addr);
    u8 hi = read_cycle(gb, addr + 1);
    return (hi << 8) + lo;
}
u16 read_imm_cycle16(GameBoy* gb) {
    u8 lo = read_imm_cycle(gb);
    u8 hi = read_imm_cycle(gb);
    return (hi << 8) + lo;
}

void write_cycle(GameBoy* gb, u16 addr, u8 data) {
    write(gb, addr, data);
    cycle(gb);
}
void write_cycle16(GameBoy* gb, u16 addr, u16 data) {
    write_cycle(gb, addr, data & 0xFF);
    write_cycle(gb, addr + 1, data >> 8);
}

// Pre-decrement is intentional and important
// Also note that this takes 3 cycles instead of 2
void push_cycle16(GameBoy* gb, u16 data) {
    cycle(gb);
    write_cycle(gb, --gb->sp, data >> 8);
    write_cycle(gb, --gb->sp, data & 0xFF);
}
u16 pop_cycle16(GameBoy* gb) {
    u8 lo = read_cycle(gb, gb->sp++);
    u8 hi = read_cycle(gb, gb->sp++);
    return (hi << 8) + lo;
}

u16 get_af(GameBoy* gb) {
    return (gb->a << 8) + (gb->f_z << 7) + (gb->f_n << 6) + (gb->f_h << 5) +
           (gb->f_c << 4);
}
void set_af(GameBoy* gb, u16 af) {
    gb->a = af >> 8;
    gb->f_z = af & (1 << 7);
    gb->f_n = af & (1 << 6);
    gb->f_h = af & (1 << 5);
    gb->f_c = af & (1 << 4);
}

typedef void (*OpFuncPtr)(GameBoy*);

// Used to compactly define families of opcodes for all possible registers
#define DEF_ALL_REG(MACRO)                                                     \
    MACRO(a) MACRO(b) MACRO(c) MACRO(d) MACRO(e) MACRO(h) MACRO(l)
#define DEF_ALL_REG16(MACRO) MACRO(bc) MACRO(de) MACRO(hl)
#define DEF_ALL_COND(MACRO)                                                    \
    MACRO(z, gb->f_z) MACRO(nz, !gb->f_z) MACRO(c, gb->f_c) MACRO(nc, !gb->f_c)

// LD r,r'
#define LD_R_R(R1, R2)                                                         \
    static void ld_##R1##_##R2(GameBoy* gb) { gb->R1 = gb->R2; }
// clang-format off
             LD_R_R(a, b) LD_R_R(a, c) LD_R_R(a, d) LD_R_R(a, e) LD_R_R(a, h) LD_R_R(a, l)
LD_R_R(b, a)              LD_R_R(b, c) LD_R_R(b, d) LD_R_R(b, e) LD_R_R(b, h) LD_R_R(b, l)
LD_R_R(c, a) LD_R_R(c, b)              LD_R_R(c, d) LD_R_R(c, e) LD_R_R(c, h) LD_R_R(c, l)
LD_R_R(d, a) LD_R_R(d, b) LD_R_R(d, c)              LD_R_R(d, e) LD_R_R(d, h) LD_R_R(d, l)
LD_R_R(e, a) LD_R_R(e, b) LD_R_R(e, c) LD_R_R(e, d)              LD_R_R(e, h) LD_R_R(e, l)
LD_R_R(h, a) LD_R_R(h, b) LD_R_R(h, c) LD_R_R(h, d) LD_R_R(h, e)              LD_R_R(h, l)
LD_R_R(l, a) LD_R_R(l, b) LD_R_R(l, c) LD_R_R(l, d) LD_R_R(l, e) LD_R_R(l, h)
; // clang-format on

// LD r, n
#define LD_R_N(R)                                                              \
    static void ld_##R##_n(GameBoy* gb) { gb->R = read_imm_cycle(gb); }
DEF_ALL_REG(LD_R_N)

// LD r, [HL]
#define LD_R_HL(R)                                                             \
    static void ld_##R##_hl(GameBoy* gb) { gb->R = read_cycle(gb, gb->hl); }
DEF_ALL_REG(LD_R_HL)

// LD [HL], r
#define LD_HL_R(R)                                                             \
    static void ld_hl_##R(GameBoy* gb) { write_cycle(gb, gb->hl, gb->R); }
DEF_ALL_REG(LD_HL_R)

// LD [HL], n
static void ld_hl_n(GameBoy* gb) {
    u8 n = read_imm_cycle(gb);
    write_cycle(gb, gb->hl, n);
}

// LD A, [BC]
static void ld_a_bc(GameBoy* gb) { gb->a = read_cycle(gb, gb->bc); }

// LD A, [DE]
static void ld_a_de(GameBoy* gb) { gb->a = read_cycle(gb, gb->de); }

// LD [BC], A
static void ld_bc_a(GameBoy* gb) { write_cycle(gb, gb->bc, gb->a); }

// LD [DE], A
static void ld_de_a(GameBoy* gb) { write_cycle(gb, gb->de, gb->a); }

// LD A, [nn]
static void ld_a_nn(GameBoy* gb) {
    u16 nn = read_imm_cycle16(gb);
    gb->a = read_cycle(gb, nn);
}

// LD [nn], A
static void ld_nn_a(GameBoy* gb) {
    u16 nn = read_imm_cycle16(gb);
    write_cycle(gb, nn, gb->a);
}

// LDH A, [C]
static void ldh_a_c(GameBoy* gb) { gb->a = read_cycle(gb, 0xFF00 + gb->c); }

// LDH [C], A
static void ldh_c_a(GameBoy* gb) { write_cycle(gb, 0xFF00 + gb->c, gb->a); }

// LDH A, [n]
static void ldh_a_n(GameBoy* gb) {
    u8 n = read_imm_cycle(gb);
    gb->a = read_cycle(gb, 0xFF00 + n);
}

// LDH [n], A
static void ldh_n_a(GameBoy* gb) {
    u8 n = read_imm_cycle(gb);
    write_cycle(gb, 0xFF00 + n, gb->a);
}

// LD A, [HL-]
static void ld_a_hld(GameBoy* gb) { gb->a = read_cycle(gb, gb->hl--); }

// LD [HL-], A
static void ld_hld_a(GameBoy* gb) { write_cycle(gb, gb->hl--, gb->a); }

// LD A, [HL+]
static void ld_a_hli(GameBoy* gb) { gb->a = read_cycle(gb, gb->hl++); }

// LD [HL+], A
static void ld_hli_a(GameBoy* gb) { write_cycle(gb, gb->hl++, gb->a); }

// LD rr, nn
#define LD_RR_NN(RR)                                                           \
    static void ld_##RR##_nn(GameBoy* gb) {                                    \
        u16 nn = read_imm_cycle16(gb);                                         \
        gb->RR = nn;                                                           \
    }
DEF_ALL_REG16(LD_RR_NN)
LD_RR_NN(sp)

// LD [nn], SP
static void ld_nn_sp(GameBoy* gb) {
    u16 nn = read_imm_cycle16(gb);
    write_cycle16(gb, nn, gb->sp);
}

// LD SP, HL
static void ld_sp_hl(GameBoy* gb) {
    gb->sp = gb->hl;
    cycle(gb);
}

// PUSH rr (does not handle PUSH AF!)
#define PUSH_RR(RR)                                                            \
    static void push_##RR(GameBoy* gb) { push_cycle16(gb, gb->RR); }
DEF_ALL_REG16(PUSH_RR)

// PUSH AF
static void push_af(GameBoy* gb) {
    u16 af = get_af(gb);
    push_cycle16(gb, af);
}

// POP rr (does not handle POP AF!)
#define POP_RR(RR)                                                             \
    static void pop_##RR(GameBoy* gb) { gb->RR = pop_cycle16(gb); }
DEF_ALL_REG16(POP_RR)

// POP AF
static void pop_af(GameBoy* gb) {
    u16 af = pop_cycle16(gb);
    set_af(gb, af);
}

// LD HL, SP+e
static void ld_hl_sp_e(GameBoy* gb) {
    u8 e = read_imm_cycle(gb);
    gb->f_h = (gb->sp & 0xF) + (gb->e & 0xF) > 0xF;
    gb->f_c = (gb->sp & 0xFF) + gb->e > 0xFF;
    gb->hl = gb->sp + (s8)e;
    gb->f_z = 0;
    gb->f_n = 0;
    cycle(gb);
}

// Helper operations for ALU operations
static inline void alu_add(GameBoy* gb, u8 data) {
    gb->f_h = (gb->a & 0xF) + (data & 0xF) > 0xF;
    gb->f_c = gb->a + data > 0xFF;
    gb->a += data;
    gb->f_z = !gb->a;
    gb->f_n = 0;
}

static inline void alu_adc(GameBoy* gb, u8 data) {
    gb->f_h = (gb->a & 0xF) + (data & 0xF) + gb->f_c > 0xF;
    gb->f_c = gb->a + data + gb->f_c > 0xFF;
    gb->a += data + gb->f_c;
    gb->f_z = !gb->a;
    gb->f_n = 0;
}

static inline void alu_sub(GameBoy* gb, u8 data) {
    gb->f_h = (gb->a & 0xF) < (data & 0xF);
    gb->f_c = gb->a < data;
    gb->a -= data;
    gb->f_z = !gb->a;
    gb->f_n = 1;
}

static inline void alu_sbc(GameBoy* gb, u8 data) {
    gb->f_h = (gb->a & 0xF) < (data & 0xF) + gb->f_c;
    gb->f_c = gb->a < data + gb->f_c;
    gb->a -= data + gb->f_c;
    gb->f_z = !gb->a;
    gb->f_n = 1;
}

static inline void alu_cp(GameBoy* gb, u8 data) {
    gb->f_z = gb->a == data;
    gb->f_n = 1;
    gb->f_h = (gb->a & 0xF) < (data & 0xF);
    gb->f_c = gb->a < data;
}

static inline void alu_and(GameBoy* gb, u8 data) {
    gb->a &= data;
    gb->f_z = !gb->a;
    gb->f_n = 0;
    gb->f_h = 1;
    gb->f_c = 0;
}

static inline void alu_or(GameBoy* gb, u8 data) {
    gb->a |= data;
    gb->f_z = !gb->a;
    gb->f_n = 0;
    gb->f_h = 0;
    gb->f_c = 0;
}

static inline void alu_xor(GameBoy* gb, u8 data) {
    gb->a ^= data;
    gb->f_z = !gb->a;
    gb->f_n = 0;
    gb->f_h = 0;
    gb->f_c = 0;
}

// Helper macros to generate ALU opcode implementations
#define ALU_REG_OP(OP, R)                                                      \
    static void OP##_##R(GameBoy* gb) { alu_##OP(gb, gb->R); }
#define ALU_HL_OP(OP)                                                          \
    static void OP##_hl(GameBoy* gb) {                                         \
        u8 data = read_cycle(gb, gb->hl);                                      \
        alu_##OP(gb, data);                                                    \
    }
#define ALU_IMM_OP(OP)                                                         \
    static void OP##_n(GameBoy* gb) {                                          \
        u8 data = read_imm_cycle(gb);                                          \
        alu_##OP(gb, data);                                                    \
    }
#define DEF_ALU_OP(OP)                                                         \
    ALU_REG_OP(OP, a)                                                          \
    ALU_REG_OP(OP, b)                                                          \
    ALU_REG_OP(OP, c)                                                          \
    ALU_REG_OP(OP, d)                                                          \
    ALU_REG_OP(OP, e)                                                          \
    ALU_REG_OP(OP, h) ALU_REG_OP(OP, l) ALU_HL_OP(OP) ALU_IMM_OP(OP)
DEF_ALU_OP(add)
DEF_ALU_OP(adc)
DEF_ALU_OP(sub)
DEF_ALU_OP(sbc)
DEF_ALU_OP(cp)
DEF_ALU_OP(and)
DEF_ALU_OP(or)
DEF_ALU_OP (xor)

// INC r
#define INC_R(R)                                                               \
    static void inc_##R(GameBoy* gb) {                                         \
        gb->R++;                                                               \
        gb->f_z = !gb->R;                                                      \
        gb->f_n = 0;                                                           \
        gb->f_h = !(gb->R & 0xF);                                              \
    }
DEF_ALL_REG(INC_R)

// INC [HL] (function name avoids conflict with INC HL)
static void inc_ahl(GameBoy* gb) {
    u8 data = read_cycle(gb, gb->hl);
    data++;
    gb->f_z = !data;
    gb->f_n = 0;
    gb->f_h = !(data & 0xF);
    write_cycle(gb, gb->hl, data);
}

// DEC r
#define DEC_R(R)                                                               \
    static void dec_##R(GameBoy* gb) {                                         \
        gb->R--;                                                               \
        gb->f_z = !gb->R;                                                      \
        gb->f_n = 1;                                                           \
        gb->f_h = (gb->R & 0xF) == 0xF;                                        \
    }
DEF_ALL_REG(DEC_R)

// DEC [HL] (function name avoids conflict with DEC HL)
static void dec_ahl(GameBoy* gb) {
    u8 data = read_cycle(gb, gb->hl);
    data--;
    gb->f_z = !data;
    gb->f_n = 1;
    gb->f_h = (data & 0xF) == 0xF;
    write_cycle(gb, gb->hl, data);
}

// CCF
static void ccf(GameBoy* gb) {
    gb->f_n = 0;
    gb->f_h = 0;
    gb->f_c = !gb->f_c;
}

// SCF
static void scf(GameBoy* gb) {
    gb->f_n = 0;
    gb->f_h = 0;
    gb->f_c = 1;
}

// DAA
// To-do implement this
static void daa(GameBoy* gb) { (void)gb; }

// CPL
static void cpl(GameBoy* gb) {
    gb->a = ~gb->a;
    gb->f_n = 1;
    gb->f_h = 1;
}

// INC rr
#define INC_RR(RR)                                                             \
    static void inc_##RR(GameBoy* gb) {                                        \
        gb->RR++;                                                              \
        cycle(gb);                                                             \
    }
DEF_ALL_REG16(INC_RR)
INC_RR(sp)

// DEC rr
#define DEC_RR(RR)                                                             \
    static void dec_##RR(GameBoy* gb) {                                        \
        gb->RR--;                                                              \
        cycle(gb);                                                             \
    }
DEF_ALL_REG16(DEC_RR)
DEC_RR(sp)

// ADD HL, rr
#define ADD_HL_RR(RR)                                                          \
    static void add_hl_##RR(GameBoy* gb) {                                     \
        gb->f_h = (gb->hl & 0xFFF) + (gb->RR & 0xFFF) > 0xFFF;                 \
        gb->f_c = gb->hl + gb->RR > 0xFFF;                                     \
        gb->hl += gb->RR;                                                      \
        gb->f_n = 0;                                                           \
        cycle(gb);                                                             \
    }
DEF_ALL_REG16(ADD_HL_RR)
ADD_HL_RR(sp)

// ADD SP, e
static void add_sp_e(GameBoy* gb) {
    u8 e = read_imm_cycle(gb);
    gb->f_h = (gb->sp & 0xF) + (e & 0xF) > 0xF;
    gb->f_c = (gb->sp & 0xFF) + e > 0xFF;
    gb->sp += (s8)e;
    gb->f_z = 0;
    gb->f_n = 0;
    cycle(gb);
    cycle(gb);
}

// RLCA
static void rlca(GameBoy* gb) {
    gb->a = (gb->a << 1) + (gb->a >> 7);
    gb->f_z = gb->f_n = gb->f_h = 0;
    gb->f_c = gb->a & 0x01;
}

// RRCA
static void rrca(GameBoy* gb) {
    gb->a = (gb->a >> 1) + (gb->a << 7);
    gb->f_z = gb->f_n = gb->f_h = 0;
    gb->f_c = gb->a & 0x80;
}

// RLA
static void rla(GameBoy* gb) {
    bool carry = gb->a & 0x80;
    gb->a = (gb->a << 1) + gb->f_c;
    gb->f_c = carry;
    gb->f_z = gb->f_n = gb->f_h = 0;
}

// RRA
static void rra(GameBoy* gb) {
    bool carry = gb->a & 0x01;
    gb->a = (gb->a >> 1) + (gb->f_c << 7);
    gb->f_c = carry;
    gb->f_z = gb->f_n = gb->f_h = 0;
}

// Helper functions for CB opcodes
static inline u8 cb_rlc(GameBoy* gb, u8 data) {
    data = (data << 1) + (data >> 7);
    gb->f_z = !data;
    gb->f_n = gb->f_h = 0;
    gb->f_c = data & 0x01;
    return data;
}

static inline u8 cb_rrc(GameBoy* gb, u8 data) {
    data = (data >> 1) + (data << 7);
    gb->f_z = !data;
    gb->f_n = gb->f_h = 0;
    gb->f_c = data & 0x80;
    return data;
}

static inline u8 cb_rl(GameBoy* gb, u8 data) {
    bool carry = data & 0x80;
    data = (data << 1) + gb->f_c;
    gb->f_c = carry;
    gb->f_z = !data;
    gb->f_n = gb->f_h = 0;
    return data;
}

static inline u8 cb_rr(GameBoy* gb, u8 data) {
    bool carry = data & 0x01;
    data = (data >> 1) + (gb->f_c << 7);
    gb->f_c = carry;
    gb->f_z = !data;
    gb->f_n = gb->f_h = 0;
    return data;
}

static inline u8 cb_sla(GameBoy* gb, u8 data) {
    gb->f_c = data & 0x80;
    data <<= 1;
    gb->f_z = !data;
    gb->f_n = gb->f_h = 0;
    return data;
}

static inline u8 cb_sra(GameBoy* gb, u8 data) {
    gb->f_c = data & 0x01;
    data = (data > 1) + (data & 0x80);
    gb->f_z = !data;
    gb->f_n = gb->f_h = 0;
    return data;
}

static inline u8 cb_swap(GameBoy* gb, u8 data) {
    data = (data << 4) + (data >> 4);
    gb->f_z = !data;
    gb->f_n = gb->f_h = gb->f_c = 0;
    return data;
}

static inline u8 cb_srl(GameBoy* gb, u8 data) {
    gb->f_c = data & 0x01;
    data >>= 1;
    gb->f_z = !data;
    gb->f_n = gb->f_h = 0;
    return data;
}

// Helper macros to generate CB opcode implementations
#define CB_REG_OP(OP, R)                                                       \
    static void OP##_##R(GameBoy* gb) { gb->R = cb_##OP(gb, gb->R); }
#define CB_HL_OP(OP)                                                           \
    static void OP##_hl(GameBoy* gb) {                                         \
        u8 data = read_cycle(gb, gb->hl);                                      \
        data = cb_##OP(gb, data);                                              \
        write_cycle(gb, gb->hl, data);                                         \
    }
#define DEF_CB_OP(OP)                                                          \
    CB_REG_OP(OP, a)                                                           \
    CB_REG_OP(OP, b)                                                           \
    CB_REG_OP(OP, c)                                                           \
    CB_REG_OP(OP, d)                                                           \
    CB_REG_OP(OP, e) CB_REG_OP(OP, h) CB_REG_OP(OP, l) CB_HL_OP(OP)
DEF_CB_OP(rlc)
DEF_CB_OP(rrc)
DEF_CB_OP(rl)
DEF_CB_OP(rr)
DEF_CB_OP(sla)
DEF_CB_OP(sra)
DEF_CB_OP(swap)
DEF_CB_OP(srl)

// BIT b, r
#define BIT_B_R(B, R)                                                          \
    static void bit_##B##_##R(GameBoy* gb) {                                   \
        gb->f_z = !(gb->R & (1 << B));                                         \
        gb->f_n = 0;                                                           \
        gb->f_h = 1;                                                           \
    }

// BIT b, [HL]
#define BIT_B_HL(B)                                                            \
    static void bit_##B##_hl(GameBoy* gb) {                                    \
        u8 data = read_cycle(gb, gb->hl);                                      \
        gb->f_z = !(data & (1 << B));                                          \
        gb->f_n = 0;                                                           \
        gb->f_h = 1;                                                           \
    }

// RES b, r
#define RES_B_R(B, R)                                                          \
    static void res_##B##_##R(GameBoy* gb) { gb->R &= ~(1 << B); }

// RES b, [HL]
#define RES_B_HL(B)                                                            \
    static void res_##B##_hl(GameBoy* gb) {                                    \
        u8 data = read_cycle(gb, gb->hl);                                      \
        data &= ~(1 << B);                                                     \
        write_cycle(gb, gb->hl, data);                                         \
    }

// SET b, r
#define SET_B_R(B, R)                                                          \
    static void set_##B##_##R(GameBoy* gb) { gb->R |= (1 << B); }

// SET b, [HL]
#define SET_B_HL(B)                                                            \
    static void set_##B##_hl(GameBoy* gb) {                                    \
        u8 data = read_cycle(gb, gb->hl);                                      \
        data |= (1 << B);                                                      \
        write_cycle(gb, gb->hl, data);                                         \
    }

#define DEF_BIT_REG(OP, B)                                                     \
    OP(B, a) OP(B, b) OP(B, c) OP(B, d) OP(B, e) OP(B, h) OP(B, l)

#define DEF_BIT2(OP)                                                           \
    DEF_BIT_REG(OP, 0)                                                         \
    DEF_BIT_REG(OP, 1)                                                         \
    DEF_BIT_REG(OP, 2)                                                         \
    DEF_BIT_REG(OP, 3)                                                         \
    DEF_BIT_REG(OP, 4) DEF_BIT_REG(OP, 5) DEF_BIT_REG(OP, 6) DEF_BIT_REG(OP, 7)
DEF_BIT2(BIT_B_R)
DEF_BIT2(RES_B_R)
DEF_BIT2(SET_B_R)

#define DEF_BIT_HL(OP) OP(0) OP(1) OP(2) OP(3) OP(4) OP(5) OP(6) OP(7)
DEF_BIT_HL(BIT_B_HL)
DEF_BIT_HL(RES_B_HL)
DEF_BIT_HL(SET_B_HL)

// JP nn
static void jp_nn(GameBoy* gb) {
    gb->pc = read_imm_cycle16(gb);
    cycle(gb);
}

// JP HL
static void jp_hl(GameBoy* gb) { gb->pc = gb->hl; }

// JP cc, nn
// CC is used in generating the function name (i.e. "nz")
// COND is used in the actual code (i.e. "!gb->f_z")
#define JP_CC_NN(CC, COND)                                                     \
    static void jp_##CC##_nn(GameBoy* gb) {                                    \
        u16 nn = read_imm_cycle16(gb);                                         \
        if (COND) {                                                            \
            gb->pc = nn;                                                       \
            cycle(gb);                                                         \
        }                                                                      \
    }
DEF_ALL_COND(JP_CC_NN)

// JR e
static void jr_e(GameBoy* gb) {
    gb->pc += (s8)read_imm_cycle(gb);
    cycle(gb);
}

// JR cc, e
#define JR_CC_E(CC, COND)                                                      \
    static void jr_##CC##_e(GameBoy* gb) {                                     \
        s8 e = read_imm_cycle(gb);                                             \
        if (COND) {                                                            \
            gb->pc += e;                                                       \
            cycle(gb);                                                         \
        }                                                                      \
    }
DEF_ALL_COND(JR_CC_E)

// CALL nn
static void call_nn(GameBoy* gb) {
    u16 nn = read_imm_cycle16(gb);
    push_cycle16(gb, gb->pc);
    gb->pc = nn;
}

// CALL cc, nn
#define CALL_CC_NN(CC, COND)                                                   \
    static void call_##CC##_nn(GameBoy* gb) {                                  \
        u16 nn = read_imm_cycle16(gb);                                         \
        if (COND) {                                                            \
            push_cycle16(gb, gb->pc);                                          \
            gb->pc = nn;                                                       \
        }                                                                      \
    }
DEF_ALL_COND(CALL_CC_NN)

// RET
static void ret(GameBoy* gb) {
    gb->pc = pop_cycle16(gb);
    cycle(gb);
}

// RET cc
#define RET_CC(CC, COND)                                                       \
    static void ret_##CC(GameBoy* gb) {                                        \
        cycle(gb);                                                             \
        if (COND) {                                                            \
            gb->pc = pop_cycle16(gb);                                          \
            cycle(gb);                                                         \
        }                                                                      \
    }
DEF_ALL_COND(RET_CC)

// RETI
static void reti(GameBoy* gb) {
    gb->pc = pop_cycle16(gb);
    gb->ime = true;
    cycle(gb);
}

// RST n
#define RST_N(N)                                                               \
    static void rst_##N(GameBoy* gb) {                                         \
        push_cycle16(gb, gb->pc);                                              \
        gb->pc = N;                                                            \
    }
RST_N(0x00)
RST_N(0x08)
RST_N(0x10)
RST_N(0x18)
RST_N(0x20)
RST_N(0x28)
RST_N(0x30)
RST_N(0x38)

// HALT
static void halt(GameBoy* gb) {
    (void)gb;
    printf("HALT not implemented yet!\n");
    exit(1);
}

// STOP
static void stop(GameBoy* gb) {
    (void)gb;
    printf("STOP not implemented yet!\n");
    exit(1);
}

// DI
static void di(GameBoy* gb) { gb->ime = false; }

// EI
static void ei(GameBoy* gb) { gb->ime = true; }

// NOP
static void nop(GameBoy* gb) {
    // Prevent compiler from complaining about unused parameter
    (void)gb;
}

// Arranged in octal for space reasons
// clang-format off
OpFuncPtr cb_ptrs[] = {
//             x0       x1       x2       x3       x4       x5        x6       x7
/*  0x */   rlc_b,   rlc_c,   rlc_d,   rlc_e,   rlc_h,   rlc_l,   rlc_hl,   rlc_a,
/*  1x */   rrc_b,   rrc_c,   rrc_d,   rrc_e,   rrc_h,   rrc_l,   rrc_hl,   rrc_a,
/*  2x */    rl_b,    rl_c,    rl_d,    rl_e,    rl_h,    rl_l,    rl_hl,    rl_a,
/*  3x */    rr_b,    rr_c,    rr_d,    rr_e,    rr_h,    rr_l,    rr_hl,    rr_a,
/*  4x */   sla_b,   sla_c,   sla_d,   sla_e,   sla_h,   sla_l,   sla_hl,   sla_a,
/*  5x */   sra_b,   sra_c,   sra_d,   sra_e,   sra_h,   sra_l,   sra_hl,   sra_a,
/*  6x */  swap_b,  swap_c,  swap_d,  swap_e,  swap_h,  swap_l,  swap_hl,  swap_a,
/*  7x */   srl_b,   srl_c,   srl_d,   srl_e,   srl_h,   srl_l,   srl_hl,   srl_a,
/* 10x */ bit_0_b, bit_0_c, bit_0_d, bit_0_e, bit_0_h, bit_0_l, bit_0_hl, bit_0_a,
/* 11x */ bit_1_b, bit_1_c, bit_1_d, bit_1_e, bit_1_h, bit_1_l, bit_1_hl, bit_1_a,
/* 12x */ bit_2_b, bit_2_c, bit_2_d, bit_2_e, bit_2_h, bit_2_l, bit_2_hl, bit_2_a,
/* 13x */ bit_3_b, bit_3_c, bit_3_d, bit_3_e, bit_3_h, bit_3_l, bit_3_hl, bit_3_a,
/* 14x */ bit_4_b, bit_4_c, bit_4_d, bit_4_e, bit_4_h, bit_4_l, bit_4_hl, bit_4_a,
/* 15x */ bit_5_b, bit_5_c, bit_5_d, bit_5_e, bit_5_h, bit_5_l, bit_5_hl, bit_5_a,
/* 16x */ bit_6_b, bit_6_c, bit_6_d, bit_6_e, bit_6_h, bit_6_l, bit_6_hl, bit_6_a,
/* 17x */ bit_7_b, bit_7_c, bit_7_d, bit_7_e, bit_7_h, bit_7_l, bit_7_hl, bit_7_a,
/* 20x */ res_0_b, res_0_c, res_0_d, res_0_e, res_0_h, res_0_l, res_0_hl, res_0_a,
/* 21x */ res_1_b, res_1_c, res_1_d, res_1_e, res_1_h, res_1_l, res_1_hl, res_1_a,
/* 22x */ res_2_b, res_2_c, res_2_d, res_2_e, res_2_h, res_2_l, res_2_hl, res_2_a,
/* 23x */ res_3_b, res_3_c, res_3_d, res_3_e, res_3_h, res_3_l, res_3_hl, res_3_a,
/* 24x */ res_4_b, res_4_c, res_4_d, res_4_e, res_4_h, res_4_l, res_4_hl, res_4_a,
/* 25x */ res_5_b, res_5_c, res_5_d, res_5_e, res_5_h, res_5_l, res_5_hl, res_5_a,
/* 26x */ res_6_b, res_6_c, res_6_d, res_6_e, res_6_h, res_6_l, res_6_hl, res_6_a,
/* 27x */ res_7_b, res_7_c, res_7_d, res_7_e, res_7_h, res_7_l, res_7_hl, res_7_a,
/* 30x */ set_0_b, set_0_c, set_0_d, set_0_e, set_0_h, set_0_l, set_0_hl, set_0_a,
/* 31x */ set_1_b, set_1_c, set_1_d, set_1_e, set_1_h, set_1_l, set_1_hl, set_1_a,
/* 32x */ set_2_b, set_2_c, set_2_d, set_2_e, set_2_h, set_2_l, set_2_hl, set_2_a,
/* 33x */ set_3_b, set_3_c, set_3_d, set_3_e, set_3_h, set_3_l, set_3_hl, set_3_a,
/* 34x */ set_4_b, set_4_c, set_4_d, set_4_e, set_4_h, set_4_l, set_4_hl, set_4_a,
/* 35x */ set_5_b, set_5_c, set_5_d, set_5_e, set_5_h, set_5_l, set_5_hl, set_5_a,
/* 36x */ set_6_b, set_6_c, set_6_d, set_6_e, set_6_h, set_6_l, set_6_hl, set_6_a,
/* 37x */ set_7_b, set_7_c, set_7_d, set_7_e, set_7_h, set_7_l, set_7_hl, set_7_a,
};
// clang-format on

static void op_cb(GameBoy* gb) {
    u8 opcode = read_imm_cycle(gb);
    OpFuncPtr func = cb_ptrs[opcode];
    func(gb);
}

static void op_ill(GameBoy* gb) {
    (void)gb;
    printf("Illegal opcode!\n");
    exit(1);
}

// Arranged in octal for space reasons
// clang-format off
OpFuncPtr op_ptrs[] = {
//                x0         x1        x2       x3          x4       x5       x6        x7
/*  0x */        nop,  ld_bc_nn,  ld_bc_a,  inc_bc,      inc_b,   dec_b,  ld_b_n,     rlca,
/*  1x */   ld_nn_sp, add_hl_bc,  ld_a_bc,  dec_bc,      inc_c,   dec_c,  ld_c_n,     rrca,
/*  2x */       stop,  ld_de_nn,  ld_de_a,  inc_de,      inc_d,   dec_d,  ld_d_n,      rla,
/*  3x */       jr_e, add_hl_de,  ld_a_de,  dec_de,      inc_e,   dec_e,  ld_e_n,      rra,
/*  4x */    jr_nz_e,  ld_hl_nn, ld_hli_a,  inc_hl,      inc_h,   dec_h,  ld_h_n,      daa,
/*  5x */     jr_z_e, add_hl_hl, ld_a_hli,  dec_hl,      inc_l,   dec_l,  ld_l_n,      cpl,
/*  6x */    jr_nc_e,  ld_sp_nn, ld_hld_a,  inc_sp,    inc_ahl, dec_ahl, ld_hl_n,      scf,
/*  7x */     jr_c_e, add_hl_sp, ld_a_hld,  dec_sp,      inc_a,   dec_a,  ld_a_n,      ccf,
/* 10x */        nop,    ld_b_c,   ld_b_d,  ld_b_e,     ld_b_h,  ld_b_l, ld_b_hl,   ld_b_a,
/* 11x */     ld_c_b,       nop,   ld_c_d,  ld_c_e,     ld_c_h,  ld_c_l, ld_c_hl,   ld_c_a,
/* 12x */     ld_d_b,    ld_d_c,      nop,  ld_d_e,     ld_d_h,  ld_d_l, ld_d_hl,   ld_d_a,
/* 13x */     ld_e_b,    ld_e_c,   ld_e_d,     nop,     ld_e_h,  ld_e_l, ld_e_hl,   ld_e_a,
/* 14x */     ld_h_b,    ld_h_c,   ld_h_d,  ld_h_e,        nop,  ld_h_l, ld_h_hl,   ld_h_a,
/* 15x */     ld_l_b,    ld_l_c,   ld_l_d,  ld_l_e,     ld_l_h,     nop, ld_l_hl,   ld_l_a,
/* 16x */    ld_hl_b,   ld_hl_c,  ld_hl_d, ld_hl_e,    ld_hl_h, ld_hl_l,    halt,  ld_hl_a,
/* 17x */     ld_a_b,    ld_a_c,   ld_a_d,  ld_a_e,     ld_a_h,  ld_a_l, ld_a_hl,      nop,
/* 20x */      add_b,     add_c,    add_d,   add_e,      add_h,   add_l,  add_hl,    add_a,
/* 21x */      adc_b,     adc_c,    adc_d,   adc_e,      adc_h,   adc_l,  adc_hl,    adc_a,
/* 22x */      sub_b,     sub_c,    sub_d,   sub_e,      sub_h,   sub_l,  sub_hl,    sub_a,
/* 23x */      sbc_b,     sbc_c,    sbc_d,   sbc_e,      sbc_h,   sbc_l,  sbc_hl,    sbc_a,
/* 24x */      and_b,     and_c,    and_d,   and_e,      and_h,   and_l,  and_hl,    and_a,
/* 25x */      xor_b,     xor_c,    xor_d,   xor_e,      xor_h,   xor_l,  xor_hl,    xor_a,
/* 26x */       or_b,      or_c,     or_d,    or_e,       or_h,    or_l,   or_hl,     or_a,
/* 27x */       cp_b,      cp_c,     cp_d,    cp_e,       cp_h,    cp_l,   cp_hl,     cp_a,
/* 30x */     ret_nz,    pop_bc, jp_nz_nn,   jp_nn, call_nz_nn, push_bc,   add_n, rst_0x00,
/* 31x */      ret_z,       ret,  jp_z_nn,   op_cb,  call_z_nn, call_nn,   adc_n, rst_0x08,
/* 32x */     ret_nc,    pop_de, jp_nc_nn,  op_ill, call_nc_nn, push_de,   sub_n, rst_0x10,
/* 33x */      ret_c,      reti,  jp_c_nn,  op_ill,  call_c_nn,  op_ill,   sbc_n, rst_0x18,
/* 34x */    ldh_n_a,    pop_hl,  ldh_c_a,  op_ill,     op_ill, push_hl,   and_n, rst_0x20,
/* 35x */   add_sp_e,     jp_hl,  ld_nn_a,  op_ill,     op_ill,  op_ill,   xor_n, rst_0x28,
/* 36x */    ldh_a_n,    pop_af,  ldh_a_c,      di,     op_ill, push_af,    or_n, rst_0x30,
/* 37x */ ld_hl_sp_e,  ld_sp_hl,  ld_a_nn,      ei,     op_ill,  op_ill,    cp_n, rst_0x38,
};
// clang-format on

void run_opcode(GameBoy* gb) {
    // Handle interrupts
    if (gb->ime && (gb->ie & gb->if_)) {
        // At least one pending interrupt
        gb->ime = false;
        cycle(gb);
        push_cycle16(gb, gb->pc);

        if (gb->ie & gb->if_ & (1 << 0)) {
            // V-Blank interrupt
            gb->if_ &= ~(1 << 0);
            gb->pc = 0x40;
        } else if (gb->ie & gb->if_ & (1 << 1)) {
            // LCD/STAT interrupt
            gb->if_ &= ~(1 << 1);
            gb->pc = 0x48;
        } else if (gb->ie & gb->if_ & (1 << 2)) {
            // Timer interrupt
            gb->if_ &= ~(1 << 2);
            gb->pc = 0x50;
        } else if (gb->ie & gb->if_ & (1 << 3)) {
            // Serial interrupt
            gb->if_ &= ~(1 << 3);
            gb->pc = 0x58;
        } else if (gb->ie & gb->if_ & (1 << 4)) {
            // Joypad interrupt
            gb->if_ &= ~(1 << 4);
            gb->pc = 0x60;
        }
        cycle(gb);

        return;
    }

    u8 opcode = read_imm_cycle(gb);
    OpFuncPtr func = op_ptrs[opcode];
    func(gb);
}