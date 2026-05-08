/**
 * @file test_cpu65c816_native.c
 * @brief Tests mode natif 65C816 — B1.7a (REP/SEP + 16-bit accumulator)
 * @author bmarty <bmarty@mailo.com>
 * @date 2026-05-07
 *
 * Couvre :
 *   - REP / SEP (manipulation du P register)
 *   - Bascule M=1 ↔ M=0 (largeur accumulateur 8 ↔ 16 bits)
 *   - Opcodes M-aware en mode N : LDA, STA, ADC, AND, ORA, EOR, CMP,
 *     BIT, INC A, DEC A, PHA, PLA
 *   - $EB polymorphique : SBC imm en mode E, XBA en mode N
 *   - Préservation invariants mode E sur REP (UNUSED reste à 1)
 *
 * Ne couvre pas (différé B1.7b) : index width X (16-bit X et Y).
 * Ne couvre pas (différé B1.8) : bus 24-bit, MVN/MVP, JSL/RTL, long
 * addressing.
 */

#include <stdio.h>
#include "cpu/cpu65c816.h"
#include "cpu/cpu6502.h"
#include "memory/memory.h"

static int tests_passed = 0;
static int tests_failed = 0;

#define TEST(name) static void name(void)
#define RUN(name) do { \
    printf("  %-58s", #name); \
    int _b = tests_failed; \
    name(); \
    if (tests_failed == _b) { tests_passed++; printf("PASS\n"); } \
} while(0)

#define ASSERT_EQ(a, b) do { \
    if ((a) != (b)) { \
        printf("FAIL\n    %s:%d: expected %ld (0x%lX), got %ld (0x%lX)\n", \
               __FILE__, __LINE__, (long)(b), (long)(b), (long)(a), (long)(a)); \
        tests_failed++; return; \
    } \
} while(0)

#define ASSERT_TRUE(x) do { \
    if (!(x)) { printf("FAIL\n    %s:%d: expected true\n", __FILE__, __LINE__); tests_failed++; return; } \
} while(0)

#define ASSERT_FALSE(x) do { \
    if ((x)) { printf("FAIL\n    %s:%d: expected false\n", __FILE__, __LINE__); tests_failed++; return; } \
} while(0)

/* Helpers */

static void place_reset(memory_t* mem, uint16_t addr) {
    mem->rom[0x3FFC] = (uint8_t)(addr & 0xFF);
    mem->rom[0x3FFD] = (uint8_t)(addr >> 8);
}

static void boot(cpu65c816_t* cpu, memory_t* mem) {
    memory_init(mem);
    place_reset(mem, 0x0200);
    cpu816_init(cpu, mem);
    cpu816_reset(cpu);
}

static void load(memory_t* mem, uint16_t addr, const uint8_t* code, size_t n) {
    for (size_t i = 0; i < n; i++) memory_write(mem, (uint16_t)(addr + i), code[i]);
}

/* Bascule en mode natif via CLC ; XCE et clear M (REP #$20). */
static void enter_native_M0(cpu65c816_t* cpu, memory_t* mem) {
    const uint8_t prog[] = { 0x18, 0xFB,    /* CLC ; XCE → mode N */
                              0xC2, 0x20 }; /* REP #$20 → M=0 */
    load(mem, cpu->PC, prog, 4);
    for (int i = 0; i < 3; i++) cpu816_step(cpu);
}

/* Bascule en mode natif M=0 et X=0 (full 16-bit). */
static void enter_native_M0_X0(cpu65c816_t* cpu, memory_t* mem) {
    const uint8_t prog[] = { 0x18, 0xFB,    /* CLC ; XCE → mode N */
                              0xC2, 0x30 }; /* REP #$30 → M=0, X=0 */
    load(mem, cpu->PC, prog, 4);
    for (int i = 0; i < 3; i++) cpu816_step(cpu);
}

/* ─── REP / SEP ─────────────────────────────────────────────────────── */

TEST(test_sep_set_m_in_native_mode) {
    cpu65c816_t cpu; memory_t mem; boot(&cpu, &mem);
    /* Bascule en mode N */
    const uint8_t go_n[] = { 0x18, 0xFB }; load(&mem, 0x0200, go_n, 2);
    cpu816_step(&cpu); cpu816_step(&cpu);
    ASSERT_FALSE(cpu.E);
    /* M est mis à 1 par défaut au reset (P=0x24, bit5=UNUSED=1).
     * Pour tester SEP, partons de M=0 d'abord via REP. */
    const uint8_t rep[] = { 0xC2, 0x20 }; load(&mem, 0x0202, rep, 2);
    cpu816_step(&cpu);
    ASSERT_FALSE(cpu.P & FLAG816_M_MEM);
    /* SEP #$20 : remet M=1 */
    const uint8_t sep[] = { 0xE2, 0x20 }; load(&mem, 0x0204, sep, 2);
    cpu816_step(&cpu);
    ASSERT_TRUE(cpu.P & FLAG816_M_MEM);
}

TEST(test_rep_in_emulation_mode_keeps_unused) {
    cpu65c816_t cpu; memory_t mem; boot(&cpu, &mem);
    /* Reste en mode E, REP #$FF tente de tout effacer.
     * UNUSED (bit 5) doit rester à 1 (sémantique 6502). */
    const uint8_t prog[] = { 0xC2, 0xFF }; load(&mem, 0x0200, prog, 2);
    cpu816_step(&cpu);
    ASSERT_TRUE(cpu.P & FLAG_UNUSED);
}

TEST(test_sep_x_truncates_x_y_high_bytes) {
    cpu65c816_t cpu; memory_t mem; boot(&cpu, &mem);
    /* Bascule mode N, REP #$10 (X=0), met X.high et Y.high à 0xFF en
     * écrivant directement, puis SEP #$10 doit tronquer. */
    const uint8_t go[] = { 0x18, 0xFB, 0xC2, 0x10 }; load(&mem, 0x0200, go, 4);
    cpu816_step(&cpu); cpu816_step(&cpu); cpu816_step(&cpu);
    ASSERT_FALSE(cpu.P & FLAG816_X_INDEX);
    cpu.X = 0xCAFE; cpu.Y = 0xBABE;
    const uint8_t sep[] = { 0xE2, 0x10 }; load(&mem, 0x0204, sep, 2);
    cpu816_step(&cpu);
    ASSERT_EQ((int)cpu.X, 0x00FE);
    ASSERT_EQ((int)cpu.Y, 0x00BE);
}

/* ─── 16-bit accumulator ops (M=0) ──────────────────────────────────── */

TEST(test_lda_immediate_16_bit_in_native) {
    cpu65c816_t cpu; memory_t mem; boot(&cpu, &mem);
    enter_native_M0(&cpu, &mem);
    /* LDA #$1234 (3 bytes) */
    const uint8_t prog[] = { 0xA9, 0x34, 0x12 }; load(&mem, cpu.PC, prog, 3);
    cpu816_step(&cpu);
    ASSERT_EQ((int)cpu.C, 0x1234);
    ASSERT_FALSE(cpu.P & FLAG_ZERO);
    ASSERT_FALSE(cpu.P & FLAG_NEGATIVE);
}

