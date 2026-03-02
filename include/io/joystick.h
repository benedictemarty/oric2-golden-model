/**
 * @file joystick.h
 * @brief IJK joystick interface for Phosphoric
 * @author bmarty <bmarty@mailo.com>
 * @date 2026-03-02
 * @version 1.6.0-alpha
 *
 * Emulates the IJK joystick interface, the most common ORIC joystick
 * adapter. The IJK connects to the printer port and is read via
 * PSG Port A (register 14), active low.
 *
 * IJK bit layout (active low: 0 = pressed, 1 = released):
 *   Bit 0: Left
 *   Bit 1: Right
 *   Bit 2: (unused, active low on some interfaces)
 *   Bit 3: Down
 *   Bit 4: Up
 *   Bit 5: Fire
 *   Bit 6-7: unused (active low = 0)
 *
 * Also supports "keyboard-as-joystick" mode where arrow keys and
 * a fire key are mapped to IJK directions.
 */

#ifndef JOYSTICK_H
#define JOYSTICK_H

#include <stdint.h>
#include <stdbool.h>

#ifdef HAS_SDL2
#include <SDL2/SDL.h>
#endif

/** IJK joystick bit positions (active low) */
#define IJK_LEFT   (1 << 0)
#define IJK_RIGHT  (1 << 1)
#define IJK_DOWN   (1 << 3)
#define IJK_UP     (1 << 4)
#define IJK_FIRE   (1 << 5)

/** Joystick input mode */
typedef enum {
    ORIC_JOY_DISABLED = 0,   /**< No joystick */
    ORIC_JOY_SDL_GAMEPAD,    /**< SDL2 game controller / joystick */
    ORIC_JOY_KEYBOARD        /**< Keyboard arrows + fire key as joystick */
} oric_joy_mode_t;

/**
 * @brief IJK joystick state
 *
 * port_a_mask holds the current joystick state as it appears on
 * PSG Port A. Active low: 0xFF = nothing pressed.
 */
typedef struct oric_joystick_s {
    uint8_t port_a_mask;       /**< IJK state (active low, AND with keyboard) */
    oric_joy_mode_t mode;      /**< Input mode */
#ifdef HAS_SDL2
    SDL_GameController* controller;  /**< SDL2 game controller handle */
    SDL_Joystick* joystick;          /**< SDL2 joystick handle (fallback) */
    int device_index;                /**< SDL2 device index */
#endif
} oric_joystick_t;

/** Initialize joystick (all released, disabled) */
void oric_joystick_init(oric_joystick_t* joy);

/** Reset joystick state (all released) */
void oric_joystick_reset(oric_joystick_t* joy);

/** Set joystick mode */
void oric_joystick_set_mode(oric_joystick_t* joy, oric_joy_mode_t mode);

/** Press a direction/button (sets bit LOW) */
void oric_joystick_press(oric_joystick_t* joy, uint8_t button_mask);

/** Release a direction/button (sets bit HIGH) */
void oric_joystick_release(oric_joystick_t* joy, uint8_t button_mask);

/** Release all directions/buttons */
void oric_joystick_release_all(oric_joystick_t* joy);

/** Get current IJK port A state (for blending with keyboard) */
uint8_t oric_joystick_read(const oric_joystick_t* joy);

#ifdef HAS_SDL2
/**
 * @brief Open SDL2 game controller or joystick
 * @param device_index SDL device index (0 = first controller)
 * @return true if a controller was opened
 */
bool oric_joystick_open_sdl(oric_joystick_t* joy, int device_index);

/** Close SDL2 controller/joystick */
void oric_joystick_close_sdl(oric_joystick_t* joy);

/**
 * @brief Handle SDL2 events (controller buttons, axis, keyboard-as-joystick)
 * @return true if the event was consumed by joystick handling
 */
bool oric_joystick_handle_sdl_event(oric_joystick_t* joy, const SDL_Event* event);
#endif

#endif /* JOYSTICK_H */
