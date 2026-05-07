/**
 * @file test_oric2_memory.c
 * @brief Tests memory map Oric 2 (B2)
 * @author bmarty <bmarty@mailo.com>
 * @date 2026-05-08
 *
 * Couvre l'allocation explicite des banks (memory_alloc_bank), la
 * persistance entre banks, et l'invariance bank 0 vis-à-vis de
 * l'extension 24-bit.
 *
 * Référence : docs/MEMORY_MAP.md
 */

#include <stdio.h>
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

/* ─── memory_alloc_bank ──────────────────────────────────────────────── */

TEST(test_alloc_bank_0_is_rejected) {
    memory_t mem; memory_init(&mem);
    /* Bank 0 est statique (ram + rom) — alloc_bank doit refuser. */
    ASSERT_FALSE(memory_alloc_bank(&mem, 0));
    memory_cleanup(&mem);
}

TEST(test_alloc_bank_1_initializes_to_zero) {
    memory_t mem; memory_init(&mem);
    ASSERT_TRUE(memory_alloc_bank(&mem, 1));
    /* Tout le bank doit être zéro après allocation */
    for (uint32_t off = 0; off < 65536; off += 4096) {
        ASSERT_EQ((int)memory_read24(&mem, 0x010000u + off), 0);
    }
    memory_cleanup(&mem);
}

TEST(test_alloc_bank_idempotent) {
    memory_t mem; memory_init(&mem);
    ASSERT_TRUE(memory_alloc_bank(&mem, 5));
    /* Écrit dans le bank */
    memory_write24(&mem, 0x051000, 0x42);
    /* Re-alloc : doit retourner true sans réinitialiser */
    ASSERT_TRUE(memory_alloc_bank(&mem, 5));
    ASSERT_EQ((int)memory_read24(&mem, 0x051000), 0x42);
    memory_cleanup(&mem);
}

TEST(test_alloc_banks_1_to_3_total_192k) {
    memory_t mem; memory_init(&mem);
    /* Simule le boot Oric 2 : alloue banks 1-3 explicitement. */
    for (uint8_t b = 1; b <= 3; b++) {
        ASSERT_TRUE(memory_alloc_bank(&mem, b));
    }
    /* Vérifie que les pointeurs sont distincts */
    ASSERT_TRUE(mem.extra_banks[1] != NULL);
    ASSERT_TRUE(mem.extra_banks[2] != NULL);
    ASSERT_TRUE(mem.extra_banks[3] != NULL);
    ASSERT_TRUE(mem.extra_banks[1] != mem.extra_banks[2]);
    ASSERT_TRUE(mem.extra_banks[2] != mem.extra_banks[3]);
    /* Banks 4+ non-alloués */
    ASSERT_TRUE(mem.extra_banks[4] == NULL);
    ASSERT_TRUE(mem.extra_banks[255] == NULL);
    memory_cleanup(&mem);
}

/* ─── Bank 0 invariance (compat Oric 1 stricte) ──────────────────────── */

TEST(test_bank_0_invariant_after_oric2_alloc) {
    memory_t mem; memory_init(&mem);
    /* Pré-écrit dans bank 0 (RAM Oric 1). */
    memory_write(&mem, 0x4000, 0xAA);
    memory_write(&mem, 0xBFFF, 0x55);
    /* Active mode oric2 (alloue banks 1-3). */
    for (uint8_t b = 1; b <= 3; b++) memory_alloc_bank(&mem, b);
    /* Bank 0 doit être strictement inchangé. */
    ASSERT_EQ((int)memory_read(&mem, 0x4000), 0xAA);
    ASSERT_EQ((int)memory_read(&mem, 0xBFFF), 0x55);
    /* Et la lecture 24-bit en bank 0 doit voir la même chose. */
    ASSERT_EQ((int)memory_read24(&mem, 0x004000), 0xAA);
    ASSERT_EQ((int)memory_read24(&mem, 0x00BFFF), 0x55);
    memory_cleanup(&mem);
}

