/**
 * @file cpu65c816_opcodes.c
 * @brief 65C816 — opcodes 6502-équivalents en mode émulation (B1.4)
 * @author bmarty <bmarty@mailo.com>
 * @date 2026-05-07
 *
 * Port du dispatch d'opcodes 6502 vers le cœur 65C816, exécuté en mode E
 * (M=X=1, registres 8 bits effectifs, S verrouillé en page 1).
 *
 * Conventions de port (mode E) :
 *   cpu->A → low byte of cpu->C (high byte = B, préservé)
 *   cpu->X / cpu->Y → low byte (high byte forcé à 0)
 *   cpu->SP → low byte of cpu->S (high byte forcé à $01)
 *
 * Cycle counts identiques au cœur 6502 Phosphoric (ADR-10), incluant
 * page-cross penalties sur lectures absolu,X/Y et indirect,Y, ainsi que
 * sur branches taken.
 *
 * ADR-11 (hybride pragmatique) :
 *   - Bug `JMP ($xxFF)` reproduit dans `addr816_indirect()`.
 *   - Opcodes illégaux NMOS = NOP (default branch consomme leur taille).
 *   - $EB conservé comme alias SBC immediate.
 */

#include <string.h>

#include "cpu/cpu65c816.h"
#include "cpu/cpu_internal.h"  /* opcode_table[] partagé avec le 6502 */
#include "memory/memory.h"

/* ─── Accès registres 8 bits en mode E ──────────────────────────────── */

static inline uint8_t a8(const cpu65c816_t* c) { return (uint8_t)(c->C & 0xFF); }
static inline void   set_a8(cpu65c816_t* c, uint8_t v) { c->C = (uint16_t)((c->C & 0xFF00) | v); }
static inline uint8_t x8(const cpu65c816_t* c) { return (uint8_t)(c->X & 0xFF); }
static inline void   set_x8(cpu65c816_t* c, uint8_t v) { c->X = (uint16_t)v; }
static inline uint8_t y8(const cpu65c816_t* c) { return (uint8_t)(c->Y & 0xFF); }
static inline void   set_y8(cpu65c816_t* c, uint8_t v) { c->Y = (uint16_t)v; }
static inline uint8_t sp8(const cpu65c816_t* c) { return (uint8_t)(c->S & 0xFF); }
static inline void   set_sp8(cpu65c816_t* c, uint8_t v) { c->S = (uint16_t)(0x0100 | v); }

/* ─── Mémoire / fetch ───────────────────────────────────────────────── */

uint8_t cpu816_mem_read(cpu65c816_t* cpu, uint16_t addr) {
    return memory_read(cpu->memory, addr);
}

void cpu816_mem_write(cpu65c816_t* cpu, uint16_t addr, uint8_t val) {
    memory_write(cpu->memory, addr, val);
}

uint8_t cpu816_fetch_byte(cpu65c816_t* cpu) {
    uint8_t v = cpu816_mem_read(cpu, cpu->PC);
    cpu->PC = (uint16_t)(cpu->PC + 1);
    return v;
}

uint16_t cpu816_fetch_word_pc(cpu65c816_t* cpu) {
    uint8_t lo = cpu816_fetch_byte(cpu);
    uint8_t hi = cpu816_fetch_byte(cpu);
    return (uint16_t)((hi << 8) | lo);
}

/* ─── Modes d'adressage ─────────────────────────────────────────────── */

static uint16_t addr816_immediate(cpu65c816_t* cpu) {
    uint16_t a = cpu->PC;
    cpu->PC = (uint16_t)(cpu->PC + 1);
    return a;
}

static uint16_t addr816_zp(cpu65c816_t* cpu) {
    return (uint16_t)cpu816_fetch_byte(cpu);
}

static uint16_t addr816_zp_x(cpu65c816_t* cpu) {
    return (uint16_t)((cpu816_fetch_byte(cpu) + x8(cpu)) & 0xFF);
}

static uint16_t addr816_zp_y(cpu65c816_t* cpu) {
    return (uint16_t)((cpu816_fetch_byte(cpu) + y8(cpu)) & 0xFF);
}

static uint16_t addr816_abs(cpu65c816_t* cpu) {
    return cpu816_fetch_word_pc(cpu);
}

static uint16_t addr816_abs_x(cpu65c816_t* cpu, bool* page_crossed) {
    uint16_t base = cpu816_fetch_word_pc(cpu);
    uint16_t a = (uint16_t)(base + x8(cpu));
    if (page_crossed) *page_crossed = ((base & 0xFF00) != (a & 0xFF00));
    return a;
}

static uint16_t addr816_abs_y(cpu65c816_t* cpu, bool* page_crossed) {
    uint16_t base = cpu816_fetch_word_pc(cpu);
    uint16_t a = (uint16_t)(base + y8(cpu));
    if (page_crossed) *page_crossed = ((base & 0xFF00) != (a & 0xFF00));
    return a;
}

/**
 * @brief JMP ($xxxx) — reproduit le bug NMOS de page-wrap (ADR-11).
 *
 * Le high byte du pointeur ne franchit pas la frontière de page : si
 * l'adresse de l'octet bas du pointeur termine par $FF, le high byte
 * est lu à l'adresse `(ptr & 0xFF00) | 0x00` au lieu de `ptr + 1`.
 */
