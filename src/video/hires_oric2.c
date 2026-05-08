/**
 * @file hires_oric2.c
 * @brief Mode HIRES Oric 2 — 240×200×3bpp (ADR-12)
 * @author bmarty <bmarty@mailo.com>
 * @date 2026-05-09
 *
 * Cf. hires_oric2.h pour la spec format pixel.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "video/hires_oric2.h"

/* Palette 8 couleurs RGB Oric 1 (ARGB 0x00RRGGBB). */
const uint32_t hires_oric2_palette[8] = {
    0x000000u,  /* 0 black   */
    0xFF0000u,  /* 1 red     */
    0x00FF00u,  /* 2 green   */
    0xFFFF00u,  /* 3 yellow  */
    0x0000FFu,  /* 4 blue    */
    0xFF00FFu,  /* 5 magenta */
    0x00FFFFu,  /* 6 cyan    */
    0xFFFFFFu,  /* 7 white   */
};

/* Calcule l'offset du triple d'octets contenant le pixel (x, y). */
static uint32_t triple_offset(int x, int y) {
    return (uint32_t)(y * HIRES2_BYTES_PER_LINE + (x / 8) * 3);
}

/* Décalage du pixel i (0..7) dans un triple 24-bit big-endian.
 * Pixel 0 → bits [23:21] (shift = 21). Pixel 7 → bits [2:0] (shift = 0). */
static int pixel_shift(int x) {
    return 21 - (x & 7) * 3;
}

uint8_t hires_oric2_get_pixel(memory_t* mem, uint8_t bank_base, int x, int y) {
    if (x < 0 || x >= HIRES2_W || y < 0 || y >= HIRES2_H) return 0;
    uint32_t base = (uint32_t)bank_base * 0x10000u;
    uint32_t off = triple_offset(x, y);
    uint8_t b0 = memory_read24(mem, base + off);
    uint8_t b1 = memory_read24(mem, base + off + 1u);
    uint8_t b2 = memory_read24(mem, base + off + 2u);
    uint32_t triple = ((uint32_t)b0 << 16) | ((uint32_t)b1 << 8) | b2;
    int shift = pixel_shift(x);
    return (uint8_t)((triple >> shift) & 0x7u);
}

void hires_oric2_set_pixel(memory_t* mem, uint8_t bank_base, int x, int y, uint8_t color) {
    if (x < 0 || x >= HIRES2_W || y < 0 || y >= HIRES2_H) return;
    color = (uint8_t)(color & 0x7u);
    uint32_t base = (uint32_t)bank_base * 0x10000u;
    uint32_t off = triple_offset(x, y);
    uint8_t b0 = memory_read24(mem, base + off);
    uint8_t b1 = memory_read24(mem, base + off + 1u);
    uint8_t b2 = memory_read24(mem, base + off + 2u);
    uint32_t triple = ((uint32_t)b0 << 16) | ((uint32_t)b1 << 8) | b2;
    int shift = pixel_shift(x);
    uint32_t mask = (uint32_t)0x7u << shift;
    triple = (triple & ~mask) | ((uint32_t)color << shift);
    memory_write24(mem, base + off,        (uint8_t)((triple >> 16) & 0xFFu));
    memory_write24(mem, base + off + 1u,   (uint8_t)((triple >> 8)  & 0xFFu));
    memory_write24(mem, base + off + 2u,   (uint8_t)( triple        & 0xFFu));
}

void hires_oric2_clear(memory_t* mem, uint8_t bank_base, uint8_t color) {
    color = (uint8_t)(color & 0x7u);
    /* Pattern 24-bit : 8 pixels même couleur = color répété 8 fois sur 3 bits.
     * Soit triple = c << 21 | c << 18 | ... | c << 0 = c * 0x249249. */
    uint32_t triple = (uint32_t)color * 0x249249u;
    uint8_t pb0 = (uint8_t)((triple >> 16) & 0xFFu);
    uint8_t pb1 = (uint8_t)((triple >> 8)  & 0xFFu);
    uint8_t pb2 = (uint8_t)( triple        & 0xFFu);
    uint32_t base = (uint32_t)bank_base * 0x10000u;
    for (uint32_t off = 0; off < (uint32_t)HIRES2_FB_SIZE; off += 3u) {
        memory_write24(mem, base + off,      pb0);
        memory_write24(mem, base + off + 1u, pb1);
        memory_write24(mem, base + off + 2u, pb2);
    }
}

void hires_oric2_render_argb(memory_t* mem, uint8_t bank_base, uint32_t* fb_argb) {
    if (!fb_argb) return;
    uint32_t base = (uint32_t)bank_base * 0x10000u;
    for (int y = 0; y < HIRES2_H; y++) {
        uint32_t line_off = (uint32_t)y * HIRES2_BYTES_PER_LINE;
        uint32_t* row = fb_argb + (size_t)y * HIRES2_W;
        for (int gx = 0; gx < HIRES2_W / 8; gx++) {
            uint32_t off = line_off + (uint32_t)gx * 3u;
            uint8_t b0 = memory_read24(mem, base + off);
            uint8_t b1 = memory_read24(mem, base + off + 1u);
            uint8_t b2 = memory_read24(mem, base + off + 2u);
            uint32_t triple = ((uint32_t)b0 << 16) | ((uint32_t)b1 << 8) | b2;
            for (int i = 0; i < 8; i++) {
                int shift = 21 - i * 3;
                uint8_t c = (uint8_t)((triple >> shift) & 0x7u);
                row[gx * 8 + i] = hires_oric2_palette[c];
            }
        }
    }
}

int hires_oric2_export_ppm(memory_t* mem, uint8_t bank_base, const char* path) {
    uint32_t* fb = (uint32_t*)malloc((size_t)HIRES2_W * HIRES2_H * sizeof(uint32_t));
    if (!fb) return -1;
    hires_oric2_render_argb(mem, bank_base, fb);
    FILE* f = fopen(path, "wb");
    if (!f) { free(fb); return -1; }
    fprintf(f, "P6\n%d %d\n255\n", HIRES2_W, HIRES2_H);
    for (int i = 0; i < HIRES2_W * HIRES2_H; i++) {
        uint32_t p = fb[i];
        uint8_t rgb[3] = {
            (uint8_t)((p >> 16) & 0xFFu),
            (uint8_t)((p >> 8)  & 0xFFu),
            (uint8_t)( p        & 0xFFu),
        };
        if (fwrite(rgb, 1, 3, f) != 3) { fclose(f); free(fb); return -1; }
    }
    fclose(f);
    free(fb);
    return 0;
}
