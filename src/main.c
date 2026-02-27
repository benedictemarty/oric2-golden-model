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
#include <strings.h>
#include <signal.h>
#include <getopt.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <errno.h>

#include "emulator.h"
#include "cpu/cpu6502.h"
#include "memory/memory.h"
#include "io/via6522.h"
#include "video/video.h"
#include "video/export.h"
#include "storage/tap.h"
#include "storage/disk.h"
#include "storage/sedoric.h"
#include "io/microdisc.h"
#include "audio/audio.h"
#include "io/keyboard.h"
#include "debugger.h"
#ifdef HAS_SDL2
#include <SDL2/SDL.h>
#endif
#include "hostfs/hostfs.h"
#include "utils/logging.h"

/* Forward declarations for renderer (in renderer.c) */
bool renderer_init(int scale);
void renderer_cleanup(void);
void renderer_present(video_t* vid);
void renderer_toggle_fullscreen(void);

static volatile bool g_running = true;

static void signal_handler(int sig) {
    (void)sig;
    g_running = false;
}

static void print_usage(const char* program_name) {
    printf("ORIC-1 Emulator v%s\n", EMU_VERSION);
    printf("Usage: %s [options]\n\n", program_name);
    printf("Options:\n");
    printf("  -t, --tape FILE            Load .TAP tape file\n");
    printf("  -d, --disk FILE            Load .DSK disk file in drive A\n");
    printf("      --disk1 FILE           Load .DSK disk file in drive B\n");
    printf("      --disk2 FILE           Load .DSK disk file in drive C\n");
    printf("      --disk3 FILE           Load .DSK disk file in drive D\n");
    printf("      --disk-rom FILE        Load Microdisc ROM (microdis.rom)\n");
    printf("  -r, --rom FILE             Load custom ROM file\n");
    printf("  -h, --hostfs PATH          Mount host directory\n");
    printf("  -f, --fast-load            Fast tape loading (inject directly, no CLOAD needed)\n");
    printf("  -n, --headless             Run without display (headless mode)\n");
    printf("  -c, --cycles NUM           Run for N cycles then exit\n");
    printf("  -v, --verbose              Verbose logging\n");
    printf("      --screenshot FILE      Take screenshot at exit (.ppm or .bmp)\n");
    printf("      --screenshot-at C:FILE Screenshot after C cycles to FILE\n");
    printf("      --frame-dump DIR       Dump frames to directory\n");
    printf("      --frame-dump-interval N  Dump every Nth frame (default: 50)\n");
    printf("  -k, --keyboard LAYOUT      Keyboard layout: qwerty (default) or azerty\n");
    printf("      --type-keys C:TEXT     Auto-type TEXT after C cycles (\\n=Return, \\pN=pause N sec)\n");
    printf("  -b, --breakpoint ADDR      Break when PC reaches address (hex, e.g. ED8A)\n");
    printf("  -D, --debug                Start in debugger mode (break at first instruction)\n");
    printf("      --break ADDR           Set initial debugger breakpoint (hex)\n");
    printf("  -?, --help                 Show this help\n");
    printf("\n");
    printf("Controls:\n");
    printf("  F1  - Help menu\n");
    printf("  F5  - Reset\n");
    printf("  F9  - Enter debugger\n");
    printf("  F10 - Quit\n");
    printf("  F11 - Fullscreen\n");
    printf("  F12 - Screenshot\n");
    printf("\n");
}

/* emulator_t is defined in include/emulator.h */

/* I/O callback: route VIA and Microdisc register access */
static uint8_t io_read_callback(uint16_t address, void* userdata) {
    emulator_t* emu = (emulator_t*)userdata;

    /* Microdisc I/O: $0310-$031F */
    if (emu->has_microdisc && address >= 0x0310 && address <= 0x031F) {
        return microdisc_read(&emu->microdisc, address);
    }

    /* VIA 6522: $0300-$030F (mirrored in $0300-$03FF) */
    return via_read(&emu->via, (uint8_t)(address & 0x0F));
}

