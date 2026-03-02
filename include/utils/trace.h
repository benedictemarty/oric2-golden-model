/**
 * @file trace.h
 * @brief CPU trace logging — instruction-level execution trace to file
 * @author bmarty <bmarty@mailo.com>
 * @date 2026-03-02
 * @version 1.0.0-alpha
 *
 * Logs each CPU instruction with disassembly and register state.
 * Output format (one line per instruction):
 *   CCCCCCCC  AAAA  XX XX XX  MNEMONIC OPERAND       A=XX X=XX Y=XX SP=XX P=XX
 */

#ifndef TRACE_H
#define TRACE_H

#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>

#include "cpu/cpu6502.h"

/**
 * @brief CPU trace logger state
 */
typedef struct {
    FILE*    fp;            /**< Output file (NULL = inactive) */
    bool     active;        /**< Trace is active */
    uint64_t count;         /**< Number of instructions traced */
    uint64_t max_count;     /**< Max instructions to trace (0 = unlimited) */
    bool     owns_fp;       /**< True if we opened the file (must fclose) */
} cpu_trace_t;

/**
 * @brief Initialize trace logger (inactive by default)
 * @param trace Pointer to trace structure
 */
void trace_init(cpu_trace_t* trace);

/**
 * @brief Open trace output file and activate tracing
 * @param trace Pointer to trace structure
 * @param filename Output file path (NULL for stdout)
 * @return true on success
 */
bool trace_open(cpu_trace_t* trace, const char* filename);

/**
 * @brief Attach an already-opened FILE* for tracing
 * @param trace Pointer to trace structure
 * @param fp File pointer (caller retains ownership)
 */
void trace_attach(cpu_trace_t* trace, FILE* fp);

/**
 * @brief Log one instruction (call BEFORE cpu_step)
 *
 * Captures current PC, disassembles the instruction, and logs
 * the full register state in a single line.
 *
 * @param trace Pointer to trace structure
 * @param cpu Pointer to CPU (const — does not modify state)
 */
void trace_log_instruction(cpu_trace_t* trace, const cpu6502_t* cpu);

/**
 * @brief Close trace file and deactivate tracing
 * @param trace Pointer to trace structure
 */
void trace_close(cpu_trace_t* trace);

/**
 * @brief Set maximum number of instructions to trace
 * @param trace Pointer to trace structure
 * @param max Maximum count (0 = unlimited)
 */
void trace_set_max(cpu_trace_t* trace, uint64_t max);

#endif /* TRACE_H */
