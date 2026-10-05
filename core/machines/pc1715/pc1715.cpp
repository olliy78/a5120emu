/**
 * @file pc1715.cpp
 * @brief PC 1715 — Verdrahtung und Laufschleife (eine CPU).
 * @see pc1715.h, doc/design/21_pc1715.md §8.1; Vorbild core/machines/prg710/prg710.cpp
 */

#include "core/machines/pc1715/pc1715.h"
#include "core/cards/pc1715w_speicher/pc1715w_bild.h"
#include "core/cards/pc1715w_speicher/pc1715w_speicher.h"
#include "core/logger.h"
#include "core/primitives/upd765.h"
#include "core/primitives/z80_dma.h"

namespace {
std::array<DriveProfile, 4> profile(const Pc1715Machine::Config& cfg) {
    return { builtinDriveProfile(cfg.laufwerke[0]), builtinDriveProfile(cfg.laufwerke[1]),
             builtinDriveProfile(cfg.laufwerke[2]), builtinDriveProfile(cfg.laufwerke[3]) };
}
}  // namespace

Pc1715Machine::Config Pc1715Machine::pruefe(const Config& cfg)
{
    if (cfg.variante == Config::Variante::Pc1715W && cfg.bild != Pc1715Zre::Bildschirm::K7222)
        throw std::runtime_error("PC 1715W: der Bildschirm ist fest 80 x 24 (K7221 gibt es dort nicht)");
    return cfg;
}

// ─── PC 1715W: E/A-Karte (doc/pc1715/pc1715w_hardware.md §1, §4) ─────────────
//
// Was weder ZRE-Karte noch Speicher/Bild belegen: U8272 1CH–1FH, KRFD 20H–23H, MOS 28H–2BH,
// KON 34H–37H, dazu die von keiner Software benutzten /SR 38H–3BH und /RST 3CH–3FH
// (Schreiben angenommen und protokolliert, Lesen FFH).  Die DMA steht selbst am Bus (00H–03H),
// ebenso CTC2 (04H–07H).
struct Pc1715Machine::W : public BusDevice {
    W() : bild(spk), dma("PC1715W-DMA"), fdc(Upd765::Config{CPU_HZ_W, 250, 0}), kon(0xFF) {}

    Pc1715wSpeicher spk;
    Pc1715wBild     bild;
    Z80Dma          dma;
    Z80CTC          ctc2{"PC1715W-CTC2"};
    Upd765          fdc;
    uint8_t krfd = 0x30;   ///< nach /RESET [?]: das Latch DS8282 hat keinen Rücksetzeingang;
                           ///< S550 schreibt als Erstes 30H — bis dahin FDC im Reset
    uint8_t mos  = 0x00;
    uint8_t kon;           ///< DIP S8, Vorgabe FFH (Bit 0 = 1: 8″-Formate erlaubt)
    bool    dma_ende = false;   ///< Ende-Ausgang der DMA (Blockende erreicht)

    /// TC des U8272 = DMA-Blockende ∧ KRFD Bit 7 (A33/A26/A109, §4.1).
    void tcNeu() { fdc.setTC(dma_ende && (krfd & 0x80)); }

    uint8_t ioRead(uint8_t port) override {
        if (uint8_t(port - 0x1C) < 4) return fdc.read((port & 1) != 0);
        if (uint8_t(port - 0x34) < 4) return kon;
        return 0xFF;
    }
    void ioWrite(uint8_t port, uint8_t d) override {
        if (uint8_t(port - 0x1C) < 4) { fdc.write((port & 1) != 0, d); return; }
        if (uint8_t(port - 0x20) < 4) {               // KRFD
            if ((d ^ krfd) & 0x40) LOG_DEBUG("PC1715W", "U8272 %s", (d & 0x40) ? "läuft" : "im Reset");
            krfd = d;
            fdc.setReset((d & 0x40) == 0);
            tcNeu();
            return;
        }
        if (uint8_t(port - 0x28) < 4) { mos = d; return; }   // Motor LW n = Bit 4+n
        LOG_DEBUG("PC1715W", "OUT %02XH,%02XH ohne Wirkung", port, d);
    }
    const char* deviceName() const override { return "PC1715W-E/A"; }