/**
 * @brief Decode PSG bus state and execute operation
 *
 * ORIC-1 PSG (AY-3-8912) is controlled via VIA (from Oricutron):
 * - VIA Port A (ORA) = PSG data bus
 * - VIA CA2 output = PSG BC1 (PCR bits 1-3: mode 6=low, mode 7=high)
 * - VIA CB2 output = PSG BDIR (PCR bits 5-7: mode 6=low, mode 7=high)
 *
 * PSG operations:
 * - BDIR=1, BC1=1 → Latch Address (ORA → PSG address register)
 * - BDIR=1, BC1=0 → Write Data (ORA → selected PSG register)
 * - BDIR=0, BC1=1 → Read Data (selected PSG register → VIA IRA)
 * - BDIR=0, BC1=0 → Inactive
 *
 * The ROM toggles CA2/CB2 via PCR writes, so this function must
 * be called when PCR, ORA, or ORB change.
 */
static void psg_decode(emulator_t* emu) {
    /* BC1 = CA2 output state (PCR bits 1-3) */
    uint8_t ca2_mode = (emu->via.pcr >> 1) & 0x07;
    bool bc1 = (ca2_mode == 0x07); /* Mode 7 = CA2 high */

    /* BDIR = CB2 output state (PCR bits 5-7) */
    uint8_t cb2_mode = (emu->via.pcr >> 5) & 0x07;
    bool bdir = (cb2_mode == 0x07); /* Mode 7 = CB2 high */

    if (bdir && bc1) {
        /* Latch Address */
        ay_write_address(&emu->psg, emu->via.ora);
    } else if (bdir && !bc1) {
        /* Write Data */
        ay_write_data(&emu->psg, emu->via.ora);
    } else if (!bdir && bc1) {
        /* Read Data - PSG data goes onto VIA input for Port A reads */
        emu->via.ira = ay_read_data(&emu->psg);
    }
}

/**
 * @brief PSG Port A input callback - returns keyboard matrix row data
 *
 * VIA ORB bits 0-2 select the keyboard column (active via 74LS138 decoder).
 * Returns row data: 0xFF = no keys, bit cleared = key pressed (active low).
 */
static uint8_t keyboard_matrix_read(void* userdata) {
    emulator_t* emu = (emulator_t*)userdata;
    uint8_t col = emu->via.orb & 0x07;
    return emu->keyboard.matrix[col];
}

/**
 * @brief VIA Port A read callback - returns PSG data bus value
 */
static uint8_t psg_porta_read(void* userdata) {
    emulator_t* emu = (emulator_t*)userdata;
    return ay_read_data(&emu->psg);
}

/**
 * @brief VIA Port B read callback - keyboard scan result on PB3
 *
 * On the ORIC, the keyboard scan works as follows (from Oricutron):
 * - ROM writes a mask to PSG register 14 (which rows to test)
 * - ROM selects column via VIA ORB bits 0-2
 * - Hardware checks if any key matches: keystates[col] & (~reg14)
 * - Result appears on VIA PB3 (bit 3): 1 = key pressed, 0 = no key
 *
 * key_matrix[] uses active-low (0 = pressed), so ~key_matrix gives
 * 1 = pressed (matching Oricutron's keystates convention).
 */
static uint8_t portb_read_callback(void* userdata) {
    emulator_t* emu = (emulator_t*)userdata;
    uint8_t col = emu->via.orb & 0x07;
    uint8_t reg14 = emu->psg.registers[14];

    /* Check: any pressed key in column matches the inverted mask?
     * ~key_matrix = pressed keys (1=pressed), ~reg14 = rows to test */
    uint8_t pressed = (~emu->keyboard.matrix[col]) & (~reg14) & 0xFF;

    /* PB3 = 1 if any key matches, 0 otherwise.
     * Other input bits default to 1 (no external input). */
    return pressed ? 0xFF : 0xF7;
}

static void io_write_callback(uint16_t address, uint8_t value, void* userdata) {
    emulator_t* emu = (emulator_t*)userdata;

    /* Microdisc I/O: $0310-$031F */
    if (emu->has_microdisc && address >= 0x0310 && address <= 0x031F) {
        microdisc_write(&emu->microdisc, address, value);
        /* Sync overlay flags to memory system */
        emu->memory.basic_rom_disabled = emu->microdisc.romdis;
        emu->memory.overlay_active = emu->microdisc.diskrom;
        return;
    }

    uint8_t reg = (uint8_t)(address & 0x0F);

    /* Intercept VIA Port A writes to forward to PSG data bus */
    if (reg == VIA_ORA || reg == 0x0F) {
        /* ORA write: data goes to PSG bus. The actual PSG operation
         * depends on BDIR/BC1 which are set via ORB. */
    }

    via_write(&emu->via, reg, value);

    /* Decode PSG bus state ONLY when control lines change.
     * BC1 = CA2, BDIR = CB2, both controlled by PCR bits.
     * Matching Oricutron: PSG bus decode is triggered only on PCR writes,
     * NOT on ORB writes (which select keyboard columns) or ORA writes
     * (which just change data bus). The ROM sequence is:
     *   1. Write ORA with address/data value
     *   2. Write PCR to set BDIR/BC1 → PSG operation happens HERE
     *   3. Write PCR to clear BDIR/BC1 */
    if (reg == VIA_PCR) {
        psg_decode(emu);
    }
}

