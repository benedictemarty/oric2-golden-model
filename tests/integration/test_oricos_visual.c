/**
 * @file test_oricos_visual.c
 * @brief Test visuel OricOS — pixel-perfect diff vs golden frame
 * @author bmarty <bmarty@mailo.com>
 * @date 2026-05-08
 *
 * PH-CI-visual : prévient les bugs render qui passent à travers les
 * tests basés sur dump mémoire (cf. bug H4 fonte 2026-05-08, qui avait
 * passé 494 tests sans détection — la fonte char manquait en RAM mais
 * les bytes ASCII en VRAM étaient corrects, donc tous les ASSERT
 * passaient).
 *
 * Approche : boot OricOS jusqu'à STP, render le framebuffer, comparer
 * pixel par pixel à `tests/golden/oricos_boot.ppm`. Diff au premier
 * offset divergent.
 *
 * Test gated : skip si kernel.bin OU golden file absent.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "cpu/cpu65c816.h"
#include "memory/memory.h"
#include "io/via6522.h"
#include "video/video.h"

#define ORICOS_KERNEL_PATH  "../OricOS/build/kernel.bin"
#define GOLDEN_PATH         "tests/golden/oricos_boot.ppm"
#define ORICOS_LOAD_BANK    1
#define ORICOS_LOAD_OFFSET  0x0200u

#define FB_SIZE  (ORIC_SCREEN_W * ORIC_SCREEN_H * 3)

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

/* ─── Helpers (dupliqués de test_oricos_boot.c) ────────────────────── */

typedef struct {
    cpu65c816_t* cpu;
    via6522_t*   via;
} sched_ctx_t;

static void via_irq_callback(bool state, void* userdata) {
    sched_ctx_t* ctx = (sched_ctx_t*)userdata;
    if (state) cpu816_irq_set(ctx->cpu, IRQF_VIA);
    else       cpu816_irq_clear(ctx->cpu, IRQF_VIA);
}

static uint8_t io_read_callback(uint16_t addr, void* userdata) {
    sched_ctx_t* ctx = (sched_ctx_t*)userdata;
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
    /* RESET stub bank 0 $0100 : CLC ; XCE ; JML $010200 */
    const uint8_t reset_stub[] = { 0x18, 0xFB, 0x5C, 0x00, 0x02, 0x01 };
    for (size_t i = 0; i < sizeof(reset_stub); i++) {
        memory_write24(mem, 0x000100u + i, reset_stub[i]);
    }
    /* IRQ trampoline $0140 → $015600 */
    const uint8_t irq_t[] = { 0x5C, 0x00, 0x56, 0x01 };
    for (size_t i = 0; i < sizeof(irq_t); i++) memory_write24(mem, 0x000140u + i, irq_t[i]);
    /* NMI trampoline $0130 → $015500 */
    const uint8_t nmi_t[] = { 0x5C, 0x00, 0x55, 0x01 };
    for (size_t i = 0; i < sizeof(nmi_t); i++) memory_write24(mem, 0x000130u + i, nmi_t[i]);
    /* COP trampoline $0150 → $015700 */
    const uint8_t cop_t[] = { 0x5C, 0x00, 0x57, 0x01 };
    for (size_t i = 0; i < sizeof(cop_t); i++) memory_write24(mem, 0x000150u + i, cop_t[i]);
    /* Vecteurs ROM */
    mem->rom[0x3FFC] = 0x00; mem->rom[0x3FFD] = 0x01;
    mem->rom[0x3FFE] = 0x40; mem->rom[0x3FFF] = 0x01;
    mem->rom[0x3FEE] = 0x40; mem->rom[0x3FEF] = 0x01;
    mem->rom[0x3FEA] = 0x30; mem->rom[0x3FEB] = 0x01;
    mem->rom[0x3FFA] = 0x30; mem->rom[0x3FFB] = 0x01;
    mem->rom[0x3FE4] = 0x50; mem->rom[0x3FE5] = 0x01;
    mem->rom[0x3FF4] = 0x50; mem->rom[0x3FF5] = 0x01;
}

