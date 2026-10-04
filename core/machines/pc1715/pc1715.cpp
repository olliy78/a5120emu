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
    // Tastatur → SIO0 Kanal A (§3.7): fertiges Zeichen am Stoppbit; der Empfänger nimmt es in
    // seinen 3-Byte-FIFO, der Empfangsinterrupt wird neu bewertet.  Aufruf nur im Lauffaden.
    kbd_.byteOut = [this](uint8_t b) {
        zre_.sio().channelA().rxByte(b);
        bus_.markIntDirty();
    };
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
    // Schnittstellen nach außen (Entwurf 19): Drucker X4 (SIO0-A Sender), V.24 X5 (SIO0-B);
    // Reihenfolge = C-ABI-Index.
    for (auto* a : serielleAnschluesse()) hub_.registriere(*a);
}

std::vector<k1520::serial::SerialAnschluss*> Pc1715Machine::serielleAnschluesse()
{
    std::vector<k1520::serial::SerialAnschluss*> v;
    for (int k = 0; k < Pc1715Zre::KanalAnzahl; ++k)
        v.push_back(&zre_.anschluss(static_cast<Pc1715Zre::Kanal>(k)));
    return v;
}

void Pc1715Machine::resetHardware()
{
    stop_.store(false);
    afs_.flushDisks();
    gehalten_.clear();
    kbd_.releaseAll();
    kbd_.reset();            // Tastatur-CPU wie beim Einschalten (Finger weg)
    kbd_rest_ = 0;
    tasten_frei_ab_ = 0;
    umschalter_gesetzt_ = false;
    {
        std::lock_guard<std::mutex> lk(tasten_sperre_);
        tasten_.clear();
    }
    arbeit_.clear();
    zre_.reset();            // CPU, CTC, SIO, 8275, ROM-Overlay ein
    afs_.reset();
    hub_.gastZurueckgesetzt();   // XOFF-/RTS-Halt des alten Gastes gilt nicht weiter
    serial_naechst_ = 0;
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

// ─── Tastatur ────────────────────────────────────────────────────────────────
namespace {
// Umschalter der Matrix (doc/pc1715/tastatur.md §4): CTRL (8,0), Shift links (8,1).
constexpr int CTRL_SP = 8, CTRL_ZE = 0, SHIFT_SP = 8, SHIFT_ZE = 1;
// Abfragedurchläufe der Tastatur-CPU in Rechnertakten (5865 Takte bei ≈ 700 kHz je Durchlauf).
constexpr uint64_t durchlaeufe(uint64_t n)
{
    return n * Tastatur1715::DURCHLAUF_TAKTE * Tastatur1715::TAKT_RECHNER_HZ / Tastatur1715::TAKT_TASTATUR_HZ;
}
// Vorlauf, bis das ROM einen einzeln gedrückten Umschalter erkannt hat.
constexpr uint64_t UMSCHALT_VORLAUF = durchlaeufe(4);
// Mindestzeit, die eine Taste unten bleibt (Entprellung = 3 Durchläufe + Phase), und die Pause
// danach, in der das ROM sie als losgelassen verbucht.  Gilt für die Warteschlange, nicht für den
// Anwender: ein kurzer Anschlag der Oberfläche wird so gestreckt statt verschluckt.
constexpr uint64_t HALTE_MIN  = durchlaeufe(6);
constexpr uint64_t PAUSE_MIN  = durchlaeufe(3);

/// Hostcode → Zeichen (0 = keins); Ctrl-Codes 1…26 werden zum Kleinbuchstaben.
char zeichenFuer(uint32_t k, bool ctrl)
{
    switch (k) {
        case 0x01000004: case 0x01000005: return '\r';   // Return, Enter
        case 0x01000000: return '\x1b';                  // Escape
        case 0x01000001: case '\t': return '\x8d';        // Tab → Taste -> (8DH; CP/A macht Tab 09H daraus)
        case 0x01000003: return '\x7f';                  // Backspace → DEL-Taste
        default: break;
    }
    if (k >= 0x80) return 0;
    if (ctrl && k >= 1 && k <= 26 && k != 13) return char('a' + k - 1);
    return static_cast<char>(k);
}
}  // namespace

void Pc1715Machine::keyPress(uint32_t k, bool, bool ctrl)
{
    std::lock_guard<std::mutex> lk(tasten_sperre_);
    tasten_.push_back({k, ctrl, true});
}

void Pc1715Machine::keyRelease(uint32_t k)
{
    std::lock_guard<std::mutex> lk(tasten_sperre_);
    tasten_.push_back({k, false, false});
}

void Pc1715Machine::tastenAbgeben()
{
    std::lock_guard<std::mutex> lk(tasten_sperre_);
    for (const auto& e : tasten_) arbeit_.push_back(e);
    tasten_.clear();
}

void Pc1715Machine::tastenVerarbeiten()
{
    while (!arbeit_.empty() && total_cycles_ >= tasten_frei_ab_) {
        const TastenEreignis e = arbeit_.front();
        const bool physisch = (e.code & ~0x7Fu) == QK_TASTE_BASE;
        const int pos = int(e.code & 0x7F);

        if (!e.gedrueckt) {
            arbeit_.pop_front();
            tasten_frei_ab_ = total_cycles_ + PAUSE_MIN;
            if (physisch) {
                if (pos < Tastatur1715::SPALTEN * Tastatur1715::ZEILEN)
                    kbd_.release(pos / Tastatur1715::ZEILEN, pos % Tastatur1715::ZEILEN);
            } else if (auto it = gehalten_.find(e.code); it != gehalten_.end()) {
                const Gehalten g = it->second;
                kbd_.release(g.sp, g.ze);
                if (g.shift) kbd_.release(SHIFT_SP, SHIFT_ZE);
                if (g.ctrl)  kbd_.release(CTRL_SP, CTRL_ZE);
                gehalten_.erase(it);
            }
            continue;
        }
        if (physisch) {
            arbeit_.pop_front();
            if (pos < Tastatur1715::SPALTEN * Tastatur1715::ZEILEN)
                kbd_.press(pos / Tastatur1715::ZEILEN, pos % Tastatur1715::ZEILEN);
            tasten_frei_ab_ = total_cycles_ + HALTE_MIN;
            continue;
        }
        Tastatur1715::Taste t;
        const char c = zeichenFuer(e.code, e.ctrl);
        if (!c || !Tastatur1715::tasteFuer(c, t) || gehalten_.count(e.code)) {
            arbeit_.pop_front();     // keine Taste dafür / schon gedrückt (Wiederholung der Oberfläche)
            continue;
        }
        const bool umschalten = t.shift || e.ctrl;
        if (umschalten && !umschalter_gesetzt_) {
            // Erst die Umschalter allein; die Zeichentaste folgt nach dem Vorlauf.
            if (t.shift) kbd_.press(SHIFT_SP, SHIFT_ZE);
            if (e.ctrl)  kbd_.press(CTRL_SP, CTRL_ZE);
            umschalter_gesetzt_ = true;
            tasten_frei_ab_ = total_cycles_ + UMSCHALT_VORLAUF;
            return;
        }
        umschalter_gesetzt_ = false;
        kbd_.press(t.spalte, t.zeile);
        gehalten_[e.code] = {t.spalte, t.zeile, t.shift, e.ctrl};
        arbeit_.pop_front();
        tasten_frei_ab_ = total_cycles_ + HALTE_MIN;
    }
}

int Pc1715Machine::run(int max_cycles)
{
    tastenAbgeben();
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
        if (!arbeit_.empty()) tastenVerarbeiten();
        // Tastatur-CPU in Paketen von ≥ 32 Takten nachführen (Rahmenende ≤ 32 Takte verspätet).
        kbd_rest_ += used;
        if (kbd_rest_ >= 32) { kbd_.run(kbd_rest_); kbd_rest_ = 0; }
        bool dirty = zre_.clockTick(used);
        // Schnittstellen nach außen: der Wandler arbeitet nur alle 1/16 Zeichenzeit.
        if (total_cycles_ >= serial_naechst_) {
            serial_naechst_ = hub_.takt(total_cycles_);
            dirty |= zre_.nimmSeriellGeaendert();
        }
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