static uint16_t addr816_indirect(cpu65c816_t* cpu) {
    uint16_t ptr = cpu816_fetch_word_pc(cpu);
    uint8_t lo = cpu816_mem_read(cpu, ptr);
    uint16_t ptr_hi = (uint16_t)((ptr & 0xFF00) | ((ptr + 1) & 0x00FF));
    uint8_t hi = cpu816_mem_read(cpu, ptr_hi);
    return (uint16_t)((hi << 8) | lo);
}

static uint16_t addr816_indexed_indirect(cpu65c816_t* cpu) {
    uint8_t zpg = (uint8_t)((cpu816_fetch_byte(cpu) + x8(cpu)) & 0xFF);
    uint8_t lo = cpu816_mem_read(cpu, zpg);
    uint8_t hi = cpu816_mem_read(cpu, (uint8_t)((zpg + 1) & 0xFF));
    return (uint16_t)((hi << 8) | lo);
}

static uint16_t addr816_indirect_indexed(cpu65c816_t* cpu, bool* page_crossed) {
    uint8_t zpg = cpu816_fetch_byte(cpu);
    uint8_t lo = cpu816_mem_read(cpu, zpg);
    uint8_t hi = cpu816_mem_read(cpu, (uint8_t)((zpg + 1) & 0xFF));
    uint16_t base = (uint16_t)((hi << 8) | lo);
    uint16_t a = (uint16_t)(base + y8(cpu));
    if (page_crossed) *page_crossed = ((base & 0xFF00) != (a & 0xFF00));
    return a;
}

static uint16_t addr816_relative(cpu65c816_t* cpu) {
    int8_t offset = (int8_t)cpu816_fetch_byte(cpu);
    return (uint16_t)(cpu->PC + offset);
}

/* ─── Stack (mode E : page 1 forcée) ────────────────────────────────── */

void cpu816_push(cpu65c816_t* cpu, uint8_t val) {
    cpu816_mem_write(cpu, (uint16_t)(0x0100 | sp8(cpu)), val);
    set_sp8(cpu, (uint8_t)(sp8(cpu) - 1));
}

uint8_t cpu816_pull(cpu65c816_t* cpu) {
    set_sp8(cpu, (uint8_t)(sp8(cpu) + 1));
    return cpu816_mem_read(cpu, (uint16_t)(0x0100 | sp8(cpu)));
}

void cpu816_push_word(cpu65c816_t* cpu, uint16_t val) {
    cpu816_push(cpu, (uint8_t)(val >> 8));
    cpu816_push(cpu, (uint8_t)(val & 0xFF));
}

uint16_t cpu816_pull_word(cpu65c816_t* cpu) {
    uint8_t lo = cpu816_pull(cpu);
    uint8_t hi = cpu816_pull(cpu);
    return (uint16_t)((hi << 8) | lo);
}

/* ─── Helpers ALU ───────────────────────────────────────────────────── */

static inline void update_nz(cpu65c816_t* cpu, uint8_t v) {
    if (v == 0)  cpu->P |=  FLAG_ZERO;     else cpu->P &= (uint8_t)~FLAG_ZERO;
    if (v & 0x80) cpu->P |= FLAG_NEGATIVE; else cpu->P &= (uint8_t)~FLAG_NEGATIVE;
}

static inline bool flag(const cpu65c816_t* cpu, uint8_t f) { return (cpu->P & f) != 0; }
static inline void setf(cpu65c816_t* cpu, uint8_t f, bool v) {
    if (v) cpu->P |= f; else cpu->P &= (uint8_t)~f;
}

/* ADC / SBC / CMP — port direct du 6502 Phosphoric */
static void op_adc(cpu65c816_t* cpu, uint8_t val) {
    uint8_t A = a8(cpu);
    if (flag(cpu, FLAG_DECIMAL)) {
        uint16_t lo = (A & 0x0F) + (val & 0x0F) + (flag(cpu, FLAG_CARRY) ? 1 : 0);
        if (lo > 0x09) lo += 0x06;
        uint16_t hi = (A >> 4) + (val >> 4) + (lo > 0x0F ? 1 : 0);
        uint16_t bin = (uint16_t)A + (uint16_t)val + (flag(cpu, FLAG_CARRY) ? 1 : 0);
        setf(cpu, FLAG_ZERO, (uint8_t)bin == 0);
        setf(cpu, FLAG_NEGATIVE, (hi & 0x08) != 0);
        setf(cpu, FLAG_OVERFLOW, ((A ^ val) & 0x80) == 0 && ((A ^ (hi << 4)) & 0x80));
        if (hi > 0x09) hi += 0x06;
        setf(cpu, FLAG_CARRY, hi > 0x0F);
        set_a8(cpu, (uint8_t)((hi << 4) | (lo & 0x0F)));
    } else {
        uint16_t sum = (uint16_t)A + (uint16_t)val + (flag(cpu, FLAG_CARRY) ? 1 : 0);
        setf(cpu, FLAG_CARRY, sum > 0xFF);
        setf(cpu, FLAG_OVERFLOW, ((A ^ (uint8_t)sum) & (val ^ (uint8_t)sum) & 0x80) != 0);
        set_a8(cpu, (uint8_t)sum);
        update_nz(cpu, a8(cpu));
    }
}

