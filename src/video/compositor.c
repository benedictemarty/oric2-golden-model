/**
 * @file compositor.c
 * @brief Compositor matériel modèle (B4, projet Oric 2)
 * @author bmarty <bmarty@mailo.com>
 * @date 2026-05-08
 *
 * Implémentation logicielle simple du compositor double-ULA (ADR-02).
 * Cycle-naïf : opère par pixels en mémoire (pas de timing rasterline).
 * La cible HDL ULX3S implémentera la même logique en combinatoire à la
 * cadence pixel HDMI ; ce module est le golden model du compositor.
 */

#include <stdlib.h>
#include <string.h>

#include "video/compositor.h"

bool compositor_init(compositor_t* c,
                     uint16_t host_w, uint16_t host_h,
                     uint16_t guest_w, uint16_t guest_h) {
    if (!c) return false;
    memset(c, 0, sizeof(*c));
    c->host.width  = host_w;
    c->host.height = host_h;
    c->guest.width  = guest_w;
    c->guest.height = guest_h;
    c->host.pixels  = (uint32_t*)calloc((size_t)host_w * host_h, sizeof(uint32_t));
    c->guest.pixels = (uint32_t*)calloc((size_t)guest_w * guest_h, sizeof(uint32_t));
    c->guest_visible = true;
    if (!c->host.pixels || !c->guest.pixels) {
        compositor_cleanup(c);
        return false;
    }
    return true;
}

void compositor_cleanup(compositor_t* c) {
    if (!c) return;
    free(c->host.pixels);
    free(c->guest.pixels);
    c->host.pixels = NULL;
    c->guest.pixels = NULL;
}

void compositor_compose(const compositor_t* c, compositor_fb_t* output) {
    if (!c || !output || !output->pixels) return;
    if (output->width != c->host.width || output->height != c->host.height) return;

    /* Étape 1 : copie host → output. */
    memcpy(output->pixels, c->host.pixels,
           (size_t)c->host.width * c->host.height * sizeof(uint32_t));

    if (!c->guest_visible) return;
    if (!c->guest.pixels) return;

    /* Étape 2 : superpose la fenêtre guest, avec clipping. */
    int16_t gx0 = c->guest_x;
    int16_t gy0 = c->guest_y;
    int16_t gw = (int16_t)c->guest.width;
    int16_t gh = (int16_t)c->guest.height;

    /* Clip aux bords du host */
    int16_t src_x_off = 0, src_y_off = 0;
    int16_t dst_x = gx0, dst_y = gy0;
    int16_t copy_w = gw, copy_h = gh;
    if (dst_x < 0) { src_x_off = (int16_t)(-dst_x); copy_w = (int16_t)(copy_w + dst_x); dst_x = 0; }
    if (dst_y < 0) { src_y_off = (int16_t)(-dst_y); copy_h = (int16_t)(copy_h + dst_y); dst_y = 0; }
    if (dst_x + copy_w > (int16_t)c->host.width)  copy_w = (int16_t)(c->host.width  - dst_x);
    if (dst_y + copy_h > (int16_t)c->host.height) copy_h = (int16_t)(c->host.height - dst_y);
    if (copy_w <= 0 || copy_h <= 0) return; /* Hors écran complet */

    for (int16_t row = 0; row < copy_h; row++) {
        const uint32_t* src = c->guest.pixels
            + (size_t)(src_y_off + row) * c->guest.width
            + src_x_off;
        uint32_t* dst = output->pixels
            + (size_t)(dst_y + row) * output->width
            + dst_x;
        memcpy(dst, src, (size_t)copy_w * sizeof(uint32_t));
    }
}
