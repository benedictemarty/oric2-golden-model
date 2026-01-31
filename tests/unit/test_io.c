/**
 * @file test_io.c
 * @brief I/O unit tests
 * @author bmarty <bmarty@mailo.com>
 * @date 2026-01-31
 * @version 0.1.0-alpha
 */

#include <stdio.h>
#include <assert.h>
#include "io/via6522.h"

void test_via_init() {
    via6522_t via;
    via_init(&via);
    assert(via.t1_counter == 0);
    printf("✓ test_via_init passed\n");
}

int main() {
    printf("Running I/O tests...\n");
    test_via_init();
    printf("All I/O tests passed!\n");
    return 0;
}
