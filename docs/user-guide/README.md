# ORIC-1 Emulator User Guide

## Installation

### From Source

```bash
sudo apt-get install build-essential libsdl2-dev
make SDL2=1
sudo make install
```

### Running

```bash
oric1-emu -r /path/to/basic10.rom
```

You need an ORIC-1 BASIC 1.0 ROM file (`basic10.rom`, 16384 bytes). ROM files are not distributed with the emulator due to copyright.

## Loading Programs

### Tape Files (.TAP)

**Interactive loading (CLOAD):**
```bash
oric1-emu -r basic10.rom -t game.tap
```
After the emulator starts, type `CLOAD` at the BASIC prompt. The program loads from the virtual tape.

**Fast loading:**
```bash
oric1-emu -r basic10.rom -t game.tap -f
```
The program is injected directly into memory and runs immediately.

### Disk Files (.DSK)

To boot from a Sedoric disk, you need both the BASIC ROM and the Microdisc ROM:

```bash
oric1-emu -r basic10.rom --disk-rom microdis.rom -d SEDORIC.DSK
```

Multiple drives are supported:
```bash
oric1-emu -r basic10.rom --disk-rom microdis.rom \
  -d system.dsk --disk1 data.dsk --disk2 games.dsk
```

### Host Filesystem

Share a directory between your host system and the emulator:
```bash
oric1-emu -r basic10.rom --hostfs /path/to/shared/folder
```

## Keyboard

The PC keyboard maps to the ORIC-1 keyboard layout. The mapping is QWERTY by default.

### Switching to AZERTY

```bash
oric1-emu -r basic10.rom --keyboard azerty
```

In AZERTY mode, the emulator uses SDL text input events, so typing works naturally regardless of your physical keyboard layout.

### Emulator Function Keys

| Key | Function |
|-----|----------|
| F4 | Cold reset |
| F5 | Warm reset |
| F10 | Quit emulator |
| F11 | Toggle fullscreen |
| F12 | Take screenshot |

### ORIC Special Keys

| PC Key | ORIC Key |
|--------|----------|
| Escape | ESC |
| Backspace | DEL |
| Left Ctrl | CTRL |
| Left Shift / Right Shift | Shift |

## Video

The ORIC-1 has two display modes:
- **TEXT mode**: 40 columns x 28 rows, with 8 colors (ink/paper attributes)
- **HIRES mode**: 240 x 200 pixels, 6 colors with serial attributes

The emulator renders at native resolution and can be toggled to fullscreen with F11.

### Screenshots

```bash
# Screenshot at exit
oric1-emu -r basic10.rom --screenshot output.bmp

# Screenshot after N cycles
oric1-emu -r basic10.rom --screenshot-at 1000000:output.ppm
```

Supported formats: PPM (P6 binary) and BMP (24-bit uncompressed).

## Audio

The ORIC-1 uses a General Instrument AY-3-8910 Programmable Sound Generator (PSG) with:
- 3 independent tone channels (12-bit period)
- 1 noise generator (5-bit period, 17-bit LFSR)
- 16 envelope shapes (attack, decay, hold, alternate)
- Mixer control (tone/noise enable per channel)

Audio output is through SDL2 at 44100 Hz stereo. The PSG runs at 1 MHz, matching the original hardware.

### BASIC Sound Commands

```basic
REM Play a tone on channel A
SOUND 1,100,15

REM Play with envelope
PLAY 0,0,0,0
```

## Headless Mode

For automated testing or scripting, run without display:

```bash
# Run for 1 million cycles
oric1-emu -r basic10.rom --headless --cycles 1000000

# With simulated keyboard input
oric1-emu -r basic10.rom --headless --cycles 2000000 \
  --type-keys "500000:PRINT \"HELLO\"\n"
```

## Conversion Tools

### bas2tap - BASIC to Tape

Convert a BASIC text file to .TAP format:

```bash
bas2tap program.bas -o program.tap
```

The BASIC file should contain numbered lines:
```basic
10 PRINT "HELLO WORLD"
20 GOTO 10
```

### bin2tap - Binary to Tape

Convert a machine code binary to .TAP:

```bash
bin2tap program.bin --start 0x0400 --exec 0x0400 -o program.tap
```

### tap2sedoric - Tape to Disk

Convert a .TAP file to Sedoric disk format:

```bash
tap2sedoric program.tap -o disk.dsk
```

## Troubleshooting

**No sound**: Make sure you built with `SDL2=1` and that SDL2 audio is initialized. Check that your system audio is not muted.

**Keyboard not working after Sedoric boot**: This was a known bug fixed in v1.0.0-beta.8. Ensure you are using the latest version.

**Program won't load with CLOAD**: Make sure the .TAP file is valid. Try fast load mode (`-f`) as an alternative.

**Blank screen**: Verify the ROM file is correct (basic10.rom, 16384 bytes). The emulator needs a valid ORIC-1 ROM to boot.
