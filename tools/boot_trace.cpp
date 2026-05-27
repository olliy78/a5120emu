/**
 * boot_trace.cpp – Standalone boot diagnostic tool for A5120 emulator.
 *
 * Compiles against k1520core and logs the full boot sequence.
 *
 * Usage:
 *   ./boot_trace [options] <disk_image>
 *   Options:
 *     -c <cycles>    Max cycles to simulate (default: 5000000)
 *     -s             Single-step trace: print every instruction in ROM (PC < 0x0400)
 *     -n <count>     Number of instructions to single-step trace (default: 200)
 *     -v             Verbose: also dump [0xFFFC] value on every change
 */

#include "core/machines/a5120/a5120.h"
#include "core/peripherals/floppy_drive/format_parser.h"
#include "core/logger.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>
#include <unordered_map>
#include <algorithm>

// ─── Milestone detection ──────────────────────────────────────────────────────

struct Milestone {
    uint16_t    pc;
    const char* label;
    bool        once;       // only report on first hit
    bool        hit = false;
};

static std::vector<Milestone> milestones = {
    { 0x0038, "RST-38h handler",            true  },
    { 0x0016, "RET M (first trampoline)",   true  },
    { 0x005C, "DJNZ fall-through → 0x5C",  false },
    { 0x001E, "DJNZ fall-through → 0x1E",  false },
    { 0x003E, "PUSH AF sequence (0x003E)",  true  },
    { 0x0040, "LD A,(0xFFFC)",              false },
    { 0x0048, "RLCA (exit candidate)",      false },
    { 0x0068, "OR A before CALL 0x0303",    false },
    { 0x006F, "CALL 0x0303 → BOOT!",        true  },
};

// ─── ROM disassembly reference table (PC → mnemonic) ─────────────────────────
// Hand-disassembled from ZRE_BOOT_ROM bytes. Used for single-step output.
static const char* romLabel(uint16_t pc) {
    switch (pc) {
        case 0x0000: return "XOR A,0x77";
        case 0x0002: return "DI";
        case 0x0004: return "ADD HL,DE";
        case 0x0006: return "DAA";
        case 0x0007: return "LD D,B";
        case 0x0008: return "INC B";
        case 0x0009: return "LD B,L";
        case 0x000A: return "RET P";
        case 0x000B: return "RET P";
        case 0x000C: return "LD DE,0x5011";
        case 0x000F: return "LD D,B";
        case 0x0010: return "DEC DE";
        case 0x0011: return "DEC DE";
        case 0x0012: return "DI";
        case 0x0014: return "DEC A";
        case 0x0015: return "DEC A";
        case 0x0016: return "RET M";
        case 0x0017: return "RET M";
        case 0x0018: return "ADD HL,BC [RST18h]";
        case 0x0019: return "ADD HL,BC";
        case 0x001A: return "OUT (0xD3),A";
        case 0x001C: return "DJNZ 0x5C";
        case 0x001E: return "OUT (0xD3),A [fall-thru]";
        case 0x0020: return "JR NC,0x21";
        case 0x0021: return "RST 38h [NC path]";
        case 0x0022: return "RST 38h [C path]";
        case 0x0026: return "RET M";
        case 0x0027: return "RET M";
        case 0x0028: return "RST 30h [RST28h]";
        case 0x0030: return "CCF [RST30h]";
        case 0x0031: return "LD A,0xDF";
        case 0x0033: return "RST 18h";
        case 0x0034: return "RST 38h [ret from RST18h]";
        case 0x0038: return "ADD A,A [RST38h]";
        case 0x0039: return "JR Z,0x48";
        case 0x003B: return "DEC C";
        case 0x003C: return "RST 30h";
        case 0x003D: return "RST 30h";
        case 0x003E: return "PUSH AF";
        case 0x003F: return "PUSH AF";
        case 0x0040: return "LD A,(0xFFFC)";
        case 0x0043: return "RST 38h [after LD A,(0xFFFC)]";
        case 0x0044: return "RST 38h [+1]";
        case 0x0045: return "JP C,0x9696";
        case 0x0048: return "RLCA";
        case 0x0049: return "JR 0x68";
        case 0x005C: return "LD H,0 [DJNZ target]";
        case 0x005E: return "PUSH AF";
        case 0x005F: return "PUSH AF";
        case 0x0060: return "LD A,A (NOP)";
        case 0x0061: return "RST 38h";
        case 0x0068: return "OR A";
        case 0x0069: return "OR A";
        case 0x006A: return "CALL M,0x28FC";
        case 0x006D: return "JR Z,0x3C";
        case 0x006F: return "CALL 0x0303 → BOOT";
        default:     return nullptr;
    }
}