    void attachToBus(K1520Bus& bus) {
        bus.registerIO(this, 0x1C, 4);
        bus.registerIO(this, 0x20, 4);
        bus.registerIO(this, 0x28, 4);
        bus.registerIO(this, 0x34, 4);
        bus.registerIO(this, 0x38, 8);
        bus.registerIO(&dma, 0x00, 4);
        bus.registerIO(&ctc2, 0x04, 4);
        spk.attachToBus(bus);
        bild.attachToBus(bus);
    }
};

Pc1715Machine::Pc1715Machine() : Pc1715Machine(Config{}) {}
Pc1715Machine::~Pc1715Machine() = default;

namespace {
Pc1715Zre::Config zreConfig(const Pc1715Machine::Config& cfg)
{
    Pc1715Zre::Config z{cfg.bild, cfg.zeichensatz};
    z.zg_satz = cfg.zg_satz;
    z.w = cfg.variante == Pc1715Machine::Config::Variante::Pc1715W;
    return z;
}
}  // namespace

Pc1715Machine::Pc1715Machine(const Config& cfg)
    : variante_(pruefe(cfg).variante)
    , zre_(bus_, zreConfig(cfg))
    , afs_(bus_, profile(cfg), cpuHz())
    , lw_(afs_, profile(cfg))
    , kbd_(cfg.tastatur, Tastatur1715::TAKT_TASTATUR_HZ,
           cfg.variante == Config::Variante::Pc1715W ? CPU_HZ_W : Tastatur1715::TAKT_RECHNER_HZ)
    , hub_(cfg.variante == Config::Variante::Pc1715W ? uint64_t(CPU_HZ_W) : k1520::serial::PHI_NENN)
{
    zre_.attachToBus(bus_);
    // Tastatur → SIO0 Kanal A (§3.7): fertiges Zeichen am Stoppbit; der Empfänger nimmt es in
    // seinen 3-Byte-FIFO, der Empfangsinterrupt wird neu bewertet.  Aufruf nur im Lauffaden.
    kbd_.byteOut = [this](uint8_t b) {
        zre_.sio().channelA().rxByte(b);
        bus_.markIntDirty();
    };
    if (variante_ == Config::Variante::Pc1715W) {
        bauW();
    } else {
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
    }
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

// ─── PC 1715W: Verdrahtung ───────────────────────────────────────────────────

void Pc1715Machine::bauW()
{
    w_ = std::make_unique<W>();
    W& w = *w_;
    w.attachToBus(bus_);

    // Speicherweg der CPU: Lesezyklus → Lesebank, Schreibzyklus → Schreibbank (BR 24H).
    Z80& cpu = zre_.cpu();
    cpu.readByte  = [this](uint16_t a) { return w_->spk.read(a); };
    cpu.writeByte = [this](uint16_t a, uint8_t d) { w_->spk.write(a, d); };

    // UA858: eigene Zyklen über den Bus — dieselbe Bankabbildung wie die CPU (§2.5).  Die
    // Adressen 40H/41H sind der DACK-Zugang zum U8272 (AB0 = 0 MSR, 1 Daten); nur die DMA
    // benutzt sie.  Ein DMA-Schreiben auf 40H bleibt wirkungslos [?] (S550-Blindlauf, §4.5).
    w.dma.memRead   = [this](uint16_t a) { return bus_.memRead(a); };
    w.dma.memWrite  = [this](uint16_t a, uint8_t d) { bus_.memWrite(a, d); };
    w.dma.portRead  = [this](uint16_t a) -> uint8_t {
        const uint8_t p = static_cast<uint8_t>(a);
        if (p == 0x41) return w_->fdc.dmaRead();
        if (p == 0x40) return w_->fdc.readMsr();
        return bus_.ioRead(p);
    };
    w.dma.portWrite = [this](uint16_t a, uint8_t d) {
        const uint8_t p = static_cast<uint8_t>(a);
        if (p == 0x41) { w_->fdc.dmaWrite(d); return; }
        if (p == 0x40) return;
        bus_.ioWrite(p, d);
    };
    // TC = Blockende ∧ KRFD Bit 7 — die DMA meldet das Ende NACH dem letzten Byte (§4.1).
    w.dma.blockEnde = [this](bool l) { w_->dma_ende = l; w_->tcNeu(); };
    // DRQ des U8272 → (A34, invertiert) → /RDY des UA858 (WR5 82H: low-aktiv).
    w.fdc.onDrq = [this](bool drq) { w_->dma.setReady(!drq); };
    w.dma.setReady(true);
    for (int u = 0; u < 4; ++u) w.fdc.setDrive(u, &afs_.drive(u));

    // CTC2: ZC/TO1 → C/TRG2 (Uhr: K1 100 Hz → K2 1 Hz, [BIOS] A785–A79F).
    w.ctc2.setZCTOCallback([this](int ch, bool lvl) { if (ch == 1) w_->ctc2.clkTrg(2, lvl); });
    w.ctc2.setzeEingangsQuelle(2, [this] { return w_->ctc2.teilerTakte(1); });

    // Interruptkette DMA → CTC2 → SIO0 (Plan, [MAME]; Scan nicht eindeutig [?]).
    bus_.setInterruptChain({&w.dma, &w.ctc2, &zre_});
}

Pc1715wSpeicher* Pc1715Machine::speicherW() { return w_ ? &w_->spk : nullptr; }
Pc1715wBild*     Pc1715Machine::bildW()     { return w_ ? &w_->bild : nullptr; }
Z80Dma*          Pc1715Machine::dmaW()      { return w_ ? &w_->dma : nullptr; }
Upd765*          Pc1715Machine::fdcW()      { return w_ ? &w_->fdc : nullptr; }
Z80CTC*          Pc1715Machine::ctc2W()     { return w_ ? &w_->ctc2 : nullptr; }
uint8_t          Pc1715Machine::krfdW() const { return w_ ? w_->krfd : 0xFF; }
uint8_t          Pc1715Machine::mosW() const  { return w_ ? w_->mos : 0x00; }

bool Pc1715Machine::isMotorOn(int d) const
{
    if (!w_) return lw_.isMotorOn(d);
    return d >= 0 && d < 4 && ((w_->mos >> (4 + d)) & 1);
}

bool Pc1715Machine::isDiskLedOn(int d) const
{
    if (!w_) return lw_.isDiskLedOn(d);
    return d >= 0 && d < 4 && (isMotorOn(d) || w_->fdc.unitBusy(d));
}

// ─── Bild und Speicher je Variante ───────────────────────────────────────────

const uint8_t* Pc1715Machine::framebuffer() const { return w_ ? w_->bild.framebuffer() : zre_.framebuffer(); }
int  Pc1715Machine::fbWidth() const  { return w_ ? w_->bild.fbWidth() : zre_.fbWidth(); }
int  Pc1715Machine::fbHeight() const { return w_ ? w_->bild.fbHeight() : zre_.fbHeight(); }
bool Pc1715Machine::fbDirty() const  { return w_ ? w_->bild.fbDirty() : zre_.fbDirty(); }
void Pc1715Machine::fbClearDirty()
{
    if (w_) w_->bild.fbClearDirty();
    else    zre_.fbClearDirty();
}
uint8_t Pc1715Machine::screenChar(int col, int row) const
{
    return w_ ? w_->bild.screenChar(col, row) : zre_.screenChar(col, row);
}
uint8_t Pc1715Machine::memReadDebug(uint16_t a)
{
    return w_ ? w_->spk.read(a) : zre_.memRead(a);
}
void Pc1715Machine::memWriteDebug(uint16_t a, uint8_t d)
{
    if (w_) w_->spk.write(a, d);
    else    zre_.memWrite(a, d);
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
    if (w_) {                // 1715W: BR = 00H (S550 sichtbar), DMA, CTC2, CRT, U8272 im Reset
        W& w = *w_;
        w.spk.reset();
        w.bild.reset();
        w.dma.reset();
        w.ctc2.reset();
        w.krfd = 0x30;
        w.mos = 0x00;
        w.dma_ende = false;
        w.fdc.setReset(true);
        w.fdc.setTC(false);
        w.dma.setReady(!w.fdc.drq());
    }
    k6022Reset();            // K6022 (gesteckt?): PIOs; Band und Stanzband bleiben
    rafReset();              // RAF (gesteckt?): nur das Latch sperrt, der Inhalt bleibt
    hub_.gastZurueckgesetzt();   // XOFF-/RTS-Halt des alten Gastes gilt nicht weiter
    serial_naechst_ = 0;
    bus_.clearNMI();
    bus_.releaseINT();
    bus_.releaseWAIT();
    bus_.markIntDirty();
    prev_afs_int_ = false;
    bild_naechst_ = total_cycles_ + frameTakte();
}

void Pc1715Machine::powerOn()
{
    zre_.powerOn(0x00);
    if (w_) w_->spk.powerOn(0x00);
    rafPowerOn();            // RAF ohne Stand-by-Pufferung: Inhalt weg (Entwurf 22 §3.4)
    resetHardware();
    LOG_INFO("PC1715", w_ ? "Netz ein (1715W): BR = 00H, S550 bei 0000H"
                          : "Netz ein: ROM-Overlay S502 bei 0000H");
}

void Pc1715Machine::reset()
{
    bestueckungAbschliessen();
    resetHardware();
    LOG_INFO("PC1715", "Reset");
}

// ─── Tastatur ────────────────────────────────────────────────────────────────
namespace {
// Umschalter der Matrix (doc/pc1715/tastatur.md §4): CTRL (8,0), Shift links (8,1).
constexpr int CTRL_SP = 8, CTRL_ZE = 0, SHIFT_SP = 8, SHIFT_ZE = 1;
// Durchläufe: Vorlauf, bis das ROM einen einzeln gedrückten Umschalter erkannt hat (4);
// Mindestzeit, die eine Taste unten bleibt (Entprellung = 3 Durchläufe + Phase: 6), und die Pause
// danach, in der das ROM sie als losgelassen verbucht (3).  Gilt für die Warteschlange, nicht für
// den Anwender: ein kurzer Anschlag der Oberfläche wird so gestreckt statt verschluckt.
constexpr uint64_t UMSCHALT_VORLAUF = 4, HALTE_MIN = 6, PAUSE_MIN = 3;

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

// Abfragedurchläufe der Tastatur-CPU in Rechnertakten (5865 Takte bei ≈ 700 kHz je Durchlauf);
// der Rechnertakt ist der der Variante.
uint64_t Pc1715Machine::durchlaeufe(uint64_t n) const
{
    const uint64_t rechner = w_ ? CPU_HZ_W : Tastatur1715::TAKT_RECHNER_HZ;
    return n * Tastatur1715::DURCHLAUF_TAKTE * rechner / Tastatur1715::TAKT_TASTATUR_HZ;
}

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
            tasten_frei_ab_ = total_cycles_ + durchlaeufe(PAUSE_MIN);
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
            tasten_frei_ab_ = total_cycles_ + durchlaeufe(HALTE_MIN);
            continue;
        }
        Tastatur1715::Taste t;
        const char c = zeichenFuer(e.code, e.ctrl);
        if (!c || !kbd_.tasteFuerDiese(c, t) || gehalten_.count(e.code)) {
            arbeit_.pop_front();     // keine Taste dafür / schon gedrückt (Wiederholung der Oberfläche)
            continue;
        }
        const bool umschalten = t.shift || e.ctrl;
        if (umschalten && !umschalter_gesetzt_) {
            // Erst die Umschalter allein; die Zeichentaste folgt nach dem Vorlauf.
            if (t.shift) kbd_.press(SHIFT_SP, SHIFT_ZE);
            if (e.ctrl)  kbd_.press(CTRL_SP, CTRL_ZE);
            umschalter_gesetzt_ = true;
            tasten_frei_ab_ = total_cycles_ + durchlaeufe(UMSCHALT_VORLAUF);
            return;
        }
        umschalter_gesetzt_ = false;
        kbd_.press(t.spalte, t.zeile);
        gehalten_[e.code] = {t.spalte, t.zeile, t.shift, e.ctrl};
        arbeit_.pop_front();
        tasten_frei_ab_ = total_cycles_ + durchlaeufe(HALTE_MIN);
    }
}

