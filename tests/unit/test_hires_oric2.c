/**
 * @file test_hires_oric2.c
 * @brief Tests Sprint 3.a — mode HIRES Oric 2 (ADR-12)
 * @author bmarty <bmarty@mailo.com>
 * @date 2026-05-09
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include "video/hires_oric2.h"
#include "memory/memory.h"

static int tests_passed = 0;
static int tests_failed = 0;

#define TEST(name) static void name(void)
#define RUN(name) do { \
    printf("  %-55s", #name); \
    int _before = tests_failed; \
    name(); \
    if (tests_failed == _before) { \
        tests_passed++; \
        printf("PASS\n"); \
    } \
} while(0)

#define ASSERT_TRUE(x) do { \
    if (!(x)) { \
        printf("FAIL\n    %s:%d: expected true\n", __FILE__, __LINE__); \
        tests_failed++; return; \
    } \
} while(0)

#define ASSERT_EQ(a, b) do { \
    if ((long)(a) != (long)(b)) { \
        printf("FAIL\n    %s:%d: expected %ld, got %ld\n", \
               __FILE__, __LINE__, (long)(b), (long)(a)); \
        tests_failed++; return; \
    } \
} while(0)

/* Setup helper : alloue le bank framebuffer (128 par défaut). */
static void setup(memory_t* mem) {
    memory_init(mem);
    memory_alloc_bank(mem, HIRES2_BANK_DEFAULT);
}

/* ─── Tests ───────────────────────────────────────────────────────── */

/* T1 : set/get round-trip sur 8 pixels d'un même groupe. */
TEST(test_pixel_set_get_round_trip_group) {
    memory_t mem;
    setup(&mem);

    /* Set pixels 0..7 ligne 0 avec couleurs 0..7. */
    for (int x = 0; x < 8; x++) {
        hires_oric2_set_pixel(&mem, HIRES2_BANK_DEFAULT, x, 0, (uint8_t)x);
    }
    /* Get + vérif. */
    for (int x = 0; x < 8; x++) {
        uint8_t c = hires_oric2_get_pixel(&mem, HIRES2_BANK_DEFAULT, x, 0);
        ASSERT_EQ(c, x);
    }

    memory_cleanup(&mem);
}

/* T2 : layout octets — vérifie le big-endian explicite.
 * Set pixels (0..7, 0) = (7, 0, 0, 0, 0, 0, 0, 0) → triple = 0xE00000.
 *   Octet 0 = 0xE0, Octet 1 = 0x00, Octet 2 = 0x00.
 * Set pixels (0..7, 0) = (0, 0, 0, 0, 0, 0, 0, 7) → triple = 0x000007.
 *   Octet 0 = 0x00, Octet 1 = 0x00, Octet 2 = 0x07. */
TEST(test_pixel_layout_big_endian) {
    memory_t mem;
    setup(&mem);

    uint32_t base = (uint32_t)HIRES2_BANK_DEFAULT * 0x10000u;

    /* Cas 1 : pixel 0 = 7, autres = 0. */
    hires_oric2_clear(&mem, HIRES2_BANK_DEFAULT, 0);
    hires_oric2_set_pixel(&mem, HIRES2_BANK_DEFAULT, 0, 0, 7);
    ASSERT_EQ(memory_read24(&mem, base + 0), 0xE0);
    ASSERT_EQ(memory_read24(&mem, base + 1), 0x00);
    ASSERT_EQ(memory_read24(&mem, base + 2), 0x00);

    /* Cas 2 : pixel 7 = 7, autres = 0. */
    hires_oric2_clear(&mem, HIRES2_BANK_DEFAULT, 0);
    hires_oric2_set_pixel(&mem, HIRES2_BANK_DEFAULT, 7, 0, 7);
    ASSERT_EQ(memory_read24(&mem, base + 0), 0x00);
    ASSERT_EQ(memory_read24(&mem, base + 1), 0x00);
    ASSERT_EQ(memory_read24(&mem, base + 2), 0x07);

    /* Cas 3 : pixels 0..7 = 1, 2, 3, 4, 5, 6, 7, 0.
     * Triple = 1<<21 | 2<<18 | 3<<15 | 4<<12 | 5<<9 | 6<<6 | 7<<3 | 0
     *       = $200000 + $80000 + $18000 + $4000 + $A00 + $180 + $38 + $0
     *       = $29CBB8. Bits MSB→LSB :
     *         001 010 011 100 101 110 111 000
     *         = 0010 1001 1100 1011 1011 1000
     *         = octet 0=$29, octet 1=$CB, octet 2=$B8. */
    hires_oric2_clear(&mem, HIRES2_BANK_DEFAULT, 0);
    for (int x = 0; x < 8; x++) {
        hires_oric2_set_pixel(&mem, HIRES2_BANK_DEFAULT, x, 0,
                              (uint8_t)((x + 1) & 7));
    }
    ASSERT_EQ(memory_read24(&mem, base + 0), 0x29);
    ASSERT_EQ(memory_read24(&mem, base + 1), 0xCB);
    ASSERT_EQ(memory_read24(&mem, base + 2), 0xB8);

    memory_cleanup(&mem);
}

