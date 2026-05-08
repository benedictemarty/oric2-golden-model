/**
 * @file test_oricos_boot.c
 * @brief Test d'intégration : boot OricOS Sprint 0 sous Phosphoric --machine oric2
 * @author bmarty <bmarty@mailo.com>
 * @date 2026-05-08
 *
 * Charge le binaire OricOS kernel (sous-projet `../OricOS`) en bank 1
 * $0200, configure un stub trampoline en bank 0, exécute jusqu'à STP,
 * et vérifie que le kernel a bien écrit son sentinel "ORIOS\x00" et sa
 * version "v0.1\x00" en bank 1 $5000.
 *
 * Test gated : skip si OricOS/build/kernel.bin absent (le binaire est
 * produit par `make` dans le sous-projet OricOS, pas inclus en git).
 *
 * Référence : OricOS Sprint 0, ADR-04 (banking), ADR-05 (kernel asm).
 */

#include <stdio.h>
#include <string.h>
#include "cpu/cpu65c816.h"
#include "memory/memory.h"

#define ORICOS_KERNEL_PATH  "../OricOS/build/kernel.bin"
#define ORICOS_LOAD_BANK    1
#define ORICOS_LOAD_OFFSET  0x0200u

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
    if (!(x)) { printf("FAIL\n    %s:%d: expected true\n", __FILE__, __LINE__); tests_failed++; return; } \
} while(0)

/**
 * @brief Charge le binaire kernel OricOS en bank 1 $0200.
 * @return 0 si OK, -1 si fichier absent (skip), -2 si I/O error.
 */
static int load_oricos_kernel(memory_t* mem) {
    FILE* fp = fopen(ORICOS_KERNEL_PATH, "rb");
    if (!fp) return -1;
    uint8_t buf[0xE000];
    size_t n = fread(buf, 1, sizeof(buf), fp);
    fclose(fp);
    if (n == 0) return -2;
    /* Charge en bank 1 à $0200 via memory_write24. */
    for (size_t i = 0; i < n; i++) {
        uint32_t addr24 = ((uint32_t)ORICOS_LOAD_BANK << 16)
                        | (uint32_t)(ORICOS_LOAD_OFFSET + i);
        memory_write24(mem, addr24, buf[i]);
    }
    return 0;
}

/**
 * @brief Installe le stub trampoline RESET en bank 0 $0100 :
 *   CLC ; XCE ; JML $010200 (kernel entry).
 */
static void install_reset_stub(memory_t* mem) {
    const uint8_t stub[] = {
        0x18,                          /* CLC : C=0 */
        0xFB,                          /* XCE : E=1→0 (mode N) */
        0x5C, 0x00, 0x02, 0x01,        /* JML $010200 */
    };
    for (size_t i = 0; i < sizeof(stub); i++) {
        memory_write24(mem, 0x000100u + i, stub[i]);
    }
    /* Vecteur RESET ($00FFFC) → $0100 */
    mem->rom[0x3FFC] = 0x00;
    mem->rom[0x3FFD] = 0x01;
}

/* ─── Tests ─────────────────────────────────────────────────────────── */

TEST(test_oricos_boots_and_writes_sentinel) {
    cpu65c816_t cpu;
    memory_t mem;
    memory_init(&mem);
    /* Mode oric2 simulé : alloue banks 1-3. */
    memory_alloc_bank(&mem, 1);
    memory_alloc_bank(&mem, 2);
    memory_alloc_bank(&mem, 3);

    int rc = load_oricos_kernel(&mem);
    if (rc == -1) {
        printf("SKIP (kernel.bin absent ; cd ../OricOS && make)        ");
        memory_cleanup(&mem);
        return;
    }
    ASSERT_EQ(rc, 0);

    install_reset_stub(&mem);

    cpu816_init(&cpu, &mem);
    cpu816_reset(&cpu);
    /* Vecteur RESET → $0100, mode E. */
    ASSERT_EQ((int)cpu.PC, 0x0100);
    ASSERT_TRUE(cpu.E);

    /* Exécute jusqu'à STP (kernel hello world s'arrête après écriture). */
    int safety = 1000;
    while (safety-- > 0 && !cpu.stopped) {
        int c = cpu816_step(&cpu);
        if (c < 0) {
            printf("FAIL\n    cpu816_step error at %02X:%04X\n",
                   cpu.PBR, cpu.PC);
            tests_failed++;
            memory_cleanup(&mem);
            return;
        }
    }
    ASSERT_TRUE(cpu.stopped);

    /* Vérifie le sentinel "ORIOS\x00" à $015000. */
    ASSERT_EQ((int)memory_read24(&mem, 0x015000), 'O');
    ASSERT_EQ((int)memory_read24(&mem, 0x015001), 'R');
    ASSERT_EQ((int)memory_read24(&mem, 0x015002), 'I');
    ASSERT_EQ((int)memory_read24(&mem, 0x015003), 'O');
    ASSERT_EQ((int)memory_read24(&mem, 0x015004), 'S');
    ASSERT_EQ((int)memory_read24(&mem, 0x015005), 0x00);

    /* Vérifie la version "v0.1\x00" à $015010. */
    ASSERT_EQ((int)memory_read24(&mem, 0x015010), 'v');
    ASSERT_EQ((int)memory_read24(&mem, 0x015011), '0');
    ASSERT_EQ((int)memory_read24(&mem, 0x015012), '.');
    ASSERT_EQ((int)memory_read24(&mem, 0x015013), '1');

    /* État final : mode N, PBR = bank 1 (kernel). */
    ASSERT_TRUE(!cpu.E);
    ASSERT_EQ((int)cpu.PBR, 0x01);

    memory_cleanup(&mem);
}

int main(void) {
    printf("\n");
    printf("═══════════════════════════════════════════════════════\n");
    printf("  OricOS Sprint 0 boot test (sous Phosphoric --machine oric2)\n");
    printf("═══════════════════════════════════════════════════════\n\n");

    RUN(test_oricos_boots_and_writes_sentinel);

    printf("\n═══════════════════════════════════════════════════════\n");
    printf("  Results: %d passed, %d failed\n", tests_passed, tests_failed);
    printf("═══════════════════════════════════════════════════════\n\n");

    return tests_failed == 0 ? 0 : 1;
}
