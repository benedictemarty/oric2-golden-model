/**
 * @file test_compositor_hires_oric2.c
 * @brief Intégration Sprint 3.a v0.2 : compositor + framebuffer Oric 2
 * @author bmarty <bmarty@mailo.com>
 * @date 2026-05-09
 *
 * Valide que le framebuffer HIRES Oric 2 (bank 128, 240×200×3bpp,
 * ADR-12) peut être rendu en ARGB et utilisé comme `host` du compositor
 * matériel (ADR-02). Pipeline complet :
 *   bank 128 mémoire → hires_oric2_render_argb → compositor.host.pixels
 *                    → compositor_compose → output ARGB.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "video/hires_oric2.h"
#include "video/compositor.h"
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
    if ((a) != (b)) { \
        printf("FAIL\n    %s:%d: expected %lu (0x%lX), got %lu (0x%lX)\n", \
               __FILE__, __LINE__, (unsigned long)(b), (unsigned long)(b), \
               (unsigned long)(a), (unsigned long)(a)); \
        tests_failed++; return; \
    } \
} while(0)

#define ASSERT_TRUE(x) do { \
    if (!(x)) { printf("FAIL\n    %s:%d: expected true\n", __FILE__, __LINE__); tests_failed++; return; } \
} while(0)

/* Couleurs palette Oric (ARGB attendus). */
#define RGB_BLACK   0x000000u
#define RGB_RED     0xFF0000u
#define RGB_CYAN    0x00FFFFu
#define RGB_MAGENTA 0xFF00FFu
#define RGB_WHITE   0xFFFFFFu

/* ─── Test 1 : host = framebuffer Oric 2 (sans guest) ──────────────── */

TEST(test_oric2_renders_as_compositor_host) {
    memory_t mem;
    memory_init(&mem);
    memory_alloc_bank(&mem, HIRES2_BANK_DEFAULT);

    /* Fond noir + carré rouge 100×80 au centre. */
    hires_oric2_clear(&mem, HIRES2_BANK_DEFAULT, 0);  /* black */
    for (int y = 60; y < 140; y++) {
        for (int x = 70; x < 170; x++) {
            hires_oric2_set_pixel(&mem, HIRES2_BANK_DEFAULT, x, y, 1); /* red */
        }
    }

    compositor_t c;
    ASSERT_TRUE(compositor_init(&c, HIRES2_W, HIRES2_H, 240, 200));
    c.guest_visible = false;  /* pas d'overlay guest */

    /* Pipeline : framebuffer Oric 2 mémoire → host pixels du compositor. */
    hires_oric2_render_argb(&mem, HIRES2_BANK_DEFAULT, c.host.pixels);

    /* Output framebuffer 240×200. */
    compositor_fb_t out;
    out.width  = HIRES2_W;
    out.height = HIRES2_H;
    out.pixels = (uint32_t*)calloc((size_t)HIRES2_W * HIRES2_H, sizeof(uint32_t));
    ASSERT_TRUE(out.pixels != NULL);

    compositor_compose(&c, &out);

    /* Vérifie : coins noirs, centre rouge. */
    ASSERT_EQ(out.pixels[0],                              RGB_BLACK);
    ASSERT_EQ(out.pixels[HIRES2_W - 1],                   RGB_BLACK);
    ASSERT_EQ(out.pixels[(HIRES2_H - 1) * HIRES2_W],      RGB_BLACK);
    /* Centre du carré rouge : (120, 100). */
    ASSERT_EQ(out.pixels[100 * HIRES2_W + 120],           RGB_RED);
    /* Bord rouge intérieur : (70, 60). */
    ASSERT_EQ(out.pixels[60 * HIRES2_W + 70],             RGB_RED);
    /* Juste hors carré : (69, 60) noir. */
    ASSERT_EQ(out.pixels[60 * HIRES2_W + 69],             RGB_BLACK);

    free(out.pixels);
    compositor_cleanup(&c);
    memory_cleanup(&mem);
}

/* ─── Test 2 : host Oric 2 + overlay guest ─────────────────────────── */

