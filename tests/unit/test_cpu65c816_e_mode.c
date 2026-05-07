/**
 * @file test_cpu65c816_e_mode.c
 * @brief Tests opcodes 6502-équivalents 65C816 en mode émulation (B1.4)
 * @author bmarty <bmarty@mailo.com>
 * @date 2026-05-07
 *
 * Couvre représentativement chaque famille d'opcodes implémentée dans
 * `cpu65c816_opcodes.c::cpu816_execute_opcode_e`, plus les vecteurs
 * d'interruption (IRQ/NMI/BRK/RTI), le bug JMP indirect (ADR-11), et
 * les opcodes illégaux NMOS traités comme NOP (ADR-11).
 */

#include <stdio.h>
#include <string.h>
#include "cpu/cpu65c816.h"
#include "cpu/cpu6502.h"
#include "memory/memory.h"

static int tests_passed = 0;
static int tests_failed = 0;

#define TEST(name) static void name(void)
#define RUN(name) do { \
    printf("  %-58s", #name); \
    name(); \
    tests_passed++; \
    printf("PASS\n"); \
} while(0)

#define ASSERT_EQ(a, b) do { \
    if ((a) != (b)) { \
        printf("FAIL\n    %s:%d: expected %ld, got %ld\n", __FILE__, __LINE__, (long)(b), (long)(a)); \
        tests_failed++; return; \
    } \
} while(0)

#define ASSERT_TRUE(x) do { \
    if (!(x)) { \
        printf("FAIL\n    %s:%d: expected true\n", __FILE__, __LINE__); \
        tests_failed++; return; \
    } \
} while(0)

#define ASSERT_FALSE(x) do { \
    if ((x)) { \
        printf("FAIL\n    %s:%d: expected false\n", __FILE__, __LINE__); \
        tests_failed++; return; \
    } \
} while(0)

/* ─── Helpers ───────────────────────────────────────────────────────── */

static void place_reset_vector(memory_t* mem, uint16_t addr) {
    mem->rom[0x3FFC] = (uint8_t)(addr & 0xFF);
    mem->rom[0x3FFD] = (uint8_t)(addr >> 8);
}

static void place_irq_vector(memory_t* mem, uint16_t addr) {
    mem->rom[0x3FFE] = (uint8_t)(addr & 0xFF);
    mem->rom[0x3FFF] = (uint8_t)(addr >> 8);
}

static void place_nmi_vector(memory_t* mem, uint16_t addr) {
    mem->rom[0x3FFA] = (uint8_t)(addr & 0xFF);
    mem->rom[0x3FFB] = (uint8_t)(addr >> 8);
}

static void boot(cpu65c816_t* cpu, memory_t* mem) {
    memory_init(mem);
    place_reset_vector(mem, 0x0200);
    cpu816_init(cpu, mem);
    cpu816_reset(cpu);
}

static void load(memory_t* mem, uint16_t addr, const uint8_t* code, size_t n) {
    for (size_t i = 0; i < n; i++) memory_write(mem, (uint16_t)(addr + i), code[i]);
}

/* Lit le low byte de C (équivalent A en mode E). */
static uint8_t a8(const cpu65c816_t* c) { return (uint8_t)(c->C & 0xFF); }
static uint8_t x8(const cpu65c816_t* c) { return (uint8_t)(c->X & 0xFF); }
static uint8_t y8(const cpu65c816_t* c) { return (uint8_t)(c->Y & 0xFF); }
static uint8_t sp8(const cpu65c816_t* c) { return (uint8_t)(c->S & 0xFF); }

/* ═══════════════════════════════════════════════════════════════════ */
/*  LD/ST                                                              */
/* ═══════════════════════════════════════════════════════════════════ */

TEST(test_lda_immediate) {
    cpu65c816_t cpu; memory_t mem; boot(&cpu, &mem);
    const uint8_t prog[] = { 0xA9, 0x42 }; load(&mem, 0x0200, prog, 2);
    ASSERT_EQ(cpu816_step(&cpu), 2);
    ASSERT_EQ((int)a8(&cpu), 0x42);
    ASSERT_FALSE(cpu.P & FLAG_ZERO);
    ASSERT_FALSE(cpu.P & FLAG_NEGATIVE);
}

TEST(test_lda_immediate_zero_flag) {
    cpu65c816_t cpu; memory_t mem; boot(&cpu, &mem);
    const uint8_t prog[] = { 0xA9, 0x00 }; load(&mem, 0x0200, prog, 2);
    cpu816_step(&cpu);
    ASSERT_TRUE(cpu.P & FLAG_ZERO);
}

