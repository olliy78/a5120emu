/**
 * @file k7028.cpp
 * @brief ATS K7028.30 des K8915 — Dekodierung, Interruptkette, Anschlüsse nach außen.
 * @see k7028.h, doc/design/16_k8915.md §3.2, §8a AP-E2
 */

#include "core/cards/k7028/k7028.h"
#include "core/logger.h"
#include "core/serial/sio_format.h"

using k1520::serial::SerialFormat;

// ─── Anschluss je Kanal (Entwurf 19 §3.2, §5.1, AP-S5, AP-S12) ──────────────
//
// Namen und Stecker nach der BESCHRIFTUNG AM GERÄT (Anwender, 2026-10-01; AP-S12):
//   X3 „Drucker/IFSS1"  = SIO1-B — fest steht unabhängig von jeder Beschriftung: der
//                         BIOS-Drucker (LIST) liegt auf SIO1-B (42H/43H, CTC1 K2).
//   X4 „V.24"           = SIO1-A — die einzige Schnittstelle mit Steuerleitungen
//                         (Stecker X4 am Gerät nur „vermutlich", Anwender).
//   X5 „DFÜ/IFSS2"      = SIO2-A — die IFSS-Stromschleife der Karte.
//
// Der Stromlaufplan 1.45.518732.4/04 Blatt 1 (k8915schaltung.pdf S. 11) zählt die
// Stecker der KARTE anders — dort liegt die volle V.24 an X3 und SIO1-B an X4; X5
// stimmt überein.  Die Gerätebeschriftung geht vor (sie ist, was der Anwender sieht);
// der Widerspruch steht in doc/design/16_k8915.md §3.2.  Elektrisch laut Plan:
//   SIO1-A  → Karten-X3: volle V.24, Empfänger D17:01/02 für 104/106/107/109/114/115/
//             125, Treiber D14:03/02 für 103/105/108/113, 111 über X18; Rx-/Tx-Takt
//             über Multiplexer D13:01/02 (DL153) aus CTC1 ZC/TO0 (über D9:02
//             invertiert), ZC/TO1 oder den Schrittakten 114/115 — nachgebildet ist
//             CTC1 K0.  /CTSA und /DCDA laufen NICHT über D13 und über keine
//             Brücke (Entwurf 19 §14.5, ausgewertet 2026-10-01): /DCDA = D17:01 1Y
//             = V109 direkt; /CTSA = D3:02 Pin 11 = NAND(NAND(¬/RTSA, V106), V107),
//             d. h. CTS aktiv ⇔ V107 ∧ (¬RTS ∨ V106) — „Senden erlaubt, wenn die DÜE
//             bereit ist und, falls gesendet werden soll, CTS meldet".  RTS ist der
//             EIGENE Ausgang RTSA der SIO: /CTSA wird deshalb auch bei jedem
//             Schreiben in SIO 1 neu gebildet (@ref K7028::bildeCtsA), nicht nur bei
//             einem Wechsel am Stecker.  Mit Loop/Prüfstecker (RTS→V106, DTR→V107+
//             V109) folgt CTS = DTR.  Polarität der Empfänger P184 angenommen
//             (invertierend wie jeder V.24-Empfänger).
//   SIO1-B  → Karten-X4 (V.24-Pegel, nur 103/104): TxDB über X16:1–3 und D12:02 an
//             D14:01 1A; RxTxCB = CTC1 ZC/TO2 über D9:02 (BIOS: CTC1-K2 = 4AH für den
//             Drucker); CTSB/DCDB nur an Wickelbrücken X15:3/4, DCDB mit R1:02
//             hochgezogen ⇒ beide inaktiv (Brücken offen, wie gezeichnet).  Eine
//             Stromschleife hat die Karte für SIO1-B NICHT — „IFSS1" am Gerät setzt
//             einen Wandler außerhalb der ATS voraus [?].
//   SIO2-A  → Karten-X5 (IFSS-Stromschleife Blatt 2, S. 16, über Q1/Q2; zusätzlich
//             103/104), Takt CTC2 K0 (ROM/BIOS).  Keine Steuerleitungen.
//   (SIO2-B → Karten-X6: Tastatur K7672, Stromschleife SD/ED und 5 P über Si.)
// IFSS-Kanäle ohne Steuerleitungen lassen /CTS und /DCD, wie sie seit AP-S3 sind:
// inaktiv — der Gast sieht dasselbe RR0 wie vorher.

class K7028::Anschluss : public k1520::serial::SerialAnschluss {
public:
    Anschluss(K7028& k, Kanal kanal) : k_(k), kanal_(kanal) {}

