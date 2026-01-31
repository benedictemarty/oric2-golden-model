/**
 * @file main.c
 * @brief ORIC-1 Emulator main entry point
 * @author bmarty <bmarty@mailo.com>
 * @date 2026-01-31
 * @version 0.1.0-alpha
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <getopt.h>

#include "cpu/cpu6502.h"
#include "memory/memory.h"
#include "io/via6522.h"
#include "storage/tap.h"
#include "hostfs/hostfs.h"
#include "utils/logging.h"

#define VERSION "0.1.0-alpha"

/**
 * @brief Print usage information
 */
static void print_usage(const char* program_name) {
    printf("ORIC-1 Emulator v%s\n", VERSION);
    printf("Usage: %s [options]\n\n", program_name);
    printf("Options:\n");
    printf("  -t, --tape FILE      Load .TAP tape file\n");
    printf("  -d, --disk FILE      Load .DSK disk file\n");
    printf("  -r, --rom FILE       Load custom ROM file\n");
    printf("  -h, --hostfs PATH    Mount host directory\n");
    printf("  -f, --fast-load      Enable fast tape loading\n");
    printf("  -v, --verbose        Verbose logging\n");
    printf("  -?, --help           Show this help\n");
    printf("\n");
    printf("Controls:\n");
    printf("  F1  - Help menu\n");
    printf("  F5  - Reset\n");
    printf("  F11 - Fullscreen\n");
    printf("  F12 - Screenshot\n");
    printf("\n");
}

/**
 * @brief Main emulator structure
 */
typedef struct {
    cpu6502_t cpu;
    memory_t memory;
    via6522_t via;
    hostfs_t hostfs;

    bool running;
    bool fast_load;
} emulator_t;

/**
 * @brief Initialize emulator
 */
static bool emulator_init(emulator_t* emu) {
    log_info("Initializing ORIC-1 emulator v%s", VERSION);

    /* Initialize memory */
    if (!memory_init(&emu->memory)) {
        log_error("Failed to initialize memory");
        return false;
    }

    /* Initialize CPU */
    cpu_init(&emu->cpu, &emu->memory);
    cpu_reset(&emu->cpu);

    /* Initialize VIA */
    via_init(&emu->via);
    via_reset(&emu->via);

    /* Initialize host filesystem */
    if (!hostfs_init(&emu->hostfs)) {
        log_error("Failed to initialize host filesystem");
        return false;
    }

    emu->running = true;
    emu->fast_load = false;

    log_info("Emulator initialized successfully");
    return true;
}

/**
 * @brief Cleanup emulator
 */
static void emulator_cleanup(emulator_t* emu) {
    log_info("Shutting down emulator");

    hostfs_cleanup(&emu->hostfs);
    memory_cleanup(&emu->memory);

    log_info("Emulator cleanup complete");
}

/**
 * @brief Main entry point
 */
int main(int argc, char* argv[]) {
    emulator_t emu;
    const char* tape_file = NULL;
    const char* disk_file = NULL;
    const char* rom_file = NULL;
    const char* hostfs_path = NULL;
    bool fast_load = false;
    bool verbose = false;

    /* Parse command line arguments */
    static struct option long_options[] = {
        {"tape",      required_argument, 0, 't'},
        {"disk",      required_argument, 0, 'd'},
        {"rom",       required_argument, 0, 'r'},
        {"hostfs",    required_argument, 0, 'h'},
        {"fast-load", no_argument,       0, 'f'},
        {"verbose",   no_argument,       0, 'v'},
        {"help",      no_argument,       0, '?'},
        {0, 0, 0, 0}
    };

    int opt;
    int option_index = 0;

    while ((opt = getopt_long(argc, argv, "t:d:r:h:fv?", long_options, &option_index)) != -1) {
        switch (opt) {
            case 't':
                tape_file = optarg;
                break;
            case 'd':
                disk_file = optarg;
                break;
            case 'r':
                rom_file = optarg;
                break;
            case 'h':
                hostfs_path = optarg;
                break;
            case 'f':
                fast_load = true;
                break;
            case 'v':
                verbose = true;
                break;
            case '?':
            default:
                print_usage(argv[0]);
                return 0;
        }
    }

    /* Initialize logging */
    log_init(verbose ? LOG_LEVEL_DEBUG : LOG_LEVEL_INFO);

    /* Initialize emulator */
    if (!emulator_init(&emu)) {
        log_error("Failed to initialize emulator");
        return 1;
    }

    emu.fast_load = fast_load;

    /* Load ROM if specified */
    if (rom_file) {
        log_info("Loading ROM: %s", rom_file);
        if (!memory_load_rom(&emu.memory, rom_file, 0xE000)) {
            log_error("Failed to load ROM");
            emulator_cleanup(&emu);
            return 1;
        }
    }

    /* Mount host filesystem if specified */
    if (hostfs_path) {
        log_info("Mounting host filesystem: %s", hostfs_path);
        if (!hostfs_mount(&emu.hostfs, hostfs_path, false)) {
            log_error("Failed to mount host filesystem");
            emulator_cleanup(&emu);
            return 1;
        }
    }

    /* Load tape if specified */
    if (tape_file) {
        log_info("Loading tape: %s", tape_file);
        /* TODO: Implement tape loading */
    }

    /* Load disk if specified */
    if (disk_file) {
        log_info("Loading disk: %s", disk_file);
        /* TODO: Implement disk loading */
    }

    log_info("Starting emulation...");

    /* Main emulation loop (stub for now) */
    printf("\n");
    printf("╔═══════════════════════════════════════════════════════════╗\n");
    printf("║          ORIC-1 EMULATOR v%s (ALPHA)                ║\n", VERSION);
    printf("╠═══════════════════════════════════════════════════════════╣\n");
    printf("║  Status: Emulator initialized successfully                ║\n");
    printf("║  Note: This is an early development version               ║\n");
    printf("║        Core emulation not yet implemented                 ║\n");
    printf("╚═══════════════════════════════════════════════════════════╝\n");
    printf("\n");

    /* TODO: Implement main emulation loop
     * while (emu.running) {
     *     cpu_execute_cycles(&emu.cpu, 1000000 / 60);  // ~60Hz frame
     *     via_update(&emu.via, 1000000 / 60);
     *     // Update video, audio, etc.
     * }
     */

    /* Cleanup */
    emulator_cleanup(&emu);
    log_cleanup();

    return 0;
}
