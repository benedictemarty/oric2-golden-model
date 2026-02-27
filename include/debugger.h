/**
 * @file debugger.h
 * @brief Interactive debugger for ORIC-1 emulator
 * @author bmarty <bmarty@mailo.com>
 * @date 2026-02-24
 * @version 1.1.0-alpha
 *
 * Provides breakpoints, watchpoints, single-step, and REPL
 * command interface for debugging ORIC programs.
 */

#ifndef DEBUGGER_H
#define DEBUGGER_H

#include <stdint.h>
#include <stdbool.h>

#define DEBUGGER_MAX_BREAKPOINTS 16
#define DEBUGGER_MAX_WATCHPOINTS 8

typedef struct {
    uint16_t breakpoints[DEBUGGER_MAX_BREAKPOINTS];
    int      num_breakpoints;

    uint16_t watchpoints[DEBUGGER_MAX_WATCHPOINTS];
    int      num_watchpoints;

    bool     watch_triggered;    /* Set by memory trace callback */
    uint16_t watch_addr_hit;     /* Which watchpoint address was hit */

    bool     active;             /* Debugger is in REPL mode */
    bool     step_mode;          /* Single-step after break */

    /* Temporary breakpoint for step-over (next command) */
    uint16_t temp_breakpoint;
    bool     has_temp_breakpoint;
} debugger_t;

/**
 * @brief Initialize debugger state
 */
void debugger_init(debugger_t* dbg);

/**
 * @brief Check if debugger should break before next instruction
 *
 * Called before each cpu_step(). Returns true if the debugger
 * should enter REPL mode (breakpoint hit, watchpoint triggered,
 * step mode, etc.)
 */
bool debugger_should_break(debugger_t* dbg, void* emu);

/**
 * @brief Interactive REPL command loop
 *
 * Reads commands from stdin and executes them.
 * Returns when the user continues execution (c/continue).
 */
void debugger_repl(debugger_t* dbg, void* emu);

/**
 * @brief Add a PC breakpoint
 * @return Index of added breakpoint, or -1 if full
 */
int debugger_add_breakpoint(debugger_t* dbg, uint16_t addr);

/**
 * @brief Remove a PC breakpoint by index
 * @return true if removed, false if index out of range
 */
bool debugger_remove_breakpoint(debugger_t* dbg, int index);

/**
 * @brief Add a memory write watchpoint
 * @return Index of added watchpoint, or -1 if full
 */
int debugger_add_watchpoint(debugger_t* dbg, uint16_t addr);

/**
 * @brief Remove a watchpoint by index
 * @return true if removed, false if index out of range
 */
bool debugger_remove_watchpoint(debugger_t* dbg, int index);

/**
 * @brief Check if a given PC address matches any breakpoint
 */
bool debugger_is_breakpoint(const debugger_t* dbg, uint16_t pc);

/**
 * @brief Install memory trace callback for watchpoints
 */
void debugger_install_watchpoint_trace(debugger_t* dbg, void* emu);

#endif /* DEBUGGER_H */