TEST(test_oric2_host_with_guest_overlay) {
    memory_t mem;
    memory_init(&mem);
    memory_alloc_bank(&mem, HIRES2_BANK_DEFAULT);

    /* Host : fond cyan uniforme. */
    hires_oric2_clear(&mem, HIRES2_BANK_DEFAULT, 6);  /* cyan */

    compositor_t c;
    ASSERT_TRUE(compositor_init(&c, HIRES2_W, HIRES2_H, 80, 50));

    /* Guest : 80×50 rempli magenta. */
    for (size_t i = 0; i < 80 * 50; i++) c.guest.pixels[i] = RGB_MAGENTA;
    c.guest_x = 50;
    c.guest_y = 30;
    c.guest_visible = true;

    /* Pipeline. */
    hires_oric2_render_argb(&mem, HIRES2_BANK_DEFAULT, c.host.pixels);

    compositor_fb_t out;
    out.width  = HIRES2_W;
    out.height = HIRES2_H;
    out.pixels = (uint32_t*)calloc((size_t)HIRES2_W * HIRES2_H, sizeof(uint32_t));
    ASSERT_TRUE(out.pixels != NULL);

    compositor_compose(&c, &out);

    /* Hors fenêtre guest : cyan. */
    ASSERT_EQ(out.pixels[0],                              RGB_CYAN);
    ASSERT_EQ(out.pixels[HIRES2_W - 1],                   RGB_CYAN);
    ASSERT_EQ(out.pixels[10 * HIRES2_W + 10],             RGB_CYAN);
    /* Dans la fenêtre guest (50..130, 30..80) : magenta. */
    ASSERT_EQ(out.pixels[40 * HIRES2_W + 60],             RGB_MAGENTA);
    ASSERT_EQ(out.pixels[79 * HIRES2_W + 129],            RGB_MAGENTA);
    /* Juste hors guest droite (130, 40) : cyan. */
    ASSERT_EQ(out.pixels[40 * HIRES2_W + 130],            RGB_CYAN);
    /* Juste hors guest bas (60, 80) : cyan. */
    ASSERT_EQ(out.pixels[80 * HIRES2_W + 60],             RGB_CYAN);

    free(out.pixels);
    compositor_cleanup(&c);
    memory_cleanup(&mem);
}

/* ─── Test 3 : guest_visible=false masque la fenêtre guest ─────────── */

TEST(test_oric2_host_guest_invisible) {
    memory_t mem;
    memory_init(&mem);
    memory_alloc_bank(&mem, HIRES2_BANK_DEFAULT);

    /* Host : blanc. */
    hires_oric2_clear(&mem, HIRES2_BANK_DEFAULT, 7);

    compositor_t c;
    ASSERT_TRUE(compositor_init(&c, HIRES2_W, HIRES2_H, 80, 50));
    /* Guest rempli magenta, mais invisible. */
    for (size_t i = 0; i < 80 * 50; i++) c.guest.pixels[i] = RGB_MAGENTA;
    c.guest_x = 50;
    c.guest_y = 30;
    c.guest_visible = false;

    hires_oric2_render_argb(&mem, HIRES2_BANK_DEFAULT, c.host.pixels);

    compositor_fb_t out;
    out.width  = HIRES2_W;
    out.height = HIRES2_H;
    out.pixels = (uint32_t*)calloc((size_t)HIRES2_W * HIRES2_H, sizeof(uint32_t));
    ASSERT_TRUE(out.pixels != NULL);

    compositor_compose(&c, &out);

    /* Pas d'overlay : tout est blanc. */
    ASSERT_EQ(out.pixels[0],                              RGB_WHITE);
    ASSERT_EQ(out.pixels[40 * HIRES2_W + 60],             RGB_WHITE);
    ASSERT_EQ(out.pixels[HIRES2_W * HIRES2_H - 1],        RGB_WHITE);

    free(out.pixels);
    compositor_cleanup(&c);
    memory_cleanup(&mem);
}

int main(void) {
    printf("\n");
    printf("═══════════════════════════════════════════════════════\n");
    printf("  Compositor + HIRES Oric 2 integration (Sprint 3.a v0.2)\n");
    printf("═══════════════════════════════════════════════════════\n\n");

    RUN(test_oric2_renders_as_compositor_host);
    RUN(test_oric2_host_with_guest_overlay);
    RUN(test_oric2_host_guest_invisible);

    printf("\n═══════════════════════════════════════════════════════\n");
    printf("  Results: %d passed, %d failed\n", tests_passed, tests_failed);
    printf("═══════════════════════════════════════════════════════\n\n");

    return tests_failed == 0 ? 0 : 1;
}
