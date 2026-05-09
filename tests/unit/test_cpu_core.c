/**
 * @file test_cpu_core.c
 * @brief Tests vtable cpu_core (cœur 65C816 unique)
 * @author bmarty <bmarty@mailo.com>
 * @date 2026-05-07 (révisé 2026-05-09 — PH-2.c.1, ADR-18 étape 1.C : retrait
 *       du cœur 6502 historique. Tests sélecteurs vtable 65C816 + rétro-compat
 *       CLI "--cpu 6502" → 65C816 mode E).
 */

#include <stdio.h>
#include <string.h>
#include "cpu/cpu_core.h"

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

/* ─── Sélecteurs vtable / kind_from_string ──────────────────────────── */

TEST(test_vtable_for_65c816_returns_table) {
    const cpu_core_vtable_t* vt = cpu_core_vtable_for(CPU_KIND_65C816);
    ASSERT_TRUE(vt != NULL);
    ASSERT_TRUE(vt == &cpu_core_vtable_65c816);
    ASSERT_TRUE(strcmp(vt->name, "65C816") == 0);
}

TEST(test_kind_from_string_65c816_lower_and_upper) {
    cpu_kind_t k;
    ASSERT_TRUE(cpu_core_kind_from_string("65c816", &k));
    ASSERT_EQ((int)k, (int)CPU_KIND_65C816);
    ASSERT_TRUE(cpu_core_kind_from_string("65C816", &k));
    ASSERT_EQ((int)k, (int)CPU_KIND_65C816);
}

/* PH-2.c.1 : "--cpu 6502" est rétro-compat (log warn + redirige vers 65C816). */
TEST(test_kind_from_string_6502_redirects_to_65c816) {
    cpu_kind_t k;
    ASSERT_TRUE(cpu_core_kind_from_string("6502", &k));
    ASSERT_EQ((int)k, (int)CPU_KIND_65C816);
}

TEST(test_kind_from_string_invalid) {
    cpu_kind_t k;
    ASSERT_FALSE(cpu_core_kind_from_string("z80", &k));
    ASSERT_FALSE(cpu_core_kind_from_string("", &k));
    ASSERT_FALSE(cpu_core_kind_from_string(NULL, &k));
}

/* ─── Vtable 65C816 — pointeurs non-NULL et nom ──────────────────────── */

TEST(test_vtable_65c816_methods_non_null) {
    const cpu_core_vtable_t* vt = &cpu_core_vtable_65c816;
    ASSERT_TRUE(vt->name != NULL);
    ASSERT_TRUE(vt->reset != NULL);
    ASSERT_TRUE(vt->step != NULL);
    ASSERT_TRUE(vt->execute_cycles != NULL);
    ASSERT_TRUE(vt->nmi != NULL);
    ASSERT_TRUE(vt->irq_set != NULL);
    ASSERT_TRUE(vt->irq_clear != NULL);
}

int main(void) {
    printf("\n");
    printf("═══════════════════════════════════════════════════════\n");
    printf("  CPU Core Vtable Tests (65C816 unique post-ADR-18)\n");
    printf("═══════════════════════════════════════════════════════\n\n");

    RUN(test_vtable_for_65c816_returns_table);
    RUN(test_kind_from_string_65c816_lower_and_upper);
    RUN(test_kind_from_string_6502_redirects_to_65c816);
    RUN(test_kind_from_string_invalid);
    RUN(test_vtable_65c816_methods_non_null);

    printf("\n═══════════════════════════════════════════════════════\n");
    printf("  Results: %d passed, %d failed\n", tests_passed, tests_failed);
    printf("═══════════════════════════════════════════════════════\n\n");

    return tests_failed == 0 ? 0 : 1;
}
