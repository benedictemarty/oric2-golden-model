/**
 * @file test_joystick.c
 * @brief IJK joystick interface unit tests
 * @author bmarty <bmarty@mailo.com>
 * @date 2026-03-02
 * @version 1.6.0-alpha
 */

#include <stdio.h>
#include <string.h>
#include "io/joystick.h"

static int tests_passed = 0;
static int tests_failed = 0;

#define TEST(name) static void name(void)
#define RUN(name) do { \
    printf("  %-50s", #name); \
    name(); \
    tests_passed++; \
    printf("PASS\n"); \
} while(0)

#define ASSERT_EQ(a, b) do { \
    if ((a) != (b)) { \
        printf("FAIL\n    %s:%d: expected 0x%llX, got 0x%llX\n", __FILE__, __LINE__, \
               (unsigned long long)(b), (unsigned long long)(a)); \
        tests_failed++; return; \
    } \
} while(0)

#define ASSERT_TRUE(x) do { \
    if (!(x)) { \
        printf("FAIL\n    %s:%d: expected true\n", __FILE__, __LINE__); \
        tests_failed++; return; \
    } \
} while(0)

#define ASSERT_FALSE(x) do { \
    if ((x)) { \
        printf("FAIL\n    %s:%d: expected false\n", __FILE__, __LINE__); \
        tests_failed++; return; \
    } \
} while(0)

/* ═══════════════════════════════════════════════════════════════ */
/*  TEST 1: Init state — all released                              */
/* ═══════════════════════════════════════════════════════════════ */

TEST(test_joystick_init) {
    oric_joystick_t joy;
    oric_joystick_init(&joy);

    /* All bits HIGH (active low = released) */
    ASSERT_EQ(joy.port_a_mask, 0xFF);
    ASSERT_EQ(joy.mode, ORIC_JOY_DISABLED);
}

/* ═══════════════════════════════════════════════════════════════ */
/*  TEST 2: Disabled mode returns 0xFF                             */
/* ═══════════════════════════════════════════════════════════════ */

TEST(test_joystick_disabled) {
    oric_joystick_t joy;
    oric_joystick_init(&joy);

    /* Even if we press, disabled mode returns 0xFF */
    joy.port_a_mask = 0x00;
    ASSERT_EQ(oric_joystick_read(&joy), 0xFF);
}

/* ═══════════════════════════════════════════════════════════════ */
/*  TEST 3: Press single direction                                 */
/* ═══════════════════════════════════════════════════════════════ */

TEST(test_joystick_press_direction) {
    oric_joystick_t joy;
    oric_joystick_init(&joy);
    oric_joystick_set_mode(&joy, ORIC_JOY_KEYBOARD);

    /* Press UP: bit 4 should go LOW */
    oric_joystick_press(&joy, IJK_UP);
    ASSERT_EQ(oric_joystick_read(&joy), (uint8_t)~IJK_UP);

    /* Release UP */
    oric_joystick_release(&joy, IJK_UP);
    ASSERT_EQ(oric_joystick_read(&joy), 0xFF);
}

/* ═══════════════════════════════════════════════════════════════ */
/*  TEST 4: Press fire button                                      */
/* ═══════════════════════════════════════════════════════════════ */

TEST(test_joystick_fire) {
    oric_joystick_t joy;
    oric_joystick_init(&joy);
    oric_joystick_set_mode(&joy, ORIC_JOY_KEYBOARD);

    oric_joystick_press(&joy, IJK_FIRE);
    /* Bit 5 should be LOW */
    ASSERT_EQ(joy.port_a_mask & IJK_FIRE, 0);
    ASSERT_EQ(oric_joystick_read(&joy), (uint8_t)~IJK_FIRE);
}

/* ═══════════════════════════════════════════════════════════════ */
/*  TEST 5: Multiple simultaneous presses                          */
/* ═══════════════════════════════════════════════════════════════ */

TEST(test_joystick_simultaneous) {
    oric_joystick_t joy;
    oric_joystick_init(&joy);
    oric_joystick_set_mode(&joy, ORIC_JOY_KEYBOARD);

    /* Press UP + RIGHT + FIRE simultaneously */
    oric_joystick_press(&joy, IJK_UP);
    oric_joystick_press(&joy, IJK_RIGHT);
    oric_joystick_press(&joy, IJK_FIRE);

    uint8_t expected = (uint8_t)~(IJK_UP | IJK_RIGHT | IJK_FIRE);
    ASSERT_EQ(oric_joystick_read(&joy), expected);

    /* Release only FIRE */
    oric_joystick_release(&joy, IJK_FIRE);
    expected = (uint8_t)~(IJK_UP | IJK_RIGHT);
    ASSERT_EQ(oric_joystick_read(&joy), expected);
}