/* T3 : palette — chaque couleur 0..7 produit le RGB attendu. */
TEST(test_palette_render) {
    memory_t mem;
    setup(&mem);

    static const uint32_t expected[8] = {
        0x000000, 0xFF0000, 0x00FF00, 0xFFFF00,
        0x0000FF, 0xFF00FF, 0x00FFFF, 0xFFFFFF,
    };

    for (int c = 0; c < 8; c++) {
        ASSERT_EQ(hires_oric2_palette[c], expected[c]);
    }

    /* Render check : remplir avec couleur c, vérifier ARGB output. */
    uint32_t* fb = malloc((size_t)HIRES2_W * HIRES2_H * sizeof(uint32_t));
    ASSERT_TRUE(fb != NULL);
    for (int c = 0; c < 8; c++) {
        hires_oric2_clear(&mem, HIRES2_BANK_DEFAULT, (uint8_t)c);
        hires_oric2_render_argb(&mem, HIRES2_BANK_DEFAULT, fb);
        ASSERT_EQ(fb[0], expected[c]);
        ASSERT_EQ(fb[HIRES2_W - 1], expected[c]);
        ASSERT_EQ(fb[HIRES2_W * HIRES2_H - 1], expected[c]);
    }
    free(fb);

    memory_cleanup(&mem);
}

/* T4 : clear + render uniforme. */
TEST(test_clear_full_screen) {
    memory_t mem;
    setup(&mem);

    hires_oric2_clear(&mem, HIRES2_BANK_DEFAULT, 5);  /* magenta */
    /* Vérifie 4 coins + centre via get_pixel. */
    ASSERT_EQ(hires_oric2_get_pixel(&mem, HIRES2_BANK_DEFAULT, 0, 0), 5);
    ASSERT_EQ(hires_oric2_get_pixel(&mem, HIRES2_BANK_DEFAULT, 239, 0), 5);
    ASSERT_EQ(hires_oric2_get_pixel(&mem, HIRES2_BANK_DEFAULT, 0, 199), 5);
    ASSERT_EQ(hires_oric2_get_pixel(&mem, HIRES2_BANK_DEFAULT, 239, 199), 5);
    ASSERT_EQ(hires_oric2_get_pixel(&mem, HIRES2_BANK_DEFAULT, 120, 100), 5);

    memory_cleanup(&mem);
}

/* T5 : pixels intra-groupe (overwrite n'affecte pas les voisins). */
TEST(test_pixel_overwrite_isolation) {
    memory_t mem;
    setup(&mem);

    /* Init pixels 0..7 ligne 0 avec couleurs distinctes 1..7,0. */
    for (int x = 0; x < 8; x++) {
        hires_oric2_set_pixel(&mem, HIRES2_BANK_DEFAULT, x, 0,
                              (uint8_t)((x + 1) & 7));
    }
    /* Overwrite pixel 3 → 0. */
    hires_oric2_set_pixel(&mem, HIRES2_BANK_DEFAULT, 3, 0, 0);

    ASSERT_EQ(hires_oric2_get_pixel(&mem, HIRES2_BANK_DEFAULT, 0, 0), 1);
    ASSERT_EQ(hires_oric2_get_pixel(&mem, HIRES2_BANK_DEFAULT, 1, 0), 2);
    ASSERT_EQ(hires_oric2_get_pixel(&mem, HIRES2_BANK_DEFAULT, 2, 0), 3);
    ASSERT_EQ(hires_oric2_get_pixel(&mem, HIRES2_BANK_DEFAULT, 3, 0), 0); /* écrasé */
    ASSERT_EQ(hires_oric2_get_pixel(&mem, HIRES2_BANK_DEFAULT, 4, 0), 5);
    ASSERT_EQ(hires_oric2_get_pixel(&mem, HIRES2_BANK_DEFAULT, 5, 0), 6);
    ASSERT_EQ(hires_oric2_get_pixel(&mem, HIRES2_BANK_DEFAULT, 6, 0), 7);
    ASSERT_EQ(hires_oric2_get_pixel(&mem, HIRES2_BANK_DEFAULT, 7, 0), 0);

    memory_cleanup(&mem);
}