    const char* name() const override {
        switch (kanal_) {
            case Sio1B: return "Drucker/IFSS1";
            case Sio1A: return "V.24";
            default:    return "DFÜ/IFSS2";
        }
    }
    const char* stecker() const override {
        switch (kanal_) {
            case Sio1B: return "X3";
            case Sio1A: return "X4";
            default:    return "X5";
        }
    }
    bool v24() const override { return kanal_ == Sio1A; }

    SerialFormat format() const override {
        uint64_t ctc = 0;
        switch (kanal_) {
            case Sio1A: ctc = k_.ctc1_.teilerTakte(0); break;
            case Sio1B: ctc = k_.ctc1_.teilerTakte(2); break;
            default:    ctc = k_.ctc2_.teilerTakte(0); break;
        }
        return k1520::serial::serialFormatAusSio(ch(), ctc);
    }
    bool senderHatZeichen() const override { return ch().senderHatZeichen(); }
    uint8_t senderNimm() override {
        uint8_t b = ch().txGet();
        // Die Leitung trägt nur die programmierten Datenbits (AP-E4c: Fassung „55 K"
        // des BIOS sendet 7 Bit) — Parität selbst bleibt unnachgebildet.
        const uint8_t bits = ch().format().tx_bits;
        if (bits < 8) b &= static_cast<uint8_t>((1u << bits) - 1);
        k_.updateInternalChain();
        k_.seriell_geaendert_ = true;
        if (!belegt_ && abnehmer_) abnehmer_(b);
        return b;
    }
    bool empfaengerFrei() const override { return ch().empfaengerFrei(); }
    void empfange(uint8_t b) override {
        ch().rxByte(b);
        k_.updateInternalChain();
        k_.seriell_geaendert_ = true;
    }
    bool rts() const override { return ch().rts(); }
    bool dtr() const override { return ch().dtr(); }
    void setzeEingaenge(bool cts, bool dsr, bool dcd) override {
        if (kanal_ != Sio1A) return;
        k_.v106_ = cts;
        k_.v107_ = dsr;
        ch().setzeDCD(dcd);   // /DCDA = V109 direkt
        k_.bildeCtsA();       // /CTSA = V107 ∧ (¬RTS ∨ V106)
        k_.updateInternalChain();
        k_.seriell_geaendert_ = true;
    }
    bool breakGesendet() const override { return ch().breakSenden(); }
    void breakEmpfang(bool aktiv) override {
        ch().setzeBreakEmpfang(aktiv);
        k_.updateInternalChain();
        k_.seriell_geaendert_ = true;
    }
    void leitungBelegt(bool belegt) override { belegt_ = belegt; }

    void setAbnehmer(Abnehmer cb) { abnehmer_ = std::move(cb); }
    void einspeisen(uint8_t b) {
        if (belegt_) return;   // Transport/Loop am Stecker: ins Leere (Entwurf 19 §8)
        empfange(b);
    }

private:
    Z80SIO::Channel& ch() const { return k_.kanal(kanal_); }

    K7028&   k_;
    Kanal    kanal_;
    bool     belegt_ = false;
    Abnehmer abnehmer_;
};

K7028::K7028() : K7028(Config{}) {}

K7028::K7028(const Config& cfg) : cfg_(cfg)
{
    for (int i = 0; i < KanalAnzahl; ++i)
        anschluesse_[static_cast<size_t>(i)] =
            std::make_unique<Anschluss>(*this, static_cast<Kanal>(i));
    updateInternalChain();
}

K7028::~K7028() = default;

k1520::serial::SerialAnschluss& K7028::anschluss(Kanal k)
{
    return *anschluesse_[static_cast<size_t>(k)];
}

void K7028::setAbnehmer(Kanal k, Abnehmer cb)
{
    anschluesse_[static_cast<size_t>(k)]->setAbnehmer(std::move(cb));
}

void K7028::attachToBus(K1520Bus& bus)
{
    bus.registerIO(this, cfg_.io_base, 32);
    bus.registerIO(this, cfg_.latch_base, 8);
}

// ─── E/A ─────────────────────────────────────────────────────────────────────
// Relativ zu io_base: AB3/AB4 wählen den Baustein, AB2 fällt weg (Spiegel),
// AB1/AB0 gehen an den Baustein.

uint8_t K7028::ioRead(uint8_t port)
{
    const uint8_t rel = static_cast<uint8_t>(port - cfg_.io_base);
    if (rel >= 32) return 0xFF;   // Latch: nur beschreibbar, offener Bus
    const uint8_t sub = port & 0x03;
    uint8_t r = 0xFF;
    switch ((rel >> 3) & 3) {
        case 0: r = sio1_.ioRead(sub); break;
        case 1: r = ctc1_.ioRead(sub); break;
        case 2: r = sio2_.ioRead(sub); break;
        case 3: r = ctc2_.ioRead(sub); break;
    }
    updateInternalChain();
    return r;
}

