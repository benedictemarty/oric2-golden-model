/**
 * @file cpu_types.h
 * @brief Shared CPU types neutral between 6502 and 65C816 cores
 * @author bmarty <bmarty@mailo.com>
 * @date 2026-05-09
 *
 * Types partagés entre les cœurs 6502 et 65C816 de Phosphoric, extraits
 * de cpu6502.h dans le cadre de l'ADR-18 (retrait du 6502, étape 1.A).
 *
 * Ce header **ne dépend** ni de `cpu6502_t` ni de `cpu65c816_t`. Il fournit
 * les types fondamentaux (`memory_t` forward decl, `cpu_flags_t`,
 * `cpu_irq_source_t`) consommables par les sous-systèmes (memory, debugger,
 * trace, profiler, emulator.h, vtable cpu_core) sans coupler ces sous-systèmes
 * à un cœur CPU spécifique.
 *
 * Phase 1.A ADR-18 (additif) : `cpu6502.h` et `cpu65c816.h` incluent ce header
 * et conservent leurs définitions historiques pour rétro-compat. La migration
 * effective des consommateurs vers `cpu_types.h` direct sera réalisée en 1.C
 * (suppression du cœur 6502).
 */

#ifndef CPU_TYPES_H
#define CPU_TYPES_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

/**
 * @brief Forward declaration of memory subsystem.
 *
 * Évite l'include circulaire : memory.h dépend de cpu_irq_source_t
 * (callbacks), et le CPU dépend de memory_t* dans son state.
 */
typedef struct memory_s memory_t;

/**
 * @brief CPU status flags (Processor Status Register, P).
 *
 * Compatible 6502 NMOS et 65C816 (mode E identique, mode N réinterprète
 * les bits 0x10 et 0x20 comme X et M, cf. ADR-11).
 */
typedef enum {
    FLAG_CARRY     = 0x01,  /**< Carry flag (C) */
    FLAG_ZERO      = 0x02,  /**< Zero flag (Z) */
    FLAG_INTERRUPT = 0x04,  /**< Interrupt disable (I) */
    FLAG_DECIMAL   = 0x08,  /**< Decimal mode (D) */
    FLAG_BREAK     = 0x10,  /**< Break command (B) — mode E. Mode N : X (index width) */
    FLAG_UNUSED    = 0x20,  /**< Unused (always 1) — mode E. Mode N : M (accu width) */
    FLAG_OVERFLOW  = 0x40,  /**< Overflow flag (V) */
    FLAG_NEGATIVE  = 0x80   /**< Negative flag (N) */
} cpu_flags_t;

/**
 * @brief IRQ source flags (bitfield for level-triggered IRQ model).
 *
 * Le 6502 et le 65C816 utilisent tous deux un IRQ niveau-déclenché : le CPU
 * prend une interruption quand IRQ est asserté ET le flag I est clair.
 * Plusieurs sources peuvent asserter IRQ simultanément, chacune sur son bit.
 */
typedef enum {
    IRQF_VIA    = 0x01,  /**< VIA 6522 IRQ (T1 timer, CB1/CB2, etc.) */
    IRQF_DISK   = 0x02,  /**< Microdisc FDC INTRQ */
    IRQF_SERIAL = 0x04   /**< ACIA 6551 serial IRQ */
} cpu_irq_source_t;

#endif /* CPU_TYPES_H */
