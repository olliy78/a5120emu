#include "core/peripherals/p8000_terminal/terminal_anschluss.h"

#include "core/util/zustand.h"

namespace k1520::p8000 {

uint64_t TerminalAnschluss::zeichenTakte() const {
    const serial::SerialFormat f = karte_.format();
    if (f.gueltig && f.zeichen_takte) return f.zeichen_takte;
    return 11ull * phi_ / 9600;   // 1 Start + 8 Daten + 2 Stopp
}

bool TerminalAnschluss::baudAbweichend() const {
    const serial::SerialFormat f = karte_.format();
    return f.gueltig && (f.baud_nenn != 9600 || f.daten != 8);
}

void TerminalAnschluss::takt(uint64_t takte) {
    auto ab = [takte](uint64_t& r) { r = r > takte ? r - takte : 0; };
    ab(rxRest_); ab(txRest_); ab(breakRest_);

    // Host → Terminal: Zeichen aus dem SIO-Sender im Zeichentakt übernehmen.
    // Firmware des Terminals (`p8t.init.s`, serielle Eingabe-Routine): 00H und 7FH werden
    // verworfen, von allen anderen Zeichen wird Bit 7 ausgeblendet.  WEGA-getty sendet die
    // Anmeldung mit Software-Parität in Bit 7 (8 Datenbits) — am Gerät lesbar, nur deshalb.
    if (rxRest_ == 0 && karte_.senderHatZeichen()) {
        const uint8_t b = karte_.senderNimm();
        if (b != 0x00 && b != 0x7F) term_.eingabe(uint8_t(b & 0x7F));
        rxRest_ = zeichenTakte();
    }
    // Terminal → Host: nur bei freiem Empfänger.
    if (txRest_ == 0 && term_.hatAusgabe() && karte_.empfaengerFrei()) {
        karte_.empfange(term_.holeAusgabe());
        txRest_ = zeichenTakte();
    }
    // BREAK: eine Zeichenzeit lang.
    if (!breakAktiv_ && term_.breakAnstehend()) {
        term_.breakQuittieren();
        karte_.breakEmpfang(true);
        breakAktiv_ = true;
        breakRest_ = zeichenTakte();
    } else if (breakAktiv_ && breakRest_ == 0) {
        karte_.breakEmpfang(false);
        breakAktiv_ = false;
    }
}

void TerminalAnschluss::serialize(std::vector<uint8_t>& out) const
{
    auto a = ZAr::schreiber(out);
    auto* self = const_cast<TerminalAnschluss*>(this);
    a.num(self->rxRest_); a.num(self->txRest_); a.num(self->breakRest_); a.flag(self->breakAktiv_);
}

bool TerminalAnschluss::deserialize(const uint8_t*& p, const uint8_t* end)
{
    auto a = ZAr::leser(p, end);
    uint64_t rx = 0, tx = 0, br = 0; bool ba = false;
    a.num(rx); a.num(tx); a.num(br); a.flag(ba);
    if (!a.ok) return false;
    rxRest_ = rx; txRest_ = tx; breakRest_ = br; breakAktiv_ = ba;
    p = a.p;
    return true;
}

}  // namespace k1520::p8000
