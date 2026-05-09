/**
 * @file test_vram_device.c
 * @brief Tests Sprint VRAM-1 — vram_device (ADR-19)
 * @author bmarty <bmarty@mailo.com>
 * @date 2026-05-09
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
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

/* ─── Init / cleanup ──────────────────────────────────────────────── */

TEST(test_init_alloc_buffer) {
    vram_device_t vd;
    ASSERT_TRUE(vram_init(&vd));
    ASSERT_TRUE(vd.buffer != NULL);
    /* Buffer initial à zéro. */
    ASSERT_EQ(vd.buffer[0], 0);
    ASSERT_EQ(vd.buffer[VRAM_SIZE - 1], 0);
    ASSERT_EQ(vd.addr, 0);
    ASSERT_EQ(vd.dma_src, 0);
    ASSERT_EQ(vd.dma_dst, 0);
    ASSERT_EQ(vd.dma_len, 0);
    vram_cleanup(&vd);
    ASSERT_EQ(vd.buffer, NULL);
}

/* ─── Address register set/get round-trip ─────────────────────────── */

TEST(test_addr_register_round_trip) {
    vram_device_t vd;
    ASSERT_TRUE(vram_init(&vd));
    /* Set 24-bit ADDR = $123456. */
    vram_write(&vd, NULL, VRAM_REG_ADDR_LO,  0x56);
    vram_write(&vd, NULL, VRAM_REG_ADDR_MID, 0x34);
    vram_write(&vd, NULL, VRAM_REG_ADDR_HI,  0x12);
    /* But VRAM v1 = 16 MiB, donc HI bits 4..7 sont mis à 0 par VRAM_ADDR_MASK
     * lors de l'accès DATA suivant. ADDR n'est masqué qu'à l'usage. */
    ASSERT_EQ(vram_read(&vd, VRAM_REG_ADDR_LO),  0x56);
    ASSERT_EQ(vram_read(&vd, VRAM_REG_ADDR_MID), 0x34);
    ASSERT_EQ(vram_read(&vd, VRAM_REG_ADDR_HI),  0x12);
    vram_cleanup(&vd);
}

/* ─── Data write/read avec auto-increment ─────────────────────────── */

TEST(test_data_write_read_auto_increment) {
    vram_device_t vd;
    ASSERT_TRUE(vram_init(&vd));
    /* Set ADDR = $001000. */
    vram_write(&vd, NULL, VRAM_REG_ADDR_LO,  0x00);
    vram_write(&vd, NULL, VRAM_REG_ADDR_MID, 0x10);
    vram_write(&vd, NULL, VRAM_REG_ADDR_HI,  0x00);
    /* Écris 4 bytes consécutifs. */
    vram_write(&vd, NULL, VRAM_REG_DATA, 0xAA);
    vram_write(&vd, NULL, VRAM_REG_DATA, 0xBB);
    vram_write(&vd, NULL, VRAM_REG_DATA, 0xCC);
    vram_write(&vd, NULL, VRAM_REG_DATA, 0xDD);
    /* ADDR = $001004 maintenant. */
    ASSERT_EQ(vram_read(&vd, VRAM_REG_ADDR_LO),  0x04);
    ASSERT_EQ(vram_read(&vd, VRAM_REG_ADDR_MID), 0x10);
    ASSERT_EQ(vram_read(&vd, VRAM_REG_ADDR_HI),  0x00);
    /* Reset ADDR pour relire. */
    vram_write(&vd, NULL, VRAM_REG_ADDR_LO, 0x00);
    /* mid/hi inchangés. */
    ASSERT_EQ(vram_read(&vd, VRAM_REG_DATA), 0xAA);
    ASSERT_EQ(vram_read(&vd, VRAM_REG_DATA), 0xBB);
    ASSERT_EQ(vram_read(&vd, VRAM_REG_DATA), 0xCC);
    ASSERT_EQ(vram_read(&vd, VRAM_REG_DATA), 0xDD);
    /* ADDR = $001004 à nouveau. */
    ASSERT_EQ(vram_read(&vd, VRAM_REG_ADDR_LO),  0x04);
    vram_cleanup(&vd);
}

