/**
 * @file prg710.cpp
 * @brief PRG 710 / 710-1 — Verdrahtung und Laufschleife (eine CPU, K5122 im /WAIT-Betrieb).
 * @see prg710.h, doc/design/20_prg710.md §7.1, AP-P1c; Vorbild core/machines/k8915/k8915.cpp
 */

#include "core/machines/prg710/prg710.h"
#include "core/logger.h"

Prg710Machine::Prg710Machine() : Prg710Machine(Config{}) {}

namespace {
std::array<DriveProfile, 4> profile(const Prg710Machine::Config& cfg) {
    return { builtinDriveProfile(cfg.laufwerke[0]), builtinDriveProfile(cfg.laufwerke[1]),
             builtinDriveProfile(cfg.laufwerke[2]), builtinDriveProfile(cfg.laufwerke[3]) };
}
K2521::Config zreConfig(Prg710Machine::Config::Variante v) {
    return v == Prg710Machine::Config::Variante::Prg710 ? K2521::Config::prg710()
                                                        : K2521::Config::prg710_1();
}
}  // namespace

Prg710Machine::Prg710Machine(const Config& cfg)
    : variante_(cfg.variante)
    , zre_(bus_, zreConfig(cfg.variante))
    , speicher_(bus_)
    , screen_(bus_, K7024::A5120Config::forPrg710())
    , afs_(bus_, profile(cfg), CPU_HZ)
    , lw_(afs_, profile(cfg))
    , ass_(bus_)
{
    zre_.attachToBus(bus_);          // CTC 80H, PIO 84H
    speicher_.attachToBus(bus_);     // E8H–EBH
    // K5122 an 10H–18H.  Fehlte bis AP-P1d: das ROM las Tor B (12H) als FFH (/TO nie 0),
    // fuhr je Laufwerk 256 Schritte ins Leere und meldete Status C0H.
    bus_.registerIO(&afs_, 0x10, 9);
    bus_.registerIO(&ass_, 0x50, 16);
    // Speicherweg der CPU: Speicherverwaltung; ZRE-Fenster und VRAM gehen an die Karten.
    zre_.setSpeicherweg([this](uint16_t a) { return speicher_.memRead(a); },
                        [this](uint16_t a, uint8_t d) { speicher_.memWrite(a, d); });
    speicher_.setZreWeg([this](uint16_t a) { return zre_.memRead(a); },
                        [this](uint16_t a, uint8_t d) { zre_.memWrite(a, d); });
    speicher_.setVramWeg([this](uint16_t a) { return screen_.memRead(a); },
                         [this](uint16_t a, uint8_t d) { screen_.memWrite(a, d); });
    // K5122 auf /WAIT (keine ZVE2); Marken-FF: PRG 710 low-aktiv, 710-1 high-aktiv (§4a.1).
    afs_.setSynchronisation(K5122::Synchronisation::Wait);
    afs_.setMkeLowAktiv(variante_ == Config::Variante::Prg710);
    // Marken-FF an jedem Sync-Byte: das 710-ROM schlägt das FF in einer engen Schleife
    // neu an (02DDH, AP-P1d); beide Varianten tragen dieselbe K5122.
    afs_.setMkeJedesSyncByte(true);
    // Interruptkette (vorläufig [?], §3.1/AP-P1c): K5122 → K2521 (CTC, PIO) → K8025.
    bus_.setInterruptChain({&afs_, &zre_, &ass_});
    zre_.cpu().abortBeforeExecute = [this] { return stop_.load(std::memory_order_relaxed); };

    // ZC/TO0 der K2521-CTC als Baudtakt der K8025-CTC wie am A5120 (Koppelbus X2 [?]);
    // die Kaskade TO2 → CLK3 steckt in der K2521 selbst.
    zre_.setZCTOCallback([this](int ch, bool lvl) {
        if (ch == 0)
            for (int i = 0; i < 4; ++i) ass_.ctcA34().clkTrg(i, lvl);
    });
    ass_.setzeZreTakt([this] { return zre_.ctc().teilerTakte(0); });
    for (int i = 0; i < K8025::SchnittstellenAnzahl; ++i)
        hub_.registriere(ass_.anschluss(static_cast<K8025::Schnittstelle>(i)));
}

std::vector<k1520::serial::SerialAnschluss*> Prg710Machine::serielleAnschluesse()
{
    std::vector<k1520::serial::SerialAnschluss*> v;
    for (int i = 0; i < K8025::SchnittstellenAnzahl; ++i)
        v.push_back(&ass_.anschluss(static_cast<K8025::Schnittstelle>(i)));
    return v;
}

void Prg710Machine::resetHardware()
{
    stop_.store(false);
    afs_.flushDisks();
    zre_.reset();            // CPU, CTC, PIO
    speicher_.reset();       // E8H–EBH: Abbildung aus
    afs_.reset();
    ass_.reset();
    hub_.gastZurueckgesetzt();
    serial_naechst_ = 0;
    bus_.clearNMI();
    bus_.releaseINT();
    bus_.releaseWAIT();
    bus_.markIntDirty();
    prev_afs_int_ = false;
    screen_.clearScreen();
}

void Prg710Machine::powerOn()
{
    zre_.powerOn(0x00);
    speicher_.powerOn(0x00);
    resetHardware();
    LOG_INFO("PRG710", "Netz ein: Abbildung aus, ROM bei 0000H");
}

void Prg710Machine::reset()
{
    resetHardware();
    LOG_INFO("PRG710", "Reset");
}

void Prg710Machine::keyPress(uint32_t k, bool shift, bool ctrl)
{
    std::lock_guard<std::mutex> lk(tasten_sperre_);
    tasten_.push_back({k, shift, ctrl, true});
}

void Prg710Machine::keyRelease(uint32_t k)
{
    std::lock_guard<std::mutex> lk(tasten_sperre_);
    tasten_.push_back({k, false, false, false});
}

int Prg710Machine::run(int max_cycles)
{
    if (nmi_taster_.exchange(false, std::memory_order_relaxed)) {
        bus_.assertNMI();
        LOG_INFO("PRG710", "NMI");
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
        dirty |= ass_.clockTick(used);
        if (total_cycles_ >= serial_naechst_) {
            serial_naechst_ = hub_.takt(total_cycles_);
            dirty |= ass_.nimmSeriellGeaendert();
        }
        if (dirty) bus_.markIntDirty();
    }
    lw_.autoFlush(total_cycles_);
    return max_cycles - remaining;
}
