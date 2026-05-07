/**
 * @file test_klaus_dormann.c
 * @brief Klaus Dormann 6502 functional test sur les deux cœurs (B1.5)
 * @author bmarty <bmarty@mailo.com>
 * @date 2026-05-07
 *
 * Charge le binaire pré-construit `6502_functional_test.bin` (64KB) du
 * repo Klaus2m5/6502_65C02_functional_tests dans la mémoire de Phosphoric,
 * lance le CPU à partir du vecteur RESET ($0400) et exécute jusqu'à
 * détecter un piège infini (`jmp *`).
 *
 * Convention :
 *   - PC == $3469 → succès (macro `success` du test)
 *   - PC stable ailleurs → trap d'erreur (l'adresse identifie le test échoué)
 *
 * Le binaire couvre $0000-$FFFF intégralement, vecteurs RESET/IRQ/NMI
 * compris (rom_vectors=1 dans la configuration Klaus par défaut).
 *
 * Test gated : skip avec PASS si le fichier n'est pas présent (i.e. le
 * sous-module Klaus n'a pas été cloné dans third_party/).
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "cpu/cpu6502.h"
#include "cpu/cpu65c816.h"
#include "memory/memory.h"

#define KLAUS_BIN_PATH      "third_party/6502_65C02_functional_tests/bin_files/6502_functional_test.bin"
#define KLAUS_SUCCESS_PC    0x3469u
#define KLAUS_RESET_VECTOR  0x0400u
#define KLAUS_MAX_CYCLES    250000000ull /* ~250M cycles ; le test passe en ~96M */

static int tests_passed = 0;
static int tests_failed = 0;

#define TEST(name) static void name(void)
#define RUN(name) do { \
    printf("  %-58s", #name); \
    fflush(stdout); \
    name(); \
    tests_passed++; \
    printf("PASS\n"); \
} while(0)

#define ASSERT_TRUE(x) do { \
    if (!(x)) { \
        printf("FAIL\n    %s:%d: expected true\n", __FILE__, __LINE__); \
        tests_failed++; return; \
    } \
} while(0)

#define ASSERT_EQ_HEX(a, b) do { \
    if ((unsigned)(a) != (unsigned)(b)) { \
        printf("FAIL\n    %s:%d: expected $%X, got $%X\n", \
               __FILE__, __LINE__, (unsigned)(b), (unsigned)(a)); \
        tests_failed++; return; \
    } \
} while(0)

/**
 * @brief Charge le binaire Klaus dans memory_t (RAM $0000-$BFFF + ROM $C000-$FFFF).
 * @return 0 si OK, -1 si fichier absent (test sera skipped), -2 si I/O error.
 */
static int load_klaus_bin(memory_t* mem) {
    FILE* fp = fopen(KLAUS_BIN_PATH, "rb");
    if (!fp) return -1;

    uint8_t buf[0x10000];
    size_t n = fread(buf, 1, sizeof(buf), fp);
    fclose(fp);

    if (n != 0x10000) {
        fprintf(stderr, "Klaus bin: expected 65536 bytes, read %zu\n", n);
        return -2;
    }

    memcpy(mem->ram, buf, RAM_SIZE);              /* $0000-$BFFF */
    memcpy(mem->rom, buf + RAM_SIZE, ROM_SIZE);   /* $C000-$FFFF */
    return 0;
}

/**
 * @brief Boucle step jusqu'à stall (PC inchangé deux fois) ou timeout.
 *
 * Pour ne pas vérifier après chaque step (coûteux), on échantillonne PC
 * tous les 65 536 cycles et on déclare stall si PC stagne deux échantillons.
 * Cela rate quelques cas de boucles courtes mais reste correct pour les
 * traps Klaus qui sont des `jmp *` exécutés indéfiniment.
 */