TEST(test_lda_immediate_negative_flag) {
    cpu65c816_t cpu; memory_t mem; boot(&cpu, &mem);
    const uint8_t prog[] = { 0xA9, 0x80 }; load(&mem, 0x0200, prog, 2);
    cpu816_step(&cpu);
    ASSERT_TRUE(cpu.P & FLAG_NEGATIVE);
}

TEST(test_lda_zp_x) {
    cpu65c816_t cpu; memory_t mem; boot(&cpu, &mem);
    cpu.X = 0x05;
    memory_write(&mem, 0x0085, 0x77);
    const uint8_t prog[] = { 0xB5, 0x80 }; load(&mem, 0x0200, prog, 2);
    cpu816_step(&cpu);
    ASSERT_EQ((int)a8(&cpu), 0x77);
}

TEST(test_lda_abs) {
    cpu65c816_t cpu; memory_t mem; boot(&cpu, &mem);
    memory_write(&mem, 0x1234, 0xAB);
    const uint8_t prog[] = { 0xAD, 0x34, 0x12 }; load(&mem, 0x0200, prog, 3);
    cpu816_step(&cpu);
    ASSERT_EQ((int)a8(&cpu), 0xAB);
}

TEST(test_lda_abs_x_page_cross_penalty) {
    cpu65c816_t cpu; memory_t mem; boot(&cpu, &mem);
    cpu.X = 0x10;
    memory_write(&mem, 0x1300, 0x99);
    const uint8_t prog[] = { 0xBD, 0xF0, 0x12 }; load(&mem, 0x0200, prog, 3);
    int c = cpu816_step(&cpu);
    ASSERT_EQ((int)a8(&cpu), 0x99);
    ASSERT_EQ(c, 5); /* base 4 + 1 penalty */
}

TEST(test_ldx_immediate) {
    cpu65c816_t cpu; memory_t mem; boot(&cpu, &mem);
    const uint8_t prog[] = { 0xA2, 0x33 }; load(&mem, 0x0200, prog, 2);
    cpu816_step(&cpu);
    ASSERT_EQ((int)x8(&cpu), 0x33);
}

TEST(test_ldy_zp) {
    cpu65c816_t cpu; memory_t mem; boot(&cpu, &mem);
    memory_write(&mem, 0x0050, 0x44);
    const uint8_t prog[] = { 0xA4, 0x50 }; load(&mem, 0x0200, prog, 2);
    cpu816_step(&cpu);
    ASSERT_EQ((int)y8(&cpu), 0x44);
}

TEST(test_sta_abs) {
    cpu65c816_t cpu; memory_t mem; boot(&cpu, &mem);
    cpu.C = 0x12AB;  /* B=0x12, A=0xAB */
    const uint8_t prog[] = { 0x8D, 0x00, 0x30 }; load(&mem, 0x0200, prog, 3);
    cpu816_step(&cpu);
    ASSERT_EQ((int)memory_read(&mem, 0x3000), 0xAB);
}

TEST(test_stx_zp) {
    cpu65c816_t cpu; memory_t mem; boot(&cpu, &mem);
    cpu.X = 0x66;
    const uint8_t prog[] = { 0x86, 0x40 }; load(&mem, 0x0200, prog, 2);
    cpu816_step(&cpu);
    ASSERT_EQ((int)memory_read(&mem, 0x0040), 0x66);
}

TEST(test_sty_abs) {
    cpu65c816_t cpu; memory_t mem; boot(&cpu, &mem);
    cpu.Y = 0x77;
    const uint8_t prog[] = { 0x8C, 0x00, 0x30 }; load(&mem, 0x0200, prog, 3);
    cpu816_step(&cpu);
    ASSERT_EQ((int)memory_read(&mem, 0x3000), 0x77);
}

TEST(test_indexed_indirect_lda) {
    cpu65c816_t cpu; memory_t mem; boot(&cpu, &mem);
    cpu.X = 0x04;
    memory_write(&mem, 0x0024, 0x00);
    memory_write(&mem, 0x0025, 0x30);
    memory_write(&mem, 0x3000, 0xCD);
    const uint8_t prog[] = { 0xA1, 0x20 }; load(&mem, 0x0200, prog, 2);
    cpu816_step(&cpu);
    ASSERT_EQ((int)a8(&cpu), 0xCD);
}

TEST(test_indirect_indexed_lda_with_page_cross) {
    cpu65c816_t cpu; memory_t mem; boot(&cpu, &mem);
    cpu.Y = 0x20;
    memory_write(&mem, 0x0080, 0xF0);
    memory_write(&mem, 0x0081, 0x12);
    memory_write(&mem, 0x1310, 0xEE);  /* $12F0 + $20 = $1310 (page cross) */
    const uint8_t prog[] = { 0xB1, 0x80 }; load(&mem, 0x0200, prog, 2);
    int c = cpu816_step(&cpu);
    ASSERT_EQ((int)a8(&cpu), 0xEE);
    ASSERT_EQ(c, 6); /* base 5 + 1 penalty */
}