static void op_sbc(cpu65c816_t* cpu, uint8_t val) {
    uint8_t A = a8(cpu);
    if (flag(cpu, FLAG_DECIMAL)) {
        uint16_t bin = (uint16_t)A - (uint16_t)val - (flag(cpu, FLAG_CARRY) ? 0 : 1);
        int16_t lo = (A & 0x0F) - (val & 0x0F) - (flag(cpu, FLAG_CARRY) ? 0 : 1);
        if (lo < 0) lo -= 0x06;
        int16_t hi = (A >> 4) - (val >> 4) - (lo < 0 ? 1 : 0);
        if (hi < 0) hi -= 0x06;
        setf(cpu, FLAG_ZERO, (uint8_t)bin == 0);
        setf(cpu, FLAG_NEGATIVE, (bin & 0x80) != 0);
        setf(cpu, FLAG_OVERFLOW, ((A ^ val) & (A ^ (uint8_t)bin) & 0x80) != 0);
        setf(cpu, FLAG_CARRY, bin < 0x100);
        set_a8(cpu, (uint8_t)((hi << 4) | (lo & 0x0F)));
    } else {
        uint16_t diff = (uint16_t)A - (uint16_t)val - (flag(cpu, FLAG_CARRY) ? 0 : 1);
        setf(cpu, FLAG_CARRY, diff < 0x100);
        setf(cpu, FLAG_OVERFLOW, ((A ^ val) & (A ^ (uint8_t)diff) & 0x80) != 0);
        set_a8(cpu, (uint8_t)diff);
        update_nz(cpu, a8(cpu));
    }
}

static void op_cmp(cpu65c816_t* cpu, uint8_t reg, uint8_t val) {
    uint8_t r = (uint8_t)(reg - val);
    setf(cpu, FLAG_CARRY, reg >= val);
    update_nz(cpu, r);
}

/* ─── Branches (page-cross penalty) ─────────────────────────────────── */

static int do_branch(cpu65c816_t* cpu, bool condition) {
    uint16_t target = addr816_relative(cpu);
    if (condition) {
        int extra = ((cpu->PC & 0xFF00) != (target & 0xFF00)) ? 2 : 1;
        cpu->PC = target;
        return extra;
    }
    return 0;
}

/* ═══════════════════════════════════════════════════════════════════ */
/*  Dispatch d'opcodes en mode émulation (E=1)                         */
/* ═══════════════════════════════════════════════════════════════════ */

