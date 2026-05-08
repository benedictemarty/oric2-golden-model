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
 * @brief Installe le trampoline NMI bank 0 $0130 + vecteur natif $00FFEA.
 */
static void install_nmi_trampoline(memory_t* mem) {
    /* bank 0 $0130 : JML $015500 (kernel NMI handler) */
    memory_write24(mem, 0x000130, 0x5C);
    memory_write24(mem, 0x000131, 0x00);
    memory_write24(mem, 0x000132, 0x55);
    memory_write24(mem, 0x000133, 0x01);
    mem->rom[0x3FEA] = 0x30;
    mem->rom[0x3FEB] = 0x01;
}

/**
 * @brief Installe le trampoline IRQ bank 0 $0140 + vecteur natif $00FFEE.
 *        Le trampoline JML vers le IRQ handler kernel en bank 1 $5600.
 */
static void install_irq_trampoline(memory_t* mem) {
    /* bank 0 $0140 : JML $015600 (kernel IRQ handler) */
    memory_write24(mem, 0x000140, 0x5C);
    memory_write24(mem, 0x000141, 0x00);
    memory_write24(mem, 0x000142, 0x56);
    memory_write24(mem, 0x000143, 0x01);
    /* Vecteur IRQ mode N $00FFEE → $0140 */
    mem->rom[0x3FEE] = 0x40;
    mem->rom[0x3FEF] = 0x01;
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

TEST(test_oricos_sprint1c_irq_driven_scheduler) {
    cpu65c816_t cpu;
    memory_t mem;
    memory_init(&mem);
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
    install_nmi_trampoline(&mem);   /* NMI placeholder en kernel */
    install_irq_trampoline(&mem);   /* IRQ → scheduler */

    cpu816_init(&cpu, &mem);
    cpu816_reset(&cpu);

    /* Sprint 1.c : injection IRQ (level-triggered) avec pattern
     * set/step/clear pour mimer un timer (VIA T1) :
     *   1. cpu816_irq_set(IRQF_VIA)
     *   2. cpu816_step → CPU prend l'IRQ, set I=1, jump trampoline
     *   3. cpu816_irq_clear → ack comme si on avait lu un registre VIA
     *   4. handler tourne normalement, RTI → reprise tâche
     *
     * Le kernel STP après TICK_GOAL=10 IRQ traités. */
    int irq_injected = 0;
    int safety = 200000;
    int last_inject = 0;
    int cycle_total = 0;
    const int BOOT_GRACE_CYCLES = 1000;
    while (safety-- > 0 && !cpu.stopped) {
        /* Inject : pattern set/step/clear */
        if (irq_injected < 12
            && cycle_total >= BOOT_GRACE_CYCLES
            && (cycle_total - last_inject) >= 200) {
            cpu816_irq_set(&cpu, IRQF_VIA);
            int c1 = cpu816_step(&cpu);  /* prend l'IRQ */
            if (c1 < 0) {
                printf("FAIL\n    step (IRQ entry) error at %02X:%04X\n",
                       cpu.PBR, cpu.PC);
                tests_failed++; memory_cleanup(&mem); return;
            }
            cycle_total += c1;
            cpu816_irq_clear(&cpu, IRQF_VIA);
            irq_injected++;
            last_inject = cycle_total;
        }
        if (cpu.stopped) break;
        int c = cpu816_step(&cpu);
        if (c < 0) {
            printf("FAIL\n    step error at %02X:%04X (cycles=%d)\n",
                   cpu.PBR, cpu.PC, cycle_total);
            tests_failed++;
            memory_cleanup(&mem);
            return;
        }
        cycle_total += c;
    }
    if (!cpu.stopped) {
        printf("FAIL\n    cpu not stopped after %d cycles. IRQ inj=%d, "
               "tick=%d, A_ctr=%d, B_ctr=%d, PBR:PC=%02X:%04X\n",
               cycle_total, irq_injected,
               (int)memory_read24(&mem, 0x015400),
               (int)memory_read24(&mem, 0x015440),
               (int)memory_read24(&mem, 0x015444),
               cpu.PBR, cpu.PC);
        tests_failed++; memory_cleanup(&mem); return;
    }

    /* Sentinel + version "v0.3" Sprint 1.b */
    ASSERT_EQ((int)memory_read24(&mem, 0x015000), 'O');
    ASSERT_EQ((int)memory_read24(&mem, 0x015013), '3');

    /* Tick counter == TICK_GOAL = 10 */
    ASSERT_EQ((int)memory_read24(&mem, 0x015400), 10);

    /* Compteurs des deux tâches doivent être > 0 (chaque tâche a tourné).
     * Avec 10 ticks et round-robin, chaque task a eu ~5 slices. */
    int task_a_ctr = (int)memory_read24(&mem, 0x015440);
    int task_b_ctr = (int)memory_read24(&mem, 0x015444);
    if (task_a_ctr == 0) {
        printf("FAIL\n    task A counter = 0 (jamais exécutée)\n");
        tests_failed++; memory_cleanup(&mem); return;
    }
    if (task_b_ctr == 0) {
        printf("FAIL\n    task B counter = 0 (jamais exécutée)\n");
        tests_failed++; memory_cleanup(&mem); return;
    }

    /* CPU mode N, PBR = $01 */
    ASSERT_TRUE(!cpu.E);
    ASSERT_EQ((int)cpu.PBR, 0x01);

    memory_cleanup(&mem);
}

int main(void) {
    printf("\n");
    printf("═══════════════════════════════════════════════════════\n");
    printf("  OricOS Sprint 1.a boot + NMI test (--machine oric2)\n");
    printf("═══════════════════════════════════════════════════════\n\n");

    RUN(test_oricos_sprint1c_irq_driven_scheduler);

    printf("\n═══════════════════════════════════════════════════════\n");
    printf("  Results: %d passed, %d failed\n", tests_passed, tests_failed);
    printf("═══════════════════════════════════════════════════════\n\n");

    return tests_failed == 0 ? 0 : 1;
}