/* VIA IRQ callback - level-triggered: set/clear VIA IRQ source bit */
static void irq_callback(bool state, void* userdata) {
    emulator_t* emu = (emulator_t*)userdata;
    if (state) {
        cpu_irq_set(&emu->cpu, IRQF_VIA);
    } else {
        cpu_irq_clear(&emu->cpu, IRQF_VIA);
    }
}

/* Microdisc CPU IRQ callbacks - level-triggered: set/clear DISK IRQ source bit */
static void microdisc_cpu_irq_set(void* userdata) {
    emulator_t* emu = (emulator_t*)userdata;
    cpu_irq_set(&emu->cpu, IRQF_DISK);
}

static void microdisc_cpu_irq_clr(void* userdata) {
    emulator_t* emu = (emulator_t*)userdata;
    cpu_irq_clear(&emu->cpu, IRQF_DISK);
}

static bool emulator_init(emulator_t* emu) {
    log_info("Initializing ORIC-1 emulator v%s", EMU_VERSION);

    if (!memory_init(&emu->memory)) {
        log_error("Failed to initialize memory");
        return false;
    }

    cpu_init(&emu->cpu, &emu->memory);

    via_init(&emu->via);
    via_reset(&emu->via);

    /* Initialize keyboard */
    oric_keyboard_init(&emu->keyboard);

    /* Initialize PSG (AY-3-8912) with keyboard input callback */
    ay_init(&emu->psg, ORIC_CLOCK_HZ);
    emu->psg.porta_input = keyboard_matrix_read;
    emu->psg.userdata = emu;

    /* Wire up I/O callbacks */
    memory_set_io_callbacks(&emu->memory, io_read_callback, io_write_callback, emu);
    via_set_irq_callback(&emu->via, irq_callback, emu);

    /* Connect VIA Port A read to PSG data read (for keyboard scan) */
    emu->via.porta_read = psg_porta_read;
    emu->via.portb_read = portb_read_callback;
    emu->via.userdata = emu;

    /* Initialize video - charset is read from RAM at $B400 by the renderer.
     * vid->charset is left NULL so the renderer uses the RAM copy
     * which the ROM populates during boot. */
    video_init(&emu->video);

    /* Initialize renderer if not headless */
    if (!emu->headless) {
        renderer_init(3);
#ifdef HAS_SDL2
        SDL_StartTextInput();  /* Enable TEXTINPUT events for symbolic keyboard */
#endif
    }

    /* Initialize audio output (connects PSG to SDL2 audio callback) */
    if (!emu->headless) {
        if (!audio_init(&emu->psg)) {
            log_warning("Failed to initialize audio output");
        }
    }

    if (!hostfs_init(&emu->hostfs)) {
        log_error("Failed to initialize host filesystem");
        return false;
    }

    /* Initialize debugger */
    debugger_init(&emu->debugger);

    emu->running = true;
    /* Note: fast_load, headless, max_cycles are set by caller before init */
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
        audio_cleanup();
        renderer_cleanup();
    }
    video_cleanup(&emu->video);
    hostfs_cleanup(&emu->hostfs);
    memory_cleanup(&emu->memory);
    if (emu->tapebuf) {
        free(emu->tapebuf);
        emu->tapebuf = NULL;
    }
    if (emu->has_microdisc) {
        microdisc_cleanup(&emu->microdisc);
    }
    for (int i = 0; i < MICRODISC_MAX_DRIVES; i++) {
        if (emu->disks[i]) {
            sedoric_destroy(emu->disks[i]);
            emu->disks[i] = NULL;
        }
    }
    log_info("Emulator cleanup complete");
}

