/**
 * @file k8915.cpp
 * @brief K8915 V3 — Verdrahtung und Laufschleife (eine CPU, keine ZVE2).
 * @see k8915.h, doc/design/16_k8915.md §7.1, §8a AP-E1
 */

#include "core/machines/k8915/k8915.h"
#include "core/logger.h"

K8915Machine::K8915Machine() : K8915Machine(Config{}) {}

K8915Machine::K8915Machine(const Config& cfg)
    : zre_(bus_)
    , ats_(cfg.pruefstecker ? K7028::Config::mitPruefstecker() : K7028::Config{})
    , screen_(bus_, K7024::A5120Config::forK8915())   // registriert VRAM 1000H–17FFH
{
    zre_.attachToBus(bus_);
    ats_.attachToBus(bus_);
    if (cfg.tastatur) kbd_.connect(ats_.sio2(), 1);   // sonst: Kabel gezogen
    // Interruptkette nach der Platzfolge (§6.4 [?]): (K5122 →) ZRE-CTC → ATS.
    bus_.setInterruptChain({&zre_, &ats_});
    zre_.cpu().abortBeforeExecute = [this] { return stop_.load(std::memory_order_relaxed); };
}

void K8915Machine::resetHardware()
{
    stop_.store(false);
    zre_.reset();            // A8H := 00H, CTC, CPU
    ats_.reset();            // SIOs, CTCs, Latch; die Tastatur hat eigenen Takt und Reset
    bus_.clearNMI();
    bus_.releaseINT();
    bus_.releaseWAIT();
    bus_.markIntDirty();
    screen_.clearScreen();
}

void K8915Machine::powerOn()
{
    zre_.powerOn(0x00);
    kbd_.powerOn();          // Selbsttest der Tastatur, KEIN DC1 (Firmware 000CH)
    resetHardware();
    LOG_INFO("K8915", "Netz ein: A8H=00H, Boot-ROM bei 0000H");
}

void K8915Machine::reset()
{
    resetHardware();
    LOG_INFO("K8915", "Reset");
}

int K8915Machine::run(int max_cycles)
{
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

        const int used = cpu.step();
        if (used == 0 && stop_.load(std::memory_order_relaxed)) break;   // Debugger-Halt
        remaining     -= used;
        total_cycles_ += used;

        bool dirty = zre_.clockTick(used);
        dirty |= ats_.clockTick(used);
        dirty |= ats_.service(total_cycles_);
        dirty |= kbd_.service(total_cycles_);
        if (dirty) bus_.markIntDirty();
    }
    return max_cycles - remaining;
}
