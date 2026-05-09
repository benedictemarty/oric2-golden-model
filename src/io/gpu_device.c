/**
 * @file gpu_device.c
 * @brief Émulation GPU Blitter HW Oric 2 (ADR-21, Sprint GPU-1)
 */

#include "io/gpu_device.h"
#include "utils/logging.h"

#include <stdlib.h>
#include <string.h>

void gpu_init(gpu_device_t* gpu) {
    if (!gpu) return;
    memset(gpu, 0, sizeof(*gpu));
}

void gpu_cleanup(gpu_device_t* gpu) {
    (void)gpu;  /* pas de heap pour l'instant */
}

/* ─── Lecture registers ──────────────────────────────────────────── */

uint8_t gpu_read(gpu_device_t* gpu, uint8_t reg) {
    if (!gpu) return 0xFFu;
    switch (reg) {
        case GPU_REG_CMD_OP:    return gpu->cmd_op;
        case GPU_REG_ARG1_LO:   return (uint8_t)( gpu->arg1        & 0xFFu);
        case GPU_REG_ARG1_MID:  return (uint8_t)((gpu->arg1 >> 8)  & 0xFFu);
        case GPU_REG_ARG1_HI:   return (uint8_t)((gpu->arg1 >> 16) & 0xFFu);
        case GPU_REG_ARG2_LO:   return (uint8_t)( gpu->arg2        & 0xFFu);
        case GPU_REG_ARG2_MID:  return (uint8_t)((gpu->arg2 >> 8)  & 0xFFu);
        case GPU_REG_ARG2_HI:   return (uint8_t)((gpu->arg2 >> 16) & 0xFFu);
        case GPU_REG_ARG3_LO:   return (uint8_t)( gpu->arg3        & 0xFFu);
        case GPU_REG_ARG3_MID:  return (uint8_t)((gpu->arg3 >> 8)  & 0xFFu);
        case GPU_REG_ARG3_HI:   return (uint8_t)((gpu->arg3 >> 16) & 0xFFu);
        case GPU_REG_ARG4_LO:   return (uint8_t)( gpu->arg4        & 0xFFu);
        case GPU_REG_ARG4_MID:  return (uint8_t)((gpu->arg4 >> 8)  & 0xFFu);
        case GPU_REG_ARG4_HI:   return (uint8_t)((gpu->arg4 >> 16) & 0xFFu);
        case GPU_REG_STATUS: {
            uint8_t s = 0;
            if (gpu->busy) s |= GPU_STATUS_BUSY;
            if (gpu->err)  s |= GPU_STATUS_ERR;
            else           s |= GPU_STATUS_DONE;
            return s;
        }
        case GPU_REG_TRIGGER:   return 0u;  /* W-only */
        case GPU_REG_INT_CTRL:  return gpu->int_ctrl;
        default:                return 0xFFu;
    }
}

/* ─── Exécution commandes ────────────────────────────────────────── */

/* CLEAR : remplit `size` octets en SDRAM[base] avec pattern (color×0x11).
 * ARG1 = base SDRAM (24-bit), ARG2 = size (octets), ARG3.LO = color (0..15). */
static void gpu_exec_clear(gpu_device_t* gpu, vram_device_t* vram) {
    if (!vram) return;
    uint32_t base = gpu->arg1 & 0xFFFFFFu;
    uint32_t size = gpu->arg2 & 0xFFFFFFu;
    uint8_t  color = (uint8_t)(gpu->arg3 & 0x0Fu);
    uint8_t  pattern = (uint8_t)((color << 4) | color);
    for (uint32_t i = 0; i < size; i++) {
        vram_poke(vram, base + i, pattern);
    }
}

/* FILL_RECT : remplit un rectangle 4bpp dans framebuffer SDRAM.
 * ARG1 = base SDRAM (24-bit) du framebuffer.
 * ARG2.LO = x (8b), ARG2.MID = y (8b), ARG2.HI = unused.
 * ARG3.LO = w (8b), ARG3.MID = h (8b), ARG3.HI = unused.
 * ARG4.LO = color (4b).
 *
 * v0.1 : x/y/w/h limités à 8-bit (≤ 255). BPL hardcodé GPU_XVGA_BPL=512.
 * v0.2 : 16-bit chacun + BPL configurable. */
static void gpu_exec_fill_rect(gpu_device_t* gpu, vram_device_t* vram) {
    if (!vram) return;
    uint32_t base = gpu->arg1 & 0xFFFFFFu;
    int x = (int)( gpu->arg2        & 0xFFu);
    int y = (int)((gpu->arg2 >> 8)  & 0xFFu);
    int w = (int)( gpu->arg3        & 0xFFu);
    int h = (int)((gpu->arg3 >> 8)  & 0xFFu);
    uint8_t color = (uint8_t)(gpu->arg4 & 0x0Fu);
    int bpl = (int)GPU_XVGA_BPL;

    for (int yy = y; yy < y + h; yy++) {
        for (int xx = x; xx < x + w; xx++) {
            uint32_t byte_off = base + (uint32_t)(yy * bpl + xx / 2);
            uint8_t b = vram_peek(vram, byte_off);
            if (xx & 1) {
                /* pixel droit (bits [3:0]) */
                b = (uint8_t)((b & 0xF0u) | (color & 0x0Fu));
            } else {
                /* pixel gauche (bits [7:4]) */
                b = (uint8_t)((b & 0x0Fu) | (color << 4));
            }
            vram_poke(vram, byte_off, b);
        }
    }
}