/**
 * @brief ROM patching for CLOAD support
 *
 * Intercepts ROM cassette routines by checking CPU PC after each instruction.
 * When PC hits known ROM entry points (getsync, readbyte), we inject tape
 * data directly into CPU registers and skip to the routine's RTS.
 * This is the same approach used by Oricutron.
 *
 * ROM addresses (ORIC-1 BASIC 1.0):
 *   getsync:      $E696 (entry) -> $E6B9 (RTS)
 *   readbyte:     $E630 (entry) -> $E65B (RTS)
 *   readbyte_store: $002F (RAM byte store location)
 */
/**
 * ROM patching approach (matching Oricutron):
 *
 * getsync ($E696): Scan forward to first 0x16 byte, leave tapeoffs
 *   pointing AT the 0x16. The ROM's getsync confirmation loop will
 *   then call readbyte to read the remaining sync bytes + marker.
 *   Jump PC to $E6B9 (getsync RTS).
 *
 * readbyte ($E630): Read tapebuf[tapeoffs++] into A, store at $002F,
 *   set Z flag, clear carry (success). Jump PC to $E65B which does
 *   LDA $2F then RTS — this is what Oricutron uses as readbyte_end.
 *
 * getsync_loop ($E681): If CPU gets stuck polling CB1 in wait_edge,
 *   recover by restoring SP and forcing a getsync patch.
 */
