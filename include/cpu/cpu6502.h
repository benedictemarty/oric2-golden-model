/**
 * @file cpu6502.h
 * @brief MOS 6502 CPU emulation (cycle-accurate)
 * @author bmarty <bmarty@mailo.com>
 * @date 2026-01-31
 * @version 0.1.0-alpha
 *
 * This module implements a cycle-accurate emulation of the MOS 6502 CPU
 * as used in the ORIC-1 computer (running at 1 MHz).
 */

#ifndef CPU6502_H
#define CPU6502_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

/**
 * @brief CPU status flags (Processor Status Register)
 */
typedef enum {
    FLAG_CARRY     = 0x01,  /**< Carry flag (C) */
    FLAG_ZERO      = 0x02,  /**< Zero flag (Z) */
    FLAG_INTERRUPT = 0x04,  /**< Interrupt disable (I) */
    FLAG_DECIMAL   = 0x08,  /**< Decimal mode (D) */
    FLAG_BREAK     = 0x10,  /**< Break command (B) */
    FLAG_UNUSED    = 0x20,  /**< Unused (always 1) */
    FLAG_OVERFLOW  = 0x40,  /**< Overflow flag (V) */
    FLAG_NEGATIVE  = 0x80   /**< Negative flag (N) */
} cpu_flags_t;

/**
 * @brief CPU state structure
 */
typedef struct {
    uint8_t  A;             /**< Accumulator */
    uint8_t  X;             /**< X register */
    uint8_t  Y;             /**< Y register */
    uint8_t  SP;            /**< Stack Pointer (0x0100 + SP) */
    uint16_t PC;            /**< Program Counter */
    uint8_t  P;             /**< Processor Status Register */

    uint64_t cycles;        /**< Total cycles executed */
    uint32_t cycles_left;   /**< Cycles remaining for current instruction */

    bool     halted;        /**< CPU halted flag */
    bool     nmi_pending;   /**< Non-Maskable Interrupt pending */
    bool     irq_pending;   /**< Interrupt Request pending */

    void*    memory;        /**< Pointer to memory subsystem */
} cpu6502_t;

/**
 * @brief Addressing modes
 */
typedef enum {
    ADDR_IMPLICIT,      /**< Implicit (no operand) */
    ADDR_ACCUMULATOR,   /**< Accumulator */
    ADDR_IMMEDIATE,     /**< Immediate (#$nn) */
    ADDR_ZERO_PAGE,     /**< Zero Page ($nn) */
    ADDR_ZERO_PAGE_X,   /**< Zero Page,X ($nn,X) */
    ADDR_ZERO_PAGE_Y,   /**< Zero Page,Y ($nn,Y) */
    ADDR_RELATIVE,      /**< Relative (branch) */
    ADDR_ABSOLUTE,      /**< Absolute ($nnnn) */
    ADDR_ABSOLUTE_X,    /**< Absolute,X ($nnnn,X) */
    ADDR_ABSOLUTE_Y,    /**< Absolute,Y ($nnnn,Y) */
    ADDR_INDIRECT,      /**< Indirect (JMP) */
    ADDR_INDEXED_INDIRECT,  /**< Indexed Indirect ($nn,X) */
    ADDR_INDIRECT_INDEXED   /**< Indirect Indexed ($nn),Y */
} addressing_mode_t;

/**
 * @brief Initialize CPU state
 *
 * @param cpu Pointer to CPU structure
 * @param memory Pointer to memory subsystem
 */
void cpu_init(cpu6502_t* cpu, void* memory);

/**
 * @brief Reset CPU (power-on or reset button)
 *
 * @param cpu Pointer to CPU structure
 */
void cpu_reset(cpu6502_t* cpu);

/**
 * @brief Execute one instruction
 *
 * @param cpu Pointer to CPU structure
 * @return Number of cycles consumed
 */
int cpu_step(cpu6502_t* cpu);

/**
 * @brief Execute N cycles
 *
 * @param cpu Pointer to CPU structure
 * @param cycles Number of cycles to execute
 * @return Actual cycles executed
 */
int cpu_execute_cycles(cpu6502_t* cpu, int cycles);

/**
 * @brief Trigger NMI (Non-Maskable Interrupt)
 *
 * @param cpu Pointer to CPU structure
 */
void cpu_nmi(cpu6502_t* cpu);

/**
 * @brief Trigger IRQ (Interrupt Request)
 *
 * @param cpu Pointer to CPU structure
 */
void cpu_irq(cpu6502_t* cpu);

/**
 * @brief Set/clear CPU flag
 *
 * @param cpu Pointer to CPU structure
 * @param flag Flag to modify
 * @param value true to set, false to clear
 */
void cpu_set_flag(cpu6502_t* cpu, cpu_flags_t flag, bool value);

/**
 * @brief Get CPU flag state
 *
 * @param cpu Pointer to CPU structure
 * @param flag Flag to check
 * @return true if set, false if clear
 */
bool cpu_get_flag(const cpu6502_t* cpu, cpu_flags_t flag);

/**
 * @brief Disassemble instruction at address
 *
 * @param cpu Pointer to CPU structure
 * @param address Address to disassemble
 * @param buffer Output buffer for disassembly string
 * @param buffer_size Size of output buffer
 * @return Number of bytes consumed by instruction
 */
int cpu_disassemble(const cpu6502_t* cpu, uint16_t address, char* buffer, size_t buffer_size);

/**
 * @brief Get CPU state as string (for debugging)
 *
 * @param cpu Pointer to CPU structure
 * @param buffer Output buffer
 * @param buffer_size Size of output buffer
 */
void cpu_get_state_string(const cpu6502_t* cpu, char* buffer, size_t buffer_size);

#endif /* CPU6502_H */
