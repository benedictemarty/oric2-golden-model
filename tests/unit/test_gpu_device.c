/**
 * @file test_gpu_device.c
 * @brief Tests Sprint GPU-1 — gpu_device (ADR-21)
 * @author bmarty <bmarty@mailo.com>
 * @date 2026-05-09
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "io/gpu_device.h"
#include "io/vram_device.h"
#include "memory/memory.h"

static int tests_passed = 0;
static int tests_failed = 0;

#define TEST(name) static void name(void)
#define RUN(name) do { \
    printf("  %-58s", #name); \
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

/* ─── Tests init / status ────────────────────────────────────────── */

TEST(test_init_state) {
    gpu_device_t gpu;
    gpu_init(&gpu);
    ASSERT_EQ(gpu.cmd_op, 0);
    ASSERT_EQ(gpu.arg1, 0);
    ASSERT_EQ(gpu.arg2, 0);
    ASSERT_EQ(gpu.arg3, 0);
    ASSERT_EQ(gpu.arg4, 0);
    ASSERT_EQ(gpu.busy, 0);
    ASSERT_EQ(gpu.err, 0);
    /* STATUS au repos : busy=0, err=0, done=1 (idle). */
    ASSERT_EQ(gpu_read(&gpu, GPU_REG_STATUS), GPU_STATUS_DONE);
    gpu_cleanup(&gpu);
}

/* ─── Round-trip args registers ──────────────────────────────────── */

TEST(test_arg_registers_round_trip) {
    gpu_device_t gpu;
    gpu_init(&gpu);

    gpu_write(&gpu, NULL, NULL, GPU_REG_CMD_OP, 0x55);
    gpu_write(&gpu, NULL, NULL, GPU_REG_ARG1_LO, 0x12);
    gpu_write(&gpu, NULL, NULL, GPU_REG_ARG1_MID, 0x34);
    gpu_write(&gpu, NULL, NULL, GPU_REG_ARG1_HI, 0x56);
    gpu_write(&gpu, NULL, NULL, GPU_REG_ARG2_LO, 0xAB);
    gpu_write(&gpu, NULL, NULL, GPU_REG_ARG2_MID, 0xCD);
    gpu_write(&gpu, NULL, NULL, GPU_REG_ARG2_HI, 0xEF);
    gpu_write(&gpu, NULL, NULL, GPU_REG_ARG3_LO, 0x01);
    gpu_write(&gpu, NULL, NULL, GPU_REG_ARG3_MID, 0x23);
    gpu_write(&gpu, NULL, NULL, GPU_REG_ARG3_HI, 0x45);
    gpu_write(&gpu, NULL, NULL, GPU_REG_ARG4_LO, 0x99);
    gpu_write(&gpu, NULL, NULL, GPU_REG_ARG4_MID, 0x88);
    gpu_write(&gpu, NULL, NULL, GPU_REG_ARG4_HI, 0x77);

    ASSERT_EQ(gpu_read(&gpu, GPU_REG_CMD_OP), 0x55);
    ASSERT_EQ(gpu_read(&gpu, GPU_REG_ARG1_LO), 0x12);
    ASSERT_EQ(gpu_read(&gpu, GPU_REG_ARG1_MID), 0x34);
    ASSERT_EQ(gpu_read(&gpu, GPU_REG_ARG1_HI), 0x56);
    ASSERT_EQ(gpu_read(&gpu, GPU_REG_ARG2_LO), 0xAB);
    ASSERT_EQ(gpu_read(&gpu, GPU_REG_ARG2_MID), 0xCD);
    ASSERT_EQ(gpu_read(&gpu, GPU_REG_ARG2_HI), 0xEF);
    ASSERT_EQ(gpu_read(&gpu, GPU_REG_ARG3_LO), 0x01);
    ASSERT_EQ(gpu_read(&gpu, GPU_REG_ARG3_MID), 0x23);
    ASSERT_EQ(gpu_read(&gpu, GPU_REG_ARG3_HI), 0x45);
    ASSERT_EQ(gpu_read(&gpu, GPU_REG_ARG4_LO), 0x99);
    ASSERT_EQ(gpu_read(&gpu, GPU_REG_ARG4_MID), 0x88);
    ASSERT_EQ(gpu_read(&gpu, GPU_REG_ARG4_HI), 0x77);
    ASSERT_EQ(gpu.arg1, 0x563412u);
    ASSERT_EQ(gpu.arg2, 0xEFCDABu);

    gpu_cleanup(&gpu);
}

/* ─── CLEAR : remplit zone SDRAM avec couleur ────────────────────── */

