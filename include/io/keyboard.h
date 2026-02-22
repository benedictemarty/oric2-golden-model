/**
 * @file keyboard.h
 * @brief ORIC-1 keyboard matrix emulation with SDL2 mapping
 *
 * The ORIC keyboard is an 8-column x 8-row matrix.
 * Column select via VIA Port B bits 0-2 (74LS138 decoder).
 * Row data via PSG Port A (register 14), active low.
 * Scan result on VIA PB3 (ROM keyboard scanning method).
 *
 * Key mapping table from Oricutron (Peter Gordon, GPL v2).
 */

#ifndef KEYBOARD_H
#define KEYBOARD_H

#include <stdint.h>
#include <stdbool.h>

#ifdef HAS_SDL2
#include <SDL2/SDL.h>
#endif

/** Keyboard layout selection */
typedef enum {
    ORIC_KB_QWERTY = 0,  /**< QWERTY (UK/US) layout */
    ORIC_KB_AZERTY       /**< AZERTY (French) layout */
} oric_kb_layout_t;

/**
 * @brief ORIC keyboard matrix (8 columns x 8 rows)
 * Active low: 0xFF = no keys, bit clear = key pressed.
 */
typedef struct {
    uint8_t matrix[8];
    oric_kb_layout_t layout;
} oric_keyboard_t;

void oric_keyboard_init(oric_keyboard_t* kb);
void oric_keyboard_reset(oric_keyboard_t* kb);
void oric_keyboard_set_layout(oric_keyboard_t* kb, oric_kb_layout_t layout);

#ifdef HAS_SDL2
/**
 * @brief Handle SDL2 key event and update ORIC keyboard matrix
 * @return true if the key was mapped to an ORIC key
 */
bool oric_keyboard_handle_sdl_event(oric_keyboard_t* kb, const SDL_Event* event);
#endif

#endif /* KEYBOARD_H */