/* ═══════════════════════════════════════════════════════════════════ */
/*  ALU                                                                */
/* ═══════════════════════════════════════════════════════════════════ */

TEST(test_adc_simple) {
    cpu65c816_t cpu; memory_t mem; boot(&cpu, &mem);
    cpu.C = 0x05; cpu.P &= (uint8_t)~FLAG_CARRY;
    const uint8_t prog[] = { 0x69, 0x03 }; load(&mem, 0x0200, prog, 2);
    cpu816_step(&cpu);
    ASSERT_EQ((int)a8(&cpu), 0x08);
    ASSERT_FALSE(cpu.P & FLAG_CARRY);
}

TEST(test_adc_overflow_flag) {
    cpu65c816_t cpu; memory_t mem; boot(&cpu, &mem);
    cpu.C = 0x7F; cpu.P &= (uint8_t)~FLAG_CARRY;
    const uint8_t prog[] = { 0x69, 0x01 }; load(&mem, 0x0200, prog, 2);
    cpu816_step(&cpu);
    ASSERT_EQ((int)a8(&cpu), 0x80);
    ASSERT_TRUE(cpu.P & FLAG_OVERFLOW);
    ASSERT_TRUE(cpu.P & FLAG_NEGATIVE);
}

TEST(test_adc_bcd_mode) {
    cpu65c816_t cpu; memory_t mem; boot(&cpu, &mem);
    cpu.C = 0x09; cpu.P |= FLAG_DECIMAL; cpu.P &= (uint8_t)~FLAG_CARRY;
    const uint8_t prog[] = { 0x69, 0x01 }; load(&mem, 0x0200, prog, 2);
    cpu816_step(&cpu);
    ASSERT_EQ((int)a8(&cpu), 0x10); /* 09 + 01 = 10 en BCD */
}

TEST(test_sbc_simple) {
    cpu65c816_t cpu; memory_t mem; boot(&cpu, &mem);
    cpu.C = 0x10; cpu.P |= FLAG_CARRY;
    const uint8_t prog[] = { 0xE9, 0x03 }; load(&mem, 0x0200, prog, 2);
    cpu816_step(&cpu);
    ASSERT_EQ((int)a8(&cpu), 0x0D);
    ASSERT_TRUE(cpu.P & FLAG_CARRY);
}

TEST(test_sbc_eb_unofficial_alias) {
    cpu65c816_t cpu; memory_t mem; boot(&cpu, &mem);
    cpu.C = 0x10; cpu.P |= FLAG_CARRY;
    const uint8_t prog[] = { 0xEB, 0x03 }; load(&mem, 0x0200, prog, 2);
    cpu816_step(&cpu);
    ASSERT_EQ((int)a8(&cpu), 0x0D);
}

TEST(test_and_or_eor) {
    cpu65c816_t cpu; memory_t mem; boot(&cpu, &mem);
    cpu.C = 0xF0;
    const uint8_t prog[] = {
        0x29, 0xCC,  /* AND #$CC → 0xC0 */
        0x09, 0x03,  /* ORA #$03 → 0xC3 */
        0x49, 0xFF,  /* EOR #$FF → 0x3C */
    };
    load(&mem, 0x0200, prog, 6);
    cpu816_step(&cpu); ASSERT_EQ((int)a8(&cpu), 0xC0);
    cpu816_step(&cpu); ASSERT_EQ((int)a8(&cpu), 0xC3);
    cpu816_step(&cpu); ASSERT_EQ((int)a8(&cpu), 0x3C);
}

TEST(test_cmp_sets_carry_when_geq) {
    cpu65c816_t cpu; memory_t mem; boot(&cpu, &mem);
    cpu.C = 0x50;
    const uint8_t prog[] = { 0xC9, 0x30 }; load(&mem, 0x0200, prog, 2);
    cpu816_step(&cpu);
    ASSERT_TRUE(cpu.P & FLAG_CARRY);
    ASSERT_FALSE(cpu.P & FLAG_ZERO);
}

TEST(test_cmp_equal_sets_zero) {
    cpu65c816_t cpu; memory_t mem; boot(&cpu, &mem);
    cpu.C = 0x42;
    const uint8_t prog[] = { 0xC9, 0x42 }; load(&mem, 0x0200, prog, 2);
    cpu816_step(&cpu);
    ASSERT_TRUE(cpu.P & FLAG_CARRY);
    ASSERT_TRUE(cpu.P & FLAG_ZERO);
}

