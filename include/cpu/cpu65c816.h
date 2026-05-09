/**
 * @file cpu65c816.h
 * @brief WDC 65C816 CPU core (squelette B1.2, projet Oric 2)
 * @author bmarty <bmarty@mailo.com>
 * @date 2026-05-07
 *
 * Cœur 65C816 destiné à devenir le CPU de l'Oric 2. En mode émulation (E=1)
 * il doit reproduire le comportement 6502 attendu par les logiciels Oric 1
 * existants (cf. ADR-10) ; en mode natif (E=0) il offre les registres 16 bits,
 * le banking et les vecteurs étendus exigés par OricOS (cf. ADR-01).
 *
 * **B1.2 (squelette)** : structure complète des registres, init et reset
 * fonctionnels (vecteur RESET en mode émulation = $00FFFC). Aucune
 * instruction n'est encore décodée — toute tentative d'exécuter `step()` ou
 * `execute_cycles()` produit une erreur. XCE et les opcodes arrivent en B1.3+.
 *
 * Sémantique exacte du mode E : ouverte (ADR-11), à trancher avant B1.4.
 */

#ifndef CPU_65C816_H
#define CPU_65C816_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#include "cpu/cpu_types.h"  /* memory_t, cpu_irq_source_t, cpu_flags_t neutres */

/**
 * @brief Flags additionnels propres au 65C816 (mode natif).
 *
 * En mode émulation (E=1), les bits 4 et 5 jouent le rôle 6502 (B et 1).
 * En mode natif (E=0), le bit 4 devient X (index width) et le bit 5
 * devient M (memory/accumulator width). 1 = 8 bits, 0 = 16 bits.
 */
typedef enum {
    FLAG816_X_INDEX = 0x10,  /**< Index register width (mode natif). 1 = 8b */
    FLAG816_M_MEM   = 0x20   /**< Memory/accumulator width (mode natif). 1 = 8b */
} cpu816_native_flags_t;

/**
 * @brief État du cœur 65C816.
 *
 * En mode E, A_full est utilisé en byte bas (champ A équivalent), X/Y
 * comme 8 bits avec hauts à 0, S verrouillé en page 1 ($01xx). En mode N,
 * tous les registres ont leur largeur effective contrôlée par E/M/X.
 */
typedef struct {
    /* Registres 16 bits (visibles différemment selon E/M/X) */
    uint16_t C;     /**< Accumulator complet (B<<8 | A). En mode E ou M=1, seul A (low) est utilisé. */
    uint16_t X;     /**< Index X (16 bits si X=0, 8 bits si X=1) */
    uint16_t Y;     /**< Index Y (idem) */
    uint16_t S;     /**< Stack pointer 16 bits ; en mode E forcé à $01xx */
    uint16_t D;     /**< Direct page register (Direct Page Relocation) */
    uint16_t PC;    /**< Program counter 16 bits (offset dans le bank PBR) */
    uint8_t  DBR;   /**< Data Bank Register */
    uint8_t  PBR;   /**< Program Bank Register */
    uint8_t  P;     /**< Processor status (NVMXDIZC en natif, NV1BDIZC en émulation) */
    bool     E;     /**< Emulation flag (true = mode E ; reset force E=1) */

    /* Comptage cycles (compatibles 6502 pour l'orchestration) */
    uint64_t cycles;
    uint32_t cycles_left;

    /* Interruptions (modèle level-triggered partagé avec le 6502) */
    bool     halted;
    bool     stopped;       /**< STP : arrêt total jusqu'au reset */
    bool     waiting;       /**< WAI : attente d'IRQ/NMI */
    bool     nmi_pending;
    uint8_t  irq;           /**< Bitfield cpu_irq_source_t */

    memory_t* memory;       /**< Bus mémoire ; B1.2 reste 16 bits (bank 0) */
} cpu65c816_t;

/* ─── API publique ──────────────────────────────────────────────────── */

void cpu816_init(cpu65c816_t* cpu, memory_t* memory);
void cpu816_reset(cpu65c816_t* cpu);
int  cpu816_step(cpu65c816_t* cpu);
int  cpu816_execute_cycles(cpu65c816_t* cpu, int cycles);
void cpu816_nmi(cpu65c816_t* cpu);
void cpu816_irq_set(cpu65c816_t* cpu, cpu_irq_source_t source);
void cpu816_irq_clear(cpu65c816_t* cpu, cpu_irq_source_t source);

/**
 * @brief Format état CPU 65C816 en string (debugger).
 *
 * Affiche tous les registres natifs : C (16-bit acc), X/Y (16-bit indices),
 * S (16-bit stack), D (direct page), DBR/PBR (data/program bank), P (flags).
 * En mode E, le format reflète NV1BDIZC ; en mode N, NVMXDIZC + e flag.
 */
void cpu816_get_state_string(const cpu65c816_t* cpu, char* buffer, size_t buffer_size);

/**
 * @brief true si le cœur est en mode émulation (E=1).
 */
static inline bool cpu816_is_emulation(const cpu65c816_t* cpu) {
    return cpu->E;
}

#endif /* CPU_65C816_H */