int cpu816_execute_opcode_e(cpu65c816_t* cpu, uint8_t opcode) {
    int cycles = opcode_table[opcode].cycles;
    int extra = 0;
    bool page_crossed = false;
    uint16_t addr;
    uint8_t val, result;

    switch (opcode) {
    /* ─── LDA ─── */
    case 0xA9: val = cpu816_mem_read(cpu, addr816_immediate(cpu)); set_a8(cpu, val); update_nz(cpu, val); break;
    case 0xA5: val = cpu816_mem_read(cpu, addr816_zp(cpu)); set_a8(cpu, val); update_nz(cpu, val); break;
    case 0xB5: val = cpu816_mem_read(cpu, addr816_zp_x(cpu)); set_a8(cpu, val); update_nz(cpu, val); break;
    case 0xAD: val = cpu816_mem_read(cpu, addr816_abs(cpu)); set_a8(cpu, val); update_nz(cpu, val); break;
    case 0xBD: addr = addr816_abs_x(cpu, &page_crossed); val = cpu816_mem_read(cpu, addr); set_a8(cpu, val); update_nz(cpu, val); if(page_crossed) extra=1; break;
    case 0xB9: addr = addr816_abs_y(cpu, &page_crossed); val = cpu816_mem_read(cpu, addr); set_a8(cpu, val); update_nz(cpu, val); if(page_crossed) extra=1; break;
    case 0xA1: val = cpu816_mem_read(cpu, addr816_indexed_indirect(cpu)); set_a8(cpu, val); update_nz(cpu, val); break;
    case 0xB1: addr = addr816_indirect_indexed(cpu, &page_crossed); val = cpu816_mem_read(cpu, addr); set_a8(cpu, val); update_nz(cpu, val); if(page_crossed) extra=1; break;

    /* ─── LDX ─── */
    case 0xA2: val = cpu816_mem_read(cpu, addr816_immediate(cpu)); set_x8(cpu, val); update_nz(cpu, val); break;
    case 0xA6: val = cpu816_mem_read(cpu, addr816_zp(cpu)); set_x8(cpu, val); update_nz(cpu, val); break;
    case 0xB6: val = cpu816_mem_read(cpu, addr816_zp_y(cpu)); set_x8(cpu, val); update_nz(cpu, val); break;
    case 0xAE: val = cpu816_mem_read(cpu, addr816_abs(cpu)); set_x8(cpu, val); update_nz(cpu, val); break;
    case 0xBE: addr = addr816_abs_y(cpu, &page_crossed); val = cpu816_mem_read(cpu, addr); set_x8(cpu, val); update_nz(cpu, val); if(page_crossed) extra=1; break;

    /* ─── LDY ─── */
    case 0xA0: val = cpu816_mem_read(cpu, addr816_immediate(cpu)); set_y8(cpu, val); update_nz(cpu, val); break;
    case 0xA4: val = cpu816_mem_read(cpu, addr816_zp(cpu)); set_y8(cpu, val); update_nz(cpu, val); break;
    case 0xB4: val = cpu816_mem_read(cpu, addr816_zp_x(cpu)); set_y8(cpu, val); update_nz(cpu, val); break;
    case 0xAC: val = cpu816_mem_read(cpu, addr816_abs(cpu)); set_y8(cpu, val); update_nz(cpu, val); break;
    case 0xBC: addr = addr816_abs_x(cpu, &page_crossed); val = cpu816_mem_read(cpu, addr); set_y8(cpu, val); update_nz(cpu, val); if(page_crossed) extra=1; break;

    /* ─── STA ─── */
    case 0x85: cpu816_mem_write(cpu, addr816_zp(cpu), a8(cpu)); break;
    case 0x95: cpu816_mem_write(cpu, addr816_zp_x(cpu), a8(cpu)); break;
    case 0x8D: cpu816_mem_write(cpu, addr816_abs(cpu), a8(cpu)); break;
    case 0x9D: addr = addr816_abs_x(cpu, NULL); cpu816_mem_write(cpu, addr, a8(cpu)); break;
    case 0x99: addr = addr816_abs_y(cpu, NULL); cpu816_mem_write(cpu, addr, a8(cpu)); break;
    case 0x81: cpu816_mem_write(cpu, addr816_indexed_indirect(cpu), a8(cpu)); break;
    case 0x91: addr = addr816_indirect_indexed(cpu, NULL); cpu816_mem_write(cpu, addr, a8(cpu)); break;

    /* ─── STX ─── */
    case 0x86: cpu816_mem_write(cpu, addr816_zp(cpu), x8(cpu)); break;
    case 0x96: cpu816_mem_write(cpu, addr816_zp_y(cpu), x8(cpu)); break;
    case 0x8E: cpu816_mem_write(cpu, addr816_abs(cpu), x8(cpu)); break;

    /* ─── STY ─── */
    case 0x84: cpu816_mem_write(cpu, addr816_zp(cpu), y8(cpu)); break;
    case 0x94: cpu816_mem_write(cpu, addr816_zp_x(cpu), y8(cpu)); break;
    case 0x8C: cpu816_mem_write(cpu, addr816_abs(cpu), y8(cpu)); break;

    /* ─── ADC ─── */
    case 0x69: op_adc(cpu, cpu816_mem_read(cpu, addr816_immediate(cpu))); break;
    case 0x65: op_adc(cpu, cpu816_mem_read(cpu, addr816_zp(cpu))); break;
    case 0x75: op_adc(cpu, cpu816_mem_read(cpu, addr816_zp_x(cpu))); break;
    case 0x6D: op_adc(cpu, cpu816_mem_read(cpu, addr816_abs(cpu))); break;
    case 0x7D: addr = addr816_abs_x(cpu, &page_crossed); op_adc(cpu, cpu816_mem_read(cpu, addr)); if(page_crossed) extra=1; break;
    case 0x79: addr = addr816_abs_y(cpu, &page_crossed); op_adc(cpu, cpu816_mem_read(cpu, addr)); if(page_crossed) extra=1; break;
    case 0x61: op_adc(cpu, cpu816_mem_read(cpu, addr816_indexed_indirect(cpu))); break;
    case 0x71: addr = addr816_indirect_indexed(cpu, &page_crossed); op_adc(cpu, cpu816_mem_read(cpu, addr)); if(page_crossed) extra=1; break;

    /* ─── SBC ─── */
    case 0xE9: op_sbc(cpu, cpu816_mem_read(cpu, addr816_immediate(cpu))); break;
    case 0xEB: op_sbc(cpu, cpu816_mem_read(cpu, addr816_immediate(cpu))); break; /* alias officieux */
    case 0xE5: op_sbc(cpu, cpu816_mem_read(cpu, addr816_zp(cpu))); break;
    case 0xF5: op_sbc(cpu, cpu816_mem_read(cpu, addr816_zp_x(cpu))); break;
    case 0xED: op_sbc(cpu, cpu816_mem_read(cpu, addr816_abs(cpu))); break;
    case 0xFD: addr = addr816_abs_x(cpu, &page_crossed); op_sbc(cpu, cpu816_mem_read(cpu, addr)); if(page_crossed) extra=1; break;
    case 0xF9: addr = addr816_abs_y(cpu, &page_crossed); op_sbc(cpu, cpu816_mem_read(cpu, addr)); if(page_crossed) extra=1; break;
    case 0xE1: op_sbc(cpu, cpu816_mem_read(cpu, addr816_indexed_indirect(cpu))); break;
    case 0xF1: addr = addr816_indirect_indexed(cpu, &page_crossed); op_sbc(cpu, cpu816_mem_read(cpu, addr)); if(page_crossed) extra=1; break;

    /* ─── AND ─── */
    case 0x29: set_a8(cpu, a8(cpu) & cpu816_mem_read(cpu, addr816_immediate(cpu))); update_nz(cpu, a8(cpu)); break;
    case 0x25: set_a8(cpu, a8(cpu) & cpu816_mem_read(cpu, addr816_zp(cpu))); update_nz(cpu, a8(cpu)); break;
    case 0x35: set_a8(cpu, a8(cpu) & cpu816_mem_read(cpu, addr816_zp_x(cpu))); update_nz(cpu, a8(cpu)); break;
    case 0x2D: set_a8(cpu, a8(cpu) & cpu816_mem_read(cpu, addr816_abs(cpu))); update_nz(cpu, a8(cpu)); break;
    case 0x3D: addr = addr816_abs_x(cpu, &page_crossed); set_a8(cpu, a8(cpu) & cpu816_mem_read(cpu, addr)); update_nz(cpu, a8(cpu)); if(page_crossed) extra=1; break;
    case 0x39: addr = addr816_abs_y(cpu, &page_crossed); set_a8(cpu, a8(cpu) & cpu816_mem_read(cpu, addr)); update_nz(cpu, a8(cpu)); if(page_crossed) extra=1; break;
    case 0x21: set_a8(cpu, a8(cpu) & cpu816_mem_read(cpu, addr816_indexed_indirect(cpu))); update_nz(cpu, a8(cpu)); break;
    case 0x31: addr = addr816_indirect_indexed(cpu, &page_crossed); set_a8(cpu, a8(cpu) & cpu816_mem_read(cpu, addr)); update_nz(cpu, a8(cpu)); if(page_crossed) extra=1; break;

    /* ─── ORA ─── */
    case 0x09: set_a8(cpu, a8(cpu) | cpu816_mem_read(cpu, addr816_immediate(cpu))); update_nz(cpu, a8(cpu)); break;
    case 0x05: set_a8(cpu, a8(cpu) | cpu816_mem_read(cpu, addr816_zp(cpu))); update_nz(cpu, a8(cpu)); break;
    case 0x15: set_a8(cpu, a8(cpu) | cpu816_mem_read(cpu, addr816_zp_x(cpu))); update_nz(cpu, a8(cpu)); break;
    case 0x0D: set_a8(cpu, a8(cpu) | cpu816_mem_read(cpu, addr816_abs(cpu))); update_nz(cpu, a8(cpu)); break;
    case 0x1D: addr = addr816_abs_x(cpu, &page_crossed); set_a8(cpu, a8(cpu) | cpu816_mem_read(cpu, addr)); update_nz(cpu, a8(cpu)); if(page_crossed) extra=1; break;
    case 0x19: addr = addr816_abs_y(cpu, &page_crossed); set_a8(cpu, a8(cpu) | cpu816_mem_read(cpu, addr)); update_nz(cpu, a8(cpu)); if(page_crossed) extra=1; break;
    case 0x01: set_a8(cpu, a8(cpu) | cpu816_mem_read(cpu, addr816_indexed_indirect(cpu))); update_nz(cpu, a8(cpu)); break;
    case 0x11: addr = addr816_indirect_indexed(cpu, &page_crossed); set_a8(cpu, a8(cpu) | cpu816_mem_read(cpu, addr)); update_nz(cpu, a8(cpu)); if(page_crossed) extra=1; break;

    /* ─── EOR ─── */
    case 0x49: set_a8(cpu, a8(cpu) ^ cpu816_mem_read(cpu, addr816_immediate(cpu))); update_nz(cpu, a8(cpu)); break;
    case 0x45: set_a8(cpu, a8(cpu) ^ cpu816_mem_read(cpu, addr816_zp(cpu))); update_nz(cpu, a8(cpu)); break;
    case 0x55: set_a8(cpu, a8(cpu) ^ cpu816_mem_read(cpu, addr816_zp_x(cpu))); update_nz(cpu, a8(cpu)); break;
    case 0x4D: set_a8(cpu, a8(cpu) ^ cpu816_mem_read(cpu, addr816_abs(cpu))); update_nz(cpu, a8(cpu)); break;
    case 0x5D: addr = addr816_abs_x(cpu, &page_crossed); set_a8(cpu, a8(cpu) ^ cpu816_mem_read(cpu, addr)); update_nz(cpu, a8(cpu)); if(page_crossed) extra=1; break;
    case 0x59: addr = addr816_abs_y(cpu, &page_crossed); set_a8(cpu, a8(cpu) ^ cpu816_mem_read(cpu, addr)); update_nz(cpu, a8(cpu)); if(page_crossed) extra=1; break;
    case 0x41: set_a8(cpu, a8(cpu) ^ cpu816_mem_read(cpu, addr816_indexed_indirect(cpu))); update_nz(cpu, a8(cpu)); break;
    case 0x51: addr = addr816_indirect_indexed(cpu, &page_crossed); set_a8(cpu, a8(cpu) ^ cpu816_mem_read(cpu, addr)); update_nz(cpu, a8(cpu)); if(page_crossed) extra=1; break;

    /* ─── CMP ─── */
    case 0xC9: op_cmp(cpu, a8(cpu), cpu816_mem_read(cpu, addr816_immediate(cpu))); break;
    case 0xC5: op_cmp(cpu, a8(cpu), cpu816_mem_read(cpu, addr816_zp(cpu))); break;
    case 0xD5: op_cmp(cpu, a8(cpu), cpu816_mem_read(cpu, addr816_zp_x(cpu))); break;
    case 0xCD: op_cmp(cpu, a8(cpu), cpu816_mem_read(cpu, addr816_abs(cpu))); break;
    case 0xDD: addr = addr816_abs_x(cpu, &page_crossed); op_cmp(cpu, a8(cpu), cpu816_mem_read(cpu, addr)); if(page_crossed) extra=1; break;
    case 0xD9: addr = addr816_abs_y(cpu, &page_crossed); op_cmp(cpu, a8(cpu), cpu816_mem_read(cpu, addr)); if(page_crossed) extra=1; break;
    case 0xC1: op_cmp(cpu, a8(cpu), cpu816_mem_read(cpu, addr816_indexed_indirect(cpu))); break;
    case 0xD1: addr = addr816_indirect_indexed(cpu, &page_crossed); op_cmp(cpu, a8(cpu), cpu816_mem_read(cpu, addr)); if(page_crossed) extra=1; break;

    /* ─── CPX ─── */
    case 0xE0: op_cmp(cpu, x8(cpu), cpu816_mem_read(cpu, addr816_immediate(cpu))); break;
    case 0xE4: op_cmp(cpu, x8(cpu), cpu816_mem_read(cpu, addr816_zp(cpu))); break;
    case 0xEC: op_cmp(cpu, x8(cpu), cpu816_mem_read(cpu, addr816_abs(cpu))); break;

    /* ─── CPY ─── */
    case 0xC0: op_cmp(cpu, y8(cpu), cpu816_mem_read(cpu, addr816_immediate(cpu))); break;
    case 0xC4: op_cmp(cpu, y8(cpu), cpu816_mem_read(cpu, addr816_zp(cpu))); break;
    case 0xCC: op_cmp(cpu, y8(cpu), cpu816_mem_read(cpu, addr816_abs(cpu))); break;

    /* ─── BIT ─── */
    case 0x24: val = cpu816_mem_read(cpu, addr816_zp(cpu));
        setf(cpu, FLAG_ZERO, (a8(cpu) & val) == 0);
        setf(cpu, FLAG_OVERFLOW, (val & 0x40) != 0);
        setf(cpu, FLAG_NEGATIVE, (val & 0x80) != 0);
        break;
    case 0x2C: val = cpu816_mem_read(cpu, addr816_abs(cpu));
        setf(cpu, FLAG_ZERO, (a8(cpu) & val) == 0);
        setf(cpu, FLAG_OVERFLOW, (val & 0x40) != 0);
        setf(cpu, FLAG_NEGATIVE, (val & 0x80) != 0);
        break;

    /* ─── ASL ─── */
    case 0x0A:
        setf(cpu, FLAG_CARRY, (a8(cpu) & 0x80) != 0);
        set_a8(cpu, (uint8_t)(a8(cpu) << 1));
        update_nz(cpu, a8(cpu));
        break;
    case 0x06: addr = addr816_zp(cpu); val = cpu816_mem_read(cpu, addr);
        setf(cpu, FLAG_CARRY, (val & 0x80) != 0); result = (uint8_t)(val << 1);
        cpu816_mem_write(cpu, addr, result); update_nz(cpu, result); break;
    case 0x16: addr = addr816_zp_x(cpu); val = cpu816_mem_read(cpu, addr);
        setf(cpu, FLAG_CARRY, (val & 0x80) != 0); result = (uint8_t)(val << 1);
        cpu816_mem_write(cpu, addr, result); update_nz(cpu, result); break;
    case 0x0E: addr = addr816_abs(cpu); val = cpu816_mem_read(cpu, addr);
        setf(cpu, FLAG_CARRY, (val & 0x80) != 0); result = (uint8_t)(val << 1);
        cpu816_mem_write(cpu, addr, result); update_nz(cpu, result); break;
    case 0x1E: addr = addr816_abs_x(cpu, NULL); val = cpu816_mem_read(cpu, addr);
        setf(cpu, FLAG_CARRY, (val & 0x80) != 0); result = (uint8_t)(val << 1);
        cpu816_mem_write(cpu, addr, result); update_nz(cpu, result); break;

    /* ─── LSR ─── */
    case 0x4A:
        setf(cpu, FLAG_CARRY, (a8(cpu) & 0x01) != 0);
        set_a8(cpu, (uint8_t)(a8(cpu) >> 1));
        update_nz(cpu, a8(cpu));
        break;
    case 0x46: addr = addr816_zp(cpu); val = cpu816_mem_read(cpu, addr);
        setf(cpu, FLAG_CARRY, (val & 0x01) != 0); result = (uint8_t)(val >> 1);
        cpu816_mem_write(cpu, addr, result); update_nz(cpu, result); break;
    case 0x56: addr = addr816_zp_x(cpu); val = cpu816_mem_read(cpu, addr);
        setf(cpu, FLAG_CARRY, (val & 0x01) != 0); result = (uint8_t)(val >> 1);
        cpu816_mem_write(cpu, addr, result); update_nz(cpu, result); break;
    case 0x4E: addr = addr816_abs(cpu); val = cpu816_mem_read(cpu, addr);
        setf(cpu, FLAG_CARRY, (val & 0x01) != 0); result = (uint8_t)(val >> 1);
        cpu816_mem_write(cpu, addr, result); update_nz(cpu, result); break;
    case 0x5E: addr = addr816_abs_x(cpu, NULL); val = cpu816_mem_read(cpu, addr);
        setf(cpu, FLAG_CARRY, (val & 0x01) != 0); result = (uint8_t)(val >> 1);
        cpu816_mem_write(cpu, addr, result); update_nz(cpu, result); break;

    /* ─── ROL ─── */
    case 0x2A: {
        uint8_t c = flag(cpu, FLAG_CARRY) ? 1 : 0;
        setf(cpu, FLAG_CARRY, (a8(cpu) & 0x80) != 0);
        set_a8(cpu, (uint8_t)((a8(cpu) << 1) | c));
        update_nz(cpu, a8(cpu));
        break;
    }
    case 0x26: addr = addr816_zp(cpu); val = cpu816_mem_read(cpu, addr); {
        uint8_t c = flag(cpu, FLAG_CARRY) ? 1 : 0;
        setf(cpu, FLAG_CARRY, (val & 0x80) != 0);
        result = (uint8_t)((val << 1) | c);
        cpu816_mem_write(cpu, addr, result); update_nz(cpu, result);
    } break;
    case 0x36: addr = addr816_zp_x(cpu); val = cpu816_mem_read(cpu, addr); {
        uint8_t c = flag(cpu, FLAG_CARRY) ? 1 : 0;
        setf(cpu, FLAG_CARRY, (val & 0x80) != 0);
        result = (uint8_t)((val << 1) | c);
        cpu816_mem_write(cpu, addr, result); update_nz(cpu, result);
    } break;
    case 0x2E: addr = addr816_abs(cpu); val = cpu816_mem_read(cpu, addr); {
        uint8_t c = flag(cpu, FLAG_CARRY) ? 1 : 0;
        setf(cpu, FLAG_CARRY, (val & 0x80) != 0);
        result = (uint8_t)((val << 1) | c);
        cpu816_mem_write(cpu, addr, result); update_nz(cpu, result);
    } break;
    case 0x3E: addr = addr816_abs_x(cpu, NULL); val = cpu816_mem_read(cpu, addr); {
        uint8_t c = flag(cpu, FLAG_CARRY) ? 1 : 0;
        setf(cpu, FLAG_CARRY, (val & 0x80) != 0);
        result = (uint8_t)((val << 1) | c);
        cpu816_mem_write(cpu, addr, result); update_nz(cpu, result);
    } break;

    /* ─── ROR ─── */
    case 0x6A: {
        uint8_t c = flag(cpu, FLAG_CARRY) ? 0x80 : 0;
        setf(cpu, FLAG_CARRY, (a8(cpu) & 0x01) != 0);
        set_a8(cpu, (uint8_t)((a8(cpu) >> 1) | c));
        update_nz(cpu, a8(cpu));
        break;
    }
    case 0x66: addr = addr816_zp(cpu); val = cpu816_mem_read(cpu, addr); {
        uint8_t c = flag(cpu, FLAG_CARRY) ? 0x80 : 0;
        setf(cpu, FLAG_CARRY, (val & 0x01) != 0);
        result = (uint8_t)((val >> 1) | c);
        cpu816_mem_write(cpu, addr, result); update_nz(cpu, result);
    } break;
    case 0x76: addr = addr816_zp_x(cpu); val = cpu816_mem_read(cpu, addr); {
        uint8_t c = flag(cpu, FLAG_CARRY) ? 0x80 : 0;
        setf(cpu, FLAG_CARRY, (val & 0x01) != 0);
        result = (uint8_t)((val >> 1) | c);
        cpu816_mem_write(cpu, addr, result); update_nz(cpu, result);
    } break;
    case 0x6E: addr = addr816_abs(cpu); val = cpu816_mem_read(cpu, addr); {
        uint8_t c = flag(cpu, FLAG_CARRY) ? 0x80 : 0;
        setf(cpu, FLAG_CARRY, (val & 0x01) != 0);
        result = (uint8_t)((val >> 1) | c);
        cpu816_mem_write(cpu, addr, result); update_nz(cpu, result);
    } break;
    case 0x7E: addr = addr816_abs_x(cpu, NULL); val = cpu816_mem_read(cpu, addr); {
        uint8_t c = flag(cpu, FLAG_CARRY) ? 0x80 : 0;
        setf(cpu, FLAG_CARRY, (val & 0x01) != 0);
        result = (uint8_t)((val >> 1) | c);
        cpu816_mem_write(cpu, addr, result); update_nz(cpu, result);
    } break;

    /* ─── INC ─── */
    case 0xE6: addr = addr816_zp(cpu); result = (uint8_t)(cpu816_mem_read(cpu, addr) + 1); cpu816_mem_write(cpu, addr, result); update_nz(cpu, result); break;
    case 0xF6: addr = addr816_zp_x(cpu); result = (uint8_t)(cpu816_mem_read(cpu, addr) + 1); cpu816_mem_write(cpu, addr, result); update_nz(cpu, result); break;
    case 0xEE: addr = addr816_abs(cpu); result = (uint8_t)(cpu816_mem_read(cpu, addr) + 1); cpu816_mem_write(cpu, addr, result); update_nz(cpu, result); break;
    case 0xFE: addr = addr816_abs_x(cpu, NULL); result = (uint8_t)(cpu816_mem_read(cpu, addr) + 1); cpu816_mem_write(cpu, addr, result); update_nz(cpu, result); break;

    /* ─── DEC ─── */
    case 0xC6: addr = addr816_zp(cpu); result = (uint8_t)(cpu816_mem_read(cpu, addr) - 1); cpu816_mem_write(cpu, addr, result); update_nz(cpu, result); break;
    case 0xD6: addr = addr816_zp_x(cpu); result = (uint8_t)(cpu816_mem_read(cpu, addr) - 1); cpu816_mem_write(cpu, addr, result); update_nz(cpu, result); break;
    case 0xCE: addr = addr816_abs(cpu); result = (uint8_t)(cpu816_mem_read(cpu, addr) - 1); cpu816_mem_write(cpu, addr, result); update_nz(cpu, result); break;
    case 0xDE: addr = addr816_abs_x(cpu, NULL); result = (uint8_t)(cpu816_mem_read(cpu, addr) - 1); cpu816_mem_write(cpu, addr, result); update_nz(cpu, result); break;

    /* ─── INX/INY/DEX/DEY ─── */
    case 0xE8: set_x8(cpu, (uint8_t)(x8(cpu) + 1)); update_nz(cpu, x8(cpu)); break;
    case 0xC8: set_y8(cpu, (uint8_t)(y8(cpu) + 1)); update_nz(cpu, y8(cpu)); break;
    case 0xCA: set_x8(cpu, (uint8_t)(x8(cpu) - 1)); update_nz(cpu, x8(cpu)); break;
    case 0x88: set_y8(cpu, (uint8_t)(y8(cpu) - 1)); update_nz(cpu, y8(cpu)); break;

    /* ─── Transferts ─── */
    case 0xAA: set_x8(cpu, a8(cpu)); update_nz(cpu, x8(cpu)); break;  /* TAX */
    case 0x8A: set_a8(cpu, x8(cpu)); update_nz(cpu, a8(cpu)); break;  /* TXA */
    case 0xA8: set_y8(cpu, a8(cpu)); update_nz(cpu, y8(cpu)); break;  /* TAY */
    case 0x98: set_a8(cpu, y8(cpu)); update_nz(cpu, a8(cpu)); break;  /* TYA */
    case 0xBA: set_x8(cpu, sp8(cpu)); update_nz(cpu, x8(cpu)); break; /* TSX */
    case 0x9A: set_sp8(cpu, x8(cpu)); break;                          /* TXS */

    /* ─── Stack ─── */
    case 0x48: cpu816_push(cpu, a8(cpu)); break;                                                /* PHA */
    case 0x68: set_a8(cpu, cpu816_pull(cpu)); update_nz(cpu, a8(cpu)); break;                   /* PLA */
    case 0x08: cpu816_push(cpu, (uint8_t)(cpu->P | FLAG_BREAK | FLAG_UNUSED)); break;           /* PHP */
    case 0x28: cpu->P = (uint8_t)((cpu816_pull(cpu) & ~FLAG_BREAK) | FLAG_UNUSED); break;       /* PLP */

    /* ─── Branches ─── */
    case 0x10: extra = do_branch(cpu, !flag(cpu, FLAG_NEGATIVE)); break; /* BPL */
    case 0x30: extra = do_branch(cpu,  flag(cpu, FLAG_NEGATIVE)); break; /* BMI */
    case 0x50: extra = do_branch(cpu, !flag(cpu, FLAG_OVERFLOW)); break; /* BVC */
    case 0x70: extra = do_branch(cpu,  flag(cpu, FLAG_OVERFLOW)); break; /* BVS */
    case 0x90: extra = do_branch(cpu, !flag(cpu, FLAG_CARRY)); break;    /* BCC */
    case 0xB0: extra = do_branch(cpu,  flag(cpu, FLAG_CARRY)); break;    /* BCS */
    case 0xD0: extra = do_branch(cpu, !flag(cpu, FLAG_ZERO)); break;     /* BNE */
    case 0xF0: extra = do_branch(cpu,  flag(cpu, FLAG_ZERO)); break;     /* BEQ */

    /* ─── JMP ─── */
    case 0x4C: cpu->PC = addr816_abs(cpu); break;
    case 0x6C: cpu->PC = addr816_indirect(cpu); break;

    /* ─── JSR ─── */
    case 0x20:
        addr = addr816_abs(cpu);
        cpu816_push_word(cpu, (uint16_t)(cpu->PC - 1));
        cpu->PC = addr;
        break;

    /* ─── RTS ─── */
    case 0x60:
        cpu->PC = (uint16_t)(cpu816_pull_word(cpu) + 1);
        break;

    /* ─── RTI ─── */
    case 0x40:
        cpu->P = (uint8_t)((cpu816_pull(cpu) & ~FLAG_BREAK) | FLAG_UNUSED);
        cpu->PC = cpu816_pull_word(cpu);
        break;

    /* ─── BRK ─── */
    case 0x00:
        cpu->PC = (uint16_t)(cpu->PC + 1); /* BRK est suivi d'un signature byte */
        cpu816_push_word(cpu, cpu->PC);
        cpu816_push(cpu, (uint8_t)(cpu->P | FLAG_BREAK | FLAG_UNUSED));
        setf(cpu, FLAG_INTERRUPT, true);
        cpu->PC = (uint16_t)(cpu816_mem_read(cpu, 0xFFFE) | (cpu816_mem_read(cpu, 0xFFFF) << 8));
        break;

    /* ─── Flag instructions (déjà testées en B1.3) ─── */
    case 0x18: setf(cpu, FLAG_CARRY, false);    break; /* CLC */
    case 0x38: setf(cpu, FLAG_CARRY, true);     break; /* SEC */
    case 0x58: setf(cpu, FLAG_INTERRUPT, false); break; /* CLI */
    case 0x78: setf(cpu, FLAG_INTERRUPT, true);  break; /* SEI */
    case 0xD8: setf(cpu, FLAG_DECIMAL, false);   break; /* CLD */
    case 0xF8: setf(cpu, FLAG_DECIMAL, true);    break; /* SED */
    case 0xB8: setf(cpu, FLAG_OVERFLOW, false);  break; /* CLV */

    /* ─── XCE (déjà testé en B1.3, géré par cpu65c816.c::step pour la
     *     transition mode N→E ; ici on duplique l'effet pour cohérence). ─── */
    case 0xFB: {
        bool old_C = flag(cpu, FLAG_CARRY);
        bool old_E = cpu->E;
        cpu->E = old_C;
        setf(cpu, FLAG_CARRY, old_E);
        if (cpu->E) {
            cpu->P |= (uint8_t)(FLAG816_M_MEM | FLAG816_X_INDEX);
            cpu->X &= 0x00FF;
            cpu->Y &= 0x00FF;
            cpu->S = (uint16_t)(0x0100 | (cpu->S & 0x00FF));
        }
        break;
    }

    /* ─── NOP ─── */
    case 0xEA: break;

    /* ─── Opcodes illégaux NMOS — ADR-11(c) : NOP avec consommation de la
     *     taille d'opérande. Le 65C816 réel décode certains comme nouveaux
     *     opcodes ; en mode E hybride on suit le comportement Phosphoric. ─── */
    default: {
        uint8_t sz = opcode_table[opcode].size;
        for (uint8_t i = 1; i < sz; i++) (void)cpu816_fetch_byte(cpu);
        break;
    }
    }

    return cycles + extra;
}
