/**
 * @file sedoric.c
 * @brief Sedoric disk filesystem implementation
 * @author bmarty <bmarty@mailo.com>
 * @date 2026-02-22
 * @version 0.7.0-alpha
 */

#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

#define SEDORIC_SECTOR_SIZE   256
#define SEDORIC_TRACKS        42
#define SEDORIC_SECTORS       17
#define SEDORIC_SIDES         1
#define SEDORIC_DISK_SIZE     (SEDORIC_TRACKS * SEDORIC_SECTORS * SEDORIC_SECTOR_SIZE)
#define SEDORIC_DIR_TRACK     20
#define SEDORIC_DIR_SECTOR    4
#define SEDORIC_MAX_NAME      9
#define SEDORIC_MAX_EXT       3

typedef struct {
    uint8_t* data;
    uint32_t size;
    uint8_t tracks;
    uint8_t sectors;
    uint8_t sides;
    bool modified;
} sedoric_disk_t;

typedef struct {
    char name[SEDORIC_MAX_NAME + 1];
    char ext[SEDORIC_MAX_EXT + 1];
    uint8_t start_track;
    uint8_t start_sector;
    uint16_t size;
    uint8_t type;
} sedoric_entry_t;

sedoric_disk_t* sedoric_create(void) {
    sedoric_disk_t* disk = (sedoric_disk_t*)calloc(1, sizeof(sedoric_disk_t));
    if (!disk) return NULL;
    disk->data = (uint8_t*)calloc(1, SEDORIC_DISK_SIZE);
    if (!disk->data) { free(disk); return NULL; }
    disk->size = SEDORIC_DISK_SIZE;
    disk->tracks = SEDORIC_TRACKS;
    disk->sectors = SEDORIC_SECTORS;
    disk->sides = SEDORIC_SIDES;
    disk->modified = false;

    /* Initialize system sectors with basic Sedoric structure */
    /* Track 0, Sector 1: System info */
    uint8_t* sys = disk->data;
    sys[0] = 'S'; sys[1] = 'E'; sys[2] = 'D';
    sys[3] = disk->tracks;
    sys[4] = disk->sectors;

    return disk;
}

sedoric_disk_t* sedoric_load(const char* filename) {
    FILE* fp = fopen(filename, "rb");
    if (!fp) return NULL;

    fseek(fp, 0, SEEK_END);
    long fsize = ftell(fp);
    fseek(fp, 0, SEEK_SET);

    sedoric_disk_t* disk = (sedoric_disk_t*)calloc(1, sizeof(sedoric_disk_t));
    if (!disk) { fclose(fp); return NULL; }

    disk->data = (uint8_t*)malloc((size_t)fsize);
    if (!disk->data) { free(disk); fclose(fp); return NULL; }

    if (fread(disk->data, 1, (size_t)fsize, fp) != (size_t)fsize) {
        free(disk->data); free(disk); fclose(fp); return NULL;
    }
    fclose(fp);

    disk->size = (uint32_t)fsize;
    disk->tracks = SEDORIC_TRACKS;
    disk->sectors = SEDORIC_SECTORS;
    disk->sides = SEDORIC_SIDES;
    disk->modified = false;
    return disk;
}

bool sedoric_save(sedoric_disk_t* disk, const char* filename) {
    if (!disk || !disk->data) return false;
    FILE* fp = fopen(filename, "wb");
    if (!fp) return false;
    bool ok = fwrite(disk->data, 1, disk->size, fp) == disk->size;
    fclose(fp);
    if (ok) disk->modified = false;
    return ok;
}

void sedoric_destroy(sedoric_disk_t* disk) {
    if (disk) {
        free(disk->data);
        free(disk);
    }
}

uint8_t* sedoric_get_sector(sedoric_disk_t* disk, uint8_t track, uint8_t sector) {
    if (!disk || !disk->data) return NULL;
    if (track >= disk->tracks || sector >= disk->sectors || sector == 0) return NULL;
    uint32_t offset = (uint32_t)((track * disk->sectors + (sector - 1)) * SEDORIC_SECTOR_SIZE);
    if (offset + SEDORIC_SECTOR_SIZE > disk->size) return NULL;
    return &disk->data[offset];
}

bool sedoric_read_sector(sedoric_disk_t* disk, uint8_t track, uint8_t sector,
                         uint8_t* buffer) {
    uint8_t* src = sedoric_get_sector(disk, track, sector);
    if (!src) return false;
    memcpy(buffer, src, SEDORIC_SECTOR_SIZE);
    return true;
}

bool sedoric_write_sector(sedoric_disk_t* disk, uint8_t track, uint8_t sector,
                          const uint8_t* buffer) {
    uint8_t* dst = sedoric_get_sector(disk, track, sector);
    if (!dst) return false;
    memcpy(dst, buffer, SEDORIC_SECTOR_SIZE);
    disk->modified = true;
    return true;
}
