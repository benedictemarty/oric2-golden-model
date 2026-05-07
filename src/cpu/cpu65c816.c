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

/* ─── Exécution ─────────────────────────────────────────────────────── */

/**
 * @brief Lit un octet à PBR:PC et incrémente PC (wraparound 16 bits dans le bank).
 *
 * En mode E ou avec PBR=0 (cas B1.3), l'accès est équivalent à un fetch 6502.
 * L'extension 24 bits (utilisation effective de PBR) viendra avec le bus
 * 24 bits en B1.7+.
 */
static uint8_t fetch_pc_byte(cpu65c816_t* cpu) {
    uint8_t v = memory_read(cpu->memory, cpu->PC);
    cpu->PC = (uint16_t)(cpu->PC + 1);
    return v;
}

/**
 * @brief Force l'invariant mode E : M=X=1, X.high=Y.high=0, S.high=$01.
 *
 * Appelé après une transition vers E=1 (au reset et sur XCE C→E).
 * D'après la datasheet WDC W65C816S §2.20, le passage en émulation force
 * les bits M et X à 1 et tronque les registres index ainsi que le high
 * byte du stack pointer.
 */
static void enter_emulation_mode(cpu65c816_t* cpu) {
    cpu->P |= (uint8_t)(FLAG816_M_MEM | FLAG816_X_INDEX);
    cpu->X &= 0x00FF;
    cpu->Y &= 0x00FF;
    cpu->S = (uint16_t)(0x0100 | (cpu->S & 0x00FF));
}

/**
 * @brief XCE — Exchange Carry and Emulation flags (opcode 0xFB, 2 cycles).
 *
 * (E, C) ↔ (C, E). Si la nouvelle valeur de E vaut 1, on applique
 * `enter_emulation_mode()` pour rétablir les invariants 6502.
 */
static void op_xce(cpu65c816_t* cpu) {
    bool old_C = (cpu->P & FLAG_CARRY) != 0;
    bool old_E = cpu->E;
    cpu->E = old_C;
    if (old_E) cpu->P |= FLAG_CARRY; else cpu->P &= (uint8_t)~FLAG_CARRY;
    if (cpu->E) enter_emulation_mode(cpu);
}

int cpu816_step(cpu65c816_t* cpu) {
    if (!cpu || !cpu->memory) return -1;

    uint8_t op = fetch_pc_byte(cpu);
    int cycles = 0;

    switch (op) {
        /* Flag manipulation (implicit, 2 cycles) — identique 6502 */
        case 0x18: cpu->P &= (uint8_t)~FLAG_CARRY;     cycles = 2; break; /* CLC */
        case 0x38: cpu->P |=        FLAG_CARRY;        cycles = 2; break; /* SEC */
        case 0x58: cpu->P &= (uint8_t)~FLAG_INTERRUPT; cycles = 2; break; /* CLI */
        case 0x78: cpu->P |=        FLAG_INTERRUPT;    cycles = 2; break; /* SEI */
        case 0xB8: cpu->P &= (uint8_t)~FLAG_OVERFLOW;  cycles = 2; break; /* CLV */
        case 0xD8: cpu->P &= (uint8_t)~FLAG_DECIMAL;   cycles = 2; break; /* CLD */
        case 0xF8: cpu->P |=        FLAG_DECIMAL;      cycles = 2; break; /* SED */

        /* Mode switch */
        case 0xFB: op_xce(cpu); cycles = 2; break;                        /* XCE */

        default:
            /* B1.3 : seul XCE et les flag instructions sont décodés. Les
             * opcodes 6502-équivalents arrivent en B1.4. */
            log_error("cpu816_step: unimplemented opcode $%02X at PC=$%04X "
                      "(jalon B1.4 livrera les opcodes 6502-équivalents).",
                      op, (cpu->PC - 1) & 0xFFFF);
            cpu->PC = (uint16_t)(cpu->PC - 1); /* annule l'avance pour aider le debug */
            return -1;
    }

    cpu->cycles += (uint64_t)cycles;
    return cycles;
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
