/**
 * @file test_compositor.c
 * @brief Tests compositor matériel modèle (B4)
 * @author bmarty <bmarty@mailo.com>
 * @date 2026-05-08
 *
 * Vérifie le mix host + guest, le clipping, la visibilité, l'init/cleanup.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "video/compositor.h"

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
               __FILE__, __LINE__, (unsigned long)(b), (unsigned long)(b), (unsigned long)(a), (unsigned long)(a)); \
        tests_failed++; return; \
    } \
} while(0)

#define ASSERT_TRUE(x) do { \
    if (!(x)) { printf("FAIL\n    %s:%d: expected true\n", __FILE__, __LINE__); tests_failed++; return; } \
} while(0)

#define ASSERT_FALSE(x) do { \
    if ((x)) { printf("FAIL\n    %s:%d: expected false\n", __FILE__, __LINE__); tests_failed++; return; } \
} while(0)

/* Helpers : remplit un FB avec une couleur unie. */
static void fb_fill(compositor_fb_t* fb, uint32_t color) {
    for (size_t i = 0; i < (size_t)fb->width * fb->height; i++) fb->pixels[i] = color;
}

static uint32_t fb_pixel(const compositor_fb_t* fb, int x, int y) {
    return fb->pixels[(size_t)y * fb->width + x];
}

/* ─── Init / cleanup ────────────────────────────────────────────────── */

TEST(test_init_alloc_dimensions) {
    compositor_t c;
    ASSERT_TRUE(compositor_init(&c, 320, 240, 240, 200));
    ASSERT_EQ((unsigned)c.host.width, 320);
    ASSERT_EQ((unsigned)c.host.height, 240);
    ASSERT_EQ((unsigned)c.guest.width, 240);
    ASSERT_EQ((unsigned)c.guest.height, 200);
    ASSERT_TRUE(c.host.pixels != NULL);
    ASSERT_TRUE(c.guest.pixels != NULL);
    ASSERT_TRUE(c.guest_visible);
    /* Pixels init à 0 (calloc) */
    ASSERT_EQ((unsigned long)fb_pixel(&c.host, 0, 0), 0u);
    compositor_cleanup(&c);
}

TEST(test_cleanup_nullifies_pointers) {
    compositor_t c;
    compositor_init(&c, 100, 80, 50, 40);
    compositor_cleanup(&c);
    ASSERT_TRUE(c.host.pixels == NULL);
    ASSERT_TRUE(c.guest.pixels == NULL);
}

/* ─── Compose ───────────────────────────────────────────────────────── */

TEST(test_compose_guest_invisible_returns_host) {
    compositor_t c; compositor_init(&c, 320, 240, 240, 200);
    fb_fill(&c.host, 0x112233);
    fb_fill(&c.guest, 0xFFFFFF);
    c.guest_visible = false;
    c.guest_x = 0; c.guest_y = 0;

    compositor_fb_t out = { (uint32_t*)calloc(320*240, sizeof(uint32_t)), 320, 240 };
    compositor_compose(&c, &out);

    /* Vérifie un pixel quelconque. */
    ASSERT_EQ((unsigned long)fb_pixel(&out, 100, 100), 0x112233u);
    ASSERT_EQ((unsigned long)fb_pixel(&out, 0, 0), 0x112233u);
    free(out.pixels);
    compositor_cleanup(&c);
}

TEST(test_compose_guest_at_origin) {
    compositor_t c; compositor_init(&c, 320, 240, 240, 200);
    fb_fill(&c.host, 0x000033);   /* host = bleu foncé */
    fb_fill(&c.guest, 0xCC0000);  /* guest = rouge */
    c.guest_x = 0; c.guest_y = 0;

    compositor_fb_t out = { (uint32_t*)calloc(320*240, sizeof(uint32_t)), 320, 240 };
    compositor_compose(&c, &out);

    /* Coin haut-gauche : guest. */
    ASSERT_EQ((unsigned long)fb_pixel(&out, 0, 0), 0xCC0000u);
    /* Pixel à (239,199) : guest. */
    ASSERT_EQ((unsigned long)fb_pixel(&out, 239, 199), 0xCC0000u);
    /* Pixel à (240,0) : host (juste à droite de la guest window). */
    ASSERT_EQ((unsigned long)fb_pixel(&out, 240, 0), 0x000033u);
    /* Pixel à (0,200) : host (juste sous la guest window). */
    ASSERT_EQ((unsigned long)fb_pixel(&out, 0, 200), 0x000033u);
    free(out.pixels);
    compositor_cleanup(&c);
}

TEST(test_compose_guest_offset_centered) {
    compositor_t c; compositor_init(&c, 320, 240, 240, 200);
    fb_fill(&c.host, 0x000000);
    fb_fill(&c.guest, 0x00FF00);
    /* Centre 240x200 sur 320x240 : (40, 20). */
    c.guest_x = 40; c.guest_y = 20;

    compositor_fb_t out = { (uint32_t*)calloc(320*240, sizeof(uint32_t)), 320, 240 };
    compositor_compose(&c, &out);

    /* Coin haut-gauche : host (à gauche du rect guest). */
    ASSERT_EQ((unsigned long)fb_pixel(&out, 0, 0), 0u);
    /* (40, 20) : guest. */
    ASSERT_EQ((unsigned long)fb_pixel(&out, 40, 20), 0x00FF00u);
    /* (279, 219) : dernier pixel guest. */
    ASSERT_EQ((unsigned long)fb_pixel(&out, 279, 219), 0x00FF00u);
    /* (280, 219) : host (juste à droite). */
    ASSERT_EQ((unsigned long)fb_pixel(&out, 280, 219), 0u);
    free(out.pixels);
    compositor_cleanup(&c);
}

