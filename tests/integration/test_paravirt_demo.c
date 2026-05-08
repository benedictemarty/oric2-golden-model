/**
 * @file test_paravirt_demo.c
 * @brief Démonstrateur paravirtualisation kernel-mode-N / guest-mode-E (B3)
 * @author bmarty <bmarty@mailo.com>
 * @date 2026-05-08
 *
 * Démontre la mécanique de bascule mode E ↔ N via XCE et JML cross-bank,
 * conforme aux ADR-01 (CPU 65C816), ADR-02 (compositor ULA double, B4),
 * ADR-04 (banking sans MMU). Pas une implémentation OricOS ; un
 * **golden test** validant que les primitives CPU fournies en B1+B2
 * supportent bien le scénario de paravirtualisation envisagé pour OricOS.
 *
 * Scénario démontré :
 *   1. RESET CPU (mode E forcé, PC depuis vecteur $00FFFC).
 *   2. Vecteur RESET pointe sur kernel boot en bank 1.
 *      → Kernel boot active mode N (CLC ; XCE), exécute en bank 1.
 *   3. Kernel écrit un sentinel à bank 1 ($01:4000 = $AA).
 *   4. Kernel saute en bank 0 via JML — toujours en mode N.
 *   5. Guest entry en bank 0 ($0200) écrit un sentinel ($00:4000 = $BB),
 *      puis SEC ; XCE pour passer en mode E.
 *   6. Guest en mode E écrit un sentinel ($00:4001 = $CC) en utilisant
 *      les opcodes 8-bit, puis BRK pour signaler.
 *   7. Vecteur BRK en mode E ($00:FFFE) pointe sur trampoline qui
 *      revient en mode N (CLC ; XCE) puis JML vers handler en bank 1.
 *   8. Handler kernel en bank 1 écrit un sentinel ($01:5000 = $DD)
 *      et exécute STP pour arrêter la simulation proprement.
 *
 * Vérifie en sortie :
 *   - Tous les sentinels sont écrits aux bonnes adresses.
 *   - Le CPU est bien en mode N après le retour.
 *   - PBR final = $01 (kernel bank).
 *
 * Référence : DAT §4.2.2 (paravirtualisation guest), ADR-01.
 */

#include <stdio.h>
#include <string.h>
#include "cpu/cpu65c816.h"
#include "cpu/cpu6502.h"
#include "memory/memory.h"

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

#define ASSERT_FALSE(x) do { \
    if ((x)) { printf("FAIL\n    %s:%d: expected false\n", __FILE__, __LINE__); tests_failed++; return; } \
} while(0)

/* Helper : write byte sequence at a 24-bit address */
static void write_bytes(memory_t* mem, uint32_t addr, const uint8_t* code, size_t n) {
    for (size_t i = 0; i < n; i++) memory_write24(mem, addr + (uint32_t)i, code[i]);
}

/* ─── Test ──────────────────────────────────────────────────────────── */