TEST(test_lda_immediate_16_bit_negative_flag_uses_bit15) {
    cpu65c816_t cpu; memory_t mem; boot(&cpu, &mem);
    enter_native_M0(&cpu, &mem);
    /* LDA #$8000 — N doit être set (bit 15) */
    const uint8_t prog[] = { 0xA9, 0x00, 0x80 }; load(&mem, cpu.PC, prog, 3);
    cpu816_step(&cpu);
    ASSERT_EQ((int)cpu.C, 0x8000);
    ASSERT_TRUE(cpu.P & FLAG_NEGATIVE);
    ASSERT_FALSE(cpu.P & FLAG_ZERO);
}

TEST(test_sta_abs_writes_two_bytes) {
    cpu65c816_t cpu; memory_t mem; boot(&cpu, &mem);
    enter_native_M0(&cpu, &mem);
    cpu.C = 0x4321;
    /* STA $3000 — écrit 2 bytes (low first) */
    const uint8_t prog[] = { 0x8D, 0x00, 0x30 }; load(&mem, cpu.PC, prog, 3);
    cpu816_step(&cpu);
    ASSERT_EQ((int)memory_read(&mem, 0x3000), 0x21);
    ASSERT_EQ((int)memory_read(&mem, 0x3001), 0x43);
}

TEST(test_lda_abs_reads_two_bytes) {
    cpu65c816_t cpu; memory_t mem; boot(&cpu, &mem);
    enter_native_M0(&cpu, &mem);
    memory_write(&mem, 0x3000, 0xCD);
    memory_write(&mem, 0x3001, 0xAB);
    const uint8_t prog[] = { 0xAD, 0x00, 0x30 }; load(&mem, cpu.PC, prog, 3);
    cpu816_step(&cpu);
    ASSERT_EQ((int)cpu.C, 0xABCD);
}

TEST(test_adc_16_bit_carry_overflow) {
    cpu65c816_t cpu; memory_t mem; boot(&cpu, &mem);
    enter_native_M0(&cpu, &mem);
    cpu.C = 0x7FFF; cpu.P &= (uint8_t)~FLAG_CARRY;
    /* ADC #$0001 — résulte 0x8000, V=1, N=1 */
    const uint8_t prog[] = { 0x69, 0x01, 0x00 }; load(&mem, cpu.PC, prog, 3);
    cpu816_step(&cpu);
    ASSERT_EQ((int)cpu.C, 0x8000);
    ASSERT_TRUE(cpu.P & FLAG_OVERFLOW);
    ASSERT_TRUE(cpu.P & FLAG_NEGATIVE);
    ASSERT_FALSE(cpu.P & FLAG_CARRY);
}

TEST(test_adc_16_bit_carry_out) {
    cpu65c816_t cpu; memory_t mem; boot(&cpu, &mem);
    enter_native_M0(&cpu, &mem);
    cpu.C = 0xFFFF; cpu.P &= (uint8_t)~FLAG_CARRY;
    const uint8_t prog[] = { 0x69, 0x01, 0x00 }; load(&mem, cpu.PC, prog, 3);
    cpu816_step(&cpu);
    ASSERT_EQ((int)cpu.C, 0x0000);
    ASSERT_TRUE(cpu.P & FLAG_CARRY);
    ASSERT_TRUE(cpu.P & FLAG_ZERO);
}

TEST(test_and_or_eor_16_bit) {
    cpu65c816_t cpu; memory_t mem; boot(&cpu, &mem);
    enter_native_M0(&cpu, &mem);
    cpu.C = 0xFF00;
    /* AND #$F0F0 → 0xF000 ; ORA #$0F0F → 0xFF0F ; EOR #$FFFF → 0x00F0 */
    const uint8_t prog[] = {
        0x29, 0xF0, 0xF0,
        0x09, 0x0F, 0x0F,
        0x49, 0xFF, 0xFF,
    };
    load(&mem, cpu.PC, prog, sizeof(prog));
    cpu816_step(&cpu); ASSERT_EQ((int)cpu.C, 0xF000);
    cpu816_step(&cpu); ASSERT_EQ((int)cpu.C, 0xFF0F);
    cpu816_step(&cpu); ASSERT_EQ((int)cpu.C, 0x00F0);
}

TEST(test_cmp_16_bit_sets_carry_when_geq) {
    cpu65c816_t cpu; memory_t mem; boot(&cpu, &mem);
    enter_native_M0(&cpu, &mem);
    cpu.C = 0x5000;
    const uint8_t prog[] = { 0xC9, 0x00, 0x30 }; load(&mem, cpu.PC, prog, 3);
    cpu816_step(&cpu);
    ASSERT_TRUE(cpu.P & FLAG_CARRY);
    ASSERT_FALSE(cpu.P & FLAG_ZERO);
}

TEST(test_inc_a_dec_a_native_mode) {
    cpu65c816_t cpu; memory_t mem; boot(&cpu, &mem);
    enter_native_M0(&cpu, &mem);
    cpu.C = 0x00FF;
    /* INC A ($1A) ; DEC A ($3A) en mode N (16-bit) */
    const uint8_t prog[] = { 0x1A, 0x3A }; load(&mem, cpu.PC, prog, 2);
    cpu816_step(&cpu); ASSERT_EQ((int)cpu.C, 0x0100); /* INC sur 16b */
    cpu816_step(&cpu); ASSERT_EQ((int)cpu.C, 0x00FF);
}

TEST(test_inc_a_in_emulation_mode_is_nop) {
    cpu65c816_t cpu; memory_t mem; boot(&cpu, &mem);
    cpu.C = 0xAB42;  /* B=$AB, A=$42 */
    /* INC A ($1A) en mode E doit être NOP (ADR-11(c)) */
    const uint8_t prog[] = { 0x1A }; load(&mem, 0x0200, prog, 1);
    cpu816_step(&cpu);
    ASSERT_EQ((int)cpu.C, 0xAB42); /* inchangé */
}

TEST(test_pha_pla_16_bit) {
    cpu65c816_t cpu; memory_t mem; boot(&cpu, &mem);
    enter_native_M0(&cpu, &mem);
    cpu.C = 0xBEEF;
    const uint8_t prog[] = {
        0x48,                /* PHA */
        0xA9, 0x00, 0x00,    /* LDA #$0000 */
        0x68,                /* PLA */
    };
    load(&mem, cpu.PC, prog, sizeof(prog));
    cpu816_step(&cpu); /* PHA — pousse 2 bytes */
    cpu816_step(&cpu); /* LDA #$0000 */
    ASSERT_EQ((int)cpu.C, 0x0000);
    cpu816_step(&cpu); /* PLA */
    ASSERT_EQ((int)cpu.C, 0xBEEF);
}

/* ─── $EB polymorphique : SBC imm en E, XBA en N ─────────────────────── */

