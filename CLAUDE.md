# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Project Overview

**Phosphoric** — Cycle-accurate ORIC-1/Atmos emulator written in C11. Emulates the complete ORIC 8-bit computer (1983): MOS 6502 CPU, 64KB memory with ROM/RAM banking, VIA 6522, AY-3-8910 PSG audio, ULA video (text 40x28 + HIRES 240x200), Microdisc WD1793 FDC, and cassette TAP format. Supports both ORIC-1 (BASIC 1.0) and Atmos (BASIC 1.1) with ROM auto-detection. Optional SDL2 for display/audio/input.

## Build Commands

```bash
make SDL2=1              # Standard build with SDL2
make DEBUG=1 SDL2=1      # Debug build (-g -O0)
make                     # Headless build (no SDL2)
make SDL2=1 CAST=1       # Build with Chromecast MJPEG streaming
make COVERAGE=1          # Build with gcov coverage instrumentation
make tools               # Build conversion tools (bas2tap, bin2tap, tap2sedoric)
make clean               # Clean build artifacts
make install PREFIX=/usr/local
```

## Testing

```bash
make tests               # All test suites (must all pass before commit)
make test-cpu            # CPU tests (74)
make test-memory         # Memory tests (19)
make test-io             # VIA/I/O tests (29)
make test-storage        # Storage tests (12)
make test-system         # Integration tests (7)
make test-rom            # ROM compatibility tests
make test-video          # Video export tests (11)
make test-audio          # PSG audio tests (8)
make test-debugger       # Debugger tests (8)
make test-savestate      # Save state tests (8)
make test-atmos          # Atmos support tests (10)
make test-joystick       # Joystick tests (10)
make test-printer        # Printer tests (10)
make test-mcp40          # MCP-40 plotter tests (10)
make test-renderer       # Display scaling tests (10)
make test-trace          # CPU trace logging tests (10)
make test-profiler       # CPU profiler tests (10)
make test-rominfo        # ROM analysis tests (10)
make test-serial         # ACIA 6551 serial tests (17)
make test-coverage       # Code coverage meta-tests
make test-cast           # Cast server tests (requires CAST=1 build)
make valgrind            # Memory leak detection (all suites under Valgrind)
make static-analysis     # Extra compiler warnings (-Wshadow, -Wconversion, etc.)
make coverage            # Full coverage pipeline: clean, build with gcov, run, report
```

Test framework is custom C macros redefined in each test file (no shared header, no external dependency): `TEST()`, `RUN()`, `ASSERT_EQ()`, `ASSERT_TRUE()`, `ASSERT_FALSE()`. Each test file defines its own `tests_passed`/`tests_failed` counters and a `setup()` helper to initialize the relevant subsystem. To add a new test: define a `TEST(name)` function, call it via `RUN(name)` in `main()`. Tests compile and run in one step via their Makefile target.

## Architecture

### Core emulator structure: `emulator_t` (include/emulator.h)
Central struct containing all hardware subsystems. Passed as pointer to most subsystem functions. Key constants: `CYCLES_PER_FRAME = 19968` (PAL: 312 lines x 64 cycles), `ORIC_CLOCK_HZ = 1000000`, `ORIC_FRAME_RATE = 50`.

