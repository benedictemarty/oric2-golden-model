/**
 * @file test_oricos_gpu.c
 * @brief OricOS Sprint GPU-3 : kernel API gfx_* via GPU (ADR-21)
 * @author bmarty <bmarty@mailo.com>
 * @date 2026-05-09
 *
 * Boot kernel exécute :
 *   1. kernel_gfx_clear : SDRAM[$004000..$0043FF] = pattern $44
 *      (1024 octets, color=4=blue VGA).
 *   2. kernel_gfx_fill_rect : rectangle 8×4 pixels (x=4, y=2)
 *      color=15 (white) dans framebuffer SDRAM[$004000+] (BPL=512).
 *
 * Validation post-STP via vram_peek :
 *   - Octets non-rect = $44 (blue uniforme).
 *   - Octets rect = $FF (white, 2 pixels par byte).
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
    if (addr >= 0x0300 && addr <= 0x030F) {
        return via_read(ctx->via, (uint8_t)(addr & 0x0F));
    }
    if (addr >= 0x0320 && addr <= 0x0327) {
        return sd_read(ctx->sd, (uint8_t)(addr & 0x07));
    }
    if (addr >= 0x0330 && addr <= 0x033F) {
        return vram_read(ctx->vram, (uint8_t)(addr & 0x3F));
    }
    if (addr >= 0x0340 && addr <= 0x034F) {
        return gpu_read(ctx->gpu, (uint8_t)(addr & 0x4F));
    }
    return 0xFF;
}

static void io_write_callback(uint16_t addr, uint8_t value, void* userdata) {
    ctx_t* ctx = (ctx_t*)userdata;
    if (addr >= 0x0300 && addr <= 0x030F) {
        via_write(ctx->via, (uint8_t)(addr & 0x0F), value);
        return;
    }
    if (addr >= 0x0320 && addr <= 0x0327) {
        sd_write(ctx->sd, (uint8_t)(addr & 0x07), value);
        return;
    }
    if (addr >= 0x0330 && addr <= 0x033F) {
        vram_write(ctx->vram, ctx->mem, (uint8_t)(addr & 0x3F), value);
        return;
    }
    if (addr >= 0x0340 && addr <= 0x034F) {
        gpu_write(ctx->gpu, ctx->vram, ctx->mem, (uint8_t)(addr & 0x4F), value);
    }
}

/* ─── Test ─────────────────────────────────────────────────────────── */

TEST(test_oricos_gpu_clear_then_fill_rect) {
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
        memory_cleanup(&mem);
        return;
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

    int safety = 1000000;  /* CLEAR 1 KiB + FILL_RECT 8×4 = ~ms simulés */
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

    /* ── CLEAR a rempli 32 KiB en SDRAM[$004000..$00BFFF] = $44 ── */
    ASSERT_EQ((int)vram_peek(&vram, 0x004000), 0x44);
    ASSERT_EQ((int)vram_peek(&vram, 0x004100), 0x44);
    ASSERT_EQ((int)vram_peek(&vram, 0x00BFFF), 0x44);
    /* Hors range : 0 (init). */
    ASSERT_EQ((int)vram_peek(&vram, 0x003FFF), 0x00);
    ASSERT_EQ((int)vram_peek(&vram, 0x00C000), 0x00);

    /* ── FILL_RECT (x=4, y=2, w=8, h=4, color=15) dans framebuffer base
     * $004000, BPL=512. Rect couvre :
     *   x=4..11, y=2..5 (inclusif).
     *   bytes par ligne = bytes 2..5 inclusif (4 bytes par ligne, x4..11
     *   = 8 pixels = 4 bytes consécutifs car x_start=4 pair x_end=11 impair).
     *   Lignes 2..5 = 4 lignes.
     *   Total 16 bytes modifiés à $FF. */
    /* Ligne 2, byte 2 (x=4, pair gauche → écrasé entièrement = $FF). */
    ASSERT_EQ((int)vram_peek(&vram, 0x004000 + 2*512 + 2), 0xFF);
    ASSERT_EQ((int)vram_peek(&vram, 0x004000 + 2*512 + 5), 0xFF);
    /* Ligne 5, dernière ligne. */
    ASSERT_EQ((int)vram_peek(&vram, 0x004000 + 5*512 + 2), 0xFF);
    ASSERT_EQ((int)vram_peek(&vram, 0x004000 + 5*512 + 5), 0xFF);
    /* Hors rect : reste blue = $44. */
    ASSERT_EQ((int)vram_peek(&vram, 0x004000 + 2*512 + 1), 0x44); /* avant rect */
    ASSERT_EQ((int)vram_peek(&vram, 0x004000 + 2*512 + 6), 0x44); /* après rect */
    ASSERT_EQ((int)vram_peek(&vram, 0x004000 + 1*512 + 2), 0x44); /* ligne 1 hors rect */
    ASSERT_EQ((int)vram_peek(&vram, 0x004000 + 6*512 + 2), 0x44); /* ligne 6 hors rect */

    /* GPU status : pas d'erreur. */
    ASSERT_EQ(gpu.err, 0);

    gpu_cleanup(&gpu);
    vram_cleanup(&vram);
    memory_cleanup(&mem);
}

int main(void) {
    printf("\n");
    printf("═══════════════════════════════════════════════════════\n");
    printf("  OricOS GPU kernel API test (Sprint GPU-3, ADR-21)\n");
    printf("═══════════════════════════════════════════════════════\n\n");

    RUN(test_oricos_gpu_clear_then_fill_rect);

    printf("\n═══════════════════════════════════════════════════════\n");
    printf("  Results: %d passed, %d failed\n", tests_passed, tests_failed);
    printf("═══════════════════════════════════════════════════════\n\n");

    return tests_failed == 0 ? 0 : 1;
}
