/**
 * @file cpu6502.c
 * @brief 6502 CPU implementation (stub)
 * @author bmarty <bmarty@mailo.com>
 * @date 2026-01-31
 * @version 0.1.0-alpha
 */

#include "cpu/cpu6502.h"
#include "memory/memory.h"
#include <stdio.h>
#include <string.h>

void cpu_init(cpu6502_t* cpu, void* memory) {
    memset(cpu, 0, sizeof(cpu6502_t));
    cpu->memory = memory;
    cpu->P = FLAG_UNUSED | FLAG_INTERRUPT;
}

void cpu_reset(cpu6502_t* cpu) {
    memory_t* mem = (memory_t*)cpu->memory;
    cpu->PC = memory_read_word(mem, 0xFFFC);
    cpu->SP = 0xFD;
    cpu->P = FLAG_UNUSED | FLAG_INTERRUPT;
    cpu->cycles = 0;
    cpu->halted = false;
}

int cpu_step(cpu6502_t* cpu) {
    /* TODO: Implement instruction execution */
    cpu->cycles += 2;
    return 2;
}

int cpu_execute_cycles(cpu6502_t* cpu, int cycles) {
    int executed = 0;
    while (executed < cycles && !cpu->halted) {
        executed += cpu_step(cpu);
    }
    return executed;
}

void cpu_nmi(cpu6502_t* cpu) {
    cpu->nmi_pending = true;
}

void cpu_irq(cpu6502_t* cpu) {
    if (!cpu_get_flag(cpu, FLAG_INTERRUPT)) {
        cpu->irq_pending = true;
    }
}

void cpu_set_flag(cpu6502_t* cpu, cpu_flags_t flag, bool value) {
    if (value) {
        cpu->P |= flag;
    } else {
        cpu->P &= ~flag;
    }
}

bool cpu_get_flag(const cpu6502_t* cpu, cpu_flags_t flag) {
    return (cpu->P & flag) != 0;
}

int cpu_disassemble(const cpu6502_t* cpu, uint16_t address, char* buffer, size_t buffer_size) {
    /* TODO: Implement disassembly */
    snprintf(buffer, buffer_size, "NOP");
    return 1;
}

void cpu_get_state_string(const cpu6502_t* cpu, char* buffer, size_t buffer_size) {
    snprintf(buffer, buffer_size,
             "A:%02X X:%02X Y:%02X SP:%02X P:%02X PC:%04X",
             cpu->A, cpu->X, cpu->Y, cpu->SP, cpu->P, cpu->PC);
}
