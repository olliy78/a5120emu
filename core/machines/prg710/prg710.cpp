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
    // ADA K6022 (E0H–E7H, Lochband über SIF1000) und ASS 590069 (C4H–C7H, Fernschreiber):
    // in beiden Varianten — die Treiber (`PTAPE.6022`, `B17x72FS`) liegen auf den Disketten
    // beider Geräte (AP-P8).  Kein Treiber und kein ROM fragt die Ports beim Start ab.
    k6022_.attachToBus(bus_);
    fs_.attachToBus(bus_);
    // Tastatur: 710 = K7609 am 8279 der ATP (C8H–C9H, D0H–D3H); 710-1 = K7672 an A32-B
    // (kein C8H/C9H, §3.9, resident.md §6).  Ohne Tastatur bleibt der Anschluss leer
    // (710: der 8279 fehlt → ROM liest FFH; 710-1: Kabel gezogen).
    if (variante_ == Config::Variante::Prg710) {
        if (cfg.tastatur) { atp_.attachTastatur(bus_); k7609_.connect(&atp_.kbc()); }
    } else if (cfg.tastatur) {
        k7672_.connect(ass_.sioA32(), 1);
    }
    // EPROMmer (AP-P7b, doc/prg710/eprommer.md): PIO D0H–D3H + Steuerregister D4H; der Typ
    // (1 KB U555 / 2 KB U2716) kommt von ZRE-PIO Port A Bit 0 (PROG 60E7).
    if (cfg.eprommer) {
        atp_.attachEprommer(bus_);
        atp_.eprommer().setTypwahl([this] { return zre_.pio().portARead(); });
        atp_.eprommer().setZeit([this] { return total_cycles_; });
    }
    // Speicherweg der CPU: Speicherverwaltung; ZRE-Fenster und VRAM gehen an die Karten.
    // (mem_trace_: Beobachter des Debuggers — Speicher der CPU geht nicht über den Systembus.)
    zre_.setSpeicherweg(
        [this](uint16_t a) {
            const uint8_t v = speicher_.memRead(a);
            if (mem_trace_) mem_trace_(false, true, a, v);
            return v;
        },
        [this](uint16_t a, uint8_t d) {
            speicher_.memWrite(a, d);
            if (mem_trace_) mem_trace_(false, false, a, d);
        });
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
    // Unformatierte Spur/Seite = Rauschen mit Scheinmarken (AP-P3b): ROM und Resident
    // warten ohne Zeitablauf auf eine Marke; am Gerät beendet erst das Rauschen das Warten.
    afs_.setRauschenAufLeererSpur(true);
    // Interruptkette (vorläufig [?], §3.1/AP-P1c): K5122 → K2521 (CTC, PIO) → K8025.
    // AP-P8: K6022 (Stanzer vor Leser) und 590069 dahinter — Stellung in der Kette [?].
    bus_.setInterruptChain({&afs_, &zre_, &ass_, &k6022_.pioStanzer(), &k6022_.pioLeser(),
                            &fs_.sio(), &fs_.ctc()});
    zre_.cpu().abortBeforeExecute = [this] { return stop_.load(std::memory_order_relaxed); };

    // ZC/TO0 der K2521-CTC als Baudtakt der K8025-CTC wie am A5120 (Koppelbus X2 [?]);
    // die Kaskade TO2 → CLK3 steckt in der K2521 selbst.
    zre_.setZCTOCallback([this](int ch, bool lvl) {
        if (ch == 0)
            for (int i = 0; i < 4; ++i) ass_.ctcA34().clkTrg(i, lvl);
    });
    ass_.setzeZreTakt([this] { return zre_.ctc().teilerTakte(0); });
    // Am 710-1 ist A32-B die Tastatur (fest verdrahtet, nicht nach außen); AP-P4 benennt neu.
    // Namen nach der Gerätebeschriftung (DRUCK.DOK, §3.7/§3.9), Reihenfolge = C-ABI-Index.
    // A33-B (52H/53H) ist am PRG unbelegt [?] und geht nicht nach außen; am 710-1 trägt
    // A32-B die Tastatur K7672 (9-poliger D-SUB statt IFSS X6).
    ass_.benenne(K8025::DfueV24,   "V.24",                    "X4");
    ass_.benenne(K8025::Drucker,   "IFSS Hauptdrucker",       "X6");
    ass_.benenne(K8025::ZifssA32A, "ZIFSS Zusatzdrucker",     "X5");
    for (auto* a : serielleAnschluesse()) hub_.registriere(*a);
    // Der V.24-Treiber taktet über CTC A34 Kanal 2 (5AH, DRUCK.DOK §1) = Taktquelle 1
    // („CTC A34 K2“); die A5120-Vorgabe 0 (ZRE-CTC K0, W1:7) ist hier nicht die Belegung.
    k1520::serial::SerialKonfig k = hub_.konfig(0);
    k.taktquelle = 1;
    hub_.konfigurieren(0, k);
}

std::vector<k1520::serial::SerialAnschluss*> Prg710Machine::serielleAnschluesse()
{
    std::vector<k1520::serial::SerialAnschluss*> v;
    v.push_back(&ass_.anschluss(K8025::DfueV24));
    if (variante_ == Config::Variante::Prg710) v.push_back(&ass_.anschluss(K8025::Drucker));
    v.push_back(&ass_.anschluss(K8025::ZifssA32A));
    v.push_back(&fs_.anschluss());   // AP-P8c: hinten, die K8025-Indizes bleiben
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
    atp_.reset();            // 8279: FIFO leer
    k6022_.reset();          // PIOs; Band im Leser und Stanzband bleiben
    fs_.reset();
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
    k7609_.powerOn();        // die Tastatur hat ihr eigenes Netz-Ein, ein /RESET trifft sie nicht
    k7672_.powerOn();        // Selbsttest, KEIN DC1 (wie K8915)
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

void Prg710Machine::tastenAbgeben()
{
    // Erst unter Sperre umhängen, dann ohne Sperre abgeben (wie K8915Machine).
    std::deque<TastenEreignis> jetzt;
    {
        std::lock_guard<std::mutex> lk(tasten_sperre_);
        jetzt.swap(tasten_);
    }
    for (const auto& e : jetzt) {
        if (variante_ == Config::Variante::Prg710) {
            if (e.gedrueckt) k7609_.keyPress(e.code, e.shift, e.ctrl);
            else             k7609_.keyRelease(e.code);
        } else {
            if (e.gedrueckt) k7672_.keyPress(e.code, e.shift, e.ctrl);
            else             k7672_.keyRelease(e.code);
        }
    }
}

int Prg710Machine::run(int max_cycles)
{
    tastenAbgeben();
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
        dirty |= k6022_.clockTick(used);
        dirty |= fs_.clockTick(used);
        if (total_cycles_ >= serial_naechst_) {
            serial_naechst_ = hub_.takt(total_cycles_);
            dirty |= ass_.nimmSeriellGeaendert();
        }
        if (variante_ == Config::Variante::Prg710) k7609_.service();   // 8279 pollt das OS, kein IRQ
        else                                       dirty |= k7672_.service(total_cycles_);
        if (dirty) bus_.markIntDirty();
    }
    lw_.autoFlush(total_cycles_);
    return max_cycles - remaining;
}
