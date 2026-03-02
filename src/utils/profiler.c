/**
 * @file profiler.c
 * @brief CPU performance profiler — execution hotspots and opcode statistics
 * @author bmarty <bmarty@mailo.com>
 * @date 2026-03-02
 * @version 1.0.0-alpha
 */

#include "utils/profiler.h"
#include "memory/memory.h"
#include "utils/logging.h"

#include <string.h>

void profiler_init(cpu_profiler_t* prof) {
    memset(prof, 0, sizeof(*prof));
}

void profiler_start(cpu_profiler_t* prof) {
    prof->active = true;
}

void profiler_stop(cpu_profiler_t* prof) {
    prof->active = false;
}

void profiler_record_instruction(cpu_profiler_t* prof, const cpu6502_t* cpu) {
    if (!prof->active) return;

    uint16_t pc = cpu->PC;
    uint8_t opcode = memory_read(cpu->memory, pc);

    prof->addr_hits[pc]++;
    prof->opcode_hits[opcode]++;
    prof->total_instructions++;
}

void profiler_record_cycles(cpu_profiler_t* prof, uint16_t pc, int cycles) {
    if (!prof->active) return;

    prof->addr_cycles[pc] += (uint32_t)cycles;
    prof->total_cycles += (uint64_t)cycles;
}

void profiler_reset(cpu_profiler_t* prof) {
    bool was_active = prof->active;
    memset(prof, 0, sizeof(*prof));
    prof->active = was_active;
}

/* Helper: find top N addresses by a given counter array */
typedef struct {
    uint16_t addr;
    uint32_t count;
} addr_entry_t;

static void find_top_n(const uint32_t counts[PROFILER_ADDR_SPACE],
                       addr_entry_t* out, int n) {
    /* Simple selection: scan for top N (good enough for 64K entries) */
    memset(out, 0, (size_t)n * sizeof(addr_entry_t));

    for (int addr = 0; addr < PROFILER_ADDR_SPACE; addr++) {
        uint32_t c = counts[addr];
        if (c == 0) continue;

        /* Find minimum in current top-N */
        int min_idx = 0;
        for (int i = 1; i < n; i++) {
            if (out[i].count < out[min_idx].count) {
                min_idx = i;
            }
        }

        if (c > out[min_idx].count) {
            out[min_idx].addr = (uint16_t)addr;
            out[min_idx].count = c;
        }
    }

    /* Sort descending by count (simple insertion sort for small N) */
    for (int i = 1; i < n; i++) {
        addr_entry_t tmp = out[i];
        int j = i - 1;
        while (j >= 0 && out[j].count < tmp.count) {
            out[j + 1] = out[j];
            j--;
        }
        out[j + 1] = tmp;
    }
}

void profiler_report(const cpu_profiler_t* prof, FILE* fp) {
    if (!fp) return;

    fprintf(fp, "═══════════════════════════════════════════════════════\n");
    fprintf(fp, "  CPU Performance Profile\n");
    fprintf(fp, "═══════════════════════════════════════════════════════\n\n");

    fprintf(fp, "Total instructions: %llu\n", (unsigned long long)prof->total_instructions);
    fprintf(fp, "Total cycles:       %llu\n", (unsigned long long)prof->total_cycles);
    if (prof->total_instructions > 0) {
        fprintf(fp, "Avg cycles/instr:   %.2f\n",
                (double)prof->total_cycles / (double)prof->total_instructions);
    }
    fprintf(fp, "\n");

    /* Top 20 hotspots by execution count */
    #define TOP_N 20
    addr_entry_t top_hits[TOP_N];
    find_top_n(prof->addr_hits, top_hits, TOP_N);

    fprintf(fp, "── Top %d addresses by execution count ──\n", TOP_N);
    fprintf(fp, "  %-8s  %-12s  %-8s\n", "Address", "Hits", "% Total");
    for (int i = 0; i < TOP_N; i++) {
        if (top_hits[i].count == 0) break;
        double pct = (prof->total_instructions > 0)
            ? 100.0 * (double)top_hits[i].count / (double)prof->total_instructions
            : 0.0;
        fprintf(fp, "  $%04X     %-12u  %6.2f%%\n",
                top_hits[i].addr, top_hits[i].count, pct);
    }
    fprintf(fp, "\n");

    /* Top 20 hotspots by cycle usage */
    addr_entry_t top_cycles[TOP_N];
    find_top_n(prof->addr_cycles, top_cycles, TOP_N);

    fprintf(fp, "── Top %d addresses by cycle usage ──\n", TOP_N);
    fprintf(fp, "  %-8s  %-12s  %-8s\n", "Address", "Cycles", "% Total");
    for (int i = 0; i < TOP_N; i++) {
        if (top_cycles[i].count == 0) break;
        double pct = (prof->total_cycles > 0)
            ? 100.0 * (double)top_cycles[i].count / (double)prof->total_cycles
            : 0.0;
        fprintf(fp, "  $%04X     %-12u  %6.2f%%\n",
                top_cycles[i].addr, top_cycles[i].count, pct);
    }
    fprintf(fp, "\n");

    /* Opcode frequency histogram */
    fprintf(fp, "── Opcode frequency ──\n");
    fprintf(fp, "  %-8s  %-12s  %-8s\n", "Opcode", "Count", "% Total");
    for (int i = 0; i < PROFILER_OPCODE_COUNT; i++) {
        if (prof->opcode_hits[i] == 0) continue;
        double pct = (prof->total_instructions > 0)
            ? 100.0 * (double)prof->opcode_hits[i] / (double)prof->total_instructions
            : 0.0;
        fprintf(fp, "  $%02X       %-12u  %6.2f%%\n",
                i, prof->opcode_hits[i], pct);
    }
    fprintf(fp, "\n");
    fprintf(fp, "═══════════════════════════════════════════════════════\n");
}

bool profiler_report_to_file(const cpu_profiler_t* prof, const char* filename) {
    FILE* fp = fopen(filename, "w");
    if (!fp) {
        log_error("Cannot open profile output: %s", filename);
        return false;
    }
    profiler_report(prof, fp);
    fclose(fp);
    log_info("Profile report written to %s", filename);
    return true;
}
