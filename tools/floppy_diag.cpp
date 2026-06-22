// Running-OS floppy read diagnostic.
// Boots a disk (optionally entering a clock time), traces every ZVE2 read via the
// 0x1F7D routine — logging the wanted (cyl,head,sector,size) and whether the IDAM
// is found (0x2038) or not (0x2034) — and reports the final ZVE1 PC + key vars.
#include "core/machines/a5120/a5120.h"
#include "core/logger.h"
#include <cstdio>

using k1520::logging::Logger;
using k1520::logging::Level;

int main(int argc, char** argv) {
    const char* disk = (argc >= 2) ? argv[1] : "disks/cpadisk_autofs_noclk_noautoexec.hfe";
    const char* tstr = (argc >= 3) ? argv[2] : nullptr;   // e.g. "12:00:00"
    Logger::instance().setBaseLevel(Level::ERROR);

    A5120Machine m;
    m.powerOn();
    if (!(m.mountDisk(0, disk, "cpa780", false) || m.mountDisk(0, disk, "cpa800", false))) {
        fprintf(stderr, "mount fail: %s\n", m.lastError().c_str()); return 1;
    }

    // Watch writes to the divide-loop divisor [0xD1BE..0xD1BF].
    m.setBusTrace([&](bool isIO, bool isRead, uint16_t addr, uint8_t data){
        if (isIO || isRead) return;
        if (addr == 0xD1B2 || addr == 0xD1B8 || addr == 0xD1B4)
            fprintf(stderr, "  WR [%04X]=%02X  (ZVE1 PC=%04X)\n", addr, data, m.cpuPC());
    });

    int reads = 0;
    bool in_read = false, trace = false;
    m.setZVE2TraceCallback([&](const Z80& z){
        if (!trace) return;
        if (z.PC == 0x1F7D) {
            uint8_t cyl = m.memReadDebug(0x1F8C), head = m.memReadDebug(0x1F8D);
            uint8_t sec = m.memReadDebug(0x1F8F), sz   = m.memReadDebug(0x1F90);
            fprintf(stderr, "read #%-3d want cyl=%u head=%u sec=%u size=%u -> ",
                    reads++, cyl, head, sec, sz);
            in_read = true;
        } else if (in_read && z.PC == 0x2038) { fprintf(stderr, "IDAM FOUND, data\n"); in_read=false; }
        else   if (in_read && z.PC == 0x2034) { fprintf(stderr, "IDAM NOT FOUND (err06)\n"); in_read=false; }
    });

    auto runc = [&](long long c){ long long e=0; while(e<c){int n=m.run(5000); if(!n)break; e+=n;} };

    runc(4'000'000);
    trace = true;                 // trace through the whole running-OS phase
    if (tstr) {
        runc(5'000'000);          // reach the clock prompt
        for (const char* p=tstr; *p; ++p){ m.keyPress((uint8_t)*p,false,false); runc(800'000); m.keyRelease((uint8_t)*p); runc(150'000);}
        m.keyPress(0x01000004,false,false); runc(800'000); m.keyRelease(0x01000004);
    }
    runc(26'000'000);

    fprintf(stderr, "(captured %d reads)  final ZVE1 PC=0x%04X  [D1BE]=%02X%02X\n",
            reads, m.cpuPC(), m.memReadDebug(0xD1BF), m.memReadDebug(0xD1BE));
    return 0;
}