TEST(test_eb_is_sbc_immediate_in_emulation) {
    cpu65c816_t cpu; memory_t mem; boot(&cpu, &mem);
    cpu.C = 0x10; cpu.P |= FLAG_CARRY; /* mode E, A=$10 */
    const uint8_t prog[] = { 0xEB, 0x03 }; load(&mem, 0x0200, prog, 2);
    cpu816_step(&cpu);
    ASSERT_EQ((int)(cpu.C & 0xFF), 0x0D);
}

TEST(test_eb_is_xba_in_native_mode) {
    cpu65c816_t cpu; memory_t mem; boot(&cpu, &mem);
    /* Bascule en mode N (sans changer M : reste 8b) */
    const uint8_t go[] = { 0x18, 0xFB }; load(&mem, 0x0200, go, 2);
    cpu816_step(&cpu); cpu816_step(&cpu);
    ASSERT_FALSE(cpu.E);
    cpu.C = 0x12AB;  /* B=$12, A=$AB */
    /* XBA (1 byte) — échange high/low byte */
    const uint8_t prog[] = { 0xEB }; load(&mem, 0x0202, prog, 1);
    int c = cpu816_step(&cpu);
    ASSERT_EQ((int)cpu.C, 0xAB12);
    ASSERT_EQ(c, 3);
}

/* ─── Bascule M : préservation B sur retour mode E ──────────────────── */

/* ─── B1.7b — Index 16-bit (X flag) ─────────────────────────────────── */

TEST(test_ldx_immediate_16_bit) {
    cpu65c816_t cpu; memory_t mem; boot(&cpu, &mem);
    enter_native_M0_X0(&cpu, &mem);
    /* LDX #$1234 (3 bytes) */
    const uint8_t prog[] = { 0xA2, 0x34, 0x12 }; load(&mem, cpu.PC, prog, 3);
    cpu816_step(&cpu);
    ASSERT_EQ((int)cpu.X, 0x1234);
    ASSERT_FALSE(cpu.P & FLAG_NEGATIVE);
}

TEST(test_ldy_abs_16_bit) {
    cpu65c816_t cpu; memory_t mem; boot(&cpu, &mem);
    enter_native_M0_X0(&cpu, &mem);
    memory_write(&mem, 0x3000, 0x55);
    memory_write(&mem, 0x3001, 0xAA);
    const uint8_t prog[] = { 0xAC, 0x00, 0x30 }; load(&mem, cpu.PC, prog, 3);
    cpu816_step(&cpu);
    ASSERT_EQ((int)cpu.Y, 0xAA55);
    ASSERT_TRUE(cpu.P & FLAG_NEGATIVE);
}

TEST(test_stx_abs_writes_two_bytes) {
    cpu65c816_t cpu; memory_t mem; boot(&cpu, &mem);
    enter_native_M0_X0(&cpu, &mem);
    cpu.X = 0x4321;
    const uint8_t prog[] = { 0x8E, 0x00, 0x40 }; load(&mem, cpu.PC, prog, 3);
    cpu816_step(&cpu);
    ASSERT_EQ((int)memory_read(&mem, 0x4000), 0x21);
    ASSERT_EQ((int)memory_read(&mem, 0x4001), 0x43);
}

TEST(test_inx_dex_16_bit_wrap) {
    cpu65c816_t cpu; memory_t mem; boot(&cpu, &mem);
    enter_native_M0_X0(&cpu, &mem);
    cpu.X = 0xFFFF;
    const uint8_t prog[] = { 0xE8, 0xCA }; load(&mem, cpu.PC, prog, 2);
    cpu816_step(&cpu); /* INX */
    ASSERT_EQ((int)cpu.X, 0x0000);
    ASSERT_TRUE(cpu.P & FLAG_ZERO);
    cpu816_step(&cpu); /* DEX */
    ASSERT_EQ((int)cpu.X, 0xFFFF);
    ASSERT_TRUE(cpu.P & FLAG_NEGATIVE);
}

TEST(test_iny_16_bit) {
    cpu65c816_t cpu; memory_t mem; boot(&cpu, &mem);
    enter_native_M0_X0(&cpu, &mem);
    cpu.Y = 0x00FF;
    const uint8_t prog[] = { 0xC8 }; load(&mem, cpu.PC, prog, 1);
    cpu816_step(&cpu);
    ASSERT_EQ((int)cpu.Y, 0x0100); /* pas de wrap au byte */
}

TEST(test_cpx_16_bit) {
    cpu65c816_t cpu; memory_t mem; boot(&cpu, &mem);
    enter_native_M0_X0(&cpu, &mem);
    cpu.X = 0x1234;
    const uint8_t prog[] = { 0xE0, 0x00, 0x10 }; load(&mem, cpu.PC, prog, 3); /* CPX #$1000 */
    cpu816_step(&cpu);
    ASSERT_TRUE(cpu.P & FLAG_CARRY);  /* $1234 >= $1000 */
    ASSERT_FALSE(cpu.P & FLAG_ZERO);
}

/* ─── B1.7b — Transfers natifs 65C816 ───────────────────────────────── */

TEST(test_tcd_16_bit_with_n_z_flags) {
    cpu65c816_t cpu; memory_t mem; boot(&cpu, &mem);
    /* Bascule en mode N, M=1 X=1 (par défaut). */
    const uint8_t go[] = { 0x18, 0xFB }; load(&mem, 0x0200, go, 2);
    cpu816_step(&cpu); cpu816_step(&cpu);
    cpu.C = 0x8042;
    /* TCD : D ← C, N/Z 16-bit */
    const uint8_t prog[] = { 0x5B }; load(&mem, 0x0202, prog, 1);
    cpu816_step(&cpu);
    ASSERT_EQ((int)cpu.D, 0x8042);
    ASSERT_TRUE(cpu.P & FLAG_NEGATIVE); /* bit 15 */
}

TEST(test_tdc_copies_d_to_c) {
    cpu65c816_t cpu; memory_t mem; boot(&cpu, &mem);
    const uint8_t go[] = { 0x18, 0xFB }; load(&mem, 0x0200, go, 2);
    cpu816_step(&cpu); cpu816_step(&cpu);
    cpu.D = 0x1234;
    cpu.C = 0;
    const uint8_t prog[] = { 0x7B }; load(&mem, 0x0202, prog, 1);
    cpu816_step(&cpu);
    ASSERT_EQ((int)cpu.C, 0x1234);
}

TEST(test_tcs_in_emulation_forces_high_byte_01) {
    cpu65c816_t cpu; memory_t mem; boot(&cpu, &mem);
    cpu.C = 0xCAFE;
    const uint8_t prog[] = { 0x1B }; load(&mem, 0x0200, prog, 1);
    cpu816_step(&cpu);
    /* TCS : S = C ; en mode E, S high forcé à $01 */
    ASSERT_EQ((int)(cpu.S >> 8), 0x01);
    ASSERT_EQ((int)(cpu.S & 0xFF), 0xFE);
}

