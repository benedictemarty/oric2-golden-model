/**
 * @file sedoric.c
 * @brief Sedoric disk filesystem implementation
 * @author bmarty <bmarty@mailo.com>
 * @date 2026-02-22
 * @version 0.7.0-alpha
 */

#include "storage/sedoric.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

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

/**
 * @brief Extract sectors from MFM track data
 *
 * Scans raw MFM track data for sector address marks ($A1 $A1 $A1 $FE)
 * and data marks ($A1 $A1 $A1 $FB), then copies 256-byte sector data
 * into the flat sector array at the correct position.
 *
 * Layout in flat array: [side0_track0_sec1..sec17, side0_track1_sec1..sec17, ...,
 *                        side1_track0_sec1..sec17, ...]
 */
static int mfm_extract_track(const uint8_t* track_data, uint8_t* flat,
                              uint8_t expected_track, uint8_t expected_side,
                              uint8_t sectors_per_track, uint8_t num_tracks) {
    int found = 0;
    for (int i = 0; i < MFM_TRACK_SIZE - 4; i++) {
        /* Look for sector ID address mark: $A1 $A1 $A1 $FE */
        if (track_data[i] == 0xA1 && track_data[i+1] == 0xA1 &&
            track_data[i+2] == 0xA1 && track_data[i+3] == 0xFE) {
            uint8_t id_track  = track_data[i+4];
            uint8_t id_side   = track_data[i+5];
            uint8_t id_sector = track_data[i+6];
            /* uint8_t id_size = track_data[i+7]; -- always 1 = 256 bytes */

            if (id_sector < 1 || id_sector > sectors_per_track) continue;

            /* Find data mark ($A1 $A1 $A1 $FB) after ID + CRC + gap */
            for (int j = i + 10; j < i + 60 && j < MFM_TRACK_SIZE - 260; j++) {
                if (track_data[j] == 0xA1 && track_data[j+1] == 0xA1 &&
                    track_data[j+2] == 0xA1 && track_data[j+3] == 0xFB) {
                    /* Calculate flat array offset:
                     * side * (num_tracks * sectors_per_track) + track * sectors_per_track + (sector-1) */
                    uint32_t offset = ((uint32_t)id_side * num_tracks * sectors_per_track +
                                       (uint32_t)id_track * sectors_per_track +
                                       (uint32_t)(id_sector - 1)) * SEDORIC_SECTOR_SIZE;
                    memcpy(flat + offset, &track_data[j+4], SEDORIC_SECTOR_SIZE);
                    found++;
                    break;
                }
            }
            (void)expected_track;
            (void)expected_side;
        }
    }
    return found;
}

sedoric_disk_t* sedoric_load(const char* filename) {
    FILE* fp = fopen(filename, "rb");
    if (!fp) return NULL;

    fseek(fp, 0, SEEK_END);
    long fsize = ftell(fp);
    fseek(fp, 0, SEEK_SET);

    /* Read entire file */
    uint8_t* raw = (uint8_t*)malloc((size_t)fsize);
    if (!raw) { fclose(fp); return NULL; }
    if (fread(raw, 1, (size_t)fsize, fp) != (size_t)fsize) {
        free(raw); fclose(fp); return NULL;
    }
    fclose(fp);

    sedoric_disk_t* disk = (sedoric_disk_t*)calloc(1, sizeof(sedoric_disk_t));
    if (!disk) { free(raw); return NULL; }

    /* Check for MFM_DISK format */
    if (fsize > MFM_DISK_HEADER_SIZE && memcmp(raw, "MFM_DISK", 8) == 0) {
        /* Parse MFM_DISK header (little-endian uint32) */
        uint32_t sides  = raw[8]  | ((uint32_t)raw[9]  << 8) | ((uint32_t)raw[10] << 16) | ((uint32_t)raw[11] << 24);
        uint32_t tracks = raw[12] | ((uint32_t)raw[13] << 8) | ((uint32_t)raw[14] << 16) | ((uint32_t)raw[15] << 24);

        if (sides > MFM_MAX_SIDES) sides = MFM_MAX_SIDES;
        if (tracks > MFM_MAX_TRACKS) tracks = MFM_MAX_TRACKS;

        uint8_t sectors_per_track = MFM_MAX_SECTORS; /* 17 sectors per track */

        /* Allocate flat sector array: sides * tracks * sectors * 256 */
        uint32_t flat_size = sides * tracks * sectors_per_track * SEDORIC_SECTOR_SIZE;
        disk->data = (uint8_t*)calloc(1, flat_size);
        if (!disk->data) { free(disk); free(raw); return NULL; }

        /* Extract sectors from each MFM track */
        int total_sectors = 0;
        for (uint32_t s = 0; s < sides; s++) {
            for (uint32_t t = 0; t < tracks; t++) {
                uint32_t track_idx = s * tracks + t; /* side 0 all tracks, then side 1 all tracks */
                uint32_t raw_offset = MFM_DISK_HEADER_SIZE + track_idx * MFM_TRACK_SIZE;
                if (raw_offset + MFM_TRACK_SIZE > (uint32_t)fsize) break;
                total_sectors += mfm_extract_track(&raw[raw_offset], disk->data,
                                                    (uint8_t)t, (uint8_t)s,
                                                    sectors_per_track, (uint8_t)tracks);
            }
        }

        disk->size = flat_size;
        disk->tracks = (uint8_t)tracks;
        disk->sectors = sectors_per_track;
        disk->sides = (uint8_t)sides;
        disk->modified = false;

        free(raw);
        return disk;
    }

    /* Raw sector format (legacy) */
    disk->data = raw;
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
