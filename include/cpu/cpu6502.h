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

#include "cpu/cpu_types.h"
/* memory_t, cpu_flags_t (FLAG_*), cpu_irq_source_t (IRQF_*) sont définis
 * dans cpu_types.h depuis ADR-18 étape 1.A (2026-05-09). Conservés via
 * include pour rétro-compat des consommateurs historiques de cpu6502.h. */

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
    uint8_t  irq;           /**< IRQ source bitfield (level-triggered) */

    memory_t* memory;       /**< Pointer to memory subsystem */
} cpu6502_t;

/* addressing_mode_t déplacé vers `opcode_metadata.h` en PH-2.c.2 (ADR-18
 * étape 1.C). Inclu via cpu_types.h ? Non — pour rester rétro-compat des
 * consommateurs de cpu6502.h, on inclut directement opcode_metadata.h ici. */
#include "cpu/opcode_metadata.h"

/**
 * @brief Initialize CPU state
 *
 * @param cpu Pointer to CPU structure
 * @param memory Pointer to memory subsystem
 */
void cpu_init(cpu6502_t* cpu, memory_t* memory);

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
 * @brief Assert IRQ source (level-triggered)
 *
 * Sets the specified IRQ source bit. The CPU will take an interrupt
 * whenever any IRQ bit is set and the I flag is clear.
 *
 * @param cpu Pointer to CPU structure
 * @param source IRQ source flag (IRQF_VIA, IRQF_DISK, etc.)
 */
void cpu_irq_set(cpu6502_t* cpu, cpu_irq_source_t source);

/**
 * @brief Deassert IRQ source (level-triggered)
 *
 * Clears the specified IRQ source bit. If no other IRQ sources
 * remain asserted, the CPU will not take further interrupts.
 *
 * @param cpu Pointer to CPU structure
 * @param source IRQ source flag to clear
 */
void cpu_irq_clear(cpu6502_t* cpu, cpu_irq_source_t source);

/**
 * @brief Trigger IRQ (legacy edge-triggered, deprecated)
 *
 * @param cpu Pointer to CPU structure
 * @deprecated Use cpu_irq_set() / cpu_irq_clear() instead
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
