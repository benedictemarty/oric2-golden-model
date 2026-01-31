# ORIC-1 Emulator

A cycle-accurate ORIC-1 (8-bit computer) emulator written in C/C++.

## Features

### Core Emulation
- **6502 CPU**: Cycle-accurate emulation of all official opcodes
- **Memory**: 64KB addressable space with ROM/RAM banking
- **VIA 6522**: I/O controller for keyboard and peripherals
- **Video**: Text mode (40x28) and HIRES graphics (240x200, 6 colors)
- **Audio**: AY-3-8910 PSG sound chip emulation
- **Storage**: Cassette (.TAP) and disk (Sedoric) support

### Modern Features
- **Host Filesystem**: Share files between host and emulator
- **Conversion Tools**: Convert BASIC/machine code to .TAP or Sedoric formats
- **Fast Load**: Turbo tape loading
- **Save States**: Quick save/restore (planned)
- **Debugger**: Built-in debugging tools (planned)

## Building

### Prerequisites
```bash
# Install dependencies (Debian/Ubuntu)
sudo apt-get install build-essential cmake libsdl2-dev libsdl2-mixer-dev

# Install dependencies (Fedora)
sudo dnf install gcc-c++ cmake SDL2-devel SDL2_mixer-devel

# Install dependencies (Arch)
sudo pacman -S base-devel cmake sdl2 sdl2_mixer
```

### Compilation
```bash
mkdir build && cd build
cmake ..
make -j$(nproc)
```

### Installation
```bash
sudo make install
```

## Usage

### Running the Emulator
```bash
# Start with default ROM
oric1-emu

# Load a .TAP file
oric1-emu --tape program.tap

# Load a .DSK file
oric1-emu --disk program.dsk

# Mount host directory
oric1-emu --hostfs /path/to/shared/folder
```

### Conversion Tools

#### BASIC to .TAP
```bash
# Convert a BASIC program to tape format
bas2tap program.bas -o program.tap
```

#### Binary to .TAP
```bash
# Convert machine code binary to tape format
bin2tap program.bin --start 0x0400 --exec 0x0400 -o program.tap
```

#### .TAP to Sedoric
```bash
# Convert tape format to Sedoric disk
tap2sedoric program.tap -o disk.dsk
```

#### Hybrid BASIC/Machine Code
```bash
# Convert hybrid BASIC+machine code
bas2tap hybrid.bas --attach code.bin --load-addr 0x0400 -o program.tap
```

## Key Bindings

### Emulator Controls
- **F1**: Help menu
- **F2**: Save state
- **F3**: Load state
- **F4**: Reset (cold)
- **F5**: Reset (warm)
- **F6**: Fast forward
- **F9**: Debugger
- **F10**: Menu
- **F11**: Fullscreen
- **F12**: Screenshot

### ORIC Keyboard
Standard PC keyboard maps to ORIC-1 layout. See [docs/keyboard-mapping.md](docs/keyboard-mapping.md)

## Architecture

```
┌─────────────────────────────────────────────┐
│              ORIC-1 Emulator                │
├─────────────────────────────────────────────┤
│  ┌────────┐  ┌───────┐  ┌────────────────┐ │
│  │  6502  │◄─┤  BUS  ├─►│  Memory (64KB) │ │
│  │  CPU   │  └───┬───┘  └────────────────┘ │
│  └────────┘      │                          │
│                  ├──► VIA 6522 (I/O)        │
│                  ├──► Video System          │
│                  ├──► AY-3-8910 (Audio)     │
│                  ├──► Storage (Tape/Disk)   │
│                  └──► Host Filesystem       │
├─────────────────────────────────────────────┤
│            SDL2 (Display/Audio/Input)       │
└─────────────────────────────────────────────┘
```

## Project Structure

```
Oric1/
├── src/                   # Source code
│   ├── cpu/              # 6502 CPU emulation
│   ├── memory/           # Memory management
│   ├── io/               # I/O (VIA 6522, keyboard)
│   ├── video/            # Video system
│   ├── audio/            # Audio (AY-3-8910)
│   ├── storage/          # Tape/disk storage
│   ├── hostfs/           # Host filesystem
│   └── utils/            # Utilities
├── include/              # Header files
├── tests/                # Test suite
│   ├── unit/            # Unit tests
│   └── integration/     # Integration tests
├── tools/                # Conversion utilities
│   ├── bas2tap.c        # BASIC → .TAP
│   ├── bin2tap.c        # Binary → .TAP
│   └── tap2sedoric.c    # .TAP → Sedoric
├── docs/                 # Documentation
│   ├── api/             # API documentation
│   ├── specs/           # Hardware specifications
│   └── user-guide/      # User manual
├── roms/                 # ROM files
└── build/                # Build directory
```

## Documentation

- [User Guide](docs/user-guide/README.md)
- [API Documentation](docs/api/README.md)
- [Hardware Specifications](docs/specs/README.md)
- [Development Guide](docs/DEVELOPMENT.md)
- [Contributing](docs/CONTRIBUTING.md)

## Roadmap

See [ROADMAP](ROADMAP) for detailed development plan.

### Current Sprint: Sprint 0 (Initialization)
- [x] Project structure
- [x] Agile documentation
- [ ] Build system
- [ ] Initial implementation

### Next Sprint: Sprint 1 (CPU Core)
- [ ] 6502 instruction set
- [ ] Cycle-accurate timing
- [ ] Comprehensive tests

## Testing

```bash
# Run all tests
make test

# Run specific test suite
./build/tests/cpu_tests
./build/tests/memory_tests

# Run with coverage
make coverage
```

## Contributing

Contributions welcome! Please read [CONTRIBUTING.md](docs/CONTRIBUTING.md) first.

### Development Workflow
1. Fork the repository
2. Create a feature branch (`git checkout -b feature/amazing-feature`)
3. Write tests for your changes
4. Implement your feature
5. Ensure all tests pass
6. Update documentation
7. Commit with descriptive message
8. Push and create Pull Request

## Agile Process

This project follows Agile methodology:
- **Sprints**: 2-week iterations
- **Daily Updates**: CIRRUS_OS status file
- **Version Tracking**: Semantic versioning
- **Documentation**: Living documentation updated with code

See [CHANGELOG](CHANGELOG) for version history and [VERSION_TRACKING](VERSION_TRACKING) for current status.

## License

To be determined.

## Credits

### ORIC-1 Hardware
- **CPU**: MOS Technology 6502 @ 1 MHz
- **RAM**: 48 KB (16 KB on base model)
- **ROM**: 16 KB (BASIC 1.0)
- **Video**: ULA (Uncommitted Logic Array)
- **Sound**: General Instrument AY-3-8910
- **I/O**: MOS Technology 6522 VIA

### References
- ORIC-1 Technical Manual
- 6502 Programming Manual
- Defence Force documentation
- Fabrice Frances' EUPHORIC emulator

## Contact

- **Maintainer**: bmarty
- **Email**: bmarty@mailo.com
- **Repository**: [Local development]

## Status

⚠️ **Early Development**: This emulator is in active development. Many features are not yet implemented.

Current Version: **0.1.0-alpha**
Last Updated: **2026-01-31**

---

*Made with ❤️ for the ORIC community*
