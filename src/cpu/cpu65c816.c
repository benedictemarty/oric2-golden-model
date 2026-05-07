/**
 * @file cpu65c816.c
 * @brief WDC 65C816 — squelette (jalon B1.2, projet Oric 2)
 * @author bmarty <bmarty@mailo.com>
 * @date 2026-05-07
 *
 * Init/reset opérationnels. Pas encore d'instructions : `cpu816_step()`
 * et `cpu816_execute_cycles()` retournent une erreur. XCE et les opcodes
 * arrivent en B1.3.
 *
 * Le bus mémoire reste 16 bits (bank 0 implicite via `memory_t`) ; en
 * mode émulation PBR et DBR sont forcés à 0 et n'ont pas d'effet
 * observable. L'extension 24 bits sera introduite au plus tôt en B1.7.
 */

#include <string.h>

#include "cpu/cpu65c816.h"
#include "memory/memory.h"
#include "utils/logging.h"

/* ─── Init / Reset ──────────────────────────────────────────────────── */

void cpu816_init(cpu65c816_t* cpu, memory_t* memory) {
    if (!cpu) return;
    memset(cpu, 0, sizeof(*cpu));
    cpu->memory = memory;
    /* L'état "post-init" reflète un CPU non encore reseté. Les valeurs
     * effectives sont posées par cpu816_reset(), qui est invoqué ensuite
     * par la boucle d'émulation (équivalent du signal RES). */
}

/**
 * Reset du 65C816 (signal RES).
 *
 * D'après la datasheet WDC W65C816S §6 :
 *   - E=1 (forcé en mode émulation, identique 6502)
 *   - M=1, X=1 (registres 8 bits ; tenus en mode émulation)
 *   - D=0, DBR=0, PBR=0
 *   - S high byte = $01 (verrouillé en page stack en mode E)
 *   - I=1 (IRQ masquée), D (decimal) = 0
 *   - PC chargé depuis le vecteur RESET en $00FFFC/$00FFFD
 *
 * Pour rester aligné sur le cœur 6502 existant (vecteur RESET commun et
 * P initial 0x34), on retient P = 0x34 (FLAG_INTERRUPT | FLAG_BREAK |
 * FLAG_UNUSED) en mode émulation. C'est conforme au 6502 Phosphoric.
 */
void cpu816_reset(cpu65c816_t* cpu) {
    if (!cpu || !cpu->memory) return;

    cpu->E   = true;            /* Reset force toujours le mode émulation */
    cpu->C   = 0;
    cpu->X   = 0;
    cpu->Y   = 0;
    cpu->D   = 0;
    cpu->DBR = 0;
    cpu->PBR = 0;
    cpu->S   = 0x01FF;          /* Stack en page 1, top */
    /* P : NV1BDIZC. M et X bits implicites en mode E (always 1, used as
     * B and unused bits 1). On positionne I=1, B=1, U=1 comme le 6502. */
    cpu->P   = FLAG_INTERRUPT | FLAG_BREAK | FLAG_UNUSED;

    /* PC depuis le vecteur RESET en bank 0. Le 65C816 lit toujours bank 0
     * pour les vecteurs ; en mode E ils sont aux mêmes adresses qu'un 6502. */
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

/* ─── Exécution (B1.3+) ─────────────────────────────────────────────── */

int cpu816_step(cpu65c816_t* cpu) {
    /* B1.2 : squelette uniquement. Aucune instruction n'est décodée.
     * Une tentative d'exécution est une erreur de programmation appelante. */
    (void)cpu;
    log_error("cpu816_step: 65C816 core has no opcodes yet (jalon B1.3+). "
              "Use --cpu 6502 to run a program.");
    return -1;
}

int cpu816_execute_cycles(cpu65c816_t* cpu, int cycles) {
    (void)cpu;
    (void)cycles;
    log_error("cpu816_execute_cycles: 65C816 core not yet executable (jalon B1.3+).");
    return -1;
}

/* ─── Interruptions (modèle partagé avec le 6502) ───────────────────── */

void cpu816_nmi(cpu65c816_t* cpu) {
    if (!cpu) return;
    cpu->nmi_pending = true;
    cpu->waiting = false; /* WAI sort sur NMI */
}

void cpu816_irq_set(cpu65c816_t* cpu, cpu_irq_source_t source) {
    if (!cpu) return;
    cpu->irq |= (uint8_t)source;
    cpu->waiting = false; /* WAI sort sur IRQ même si I=1 */
}

void cpu816_irq_clear(cpu65c816_t* cpu, cpu_irq_source_t source) {
    if (!cpu) return;
    cpu->irq &= (uint8_t)~source;
}