TEST(test_compose_guest_clip_negative_offset) {
    compositor_t c; compositor_init(&c, 320, 240, 240, 200);
    fb_fill(&c.host, 0xAAAAAA);
    fb_fill(&c.guest, 0x0000FF);
    /* Guest position négative : seuls les derniers pixels guest visibles. */
    c.guest_x = -100;
    c.guest_y = -50;

    compositor_fb_t out = { (uint32_t*)calloc(320*240, sizeof(uint32_t)), 320, 240 };
    compositor_compose(&c, &out);

    /* (0,0) doit être guest (pixel guest @ (100,50) → output @ (0,0)). */
    ASSERT_EQ((unsigned long)fb_pixel(&out, 0, 0), 0x0000FFu);
    /* (139, 149) = dernier pixel guest visible (guest 240x200 → x_max = 240-100-1=139, y_max=200-50-1=149) */
    ASSERT_EQ((unsigned long)fb_pixel(&out, 139, 149), 0x0000FFu);
    /* (140, 149) = host */
    ASSERT_EQ((unsigned long)fb_pixel(&out, 140, 149), 0xAAAAAAu);
    /* (139, 150) = host */
    ASSERT_EQ((unsigned long)fb_pixel(&out, 139, 150), 0xAAAAAAu);
    free(out.pixels);
    compositor_cleanup(&c);
}

TEST(test_compose_guest_clip_overflow) {
    compositor_t c; compositor_init(&c, 320, 240, 240, 200);
    fb_fill(&c.host, 0x808080);
    fb_fill(&c.guest, 0xFF00FF);
    /* Guest à (300, 230) : déborde fortement à droite/bas. */
    c.guest_x = 300; c.guest_y = 230;

    compositor_fb_t out = { (uint32_t*)calloc(320*240, sizeof(uint32_t)), 320, 240 };
    compositor_compose(&c, &out);

    /* (319, 239) doit être guest (un pixel visible). */
    ASSERT_EQ((unsigned long)fb_pixel(&out, 319, 239), 0xFF00FFu);
    /* (300, 230) doit être guest. */
    ASSERT_EQ((unsigned long)fb_pixel(&out, 300, 230), 0xFF00FFu);
    /* (299, 230) doit être host. */
    ASSERT_EQ((unsigned long)fb_pixel(&out, 299, 230), 0x808080u);
    free(out.pixels);
    compositor_cleanup(&c);
}

TEST(test_compose_guest_fully_offscreen) {
    compositor_t c; compositor_init(&c, 320, 240, 240, 200);
    fb_fill(&c.host, 0x123456);
    fb_fill(&c.guest, 0xDEADBE);
    c.guest_x = 1000;  /* loin à droite, hors écran */
    c.guest_y = 0;

    compositor_fb_t out = { (uint32_t*)calloc(320*240, sizeof(uint32_t)), 320, 240 };
    compositor_compose(&c, &out);
    /* output = host pur */
    ASSERT_EQ((unsigned long)fb_pixel(&out, 0, 0), 0x123456u);
    ASSERT_EQ((unsigned long)fb_pixel(&out, 319, 239), 0x123456u);
    free(out.pixels);
    compositor_cleanup(&c);
}

/* ─── Modèle Oric 2 — fenêtre guest centrée par défaut ───────────────── */

TEST(test_oric2_default_layout) {
    /* Représentatif Oric 2 : host 320x240, guest 240x200 ULA, fenêtre centrée. */
    compositor_t c; compositor_init(&c, 320, 240, 240, 200);
    /* Marque les 4 coins de la fenêtre guest avec une couleur distincte. */
    c.guest.pixels[0] = 0x111111;
    c.guest.pixels[239] = 0x222222;
    c.guest.pixels[(size_t)199 * 240] = 0x333333;
    c.guest.pixels[(size_t)199 * 240 + 239] = 0x444444;
    /* Host vide noir. */
    c.guest_x = 40; c.guest_y = 20;

    compositor_fb_t out = { (uint32_t*)calloc(320*240, sizeof(uint32_t)), 320, 240 };
    compositor_compose(&c, &out);

    ASSERT_EQ((unsigned long)fb_pixel(&out, 40, 20), 0x111111u);
    ASSERT_EQ((unsigned long)fb_pixel(&out, 279, 20), 0x222222u);
    ASSERT_EQ((unsigned long)fb_pixel(&out, 40, 219), 0x333333u);
    ASSERT_EQ((unsigned long)fb_pixel(&out, 279, 219), 0x444444u);
    free(out.pixels);
    compositor_cleanup(&c);
}

int main(void) {
    printf("\n");
    printf("═══════════════════════════════════════════════════════\n");
    printf("  Oric 2 Compositor (B4) — double ULA model\n");
    printf("═══════════════════════════════════════════════════════\n\n");

    RUN(test_init_alloc_dimensions);
    RUN(test_cleanup_nullifies_pointers);
    RUN(test_compose_guest_invisible_returns_host);
    RUN(test_compose_guest_at_origin);
    RUN(test_compose_guest_offset_centered);
    RUN(test_compose_guest_clip_negative_offset);
    RUN(test_compose_guest_clip_overflow);
    RUN(test_compose_guest_fully_offscreen);
    RUN(test_oric2_default_layout);

    printf("\n═══════════════════════════════════════════════════════\n");
    printf("  Results: %d passed, %d failed\n", tests_passed, tests_failed);
    printf("═══════════════════════════════════════════════════════\n\n");

    return tests_failed == 0 ? 0 : 1;
}