/* T6 : bornes — pixels hors écran sont no-op. */
TEST(test_out_of_bounds) {
    memory_t mem;
    setup(&mem);

    /* Tentative d'écriture hors bornes — ne doit pas crasher. */
    hires_oric2_set_pixel(&mem, HIRES2_BANK_DEFAULT, -1, 0, 7);
    hires_oric2_set_pixel(&mem, HIRES2_BANK_DEFAULT, HIRES2_W, 0, 7);
    hires_oric2_set_pixel(&mem, HIRES2_BANK_DEFAULT, 0, -1, 7);
    hires_oric2_set_pixel(&mem, HIRES2_BANK_DEFAULT, 0, HIRES2_H, 7);

    /* Lecture hors bornes retourne 0. */
    ASSERT_EQ(hires_oric2_get_pixel(&mem, HIRES2_BANK_DEFAULT, -1, 0), 0);
    ASSERT_EQ(hires_oric2_get_pixel(&mem, HIRES2_BANK_DEFAULT, HIRES2_W, 0), 0);
    ASSERT_EQ(hires_oric2_get_pixel(&mem, HIRES2_BANK_DEFAULT, 0, HIRES2_H), 0);

    /* Le pixel (0,0) reste à 0 (clear initial implicite). */
    ASSERT_EQ(hires_oric2_get_pixel(&mem, HIRES2_BANK_DEFAULT, 0, 0), 0);

    memory_cleanup(&mem);
}

/* T7 : export PPM — round-trip taille fichier + en-tête. */
TEST(test_ppm_export) {
    memory_t mem;
    setup(&mem);

    hires_oric2_clear(&mem, HIRES2_BANK_DEFAULT, 7);  /* white */
    /* Dessine un point noir au centre. */
    hires_oric2_set_pixel(&mem, HIRES2_BANK_DEFAULT, 120, 100, 0);

    const char* path = "/tmp/test_hires_oric2.ppm";
    int rc = hires_oric2_export_ppm(&mem, HIRES2_BANK_DEFAULT, path);
    ASSERT_EQ(rc, 0);

    FILE* f = fopen(path, "rb");
    ASSERT_TRUE(f != NULL);
    char header[32];
    if (!fgets(header, sizeof(header), f)) { fclose(f); ASSERT_TRUE(0); }
    ASSERT_TRUE(strncmp(header, "P6", 2) == 0);
    /* dim line */
    if (!fgets(header, sizeof(header), f)) { fclose(f); ASSERT_TRUE(0); }
    ASSERT_TRUE(strstr(header, "240 200") != NULL);
    fclose(f);

    /* Cleanup */
    unlink(path);
    memory_cleanup(&mem);
}

/* T8 : ligne complète — tous les pixels d'une ligne sont contigus mémoire. */
TEST(test_full_line) {
    memory_t mem;
    setup(&mem);

    /* Set ligne 50 avec dégradé couleur cyclique. */
    for (int x = 0; x < HIRES2_W; x++) {
        hires_oric2_set_pixel(&mem, HIRES2_BANK_DEFAULT, x, 50, (uint8_t)(x & 7));
    }
    for (int x = 0; x < HIRES2_W; x++) {
        ASSERT_EQ(hires_oric2_get_pixel(&mem, HIRES2_BANK_DEFAULT, x, 50),
                  (uint8_t)(x & 7));
    }
    /* Ligne 49 et 51 doivent rester à 0. */
    for (int x = 0; x < HIRES2_W; x++) {
        ASSERT_EQ(hires_oric2_get_pixel(&mem, HIRES2_BANK_DEFAULT, x, 49), 0);
        ASSERT_EQ(hires_oric2_get_pixel(&mem, HIRES2_BANK_DEFAULT, x, 51), 0);
    }

    memory_cleanup(&mem);
}

int main(void) {
    printf("\n");
    printf("═══════════════════════════════════════════════════════\n");
    printf("  Mode HIRES Oric 2 tests (Sprint 3.a, ADR-12)\n");
    printf("═══════════════════════════════════════════════════════\n\n");

    RUN(test_pixel_set_get_round_trip_group);
    RUN(test_pixel_layout_big_endian);
    RUN(test_palette_render);
    RUN(test_clear_full_screen);
    RUN(test_pixel_overwrite_isolation);
    RUN(test_out_of_bounds);
    RUN(test_ppm_export);
    RUN(test_full_line);

    printf("\n═══════════════════════════════════════════════════════\n");
    printf("  Results: %d passed, %d failed\n", tests_passed, tests_failed);
    printf("═══════════════════════════════════════════════════════\n\n");

    return tests_failed == 0 ? 0 : 1;
}
