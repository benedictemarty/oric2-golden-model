/**
 * @file test_oricos_sd.c
 * @brief Test fonctionnel : driver SD bloc OricOS lit une image SD réelle
 * @author bmarty <bmarty@mailo.com>
 * @date 2026-05-08
 *
 * Vérifie que le pipeline complet driver SD côté kernel ↔ device émulé
 * Phosphoric fonctionne :
 *   1. Crée image SD test (1 bloc 512 octets, pattern reconnaissable).
 *   2. Charge le kernel OricOS + setup SD device avec l'image.
 *   3. Run jusqu'à STP.
 *   4. ASSERT que `mem[$01:5D40..+512]` contient le pattern (kernel a
 *      lu le bloc 0 via kernel_sd_read_block au boot).
 *
 * Test gated : skip si OricOS/build/kernel.bin absent.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "cpu/cpu65c816.h"
#include "memory/memory.h"
#include "io/via6522.h"
#include "io/sd_device.h"

#define ORICOS_KERNEL_PATH  "../OricOS/build/kernel.bin"
#define SD_TEST_IMAGE       "/tmp/oricos_sd_test.bin"
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

#define ASSERT_TRUE(x) do { \
    if (!(x)) { printf("FAIL\n    %s:%d: expected true\n", __FILE__, __LINE__); tests_failed++; return; } \
} while(0)

#define ASSERT_EQ(a, b) do { \
    if ((a) != (b)) { \
        printf("FAIL\n    %s:%d: expected %ld (0x%lX), got %ld (0x%lX)\n", \
               __FILE__, __LINE__, (long)(b), (long)(b), (long)(a), (long)(a)); \
        tests_failed++; return; \
    } \
} while(0)

/* ─── Context full-system ──────────────────────────────────────────── */

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
    if (addr >= 0x0320 && addr <= 0x0327) {
        return sd_read(ctx->sd, (uint8_t)(addr - 0x0320));
    }
    if (addr >= 0x0300 && addr <= 0x03FF) {
        return via_read(ctx->via, (uint8_t)(addr & 0x0F));
    }
    return 0xFF;
}

static void io_write_callback(uint16_t addr, uint8_t value, void* userdata) {
    ctx_t* ctx = (ctx_t*)userdata;
    if (addr >= 0x0320 && addr <= 0x0327) {
        sd_write(ctx->sd, (uint8_t)(addr - 0x0320), value);
        return;
    }
    if (addr >= 0x0300 && addr <= 0x03FF) {
        via_write(ctx->via, (uint8_t)(addr & 0x0F), value);
    }
}

/* ─── Helpers (dupliqués de test_oricos_boot.c) ────────────────────── */

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
    const uint8_t reset_stub[] = { 0x18, 0xFB, 0x5C, 0x00, 0x02, 0x01 };
    for (size_t i = 0; i < sizeof(reset_stub); i++)
        memory_write24(mem, 0x000100u + i, reset_stub[i]);
    const uint8_t irq_t[] = { 0x5C, 0x00, 0x56, 0x01 };
    for (size_t i = 0; i < sizeof(irq_t); i++)
        memory_write24(mem, 0x000140u + i, irq_t[i]);
    const uint8_t nmi_t[] = { 0x5C, 0x00, 0x55, 0x01 };
    for (size_t i = 0; i < sizeof(nmi_t); i++)
        memory_write24(mem, 0x000130u + i, nmi_t[i]);
    const uint8_t cop_t[] = { 0x5C, 0x00, 0x57, 0x01 };
    for (size_t i = 0; i < sizeof(cop_t); i++)
        memory_write24(mem, 0x000150u + i, cop_t[i]);
    mem->rom[0x3FFC] = 0x00; mem->rom[0x3FFD] = 0x01;
    mem->rom[0x3FFE] = 0x40; mem->rom[0x3FFF] = 0x01;
    mem->rom[0x3FEE] = 0x40; mem->rom[0x3FEF] = 0x01;
    mem->rom[0x3FEA] = 0x30; mem->rom[0x3FEB] = 0x01;
    mem->rom[0x3FFA] = 0x30; mem->rom[0x3FFB] = 0x01;
    mem->rom[0x3FE4] = 0x50; mem->rom[0x3FE5] = 0x01;
    mem->rom[0x3FF4] = 0x50; mem->rom[0x3FF5] = 0x01;
}

