/**
 * @file k8915.cpp
 * @brief K8915 V3 — Verdrahtung und Laufschleife (eine CPU, keine ZVE2).
 * @see k8915.h, doc/design/16_k8915.md §7.1, §8a AP-E1
 */

#include "core/machines/k8915/k8915.h"
#include "core/logger.h"

K8915Machine::K8915Machine()
    : zre_(bus_)
    , screen_(bus_, K7024::A5120Config::forK8915())   // registriert VRAM 1000H–17FFH
{
    zre_.attachToBus(bus_);
    // Interruptkette: vorerst nur die CTC der ZRE.  Vorgabe für später (§6.4,
    // Platzfolge): K5122 → ZRE-CTC → ATS.
    bus_.setInterruptChain({&zre_});
    zre_.cpu().abortBeforeExecute = [this] { return stop_.load(std::memory_order_relaxed); };
}

void K8915Machine::resetHardware()
{
    stop_.store(false);
    zre_.reset();            // A8H := 00H, CTC, CPU
    bus_.clearNMI();
    bus_.releaseINT();
    bus_.releaseWAIT();
    bus_.markIntDirty();
    screen_.clearScreen();
}

void K8915Machine::powerOn()
{
    zre_.powerOn(0x00);
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

        if (zre_.clockTick(used)) bus_.markIntDirty();
    }
    return max_cycles - remaining;
}