TEST(test_bit_zero_page) {
    cpu65c816_t cpu; memory_t mem; boot(&cpu, &mem);
    cpu.C = 0x0F;
    memory_write(&mem, 0x0050, 0xC0);
    const uint8_t prog[] = { 0x24, 0x50 }; load(&mem, 0x0200, prog, 2);
    cpu816_step(&cpu);
    ASSERT_TRUE(cpu.P & FLAG_ZERO);   /* 0x0F & 0xC0 = 0 */
    ASSERT_TRUE(cpu.P & FLAG_OVERFLOW); /* bit 6 de mem */
    ASSERT_TRUE(cpu.P & FLAG_NEGATIVE); /* bit 7 de mem */
}

/* ═══════════════════════════════════════════════════════════════════ */
/*  Shift / Rotate (RMW)                                              */
/* ═══════════════════════════════════════════════════════════════════ */

TEST(test_asl_accumulator) {
    cpu65c816_t cpu; memory_t mem; boot(&cpu, &mem);
    cpu.C = 0x81;
    const uint8_t prog[] = { 0x0A }; load(&mem, 0x0200, prog, 1);
    cpu816_step(&cpu);
    ASSERT_EQ((int)a8(&cpu), 0x02);
    ASSERT_TRUE(cpu.P & FLAG_CARRY);
}

TEST(test_lsr_memory) {
    cpu65c816_t cpu; memory_t mem; boot(&cpu, &mem);
    memory_write(&mem, 0x0050, 0x03);
    const uint8_t prog[] = { 0x46, 0x50 }; load(&mem, 0x0200, prog, 2);
    cpu816_step(&cpu);
    ASSERT_EQ((int)memory_read(&mem, 0x0050), 0x01);
    ASSERT_TRUE(cpu.P & FLAG_CARRY);
}

TEST(test_rol_accumulator_with_carry_in) {
    cpu65c816_t cpu; memory_t mem; boot(&cpu, &mem);
    cpu.C = 0x40; cpu.P |= FLAG_CARRY;
    const uint8_t prog[] = { 0x2A }; load(&mem, 0x0200, prog, 1);
    cpu816_step(&cpu);
    ASSERT_EQ((int)a8(&cpu), 0x81);
    ASSERT_FALSE(cpu.P & FLAG_CARRY);
}

TEST(test_ror_accumulator_with_carry_in) {
    cpu65c816_t cpu; memory_t mem; boot(&cpu, &mem);
    cpu.C = 0x02; cpu.P |= FLAG_CARRY;
    const uint8_t prog[] = { 0x6A }; load(&mem, 0x0200, prog, 1);
    cpu816_step(&cpu);
    ASSERT_EQ((int)a8(&cpu), 0x81);
    ASSERT_FALSE(cpu.P & FLAG_CARRY);
}

/* ═══════════════════════════════════════════════════════════════════ */
/*  Inc/Dec/Transfers                                                 */
/* ═══════════════════════════════════════════════════════════════════ */

TEST(test_inx_dex_iny_dey) {
    cpu65c816_t cpu; memory_t mem; boot(&cpu, &mem);
    cpu.X = 0x10; cpu.Y = 0x20;
    const uint8_t prog[] = { 0xE8, 0xCA, 0xC8, 0x88 }; load(&mem, 0x0200, prog, 4);
    cpu816_step(&cpu); ASSERT_EQ((int)x8(&cpu), 0x11);
    cpu816_step(&cpu); ASSERT_EQ((int)x8(&cpu), 0x10);
    cpu816_step(&cpu); ASSERT_EQ((int)y8(&cpu), 0x21);
    cpu816_step(&cpu); ASSERT_EQ((int)y8(&cpu), 0x20);
}

TEST(test_inc_dec_memory) {
    cpu65c816_t cpu; memory_t mem; boot(&cpu, &mem);
    memory_write(&mem, 0x0050, 0x10);
    const uint8_t prog[] = { 0xE6, 0x50, 0xC6, 0x50 }; load(&mem, 0x0200, prog, 4);
    cpu816_step(&cpu); ASSERT_EQ((int)memory_read(&mem, 0x0050), 0x11);
    cpu816_step(&cpu); ASSERT_EQ((int)memory_read(&mem, 0x0050), 0x10);
}

TEST(test_transfers_tax_txa_tay_tya) {
    cpu65c816_t cpu; memory_t mem; boot(&cpu, &mem);
    cpu.C = 0x42;
    const uint8_t prog[] = { 0xAA, 0x8A, 0xA8, 0x98 }; load(&mem, 0x0200, prog, 4);
    cpu816_step(&cpu); ASSERT_EQ((int)x8(&cpu), 0x42);
    cpu816_step(&cpu); ASSERT_EQ((int)a8(&cpu), 0x42);
    cpu816_step(&cpu); ASSERT_EQ((int)y8(&cpu), 0x42);
    cpu816_step(&cpu); ASSERT_EQ((int)a8(&cpu), 0x42);
}

