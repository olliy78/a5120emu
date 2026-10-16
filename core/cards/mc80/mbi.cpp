/**
 * @file mbi.cpp
 * @brief Magnetbandinterface MBI des MC80.2x (AP M11).
 * @see mbi.h, doc/mc80/hardware_2x.md §7, doc/mc80/bsys_2x.prn (LIN0 07A0H, WIN0 0A60H,
 *      ISY 0B81H, ILB 0BA2H, ISB 0BD1H, IPA 2102H)
 */

#include "core/cards/mc80/mbi.h"

namespace {
/// Takte, die das Laufwerk höchstens zusammengefasst bekommt (≈ 26 µs; ein Byte dauert 5,2 ms).
constexpr int kLaufwerkScheibe = 64;
/// Bittakt der MBI: 3072 Hz aus CTC 2 (07H/32H) ÷ 2 [?] (hardware_2x.md §7.3).
constexpr double kBitrate = 1536.0;
}  // namespace

Mc80Mbi::Mc80Mbi() : Mc80Mbi(Config{}) {}

Mc80Mbi::Mc80Mbi(const Config& cfg) : cfg_(cfg), lw_(cfg.profil, cfg.takt_hz)
{
    lw_.setNeueKassette(kassette::Geometrie::kompaktkassette());
    // PIO A Betriebsart 1 mit Datenblatt-RDY (bereit ab dem Steuerwort 4FH, wieder nach jedem
    // IN (FCH)): LIN0 liest vor dem ersten Byte nicht blind — sonst ginge die Präambel verloren.
    ctc_.setZCTOCallback([this](int ch, bool pegel) {
        if (!pegel) zcto(ch);
    }, 0x05);
    ctc_.setRetiNachIus(true);
    reset();
}

void Mc80Mbi::attachToBus(K1520Bus& bus)
{
    bus.registerIO(this, cfg_.reg_port, 1);
    bus.registerIO(this, cfg_.io_base, 8);
}

void Mc80Mbi::reset()
{
    ctc_.reset();
    pio_.reset();
    efh_ = 0;
    fe_ = 0x01;
    a_ = 0;
    teiler_ = false;
    bms_ = false;
    ende_in_ = -1;
    laufwerkSteuern();
    pinsB();
    updateInternalChain();
}

// ─── Laufwerk ────────────────────────────────────────────────────────────────

void Mc80Mbi::laufwerkSteuern()
{
    const bool capstan = (fe_ & 0x02) != 0;
    lw_.reservieren(efh_ != 0 || capstan);
    lw_.anwaehlen(true);
    bool vor = false, rueck = false, hge = false;
    switch (efh_) {
        case 0x13: vor = capstan; break;            // Lauf 4,76 cm/s [?]
        case 0x0A: rueck = hge = true; break;       // schneller Rücklauf [?]
        case 0x06: vor = hge = true; break;         // schneller Vorlauf [?]
        default: break;                             // 00H/11H/02H …: Band steht [?]
    }
    // Erst beide Richtungen lösen: VOR und RCK zugleich wäre im Laufwerk ein Fehlerzustand.
    lw_.vor(false);
    lw_.rueck(false);
    lw_.hge(hge);
    lw_.vor(vor);
    lw_.rueck(rueck);
    lw_.aufzeichnen(!lesen() && capstan);
    lw_.wiedergabe(lesen());
}

uint8_t Mc80Mbi::eingaengeB() const
{
    uint8_t v = 0;
    if (lw_.kassetteGeladen()) v |= 0x80;
    if (!lw_.aebAktiv()) v |= 0x40;          // 0 = Bandende/Vorspann [?]
    if (lw_.schreibsperre()) v |= 0x20;      // Riegel ausgebrochen
    // Bit 4 (Phase beim Blockanfang) = 0: acht Bit bis zum ersten Byte [?]
    return v;
}

void Mc80Mbi::pinsB()
{
    pins_b_ = eingaengeB();
    pio_.setExternB(pins_b_, 0xF0);
}

// ─── Takt ────────────────────────────────────────────────────────────────────

void Mc80Mbi::puls(int kanal)
{
    ctc_.clkTrg(kanal, true);
    ctc_.clkTrg(kanal, false);
}

void Mc80Mbi::zcto(int kanal)
{
    if (lesen()) return;                     // beim Lesen hängt das Schieberegister am Band
    if (kanal == 2) {                        // Halbzelle → Bittakt (÷2) [?]
        teiler_ = !teiler_;
        if (!teiler_) puls(0);
    } else if (kanal == 0) {                 // Bytegrenze: Schieberegister übernimmt PIO A
        if ((fe_ & 0x08) && lw_.schreibeByte(a_)) ++n_geschrieben_;
        puls(3);                             // ISB / IVL / IPA
    }
}