/* BLIT v0.1 : copie un bloc rectangulaire SDRAM → SDRAM.
 * ARG1 = src_addr 24-bit, ARG2 = dst_addr 24-bit.
 * ARG3.LO = byte_w (octets/ligne, 1..255), ARG3.MID = byte_h (lignes).
 * ARG4 = flags (unused v0.1).
 *
 * v0.1 limites :
 * - src et dst doivent être byte-alignés (= x pair en 4bpp).
 * - Pas de gestion d'overlap (src/dst doivent être disjoints).
 * - Pas de transparency / ROP (flags ignorés).
 * - BPL hardcodé GPU_XVGA_BPL=512 pour src et dst.
 * v0.2 : alignement pixel-arbitraire, overlap gauche-vers-droite, transparency. */
static void gpu_exec_blit(gpu_device_t* gpu, vram_device_t* vram) {
    if (!vram) return;
    uint32_t src = gpu->arg1 & 0xFFFFFFu;
    uint32_t dst = gpu->arg2 & 0xFFFFFFu;
    int byte_w = (int)( gpu->arg3        & 0xFFu);
    int byte_h = (int)((gpu->arg3 >> 8)  & 0xFFu);
    int bpl = (int)GPU_XVGA_BPL;

    for (int y = 0; y < byte_h; y++) {
        uint32_t soff = src + (uint32_t)(y * bpl);
        uint32_t doff = dst + (uint32_t)(y * bpl);
        for (int x = 0; x < byte_w; x++) {
            uint8_t b = vram_peek(vram, soff + (uint32_t)x);
            vram_poke(vram, doff + (uint32_t)x, b);
        }
    }
}

/* Helper : set pixel 4bpp à (x, y) dans framebuffer base SDRAM. */
static void gpu_set_pixel(vram_device_t* vram, uint32_t base, int x, int y,
                          uint8_t color) {
    uint32_t byte_off = base + (uint32_t)(y * (int)GPU_XVGA_BPL + x / 2);
    uint8_t b = vram_peek(vram, byte_off);
    if (x & 1) {
        b = (uint8_t)((b & 0xF0u) | (color & 0x0Fu));
    } else {
        b = (uint8_t)((b & 0x0Fu) | (color << 4));
    }
    vram_poke(vram, byte_off, b);
}

/* LINE v0.1 : tracé Bresenham 4bpp.
 * ARG1 = base SDRAM framebuffer.
 * ARG2.LO/MID = (x1, y1) 8-bit chacun.
 * ARG3.LO/MID = (x2, y2) 8-bit chacun.
 * ARG4.LO = color (4-bit). */
static void gpu_exec_line(gpu_device_t* gpu, vram_device_t* vram) {
    if (!vram) return;
    uint32_t base = gpu->arg1 & 0xFFFFFFu;
    int x1 = (int)( gpu->arg2        & 0xFFu);
    int y1 = (int)((gpu->arg2 >> 8)  & 0xFFu);
    int x2 = (int)( gpu->arg3        & 0xFFu);
    int y2 = (int)((gpu->arg3 >> 8)  & 0xFFu);
    uint8_t color = (uint8_t)(gpu->arg4 & 0x0Fu);

    /* Bresenham line algorithm. */
    int dx = abs(x2 - x1);
    int dy = -abs(y2 - y1);
    int sx = (x1 < x2) ? 1 : -1;
    int sy = (y1 < y2) ? 1 : -1;
    int err = dx + dy;
    int x = x1, y = y1;
    for (;;) {
        gpu_set_pixel(vram, base, x, y, color);
        if (x == x2 && y == y2) break;
        int e2 = 2 * err;
        if (e2 >= dy) { err += dy; x += sx; }
        if (e2 <= dx) { err += dx; y += sy; }
    }
}

/* TEXT v0.1 : rendu fonte 8×8 mono à color_fg.
 * Args :
 *   ARG1 = base SDRAM framebuffer (24-bit).
 *   ARG2 = font_addr 24-bit (= 256 chars × 8 bytes/char en SDRAM).
 *   ARG3 = string_addr 24-bit (null-terminated, max 255 chars).
 *   ARG4.LO = x (8-bit), ARG4.MID = y (8-bit), ARG4.HI = color_fg (4b).
 *
 * Pixels OFF dans le bitmap = laissés intacts (pas de color_bg).
 * BPL hardcodé GPU_XVGA_BPL=512.
 *
 * v0.2 reportés : color_bg, fonte taille variable, length 16-bit. */