TEST(test_tsx_txs) {
    cpu65c816_t cpu; memory_t mem; boot(&cpu, &mem);
    cpu.S = 0x01AB;
    const uint8_t prog[] = { 0xBA, 0xA2, 0x55, 0x9A }; load(&mem, 0x0200, prog, 4);
    cpu816_step(&cpu); ASSERT_EQ((int)x8(&cpu), 0xAB);  /* TSX */
    cpu816_step(&cpu); ASSERT_EQ((int)x8(&cpu), 0x55);  /* LDX #$55 */
    cpu816_step(&cpu); ASSERT_EQ((int)sp8(&cpu), 0x55); /* TXS */
    /* S high byte doit rester $01 en mode E */
    ASSERT_EQ((int)(cpu.S >> 8), 0x01);
}

/* ═══════════════════════════════════════════════════════════════════ */
/*  Stack                                                              */
/* ═══════════════════════════════════════════════════════════════════ */

TEST(test_pha_pla) {
    cpu65c816_t cpu; memory_t mem; boot(&cpu, &mem);
    cpu.C = 0x77;
    const uint8_t prog[] = { 0x48, 0xA9, 0x00, 0x68 }; load(&mem, 0x0200, prog, 4);
    cpu816_step(&cpu); /* PHA */
    cpu816_step(&cpu); /* LDA #$00 */
    ASSERT_EQ((int)a8(&cpu), 0x00);
    cpu816_step(&cpu); /* PLA */
    ASSERT_EQ((int)a8(&cpu), 0x77);
    ASSERT_EQ((int)sp8(&cpu), 0xFF); /* SP rétabli */
}

TEST(test_php_plp_b_and_unused_handling) {
    cpu65c816_t cpu; memory_t mem; boot(&cpu, &mem);
    cpu.P = (uint8_t)(FLAG_CARRY | FLAG_INTERRUPT | FLAG_UNUSED); /* B=0 */
    const uint8_t prog[] = { 0x08, 0x28 }; load(&mem, 0x0200, prog, 2);
    cpu816_step(&cpu); /* PHP — pousse P avec B=1, UNUSED=1 */
    ASSERT_EQ((int)memory_read(&mem, 0x01FF),
              (int)(FLAG_CARRY | FLAG_INTERRUPT | FLAG_BREAK | FLAG_UNUSED));
    cpu.P = 0;
    cpu816_step(&cpu); /* PLP — récupère P avec B=0, UNUSED=1 */
    ASSERT_FALSE(cpu.P & FLAG_BREAK);
    ASSERT_TRUE(cpu.P & FLAG_UNUSED);
    ASSERT_TRUE(cpu.P & FLAG_CARRY);
}

/* ═══════════════════════════════════════════════════════════════════ */
/*  Branches                                                           */
/* ═══════════════════════════════════════════════════════════════════ */

TEST(test_beq_taken_no_page_cross) {
    cpu65c816_t cpu; memory_t mem; boot(&cpu, &mem);
    cpu.P |= FLAG_ZERO;
    const uint8_t prog[] = { 0xF0, 0x10 }; load(&mem, 0x0200, prog, 2);
    int c = cpu816_step(&cpu);
    ASSERT_EQ((int)cpu.PC, 0x0212);
    ASSERT_EQ(c, 3); /* 2 base + 1 taken no cross */
}

TEST(test_beq_not_taken) {
    cpu65c816_t cpu; memory_t mem; boot(&cpu, &mem);
    cpu.P &= (uint8_t)~FLAG_ZERO;
    const uint8_t prog[] = { 0xF0, 0x10 }; load(&mem, 0x0200, prog, 2);
    int c = cpu816_step(&cpu);
    ASSERT_EQ((int)cpu.PC, 0x0202);
    ASSERT_EQ(c, 2);
}

TEST(test_bne_taken_with_page_cross) {
    cpu65c816_t cpu; memory_t mem; place_reset_vector(&mem, 0x02F0);
    cpu816_init(&cpu, &mem); cpu816_reset(&cpu);
    cpu.P &= (uint8_t)~FLAG_ZERO;
    /* Branchement de $02F0 vers $0312 (cross page) */
    const uint8_t prog[] = { 0xD0, 0x20 }; load(&mem, 0x02F0, prog, 2);
    int c = cpu816_step(&cpu);
    ASSERT_EQ((int)cpu.PC, 0x0312);
    ASSERT_EQ(c, 4); /* 2 base + 2 taken with cross */
}

