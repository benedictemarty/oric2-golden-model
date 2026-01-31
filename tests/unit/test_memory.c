/**
 * @file test_memory.c
 * @brief Memory unit tests
 * @author bmarty <bmarty@mailo.com>
 * @date 2026-01-31
 * @version 0.1.0-alpha
 */

#include <stdio.h>
#include <assert.h>
#include "memory/memory.h"

void test_memory_init() {
    memory_t mem;
    assert(memory_init(&mem));
    printf("✓ test_memory_init passed\n");
}

void test_memory_read_write() {
    memory_t mem;
    memory_init(&mem);

    memory_write(&mem, 0x0100, 0x42);
    assert(memory_read(&mem, 0x0100) == 0x42);

    printf("✓ test_memory_read_write passed\n");
}

int main() {
    printf("Running memory tests...\n");
    test_memory_init();
    test_memory_read_write();
    printf("All memory tests passed!\n");
    return 0;
}