/* ═══════════════════════════════════════════════════════════════ */
/*  TEST 6: Release all                                            */
/* ═══════════════════════════════════════════════════════════════ */

TEST(test_joystick_release_all) {
    oric_joystick_t joy;
    oric_joystick_init(&joy);
    oric_joystick_set_mode(&joy, ORIC_JOY_KEYBOARD);

    oric_joystick_press(&joy, IJK_UP | IJK_DOWN | IJK_LEFT | IJK_RIGHT | IJK_FIRE);
    ASSERT_TRUE(oric_joystick_read(&joy) != 0xFF);

    oric_joystick_release_all(&joy);
    ASSERT_EQ(oric_joystick_read(&joy), 0xFF);
}

/* ═══════════════════════════════════════════════════════════════ */
/*  TEST 7: IJK bit layout matches spec                            */
/* ═══════════════════════════════════════════════════════════════ */

TEST(test_ijk_bit_layout) {
    /* Verify IJK bit positions match the standard */
    ASSERT_EQ(IJK_LEFT,  0x01);  /* Bit 0 */
    ASSERT_EQ(IJK_RIGHT, 0x02);  /* Bit 1 */
    ASSERT_EQ(IJK_DOWN,  0x08);  /* Bit 3 */
    ASSERT_EQ(IJK_UP,    0x10);  /* Bit 4 */
    ASSERT_EQ(IJK_FIRE,  0x20);  /* Bit 5 */
}

/* ═══════════════════════════════════════════════════════════════ */
/*  TEST 8: Mode switching resets state                            */
/* ═══════════════════════════════════════════════════════════════ */

TEST(test_joystick_mode_switch) {
    oric_joystick_t joy;
    oric_joystick_init(&joy);
    oric_joystick_set_mode(&joy, ORIC_JOY_KEYBOARD);

    oric_joystick_press(&joy, IJK_FIRE);
    ASSERT_TRUE(oric_joystick_read(&joy) != 0xFF);

    /* Switching mode should reset state */
    oric_joystick_set_mode(&joy, ORIC_JOY_SDL_GAMEPAD);
    ASSERT_EQ(oric_joystick_read(&joy), 0xFF);
}

/* ═══════════════════════════════════════════════════════════════ */
/*  TEST 9: All four directions                                    */
/* ═══════════════════════════════════════════════════════════════ */

TEST(test_joystick_all_directions) {
    oric_joystick_t joy;
    oric_joystick_init(&joy);
    oric_joystick_set_mode(&joy, ORIC_JOY_KEYBOARD);

    /* Test each direction individually */
    uint8_t dirs[] = { IJK_UP, IJK_DOWN, IJK_LEFT, IJK_RIGHT };
    for (int i = 0; i < 4; i++) {
        oric_joystick_press(&joy, dirs[i]);
        ASSERT_EQ(joy.port_a_mask & dirs[i], 0);  /* Bit should be LOW */
        oric_joystick_release(&joy, dirs[i]);
        ASSERT_TRUE((joy.port_a_mask & dirs[i]) != 0);  /* Bit should be HIGH */
    }
}

/* ═══════════════════════════════════════════════════════════════ */
/*  TEST 10: Reset clears state                                    */
/* ═══════════════════════════════════════════════════════════════ */

TEST(test_joystick_reset) {
    oric_joystick_t joy;
    oric_joystick_init(&joy);
    oric_joystick_set_mode(&joy, ORIC_JOY_KEYBOARD);

    oric_joystick_press(&joy, IJK_UP | IJK_FIRE);
    oric_joystick_reset(&joy);
    ASSERT_EQ(joy.port_a_mask, 0xFF);
}

/* ═══════════════════════════════════════════════════════════════ */
/*  MAIN                                                           */
/* ═══════════════════════════════════════════════════════════════ */

int main(void) {
    printf("\n");
    printf("═══════════════════════════════════════════════════════\n");
    printf("  IJK Joystick Interface Tests\n");
    printf("═══════════════════════════════════════════════════════\n");
    printf("\n");

    RUN(test_joystick_init);
    RUN(test_joystick_disabled);
    RUN(test_joystick_press_direction);
    RUN(test_joystick_fire);
    RUN(test_joystick_simultaneous);
    RUN(test_joystick_release_all);
    RUN(test_ijk_bit_layout);
    RUN(test_joystick_mode_switch);
    RUN(test_joystick_all_directions);
    RUN(test_joystick_reset);

    printf("\n");
    printf("═══════════════════════════════════════════════════════\n");
    printf("  Results: %d passed, %d failed\n", tests_passed, tests_failed);
    printf("═══════════════════════════════════════════════════════\n");
    printf("\n");

    return tests_failed > 0 ? 1 : 0;
}
