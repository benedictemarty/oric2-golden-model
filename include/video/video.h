/**
 * @file video.h
 * @brief ORIC-1 video system interface
 */

#ifndef VIDEO_H
#define VIDEO_H

#include <stdint.h>
#include <stdbool.h>

#define ORIC_SCREEN_W   240
#define ORIC_SCREEN_H   224
#define ORIC_TEXT_COLS   40
#define ORIC_TEXT_ROWS   28
#define ORIC_HIRES_W    240
#define ORIC_HIRES_H    200
#define ORIC_CHAR_W     6
#define ORIC_CHAR_H     8
#define ORIC_FPS         50

/* ULA2 Color RAM (emulator enhancement) */
#define ORIC_COLOR_RAM_ADDR  0xB000  /* Color RAM mapped at $B000-$B45F */
#define ORIC_COLOR_RAM_SIZE  1120    /* 40 cols × 28 rows = 1120 bytes */

/* Color RAM byte format:
 *   bits 0-2 : ink (foreground color 0-7)
 *   bits 3-5 : paper (background color 0-7)
 *   bit 6    : alternate charset (0=standard, 1=alternate)
 *   bit 7    : inverse (0=normal, 1=inverted)
 */
#define ULA2_INK_MASK    0x07
#define ULA2_PAPER_MASK  0x38
#define ULA2_PAPER_SHIFT 3
#define ULA2_ALT_CHAR    0x40
#define ULA2_INVERSE     0x80

/* ORIC colors */
#define ORIC_BLACK   0
#define ORIC_RED     1
#define ORIC_GREEN   2
#define ORIC_YELLOW  3
#define ORIC_BLUE    4
#define ORIC_MAGENTA 5
#define ORIC_CYAN    6
#define ORIC_WHITE   7

typedef struct video_s {
    uint8_t framebuffer[ORIC_SCREEN_W * ORIC_SCREEN_H * 3]; /* RGB888 */
    bool hires_mode;
    bool need_refresh;
    uint8_t* screen_ram;    /* Pointer into memory $BB80 (text) or $A000 (hires) */
    uint8_t* charset;       /* Character set ROM */
    uint8_t vid_mode;       /* ULA video mode (persistent, set by serial attrs 24-31).
                             * Bit 2: HIRES when set. Initialized to 2 (TEXT/PAL50). */

    /* ULA2: per-cell color attributes (emulator enhancement).
     * When enabled, colors come from color_ram[] instead of serial attributes.
     * Serial attribute bytes in video RAM are rendered as characters, not consumed.
     * Color RAM is mapped at $B000-$B45F (read/write by the 6502). */
    bool ula2_enabled;      /**< ULA2 mode active (--ula2) */
} video_t;

bool video_init(video_t* vid);
void video_cleanup(video_t* vid);
void video_reset(video_t* vid);
void video_set_mode(video_t* vid, bool hires);
void video_render_frame(video_t* vid, const uint8_t* memory);
void video_get_rgb(uint8_t oric_color, uint8_t* r, uint8_t* g, uint8_t* b);

#endif
