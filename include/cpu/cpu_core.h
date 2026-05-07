/**
 * @file cpu_core.h
 * @brief Vtable abstraction for swappable CPU cores (6502 / 65C816)
 * @author bmarty <bmarty@mailo.com>
 * @date 2026-05-07
 *
 * Cohabitation 6502 / 65C816 (jalon B1.1, projet Oric 2).
 * Le boucle d'émulation invoque le CPU à travers cette vtable, ce qui permet
 * de remplacer le cœur sans modifier main.c. Les accès en lecture aux champs
 * de registres (debugger, savestate) restent directs sur cpu6502_t en B1.1
 * et seront convertis en B1.2 lors de l'arrivée du 65C816.
 */

#ifndef CPU_CORE_H
#define CPU_CORE_H

#include "cpu/cpu6502.h"

typedef enum {
    CPU_KIND_6502   = 0,
    CPU_KIND_65C816 = 1
} cpu_kind_t;

/**
 * @brief Table de dispatch d'un cœur CPU.
 *
 * Chaque implémentation (6502 actuel, 65C816 à venir) fournit une instance
 * statique de cette table. Le pointeur `impl` passé à chaque méthode est
 * opaque vis-à-vis de la table mais doit être du type attendu par
 * l'implémentation (ex: cpu6502_t* pour la table 6502).
 */
typedef struct cpu_core_vtable_s {
    const char* name;                                          /**< Nom lisible ("6502", "65C816") */
    void (*reset)(void* impl);                                 /**< Reset (signal RES) */
    int  (*step)(void* impl);                                  /**< Une instruction, retourne cycles consommés */
    int  (*execute_cycles)(void* impl, int cycles);            /**< Boucle jusqu'à `cycles` cycles cumulés */
    void (*nmi)(void* impl);                                   /**< Demande NMI (front) */
    void (*irq_set)(void* impl, cpu_irq_source_t src);         /**< Asserter source IRQ (niveau) */
    void (*irq_clear)(void* impl, cpu_irq_source_t src);       /**< Désasserter source IRQ */
} cpu_core_vtable_t;

/** Vtable du cœur 6502 actuel. */
extern const cpu_core_vtable_t cpu_core_vtable_6502;

/**
 * @brief Sélectionne la vtable correspondant à un cpu_kind_t.
 * @return NULL si le kind n'est pas (encore) supporté.
 */
const cpu_core_vtable_t* cpu_core_vtable_for(cpu_kind_t kind);

/**
 * @brief Parse une chaîne CLI ("6502" ou "65c816"/"65C816") en cpu_kind_t.
 * @return false si la chaîne est inconnue (out non modifié).
 */
bool cpu_core_kind_from_string(const char* s, cpu_kind_t* out);

#endif /* CPU_CORE_H */
