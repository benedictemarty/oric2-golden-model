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
#include "cpu/opcode_metadata.h"  /* opcode_table[] neutre */
#include "memory/memory.h"

/* Forward declarations (some helpers reference each other across sections). */
static inline bool M_is_8bit(const cpu65c816_t* c);
static inline bool X_is_8bit(const cpu65c816_t* c);

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
    /* B1.8 — fetch depuis PBR:PC. Bank 0 reste équivalent au comportement
     * antérieur (memory_read24 route vers memory_read pour bank 0). */
    uint8_t v = memory_read24(cpu->memory, ((uint32_t)cpu->PBR << 16) | cpu->PC);
    cpu->PC = (uint16_t)(cpu->PC + 1);
    return v;
}

uint16_t cpu816_fetch_word_pc(cpu65c816_t* cpu) {
    uint8_t lo = cpu816_fetch_byte(cpu);
    uint8_t hi = cpu816_fetch_byte(cpu);
    return (uint16_t)((hi << 8) | lo);
}

/* ─── Modes d'adressage ─────────────────────────────────────────────── */

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
    /* B1.7b fix : utilise X complet (8 ou 16 bits) selon flag X. */
    uint16_t idx = X_is_8bit(cpu) ? (uint16_t)(cpu->X & 0xFF) : cpu->X;
    uint16_t a = (uint16_t)(base + idx);
    if (page_crossed) *page_crossed = ((base & 0xFF00) != (a & 0xFF00));
    return a;
}

static uint16_t addr816_abs_y(cpu65c816_t* cpu, bool* page_crossed) {
    uint16_t base = cpu816_fetch_word_pc(cpu);
    uint16_t idx = X_is_8bit(cpu) ? (uint16_t)(cpu->Y & 0xFF) : cpu->Y;
    uint16_t a = (uint16_t)(base + idx);
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
    /* B1.7b fix : utilise Y complet (8 ou 16 bits) selon flag X. */
    uint16_t idx = X_is_8bit(cpu) ? (uint16_t)(cpu->Y & 0xFF) : cpu->Y;
    uint16_t a = (uint16_t)(base + idx);
    if (page_crossed) *page_crossed = ((base & 0xFF00) != (a & 0xFF00));
    return a;
}

static uint16_t addr816_relative(cpu65c816_t* cpu) {
    int8_t offset = (int8_t)cpu816_fetch_byte(cpu);
    return (uint16_t)(cpu->PC + offset);
}

/* ─── B1.8 — Modes d'adressage long 24-bit ──────────────────────────── */

/* fetch_long : fetch 3 bytes au PC, retourne 24-bit address */
static uint32_t fetch_long_pc(cpu65c816_t* cpu) {
    uint8_t lo = cpu816_fetch_byte(cpu);
    uint8_t hi = cpu816_fetch_byte(cpu);
    uint8_t bank = cpu816_fetch_byte(cpu);
    return ((uint32_t)bank << 16) | ((uint32_t)hi << 8) | lo;
}

/* Long absolute : $lllllll (24-bit address embedded in instruction) */
static uint32_t addr816_long(cpu65c816_t* cpu) {
    return fetch_long_pc(cpu);
}

/* Long absolute,X : $lllllll + X */
static uint32_t addr816_long_x(cpu65c816_t* cpu) {
    uint32_t base = fetch_long_pc(cpu);
    return (base + cpu->X) & 0xFFFFFFu;
}

/* DP indirect long [dp] : lit 3 bytes en dp[$nn..$nn+2] (bank 0 forcé) */
static uint32_t addr816_dp_indirect_long(cpu65c816_t* cpu) {
    uint16_t zpg = (uint16_t)cpu816_fetch_byte(cpu);
    uint16_t base = (uint16_t)(cpu->D + zpg);
    uint8_t lo = memory_read24(cpu->memory, base);
    uint8_t hi = memory_read24(cpu->memory, (uint16_t)(base + 1));
    uint8_t bk = memory_read24(cpu->memory, (uint16_t)(base + 2));
    return ((uint32_t)bk << 16) | ((uint32_t)hi << 8) | lo;
}

/* [dp],Y : indirect long indexed by Y */
static uint32_t addr816_dp_indirect_long_y(cpu65c816_t* cpu) {
    return (addr816_dp_indirect_long(cpu) + cpu->Y) & 0xFFFFFFu;
}

/* (dp) : DP indirect 16-bit (bank = DBR). Lit 2 bytes en dp[$nn..$nn+1] */
static uint32_t addr816_dp_indirect(cpu65c816_t* cpu) {
    uint16_t zpg = (uint16_t)cpu816_fetch_byte(cpu);
    uint16_t base = (uint16_t)(cpu->D + zpg);
    uint8_t lo = memory_read24(cpu->memory, base);
    uint8_t hi = memory_read24(cpu->memory, (uint16_t)(base + 1));
    uint16_t addr16 = (uint16_t)((hi << 8) | lo);
    return ((uint32_t)cpu->DBR << 16) | addr16;
}

/* Helpers de lecture/écriture 24-bit M-aware (1 ou 2 bytes selon M) */
static inline uint16_t read_M_24(cpu65c816_t* cpu, uint32_t addr24) {
    if (M_is_8bit(cpu)) return memory_read24(cpu->memory, addr24);
    uint8_t lo = memory_read24(cpu->memory, addr24);
    uint8_t hi = memory_read24(cpu->memory, (addr24 + 1) & 0xFFFFFFu);
    return (uint16_t)(lo | (hi << 8));
}
static inline void write_M_24(cpu65c816_t* cpu, uint32_t addr24, uint16_t val) {
    memory_write24(cpu->memory, addr24, (uint8_t)val);
    if (!M_is_8bit(cpu))
        memory_write24(cpu->memory, (addr24 + 1) & 0xFFFFFFu, (uint8_t)(val >> 8));
}

/* ─── Stack ──────────────────────────────────────────────────────────
 * Mode E : S verrouillé en page 1 ($01xx).
 * Mode N : S 16-bit, accès en bank 0 plein.
 * Cf. WDC W65C816S datasheet.
 */

void cpu816_push(cpu65c816_t* cpu, uint8_t val) {
    cpu816_mem_write(cpu, cpu->S, val);
    if (cpu->E) {
        /* Mode E : décrément seulement le low byte, high reste à $01. */
        cpu->S = (uint16_t)(0x0100 | ((cpu->S - 1) & 0xFF));
    } else {
        cpu->S = (uint16_t)(cpu->S - 1);
    }
}

