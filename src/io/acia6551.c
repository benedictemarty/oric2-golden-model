/**
 * @file acia6551.c
 * @brief MOS 6551 ACIA — Asynchronous Communication Interface Adapter
 * @author bmarty <bmarty@mailo.com>
 * @date 2026-03-22
 *
 * Faithful emulation of the 6551 ACIA as used in ORIC serial interfaces
 * (Digitelec DTL 2000, MCP RS232-C, Kenema 300/300, Telestrat).
 * Mapped at $031C-$031F (standard de facto for ORIC).
 *
 * References:
 *   - MOS 6551 datasheet
 *   - Defence Force Wiki: oric:hardware:serial
 */

#include <string.h>
#include "io/acia6551.h"
#include "io/serial_backend.h"
#include "utils/logging.h"

/* ═══════════════════════════════════════════════════════════════════════
 *  ACIA 6551 crystal oscillator and baud rate
 *
 *  The 6551 has its own 1.8432 MHz crystal, divided internally by 16,
 *  giving a 115200 Hz internal clock. The baud rate generator further
 *  divides this to produce the selected baud rate.
 *
 *  CPU cycles per byte = (CPU_CLOCK * framebits) / baud_rate
 *                      = (1000000 * framebits) / baud_rate
 * ═══════════════════════════════════════════════════════════════════════ */

#define ACIA_XTAL_HZ       1843200
#define ACIA_PRESCALER      16
#define ACIA_INTERNAL_HZ    (ACIA_XTAL_HZ / ACIA_PRESCALER)  /* 115200 */

static const int baud_rate_table[16] = {
    0,      /* 0: External clock (treated as max speed) */
    50, 75, 110, 135, 150, 300, 600,
    1200, 1800, 2400, 3600, 4800, 7200, 9600, 19200
};

/**
 * @brief Calculate frame format bits from control/command registers
 *
 * Decodes word length (control bits 6-5), stop bits (control bit 7),
 * and parity enable (command bit 5/PME) to compute total bits per frame.
 * Also sets acia->bitmask based on word length.
 */
static void acia_calc_framebits(acia6551_t* acia)
{
    /* Word length from control register bits 6-5: 00=8, 01=7, 10=6, 11=5 */
    uint8_t wl_field = (acia->control & ACIA_CTL_WL_MASK) >> ACIA_CTL_WL_SHIFT;
    uint8_t wordlen = (uint8_t)(8 - wl_field);

    /* Stop bits from control register bit 7: 0=1 stop, 1=2 stop */
    uint8_t stopbits = (acia->control & ACIA_CTL_SBN) ? 2 : 1;

    /* Parity from command register bit 5 (PME): 1=parity enabled */
    uint8_t parity = (acia->command & ACIA_CMD_PME) ? 1 : 0;

    /* Total: 1 start + data + parity + stop */
    acia->framebits = (uint8_t)(1 + wordlen + parity + stopbits);

    /* Data mask based on word length */
    acia->bitmask = (uint8_t)((1U << wordlen) - 1);
}

/**
 * @brief Compute CPU cycles per byte for a given baud rate and frame format
 */
static int32_t cycles_per_byte(int baud, uint8_t framebits)
{
    if (baud <= 0) return 1;  /* External clock: instant */
    /* CPU cycles = (CPU_CLOCK * framebits) / baud_rate */
    return (int32_t)((1000000L * framebits) / baud);
}

/* ═══════════════════════════════════════════════════════════════════════
 *  IRQ management
 * ═══════════════════════════════════════════════════════════════════════ */

static void acia_update_irq(acia6551_t* acia)
{
    bool irq_wanted = false;

    /* RX IRQ: RDRF set AND receiver IRQ enabled (IRD bit = 0) */
    if ((acia->status & ACIA_STATUS_RDRF) && !(acia->command & ACIA_CMD_IRD)) {
        irq_wanted = true;
    }

    /* TX IRQ: TDRE set AND TIC = 01 (RTS low, TX IRQ enabled) */
    uint8_t tic = (acia->command & ACIA_CMD_TIC_MASK) >> ACIA_CMD_TIC_SHIFT;
    if ((acia->status & ACIA_STATUS_TDRE) && tic == ACIA_TIC_RTS_LOW_IRQ_ON) {
        irq_wanted = true;
    }

    /* Update status register IRQ bit */
    if (irq_wanted) {
        acia->status |= ACIA_STATUS_IRQ;
    } else {
        acia->status &= ~ACIA_STATUS_IRQ;
    }

    /* Drive IRQ line (edge detection) */
    if (irq_wanted && !acia->irq_line) {
        acia->irq_line = true;
        if (acia->irq_set) acia->irq_set(acia->irq_userdata);
    } else if (!irq_wanted && acia->irq_line) {
        acia->irq_line = false;
        if (acia->irq_clr) acia->irq_clr(acia->irq_userdata);
    }
}

