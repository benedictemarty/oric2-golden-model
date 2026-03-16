/**
 * @file tap.c
 * @brief ORIC .TAP tape format - complete implementation
 * @author bmarty <bmarty@mailo.com>
 * @date 2026-02-22
 * @version 0.6.0-alpha
 */

#include "storage/tap.h"
#include <stdlib.h>
#include <string.h>

tap_file_t* tap_open_read(const char* filename, bool fast_load) {
    FILE* fp = fopen(filename, "rb");
    if (!fp) return NULL;

    tap_file_t* tap = (tap_file_t*)calloc(1, sizeof(tap_file_t));
    if (!tap) { fclose(fp); return NULL; }

    fseek(fp, 0, SEEK_END);
    tap->size = (uint32_t)ftell(fp);
    fseek(fp, 0, SEEK_SET);

    tap->file = fp;
    tap->writing = false;
    tap->position = 0;
    tap->fast_load = fast_load;

    if (fast_load && tap->size > 0) {
        tap->data = (uint8_t*)malloc(tap->size);
        if (tap->data) {
            if (fread(tap->data, 1, tap->size, fp) != tap->size) {
                free(tap->data);
                tap->data = NULL;
                tap->fast_load = false;
            }
        }
    }

    return tap;
}

tap_file_t* tap_open_write(const char* filename) {
    FILE* fp = fopen(filename, "wb");
    if (!fp) return NULL;

    tap_file_t* tap = (tap_file_t*)calloc(1, sizeof(tap_file_t));
    if (!tap) { fclose(fp); return NULL; }

    tap->file = fp;
    tap->writing = true;
    tap->position = 0;
    tap->size = 0;
    tap->fast_load = false;
    tap->data = NULL;

    return tap;
}

void tap_close(tap_file_t* tap) {
    if (tap) {
        if (tap->file) fclose(tap->file);
        if (tap->data) free(tap->data);
        free(tap);
    }
}

/* Skip sync bytes ($16), return number skipped */
static int skip_sync(tap_file_t* tap) {
    int count = 0;
    while (!tap_eof(tap)) {
        uint8_t byte;
        if (tap->fast_load && tap->data) {
            byte = tap->data[tap->position];
        } else {
            if (fread(&byte, 1, 1, tap->file) != 1) break;
        }
        if (byte == TAP_SYNC_BYTE) {
            tap->position++;
            count++;
        } else {
            /* Not a sync byte, push back */
            if (!tap->fast_load) fseek(tap->file, -1, SEEK_CUR);
            break;
        }
    }
    return count;
}

static uint8_t read_byte(tap_file_t* tap) {
    uint8_t byte = 0;
    if (tap->fast_load && tap->data) {
        if (tap->position < tap->size) byte = tap->data[tap->position];
    } else {
        if (fread(&byte, 1, 1, tap->file) != 1) return 0;
    }
    tap->position++;
    return byte;
}

bool tap_read_header(tap_file_t* tap, tap_header_t* header) {
    if (!tap || !header) return false;

    /* Skip sync bytes */
    skip_sync(tap);

    /* Read marker byte */
    uint8_t marker = read_byte(tap);
    if (marker != TAP_MARKER) return false;

    /* Read 9 header bytes — matching the ORIC ROM's CLOAD routine which
     * reads exactly 9 bytes after the $24 marker and stores them at
     * zero page $5E-$66 in reverse order. The ROM then uses:
     *   start_addr = ($60,$5F) = bytes 7-8
     *   end_addr   = ($62,$61) = bytes 5-6
     *   type/flag  = $66       = byte 1
     *   auto       = $65       = byte 2
     */
    header->type = read_byte(tap);         /* byte 1 → $66 */
    header->auto_run = read_byte(tap);     /* byte 2 → $65 */
    read_byte(tap);                        /* byte 3 → $64 (extra) */
    read_byte(tap);                        /* byte 4 → $63 (extra) */
    uint8_t end_hi = read_byte(tap);       /* byte 5 → $62 */
    uint8_t end_lo = read_byte(tap);       /* byte 6 → $61 */
    header->end_addr = (uint16_t)((end_hi << 8) | end_lo);
    uint8_t start_hi = read_byte(tap);     /* byte 7 → $60 */
    uint8_t start_lo = read_byte(tap);     /* byte 8 → $5F */
    header->start_addr = (uint16_t)((start_hi << 8) | start_lo);
    read_byte(tap);                        /* byte 9 → $5E (unused) */

    /* Read program name (null-terminated, up to 16 chars) */
    memset(header->name, 0, TAP_NAME_LEN);
    for (int i = 0; i < TAP_NAME_LEN; i++) {
        uint8_t ch = read_byte(tap);
        if (ch == 0) break;
        header->name[i] = (char)ch;
    }

    return true;
}

