// Interactive scripted driver: boot CP/A with two mounted disks, send keystrokes
// from a script, and dump the 80x24 VRAM text between steps.  Built to drive the
// interactive FORMAT.COM / FORMATB.COM utilities (drive letter / drive type /
// disk format prompts) and to actually format the disk mounted in drive B:.
//
// Usage:  format_driver <diskA> <diskB> <script>
//
// Script commands (one per line, '#' = comment, blank lines ignored):
//   boot <Mcycles>     run <Mcycles> million CPU cycles (no key)
//   run  <Mcycles>     same as boot (alias)
//   type <text>        type the literal rest-of-line, char by char
//   enter              press Return
//   key  <name>        press a named key: return esc space bs del up down left right
//   dump <label>       print the current 80x24 screen with <label>
//   wp   <0|1>         set write-protect on drive B (1=protected)
//
// Each printable char and Enter is sent one-per-run-batch (1M cycles) so the BIOS
// 5 ms keyboard poll picks it up before the next arrives — same cadence kbd_test
// uses.  Disks are mounted read/write so FORMAT writes land in the diskB file.
#include "core/machines/a5120/a5120.h"
#include "core/logger.h"
#include <cstdio>
#include <cstring>
#include <string>
#include <fstream>
#include <sstream>

using k1520::logging::Logger;
using k1520::logging::Level;

static constexpr uint32_t QK_RETURN    = 0x01000004;
static constexpr uint32_t QK_ESCAPE    = 0x01000000;
static constexpr uint32_t QK_BACKSPACE = 0x01000003;
static constexpr uint32_t QK_DELETE    = 0x01000007;
static constexpr uint32_t QK_LEFT      = 0x01000012;
static constexpr uint32_t QK_UP        = 0x01000013;
static constexpr uint32_t QK_RIGHT     = 0x01000014;
static constexpr uint32_t QK_DOWN      = 0x01000015;

static void dumpScreen(A5120Machine& m, const char* label) {
    printf("\n=== SCREEN: %s ===\n", label);
    for (int row = 0; row < 24; ++row) {
        char line[81];
        for (int col = 0; col < 80; ++col) {
            uint8_t c = m.memReadDebug(static_cast<uint16_t>(0xF800 + row * 80 + col));
            line[col] = (c >= 0x20 && c < 0x7F) ? static_cast<char>(c) : ' ';
        }
        line[80] = 0;
        // trim trailing spaces for readability
        int e = 80; while (e > 0 && line[e-1] == ' ') line[--e] = 0;
        printf("  |%s\n", line);
    }
    fflush(stdout);
}

static void runFor(A5120Machine& m, long long cycles) {
    long long done = 0;
    while (done < cycles) {
        int n = m.run(5000);
        if (n == 0) break;
        done += n;
    }
}

static void pressKey(A5120Machine& m, uint32_t kc) {
    m.keyPress(kc, false, false);
    runFor(m, 1'000'000);
    m.keyRelease(kc);
    runFor(m, 300'000);
}

int main(int argc, char** argv) {
    if (argc < 4) { fprintf(stderr, "usage: %s <diskA> <diskB> <script>\n", argv[0]); return 1; }
    const char* diskA = argv[1];
    const char* diskB = argv[2];
    const char* script = argv[3];

    Logger::instance().setBaseLevel(Level::ERROR);

    A5120Machine machine;
    machine.powerOn();

    if (!(machine.mountDisk(0, diskA, "cpa780", false) ||
          machine.mountDisk(0, diskA, "cpa800", false))) {
        fprintf(stderr, "ERROR: mount A '%s': %s\n", diskA, machine.lastError().c_str());
        return 1;
    }
    if (!(machine.mountDisk(1, diskB, "cpa780", false) ||
          machine.mountDisk(1, diskB, "cpa800", false))) {
        fprintf(stderr, "ERROR: mount B '%s': %s\n", diskB, machine.lastError().c_str());
        return 1;
    }
    fprintf(stderr, "Mounted A=%s  B=%s\n", diskA, diskB);

    std::ifstream in(script);
    if (!in) { fprintf(stderr, "ERROR: cannot open script '%s'\n", script); return 1; }

    std::string line;
    while (std::getline(in, line)) {
        // strip CR
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (line.empty() || line[0] == '#') continue;
        std::istringstream ls(line);
        std::string cmd; ls >> cmd;

        if (cmd == "boot" || cmd == "run") {
            double mc = 0; ls >> mc;
            fprintf(stderr, "[run %.1fM cycles]\n", mc);
            runFor(machine, (long long)(mc * 1'000'000));
        } else if (cmd == "type") {
            std::string rest;
            std::getline(ls, rest);
            if (!rest.empty() && rest[0] == ' ') rest.erase(0, 1);
            fprintf(stderr, "[type '%s']\n", rest.c_str());
            for (char ch : rest) pressKey(machine, static_cast<uint8_t>(ch));
        } else if (cmd == "enter") {
            fprintf(stderr, "[enter]\n");
            pressKey(machine, QK_RETURN);
        } else if (cmd == "key") {
            std::string name; ls >> name;
            uint32_t kc = 0;
            if (name == "return" || name == "enter") kc = QK_RETURN;
            else if (name == "esc") kc = QK_ESCAPE;
            else if (name == "space") kc = ' ';
            else if (name == "bs") kc = QK_BACKSPACE;
            else if (name == "del") kc = QK_DELETE;
            else if (name == "up") kc = QK_UP;
            else if (name == "down") kc = QK_DOWN;
            else if (name == "left") kc = QK_LEFT;
            else if (name == "right") kc = QK_RIGHT;
            else { kc = static_cast<uint8_t>(name.empty() ? ' ' : name[0]); }
            fprintf(stderr, "[key %s]\n", name.c_str());
            pressKey(machine, kc);
        } else if (cmd == "dump") {
            std::string lbl; std::getline(ls, lbl);
            dumpScreen(machine, lbl.c_str());
        } else if (cmd == "wp") {
            int v = 0; ls >> v;
            machine.setDiskWriteProtect(1, v != 0);
            fprintf(stderr, "[wp B=%d]\n", v);
        } else {
            fprintf(stderr, "[unknown cmd: %s]\n", cmd.c_str());
        }
    }

    dumpScreen(machine, "final");
    return 0;
}
