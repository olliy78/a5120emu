// bench_run — Fixed-workload Performance-Benchmark für den A5120-Core.
//
// Bootet die Maschine bis zum interaktiven Prompt (oder bis zu einem Cutoff)
// und misst danach die reine Emulationsrate von A5120Machine::run() über eine
// feste Zahl emulierter Takte im laufenden Zustand — genau der Pfad, den die
// GUI im "N× Echtzeit"-Betrieb ausführt.  Gibt Mcycles/s und den Faktor gegen
// 2,5 MHz (A5120-Takt) aus.  Kein Logging, keine Trace-Callbacks.
#include "core/machines/a5120/a5120.h"
#include "core/logger.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <chrono>

int main(int argc, char** argv) {
    const char* disk = "disks/cpadisk_autofs_clock_noautoexec.hfe";
    long long bench_cycles = 300000000LL;   // gemessener Workload (~120 s @2,5 MHz)
    long long boot_budget  = 20000000LL;     // Takte bis zum Prompt (großzügig)
    for (int i = 1; i < argc; ++i) {
        if (!strcmp(argv[i], "-c") && i + 1 < argc) bench_cycles = atoll(argv[++i]);
        else if (!strcmp(argv[i], "-d") && i + 1 < argc) disk = argv[++i];
        else if (!strcmp(argv[i], "--boot") && i + 1 < argc) boot_budget = atoll(argv[++i]);
    }

    // Logging aus dem Messfenster halten (Floppy-INFO-Zeilen verfälschen die Rate).
    k1520::logging::Logger::instance().setBaseLevel(k1520::logging::Level::ERROR);

    A5120Machine machine;
    machine.powerOn();
    if (!machine.mountDisk(0, disk, "cpa780", false) &&
        !machine.mountDisk(0, disk, "cpa800", false)) {
        fprintf(stderr, "mountDisk fehlgeschlagen: %s\n", machine.lastError().c_str());
        return 1;
    }

    // Warmlauf: bis zum Prompt boote (Boot-Zeit selbst ist nicht Teil der Messung).
    long long booted = 0;
    while (booted < boot_budget) booted += machine.run(200000);

    // Messung: feste Taktzahl im laufenden Zustand.
    auto t0 = std::chrono::steady_clock::now();
    long long done = 0;
    while (done < bench_cycles) done += machine.run(200000);
    auto t1 = std::chrono::steady_clock::now();

    double dt = std::chrono::duration<double>(t1 - t0).count();
    double mcps = (done / 1e6) / dt;
    printf("bench: %lld Takte in %.3f s  ->  %.2f Mcycles/s  (%.1fx Echtzeit @2,5MHz)\n",
           done, dt, mcps, mcps / 2.5);
    return 0;
}
