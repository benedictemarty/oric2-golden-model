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

TEST(test_phx_in_emulation_is_nop) {
    cpu65c816_t cpu; memory_t mem; boot(&cpu, &mem);
    cpu.X = 0x42;
    /* En mode E, PHX ($DA) doit être NOP (illégal NMOS, ADR-11(c)). */
    uint16_t sp_before = cpu.S;
    const uint8_t prog[] = { 0xDA }; load(&mem, 0x0200, prog, 1);
    cpu816_step(&cpu);
    ASSERT_EQ((int)cpu.S, (int)sp_before); /* SP inchangé */
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
    RUN(test_phx_in_emulation_is_nop);

    printf("\n═══════════════════════════════════════════════════════\n");
    printf("  Results: %d passed, %d failed\n", tests_passed, tests_failed);
    printf("═══════════════════════════════════════════════════════\n\n");

    return tests_failed == 0 ? 0 : 1;
}
