/**
 * @file test_oricos_live_alloc.c
 * @brief OricOS Sprint VRAM-3 : pool LIVE banks 129-159 (ADR-19)
 * @author bmarty <bmarty@mailo.com>
 * @date 2026-05-09
 *
 * Boot kernel exécute la démo allocator live :
 *   - alloc 3 banks live consécutifs ($81, $82, $83).
 *   - free $82.
 *   - alloc 1 → doit retourner $82 (LIFO pop).
 *
 * Sentinels écrits dans BANK_LIVE_DEMO ($01:5468..$01:546B).
 */

#include <stdio.h>
#include <string.h>
#include "cpu/cpu65c816.h"
#include "memory/memory.h"
#include "io/via6522.h"
#include "io/sd_device.h"

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
    cpu65c816_t* cpu;
    via6522_t*   via;
    sd_device_t* sd;
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
    }
}

/* ─── Test ─────────────────────────────────────────────────────────── */

TEST(test_oricos_live_alloc_demo) {
    cpu65c816_t cpu;
    memory_t mem;
    via6522_t via;
    sd_device_t sd;
    ctx_t ctx = { &cpu, &via, &sd };

    memory_init(&mem);
    memory_alloc_bank(&mem, 1);
    memory_alloc_bank(&mem, 2);
    memory_alloc_bank(&mem, 3);
    memory_alloc_bank(&mem, 4);

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
    memory_set_io_callbacks(&mem, io_read_callback, io_write_callback, &ctx);

    cpu816_init(&cpu, &mem);
    cpu816_reset(&cpu);

    int safety = 500000;
    while (safety-- > 0 && !cpu.stopped) {
        int c = cpu816_step(&cpu);
        if (c < 0) {
            printf("FAIL\n    cpu step error at %02X:%04X\n", cpu.PBR, cpu.PC);
            tests_failed++; memory_cleanup(&mem); return;
        }
        via_update(&via, c);
    }
    ASSERT_TRUE(cpu.stopped);

    /* BANK_LIVE_DEMO @ $01:5468..$01:546B (ADR-20 : banks 128-131
     * réservés framebuffer SVGA, pool live démarre à bank 132 = $84) :
     *   +0 = 0x84 (1ère alloc bump, bank 132)
     *   +1 = 0x85 (2ème alloc bump, bank 133)
     *   +2 = 0x86 (3ème alloc bump, bank 134)
     *   +3 = 0x85 (alloc après free 0x85 → LIFO pop) */
    ASSERT_EQ((int)memory_read24(&mem, 0x015468), 0x84);
    ASSERT_EQ((int)memory_read24(&mem, 0x015469), 0x85);
    ASSERT_EQ((int)memory_read24(&mem, 0x01546A), 0x86);
    ASSERT_EQ((int)memory_read24(&mem, 0x01546B), 0x85);

    memory_cleanup(&mem);
}

int main(void) {
    printf("\n");
    printf("═══════════════════════════════════════════════════════\n");
    printf("  OricOS LIVE bank allocator test (Sprint VRAM-3, ADR-19)\n");
    printf("═══════════════════════════════════════════════════════\n\n");

    RUN(test_oricos_live_alloc_demo);

    printf("\n═══════════════════════════════════════════════════════\n");
    printf("  Results: %d passed, %d failed\n", tests_passed, tests_failed);
    printf("═══════════════════════════════════════════════════════\n\n");

    return tests_failed == 0 ? 0 : 1;
}
