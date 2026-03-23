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
 * Emulator enhancements beyond real MOS 6551:
 *   - RX FIFO buffer (--serial-buffer N) prevents overrun during long
 *     CPU operations. Real programs used IRQ + software buffer.
 *   - WDC 65C51 IRQ mode (--serial-irq-on-rdrf) re-triggers IRQ while
 *     RDRF is set, preventing lost IRQs during simultaneous TX/RX.
 *
 * References:
 *   - MOS 6551 datasheet (Princeton)
 *   - WDC W65C51N datasheet (improved IRQ behavior)
 *   - Defence Force Wiki: oric:hardware:serial
 */

#include <stdlib.h>
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
 */
static void acia_calc_framebits(acia6551_t* acia)
{
    uint8_t wl_field = (acia->control & ACIA_CTL_WL_MASK) >> ACIA_CTL_WL_SHIFT;
    uint8_t wordlen = (uint8_t)(8 - wl_field);
    uint8_t stopbits = (acia->control & ACIA_CTL_SBN) ? 2 : 1;
    uint8_t parity = (acia->command & ACIA_CMD_PME) ? 1 : 0;

    acia->framebits = (uint8_t)(1 + wordlen + parity + stopbits);
    acia->bitmask = (uint8_t)((1U << wordlen) - 1);
}

/**
 * @brief Compute CPU cycles per byte for a given baud rate and frame format
 */
static int32_t cycles_per_byte(int baud, uint8_t framebits)
{
    if (baud <= 0) return 1;
    return (int32_t)((1000000L * framebits) / baud);
}

/* ═══════════════════════════════════════════════════════════════════════
 *  Serial trace helpers
 * ═══════════════════════════════════════════════════════════════════════ */

static void trace_header(acia6551_t* acia)
{
    if (!acia->trace_file) return;
    fprintf(acia->trace_file,
            "# Phosphoric ACIA 6551 Serial Trace\n"
            "# CYCLE      DIR  HEX  CHR  STATUS    FIFO  SIGNALS\n"
            "# ────────── ───  ──── ───  ────────  ────  ──────────────────\n");
    fflush(acia->trace_file);
}

static void trace_signals(acia6551_t* acia, char* buf, int size)
{
    snprintf(buf, (size_t)size, "DTR=%d DCD=%d CTS=%d DSR=%d",
             (acia->command & ACIA_CMD_DTR) ? 1 : 0,
             acia->dcd ? 1 : 0,
             acia->cts ? 1 : 0,
             acia->dsr ? 1 : 0);
}

static void trace_data(acia6551_t* acia, const char* dir, uint8_t byte)
{
    if (!acia->trace_file) return;
    char sig[64];
    trace_signals(acia, sig, (int)sizeof(sig));
    char ch = (byte >= 32 && byte < 127) ? (char)byte : '.';
    fprintf(acia->trace_file, "%010llu  %-3s  %02X   %c    %c%c%c%c%c%c%c%c  %3d   %s\n",
            (unsigned long long)acia->trace_cycle,
            dir, byte, ch,
            (acia->status & ACIA_STATUS_IRQ)  ? 'I' : '.',
            (acia->status & ACIA_STATUS_DSR)  ? 'D' : '.',
            (acia->status & ACIA_STATUS_DCD)  ? 'C' : '.',
            (acia->status & ACIA_STATUS_TDRE) ? 'T' : '.',
            (acia->status & ACIA_STATUS_RDRF) ? 'R' : '.',
            (acia->status & ACIA_STATUS_OVRN) ? 'O' : '.',
            (acia->status & ACIA_STATUS_FE)   ? 'F' : '.',
            (acia->status & ACIA_STATUS_PE)   ? 'P' : '.',
            acia->rx_fifo ? acia->rx_fifo_count : 0,
            sig);
    fflush(acia->trace_file);
}

static void trace_event(acia6551_t* acia, const char* event)
{
    if (!acia->trace_file) return;
    char sig[64];
    trace_signals(acia, sig, (int)sizeof(sig));
    fprintf(acia->trace_file, "%010llu  %-3s  --   -    --------  ---   %s  %s\n",
            (unsigned long long)acia->trace_cycle,
            "SIG", sig, event);
    fflush(acia->trace_file);
}

