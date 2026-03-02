/**
 * @file trace.c
 * @brief CPU trace logging — instruction-level execution trace to file
 * @author bmarty <bmarty@mailo.com>
 * @date 2026-03-02
 * @version 1.0.0-alpha
 */

#include "utils/trace.h"
#include "cpu/cpu6502.h"
#include "memory/memory.h"
#include "utils/logging.h"

#include <string.h>

void trace_init(cpu_trace_t* trace) {
    memset(trace, 0, sizeof(*trace));
    trace->fp = NULL;
    trace->active = false;
    trace->count = 0;
    trace->max_count = 0;
    trace->owns_fp = false;
}

bool trace_open(cpu_trace_t* trace, const char* filename) {
    if (trace->active) {
        trace_close(trace);
    }

    FILE* fp;
    if (filename == NULL) {
        fp = stdout;
        trace->owns_fp = false;
    } else {
        fp = fopen(filename, "w");
        if (!fp) {
            log_error("Cannot open trace file: %s", filename);
            return false;
        }
        trace->owns_fp = true;
    }

    trace->fp = fp;
    trace->active = true;
    trace->count = 0;
    log_info("CPU trace logging to %s", filename ? filename : "stdout");
    return true;
}

void trace_attach(cpu_trace_t* trace, FILE* fp) {
    if (trace->active) {
        trace_close(trace);
    }
    trace->fp = fp;
    trace->active = (fp != NULL);
    trace->count = 0;
    trace->owns_fp = false;
}

void trace_log_instruction(cpu_trace_t* trace, const cpu6502_t* cpu) {
    if (!trace->active || !trace->fp) return;

    /* Check max count */
    if (trace->max_count > 0 && trace->count >= trace->max_count) {
        trace_close(trace);
        return;
    }

    uint16_t pc = cpu->PC;

    /* Read raw instruction bytes (up to 3) */
    /* Cast away const — memory_read doesn't modify state for ROM/RAM reads */
    memory_t* mem = cpu->memory;
    uint8_t b0 = memory_read(mem, pc);
    uint8_t b1 = memory_read(mem, (uint16_t)(pc + 1));
    uint8_t b2 = memory_read(mem, (uint16_t)(pc + 2));

    /* Disassemble */
    char disasm[32];
    int size = cpu_disassemble(cpu, pc, disasm, sizeof(disasm));

    /* Format raw bytes */
    char bytes[12];
    if (size == 1) {
        snprintf(bytes, sizeof(bytes), "%02X      ", b0);
    } else if (size == 2) {
        snprintf(bytes, sizeof(bytes), "%02X %02X   ", b0, b1);
    } else {
        snprintf(bytes, sizeof(bytes), "%02X %02X %02X", b0, b1, b2);
    }

    /* Output: CYCLES  PC  BYTES  DISASM                A=XX X=XX Y=XX SP=XX P=XX */
    fprintf(trace->fp, "%08llu  %04X  %s  %-20s  A=%02X X=%02X Y=%02X SP=%02X P=%02X\n",
            (unsigned long long)cpu->cycles,
            pc, bytes, disasm,
            cpu->A, cpu->X, cpu->Y, cpu->SP, cpu->P);

    trace->count++;
}

void trace_close(cpu_trace_t* trace) {
    if (trace->fp && trace->owns_fp) {
        fclose(trace->fp);
    }
    trace->fp = NULL;
    trace->active = false;
    if (trace->count > 0) {
        log_info("CPU trace: %llu instructions logged", (unsigned long long)trace->count);
    }
}

void trace_set_max(cpu_trace_t* trace, uint64_t max) {
    trace->max_count = max;
}
