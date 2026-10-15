/**
 * @file mc80.cpp
 * @brief MC80.3x — Aufbau, Lebenslauf, Laufschleife (AP M2).
 * @see mc80.h, doc/design/31_mc80.md, doc/mc80/hardware_3x.md
 */

#include "core/machines/mc80/mc80.h"
#include "core/logger.h"
#include <stdexcept>

const char* Mc80Machine::variantenName(Config::Variante v)
{
    switch (v) {
        case Config::Variante::Mc8030: return "MC80.30";
        case Config::Variante::Mc8031: return "MC80.31";
        case Config::Variante::Mc8033: return "MC80.33";
        case Config::Variante::Mc8021: return "MC80.21";
        case Config::Variante::Mc8022: return "MC80.22";
    }
    return "MC80.?";
}

Mc80Machine::Config Mc80Machine::pruefe(const Config& cfg)
{
    switch (cfg.variante) {
        case Config::Variante::Mc8030:
        case Config::Variante::Mc8031:
            break;
        case Config::Variante::Mc8033:
            throw std::invalid_argument(
                "MC80.33 ist noch nicht nachgebildet (AKM + PK 3; Treiber-ROM nur halb vorhanden)");
        case Config::Variante::Mc8021:
        case Config::Variante::Mc8022:
            throw std::invalid_argument(std::string(variantenName(cfg.variante)) +
                                        " ist noch nicht nachgebildet (MC80.2x = Etappe 3)");
    }
    if (cfg.spe_karten < 1 || cfg.spe_karten > 2)
        throw std::invalid_argument("MC80: spe_karten muss 1 oder 2 sein");
    return cfg;
}

Mc80Machine::Mc80Machine(const Config& cfg)
    : cfg_(pruefe(cfg)), zve_(bus_), fb_(static_cast<size_t>(FB_BREITE) * FB_HOEHE, 0)
{
    zve_.attachToBus(bus_);
    for (int i = 0; i < cfg_.spe_karten; ++i) {
        auto c = i == 0 ? Mc80Spe2::Config::mc8030()
                        : Mc80Spe2::Config::nurRam(static_cast<uint8_t>(1u << i));
        auto karte = std::make_unique<Mc80Spe2>(c);
        karte->attachToBus(bus_);
        fenster_.anmelden(karte.get());
        spe_.push_back(std::move(karte));
    }
    fenster_.attachToBus(bus_);
    zve_.setSegmentCallback([this](uint8_t m) {
        for (auto& k : spe_) k->setSegmentRegister(m);
    });
    bus_.setInterruptChain({&zve_});
    powerOn();
}

Mc80Machine::~Mc80Machine() = default;

const FormatCatalog& Mc80Machine::formatCatalog() const
{
    static const FormatCatalog leer;
    return leer;
}

void Mc80Machine::powerOn()
{
    for (auto& k : spe_) k->powerOn(cfg_.ram_fill);
    zve_.powerOn(cfg_.ram_fill);   // zuletzt: meldet Segmentregister 00H an die SPE
    rafPowerOn();
    total_cycles_ = 0;
    reset();
    LOG_INFO("MC80", "%s: Netz ein", variantenName(cfg_.variante));
}

void Mc80Machine::reset()
{
    bestueckungAbschliessen();
    for (auto& k : spe_) k->reset();
    zve_.reset();
    k6022Reset();
    k5020Reset();
    rafReset();
    bus_.clearNMI();
    bus_.releaseINT();
    bus_.markIntDirty();
}

int Mc80Machine::run(int max_cycles)
{
    bestueckungAbschliessen();
    if (nmi_taster_.exchange(false, std::memory_order_relaxed)) bus_.assertNMI();
    Z80& cpu = zve_.cpu();
    int remaining = max_cycles;
    while (remaining > 0 && !stop_.load(std::memory_order_relaxed)) {
        k1520::logging::Logger::instance().update(total_cycles_, cpu.PC, 0);
        bus_.updateInterruptChain();
        // Nach `EI` erst nach dem folgenden Befehl (Z80::ei_schatten, Fund A8).
        if (bus_.isINT() && cpu.interruptAnnehmbar()) cpu.interrupt(bus_.interruptAcknowledge());
        if (bus_.isNMI()) {
            cpu.nmi();
            bus_.clearNMI();
        }
        const int used = cpu.step();
        if (used == 0 && stop_.load(std::memory_order_relaxed)) break;   // Debugger-Halt
        remaining     -= used;
        total_cycles_ += static_cast<uint64_t>(used);

        bool dirty = zve_.clockTick(used);
        dirty |= k6022Takt(used);
        dirty |= k5020Takt(used);
        if (dirty) bus_.markIntDirty();
    }
    k6022AutoFlush();
    k5020AutoFlush();
    return max_cycles - remaining;
}