/* ─── Test ─────────────────────────────────────────────────────────── */

TEST(test_oricos_sd_read_block_via_kernel_boot) {
    /* Crée image SD test : pattern A..Z répété sur 512 octets. */
    FILE* fimg = fopen(SD_TEST_IMAGE, "wb");
    if (!fimg) {
        printf("FAIL\n    cannot create %s\n", SD_TEST_IMAGE);
        tests_failed++;
        return;
    }
    uint8_t pattern[SD_BLOCK_SIZE];
    for (size_t i = 0; i < SD_BLOCK_SIZE; i++) {
        pattern[i] = (uint8_t)('A' + (i % 26));
    }
    fwrite(pattern, 1, SD_BLOCK_SIZE, fimg);
    fclose(fimg);

    cpu65c816_t cpu;
    memory_t mem;
    via6522_t via;
    sd_device_t sd;
    ctx_t ctx = { &cpu, &via, &sd };

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
    if (!sd_load_image(&sd, SD_TEST_IMAGE)) {
        printf("FAIL\n    sd_load_image failed\n");
        tests_failed++;
        memory_cleanup(&mem);
        return;
    }
    memory_set_io_callbacks(&mem, io_read_callback, io_write_callback, &ctx);

    cpu816_init(&cpu, &mem);
    cpu816_reset(&cpu);

    int safety = 500000;
    int cycles = 0;
    while (safety-- > 0 && !cpu.stopped) {
        int c = cpu816_step(&cpu);
        if (c < 0) {
            printf("FAIL\n    cpu step error at %02X:%04X\n", cpu.PBR, cpu.PC);
            tests_failed++;
            sd_close(&sd);
            memory_cleanup(&mem);
            return;
        }
        cycles += c;
        via_update(&via, c);
    }
    if (!cpu.stopped) {
        printf("FAIL\n    cpu not stopped after %d cycles, PBR:PC=%02X:%04X\n",
               cycles, cpu.PBR, cpu.PC);
        tests_failed++;
        sd_close(&sd);
        memory_cleanup(&mem);
        return;
    }

    /* Vérifie que kernel_sd_read_block a copié le pattern depuis l'image
     * SD vers $01:5D40 (zone destination du test au boot kernel). */
    for (size_t i = 0; i < SD_BLOCK_SIZE; i++) {
        uint8_t actual = (uint8_t)memory_read24(&mem, 0x015D40u + (uint32_t)i);
        if (actual != pattern[i]) {
            printf("FAIL\n    mem[$01:5D40+%zu] = 0x%02X, expected 0x%02X ('%c')\n",
                   i, actual, pattern[i], pattern[i]);
            tests_failed++;
            sd_close(&sd);
            memory_cleanup(&mem);
            return;
        }
    }

    sd_close(&sd);
    memory_cleanup(&mem);
}

int main(void) {
    printf("\n");
    printf("═══════════════════════════════════════════════════════\n");
    printf("  OricOS SD bloc driver test (Sprint 2.j.1)\n");
    printf("═══════════════════════════════════════════════════════\n\n");

    RUN(test_oricos_sd_read_block_via_kernel_boot);

    printf("\n═══════════════════════════════════════════════════════\n");
    printf("  Results: %d passed, %d failed\n", tests_passed, tests_failed);
    printf("═══════════════════════════════════════════════════════\n\n");

    return tests_failed == 0 ? 0 : 1;
}
