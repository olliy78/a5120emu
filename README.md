# Robotron A5120 Emulator

A cycle-accurate emulator for the **Robotron A5120** — an East German Z80-based office computer from 1984, running the **CPA** operating system (a CP/M 2.2 derivative developed by the Akademie der Wissenschaften der DDR).

---

## Features

- Full **Z80 CPU** emulation: all documented and undocumented opcodes, all interrupt modes (IM 0/1/2), accurate flag behavior
- **80×24 text terminal**: ANSI console mode (any terminal emulator) and SDL2 pixel-accurate mode (green-phosphor look)
- **5.25" DD DS floppy** emulation: 800 KB images (80 tracks, 2 sides, 5×1024-byte physical sectors → 40×128-byte logical CP/M sectors)
- **CPA/CP/M BIOS**: original Z80 BIOS code from the real hardware, trap-intercepted by the emulator for I/O
- **Directory mount**: any host folder can be mounted as a virtual CP/M drive A:
- **Batch/auto-exec mode**: inject a command string at boot, capture console output to a file
- **cparun**: standalone CP/M command-line runner — executes `.com` programs (e.g. M80, LINKMT) directly on the host without disk images, usable independently of the full emulator

---

## Screenshot

```
A>dir
A: @OS     COM : LINKMT  COM : M80     COM
A: BIOS    MAC : BDOS    ERL : CCP     ERL
A>m80 =bios
M80 Ver 4.0
End of assembly, 0 error(s)
A>
```

*(ANSI terminal mode)*

---

## Hardware Background

| Component | Original Hardware | Emulation |
|---|---|---|
| CPU | U880D (Z80 clone) @ 2.5 MHz | Full Z80 including undocumented opcodes |
| RAM | 64 KB | 64 KB flat address space |
| Display | K7027 CRT card, 80×24 characters | ANSI Escape + SDL2 pixel graphics |
| Keyboard | Matrix keyboard via PIO | PC keyboard → CPA key codes |
| FDC | K2526 card with K5122 FDC, PIO-driven | PIO register emulation + image I/O |
| OS | CPA (CP/M 2.2, K1520 bus) | Original BIOS code, BDOS+CCP via @os.com |

The A5120 was produced by VEB Robotron Dresden and used in research institutions and universities across the DDR. It ran the **CPA** OS (*CP/M für Kleincomputer des Akademie-Netzes*), which is largely compatible with standard CP/M 2.2.

---

## Requirements

- **C++17** compiler (GCC 8+, Clang 7+)
- **CMake** 3.16 or newer
- Linux, macOS, or WSL
- *(optional)* **SDL2** for the graphical terminal mode

---

## Build

```sh
cmake -B build
cmake --build build
```

To enable the SDL2 graphical window:

```sh
cmake -B build -DUSE_SDL=ON
cmake --build build
```

To run the tests:

```sh
cd build && ctest --output-on-failure
```

---

## Usage

### Full Emulator

```sh
# Boot from @os.com with files from boot_disk/
./build/a5120emu -os boot_disk/@os.com -dir boot_disk

# Boot from a disk image
./build/a5120emu -a disk_a.img -boot 0

# Two disk drives + graphical window
./build/a5120emu -os boot_disk/@os.com -a disk_a.img -b disk_b.img -sdl

# Execute a single command and capture output (batch mode)
./build/a5120emu -os boot_disk/@os.com -dir boot_disk -exec "dir" -cout out.txt
```

#### Options

| Option | Description |
|---|---|
| `-os <file>` | Boot from `@os.com` loader file (default: `boot_disk/@os.com`) |
| `-a <image>` | Mount 800 KB disk image on drive A: |
| `-b <image>` | Mount 800 KB disk image on drive B: |
| `-dir <path>` | Mount host directory as virtual CP/M drive A: |
| `-boot <0\|1>` | Boot from disk image (0 = A:, 1 = B:) |
| `-exec <cmd>` | Inject command at boot prompt and exit when done |
| `-log <file>` | Write BIOS diagnostic trace to file |
| `-cout <file>` | Capture console output to file |
| `-sdl` | Use SDL2 graphical window |
| `-h`, `--help` | Show help |

