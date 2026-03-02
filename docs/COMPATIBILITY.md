# Compatibility List

Programs tested with Phosphoric v1.0.0-rc.

## Operating Systems

| Program | Format | Status | Notes |
|---------|--------|--------|-------|
| Sedoric V4.0 | .DSK | Working | Boots to prompt, keyboard functional |
| BASIC 1.0 ROM | .ROM | Working | Full BASIC interpreter, all commands |

## Games

| Program | Format | Status | Notes |
|---------|--------|--------|-------|
| Poker (poker-asn.tap) | .TAP | Working | HIRES graphics render correctly |
| Explode | .TAP | Working | CLOAD loading, gameplay functional |

## Demos / Test Programs

| Program | Format | Status | Notes |
|---------|--------|--------|-------|
| Hello World | .TAP | Working | Text mode, PRINT output |
| Sound demo | .TAP | Working | PSG tone/envelope generation |
| HIRES graphics demo | .TAP | Working | 240x200 rendering |

## Subsystem Test Results

| Subsystem | Tests | Status |
|-----------|-------|--------|
| CPU 6502 | 74/74 | All pass |
| Memory | 19/19 | All pass |
| VIA 6522 I/O | 24/24 | All pass |
| Storage (Sedoric/FDC) | 12/12 | All pass |
| System Integration | 7/7 | All pass |
| Video Export | 11/11 | All pass |
| Audio PSG | 8/8 | All pass |
| **Total** | **155/155** | **100%** |

## ROM Compatibility

| ROM | Size | Reset Vector | Status |
|-----|------|-------------|--------|
| basic10.rom (ORIC-1 BASIC 1.0) | 16384 bytes | $F42D | Validated |
| microdis.rom (Microdisc) | varies | N/A | Working (overlay at $E000) |

## Known Limitations

- Save states not yet implemented
- Debugger not yet implemented
- Joystick not supported
- Printer emulation not available
- ORIC Atmos mode not supported (planned post-1.0)
- Some copy-protected programs may not load correctly

## Hardware Emulation Accuracy

| Component | Accuracy | Notes |
|-----------|----------|-------|
| 6502 CPU | Cycle-accurate | All 151 official opcodes, BCD, JMP bug |
| VIA 6522 | Functional | Timers, interrupts, port callbacks |
| AY-3-8910 PSG | Accurate | Oricutron-compatible DAC curve, clock dividers |
| ULA Video | Functional | Text + HIRES modes, serial attributes |
| WD1793 FDC | Functional | Type I/II commands, sector R/W |
| Keyboard | Accurate | 8x8 matrix scan via VIA + PSG Port A |

## Reporting Compatibility

If you test a program not listed here, please report:
- Program name and format (.TAP/.DSK)
- Whether it loads and runs correctly
- Any visual/audio glitches observed
- Steps to reproduce any issues