/* ═══════════════════════════════════════════════════════════════════ */
/*  JMP / JSR / RTS                                                    */
/* ═══════════════════════════════════════════════════════════════════ */

TEST(test_jmp_absolute) {
    cpu65c816_t cpu; memory_t mem; boot(&cpu, &mem);
    const uint8_t prog[] = { 0x4C, 0x34, 0x12 }; load(&mem, 0x0200, prog, 3);
    cpu816_step(&cpu);
    ASSERT_EQ((int)cpu.PC, 0x1234);
}

TEST(test_jmp_indirect_normal) {
    cpu65c816_t cpu; memory_t mem; boot(&cpu, &mem);
    memory_write(&mem, 0x3000, 0x78);
    memory_write(&mem, 0x3001, 0x56);
    const uint8_t prog[] = { 0x6C, 0x00, 0x30 }; load(&mem, 0x0200, prog, 3);
    cpu816_step(&cpu);
    ASSERT_EQ((int)cpu.PC, 0x5678);
}

TEST(test_jmp_indirect_page_wrap_bug_adr11) {
    cpu65c816_t cpu; memory_t mem; boot(&cpu, &mem);
    /* Pointeur en $30FF : low byte lu en $30FF, high byte lu en $3000
     * (au lieu de $3100, c'est le bug NMOS conservé par ADR-11 hybride). */
    memory_write(&mem, 0x30FF, 0x78);
    memory_write(&mem, 0x3100, 0x99); /* ne doit PAS être lu */
    memory_write(&mem, 0x3000, 0x56); /* ce qui est réellement lu */
    const uint8_t prog[] = { 0x6C, 0xFF, 0x30 }; load(&mem, 0x0200, prog, 3);
    cpu816_step(&cpu);
    ASSERT_EQ((int)cpu.PC, 0x5678);
}

TEST(test_jsr_rts_round_trip) {
    cpu65c816_t cpu; memory_t mem; boot(&cpu, &mem);
    /* JSR $0210 puis NOP. Subroutine en $0210 : LDA #$AA ; RTS. */
    const uint8_t main_prog[] = { 0x20, 0x10, 0x02, 0xEA }; load(&mem, 0x0200, main_prog, 4);
    const uint8_t sub_prog[]  = { 0xA9, 0xAA, 0x60 };       load(&mem, 0x0210, sub_prog, 3);
    cpu816_step(&cpu); /* JSR */
    ASSERT_EQ((int)cpu.PC, 0x0210);
    cpu816_step(&cpu); /* LDA #$AA */
    ASSERT_EQ((int)a8(&cpu), 0xAA);
    cpu816_step(&cpu); /* RTS */
    ASSERT_EQ((int)cpu.PC, 0x0203);
}

/* ═══════════════════════════════════════════════════════════════════ */
/*  BRK / RTI / NMI / IRQ                                              */
/* ═══════════════════════════════════════════════════════════════════ */

TEST(test_brk_pushes_pc_plus_two_and_loads_irq_vector) {
    cpu65c816_t cpu; memory_t mem; boot(&cpu, &mem);
    place_irq_vector(&mem, 0x4000);
    const uint8_t prog[] = { 0x00, 0xEA }; load(&mem, 0x0200, prog, 2);
    cpu816_step(&cpu);
    ASSERT_EQ((int)cpu.PC, 0x4000);
    ASSERT_TRUE(cpu.P & FLAG_INTERRUPT);
    /* Pile : retour PC = $0202, P avec B=1 */
    ASSERT_EQ((int)memory_read(&mem, 0x01FF), 0x02);
    ASSERT_EQ((int)memory_read(&mem, 0x01FE), 0x02);
}

TEST(test_nmi_takes_vector_when_pending) {
    cpu65c816_t cpu; memory_t mem; boot(&cpu, &mem);
    place_nmi_vector(&mem, 0x5000);
    const uint8_t prog[] = { 0xEA }; load(&mem, 0x0200, prog, 1);
    cpu816_nmi(&cpu);
    int c = cpu816_step(&cpu);
    ASSERT_EQ(c, 7);
    ASSERT_EQ((int)cpu.PC, 0x5000);
    ASSERT_FALSE(cpu.nmi_pending);
}

TEST(test_irq_taken_when_i_clear) {
    cpu65c816_t cpu; memory_t mem; boot(&cpu, &mem);
    place_irq_vector(&mem, 0x6000);
    cpu.P &= (uint8_t)~FLAG_INTERRUPT;
    const uint8_t prog[] = { 0xEA }; load(&mem, 0x0200, prog, 1);
    cpu816_irq_set(&cpu, IRQF_VIA);
    int c = cpu816_step(&cpu);
    ASSERT_EQ(c, 7);
    ASSERT_EQ((int)cpu.PC, 0x6000);
    ASSERT_TRUE(cpu.P & FLAG_INTERRUPT);  /* I forcé */
}