TEST(test_tcs_in_native_full_16_bit) {
    cpu65c816_t cpu; memory_t mem; boot(&cpu, &mem);
    const uint8_t go[] = { 0x18, 0xFB }; load(&mem, 0x0200, go, 2);
    cpu816_step(&cpu); cpu816_step(&cpu);
    cpu.C = 0x1FFF;
    const uint8_t prog[] = { 0x1B }; load(&mem, 0x0202, prog, 1);
    cpu816_step(&cpu);
    ASSERT_EQ((int)cpu.S, 0x1FFF); /* S 16-bit complet */
}

TEST(test_tsc_copies_s_to_c) {
    cpu65c816_t cpu; memory_t mem; boot(&cpu, &mem);
    const uint8_t go[] = { 0x18, 0xFB }; load(&mem, 0x0200, go, 2);
    cpu816_step(&cpu); cpu816_step(&cpu);
    cpu.S = 0xABCD;
    cpu.C = 0;
    const uint8_t prog[] = { 0x3B }; load(&mem, 0x0202, prog, 1);
    cpu816_step(&cpu);
    ASSERT_EQ((int)cpu.C, 0xABCD);
}

TEST(test_txy_tyx_16_bit) {
    cpu65c816_t cpu; memory_t mem; boot(&cpu, &mem);
    enter_native_M0_X0(&cpu, &mem);
    cpu.X = 0x1111; cpu.Y = 0x2222;
    const uint8_t prog[] = { 0x9B, 0xBB }; load(&mem, cpu.PC, prog, 2);
    cpu816_step(&cpu); /* TXY : Y = X */
    ASSERT_EQ((int)cpu.Y, 0x1111);
    cpu.X = 0xCAFE;
    cpu816_step(&cpu); /* TYX : X = Y */
    ASSERT_EQ((int)cpu.X, 0x1111);
}

/* ─── B1.7b — TAX/TAY/TXA/TYA width-aware ────────────────────────────── */

TEST(test_tax_16_bit_in_native) {
    cpu65c816_t cpu; memory_t mem; boot(&cpu, &mem);
    enter_native_M0_X0(&cpu, &mem);
    cpu.C = 0xBEEF;
    const uint8_t prog[] = { 0xAA }; load(&mem, cpu.PC, prog, 1);
    cpu816_step(&cpu);
    ASSERT_EQ((int)cpu.X, 0xBEEF);
}

TEST(test_tax_truncates_when_x_8bit) {
    cpu65c816_t cpu; memory_t mem; boot(&cpu, &mem);
    /* Mode N M=0 X=1 : A 16-bit, X 8-bit. TAX prend low byte de A.
     * Au reset, P=0x24 → X-bit=0 par défaut en mode N. Il faut SEP #$10
     * pour forcer X=1 (8-bit). */
    const uint8_t go[] = { 0x18, 0xFB,    /* CLC, XCE → mode N */
                            0xC2, 0x20,   /* REP #$20 → M=0 */
                            0xE2, 0x10 }; /* SEP #$10 → X=1 */
    load(&mem, 0x0200, go, 6);
    for (int i = 0; i < 4; i++) cpu816_step(&cpu);
    ASSERT_TRUE(cpu.P & FLAG816_X_INDEX);
    cpu.C = 0xBEEF;
    cpu.X = 0xCAFE; /* sera tronqué par SEP — vérifions, au cas où. */
    /* SEP #$10 a tronqué X.high déjà ; on remet manuellement pour test. */
    cpu.X = 0xCAFE;
    const uint8_t prog[] = { 0xAA }; load(&mem, cpu.PC, prog, 1);
    cpu816_step(&cpu);
    ASSERT_EQ((int)cpu.X, 0x00EF); /* truncated, high byte cleared */
}

TEST(test_tya_16_bit_in_native) {
    cpu65c816_t cpu; memory_t mem; boot(&cpu, &mem);
    enter_native_M0_X0(&cpu, &mem);
    cpu.Y = 0xCAFE;
    const uint8_t prog[] = { 0x98 }; load(&mem, cpu.PC, prog, 1);
    cpu816_step(&cpu);
    ASSERT_EQ((int)cpu.C, 0xCAFE);
}

/* ─── B1.7b — PHX/PHY/PLX/PLY ────────────────────────────────────────── */

TEST(test_phx_plx_16_bit_in_native) {
    cpu65c816_t cpu; memory_t mem; boot(&cpu, &mem);
    enter_native_M0_X0(&cpu, &mem);
    cpu.X = 0xDEAD;
    const uint8_t prog[] = {
        0xDA,                /* PHX */
        0xA2, 0x00, 0x00,    /* LDX #$0000 */
        0xFA,                /* PLX */
    };
    load(&mem, cpu.PC, prog, sizeof(prog));
    cpu816_step(&cpu); /* PHX */
    cpu816_step(&cpu); /* LDX #$0000 */
    ASSERT_EQ((int)cpu.X, 0x0000);
    cpu816_step(&cpu); /* PLX */
    ASSERT_EQ((int)cpu.X, 0xDEAD);
}

/* TXS sémantique 65C816 documentée : mode N + X=0 (16-bit) copie X
 * complet 16-bit dans S. Mode N + X=1 (8-bit) : SEP #$10 a déjà forcé
 * X high byte à 0, donc S = $00:XL. Pour stack en page 1 standard,
 * utiliser TCS (transfer C 16-bit to S) au lieu de TXS+X=1. */
TEST(test_txs_native_X0_copies_full_16bit) {
    cpu65c816_t cpu; memory_t mem; boot(&cpu, &mem);
    enter_native_M0_X0(&cpu, &mem);
    cpu.X = 0xCAFE;
    const uint8_t prog[] = { 0x9A }; /* TXS */
    load(&mem, cpu.PC, prog, 1);
    cpu816_step(&cpu);
    ASSERT_EQ((int)cpu.S, 0xCAFE);
}

/* PH-fix-dp-indirect : opcode $92 STA (dp) — DP indirect 16-bit.
 * Pointer 16-bit en DP+dp/+1, addr finale en DBR:ptr. Avant fix,
 * ces 8 opcodes ($12/$32/$52/$72/$92/$B2/$D2/$F2) étaient traités
 * comme NOP size=1 → corruption décodage opérande. */
TEST(test_sta_dp_indirect_writes_DBR_bank) {
    cpu65c816_t cpu; memory_t mem; boot(&cpu, &mem);
    enter_native_M0_X0(&cpu, &mem);
    /* SEP #$30 → M=1, X=1 (8-bit) */
    const uint8_t sep[] = { 0xE2, 0x30 };
    load(&mem, cpu.PC, sep, 2);
    cpu816_step(&cpu);
    /* Setup pointer 16-bit en DP+$08/$09 = $4000 (RAM bank DBR=0). */
    memory_write24(&mem, 0x000008, 0x00);
    memory_write24(&mem, 0x000009, 0x40);
    /* LDA #$42 ; STA ($08) */
    const uint8_t prog[] = { 0xA9, 0x42, 0x92, 0x08 };
    load(&mem, cpu.PC, prog, 4);
    cpu816_step(&cpu); /* LDA #$42 */
    cpu816_step(&cpu); /* STA ($08) */
    ASSERT_EQ((int)memory_read24(&mem, 0x004000), 0x42);
}

