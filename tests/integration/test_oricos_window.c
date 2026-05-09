/**
 * @file test_oricos_window.c
 * @brief OricOS Sprint 3.c v0.1 : kernel_window_draw via GPU
 * @author bmarty <bmarty@mailo.com>
 * @date 2026-05-09
 *
 * Boot kernel exécute :
 *   1. CLEAR fond noir 32 KiB à SDRAM[$00C000].
 *   2. kernel_window_draw(base=$00C000, x=20, y=10, w=80, h=60,
 *      titlebar_h=8, frame=0=black, title=1=blue, body=7=lgray).
 *
 * Validation post-STP : pixels frame, title bar, body.
 *
 * Layout pixels attendus (4bpp, BPL=512) :
 *   - Pixels frame (cadre 1px) : color 0 → byte $00.
 *   - Pixels title bar interior : color 1 → byte $11.
 *   - Pixels body interior : color 7 → byte $77.
 */

#include <stdio.h>
#include <string.h>
#include "cpu/cpu65c816.h"
#include "memory/memory.h"
#include "io/via6522.h"
#include "io/sd_device.h"
#include "io/vram_device.h"
#include "io/gpu_device.h"

#define ORICOS_KERNEL_PATH  "../OricOS/build/kernel.bin"
#define ORICOS_LOAD_BANK    1
#define ORICOS_LOAD_OFFSET  0x0200u

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
    if ((long)(a) != (long)(b)) { \
        printf("FAIL\n    %s:%d: expected %ld (0x%lX), got %ld (0x%lX)\n", \
               __FILE__, __LINE__, (long)(b), (long)(b), (long)(a), (long)(a)); \
        tests_failed++; return; \
    } \
} while(0)

#define ASSERT_TRUE(x) do { \
    if (!(x)) { printf("FAIL\n    %s:%d: expected true\n", __FILE__, __LINE__); tests_failed++; return; } \
} while(0)

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
    memory_write24(mem, 0x000130, 0x5C);
    memory_write24(mem, 0x000131, 0x00);
    memory_write24(mem, 0x000132, 0x55);
    memory_write24(mem, 0x000133, 0x01);
    mem->rom[0x3FEA] = 0x30; mem->rom[0x3FEB] = 0x01;
    memory_write24(mem, 0x000140, 0x5C);
    memory_write24(mem, 0x000141, 0x00);
    memory_write24(mem, 0x000142, 0x56);
    memory_write24(mem, 0x000143, 0x01);
    mem->rom[0x3FEE] = 0x40; mem->rom[0x3FEF] = 0x01;
    memory_write24(mem, 0x000150, 0x5C);
    memory_write24(mem, 0x000151, 0x00);
    memory_write24(mem, 0x000152, 0x57);
    memory_write24(mem, 0x000153, 0x01);
    mem->rom[0x3FE4] = 0x50; mem->rom[0x3FE5] = 0x01;
    const uint8_t stub[] = { 0x18, 0xFB, 0x5C, 0x00, 0x02, 0x01 };
    for (size_t i = 0; i < sizeof(stub); i++) {
        memory_write24(mem, 0x000100u + i, stub[i]);
    }
    mem->rom[0x3FFC] = 0x00; mem->rom[0x3FFD] = 0x01;
}

typedef struct {
    cpu65c816_t*   cpu;
    via6522_t*     via;
    sd_device_t*   sd;
    vram_device_t* vram;
    gpu_device_t*  gpu;
    memory_t*      mem;
} ctx_t;

static void via_irq_callback(bool state, void* userdata) {
    ctx_t* ctx = (ctx_t*)userdata;
    if (state) cpu816_irq_set(ctx->cpu, IRQF_VIA);
    else       cpu816_irq_clear(ctx->cpu, IRQF_VIA);
}

static uint8_t io_read_callback(uint16_t addr, void* userdata) {
    ctx_t* ctx = (ctx_t*)userdata;
    if (addr >= 0x0300 && addr <= 0x030F) return via_read(ctx->via, (uint8_t)(addr & 0x0F));
    if (addr >= 0x0320 && addr <= 0x0327) return sd_read(ctx->sd, (uint8_t)(addr & 0x07));
    if (addr >= 0x0330 && addr <= 0x033F) return vram_read(ctx->vram, (uint8_t)(addr & 0x3F));
    if (addr >= 0x0340 && addr <= 0x034F) return gpu_read(ctx->gpu, (uint8_t)(addr & 0x4F));
    return 0xFF;
}

static void io_write_callback(uint16_t addr, uint8_t value, void* userdata) {
    ctx_t* ctx = (ctx_t*)userdata;
    if (addr >= 0x0300 && addr <= 0x030F) { via_write(ctx->via, (uint8_t)(addr & 0x0F), value); return; }
    if (addr >= 0x0320 && addr <= 0x0327) { sd_write(ctx->sd, (uint8_t)(addr & 0x07), value); return; }
    if (addr >= 0x0330 && addr <= 0x033F) { vram_write(ctx->vram, ctx->mem, (uint8_t)(addr & 0x3F), value); return; }
    if (addr >= 0x0340 && addr <= 0x034F) { gpu_write(ctx->gpu, ctx->vram, ctx->mem, (uint8_t)(addr & 0x4F), value); }
}

/* ─── Helper : lit le pixel 4bpp à (x, y) du framebuffer base. ──── */
static int read_pixel_4bpp(vram_device_t* vram, uint32_t base, int x, int y) {
    uint32_t byte_off = base + (uint32_t)(y * 512 + x / 2);
    uint8_t b = vram_peek(vram, byte_off);
    if (x & 1) return (int)(b & 0x0F);     /* pixel droit */
    return (int)((b >> 4) & 0x0F);          /* pixel gauche */
}

