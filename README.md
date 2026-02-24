# ORIC-1 Emulator

A cycle-accurate ORIC-1 (8-bit computer, 1983) emulator written in C.

**Version: 1.0.0-rc** | **Status: Release Candidate** | **155 tests, 100% pass**

## Quick Start

```bash
# Install dependencies (Debian/Ubuntu)
sudo apt-get install build-essential libsdl2-dev

# Build
make SDL2=1

# Run with ROM
./oric1-emu -r roms/basic10.rom

# Load a tape program
./oric1-emu -r roms/basic10.rom -t program.tap

# Boot Sedoric from disk
./oric1-emu -r roms/basic10.rom --disk-rom roms/microdis.rom -d disk.dsk
```

## Features

### Core Emulation
- **6502 CPU**: Cycle-accurate emulation of all 151 official opcodes
- **Memory**: 64KB addressable space with ROM/RAM banking
- **VIA 6522**: I/O controller with timers, interrupts, keyboard scanning
- **Video**: Text mode (40x28) and HIRES graphics (240x200, 6 colors)
- **Audio**: AY-3-8910 PSG sound chip (3 tone channels, noise, 16 envelope shapes)
- **Storage**: Cassette (.TAP) and disk (Sedoric .DSK) support
- **Microdisc**: WD1793 FDC controller with 4 drives, overlay ROM

### BASIC Loading
- `CLOAD` command supported via ROM patching
- Fast load mode with `-f` flag (direct memory injection)
- Sedoric disk boot with full SYSTEM.DOS loading

### Modern Features
- **Host Filesystem**: Share files between host and emulator (`--hostfs`)
- **Conversion Tools**: `bas2tap`, `bin2tap`, `tap2sedoric`
- **Video Export**: Screenshot in PPM/BMP format (`--screenshot`)
- **Keyboard Layouts**: QWERTY and AZERTY (`--keyboard azerty`)
- **Headless Mode**: Run without display for testing/automation

## Building

### Prerequisites
```bash
# Debian/Ubuntu
sudo apt-get install build-essential cmake libsdl2-dev

# Fedora
sudo dnf install gcc cmake SDL2-devel

# Arch
sudo pacman -S base-devel cmake sdl2
```

### With Make (primary)
```bash
# Build with SDL2 (display + audio)
make SDL2=1

# Build headless (no display)
make

# Debug build
make DEBUG=1 SDL2=1

# Install
sudo make install
```

### With CMake
```bash
mkdir build && cd build
cmake ..
make -j$(nproc)
sudo make install
```

## Usage

### Command Line Options
```
./oric1-emu [OPTIONS]

  -r, --rom FILE          Load BASIC ROM (required)
  -t, --tape FILE         Load .TAP cassette file
  -f, --fast-load         Fast load tape (direct memory injection)
  -d, --disk FILE         Load .DSK disk image (drive A)
  --disk-rom FILE         Load Microdisc ROM for disk boot
  --disk1/2/3 FILE        Load disk for drives B/C/D
  --hostfs DIR            Mount host directory
  --keyboard LAYOUT       Keyboard layout: qwerty (default), azerty
  --headless              Run without display
  --cycles N              Run N cycles then exit (headless)
  --screenshot FILE       Screenshot at exit (.ppm or .bmp)
  --screenshot-at N:FILE  Screenshot after N cycles
  --type-keys N:TEXT      Simulate keyboard input after N cycles
  -v, --verbose           Enable debug logging
```

### Key Bindings
| Key | Function |
|-----|----------|
| F4 | Cold reset |
| F5 | Warm reset |
| F10 | Quit |
| F11 | Fullscreen |
| F12 | Screenshot |

### ORIC Keyboard
Standard PC keyboard maps to ORIC-1 layout. The ORIC has 48 keys arranged in an 8x8 matrix. Shift and CTRL modifiers are supported.

### Loading Programs

**From tape (CLOAD):**
```bash
./oric1-emu -r basic10.rom -t game.tap
# Then type CLOAD in BASIC
```

**From tape (fast load):**
```bash
./oric1-emu -r basic10.rom -t game.tap -f
```

**From Sedoric disk:**
```bash
./oric1-emu -r basic10.rom --disk-rom microdis.rom -d SEDORIC.DSK
```

## Conversion Tools

```bash
# Convert BASIC text file to .TAP
bas2tap program.bas -o program.tap

# Convert binary to .TAP with load/exec address
bin2tap program.bin --start 0x0400 --exec 0x0400 -o program.tap

# Convert .TAP to Sedoric disk
tap2sedoric program.tap -o disk.dsk
```

## Testing

```bash
# Run all tests (155 tests)
make tests

# Run specific test suite
make test-cpu       # 74 CPU tests
make test-memory    # 19 memory tests
make test-io        # 24 VIA/I/O tests
make test-storage   # 12 storage tests
make test-system    # 7 integration tests
make test-video     # 11 video export tests
make test-audio     # 8 PSG audio tests

# Memory leak check
make valgrind

# Static analysis
make static-analysis
```

## Architecture

```
+---------------------------------------------+
|              ORIC-1 Emulator                |
+---------------------------------------------+
|  +--------+  +-------+  +----------------+ |
|  |  6502  |<-|  BUS  |->|  Memory (64KB) | |
|  |  CPU   |  +---+---+  +----------------+ |
|  +--------+      |                          |
|                   +---> VIA 6522 (I/O)      |
|                   +---> Video System        |
|                   +---> AY-3-8910 (Audio)   |
|                   +---> Microdisc (FDC)     |
|                   +---> Host Filesystem     |
+---------------------------------------------+
|            SDL2 (Display/Audio/Input)       |
+---------------------------------------------+
```

## Project Structure

```
Oric1/
  src/
    cpu/          # 6502 CPU emulation
    memory/       # Memory management + banking
    io/           # VIA 6522, keyboard, cassette, microdisc
    video/        # Video (text + HIRES) + export
    audio/        # AY-3-8910 PSG + SDL2 output
    storage/      # TAP, Sedoric, FDC WD1793
    hostfs/       # Host filesystem + VFS
    utils/        # Logging, config
  include/        # Header files
  tests/unit/     # Unit tests (155 tests)
  tools/          # Conversion tools
  examples/       # Example BASIC programs
  docs/           # Documentation
  roms/           # ROM files (not distributed)
```

## Documentation

- [User Guide](docs/user-guide/README.md)
- [Compatibility List](docs/COMPATIBILITY.md)
- [Contributing](CONTRIBUTING.md)
- [Changelog](CHANGELOG)
- [Roadmap](ROADMAP)

## Contributing

See [CONTRIBUTING.md](CONTRIBUTING.md) for build instructions, coding style, and contribution guidelines.

## Credits

### ORIC-1 Hardware (1983)
- **CPU**: MOS Technology 6502 @ 1 MHz
- **RAM**: 48 KB
- **ROM**: 16 KB (BASIC 1.0)
- **Video**: ULA (Uncommitted Logic Array)
- **Sound**: General Instrument AY-3-8910
- **I/O**: MOS Technology 6522 VIA

### References
- ORIC-1 Technical Manual
- 6502 Programming Manual
- Defence Force documentation
- Fabrice Frances' EUPHORIC emulator
- Oricutron (reference implementation for PSG, keyboard, ULA)

## License

To be determined.

## Contact

- **Maintainer**: bmarty
- **Email**: bmarty@mailo.com

---

Current Version: **1.0.0-rc** | Last Updated: **2026-02-24**
