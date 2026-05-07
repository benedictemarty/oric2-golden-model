/**
 * @file cpu65c816.c
 * @brief WDC 65C816 — orchestration (init, reset, step, interrupts)
 * @author bmarty <bmarty@mailo.com>
 * @date 2026-05-07
 *
 * Le décodage d'opcodes en mode émulation est dans `cpu65c816_opcodes.c`.
 * Le mode natif (E=0) est non encore exécutable (jalon B1.7+).
 */

#include <string.h>

#include "cpu/cpu65c816.h"
#include "cpu/cpu_internal.h"  /* opcode_table partagé pour la taille d'opérande */
#include "memory/memory.h"
#include "utils/logging.h"

/* Définis dans cpu65c816_opcodes.c — duplication non recommandée. */
extern uint8_t cpu816_mem_read(cpu65c816_t* cpu, uint16_t addr);
extern void    cpu816_push(cpu65c816_t* cpu, uint8_t val);
extern void    cpu816_push_word(cpu65c816_t* cpu, uint16_t val);
extern int     cpu816_execute_opcode_e(cpu65c816_t* cpu, uint8_t opcode);

/* ─── Init / Reset ──────────────────────────────────────────────────── */

void cpu816_init(cpu65c816_t* cpu, memory_t* memory) {
    if (!cpu) return;
    memset(cpu, 0, sizeof(*cpu));
    cpu->memory = memory;
}

/**
 * Reset 65C816 (signal RES). Conforme datasheet WDC W65C816S §6 :
 * E=1 forcé, M=X=1, D=0, DBR=PBR=0, S high=$01 (page stack mode E),
 * I=1, D=0, PC chargé depuis le vecteur RESET en bank 0 ($00FFFC).
 *
 * En mode émulation P réplique le P du 6502 Phosphoric (bits B et UNUSED
 * forcés) pour aligner les transitions B1.4 sur le golden model.
 */
void cpu816_reset(cpu65c816_t* cpu) {
    if (!cpu || !cpu->memory) return;

    cpu->E   = true;
    cpu->C   = 0;
    cpu->X   = 0;
    cpu->Y   = 0;
    cpu->D   = 0;
    cpu->DBR = 0;
    cpu->PBR = 0;
    cpu->S   = 0x01FF;
    cpu->P   = (uint8_t)(FLAG_INTERRUPT | FLAG_BREAK | FLAG_UNUSED);

    uint8_t lo = memory_read(cpu->memory, 0xFFFC);
    uint8_t hi = memory_read(cpu->memory, 0xFFFD);
    cpu->PC = (uint16_t)(lo | (hi << 8));

    cpu->cycles      = 0;
    cpu->cycles_left = 0;
    cpu->halted      = false;
    cpu->stopped     = false;
    cpu->waiting     = false;
    cpu->nmi_pending = false;
    cpu->irq         = 0;
}

/* ─── Interruptions ─────────────────────────────────────────────────── */

/**
 * Service d'une interruption en mode émulation. Le bit B sur la pile
 * distingue NMI/IRQ (B=0, comme 6502) de BRK (B=1, géré dans BRK).
 * I est forcé à 1 pour empêcher la réentrance.
 *
 * Vecteurs en mode E (identiques aux adresses 6502) :
 *   NMI : $FFFA / $FFFB
 *   IRQ : $FFFE / $FFFF (partagé avec BRK, le bit B distingue)
 */
static void handle_irq_or_nmi(cpu65c816_t* cpu, uint16_t vector) {
    cpu816_push_word(cpu, cpu->PC);
    cpu816_push(cpu, (uint8_t)((cpu->P & ~FLAG_BREAK) | FLAG_UNUSED));
    cpu->P |= FLAG_INTERRUPT;
    uint8_t lo = cpu816_mem_read(cpu, vector);
    uint8_t hi = cpu816_mem_read(cpu, (uint16_t)(vector + 1));
    cpu->PC = (uint16_t)(lo | (hi << 8));
    cpu->waiting = false;
}

/* ─── step / execute_cycles ────────────────────────────────────────── */

int cpu816_step(cpu65c816_t* cpu) {
    if (!cpu || !cpu->memory) return -1;
    if (cpu->halted || cpu->stopped) return 0;

    /* WAI : reste figé jusqu'à NMI/IRQ. Les setters d'interruption
     * (cpu816_irq_set / cpu816_nmi) clearent waiting. */
    if (cpu->waiting && !cpu->nmi_pending && cpu->irq == 0) {
        return 2; /* Cycles bidons pour avancer le temps. */
    }

    /* NMI prioritaire ; IRQ ignoré si I=1 (level-triggered). */
    if (cpu->nmi_pending) {
        handle_irq_or_nmi(cpu, 0xFFFA);
        cpu->nmi_pending = false;
        cpu->cycles += 7;
        return 7;
    }
    if (cpu->irq != 0 && (cpu->P & FLAG_INTERRUPT) == 0) {
        handle_irq_or_nmi(cpu, 0xFFFE);
        cpu->cycles += 7;
        return 7;
    }

    /* B1.4 : seul le mode émulation est correctement décodé.
     * En mode natif (E=0), on dispatche tout de même via execute_opcode_e :
     *   - les opcodes triviaux (CLC/SEC/CLI/SEI/CLD/SED/CLV/XCE) sont
     *     mode-agnostiques et restent corrects ;
     *   - tout autre opcode est susceptible d'être faux côté largeur
     *     registres / banking. La sémantique mode N propre arrive en B1.7. */
    uint8_t opcode = cpu816_mem_read(cpu, cpu->PC);
    cpu->PC = (uint16_t)(cpu->PC + 1);
    int cyc = cpu816_execute_opcode_e(cpu, opcode);
    if (cyc < 0) return -1;
    cpu->cycles += (uint64_t)cyc;
    return cyc;
}

int cpu816_execute_cycles(cpu65c816_t* cpu, int cycles) {
    if (!cpu) return -1;
    int total = 0;
    while (total < cycles) {
        int n = cpu816_step(cpu);
        if (n < 0) return -1;
        total += n;
    }
    return total;
}

/* ─── Interruptions (modèle partagé avec le 6502) ───────────────────── */

void cpu816_nmi(cpu65c816_t* cpu) {
    if (!cpu) return;
    cpu->nmi_pending = true;
    cpu->waiting = false;
}

void cpu816_irq_set(cpu65c816_t* cpu, cpu_irq_source_t source) {
    if (!cpu) return;
    cpu->irq |= (uint8_t)source;
    cpu->waiting = false;
}

void cpu816_irq_clear(cpu65c816_t* cpu, cpu_irq_source_t source) {
    if (!cpu) return;
    cpu->irq &= (uint8_t)~source;
}