static void tape_patches(emulator_t* emu) {
    if (!emu->tape_loaded)
        return;

    uint16_t pc = emu->cpu.PC;

    if (pc == 0xE696) {
        /* getsync: scan forward to first 0x16 sync byte.
         * Leave tapeoffs pointing AT the 0x16 so readbyte will
         * read the sync bytes (ROM confirmation loop needs them). */
        if (emu->tapebuf[emu->tapeoffs] != 0x16) {
            while (emu->tapeoffs < emu->tapelen &&
                   emu->tapebuf[emu->tapeoffs] != 0x16) {
                emu->tapeoffs++;
            }
            if (emu->tapeoffs >= emu->tapelen)
                return; /* No sync found - let ROM timeout */
        }
        /* Save stack pointer for sync loop recovery */
        emu->tape_syncstack = emu->cpu.SP;
        /* Jump to end of getsync */
        emu->cpu.PC = 0xE6B9;
    } else if (pc == 0xE630) {
        /* readbyte: read next byte from tape buffer */
        if (emu->tapeoffs < emu->tapelen) {
            uint8_t byte = emu->tapebuf[emu->tapeoffs++];
            emu->cpu.A = byte;
            /* Set Z flag (like Oricutron: f_z = a == 0) */
            if (byte == 0)
                emu->cpu.P |= FLAG_ZERO;
            else
                emu->cpu.P &= ~FLAG_ZERO;
            /* Clear carry = success */
            emu->cpu.P &= ~FLAG_CARRY;
            /* Store byte at $002F (ROM side effect) */
            memory_write(&emu->memory, 0x002F, byte);
            /* Jump to $E65B (LDA $2F; RTS) — matches Oricutron readbyte_end */
            emu->cpu.PC = 0xE65B;
        }
        /* If tape exhausted, let ROM handle (will timeout) */
    } else if (pc == 0xE681) {
        /* Sync loop recovery: CPU is stuck polling VIA CB1 in wait_edge.
         * This happens if getsync's confirmation readbyte calls enter
         * the real ROM code. Recover by restoring SP and forcing getsync. */
        if (emu->tape_syncstack >= 0) {
            emu->cpu.SP = (uint8_t)emu->tape_syncstack;
            emu->tape_syncstack = -1;
            /* Force getsync patch */
            if (emu->tapebuf[emu->tapeoffs] != 0x16) {
                while (emu->tapeoffs < emu->tapelen &&
                       emu->tapebuf[emu->tapeoffs] != 0x16) {
                    emu->tapeoffs++;
                }
                if (emu->tapeoffs >= emu->tapelen) {
                    emu->tape_loaded = false;
                    return;
                }
            }
            emu->cpu.PC = 0xE6B9;
        }
    }
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
        bool vsync_triggered = false;
        while (frame_cycles < CYCLES_PER_FRAME && !emu->cpu.halted) {
            /* Legacy single breakpoint (--breakpoint / -b) */
            if (emu->breakpoint >= 0 && emu->cpu.PC == (uint16_t)emu->breakpoint) {
                /* Promote to interactive debugger if available */
                emu->debugger.active = true;
            }

            /* Interactive debugger check */
            if (emu->debugger.active || debugger_should_break(&emu->debugger, emu)) {
                debugger_repl(&emu->debugger, emu);
                if (!emu->running) break;
            }

            tape_patches(emu);
            int step = cpu_step(&emu->cpu);
            frame_cycles += step;

            /* Update VIA timers */
            via_update(&emu->via, step);

            /* Microdisc FDC: process delayed DRQ/INTRQ timers */
            if (emu->has_microdisc) {
                fdc_ticktock(&emu->microdisc.fdc, step);
            }

            /* VSync trigger at line 256 (cycle 16384) — On real Oric hardware,
             * the ULA drives CB1 low at the start of vertical blanking.
             * Only generate the CB1 pulse if software has enabled CB1 in IER,
             * to avoid disturbing the ROM keyboard scan IRQ timing. */
            if (!vsync_triggered && frame_cycles >= VSYNC_CYCLE) {
                if (emu->via.ier & VIA_INT_CB1) {
                    via_set_cb1(&emu->via, false);  /* Falling edge: VSync active */
                }
                vsync_triggered = true;
            }
        }

        /* Release VSync at end of frame (CB1 returns high) */
        if (vsync_triggered && !(emu->via.cb1_pin)) {
            via_set_cb1(&emu->via, true);
        }

        total_executed += (uint64_t)frame_cycles;

        /* Auto-type: inject keystrokes at specified cycle count.
         * Each key is pressed for ~2 frames (40ms) then released for ~2 frames.
         * This simulates realistic typing speed for the ROM keyboard scanner. */
        if (emu->type_keys_text && !emu->type_keys_done &&
            (int64_t)total_executed >= emu->type_keys_at) {
            if ((int64_t)total_executed >= emu->type_keys_next_cycle) {
                int idx = emu->type_keys_idx;
                char c = emu->type_keys_text[idx];
                if (c == '\0') {
                    /* Done typing */
                    oric_keyboard_release_all(&emu->keyboard);
                    emu->type_keys_done = true;
                } else if (c == '\\' && emu->type_keys_text[idx+1] == 'n') {
                    /* \n = RETURN */
                    oric_keyboard_release_all(&emu->keyboard);
                    oric_keyboard_press_char(&emu->keyboard, '\n');
                    emu->type_keys_idx += 2;
                    emu->type_keys_next_cycle = (int64_t)total_executed + CYCLES_PER_FRAME * 4;
                } else if (c == '\\' && emu->type_keys_text[idx+1] == 'p') {
                    /* \pN = pause N seconds (N = single digit) */
                    int secs = emu->type_keys_text[idx+2] - '0';
                    if (secs < 1) secs = 1;
                    if (secs > 9) secs = 9;
                    oric_keyboard_release_all(&emu->keyboard);
                    emu->type_keys_idx += 3;
                    emu->type_keys_next_cycle = (int64_t)total_executed + ORIC_CLOCK_HZ * secs;
                } else {
                    /* Regular character */
                    oric_keyboard_release_all(&emu->keyboard);
                    oric_keyboard_press_char(&emu->keyboard, c);
                    emu->type_keys_idx++;
                    emu->type_keys_next_cycle = (int64_t)total_executed + CYCLES_PER_FRAME * 4;
                }
            }
        }

        /* Render video frame */
        video_render_frame(&emu->video, emu->memory.ram);

        /* Present to screen and handle events if not headless */
        if (!emu->headless) {
            renderer_present(&emu->video);
#ifdef HAS_SDL2
            /* Poll SDL events (keyboard, window close, etc.) */
            SDL_Event event;
            while (SDL_PollEvent(&event)) {
                switch (event.type) {
                case SDL_QUIT:
                    emu->running = false;
                    break;
                case SDL_KEYDOWN:
                    /* F5 = Reset, F10 = Quit, F11 = Fullscreen, F12 = Screenshot */
                    switch (event.key.keysym.sym) {
                    case SDLK_F5:
                        cpu_reset(&emu->cpu);
                        break;
                    case SDLK_F9:
                        /* Enter interactive debugger */
                        emu->debugger.active = true;
                        break;
                    case SDLK_F10:
                        emu->running = false;
                        break;
                    case SDLK_F11:
                        renderer_toggle_fullscreen();
                        break;
                    case SDLK_F12:
                        video_export_ppm(&emu->video, "screenshot.ppm");
                        log_info("Screenshot saved to screenshot.ppm");
                        break;
                    default:
                        break;
                    }
                    /* Fall through to keyboard handler */
                    oric_keyboard_handle_sdl_event(&emu->keyboard, &event);
                    break;
                case SDL_KEYUP:
                    oric_keyboard_handle_sdl_event(&emu->keyboard, &event);
                    break;
                case SDL_TEXTINPUT:
                    /* Symbolic mode: character -> ORIC key mapping */
                    oric_keyboard_handle_sdl_event(&emu->keyboard, &event);
                    break;
                default:
                    break;
                }
            }
#endif
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
    emu.breakpoint = -1;

    const char* tape_file = NULL;
    const char* disk_files[MICRODISC_MAX_DRIVES] = {NULL, NULL, NULL, NULL};
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
    const char* keyboard_layout = NULL;

    const char* type_keys_arg = NULL;
    const char* disk_rom_file = NULL;
    bool debug_mode = false;
    const char* debug_break_addr = NULL;

    /* Long option codes for options without short equivalents */
    enum { OPT_SCREENSHOT = 256, OPT_SCREENSHOT_AT, OPT_FRAME_DUMP, OPT_FRAME_DUMP_INTERVAL, OPT_TYPE_KEYS, OPT_DISK_ROM, OPT_DISK1, OPT_DISK2, OPT_DISK3, OPT_BREAKPOINT, OPT_DEBUG_BREAK };

    static struct option long_options[] = {
        {"tape",                required_argument, 0, 't'},
        {"disk",                required_argument, 0, 'd'},
        {"disk1",               required_argument, 0, OPT_DISK1},
        {"disk2",               required_argument, 0, OPT_DISK2},
        {"disk3",               required_argument, 0, OPT_DISK3},
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
        {"keyboard",            required_argument, 0, 'k'},
        {"type-keys",           required_argument, 0, OPT_TYPE_KEYS},
        {"disk-rom",            required_argument, 0, OPT_DISK_ROM},
        {"breakpoint",          required_argument, 0, 'b'},
        {"debug",               no_argument,       0, 'D'},
        {"break",               required_argument, 0, OPT_DEBUG_BREAK},
        {"help",                no_argument,       0, '?'},
        {0, 0, 0, 0}
    };

    int opt;
    int option_index = 0;

    while ((opt = getopt_long(argc, argv, "t:d:r:h:fnc:vk:b:D?", long_options, &option_index)) != -1) {
        switch (opt) {
            case 't': tape_file = optarg; break;
            case 'd': disk_files[0] = optarg; break;
            case OPT_DISK1: disk_files[1] = optarg; break;
            case OPT_DISK2: disk_files[2] = optarg; break;
            case OPT_DISK3: disk_files[3] = optarg; break;
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
            case 'k': keyboard_layout = optarg; break;
            case OPT_TYPE_KEYS: type_keys_arg = optarg; break;
            case OPT_DISK_ROM: disk_rom_file = optarg; break;
            case 'b': emu.breakpoint = (int32_t)strtol(optarg, NULL, 16); break;
            case 'D': debug_mode = true; break;
            case OPT_DEBUG_BREAK: debug_break_addr = optarg; break;
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

    /* Set keyboard layout */
    if (keyboard_layout && strcasecmp(keyboard_layout, "azerty") == 0) {
        oric_keyboard_set_layout(&emu.keyboard, ORIC_KB_AZERTY);
        log_info("Keyboard layout: AZERTY");
    } else {
        log_info("Keyboard layout: QWERTY");
    }
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

    /* Parse --type-keys CYCLES:TEXT */
    if (type_keys_arg) {
        const char* colon = strchr(type_keys_arg, ':');
        if (colon) {
            emu.type_keys_at = atoll(type_keys_arg);
            emu.type_keys_text = colon + 1;
            emu.type_keys_idx = 0;
            emu.type_keys_next_cycle = emu.type_keys_at;
            emu.type_keys_done = false;
            log_info("Auto-type at %lld cycles: \"%s\"",
                     (long long)emu.type_keys_at, emu.type_keys_text);
        } else {
            log_error("Invalid --type-keys format. Use CYCLES:TEXT (e.g. 3000000:CLOAD\"\"\\n)");
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
        if (fast_load) {
            /* Fast load: inject directly into memory (no CLOAD needed) */
            tap_file_t* tap = tap_open_read(tape_file, true);
            if (tap) {
                tap_header_t header;
                if (tap_read_header(tap, &header)) {
                    log_info("Fast load: '%s' type=%02X start=$%04X end=$%04X",
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
        } else {
            /* Normal load: buffer TAP for CLOAD via ROM patching */
            FILE* f = fopen(tape_file, "rb");
            if (f) {
                fseek(f, 0, SEEK_END);
                emu.tapelen = ftell(f);
                fseek(f, 0, SEEK_SET);
                emu.tapebuf = (uint8_t*)malloc(emu.tapelen);
                if (emu.tapebuf) {
                    size_t rd = fread(emu.tapebuf, 1, emu.tapelen, f);
                    if ((int)rd == emu.tapelen) {
                        emu.tapeoffs = 0;
                        emu.tape_loaded = true;
                        emu.tape_syncstack = -1;
                        log_info("Tape buffered for CLOAD: %d bytes", emu.tapelen);
                    } else {
                        log_warning("Tape read incomplete: %zu/%d bytes", rd, emu.tapelen);
                        free(emu.tapebuf);
                        emu.tapebuf = NULL;
                    }
                }
                fclose(f);
            } else {
                log_warning("Failed to open tape: %s", tape_file);
            }
        }
    }

    /* Load disks with Microdisc controller */
    bool any_disk = false;
    for (int i = 0; i < MICRODISC_MAX_DRIVES; i++) {
        if (disk_files[i]) { any_disk = true; break; }
    }

    if (any_disk) {
        /* Initialize Microdisc controller */
        microdisc_init(&emu.microdisc);
        emu.microdisc.cpu_irq_set = microdisc_cpu_irq_set;
        emu.microdisc.cpu_irq_clr = microdisc_cpu_irq_clr;
        emu.microdisc.cpu_userdata = &emu;
        emu.has_microdisc = true;

        /* Load Microdisc ROM if specified */
        if (disk_rom_file) {
            log_info("Loading Microdisc ROM: %s", disk_rom_file);
            if (!microdisc_load_rom(&emu.microdisc, disk_rom_file)) {
                log_error("Failed to load Microdisc ROM: %s", disk_rom_file);
                emulator_cleanup(&emu);
                return 1;
            }
            /* Set overlay ROM in memory system */
            emu.memory.overlay_rom = emu.microdisc.diskrom_data;
            emu.memory.overlay_rom_size = emu.microdisc.diskrom_size;
            emu.memory.overlay_active = true;
            emu.memory.basic_rom_disabled = true;
            log_info("Microdisc ROM loaded (%u bytes), overlay active", emu.microdisc.diskrom_size);
        }

        /* Load disk images into drives A-D */
        for (int i = 0; i < MICRODISC_MAX_DRIVES; i++) {
            if (!disk_files[i]) continue;

            log_info("Loading disk drive %c: %s", 'A' + i, disk_files[i]);
            emu.disks[i] = sedoric_load(disk_files[i]);
            if (!emu.disks[i]) {
                log_error("Failed to load disk image: %s", disk_files[i]);
                emulator_cleanup(&emu);
                return 1;
            }

            /* Connect disk data to Microdisc drive slot */
            microdisc_set_disk(&emu.microdisc, (uint8_t)i,
                               emu.disks[i]->data, emu.disks[i]->size,
                               emu.disks[i]->tracks, emu.disks[i]->sectors);
            log_info("Drive %c: %u bytes, %d sides x %d tracks x %d sectors",
                     'A' + i, emu.disks[i]->size, emu.disks[i]->sides,
                     emu.disks[i]->tracks, emu.disks[i]->sectors);
        }
    }

    /* Setup debugger if requested */
    if (debug_mode) {
        emu.debugger.active = true;
        log_info("Debugger mode enabled (will break at first instruction)");
    }
    if (debug_break_addr) {
        uint16_t addr = (uint16_t)strtol(debug_break_addr, NULL, 16);
        debugger_add_breakpoint(&emu.debugger, addr);
        log_info("Debugger breakpoint set at $%04X", addr);
    }

    if (!headless) {
        printf("\n");
        printf("ORIC-1 Emulator v%s\n", EMU_VERSION);
        printf("Press Ctrl+C to quit\n\n");
    }

    /* Run emulation */
    emulator_run(&emu);

    emulator_cleanup(&emu);
    log_cleanup();

    return 0;
}
