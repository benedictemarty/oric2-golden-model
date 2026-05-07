/**
 * @file cpu_core.c
 * @brief Implémentation de la vtable cpu_core (B1.1, projet Oric 2)
 * @author bmarty <bmarty@mailo.com>
 * @date 2026-05-07
 */

#include <string.h>
#include <strings.h>

#include "cpu/cpu_core.h"
#include "cpu/cpu6502.h"

/* ─── Adaptateurs 6502 ──────────────────────────────────────────────── */

static void v6502_reset(void* impl) {
    cpu_reset((cpu6502_t*)impl);
}

static int v6502_step(void* impl) {
    return cpu_step((cpu6502_t*)impl);
}

static int v6502_execute_cycles(void* impl, int cycles) {
    return cpu_execute_cycles((cpu6502_t*)impl, cycles);
}

static void v6502_nmi(void* impl) {
    cpu_nmi((cpu6502_t*)impl);
}

static void v6502_irq_set(void* impl, cpu_irq_source_t src) {
    cpu_irq_set((cpu6502_t*)impl, src);
}

static void v6502_irq_clear(void* impl, cpu_irq_source_t src) {
    cpu_irq_clear((cpu6502_t*)impl, src);
}

const cpu_core_vtable_t cpu_core_vtable_6502 = {
    .name           = "6502",
    .reset          = v6502_reset,
    .step           = v6502_step,
    .execute_cycles = v6502_execute_cycles,
    .nmi            = v6502_nmi,
    .irq_set        = v6502_irq_set,
    .irq_clear      = v6502_irq_clear,
};

/* ─── Sélecteurs ────────────────────────────────────────────────────── */

const cpu_core_vtable_t* cpu_core_vtable_for(cpu_kind_t kind) {
    switch (kind) {
        case CPU_KIND_6502:   return &cpu_core_vtable_6502;
        case CPU_KIND_65C816: return NULL; /* B1.2 */
    }
    return NULL;
}

bool cpu_core_kind_from_string(const char* s, cpu_kind_t* out) {
    if (!s || !out) return false;
    if (strcmp(s, "6502") == 0) {
        *out = CPU_KIND_6502;
        return true;
    }
    if (strcasecmp(s, "65c816") == 0) {
        *out = CPU_KIND_65C816;
        return true;
    }
    return false;
}
