/**
 * @file k8915.cpp
 * @brief K8915 V3 — Verdrahtung und Laufschleife (eine CPU, keine ZVE2).
 * @see k8915.h, doc/design/16_k8915.md §7.1, §8a AP-E1
 */

#include "core/machines/k8915/k8915.h"
#include "core/logger.h"

K8915Machine::K8915Machine() : K8915Machine(Config{}) {}

namespace {
std::array<DriveProfile, 4> profile(const K8915Machine::Config& cfg) {
    return { builtinDriveProfile(cfg.laufwerke[0]), builtinDriveProfile(cfg.laufwerke[1]),
             builtinDriveProfile(cfg.laufwerke[2]), builtinDriveProfile(cfg.laufwerke[3]) };
}
}  // namespace

K8915Machine::K8915Machine(const Config& cfg)
    : zre_(bus_)
    , ats_()
    , screen_(bus_, K7024::A5120Config::forK8915())   // registriert VRAM 1000H–17FFH
    , afs_(bus_, profile(cfg), CPU_HZ)
    , lw_(afs_, profile(cfg))
    , pruefstecker_(cfg.pruefstecker)
{
    zre_.attachToBus(bus_);
    ats_.attachToBus(bus_);
    // K5122 062-8390 auf /WAIT gebrückt (am Gerät abgelesen, §3.4): keine ZVE2.
    afs_.setSynchronisation(K5122::Synchronisation::Wait);
    bus_.registerIO(&afs_, 0x10, 9);
    if (cfg.tastatur) kbd_.connect(ats_.sio2(), 1);   // sonst: Kabel gezogen
    // Interruptkette nach der Platzfolge (§6.4 [?]): K5122 → ZRE-CTC → ATS.
    bus_.setInterruptChain({&afs_, &zre_, &ats_});
    zre_.cpu().abortBeforeExecute = [this] { return stop_.load(std::memory_order_relaxed); };
    // Schnittstellen nach außen (Entwurf 19 §3.2): Reihenfolge = Stecker X3, X4, X5
    // (= Wert von K7028::Kanal, AP-S12).
    for (int k = 0; k < K7028::KanalAnzahl; ++k) {
        const int i = hub_.registriere(ats_.anschluss(static_cast<K7028::Kanal>(k)));
        k1520::serial::SerialKonfig c = hub_.konfig(i);
        c.loop = pruefstecker_;   // Prüfstecker = Rx/Tx-Loop (§6.5)
        hub_.konfigurieren(i, c);
    }
}

std::vector<k1520::serial::SerialAnschluss*> K8915Machine::serielleAnschluesse()
{
    std::vector<k1520::serial::SerialAnschluss*> v;
    for (int k = 0; k < K7028::KanalAnzahl; ++k)
        v.push_back(&ats_.anschluss(static_cast<K7028::Kanal>(k)));
    return v;
}

void K8915Machine::altRueckruf(K7028::Kanal k, SerialCb cb)
{
    const bool gesetzt = static_cast<bool>(cb);
    ats_.setAbnehmer(k, std::move(cb));
    // Hub-Index = Kanal (Anmeldung in der Reihenfolge von K7028::Kanal).
    k1520::serial::SerialKonfig c = hub_.konfig(k);
    const bool loop = gesetzt ? false : pruefstecker_;
    if (c.loop == loop) return;
    if (loop && k1520::serial::istAktiv(hub_.status(k).zustand)) return;   // Verbindung nicht kappen
    c.loop = loop;
    hub_.konfigurieren(k, c);
}

void K8915Machine::resetHardware()
{
    stop_.store(false);
    afs_.flushDisks();       // das interne Abbild überlebt den Reset, die Datei folgt ihm
    zre_.reset();            // A8H := 00H, CTC, CPU
    afs_.reset();            // K5122: PIOs, Marken-FF; Disketten und Kopfposition bleiben
    ats_.reset();            // SIOs, CTCs, Latch; die Tastatur hat eigenen Takt und Reset
    rafReset();              // RAF (gesteckt?): nur das Latch sperrt, der Inhalt bleibt
    hub_.gastZurueckgesetzt();   // XOFF-/RTS-Halt des alten Gastes gilt nicht weiter
    serial_naechst_ = 0;
    bus_.clearNMI();
    bus_.releaseINT();
    bus_.releaseWAIT();
    bus_.markIntDirty();
    prev_afs_int_ = false;
    screen_.clearScreen();
}

