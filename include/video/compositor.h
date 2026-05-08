/**
 * @file compositor.h
 * @brief Compositor matériel modèle (B4, projet Oric 2)
 * @author bmarty <bmarty@mailo.com>
 * @date 2026-05-08
 *
 * Modèle logiciel du compositor matériel ULX3S (cf. ADR-02). Combine
 * deux framebuffers (host + guest) en un framebuffer de sortie. Sur la
 * cible HDL, ce composant est implémenté en logique combinatoire :
 * pour chaque pixel d'horloge, il choisit entre source host et guest
 * selon la position de la fenêtre guest.
 *
 * Le format de pixel est `uint32_t` ARGB (`0x00RRGGBB`). Bit alpha
 * non utilisé pour l'instant — la fenêtre guest est opaque.
 *
 * Référence : ADR-02 (compositor + double ULA), DAT §4.3.
 */

#ifndef COMPOSITOR_H
#define COMPOSITOR_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

/**
 * @brief Framebuffer ARGB8888.
 *
 * Allocation laissée à l'appelant ; ce header ne décrit que la structure.
 */
typedef struct {
    uint32_t* pixels;   /**< W*H pixels ARGB. NULL si non alloué. */
    uint16_t  width;
    uint16_t  height;
} compositor_fb_t;

/**
 * @brief État du compositor.
 *
 * Les deux framebuffers (host et guest) sont alloués au moment d'init.
 * `guest_x` et `guest_y` placent le coin haut-gauche de la fenêtre guest
 * dans les coordonnées du framebuffer host. La fenêtre guest est de la
 * taille du framebuffer guest (typique 240×200 pour l'ULA Oric 1).
 */
typedef struct {
    compositor_fb_t host;          /**< Framebuffer host (OricOS principal) */
    compositor_fb_t guest;         /**< Framebuffer guest (ULA Oric 1) */
    int16_t guest_x;               /**< Position X de la fenêtre guest sur host (signed pour clip négatif) */
    int16_t guest_y;               /**< Position Y de la fenêtre guest sur host */
    bool guest_visible;            /**< Si false : compose ne rend que le host */
} compositor_t;

/**
 * @brief Init compositor avec dimensions explicites. Alloue les pixels.
 *
 * @return true si OK, false si OOM.
 */
bool compositor_init(compositor_t* c,
                     uint16_t host_w, uint16_t host_h,
                     uint16_t guest_w, uint16_t guest_h);

/**
 * @brief Libère les pixels alloués.
 */
void compositor_cleanup(compositor_t* c);

/**
 * @brief Remplit `output` avec la composition host + guest.
 *
 * `output` doit avoir les mêmes dimensions que le framebuffer host. Si
 * `guest_visible` est faux, output = host. Sinon, output = host avec la
 * fenêtre guest superposée à (guest_x, guest_y). La fenêtre guest est
 * **clippée** automatiquement aux bords du host (dépassement gauche/haut
 * via offset négatif, dépassement droite/bas via troncature).
 */
void compositor_compose(const compositor_t* c, compositor_fb_t* output);

#endif /* COMPOSITOR_H */
