/**
 * @file pc1715.cpp
 * @brief PC 1715 — Verdrahtung und Laufschleife (eine CPU).
 * @see pc1715.h, doc/design/21_pc1715.md §8.1; Vorbild core/machines/prg710/prg710.cpp
 */

#include "core/machines/pc1715/pc1715.h"
#include "core/logger.h"

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
{
    zre_.attachToBus(bus_);
    // Interruptkette (§3.3): FD-PIOs (AP-2) → CTC → SIO → Steckplatz.  Die Floppy kommt
    // an die Spitze dieser Liste; die ZRE trägt CTC0 und SIO0.
    bus_.setInterruptChain({&zre_});
    zre_.cpu().abortBeforeExecute = [this] { return stop_.load(std::memory_order_relaxed); };
}

void Pc1715Machine::resetHardware()
{
    stop_.store(false);
    zre_.reset();            // CPU, CTC, SIO, 8275, ROM-Overlay ein
    bus_.clearNMI();
    bus_.releaseINT();
    bus_.releaseWAIT();
    bus_.markIntDirty();
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
        // /WAIT (K5122, AP-2): die CPU stand, die Zeit vergeht für alle anderen mit.
        if (const int w = bus_.takeWaitCycles(); w > 0) {
            used += w;
            cpu.cycles += static_cast<uint64_t>(w);
        }
        remaining     -= used;
        total_cycles_ += used;

        bool dirty = zre_.clockTick(used);
        // Bildwechsel alle 20 ms Maschinenzeit: 8275-DMA + Rastern.
        while (total_cycles_ >= bild_naechst_) {
            zre_.frame();
            bild_naechst_ += Pc1715Zre::FRAME_TAKTE;
        }
        if (dirty) bus_.markIntDirty();
    }
    return max_cycles - remaining;
}