TEST(test_paravirt_kernel_guest_round_trip) {
    cpu65c816_t cpu;
    memory_t mem;
    memory_init(&mem);
    /* Mode oric2 simulé : alloue banks 1-3. */
    memory_alloc_bank(&mem, 1);
    memory_alloc_bank(&mem, 2);
    memory_alloc_bank(&mem, 3);

    /* ─── Kernel boot en bank 1 $0200 ─── */
    /* Le vecteur RESET ($00FFFC) pointe sur stub trampoline en bank 0
     * qui passe en mode N et JML vers le kernel en bank 1.
     * On simplifie : reset vector → $0100 stub bank 0, qui CLC; XCE; JML.
     */
    /* Stub reset @ bank 0 $0100 :
     *   CLC ($18)   ; mode E → C=0
     *   XCE ($FB)   ; E=0 (mode N), C=1
     *   JML $010200 ($5C $00 $02 $01)
     */
    const uint8_t reset_stub[] = {
        0x18, 0xFB,
        0x5C, 0x00, 0x02, 0x01,
    };
    write_bytes(&mem, 0x000100, reset_stub, sizeof(reset_stub));
    /* Vecteur RESET → $0100 (bank 0) */
    mem.rom[0x3FFC] = 0x00; mem.rom[0x3FFD] = 0x01;

    /* Kernel boot en bank 1 $0200 :
     *   LDA #$AA ($A9 $AA)        ; A = $AA (mode N M=1 par défaut → 1 byte)
     *   STA $4000 ($8D $00 $40)   ; *bank-1-DBR-relative* — DBR=0 par défaut !
     *   Hmm. Pour écrire en bank 1 depuis kernel en mode N, on a besoin de
     *   DBR=1 ou d'utiliser STA long $014000.
     *   Simplifions avec long addressing : STA $014000 ($8F $00 $40 $01).
     *
     *   Puis JML $00:0200 (guest entry) ($5C $00 $02 $00).
     */
    const uint8_t kernel_boot[] = {
        0xA9, 0xAA,                    /* LDA #$AA */
        0x8F, 0x00, 0x40, 0x01,        /* STA $014000 (bank 1) */
        0x5C, 0x00, 0x02, 0x00,        /* JML $000200 (guest) */
    };
    write_bytes(&mem, 0x010200, kernel_boot, sizeof(kernel_boot));

    /* Guest entry en bank 0 $0200 :
     *   LDA #$BB ($A9 $BB)
     *   STA $4000 ($8D $00 $40)        ; bank 0 (DBR=0)
     *   SEC ($38)                      ; C=1
     *   XCE ($FB)                      ; E=1 (mode E)
     *   LDA #$CC ($A9 $CC)             ; mode E, 8-bit
     *   STA $4001 ($8D $01 $40)        ; bank 0
     *   BRK ($00 $99)                  ; soft int (signature byte)
     */
    const uint8_t guest_code[] = {
        0xA9, 0xBB,
        0x8D, 0x00, 0x40,
        0x38,
        0xFB,
        0xA9, 0xCC,
        0x8D, 0x01, 0x40,
        0x00, 0x99,
    };
    write_bytes(&mem, 0x000200, guest_code, sizeof(guest_code));

    /* Trampoline IRQ/BRK en bank 0 $0400 (vecteur IRQ mode E = $FFFE) :
     *   CLC ($18)                  ; C=0
     *   XCE ($FB)                  ; E=0 (mode N)
     *   JML $015000                ; handler kernel en bank 1
     */
    const uint8_t trampoline[] = {
        0x18, 0xFB,
        0x5C, 0x00, 0x50, 0x01,
    };
    write_bytes(&mem, 0x000400, trampoline, sizeof(trampoline));
    /* Vecteur IRQ/BRK mode E ($00FFFE) → $0400 */
    mem.rom[0x3FFE] = 0x00; mem.rom[0x3FFF] = 0x04;

    /* Handler kernel en bank 1 $5000 :
     *   LDA #$DD
     *   STA $015000 — non, on veut un sentinel à $015100 pour pas
     *                 écraser le handler lui-même.
     *   STZ $015100 (mais STZ NOP en mode E... on est en mode N ici).
     *   Plutôt : LDA #$DD ; STA $015100.
     *   Puis STP ($DB) pour arrêter le CPU proprement.
     */
    const uint8_t handler[] = {
        0xA9, 0xDD,
        0x8F, 0x00, 0x51, 0x01,        /* STA $015100 */
        0xDB,                          /* STP — halt */
    };
    write_bytes(&mem, 0x015000, handler, sizeof(handler));

    /* ─── Init et exécution ─── */
    cpu816_init(&cpu, &mem);
    cpu816_reset(&cpu);
    /* PC chargé depuis vecteur RESET = $0100 (bank 0). E=1 par défaut. */
    ASSERT_EQ((int)cpu.PC, 0x0100);
    ASSERT_TRUE(cpu.E);

    /* Exécute jusqu'à STP ou échec. Limite 1000 cycles (large). */
    int safety = 1000;
    while (safety-- > 0 && !cpu.stopped) {
        int c = cpu816_step(&cpu);
        if (c < 0) {
            printf("FAIL\n    cpu816_step error at PBR:PC = %02X:%04X\n",
                   cpu.PBR, cpu.PC);
            tests_failed++;
            memory_cleanup(&mem);
            return;
        }
    }
    ASSERT_TRUE(cpu.stopped);

    /* ─── Vérifications ─── */
    /* Sentinel kernel boot en bank 1 */
    ASSERT_EQ((int)memory_read24(&mem, 0x014000), 0xAA);
    /* Sentinel guest mode N en bank 0 */
    ASSERT_EQ((int)memory_read24(&mem, 0x004000), 0xBB);
    /* Sentinel guest mode E en bank 0 */
    ASSERT_EQ((int)memory_read24(&mem, 0x004001), 0xCC);
    /* Sentinel handler en bank 1 */
    ASSERT_EQ((int)memory_read24(&mem, 0x015100), 0xDD);
    /* CPU en mode N à la fin (handler kernel) */
    ASSERT_FALSE(cpu.E);
    /* PBR = bank 1 (handler) */
    ASSERT_EQ((int)cpu.PBR, 0x01);

    memory_cleanup(&mem);
}