TEST(test_lda_dp_indirect_reads_DBR_bank) {
    cpu65c816_t cpu; memory_t mem; boot(&cpu, &mem);
    enter_native_M0_X0(&cpu, &mem);
    const uint8_t sep[] = { 0xE2, 0x30 };
    load(&mem, cpu.PC, sep, 2);
    cpu816_step(&cpu);
    /* Pointer en DP+$10/$11 = $5000 (RAM). Valeur cible $5000 = $99. */
    memory_write24(&mem, 0x000010, 0x00);
    memory_write24(&mem, 0x000011, 0x50);
    memory_write24(&mem, 0x005000, 0x99);
    const uint8_t prog[] = { 0xB2, 0x10 }; /* LDA ($10) */
    load(&mem, cpu.PC, prog, 2);
    cpu816_step(&cpu);
    ASSERT_EQ((int)(cpu.C & 0xFF), 0x99);
}

/* SEP #$10 doit forcer high byte de X et Y à 0 (conformément WDC).
 * Cf. cpu65c816_opcodes.c case 0xE2. */
TEST(test_sep_x_truncates_x_and_y) {
    cpu65c816_t cpu; memory_t mem; boot(&cpu, &mem);
    enter_native_M0_X0(&cpu, &mem);
    cpu.X = 0xDEAD;
    cpu.Y = 0xBEEF;
    const uint8_t prog[] = { 0xE2, 0x10 }; /* SEP #$10 → X=1 */
    load(&mem, cpu.PC, prog, 2);
    cpu816_step(&cpu);
    ASSERT_EQ((int)cpu.X, 0x00AD); /* high byte forced to 0 */
    ASSERT_EQ((int)cpu.Y, 0x00EF);
}

TEST(test_phx_in_emulation_is_nop) {
    cpu65c816_t cpu; memory_t mem; boot(&cpu, &mem);
    cpu.X = 0x42;
    /* En mode E, PHX ($DA) doit être NOP (illégal NMOS, ADR-11(c)). */
    uint16_t sp_before = cpu.S;
    const uint8_t prog[] = { 0xDA }; load(&mem, 0x0200, prog, 1);
    cpu816_step(&cpu);
    ASSERT_EQ((int)cpu.S, (int)sp_before); /* SP inchangé */
}

/* ─── B1.7c — Stack natifs (PHB/PLB/PHK/PHD/PLD/PEA/PEI/PER) ─────────── */

TEST(test_phb_plb) {
    cpu65c816_t cpu; memory_t mem; boot(&cpu, &mem);
    const uint8_t go[] = { 0x18, 0xFB }; load(&mem, 0x0200, go, 2);
    cpu816_step(&cpu); cpu816_step(&cpu);
    cpu.DBR = 0x42;
    const uint8_t prog[] = {
        0x8B,        /* PHB */
        0xA9, 0x00,  /* LDA #$00 */
        0xAB,        /* PLB */
    };
    load(&mem, 0x0202, prog, sizeof(prog));
    cpu816_step(&cpu); /* PHB */
    cpu.DBR = 0x99; /* salit pour vérifier PLB */
    cpu816_step(&cpu); /* LDA */
    cpu816_step(&cpu); /* PLB */
    ASSERT_EQ((int)cpu.DBR, 0x42);
}

TEST(test_phk_pushes_pbr) {
    cpu65c816_t cpu; memory_t mem; boot(&cpu, &mem);
    const uint8_t go[] = { 0x18, 0xFB }; load(&mem, 0x0200, go, 2);
    cpu816_step(&cpu); cpu816_step(&cpu);
    /* B1.8 : avec PBR-aware fetch, on doit placer l'opcode dans le bon
     * bank. PHK ne change pas PBR ; place le code en bank 3. */
    memory_write24(&mem, 0x030200, 0x4B);
    cpu.PBR = 0x03;
    cpu.PC = 0x0200;
    uint16_t sp_before = cpu.S;
    cpu816_step(&cpu);
    ASSERT_EQ((int)cpu.S, (int)(sp_before - 1));
    ASSERT_EQ((int)memory_read(&mem, sp_before), 0x03);
    memory_cleanup(&mem);
}

TEST(test_phd_pld_16_bit) {
    cpu65c816_t cpu; memory_t mem; boot(&cpu, &mem);
    const uint8_t go[] = { 0x18, 0xFB }; load(&mem, 0x0200, go, 2);
    cpu816_step(&cpu); cpu816_step(&cpu);
    cpu.D = 0x1234;
    const uint8_t prog[] = { 0x0B, 0x2B }; load(&mem, 0x0202, prog, 2);
    cpu816_step(&cpu); /* PHD */
    cpu.D = 0;
    cpu816_step(&cpu); /* PLD */
    ASSERT_EQ((int)cpu.D, 0x1234);
}

TEST(test_pea_pushes_immediate_word) {
    cpu65c816_t cpu; memory_t mem; boot(&cpu, &mem);
    const uint8_t go[] = { 0x18, 0xFB }; load(&mem, 0x0200, go, 2);
    cpu816_step(&cpu); cpu816_step(&cpu);
    /* PEA #$5678 */
    const uint8_t prog[] = { 0xF4, 0x78, 0x56 }; load(&mem, 0x0202, prog, 3);
    uint16_t sp_before = cpu.S;
    cpu816_step(&cpu);
    /* push 16-bit (high d'abord puis low). SP décrémenté de 2. */
    ASSERT_EQ((int)cpu.S, (int)(sp_before - 2));
    /* pull pour vérifier */
    uint16_t pulled = (uint16_t)(memory_read(&mem, (uint16_t)(cpu.S + 1))
                                 | (memory_read(&mem, (uint16_t)(cpu.S + 2)) << 8));
    ASSERT_EQ((int)pulled, 0x5678);
}

TEST(test_per_pushes_pc_plus_offset) {
    cpu65c816_t cpu; memory_t mem; boot(&cpu, &mem);
    const uint8_t go[] = { 0x18, 0xFB }; load(&mem, 0x0200, go, 2);
    cpu816_step(&cpu); cpu816_step(&cpu);
    /* PER +$100 : à PC=$0202 (avant), après fetch 3 bytes PC=$0205, target = $0305. */
    const uint8_t prog[] = { 0x62, 0x00, 0x01 }; load(&mem, 0x0202, prog, 3);
    cpu816_step(&cpu);
    uint16_t pulled = (uint16_t)(memory_read(&mem, (uint16_t)(cpu.S + 1))
                                 | (memory_read(&mem, (uint16_t)(cpu.S + 2)) << 8));
    ASSERT_EQ((int)pulled, 0x0305);
}

TEST(test_phb_in_emulation_is_nop) {
    cpu65c816_t cpu; memory_t mem; boot(&cpu, &mem);
    cpu.DBR = 0x42;
    uint16_t sp_before = cpu.S;
    const uint8_t prog[] = { 0x8B }; load(&mem, 0x0200, prog, 1);
    cpu816_step(&cpu);
    ASSERT_EQ((int)cpu.S, (int)sp_before);
}

