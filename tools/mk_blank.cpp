// Legt eine leere (formatierte) Diskette an — Scratch-Werkzeug für Formatier-Tests.
// Aufruf: mk_blank <pfad.hfe> <format_name>   (z.B. k5601_16x256 = DD-DS 16x256)
#include "core/machines/a5120/a5120.h"
#include <cstdio>
int main(int argc, char** argv) {
    if (argc < 3) { fprintf(stderr, "usage: %s <path> <format>\n", argv[0]); return 2; }
    A5120Machine m;
    m.powerOn();
    bool ok = m.createDisk(1, argv[1], argv[2], false);   // Slot 1 = B:
    if (!ok) { fprintf(stderr, "createDisk failed: %s\n", m.lastError().c_str()); return 1; }
    printf("OK: %s (%s)\n", argv[1], argv[2]);
    return 0;
}
