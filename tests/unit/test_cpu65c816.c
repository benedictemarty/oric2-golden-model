/**
 * @file test_cpu65c816.c
 * @brief Tests squelette 65C816 (B1.2, projet Oric 2)
 * @author bmarty <bmarty@mailo.com>
 * @date 2026-05-07
 *
 * Vérifie l'état initial après init et reset. Les opcodes (XCE et le
 * reste) arrivent en B1.3 — toute tentative d'exécution doit retourner
 * une erreur en B1.2.
 */

#include <stdio.h>
#include "cpu/cpu65c816.h"
#include "cpu/cpu6502.h"   /* FLAG_INTERRUPT, FLAG_BREAK, FLAG_UNUSED */
#include "memory/memory.h"

static int tests_passed = 0;
static int tests_failed = 0;

#define TEST(name) static void name(void)
#define RUN(name) do { \
    printf("  %-50s", #name); \
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

/* Place le vecteur RESET à $0200 dans la ROM. */
static void place_reset_vector(memory_t* mem, uint16_t addr) {
    mem->rom[0x3FFC] = (uint8_t)(addr & 0xFF);
    mem->rom[0x3FFD] = (uint8_t)(addr >> 8);
}

/* ─── Init ──────────────────────────────────────────────────────────── */

TEST(test_init_zeroes_state) {
    cpu65c816_t cpu;
    memory_t mem;
    memory_init(&mem);
    cpu816_init(&cpu, &mem);
    ASSERT_EQ(cpu.C, 0);
    ASSERT_EQ(cpu.X, 0);
    ASSERT_EQ(cpu.Y, 0);
    ASSERT_EQ(cpu.D, 0);
    ASSERT_EQ((int)cpu.DBR, 0);
    ASSERT_EQ((int)cpu.PBR, 0);
    ASSERT_TRUE(cpu.memory == &mem);
    ASSERT_FALSE(cpu.halted);
    ASSERT_FALSE(cpu.stopped);
    ASSERT_FALSE(cpu.waiting);
    ASSERT_FALSE(cpu.nmi_pending);
    ASSERT_EQ((int)cpu.irq, 0);
}

/* ─── Reset ─────────────────────────────────────────────────────────── */

TEST(test_reset_loads_pc_from_vector) {
    cpu65c816_t cpu;
    memory_t mem;
    memory_init(&mem);
    place_reset_vector(&mem, 0xC123);
    cpu816_init(&cpu, &mem);
    cpu816_reset(&cpu);
    ASSERT_EQ((int)cpu.PC, 0xC123);
}

TEST(test_reset_forces_emulation_mode) {
    cpu65c816_t cpu;
    memory_t mem;
    memory_init(&mem);
    place_reset_vector(&mem, 0x0200);
    cpu816_init(&cpu, &mem);
    /* Simule un état natif pré-reset : E doit être forcé à 1 par RES. */
    cpu.E = false;
    cpu816_reset(&cpu);
    ASSERT_TRUE(cpu.E);
    ASSERT_TRUE(cpu816_is_emulation(&cpu));
}

TEST(test_reset_stack_in_page_one) {
    cpu65c816_t cpu;
    memory_t mem;
    memory_init(&mem);
    place_reset_vector(&mem, 0x0200);
    cpu816_init(&cpu, &mem);
    cpu816_reset(&cpu);
    /* En mode E, S est verrouillé en page 1 ($01FF par convention au reset). */
    ASSERT_EQ((int)(cpu.S >> 8), 0x01);
    ASSERT_EQ((int)(cpu.S & 0xFF), 0xFF);
}

TEST(test_reset_clears_banks_and_direct_page) {
    cpu65c816_t cpu;
    memory_t mem;
    memory_init(&mem);
    place_reset_vector(&mem, 0x0200);
    cpu816_init(&cpu, &mem);
    /* Salit l'état pré-reset */
    cpu.DBR = 0x42;
    cpu.PBR = 0x99;
    cpu.D   = 0xABCD;
    cpu816_reset(&cpu);
    ASSERT_EQ((int)cpu.DBR, 0);
    ASSERT_EQ((int)cpu.PBR, 0);
    ASSERT_EQ((int)cpu.D, 0);
}

TEST(test_reset_processor_status) {
    cpu65c816_t cpu;
    memory_t mem;
    memory_init(&mem);
    place_reset_vector(&mem, 0x0200);
    cpu816_init(&cpu, &mem);
    cpu816_reset(&cpu);
    /* I doit être à 1 (IRQ masquée), D à 0 (post-reset), B et UNUSED à 1
     * (alignement avec le 6502 Phosphoric). */
    ASSERT_TRUE(cpu.P & FLAG_INTERRUPT);
    ASSERT_FALSE(cpu.P & FLAG_DECIMAL);
    ASSERT_TRUE(cpu.P & FLAG_BREAK);
    ASSERT_TRUE(cpu.P & FLAG_UNUSED);
}

TEST(test_reset_clears_pending_interrupts) {
    cpu65c816_t cpu;
    memory_t mem;
    memory_init(&mem);
    place_reset_vector(&mem, 0x0200);
    cpu816_init(&cpu, &mem);
    cpu.nmi_pending = true;
    cpu.irq = (uint8_t)IRQF_VIA;
    cpu.waiting = true;
    cpu816_reset(&cpu);
    ASSERT_FALSE(cpu.nmi_pending);
    ASSERT_EQ((int)cpu.irq, 0);
    ASSERT_FALSE(cpu.waiting);
}

/* ─── Interruptions (modèle level-triggered partagé) ─────────────────── */

TEST(test_irq_set_clear_bitfield) {
    cpu65c816_t cpu;
    memory_t mem;
    memory_init(&mem);
    place_reset_vector(&mem, 0x0200);
    cpu816_init(&cpu, &mem);
    cpu816_reset(&cpu);
    cpu816_irq_set(&cpu, IRQF_VIA);
    ASSERT_EQ((int)cpu.irq, (int)IRQF_VIA);
    cpu816_irq_set(&cpu, IRQF_DISK);
    ASSERT_EQ((int)cpu.irq, (int)(IRQF_VIA | IRQF_DISK));
    cpu816_irq_clear(&cpu, IRQF_VIA);
    ASSERT_EQ((int)cpu.irq, (int)IRQF_DISK);
}

TEST(test_nmi_sets_pending) {
    cpu65c816_t cpu;
    memory_t mem;
    memory_init(&mem);
    place_reset_vector(&mem, 0x0200);
    cpu816_init(&cpu, &mem);
    cpu816_reset(&cpu);
    cpu816_nmi(&cpu);
    ASSERT_TRUE(cpu.nmi_pending);
}

TEST(test_irq_set_wakes_from_wai) {
    cpu65c816_t cpu;
    memory_t mem;
    memory_init(&mem);
    place_reset_vector(&mem, 0x0200);
    cpu816_init(&cpu, &mem);
    cpu816_reset(&cpu);
    cpu.waiting = true;
    cpu816_irq_set(&cpu, IRQF_DISK);
    ASSERT_FALSE(cpu.waiting);
}

/* ─── Helpers d'exécution ────────────────────────────────────────────── */

static void load_prog(memory_t* mem, uint16_t addr, const uint8_t* code, size_t n) {
    for (size_t i = 0; i < n; i++) memory_write(mem, (uint16_t)(addr + i), code[i]);
}

static void boot(cpu65c816_t* cpu, memory_t* mem) {
    memory_init(mem);
    place_reset_vector(mem, 0x0200);
    cpu816_init(cpu, mem);
    cpu816_reset(cpu);
}

/* ─── Flag instructions (CLC/SEC/CLI/SEI/CLD/SED/CLV) ────────────────── */

TEST(test_clc_clears_carry) {
    cpu65c816_t cpu; memory_t mem; boot(&cpu, &mem);
    cpu.P |= FLAG_CARRY;
    const uint8_t prog[] = { 0x18 }; load_prog(&mem, 0x0200, prog, 1);
    int c = cpu816_step(&cpu);
    ASSERT_EQ(c, 2);
    ASSERT_FALSE(cpu.P & FLAG_CARRY);
    ASSERT_EQ((int)cpu.PC, 0x0201);
}

TEST(test_sec_sets_carry) {
    cpu65c816_t cpu; memory_t mem; boot(&cpu, &mem);
    cpu.P &= (uint8_t)~FLAG_CARRY;
    const uint8_t prog[] = { 0x38 }; load_prog(&mem, 0x0200, prog, 1);
    int c = cpu816_step(&cpu);
    ASSERT_EQ(c, 2);
    ASSERT_TRUE(cpu.P & FLAG_CARRY);
}

TEST(test_cli_clears_interrupt) {
    cpu65c816_t cpu; memory_t mem; boot(&cpu, &mem);
    cpu.P |= FLAG_INTERRUPT;
    const uint8_t prog[] = { 0x58 }; load_prog(&mem, 0x0200, prog, 1);
    cpu816_step(&cpu);
    ASSERT_FALSE(cpu.P & FLAG_INTERRUPT);
}

TEST(test_sei_sets_interrupt) {
    cpu65c816_t cpu; memory_t mem; boot(&cpu, &mem);
    cpu.P &= (uint8_t)~FLAG_INTERRUPT;
    const uint8_t prog[] = { 0x78 }; load_prog(&mem, 0x0200, prog, 1);
    cpu816_step(&cpu);
    ASSERT_TRUE(cpu.P & FLAG_INTERRUPT);
}

TEST(test_cld_clears_decimal) {
    cpu65c816_t cpu; memory_t mem; boot(&cpu, &mem);
    cpu.P |= FLAG_DECIMAL;
    const uint8_t prog[] = { 0xD8 }; load_prog(&mem, 0x0200, prog, 1);
    cpu816_step(&cpu);
    ASSERT_FALSE(cpu.P & FLAG_DECIMAL);
}

TEST(test_sed_sets_decimal) {
    cpu65c816_t cpu; memory_t mem; boot(&cpu, &mem);
    const uint8_t prog[] = { 0xF8 }; load_prog(&mem, 0x0200, prog, 1);
    cpu816_step(&cpu);
    ASSERT_TRUE(cpu.P & FLAG_DECIMAL);
}

TEST(test_clv_clears_overflow) {
    cpu65c816_t cpu; memory_t mem; boot(&cpu, &mem);
    cpu.P |= FLAG_OVERFLOW;
    const uint8_t prog[] = { 0xB8 }; load_prog(&mem, 0x0200, prog, 1);
    cpu816_step(&cpu);
    ASSERT_FALSE(cpu.P & FLAG_OVERFLOW);
}

/* ─── XCE — Exchange Carry / Emulation ──────────────────────────────── */

TEST(test_xce_e_to_native_when_clc_then_xce) {
    cpu65c816_t cpu; memory_t mem; boot(&cpu, &mem);
    /* Reset → E=1, C=? (0). CLC ; XCE → E doit recevoir l'ancien C (0) → mode N. */
    const uint8_t prog[] = { 0x18, 0xFB }; load_prog(&mem, 0x0200, prog, 2);
    ASSERT_TRUE(cpu.E);
    cpu816_step(&cpu);             /* CLC */
    int c = cpu816_step(&cpu);     /* XCE */
    ASSERT_EQ(c, 2);
    ASSERT_FALSE(cpu.E);
    /* Nouveau C = ancien E (1). */
    ASSERT_TRUE(cpu.P & FLAG_CARRY);
}

TEST(test_xce_native_to_e_forces_emulation_invariants) {
    cpu65c816_t cpu; memory_t mem; boot(&cpu, &mem);
    /* Bascule en mode N : CLC ; XCE → N */
    const uint8_t prog[] = { 0x18, 0xFB, 0x38, 0xFB };
    load_prog(&mem, 0x0200, prog, 4);
    cpu816_step(&cpu); cpu816_step(&cpu);
    ASSERT_FALSE(cpu.E);
    /* Salit l'état natif : registres 16 bits, S 16 bits, M=X=0 */
    cpu.P &= (uint8_t)~(FLAG816_M_MEM | FLAG816_X_INDEX);
    cpu.X = 0xCAFE;
    cpu.Y = 0xBABE;
    cpu.S = 0x1FF0;
    /* SEC ; XCE : C=1 → E=1, transition force M=X=1, X.high=Y.high=0, S.high=$01 */
    cpu816_step(&cpu); cpu816_step(&cpu);
    ASSERT_TRUE(cpu.E);
    ASSERT_TRUE(cpu.P & FLAG816_M_MEM);
    ASSERT_TRUE(cpu.P & FLAG816_X_INDEX);
    ASSERT_EQ((int)(cpu.X >> 8), 0);
    ASSERT_EQ((int)(cpu.Y >> 8), 0);
    ASSERT_EQ((int)(cpu.S >> 8), 0x01);
}

TEST(test_xce_round_trip_preserves_b_high_byte_of_C) {
    cpu65c816_t cpu; memory_t mem; boot(&cpu, &mem);
    /* C (16 bits) = $9942 : B=$99, A=$42. En mode E, A est 8 bits effectifs ;
     * B doit survivre à E→N→E. */
    cpu.C = 0x9942;
    const uint8_t prog[] = { 0x18, 0xFB,   /* E→N */
                              0x38, 0xFB }; /* N→E */
    load_prog(&mem, 0x0200, prog, 4);
    cpu816_step(&cpu); cpu816_step(&cpu);
    ASSERT_FALSE(cpu.E);
    ASSERT_EQ((int)cpu.C, 0x9942);  /* B et A intacts */
    cpu816_step(&cpu); cpu816_step(&cpu);
    ASSERT_TRUE(cpu.E);
    ASSERT_EQ((int)cpu.C, 0x9942);  /* B retenu après retour en mode E */
}

TEST(test_xce_consumes_two_cycles) {
    cpu65c816_t cpu; memory_t mem; boot(&cpu, &mem);
    const uint8_t prog[] = { 0xFB }; load_prog(&mem, 0x0200, prog, 1);
    uint64_t before = cpu.cycles;
    int c = cpu816_step(&cpu);
    ASSERT_EQ(c, 2);
    ASSERT_EQ((int)(cpu.cycles - before), 2);
}

/* ─── B1.4 a complété tous les opcodes 6502-équivalents : il n'existe
 *     plus d'« opcode non implémenté » en mode E (les illégaux NMOS sont
 *     traités comme NOP par défaut, ADR-11 hybride). Le test correspondant
 *     de B1.3 est devenu obsolète et a été retiré. ─── */

int main(void) {
    printf("\n");
    printf("═══════════════════════════════════════════════════════\n");
    printf("  65C816 Skeleton Tests (B1.2)\n");
    printf("═══════════════════════════════════════════════════════\n\n");

    RUN(test_init_zeroes_state);
    RUN(test_reset_loads_pc_from_vector);
    RUN(test_reset_forces_emulation_mode);
    RUN(test_reset_stack_in_page_one);
    RUN(test_reset_clears_banks_and_direct_page);
    RUN(test_reset_processor_status);
    RUN(test_reset_clears_pending_interrupts);
    RUN(test_irq_set_clear_bitfield);
    RUN(test_nmi_sets_pending);
    RUN(test_irq_set_wakes_from_wai);
    RUN(test_clc_clears_carry);
    RUN(test_sec_sets_carry);
    RUN(test_cli_clears_interrupt);
    RUN(test_sei_sets_interrupt);
    RUN(test_cld_clears_decimal);
    RUN(test_sed_sets_decimal);
    RUN(test_clv_clears_overflow);
    RUN(test_xce_e_to_native_when_clc_then_xce);
    RUN(test_xce_native_to_e_forces_emulation_invariants);
    RUN(test_xce_round_trip_preserves_b_high_byte_of_C);
    RUN(test_xce_consumes_two_cycles);

    printf("\n═══════════════════════════════════════════════════════\n");
    printf("  Results: %d passed, %d failed\n", tests_passed, tests_failed);
    printf("═══════════════════════════════════════════════════════\n\n");

    return tests_failed == 0 ? 0 : 1;
}
