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

/* ─── Exécution non encore disponible ────────────────────────────────── */

TEST(test_step_returns_error_in_skeleton) {
    cpu65c816_t cpu;
    memory_t mem;
    memory_init(&mem);
    place_reset_vector(&mem, 0x0200);
    cpu816_init(&cpu, &mem);
    cpu816_reset(&cpu);
    /* B1.2 : aucune instruction implémentée. step doit retourner < 0.
     * Le log_error attendu est imprimé sur stderr — bruit assumé. */
    int rc = cpu816_step(&cpu);
    ASSERT_TRUE(rc < 0);
    rc = cpu816_execute_cycles(&cpu, 100);
    ASSERT_TRUE(rc < 0);
}

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
    RUN(test_step_returns_error_in_skeleton);

    printf("\n═══════════════════════════════════════════════════════\n");
    printf("  Results: %d passed, %d failed\n", tests_passed, tests_failed);
    printf("═══════════════════════════════════════════════════════\n\n");

    return tests_failed == 0 ? 0 : 1;
}
