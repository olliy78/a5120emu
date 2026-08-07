#include "tests/support/machine_run.h"

#include "tests/support/screen.h"

namespace k1520test {

void runCycles(A5120Machine& m, long long cycles) {
    for (long long done = 0; done < cycles; done += kSmallBatch)
        m.run(static_cast<int>(kSmallBatch));
}

bool runSmallUntil(A5120Machine& m, const std::string& needle, long long max_cycles,
                   long long check_every) {
    long long since_check = 0;
    for (long long done = 0; done < max_cycles; done += kSmallBatch) {
        m.run(static_cast<int>(kSmallBatch));
        since_check += kSmallBatch;
        if (since_check < check_every) continue;
        since_check = 0;
        if (vramText(m).find(needle) != std::string::npos) return true;
    }
    return false;
}

bool runUntilVramContains(A5120Machine& m, const std::string& needle, long long max_cycles) {
    for (long long done = 0; done < max_cycles; done += kCoarseBatch) {
        m.run(static_cast<int>(kCoarseBatch));
        if (vramText(m).find(needle) != std::string::npos) return true;
    }
    return false;
}

bool runUntilPC(A5120Machine& m, uint16_t target, long long max_cycles) {
    bool reached = false;
    m.setCpuTraceCallback([&](const Z80& z) {
        if (z.PC == target && !m.isRomEnabled()) reached = true;
    });
    for (long long done = 0; done < max_cycles && !reached; done += kCoarseBatch)
        m.run(static_cast<int>(kCoarseBatch));
    m.setCpuTraceCallback({});   // Lambda fallenlassen (es hält lokale Referenzen)
    return reached;
}

}  // namespace k1520test
