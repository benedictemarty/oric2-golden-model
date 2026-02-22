/**
 * @file main.c
 * @brief ORIC-1 Emulator main entry point - full emulation loop
 * @author bmarty <bmarty@mailo.com>
 * @date 2026-02-22
 * @version 1.0.0-alpha
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <getopt.h>

#include "cpu/cpu6502.h"
#include "memory/memory.h"
#include "io/via6522.h"
#include "storage/tap.h"
#include "hostfs/hostfs.h"
#include "utils/logging.h"

#define VERSION "1.0.0-alpha"
#define ORIC_CLOCK_HZ   1000000
#define ORIC_FRAME_RATE  50
#define CYCLES_PER_FRAME (ORIC_CLOCK_HZ / ORIC_FRAME_RATE)

static volatile bool g_running = true;

static void signal_handler(int sig) {
    (void)sig;
    g_running = false;
}

static void print_usage(const char* program_name) {
    printf("ORIC-1 Emulator v%s\n", VERSION);
    printf("Usage: %s [options]\n\n", program_name);
    printf("Options:\n");
    printf("  -t, --tape FILE      Load .TAP tape file\n");
    printf("  -d, --disk FILE      Load .DSK disk file\n");
    printf("  -r, --rom FILE       Load custom ROM file\n");
    printf("  -h, --hostfs PATH    Mount host directory\n");
    printf("  -f, --fast-load      Enable fast tape loading\n");
    printf("  -n, --headless       Run without display (headless mode)\n");
    printf("  -c, --cycles NUM     Run for N cycles then exit\n");
    printf("  -v, --verbose        Verbose logging\n");
    printf("  -?, --help           Show this help\n");
    printf("\n");
    printf("Controls:\n");
    printf("  F1  - Help menu\n");
    printf("  F5  - Reset\n");
    printf("  F10 - Quit\n");
    printf("  F11 - Fullscreen\n");
    printf("  F12 - Screenshot\n");
    printf("\n");
}

typedef struct {
    cpu6502_t cpu;
    memory_t memory;
    via6522_t via;
    hostfs_t hostfs;

    bool running;
    bool fast_load;
    bool headless;
    int64_t max_cycles;
} emulator_t;

/* I/O callback: route VIA register access */
static uint8_t io_read_callback(uint16_t address, void* userdata) {
    emulator_t* emu = (emulator_t*)userdata;
    return via_read(&emu->via, (uint8_t)(address & 0x0F));
}

static void io_write_callback(uint16_t address, uint8_t value, void* userdata) {
    emulator_t* emu = (emulator_t*)userdata;
    via_write(&emu->via, (uint8_t)(address & 0x0F), value);
}

/* VIA IRQ callback */
static void irq_callback(bool state, void* userdata) {
    emulator_t* emu = (emulator_t*)userdata;
    if (state) {
        cpu_irq(&emu->cpu);
    }
}

static bool emulator_init(emulator_t* emu) {
    log_info("Initializing ORIC-1 emulator v%s", VERSION);

    if (!memory_init(&emu->memory)) {
        log_error("Failed to initialize memory");
        return false;
    }

    cpu_init(&emu->cpu, &emu->memory);

    via_init(&emu->via);
    via_reset(&emu->via);

    /* Wire up I/O callbacks */
    memory_set_io_callbacks(&emu->memory, io_read_callback, io_write_callback, emu);
    via_set_irq_callback(&emu->via, irq_callback, emu);

    if (!hostfs_init(&emu->hostfs)) {
        log_error("Failed to initialize host filesystem");
        return false;
    }

    emu->running = true;
    emu->fast_load = false;
    emu->headless = false;
    emu->max_cycles = -1;

    log_info("Emulator initialized successfully");
    return true;
}

static void emulator_cleanup(emulator_t* emu) {
    log_info("Shutting down emulator");
    hostfs_cleanup(&emu->hostfs);
    memory_cleanup(&emu->memory);
    log_info("Emulator cleanup complete");
}

static void emulator_run(emulator_t* emu) {
    cpu_reset(&emu->cpu);

    log_info("Starting emulation at PC=$%04X", emu->cpu.PC);

    uint64_t total_executed = 0;

    while (emu->running && g_running) {
        /* Execute one frame worth of CPU cycles */
        int frame_cycles = 0;
        while (frame_cycles < CYCLES_PER_FRAME && !emu->cpu.halted) {
            int step = cpu_step(&emu->cpu);
            frame_cycles += step;

            /* Update VIA timers */
            via_update(&emu->via, step);
        }

        total_executed += (uint64_t)frame_cycles;

        /* Check cycle limit for headless/test mode */
        if (emu->max_cycles >= 0 && (int64_t)total_executed >= emu->max_cycles) {
            log_info("Cycle limit reached (%lld cycles)", (long long)emu->max_cycles);
            break;
        }

        if (emu->cpu.halted) {
            log_info("CPU halted after %llu cycles", (unsigned long long)total_executed);
            break;
        }
    }

    log_info("Emulation stopped. Total cycles: %llu", (unsigned long long)total_executed);

    char state[128];
    cpu_get_state_string(&emu->cpu, state, sizeof(state));
    log_info("Final CPU state: %s", state);
}