### CP/M Runner (cparun)

`cparun` runs CP/M `.com` programs directly on the host without disk images — useful for integrating CP/M build tools (M80, LINKMT, etc.) into Linux Makefiles or CMake scripts.

```sh
# Assemble with M80
./build/cparun -dir cpa_src m80 bios.erl=bios

# Link with LINKMT
./build/cparun -dir cpa_src linkmt "@OS=cpabas,ccp,bdos,bios/p:0BB80"
```

See [cparun/README.md](cparun/README.md) for the standalone build and full documentation.

---

## Architecture

### Boot Process

1. `@os.com` (the CPA loader) is loaded at `0x0100`
2. The loader header contains relocation parameters
3. CCP + BDOS + BIOS code is relocated to its final address (`0xBA00`)
4. BIOS jump-table entries are replaced with `HALT` trap opcodes
5. Page zero (`0x0000`, `0x0005`) is configured
6. The Z80 starts executing the CCP at `0xBA00`

### Memory Map

```
0x0000–0x0002   Warm boot vector  (JP BIOS+3)
0x0003          IOBYTE
0x0004          Current disk
0x0005–0x0007   BDOS entry vector (JP BDOS+6)
0x0100–0xB9FF   TPA – Transient Program Area (~46 KB)
0xBA00–0xC1FF   CCP – Console Command Processor
0xC200–0xCFFF   BDOS – Basic Disk Operating System
0xD000–0xF7FF   BIOS – emulated via HALT traps
0xF800–0xFFFF   Screen buffer (80×24 = 1920 bytes)
```

### BIOS Trap Mechanism

CP/M applications call the BIOS via the jump table at `0xD000`. The emulator replaces each jump-table target with a `HALT` opcode. When the Z80 executes a `HALT`, the emulator's run loop identifies the trap address and dispatches the corresponding C++ function (console I/O, disk read/write, etc.).

### Disk Format

Standard A5120 800 KB DS DD format:

| Parameter | Value |
|---|---|
| Tracks | 80 (double-sided → 160 logical) |
| Physical sectors/track | 5 × 1024 bytes |
| Logical sectors/track | 40 × 128 bytes (CP/M) |
| Block size | 2 KB (BSH = 4) |
| Total blocks | 400 (DSM = 399) |
| Directory entries | 192 (DRM = 191) |
| System tracks | 2 |

### Key Mapping

| PC Key | CPA Code |
|---|---|
| Arrow Up | `0x1A` |
| Arrow Down | `0x18` |
| Arrow Left | `0x08` (Backspace) |
| Arrow Right | `0x15` |
| Home / Pos1 | `0x01` |
| Delete | `0x7F` |
| Escape | `0x1B` |
| Ctrl+C | `0x03` |

---

## Project Structure

```
a5120emu/
├── src/
│   ├── a5120emu.cpp        Main program – argument parsing, component wiring, boot
│   ├── z80.h / z80.cpp     Z80 CPU emulator (all opcodes, IM0/1/2, HALT trap)
│   ├── memory.h / memory.cpp   64 KB address space, screen buffer helpers
│   ├── cpa_bios.h / cpa_bios.cpp  CPA BIOS + CRT + FDC emulation
│   ├── floppy.h / floppy.cpp      800 KB disk image I/O, directory mount
│   ├── terminal.h          Abstract terminal interface
│   ├── terminal_ansi.cpp   ANSI/VT100 console implementation
│   ├── terminal_sdl.cpp    SDL2 pixel-graphics implementation
│   ├── cparun.cpp          CP/M runner main (standalone tool)
│   ├── cpm_bdos.h / cpm_bdos.cpp  CP/M 2.2 BDOS emulation for cparun
│   └── font8x8.h           8×8 bitmap font for SDL mode
├── cparun/                 Standalone cparun sub-project (own CMakeLists.txt)
├── boot_disk/              @os.com and default CP/M files
├── cpa_src/                Original CPA BIOS source files (.mac, .erl)
├── tests/                  Unit tests (Z80 opcodes, memory, floppy)
├── docs/                   Additional documentation
└── CMakeLists.txt
```

---

## License

MIT License — see individual source files for the full license text.

Copyright © Olaf Krieger