static void trace_reg(acia6551_t* acia, const char* dir, const char* reg, uint8_t val)
{
    if (!acia->trace_file) return;
    fprintf(acia->trace_file, "%010llu  %-3s  %02X   -    %-8s  ---   %s\n",
            (unsigned long long)acia->trace_cycle,
            dir, val, reg, "");
    fflush(acia->trace_file);
}

/* ═══════════════════════════════════════════════════════════════════════
 *  RX FIFO helpers
 * ═══════════════════════════════════════════════════════════════════════ */

static bool fifo_push(acia6551_t* acia, uint8_t byte)
{
    if (!acia->rx_fifo || acia->rx_fifo_count >= acia->rx_fifo_size)
        return false;
    acia->rx_fifo[acia->rx_fifo_head] = byte;
    acia->rx_fifo_head = (acia->rx_fifo_head + 1) % acia->rx_fifo_size;
    acia->rx_fifo_count++;
    return true;
}

static bool fifo_pop(acia6551_t* acia, uint8_t* byte)
{
    if (!acia->rx_fifo || acia->rx_fifo_count <= 0)
        return false;
    *byte = acia->rx_fifo[acia->rx_fifo_tail];
    acia->rx_fifo_tail = (acia->rx_fifo_tail + 1) % acia->rx_fifo_size;
    acia->rx_fifo_count--;
    return true;
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

    /* Drive IRQ line.
     * Standard MOS 6551: edge detection only (set/clear on transitions).
     * WDC 65C51 mode (irq_on_rdrf): assert IRQ whenever RDRF is set,
     * even if already asserted — prevents lost IRQs during TX+RX. */
    if (acia->irq_on_rdrf) {
        /* 65C51 mode: force IRQ state to match irq_wanted */
        if (irq_wanted && !acia->irq_line) {
            acia->irq_line = true;
            if (acia->irq_set) acia->irq_set(acia->irq_userdata);
        } else if (!irq_wanted && acia->irq_line) {
            acia->irq_line = false;
            if (acia->irq_clr) acia->irq_clr(acia->irq_userdata);
        }
        /* Re-assert if RDRF still set (key difference from MOS 6551) */
        if ((acia->status & ACIA_STATUS_RDRF) && !(acia->command & ACIA_CMD_IRD)) {
            if (!acia->irq_line) {
                acia->irq_line = true;
                if (acia->irq_set) acia->irq_set(acia->irq_userdata);
            }
        }
    } else {
        /* Standard MOS 6551: edge detection */
        if (irq_wanted && !acia->irq_line) {
            acia->irq_line = true;
            if (acia->irq_set) acia->irq_set(acia->irq_userdata);
        } else if (!irq_wanted && acia->irq_line) {
            acia->irq_line = false;
            if (acia->irq_clr) acia->irq_clr(acia->irq_userdata);
        }
    }
}

/**
 * @brief Recalculate TX/RX timing from control/command registers
 */
static void acia_update_timing(acia6551_t* acia)
{
    acia_calc_framebits(acia);

    if (acia->v23_mode) {
        acia->baud_rate = ACIA_V23_RX_BAUD;
        acia->rx_reload = cycles_per_byte(ACIA_V23_RX_BAUD, acia->framebits);
        acia->tx_reload = cycles_per_byte(ACIA_V23_TX_BAUD, acia->framebits);
    } else {
        int baud_idx = acia->control & ACIA_CTL_BAUD_MASK;
        int baud = baud_rate_table[baud_idx];
        acia->baud_rate = (uint32_t)baud;
        acia->rx_reload = cycles_per_byte(baud, acia->framebits);
        acia->tx_reload = cycles_per_byte(baud, acia->framebits);
    }

    /* Resynchronize counters when timing changes (baud rate switch).
     * Clamp to new reload value if current counter is out of range. */
    if (acia->tx_cycles <= 0 || acia->tx_cycles > acia->tx_reload)
        acia->tx_cycles = acia->tx_reload;
    if (acia->rx_cycles <= 0 || acia->rx_cycles > acia->rx_reload)
        acia->rx_cycles = acia->rx_reload;
}

/* ═══════════════════════════════════════════════════════════════════════
 *  Init / Reset
 * ═══════════════════════════════════════════════════════════════════════ */