TEST(test_clear_fills_sdram) {
    gpu_device_t gpu;
    vram_device_t vram;
    gpu_init(&gpu);
    ASSERT_TRUE(vram_init(&vram));

    /* CLEAR(base=$001000, size=$100=256 octets, color=4=blue VGA). */
    gpu_write(&gpu, NULL, NULL, GPU_REG_ARG1_LO,  0x00);
    gpu_write(&gpu, NULL, NULL, GPU_REG_ARG1_MID, 0x10);
    gpu_write(&gpu, NULL, NULL, GPU_REG_ARG1_HI,  0x00);
    gpu_write(&gpu, NULL, NULL, GPU_REG_ARG2_LO,  0x00);
    gpu_write(&gpu, NULL, NULL, GPU_REG_ARG2_MID, 0x01);
    gpu_write(&gpu, NULL, NULL, GPU_REG_ARG2_HI,  0x00);
    gpu_write(&gpu, NULL, NULL, GPU_REG_ARG3_LO,  0x04);
    gpu_write(&gpu, NULL, NULL, GPU_REG_CMD_OP,   GPU_OP_CLEAR);
    gpu_write(&gpu, &vram, NULL, GPU_REG_TRIGGER, 0x01);

    /* Pattern attendu : color × 0x11 = 0x44 (2 pixels blue). */
    ASSERT_EQ((int)vram_peek(&vram, 0x001000), 0x44);
    ASSERT_EQ((int)vram_peek(&vram, 0x001050), 0x44);
    ASSERT_EQ((int)vram_peek(&vram, 0x0010FF), 0x44);
    /* Hors range : reste à 0. */
    ASSERT_EQ((int)vram_peek(&vram, 0x000FFF), 0x00);
    ASSERT_EQ((int)vram_peek(&vram, 0x001100), 0x00);
    /* Status : err=false, done=1. */
    ASSERT_EQ(gpu.err, 0);

    vram_cleanup(&vram);
    gpu_cleanup(&gpu);
}

/* ─── FILL_RECT : dessine un rectangle 4bpp ──────────────────────── */

TEST(test_fill_rect_aligned) {
    gpu_device_t gpu;
    vram_device_t vram;
    gpu_init(&gpu);
    ASSERT_TRUE(vram_init(&vram));

    /* FILL_RECT(base=0, x=0, y=0, w=4, h=2, color=7=lightgray).
     * BPL=GPU_XVGA_BPL=512.
     * Pattern attendu : 4 pixels = 2 octets, sur 2 lignes :
     *   ligne 0 : bytes 0..1 = 0x77, 0x77
     *   ligne 1 : bytes 512..513 = 0x77, 0x77 */
    gpu_write(&gpu, NULL, NULL, GPU_REG_ARG1_LO,  0x00);
    gpu_write(&gpu, NULL, NULL, GPU_REG_ARG1_MID, 0x00);
    gpu_write(&gpu, NULL, NULL, GPU_REG_ARG1_HI,  0x00);
    gpu_write(&gpu, NULL, NULL, GPU_REG_ARG2_LO,  0x00);  /* x=0 */
    gpu_write(&gpu, NULL, NULL, GPU_REG_ARG2_MID, 0x00);  /* y=0 */
    gpu_write(&gpu, NULL, NULL, GPU_REG_ARG3_LO,  0x04);  /* w=4 */
    gpu_write(&gpu, NULL, NULL, GPU_REG_ARG3_MID, 0x02);  /* h=2 */
    gpu_write(&gpu, NULL, NULL, GPU_REG_ARG4_LO,  0x07);  /* color=7 */
    gpu_write(&gpu, NULL, NULL, GPU_REG_CMD_OP,   GPU_OP_FILL_RECT);
    gpu_write(&gpu, &vram, NULL, GPU_REG_TRIGGER, 0x01);

    ASSERT_EQ((int)vram_peek(&vram, 0),       0x77);
    ASSERT_EQ((int)vram_peek(&vram, 1),       0x77);
    ASSERT_EQ((int)vram_peek(&vram, 2),       0x00);  /* hors rect */
    ASSERT_EQ((int)vram_peek(&vram, 512),     0x77);
    ASSERT_EQ((int)vram_peek(&vram, 513),     0x77);
    ASSERT_EQ((int)vram_peek(&vram, 514),     0x00);
    ASSERT_EQ((int)vram_peek(&vram, 1024),    0x00);  /* ligne 2 hors h=2 */

    vram_cleanup(&vram);
    gpu_cleanup(&gpu);
}

/* ─── FILL_RECT : pixels intra-octet (gauche/droite mask) ─────────── */

