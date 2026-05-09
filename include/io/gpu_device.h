/**
 * @file gpu_device.h
 * @brief Émulation GPU Blitter HW Oric 2 (ADR-21, Sprint GPU-1)
 * @author bmarty <bmarty@mailo.com>
 * @date 2026-05-09
 *
 * Co-processeur graphique simulé. Le CPU enqueue commandes via I/O
 * ports $0340-$034F, le GPU les exécute en accédant directement à la
 * VRAM SDRAM (vram_device) et/ou au banking RAM (memory_t).
 *
 * v0.1 : 2 commandes (CLEAR, FILL_RECT) synchrones (busy=0 dès retour).
 * v0.2 : étendre à BLIT, LINE, TEXT (cf. ADR-21).
 * v0.3 : async (cycles + IRQ).
 *
 * Cible HDL ULX3S : controller GPU ECP5 avec command queue + units
 * matériels par opération.
 *
 * Modèle de programmation typique :
 *   1. Écrire ARG1/ARG2/ARG3/ARG4 selon la commande.
 *   2. Écrire CMD_OP avec l'opcode.
 *   3. Écrire any value à TRIGGER → exec.
 *   4. Optionnel : poll STATUS bit 7 (busy) ou attendre IRQ.
 */

#ifndef GPU_DEVICE_H
#define GPU_DEVICE_H

#include <stdbool.h>
#include <stdint.h>
#include "memory/memory.h"
#include "io/vram_device.h"

/* Offsets de registres I/O dans la page $03 (mask 0x4F = bas 7 bits). */
#define GPU_REG_CMD_OP       0x40u
#define GPU_REG_ARG1_LO      0x41u
#define GPU_REG_ARG1_MID     0x42u
#define GPU_REG_ARG1_HI      0x43u
#define GPU_REG_ARG2_LO      0x44u
#define GPU_REG_ARG2_MID     0x45u
#define GPU_REG_ARG2_HI      0x46u
#define GPU_REG_ARG3_LO      0x47u
#define GPU_REG_ARG3_MID     0x48u
#define GPU_REG_ARG3_HI      0x49u
#define GPU_REG_ARG4_LO      0x4Au
#define GPU_REG_ARG4_MID     0x4Bu
#define GPU_REG_ARG4_HI      0x4Cu
#define GPU_REG_STATUS       0x4Du
#define GPU_REG_TRIGGER      0x4Eu
#define GPU_REG_INT_CTRL     0x4Fu

/* Status register bits. */
#define GPU_STATUS_BUSY      0x80u    /* bit 7 : commande en cours */
#define GPU_STATUS_ERR       0x40u    /* bit 6 : opcode inconnu / erreur */
#define GPU_STATUS_QFULL     0x20u    /* bit 5 : queue pleine (v0.3) */
#define GPU_STATUS_DONE      0x01u    /* bit 0 : dernière commande OK */

/* Opcodes commandes ratifiées (cf. ADR-21). */
#define GPU_OP_NOP           0x00u
#define GPU_OP_CLEAR         0x01u    /* fill linéaire d'une zone (v0.1) */
#define GPU_OP_FILL_RECT     0x02u    /* fill rectangle 4bpp (v0.1) */
#define GPU_OP_BLIT          0x03u    /* copie de bloc (v0.2) */
#define GPU_OP_LINE          0x04u    /* ligne Bresenham (v0.2) */
#define GPU_OP_TEXT          0x05u    /* rendu fonte (v0.2) */

/* Hardcoded BPL (bytes per line) pour XVGA 1024×768×4bpp (ADR-20 v3).
 * v0.2 : sera configurable via registre dédié. */
#define GPU_XVGA_BPL         512u

/**
 * @brief État du GPU device.
 */
typedef struct gpu_device_s {
    uint8_t  cmd_op;
    uint32_t arg1;            /* 24-bit */
    uint32_t arg2;            /* 24-bit */
    uint32_t arg3;            /* 24-bit */
    uint32_t arg4;            /* 24-bit */
    uint8_t  int_ctrl;
    bool     busy;            /* v0.1 toujours false après exec */
    bool     err;             /* set si dernier opcode inconnu */
} gpu_device_t;

/**
 * @brief Init GPU. Reset registres et état.
 */
void gpu_init(gpu_device_t* gpu);

/**
 * @brief Cleanup (no-op pour l'instant ; pas de heap).
 */
void gpu_cleanup(gpu_device_t* gpu);

/**
 * @brief Lecture I/O register.
 */
uint8_t gpu_read(gpu_device_t* gpu, uint8_t reg);

/**
 * @brief Écriture I/O register.
 *
 * `vram` est requis pour les commandes qui ciblent la VRAM SDRAM
 * (CLEAR, FILL_RECT). `mem` est requis pour les commandes qui
 * ciblent le banking RAM (à venir v0.2). Peuvent être NULL pour
 * les écritures de registres simples (no exec).
 */
void gpu_write(gpu_device_t* gpu, vram_device_t* vram, memory_t* mem,
               uint8_t reg, uint8_t value);

#endif /* GPU_DEVICE_H */