/* ─── Address wrap-around à 16 MiB ────────────────────────────────── */

TEST(test_addr_wraps_at_16mib) {
    vram_device_t vd;
    ASSERT_TRUE(vram_init(&vd));
    /* Set ADDR = $FFFFFE (avant-dernier byte). */
    vram_write(&vd, NULL, VRAM_REG_ADDR_LO,  0xFE);
    vram_write(&vd, NULL, VRAM_REG_ADDR_MID, 0xFF);
    vram_write(&vd, NULL, VRAM_REG_ADDR_HI,  0xFF);
    /* Écris 2 bytes. ADDR doit wrap à $000000 après. */
    vram_write(&vd, NULL, VRAM_REG_DATA, 0x42);
    vram_write(&vd, NULL, VRAM_REG_DATA, 0x43);
    /* Maintenant ADDR = $000000 (wrap). */
    ASSERT_EQ(vram_read(&vd, VRAM_REG_ADDR_LO),  0x00);
    ASSERT_EQ(vram_read(&vd, VRAM_REG_ADDR_MID), 0x00);
    ASSERT_EQ(vram_read(&vd, VRAM_REG_ADDR_HI),  0x00);
    /* Vérifie via peek que les 2 bytes ont été écrits aux fins de buffer. */
    ASSERT_EQ(vram_peek(&vd, 0xFFFFFE), 0x42);
    ASSERT_EQ(vram_peek(&vd, 0xFFFFFF), 0x43);
    vram_cleanup(&vd);
}

/* ─── DMA SDRAM → bank live (DIR=0) ───────────────────────────────── */

TEST(test_dma_sdram_to_bank) {
    vram_device_t vd;
    ASSERT_TRUE(vram_init(&vd));
    memory_t mem;
    memory_init(&mem);
    memory_alloc_bank(&mem, 128); /* bank live */

    /* Pré-charge SDRAM[$001000..$00100F] avec pattern 0x10..0x1F. */
    for (int i = 0; i < 16; i++) {
        vram_poke(&vd, 0x001000 + i, (uint8_t)(0x10 + i));
    }

    /* DMA SDRAM[$001000] → bank $80:0200, len=16. */
    vram_write(&vd, NULL, VRAM_REG_DMA_SRC_LO,  0x00);
    vram_write(&vd, NULL, VRAM_REG_DMA_SRC_MID, 0x10);
    vram_write(&vd, NULL, VRAM_REG_DMA_SRC_HI,  0x00);
    vram_write(&vd, NULL, VRAM_REG_DMA_DST_LO,  0x00);
    vram_write(&vd, NULL, VRAM_REG_DMA_DST_MID, 0x02);
    vram_write(&vd, NULL, VRAM_REG_DMA_DST_HI,  0x80);
    vram_write(&vd, NULL, VRAM_REG_DMA_LEN_LO,  0x10);
    vram_write(&vd, NULL, VRAM_REG_DMA_LEN_HI,  0x00);
    /* Trigger DMA SDRAM → bank (DIR=0, bit 1 = 0). */
    vram_write(&vd, &mem, VRAM_REG_DMA_CTRL, VRAM_DMA_CTRL_TRIGGER);

    /* Vérifie bank $80:0200..$80:020F = 0x10..0x1F. */
    for (int i = 0; i < 16; i++) {
        ASSERT_EQ((int)memory_read24(&mem, 0x800200 + i), 0x10 + i);
    }
    /* SDRAM source intacte. */
    for (int i = 0; i < 16; i++) {
        ASSERT_EQ((int)vram_peek(&vd, 0x001000 + i), 0x10 + i);
    }
    /* busy = 0 (synchrone v0.1). */
    ASSERT_EQ((vram_read(&vd, VRAM_REG_DMA_CTRL) & VRAM_DMA_CTRL_BUSY), 0);

    memory_cleanup(&mem);
    vram_cleanup(&vd);
}

