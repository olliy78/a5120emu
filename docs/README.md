# Robotron A5120 Emulator

A C++ emulator for the Robotron A5120 computer, running the CPA operating system (CP/M 2.2 derivative).

## Hardware Emulation

| Component | Details |
|-----------|---------|
| CPU | Z80 (U880) @ 2.5 MHz, full instruction set |
| RAM | 64 KB |
| Display | 80×24 text mode, screen buffer at 0xF800 |
| Keyboard | PC keyboard mapped to CPA key codes |
| Floppy | 2× 800 KB (80 tracks, 2 sides, 5×1024-byte sectors) |
| OS | CPA (CP/M 2.2, Akademie der Wissenschaften) |

## Building

```bash
mkdir build && cd build
cmake ..
make -j$(nproc)
```

Optional SDL2 graphical window:
```bash
cmake -DUSE_SDL=ON ..
make -j$(nproc)
```

## Usage

```bash
# Boot from @os.com with files from boot_disk/
./a5120emu -os boot_disk/@os.com -dir boot_disk

# Boot from disk image
./a5120emu -a disk_a.img -boot 0

# Mount two disk images
./a5120emu -os boot_disk/@os.com -a disk_a.img -b disk_b.img

# SDL graphical window (requires -DUSE_SDL=ON build)
./a5120emu -os boot_disk/@os.com -dir boot_disk -sdl
```

### Command Line Options

| Option | Description |
|--------|-------------|
| `-os <file>` | Boot from @os.com file |
| `-a <image>` | Mount 800 KB disk image on drive A: |
| `-b <image>` | Mount 800 KB disk image on drive B: |
| `-dir <path>` | Mount host directory as virtual disk on A: |
| `-boot <0\|1>` | Boot from disk image (0=A:, 1=B:) |
| `-sdl` | Use SDL2 graphical window |
| `-h` | Show help |

## Architecture

### Emulation Approach

The emulator uses a **BIOS trap** mechanism:
- CCP and BDOS from `@os.com` run natively on the Z80 emulator
- BIOS calls are intercepted via HALT instructions placed at BIOS entry points
- The host provides I/O through Terminal (ANSI or SDL) and FloppyDisk classes

### Boot Process

1. `@os.com` is loaded at address 0x0100
2. The emulator reads relocation parameters from the loader header
3. CCP+BDOS+BIOS code is copied from 0x0180 to its final address (0xBA00)
4. BIOS jump table entries are replaced with HALT traps
5. Page zero (jump vectors at 0x0000 and 0x0005) is configured
6. CPU starts executing CCP at 0xBA00

### Memory Map

```
0x0000-0x0002  Warm boot vector (JP BIOS+3)
0x0003         IOBYTE
0x0004         Current disk
0x0005-0x0007  BDOS entry vector (JP BDOS+6)
0x0100-0xB9FF  TPA (Transient Program Area)
0xBA00-0xC1FF  CCP (Console Command Processor)
0xC200-0xCFFF  BDOS (Basic Disk Operating System)
0xD000-0xF7FF  BIOS (emulated via traps)
0xF800-0xFFFF  Screen buffer (80×24 = 1920 bytes)
```

### Disk Format

800 KB floppy disk format:
- 80 tracks, double-sided
- 5 physical sectors per track (1024 bytes each)
- 40 logical sectors per track (128 bytes, CP/M standard)
- Block size: 2 KB (BSH=4)
- 195 data blocks (DSM=194)
- 128 directory entries (DRM=127)
- 2 system tracks reserved

### Key Mapping

| PC Key | CPA Function |
|--------|-------------|
| Arrow Up | Cursor up (0x1A) |
| Arrow Down | Cursor down (0x18) |
| Arrow Left | Backspace (0x08) |
| Arrow Right | Cursor right (0x15) |
| Ctrl+C | Break |
| Escape | ESC (escape sequences) |

### Screen Control Codes

| Code | Function |
|------|----------|
| 0x01 | Cursor home |
| 0x02 | Cursor on |
| 0x03 | Cursor off |
| 0x07 | Bell |
| 0x08 | Backspace |
| 0x0A | Line feed |
| 0x0C | Clear screen |
| 0x0D | Carriage return |
| 0x14 | Clear to end of screen |
| 0x16 | Clear to end of line |
| 0x17 | Insert line |
| 0x19 | Delete line |
| 0x1B | ESC (ADM3A cursor positioning) |

## Source Files

| File | Description |
|------|-------------|
| `src/z80.h/cpp` | Z80 CPU emulation (full instruction set) |
| `src/memory.h/cpp` | 64 KB RAM with screen buffer |
| `src/terminal.h` | Abstract terminal interface |
| `src/terminal_ansi.cpp` | ANSI console terminal |
| `src/terminal_sdl.cpp` | SDL2 graphical terminal |
| `src/font8x8.h` | 8×8 bitmap font for SDL mode |
| `src/floppy.h/cpp` | Disk image handler |
| `src/cpa_bios.h/cpp` | CPA BIOS emulation (trap-based) |
| `src/a5120emu.cpp` | Main program |
| `tests/test_main.cpp` | Unit tests (58 tests) |

## Tests

```bash
cd build
./a5120emu_test
```

Tests cover: Z80 instructions (basic, extended, CB prefix, IX/IY), memory operations, floppy disk I/O, program execution integration, and CP/M structure compatibility.

## CPA Source Reference

The `cpa_src/` directory contains the original CPA BIOS source files in Z80 assembly (.mac format). Key files:
- `bios.mac` - Main BIOS include file
- `biosnuc.mac` - BIOS nucleus (jump table, cold/warm boot)
- `bioscrt.mac` - CRT display driver
- `bioskbd.mac` - Keyboard driver
- `biosdsk.mac` - Disk I/O
- `biosmem.mac` - Memory management