void K8915Machine::powerOn()
{
    zre_.powerOn(0x00);
    kbd_.powerOn();          // Selbsttest der Tastatur, KEIN DC1 (Firmware 000CH)
    rafPowerOn();            // RAF ohne Stand-by-Pufferung: Inhalt weg (Entwurf 22 §3.4)
    resetHardware();
    anzeigenSpiegeln();
    LOG_INFO("K8915", "Netz ein: A8H=00H, Boot-ROM bei 0000H");
}

void K8915Machine::reset()
{
    bestueckungAbschliessen();
    resetHardware();
    anzeigenSpiegeln();
    LOG_INFO("K8915", "Reset");
}

void K8915Machine::keyPress(uint32_t k, bool shift, bool ctrl)
{
    std::lock_guard<std::mutex> lk(tasten_sperre_);
    tasten_.push_back({k, shift, ctrl, true});
}

void K8915Machine::keyRelease(uint32_t k)
{
    std::lock_guard<std::mutex> lk(tasten_sperre_);
    tasten_.push_back({k, false, false, false});
}

void K8915Machine::tastenAbgeben()
{
    // Erst unter Sperre umhängen, dann ohne Sperre abgeben: die K7672 darf beim
    // Einreihen nichts vom Oberflächenfaden aufhalten.
    std::deque<TastenEreignis> jetzt;
    {
        std::lock_guard<std::mutex> lk(tasten_sperre_);
        jetzt.swap(tasten_);
    }
    for (const auto& e : jetzt) {
        if (e.gedrueckt) kbd_.keyPress(e.code, e.shift, e.ctrl);
        else             kbd_.keyRelease(e.code);
    }
}

void K8915Machine::anzeigenSpiegeln()
{
    lampen_.store(ats_.anzeige(), std::memory_order_relaxed);
    leds_.store(kbd_.leds(), std::memory_order_relaxed);
    summer_.store(kbd_.summerZaehler(), std::memory_order_relaxed);
}

int K8915Machine::run(int max_cycles)
{
    bestueckungAbschliessen();
    tastenAbgeben();
    // NMI-Taster: eine Flanke je Druck, zugestellt vor dem ersten Befehl.
    if (nmi_taster_.exchange(false, std::memory_order_relaxed)) {
        bus_.assertNMI();
        LOG_INFO("K8915", "NMI-Taster");
    }
    Z80& cpu = zre_.cpu();
    int remaining = max_cycles;
    while (remaining > 0 && !stop_.load(std::memory_order_relaxed)) {
        k1520::logging::Logger::instance().update(total_cycles_, cpu.PC, 0);
        // Index-Puls und Marken-FF der K5122 entstehen zeitgetrieben in update(), nicht
        // bei einem Portzugriff — die Flanke muss die Kette neu bewerten lassen.
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
        // /WAIT der K5122: die CPU stand, bis das Byte unter dem Kopf war — die Zeit
        // vergeht für alle anderen Bausteine mit (Zeitgeber, SIO, Index).
        if (const int w = bus_.takeWaitCycles(); w > 0) {
            used += w;
            cpu.cycles += static_cast<uint64_t>(w);
        }
        remaining     -= used;
        total_cycles_ += used;

        afs_.update(used);
        bool dirty = zre_.clockTick(used);
        dirty |= ats_.clockTick(used);
        // Schnittstellen nach außen (Entwurf 19 §6): der Wandler arbeitet nur alle
        // 1/16 Zeichenzeit — dazwischen kostet es nur diesen Vergleich.
        if (total_cycles_ >= serial_naechst_) {
            serial_naechst_ = hub_.takt(total_cycles_);
            dirty |= ats_.nimmSeriellGeaendert();
        }
        dirty |= kbd_.service(total_cycles_);
        if (dirty) bus_.markIntDirty();
    }
    lw_.autoFlush(total_cycles_);
    anzeigenSpiegeln();
    return max_cycles - remaining;
}