/* ─── B1.7c — BRA / BRL ──────────────────────────────────────────────── */

TEST(test_bra_always_taken) {
    cpu65c816_t cpu; memory_t mem; boot(&cpu, &mem);
    /* Test en mode E (BRA est aussi disponible en E par 65C02/65C816). */
    cpu.P |= FLAG_INTERRUPT; /* irrelevant — BRA ignore tout */
    /* BRA +5 (offset $05) */
    const uint8_t prog[] = { 0x80, 0x05 }; load(&mem, 0x0200, prog, 2);
    int c = cpu816_step(&cpu);
    ASSERT_EQ((int)cpu.PC, 0x0207);
    ASSERT_TRUE(c >= 3); /* base 2 + 1 taken */
}

TEST(test_brl_long_branch_signed) {
    cpu65c816_t cpu; memory_t mem; boot(&cpu, &mem);
    /* BRL -$200 (offset $FE00 signé) : PC après fetch = $0203, target = $0003. */
    const uint8_t prog[] = { 0x82, 0x00, 0xFE }; load(&mem, 0x0200, prog, 3);
    cpu816_step(&cpu);
    ASSERT_EQ((int)cpu.PC, 0x0003);
}

/* ─── B1.7c — COP / WDM ─────────────────────────────────────────────── */

TEST(test_cop_in_emulation_uses_emul_vector) {
    cpu65c816_t cpu; memory_t mem; boot(&cpu, &mem);
    /* Vecteur COP en mode E = $00FFF4. Place handler à $5000. */
    mem.rom[0x3FF4] = 0x00; mem.rom[0x3FF5] = 0x50;
    const uint8_t prog[] = { 0x02, 0xAA }; load(&mem, 0x0200, prog, 2);
    cpu816_step(&cpu);
    ASSERT_EQ((int)cpu.PC, 0x5000);
    ASSERT_TRUE(cpu.P & FLAG_INTERRUPT);
}

TEST(test_cop_in_native_uses_native_vector) {
    cpu65c816_t cpu; memory_t mem; boot(&cpu, &mem);
    const uint8_t go[] = { 0x18, 0xFB }; load(&mem, 0x0200, go, 2);
    cpu816_step(&cpu); cpu816_step(&cpu);
    /* Vecteur COP en mode N = $00FFE4. Place handler à $6000. */
    mem.rom[0x3FE4] = 0x00; mem.rom[0x3FE5] = 0x60;
    const uint8_t prog[] = { 0x02, 0xAA }; load(&mem, 0x0202, prog, 2);
    cpu816_step(&cpu);
    ASSERT_EQ((int)cpu.PC, 0x6000);
    ASSERT_TRUE(cpu.P & FLAG_INTERRUPT);
    ASSERT_FALSE(cpu.P & FLAG_DECIMAL); /* D forcé à 0 sur interrupt mode N */
}

TEST(test_wdm_consumes_one_byte) {
    cpu65c816_t cpu; memory_t mem; boot(&cpu, &mem);
    const uint8_t prog[] = { 0x42, 0x99, 0xA9, 0x42 }; load(&mem, 0x0200, prog, 4);
    cpu816_step(&cpu); /* WDM consomme 2 bytes (opcode + signature) */
    ASSERT_EQ((int)cpu.PC, 0x0202);
    cpu816_step(&cpu); /* LDA #$42 */
    ASSERT_EQ((int)(cpu.C & 0xFF), 0x42);
}

/* ─── B1.7c — STZ ────────────────────────────────────────────────────── */

TEST(test_stz_abs_in_native_writes_two_zeros) {
    cpu65c816_t cpu; memory_t mem; boot(&cpu, &mem);
    enter_native_M0(&cpu, &mem);
    /* prépare quelque chose en mémoire pour vérifier qu'on écrase. */
    memory_write(&mem, 0x4000, 0xAA);
    memory_write(&mem, 0x4001, 0xBB);
    /* STZ $4000 (en mode N M=0) : écrit 2 bytes 0. */
    const uint8_t prog[] = { 0x9C, 0x00, 0x40 }; load(&mem, cpu.PC, prog, 3);
    cpu816_step(&cpu);
    ASSERT_EQ((int)memory_read(&mem, 0x4000), 0x00);
    ASSERT_EQ((int)memory_read(&mem, 0x4001), 0x00);
}

TEST(test_stz_abs_in_emulation_is_nop) {
    cpu65c816_t cpu; memory_t mem; boot(&cpu, &mem);
    /* En mode E, $9C est illégal NMOS — ADR-11(c) NOP (consomme 3 bytes). */
    memory_write(&mem, 0x3000, 0xCC);
    const uint8_t prog[] = { 0x9C, 0x00, 0x30 }; load(&mem, 0x0200, prog, 3);
    cpu816_step(&cpu);
    ASSERT_EQ((int)cpu.PC, 0x0203); /* PC avancé de 3 bytes */
    ASSERT_EQ((int)memory_read(&mem, 0x3000), 0xCC); /* mémoire intacte */
}

TEST(test_stz_zp_in_native) {
    cpu65c816_t cpu; memory_t mem; boot(&cpu, &mem);
    /* Mode N M=1 : STZ écrit 1 byte. */
    const uint8_t go[] = { 0x18, 0xFB }; load(&mem, 0x0200, go, 2);
    cpu816_step(&cpu); cpu816_step(&cpu);
    memory_write(&mem, 0x0050, 0xFF);
    const uint8_t prog[] = { 0x64, 0x50 }; load(&mem, 0x0202, prog, 2);
    cpu816_step(&cpu);
    ASSERT_EQ((int)memory_read(&mem, 0x0050), 0x00);
}

/* ─── B1.8 — Long addressing modes (24-bit bus, banks 1+) ────────────── */

TEST(test_lda_long_reads_bank_2) {
    cpu65c816_t cpu; memory_t mem; boot(&cpu, &mem);
    enter_native_M0(&cpu, &mem);
    /* Place une valeur en bank $02 :$3000 via memory_write24. */
    memory_write24(&mem, 0x023000, 0x55);
    memory_write24(&mem, 0x023001, 0xAA);
    /* LDA $023000 : 4 bytes (op + 3 byte addr). M=0 → 16-bit. */
    const uint8_t prog[] = { 0xAF, 0x00, 0x30, 0x02 }; load(&mem, cpu.PC, prog, 4);
    cpu816_step(&cpu);
    ASSERT_EQ((int)cpu.C, 0xAA55);
    /* Cleanup pour éviter les leaks */
    memory_cleanup(&mem);
}

TEST(test_sta_long_writes_bank_3) {
    cpu65c816_t cpu; memory_t mem; boot(&cpu, &mem);
    enter_native_M0(&cpu, &mem);
    cpu.C = 0x1234;
    /* STA $034000 */
    const uint8_t prog[] = { 0x8F, 0x00, 0x40, 0x03 }; load(&mem, cpu.PC, prog, 4);
    cpu816_step(&cpu);
    ASSERT_EQ((int)memory_read24(&mem, 0x034000), 0x34);
    ASSERT_EQ((int)memory_read24(&mem, 0x034001), 0x12);
    memory_cleanup(&mem);
}

