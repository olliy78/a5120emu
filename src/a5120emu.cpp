/**
 * @file a5120emu.cpp
 * @brief Hauptprogramm des Robotron A5120 Emulators.
 *
 * Emuliert einen Robotron A5120 Burocomputer (DDR, 1984):
 * - Z80 CPU (2,5 MHz), 64 KB RAM
 * - 80×24 Textbildschirm (Baugruppe K7027)
 * - Bis zu 2 Diskettenlaufwerke 5,25" DS DD (Baugruppe K2526/K5122)
 * - CPA-Betriebssystem (CP/M 2.2 Ableitung)
 *
 * Das Programm setzt die Emulationskomponenten zusammen, verarbeitet
 * die Kommandozeilenargumente, fuehrt den Boot-Vorgang durch und startet
 * die Emulationsschleife. Die Hauptschleife laeuft in CpaBios::run().
 *
 * Startmodi:
 * - `- os <datei>`: Bootet aus einer @os.com-Datei (BIOS + CCP geladen)
 * - `-boot <0|1>`:  Bootet vom Disketten-Image (Laufwerk A oder B)
 * - Standard:       Bootet automatisch aus boot_disk/@os.com
 *
 * @author Olaf Krieger
 * @license MIT License
 * @see https://opensource.org/licenses/MIT
 */
#include "z80.h"
#include "memory.h"
#include "terminal.h"
#include "cpa_bios.h"
#include "floppy.h"

#include <cstdio>
#include <cstring>
#include <string>
#include <csignal>

/** @brief Globales Lauf-Flag; wird durch signalHandler() auf false gesetzt. */
static volatile bool g_running = true;

/**
 * @brief Signal-Handler fuer SIGINT und SIGTERM.
 *
 * Setzt g_running auf false, damit die Emulationsschleife sauber beendet
 * werden kann (kein harter Abbruch via exit()).
 *
 * @param sig Signalnummer (wird nicht ausgewertet).
 */
static void signalHandler(int /*sig*/) {
    g_running = false;
}

/**
 * @brief Gibt die Benutzerhilfe mit allen Kommandozeilenoptionen auf stdout aus.
 * @param prog Programmname (argv[0]) fuer die Verwendungszeile.
 */
static void printUsage(const char* prog) {
    printf("Robotron A5120 Emulator\n");
    printf("Usage: %s [options]\n\n", prog);
    printf("Options:\n");
    printf("  -os <file>    Boot from @os.com file\n");
    printf("  -a <image>    Mount disk image on drive A:\n");
    printf("  -b <image>    Mount disk image on drive B:\n");
    printf("  -dir <path>   Mount directory as virtual disk on A:\n");
    printf("  -boot <0|1>   Boot from disk image (0=A:, 1=B:)\n");
    printf("  -exec <cmd>   Execute CP/M command after boot and exit\n");
    printf("  -log <file>   Write diagnostic trace to file\n");
    printf("  -cout <file>  Capture console output to file\n");
#ifdef USE_SDL
    printf("  -sdl          Use SDL2 graphical window\n");
#endif
    printf("  -h, --help    Show this help\n");
    printf("\nExamples:\n");
    printf("  %s -os boot_disk/@os.com -dir boot_disk\n", prog);
    printf("  %s -a disk_a.img -boot 0\n", prog);
    printf("  %s -os boot_disk/@os.com -dir boot_disk -exec \"dir\" -cout out.txt\n", prog);
}

/**
 * @brief Einstiegspunkt des Emulators.
 *
 * Verarbeitet Kommandozeilenargumente, erstellt alle Emulationskomponenten
 * (Z80, Memory, Terminal, CpaBios), haengt Disketten ein, fuehrt den
 * Bootvorgang durch und uebergibt die Kontrolle an CpaBios::run().
 *
 * @param argc Anzahl der Kommandozeilenargumente.
 * @param argv Zeigerliste auf die Argumentstrings.
 * @return 0 bei normalem Ende, 1 bei einem Fehler.
 */
