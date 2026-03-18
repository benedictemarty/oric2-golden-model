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

    /* Read raw bytes for format auto-detection.
     * TAP headers come in two variants:
     *   9-byte (ROM format): type auto extra extra end_hi end_lo start_hi start_lo unused
     *   7-byte (tool format): type auto end_hi end_lo start_hi start_lo separator
     * Some TAP files also have 1-4 padding bytes between the $24 marker
     * and the actual header (e.g. poker-asn.tap has $52 $54 padding). */
    uint32_t save_pos = tap->position;
    uint8_t raw[32];
    int raw_len = 0;
    for (int i = 0; i < 32 && !tap_eof(tap); i++) {
        raw[i] = read_byte(tap);
        raw_len++;
    }

    int best_off = 0, best_sz = 9;
    bool found = false;

    /* Try no-padding 9-byte first (standard ROM format, handles TYRANN) */
    if (raw_len >= 9) {
        uint8_t t = raw[0];
        if (t == 0x00 || t == 0x80 || t == 0xC0) {
            uint16_t end = (uint16_t)((raw[4] << 8) | raw[5]);
            uint16_t start = (uint16_t)((raw[6] << 8) | raw[7]);
            if (start < end && start >= 0x0100 && end < 0xC000) {
                best_off = 0; best_sz = 9; found = true;
            }
        }
    }

    /* If not found, try padding (0-4) + 7-byte, then padding + 9-byte */
    if (!found) {
        for (int skip = 0; skip <= 4 && !found; skip++) {
            if (skip + 7 > raw_len) break;
            uint8_t t = raw[skip];
            if (t != 0x00 && t != 0x80 && t != 0xC0) continue;

            /* Try 7-byte format */
            if (skip + 7 <= raw_len) {
                uint16_t end = (uint16_t)((raw[skip + 2] << 8) | raw[skip + 3]);
                uint16_t start = (uint16_t)((raw[skip + 4] << 8) | raw[skip + 5]);
                if (start < end && start >= 0x0100 && end < 0xC000) {
                    best_off = skip; best_sz = 7; found = true;
                    break;
                }
            }
            /* Try 9-byte format with padding */
            if (skip > 0 && skip + 9 <= raw_len) {
                uint16_t end = (uint16_t)((raw[skip + 4] << 8) | raw[skip + 5]);
                uint16_t start = (uint16_t)((raw[skip + 6] << 8) | raw[skip + 7]);
                if (start < end && start >= 0x0100 && end < 0xC000) {
                    best_off = skip; best_sz = 9; found = true;
                    break;
                }
            }
        }
    }

    /* Fallback: 9-byte at offset 0 (original behavior) */

    /* Parse header from raw buffer */
    int p = best_off;
    header->type = raw[p++];
    header->auto_run = raw[p++];
    if (best_sz == 9) {
        p += 2;  /* skip extra bytes (ROM $64/$63) */
    }
    header->end_addr = (uint16_t)((raw[p] << 8) | raw[p + 1]);
    p += 2;
    header->start_addr = (uint16_t)((raw[p] << 8) | raw[p + 1]);
    p += 2;
    p++;  /* skip separator (7-byte) or unused byte (9-byte) */

    /* Read program name from raw buffer */
    memset(header->name, 0, TAP_NAME_LEN);
    for (int i = 0; i < TAP_NAME_LEN - 1 && p < raw_len; i++, p++) {
        if (raw[p] == 0) { p++; break; }
        header->name[i] = (char)raw[p];
    }

    /* Reposition stream to just after name's null terminator */
    tap->position = save_pos + p;
    if (!tap->fast_load && tap->file) {
        fseek(tap->file, (long)tap->position, SEEK_SET);
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
