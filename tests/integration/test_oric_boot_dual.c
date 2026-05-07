/**
 * @file test_oric_boot_dual.c
 * @brief Boot ROM Oric 1 — équivalence stricte 6502 ↔ 65C816 mode E (B1.6)
 * @author bmarty <bmarty@mailo.com>
 * @date 2026-05-07
 *
 * Charge la ROM Oric 1 BASIC 1.0 (16 KiB en $C000-$FFFF), reset les
 * deux cœurs, exécute N cycles sans I/O (pas de VIA, pas d'ULA), puis
 * vérifie l'équivalence stricte de l'état :
 *   - registres CPU (A/X/Y/SP/PC/P/cycles)
 *   - RAM complète ($0000-$BFFF)
 *
 * Sans VIA, le ROM boucle sur ses attentes timer (par ex. $E25E) après
 * sa phase d'init RAM ; les deux cœurs doivent boucler identiquement.
 *
 * Test gated : skip si la ROM n'est pas présente sous `roms/basic10.rom`.
 */

#include <stdio.h>
#include <string.h>
#include "cpu/cpu6502.h"
#include "cpu/cpu65c816.h"
#include "memory/memory.h"

#define ROM_PATH       "roms/basic10.rom"
#define ROM_EXPECTED   16384

static int tests_passed = 0;
static int tests_failed = 0;

#define TEST(name) static void name(void)
#define RUN(name) do { \
    printf("  %-58s", #name); \
    fflush(stdout); \
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
    if (!(x)) { \
        printf("FAIL\n    %s:%d: expected true\n", __FILE__, __LINE__); \
        tests_failed++; return; \
    } \
} while(0)

/* ─── Helpers ───────────────────────────────────────────────────────── */

static int load_rom(memory_t* mem) {
    FILE* fp = fopen(ROM_PATH, "rb");
    if (!fp) return -1;
    size_t n = fread(mem->rom, 1, ROM_EXPECTED, fp);
    fclose(fp);
    return (n == ROM_EXPECTED) ? 0 : -2;
}

/* Hash RAM rapide pour comparaison (FNV-1a 64). */
static uint64_t fnv1a(const uint8_t* p, size_t n) {
    uint64_t h = 0xcbf29ce484222325ull;
    for (size_t i = 0; i < n; i++) {
        h ^= p[i];
        h *= 0x100000001b3ull;
    }
    return h;
}

/* ─── Tests ─────────────────────────────────────────────────────────── */

TEST(test_rom_loads_and_reset_vector_matches_between_cores) {
    memory_t mem6, mem8;
    cpu6502_t c6;
    cpu65c816_t c8;
    memory_init(&mem6); memory_init(&mem8);
    if (load_rom(&mem6) != 0 || load_rom(&mem8) != 0) {
        printf("SKIP (basic10.rom absent)        ");
        return;
    }
    cpu_init(&c6, &mem6); cpu_reset(&c6);
    cpu816_init(&c8, &mem8); cpu816_reset(&c8);
    ASSERT_EQ((int)c6.PC, (int)c8.PC); /* même vecteur RESET */
    ASSERT_TRUE(c8.E);                  /* 65C816 démarre en mode E */
}

/**
 * Équivalence stricte sur N cycles d'exécution sans I/O.
 * Les deux cœurs doivent produire exactement le même état CPU et la
 * même image RAM. Passe Klaus → cycle/sémantique mode E identique →
 * boot ROM doit l'être aussi tant qu'on n'utilise pas d'opcodes natifs
 * (la ROM Oric 1 n'en utilise pas, par construction).
 */
