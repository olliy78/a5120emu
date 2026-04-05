/**
 * @file cparun.cpp
 * @brief CP/M-Kommandozeilen-Runner fuer CPA/CP/M .com-Programme.
 *
 * Fuehrt CP/M .com-Programme direkt auf dem Host-Dateisystem aus,
 * ohne Disketten-Image-Emulation. Gedacht fuer die Integration in
 * Build-Umgebungen (z.B. m80, linkmt) auf Linux- oder Windows-Hosts.
 *
 * Alle Dateioperationen werden direkt auf ein Arbeitsverzeichnis
 * abgebildet. Konsolenausgaben gehen auf stdout, Eingaben von stdin.
 *
 * Verwendung:
 *   cparun [optionen] <programm> [argumente...]
 *
 * Optionen:
 *   -dir <pfad>   Arbeitsverzeichnis (Standard: aktuelles Verzeichnis)
 *   -h, --help    Hilfe anzeigen
 *
 * Beispiele:
 *   cparun -dir cpa_src m80 bios.erl=bios
 *   cparun -dir cpa_src linkmt "@OS=cpabas,ccp,bdos,bios/p:0BB80"
 *
 * @author Olaf Krieger
 * @license MIT License
 */
#include "z80.h"
#include "memory.h"
#include "cpm_bdos.h"

#include <cstdio>
#include <cstring>
#include <string>
#include <csignal>

static volatile bool g_running = true;

static void signalHandler(int /*sig*/) {
    g_running = false;
}

static void printUsage(const char* prog) {
    printf("CP/M Command-Line Runner\n");
    printf("Executes CP/M .com programs using the host filesystem.\n\n");
    printf("Usage: %s [options] <command> [args...]\n\n", prog);
    printf("Options:\n");
    printf("  -dir <path>   Working directory (default: current directory)\n");
    printf("  -h, --help    Show this help\n");
    printf("\nExamples:\n");
    printf("  %s -dir cpa_src m80 bios.erl=bios\n", prog);
    printf("  %s -dir cpa_src linkmt \"@OS=cpabas,ccp,bdos,bios/p:0BB80\"\n", prog);
}

int main(int argc, char* argv[]) {
    std::string workDir = ".";
    int cmdStart = -1;

    // Parse options
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-dir") == 0 && i + 1 < argc) {
            workDir = argv[++i];
        } else if (strcmp(argv[i], "-h") == 0 || strcmp(argv[i], "--help") == 0) {
            printUsage(argv[0]);
            return 0;
        } else {
            cmdStart = i;
            break;
        }
    }

    if (cmdStart < 0) {
        fprintf(stderr, "Error: No command specified.\n\n");
        printUsage(argv[0]);
        return 1;
    }

    // Build argument string from remaining args
    std::string command = argv[cmdStart];
    std::string args;
    for (int i = cmdStart + 1; i < argc; i++) {
        if (!args.empty()) args += ' ';
        args += argv[i];
    }

    // Set up signal handlers
    signal(SIGINT, signalHandler);
    signal(SIGTERM, signalHandler);

    // Create emulator components
    Z80 cpu;
    Memory mem;
    CpmBdos bdos(cpu, mem, workDir);

    // Set up page zero and traps
    bdos.setup();

    // Load .com file
    if (!bdos.loadCom(command)) {
        fprintf(stderr, "cparun: cannot find %s.com in %s\n",
                command.c_str(), workDir.c_str());
        return 1;
    }

    // Parse command line into FCBs and command tail
    bdos.parseCommandLine(args);

    // Connect CPU to memory
    cpu.readByte = [&mem](uint16_t addr) -> uint8_t { return mem.read(addr); };
    cpu.writeByte = [&mem](uint16_t addr, uint8_t val) { mem.write(addr, val); };

    // No hardware I/O - all I/O goes through BDOS traps
    cpu.readPort = [](uint16_t) -> uint8_t { return 0xFF; };
    cpu.writePort = [](uint16_t, uint8_t) {};

    // Run program
    return bdos.run();
}
