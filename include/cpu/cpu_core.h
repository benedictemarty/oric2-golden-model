/**
 * @file cpu_core.h
 * @brief Vtable abstraction for the WDC 65C816 CPU core
 * @author bmarty <bmarty@mailo.com>
 * @date 2026-05-07 (révisé 2026-05-09 — PH-2.c.1, ADR-18 étape 1.C, retrait
 *       du cœur 6502 historique. Mode E du 65C816 le remplace avec
 *       équivalence comportementale prouvée bit-à-bit (cf. PH-2.b)).
 *
 * Le boucle d'émulation invoque le CPU à travers cette vtable. Historiquement
 * (B1.1) elle abstraisait deux cœurs en cohabitation ; depuis ADR-18 elle
 * encapsule uniquement le cœur 65C816. La vtable est conservée comme point
 * d'extension futur (ports HDL co-simulation, profiler-instrumented core,
 * etc.).
 */

#ifndef CPU_CORE_H
#define CPU_CORE_H

#include <stdbool.h>

#include "cpu/cpu_types.h"

typedef enum {
    CPU_KIND_65C816 = 0
} cpu_kind_t;

/**
 * @brief Table de dispatch du cœur CPU.
 *
 * Le pointeur `impl` passé à chaque méthode est opaque vis-à-vis de la table
 * mais doit être du type attendu par l'implémentation (cpu65c816_t* pour
 * la table 65c816).
 */
typedef struct cpu_core_vtable_s {
    const char* name;                                          /**< Nom lisible ("65C816") */
    void (*reset)(void* impl);                                 /**< Reset (signal RES) */
    int  (*step)(void* impl);                                  /**< Une instruction, retourne cycles consommés */
    int  (*execute_cycles)(void* impl, int cycles);            /**< Boucle jusqu'à `cycles` cycles cumulés */
    void (*nmi)(void* impl);                                   /**< Demande NMI (front) */
    void (*irq_set)(void* impl, cpu_irq_source_t src);         /**< Asserter source IRQ (niveau) */
    void (*irq_clear)(void* impl, cpu_irq_source_t src);       /**< Désasserter source IRQ */
} cpu_core_vtable_t;

/** Vtable du cœur 65C816 (mode E par défaut au reset → reproduit 6502 strict). */
extern const cpu_core_vtable_t cpu_core_vtable_65c816;

/**
 * @brief Sélectionne la vtable correspondant à un cpu_kind_t.
 * @return NULL si le kind n'est pas supporté.
 */
const cpu_core_vtable_t* cpu_core_vtable_for(cpu_kind_t kind);

/**
 * @brief Parse une chaîne CLI ("65c816"/"65C816", ou "6502" pour rétro-compat
 * CLI — redirigé vers 65c816 mode E avec warning).
 * @return false si la chaîne est inconnue (out non modifié).
 */
bool cpu_core_kind_from_string(const char* s, cpu_kind_t* out);

#endif /* CPU_CORE_H */