TEST(test_lda_long_x_indexed) {
    cpu65c816_t cpu; memory_t mem; boot(&cpu, &mem);
    enter_native_M0_X0(&cpu, &mem);
    cpu.X = 0x0010;
    memory_write24(&mem, 0x025010, 0x99);
    /* LDA $025000,X — M=1 ici (par défaut après enter_native_M0_X0) */
    /* Re-test avec M=1 pour 8-bit */
    cpu.P |= FLAG816_M_MEM; /* force M=1 */
    const uint8_t prog[] = { 0xBF, 0x00, 0x50, 0x02 }; load(&mem, cpu.PC, prog, 4);
    cpu816_step(&cpu);
    ASSERT_EQ((int)(cpu.C & 0xFF), 0x99);
    memory_cleanup(&mem);
}

TEST(test_lda_dp_indirect_long) {
    cpu65c816_t cpu; memory_t mem; boot(&cpu, &mem);
    const uint8_t go[] = { 0x18, 0xFB }; load(&mem, 0x0200, go, 2);
    cpu816_step(&cpu); cpu816_step(&cpu);
    /* En mode N (M=1, X=1), [dp] : pointeur 24-bit en zp. */
    cpu.D = 0x0000;
    /* ZP $50 : pointeur vers $024000. Stocke low/hi/bank. */
    memory_write24(&mem, 0x0050, 0x00);
    memory_write24(&mem, 0x0051, 0x40);
    memory_write24(&mem, 0x0052, 0x02);
    memory_write24(&mem, 0x024000, 0x77);
    /* LDA [$50] */
    const uint8_t prog[] = { 0xA7, 0x50 }; load(&mem, 0x0202, prog, 2);
    cpu816_step(&cpu);
    ASSERT_EQ((int)(cpu.C & 0xFF), 0x77);
    memory_cleanup(&mem);
}

TEST(test_lda_dp_indirect_long_y) {
    cpu65c816_t cpu; memory_t mem; boot(&cpu, &mem);
    const uint8_t go[] = { 0x18, 0xFB }; load(&mem, 0x0200, go, 2);
    cpu816_step(&cpu); cpu816_step(&cpu);
    cpu.D = 0x0000;
    cpu.Y = 0x10;
    memory_write24(&mem, 0x0080, 0x00);
    memory_write24(&mem, 0x0081, 0x60);
    memory_write24(&mem, 0x0082, 0x05);
    memory_write24(&mem, 0x056010, 0xC3);
    /* LDA [$80],Y → $056000 + $10 = $056010 */
    const uint8_t prog[] = { 0xB7, 0x80 }; load(&mem, 0x0202, prog, 2);
    cpu816_step(&cpu);
    ASSERT_EQ((int)(cpu.C & 0xFF), 0xC3);
    memory_cleanup(&mem);
}

/* ─── B1.8 — JMP long, JML, JSL, RTL ─────────────────────────────────── */

TEST(test_jmp_long_jumps_to_bank) {
    cpu65c816_t cpu; memory_t mem; boot(&cpu, &mem);
    const uint8_t go[] = { 0x18, 0xFB }; load(&mem, 0x0200, go, 2);
    cpu816_step(&cpu); cpu816_step(&cpu);
    /* JMP $025678 : PBR ← $02, PC ← $5678 */
    const uint8_t prog[] = { 0x5C, 0x78, 0x56, 0x02 }; load(&mem, 0x0202, prog, 4);
    cpu816_step(&cpu);
    ASSERT_EQ((int)cpu.PBR, 0x02);
    ASSERT_EQ((int)cpu.PC, 0x5678);
    memory_cleanup(&mem);
}

TEST(test_jsl_rtl_round_trip) {
    cpu65c816_t cpu; memory_t mem; boot(&cpu, &mem);
    const uint8_t go[] = { 0x18, 0xFB }; load(&mem, 0x0200, go, 2);
    cpu816_step(&cpu); cpu816_step(&cpu);
    /* JSL $030010 ; sub @ $030010 = LDA #$AA (avec M=1) ; RTL.
     * (M=1 par défaut après XCE, donc LDA #$AA prend 1 byte d'opérande.) */
    const uint8_t main_prog[] = { 0x22, 0x10, 0x00, 0x03, 0xEA }; load(&mem, 0x0202, main_prog, 5);
    /* sub en bank 3 : LDA #$AA ; RTL */
    memory_write24(&mem, 0x030010, 0xA9);
    memory_write24(&mem, 0x030011, 0xAA);
    memory_write24(&mem, 0x030012, 0x6B);
    cpu816_step(&cpu); /* JSL */
    ASSERT_EQ((int)cpu.PBR, 0x03);
    ASSERT_EQ((int)cpu.PC, 0x0010);
    cpu816_step(&cpu); /* LDA #$AA */
    ASSERT_EQ((int)(cpu.C & 0xFF), 0xAA);
    cpu816_step(&cpu); /* RTL */
    ASSERT_EQ((int)cpu.PBR, 0x00);
    ASSERT_EQ((int)cpu.PC, 0x0206); /* après JSL @ $0202 + 4 bytes */
    memory_cleanup(&mem);
}

/* ─── B1.8 — MVN block move ──────────────────────────────────────────── */

TEST(test_mvn_copies_block_ascending) {
    cpu65c816_t cpu; memory_t mem; boot(&cpu, &mem);
    const uint8_t go[] = { 0x18, 0xFB, 0xC2, 0x30 }; load(&mem, 0x0200, go, 4); /* mode N M=0 X=0 */
    for (int i = 0; i < 3; i++) cpu816_step(&cpu);
    /* Source bank $01:$2000-$2009 (10 octets), destination bank $02:$3000.
     * MVN dst=$02, src=$01. C = count-1 = 9. X = source offset, Y = dest offset. */
    for (int i = 0; i < 10; i++) memory_write24(&mem, 0x012000 + i, (uint8_t)(0xA0 + i));
    cpu.X = 0x2000;
    cpu.Y = 0x3000;
    cpu.C = 9;  /* 10 octets */
    /* MVN $02, $01 (note : opcode encoding is dest_bank, src_bank). */
    const uint8_t prog[] = { 0x54, 0x02, 0x01 }; load(&mem, cpu.PC, prog, 3);
    cpu816_step(&cpu);
    /* Vérifie destination */
    for (int i = 0; i < 10; i++) {
        if (memory_read24(&mem, 0x023000 + i) != (uint8_t)(0xA0 + i)) {
            printf("FAIL at offset %d\n", i); tests_failed++; memory_cleanup(&mem); return;
        }
    }
    ASSERT_EQ((int)cpu.X, 0x200A);
    ASSERT_EQ((int)cpu.Y, 0x300A);
    ASSERT_EQ((int)cpu.C, 0xFFFF);
    ASSERT_EQ((int)cpu.DBR, 0x02);
    memory_cleanup(&mem);
}