// ─── main ─────────────────────────────────────────────────────────────────────

int main(int argc, char** argv) {
    const char* disk_path   = nullptr;
    int   total_limit       = 5000000;
    bool  single_step       = false;
    int   single_step_count = 200;
    bool  verbose           = false;

    // Parse arguments
    for (int i = 1; i < argc; ++i) {
        if (!strcmp(argv[i], "-c") && i+1 < argc) { total_limit = atoi(argv[++i]); }
        else if (!strcmp(argv[i], "-s"))            { single_step = true; }
        else if (!strcmp(argv[i], "-n") && i+1 < argc) { single_step_count = atoi(argv[++i]); }
        else if (!strcmp(argv[i], "-v"))            { verbose = true; }
        else                                        { disk_path = argv[i]; }
    }
    if (!disk_path) disk_path = "disk_b.img";

    fprintf(stderr, "=== A5120 Boot Trace ===\n");
    fprintf(stderr, "Disk:       %s\n", disk_path);
    fprintf(stderr, "Max cycles: %d\n", total_limit);
    fprintf(stderr, "Step trace: %s", single_step ? "ON" : "OFF");
    if (single_step) fprintf(stderr, " (first %d ROM instructions)", single_step_count);
    fprintf(stderr, "\n\n");

    A5120Machine machine;
    machine.powerOn();

    // ── Single-step trace in ROM ───────────────────────────────────────────────
    int step_count = 0;
    if (single_step) {
        machine.setCpuTraceCallback([&](const Z80& z) {
            if (z.PC >= 0x0400 || step_count >= single_step_count) return;
            const char* label = romLabel(z.PC);
            fprintf(stderr, "  [step %3d] PC=%04X SP=%04X AF=%04X BC=%04X DE=%04X HL=%04X  %s\n",
                    step_count, z.PC, z.SP, z.AF, z.BC, z.DE, z.HL,
                    label ? label : "");
            ++step_count;
        });
    }

    // ── PC histogram ─────────────────────────────────────────────────────────
    std::unordered_map<uint16_t, uint32_t> pc_hist;
    uint32_t sample_count = 0;

    // ── Mount boot disk ───────────────────────────────────────────────────────
    bool mounted = machine.mountDisk(0, disk_path, "cpa800", false);
    if (!mounted) {
        // Try cpa780 as fallback
        mounted = machine.mountDisk(0, disk_path, "cpa780", false);
    }
    if (!mounted) {
        fprintf(stderr, "ERROR: Could not mount disk '%s'\n", disk_path);
        fprintf(stderr, "Last error: %s\n", machine.lastError().c_str());
        fprintf(stderr, "Continuing without disk...\n\n");
    } else {
        fprintf(stderr, "Disk mounted OK\n\n");
    }

    // ── Run loop ──────────────────────────────────────────────────────────────
    int    cycles_done    = 0;
    int    batch          = 500;
    int    next_progress  = 0;
    bool   boot_reached   = false;
    bool   rom_was_active = true;

    uint8_t last_fffc    = 0xFF;   // track [0xFFFC] changes

    // Milestone hit counters (separate from the once-flag to count total hits)
    std::unordered_map<uint16_t, uint32_t> milestone_counts;

    while (cycles_done < total_limit) {
        int done = machine.run(batch);
        cycles_done += done;

        uint16_t pc = machine.cpuPC();
        pc_hist[pc]++;
        sample_count++;

        // ── ROM enable/disable transition ─────────────────────────────────────
        bool rom_now = machine.isRomEnabled();
        if (rom_was_active && !rom_now) {
            fprintf(stderr, "\n*** ROM DISABLED at cycle %d (PC=0x%04X) ***\n",
                    cycles_done, pc);
        }
        rom_was_active = rom_now;

        // ── [0xFFFC] change tracking ──────────────────────────────────────────
        uint8_t cur_fffc = machine.memReadDebug(0xFFFC);
        if (cur_fffc != last_fffc) {
            if (verbose)
                fprintf(stderr, "  [cycle %7d] [0xFFFC] changed: 0x%02X → 0x%02X  SP=0x%04X PC=0x%04X\n",
                        cycles_done, last_fffc, cur_fffc, machine.cpuSP(), pc);
            last_fffc = cur_fffc;
        }

        // ── Milestone detection ───────────────────────────────────────────────
        for (auto& m : milestones) {
            if (pc == m.pc) {
                milestone_counts[m.pc]++;
                if (!m.hit) {
                    m.hit = true;
                    fprintf(stderr, "\n[MILESTONE] cycle=%7d PC=0x%04X  %s\n",
                            cycles_done, pc, m.label);
                    fprintf(stderr, "            SP=%04X AF=%04X BC=%04X DE=%04X HL=%04X\n",
                            machine.cpuSP(), machine.cpuAF(),
                            machine.cpuBC(), machine.cpuDE(), machine.cpuHL());
                } else if (!m.once) {
                    // Repeating milestone — show occasionally
                    uint32_t n = milestone_counts[m.pc];
                    if (n == 10 || n == 100 || (n % 500) == 0)
                        fprintf(stderr, "  [milestone] 0x%04X hit %u times (cycle %d)\n",
                                m.pc, n, cycles_done);
                }
                if (m.pc == 0x006F) { boot_reached = true; }
            }
        }

        // ── Progress report ───────────────────────────────────────────────────
        if (cycles_done >= next_progress) {
            fprintf(stderr, "\n[PROGRESS] cycles=%7d  PC=0x%04X  SP=0x%04X  ROM=%s  [0xFFFC]=0x%02X\n",
                    cycles_done, pc, machine.cpuSP(),
                    machine.isRomEnabled() ? "ON" : "OFF",
                    machine.memReadDebug(0xFFFC));
            fflush(stderr);
            next_progress = cycles_done + 200000;
        }

        if (done == 0 || boot_reached) break;
    }

    // ── Final summary ─────────────────────────────────────────────────────────
    fprintf(stderr, "\n=== Boot Trace Complete: %d cycles ===\n", cycles_done);
    fprintf(stderr, "Boot reached: %s\n", boot_reached ? "YES!" : "NO");
    fprintf(stderr, "ROM enabled:  %s\n", machine.isRomEnabled() ? "YES" : "NO (disabled by BIOS)");

    // Milestone summary
    fprintf(stderr, "\nMilestone hit counts:\n");
    for (auto& m : milestones) {
        uint32_t n = milestone_counts[m.pc];
        fprintf(stderr, "  0x%04X %-40s  %u\n", m.pc, m.label, n);
    }

    // [0xFFFC..0xFFFF] dump
    fprintf(stderr, "\nStack area [0xFFFC..0xFFFF]: ");
    for (int i = 0; i < 4; i++)
        fprintf(stderr, "%02X ", machine.memReadDebug(0xFFFC + i));
    fprintf(stderr, "\n");

    fprintf(stderr, "Stack area [0xFFF0..0xFFFF]: ");
    for (int i = 0; i < 16; i++)
        fprintf(stderr, "%02X ", machine.memReadDebug(0xFFF0 + i));
    fprintf(stderr, "\n");

    // VRAM first 40 bytes (screen start)
    fprintf(stderr, "\nVRAM [0xF800..0xF827]: ");
    for (int i = 0; i < 40; i++)
        fprintf(stderr, "%02X ", machine.memReadDebug(0xF800 + i));
    fprintf(stderr, "\n");

    // Final register state
    fprintf(stderr, "\nFinal CPU state:\n");
    fprintf(stderr, "  PC=%04X SP=%04X AF=%04X BC=%04X DE=%04X HL=%04X\n",
            machine.cpuPC(), machine.cpuSP(), machine.cpuAF(),
            machine.cpuBC(), machine.cpuDE(), machine.cpuHL());

    // PC histogram (top 30)
    std::vector<std::pair<uint32_t, uint16_t>> hist_sorted;
    hist_sorted.reserve(pc_hist.size());
    for (auto& kv : pc_hist)
        hist_sorted.push_back({kv.second, kv.first});
    std::sort(hist_sorted.rbegin(), hist_sorted.rend());
    fprintf(stderr, "\nPC histogram (top 30, %u samples total):\n", sample_count);
    int shown = 0;
    for (auto& kv : hist_sorted) {
        const char* lbl = romLabel(kv.second);
        fprintf(stderr, "  0x%04X : %6u  (%5.2f%%)  %s\n",
                kv.second, kv.first,
                100.0 * kv.first / sample_count,
                lbl ? lbl : "");
        if (++shown >= 30) break;
    }

    // Framebuffer activity check
    const uint8_t* fb = machine.framebuffer();
    if (fb) {
        int nonzero = 0;
        for (int i = 0; i < 80 * 24; i++)
            if (fb[i] != 0 && fb[i] != 0xFF) nonzero++;
        fprintf(stderr, "\nFramebuffer non-default bytes: %d / %d\n", nonzero, 80*24);
    }

    return boot_reached ? 0 : 1;
}
