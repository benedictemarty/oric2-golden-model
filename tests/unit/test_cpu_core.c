/**
 * @file test_cpu_core.c
 * @brief Tests vtable cpu_core (B1.1, projet Oric 2)
 * @author bmarty <bmarty@mailo.com>
 * @date 2026-05-07
 *
 * Vérifie que la dispatch via vtable produit le même comportement
 * qu'un appel direct au cœur 6502, et que la résolution kind→vtable
 * et le parsing de la chaîne CLI sont corrects.
 */

#include <stdio.h>
#include <string.h>
#include "cpu/cpu_core.h"
#include "cpu/cpu6502.h"
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

/* Place le vecteur RESET à $0200 et reset le CPU. */
static void setup(cpu6502_t* cpu, memory_t* mem) {
    memory_init(mem);
    cpu_init(cpu, mem);
    mem->rom[0x3FFC] = 0x00;
    mem->rom[0x3FFD] = 0x02;
    cpu_reset(cpu);
}

/* ─── Sélecteurs ────────────────────────────────────────────────────── */

TEST(test_vtable_for_6502_returns_table) {
    const cpu_core_vtable_t* vt = cpu_core_vtable_for(CPU_KIND_6502);
    ASSERT_TRUE(vt != NULL);
    ASSERT_TRUE(vt == &cpu_core_vtable_6502);
    ASSERT_TRUE(strcmp(vt->name, "6502") == 0);
}

TEST(test_vtable_for_65c816_not_yet_supported) {
    /* B1.2 le rendra non-NULL. */
    const cpu_core_vtable_t* vt = cpu_core_vtable_for(CPU_KIND_65C816);
    ASSERT_TRUE(vt == NULL);
}

TEST(test_kind_from_string_6502) {
    cpu_kind_t k = (cpu_kind_t)42;
    ASSERT_TRUE(cpu_core_kind_from_string("6502", &k));
    ASSERT_EQ((int)k, (int)CPU_KIND_6502);
}

TEST(test_kind_from_string_65c816_lower_and_upper) {
    cpu_kind_t k;
    ASSERT_TRUE(cpu_core_kind_from_string("65c816", &k));
    ASSERT_EQ((int)k, (int)CPU_KIND_65C816);
    ASSERT_TRUE(cpu_core_kind_from_string("65C816", &k));
    ASSERT_EQ((int)k, (int)CPU_KIND_65C816);
}

TEST(test_kind_from_string_invalid) {
    cpu_kind_t k;
    ASSERT_FALSE(cpu_core_kind_from_string("z80", &k));
    ASSERT_FALSE(cpu_core_kind_from_string("", &k));
    ASSERT_FALSE(cpu_core_kind_from_string(NULL, &k));
}

/* ─── Équivalence vtable ↔ appel direct ─────────────────────────────── */

TEST(test_vtable_reset_matches_direct) {
    cpu6502_t a, b;
    memory_t mem_a, mem_b;
    setup(&a, &mem_a);
    setup(&b, &mem_b);
    /* Modifie l'état pour s'assurer que reset le restaure */
    a.A = 0x42; a.X = 0x7F; b.A = 0x42; b.X = 0x7F;
    cpu_reset(&a);
    cpu_core_vtable_6502.reset(&b);
    ASSERT_EQ(a.A, b.A);
    ASSERT_EQ(a.X, b.X);
    ASSERT_EQ(a.PC, b.PC);
    ASSERT_EQ(a.SP, b.SP);
    ASSERT_EQ(a.P, b.P);
}

TEST(test_vtable_step_matches_direct) {
    cpu6502_t a, b;
    memory_t mem_a, mem_b;
    setup(&a, &mem_a);
    setup(&b, &mem_b);
    /* LDA #$55 ; NOP ; NOP — programme identique sur les deux. */
    const uint8_t prog[] = { 0xA9, 0x55, 0xEA, 0xEA };
    for (size_t i = 0; i < sizeof(prog); i++) {
        memory_write(&mem_a, (uint16_t)(0x0200 + i), prog[i]);
        memory_write(&mem_b, (uint16_t)(0x0200 + i), prog[i]);
    }
    int ca = cpu_step(&a);
    int cb = cpu_core_vtable_6502.step(&b);
    ASSERT_EQ(ca, cb);
    ASSERT_EQ(a.A, b.A);
    ASSERT_EQ(a.PC, b.PC);
    ASSERT_EQ((int)a.cycles, (int)b.cycles);
}

TEST(test_vtable_irq_set_clear_matches_direct) {
    cpu6502_t a, b;
    memory_t mem_a, mem_b;
    setup(&a, &mem_a);
    setup(&b, &mem_b);
    cpu_irq_set(&a, IRQF_VIA);
    cpu_core_vtable_6502.irq_set(&b, IRQF_VIA);
    ASSERT_EQ(a.irq, b.irq);
    cpu_irq_set(&a, IRQF_DISK);
    cpu_core_vtable_6502.irq_set(&b, IRQF_DISK);
    ASSERT_EQ(a.irq, b.irq);
    cpu_irq_clear(&a, IRQF_VIA);
    cpu_core_vtable_6502.irq_clear(&b, IRQF_VIA);
    ASSERT_EQ(a.irq, b.irq);
}

TEST(test_vtable_nmi_matches_direct) {
    cpu6502_t a, b;
    memory_t mem_a, mem_b;
    setup(&a, &mem_a);
    setup(&b, &mem_b);
    cpu_nmi(&a);
    cpu_core_vtable_6502.nmi(&b);
    ASSERT_EQ(a.nmi_pending, b.nmi_pending);
}

TEST(test_vtable_execute_cycles_matches_direct) {
    cpu6502_t a, b;
    memory_t mem_a, mem_b;
    setup(&a, &mem_a);
    setup(&b, &mem_b);
    /* Boucle de 4 NOP (8 cycles). */
    for (int i = 0; i < 8; i++) {
        memory_write(&mem_a, (uint16_t)(0x0200 + i), 0xEA);
        memory_write(&mem_b, (uint16_t)(0x0200 + i), 0xEA);
    }
    int ca = cpu_execute_cycles(&a, 6);
    int cb = cpu_core_vtable_6502.execute_cycles(&b, 6);
    ASSERT_EQ(ca, cb);
    ASSERT_EQ(a.PC, b.PC);
    ASSERT_EQ((int)a.cycles, (int)b.cycles);
}

int main(void) {
    printf("\n");
    printf("═══════════════════════════════════════════════════════\n");
    printf("  CPU Core Vtable Tests (B1.1)\n");
    printf("═══════════════════════════════════════════════════════\n\n");

    RUN(test_vtable_for_6502_returns_table);
    RUN(test_vtable_for_65c816_not_yet_supported);
    RUN(test_kind_from_string_6502);
    RUN(test_kind_from_string_65c816_lower_and_upper);
    RUN(test_kind_from_string_invalid);
    RUN(test_vtable_reset_matches_direct);
    RUN(test_vtable_step_matches_direct);
    RUN(test_vtable_irq_set_clear_matches_direct);
    RUN(test_vtable_nmi_matches_direct);
    RUN(test_vtable_execute_cycles_matches_direct);

    printf("\n═══════════════════════════════════════════════════════\n");
    printf("  Results: %d passed, %d failed\n", tests_passed, tests_failed);
    printf("═══════════════════════════════════════════════════════\n\n");

    return tests_failed == 0 ? 0 : 1;
}
