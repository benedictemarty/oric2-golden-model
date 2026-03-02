/**
 * @file printer.c
 * @brief Centronics parallel printer interface for Phosphoric
 * @author bmarty <bmarty@mailo.com>
 * @date 2026-03-02
 * @version 1.7.0-alpha
 *
 * Captures ORIC printer output (LPRINT/LLIST) to a text file.
 * Detects Centronics STROBE via VIA CA2 forced-low → forced-high
 * transition in PCR writes.
 */

#include "io/printer.h"
#include "utils/logging.h"

#include <string.h>

void oric_printer_init(oric_printer_t* printer)
{
    memset(printer, 0, sizeof(*printer));
    printer->output = NULL;
    printer->filename = NULL;
    printer->strobe_low = false;
    printer->byte_count = 0;
}

bool oric_printer_open(oric_printer_t* printer, const char* filename)
{
    if (!printer || !filename) return false;

    /* Close previous file if any */
    if (printer->output) {
        oric_printer_close(printer);
    }

    printer->output = fopen(filename, "w");
    if (!printer->output) {
        log_error("printer: cannot open '%s' for writing", filename);
        return false;
    }

    printer->filename = filename;
    printer->byte_count = 0;
    printer->strobe_low = false;
    log_info("printer: output to '%s'", filename);
    return true;
}

void oric_printer_close(oric_printer_t* printer)
{
    if (!printer) return;

    if (printer->output) {
        fflush(printer->output);
        fclose(printer->output);
        log_info("printer: closed '%s' (%u bytes printed)",
                 printer->filename ? printer->filename : "?",
                 printer->byte_count);
        printer->output = NULL;
    }
    printer->filename = NULL;
    printer->strobe_low = false;
}

void oric_printer_check_strobe(oric_printer_t* printer, uint8_t old_pcr,
                                uint8_t new_pcr, uint8_t data)
{
    if (!printer || !printer->output) return;

    /* Extract CA2 control mode (PCR bits 1-3) */
    uint8_t old_ca2 = (old_pcr >> 1) & 0x07;
    uint8_t new_ca2 = (new_pcr >> 1) & 0x07;

    /*
     * Detect STROBE sequence:
     *   Step 1: CA2 forced low (mode 110 = 0x06) — STROBE asserted
     *   Step 2: CA2 forced high (mode 111 = 0x07) — STROBE released
     *
     * The rising edge (low → high) latches the data byte.
     */
    if (new_ca2 == CA2_FORCED_LOW) {
        printer->strobe_low = true;
    } else if (new_ca2 == CA2_FORCED_HIGH && printer->strobe_low) {
        /* Rising edge: capture the byte */
        fputc(data, printer->output);
        printer->byte_count++;
        printer->strobe_low = false;

        /* Flush periodically (every newline for text readability) */
        if (data == '\n' || data == '\r') {
            fflush(printer->output);
        }
    } else if (old_ca2 != new_ca2) {
        /* CA2 mode changed to something else — reset strobe tracking */
        printer->strobe_low = false;
    }
}

void oric_printer_flush(oric_printer_t* printer)
{
    if (printer && printer->output) {
        fflush(printer->output);
    }
}

bool oric_printer_is_active(const oric_printer_t* printer)
{
    return printer && printer->output != NULL;
}