uint8_t cpu816_pull(cpu65c816_t* cpu) {
    if (cpu->E) {
        cpu->S = (uint16_t)(0x0100 | ((cpu->S + 1) & 0xFF));
    } else {
        cpu->S = (uint16_t)(cpu->S + 1);
    }
    return cpu816_mem_read(cpu, cpu->S);
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

/* ─── Helpers ALU et width-awareness M/X (B1.7) ──────────────────────── */

static inline void update_nz(cpu65c816_t* cpu, uint8_t v) {
    if (v == 0)  cpu->P |=  FLAG_ZERO;     else cpu->P &= (uint8_t)~FLAG_ZERO;
    if (v & 0x80) cpu->P |= FLAG_NEGATIVE; else cpu->P &= (uint8_t)~FLAG_NEGATIVE;
}

static inline bool flag(const cpu65c816_t* cpu, uint8_t f) { return (cpu->P & f) != 0; }
static inline void setf(cpu65c816_t* cpu, uint8_t f, bool v) {
    if (v) cpu->P |= f; else cpu->P &= (uint8_t)~f;
}

/* B1.7 — width-awareness mode N
 * En mode E ou avec FLAG816_M_MEM=1, l'accumulateur est 8 bits.
 * En mode E ou avec FLAG816_X_INDEX=1, X et Y sont 8 bits.
 */
static inline bool M_is_8bit(const cpu65c816_t* c) {
    return c->E || (c->P & FLAG816_M_MEM);
}
static inline bool X_is_8bit(const cpu65c816_t* c) {
    return c->E || (c->P & FLAG816_X_INDEX);
}

/* Accumulateur (low byte ou full 16 bits selon M) */
static inline uint16_t a_get_M(const cpu65c816_t* c) {
    return M_is_8bit(c) ? (c->C & 0xFF) : c->C;
}
static inline void a_set_M(cpu65c816_t* c, uint16_t v) {
    if (M_is_8bit(c)) c->C = (uint16_t)((c->C & 0xFF00) | (v & 0xFF));
    else              c->C = v;
}

/* update N et Z selon la largeur courante de l'accumulateur. */
static inline void update_nz_M(cpu65c816_t* cpu, uint16_t v) {
    if (M_is_8bit(cpu)) { update_nz(cpu, (uint8_t)v); return; }
    cpu->P &= (uint8_t)~(FLAG_ZERO | FLAG_NEGATIVE);
    if (v == 0)        cpu->P |= FLAG_ZERO;
    if (v & 0x8000)    cpu->P |= FLAG_NEGATIVE;
}

/* Lecture/écriture mémoire 8 ou 16 bits selon M.
 * Les addr+1 wrap à 16 bits dans le bank courant (pas de propagation
 * vers DBR — strict bus 16-bit en B1.7, l'extension 24-bit est B1.8). */
static inline uint16_t read_M(cpu65c816_t* cpu, uint16_t addr) {
    if (M_is_8bit(cpu)) return cpu816_mem_read(cpu, addr);
    uint8_t lo = cpu816_mem_read(cpu, addr);
    uint8_t hi = cpu816_mem_read(cpu, (uint16_t)(addr + 1));
    return (uint16_t)(lo | (hi << 8));
}
static inline void write_M(cpu65c816_t* cpu, uint16_t addr, uint16_t val) {
    cpu816_mem_write(cpu, addr, (uint8_t)val);
    if (!M_is_8bit(cpu))
        cpu816_mem_write(cpu, (uint16_t)(addr + 1), (uint8_t)(val >> 8));
}

/* fetch immédiat selon largeur courante M (1 ou 2 bytes consommés au PC). */
static inline uint16_t fetch_imm_M(cpu65c816_t* cpu) {
    if (M_is_8bit(cpu)) return cpu816_fetch_byte(cpu);
    return cpu816_fetch_word_pc(cpu);
}

/* B1.7b — variantes X-aware (X et Y) ─────────────────────────────── */

static inline uint16_t x_get_X(const cpu65c816_t* c) {
    return X_is_8bit(c) ? (c->X & 0xFF) : c->X;
}
static inline void x_set_X(cpu65c816_t* c, uint16_t v) {
    if (X_is_8bit(c)) c->X = (uint16_t)(v & 0xFF); else c->X = v;
}
static inline uint16_t y_get_X(const cpu65c816_t* c) {
    return X_is_8bit(c) ? (c->Y & 0xFF) : c->Y;
}
static inline void y_set_X(cpu65c816_t* c, uint16_t v) {
    if (X_is_8bit(c)) c->Y = (uint16_t)(v & 0xFF); else c->Y = v;
}
static inline void update_nz_X(cpu65c816_t* cpu, uint16_t v) {
    if (X_is_8bit(cpu)) { update_nz(cpu, (uint8_t)v); return; }
    cpu->P &= (uint8_t)~(FLAG_ZERO | FLAG_NEGATIVE);
    if (v == 0)     cpu->P |= FLAG_ZERO;
    if (v & 0x8000) cpu->P |= FLAG_NEGATIVE;
}
static inline uint16_t read_X(cpu65c816_t* cpu, uint16_t addr) {
    if (X_is_8bit(cpu)) return cpu816_mem_read(cpu, addr);
    uint8_t lo = cpu816_mem_read(cpu, addr);
    uint8_t hi = cpu816_mem_read(cpu, (uint16_t)(addr + 1));
    return (uint16_t)(lo | (hi << 8));
}
static inline void write_X(cpu65c816_t* cpu, uint16_t addr, uint16_t val) {
    cpu816_mem_write(cpu, addr, (uint8_t)val);
    if (!X_is_8bit(cpu))
        cpu816_mem_write(cpu, (uint16_t)(addr + 1), (uint8_t)(val >> 8));
}
static inline uint16_t fetch_imm_X(cpu65c816_t* cpu) {
    if (X_is_8bit(cpu)) return cpu816_fetch_byte(cpu);
    return cpu816_fetch_word_pc(cpu);
}
static inline int X_extra_cycle(const cpu65c816_t* cpu) {
    return X_is_8bit(cpu) ? 0 : 1;
}
/* push/pull X-aware (pour PHX/PHY/PLX/PLY) */
static inline void push_X(cpu65c816_t* cpu, uint16_t val) {
    if (!X_is_8bit(cpu)) cpu816_push(cpu, (uint8_t)(val >> 8));
    cpu816_push(cpu, (uint8_t)val);
}
static inline uint16_t pull_X(cpu65c816_t* cpu) {
    uint8_t lo = cpu816_pull(cpu);
    if (X_is_8bit(cpu)) return lo;
    uint8_t hi = cpu816_pull(cpu);
    return (uint16_t)(lo | (hi << 8));
}

/* Penalty cycle ajouté quand M=0 sur un opcode read/write 16-bit. */
static inline int M_extra_cycle(const cpu65c816_t* cpu) {
    return M_is_8bit(cpu) ? 0 : 1;
}

/* Stack push/pull width-aware (M ou X selon contexte d'appel) */
static inline void push_M(cpu65c816_t* cpu, uint16_t val) {
    if (!M_is_8bit(cpu)) cpu816_push(cpu, (uint8_t)(val >> 8));
    cpu816_push(cpu, (uint8_t)val);
}
static inline uint16_t pull_M(cpu65c816_t* cpu) {
    uint8_t lo = cpu816_pull(cpu);
    if (M_is_8bit(cpu)) return lo;
    uint8_t hi = cpu816_pull(cpu);
    return (uint16_t)(lo | (hi << 8));
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

/* B1.7 — Versions width-aware (M flag) pour ALU accumulator ────────── */

static void op_adc_M(cpu65c816_t* cpu, uint16_t val) {
    if (M_is_8bit(cpu)) { op_adc(cpu, (uint8_t)val); return; }
    /* 16-bit binaire (BCD 16b non implémenté en B1.7 — rare ; tombe en
     * 8 bits si D=1 pour rester conservateur). */
    if (flag(cpu, FLAG_DECIMAL)) { op_adc(cpu, (uint8_t)val); return; }
    uint16_t A = cpu->C;
    uint32_t sum = (uint32_t)A + (uint32_t)val + (flag(cpu, FLAG_CARRY) ? 1u : 0u);
    setf(cpu, FLAG_CARRY, sum > 0xFFFF);
    uint16_t res = (uint16_t)sum;
    setf(cpu, FLAG_OVERFLOW, ((A ^ res) & (val ^ res) & 0x8000) != 0);
    cpu->C = res;
    update_nz_M(cpu, res);
}

static void op_sbc_M(cpu65c816_t* cpu, uint16_t val) {
    if (M_is_8bit(cpu)) { op_sbc(cpu, (uint8_t)val); return; }
    if (flag(cpu, FLAG_DECIMAL)) { op_sbc(cpu, (uint8_t)val); return; }
    uint16_t A = cpu->C;
    uint32_t diff = (uint32_t)A - (uint32_t)val - (flag(cpu, FLAG_CARRY) ? 0u : 1u);
    setf(cpu, FLAG_CARRY, diff < 0x10000);
    uint16_t res = (uint16_t)diff;
    setf(cpu, FLAG_OVERFLOW, ((A ^ val) & (A ^ res) & 0x8000) != 0);
    cpu->C = res;
    update_nz_M(cpu, res);
}

static void op_cmp_M(cpu65c816_t* cpu, uint16_t reg, uint16_t val) {
    if (M_is_8bit(cpu)) { op_cmp(cpu, (uint8_t)reg, (uint8_t)val); return; }
    uint16_t r = (uint16_t)(reg - val);
    setf(cpu, FLAG_CARRY, reg >= val);
    update_nz_M(cpu, r);
}

/* CMP X-aware (pour CPX/CPY). */
static void op_cmp_X(cpu65c816_t* cpu, uint16_t reg, uint16_t val) {
    if (X_is_8bit(cpu)) { op_cmp(cpu, (uint8_t)reg, (uint8_t)val); return; }
    uint16_t r = (uint16_t)(reg - val);
    setf(cpu, FLAG_CARRY, reg >= val);
    update_nz_X(cpu, r);
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
    uint16_t v16;

    switch (opcode) {
    /* ─── LDA (M-aware) ─── */
    case 0xA9: v16 = fetch_imm_M(cpu); a_set_M(cpu, v16); update_nz_M(cpu, v16); extra += M_extra_cycle(cpu); break;
    case 0xA5: v16 = read_M(cpu, addr816_zp(cpu)); a_set_M(cpu, v16); update_nz_M(cpu, v16); extra += M_extra_cycle(cpu); break;
    case 0xB5: v16 = read_M(cpu, addr816_zp_x(cpu)); a_set_M(cpu, v16); update_nz_M(cpu, v16); extra += M_extra_cycle(cpu); break;
    case 0xAD: v16 = read_M(cpu, addr816_abs(cpu)); a_set_M(cpu, v16); update_nz_M(cpu, v16); extra += M_extra_cycle(cpu); break;
    case 0xBD: addr = addr816_abs_x(cpu, &page_crossed); v16 = read_M(cpu, addr); a_set_M(cpu, v16); update_nz_M(cpu, v16); if(page_crossed) extra++; extra += M_extra_cycle(cpu); break;
    case 0xB9: addr = addr816_abs_y(cpu, &page_crossed); v16 = read_M(cpu, addr); a_set_M(cpu, v16); update_nz_M(cpu, v16); if(page_crossed) extra++; extra += M_extra_cycle(cpu); break;
    case 0xA1: v16 = read_M(cpu, addr816_indexed_indirect(cpu)); a_set_M(cpu, v16); update_nz_M(cpu, v16); extra += M_extra_cycle(cpu); break;
    case 0xB1: addr = addr816_indirect_indexed(cpu, &page_crossed); v16 = read_M(cpu, addr); a_set_M(cpu, v16); update_nz_M(cpu, v16); if(page_crossed) extra++; extra += M_extra_cycle(cpu); break;

    /* ─── B1.8 — LDA long addressing modes (NOP en mode E, ADR-11(c)) ─── */
    case 0xAF: if (cpu->E) { (void)fetch_long_pc(cpu); break; }                           /* LDA $lll */
        v16 = read_M_24(cpu, addr816_long(cpu)); a_set_M(cpu, v16); update_nz_M(cpu, v16);
        extra += M_extra_cycle(cpu); break;
    case 0xBF: if (cpu->E) { (void)fetch_long_pc(cpu); break; }                           /* LDA $lll,X */
        v16 = read_M_24(cpu, addr816_long_x(cpu)); a_set_M(cpu, v16); update_nz_M(cpu, v16);
        extra += M_extra_cycle(cpu); break;
    case 0xA7: if (cpu->E) { (void)cpu816_fetch_byte(cpu); break; }                       /* LDA [dp] */
        v16 = read_M_24(cpu, addr816_dp_indirect_long(cpu)); a_set_M(cpu, v16); update_nz_M(cpu, v16);
        extra += M_extra_cycle(cpu); break;
    case 0xB7: if (cpu->E) { (void)cpu816_fetch_byte(cpu); break; }                       /* LDA [dp],Y */
        v16 = read_M_24(cpu, addr816_dp_indirect_long_y(cpu)); a_set_M(cpu, v16); update_nz_M(cpu, v16);
        extra += M_extra_cycle(cpu); break;

    /* ─── B1.8 — STA long addressing modes ─── */
    case 0x8F: if (cpu->E) { (void)fetch_long_pc(cpu); break; }                           /* STA $lll */
        write_M_24(cpu, addr816_long(cpu), a_get_M(cpu)); extra += M_extra_cycle(cpu); break;
    case 0x9F: if (cpu->E) { (void)fetch_long_pc(cpu); break; }                           /* STA $lll,X */
        write_M_24(cpu, addr816_long_x(cpu), a_get_M(cpu)); extra += M_extra_cycle(cpu); break;
    case 0x87: if (cpu->E) { (void)cpu816_fetch_byte(cpu); break; }                       /* STA [dp] */
        write_M_24(cpu, addr816_dp_indirect_long(cpu), a_get_M(cpu)); extra += M_extra_cycle(cpu); break;
    case 0x97: if (cpu->E) { (void)cpu816_fetch_byte(cpu); break; }                       /* STA [dp],Y */
        write_M_24(cpu, addr816_dp_indirect_long_y(cpu), a_get_M(cpu)); extra += M_extra_cycle(cpu); break;

    /* ─── B1.8 — ALU long addressing (ADC/SBC/AND/ORA/EOR/CMP) ─── */
    case 0x6F: if (cpu->E) { (void)fetch_long_pc(cpu); break; }                           /* ADC $lll */
        op_adc_M(cpu, read_M_24(cpu, addr816_long(cpu))); extra += M_extra_cycle(cpu); break;
    case 0x7F: if (cpu->E) { (void)fetch_long_pc(cpu); break; }                           /* ADC $lll,X */
        op_adc_M(cpu, read_M_24(cpu, addr816_long_x(cpu))); extra += M_extra_cycle(cpu); break;
    case 0x67: if (cpu->E) { (void)cpu816_fetch_byte(cpu); break; }                       /* ADC [dp] */
        op_adc_M(cpu, read_M_24(cpu, addr816_dp_indirect_long(cpu))); extra += M_extra_cycle(cpu); break;
    case 0x77: if (cpu->E) { (void)cpu816_fetch_byte(cpu); break; }                       /* ADC [dp],Y */
        op_adc_M(cpu, read_M_24(cpu, addr816_dp_indirect_long_y(cpu))); extra += M_extra_cycle(cpu); break;

    case 0xEF: if (cpu->E) { (void)fetch_long_pc(cpu); break; }                           /* SBC $lll */
        op_sbc_M(cpu, read_M_24(cpu, addr816_long(cpu))); extra += M_extra_cycle(cpu); break;
    case 0xFF: if (cpu->E) { (void)fetch_long_pc(cpu); break; }                           /* SBC $lll,X */
        op_sbc_M(cpu, read_M_24(cpu, addr816_long_x(cpu))); extra += M_extra_cycle(cpu); break;
    case 0xE7: if (cpu->E) { (void)cpu816_fetch_byte(cpu); break; }                       /* SBC [dp] */
        op_sbc_M(cpu, read_M_24(cpu, addr816_dp_indirect_long(cpu))); extra += M_extra_cycle(cpu); break;
    case 0xF7: if (cpu->E) { (void)cpu816_fetch_byte(cpu); break; }                       /* SBC [dp],Y */
        op_sbc_M(cpu, read_M_24(cpu, addr816_dp_indirect_long_y(cpu))); extra += M_extra_cycle(cpu); break;

    case 0x2F: if (cpu->E) { (void)fetch_long_pc(cpu); break; }                           /* AND $lll */
        v16 = a_get_M(cpu) & read_M_24(cpu, addr816_long(cpu)); a_set_M(cpu, v16); update_nz_M(cpu, v16); extra += M_extra_cycle(cpu); break;
    case 0x3F: if (cpu->E) { (void)fetch_long_pc(cpu); break; }                           /* AND $lll,X */
        v16 = a_get_M(cpu) & read_M_24(cpu, addr816_long_x(cpu)); a_set_M(cpu, v16); update_nz_M(cpu, v16); extra += M_extra_cycle(cpu); break;
    case 0x27: if (cpu->E) { (void)cpu816_fetch_byte(cpu); break; }                       /* AND [dp] */
        v16 = a_get_M(cpu) & read_M_24(cpu, addr816_dp_indirect_long(cpu)); a_set_M(cpu, v16); update_nz_M(cpu, v16); extra += M_extra_cycle(cpu); break;
    case 0x37: if (cpu->E) { (void)cpu816_fetch_byte(cpu); break; }                       /* AND [dp],Y */
        v16 = a_get_M(cpu) & read_M_24(cpu, addr816_dp_indirect_long_y(cpu)); a_set_M(cpu, v16); update_nz_M(cpu, v16); extra += M_extra_cycle(cpu); break;

    case 0x0F: if (cpu->E) { (void)fetch_long_pc(cpu); break; }                           /* ORA $lll */
        v16 = a_get_M(cpu) | read_M_24(cpu, addr816_long(cpu)); a_set_M(cpu, v16); update_nz_M(cpu, v16); extra += M_extra_cycle(cpu); break;
    case 0x1F: if (cpu->E) { (void)fetch_long_pc(cpu); break; }                           /* ORA $lll,X */
        v16 = a_get_M(cpu) | read_M_24(cpu, addr816_long_x(cpu)); a_set_M(cpu, v16); update_nz_M(cpu, v16); extra += M_extra_cycle(cpu); break;
    case 0x07: if (cpu->E) { (void)cpu816_fetch_byte(cpu); break; }                       /* ORA [dp] */
        v16 = a_get_M(cpu) | read_M_24(cpu, addr816_dp_indirect_long(cpu)); a_set_M(cpu, v16); update_nz_M(cpu, v16); extra += M_extra_cycle(cpu); break;
    case 0x17: if (cpu->E) { (void)cpu816_fetch_byte(cpu); break; }                       /* ORA [dp],Y */
        v16 = a_get_M(cpu) | read_M_24(cpu, addr816_dp_indirect_long_y(cpu)); a_set_M(cpu, v16); update_nz_M(cpu, v16); extra += M_extra_cycle(cpu); break;

    case 0x4F: if (cpu->E) { (void)fetch_long_pc(cpu); break; }                           /* EOR $lll */
        v16 = a_get_M(cpu) ^ read_M_24(cpu, addr816_long(cpu)); a_set_M(cpu, v16); update_nz_M(cpu, v16); extra += M_extra_cycle(cpu); break;
    case 0x5F: if (cpu->E) { (void)fetch_long_pc(cpu); break; }                           /* EOR $lll,X */
        v16 = a_get_M(cpu) ^ read_M_24(cpu, addr816_long_x(cpu)); a_set_M(cpu, v16); update_nz_M(cpu, v16); extra += M_extra_cycle(cpu); break;
    case 0x47: if (cpu->E) { (void)cpu816_fetch_byte(cpu); break; }                       /* EOR [dp] */
        v16 = a_get_M(cpu) ^ read_M_24(cpu, addr816_dp_indirect_long(cpu)); a_set_M(cpu, v16); update_nz_M(cpu, v16); extra += M_extra_cycle(cpu); break;
    case 0x57: if (cpu->E) { (void)cpu816_fetch_byte(cpu); break; }                       /* EOR [dp],Y */
        v16 = a_get_M(cpu) ^ read_M_24(cpu, addr816_dp_indirect_long_y(cpu)); a_set_M(cpu, v16); update_nz_M(cpu, v16); extra += M_extra_cycle(cpu); break;

    case 0xCF: if (cpu->E) { (void)fetch_long_pc(cpu); break; }                           /* CMP $lll */
        op_cmp_M(cpu, a_get_M(cpu), read_M_24(cpu, addr816_long(cpu))); extra += M_extra_cycle(cpu); break;
    case 0xDF: if (cpu->E) { (void)fetch_long_pc(cpu); break; }                           /* CMP $lll,X */
        op_cmp_M(cpu, a_get_M(cpu), read_M_24(cpu, addr816_long_x(cpu))); extra += M_extra_cycle(cpu); break;
    case 0xC7: if (cpu->E) { (void)cpu816_fetch_byte(cpu); break; }                       /* CMP [dp] */
        op_cmp_M(cpu, a_get_M(cpu), read_M_24(cpu, addr816_dp_indirect_long(cpu))); extra += M_extra_cycle(cpu); break;
    case 0xD7: if (cpu->E) { (void)cpu816_fetch_byte(cpu); break; }                       /* CMP [dp],Y */
        op_cmp_M(cpu, a_get_M(cpu), read_M_24(cpu, addr816_dp_indirect_long_y(cpu))); extra += M_extra_cycle(cpu); break;

    /* ─── PH-fix-dp-indirect : 8 opcodes (dp) DP indirect 16-bit ─── */
    /* Pointer 16-bit en DP+dp/+1, addr finale en DBR:ptr (cf. WDC §A). */
    case 0x12: if (cpu->E) { (void)cpu816_fetch_byte(cpu); break; }                       /* ORA (dp) */
        v16 = a_get_M(cpu) | read_M_24(cpu, addr816_dp_indirect(cpu)); a_set_M(cpu, v16); update_nz_M(cpu, v16); extra += M_extra_cycle(cpu); break;
    case 0x32: if (cpu->E) { (void)cpu816_fetch_byte(cpu); break; }                       /* AND (dp) */
        v16 = a_get_M(cpu) & read_M_24(cpu, addr816_dp_indirect(cpu)); a_set_M(cpu, v16); update_nz_M(cpu, v16); extra += M_extra_cycle(cpu); break;
    case 0x52: if (cpu->E) { (void)cpu816_fetch_byte(cpu); break; }                       /* EOR (dp) */
        v16 = a_get_M(cpu) ^ read_M_24(cpu, addr816_dp_indirect(cpu)); a_set_M(cpu, v16); update_nz_M(cpu, v16); extra += M_extra_cycle(cpu); break;
    case 0x72: if (cpu->E) { (void)cpu816_fetch_byte(cpu); break; }                       /* ADC (dp) */
        op_adc_M(cpu, read_M_24(cpu, addr816_dp_indirect(cpu))); extra += M_extra_cycle(cpu); break;
    case 0x92: if (cpu->E) { (void)cpu816_fetch_byte(cpu); break; }                       /* STA (dp) */
        write_M_24(cpu, addr816_dp_indirect(cpu), a_get_M(cpu)); extra += M_extra_cycle(cpu); break;
    case 0xB2: if (cpu->E) { (void)cpu816_fetch_byte(cpu); break; }                       /* LDA (dp) */
        v16 = read_M_24(cpu, addr816_dp_indirect(cpu)); a_set_M(cpu, v16); update_nz_M(cpu, v16); extra += M_extra_cycle(cpu); break;
    case 0xD2: if (cpu->E) { (void)cpu816_fetch_byte(cpu); break; }                       /* CMP (dp) */
        op_cmp_M(cpu, a_get_M(cpu), read_M_24(cpu, addr816_dp_indirect(cpu))); extra += M_extra_cycle(cpu); break;
    case 0xF2: if (cpu->E) { (void)cpu816_fetch_byte(cpu); break; }                       /* SBC (dp) */
        op_sbc_M(cpu, read_M_24(cpu, addr816_dp_indirect(cpu))); extra += M_extra_cycle(cpu); break;

    /* ─── LDX (X-aware) ─── */
    case 0xA2: v16 = fetch_imm_X(cpu); x_set_X(cpu, v16); update_nz_X(cpu, v16); extra += X_extra_cycle(cpu); break;
    case 0xA6: v16 = read_X(cpu, addr816_zp(cpu)); x_set_X(cpu, v16); update_nz_X(cpu, v16); extra += X_extra_cycle(cpu); break;
    case 0xB6: v16 = read_X(cpu, addr816_zp_y(cpu)); x_set_X(cpu, v16); update_nz_X(cpu, v16); extra += X_extra_cycle(cpu); break;
    case 0xAE: v16 = read_X(cpu, addr816_abs(cpu)); x_set_X(cpu, v16); update_nz_X(cpu, v16); extra += X_extra_cycle(cpu); break;
    case 0xBE: addr = addr816_abs_y(cpu, &page_crossed); v16 = read_X(cpu, addr); x_set_X(cpu, v16); update_nz_X(cpu, v16); if(page_crossed) extra++; extra += X_extra_cycle(cpu); break;

    /* ─── LDY (X-aware) ─── */
    case 0xA0: v16 = fetch_imm_X(cpu); y_set_X(cpu, v16); update_nz_X(cpu, v16); extra += X_extra_cycle(cpu); break;
    case 0xA4: v16 = read_X(cpu, addr816_zp(cpu)); y_set_X(cpu, v16); update_nz_X(cpu, v16); extra += X_extra_cycle(cpu); break;
    case 0xB4: v16 = read_X(cpu, addr816_zp_x(cpu)); y_set_X(cpu, v16); update_nz_X(cpu, v16); extra += X_extra_cycle(cpu); break;
    case 0xAC: v16 = read_X(cpu, addr816_abs(cpu)); y_set_X(cpu, v16); update_nz_X(cpu, v16); extra += X_extra_cycle(cpu); break;
    case 0xBC: addr = addr816_abs_x(cpu, &page_crossed); v16 = read_X(cpu, addr); y_set_X(cpu, v16); update_nz_X(cpu, v16); if(page_crossed) extra++; extra += X_extra_cycle(cpu); break;

    /* ─── STA (M-aware) ─── */
    case 0x85: write_M(cpu, addr816_zp(cpu), a_get_M(cpu)); extra += M_extra_cycle(cpu); break;
    case 0x95: write_M(cpu, addr816_zp_x(cpu), a_get_M(cpu)); extra += M_extra_cycle(cpu); break;
    case 0x8D: write_M(cpu, addr816_abs(cpu), a_get_M(cpu)); extra += M_extra_cycle(cpu); break;
    case 0x9D: addr = addr816_abs_x(cpu, NULL); write_M(cpu, addr, a_get_M(cpu)); extra += M_extra_cycle(cpu); break;
    case 0x99: addr = addr816_abs_y(cpu, NULL); write_M(cpu, addr, a_get_M(cpu)); extra += M_extra_cycle(cpu); break;
    case 0x81: write_M(cpu, addr816_indexed_indirect(cpu), a_get_M(cpu)); extra += M_extra_cycle(cpu); break;
    case 0x91: addr = addr816_indirect_indexed(cpu, NULL); write_M(cpu, addr, a_get_M(cpu)); extra += M_extra_cycle(cpu); break;

    /* ─── STX (X-aware) ─── */
    case 0x86: write_X(cpu, addr816_zp(cpu), x_get_X(cpu)); extra += X_extra_cycle(cpu); break;
    case 0x96: write_X(cpu, addr816_zp_y(cpu), x_get_X(cpu)); extra += X_extra_cycle(cpu); break;
    case 0x8E: write_X(cpu, addr816_abs(cpu), x_get_X(cpu)); extra += X_extra_cycle(cpu); break;

    /* ─── STY (X-aware) ─── */
    case 0x84: write_X(cpu, addr816_zp(cpu), y_get_X(cpu)); extra += X_extra_cycle(cpu); break;
    case 0x94: write_X(cpu, addr816_zp_x(cpu), y_get_X(cpu)); extra += X_extra_cycle(cpu); break;
    case 0x8C: write_X(cpu, addr816_abs(cpu), y_get_X(cpu)); extra += X_extra_cycle(cpu); break;

    /* ─── ADC (M-aware) ─── */
    case 0x69: op_adc_M(cpu, fetch_imm_M(cpu)); extra += M_extra_cycle(cpu); break;
    case 0x65: op_adc_M(cpu, read_M(cpu, addr816_zp(cpu))); extra += M_extra_cycle(cpu); break;
    case 0x75: op_adc_M(cpu, read_M(cpu, addr816_zp_x(cpu))); extra += M_extra_cycle(cpu); break;
    case 0x6D: op_adc_M(cpu, read_M(cpu, addr816_abs(cpu))); extra += M_extra_cycle(cpu); break;
    case 0x7D: addr = addr816_abs_x(cpu, &page_crossed); op_adc_M(cpu, read_M(cpu, addr)); if(page_crossed) extra++; extra += M_extra_cycle(cpu); break;
    case 0x79: addr = addr816_abs_y(cpu, &page_crossed); op_adc_M(cpu, read_M(cpu, addr)); if(page_crossed) extra++; extra += M_extra_cycle(cpu); break;
    case 0x61: op_adc_M(cpu, read_M(cpu, addr816_indexed_indirect(cpu))); extra += M_extra_cycle(cpu); break;
    case 0x71: addr = addr816_indirect_indexed(cpu, &page_crossed); op_adc_M(cpu, read_M(cpu, addr)); if(page_crossed) extra++; extra += M_extra_cycle(cpu); break;

    /* ─── SBC (M-aware) ─── */
    case 0xE9: op_sbc_M(cpu, fetch_imm_M(cpu)); extra += M_extra_cycle(cpu); break;
    case 0xEB: /* En mode E : alias officieux SBC immediate (NMOS).
                * En mode N : XBA (échange high/low byte de C, 3 cycles).
                * cf. WDC W65C816S — opcode polymorphique. */
        if (cpu->E) { op_sbc_M(cpu, fetch_imm_M(cpu)); }
        else { uint8_t lo = (uint8_t)(cpu->C & 0xFF); uint8_t hi = (uint8_t)(cpu->C >> 8);
               cpu->C = (uint16_t)((lo << 8) | hi);
               /* N et Z reflètent le nouveau low byte (= ancien high). */
               update_nz(cpu, hi); cycles = 3; }
        break;
    case 0xE5: op_sbc_M(cpu, read_M(cpu, addr816_zp(cpu))); extra += M_extra_cycle(cpu); break;
    case 0xF5: op_sbc_M(cpu, read_M(cpu, addr816_zp_x(cpu))); extra += M_extra_cycle(cpu); break;
    case 0xED: op_sbc_M(cpu, read_M(cpu, addr816_abs(cpu))); extra += M_extra_cycle(cpu); break;
    case 0xFD: addr = addr816_abs_x(cpu, &page_crossed); op_sbc_M(cpu, read_M(cpu, addr)); if(page_crossed) extra++; extra += M_extra_cycle(cpu); break;
    case 0xF9: addr = addr816_abs_y(cpu, &page_crossed); op_sbc_M(cpu, read_M(cpu, addr)); if(page_crossed) extra++; extra += M_extra_cycle(cpu); break;
    case 0xE1: op_sbc_M(cpu, read_M(cpu, addr816_indexed_indirect(cpu))); extra += M_extra_cycle(cpu); break;
    case 0xF1: addr = addr816_indirect_indexed(cpu, &page_crossed); op_sbc_M(cpu, read_M(cpu, addr)); if(page_crossed) extra++; extra += M_extra_cycle(cpu); break;

    /* ─── AND (M-aware) ─── */
    case 0x29: v16 = a_get_M(cpu) & fetch_imm_M(cpu); a_set_M(cpu, v16); update_nz_M(cpu, v16); extra += M_extra_cycle(cpu); break;
    case 0x25: v16 = a_get_M(cpu) & read_M(cpu, addr816_zp(cpu)); a_set_M(cpu, v16); update_nz_M(cpu, v16); extra += M_extra_cycle(cpu); break;
    case 0x35: v16 = a_get_M(cpu) & read_M(cpu, addr816_zp_x(cpu)); a_set_M(cpu, v16); update_nz_M(cpu, v16); extra += M_extra_cycle(cpu); break;
    case 0x2D: v16 = a_get_M(cpu) & read_M(cpu, addr816_abs(cpu)); a_set_M(cpu, v16); update_nz_M(cpu, v16); extra += M_extra_cycle(cpu); break;
    case 0x3D: addr = addr816_abs_x(cpu, &page_crossed); v16 = a_get_M(cpu) & read_M(cpu, addr); a_set_M(cpu, v16); update_nz_M(cpu, v16); if(page_crossed) extra++; extra += M_extra_cycle(cpu); break;
    case 0x39: addr = addr816_abs_y(cpu, &page_crossed); v16 = a_get_M(cpu) & read_M(cpu, addr); a_set_M(cpu, v16); update_nz_M(cpu, v16); if(page_crossed) extra++; extra += M_extra_cycle(cpu); break;
    case 0x21: v16 = a_get_M(cpu) & read_M(cpu, addr816_indexed_indirect(cpu)); a_set_M(cpu, v16); update_nz_M(cpu, v16); extra += M_extra_cycle(cpu); break;
    case 0x31: addr = addr816_indirect_indexed(cpu, &page_crossed); v16 = a_get_M(cpu) & read_M(cpu, addr); a_set_M(cpu, v16); update_nz_M(cpu, v16); if(page_crossed) extra++; extra += M_extra_cycle(cpu); break;

    /* ─── ORA (M-aware) ─── */
    case 0x09: v16 = a_get_M(cpu) | fetch_imm_M(cpu); a_set_M(cpu, v16); update_nz_M(cpu, v16); extra += M_extra_cycle(cpu); break;
    case 0x05: v16 = a_get_M(cpu) | read_M(cpu, addr816_zp(cpu)); a_set_M(cpu, v16); update_nz_M(cpu, v16); extra += M_extra_cycle(cpu); break;
    case 0x15: v16 = a_get_M(cpu) | read_M(cpu, addr816_zp_x(cpu)); a_set_M(cpu, v16); update_nz_M(cpu, v16); extra += M_extra_cycle(cpu); break;
    case 0x0D: v16 = a_get_M(cpu) | read_M(cpu, addr816_abs(cpu)); a_set_M(cpu, v16); update_nz_M(cpu, v16); extra += M_extra_cycle(cpu); break;
    case 0x1D: addr = addr816_abs_x(cpu, &page_crossed); v16 = a_get_M(cpu) | read_M(cpu, addr); a_set_M(cpu, v16); update_nz_M(cpu, v16); if(page_crossed) extra++; extra += M_extra_cycle(cpu); break;
    case 0x19: addr = addr816_abs_y(cpu, &page_crossed); v16 = a_get_M(cpu) | read_M(cpu, addr); a_set_M(cpu, v16); update_nz_M(cpu, v16); if(page_crossed) extra++; extra += M_extra_cycle(cpu); break;
    case 0x01: v16 = a_get_M(cpu) | read_M(cpu, addr816_indexed_indirect(cpu)); a_set_M(cpu, v16); update_nz_M(cpu, v16); extra += M_extra_cycle(cpu); break;
    case 0x11: addr = addr816_indirect_indexed(cpu, &page_crossed); v16 = a_get_M(cpu) | read_M(cpu, addr); a_set_M(cpu, v16); update_nz_M(cpu, v16); if(page_crossed) extra++; extra += M_extra_cycle(cpu); break;

    /* ─── EOR (M-aware) ─── */
    case 0x49: v16 = a_get_M(cpu) ^ fetch_imm_M(cpu); a_set_M(cpu, v16); update_nz_M(cpu, v16); extra += M_extra_cycle(cpu); break;
    case 0x45: v16 = a_get_M(cpu) ^ read_M(cpu, addr816_zp(cpu)); a_set_M(cpu, v16); update_nz_M(cpu, v16); extra += M_extra_cycle(cpu); break;
    case 0x55: v16 = a_get_M(cpu) ^ read_M(cpu, addr816_zp_x(cpu)); a_set_M(cpu, v16); update_nz_M(cpu, v16); extra += M_extra_cycle(cpu); break;
    case 0x4D: v16 = a_get_M(cpu) ^ read_M(cpu, addr816_abs(cpu)); a_set_M(cpu, v16); update_nz_M(cpu, v16); extra += M_extra_cycle(cpu); break;
    case 0x5D: addr = addr816_abs_x(cpu, &page_crossed); v16 = a_get_M(cpu) ^ read_M(cpu, addr); a_set_M(cpu, v16); update_nz_M(cpu, v16); if(page_crossed) extra++; extra += M_extra_cycle(cpu); break;
    case 0x59: addr = addr816_abs_y(cpu, &page_crossed); v16 = a_get_M(cpu) ^ read_M(cpu, addr); a_set_M(cpu, v16); update_nz_M(cpu, v16); if(page_crossed) extra++; extra += M_extra_cycle(cpu); break;
    case 0x41: v16 = a_get_M(cpu) ^ read_M(cpu, addr816_indexed_indirect(cpu)); a_set_M(cpu, v16); update_nz_M(cpu, v16); extra += M_extra_cycle(cpu); break;
    case 0x51: addr = addr816_indirect_indexed(cpu, &page_crossed); v16 = a_get_M(cpu) ^ read_M(cpu, addr); a_set_M(cpu, v16); update_nz_M(cpu, v16); if(page_crossed) extra++; extra += M_extra_cycle(cpu); break;

    /* ─── CMP (M-aware) ─── */
    case 0xC9: op_cmp_M(cpu, a_get_M(cpu), fetch_imm_M(cpu)); extra += M_extra_cycle(cpu); break;
    case 0xC5: op_cmp_M(cpu, a_get_M(cpu), read_M(cpu, addr816_zp(cpu))); extra += M_extra_cycle(cpu); break;
    case 0xD5: op_cmp_M(cpu, a_get_M(cpu), read_M(cpu, addr816_zp_x(cpu))); extra += M_extra_cycle(cpu); break;
    case 0xCD: op_cmp_M(cpu, a_get_M(cpu), read_M(cpu, addr816_abs(cpu))); extra += M_extra_cycle(cpu); break;
    case 0xDD: addr = addr816_abs_x(cpu, &page_crossed); op_cmp_M(cpu, a_get_M(cpu), read_M(cpu, addr)); if(page_crossed) extra++; extra += M_extra_cycle(cpu); break;
    case 0xD9: addr = addr816_abs_y(cpu, &page_crossed); op_cmp_M(cpu, a_get_M(cpu), read_M(cpu, addr)); if(page_crossed) extra++; extra += M_extra_cycle(cpu); break;
    case 0xC1: op_cmp_M(cpu, a_get_M(cpu), read_M(cpu, addr816_indexed_indirect(cpu))); extra += M_extra_cycle(cpu); break;
    case 0xD1: addr = addr816_indirect_indexed(cpu, &page_crossed); op_cmp_M(cpu, a_get_M(cpu), read_M(cpu, addr)); if(page_crossed) extra++; extra += M_extra_cycle(cpu); break;

    /* ─── CPX (X-aware) ─── */
    case 0xE0: op_cmp_X(cpu, x_get_X(cpu), fetch_imm_X(cpu)); extra += X_extra_cycle(cpu); break;
    case 0xE4: op_cmp_X(cpu, x_get_X(cpu), read_X(cpu, addr816_zp(cpu))); extra += X_extra_cycle(cpu); break;
    case 0xEC: op_cmp_X(cpu, x_get_X(cpu), read_X(cpu, addr816_abs(cpu))); extra += X_extra_cycle(cpu); break;

    /* ─── CPY (X-aware) ─── */
    case 0xC0: op_cmp_X(cpu, y_get_X(cpu), fetch_imm_X(cpu)); extra += X_extra_cycle(cpu); break;
    case 0xC4: op_cmp_X(cpu, y_get_X(cpu), read_X(cpu, addr816_zp(cpu))); extra += X_extra_cycle(cpu); break;
    case 0xCC: op_cmp_X(cpu, y_get_X(cpu), read_X(cpu, addr816_abs(cpu))); extra += X_extra_cycle(cpu); break;

    /* ─── BIT (M-aware) ─── */
    case 0x24: v16 = read_M(cpu, addr816_zp(cpu));
        setf(cpu, FLAG_ZERO, (a_get_M(cpu) & v16) == 0);
        if (M_is_8bit(cpu)) {
            setf(cpu, FLAG_OVERFLOW, (v16 & 0x40) != 0);
            setf(cpu, FLAG_NEGATIVE, (v16 & 0x80) != 0);
        } else {
            setf(cpu, FLAG_OVERFLOW, (v16 & 0x4000) != 0);
            setf(cpu, FLAG_NEGATIVE, (v16 & 0x8000) != 0);
            extra++;
        }
        break;
    case 0x2C: v16 = read_M(cpu, addr816_abs(cpu));
        setf(cpu, FLAG_ZERO, (a_get_M(cpu) & v16) == 0);
        if (M_is_8bit(cpu)) {
            setf(cpu, FLAG_OVERFLOW, (v16 & 0x40) != 0);
            setf(cpu, FLAG_NEGATIVE, (v16 & 0x80) != 0);
        } else {
            setf(cpu, FLAG_OVERFLOW, (v16 & 0x4000) != 0);
            setf(cpu, FLAG_NEGATIVE, (v16 & 0x8000) != 0);
            extra++;
        }
        break;

    /* ─── ASL ─── */
    case 0x0A:
        if (M_is_8bit(cpu)) {
            setf(cpu, FLAG_CARRY, (a8(cpu) & 0x80) != 0);
            set_a8(cpu, (uint8_t)(a8(cpu) << 1));
            update_nz(cpu, a8(cpu));
        } else {
            /* M=0 : ASL A 16-bit complet (cpu->C) — fix Sprint 3.b debug. */
            setf(cpu, FLAG_CARRY, (cpu->C & 0x8000) != 0);
            cpu->C = (uint16_t)(cpu->C << 1);
            update_nz_M(cpu, cpu->C);
        }
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
        if (M_is_8bit(cpu)) {
            setf(cpu, FLAG_CARRY, (a8(cpu) & 0x01) != 0);
            set_a8(cpu, (uint8_t)(a8(cpu) >> 1));
            update_nz(cpu, a8(cpu));
        } else {
            setf(cpu, FLAG_CARRY, (cpu->C & 0x0001) != 0);
            cpu->C = (uint16_t)(cpu->C >> 1);
            update_nz_M(cpu, cpu->C);
        }
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
    case 0x2A:
        if (M_is_8bit(cpu)) {
            uint8_t c = flag(cpu, FLAG_CARRY) ? 1 : 0;
            setf(cpu, FLAG_CARRY, (a8(cpu) & 0x80) != 0);
            set_a8(cpu, (uint8_t)((a8(cpu) << 1) | c));
            update_nz(cpu, a8(cpu));
        } else {
            uint16_t c = flag(cpu, FLAG_CARRY) ? 1 : 0;
            setf(cpu, FLAG_CARRY, (cpu->C & 0x8000) != 0);
            cpu->C = (uint16_t)((cpu->C << 1) | c);
            update_nz_M(cpu, cpu->C);
        }
        break;
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
    case 0x6A:
        if (M_is_8bit(cpu)) {
            uint8_t c = flag(cpu, FLAG_CARRY) ? 0x80 : 0;
            setf(cpu, FLAG_CARRY, (a8(cpu) & 0x01) != 0);
            set_a8(cpu, (uint8_t)((a8(cpu) >> 1) | c));
            update_nz(cpu, a8(cpu));
        } else {
            uint16_t c = flag(cpu, FLAG_CARRY) ? 0x8000 : 0;
            setf(cpu, FLAG_CARRY, (cpu->C & 0x0001) != 0);
            cpu->C = (uint16_t)((cpu->C >> 1) | c);
            update_nz_M(cpu, cpu->C);
        }
        break;
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

    /* ─── INC A / DEC A — 65C816-only ($1A, $3A) M-aware
     *     En mode E : NOP (ADR-11(c) hybride, aligné 6502 Phosphoric).
     *     En mode N : opération sur l'accumulateur largeur M. ─── */
    case 0x1A: if (cpu->E) break;
               v16 = (uint16_t)(a_get_M(cpu) + 1); a_set_M(cpu, v16); update_nz_M(cpu, v16); break;
    case 0x3A: if (cpu->E) break;
               v16 = (uint16_t)(a_get_M(cpu) - 1); a_set_M(cpu, v16); update_nz_M(cpu, v16); break;

    /* ─── INX/INY/DEX/DEY (X-aware) ─── */
    case 0xE8: v16 = (uint16_t)(x_get_X(cpu) + 1); x_set_X(cpu, v16); update_nz_X(cpu, v16); break;
    case 0xC8: v16 = (uint16_t)(y_get_X(cpu) + 1); y_set_X(cpu, v16); update_nz_X(cpu, v16); break;
    case 0xCA: v16 = (uint16_t)(x_get_X(cpu) - 1); x_set_X(cpu, v16); update_nz_X(cpu, v16); break;
    case 0x88: v16 = (uint16_t)(y_get_X(cpu) - 1); y_set_X(cpu, v16); update_nz_X(cpu, v16); break;

    /* ─── Transferts (width-aware sur registre cible)
     *     TAX/TAY : cible X-flag (X ou Y). Source = C (16-bit raw).
     *     TXA/TYA : cible M-flag (A). Source = X ou Y (16-bit raw). */
    case 0xAA: x_set_X(cpu, cpu->C); update_nz_X(cpu, x_get_X(cpu)); break;  /* TAX */
    case 0x8A: a_set_M(cpu, cpu->X); update_nz_M(cpu, a_get_M(cpu)); break;  /* TXA */
    case 0xA8: y_set_X(cpu, cpu->C); update_nz_X(cpu, y_get_X(cpu)); break;  /* TAY */
    case 0x98: a_set_M(cpu, cpu->Y); update_nz_M(cpu, a_get_M(cpu)); break;  /* TYA */
    case 0xBA: v16 = X_is_8bit(cpu) ? sp8(cpu) : cpu->S; x_set_X(cpu, v16); update_nz_X(cpu, v16); break; /* TSX */
    case 0x9A: /* TXS — WDC W65C816S Programming Manual §A.32 :
                * Mode E : SH=$01, SL=XL.
                * Mode N : S = X (16-bit). Note : en X=1, le high byte de
                * cpu->X est déjà forcé à 0 par SEP #$10 (cf. case 0xE2),
                * donc x_get_X et cpu->X sont équivalents en mode N. */
        if (cpu->E) set_sp8(cpu, (uint8_t)x_get_X(cpu));
        else        cpu->S = x_get_X(cpu);
        break;

    /* ─── Transfers natifs 65C816 (toujours opérationnels, B1.7b) ─── */
    case 0x5B: /* TCD : C → D. N/Z toujours 16-bit (D est 16-bit). */
        cpu->D = cpu->C;
        cpu->P &= (uint8_t)~(FLAG_ZERO | FLAG_NEGATIVE);
        if (cpu->D == 0)        cpu->P |= FLAG_ZERO;
        if (cpu->D & 0x8000)    cpu->P |= FLAG_NEGATIVE;
        break;
    case 0x7B: cpu->C = cpu->D;                                                           /* TDC : D → C */
        cpu->P &= (uint8_t)~(FLAG_ZERO | FLAG_NEGATIVE);
        if (cpu->C == 0)        cpu->P |= FLAG_ZERO;
        if (cpu->C & 0x8000)    cpu->P |= FLAG_NEGATIVE;
        break;
    case 0x1B: /* TCS : C → S. En mode E, S high forcé à $01. Pas de set N/Z. */
        if (cpu->E) cpu->S = (uint16_t)(0x0100 | (cpu->C & 0xFF));
        else        cpu->S = cpu->C;
        break;
    case 0x3B: cpu->C = cpu->S;                                                           /* TSC : S → C */
        cpu->P &= (uint8_t)~(FLAG_ZERO | FLAG_NEGATIVE);
        if (cpu->C == 0)        cpu->P |= FLAG_ZERO;
        if (cpu->C & 0x8000)    cpu->P |= FLAG_NEGATIVE;
        break;
    case 0x9B: v16 = x_get_X(cpu); y_set_X(cpu, v16); update_nz_X(cpu, v16); break;       /* TXY */
    case 0xBB: v16 = y_get_X(cpu); x_set_X(cpu, v16); update_nz_X(cpu, v16); break;       /* TYX */

    /* ─── Stack (PHA/PLA M-aware ; PHP/PLP toujours 8b — P fait 8 bits) ─── */
    case 0x48: push_M(cpu, a_get_M(cpu)); extra += M_extra_cycle(cpu); break;                   /* PHA */
    case 0x68: v16 = pull_M(cpu); a_set_M(cpu, v16); update_nz_M(cpu, v16); extra += M_extra_cycle(cpu); break; /* PLA */
    case 0x08: /* PHP — en mode E, force B|UNUSED (sémantique 6502).
                * En mode N, push P entier (bits 4/5 = X/M doivent être
                * préservés sans modification). */
        if (cpu->E) cpu816_push(cpu, (uint8_t)(cpu->P | FLAG_BREAK | FLAG_UNUSED));
        else        cpu816_push(cpu, cpu->P);
        break;
    /* ─── PHX/PHY/PLX/PLY 65C816-only (X-aware, NOP en mode E ADR-11(c)) ─── */
    case 0xDA: if (cpu->E) break; push_X(cpu, x_get_X(cpu)); break;                       /* PHX */
    case 0x5A: if (cpu->E) break; push_X(cpu, y_get_X(cpu)); break;                       /* PHY */
    case 0xFA: if (cpu->E) break; v16 = pull_X(cpu); x_set_X(cpu, v16); update_nz_X(cpu, v16); break; /* PLX */
    case 0x7A: if (cpu->E) break; v16 = pull_X(cpu); y_set_X(cpu, v16); update_nz_X(cpu, v16); break; /* PLY */

    case 0x28: /* PLP — en mode E, masque B et force UNUSED (6502).
                * En mode N, pull P entier ; bits 4/5 = X/M restaurés
                * tels quels (cf. WDC W65C816S §A.21). Si X passe à 1
                * (8-bit), le high byte de X/Y est tronqué (cf. SEP). */
        if (cpu->E) {
            cpu->P = (uint8_t)((cpu816_pull(cpu) & ~FLAG_BREAK) | FLAG_UNUSED);
        } else {
            uint8_t old_x = cpu->P & FLAG816_X_INDEX;
            cpu->P = cpu816_pull(cpu);
            /* Si X transitionne de 0 (16-bit) à 1 (8-bit), tronquer X et Y. */
            if (!old_x && (cpu->P & FLAG816_X_INDEX)) {
                cpu->X &= 0x00FF;
                cpu->Y &= 0x00FF;
            }
        }
        break;

    /* ─── B1.7c — Stack opcodes 65C816 (NOP en mode E par ADR-11(c)) ─── */
    case 0x8B: if (cpu->E) break; cpu816_push(cpu, cpu->DBR); cycles = 3; break;       /* PHB */
    case 0xAB: if (cpu->E) break; cpu->DBR = cpu816_pull(cpu); update_nz(cpu, cpu->DBR); cycles = 4; break; /* PLB */
    case 0x4B: if (cpu->E) break; cpu816_push(cpu, cpu->PBR); cycles = 3; break;       /* PHK */
    case 0x0B: if (cpu->E) break; cpu816_push_word(cpu, cpu->D); cycles = 4; break;    /* PHD */
    case 0x2B: if (cpu->E) break; cpu->D = cpu816_pull_word(cpu);                       /* PLD */
        cpu->P &= (uint8_t)~(FLAG_ZERO | FLAG_NEGATIVE);
        if (cpu->D == 0)        cpu->P |= FLAG_ZERO;
        if (cpu->D & 0x8000)    cpu->P |= FLAG_NEGATIVE;
        cycles = 5; break;
    /* PEA #$nnnn : push absolute (the immediate value as data, not an addr) */
    case 0xF4: if (cpu->E) break;
        v16 = cpu816_fetch_word_pc(cpu);
        cpu816_push_word(cpu, v16);
        cycles = 5; break;
    /* PEI ($nn) : push effective indirect (read 16-bit pointer at zp, push it) */
    case 0xD4: if (cpu->E) break; {
        uint8_t zpg = cpu816_fetch_byte(cpu);
        uint8_t lo = cpu816_mem_read(cpu, zpg);
        uint8_t hi = cpu816_mem_read(cpu, (uint8_t)((zpg + 1) & 0xFF));
        cpu816_push_word(cpu, (uint16_t)((hi << 8) | lo));
        cycles = 6;
        break;
    }
    /* PER label : push effective relative (PC after operand + 16-bit signed offset) */
    case 0x62: if (cpu->E) break; {
        int16_t offset = (int16_t)cpu816_fetch_word_pc(cpu);
        cpu816_push_word(cpu, (uint16_t)((int)cpu->PC + offset));
        cycles = 6;
        break;
    }

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

    /* ─── B1.8 — JMP long et JML (mode N seulement, NOP en mode E) ─── */
    case 0x5C: if (cpu->E) { (void)fetch_long_pc(cpu); break; }                           /* JMP $lllllll */
    {
        uint32_t target = addr816_long(cpu);
        cpu->PBR = (uint8_t)(target >> 16);
        cpu->PC = (uint16_t)(target & 0xFFFF);
        break;
    }
    case 0xDC: if (cpu->E) { (void)cpu816_fetch_word_pc(cpu); break; }                    /* JML [a] : indirect long via abs ptr */
    {
        uint16_t ptr = cpu816_fetch_word_pc(cpu);
        uint8_t lo = memory_read24(cpu->memory, ptr);
        uint8_t hi = memory_read24(cpu->memory, (uint16_t)(ptr + 1));
        uint8_t bk = memory_read24(cpu->memory, (uint16_t)(ptr + 2));
        cpu->PBR = bk;
        cpu->PC = (uint16_t)(lo | (hi << 8));
        break;
    }

    /* ─── B1.8 — JSL : jump to subroutine long ($22) ───
     *     Push PBR puis PC-1 (haut, bas), charge nouveau PBR:PC. */
    case 0x22: if (cpu->E) { (void)fetch_long_pc(cpu); break; }
    {
        uint32_t target = fetch_long_pc(cpu);
        /* PC pointe maintenant sur l'instr suivante. Push PC-1 + PBR. */
        cpu816_push(cpu, cpu->PBR);
        cpu816_push_word(cpu, (uint16_t)(cpu->PC - 1));
        cpu->PBR = (uint8_t)(target >> 16);
        cpu->PC = (uint16_t)(target & 0xFFFF);
        break;
    }

    /* ─── B1.8 — RTL : return from long subroutine ($6B) ───
     *     Pull PC+1 puis PBR. */
    case 0x6B: if (cpu->E) break;
        cpu->PC = (uint16_t)(cpu816_pull_word(cpu) + 1);
        cpu->PBR = cpu816_pull(cpu);
        break;

    /* ─── B1.8 — MVN/MVP : block move ($54 / $44) ─── */
    case 0x54: if (cpu->E) { (void)cpu816_fetch_byte(cpu); (void)cpu816_fetch_byte(cpu); break; }   /* MVN dst,src */
    /* fall through to shared MVN/MVP body */
    /* FALLTHROUGH */
    case 0x44: if (cpu->E) { (void)cpu816_fetch_byte(cpu); (void)cpu816_fetch_byte(cpu); break; }   /* MVP dst,src */
    {
        /* Note : opcode déjà fetched. dest_bank et src_bank suivent.
         * Ces opcodes copient C+1 octets entre src_bank:X et dst_bank:Y.
         * Direction : MVN incrémente X et Y, MVP les décrémente. À la fin,
         * X et Y pointent sur un cran après le dernier octet copié, et C = $FFFF.
         * DBR est mis à dest_bank.
         * Implementation : fait toute la boucle en un seul step (simple,
         * pas cycle-exact ; un vrai 65C816 fait 7 cycles/byte). */
        uint8_t dst_bank = cpu816_fetch_byte(cpu);
        uint8_t src_bank = cpu816_fetch_byte(cpu);
        cpu->DBR = dst_bank;
        bool ascending = (opcode == 0x54);
        /* Mode E : indices/A 8-bit : trait C comme A 8-bit + B 8-bit = 16-bit ?
         * En mode N M=0, C = full 16-bit count. C+1 octets copiés. */
        uint32_t count = (uint32_t)cpu->C + 1u;
        for (uint32_t i = 0; i < count; i++) {
            uint32_t src_addr = ((uint32_t)src_bank << 16) | cpu->X;
            uint32_t dst_addr = ((uint32_t)dst_bank << 16) | cpu->Y;
            uint8_t b = memory_read24(cpu->memory, src_addr);
            memory_write24(cpu->memory, dst_addr, b);
            if (ascending) {
                cpu->X = (uint16_t)(cpu->X + 1);
                cpu->Y = (uint16_t)(cpu->Y + 1);
            } else {
                cpu->X = (uint16_t)(cpu->X - 1);
                cpu->Y = (uint16_t)(cpu->Y - 1);
            }
        }
        cpu->C = 0xFFFF;
        cycles = 7 * (int)count;
        break;
    }

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

    /* ─── RTI ─── (mode N pulls PBR en plus, cf. WDC datasheet)
     * Mode E : P pull avec masque B + force UNUSED (sémantique 6502).
     * Mode N : P pull entier ; bits X/M restaurés tels quels.
     *          Tronque X/Y si transition X 0→1 (cf. SEP).
     */
    case 0x40:
        if (cpu->E) {
            cpu->P = (uint8_t)((cpu816_pull(cpu) & ~FLAG_BREAK) | FLAG_UNUSED);
        } else {
            uint8_t old_x = cpu->P & FLAG816_X_INDEX;
            cpu->P = cpu816_pull(cpu);
            if (!old_x && (cpu->P & FLAG816_X_INDEX)) {
                cpu->X &= 0x00FF;
                cpu->Y &= 0x00FF;
            }
        }
        cpu->PC = cpu816_pull_word(cpu);
        if (!cpu->E) cpu->PBR = cpu816_pull(cpu);
        break;

    /* ─── B1.7c — BRA / BRL : branches inconditionnelles 65C816 ─── */
    case 0x80: { /* BRA — branch always relative 8-bit */
        uint16_t target = addr816_relative(cpu);
        int penalty = ((cpu->PC & 0xFF00) != (target & 0xFF00)) ? 2 : 1;
        cpu->PC = target;
        cycles = 2 + penalty; /* base 2 + 1 taken (+ 1 if cross-page mode E) */
        break;
    }
    case 0x82: { /* BRL — branch relative long (signed 16-bit, always 4 cycles) */
        int16_t offset = (int16_t)cpu816_fetch_word_pc(cpu);
        cpu->PC = (uint16_t)((int)cpu->PC + offset);
        cycles = 4;
        break;
    }

    /* ─── B1.7c — COP : software interrupt (BRK-like, vecteur dédié)
     *     Mode N : push PBR + clear PBR (handler en bank 0).
     *     Le P pushé doit refléter l'état courant (en mode N, bits 4/5
     *     = X/M, ne PAS forcer comme en mode E). PH-bug-dp-indirect-Y
     *     2026-05-08 : sans ce fix, RTI restorait X=0 (16-bit) et
     *     cassait les routines en X=1 du caller. */
    case 0x02: {
        cpu->PC = (uint16_t)(cpu->PC + 1); /* COP suivi d'un signature byte */
        if (!cpu->E) {
            cpu816_push(cpu, cpu->PBR);
            cpu->PBR = 0;
        }
        cpu816_push_word(cpu, cpu->PC);
        if (cpu->E)
            cpu816_push(cpu, (uint8_t)((cpu->P & ~FLAG_BREAK) | FLAG_UNUSED));
        else
            cpu816_push(cpu, cpu->P);
        setf(cpu, FLAG_INTERRUPT, true);
        setf(cpu, FLAG_DECIMAL, false); /* mode N : D forcé à 0 par interrupt */
        uint16_t vec = cpu->E ? 0xFFF4 : 0xFFE4;
        cpu->PC = (uint16_t)(cpu816_mem_read(cpu, vec) | (cpu816_mem_read(cpu, (uint16_t)(vec + 1)) << 8));
        cycles = cpu->E ? 7 : 8;
        break;
    }

    /* ─── B1.7c — WDM : reserved 2-byte NOP ($42) ─── */
    case 0x42: (void)cpu816_fetch_byte(cpu); cycles = 2; break;

    /* ─── B3 — STP / WAI : halt CPU
     *     STP $DB : arrêt total (sortie uniquement par RESET).
     *     WAI $CB : attend NMI ou IRQ (sortie même si I=1).
     *     NOP en mode E par ADR-11(c) (illégaux NMOS). */
    case 0xDB: if (cpu->E) break; cpu->stopped = true; cycles = 3; break;
    case 0xCB: if (cpu->E) break; cpu->waiting = true; cycles = 3; break;

    /* ─── B1.7c — STZ : store zero (M-aware) — 65C02/65C816 ───
     *     En mode E (ADR-11(c)) : NOP (consomme la taille opérande). */
    case 0x9C: if (cpu->E) { (void)cpu816_fetch_word_pc(cpu); break; }
               write_M(cpu, addr816_abs(cpu), 0); extra += M_extra_cycle(cpu); break;
    case 0x9E: if (cpu->E) { (void)cpu816_fetch_word_pc(cpu); break; }
               addr = addr816_abs_x(cpu, NULL); write_M(cpu, addr, 0); extra += M_extra_cycle(cpu); break;
    case 0x64: if (cpu->E) { (void)cpu816_fetch_byte(cpu); break; }
               write_M(cpu, addr816_zp(cpu), 0); extra += M_extra_cycle(cpu); break;
    case 0x74: if (cpu->E) { (void)cpu816_fetch_byte(cpu); break; }
               write_M(cpu, addr816_zp_x(cpu), 0); extra += M_extra_cycle(cpu); break;

    /* ─── BRK ─── (mode N push PBR + clear PBR, vecteur $00FFE6 ; mode E vecteur $FFFE)
     * Mode E : P pushé avec B|UNUSED (sémantique 6502).
     * Mode N : P pushé entier ; bits 4/5 = X/M préservés.
     */
    case 0x00:
        cpu->PC = (uint16_t)(cpu->PC + 1); /* BRK est suivi d'un signature byte */
        if (!cpu->E) {
            cpu816_push(cpu, cpu->PBR);
            cpu->PBR = 0;
        }
        cpu816_push_word(cpu, cpu->PC);
        if (cpu->E)
            cpu816_push(cpu, (uint8_t)(cpu->P | FLAG_BREAK | FLAG_UNUSED));
        else
            cpu816_push(cpu, cpu->P);
        setf(cpu, FLAG_INTERRUPT, true);
        if (!cpu->E) setf(cpu, FLAG_DECIMAL, false);
        {
            uint16_t brk_vec = cpu->E ? 0xFFFE : 0xFFE6;
            cpu->PC = (uint16_t)(cpu816_mem_read(cpu, brk_vec)
                                | (cpu816_mem_read(cpu, (uint16_t)(brk_vec + 1)) << 8));
        }
        break;

    /* ─── Flag instructions (déjà testées en B1.3) ─── */
    case 0x18: setf(cpu, FLAG_CARRY, false);    break; /* CLC */
    case 0x38: setf(cpu, FLAG_CARRY, true);     break; /* SEC */
    case 0x58: setf(cpu, FLAG_INTERRUPT, false); break; /* CLI */
    case 0x78: setf(cpu, FLAG_INTERRUPT, true);  break; /* SEI */
    case 0xD8: setf(cpu, FLAG_DECIMAL, false);   break; /* CLD */
    case 0xF8: setf(cpu, FLAG_DECIMAL, true);    break; /* SED */
    case 0xB8: setf(cpu, FLAG_OVERFLOW, false);  break; /* CLV */

    /* ─── REP / SEP — P bits manipulation 65C816 (B1.7) ─── */
    case 0xC2: { /* REP #imm — clear bits in P selected by mask */
        uint8_t mask = cpu816_fetch_byte(cpu);
        cpu->P = (uint8_t)(cpu->P & ~mask);
        /* En mode E, UNUSED reste à 1 (sémantique 6502 sur bit 5).
         * M_is_8bit/X_is_8bit court-circuitent sur cpu->E, donc la
         * largeur effective est garantie par E=1 indépendamment de P. */
        if (cpu->E) cpu->P |= FLAG_UNUSED;
        cycles = 3;
        break;
    }
    case 0xE2: { /* SEP #imm — set bits in P selected by mask */
        uint8_t mask = cpu816_fetch_byte(cpu);
        cpu->P |= mask;
        /* Quand X passe à 1 (8-bit indices), les high bytes de X et Y
         * sont tronqués. */
        if (mask & FLAG816_X_INDEX) {
            cpu->X &= 0x00FF;
            cpu->Y &= 0x00FF;
        }
        cycles = 3;
        break;
    }

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