/* ─── DMA bank live → SDRAM (DIR=1) ───────────────────────────────── */

TEST(test_dma_bank_to_sdram) {
    vram_device_t vd;
    ASSERT_TRUE(vram_init(&vd));
    memory_t mem;
    memory_init(&mem);
    memory_alloc_bank(&mem, 128);

    /* Pré-charge bank $80:0200..$80:020F = 0xC0..0xCF. */
    for (int i = 0; i < 16; i++) {
        memory_write24(&mem, 0x800200 + i, (uint8_t)(0xC0 + i));
    }

    /* DMA bank $80:0200 → SDRAM[$002000], len=16. */
    vram_write(&vd, NULL, VRAM_REG_DMA_SRC_LO,  0x00);
    vram_write(&vd, NULL, VRAM_REG_DMA_SRC_MID, 0x02);
    vram_write(&vd, NULL, VRAM_REG_DMA_SRC_HI,  0x80);
    vram_write(&vd, NULL, VRAM_REG_DMA_DST_LO,  0x00);
    vram_write(&vd, NULL, VRAM_REG_DMA_DST_MID, 0x20);
    vram_write(&vd, NULL, VRAM_REG_DMA_DST_HI,  0x00);
    vram_write(&vd, NULL, VRAM_REG_DMA_LEN_LO,  0x10);
    vram_write(&vd, NULL, VRAM_REG_DMA_LEN_HI,  0x00);
    /* Trigger DMA bank → SDRAM (DIR=1). */
    vram_write(&vd, &mem, VRAM_REG_DMA_CTRL,
               VRAM_DMA_CTRL_TRIGGER | VRAM_DMA_CTRL_DIR);

    /* Vérifie SDRAM[$002000..$00200F] = 0xC0..0xCF. */
    for (int i = 0; i < 16; i++) {
        ASSERT_EQ((int)vram_peek(&vd, 0x002000 + i), 0xC0 + i);
    }

    memory_cleanup(&mem);
    vram_cleanup(&vd);
}

/* ─── DMA len=0 → 65536 octets (max burst) ────────────────────────── */

TEST(test_dma_len_zero_means_64k) {
    vram_device_t vd;
    ASSERT_TRUE(vram_init(&vd));
    memory_t mem;
    memory_init(&mem);
    memory_alloc_bank(&mem, 128);
    memory_alloc_bank(&mem, 129);

    /* Remplir 64 KiB consécutifs en bank 128 (= bank $80 entier). */
    for (uint32_t i = 0; i < 65536u; i++) {
        memory_write24(&mem, 0x800000u + i, (uint8_t)(i & 0xFFu));
    }

    /* DMA bank $80:0000 → SDRAM[$004000], len=0 (= 65536). */
    vram_write(&vd, NULL, VRAM_REG_DMA_SRC_LO,  0x00);
    vram_write(&vd, NULL, VRAM_REG_DMA_SRC_MID, 0x00);
    vram_write(&vd, NULL, VRAM_REG_DMA_SRC_HI,  0x80);
    vram_write(&vd, NULL, VRAM_REG_DMA_DST_LO,  0x00);
    vram_write(&vd, NULL, VRAM_REG_DMA_DST_MID, 0x40);
    vram_write(&vd, NULL, VRAM_REG_DMA_DST_HI,  0x00);
    vram_write(&vd, NULL, VRAM_REG_DMA_LEN_LO,  0x00);
    vram_write(&vd, NULL, VRAM_REG_DMA_LEN_HI,  0x00);
    vram_write(&vd, &mem, VRAM_REG_DMA_CTRL,
               VRAM_DMA_CTRL_TRIGGER | VRAM_DMA_CTRL_DIR);

    /* Vérifie 4 points. */
    ASSERT_EQ((int)vram_peek(&vd, 0x004000),         0x00);
    ASSERT_EQ((int)vram_peek(&vd, 0x004001),         0x01);
    ASSERT_EQ((int)vram_peek(&vd, 0x004000 + 0xFF),  0xFF);
    ASSERT_EQ((int)vram_peek(&vd, 0x004000 + 65535), 0xFF);

    memory_cleanup(&mem);
    vram_cleanup(&vd);
}