TEST(test_long_opcodes_in_emulation_consume_size) {
    cpu65c816_t cpu; memory_t mem; boot(&cpu, &mem);
    /* LDA $lll en mode E doit être NOP de taille 4. */
    const uint8_t prog[] = { 0xAF, 0x99, 0x88, 0x77, 0xA9, 0x42 }; load(&mem, 0x0200, prog, 6);
    cpu816_step(&cpu); /* LDA $778899 → NOP, PC avance de 4 */
    ASSERT_EQ((int)cpu.PC, 0x0204);
    cpu816_step(&cpu); /* LDA #$42 */
    ASSERT_EQ((int)(cpu.C & 0xFF), 0x42);
    memory_cleanup(&mem);
}

/* ─── B1.8 — Memory 24-bit bus extension lazy alloc ─────────────────── */

TEST(test_memory_read24_uninitialized_returns_zero) {
    memory_t mem;
    memory_init(&mem);
    ASSERT_EQ((int)memory_read24(&mem, 0x100000), 0);
    memory_cleanup(&mem);
}

TEST(test_memory_write24_then_read_round_trip) {
    memory_t mem;
    memory_init(&mem);
    memory_write24(&mem, 0x420000, 0x55);
    memory_write24(&mem, 0x42FFFF, 0xAA);
    ASSERT_EQ((int)memory_read24(&mem, 0x420000), 0x55);
    ASSERT_EQ((int)memory_read24(&mem, 0x42FFFF), 0xAA);
    /* Bank non touché reste à 0 */
    ASSERT_EQ((int)memory_read24(&mem, 0x430000), 0);
    memory_cleanup(&mem);
}

TEST(test_memory_24_bank_0_routes_to_ram) {
    memory_t mem;
    memory_init(&mem);
    memory_write24(&mem, 0x0050, 0xCC);
    /* memory_read (bank 0) doit voir la même valeur. */
    ASSERT_EQ((int)memory_read(&mem, 0x0050), 0xCC);
    ASSERT_EQ((int)mem.ram[0x0050], 0xCC);
    memory_cleanup(&mem);
}

TEST(test_xce_back_to_e_preserves_b_register) {
    cpu65c816_t cpu; memory_t mem; boot(&cpu, &mem);
    enter_native_M0(&cpu, &mem);
    cpu.C = 0xDEAD;  /* B=$DE, A=$AD */
    /* SEP #$20 (M=1) ; SEC ; XCE → mode E. B doit rester $DE. */
    const uint8_t prog[] = { 0xE2, 0x20, 0x38, 0xFB };
    load(&mem, cpu.PC, prog, 4);
    for (int i = 0; i < 4; i++) cpu816_step(&cpu);
    ASSERT_TRUE(cpu.E);
    ASSERT_EQ((int)cpu.C, 0xDEAD); /* B et A intacts */
}

int main(void) {
    printf("\n");
    printf("═══════════════════════════════════════════════════════\n");
    printf("  65C816 Native Mode Tests (B1.7a — REP/SEP + M flag)\n");
    printf("═══════════════════════════════════════════════════════\n\n");

    RUN(test_sep_set_m_in_native_mode);
    RUN(test_rep_in_emulation_mode_keeps_unused);
    RUN(test_sep_x_truncates_x_y_high_bytes);
    RUN(test_lda_immediate_16_bit_in_native);
    RUN(test_lda_immediate_16_bit_negative_flag_uses_bit15);
    RUN(test_sta_abs_writes_two_bytes);
    RUN(test_lda_abs_reads_two_bytes);
    RUN(test_adc_16_bit_carry_overflow);
    RUN(test_adc_16_bit_carry_out);
    RUN(test_and_or_eor_16_bit);
    RUN(test_cmp_16_bit_sets_carry_when_geq);
    RUN(test_inc_a_dec_a_native_mode);
    RUN(test_inc_a_in_emulation_mode_is_nop);
    RUN(test_pha_pla_16_bit);
    RUN(test_eb_is_sbc_immediate_in_emulation);
    RUN(test_eb_is_xba_in_native_mode);
    RUN(test_xce_back_to_e_preserves_b_register);

    /* B1.7b */
    RUN(test_ldx_immediate_16_bit);
    RUN(test_ldy_abs_16_bit);
    RUN(test_stx_abs_writes_two_bytes);
    RUN(test_inx_dex_16_bit_wrap);
    RUN(test_iny_16_bit);
    RUN(test_cpx_16_bit);
    RUN(test_tcd_16_bit_with_n_z_flags);
    RUN(test_tdc_copies_d_to_c);
    RUN(test_tcs_in_emulation_forces_high_byte_01);
    RUN(test_tcs_in_native_full_16_bit);
    RUN(test_tsc_copies_s_to_c);
    RUN(test_txy_tyx_16_bit);
    RUN(test_tax_16_bit_in_native);
    RUN(test_tax_truncates_when_x_8bit);
    RUN(test_tya_16_bit_in_native);
    RUN(test_phx_plx_16_bit_in_native);
    RUN(test_txs_native_X0_copies_full_16bit);
    RUN(test_sta_dp_indirect_writes_DBR_bank);
    RUN(test_lda_dp_indirect_reads_DBR_bank);
    RUN(test_sep_x_truncates_x_and_y);
    RUN(test_phx_in_emulation_is_nop);

    /* B1.7c */
    RUN(test_phb_plb);
    RUN(test_phk_pushes_pbr);
    RUN(test_phd_pld_16_bit);
    RUN(test_pea_pushes_immediate_word);
    RUN(test_per_pushes_pc_plus_offset);
    RUN(test_phb_in_emulation_is_nop);
    RUN(test_bra_always_taken);
    RUN(test_brl_long_branch_signed);
    RUN(test_cop_in_emulation_uses_emul_vector);
    RUN(test_cop_in_native_uses_native_vector);
    RUN(test_wdm_consumes_one_byte);
    RUN(test_stz_abs_in_native_writes_two_zeros);
    RUN(test_stz_abs_in_emulation_is_nop);
    RUN(test_stz_zp_in_native);

    /* B1.8 — long addressing, JSL/RTL/JMP long, MVN, bus 24-bit */
    RUN(test_lda_long_reads_bank_2);
    RUN(test_sta_long_writes_bank_3);
    RUN(test_lda_long_x_indexed);
    RUN(test_lda_dp_indirect_long);
    RUN(test_lda_dp_indirect_long_y);
    RUN(test_jmp_long_jumps_to_bank);
    RUN(test_jsl_rtl_round_trip);
    RUN(test_mvn_copies_block_ascending);
    RUN(test_long_opcodes_in_emulation_consume_size);
    RUN(test_memory_read24_uninitialized_returns_zero);
    RUN(test_memory_write24_then_read_round_trip);
    RUN(test_memory_24_bank_0_routes_to_ram);

    printf("\n═══════════════════════════════════════════════════════\n");
    printf("  Results: %d passed, %d failed\n", tests_passed, tests_failed);
    printf("═══════════════════════════════════════════════════════\n\n");

    return tests_failed == 0 ? 0 : 1;
}