void acia_init(acia6551_t* acia)
{
    /* Preserve FIFO config and IRQ callbacks across re-init */
    uint8_t* saved_fifo = acia->rx_fifo;
    int saved_fifo_size = acia->rx_fifo_size;
    bool saved_irq_on_rdrf = acia->irq_on_rdrf;
    void (*saved_irq_set)(emulator_t*) = acia->irq_set;
    void (*saved_irq_clr)(emulator_t*) = acia->irq_clr;
    emulator_t* saved_userdata = acia->irq_userdata;
    serial_backend_t* saved_backend = acia->backend;

    memset(acia, 0, sizeof(acia6551_t));

    /* Restore preserved fields */
    acia->rx_fifo = saved_fifo;
    acia->rx_fifo_size = saved_fifo_size;
    acia->irq_on_rdrf = saved_irq_on_rdrf;
    acia->irq_set = saved_irq_set;
    acia->irq_clr = saved_irq_clr;
    acia->irq_userdata = saved_userdata;
    acia->backend = saved_backend;

    /* Power-on state per datasheet */
    acia->status = ACIA_STATUS_TDRE;
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

    /* Reset FIFO state (keep buffer allocated) */
    acia->rx_fifo_head = 0;
    acia->rx_fifo_tail = 0;
    acia->rx_fifo_count = 0;

    /* Calculate timing and initialize cycle counters to full frame */
    acia_update_timing(acia);

    log_info("ACIA 6551 initialized (xtal %d Hz / %d = %d Hz%s%s)",
             ACIA_XTAL_HZ, ACIA_PRESCALER, ACIA_INTERNAL_HZ,
             acia->rx_fifo ? ", FIFO=" : "",
             acia->rx_fifo ? "" : "");
    if (acia->rx_fifo) {
        log_info("  RX FIFO: %d bytes", acia->rx_fifo_size);
    }
    if (acia->irq_on_rdrf) {
        log_info("  IRQ mode: WDC 65C51 (re-trigger on RDRF)");
    }
}

void acia_reset(acia6551_t* acia)
{
    acia->status = ACIA_STATUS_TDRE;
    acia->command = acia->command & 0xE0;
    acia->tx_pending = false;
    acia->rx_full = false;

    /* Flush FIFO on reset */
    acia->rx_fifo_head = 0;
    acia->rx_fifo_tail = 0;
    acia->rx_fifo_count = 0;

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
        acia->status &= ~(ACIA_STATUS_RDRF | ACIA_STATUS_OVRN |
                          ACIA_STATUS_PE | ACIA_STATUS_FE);

        /* FIFO: auto-load next byte if available */
        if (acia->rx_fifo && acia->rx_fifo_count > 0) {
            uint8_t next;
            if (fifo_pop(acia, &next)) {
                acia->rdr = next;
                acia->rx_full = true;
                acia->status |= ACIA_STATUS_RDRF;
            }
        }

        acia_update_irq(acia);
        return data;
    }

    case 0x01: {
        /* $031D: Read Status Register.
         * DCD/DSR bits reflect live pin state (not latched).
         * Reading clears IRQ bit (MOS 6551 behavior).
         * In 65C51 mode, IRQ re-asserts if RDRF still set. */
        uint8_t status = acia->status;

        if (!acia->dcd) status |= ACIA_STATUS_DCD;
        else            status &= ~ACIA_STATUS_DCD;

        if (!acia->dsr) status |= ACIA_STATUS_DSR;
        else            status &= ~ACIA_STATUS_DSR;

        /* Clear IRQ on status read (MOS 6551 datasheet) */
        acia->status &= ~ACIA_STATUS_IRQ;
        if (acia->irq_line) {
            acia->irq_line = false;
            if (acia->irq_clr) acia->irq_clr(acia->irq_userdata);
        }

        /* 65C51 mode: immediately re-assert if RDRF still set */
        if (acia->irq_on_rdrf) {
            acia_update_irq(acia);
        }

        return status;
    }

    case 0x02:
        return acia->command;

    case 0x03:
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
        acia->tdr = value & acia->bitmask;
        acia->tx_pending = true;
        acia->tx_cycles = acia->tx_reload;
        acia->status &= ~ACIA_STATUS_TDRE;
        acia_update_irq(acia);
        break;

    case 0x01:
        (void)value;
        acia_reset(acia);
        break;

    case 0x02:
        acia->command = value;
        trace_reg(acia, "WR", "CMD", value);
        acia_update_timing(acia);
        acia_update_irq(acia);
        break;

    case 0x03:
        acia->control = value;
        trace_reg(acia, "WR", "CTL", value);
        acia_update_timing(acia);
        break;
    }
}

