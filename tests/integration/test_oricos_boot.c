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
#include "io/via6522.h"

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

/* ─── Full-system context for VIA-based test (Sprint 2.a) ────────────── */

typedef struct {
    cpu65c816_t* cpu;
    via6522_t*   via;
    int irq_callback_count;
    int last_irq_state;
} sched_ctx_t;

static void via_irq_callback(bool state, void* userdata) {
    sched_ctx_t* ctx = (sched_ctx_t*)userdata;
    ctx->irq_callback_count++;
    ctx->last_irq_state = state ? 1 : 0;
    if (state) cpu816_irq_set(ctx->cpu, IRQF_VIA);
    else       cpu816_irq_clear(ctx->cpu, IRQF_VIA);
}

static uint8_t io_read_callback(uint16_t addr, void* userdata) {
    sched_ctx_t* ctx = (sched_ctx_t*)userdata;
    /* $0300-$030F : VIA 6522 (mirroir $0300-$03FF avec mask 0x0F). */
    if (addr >= 0x0300 && addr <= 0x03FF) {
        return via_read(ctx->via, (uint8_t)(addr & 0x0F));
    }
    return 0xFF;
}

static void io_write_callback(uint16_t addr, uint8_t value, void* userdata) {
    sched_ctx_t* ctx = (sched_ctx_t*)userdata;
    if (addr >= 0x0300 && addr <= 0x03FF) {
        via_write(ctx->via, (uint8_t)(addr & 0x0F), value);
    }
}

TEST(test_oricos_sprint2a_via_t1_timer_drives_scheduler) {
    cpu65c816_t cpu;
    memory_t mem;
    via6522_t via;
    sched_ctx_t ctx = { &cpu, &via, 0, 0 };

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
    install_nmi_trampoline(&mem);
    install_irq_trampoline(&mem);

    via_init(&via);
    via_reset(&via);
    via_set_irq_callback(&via, via_irq_callback, &ctx);
    memory_set_io_callbacks(&mem, io_read_callback, io_write_callback, &ctx);

    cpu816_init(&cpu, &mem);
    cpu816_reset(&cpu);

    /* Sprint 2.a : autonomie complète, plus aucune injection IRQ
     * manuelle. Le kernel configure VIA T1 (ACR/T1L/IER) puis CLI.
     * VIA T1 fire IRQ tous les ~512 cycles, scheduler kernel ack via
     * lecture T1C-L et bascule task. STP au tick #10 → ~5120 cycles. */
    int safety = 200000;
    int cycle_total = 0;
    while (safety-- > 0 && !cpu.stopped) {
        int c = cpu816_step(&cpu);
        if (c < 0) {
            printf("FAIL\n    step error at %02X:%04X (cycles=%d)\n",
                   cpu.PBR, cpu.PC, cycle_total);
            tests_failed++;
            memory_cleanup(&mem);
            return;
        }
        cycle_total += c;
        via_update(&via, c);
    }

    if (!cpu.stopped) {
        printf("FAIL\n    cpu not stopped after %d cycles. tick=%d "
               "A_ctr=%d B_ctr=%d PBR:PC=%02X:%04X\n"
               "    VIA debug: irq_callback_count=%d last_state=%d "
               "ifr=%02X ier=%02X t1_counter=%04X t1_running=%d acr=%02X\n",
               cycle_total,
               (int)memory_read24(&mem, 0x015400),
               (int)memory_read24(&mem, 0x015440),
               (int)memory_read24(&mem, 0x015444),
               cpu.PBR, cpu.PC,
               ctx.irq_callback_count, ctx.last_irq_state,
               (unsigned)via.ifr, (unsigned)via.ier,
               (unsigned)via.t1_counter, (int)via.t1_running,
               (unsigned)via.acr);
        tests_failed++; memory_cleanup(&mem); return;
    }

    /* Vérifications */
    ASSERT_EQ((int)memory_read24(&mem, 0x015400), 10);
    int a = (int)memory_read24(&mem, 0x015440);
    int b = (int)memory_read24(&mem, 0x015444);
    if (a == 0) { printf("FAIL\n    task A counter = 0\n"); tests_failed++; memory_cleanup(&mem); return; }
    if (b == 0) { printf("FAIL\n    task B counter = 0\n"); tests_failed++; memory_cleanup(&mem); return; }

    ASSERT_TRUE(!cpu.E);
    ASSERT_EQ((int)cpu.PBR, 0x01);

    /* Sprint 2.b : bank allocator. */
    ASSERT_EQ((int)memory_read24(&mem, 0x015460), 0x04);
    ASSERT_EQ((int)memory_read24(&mem, 0x015461), 0x05);
    ASSERT_EQ((int)memory_read24(&mem, 0x015462), 0x06);
    ASSERT_EQ((int)memory_read24(&mem, 0x015450), 0x07);

    /* Sprint 2.c : driver console. Le boot kernel a clear screen et
     * écrit "OricOS v0.7" à $00BB80 (screen RAM Oric 1, bank 0). */
    static const char banner[] = "OricOS v0.7";
    for (int i = 0; i < (int)sizeof(banner) - 1; i++) {
        int got = (int)memory_read24(&mem, 0x00BB80 + i);
        if (got != (int)(unsigned char)banner[i]) {
            printf("FAIL\n    screen[%d] = 0x%02X, expected '%c' (0x%02X)\n",
                   i, got, banner[i], banner[i]);
            tests_failed++; memory_cleanup(&mem); return;
        }
    }
    /* Vérifie aussi qu'au-delà du banner le screen est cleared (espaces) */
    {
        int got = (int)memory_read24(&mem, 0x00BB8C);
        if (got != 0x20) {
            printf("FAIL\n    screen[12] = 0x%02X (after banner). Dump $BB80-$BB9F:\n    ", got);
            for (int i = 0; i < 32; i++) {
                printf("%02X ", (unsigned)memory_read24(&mem, 0x00BB80 + i));
            }
            printf("\n");
            tests_failed++; memory_cleanup(&mem); return;
        }
    }
    ASSERT_EQ((int)memory_read24(&mem, 0x00BFDF), 0x20);      /* dernier byte écran (40*28-1) */

    memory_cleanup(&mem);
}

int main(void) {
    printf("\n");
    printf("═══════════════════════════════════════════════════════\n");
    printf("  OricOS Sprint 1.a boot + NMI test (--machine oric2)\n");
    printf("═══════════════════════════════════════════════════════\n\n");

    /* Sprint 1.c (manual IRQ injection) supersedé par 2.a (VIA T1 réel)
     * — la fonction est conservée pour référence mais plus appelée. */
    RUN(test_oricos_sprint2a_via_t1_timer_drives_scheduler);

    printf("\n═══════════════════════════════════════════════════════\n");
    printf("  Results: %d passed, %d failed\n", tests_passed, tests_failed);
    printf("═══════════════════════════════════════════════════════\n\n");

    return tests_failed == 0 ? 0 : 1;
}