int Pc1715Machine::run(int max_cycles)
{
    bestueckungAbschliessen();
    if (w_) return runW(max_cycles);
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
        dirty |= k6022Takt(used);   // Lochstreifen (gesteckt?)
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
    k6022AutoFlush();   // Stanzdatei nach der Stanzpause (Entwurf 23 §5)
    return max_cycles - remaining;
}

// ─── Laufschleife PC 1715W ───────────────────────────────────────────────────
//
// Solange der UA858 den Bus fordert (`busRequest`), steht die CPU; je Schritt bewegt die DMA
// ein Byte über den Bus (Bankregeln von `Pc1715wSpeicher`).  Im Continuous-Betrieb hält sie den
// Bus auch ohne RDY — dann vergeht nur Zeit.  Der U8272 bekommt die Takte über tick().
int Pc1715Machine::runW(int max_cycles)
{
    tastenAbgeben();
    if (nmi_taster_.exchange(false, std::memory_order_relaxed)) {
        bus_.assertNMI();
        LOG_INFO("PC1715", "NMI");
    }
    W& w = *w_;
    Z80& cpu = zre_.cpu();
    const uint64_t frame_takte = frameTakte();
    int remaining = max_cycles;
    while (remaining > 0 && !stop_.load(std::memory_order_relaxed)) {
        k1520::logging::Logger::instance().update(total_cycles_, cpu.PC, 0);
        int used;
        if (w.dma.busRequest()) {
            used = w.dma.step();
            if (used == 0) used = 4;                 // Bus gehalten, kein RDY
            cpu.cycles += static_cast<uint64_t>(used);
            bus_.markIntDirty();                     // Blockende kann einen Interrupt melden
        } else {
            bus_.updateInterruptChain();
            if (bus_.isINT() && cpu.IFF1) {
                const uint8_t vec = bus_.interruptAcknowledge();
                cpu.interrupt(vec);
            }
            if (bus_.isNMI()) {
                cpu.nmi();
                bus_.clearNMI();
            }
            used = cpu.step();
            if (used == 0 && stop_.load(std::memory_order_relaxed)) break;   // Debugger-Halt
        }
        remaining     -= used;
        total_cycles_ += used;

        w.fdc.tick(static_cast<uint32_t>(used));
        if (!arbeit_.empty()) tastenVerarbeiten();
        kbd_rest_ += used;
        if (kbd_rest_ >= 32) { kbd_.run(kbd_rest_); kbd_rest_ = 0; }
        bool dirty = zre_.clockTick(used);
        dirty |= w.ctc2.clockTick(used);
        dirty |= k6022Takt(used);   // Lochstreifen (gesteckt?)
        if (total_cycles_ >= serial_naechst_) {
            serial_naechst_ = hub_.takt(total_cycles_);
            dirty |= zre_.nimmSeriellGeaendert();
        }
        while (total_cycles_ >= bild_naechst_) {
            w.bild.frame();
            bild_naechst_ += frame_takte;
        }
        if (dirty) bus_.markIntDirty();
    }
    lw_.autoFlush(total_cycles_);
    k6022AutoFlush();   // Stanzdatei nach der Stanzpause (Entwurf 23 §5)
    return max_cycles - remaining;
}
