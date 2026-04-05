# cparun

**cparun** is a lightweight CP/M command-line runner that executes CP/M `.com`
programs directly on a Linux (or other POSIX) host. It provides a CP/M 2.2
BDOS emulation layer that maps all file operations to the host filesystem,
so no floppy disk images are required.

It was originally developed as part of the
[a5120emu](https://github.com/olliy/a5120emu) project to drive build tools
such as the M80 macro assembler and the LINKMT linker from within Makefiles
and CMake scripts on Linux.

---

## Features

- Full Z80 CPU emulation (documented and undocumented opcodes)
- CP/M 2.2 BDOS subset sufficient to run typical transient programs:
  - Console I/O (functions 1, 2, 6, 9, 10, 11)
  - File I/O: open, close, make, delete, rename, search first/next (functions 15–23)
  - Sequential and random record access (functions 20, 21, 33, 34, 35, 36, 40)
  - Disk management stubs (functions 12–14, 24–32, 37)
- Case-insensitive file lookup on the host filesystem
- FCB parsing with drive prefix support (`A:`, `B:`, …)
- Command tail and dual FCB setup (at `0x005C` / `0x006C`) as per CP/M convention
- TPA of ~60 KB (`0x0100`–`0xEFFF`)
- Clean exit via `JP 0000H` or BDOS function 0

---

## Requirements

- C++17 compiler (GCC 8+, Clang 7+, MSVC 2019+)
- CMake 3.16 or newer
- Linux, macOS, Windows (MSVC or MinGW/MSYS2), or WSL

---

## Build

### Linux / macOS

```sh
cd cparun
cmake -B build
cmake --build build
```

The resulting binary is `build/cparun`.

To install system-wide:

```sh
cmake --install build --prefix /usr/local
```

### Windows — Visual Studio (MSVC)

Open a **Developer Command Prompt for VS** (or a terminal with the MSVC
environment loaded), then:

```bat
cd cparun
cmake -B build -G "Visual Studio 17 2022"
cmake --build build --config Release
```

The binary is created at `build\Release\cparun.exe`.

### Windows — MinGW / MSYS2

In an MSYS2 MINGW64 shell:

```sh
cd cparun
cmake -B build -G "MinGW Makefiles"
cmake --build build
```

The binary is `build/cparun.exe`.

---

## Usage

```
cparun [options] <command> [args...]
```

### Options

| Option | Description |
|---|---|
| `-dir <path>` | Working directory where `.com` files and data files are located (default: current directory) |
| `-h`, `--help` | Print help and exit |

### Arguments

| Argument | Description |
|---|---|
| `<command>` | Name of the CP/M program to run, with or without the `.com` extension |
| `[args...]` | Arguments passed to the CP/M program via the command tail and FCBs |

### Examples

Run the M80 macro assembler on a source file in `cpa_src/`:

```sh
cparun -dir cpa_src m80 bios.erl=bios
```

Link object files with LINKMT:

```sh
cparun -dir cpa_src linkmt "@OS=cpabas,ccp,bdos,bios/p:0BB80"
```

Run a program in the current directory:

```sh
cparun myprog arg1 arg2
```

---

## How It Works

1. The Z80 CPU is initialized and page zero is set up with CP/M-compatible
   jump vectors (`0x0000` warm boot, `0x0005` BDOS entry).
2. The `.com` file is loaded at `0x0100` from the working directory
   (case-insensitive search).
3. The argument string is parsed into FCB1 (`0x005C`), FCB2 (`0x006C`),
   and the command tail (`0x0080`), following CP/M 2.2 conventions.
4. The CPU runs until the program terminates via BDOS function 0 or
   `JP 0000H` (warm boot).
5. All BDOS calls (`CALL 0005H`) are intercepted via a HALT trap and
   dispatched in C++. File operations are transparently redirected to
   the host filesystem.

---

## License

MIT — see [LICENSE.txt](LICENSE.txt).
