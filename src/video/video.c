/**
 * @file video.c
 * @brief ORIC-1 video system - text mode 40x28 + HIRES 240x200
 * @author bmarty <bmarty@mailo.com>
 * @date 2026-02-22
 * @version 1.0.0-beta.2
 */

#include "video/video.h"
#include <string.h>

static const uint8_t palette[8][3] = {
    {0x00,0x00,0x00},{0xFF,0x00,0x00},{0x00,0xFF,0x00},{0xFF,0xFF,0x00},
    {0x00,0x00,0xFF},{0xFF,0x00,0xFF},{0x00,0xFF,0xFF},{0xFF,0xFF,0xFF},
};

bool video_init(video_t* vid) {
    memset(vid, 0, sizeof(video_t));
    vid->hires_mode = false;
    vid->need_refresh = true;
    return true;
}

void video_cleanup(video_t* vid) { (void)vid; }

void video_reset(video_t* vid) {
    vid->hires_mode = false;
    vid->need_refresh = true;
    memset(vid->framebuffer, 0, sizeof(vid->framebuffer));
}

void video_set_mode(video_t* vid, bool hires) {
    vid->hires_mode = hires;
    vid->need_refresh = true;
}

void video_get_rgb(uint8_t oric_color, uint8_t* r, uint8_t* g, uint8_t* b) {
    uint8_t c = oric_color & 0x07;
    *r = palette[c][0]; *g = palette[c][1]; *b = palette[c][2];
}

static void set_pixel(video_t* vid, int x, int y, uint8_t r, uint8_t g, uint8_t b) {
    if (x < 0 || x >= ORIC_SCREEN_W || y < 0 || y >= ORIC_SCREEN_H) return;
    int off = (y * ORIC_SCREEN_W + x) * 3;
    vid->framebuffer[off] = r; vid->framebuffer[off+1] = g; vid->framebuffer[off+2] = b;
}

/* Get charset byte: use vid->charset override, or RAM charset at $B400 */
static uint8_t get_charset_byte(video_t* vid, uint8_t* mem, int char_idx, int row) {
    if (vid->charset) return vid->charset[char_idx * 8 + row];
    return mem[0xB400 + char_idx * 8 + row];
}

static void render_text(video_t* vid, uint8_t* mem) {
    for (int row = 0; row < ORIC_TEXT_ROWS; row++) {
        uint8_t ink = ORIC_WHITE, paper = ORIC_BLACK;
        bool inverse = false;
        for (int col = 0; col < ORIC_TEXT_COLS; col++) {
            uint8_t byte = mem[0xBB80 + row * 40 + col];
            if (byte < 32) {
                if (byte < 8) ink = byte;
                else if (byte >= 16 && byte < 24) paper = byte - 16;
                else if (byte == 28) inverse = false;
                else if (byte == 29) inverse = true;
                uint8_t pr, pg, pb;
                video_get_rgb(paper, &pr, &pg, &pb);
                for (int cy = 0; cy < 8; cy++)
                    for (int bx = 0; bx < 6; bx++)
                        set_pixel(vid, col*6+bx, row*8+cy, pr, pg, pb);
            } else {
                uint8_t fg = inverse ? paper : ink;
                uint8_t bg = inverse ? ink : paper;
                uint8_t ir, ig, ib, pr, pg, pb;
                video_get_rgb(fg, &ir, &ig, &ib);
                video_get_rgb(bg, &pr, &pg, &pb);
                for (int cy = 0; cy < 8; cy++) {
                    uint8_t bits = get_charset_byte(vid, mem, byte & 0x7F, cy);
                    bool char_inv = (byte & 0x80) != 0;
                    for (int bx = 5; bx >= 0; bx--) {
                        bool on = (bits & (1 << bx)) != 0;
                        if (char_inv) on = !on;
                        if (on) set_pixel(vid, col*6+(5-bx), row*8+cy, ir, ig, ib);
                        else    set_pixel(vid, col*6+(5-bx), row*8+cy, pr, pg, pb);
                    }
                }
            }
        }
    }
}

static void render_hires(video_t* vid, uint8_t* mem) {
    for (int y = 0; y < 200; y++) {
        uint8_t ink = ORIC_WHITE, paper = ORIC_BLACK;
        for (int col = 0; col < 40; col++) {
            uint8_t byte = mem[0xA000 + y*40 + col];
            if (!(byte & 0x40)) {
                /* Attribute byte */
                uint8_t attr = byte & 0x3F;
                if (attr < 8) ink = attr;
                else if (attr >= 16 && attr < 24) paper = attr - 16;
                uint8_t pr, pg, pb;
                video_get_rgb(paper, &pr, &pg, &pb);
                for (int bx = 0; bx < 6; bx++)
                    set_pixel(vid, col*6+bx, y, pr, pg, pb);
            } else {
                uint8_t ir, ig, ib, pr, pg, pb;
                video_get_rgb(ink, &ir, &ig, &ib);
                video_get_rgb(paper, &pr, &pg, &pb);
                for (int bx = 5; bx >= 0; bx--) {
                    if (byte & (1 << bx))
                        set_pixel(vid, col*6+(5-bx), y, ir, ig, ib);
                    else
                        set_pixel(vid, col*6+(5-bx), y, pr, pg, pb);
                }
            }
        }
    }
    /* Bottom 3 text rows (rows 25-27): rendered with serial attributes like text mode */
    for (int row = 25; row < 28; row++) {
        uint8_t ink = ORIC_WHITE, paper = ORIC_BLACK;
        bool inverse = false;
        for (int col = 0; col < 40; col++) {
            uint8_t byte = mem[0xBB80 + row*40 + col];
            int sy = 200 + (row - 25) * 8;
            if (byte < 32) {
                /* Serial attribute */
                if (byte < 8) ink = byte;
                else if (byte >= 16 && byte < 24) paper = byte - 16;
                else if (byte == 28) inverse = false;
                else if (byte == 29) inverse = true;
                uint8_t pr, pg, pb;
                video_get_rgb(paper, &pr, &pg, &pb);
                for (int cy = 0; cy < 8; cy++)
                    for (int bx = 0; bx < 6; bx++)
                        set_pixel(vid, col*6+bx, sy+cy, pr, pg, pb);
            } else {
                uint8_t fg = inverse ? paper : ink;
                uint8_t bg = inverse ? ink : paper;
                uint8_t ir, ig, ib, pr, pg, pb;
                video_get_rgb(fg, &ir, &ig, &ib);
                video_get_rgb(bg, &pr, &pg, &pb);
                for (int cy = 0; cy < 8; cy++) {
                    uint8_t bits = get_charset_byte(vid, mem, byte & 0x7F, cy);
                    bool char_inv = (byte & 0x80) != 0;
                    for (int bx = 5; bx >= 0; bx--) {
                        bool on = (bits & (1 << bx)) != 0;
                        if (char_inv) on = !on;
                        if (on) set_pixel(vid, col*6+(5-bx), sy+cy, ir, ig, ib);
                        else    set_pixel(vid, col*6+(5-bx), sy+cy, pr, pg, pb);
                    }
                }
            }
        }
    }
}

void video_render_frame(video_t* vid, uint8_t* memory) {
    if (!memory) return;
    /* Auto-detect video mode from system variable $26A (HTEFLAG):
     * bit 2 set = HIRES mode, clear = TEXT mode.
     * This is set by the ROM HIRES/TEXT commands. */
    vid->hires_mode = (memory[0x26A] & 0x04) != 0;
    if (vid->hires_mode) render_hires(vid, memory);
    else render_text(vid, memory);
    vid->need_refresh = false;
}
