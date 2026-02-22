/**
 * @file disk.c
 * @brief FDC WD1793 disk controller emulation
 * @author bmarty <bmarty@mailo.com>
 * @date 2026-02-22
 * @version 0.7.0-alpha
 */

#include <stdint.h>
#include <stdbool.h>
#include <string.h>

/* FDC Registers */
#define FDC_STATUS  0
#define FDC_COMMAND 0
#define FDC_TRACK   1
#define FDC_SECTOR  2
#define FDC_DATA    3

/* Status bits */
#define FDC_ST_BUSY      0x01
#define FDC_ST_DRQ       0x02
#define FDC_ST_LOST_DATA 0x04
#define FDC_ST_CRC_ERROR 0x08
#define FDC_ST_SEEK_ERR  0x10
#define FDC_ST_NOT_FOUND 0x10
#define FDC_ST_WRITE_PROT 0x40

typedef struct {
    uint8_t status;
    uint8_t command;
    uint8_t track;
    uint8_t sector;
    uint8_t data;
    uint8_t direction; /* 0 = step in, 1 = step out */
    bool busy;
    bool drq;         /* Data Request */
    bool irq;         /* Interrupt Request */

    /* Disk data pointer (from sedoric) */
    uint8_t* disk_data;
    uint32_t disk_size;
    uint8_t tracks;
    uint8_t sectors_per_track;

    /* Data transfer buffer */
    uint8_t sector_buf[256];
    int buf_pos;
    int buf_len;
    bool reading;
    bool writing;
} fdc_t;

void fdc_init(fdc_t* fdc) {
    memset(fdc, 0, sizeof(fdc_t));
    fdc->tracks = 42;
    fdc->sectors_per_track = 17;
}

void fdc_reset(fdc_t* fdc) {
    fdc->status = 0;
    fdc->track = 0;
    fdc->sector = 1;
    fdc->busy = false;
    fdc->drq = false;
    fdc->irq = false;
    fdc->buf_pos = 0;
    fdc->buf_len = 0;
    fdc->reading = false;
    fdc->writing = false;
}

void fdc_set_disk(fdc_t* fdc, uint8_t* data, uint32_t size) {
    fdc->disk_data = data;
    fdc->disk_size = size;
}

static uint8_t* fdc_get_sector_ptr(fdc_t* fdc) {
    if (!fdc->disk_data) return NULL;
    if (fdc->track >= fdc->tracks || fdc->sector == 0 || fdc->sector > fdc->sectors_per_track)
        return NULL;
    uint32_t offset = ((uint32_t)fdc->track * fdc->sectors_per_track + (fdc->sector - 1)) * 256;
    if (offset + 256 > fdc->disk_size) return NULL;
    return &fdc->disk_data[offset];
}

void fdc_write_command(fdc_t* fdc, uint8_t cmd) {
    fdc->command = cmd;
    uint8_t type = cmd >> 4;

    if (cmd == 0xD0) { /* Force interrupt */
        fdc->busy = false;
        fdc->reading = false;
        fdc->writing = false;
        fdc->irq = true;
        fdc->status &= ~FDC_ST_BUSY;
        return;
    }

    if (type <= 1) {
        /* Type I: Restore, Seek, Step */
        fdc->busy = true;
        fdc->status |= FDC_ST_BUSY;

        if ((cmd & 0xF0) == 0x00) { /* Restore */
            fdc->track = 0;
        } else if ((cmd & 0xF0) == 0x10) { /* Seek */
            fdc->track = fdc->data;
        } else { /* Step */
            if (cmd & 0x40) { /* Step in/out */
                fdc->direction = (cmd & 0x20) ? 1 : 0;
            }
            if (fdc->direction == 0 && fdc->track < fdc->tracks - 1) fdc->track++;
            else if (fdc->direction == 1 && fdc->track > 0) fdc->track--;
        }

        fdc->busy = false;
        fdc->status &= ~FDC_ST_BUSY;
        fdc->irq = true;

    } else if (type >= 8 && type <= 9) {
        /* Type II: Read Sector */
        uint8_t* src = fdc_get_sector_ptr(fdc);
        if (src) {
            memcpy(fdc->sector_buf, src, 256);
            fdc->buf_pos = 0;
            fdc->buf_len = 256;
            fdc->reading = true;
            fdc->drq = true;
            fdc->status |= FDC_ST_DRQ | FDC_ST_BUSY;
        } else {
            fdc->status |= FDC_ST_NOT_FOUND;
            fdc->irq = true;
        }

    } else if (type >= 0xA && type <= 0xB) {
        /* Type II: Write Sector */
        uint8_t* dst = fdc_get_sector_ptr(fdc);
        if (dst) {
            fdc->buf_pos = 0;
            fdc->buf_len = 256;
            fdc->writing = true;
            fdc->drq = true;
            fdc->status |= FDC_ST_DRQ | FDC_ST_BUSY;
        } else {
            fdc->status |= FDC_ST_NOT_FOUND;
            fdc->irq = true;
        }
    }
}

uint8_t fdc_read(fdc_t* fdc, uint8_t reg) {
    switch (reg & 3) {
    case FDC_STATUS:
        fdc->irq = false;
        return fdc->status;
    case FDC_TRACK:
        return fdc->track;
    case FDC_SECTOR:
        return fdc->sector;
    case FDC_DATA:
        if (fdc->reading && fdc->buf_pos < fdc->buf_len) {
            fdc->data = fdc->sector_buf[fdc->buf_pos++];
            if (fdc->buf_pos >= fdc->buf_len) {
                fdc->reading = false;
                fdc->drq = false;
                fdc->busy = false;
                fdc->status &= ~(FDC_ST_DRQ | FDC_ST_BUSY);
                fdc->irq = true;
            }
        }
        return fdc->data;
    }
    return 0xFF;
}

void fdc_write(fdc_t* fdc, uint8_t reg, uint8_t value) {
    switch (reg & 3) {
    case FDC_COMMAND:
        fdc_write_command(fdc, value);
        break;
    case FDC_TRACK:
        fdc->track = value;
        break;
    case FDC_SECTOR:
        fdc->sector = value;
        break;
    case FDC_DATA:
        fdc->data = value;
        if (fdc->writing && fdc->buf_pos < fdc->buf_len) {
            fdc->sector_buf[fdc->buf_pos++] = value;
            if (fdc->buf_pos >= fdc->buf_len) {
                /* Write buffer to disk */
                uint8_t* dst = fdc_get_sector_ptr(fdc);
                if (dst) memcpy(dst, fdc->sector_buf, 256);
                fdc->writing = false;
                fdc->drq = false;
                fdc->busy = false;
                fdc->status &= ~(FDC_ST_DRQ | FDC_ST_BUSY);
                fdc->irq = true;
            }
        }
        break;
    }
}
