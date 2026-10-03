/**
 * @file pc1715.cpp
 * @brief PC 1715 — Verdrahtung und Laufschleife (eine CPU).
 * @see pc1715.h, doc/design/21_pc1715.md §8.1; Vorbild core/machines/prg710/prg710.cpp
 */

#include "core/machines/pc1715/pc1715.h"
#include "core/logger.h"

namespace {
std::array<DriveProfile, 4> profile(const Pc1715Machine::Config& cfg) {
    return { builtinDriveProfile(cfg.laufwerke[0]), builtinDriveProfile(cfg.laufwerke[1]),
             builtinDriveProfile(cfg.laufwerke[2]), builtinDriveProfile(cfg.laufwerke[3]) };
}
}  // namespace

Pc1715Machine::Config Pc1715Machine::pruefe(const Config& cfg)
{
    if (cfg.variante == Config::Variante::Pc1715W)
        throw std::runtime_error("PC 1715W wird noch nicht unterstützt (256 KB Bänke, U8272, "
                                 "Z80-DMA: doc/design/21_pc1715.md §4, AP-W1…W3)");
    return cfg;
}

Pc1715Machine::Pc1715Machine() : Pc1715Machine(Config{}) {}

Pc1715Machine::Pc1715Machine(const Config& cfg)
    : variante_(pruefe(cfg).variante)
    , zre_(bus_, Pc1715Zre::Config{cfg.bild, cfg.zeichensatz})
    , afs_(bus_, profile(cfg), CPU_HZ)
    , lw_(afs_, profile(cfg))
{
    zre_.attachToBus(bus_);
    // Floppy-Ansteuerung 20-330-0102 (Servicehandbuch §1.5.2.1): Daten-PIO 00H–03H,
    // Steuer-PIO 04H–07H, /KRFD 20H–23H (AB0 = 0 SE-Register, AB0 = 1 MO-Register; AB1
    // ist nicht ausgewertet — 22H/23H spiegeln 20H/21H).
    afs_.setPortlage(K5122::Portlage::Pc1715);
    bus_.registerIO(&afs_, 0x00, 8);
    bus_.registerIO(&afs_, 0x20, 4);
    // /WAIT-Synchronisation (§1.5.3.6: IN/OUT ohne bereitstehendes Byte hält die CPU an).
    // Marken-FF high-aktiv (Vorgabe): S502 0490H `IN A,(06H) / AND 02H / JR Z` wartet auf 1.
    afs_.setSynchronisation(K5122::Synchronisation::Wait);
    // Interruptkette (§3.3, Servicehandbuch §1.2.3 „Prioritätenkette“): die Steuer-PIO der
    // FD-Steuerung ist das vorderste Glied, dann CTC → SIO der ZRE → Steckplatz.  Die
    // Daten-PIO zeigt das Bild nicht; im Modell hängt sie innerhalb der K5122 hinter der
    // Steuer-PIO und meldet nie (S502/BIOS geben ihr kein Interruptwort) [?].
    bus_.setInterruptChain({&afs_, &zre_});
    zre_.cpu().abortBeforeExecute = [this] { return stop_.load(std::memory_order_relaxed); };
}

void Pc1715Machine::resetHardware()
{
    stop_.store(false);
    afs_.flushDisks();
    zre_.reset();            // CPU, CTC, SIO, 8275, ROM-Overlay ein
    afs_.reset();
    bus_.clearNMI();
    bus_.releaseINT();
    bus_.releaseWAIT();
    bus_.markIntDirty();
    prev_afs_int_ = false;
    bild_naechst_ = total_cycles_ + Pc1715Zre::FRAME_TAKTE;
}

void Pc1715Machine::powerOn()
{
    zre_.powerOn(0x00);
    resetHardware();
    LOG_INFO("PC1715", "Netz ein: ROM-Overlay S502 bei 0000H");
}

void Pc1715Machine::reset()
{
    resetHardware();
    LOG_INFO("PC1715", "Reset");
}

int Pc1715Machine::run(int max_cycles)
{
    if (nmi_taster_.exchange(false, std::memory_order_relaxed)) {
        bus_.assertNMI();
        LOG_INFO("PC1715", "NMI");
    }
    Z80& cpu = zre_.cpu();
    int remaining = max_cycles;
    while (remaining > 0 && !stop_.load(std::memory_order_relaxed)) {
        k1520::logging::Logger::instance().update(total_cycles_, cpu.PC, 0);
        {
            const bool fi = afs_.hasInterrupt();
            if (fi != prev_afs_int_) { bus_.markIntDirty(); prev_afs_int_ = fi; }
        }
        bus_.updateInterruptChain();

        if (bus_.isINT() && cpu.IFF1) {
            const uint8_t vec = bus_.interruptAcknowledge();
            cpu.interrupt(vec);
        }
        if (bus_.isNMI()) {
            cpu.nmi();
            bus_.clearNMI();
        }

        int used = cpu.step();
        if (used == 0 && stop_.load(std::memory_order_relaxed)) break;   // Debugger-Halt
        // /WAIT der K5122: die CPU stand, die Zeit vergeht für alle anderen mit.
        if (const int w = bus_.takeWaitCycles(); w > 0) {
            used += w;
            cpu.cycles += static_cast<uint64_t>(w);
        }
        remaining     -= used;
        total_cycles_ += used;

        afs_.update(used);
        bool dirty = zre_.clockTick(used);
        // Bildwechsel alle 20 ms Maschinenzeit: 8275-DMA + Rastern.
        while (total_cycles_ >= bild_naechst_) {
            zre_.frame();
            bild_naechst_ += Pc1715Zre::FRAME_TAKTE;
        }
        if (dirty) bus_.markIntDirty();
    }
    lw_.autoFlush(total_cycles_);
    return max_cycles - remaining;
}
