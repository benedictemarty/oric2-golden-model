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

    printf("\n═══════════════════════════════════════════════════════\n");
    printf("  Results: %d passed, %d failed\n", tests_passed, tests_failed);
    printf("═══════════════════════════════════════════════════════\n\n");

    return tests_failed == 0 ? 0 : 1;
}
