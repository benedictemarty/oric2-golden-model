/**
 * @file sd_device.h
 * @brief Émulation device SD/MMC bloc minimal (Sprint 2.j Oric 2)
 * @author bmarty <bmarty@mailo.com>
 * @date 2026-05-08
 *
 * Device SD émulé exposé via 8 registres I/O à $0320-$0327 :
 *   $0320 : SD_LBA_LO    (R/W) — bits 0-7 du LBA (block number)
 *   $0321 : SD_LBA_MID   (R/W) — bits 8-15
 *   $0322 : SD_LBA_HI    (R/W) — bits 16-23 (max 16M blocs = 8 GiB)
 *   $0323 : SD_CTRL      (R/W) — bit 0 = read trigger ; bit 7 = busy
 *   $0324 : SD_DATA      (R)   — auto-increment data port
 *   $0325-$0327 : reserved
 *
 * Modèle de programmation :
 *   1. Écrire LBA dans LBA_LO/MID/HI.
 *   2. Écrire $01 dans CTRL → déclenche lecture.
 *   3. Lire 512 octets séquentiellement depuis DATA.
 *
 * v0.1 : lecture seule, opération synchrone (busy=0 immédiatement).
 * v0.2 : écriture (bit 1 de CTRL), busy asynchrone.
 *
 * Cible HDL ULX3S : SPI mode native, pas ce stub. Le driver OricOS
 * abstrait l'interface bloc pour porter facilement.
 */

#ifndef SD_DEVICE_H
#define SD_DEVICE_H

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>

#define SD_BLOCK_SIZE  512u

typedef struct sd_device_s {
    FILE*    image;             /**< image file ouverte, ou NULL */
    uint32_t lba;                /**< LBA 24-bit du bloc à lire */
    uint8_t  buffer[SD_BLOCK_SIZE];
    uint16_t byte_idx;           /**< index dans buffer (0..511) */
    bool     busy;               /**< false = idle, true = en cours */
    bool     image_valid;        /**< image chargée et lisible */
} sd_device_t;

/** Init device (image=NULL, busy=0). */
void sd_init(sd_device_t* sd);

/** Charge une image SD. Retourne true si OK, false si fichier absent. */
bool sd_load_image(sd_device_t* sd, const char* path);

/** Ferme l'image et libère ressources. */
void sd_close(sd_device_t* sd);

/** Lecture d'un registre I/O (addr = 0..7, offset depuis $0320). */
uint8_t sd_read(sd_device_t* sd, uint8_t addr);

/** Écriture d'un registre I/O. */
void sd_write(sd_device_t* sd, uint8_t addr, uint8_t value);

#endif /* SD_DEVICE_H */