bool Mc80Mbi::clockTick(int ticks)
{
    bool r = ctc_.clockTick(ticks);
    lw_rest_ += ticks;
    if (lw_rest_ >= kLaufwerkScheibe) {
        lw_.clockTick(lw_rest_);
        lw_rest_ = 0;
        if (eingaengeB() != pins_b_) {
            pinsB();
            r = true;
        }
        const bool bms = lw_.bms();
        if (lesen()) {
            if (bms && !bms_) {              // Block kommt unter den Kopf: Synchronisation
                puls(3);
                ende_in_ = -1;
                r = true;
            } else if (!bms && bms_) {       // Ende: zwei Bitzeiten ohne Fluss [?]
                ende_in_ = static_cast<int64_t>(2.0 * cfg_.takt_hz / kBitrate);
            }
            while (auto b = lw_.holeByte()) {
                pio_.setExternA(*b);
                pio_.setStbA(false);
                pio_.setStbA(true);
                for (int i = 0; i < 8; ++i) puls(0);
                ++n_gelesen_;
                r = true;
            }
        } else {
            while (lw_.holeByte()) {}        // Echo beim Schreiben: ungenutzt
            ende_in_ = -1;
        }
        bms_ = bms;
    }
    if (ende_in_ >= 0) {
        ende_in_ -= ticks;
        if (ende_in_ < 0) {
            puls(3);
            r = true;
        }
    }
    if (r) updateInternalChain();
    return r;
}

// ─── E/A ─────────────────────────────────────────────────────────────────────

uint8_t Mc80Mbi::ioRead(uint8_t port)
{
    if (port == cfg_.reg_port) return 0xFF;  // nur schreibbar [HB IV-9]
    const uint8_t rel = static_cast<uint8_t>(port - cfg_.io_base) & 7;
    const uint8_t r = rel < 4 ? ctc_.ioRead(rel) : pio_.ioRead(static_cast<uint8_t>(rel - 4));
    updateInternalChain();
    return r;
}

void Mc80Mbi::ioWrite(uint8_t port, uint8_t data)
{
    if (port == cfg_.reg_port) {
        efh_ = data;
        laufwerkSteuern();
        return;
    }
    const uint8_t rel = static_cast<uint8_t>(port - cfg_.io_base) & 7;
    if (rel < 4) {
        ctc_.ioWrite(rel, data);
    } else {
        const uint8_t p = static_cast<uint8_t>(rel - 4);
        pio_.ioWrite(p, data);
        if (p == 0) {
            a_ = data;
        } else if (p == 2) {
            fe_ = data;
            laufwerkSteuern();
        } else if (p == 3) {
            pinsB();                         // Betriebsart 3: Eingänge neu anlegen
        }
    }
    updateInternalChain();
}

// ─── Interruptkette ──────────────────────────────────────────────────────────

void Mc80Mbi::updateInternalChain()
{
    ctc_.setIEI(iei_in_);
    pio_.setIEI(ctc_.getIEO());
}

void Mc80Mbi::setIEI(bool iei)
{
    iei_in_ = iei;
    updateInternalChain();
}

uint8_t Mc80Mbi::getVector() const
{
    auto& self = const_cast<Mc80Mbi&>(*this);
    uint8_t v = 0xFF;
    if (ctc_.hasInterrupt()) v = self.ctc_.getVector();
    else if (pio_.hasInterrupt()) v = self.pio_.getVector();
    self.updateInternalChain();
    return v;
}

void Mc80Mbi::onRETI()
{
    ctc_.onRETI();
    pio_.onRETI();
    updateInternalChain();
}

bool Mc80Mbi::retiAnnehmen()
{
    if (ctc_.inBedienung()) {
        ctc_.onRETI();                       // setRetiNachIus: höchster Kanal in Bedienung
    } else if (pio_.inBedienung()) {
        pio_.setIEI(true);                   // im ED-Zyklus sperrt nur, wer in Bedienung ist
        pio_.onRETI();
    } else {
        return false;
    }
    updateInternalChain();
    return true;
}

const char* Mc80Mbi::intDeviceName() const
{
    if (ctc_.hasInterrupt()) return "MC80.2x-MBI CTC";
    if (pio_.hasInterrupt()) return "MC80.2x-MBI PIO";
    return "MC80.2x-MBI";
}
