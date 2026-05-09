/**
 * @file vram_device.h
 * @brief Émulation VRAM cold SDRAM (ADR-19, Sprint VRAM-1 Oric 2)
 * @author bmarty <bmarty@mailo.com>
 * @date 2026-05-09
 *
 * VRAM cold de l'Oric 2 (cf. ADR-19) : SDRAM ULX3S 32 MiB, accessible
 * uniquement via I/O ports MMIO ($0330-$033C) — pas dans le banking
 * 24-bit du CPU. v1 expose les 16 premiers MiB (24-bit addressing).
 *
 * Modèle de programmation :
 *   1. Set ADDR via VRAM_ADDR_LO/MID/HI (24-bit).
 *   2. Lire/écrire via VRAM_DATA → auto-increment ADDR.
 *   3. Pour transferts massifs : set DMA_SRC, DMA_DST, DMA_LEN puis
 *      écrire DMA_CTRL avec bit 0 = trigger.
 *
 * v0.1 : DMA synchrone (instantané, busy=0 immédiat).
 * v0.2 : DMA asynchrone avec busy bit + interrupts.
 *
 * Cible HDL ULX3S : controller SDRAM avec address latch + auto-inc,
 * DMA SDRAM↔BRAM (banks 128-159) en parallèle du CPU.
 */

#ifndef VRAM_DEVICE_H
#define VRAM_DEVICE_H

#include <stdbool.h>
#include <stdint.h>
#include "memory/memory.h"

/* Capacité VRAM cold v1 : 16 MiB (24-bit addressable).
 * Le HDL ULX3S possède 32 MiB de SDRAM ; les 16 MiB additionnels sont
 * réservés à v2 (extension via bit additionnel dans DMA_CTRL ou autre
 * mécanisme à définir). */
#define VRAM_SIZE        (16u * 1024u * 1024u)
#define VRAM_ADDR_MASK   (VRAM_SIZE - 1u)

/* Offsets de registres I/O (ports $0330-$033C en bank 0). */
#define VRAM_REG_ADDR_LO     0x30u
#define VRAM_REG_ADDR_MID    0x31u
#define VRAM_REG_ADDR_HI     0x32u
#define VRAM_REG_DATA        0x33u
#define VRAM_REG_DMA_CTRL    0x34u
#define VRAM_REG_DMA_SRC_LO  0x35u
#define VRAM_REG_DMA_SRC_MID 0x36u
#define VRAM_REG_DMA_SRC_HI  0x37u
#define VRAM_REG_DMA_DST_LO  0x38u
#define VRAM_REG_DMA_DST_MID 0x39u
#define VRAM_REG_DMA_DST_HI  0x3Au
#define VRAM_REG_DMA_LEN_LO  0x3Bu
#define VRAM_REG_DMA_LEN_HI  0x3Cu

/* DMA control bits (write to VRAM_REG_DMA_CTRL). */
#define VRAM_DMA_CTRL_TRIGGER  0x01u  /* bit 0 : déclencher transfert */
#define VRAM_DMA_CTRL_DIR      0x02u  /* bit 1 : 0=SDRAM→bank, 1=bank→SDRAM */
#define VRAM_DMA_CTRL_BUSY     0x80u  /* read-only : DMA en cours (v0.1 toujours 0) */

/**
 * @brief État du device VRAM cold.
 *
 * `buffer` est alloué heap-side (16 MiB calloc). Les registres
 * mémorisent les pointers courants pour les accès séquentiels et
 * les paramètres DMA.
 */
typedef struct vram_device_s {
    uint8_t* buffer;           /**< 16 MiB heap, NULL si init failed */
    uint32_t addr;             /**< curseur d'adresse 24-bit */
    uint32_t dma_src;          /**< adresse source DMA (24-bit) */
    uint32_t dma_dst;          /**< adresse dest DMA (24-bit, = banking si DIR=0) */
    uint16_t dma_len;          /**< longueur DMA (octets, 0 = 65536) */
    bool     busy;             /**< v0.1 : toujours false (DMA synchrone) */
} vram_device_t;

/**
 * @brief Init device. Alloue 16 MiB heap zero-fill.
 * @return true si OK, false si OOM.
 */
bool vram_init(vram_device_t* vd);

/**
 * @brief Libère le buffer heap.
 */
void vram_cleanup(vram_device_t* vd);

/**
 * @brief Lecture I/O register.
 *
 * `reg` est l'offset bas (0x30..0x3C). Pour `VRAM_REG_DATA`, lit
 * `buffer[addr]` puis incrémente `addr`.
 */
uint8_t vram_read(vram_device_t* vd, uint8_t reg);

/**
 * @brief Écriture I/O register.
 *
 * `reg` est l'offset bas. Pour `VRAM_REG_DMA_CTRL` avec bit 0 = 1,
 * déclenche le DMA (synchrone v0.1) qui copie entre SDRAM et banks
 * via `mem`. `mem` peut être NULL pour les writes non-DMA.
 */
void vram_write(vram_device_t* vd, memory_t* mem, uint8_t reg, uint8_t value);

/**
 * @brief Direct peek/poke pour tests (bypass des registres I/O).
 */
uint8_t vram_peek(const vram_device_t* vd, uint32_t addr);
void    vram_poke(vram_device_t* vd, uint32_t addr, uint8_t value);

#endif /* VRAM_DEVICE_H */
