/**
 * @file cpu_core.c
 * @brief Implémentation de la vtable cpu_core (cœur 65C816 unique)
 * @author bmarty <bmarty@mailo.com>
 * @date 2026-05-07 (révisé 2026-05-09 — PH-2.c.1, ADR-18 étape 1.C, retrait
 *       du cœur 6502 historique).
 */

#include <string.h>
#include <strings.h>

#include "cpu/cpu_core.h"
#include "cpu/cpu65c816.h"
#include "utils/logging.h"

/* ─── Adaptateurs 65C816 ────────────────────────────────────────────── */

static void v816_reset(void* impl) {
    cpu816_reset((cpu65c816_t*)impl);
}

static int v816_step(void* impl) {
    return cpu816_step((cpu65c816_t*)impl);
}

static int v816_execute_cycles(void* impl, int cycles) {
    return cpu816_execute_cycles((cpu65c816_t*)impl, cycles);
}

static void v816_nmi(void* impl) {
    cpu816_nmi((cpu65c816_t*)impl);
}

static void v816_irq_set(void* impl, cpu_irq_source_t src) {
    cpu816_irq_set((cpu65c816_t*)impl, src);
}

static void v816_irq_clear(void* impl, cpu_irq_source_t src) {
    cpu816_irq_clear((cpu65c816_t*)impl, src);
}

const cpu_core_vtable_t cpu_core_vtable_65c816 = {
    .name           = "65C816",
    .reset          = v816_reset,
    .step           = v816_step,
    .execute_cycles = v816_execute_cycles,
    .nmi            = v816_nmi,
    .irq_set        = v816_irq_set,
    .irq_clear      = v816_irq_clear,
};

/* ─── Sélecteurs ────────────────────────────────────────────────────── */

const cpu_core_vtable_t* cpu_core_vtable_for(cpu_kind_t kind) {
    switch (kind) {
        case CPU_KIND_65C816: return &cpu_core_vtable_65c816;
    }
    return NULL;
}

bool cpu_core_kind_from_string(const char* s, cpu_kind_t* out) {
    if (!s || !out) return false;
    if (strcmp(s, "6502") == 0) {
        /* Rétro-compat CLI (--cpu 6502) post-ADR-18 étape 1.C : le cœur 6502
         * historique a été retiré. Le 65C816 mode E (E=1, par défaut au reset)
         * reproduit le comportement 6502 strict bit-à-bit (cf. PH-2.b
         * validation : diff PPM identique 20M cycles, ROM 1.0 et 1.1). */
        log_warning("--cpu 6502 deprecated since ADR-18 (PH-2.c, 2026-05-09); "
                    "redirected to 65C816 mode E (behaviorally equivalent).");
        *out = CPU_KIND_65C816;
        return true;
    }
    if (strcasecmp(s, "65c816") == 0) {
        *out = CPU_KIND_65C816;
        return true;
    }
    return false;
}