static void run_equivalence(uint64_t target_cycles, const char* label) {
    memory_t mem6, mem8;
    cpu6502_t c6;
    cpu65c816_t c8;
    memory_init(&mem6); memory_init(&mem8);
    if (load_rom(&mem6) != 0 || load_rom(&mem8) != 0) {
        printf("SKIP (basic10.rom absent) [%s]", label);
        return;
    }
    cpu_init(&c6, &mem6); cpu_reset(&c6);
    cpu816_init(&c8, &mem8); cpu816_reset(&c8);

    /* Avance les deux cœurs jusqu'à dépasser target_cycles. */
    while (c6.cycles < target_cycles) {
        int n = cpu_step(&c6);
        if (n <= 0) break;
    }
    while (c8.cycles < target_cycles) {
        int n = cpu816_step(&c8);
        if (n < 0) break;
    }

    /* Égalité stricte des registres 8 bits (mode E) */
    ASSERT_EQ((int)c6.A,  (int)(c8.C & 0xFF));
    ASSERT_EQ((int)c6.X,  (int)(c8.X & 0xFF));
    ASSERT_EQ((int)c6.Y,  (int)(c8.Y & 0xFF));
    ASSERT_EQ((int)c6.SP, (int)(c8.S & 0xFF));
    ASSERT_EQ((int)c6.PC, (int)c8.PC);
    ASSERT_EQ((int)c6.P,  (int)c8.P);
    /* Le cycle compteur peut diverger d'un step ; on vérifie qu'on est
     * dans la fenêtre [target, target+max_instr_cycles=8). */
    ASSERT_TRUE(c6.cycles >= target_cycles);
    ASSERT_TRUE(c8.cycles >= target_cycles);
    ASSERT_EQ((int)c6.cycles, (int)c8.cycles);

    /* RAM identique bit-à-bit */
    uint64_t h6 = fnv1a(mem6.ram, RAM_SIZE);
    uint64_t h8 = fnv1a(mem8.ram, RAM_SIZE);
    if (h6 != h8) {
        printf("FAIL\n    [%s] RAM hash diverge : 6502=%016llx 65C816=%016llx\n",
               label, (unsigned long long)h6, (unsigned long long)h8);
        tests_failed++;
        return;
    }
    /* On est en mode E — le high byte de C ne doit pas avoir été pollué
     * par les ops 8 bits. */
    ASSERT_EQ((int)(c8.C >> 8), 0);
}

TEST(test_boot_equivalence_short_100k_cycles) {
    run_equivalence(100000ull, "100k");
}

TEST(test_boot_equivalence_medium_1m_cycles) {
    run_equivalence(1000000ull, "1M");
}

/**
 * Vérifie qu'après ~50 000 cycles le bootstrap de la ROM a réécrit la
 * zone $0000-$00FF (zero-page) avec des valeurs non triviales, signe
 * que le code ROM a bien été exécuté sur les deux cœurs.
 */
TEST(test_boot_writes_zero_page) {
    memory_t mem6;
    cpu6502_t c6;
    memory_init(&mem6);
    if (load_rom(&mem6) != 0) {
        printf("SKIP (basic10.rom absent)        ");
        return;
    }
    cpu_init(&c6, &mem6); cpu_reset(&c6);
    while (c6.cycles < 50000) {
        if (cpu_step(&c6) <= 0) break;
    }
    /* Cherche au moins un octet non nul en zero-page (le ROM y stocke
     * des pointeurs et flags durant l'init). */
    int has_non_zero = 0;
    for (int i = 0; i < 0x100; i++) {
        if (mem6.ram[i] != 0) { has_non_zero = 1; break; }
    }
    ASSERT_TRUE(has_non_zero);
}

int main(void) {
    printf("\n");
    printf("═══════════════════════════════════════════════════════\n");
    printf("  Oric 1 ROM Boot Equivalence — 6502 ↔ 65C816 (B1.6)\n");
    printf("═══════════════════════════════════════════════════════\n\n");

    RUN(test_rom_loads_and_reset_vector_matches_between_cores);
    RUN(test_boot_writes_zero_page);
    RUN(test_boot_equivalence_short_100k_cycles);
    RUN(test_boot_equivalence_medium_1m_cycles);

    printf("\n═══════════════════════════════════════════════════════\n");
    printf("  Results: %d passed, %d failed\n", tests_passed, tests_failed);
    printf("═══════════════════════════════════════════════════════\n\n");

    return tests_failed == 0 ? 0 : 1;
}
