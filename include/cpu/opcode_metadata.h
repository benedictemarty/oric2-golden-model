/**
 * @file opcode_metadata.h
 * @brief Métadonnées 256 opcodes 6502 (cycles, size, addressing mode, mnémonique)
 * @author bmarty <bmarty@mailo.com>
 * @date 2026-05-09
 *
 * Table neutre extraite de `opcodes.c` (cœur 6502 historique) en PH-2.c.2
 * (ADR-18 étape 1.C). Consommée à la fois par :
 *   - le cœur 6502 (`opcodes.c`, `cpu_execute_opcode`),
 *   - le cœur 65C816 (`cpu65c816_opcodes.c`, pour `cycles` et `size` partagés
 *     en mode E),
 *   - le debugger / disassembler (utilise `name` et `mode`).
 *
 * L'extraction permet de retirer `opcodes.c` du cœur 6502 sans casser le
 * cœur 65C816, qui ne dépendait du fichier que pour cette table.
 */

#ifndef OPCODE_METADATA_H
#define OPCODE_METADATA_H

#include <stdint.h>

/**
 * @brief Modes d'adressage 6502 / 65C816 mode E.
 *
 * Historiquement défini dans `cpu6502.h`, déplacé ici pour neutralité.
 */
typedef enum {
    ADDR_IMPLICIT,           /**< Implicit (no operand) */
    ADDR_ACCUMULATOR,        /**< Accumulator */
    ADDR_IMMEDIATE,          /**< Immediate (#$nn) */
    ADDR_ZERO_PAGE,          /**< Zero Page ($nn) */
    ADDR_ZERO_PAGE_X,        /**< Zero Page,X ($nn,X) */
    ADDR_ZERO_PAGE_Y,        /**< Zero Page,Y ($nn,Y) */
    ADDR_RELATIVE,           /**< Relative (branch) */
    ADDR_ABSOLUTE,           /**< Absolute ($nnnn) */
    ADDR_ABSOLUTE_X,         /**< Absolute,X ($nnnn,X) */
    ADDR_ABSOLUTE_Y,         /**< Absolute,Y ($nnnn,Y) */
    ADDR_INDIRECT,           /**< Indirect (JMP) */
    ADDR_INDEXED_INDIRECT,   /**< Indexed Indirect ($nn,X) */
    ADDR_INDIRECT_INDEXED    /**< Indirect Indexed ($nn),Y */
} addressing_mode_t;

/**
 * @brief Métadonnées d'un opcode (déclaratif, pas d'effet d'exécution).
 *
 * - `name`     : mnémonique 3-4 lettres (e.g. "LDA", "BRK", "???" pour les
 *                opcodes illégaux NMOS traités comme NOP, cf. ADR-11).
 * - `cycles`   : durée de base de l'opcode (cycles 6502 / 65C816 mode E).
 * - `size`     : taille en octets de l'instruction (1, 2, ou 3).
 * - `mode`     : mode d'adressage de l'opérande.
 */
typedef struct opcode_info_s {
    const char* name;
    uint8_t cycles;
    uint8_t size;
    addressing_mode_t mode;
} opcode_info_t;

/**
 * @brief Table des 256 opcodes 6502 / 65C816 mode E.
 *
 * Indexée par opcode 0x00..0xFF. Définie dans `src/cpu/opcode_metadata.c`.
 */
extern const opcode_info_t opcode_table[256];

#endif /* OPCODE_METADATA_H */
