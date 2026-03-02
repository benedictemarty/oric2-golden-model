# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Project Overview

Oric-1 cycle-accurate emulator written in C11. Emulates the complete ORIC-1 8-bit computer (1983): MOS 6502 CPU, 64KB memory with ROM/RAM banking, VIA 6522, AY-3-8910 PSG audio, ULA video (text 40x28 + HIRES 240x200), Microdisc WD1793 FDC, and cassette TAP format. Optional SDL2 for display/audio/input.

## Build Commands

```bash
make SDL2=1              # Standard build with SDL2
make DEBUG=1 SDL2=1      # Debug build (-g -O0)
make                     # Headless build (no SDL2)
make clean               # Clean build artifacts
make install PREFIX=/usr/local
```

## Testing

```bash
make tests               # All 176 tests (must all pass before commit)
make test-cpu            # 74 CPU tests
make test-memory         # 19 memory tests
make test-io             # 29 VIA/I/O tests
make test-storage        # 12 storage tests
make test-system         # 7 integration tests
make test-video          # 11 video export tests
make test-audio          # 8 PSG audio tests
make test-debugger       # 8 debugger tests
make test-savestate      # 8 save state tests
make valgrind            # Memory leak detection
make static-analysis     # Compiler warnings analysis
```

Test framework is custom C macros (no external dependency): `TEST()`, `RUN()`, `ASSERT_EQ()`, `ASSERT_TRUE()`.

## Architecture

### Core emulator structure: `emulator_t` (include/emulator.h)
Contains all hardware subsystems: cpu, memory, via, psg, video, keyboard, microdisc, debugger, disk drives.

### Hardware subsystems (src/)
- **cpu/** — MOS 6502: 151 opcodes, 13 addressing modes, cycle-accurate, level-triggered IRQ (IRQF_VIA, IRQF_DISK)
- **memory/** — 64KB: RAM ($0000-$BFFF), VIA I/O ($0300-$030F), Microdisc I/O ($0310-$031F), ROM/RAM overlay ($C000-$FFFF)
- **io/via6522.c** — VIA 6522: 16 registers, Timer 1/2, IFR/IER interrupts, Port A/B callbacks, keyboard matrix scanning
- **io/keyboard.c** — 8x8 matrix: VIA ORB bits 0-2 select column, Port A reads rows (active low)
- **io/microdisc.c** — Microdisc: WD1793 FDC at $0310-$031F, 4 drives, overlay ROM banking
- **video/** — ULA: text/HIRES framebuffer, PPM/BMP/ASCII export
- **audio/** — AY-3-8910 PSG: 3 tone + noise + envelope, SDL2 audio callback
- **storage/** — TAP format, Sedoric filesystem, WD1793 disk controller
- **debugger.c** — Interactive REPL: breakpoints, watchpoints, step/continue, register/memory inspection

### Emulation loop (src/main.c)
Runs 20,000 CPU cycles per frame (50 FPS @ 1 MHz). Each cycle: debugger check → cpu_step → tape patches → via_tick → PSG decode. After frame: video render → SDL2 present.

### I/O routing
Memory reads/writes at $0300-$031F trigger `io_read_callback()`/`io_write_callback()` which route to VIA or Microdisc.

### Tools (tools/)
- `bas2tap` — BASIC text → .TAP
- `bin2tap` — Binary → .TAP with load/exec address
- `tap2sedoric` — .TAP → Sedoric disk

## Key Conventions

- **Language:** C11, gcc >= 9.0 or clang >= 10.0
- **Style:** 4-space indent, K&R braces, `snake_case` functions/vars, `UPPER_CASE` macros, 100 char line limit
- **Headers:** Include guards `#ifndef FILE_H`
- **Commits:** Conventional commits (feat:, fix:, docs:, test:)
- **Versioning:** Semantic (MAJOR.MINOR.PATCH-LABEL)

## Per-Commit Requirements

Every modification must:
1. Run `make tests` — all tests must pass
2. Update **CHANGELOG** with changes
3. Update **VERSION_TRACKING** with version metadata
4. Update **CIRRUS_OS** with build/test status
5. Update **ROADMAP** with sprint progress

## Running the Emulator

```bash
./oric1-emu -r roms/basic10.rom                          # Boot BASIC
./oric1-emu -r roms/basic10.rom -t prog.tap -f           # Fast-load tape
./oric1-emu -r roms/basic10.rom --disk-rom roms/microdis.rom -d SEDO40u.DSK  # Sedoric
./oric1-emu -r roms/basic10.rom --debug                  # Start in debugger
```

## Dependencies

- **Required:** GCC/Clang, Make, libm
- **Optional:** SDL2 (display/audio/input), pkg-config, CMake, Valgrind
