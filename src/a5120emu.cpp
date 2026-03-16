// a5120emu.cpp - Robotron A5120 Emulator Main Program
// Emulates a Robotron A5120 (Z80 + 64KB RAM + 80x24 text CRT + 2x floppy)
// running CPA (CP/M 2.2 derivative)
//
// Usage:
//   a5120emu [options]
//
// Options:
//   -os <file>        Boot from @os.com file (default: boot_disk/@os.com)
//   -a <image>        Mount disk image on drive A:
//   -b <image>        Mount disk image on drive B:
//   -dir <path>       Mount directory as drive A: (creates virtual disk)
//   -boot <drive>     Boot from disk image (0=A, 1=B)
//   -sdl              Use SDL2 graphical window (default: ANSI terminal)
//   -h, --help        Show help
//
#include "z80.h"
#include "memory.h"
#include "terminal.h"
#include "cpa_bios.h"
#include "floppy.h"

#include <cstdio>
#include <cstring>
#include <string>
#include <csignal>

static volatile bool g_running = true;

static void signalHandler(int /*sig*/) {
    g_running = false;
}

static void printUsage(const char* prog) {
    printf("Robotron A5120 Emulator\n");
    printf("Usage: %s [options]\n\n", prog);
    printf("Options:\n");
    printf("  -os <file>    Boot from @os.com file\n");
    printf("  -a <image>    Mount disk image on drive A:\n");
    printf("  -b <image>    Mount disk image on drive B:\n");
    printf("  -dir <path>   Mount directory as virtual disk on A:\n");
    printf("  -boot <0|1>   Boot from disk image (0=A:, 1=B:)\n");
#ifdef USE_SDL
    printf("  -sdl          Use SDL2 graphical window\n");
#endif
    printf("  -h, --help    Show this help\n");
    printf("\nExamples:\n");
    printf("  %s -os boot_disk/@os.com -dir boot_disk\n", prog);
    printf("  %s -a disk_a.img -boot 0\n", prog);
}

int main(int argc, char* argv[]) {
    std::string osFile;
    std::string diskA, diskB;
    std::string dirMount;
    bool useSDL = false;
    int bootDrive = -1;

    // Parse command line arguments
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-os") == 0 && i + 1 < argc) {
            osFile = argv[++i];
        } else if (strcmp(argv[i], "-a") == 0 && i + 1 < argc) {
            diskA = argv[++i];
        } else if (strcmp(argv[i], "-b") == 0 && i + 1 < argc) {
            diskB = argv[++i];
        } else if (strcmp(argv[i], "-dir") == 0 && i + 1 < argc) {
            dirMount = argv[++i];
        } else if (strcmp(argv[i], "-boot") == 0 && i + 1 < argc) {
            bootDrive = atoi(argv[++i]);
        } else if (strcmp(argv[i], "-sdl") == 0) {
            useSDL = true;
        } else if (strcmp(argv[i], "-h") == 0 || strcmp(argv[i], "--help") == 0) {
            printUsage(argv[0]);
            return 0;
        } else {
            fprintf(stderr, "Unknown option: %s\n", argv[i]);
            printUsage(argv[0]);
            return 1;
        }
    }

    // Default: boot from @os.com if no other boot source specified
    if (osFile.empty() && bootDrive < 0 && dirMount.empty()) {
        osFile = "boot_disk/@os.com";
    }

    // Set up signal handler
    signal(SIGINT, signalHandler);
    signal(SIGTERM, signalHandler);

    // Create components
    Z80 cpu;
    Memory mem;

    // Create terminal
    Terminal* terminal = nullptr;
#ifdef USE_SDL
    if (useSDL) {
        terminal = new SDLTerminal();
    } else {
        terminal = new AnsiTerminal();
    }
#else
    if (useSDL) {
        fprintf(stderr, "Error: SDL2 support was not compiled in.\n");
        fprintf(stderr, "Rebuild with: cmake -DUSE_SDL=ON ..\n");
        return 1;
    }
    terminal = new AnsiTerminal();
#endif

    terminal->init();

    // Create BIOS
    CpaBios bios(cpu, mem, *terminal);

    // Connect CPU to memory
    cpu.readByte = [&mem](uint16_t addr) -> uint8_t { return mem.read(addr); };
    cpu.writeByte = [&mem](uint16_t addr, uint8_t val) { mem.write(addr, val); };

    // I/O port handlers - emulate A5120 hardware ports
    cpu.readPort = [&bios, &terminal](uint16_t port) -> uint8_t {
        uint8_t p = port & 0xFF;
        switch (p) {
            case 0x0A: // System PIO - report BAB2 (24x80 screen)
                return 0x40; // Bit 6 = screen buffer enabled
            case 0xFE: // Keyboard status
                return terminal->keyAvailable() ? 0x08 : 0x00; // Bit 3 = key pressed
            case 0xFF: // Keyboard data
                return terminal->keyAvailable() ? terminal->getKey() : 0x00;
            case 0x0C: case 0x0D: case 0x0E: case 0x0F:
                return 0; // CTC channels - not active
            case 0x40:
                return 0; // Video controller
            default:
                return 0xFF; // Unconnected port
        }
    };

    cpu.writePort = [](uint16_t port, uint8_t val) {
        // Most output ports are handled silently
        uint8_t p = port & 0xFF;
        switch (p) {
            case 0x0A: // System PIO
            case 0x02: // Protection reset
            case 0x09: // PIO control
            case 0x0C: case 0x0D: case 0x0E: case 0x0F: // CTC
            case 0x11: case 0x13: // More CTC/PIO
            case 0x18: // Extended
            case 0x40: // Video controller
                break;
            default:
                break;
        }
    };

    // Mount disks
    if (!diskA.empty()) {
        if (!bios.mountDisk(0, diskA)) {
            fprintf(stderr, "Warning: Cannot mount disk A: from %s\n", diskA.c_str());
        }
    }
    if (!diskB.empty()) {
        if (!bios.mountDisk(1, diskB)) {
            fprintf(stderr, "Warning: Cannot mount disk B: from %s\n", diskB.c_str());
        }
    }
    if (!dirMount.empty()) {
        if (!bios.mountDirectory(0, dirMount)) {
            fprintf(stderr, "Warning: Cannot mount directory %s as A:\n", dirMount.c_str());
        }
    }

    // Boot
    bool booted = false;
    if (bootDrive >= 0) {
        booted = bios.bootFromDisk(bootDrive);
        if (!booted) {
            fprintf(stderr, "Error: Cannot boot from drive %c:\n", 'A' + bootDrive);
        }
    }
    if (!booted && !osFile.empty()) {
        booted = bios.bootFromFile(osFile);
        if (!booted) {
            fprintf(stderr, "Error: Cannot boot from %s\n", osFile.c_str());
        }
    }

    if (!booted) {
        fprintf(stderr, "Error: No boot source available\n");
        terminal->shutdown();
        delete terminal;
        return 1;
    }

    // Run emulator
    bios.run();

    // Cleanup
    terminal->shutdown();
    delete terminal;

    return 0;
}
