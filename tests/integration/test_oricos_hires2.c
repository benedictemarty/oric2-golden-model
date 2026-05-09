/**
 * @file test_oricos_hires2.c
 * @brief OricOS Sprint 3.b : kernel_hires2_clear → framebuffer Oric 2 (ADR-12)
 * @author bmarty <bmarty@mailo.com>
 * @date 2026-05-09
 *
 * Vérifie que `kernel_hires2_clear(blue)` au boot OricOS remplit la
 * bank 128 avec le pattern uniforme blue (color 4 = $92 $49 $24 répété).
 * Combine OricOS + module video/hires_oric2 (ADR-12) pour valider la
 * chaîne complète : kernel asm → bank 128 → render ARGB → ASSERT pixels.
 */

#include <stdio.h>
#include <string.h>
#include "cpu/cpu65c816.h"
#include "memory/memory.h"
#include "io/via6522.h"
#include "video/hires_oric2.h"

#define ORICOS_KERNEL_PATH  "../OricOS/build/kernel.bin"
#define ORICOS_LOAD_BANK    1
#define ORICOS_LOAD_OFFSET  0x0200u

#define BLUE_RGB            0x0000FFu
#define RED_RGB             0xFF0000u

static int tests_passed = 0;
static int tests_failed = 0;

#define TEST(name) static void name(void)
#define RUN(name) do { \
    printf("  %-58s", #name); fflush(stdout); \
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

/* Helpers (identiques aux autres tests OricOS). */
static int load_oricos_kernel(memory_t* mem) {
    FILE* fp = fopen(ORICOS_KERNEL_PATH, "rb");
    if (!fp) return -1;
    uint8_t buf[0xE000];
    size_t n = fread(buf, 1, sizeof(buf), fp);
    fclose(fp);
    if (n == 0) return -2;
    for (size_t i = 0; i < n; i++) {
        uint32_t addr24 = ((uint32_t)ORICOS_LOAD_BANK << 16)
                        | (uint32_t)(ORICOS_LOAD_OFFSET + i);
        memory_write24(mem, addr24, buf[i]);
    }
    return 0;
}

static void install_trampolines(memory_t* mem) {
    /* NMI bank 0 $0130 → JML $015500 */
    memory_write24(mem, 0x000130, 0x5C);
    memory_write24(mem, 0x000131, 0x00);
    memory_write24(mem, 0x000132, 0x55);
    memory_write24(mem, 0x000133, 0x01);
    mem->rom[0x3FEA] = 0x30; mem->rom[0x3FEB] = 0x01;
    /* IRQ bank 0 $0140 → JML $015600 */
    memory_write24(mem, 0x000140, 0x5C);
    memory_write24(mem, 0x000141, 0x00);
    memory_write24(mem, 0x000142, 0x56);
    memory_write24(mem, 0x000143, 0x01);
    mem->rom[0x3FEE] = 0x40; mem->rom[0x3FEF] = 0x01;
    /* COP bank 0 $0150 → JML $015700 */
    memory_write24(mem, 0x000150, 0x5C);
    memory_write24(mem, 0x000151, 0x00);
    memory_write24(mem, 0x000152, 0x57);
    memory_write24(mem, 0x000153, 0x01);
    mem->rom[0x3FE4] = 0x50; mem->rom[0x3FE5] = 0x01;
    /* Reset stub bank 0 $0100 → CLC ; XCE ; JML $010200 */
    const uint8_t stub[] = { 0x18, 0xFB, 0x5C, 0x00, 0x02, 0x01 };
    for (size_t i = 0; i < sizeof(stub); i++) {
        memory_write24(mem, 0x000100u + i, stub[i]);
    }
    mem->rom[0x3FFC] = 0x00; mem->rom[0x3FFD] = 0x01;
}

typedef struct {
    cpu65c816_t* cpu;
    via6522_t*   via;
} ctx_t;

static void via_irq_callback(bool state, void* userdata) {
    ctx_t* ctx = (ctx_t*)userdata;
    if (state) cpu816_irq_set(ctx->cpu, IRQF_VIA);
    else       cpu816_irq_clear(ctx->cpu, IRQF_VIA);
}

static uint8_t io_read_callback(uint16_t addr, void* userdata) {
    ctx_t* ctx = (ctx_t*)userdata;
    if (addr >= 0x0300 && addr <= 0x03FF) {
        return via_read(ctx->via, (uint8_t)(addr & 0x0F));
    }
    return 0xFF;
}

static void io_write_callback(uint16_t addr, uint8_t value, void* userdata) {
    ctx_t* ctx = (ctx_t*)userdata;
    if (addr >= 0x0300 && addr <= 0x03FF) {
        via_write(ctx->via, (uint8_t)(addr & 0x0F), value);
    }
}

/* ─── Test ─────────────────────────────────────────────────────────── */

TEST(test_oricos_hires2_clear_and_rect) {
    cpu65c816_t cpu;
    memory_t mem;
    via6522_t via;
    ctx_t ctx = { &cpu, &via };

    memory_init(&mem);
    memory_alloc_bank(&mem, 1);
    memory_alloc_bank(&mem, 2);
    memory_alloc_bank(&mem, 3);
    /* Bank 128 sera lazy-allouée par le kernel à la 1ère écriture. */

    int rc = load_oricos_kernel(&mem);
    if (rc == -1) {
        printf("SKIP (kernel.bin absent ; cd ../OricOS && make)        ");
        memory_cleanup(&mem);
        return;
    }
    ASSERT_EQ(rc, 0);

    install_trampolines(&mem);
    via_init(&via);
    via_reset(&via);
    via_set_irq_callback(&via, via_irq_callback, &ctx);
    memory_set_io_callbacks(&mem, io_read_callback, io_write_callback, &ctx);

    cpu816_init(&cpu, &mem);
    cpu816_reset(&cpu);

    /* Boot kernel. kernel_hires2_clear(blue) tourne tôt, écrit 18 000
     * octets en bank 128. ~80 000 cycles total à prévoir. */
    int safety = 500000;
    while (safety-- > 0 && !cpu.stopped) {
        int c = cpu816_step(&cpu);
        if (c < 0) {
            printf("FAIL\n    step error at %02X:%04X\n", cpu.PBR, cpu.PC);
            tests_failed++; memory_cleanup(&mem); return;
        }
        via_update(&via, c);
    }
    ASSERT_TRUE(cpu.stopped);

    /* Boot kernel exécute :
     *   1. kernel_hires2_clear(blue) → tout bank 128 = pattern $92 $49 $24
     *   2. kernel_fill_rect_aligned(gx=10, gxc=10, y=60, yc=80, red)
     *      → rectangle red 80×80 pixels en (80, 60).
     * Vérif :
     *   - 4 coins du framebuffer = blue (hors rectangle).
     *   - 4 coins + centre du rectangle = red.
     *   - juste hors rectangle (1 pixel offset) = blue.
     *   - comptage : 6400 pixels red + 41600 pixels blue, 0 autres. */

    ASSERT_EQ(hires_oric2_get_pixel(&mem, HIRES2_BANK_DEFAULT, 0, 0), 4);
    ASSERT_EQ(hires_oric2_get_pixel(&mem, HIRES2_BANK_DEFAULT, 239, 0), 4);
    ASSERT_EQ(hires_oric2_get_pixel(&mem, HIRES2_BANK_DEFAULT, 0, 199), 4);
    ASSERT_EQ(hires_oric2_get_pixel(&mem, HIRES2_BANK_DEFAULT, 239, 199), 4);

    /* Coins + centre du rectangle red. */
    ASSERT_EQ(hires_oric2_get_pixel(&mem, HIRES2_BANK_DEFAULT, 80, 60), 1);
    ASSERT_EQ(hires_oric2_get_pixel(&mem, HIRES2_BANK_DEFAULT, 159, 60), 1);
    ASSERT_EQ(hires_oric2_get_pixel(&mem, HIRES2_BANK_DEFAULT, 80, 139), 1);
    ASSERT_EQ(hires_oric2_get_pixel(&mem, HIRES2_BANK_DEFAULT, 159, 139), 1);
    ASSERT_EQ(hires_oric2_get_pixel(&mem, HIRES2_BANK_DEFAULT, 120, 100), 1);

    /* Frontières strictes : 1 pixel hors rect = blue. */
    ASSERT_EQ(hires_oric2_get_pixel(&mem, HIRES2_BANK_DEFAULT, 79, 60), 4);
    ASSERT_EQ(hires_oric2_get_pixel(&mem, HIRES2_BANK_DEFAULT, 160, 60), 4);
    ASSERT_EQ(hires_oric2_get_pixel(&mem, HIRES2_BANK_DEFAULT, 80, 59), 4);
    ASSERT_EQ(hires_oric2_get_pixel(&mem, HIRES2_BANK_DEFAULT, 80, 140), 4);

    /* Comptage ARGB : 6400 red + 41600 blue, 0 autres. */
    static uint32_t fb[HIRES2_W * HIRES2_H];
    hires_oric2_render_argb(&mem, HIRES2_BANK_DEFAULT, fb);
    int red_count = 0, blue_count = 0, other_count = 0;
    for (int i = 0; i < HIRES2_W * HIRES2_H; i++) {
        if (fb[i] == RED_RGB) red_count++;
        else if (fb[i] == BLUE_RGB) blue_count++;
        else other_count++;
    }
    ASSERT_EQ(red_count, 80 * 80);
    ASSERT_EQ(blue_count, HIRES2_W * HIRES2_H - 80 * 80);
    ASSERT_EQ(other_count, 0);

    memory_cleanup(&mem);
}

int main(void) {
    printf("\n");
    printf("═══════════════════════════════════════════════════════\n");
    printf("  OricOS HIRES Oric 2 framebuffer test (Sprint 3.b)\n");
    printf("═══════════════════════════════════════════════════════\n\n");

    RUN(test_oricos_hires2_clear_and_rect);

    printf("\n═══════════════════════════════════════════════════════\n");
    printf("  Results: %d passed, %d failed\n", tests_passed, tests_failed);
    printf("═══════════════════════════════════════════════════════\n\n");

    return tests_failed == 0 ? 0 : 1;
}