/* ═══════════════════════════════════════════════════════════════════════
 *  Tick — called once per CPU cycle from main loop
 * ═══════════════════════════════════════════════════════════════════════ */

void acia_tick(acia6551_t* acia)
{
    if (!acia->backend) return;
    if (!(acia->command & ACIA_CMD_DTR)) return;

    /* ── TX path ── */
    if (acia->tx_pending) {
        acia->tx_cycles--;
        if (acia->tx_cycles <= 0) {
            if (acia->cts && acia->backend->send) {
                acia->backend->send(acia->backend, acia->tdr);
                trace_data(acia, "TX", acia->tdr);
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

        if (acia->dcd && acia->backend->poll &&
            acia->backend->poll(acia->backend)) {
            uint8_t byte;
            if (acia->backend->recv(acia->backend, &byte)) {
                byte &= acia->bitmask;

                trace_data(acia, "RX", byte);

                if (acia->rx_fifo) {
                    /* ── FIFO mode: queue byte ── */
                    if (!acia->rx_full) {
                        acia->rdr = byte;
                        acia->rx_full = true;
                        acia->status |= ACIA_STATUS_RDRF;
                    } else if (!fifo_push(acia, byte)) {
                        acia->status |= ACIA_STATUS_OVRN;
                        trace_event(acia, "OVERRUN (FIFO full)");
                    }
                } else {
                    /* ── Classic 1-byte mode: overwrite RDR ── */
                    if (acia->rx_full) {
                        acia->status |= ACIA_STATUS_OVRN;
                        trace_event(acia, "OVERRUN (RDR not read)");
                    }
                    acia->rdr = byte;
                    acia->rx_full = true;
                    acia->status |= ACIA_STATUS_RDRF;
                }

                /* Echo mode */
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
    bool old = acia->dcd;
    acia->dcd = active;
    if (old != active) {
        trace_event(acia, active ? "DCD ON (carrier)" : "DCD OFF (no carrier)");
        acia_update_irq(acia);
    }
}

void acia_set_dsr(acia6551_t* acia, bool active)
{
    acia->dsr = active;
}

void acia_set_cts(acia6551_t* acia, bool active)
{
    acia->cts = active;
}

void acia_set_rx_fifo(acia6551_t* acia, int size)
{
    /* Free existing FIFO */
    free(acia->rx_fifo);
    acia->rx_fifo = NULL;
    acia->rx_fifo_size = 0;
    acia->rx_fifo_head = 0;
    acia->rx_fifo_tail = 0;
    acia->rx_fifo_count = 0;

    if (size > 0) {
        if (size > ACIA_FIFO_MAX_SIZE) size = ACIA_FIFO_MAX_SIZE;
        acia->rx_fifo = (uint8_t*)malloc((size_t)size);
        if (acia->rx_fifo) {
            acia->rx_fifo_size = size;
            log_info("ACIA RX FIFO enabled: %d bytes", size);
        } else {
            log_error("ACIA RX FIFO: allocation failed for %d bytes", size);
        }
    }
}

void acia_set_irq_on_rdrf(acia6551_t* acia, bool enabled)
{
    acia->irq_on_rdrf = enabled;
    if (enabled) {
        log_info("ACIA IRQ mode: WDC 65C51 (re-trigger while RDRF set)");
    }
}

void acia_set_trace(acia6551_t* acia, const char* filename)
{
    /* Close existing trace */
    if (acia->trace_file) {
        fclose(acia->trace_file);
        acia->trace_file = NULL;
    }

    if (filename) {
        acia->trace_file = fopen(filename, "w");
        if (acia->trace_file) {
            trace_header(acia);
            log_info("ACIA serial trace: %s", filename);
        } else {
            log_error("ACIA serial trace: failed to open %s", filename);
        }
    }
}

void acia_set_trace_cycle(acia6551_t* acia, uint64_t cycle)
{
    acia->trace_cycle = cycle;
}