/**
 * @brief Recalculate TX/RX timing from control/command registers
 */
static void acia_update_timing(acia6551_t* acia)
{
    /* Recalculate frame format (framebits, bitmask) */
    acia_calc_framebits(acia);

    if (acia->v23_mode) {
        /* V23 Minitel/Prestel: asymmetric 1200 RX / 75 TX */
        acia->baud_rate = ACIA_V23_RX_BAUD;  /* Report RX baud */
        acia->rx_reload = cycles_per_byte(ACIA_V23_RX_BAUD, acia->framebits);
        acia->tx_reload = cycles_per_byte(ACIA_V23_TX_BAUD, acia->framebits);
    } else {
        int baud_idx = acia->control & ACIA_CTL_BAUD_MASK;
        int baud = baud_rate_table[baud_idx];
        acia->baud_rate = (uint32_t)baud;
        acia->rx_reload = cycles_per_byte(baud, acia->framebits);
        acia->tx_reload = cycles_per_byte(baud, acia->framebits);
    }
}

/* ═══════════════════════════════════════════════════════════════════════
 *  Init / Reset
 * ═══════════════════════════════════════════════════════════════════════ */

void acia_init(acia6551_t* acia)
{
    memset(acia, 0, sizeof(acia6551_t));

    /* Configurable base address (default $031C) */
    acia->base_addr = ACIA_DEFAULT_BASE;

    /* Power-on state per datasheet */
    acia->status = ACIA_STATUS_TDRE;  /* Transmitter starts empty */
    acia->command = 0x00;
    acia->control = 0x00;
    acia->tx_pending = false;
    acia->rx_full = false;
    acia->irq_line = false;

    /* Default signal lines: carrier and ready active */
    acia->dcd = true;
    acia->dsr = true;
    acia->cts = true;

    acia->v23_mode = false;
    acia->backend = NULL;

    acia->irq_set = NULL;
    acia->irq_clr = NULL;
    acia->irq_userdata = NULL;

    /* Calculate initial frame format and timing */
    acia_update_timing(acia);

    log_info("ACIA 6551 initialized at $%04X-$%04X (xtal %d Hz / %d = %d Hz)",
             acia->base_addr, acia->base_addr + 3,
             ACIA_XTAL_HZ, ACIA_PRESCALER, ACIA_INTERNAL_HZ);
}

void acia_reset(acia6551_t* acia)
{
    /* Programmed reset (write to $031D):
     * - Clears overrun, resets status
     * - Disables receiver/transmitter
     * - Does NOT affect Control Register (per datasheet) */
    acia->status = ACIA_STATUS_TDRE;
    acia->command = acia->command & 0xE0;  /* Preserve parity bits, clear DTR/IRD/TIC/ECHO */
    acia->tx_pending = false;
    acia->rx_full = false;

    /* Deassert IRQ */
    if (acia->irq_line) {
        acia->irq_line = false;
        if (acia->irq_clr) acia->irq_clr(acia->irq_userdata);
    }

    log_debug("ACIA 6551 programmed reset");
}

/* ═══════════════════════════════════════════════════════════════════════
 *  Register Read
 * ═══════════════════════════════════════════════════════════════════════ */

uint8_t acia_read(acia6551_t* acia, uint16_t addr)
{
    switch (addr & ACIA_ADDR_MASK) {
    case 0x00: {
        /* $031C: Read Receiver Data Register */
        uint8_t data = acia->rdr;
        acia->rx_full = false;
        acia->status &= ~(ACIA_STATUS_RDRF | ACIA_STATUS_OVRN | ACIA_STATUS_PE | ACIA_STATUS_FE);
        acia_update_irq(acia);
        return data;
    }

    case 0x01: {
        /* $031D: Read Status Register — clears IRQ bit after read */
        uint8_t status = acia->status;

        /* Reflect external signal lines (active-low on hardware) */
        if (!acia->dcd) status |= ACIA_STATUS_DCD;
        else            status &= ~ACIA_STATUS_DCD;

        if (!acia->dsr) status |= ACIA_STATUS_DSR;
        else            status &= ~ACIA_STATUS_DSR;

        /* Reading status clears IRQ (per datasheet) */
        acia->status &= ~ACIA_STATUS_IRQ;
        if (acia->irq_line) {
            acia->irq_line = false;
            if (acia->irq_clr) acia->irq_clr(acia->irq_userdata);
        }
        return status;
    }

    case 0x02:
        /* $031E: Read Command Register */
        return acia->command;

    case 0x03:
        /* $031F: Read Control Register */
        return acia->control;

    default:
        return 0xFF;
    }
}

