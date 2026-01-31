/**
 * @file tap2sedoric.c
 * @brief TAP to Sedoric disk converter
 * @author bmarty <bmarty@mailo.com>
 * @date 2026-01-31
 * @version 0.1.0-alpha
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "storage/tap.h"
#include "utils/logging.h"

int main(int argc, char* argv[]) {
    if (argc < 4) {
        printf("Usage: %s <input.tap> -o <output.dsk>\n", argv[0]);
        return 1;
    }

    const char* input_file = argv[1];
    const char* output_file = NULL;

    for (int i = 2; i < argc; i++) {
        if (strcmp(argv[i], "-o") == 0 && i + 1 < argc) {
            output_file = argv[++i];
        }
    }

    if (!output_file) {
        fprintf(stderr, "Error: Output file not specified\n");
        return 1;
    }

    printf("Converting TAP to Sedoric...\n");
    printf("Input:  %s\n", input_file);
    printf("Output: %s\n", output_file);

    fprintf(stderr, "Conversion not yet implemented\n");
    return 1;
}