/* ─── DMA registers round-trip read/write ─────────────────────────── */

TEST(test_dma_registers_round_trip) {
    vram_device_t vd;
    ASSERT_TRUE(vram_init(&vd));

    vram_write(&vd, NULL, VRAM_REG_DMA_SRC_LO,  0x12);
    vram_write(&vd, NULL, VRAM_REG_DMA_SRC_MID, 0x34);
    vram_write(&vd, NULL, VRAM_REG_DMA_SRC_HI,  0x56);
    vram_write(&vd, NULL, VRAM_REG_DMA_DST_LO,  0xAB);
    vram_write(&vd, NULL, VRAM_REG_DMA_DST_MID, 0xCD);
    vram_write(&vd, NULL, VRAM_REG_DMA_DST_HI,  0xEF);
    vram_write(&vd, NULL, VRAM_REG_DMA_LEN_LO,  0x99);
    vram_write(&vd, NULL, VRAM_REG_DMA_LEN_HI,  0x42);

    ASSERT_EQ(vram_read(&vd, VRAM_REG_DMA_SRC_LO),  0x12);
    ASSERT_EQ(vram_read(&vd, VRAM_REG_DMA_SRC_MID), 0x34);
    ASSERT_EQ(vram_read(&vd, VRAM_REG_DMA_SRC_HI),  0x56);
    ASSERT_EQ(vram_read(&vd, VRAM_REG_DMA_DST_LO),  0xAB);
    ASSERT_EQ(vram_read(&vd, VRAM_REG_DMA_DST_MID), 0xCD);
    ASSERT_EQ(vram_read(&vd, VRAM_REG_DMA_DST_HI),  0xEF);
    ASSERT_EQ(vram_read(&vd, VRAM_REG_DMA_LEN_LO),  0x99);
    ASSERT_EQ(vram_read(&vd, VRAM_REG_DMA_LEN_HI),  0x42);

    vram_cleanup(&vd);
}

/* ─── Peek/poke direct (test infrastructure) ──────────────────────── */

TEST(test_peek_poke_direct) {
    vram_device_t vd;
    ASSERT_TRUE(vram_init(&vd));
    vram_poke(&vd, 0x123456, 0x77);
    ASSERT_EQ(vram_peek(&vd, 0x123456), 0x77);
    /* Mask : 0x10123456 → 0x00123456 (16 MiB modulo). */
    vram_poke(&vd, 0x10123456, 0x88);
    ASSERT_EQ(vram_peek(&vd, 0x123456), 0x88);
    vram_cleanup(&vd);
}

int main(void) {
    printf("\n");
    printf("═══════════════════════════════════════════════════════\n");
    printf("  VRAM device tests (Sprint VRAM-1, ADR-19)\n");
    printf("═══════════════════════════════════════════════════════\n\n");

    RUN(test_init_alloc_buffer);
    RUN(test_addr_register_round_trip);
    RUN(test_data_write_read_auto_increment);
    RUN(test_addr_wraps_at_16mib);
    RUN(test_dma_sdram_to_bank);
    RUN(test_dma_bank_to_sdram);
    RUN(test_dma_len_zero_means_64k);
    RUN(test_dma_registers_round_trip);
    RUN(test_peek_poke_direct);

    printf("\n═══════════════════════════════════════════════════════\n");
    printf("  Results: %d passed, %d failed\n", tests_passed, tests_failed);
    printf("═══════════════════════════════════════════════════════\n\n");

    return tests_failed == 0 ? 0 : 1;
}
