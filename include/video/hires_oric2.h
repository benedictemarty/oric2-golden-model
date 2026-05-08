/**
 * @file hires_oric2.h
 * @brief Mode HIRES Oric 2 — 240×200×3bpp (ADR-12)
 * @author bmarty <bmarty@mailo.com>
 * @date 2026-05-09
 *
 * Implémentation de l'ADR-12 (mode HIRES Oric 2) : framebuffer 240×200
 * pixels, 3 bits par pixel direct, palette 8 couleurs RGB fixes (idem
 * Oric 1). Localisation mémoire : banks 128-191 (cf. MEMORY_MAP.md §8).
 *
 * Format pixel : 8 pixels groupés en 24 bits sur 3 octets contigus,
 * big-endian (pixel 0 = bits hauts) :
 *   Octet n+0: P0[2:0] P1[2:0] P2[2:1]
 *   Octet n+1: P2[0]   P3[2:0] P4[2:0] P5[2]
 *   Octet n+2: P5[1:0] P6[2:0] P7[2:0]
 *
 * Adressage : 240 / 8 = 30 groupes × 3 octets = 90 octets par ligne.
 * 200 lignes × 90 = 18 000 octets total. Bank 128 ($80xxxx) offset 0
 * en v0.1 (registre I/O configurable v0.2).
 *
 * Distinction avec hires.c (Oric 1 historique) :
 *   - Oric 1 : 240×200, 1 bit/pixel + attribute, base $A000 bank 0.
 *   - Oric 2 : 240×200, 3 bits/pixel direct, base $80:0000 bank 128+.
 */

#ifndef HIRES_ORIC2_H
#define HIRES_ORIC2_H

#include <stdint.h>
#include "memory/memory.h"

/* ─── Constantes ──────────────────────────────────────────────────── */
#define HIRES2_W                240
#define HIRES2_H                200
#define HIRES2_BYTES_PER_LINE   90
#define HIRES2_FB_SIZE          (HIRES2_BYTES_PER_LINE * HIRES2_H)  /* 18000 */
#define HIRES2_BANK_DEFAULT     128

/* ─── Palette 8 couleurs (idem Oric 1, ARGB 0x00RRGGBB) ───────────── */
extern const uint32_t hires_oric2_palette[8];

/* ─── API ─────────────────────────────────────────────────────────── */

/**
 * @brief Lit un pixel à (x, y). Retourne 0 si hors bornes.
 *
 * @param mem        Mémoire Phosphoric.
 * @param bank_base  Bank de départ du framebuffer (128 typique).
 * @param x          Colonne 0..239.
 * @param y          Ligne   0..199.
 * @return Couleur 0..7.
 */
uint8_t hires_oric2_get_pixel(memory_t* mem, uint8_t bank_base, int x, int y);

/**
 * @brief Écrit un pixel à (x, y). No-op si hors bornes.
 *
 * `color` est masqué à 3 bits (couleur 0..7).
 */
void hires_oric2_set_pixel(memory_t* mem, uint8_t bank_base, int x, int y, uint8_t color);

/**
 * @brief Efface le framebuffer entier avec une couleur.
 *
 * Utilise un pattern 24-bit (color × 8 pixels) écrit triple par triple.
 */
void hires_oric2_clear(memory_t* mem, uint8_t bank_base, uint8_t color);

/**
 * @brief Render le framebuffer Oric 2 vers un buffer ARGB 240×200.
 *
 * `fb_argb` doit pointer un buffer alloué de `HIRES2_W * HIRES2_H`
 * uint32_t (= 192 000 octets).
 */
void hires_oric2_render_argb(memory_t* mem, uint8_t bank_base, uint32_t* fb_argb);

/**
 * @brief Export PPM (P6 binaire) du framebuffer Oric 2.
 *
 * @return 0 si OK, -1 si erreur (fopen, malloc).
 */
int hires_oric2_export_ppm(memory_t* mem, uint8_t bank_base, const char* path);

#endif /* HIRES_ORIC2_H */