TEST(test_irq_masked_when_i_set) {
    cpu65c816_t cpu; memory_t mem; boot(&cpu, &mem);
    place_irq_vector(&mem, 0x6000);
    cpu.P |= FLAG_INTERRUPT;
    const uint8_t prog[] = { 0xA9, 0x42 }; load(&mem, 0x0200, prog, 2);
    cpu816_irq_set(&cpu, IRQF_VIA);
    cpu816_step(&cpu);
    ASSERT_EQ((int)cpu.PC, 0x0202); /* a exécuté LDA #$42 */
    ASSERT_EQ((int)a8(&cpu), 0x42);
}

TEST(test_rti_restores_p_and_pc) {
    cpu65c816_t cpu; memory_t mem; boot(&cpu, &mem);
    /* Pile pré-chargée : low PC, high PC, P (avec B=1, sera nettoyé). */
    cpu.S = 0x01FC;
    memory_write(&mem, 0x01FD, (uint8_t)(FLAG_CARRY | FLAG_BREAK | FLAG_UNUSED));
    memory_write(&mem, 0x01FE, 0x34);
    memory_write(&mem, 0x01FF, 0x12);
    const uint8_t prog[] = { 0x40 }; load(&mem, 0x0200, prog, 1);
    cpu816_step(&cpu);
    ASSERT_EQ((int)cpu.PC, 0x1234);
    ASSERT_TRUE(cpu.P & FLAG_CARRY);
    ASSERT_FALSE(cpu.P & FLAG_BREAK);
    ASSERT_TRUE(cpu.P & FLAG_UNUSED);
}

/* ═══════════════════════════════════════════════════════════════════ */
/*  ADR-11 (c) — Opcodes illégaux NMOS = NOP                           */
/* ═══════════════════════════════════════════════════════════════════ */

TEST(test_illegal_opcode_03_consumes_nothing) {
    cpu65c816_t cpu; memory_t mem; boot(&cpu, &mem);
    const uint8_t prog[] = { 0x03, 0xEA }; load(&mem, 0x0200, prog, 2);
    cpu816_step(&cpu);
    /* opcode_table[$03] = size 1 → PC avance d'un seul byte */
    ASSERT_EQ((int)cpu.PC, 0x0201);
}

TEST(test_illegal_opcode_with_2_byte_size_consumes_operand) {
    cpu65c816_t cpu; memory_t mem; boot(&cpu, &mem);
    /* $04 est un illegal NMOS de taille 2 dans la table Phosphoric */
    const uint8_t prog[] = { 0x04, 0x99 }; load(&mem, 0x0200, prog, 2);
    cpu816_step(&cpu);
    ASSERT_EQ((int)cpu.PC, 0x0202);
}

/* ═══════════════════════════════════════════════════════════════════ */
/*  Sticky high byte de C (B) — préservé sur opérations 8 bits        */
/* ═══════════════════════════════════════════════════════════════════ */

TEST(test_b_register_preserved_through_lda_imm) {
    cpu65c816_t cpu; memory_t mem; boot(&cpu, &mem);
    cpu.C = 0x99FF; /* B=$99, A=$FF */
    const uint8_t prog[] = { 0xA9, 0x42 }; load(&mem, 0x0200, prog, 2);
    cpu816_step(&cpu);
    ASSERT_EQ((int)cpu.C, 0x9942); /* B intact, A=$42 */
}

/* ═══════════════════════════════════════════════════════════════════ */
/*  Programme composé : Fibonacci court                                */
/* ═══════════════════════════════════════════════════════════════════ */

