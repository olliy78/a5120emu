/**
 * @file mc80.cpp
 * @brief MC80.3x — Aufbau, Lebenslauf, Laufschleife (AP M2).
 * @see mc80.h, doc/design/31_mc80.md, doc/mc80/hardware_3x.md
 */

#include "core/machines/mc80/mc80.h"
#include "core/cards/mc80/egos_text.h"
#include "core/cards/mc80/rom_segmente.h"
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
    : cfg_(pruefe(cfg)), zve_(bus_), vis_(bus_)
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
    if (cfg_.variante == Config::Variante::Mc8031) {
        // MC80.31E: EPROM-Segmente 10H und 20H (doc/EPROMS/MC80/segmente/, je 4-KB-Abzug).
        // Lage: d = 4000H (bytegleich doc/mc80/spiele/tetris_4000_4fff.bin), f = 4000H und
        // g = 5000H (= 2. Hälfte von hasi_4000_5fff.bin); b = 5000H, c = 6000H, h = 6000H nach
        // den Eigenbezügen der Sprungziele; a = 7000H [?] (ohne Beleg), e (20H, 7000H?) fehlt.
        auto s10 = std::make_unique<Mc80EpromSegment>(0x10);
        s10->abzug(0x4000, MC80_SEG10_D, sizeof(MC80_SEG10_D));
        s10->abzug(0x5000, MC80_SEG10_B, sizeof(MC80_SEG10_B));
        s10->abzug(0x6000, MC80_SEG10_C, sizeof(MC80_SEG10_C));
        s10->abzug(0x7000, MC80_SEG10_A, sizeof(MC80_SEG10_A));
        auto s20 = std::make_unique<Mc80EpromSegment>(0x20);
        s20->abzug(0x4000, MC80_SEG20_F, sizeof(MC80_SEG20_F));
        s20->abzug(0x5000, MC80_SEG20_G, sizeof(MC80_SEG20_G));
        s20->abzug(0x6000, MC80_SEG20_H, sizeof(MC80_SEG20_H));
        for (auto* k : {s10.get(), s20.get()}) fenster_.anmelden(k);
        eprom_.push_back(std::move(s10));
        eprom_.push_back(std::move(s20));
    }
    fenster_.attachToBus(bus_);
    vis_.attachToBus(bus_);
    asp_.attachToBus(bus_);
    tast_.empfaengerFrei = [this]() { return asp_.tastaturEmpfaengerFrei(); };
    tast_.senden = [this](uint8_t b) { asp_.empfange(b); };
    zve_.setSegmentCallback([this](uint8_t m) {
        for (auto& k : spe_) k->setSegmentRegister(m);
        for (auto& k : eprom_) k->setSegmentRegister(m);
    });
    bus_.setInterruptChain({&zve_, &asp_});   // Kartenreihenfolge [?], hardware_3x.md §7
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
    vis_.powerOn();
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
    asp_.reset();     // die ASP liefert selbst einen Rücksetzimpuls [BDA S.17]
    tast_.reset();    // VIS 2a: /RESET lässt das Bild stehen
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
        dirty |= asp_.clockTick(used);
        dirty |= tast_.takt(used);
        while (asp_.hatGesendet()) {
            tast_.vomRechner(asp_.nimmGesendet());
            dirty = true;
        }
        dirty |= k6022Takt(used);
        dirty |= k5020Takt(used);
        if (dirty) bus_.markIntDirty();
    }
    k6022AutoFlush();
    k5020AutoFlush();
    return max_cycles - remaining;
}

// ─── Tastatur, Text ──────────────────────────────────────────────────────────

void Mc80Machine::keyPress(uint32_t keycode, bool shift, bool ctrl)
{
    tast_.qtTaste(keycode, shift, ctrl);
}

std::string Mc80Machine::textZeile(int row) const
{
    if (row < 0 || row >= mc80::EGOS_TEXTZEILEN) return {};
    if (text_revision_ != vis_.revision() || text_.empty()) {
        auto* self = const_cast<Mc80Machine*>(this);
        auto mem = [self](uint16_t a) { return self->bus_.memRead(a); };
        text_ = mc80::egosText(vis_, mem);
        text_revision_ = vis_.revision();
    }
    return text_[static_cast<size_t>(row)];
}

uint8_t Mc80Machine::screenChar(int col, int row) const
{
    if (col < 0 || col >= TEXT_SPALTEN || row < 0 || row >= mc80::EGOS_TEXTZEILEN) return 0;
    const std::string z = textZeile(row);
    return col < static_cast<int>(z.size()) ? static_cast<uint8_t>(z[static_cast<size_t>(col)]) : ' ';
}
