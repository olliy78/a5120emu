// Standalone keyboard smoke test: boot CP/A, type a command, dump the screen.
//
// Boots the supplied disk, runs until the console-input wait loop is reached,
// then injects keystrokes (default "dir\r") and dumps the 80x24 VRAM text so we
// can see whether the typed characters and the command output appear.
//
// Usage: kbd_test <disk.hfe|.img> [text]
#include "core/machines/a5120/a5120.h"
#include "core/logger.h"
#include <cstdio>
#include <cstring>
#include <string>
#include <algorithm>
#include <vector>

using k1520::logging::Logger;
using k1520::logging::Level;

// Qt keycode for Return (matches K7637::QK_RETURN).
static constexpr uint32_t QK_RETURN = 0x01000004;

static void dumpScreen(A5120Machine& m, const char* title) {
    fprintf(stderr, "\n=== %s ===\n", title);
    for (int row = 0; row < 24; ++row) {
        char line[81];
        for (int col = 0; col < 80; ++col) {
            uint8_t c = m.memReadDebug(static_cast<uint16_t>(0xF800 + row * 80 + col));
            line[col] = (c >= 0x20 && c < 0x7F) ? static_cast<char>(c) : '.';
        }
        line[80] = 0;
        fprintf(stderr, "  |%s|\n", line);
    }
}

// Run a number of cycles in small batches (batch granularity matters: the
// keyboard queue is drained once per run() call).
// When `hist` is non-null, sample the ZVE1 PC after each batch (skipping the
// timer-ISR range 0xE600-0xE63F so the foreground/CCP wait is visible).
#include <map>
static void runFor(A5120Machine& m, long long cycles, std::map<uint16_t,long>* hist = nullptr) {
    long long done = 0;
    while (done < cycles) {
        int n = m.run(5000);
        if (n == 0) break;
        done += n;
        if (hist) {
            uint16_t pc = m.cpuPC();
            if (!(pc >= 0xE600 && pc <= 0xE63F)) (*hist)[pc]++;
        }
    }
}

int main(int argc, char** argv) {
    if (argc < 2) { fprintf(stderr, "usage: %s <disk> [text]\n", argv[0]); return 1; }
    const char* disk = argv[1];
    std::string text = (argc >= 3) ? argv[2] : "dir";

    Logger::instance().setBaseLevel(Level::ERROR);

    A5120Machine machine;
    machine.powerOn();

    bool mounted = machine.mountDisk(0, disk, "cpa780", false) ||
                   machine.mountDisk(0, disk, "cpa800", false);
    if (!mounted) {
        fprintf(stderr, "ERROR: could not mount '%s': %s\n", disk, machine.lastError().c_str());
        return 1;
    }

    // Count keyboard SIO port traffic to see whether the OS polls it.
    long long rd5c = 0, rd5d = 0, wr5c = 0;
    bool trace_on = false;
    machine.setBusTrace([&](bool isIO, bool isRead, uint16_t addr, uint8_t data) {
        if (!isIO || !trace_on) return;
        if (addr == 0x5D && isRead) rd5d++;
        if (addr == 0x5C && isRead) { rd5c++; fprintf(stderr, "  RD 5C -> %02X  @PC=%04X\n", data, machine.cpuPC()); }
        if (addr == 0x5C && !isRead) { wr5c++; fprintf(stderr, "  WR 5C <- %02X  @PC=%04X\n", data, machine.cpuPC()); }
    });

    // Boot: run well past the @OS.COM load + CCP start.
    runFor(machine, 130'000'000);
    dumpScreen(machine, "After boot (before typing)");
    trace_on = true;
    fprintf(stderr, "\n[port trace ON]\n");

    // Type the command, one key per run-batch so each is drained and the BIOS
    // 5 ms poll can pick it up before the next arrives.
    // A '^' prefix sends the NEXT char with Ctrl held (so "^C" = Ctrl+C); '~'
    // alone sends a bare Ctrl+C (line/submit abort) with no following char.
    std::map<uint16_t,long> fg_hist;  // foreground PC histogram during typing
    bool ctrl_next = false;
    for (char ch : text) {
        if (ch == '^') { ctrl_next = true; continue; }
        bool ctrl = ctrl_next; ctrl_next = false;
        // '|' = press Enter here (lets one run type e.g. the clock then a command);
        // '~' = bare Ctrl+C; '^X' = Ctrl+X.
        uint32_t kc;
        if      (ch == '|') kc = QK_RETURN;
        else if (ch == '~') { kc = static_cast<uint32_t>('C'); ctrl = true; }
        else                kc = static_cast<uint8_t>(ch);
        machine.keyPress(kc, false, ctrl);
        runFor(machine, 1'000'000, &fg_hist);
        machine.keyRelease(kc);
        runFor(machine, 200'000, &fg_hist);
        if (ch == '|') runFor(machine, 4'000'000, &fg_hist);  // let the command settle
    }
    // Enter
    machine.keyPress(QK_RETURN, false, false);
    runFor(machine, 1'000'000, &fg_hist);
    machine.keyRelease(QK_RETURN);

    // Let the command run.
    runFor(machine, 40'000'000, &fg_hist);

    // Report the hottest foreground (non-ISR) PCs — the CCP/input wait location.
    {
        std::vector<std::pair<uint16_t,long>> v(fg_hist.begin(), fg_hist.end());
        std::sort(v.begin(), v.end(), [](auto&a, auto&b){ return a.second > b.second; });
        fprintf(stderr, "\nForeground PC histogram (top 12, ISR 0xE600-E63F excluded):\n");
        for (size_t i = 0; i < v.size() && i < 12; ++i)
            fprintf(stderr, "  0x%04X : %ld\n", v[i].first, v[i].second);
    }
    dumpScreen(machine, "After typing + Enter");
    fprintf(stderr, "\nKbd SIO port traffic during/after typing: 0x5D rd=%lld  0x5C rd=%lld wr=%lld\n",
            rd5d, rd5c, wr5c);

    return 0;
}
