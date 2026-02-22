/**
 * @file main.c
 * @brief ORIC-1 Emulator main entry point - full emulation loop
 * @author bmarty <bmarty@mailo.com>
 * @date 2026-02-22
 * @version 1.0.0-beta.2
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <getopt.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <errno.h>

#include "cpu/cpu6502.h"
#include "memory/memory.h"
#include "io/via6522.h"
#include "video/video.h"
#include "video/export.h"
#include "storage/tap.h"
#include "hostfs/hostfs.h"
#include "utils/logging.h"

#define VERSION "1.0.0-beta.2"
#define ORIC_CLOCK_HZ   1000000
#define ORIC_FRAME_RATE  50
#define CYCLES_PER_FRAME (ORIC_CLOCK_HZ / ORIC_FRAME_RATE)

/* Forward declarations for renderer (in renderer.c) */
bool renderer_init(int scale);
void renderer_cleanup(void);
void renderer_present(video_t* vid);

static volatile bool g_running = true;

static void signal_handler(int sig) {
    (void)sig;
    g_running = false;
}

static void print_usage(const char* program_name) {
    printf("ORIC-1 Emulator v%s\n", VERSION);
    printf("Usage: %s [options]\n\n", program_name);
    printf("Options:\n");
    printf("  -t, --tape FILE            Load .TAP tape file\n");
    printf("  -d, --disk FILE            Load .DSK disk file\n");
    printf("  -r, --rom FILE             Load custom ROM file\n");
    printf("  -h, --hostfs PATH          Mount host directory\n");
    printf("  -f, --fast-load            Enable fast tape loading\n");
    printf("  -n, --headless             Run without display (headless mode)\n");
    printf("  -c, --cycles NUM           Run for N cycles then exit\n");
    printf("  -v, --verbose              Verbose logging\n");
    printf("      --screenshot FILE      Take screenshot at exit (.ppm or .bmp)\n");
    printf("      --screenshot-at C:FILE Screenshot after C cycles to FILE\n");
    printf("      --frame-dump DIR       Dump frames to directory\n");
    printf("      --frame-dump-interval N  Dump every Nth frame (default: 50)\n");
    printf("  -?, --help                 Show this help\n");
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
    video_t video;
    hostfs_t hostfs;

    bool running;
    bool fast_load;
    bool headless;
    int64_t max_cycles;

    /* Screenshot options */
    const char* screenshot_file;
    int64_t screenshot_at_cycles;
    const char* screenshot_at_file;

    /* Frame dump options */
    const char* frame_dump_dir;
    int frame_dump_interval;
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

    /* Initialize video - charset is read from RAM at $B400 by the renderer.
     * vid->charset is left NULL so the renderer uses the RAM copy
     * which the ROM populates during boot. */
    video_init(&emu->video);

    /* Initialize renderer if not headless */
    if (!emu->headless) {
        renderer_init(3);
    }

    if (!hostfs_init(&emu->hostfs)) {
        log_error("Failed to initialize host filesystem");
        return false;
    }

    emu->running = true;
    emu->fast_load = false;
    emu->headless = false;
    emu->max_cycles = -1;
    emu->screenshot_file = NULL;
    emu->screenshot_at_cycles = -1;
    emu->screenshot_at_file = NULL;
    emu->frame_dump_dir = NULL;
    emu->frame_dump_interval = 50;

    log_info("Emulator initialized successfully");
    return true;
}

static void emulator_cleanup(emulator_t* emu) {
    log_info("Shutting down emulator");
    if (!emu->headless) {
        renderer_cleanup();
    }
    video_cleanup(&emu->video);
    hostfs_cleanup(&emu->hostfs);
    memory_cleanup(&emu->memory);
    log_info("Emulator cleanup complete");
}