TEST(test_fill_rect_pixel_left_right) {
    gpu_device_t gpu;
    vram_device_t vram;
    gpu_init(&gpu);
    ASSERT_TRUE(vram_init(&vram));

    /* Pré-remplir SDRAM[0..2] avec pattern $AB $CD $EF pour vérifier
     * que FILL_RECT ne touche que les pixels demandés (mask correct). */
    vram_poke(&vram, 0, 0xAB);
    vram_poke(&vram, 1, 0xCD);
    vram_poke(&vram, 2, 0xEF);

    /* FILL_RECT(x=1, y=0, w=2, h=1, color=$3=cyan).
     * x=1 → pixel droit du byte 0 (mask 0x0F) → 0xA3.
     * x=2 → pixel gauche du byte 1 (mask 0xF0) → 0x3D.
     * Total 2 pixels écrits, byte 2 inchangé. */
    gpu_write(&gpu, NULL, NULL, GPU_REG_ARG1_LO,  0x00);
    gpu_write(&gpu, NULL, NULL, GPU_REG_ARG1_MID, 0x00);
    gpu_write(&gpu, NULL, NULL, GPU_REG_ARG1_HI,  0x00);
    gpu_write(&gpu, NULL, NULL, GPU_REG_ARG2_LO,  0x01);  /* x=1 */
    gpu_write(&gpu, NULL, NULL, GPU_REG_ARG2_MID, 0x00);  /* y=0 */
    gpu_write(&gpu, NULL, NULL, GPU_REG_ARG3_LO,  0x02);  /* w=2 */
    gpu_write(&gpu, NULL, NULL, GPU_REG_ARG3_MID, 0x01);  /* h=1 */
    gpu_write(&gpu, NULL, NULL, GPU_REG_ARG4_LO,  0x03);  /* color=3 */
    gpu_write(&gpu, NULL, NULL, GPU_REG_CMD_OP,   GPU_OP_FILL_RECT);
    gpu_write(&gpu, &vram, NULL, GPU_REG_TRIGGER, 0x01);

    ASSERT_EQ((int)vram_peek(&vram, 0), 0xA3);
    ASSERT_EQ((int)vram_peek(&vram, 1), 0x3D);
    ASSERT_EQ((int)vram_peek(&vram, 2), 0xEF);

    vram_cleanup(&vram);
    gpu_cleanup(&gpu);
}

/* ─── Opcode inconnu → err=true ──────────────────────────────────── */

TEST(test_unknown_opcode_sets_err) {
    gpu_device_t gpu;
    vram_device_t vram;
    gpu_init(&gpu);
    ASSERT_TRUE(vram_init(&vram));

    gpu_write(&gpu, NULL, NULL, GPU_REG_CMD_OP, 0xFF);  /* opcode invalide */
    gpu_write(&gpu, &vram, NULL, GPU_REG_TRIGGER, 0x01);

    ASSERT_EQ(gpu.err, 1);
    /* STATUS bit 6 (ERR) doit être set. */
    uint8_t s = gpu_read(&gpu, GPU_REG_STATUS);
    ASSERT_TRUE((s & GPU_STATUS_ERR) != 0);
    ASSERT_TRUE((s & GPU_STATUS_DONE) == 0);

    vram_cleanup(&vram);
    gpu_cleanup(&gpu);
}

/* ─── CLEAR taille XVGA framebuffer complet ──────────────────────── */

TEST(test_clear_xvga_full_framebuffer) {
    gpu_device_t gpu;
    vram_device_t vram;
    gpu_init(&gpu);
    ASSERT_TRUE(vram_init(&vram));

    /* CLEAR(base=0, size=384KiB=393216, color=15=white).
     * 393216 = $060000. */
    gpu_write(&gpu, NULL, NULL, GPU_REG_ARG1_LO,  0x00);
    gpu_write(&gpu, NULL, NULL, GPU_REG_ARG1_MID, 0x00);
    gpu_write(&gpu, NULL, NULL, GPU_REG_ARG1_HI,  0x00);
    gpu_write(&gpu, NULL, NULL, GPU_REG_ARG2_LO,  0x00);
    gpu_write(&gpu, NULL, NULL, GPU_REG_ARG2_MID, 0x00);
    gpu_write(&gpu, NULL, NULL, GPU_REG_ARG2_HI,  0x06);  /* size = $060000 */
    gpu_write(&gpu, NULL, NULL, GPU_REG_ARG3_LO,  0x0F);  /* color=15 */
    gpu_write(&gpu, NULL, NULL, GPU_REG_CMD_OP,   GPU_OP_CLEAR);
    gpu_write(&gpu, &vram, NULL, GPU_REG_TRIGGER, 0x01);

    /* 4 coins du framebuffer XVGA (1024×768×4bpp, 512 oct/line, 384 KiB). */
    ASSERT_EQ((int)vram_peek(&vram, 0),         0xFF);  /* (0, 0) top-left */
    ASSERT_EQ((int)vram_peek(&vram, 511),       0xFF);  /* (1022..1023, 0) */
    ASSERT_EQ((int)vram_peek(&vram, 393215),    0xFF);  /* (1022..1023, 767) */
    /* Octet juste après : pas touché. */
    ASSERT_EQ((int)vram_peek(&vram, 393216),    0x00);

    vram_cleanup(&vram);
    gpu_cleanup(&gpu);
}

int main(void) {
    printf("\n");
    printf("═══════════════════════════════════════════════════════\n");
    printf("  GPU device tests (Sprint GPU-1, ADR-21)\n");
    printf("═══════════════════════════════════════════════════════\n\n");

    RUN(test_init_state);
    RUN(test_arg_registers_round_trip);
    RUN(test_clear_fills_sdram);
    RUN(test_fill_rect_aligned);
    RUN(test_fill_rect_pixel_left_right);
    RUN(test_unknown_opcode_sets_err);
    RUN(test_clear_xvga_full_framebuffer);

    printf("\n═══════════════════════════════════════════════════════\n");
    printf("  Results: %d passed, %d failed\n", tests_passed, tests_failed);
    printf("═══════════════════════════════════════════════════════\n\n");

    return tests_failed == 0 ? 0 : 1;
}