TEST(test_short_program_fibonacci_8bit) {
    /* fib(10) = 55, calculé en boucle simple :
     *   LDX #$0A   ; counter
     *   LDA #$00   ; a
     *   LDY #$01   ; b
     * loop:
     *   STA $50    ; tmp = a
     *   TYA        ; a = b
     *   CLC
     *   ADC $50    ; a = a + tmp -> nouveau b
     *   PHA        ; sauve b
     *   LDA $50    ; restore tmp -> nouvelle valeur de a
     *   TAY        ; ne, refaisons : non. Plutôt :
     *
     * Plus simple : on calcule itérativement a,b = b, a+b dix fois.
     *   LDX #$0A
     *   LDA #$00     ; a
     *   STA $50
     *   LDA #$01     ; b
     *   STA $51
     * loop:
     *   LDA $50      ; A=a
     *   CLC
     *   ADC $51      ; A = a+b
     *   PHA          ; save (futur b)
     *   LDA $51      ; A=b
     *   STA $50      ; a = b
     *   PLA          ; A = a+b
     *   STA $51      ; b = a+b
     *   DEX
     *   BNE loop
     *   BRK
     */
    cpu65c816_t cpu; memory_t mem; boot(&cpu, &mem);
    place_irq_vector(&mem, 0x0300); /* BRK ira ici */
    memory_write(&mem, 0x0300, 0xEA); /* NOP infini après BRK */

    const uint8_t prog[] = {
        0xA2, 0x0A,             /* LDX #$0A    */
        0xA9, 0x00, 0x85, 0x50, /* LDA #$00 ; STA $50 */
        0xA9, 0x01, 0x85, 0x51, /* LDA #$01 ; STA $51 */
        /* loop @ $020A */
        0xA5, 0x50,             /* LDA $50     */
        0x18,                   /* CLC         */
        0x65, 0x51,             /* ADC $51     */
        0x48,                   /* PHA         */
        0xA5, 0x51,             /* LDA $51     */
        0x85, 0x50,             /* STA $50     */
        0x68,                   /* PLA         */
        0x85, 0x51,             /* STA $51     */
        0xCA,                   /* DEX         */
        0xD0, 0xF0,             /* BNE loop ($020A) */
        0x00,                   /* BRK         */
    };
    load(&mem, 0x0200, prog, sizeof(prog));

    /* Exécute jusqu'à atteindre 0x0300 (vecteur BRK). */
    for (int i = 0; i < 5000; i++) {
        if (cpu.PC == 0x0300) break;
        if (cpu816_step(&cpu) < 0) break;
    }
    ASSERT_EQ((int)cpu.PC, 0x0300);
    ASSERT_EQ((int)memory_read(&mem, 0x0051), 89);  /* fib(11) = 89 */
}

/* ═══════════════════════════════════════════════════════════════════ */

int main(void) {
    printf("\n");
    printf("═══════════════════════════════════════════════════════\n");
    printf("  65C816 Mode-E (6502 equivalence) Tests (B1.4)\n");
    printf("═══════════════════════════════════════════════════════\n\n");

    RUN(test_lda_immediate);
    RUN(test_lda_immediate_zero_flag);
    RUN(test_lda_immediate_negative_flag);
    RUN(test_lda_zp_x);
    RUN(test_lda_abs);
    RUN(test_lda_abs_x_page_cross_penalty);
    RUN(test_ldx_immediate);
    RUN(test_ldy_zp);
    RUN(test_sta_abs);
    RUN(test_stx_zp);
    RUN(test_sty_abs);
    RUN(test_indexed_indirect_lda);
    RUN(test_indirect_indexed_lda_with_page_cross);
    RUN(test_adc_simple);
    RUN(test_adc_overflow_flag);
    RUN(test_adc_bcd_mode);
    RUN(test_sbc_simple);
    RUN(test_sbc_eb_unofficial_alias);
    RUN(test_and_or_eor);
    RUN(test_cmp_sets_carry_when_geq);
    RUN(test_cmp_equal_sets_zero);
    RUN(test_bit_zero_page);
    RUN(test_asl_accumulator);
    RUN(test_lsr_memory);
    RUN(test_rol_accumulator_with_carry_in);
    RUN(test_ror_accumulator_with_carry_in);
    RUN(test_inx_dex_iny_dey);
    RUN(test_inc_dec_memory);
    RUN(test_transfers_tax_txa_tay_tya);
    RUN(test_tsx_txs);
    RUN(test_pha_pla);
    RUN(test_php_plp_b_and_unused_handling);
    RUN(test_beq_taken_no_page_cross);
    RUN(test_beq_not_taken);
    RUN(test_bne_taken_with_page_cross);
    RUN(test_jmp_absolute);
    RUN(test_jmp_indirect_normal);
    RUN(test_jmp_indirect_page_wrap_bug_adr11);
    RUN(test_jsr_rts_round_trip);
    RUN(test_brk_pushes_pc_plus_two_and_loads_irq_vector);
    RUN(test_nmi_takes_vector_when_pending);
    RUN(test_irq_taken_when_i_clear);
    RUN(test_irq_masked_when_i_set);
    RUN(test_rti_restores_p_and_pc);
    RUN(test_illegal_opcode_03_consumes_nothing);
    RUN(test_illegal_opcode_with_2_byte_size_consumes_operand);
    RUN(test_b_register_preserved_through_lda_imm);
    RUN(test_short_program_fibonacci_8bit);

    printf("\n═══════════════════════════════════════════════════════\n");
    printf("  Results: %d passed, %d failed\n", tests_passed, tests_failed);
    printf("═══════════════════════════════════════════════════════\n\n");

    return tests_failed == 0 ? 0 : 1;
}