void K7028::ioWrite(uint8_t port, uint8_t data)
{
    const uint8_t rel = static_cast<uint8_t>(port - cfg_.io_base);
    if (rel >= 32) {
        latch_ = data;
        LOG_DEBUG("K7028", "Anzeigelatch %02XH := %02X", port, data);
        return;
    }
    const uint8_t sub = port & 0x03;
    switch ((rel >> 3) & 3) {
        case 0: sio1_.ioWrite(sub, data); bildeCtsA(); break;   // WR5 kann RTSA ändern
        case 1: ctc1_.ioWrite(sub, data); break;
        case 2: sio2_.ioWrite(sub, data); break;
        case 3: ctc2_.ioWrite(sub, data); break;
    }
    updateInternalChain();
}

// /CTSA aus der Plan-Logik (Kopfkommentar, Entwurf 19 §14.5): V107 ∧ (¬RTSA ∨ V106).
// Hängt am eigenen Ausgang RTSA — daher nach jedem Schreiben in SIO 1 und bei jedem
// Wechsel am Stecker neu gebildet.  `setzeCTS` meldet nur echte Wechsel (Ext/Status).
void K7028::bildeCtsA()
{
    auto& a = sio1_.channelA();
    a.setzeCTS(v107_ && (!a.rts() || v106_));
}

// ─── Interruptkette ──────────────────────────────────────────────────────────
// Reihenfolge auf der Karte aus dem Stromlaufplan nicht abgelesen [?] (§6.4).
// Im Selbsttest und im BIOS fordern nie zwei Bausteine der ATS zugleich an
// (BIOS: nur SIO 2), die Reihenfolge ist dort ohne Wirkung.

void K7028::updateInternalChain()
{
    sio1_.setIEI(iei_in_);
    sio2_.setIEI(sio1_.getIEO());
    ctc1_.setIEI(sio2_.getIEO());
    ctc2_.setIEI(ctc1_.getIEO());
}

void K7028::setIEI(bool iei)
{
    iei_in_ = iei;
    updateInternalChain();
}

bool K7028::getIEO() const { return ctc2_.getIEO(); }

bool K7028::hasInterrupt() const
{
    return sio1_.hasInterrupt() || sio2_.hasInterrupt()
        || ctc1_.hasInterrupt() || ctc2_.hasInterrupt();
}

uint8_t K7028::getVector() const
{
    // Z80SIO/Z80CTC::getVector() quittieren (IUS) — mutable im Baustein.
    auto& self = const_cast<K7028&>(*this);
    uint8_t v = 0xFF;
    if      (sio1_.hasInterrupt()) v = self.sio1_.getVector();
    else if (sio2_.hasInterrupt()) v = self.sio2_.getVector();
    else if (ctc1_.hasInterrupt()) v = self.ctc1_.getVector();
    else if (ctc2_.hasInterrupt()) v = self.ctc2_.getVector();
    self.updateInternalChain();
    return v;
}

void K7028::onRETI()
{
    // Das RETI erkennt nur der Baustein mit gesetztem IUS und freiem IEI.
    sio1_.onRETI();
    sio2_.onRETI();
    ctc1_.onRETI();
    ctc2_.onRETI();
    updateInternalChain();
}

const char* K7028::intDeviceName() const
{
    if (sio1_.hasInterrupt()) return "ATS SIO1";
    if (sio2_.hasInterrupt()) return "ATS SIO2";
    if (ctc1_.hasInterrupt()) return "ATS CTC1";
    if (ctc2_.hasInterrupt()) return "ATS CTC2";
    return "ATS K7028";
}

// ─── Lebenslauf / Takt ───────────────────────────────────────────────────────

void K7028::reset()
{
    sio1_.reset();
    sio2_.reset();
    ctc1_.reset();
    ctc2_.reset();
    latch_ = 0xFF;
    bildeCtsA();   // RTSA nach Reset aus: CTS = V107
    updateInternalChain();
}

bool K7028::clockTick(int ticks)
{
    const bool a = ctc1_.clockTick(ticks);
    const bool b = ctc2_.clockTick(ticks);
    return a || b;
}

Z80SIO::Channel& K7028::kanal(Kanal k)
{
    switch (k) {
        case Sio1A: return sio1_.channelA();
        case Sio1B: return sio1_.channelB();
        default:    return sio2_.channelA();
    }
}

void K7028::empfange(Kanal k, uint8_t byte)
{
    anschluesse_[static_cast<size_t>(k)]->einspeisen(byte);
}
