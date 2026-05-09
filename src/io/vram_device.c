/**
 * @file vram_device.c
 * @brief Émulation VRAM cold SDRAM (ADR-19, Sprint VRAM-1 Oric 2)
 */

#include "io/vram_device.h"
#include "utils/logging.h"

#include <stdlib.h>
#include <string.h>

bool vram_init(vram_device_t* vd) {
    if (!vd) return false;
    memset(vd, 0, sizeof(*vd));
    vd->buffer = (uint8_t*)calloc(VRAM_SIZE, 1);
    if (!vd->buffer) {
        log_error("vram: calloc(%u) failed", (unsigned)VRAM_SIZE);
        return false;
    }
    return true;
}

void vram_cleanup(vram_device_t* vd) {
    if (!vd) return;
    free(vd->buffer);
    vd->buffer = NULL;
    vd->addr = 0;
    vd->dma_src = 0;
    vd->dma_dst = 0;
    vd->dma_len = 0;
    vd->busy = false;
}

uint8_t vram_read(vram_device_t* vd, uint8_t reg) {
    if (!vd || !vd->buffer) return 0xFF;
    switch (reg) {
        case VRAM_REG_ADDR_LO:     return (uint8_t)( vd->addr        & 0xFFu);
        case VRAM_REG_ADDR_MID:    return (uint8_t)((vd->addr >> 8)  & 0xFFu);
        case VRAM_REG_ADDR_HI:     return (uint8_t)((vd->addr >> 16) & 0xFFu);
        case VRAM_REG_DATA: {
            uint8_t v = vd->buffer[vd->addr & VRAM_ADDR_MASK];
            vd->addr = (vd->addr + 1u) & VRAM_ADDR_MASK;
            return v;
        }
        case VRAM_REG_DMA_CTRL:    return vd->busy ? VRAM_DMA_CTRL_BUSY : 0x00u;
        case VRAM_REG_DMA_SRC_LO:  return (uint8_t)( vd->dma_src        & 0xFFu);
        case VRAM_REG_DMA_SRC_MID: return (uint8_t)((vd->dma_src >> 8)  & 0xFFu);
        case VRAM_REG_DMA_SRC_HI:  return (uint8_t)((vd->dma_src >> 16) & 0xFFu);
        case VRAM_REG_DMA_DST_LO:  return (uint8_t)( vd->dma_dst        & 0xFFu);
        case VRAM_REG_DMA_DST_MID: return (uint8_t)((vd->dma_dst >> 8)  & 0xFFu);
        case VRAM_REG_DMA_DST_HI:  return (uint8_t)((vd->dma_dst >> 16) & 0xFFu);
        case VRAM_REG_DMA_LEN_LO:  return (uint8_t)( vd->dma_len        & 0xFFu);
        case VRAM_REG_DMA_LEN_HI:  return (uint8_t)((vd->dma_len >> 8)  & 0xFFu);
        default:                   return 0xFFu;
    }
}

/* Exécute un DMA synchrone selon les paramètres courants. v0.1 : busy
 * reste à 0 (transfert "instantané" pour le simulateur). */
static void vram_dma_exec(vram_device_t* vd, memory_t* mem, uint8_t ctrl) {
    if (!vd || !vd->buffer) return;
    /* len = 0 → 65536 octets (max d'un burst). */
    uint32_t len = (vd->dma_len == 0u) ? 65536u : (uint32_t)vd->dma_len;
    bool dir_bank_to_sdram = (ctrl & VRAM_DMA_CTRL_DIR) != 0u;

    if (dir_bank_to_sdram) {
        /* bank → SDRAM : src = adresse banking 24-bit (mem), dst = SDRAM. */
        if (!mem) return;
        for (uint32_t i = 0u; i < len; i++) {
            uint8_t b = memory_read24(mem, (vd->dma_src + i) & 0x00FFFFFFu);
            vd->buffer[(vd->dma_dst + i) & VRAM_ADDR_MASK] = b;
        }
    } else {
        /* SDRAM → bank : src = SDRAM, dst = adresse banking 24-bit. */
        if (!mem) return;
        for (uint32_t i = 0u; i < len; i++) {
            uint8_t b = vd->buffer[(vd->dma_src + i) & VRAM_ADDR_MASK];
            memory_write24(mem, (vd->dma_dst + i) & 0x00FFFFFFu, b);
        }
    }
}

void vram_write(vram_device_t* vd, memory_t* mem, uint8_t reg, uint8_t value) {
    if (!vd || !vd->buffer) return;
    switch (reg) {
        case VRAM_REG_ADDR_LO:
            vd->addr = (vd->addr & 0xFFFF00u) | (uint32_t)value;
            break;
        case VRAM_REG_ADDR_MID:
            vd->addr = (vd->addr & 0xFF00FFu) | ((uint32_t)value << 8);
            break;
        case VRAM_REG_ADDR_HI:
            vd->addr = (vd->addr & 0x00FFFFu) | ((uint32_t)value << 16);
            break;
        case VRAM_REG_DATA:
            vd->buffer[vd->addr & VRAM_ADDR_MASK] = value;
            vd->addr = (vd->addr + 1u) & VRAM_ADDR_MASK;
            break;
        case VRAM_REG_DMA_SRC_LO:
            vd->dma_src = (vd->dma_src & 0xFFFF00u) | (uint32_t)value;
            break;
        case VRAM_REG_DMA_SRC_MID:
            vd->dma_src = (vd->dma_src & 0xFF00FFu) | ((uint32_t)value << 8);
            break;
        case VRAM_REG_DMA_SRC_HI:
            vd->dma_src = (vd->dma_src & 0x00FFFFu) | ((uint32_t)value << 16);
            break;
        case VRAM_REG_DMA_DST_LO:
            vd->dma_dst = (vd->dma_dst & 0xFFFF00u) | (uint32_t)value;
            break;
        case VRAM_REG_DMA_DST_MID:
            vd->dma_dst = (vd->dma_dst & 0xFF00FFu) | ((uint32_t)value << 8);
            break;
        case VRAM_REG_DMA_DST_HI:
            vd->dma_dst = (vd->dma_dst & 0x00FFFFu) | ((uint32_t)value << 16);
            break;
        case VRAM_REG_DMA_LEN_LO:
            vd->dma_len = (uint16_t)((vd->dma_len & 0xFF00u) | (uint16_t)value);
            break;
        case VRAM_REG_DMA_LEN_HI:
            vd->dma_len = (uint16_t)((vd->dma_len & 0x00FFu) | ((uint16_t)value << 8));
            break;
        case VRAM_REG_DMA_CTRL:
            if (value & VRAM_DMA_CTRL_TRIGGER) {
                vram_dma_exec(vd, mem, value);
            }
            break;
        default:
            break;
    }
}

uint8_t vram_peek(const vram_device_t* vd, uint32_t addr) {
    if (!vd || !vd->buffer) return 0u;
    return vd->buffer[addr & VRAM_ADDR_MASK];
}

void vram_poke(vram_device_t* vd, uint32_t addr, uint8_t value) {
    if (!vd || !vd->buffer) return;
    vd->buffer[addr & VRAM_ADDR_MASK] = value;
}