/* ═══════════════════════════════════════════════════════════════════════
 *  Register Write
 * ═══════════════════════════════════════════════════════════════════════ */

void acia_write(acia6551_t* acia, uint16_t addr, uint8_t value)
{
    switch (addr & ACIA_ADDR_MASK) {
    case 0x00:
        /* $031C: Write Transmitter Data Register */
        acia->tdr = value;
        acia->tx_pending = true;
        acia->status &= ~ACIA_STATUS_TDRE;  /* TDR now full */
        acia_update_irq(acia);
        break;

    case 0x01:
        /* $031D: Programmed Reset (any write triggers reset) */
        (void)value;
        acia_reset(acia);
        break;

    case 0x02:
        /* $031E: Write Command Register */
        acia->command = value;
        acia_update_timing(acia);  /* PME bit affects framebits */
        acia_update_irq(acia);
        break;

    case 0x03:
        /* $031F: Write Control Register */
        acia->control = value;
        acia_update_timing(acia);
        break;
    }
}

/* ═══════════════════════════════════════════════════════════════════════
 *  Tick — called once per CPU cycle from main loop
 * ═══════════════════════════════════════════════════════════════════════ */

void acia_tick(acia6551_t* acia)
{
    /* Skip if no backend or DTR not asserted */
    if (!acia->backend) return;
    if (!(acia->command & ACIA_CMD_DTR)) return;

    /* ── TX path ── */
    if (acia->tx_pending) {
        acia->tx_cycles--;
        if (acia->tx_cycles <= 0) {
            /* Send byte to backend if CTS allows */
            if (acia->cts && acia->backend->send) {
                acia->backend->send(acia->backend, acia->tdr);
            }
            acia->tx_pending = false;
            acia->status |= ACIA_STATUS_TDRE;
            acia->tx_cycles = acia->tx_reload;
            acia_update_irq(acia);
        }
    }

    /* ── RX path ── */
    acia->rx_cycles--;
    if (acia->rx_cycles <= 0) {
        acia->rx_cycles = acia->rx_reload;

        /* Try to receive if DCD is active and backend has data */
        if (acia->dcd && acia->backend->poll && acia->backend->poll(acia->backend)) {
            uint8_t byte;
            if (acia->backend->recv(acia->backend, &byte)) {
                if (acia->rx_full) {
                    /* Overrun: previous byte not read */
                    acia->status |= ACIA_STATUS_OVRN;
                }
                acia->rdr = byte;
                acia->rx_full = true;
                acia->status |= ACIA_STATUS_RDRF;

                /* Echo mode: retransmit received byte */
                if (acia->command & ACIA_CMD_ECHO) {
                    if (acia->backend->send) {
                        acia->backend->send(acia->backend, byte);
                    }
                }

                acia_update_irq(acia);
            }
        }
    }
}

/* ═══════════════════════════════════════════════════════════════════════
 *  Configuration helpers
 * ═══════════════════════════════════════════════════════════════════════ */

void acia_set_v23_mode(acia6551_t* acia, bool enabled)
{
    acia->v23_mode = enabled;
    acia_update_timing(acia);
    if (enabled) {
        log_info("ACIA V23 mode: RX=%d baud, TX=%d baud (Minitel/Prestel)",
                 ACIA_V23_RX_BAUD, ACIA_V23_TX_BAUD);
    }
}

void acia_set_backend(acia6551_t* acia, serial_backend_t* backend)
{
    acia->backend = backend;
}

void acia_set_dcd(acia6551_t* acia, bool active)
{
    acia->dcd = active;
}

void acia_set_dsr(acia6551_t* acia, bool active)
{
    acia->dsr = active;
}

void acia_set_cts(acia6551_t* acia, bool active)
{
    acia->cts = active;
}