static uint16_t run_until_stall_6502(cpu6502_t* cpu) {
    uint16_t prev_sample = 0xFFFF;
    int stall_count = 0;
    uint64_t cycles_done = 0;

    while (cycles_done < KLAUS_MAX_CYCLES) {
        for (int i = 0; i < 65536; i++) {
            int c = cpu_step(cpu);
            if (c <= 0) return cpu->PC;
            cycles_done += (uint64_t)c;
        }
        if (cpu->PC == prev_sample) {
            if (++stall_count >= 2) return cpu->PC;
        } else {
            stall_count = 0;
            prev_sample = cpu->PC;
        }
    }
    return 0xFFFF; /* timeout */
}

static uint16_t run_until_stall_816(cpu65c816_t* cpu) {
    uint16_t prev_sample = 0xFFFF;
    int stall_count = 0;
    uint64_t cycles_done = 0;

    while (cycles_done < KLAUS_MAX_CYCLES) {
        for (int i = 0; i < 65536; i++) {
            int c = cpu816_step(cpu);
            if (c < 0) return cpu->PC;
            cycles_done += (uint64_t)c;
        }
        if (cpu->PC == prev_sample) {
            if (++stall_count >= 2) return cpu->PC;
        } else {
            stall_count = 0;
            prev_sample = cpu->PC;
        }
    }
    return 0xFFFF; /* timeout */
}

/* ─── Tests ─────────────────────────────────────────────────────────── */

TEST(test_klaus_passes_on_6502_core) {
    memory_t mem;
    cpu6502_t cpu;
    memory_init(&mem);
    int rc = load_klaus_bin(&mem);
    if (rc == -1) {
        printf("SKIP (bin absent)            "); /* aligne le PASS final */
        return;
    }
    ASSERT_TRUE(rc == 0);

    cpu_init(&cpu, &mem);
    cpu_reset(&cpu);
    /* Klaus place un `res_trap` sur le vecteur RESET (anti-reset spurieux).
     * L'entry point réel du test est $0400. */
    cpu.PC = KLAUS_RESET_VECTOR;

    uint16_t end_pc = run_until_stall_6502(&cpu);
    if (end_pc != KLAUS_SUCCESS_PC) {
        printf("FAIL\n    Klaus 6502 trap at PC=$%04X (expected $%04X), cycles=%llu\n",
               end_pc, KLAUS_SUCCESS_PC, (unsigned long long)cpu.cycles);
        tests_failed++;
        return;
    }
}

TEST(test_klaus_passes_on_65c816_core_e_mode) {
    memory_t mem;
    cpu65c816_t cpu;
    memory_init(&mem);
    int rc = load_klaus_bin(&mem);
    if (rc == -1) {
        printf("SKIP (bin absent)            ");
        return;
    }
    ASSERT_TRUE(rc == 0);

    cpu816_init(&cpu, &mem);
    cpu816_reset(&cpu);
    ASSERT_TRUE(cpu.E);
    /* Override PC : voir le test 6502 ci-dessus. */
    cpu.PC = KLAUS_RESET_VECTOR;

    uint16_t end_pc = run_until_stall_816(&cpu);
    if (end_pc != KLAUS_SUCCESS_PC) {
        printf("FAIL\n    Klaus 65C816(E) trap at PC=$%04X (expected $%04X), cycles=%llu\n",
               end_pc, KLAUS_SUCCESS_PC, (unsigned long long)cpu.cycles);
        tests_failed++;
        return;
    }
}

int main(void) {
    printf("\n");
    printf("═══════════════════════════════════════════════════════\n");
    printf("  Klaus Dormann 6502 Functional Test (B1.5)\n");
    printf("═══════════════════════════════════════════════════════\n\n");

    RUN(test_klaus_passes_on_6502_core);
    RUN(test_klaus_passes_on_65c816_core_e_mode);

    printf("\n═══════════════════════════════════════════════════════\n");
    printf("  Results: %d passed, %d failed\n", tests_passed, tests_failed);
    printf("═══════════════════════════════════════════════════════\n\n");

    return tests_failed == 0 ? 0 : 1;
}
