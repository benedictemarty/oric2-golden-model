/**
 * @file keyboard.c
 * @brief ORIC-1 keyboard matrix emulation with SDL2 key mapping
 * @author bmarty <bmarty@mailo.com>
 * @date 2026-02-22
 * @version 0.5.0-alpha
 *
 * The ORIC keyboard is an 8-column x 8-row matrix (64 keys).
 * Column select via VIA Port B bits 0-2 → 74LS138 decoder.
 * Row data read via PSG Port A (register 14), active low.
 * ROM scans keyboard via VIA PB3 (set when a key matches).
 *
 * Key mapping table based on Oricutron by Peter Gordon (GPL v2).
 * The table maps SDL2 keycodes to (column, row) positions.
 * Layout: keytab[row*8 + column_bit_index] = SDL keycode
 * Where column_bit_index maps to the bit position in the row mask:
 *   FE(bit0) FD(bit1) FB(bit2) F7(bit3) EF(bit4) DF(bit5) BF(bit6) 7F(bit7)
 */

#include "io/keyboard.h"
#include <string.h>

void oric_keyboard_init(oric_keyboard_t* kb) {
    memset(kb->matrix, 0xFF, sizeof(kb->matrix));
}

void oric_keyboard_reset(oric_keyboard_t* kb) {
    memset(kb->matrix, 0xFF, sizeof(kb->matrix));
}

#ifdef HAS_SDL2

/**
 * ORIC keyboard matrix mapping (QWERTY layout from Oricutron)
 *
 * The matrix is 8 rows × 8 columns.
 * Each entry keytab[row * 8 + col] gives the SDL keycode.
 * Row = index / 8, Column bit = index % 8.
 *
 * Matrix layout (from Oricutron 8912.c):
 *              Col0(FE) Col1(FD) Col2(FB) Col3(F7) Col4(EF)  Col5(DF)  Col6(BF)  Col7(7F)
 * Row 0:       7        n        5        v        RCTRL     1         x         3
 * Row 1:       j        t        r        f        (none)    ESC       q         d
 * Row 2:       m        6        b        4        LCTRL     z         2         c
 * Row 3:       k        9        ;        -        #         (none)    \         '
 * Row 4:       SPACE    ,        .        UP       LSHIFT    LEFT      DOWN      RIGHT
 * Row 5:       u        i        o        p        LALT      BKSP      ]         [
 * Row 6:       y        h        g        e        RALT      a         s         w
 * Row 7:       8        l        0        /        RSHIFT    RETURN    `         =
 */
static const SDL_Keycode keytab[64] = {
    /* Row 0 */ SDLK_7,     SDLK_n,     SDLK_5,        SDLK_v,     SDLK_RCTRL,    SDLK_1,        SDLK_x,         SDLK_3,
    /* Row 1 */ SDLK_j,     SDLK_t,     SDLK_r,        SDLK_f,     0,             SDLK_ESCAPE,   SDLK_q,         SDLK_d,
    /* Row 2 */ SDLK_m,     SDLK_6,     SDLK_b,        SDLK_4,     SDLK_LCTRL,    SDLK_z,        SDLK_2,         SDLK_c,
    /* Row 3 */ SDLK_k,     SDLK_9,     SDLK_SEMICOLON,SDLK_MINUS, SDLK_HASH,     0,             SDLK_BACKSLASH, SDLK_QUOTE,
    /* Row 4 */ SDLK_SPACE, SDLK_COMMA, SDLK_PERIOD,   SDLK_UP,    SDLK_LSHIFT,   SDLK_LEFT,     SDLK_DOWN,      SDLK_RIGHT,
    /* Row 5 */ SDLK_u,     SDLK_i,     SDLK_o,        SDLK_p,     SDLK_LALT,     SDLK_BACKSPACE,SDLK_RIGHTBRACKET,SDLK_LEFTBRACKET,
    /* Row 6 */ SDLK_y,     SDLK_h,     SDLK_g,        SDLK_e,     SDLK_RALT,     SDLK_a,        SDLK_s,         SDLK_w,
    /* Row 7 */ SDLK_8,     SDLK_l,     SDLK_0,        SDLK_SLASH, SDLK_RSHIFT,   SDLK_RETURN,   SDLK_BACKQUOTE, SDLK_EQUALS
};

bool oric_keyboard_handle_sdl_event(oric_keyboard_t* kb, const SDL_Event* event) {
    if (event->type != SDL_KEYDOWN && event->type != SDL_KEYUP)
        return false;

    SDL_Keycode key = event->key.keysym.sym;
    bool down = (event->type == SDL_KEYDOWN);

    /* Search for this key in the mapping table */
    for (int i = 0; i < 64; i++) {
        if (keytab[i] == key) {
            int row = i / 8;  /* Which row in the matrix */
            int col = i % 8;  /* Which column bit */

            if (down) {
                /* Key pressed: clear the bit (active low) */
                kb->matrix[row] &= ~(1 << col);
            } else {
                /* Key released: set the bit (active low) */
                kb->matrix[row] |= (1 << col);
            }
            return true;
        }
    }

    return false;
}

#endif /* HAS_SDL2 */