/* Charge un PPM P6 dans buffer. Retourne 0 si OK, -1 si absent, -2 si format. */
static int load_ppm_p6(const char* path, uint8_t* buf, size_t buf_size,
                       int* w_out, int* h_out) {
    FILE* fp = fopen(path, "rb");
    if (!fp) return -1;
    char magic[4];
    int w, h, maxval;
    if (fscanf(fp, "%3s %d %d %d", magic, &w, &h, &maxval) != 4
        || strcmp(magic, "P6") != 0 || maxval != 255) {
        fclose(fp); return -2;
    }
    fgetc(fp); /* consume single whitespace après maxval */
    size_t expected = (size_t)w * h * 3;
    if (expected > buf_size) { fclose(fp); return -3; }
    if (fread(buf, 1, expected, fp) != expected) { fclose(fp); return -4; }
    fclose(fp);
    if (w_out) *w_out = w;
    if (h_out) *h_out = h;
    return 0;
}

/* ─── Test ─────────────────────────────────────────────────────────── */

TEST(test_oricos_visual_matches_golden) {
    cpu65c816_t cpu;
    memory_t mem;
    via6522_t via;
    sched_ctx_t ctx = { &cpu, &via };

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
    memory_set_io_callbacks(&mem, io_read_callback, io_write_callback, &ctx);
    cpu816_init(&cpu, &mem);
    cpu816_reset(&cpu);

    /* Run kernel jusqu'à STP (10 ticks scheduler). */
    int safety = 500000;
    while (safety-- > 0 && !cpu.stopped) {
        int c = cpu816_step(&cpu);
        via_update(&via, c);
    }
    ASSERT_TRUE(cpu.stopped);

    /* Render frame. video_render_frame lit depuis ram bank 0. */
    video_t vid;
    video_init(&vid);
    video_render_frame(&vid, mem.ram);

    /* Load golden PPM */
    uint8_t* golden = (uint8_t*)malloc(FB_SIZE);
    if (!golden) {
        printf("FAIL (malloc)\n"); tests_failed++;
        memory_cleanup(&mem); return;
    }
    int gw, gh;
    int load_rc = load_ppm_p6(GOLDEN_PATH, golden, FB_SIZE, &gw, &gh);
    if (load_rc == -1) {
        printf("SKIP (golden absent : %s)            ", GOLDEN_PATH);
        free(golden); memory_cleanup(&mem); return;
    }
    if (load_rc < 0) {
        printf("FAIL\n    golden load error %d\n", load_rc);
        tests_failed++; free(golden); memory_cleanup(&mem); return;
    }
    if (gw != ORIC_SCREEN_W || gh != ORIC_SCREEN_H) {
        printf("FAIL\n    golden dim %dx%d, expected %dx%d\n",
               gw, gh, ORIC_SCREEN_W, ORIC_SCREEN_H);
        tests_failed++; free(golden); memory_cleanup(&mem); return;
    }

    /* Compare framebuffer pixel par pixel. */
    int first_diff = -1;
    for (size_t i = 0; i < FB_SIZE; i++) {
        if (vid.framebuffer[i] != golden[i]) {
            first_diff = (int)i;
            break;
        }
    }

    if (first_diff >= 0) {
        int byte = first_diff;
        int pixel = byte / 3;
        int channel = byte % 3;
        int x = pixel % ORIC_SCREEN_W;
        int y = pixel / ORIC_SCREEN_W;
        printf("FAIL\n    visual diff at byte %d (pixel x=%d,y=%d, channel=%d) "
               "expected %02X got %02X\n",
               byte, x, y, channel, golden[byte], vid.framebuffer[byte]);
        tests_failed++;
    }

    free(golden);
    memory_cleanup(&mem);
}

int main(void) {
    printf("\n");
    printf("═══════════════════════════════════════════════════════\n");
    printf("  OricOS visual golden frame test (PH-CI-visual)\n");
    printf("═══════════════════════════════════════════════════════\n\n");

    RUN(test_oricos_visual_matches_golden);

    printf("\n═══════════════════════════════════════════════════════\n");
    printf("  Results: %d passed, %d failed\n", tests_passed, tests_failed);
    printf("═══════════════════════════════════════════════════════\n\n");

    return tests_failed == 0 ? 0 : 1;
}