static void gpu_exec_text(gpu_device_t* gpu, vram_device_t* vram) {
    if (!vram) return;
    uint32_t base = gpu->arg1 & 0xFFFFFFu;
    uint32_t font = gpu->arg2 & 0xFFFFFFu;
    uint32_t str  = gpu->arg3 & 0xFFFFFFu;
    int x_start = (int)( gpu->arg4        & 0xFFu);
    int y_start = (int)((gpu->arg4 >> 8)  & 0xFFu);
    uint8_t color = (uint8_t)((gpu->arg4 >> 16) & 0x0Fu);

    for (int ci = 0; ci < 256; ci++) {
        uint8_t c = vram_peek(vram, str + (uint32_t)ci);
        if (c == 0u) break;  /* null-terminated */
        uint32_t char_addr = font + (uint32_t)c * 8u;
        int char_x = x_start + ci * 8;
        for (int row = 0; row < 8; row++) {
            uint8_t bits = vram_peek(vram, char_addr + (uint32_t)row);
            for (int col = 0; col < 8; col++) {
                if (bits & (uint8_t)(0x80u >> col)) {
                    gpu_set_pixel(vram, base, char_x + col, y_start + row,
                                  color);
                }
            }
        }
    }
}

/* Dispatch trigger. */
static void gpu_dispatch(gpu_device_t* gpu, vram_device_t* vram, memory_t* mem) {
    (void)mem;  /* unused v0.1 (toutes ops ciblent SDRAM) */
    gpu->err = false;
    switch (gpu->cmd_op) {
        case GPU_OP_NOP:
            break;
        case GPU_OP_CLEAR:
            gpu_exec_clear(gpu, vram);
            break;
        case GPU_OP_FILL_RECT:
            gpu_exec_fill_rect(gpu, vram);
            break;
        case GPU_OP_BLIT:
            gpu_exec_blit(gpu, vram);
            break;
        case GPU_OP_LINE:
            gpu_exec_line(gpu, vram);
            break;
        case GPU_OP_TEXT:
            gpu_exec_text(gpu, vram);
            break;
        default:
            gpu->err = true;  /* opcode inconnu */
            break;
    }
    /* v0.1 synchrone : busy reste false. */
}

/* ─── Écriture registers ─────────────────────────────────────────── */

void gpu_write(gpu_device_t* gpu, vram_device_t* vram, memory_t* mem,
               uint8_t reg, uint8_t value) {
    if (!gpu) return;
    switch (reg) {
        case GPU_REG_CMD_OP:    gpu->cmd_op = value; break;
        case GPU_REG_ARG1_LO:   gpu->arg1 = (gpu->arg1 & 0xFFFF00u) |  (uint32_t)value; break;
        case GPU_REG_ARG1_MID:  gpu->arg1 = (gpu->arg1 & 0xFF00FFu) | ((uint32_t)value << 8); break;
        case GPU_REG_ARG1_HI:   gpu->arg1 = (gpu->arg1 & 0x00FFFFu) | ((uint32_t)value << 16); break;
        case GPU_REG_ARG2_LO:   gpu->arg2 = (gpu->arg2 & 0xFFFF00u) |  (uint32_t)value; break;
        case GPU_REG_ARG2_MID:  gpu->arg2 = (gpu->arg2 & 0xFF00FFu) | ((uint32_t)value << 8); break;
        case GPU_REG_ARG2_HI:   gpu->arg2 = (gpu->arg2 & 0x00FFFFu) | ((uint32_t)value << 16); break;
        case GPU_REG_ARG3_LO:   gpu->arg3 = (gpu->arg3 & 0xFFFF00u) |  (uint32_t)value; break;
        case GPU_REG_ARG3_MID:  gpu->arg3 = (gpu->arg3 & 0xFF00FFu) | ((uint32_t)value << 8); break;
        case GPU_REG_ARG3_HI:   gpu->arg3 = (gpu->arg3 & 0x00FFFFu) | ((uint32_t)value << 16); break;
        case GPU_REG_ARG4_LO:   gpu->arg4 = (gpu->arg4 & 0xFFFF00u) |  (uint32_t)value; break;
        case GPU_REG_ARG4_MID:  gpu->arg4 = (gpu->arg4 & 0xFF00FFu) | ((uint32_t)value << 8); break;
        case GPU_REG_ARG4_HI:   gpu->arg4 = (gpu->arg4 & 0x00FFFFu) | ((uint32_t)value << 16); break;
        case GPU_REG_TRIGGER:
            gpu_dispatch(gpu, vram, mem);
            break;
        case GPU_REG_INT_CTRL:
            gpu->int_ctrl = value;
            break;
        default:
            break;
    }
}
