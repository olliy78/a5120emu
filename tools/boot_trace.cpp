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
    { 0x0008, "LDI loop – zeroing RAM 0000-07FF",          true  },
    { 0x000E, "XOR A after zeroing – PIO init start",       true  },
    { 0x001C, "OUT BS-PIO A ctrl (Mode 1)",                 true  },
    { 0x00BC, "ROM-to-RAM copy section (JR Z target)",      true  },
    { 0x00C4, "LDIR: ROM[BA..3FF] → RAM[BA..3FF]",         true  },
    { 0x00C8, "OUT BS-PIO B ctrl: Mode 3",                  true  },
    { 0x00D0, "IN A, BS-PIO B – read-modify-write start",   true  },
    { 0x00D4, "OUT BS-PIO B – ROM DISABLE",                 true  },
    { 0x00D6, "EI after ROM disable",                       true  },
    { 0x010F, "IN fcpiobd – start of disk seek loop",       false },
    { 0x01DD, "ZVE2 DMA entry / boot sector load",          false },
};

// ─── ROM disassembly reference table (PC → mnemonic) ─────────────────────────
// Hand-disassembled from ZRE_BOOT_ROM bytes. Used for single-step output.
static const char* romLabel(uint16_t pc) {
    switch (pc) {
        case 0x0000: return "NOP (reset entry)";
        case 0x0001: return "LD BC,0x0800 (zero loop count)";
        case 0x0004: return "LD D,C / LD E,C / LD H,C / LD L,C";
        case 0x0008: return "LDI – zero RAM[DE++] from ROM[0]";
        case 0x000A: return "DEC HL – keep HL=0";
        case 0x000B: return "JP PE,0x0008 – loop while BC≠0";
        case 0x000E: return "XOR A – A=0 after zero loop";
        case 0x000F: return "OUT (0x02),A – reset Q240 (/RES-SPA)";
        case 0x0011: return "LD SP,0x07E0";
        case 0x0014: return "IM 2";
        case 0x0016: return "LD A,0 / LD I,A – I=0";
        case 0x001A: return "LD A,0x7F";
        case 0x001C: return "OUT (0x09),A – BS-PIO A ctrl: Mode 1";
        case 0x001E: return "OUT (0x0B),A – BS-PIO B ctrl: Mode 1";
        case 0x0020: return "LD A,0xFF";
        case 0x0022: return "OUT (0x08),A – BS-PIO A data: 0xFF";
        case 0x0024: return "OUT (0x0A),A – BS-PIO B data: 0xFF (preload latch)";
        case 0x0026: return "LD A,0xB8";
        case 0x0028: return "OUT (0x09),A – BS-PIO A: interrupt vector 0xB8";
        case 0x002A: return "LD A,0xFF";
        case 0x002C: return "OUT (0x09),A – BS-PIO A ctrl: Mode 3";
        case 0x002E: return "LD A,0x7F";
        case 0x0030: return "OUT (0x09),A – BS-PIO A dir mask: 0x7F";
        case 0x0032: return "LD IX,0x0800";
        case 0x0036: return "LD (0x0462),IX";
        case 0x003A: return "LD HL,0x044E";
        case 0x003D: return "LD DE,0x0400";
        case 0x0040: return "LD B,0x3E (loop counter=62)";
        case 0x0042: return "LD C,(IX+0) – read page marker";
        case 0x0045: return "LD A,0xD7";
        case 0x0047: return "OUT (0x09),A – BS-PIO A ctrl (int mask)";
        case 0x0049: return "LD A,0x9F";
        case 0x004B: return "OUT (0x09),A – BS-PIO A: int enable";
        case 0x004D: return "EI";
        case 0x004E: return "LD (IX+0),0xFF – write page marker";
        case 0x0052: return "NOP";
        case 0x0053: return "DI";
        case 0x0054: return "LD A,0x47";
        case 0x0056: return "OUT (0x09),A – BS-PIO A: int disable";
        case 0x0058: return "ADD IX,DE – IX += 0x400";
        case 0x005A: return "DJNZ 0x0042 – page loop";
        case 0x005C: return "LD HL,0x046F";
        case 0x005F: return "LD DE,0x006F";
        case 0x0062: return "LD BC,0x0024";
        case 0x0065: return "LDDR – copy config from 0x046F downward";
        case 0x0067: return "LD HL,0x044E";
        case 0x006A: return "BIT 0,(HL) – check boot flag";
        case 0x006C: return "JR Z,0x00BC – jump if normal boot";
        case 0x006E: return "LD HL,0x0800";
        case 0x0071: return "LD (0x004C),HL";
        case 0x0074: return "LD HL,(0x0462)";
        case 0x0077: return "LD L,0x9D";
        case 0x0079: return "JP (HL) – alternate boot path";
        case 0x007B: return "LD A,0x47 (int disable ctrl)";
        case 0x007D: return "OUT (0x09),A";
        case 0x007F: return "LD A,0xFF";
        case 0x00BC: return "LD HL,0x00BA – ROM copy src";
        case 0x00BF: return "LD D,H / LD E,L – DE=0x00BA";
        case 0x00C1: return "LD BC,0x0346";
        case 0x00C4: return "LDIR – copy ROM[BA..3FF] to RAM";
        case 0x00C6: return "LD A,0xFF";
        case 0x00C8: return "OUT (0x0B),A – BS-PIO B ctrl: Mode 3";
        case 0x00CA: return "OUT (0x13),A – fcpiobc: Mode 3";
        case 0x00CC: return "LD A,0xE2";
        case 0x00CE: return "OUT (0x0B),A – BS-PIO B dir mask: 0xE2";
        case 0x00D0: return "IN A,(0x0A) – read BS-PIO B";
        case 0x00D2: return "AND 0xF6 – clear B0(/LD-ROM) and B3(/WAIT-ZVE2)";
        case 0x00D4: return "OUT (0x0A),A – ROM DISABLE + ZVE2 stall";
        case 0x00D6: return "EI – interrupts enabled after ROM disable";
        case 0x00D7: return "LD A,0xF3";
        case 0x00D9: return "OUT (0x13),A – fcpiobc: floppy PIO B setup";
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
    // Try cpa780 first: boot ROM expects 128B sectors (size code 0x00 at [0x03F6])
    bool mounted = machine.mountDisk(0, disk_path, "cpa780", false);
    if (!mounted) {
        mounted = machine.mountDisk(0, disk_path, "cpa800", false);
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
                if (m.pc == 0x01DD) { boot_reached = true; }
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
