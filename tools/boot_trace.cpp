/**
 * boot_trace.cpp – Standalone boot diagnostic tool for A5120 emulator.
 *
 * Compiles against k1520core and logs the full boot sequence.
 * Run with: ./boot_trace <disk_image> [cycles_limit]
 */

#include "core/machines/a5120/a5120.h"
#include "core/peripherals/floppy_drive/format_parser.h"
#include "core/logger.h"
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>
#include <unordered_map>
#include <algorithm>

int main(int argc, char** argv) {
    const char* disk_path = argc >= 2 ? argv[1] : "disk_b.img";
    int total_limit = argc >= 3 ? std::atoi(argv[2]) : 1000000;

    fprintf(stderr, "=== A5120 Boot Trace ===\n");
    fprintf(stderr, "Disk: %s\n", disk_path);
    fprintf(stderr, "Max cycles: %d\n\n", disk_path, total_limit);

    A5120Machine machine;
    machine.powerOn();

    // PC histogram: count how many opcode fetches land at each address.
    // We detect opcode fetches as mem-reads where the address equals the
    // previous address + 1 or the same address (RST, JP etc.) -- actually
    // we just count every unique read address. For a proper PC-only histogram
    // we sample cpuPC() after each batch.
    std::unordered_map<uint16_t, uint32_t> pc_hist;
    uint32_t sample_count = 0;

    // Mount boot disk
    bool mounted = machine.mountDisk(0, disk_path, "cpa800", false);
    if (!mounted) {
        fprintf(stderr, "ERROR: Could not mount disk '%s'\n", disk_path);
        fprintf(stderr, "Last error: %s\n", machine.lastError().c_str());
        return 1;
    }
    fprintf(stderr, "Disk mounted OK\n\n");

    // Run in small batches and report progress
    int cycles_done = 0;
    int batch = 500;
    int next_sample = 0;

    while (cycles_done < total_limit) {
        int done = machine.run(batch);
        cycles_done += done;

        // Sample PC after every batch
        uint16_t pc = machine.cpuPC();
        pc_hist[pc]++;
        sample_count++;

        // Every 50000 cycles, print a progress marker to stderr
        if (cycles_done >= next_sample) {
            fprintf(stderr, "\n[PROGRESS] cycles=%d  PC=0x%04X\n", cycles_done, pc);
            fflush(stderr);
            next_sample = cycles_done + 50000;
        }
        if (done == 0) break;
    }

    fprintf(stderr, "\n=== Boot Trace Complete: %d cycles ===\n", cycles_done);

    // Print top-20 hottest PC values
    std::vector<std::pair<uint32_t, uint16_t>> hist_sorted;
    hist_sorted.reserve(pc_hist.size());
    for (auto& kv : pc_hist)
        hist_sorted.push_back({kv.second, kv.first});
    std::sort(hist_sorted.rbegin(), hist_sorted.rend());
    fprintf(stderr, "\nPC histogram (top 30 hottest, %u samples total):\n", sample_count);
    int shown = 0;
    for (auto& kv : hist_sorted) {
        fprintf(stderr, "  0x%04X : %6u  (%.1f%%)\n",
                kv.second, kv.first,
                100.0 * kv.first / sample_count);
        if (++shown >= 30) break;
    }

    // Final PC
    fprintf(stderr, "\nFinal PC: 0x%04X\n", machine.cpuPC());

    // Check VRAM content (read via debug bus reads)
    fprintf(stderr, "\nVRAM 0xF800-0xF827: ");
    for (int i = 0; i < 40; i++)
        fprintf(stderr, "%02X ", machine.memReadDebug(0xF800 + i));
    fprintf(stderr, "\n");
    fprintf(stderr, "VRAM 0xFFFC-0xFFFF:  ");
    for (int i = 0; i < 4; i++)
        fprintf(stderr, "%02X ", machine.memReadDebug(0xFFFC + i));
    fprintf(stderr, "\n");

    // Check framebuffer for activity
    const uint8_t* fb = machine.framebuffer();
    if (fb) {
        int nonzero = 0;
        for (int i = 0; i < 80 * 24; i++)
            if (fb[i] != 0 && fb[i] != 0xFF) nonzero++;
        fprintf(stderr, "Framebuffer (pixel buf) non-zero bytes: %d\n", nonzero);
    }

    return 0;
}
