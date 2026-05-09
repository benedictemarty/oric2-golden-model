/**
 * @file opcode_metadata.c
 * @brief Définition de la table des 256 opcodes 6502 / 65C816 mode E.
 * @author bmarty <bmarty@mailo.com>
 * @date 2026-05-09
 *
 * Extrait de `opcodes.c` en PH-2.c.2 (ADR-18 étape 1.C). Permet au cœur
 * 65C816 de référencer `opcode_table[]` sans dépendre du cœur 6502
 * historique. La sémantique d'exécution des opcodes 6502 reste dans
 * `opcodes.c` (sera supprimée en fin de PH-2.c.2 avec `cpu6502.c` et
 * `addressing.c`).
 */

#include "cpu/opcode_metadata.h"

const opcode_info_t opcode_table[256] = {
    /* 0x00 */ {"BRK",7,1,ADDR_IMPLICIT},    {"ORA",6,2,ADDR_INDEXED_INDIRECT},
    /* 0x02 */ {"???",2,1,ADDR_IMPLICIT},     {"???",2,1,ADDR_IMPLICIT},
    /* 0x04 */ {"???",3,2,ADDR_ZERO_PAGE},    {"ORA",3,2,ADDR_ZERO_PAGE},
    /* 0x06 */ {"ASL",5,2,ADDR_ZERO_PAGE},    {"???",2,1,ADDR_IMPLICIT},
    /* 0x08 */ {"PHP",3,1,ADDR_IMPLICIT},     {"ORA",2,2,ADDR_IMMEDIATE},
    /* 0x0A */ {"ASL",2,1,ADDR_ACCUMULATOR},  {"???",2,1,ADDR_IMPLICIT},
    /* 0x0C */ {"???",4,3,ADDR_ABSOLUTE},     {"ORA",4,3,ADDR_ABSOLUTE},
    /* 0x0E */ {"ASL",6,3,ADDR_ABSOLUTE},     {"???",2,1,ADDR_IMPLICIT},

    /* 0x10 */ {"BPL",2,2,ADDR_RELATIVE},     {"ORA",5,2,ADDR_INDIRECT_INDEXED},
    /* 0x12 */ {"???",2,1,ADDR_IMPLICIT},     {"???",2,1,ADDR_IMPLICIT},
    /* 0x14 */ {"???",4,2,ADDR_ZERO_PAGE_X},  {"ORA",4,2,ADDR_ZERO_PAGE_X},
    /* 0x16 */ {"ASL",6,2,ADDR_ZERO_PAGE_X},  {"???",2,1,ADDR_IMPLICIT},
    /* 0x18 */ {"CLC",2,1,ADDR_IMPLICIT},     {"ORA",4,3,ADDR_ABSOLUTE_Y},
    /* 0x1A */ {"???",2,1,ADDR_IMPLICIT},     {"???",2,1,ADDR_IMPLICIT},
    /* 0x1C */ {"???",4,3,ADDR_ABSOLUTE_X},   {"ORA",4,3,ADDR_ABSOLUTE_X},
    /* 0x1E */ {"ASL",7,3,ADDR_ABSOLUTE_X},   {"???",2,1,ADDR_IMPLICIT},

    /* 0x20 */ {"JSR",6,3,ADDR_ABSOLUTE},     {"AND",6,2,ADDR_INDEXED_INDIRECT},
    /* 0x22 */ {"???",2,1,ADDR_IMPLICIT},     {"???",2,1,ADDR_IMPLICIT},
    /* 0x24 */ {"BIT",3,2,ADDR_ZERO_PAGE},    {"AND",3,2,ADDR_ZERO_PAGE},
    /* 0x26 */ {"ROL",5,2,ADDR_ZERO_PAGE},    {"???",2,1,ADDR_IMPLICIT},
    /* 0x28 */ {"PLP",4,1,ADDR_IMPLICIT},     {"AND",2,2,ADDR_IMMEDIATE},
    /* 0x2A */ {"ROL",2,1,ADDR_ACCUMULATOR},  {"???",2,1,ADDR_IMPLICIT},
    /* 0x2C */ {"BIT",4,3,ADDR_ABSOLUTE},     {"AND",4,3,ADDR_ABSOLUTE},
    /* 0x2E */ {"ROL",6,3,ADDR_ABSOLUTE},     {"???",2,1,ADDR_IMPLICIT},

    /* 0x30 */ {"BMI",2,2,ADDR_RELATIVE},     {"AND",5,2,ADDR_INDIRECT_INDEXED},
    /* 0x32 */ {"???",2,1,ADDR_IMPLICIT},     {"???",2,1,ADDR_IMPLICIT},
    /* 0x34 */ {"???",4,2,ADDR_ZERO_PAGE_X},  {"AND",4,2,ADDR_ZERO_PAGE_X},
    /* 0x36 */ {"ROL",6,2,ADDR_ZERO_PAGE_X},  {"???",2,1,ADDR_IMPLICIT},
    /* 0x38 */ {"SEC",2,1,ADDR_IMPLICIT},     {"AND",4,3,ADDR_ABSOLUTE_Y},
    /* 0x3A */ {"???",2,1,ADDR_IMPLICIT},     {"???",2,1,ADDR_IMPLICIT},
    /* 0x3C */ {"???",4,3,ADDR_ABSOLUTE_X},   {"AND",4,3,ADDR_ABSOLUTE_X},
    /* 0x3E */ {"ROL",7,3,ADDR_ABSOLUTE_X},   {"???",2,1,ADDR_IMPLICIT},

    /* 0x40 */ {"RTI",6,1,ADDR_IMPLICIT},     {"EOR",6,2,ADDR_INDEXED_INDIRECT},
    /* 0x42 */ {"???",2,1,ADDR_IMPLICIT},     {"???",2,1,ADDR_IMPLICIT},
    /* 0x44 */ {"???",3,2,ADDR_ZERO_PAGE},    {"EOR",3,2,ADDR_ZERO_PAGE},
    /* 0x46 */ {"LSR",5,2,ADDR_ZERO_PAGE},    {"???",2,1,ADDR_IMPLICIT},
    /* 0x48 */ {"PHA",3,1,ADDR_IMPLICIT},     {"EOR",2,2,ADDR_IMMEDIATE},
    /* 0x4A */ {"LSR",2,1,ADDR_ACCUMULATOR},  {"???",2,1,ADDR_IMPLICIT},
    /* 0x4C */ {"JMP",3,3,ADDR_ABSOLUTE},     {"EOR",4,3,ADDR_ABSOLUTE},
    /* 0x4E */ {"LSR",6,3,ADDR_ABSOLUTE},     {"???",2,1,ADDR_IMPLICIT},

    /* 0x50 */ {"BVC",2,2,ADDR_RELATIVE},     {"EOR",5,2,ADDR_INDIRECT_INDEXED},
    /* 0x52 */ {"???",2,1,ADDR_IMPLICIT},     {"???",2,1,ADDR_IMPLICIT},
    /* 0x54 */ {"???",4,2,ADDR_ZERO_PAGE_X},  {"EOR",4,2,ADDR_ZERO_PAGE_X},
    /* 0x56 */ {"LSR",6,2,ADDR_ZERO_PAGE_X},  {"???",2,1,ADDR_IMPLICIT},
    /* 0x58 */ {"CLI",2,1,ADDR_IMPLICIT},     {"EOR",4,3,ADDR_ABSOLUTE_Y},
    /* 0x5A */ {"???",2,1,ADDR_IMPLICIT},     {"???",2,1,ADDR_IMPLICIT},
    /* 0x5C */ {"???",4,3,ADDR_ABSOLUTE_X},   {"EOR",4,3,ADDR_ABSOLUTE_X},
    /* 0x5E */ {"LSR",7,3,ADDR_ABSOLUTE_X},   {"???",2,1,ADDR_IMPLICIT},

    /* 0x60 */ {"RTS",6,1,ADDR_IMPLICIT},     {"ADC",6,2,ADDR_INDEXED_INDIRECT},
    /* 0x62 */ {"???",2,1,ADDR_IMPLICIT},     {"???",2,1,ADDR_IMPLICIT},
    /* 0x64 */ {"???",3,2,ADDR_ZERO_PAGE},    {"ADC",3,2,ADDR_ZERO_PAGE},
    /* 0x66 */ {"ROR",5,2,ADDR_ZERO_PAGE},    {"???",2,1,ADDR_IMPLICIT},
    /* 0x68 */ {"PLA",4,1,ADDR_IMPLICIT},     {"ADC",2,2,ADDR_IMMEDIATE},
    /* 0x6A */ {"ROR",2,1,ADDR_ACCUMULATOR},  {"???",2,1,ADDR_IMPLICIT},
    /* 0x6C */ {"JMP",5,3,ADDR_INDIRECT},     {"ADC",4,3,ADDR_ABSOLUTE},
    /* 0x6E */ {"ROR",6,3,ADDR_ABSOLUTE},     {"???",2,1,ADDR_IMPLICIT},

    /* 0x70 */ {"BVS",2,2,ADDR_RELATIVE},     {"ADC",5,2,ADDR_INDIRECT_INDEXED},
    /* 0x72 */ {"???",2,1,ADDR_IMPLICIT},     {"???",2,1,ADDR_IMPLICIT},
    /* 0x74 */ {"???",4,2,ADDR_ZERO_PAGE_X},  {"ADC",4,2,ADDR_ZERO_PAGE_X},
    /* 0x76 */ {"ROR",6,2,ADDR_ZERO_PAGE_X},  {"???",2,1,ADDR_IMPLICIT},
    /* 0x78 */ {"SEI",2,1,ADDR_IMPLICIT},     {"ADC",4,3,ADDR_ABSOLUTE_Y},
    /* 0x7A */ {"???",2,1,ADDR_IMPLICIT},     {"???",2,1,ADDR_IMPLICIT},
    /* 0x7C */ {"???",4,3,ADDR_ABSOLUTE_X},   {"ADC",4,3,ADDR_ABSOLUTE_X},
    /* 0x7E */ {"ROR",7,3,ADDR_ABSOLUTE_X},   {"???",2,1,ADDR_IMPLICIT},

    /* 0x80 */ {"???",2,2,ADDR_IMMEDIATE},    {"STA",6,2,ADDR_INDEXED_INDIRECT},
    /* 0x82 */ {"???",2,2,ADDR_IMMEDIATE},    {"???",2,1,ADDR_IMPLICIT},
    /* 0x84 */ {"STY",3,2,ADDR_ZERO_PAGE},    {"STA",3,2,ADDR_ZERO_PAGE},
    /* 0x86 */ {"STX",3,2,ADDR_ZERO_PAGE},    {"???",2,1,ADDR_IMPLICIT},
    /* 0x88 */ {"DEY",2,1,ADDR_IMPLICIT},     {"???",2,2,ADDR_IMMEDIATE},
    /* 0x8A */ {"TXA",2,1,ADDR_IMPLICIT},     {"???",2,1,ADDR_IMPLICIT},
    /* 0x8C */ {"STY",4,3,ADDR_ABSOLUTE},     {"STA",4,3,ADDR_ABSOLUTE},
    /* 0x8E */ {"STX",4,3,ADDR_ABSOLUTE},     {"???",2,1,ADDR_IMPLICIT},

    /* 0x90 */ {"BCC",2,2,ADDR_RELATIVE},     {"STA",6,2,ADDR_INDIRECT_INDEXED},
    /* 0x92 */ {"???",2,1,ADDR_IMPLICIT},     {"???",2,1,ADDR_IMPLICIT},
    /* 0x94 */ {"STY",4,2,ADDR_ZERO_PAGE_X},  {"STA",4,2,ADDR_ZERO_PAGE_X},
    /* 0x96 */ {"STX",4,2,ADDR_ZERO_PAGE_Y},  {"???",2,1,ADDR_IMPLICIT},
    /* 0x98 */ {"TYA",2,1,ADDR_IMPLICIT},     {"STA",5,3,ADDR_ABSOLUTE_Y},
    /* 0x9A */ {"TXS",2,1,ADDR_IMPLICIT},     {"???",2,1,ADDR_IMPLICIT},
    /* 0x9C */ {"???",2,1,ADDR_IMPLICIT},     {"STA",5,3,ADDR_ABSOLUTE_X},
    /* 0x9E */ {"???",2,1,ADDR_IMPLICIT},     {"???",2,1,ADDR_IMPLICIT},

    /* 0xA0 */ {"LDY",2,2,ADDR_IMMEDIATE},    {"LDA",6,2,ADDR_INDEXED_INDIRECT},
    /* 0xA2 */ {"LDX",2,2,ADDR_IMMEDIATE},    {"???",2,1,ADDR_IMPLICIT},
    /* 0xA4 */ {"LDY",3,2,ADDR_ZERO_PAGE},    {"LDA",3,2,ADDR_ZERO_PAGE},
    /* 0xA6 */ {"LDX",3,2,ADDR_ZERO_PAGE},    {"???",2,1,ADDR_IMPLICIT},
    /* 0xA8 */ {"TAY",2,1,ADDR_IMPLICIT},     {"LDA",2,2,ADDR_IMMEDIATE},
    /* 0xAA */ {"TAX",2,1,ADDR_IMPLICIT},     {"???",2,1,ADDR_IMPLICIT},
    /* 0xAC */ {"LDY",4,3,ADDR_ABSOLUTE},     {"LDA",4,3,ADDR_ABSOLUTE},
    /* 0xAE */ {"LDX",4,3,ADDR_ABSOLUTE},     {"???",2,1,ADDR_IMPLICIT},

    /* 0xB0 */ {"BCS",2,2,ADDR_RELATIVE},     {"LDA",5,2,ADDR_INDIRECT_INDEXED},
    /* 0xB2 */ {"???",2,1,ADDR_IMPLICIT},     {"???",2,1,ADDR_IMPLICIT},
    /* 0xB4 */ {"LDY",4,2,ADDR_ZERO_PAGE_X},  {"LDA",4,2,ADDR_ZERO_PAGE_X},
    /* 0xB6 */ {"LDX",4,2,ADDR_ZERO_PAGE_Y},  {"???",2,1,ADDR_IMPLICIT},
    /* 0xB8 */ {"CLV",2,1,ADDR_IMPLICIT},     {"LDA",4,3,ADDR_ABSOLUTE_Y},
    /* 0xBA */ {"TSX",2,1,ADDR_IMPLICIT},     {"???",2,1,ADDR_IMPLICIT},
    /* 0xBC */ {"LDY",4,3,ADDR_ABSOLUTE_X},   {"LDA",4,3,ADDR_ABSOLUTE_X},
    /* 0xBE */ {"LDX",4,3,ADDR_ABSOLUTE_Y},   {"???",2,1,ADDR_IMPLICIT},

    /* 0xC0 */ {"CPY",2,2,ADDR_IMMEDIATE},    {"CMP",6,2,ADDR_INDEXED_INDIRECT},
    /* 0xC2 */ {"???",2,2,ADDR_IMMEDIATE},    {"???",2,1,ADDR_IMPLICIT},
    /* 0xC4 */ {"CPY",3,2,ADDR_ZERO_PAGE},    {"CMP",3,2,ADDR_ZERO_PAGE},
    /* 0xC6 */ {"DEC",5,2,ADDR_ZERO_PAGE},    {"???",2,1,ADDR_IMPLICIT},
    /* 0xC8 */ {"INY",2,1,ADDR_IMPLICIT},     {"CMP",2,2,ADDR_IMMEDIATE},
    /* 0xCA */ {"DEX",2,1,ADDR_IMPLICIT},     {"???",2,1,ADDR_IMPLICIT},
    /* 0xCC */ {"CPY",4,3,ADDR_ABSOLUTE},     {"CMP",4,3,ADDR_ABSOLUTE},
    /* 0xCE */ {"DEC",6,3,ADDR_ABSOLUTE},     {"???",2,1,ADDR_IMPLICIT},

    /* 0xD0 */ {"BNE",2,2,ADDR_RELATIVE},     {"CMP",5,2,ADDR_INDIRECT_INDEXED},
    /* 0xD2 */ {"???",2,1,ADDR_IMPLICIT},     {"???",2,1,ADDR_IMPLICIT},
    /* 0xD4 */ {"???",4,2,ADDR_ZERO_PAGE_X},  {"CMP",4,2,ADDR_ZERO_PAGE_X},
    /* 0xD6 */ {"DEC",6,2,ADDR_ZERO_PAGE_X},  {"???",2,1,ADDR_IMPLICIT},
    /* 0xD8 */ {"CLD",2,1,ADDR_IMPLICIT},     {"CMP",4,3,ADDR_ABSOLUTE_Y},
    /* 0xDA */ {"???",2,1,ADDR_IMPLICIT},     {"???",2,1,ADDR_IMPLICIT},
    /* 0xDC */ {"???",4,3,ADDR_ABSOLUTE_X},   {"CMP",4,3,ADDR_ABSOLUTE_X},
    /* 0xDE */ {"DEC",7,3,ADDR_ABSOLUTE_X},   {"???",2,1,ADDR_IMPLICIT},

    /* 0xE0 */ {"CPX",2,2,ADDR_IMMEDIATE},    {"SBC",6,2,ADDR_INDEXED_INDIRECT},
    /* 0xE2 */ {"???",2,2,ADDR_IMMEDIATE},    {"???",2,1,ADDR_IMPLICIT},
    /* 0xE4 */ {"CPX",3,2,ADDR_ZERO_PAGE},    {"SBC",3,2,ADDR_ZERO_PAGE},
    /* 0xE6 */ {"INC",5,2,ADDR_ZERO_PAGE},    {"???",2,1,ADDR_IMPLICIT},
    /* 0xE8 */ {"INX",2,1,ADDR_IMPLICIT},     {"SBC",2,2,ADDR_IMMEDIATE},
    /* 0xEA */ {"NOP",2,1,ADDR_IMPLICIT},     {"???",2,2,ADDR_IMMEDIATE},
    /* 0xEC */ {"CPX",4,3,ADDR_ABSOLUTE},     {"SBC",4,3,ADDR_ABSOLUTE},
    /* 0xEE */ {"INC",6,3,ADDR_ABSOLUTE},     {"???",2,1,ADDR_IMPLICIT},

    /* 0xF0 */ {"BEQ",2,2,ADDR_RELATIVE},     {"SBC",5,2,ADDR_INDIRECT_INDEXED},
    /* 0xF2 */ {"???",2,1,ADDR_IMPLICIT},     {"???",2,1,ADDR_IMPLICIT},
    /* 0xF4 */ {"???",4,2,ADDR_ZERO_PAGE_X},  {"SBC",4,2,ADDR_ZERO_PAGE_X},
    /* 0xF6 */ {"INC",6,2,ADDR_ZERO_PAGE_X},  {"???",2,1,ADDR_IMPLICIT},
    /* 0xF8 */ {"SED",2,1,ADDR_IMPLICIT},     {"SBC",4,3,ADDR_ABSOLUTE_Y},
    /* 0xFA */ {"???",2,1,ADDR_IMPLICIT},     {"???",2,1,ADDR_IMPLICIT},
    /* 0xFC */ {"???",4,3,ADDR_ABSOLUTE_X},   {"SBC",4,3,ADDR_ABSOLUTE_X},
    /* 0xFE */ {"INC",7,3,ADDR_ABSOLUTE_X},   {"???",2,1,ADDR_IMPLICIT},
};