TEST(test_bank_0_writes_dont_leak_to_other_banks) {
    memory_t mem; memory_init(&mem);
    for (uint8_t b = 1; b <= 3; b++) memory_alloc_bank(&mem, b);
    memory_write24(&mem, 0x001234, 0xCC);
    /* Vérifie qu'aucune copie en bank 1, 2, 3 à $1234 */
    ASSERT_EQ((int)memory_read24(&mem, 0x011234), 0);
    ASSERT_EQ((int)memory_read24(&mem, 0x021234), 0);
    ASSERT_EQ((int)memory_read24(&mem, 0x031234), 0);
    memory_cleanup(&mem);
}

/* ─── Memory map regions (cf. spec) ──────────────────────────────────── */

TEST(test_bank_1_kernel_region_writable) {
    memory_t mem; memory_init(&mem);
    memory_alloc_bank(&mem, 1);
    /* Écrit dans toutes les régions du bank 1 documentées. */
    memory_write24(&mem, 0x010050, 0x11); /* DP kernel */
    memory_write24(&mem, 0x010200, 0x22); /* Stack kernel */
    memory_write24(&mem, 0x012000, 0x33); /* Code kernel */
    memory_write24(&mem, 0x01F000, 0x44); /* ROM système (writable en émulation) */
    ASSERT_EQ((int)memory_read24(&mem, 0x010050), 0x11);
    ASSERT_EQ((int)memory_read24(&mem, 0x010200), 0x22);
    ASSERT_EQ((int)memory_read24(&mem, 0x012000), 0x33);
    ASSERT_EQ((int)memory_read24(&mem, 0x01F000), 0x44);
    memory_cleanup(&mem);
}

TEST(test_lazy_alloc_bank_above_3) {
    memory_t mem; memory_init(&mem);
    /* Ne pré-alloue rien. Écrire dans bank 100 doit l'allouer paresseusement. */
    ASSERT_TRUE(mem.extra_banks[100] == NULL);
    memory_write24(&mem, 0x643000, 0x77);
    ASSERT_TRUE(mem.extra_banks[100] != NULL);
    ASSERT_EQ((int)memory_read24(&mem, 0x643000), 0x77);
    memory_cleanup(&mem);
}

TEST(test_cleanup_frees_all_banks) {
    memory_t mem; memory_init(&mem);
    /* Alloue banks 1, 5, 100. */
    memory_alloc_bank(&mem, 1);
    memory_alloc_bank(&mem, 5);
    memory_alloc_bank(&mem, 100);
    ASSERT_TRUE(mem.extra_banks[1] != NULL);
    memory_cleanup(&mem);
    /* Tous les pointeurs remis à NULL. */
    ASSERT_TRUE(mem.extra_banks[1] == NULL);
    ASSERT_TRUE(mem.extra_banks[5] == NULL);
    ASSERT_TRUE(mem.extra_banks[100] == NULL);
}

int main(void) {
    printf("\n");
    printf("═══════════════════════════════════════════════════════\n");
    printf("  Oric 2 Memory Map Tests (B2 — bank alloc, regions)\n");
    printf("═══════════════════════════════════════════════════════\n\n");

    RUN(test_alloc_bank_0_is_rejected);
    RUN(test_alloc_bank_1_initializes_to_zero);
    RUN(test_alloc_bank_idempotent);
    RUN(test_alloc_banks_1_to_3_total_192k);
    RUN(test_bank_0_invariant_after_oric2_alloc);
    RUN(test_bank_0_writes_dont_leak_to_other_banks);
    RUN(test_bank_1_kernel_region_writable);
    RUN(test_lazy_alloc_bank_above_3);
    RUN(test_cleanup_frees_all_banks);

    printf("\n═══════════════════════════════════════════════════════\n");
    printf("  Results: %d passed, %d failed\n", tests_passed, tests_failed);
    printf("═══════════════════════════════════════════════════════\n\n");

    return tests_failed == 0 ? 0 : 1;
}
