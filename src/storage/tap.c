/**
 * @file tap.c
 * @brief TAP format implementation (stub)
 * @author bmarty <bmarty@mailo.com>
 * @date 2026-01-31
 * @version 0.1.0-alpha
 */

#include "storage/tap.h"
#include <stdlib.h>
#include <string.h>

tap_file_t* tap_open_read(const char* filename, bool fast_load) {
    /* TODO: Implement TAP reading */
    (void)filename;
    (void)fast_load;
    return NULL;
}

tap_file_t* tap_open_write(const char* filename) {
    /* TODO: Implement TAP writing */
    (void)filename;
    return NULL;
}

void tap_close(tap_file_t* tap) {
    if (tap) {
        if (tap->file) fclose(tap->file);
        if (tap->data) free(tap->data);
        free(tap);
    }
}

bool tap_read_header(tap_file_t* tap, tap_header_t* header) {
    /* TODO: Implement header reading */
    (void)tap;
    (void)header;
    return false;
}

bool tap_write_header(tap_file_t* tap, const tap_header_t* header) {
    /* TODO: Implement header writing */
    (void)tap;
    (void)header;
    return false;
}

int tap_read_data(tap_file_t* tap, uint8_t* buffer, size_t size) {
    /* TODO: Implement data reading */
    (void)tap;
    (void)buffer;
    (void)size;
    return -1;
}

bool tap_write_data(tap_file_t* tap, const uint8_t* buffer, size_t size) {
    /* TODO: Implement data writing */
    (void)tap;
    (void)buffer;
    (void)size;
    return false;
}

void tap_rewind(tap_file_t* tap) {
    if (tap && tap->file) {
        rewind(tap->file);
        tap->position = 0;
    }
}

uint32_t tap_tell(const tap_file_t* tap) {
    return tap ? tap->position : 0;
}

uint32_t tap_size(const tap_file_t* tap) {
    return tap ? tap->size : 0;
}

bool tap_eof(const tap_file_t* tap) {
    if (!tap) return true;
    return tap->position >= tap->size;
}

uint8_t tap_checksum(const uint8_t* data, size_t size) {
    uint8_t sum = 0;
    for (size_t i = 0; i < size; i++) {
        sum += data[i];
    }
    return sum;
}

bool tap_from_basic(const char* basic_file, const char* tap_file, bool auto_run) {
    /* TODO: Implement BASIC to TAP conversion */
    (void)basic_file;
    (void)tap_file;
    (void)auto_run;
    return false;
}

bool tap_from_binary(const char* bin_file, const char* tap_file,
                    uint16_t start_addr, uint16_t exec_addr, const char* name) {
    /* TODO: Implement binary to TAP conversion */
    (void)bin_file;
    (void)tap_file;
    (void)start_addr;
    (void)exec_addr;
    (void)name;
    return false;
}