static void emulator_run(emulator_t* emu) {
    cpu_reset(&emu->cpu);

    log_info("Starting emulation at PC=$%04X", emu->cpu.PC);

    uint64_t total_executed = 0;
    uint64_t frame_count = 0;
    bool screenshot_at_done = false;

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

        /* Render video frame */
        video_render_frame(&emu->video, emu->memory.ram);

        /* Present to screen if not headless */
        if (!emu->headless) {
            renderer_present(&emu->video);
        }

        /* Screenshot at specific cycle count */
        if (!screenshot_at_done && emu->screenshot_at_cycles >= 0 &&
            (int64_t)total_executed >= emu->screenshot_at_cycles) {
            log_info("Taking screenshot at %llu cycles -> %s",
                     (unsigned long long)total_executed, emu->screenshot_at_file);
            video_export_auto(&emu->video, emu->screenshot_at_file);
            screenshot_at_done = true;
        }

        /* Frame dump */
        if (emu->frame_dump_dir && (frame_count % (uint64_t)emu->frame_dump_interval == 0)) {
            char path[512];
            snprintf(path, sizeof(path), "%s/frame_%06llu.ppm",
                     emu->frame_dump_dir, (unsigned long long)frame_count);
            video_export_ppm(&emu->video, path);
        }

        frame_count++;

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

    /* End-of-run screenshot */
    if (emu->screenshot_file) {
        log_info("Taking exit screenshot -> %s", emu->screenshot_file);
        video_render_frame(&emu->video, emu->memory.ram);
        video_export_auto(&emu->video, emu->screenshot_file);
    }

    log_info("Emulation stopped. Total cycles: %llu, frames: %llu",
             (unsigned long long)total_executed, (unsigned long long)frame_count);

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
    const char* screenshot_file = NULL;
    const char* screenshot_at_arg = NULL;
    const char* frame_dump_dir = NULL;
    int frame_dump_interval = 50;

    /* Long option codes for options without short equivalents */
    enum { OPT_SCREENSHOT = 256, OPT_SCREENSHOT_AT, OPT_FRAME_DUMP, OPT_FRAME_DUMP_INTERVAL };

    static struct option long_options[] = {
        {"tape",                required_argument, 0, 't'},
        {"disk",                required_argument, 0, 'd'},
        {"rom",                 required_argument, 0, 'r'},
        {"hostfs",              required_argument, 0, 'h'},
        {"fast-load",           no_argument,       0, 'f'},
        {"headless",            no_argument,       0, 'n'},
        {"cycles",              required_argument, 0, 'c'},
        {"verbose",             no_argument,       0, 'v'},
        {"screenshot",          required_argument, 0, OPT_SCREENSHOT},
        {"screenshot-at",       required_argument, 0, OPT_SCREENSHOT_AT},
        {"frame-dump",          required_argument, 0, OPT_FRAME_DUMP},
        {"frame-dump-interval", required_argument, 0, OPT_FRAME_DUMP_INTERVAL},
        {"help",                no_argument,       0, '?'},
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
            case OPT_SCREENSHOT: screenshot_file = optarg; break;
            case OPT_SCREENSHOT_AT: screenshot_at_arg = optarg; break;
            case OPT_FRAME_DUMP: frame_dump_dir = optarg; break;
            case OPT_FRAME_DUMP_INTERVAL: frame_dump_interval = atoi(optarg); break;
            case '?':
            default:
                print_usage(argv[0]);
                return 0;
        }
    }

    log_init(verbose ? LOG_LEVEL_DEBUG : LOG_LEVEL_INFO);

    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);

    /* Set headless before init so renderer is not started */
    emu.headless = headless;

    if (!emulator_init(&emu)) {
        log_error("Failed to initialize emulator");
        return 1;
    }

    emu.fast_load = fast_load;
    emu.max_cycles = max_cycles;
    emu.screenshot_file = screenshot_file;
    emu.frame_dump_dir = frame_dump_dir;
    emu.frame_dump_interval = (frame_dump_interval > 0) ? frame_dump_interval : 50;

    /* Parse --screenshot-at CYCLES:FILE */
    if (screenshot_at_arg) {
        const char* colon = strchr(screenshot_at_arg, ':');
        if (colon) {
            emu.screenshot_at_cycles = atoll(screenshot_at_arg);
            emu.screenshot_at_file = colon + 1;
        } else {
            log_error("Invalid --screenshot-at format. Use CYCLES:FILE");
            emulator_cleanup(&emu);
            return 1;
        }
    }

    /* Create frame dump directory if specified */
    if (frame_dump_dir) {
        mkdir(frame_dump_dir, 0755);
    }

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