/* ─── Test ─────────────────────────────────────────────────────────── */

TEST(test_oricos_window_draw) {
    cpu65c816_t cpu;
    memory_t mem;
    via6522_t via;
    sd_device_t sd;
    vram_device_t vram;
    gpu_device_t gpu;
    ctx_t ctx = { &cpu, &via, &sd, &vram, &gpu, &mem };

    memory_init(&mem);
    memory_alloc_bank(&mem, 1);
    memory_alloc_bank(&mem, 2);
    memory_alloc_bank(&mem, 3);

    int rc = load_oricos_kernel(&mem);
    if (rc == -1) {
        printf("SKIP (kernel.bin absent)                              ");
        memory_cleanup(&mem); return;
    }
    ASSERT_EQ(rc, 0);

    install_trampolines(&mem);
    via_init(&via);
    via_reset(&via);
    via_set_irq_callback(&via, via_irq_callback, &ctx);
    sd_init(&sd);
    ASSERT_TRUE(vram_init(&vram));
    gpu_init(&gpu);
    memory_set_io_callbacks(&mem, io_read_callback, io_write_callback, &ctx);

    cpu816_init(&cpu, &mem);
    cpu816_reset(&cpu);

    int safety = 2000000;  /* CLEAR 32 KiB + window FILL_RECT 80×60 */
    while (safety-- > 0 && !cpu.stopped) {
        int c = cpu816_step(&cpu);
        if (c < 0) {
            printf("FAIL\n    cpu step error at %02X:%04X\n", cpu.PBR, cpu.PC);
            tests_failed++; gpu_cleanup(&gpu); vram_cleanup(&vram);
            memory_cleanup(&mem); return;
        }
        via_update(&via, c);
    }
    ASSERT_TRUE(cpu.stopped);

    /* Fenêtre à (x=20, y=10, w=80, h=60), titlebar h=8.
     * Frame=0(black), title=1(blue), body=7(lgray).
     * Bord cadre : x=20, x=99, y=10, y=69 (1px épaisseur).
     * Title bar interior : x=20..99, y=10..17 (avant les bordures écrasent).
     * Body interior : x=20..99, y=18..69. */
    uint32_t base = 0x00C000;

    /* ── Cadre (1 px sur les 4 bords) = color 0 (black) ── */
    ASSERT_EQ(read_pixel_4bpp(&vram, base, 20, 10),  0);  /* coin top-left */
    ASSERT_EQ(read_pixel_4bpp(&vram, base, 99, 10),  0);  /* coin top-right */
    ASSERT_EQ(read_pixel_4bpp(&vram, base, 20, 69),  0);  /* coin bot-left */
    ASSERT_EQ(read_pixel_4bpp(&vram, base, 99, 69),  0);  /* coin bot-right */
    ASSERT_EQ(read_pixel_4bpp(&vram, base, 50, 10),  0);  /* milieu top edge */
    ASSERT_EQ(read_pixel_4bpp(&vram, base, 50, 69),  0);  /* milieu bot edge */
    ASSERT_EQ(read_pixel_4bpp(&vram, base, 20, 30),  0);  /* milieu left edge */
    ASSERT_EQ(read_pixel_4bpp(&vram, base, 99, 30),  0);  /* milieu right edge */

    /* ── Title bar interior : color 1 (blue) ── */
    ASSERT_EQ(read_pixel_4bpp(&vram, base, 50, 14),  1);  /* milieu titlebar */
    ASSERT_EQ(read_pixel_4bpp(&vram, base, 30, 11),  1);
    ASSERT_EQ(read_pixel_4bpp(&vram, base, 90, 17),  1);  /* dernière ligne titlebar */

    /* ── Body interior : color 7 (lightgray) ── */
    ASSERT_EQ(read_pixel_4bpp(&vram, base, 50, 30),  7);  /* milieu body */
    ASSERT_EQ(read_pixel_4bpp(&vram, base, 30, 18),  7);  /* début body (juste sous titlebar) */
    ASSERT_EQ(read_pixel_4bpp(&vram, base, 90, 60),  7);  /* fin body (avant frame bot) */

    /* ── Hors fenêtre : color 0 (black) du CLEAR initial ── */
    ASSERT_EQ(read_pixel_4bpp(&vram, base, 10, 10),  0);  /* à gauche fenêtre */
    ASSERT_EQ(read_pixel_4bpp(&vram, base, 100, 10), 0);  /* à droite fenêtre */
    ASSERT_EQ(read_pixel_4bpp(&vram, base, 50, 5),   0);  /* au-dessus fenêtre */
    ASSERT_EQ(read_pixel_4bpp(&vram, base, 50, 80),  0);  /* sous fenêtre */

    /* GPU sans erreur. */
    ASSERT_EQ(gpu.err, 0);

    gpu_cleanup(&gpu);
    vram_cleanup(&vram);
    memory_cleanup(&mem);
}

int main(void) {
    printf("\n");
    printf("═══════════════════════════════════════════════════════\n");
    printf("  OricOS window manager test (Sprint 3.c v0.1)\n");
    printf("═══════════════════════════════════════════════════════\n\n");

    RUN(test_oricos_window_draw);

    printf("\n═══════════════════════════════════════════════════════\n");
    printf("  Results: %d passed, %d failed\n", tests_passed, tests_failed);
    printf("═══════════════════════════════════════════════════════\n\n");

    return tests_failed == 0 ? 0 : 1;
}