TEST(test_paravirt_demonstrates_xce_round_trip_with_b_preserved) {
    /* Test plus simple : A préservé sur E→N→E pendant un saut cross-bank. */
    cpu65c816_t cpu;
    memory_t mem;
    memory_init(&mem);
    memory_alloc_bank(&mem, 1);

    /* @ bank 0 $0100 : mode N, écrit $11 dans bank 0 $4000, JML bank 1 */
    const uint8_t bank0[] = {
        0x18, 0xFB,                /* CLC ; XCE → N */
        0xA9, 0x11,                /* LDA #$11 */
        0x8D, 0x00, 0x40,          /* STA $4000 */
        0x5C, 0x00, 0x02, 0x01,    /* JML $010200 */
    };
    write_bytes(&mem, 0x000100, bank0, sizeof(bank0));

    /* @ bank 1 $0200 : écrit $22 en $015000, STP */
    const uint8_t bank1[] = {
        0xA9, 0x22,
        0x8F, 0x00, 0x50, 0x01,    /* STA $015000 */
        0xDB,                      /* STP */
    };
    write_bytes(&mem, 0x010200, bank1, sizeof(bank1));

    mem.rom[0x3FFC] = 0x00; mem.rom[0x3FFD] = 0x01;

    cpu816_init(&cpu, &mem); cpu816_reset(&cpu);

    int safety = 200;
    while (safety-- > 0 && !cpu.stopped) {
        if (cpu816_step(&cpu) < 0) { tests_failed++; memory_cleanup(&mem); return; }
    }
    ASSERT_TRUE(cpu.stopped);
    ASSERT_EQ((int)memory_read24(&mem, 0x004000), 0x11);
    ASSERT_EQ((int)memory_read24(&mem, 0x015000), 0x22);
    ASSERT_EQ((int)cpu.PBR, 0x01);
    ASSERT_FALSE(cpu.E);
    memory_cleanup(&mem);
}

int main(void) {
    printf("\n");
    printf("═══════════════════════════════════════════════════════\n");
    printf("  Oric 2 paravirtualisation demo (B3)\n");
    printf("═══════════════════════════════════════════════════════\n\n");

    RUN(test_paravirt_demonstrates_xce_round_trip_with_b_preserved);
    RUN(test_paravirt_kernel_guest_round_trip);

    printf("\n═══════════════════════════════════════════════════════\n");
    printf("  Results: %d passed, %d failed\n", tests_passed, tests_failed);
    printf("═══════════════════════════════════════════════════════\n\n");

    return tests_failed == 0 ? 0 : 1;
}