bool tap_write_header(tap_file_t* tap, const tap_header_t* header) {
    if (!tap || !header || !tap->writing) return false;

    /* Write sync bytes */
    uint8_t sync = TAP_SYNC_BYTE;
    int sync_count = header->sync_len ? header->sync_len : 3;
    for (int i = 0; i < sync_count; i++) {
        fwrite(&sync, 1, 1, tap->file);
        tap->position++;
    }

    /* Marker */
    uint8_t marker = TAP_MARKER;
    fwrite(&marker, 1, 1, tap->file); tap->position++;

    /* Type and auto-run */
    fwrite(&header->type, 1, 1, tap->file); tap->position++;
    fwrite(&header->auto_run, 1, 1, tap->file); tap->position++;

    /* End address (big-endian) */
    uint8_t hi = (uint8_t)(header->end_addr >> 8);
    uint8_t lo = (uint8_t)(header->end_addr & 0xFF);
    fwrite(&hi, 1, 1, tap->file); tap->position++;
    fwrite(&lo, 1, 1, tap->file); tap->position++;

    /* Start address (big-endian) */
    hi = (uint8_t)(header->start_addr >> 8);
    lo = (uint8_t)(header->start_addr & 0xFF);
    fwrite(&hi, 1, 1, tap->file); tap->position++;
    fwrite(&lo, 1, 1, tap->file); tap->position++;

    /* Null separator */
    uint8_t null_byte = 0;
    fwrite(&null_byte, 1, 1, tap->file); tap->position++;

    /* Name (null-terminated) */
    size_t namelen = strlen(header->name);
    if (namelen > TAP_NAME_LEN - 1) namelen = TAP_NAME_LEN - 1;
    fwrite(header->name, 1, namelen, tap->file); tap->position += (uint32_t)namelen;
    fwrite(&null_byte, 1, 1, tap->file); tap->position++;

    tap->size = tap->position;
    return true;
}

int tap_read_data(tap_file_t* tap, uint8_t* buffer, size_t size) {
    if (!tap || !buffer) return -1;

    size_t read_count = 0;
    for (size_t i = 0; i < size && !tap_eof(tap); i++) {
        buffer[i] = read_byte(tap);
        read_count++;
    }
    return (int)read_count;
}

bool tap_write_data(tap_file_t* tap, const uint8_t* buffer, size_t size) {
    if (!tap || !buffer || !tap->writing) return false;

    size_t written = fwrite(buffer, 1, size, tap->file);
    tap->position += (uint32_t)written;
    tap->size = tap->position;
    return written == size;
}

void tap_rewind(tap_file_t* tap) {
    if (tap) {
        if (tap->file) rewind(tap->file);
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
    for (size_t i = 0; i < size; i++) sum ^= data[i];
    return sum;
}

bool tap_from_basic(const char* basic_file, const char* tap_filename, bool auto_run) {
    FILE* fp = fopen(basic_file, "rb");
    if (!fp) return false;

    fseek(fp, 0, SEEK_END);
    long fsize = ftell(fp);
    fseek(fp, 0, SEEK_SET);

    if (fsize <= 0 || fsize > 48000) { fclose(fp); return false; }

    uint8_t* data = (uint8_t*)malloc((size_t)fsize);
    if (!data) { fclose(fp); return false; }
    if (fread(data, 1, (size_t)fsize, fp) != (size_t)fsize) {
        free(data); fclose(fp); return false;
    }
    fclose(fp);

    tap_file_t* tap = tap_open_write(tap_filename);
    if (!tap) { free(data); return false; }

    tap_header_t header;
    memset(&header, 0, sizeof(header));
    header.sync_len = 3;
    header.type = TAP_BASIC;
    header.auto_run = auto_run ? 0x80 : 0x00;
    header.start_addr = 0x0501; /* Standard BASIC start */
    header.end_addr = header.start_addr + (uint16_t)fsize - 1;

    /* Extract name from filename */
    const char* base = strrchr(basic_file, '/');
    base = base ? base + 1 : basic_file;
    strncpy(header.name, base, TAP_NAME_LEN - 1);
    /* Remove extension */
    char* dot = strrchr(header.name, '.');
    if (dot) *dot = '\0';

    tap_write_header(tap, &header);
    tap_write_data(tap, data, (size_t)fsize);

    tap_close(tap);
    free(data);
    return true;
}

bool tap_from_binary(const char* bin_file, const char* tap_filename,
                    uint16_t start_addr, uint16_t exec_addr, const char* name) {
    FILE* fp = fopen(bin_file, "rb");
    if (!fp) return false;

    fseek(fp, 0, SEEK_END);
    long fsize = ftell(fp);
    fseek(fp, 0, SEEK_SET);

    if (fsize <= 0 || fsize > 48000) { fclose(fp); return false; }

    uint8_t* data = (uint8_t*)malloc((size_t)fsize);
    if (!data) { fclose(fp); return false; }
    if (fread(data, 1, (size_t)fsize, fp) != (size_t)fsize) {
        free(data); fclose(fp); return false;
    }
    fclose(fp);

    tap_file_t* tap = tap_open_write(tap_filename);
    if (!tap) { free(data); return false; }

    tap_header_t header;
    memset(&header, 0, sizeof(header));
    header.sync_len = 3;
    header.type = TAP_MACHINE;
    header.auto_run = (exec_addr != 0) ? 0x80 : 0x00;
    header.start_addr = start_addr;
    header.end_addr = start_addr + (uint16_t)fsize - 1;
    if (name) strncpy(header.name, name, TAP_NAME_LEN - 1);

    tap_write_header(tap, &header);
    tap_write_data(tap, data, (size_t)fsize);

    tap_close(tap);
    free(data);
    return true;
}
