/**
 * @file test_cpu.c
 * @brief CPU unit tests
 * @author bmarty <bmarty@mailo.com>
 * @date 2026-01-31
 * @version 0.1.0-alpha
 */

#include <stdio.h>
#include <assert.h>
#include "cpu/cpu6502.h"
#include "memory/memory.h"

void test_cpu_init() {
    cpu6502_t cpu;
    memory_t mem;

    memory_init(&mem);
    cpu_init(&cpu, &mem);

    assert(cpu.memory == &mem);
    printf("✓ test_cpu_init passed\n");
}

void test_cpu_reset() {
    cpu6502_t cpu;
    memory_t mem;

    memory_init(&mem);
    cpu_init(&cpu, &mem);
    cpu_reset(&cpu);

    assert(cpu.SP == 0xFD);
    assert(cpu.cycles == 0);
    printf("✓ test_cpu_reset passed\n");
}

int main() {
    printf("Running CPU tests...\n");
    test_cpu_init();
    test_cpu_reset();
    printf("All CPU tests passed!\n");
    return 0;
}