### Hardware subsystems (src/)
- **cpu/** — MOS 6502: 151 opcodes, 13 addressing modes, cycle-accurate, level-triggered IRQ (IRQF_VIA, IRQF_DISK)
- **memory/** — 64KB: RAM ($0000-$BFFF), VIA I/O ($0300-$030F), Microdisc I/O ($0310-$031F), ROM/RAM overlay ($C000-$FFFF)
- **io/via6522.c** — VIA 6522: 16 registers, Timer 1/2, IFR/IER interrupts, Port A/B callbacks, keyboard matrix scanning
- **io/keyboard.c** — 8x8 matrix: VIA ORB bits 0-2 select column, Port A reads rows (active low)
- **io/joystick.c** — IJK joystick adapter: active low on PSG Port A, keyboard/gamepad modes
- **io/printer.c** — Centronics printer: VIA Port A data + CA2 STROBE, text file capture
- **io/mcp40.c** — MCP-40 4-color pen plotter: 480x400 framebuffer, Bresenham line drawing, BMP export
- **io/cassette.c** — Cassette interface: TAP format loading/saving (CLOAD/CSAVE ROM patching, post-CLOAD rechain)
- **io/acia6551.c** — ACIA 6551 serial at $031C-$031F: TX/RX, IRQ, baud rate timing, V23 mode (Digitelec DTL 2000, Minitel)
- **io/serial_backend.c** — Serial backends: loopback, TCP, PTY, modem Hayes (AT commands, 64KB buffers), COM (termios)
- **io/microdisc.c** — Microdisc: WD1793 FDC at $0310-$031F, 4 drives, overlay ROM banking
- **video/** — ULA: text/HIRES framebuffer, PPM/BMP/ASCII export, `renderer.c` for SDL2 scaling (x1-x4)
- **audio/** — AY-3-8910 PSG: 3 tone + noise + envelope, SDL2 audio callback
- **storage/** — TAP format, Sedoric filesystem, WD1793 disk controller
- **hostfs/** — Host filesystem sharing (--hostfs DIR), VFS abstraction layer
- **utils/** — Logging, INI config parser, CPU trace, CPU profiler, ROM analysis
- **network/** — MJPEG cast server, CASTV2 Chromecast client (requires CAST=1)
- **debugger.c** — Interactive REPL: breakpoints (16 max), watchpoints (8 max), step/continue, register/memory inspection
- **savestate.c** — Binary .ost format: 10 sections (CPU, MEM, VIA, PSG, VID, KBD, FDC, MDC, TAP, META) with CRC32

### Emulation loop (src/main.c)
Runs `CYCLES_PER_FRAME` (19968) CPU cycles per frame at 50 FPS. Each cycle: debugger check → cpu_step → tape patches → via_tick → PSG decode. After frame: video render → SDL2 present.

### I/O routing
Memory reads/writes at $0300-$031F trigger `io_read_callback()`/`io_write_callback()` which route to VIA ($0300-$030F) or Microdisc ($0310-$031F). Callbacks are registered via `memory_set_io_callbacks()` in `main.c`.

### Tape loading flow
ROM cassette routines (CLOAD/CSAVE) are intercepted by PC-matching patches (`rom_patches_t` in `emulator.h`). Patch addresses differ between BASIC 1.0 and 1.1 — selected at boot via ROM auto-detection. Post-CLOAD, BASIC line pointers are rechained (`cassette_rechain_basic()`) to fix link addresses.

### Tools (tools/)
- `bas2tap` — BASIC text → .TAP
- `bin2tap` — Binary → .TAP with load/exec address
- `tap2sedoric` — .TAP → Sedoric disk

## Key Conventions

- **Language:** C11, gcc >= 9.0 or clang >= 10.0
- **Style:** 4-space indent, K&R braces, `snake_case` functions/vars, `UPPER_CASE` macros, 100 char line limit
- **Headers:** Include guards `#ifndef FILE_H`
- **Commits:** Conventional commits (feat:, fix:, docs:, test:)
- **Versioning:** Semantic (MAJOR.MINOR.PATCH-LABEL), current version in `EMU_VERSION` macro in `include/emulator.h`

## Per-Commit Requirements

Every modification must:
1. Run `make tests` — all tests must pass
2. Update **CHANGELOG** — append entry under current version section (plain text, reverse chronological)
3. Update **VERSION_TRACKING** — version metadata, date, test count
4. Update **CIRRUS_OS** — build/test status summary
5. Update **ROADMAP** — sprint progress and task completion

## Running the Emulator

```bash
./oric1-emu -r roms/basic10.rom                          # Boot BASIC 1.0 (ORIC-1)
./oric1-emu -r roms/basic11b.rom                         # Boot BASIC 1.1 (Atmos, auto-detected)
./oric1-emu -r roms/basic10.rom -t prog.tap -f           # Fast-load tape
./oric1-emu -r roms/basic10.rom --disk-rom roms/microdis.rom -d SEDO40u.DSK  # Sedoric
./oric1-emu -r roms/basic10.rom --debug                  # Start in debugger
./oric1-emu -r roms/basic10.rom -j keys                  # IJK joystick via keyboard
./oric1-emu -r roms/basic10.rom --trace trace.log        # CPU instruction trace
./oric1-emu -r roms/basic10.rom --profile prof.txt       # CPU profiler
./oric1-emu -r roms/basic10.rom --rom-info               # ROM analysis
./oric1-emu -r roms/basic10.rom --serial tcp:bbs.host:23  # Serial via TCP (BBS/telnet)
./oric1-emu -r roms/basic10.rom --serial pty              # Serial via pseudo-terminal
./oric1-emu -r roms/basic10.rom --serial modem:bbs.host:23 --serial-v23  # Modem Hayes + V23
./oric1-emu -r roms/basic10.rom --serial modem:listen:2323               # BBS server mode
./oric1-emu -r roms/basic10.rom --serial com:9600,8,N,1,/dev/ttyUSB0    # Vrai port série
./oric1-emu -r roms/basic10.rom --serial loopback --acia-addr 0320      # Offset custom
```

## Dependencies

- **Required:** GCC/Clang, Make, libm
- **Optional:** SDL2 (display/audio/input), pkg-config, Valgrind, OpenSSL (for CAST=1)