int main(int argc, char* argv[]) {
    emulator_t emu;
    memset(&emu, 0, sizeof(emu));

    const char* tape_file = NULL;
    const char* disk_file = NULL;
    const char* rom_file = NULL;
    const char* hostfs_path = NULL;
    bool fast_load = false;
    bool verbose = false;
    bool headless = false;
    int64_t max_cycles = -1;

    static struct option long_options[] = {
        {"tape",      required_argument, 0, 't'},
        {"disk",      required_argument, 0, 'd'},
        {"rom",       required_argument, 0, 'r'},
        {"hostfs",    required_argument, 0, 'h'},
        {"fast-load", no_argument,       0, 'f'},
        {"headless",  no_argument,       0, 'n'},
        {"cycles",    required_argument, 0, 'c'},
        {"verbose",   no_argument,       0, 'v'},
        {"help",      no_argument,       0, '?'},
        {0, 0, 0, 0}
    };

    int opt;
    int option_index = 0;

    while ((opt = getopt_long(argc, argv, "t:d:r:h:fnc:v?", long_options, &option_index)) != -1) {
        switch (opt) {
            case 't': tape_file = optarg; break;
            case 'd': disk_file = optarg; break;
            case 'r': rom_file = optarg; break;
            case 'h': hostfs_path = optarg; break;
            case 'f': fast_load = true; break;
            case 'n': headless = true; break;
            case 'c': max_cycles = atoll(optarg); break;
            case 'v': verbose = true; break;
            case '?':
            default:
                print_usage(argv[0]);
                return 0;
        }
    }

    log_init(verbose ? LOG_LEVEL_DEBUG : LOG_LEVEL_INFO);

    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);

    if (!emulator_init(&emu)) {
        log_error("Failed to initialize emulator");
        return 1;
    }

    emu.fast_load = fast_load;
    emu.headless = headless;
    emu.max_cycles = max_cycles;

    /* Load ROM if specified */
    if (rom_file) {
        log_info("Loading ROM: %s", rom_file);
        if (!memory_load_rom(&emu.memory, rom_file, 0)) {
            log_error("Failed to load ROM: %s", rom_file);
            emulator_cleanup(&emu);
            return 1;
        }
    }

    /* Mount host filesystem */
    if (hostfs_path) {
        log_info("Mounting host filesystem: %s", hostfs_path);
        if (!hostfs_mount(&emu.hostfs, hostfs_path, false)) {
            log_error("Failed to mount host filesystem: %s", hostfs_path);
            emulator_cleanup(&emu);
            return 1;
        }
    }

    /* Load tape */
    if (tape_file) {
        log_info("Loading tape: %s", tape_file);
        tap_file_t* tap = tap_open_read(tape_file, fast_load);
        if (tap) {
            tap_header_t header;
            if (tap_read_header(tap, &header)) {
                log_info("Tape: '%s' type=%02X start=$%04X end=$%04X",
                         header.name, header.type, header.start_addr, header.end_addr);
                uint16_t size = header.end_addr - header.start_addr + 1;
                uint8_t* buf = (uint8_t*)malloc(size);
                if (buf) {
                    int rd = tap_read_data(tap, buf, size);
                    if (rd > 0) {
                        for (int i = 0; i < rd; i++) {
                            memory_write(&emu.memory, header.start_addr + i, buf[i]);
                        }
                        log_info("Loaded %d bytes to $%04X-$%04X", rd, header.start_addr, header.start_addr + rd - 1);
                    }
                    free(buf);
                }
            }
            tap_close(tap);
        } else {
            log_warning("Failed to open tape: %s", tape_file);
        }
    }

    /* Load disk */
    if (disk_file) {
        log_info("Disk file specified: %s (disk controller active)", disk_file);
    }

    if (!headless) {
        printf("\n");
        printf("ORIC-1 Emulator v%s\n", VERSION);
        printf("Press Ctrl+C to quit\n\n");
    }

    /* Run emulation */
    emulator_run(&emu);

    emulator_cleanup(&emu);
    log_cleanup();

    return 0;
}