int main(int argc, char* argv[]) {
    std::string osFile;
    std::string diskA, diskB;
    std::string dirMount;
    std::string execCmd;
    std::string logFile;
    std::string coutFile;
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
        } else if (strcmp(argv[i], "-exec") == 0 && i + 1 < argc) {
            execCmd = argv[++i];
        } else if (strcmp(argv[i], "-log") == 0 && i + 1 < argc) {
            logFile = argv[++i];
        } else if (strcmp(argv[i], "-cout") == 0 && i + 1 < argc) {
            coutFile = argv[++i];
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

    // Default: boot from @os.com if no other boot source specified.
    // Wenn eine Disk eingehängt aber kein expliziter Boot-Modus angegeben:
    // vom Laufwerk A: booten (bootFromDisk erkennt SYL-Disk und liest @OS.COM).
    if (osFile.empty() && bootDrive < 0 && dirMount.empty()) {
        if (!diskA.empty()) {
            bootDrive = 0;  // SYL-Disk-Boot von Laufwerk A:
        } else {
            osFile = "boot_disk/@os.com";
        }
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

    // Set batch mode before init if using exec
    if (!execCmd.empty()) {
        terminal->setBatchMode(true);
    }

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
            // FDC PIO ports
            case 0x10: case 0x11: case 0x12: case 0x13:
            case 0x14: case 0x15: case 0x16: case 0x17:
                return bios.fdcReadPort(p);
            default:
                return 0xFF; // Unconnected port
        }
    };

    cpu.writePort = [&bios](uint16_t port, uint8_t val) {
        // Most output ports are handled silently
        uint8_t p = port & 0xFF;
        switch (p) {
            case 0x0A: // System PIO
            case 0x02: // Protection reset
            case 0x09: // PIO control
            case 0x0C: case 0x0D: case 0x0E: case 0x0F: // CTC
                break;
            // FDC PIO ports
            case 0x10: case 0x11: case 0x12: case 0x13:
            case 0x14: case 0x15: case 0x16: case 0x17:
            case 0x18: // Drive select
                bios.fdcWritePort(p, val);
                break;
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

    // Set up logging
    FILE* diagFile = nullptr;
    FILE* consoleFile = nullptr;
    if (!logFile.empty()) {
        diagFile = fopen(logFile.c_str(), "w");
        if (diagFile) bios.setDiagLog(diagFile);
        else fprintf(stderr, "Warning: Cannot open log file %s\n", logFile.c_str());
    }
    if (!coutFile.empty()) {
        consoleFile = fopen(coutFile.c_str(), "w");
        if (consoleFile) bios.setConsoleLog(consoleFile);
        else fprintf(stderr, "Warning: Cannot open console output file %s\n", coutFile.c_str());
    }

    // Set up auto-exec
    if (!execCmd.empty()) {
        terminal->setBatchMode(true);
        // Process escape sequences in exec command: \r=CR, \n=LF, \t=TAB, \\=backslash
        std::string processed;
        for (size_t i = 0; i < execCmd.size(); i++) {
            if (execCmd[i] == '\\' && i + 1 < execCmd.size()) {
                switch (execCmd[i + 1]) {
                    case 'r': processed += '\r'; i++; break;
                    case 'n': processed += '\n'; i++; break;
                    case 't': processed += '\t'; i++; break;
                    case '\\': processed += '\\'; i++; break;
                    default: processed += execCmd[i]; break;
                }
            } else {
                processed += execCmd[i];
            }
        }
        // Append CR if not already ending with one
        if (processed.empty() || processed.back() != '\r') {
            processed += '\r';
        }
        terminal->injectKeys(processed);
        bios.setAutoExit(true);
        bios.setExecCommand(execCmd);
    }

    // Run emulator
    bios.run();

    // Cleanup
    if (diagFile) fclose(diagFile);
    if (consoleFile) fclose(consoleFile);
    terminal->shutdown();
    delete terminal;

    return 0;
}
