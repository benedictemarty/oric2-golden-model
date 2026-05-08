/**
 * @file sd_device.c
 * @brief Émulation device SD bloc minimal (Sprint 2.j Oric 2)
 */

#include "io/sd_device.h"
#include "utils/logging.h"

#include <stdlib.h>
#include <string.h>

void sd_init(sd_device_t* sd) {
    if (!sd) return;
    memset(sd, 0, sizeof(*sd));
}

bool sd_load_image(sd_device_t* sd, const char* path) {
    if (!sd || !path) return false;
    sd->image = fopen(path, "rb");
    if (!sd->image) {
        log_error("sd: cannot open image '%s'", path);
        sd->image_valid = false;
        return false;
    }
    sd->image_valid = true;
    log_info("sd: loaded image %s", path);
    return true;
}

void sd_close(sd_device_t* sd) {
    if (!sd) return;
    if (sd->image) {
        fclose(sd->image);
        sd->image = NULL;
    }
    sd->image_valid = false;
}

uint8_t sd_read(sd_device_t* sd, uint8_t addr) {
    if (!sd) return 0xFF;
    switch (addr) {
        case 0: return (uint8_t)(sd->lba & 0xFF);
        case 1: return (uint8_t)((sd->lba >> 8) & 0xFF);
        case 2: return (uint8_t)((sd->lba >> 16) & 0xFF);
        case 3: return sd->busy ? 0x80 : 0x00;
        case 4:
            if (sd->byte_idx < SD_BLOCK_SIZE) {
                return sd->buffer[sd->byte_idx++];
            }
            return 0xFF;
        default:
            return 0xFF;
    }
}

void sd_write(sd_device_t* sd, uint8_t addr, uint8_t value) {
    if (!sd) return;
    switch (addr) {
        case 0: sd->lba = (sd->lba & 0xFFFF00u) | (uint32_t)value; break;
        case 1: sd->lba = (sd->lba & 0xFF00FFu) | ((uint32_t)value << 8); break;
        case 2: sd->lba = (sd->lba & 0x00FFFFu) | ((uint32_t)value << 16); break;
        case 3:
            /* CTRL : bit 0 = trigger lecture du bloc LBA. */
            if ((value & 0x01) && sd->image_valid && sd->image) {
                long off = (long)sd->lba * (long)SD_BLOCK_SIZE;
                if (fseek(sd->image, off, SEEK_SET) != 0) {
                    memset(sd->buffer, 0, SD_BLOCK_SIZE);
                } else {
                    size_t n = fread(sd->buffer, 1, SD_BLOCK_SIZE, sd->image);
                    if (n < SD_BLOCK_SIZE) {
                        memset(sd->buffer + n, 0, SD_BLOCK_SIZE - n);
                    }
                }
                sd->byte_idx = 0;
                /* Synchrone v0.1 : busy=0 dès que la lecture est terminée. */
                sd->busy = false;
            }
            break;
        default:
            break;
    }
}
